import { spawn } from "node:child_process";
import { access } from "node:fs/promises";
import path from "node:path";

export const runtime = "nodejs";

const healthUrl = "http://127.0.0.1:4318/health";

function fromThisComputer(request: Request): boolean {
  const host = (request.headers.get("host") ?? "").replace(/:\d+$/, "");
  const name = host.replace(/^\[|\]$/g, "");
  return name === "localhost" || name === "127.0.0.1" || name === "::1";
}

async function typerIsUp(): Promise<boolean> {
  try {
    const response = await fetch(healthUrl, { cache: "no-store" });
    return response.ok;
  } catch {
    return false;
  }
}

let starting: Promise<boolean> | null = null;

async function startTyper(): Promise<boolean> {
  if (await typerIsUp()) return true;
  const hostDir = path.resolve(process.cwd(), "..", "host");
  const script = path.join(hostDir, "kchain-usb.mjs");
  await access(script);
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

function ensureTyper(): Promise<boolean> {
  if (!starting) {
    starting = startTyper().finally(() => {
      starting = null;
    });
  }
  return starting;
}

export async function POST(request: Request) {
  if (!fromThisComputer(request)) {
    return Response.json({ ok: false }, { status: 403 });
  }
  try {
    const ok = await ensureTyper();
    return Response.json({ ok });
  } catch {
    return Response.json({ ok: false }, { status: 500 });
  }
}
