#!/usr/bin/env node
import { spawn } from "node:child_process";
import { dirname, join } from "node:path";
import { fileURLToPath } from "node:url";

const here = dirname(fileURLToPath(import.meta.url));
const hostDir = join(here, "..", "..", "host");
const script = join(hostDir, "kchain-usb.mjs");
const healthUrl = "http://127.0.0.1:4318/health";

function send(message) {
  const json = Buffer.from(JSON.stringify(message));
  const header = Buffer.alloc(4);
  header.writeUInt32LE(json.length, 0);
  process.stdout.write(Buffer.concat([header, json]));
}

async function typerIsUp() {
  try {
    const response = await fetch(healthUrl, { cache: "no-store" });
    return response.ok;
  } catch {
    return false;
  }
}

async function ensure() {
  if (await typerIsUp()) return true;
  const child = spawn(process.execPath, [script], {
    cwd: hostDir,
    detached: true,
    stdio: "ignore",
  });
  child.unref();
  for (let attempt = 0; attempt < 20; attempt++) {
    if (await typerIsUp()) return true;
    await new Promise((resolve) => setTimeout(resolve, 250));
  }
  return false;
}

let buffer = Buffer.alloc(0);

async function handle(message) {
  if (!message || message.op !== "ensure") {
    send({ ok: false });
    return;
  }
  try {
    send({ ok: await ensure() });
  } catch {
    send({ ok: false });
  }
}

process.stdin.on("data", (chunk) => {
  buffer = Buffer.concat([buffer, chunk]);
  while (buffer.length >= 4) {
    const length = buffer.readUInt32LE(0);
    if (length <= 0 || length > 1024 * 1024) process.exit(1);
    if (buffer.length < 4 + length) return;
    const body = buffer.subarray(4, 4 + length).toString("utf8");
    buffer = buffer.subarray(4 + length);
    let message = null;
    try {
      message = JSON.parse(body);
    } catch {
      send({ ok: false });
      continue;
    }
    void handle(message);
  }
});

process.stdin.on("end", () => process.exit(0));
