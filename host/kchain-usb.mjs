import { spawn } from "node:child_process";
import { access, constants, stat } from "node:fs/promises";
import http from "node:http";
import { dirname, join } from "node:path";
import { fileURLToPath } from "node:url";
import { SerialPort } from "serialport";
import { ReadlineParser } from "@serialport/parser-readline";

const root = dirname(fileURLToPath(import.meta.url));
const swiftSource = join(root, "type-keys.swift");
const swiftBinary = join(root, "kchain-type");
const portNumber = 4318;

let deviceOpen = false;
let stopping = false;
let writeChain = Promise.resolve();
let activePort = null;
const subscribers = new Set();

function log(message) {
  console.log(message);
}

function originAllowed(origin) {
  if (!origin) return true;
  let url;
  try {
    url = new URL(origin);
  } catch {
    return false;
  }
  const host = url.hostname.replace(/^\[|\]$/g, "");
  if (host === "localhost" || host === "127.0.0.1" || host === "::1") return true;
  const parts = host.split(".").map((part) => Number(part));
  if (parts.length !== 4 || parts.some((part) => !Number.isInteger(part))) return false;
  if (parts[0] === 10) return true;
  if (parts[0] === 192 && parts[1] === 168) return true;
  if (parts[0] === 172 && parts[1] >= 16 && parts[1] <= 31) return true;
  return false;
}

function applyCors(req, res) {
  const origin = req.headers.origin;
  if (origin && originAllowed(origin)) {
    res.setHeader("Access-Control-Allow-Origin", origin);
    res.setHeader("Vary", "Origin");
    res.setHeader("Access-Control-Allow-Private-Network", "true");
  }
  res.setHeader("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
  res.setHeader("Access-Control-Allow-Headers", "Content-Type");
}

function forward(line) {
  const payload = `data: ${line}\n\n`;
  for (const response of subscribers) response.write(payload);
}

function writeLine(line) {
  const port = activePort;
  if (!port || !port.isOpen) return Promise.reject(new Error("closed"));
  const text = line.endsWith("\n") ? line : `${line}\n`;
  const run = writeChain.then(
    () =>
      new Promise((resolve, reject) => {
        port.write(text, (error) => (error ? reject(error) : resolve()));
      }),
  );
  writeChain = run.catch(() => {});
  return run;
}

function shorten(text) {
  const oneLine = text.replace(/\s+/g, " ").trim();
  if (!oneLine) return "The USB typer could not type.";
  return oneLine.length > 90 ? oneLine.slice(0, 90) : oneLine;
}

function runProcess(command, args, input) {
  return new Promise((resolve, reject) => {
    const child = spawn(command, args, { stdio: ["pipe", "pipe", "pipe"] });
    let stderr = "";
    child.stderr.on("data", (chunk) => {
      stderr += chunk.toString();
    });
    child.on("error", reject);
    child.on("close", (code) => {
      if (code === 0) resolve();
      else reject(new Error(shorten(stderr || `${command} exited ${code}`)));
    });
    if (input != null) child.stdin.end(input);
    else child.stdin.end();
  });
}

async function binaryIsCurrent() {
  try {
    await access(swiftBinary, constants.X_OK);
    const [binary, source] = await Promise.all([stat(swiftBinary), stat(swiftSource)]);
    return binary.mtimeMs >= source.mtimeMs;
  } catch {
    return false;
  }
}

async function ensureTyper() {
  if (process.platform !== "darwin") return;
  if (await binaryIsCurrent()) return;
  log("Compiling the keystroke helper.");
  await runProcess("swiftc", ["-O", "-o", swiftBinary, swiftSource], null);
}

async function typeSteps(steps) {
  if (process.platform === "darwin") {
    await ensureTyper();
    await runProcess(swiftBinary, [], JSON.stringify(steps));
    return;
  }
  if (process.platform === "linux") {
    await typeLinux(steps);
    return;
  }
  if (process.platform === "win32") {
    await typeWindows(steps);
    return;
  }
  throw new Error("This operating system cannot type from the USB typer.");
}

async function typeLinux(steps) {
  const wayland = Boolean(process.env.WAYLAND_DISPLAY);
  const command = wayland ? "wtype" : "xdotool";
  for (const step of steps) {
    if (step.length === 0) continue;
    const args =
      step === "\t"
        ? wayland
          ? ["-k", "tab"]
          : ["key", "Tab"]
        : wayland
          ? [step]
          : ["type", "--delay", "10", "--", step];
    await runProcess(command, args, null);
  }
}

function sendKeysSteps(steps) {
  return steps
    .map((step) => (step === "\t" ? "{TAB}" : step.replace(/([+^%~(){}\[\]])/g, "{$1}")))
    .join("");
}

function typeWindows(steps) {
  const script = `
$raw = [Console]::In.ReadToEnd()
Add-Type -AssemblyName System.Windows.Forms
[System.Windows.Forms.SendKeys]::SendWait($raw)
`;
  return runProcess(
    "powershell.exe",
    ["-NoProfile", "-NonInteractive", "-Command", script],
    sendKeysSteps(steps),
  );
}

function stepsFrom(parsed) {
  if (parsed && Array.isArray(parsed.steps)) {
    if (parsed.steps.length === 0 || parsed.steps.length > 8) {
      return { error: "Missing text to type." };
    }
    const steps = [];
    for (const step of parsed.steps) {
      if (typeof step !== "string") return { error: "Missing text to type." };
      if (step !== "\t" && step.length > 256) return { error: "Text is too long." };
      steps.push(step);
    }
    if (steps.every((step) => step.length === 0)) return { error: "Missing text to type." };
    return { steps };
  }
  if (parsed && typeof parsed.password === "string" && parsed.password.length > 0) {
    if (parsed.password.length > 128) return { error: "Password is too long." };
    return { steps: [parsed.password] };
  }
  return { error: "Missing password." };
}

async function onDeviceLine(line) {
  const trimmed = line.replace(/\r$/, "");
  if (!trimmed) return;
  let message = null;
  try {
    message = JSON.parse(trimmed);
  } catch {
    forward(trimmed);
    return;
  }
  if (message && message.event === "type_usb") {
    const name = typeof message.name === "string" ? message.name : "entry";
    const typing = stepsFrom(message);
    if (typing.error) {
      log(typing.error);
      try {
        await writeLine(JSON.stringify({ op: "type_ack", ok: false, error: typing.error }));
      } catch {
        // The cable dropped while reporting the failure.
      }
      return;
    }
    log(`Typing ${name}.`);
    try {
      await typeSteps(typing.steps);
      await writeLine(JSON.stringify({ op: "type_ack", ok: true }));
    } catch (error) {
      const text = error instanceof Error ? error.message : "Could not type.";
      log(text);
      try {
        await writeLine(JSON.stringify({ op: "type_ack", ok: false, error: shorten(text) }));
      } catch {
        // The cable dropped while reporting the failure.
      }
    }
    return;
  }
  forward(trimmed);
}

function portIsBusy(text) {
  return /busy|EBUSY|already open/i.test(text);
}

function hostPath(path) {
  // The macOS tty node can sit open and deliver no bytes. The callout node is the one that talks.
  if (process.platform === "darwin") return path.replace("/dev/tty.", "/dev/cu.");
  return path;
}

async function findPort() {
  if (process.env.KCHAIN_PORT) return hostPath(process.env.KCHAIN_PORT);
  const ports = await SerialPort.list();
  const match = ports.find((port) => (port.vendorId || "").toLowerCase() === "303a");
  return match ? hostPath(match.path) : null;
}

function openPort(path) {
  return new Promise((resolve, reject) => {
    const port = new SerialPort({
      path,
      baudRate: 115200,
      dataBits: 8,
      stopBits: 1,
      parity: "none",
      hupcl: false,
      autoOpen: false,
    });
    port.open((error) => (error ? reject(error) : resolve(port)));
  });
}

function holdPort(port) {
  return new Promise((resolve) => {
    const done = () => resolve();
    port.once("close", done);
    port.once("error", done);
  });
}

async function session(path) {
  const port = await openPort(path);
  log(`Opened ${path}.`);
  activePort = port;
  deviceOpen = true;
  writeChain = Promise.resolve();
  try {
    await new Promise((resolve) => {
      port.set({ dtr: false, rts: false }, () => resolve());
    });
  } catch {
    // Some USB serial adapters reject line-state changes. The port is still open.
  }
  const parser = port.pipe(new ReadlineParser({ delimiter: "\n" }));
  parser.on("data", (line) => {
    void onDeviceLine(String(line));
  });
  const heartbeat = setInterval(() => {
    void writeLine(JSON.stringify({ op: "usb_ready" })).catch(() => {});
  }, 1000);
  heartbeat.unref();
  await writeLine(JSON.stringify({ op: "usb_ready" }));
  await holdPort(port);
  clearInterval(heartbeat);
  deviceOpen = false;
  activePort = null;
  log("Serial port closed. Waiting for the board.");
}

function readBody(req) {
  return new Promise((resolve, reject) => {
    let body = "";
    req.on("data", (chunk) => {
      body += chunk;
      if (body.length > 4096) {
        reject(new Error("too long"));
        req.destroy();
      }
    });
    req.on("end", () => resolve(body));
    req.on("error", reject);
  });
}

function startServer() {
  const server = http.createServer((req, res) => {
    const origin = req.headers.origin;
    if (origin && !originAllowed(origin)) {
      res.writeHead(403);
      res.end();
      return;
    }
    applyCors(req, res);
    if (req.method === "OPTIONS") {
      res.writeHead(204);
      res.end();
      return;
    }
    const url = new URL(req.url || "/", "http://127.0.0.1");
    if (req.method === "GET" && url.pathname === "/health") {
      res.writeHead(200, { "Content-Type": "application/json", "Cache-Control": "no-store" });
      res.end(JSON.stringify({ ok: true, device: deviceOpen, typing: true }));
      return;
    }
    if (req.method === "POST" && url.pathname === "/type") {
      void (async () => {
        try {
          const body = (await readBody(req)).trim();
          const parsed = JSON.parse(body);
          const typing = stepsFrom(parsed);
          if (typing.error) {
            res.writeHead(400);
            res.end(typing.error);
            return;
          }
          log("Typing from the editor.");
          await typeSteps(typing.steps);
          res.writeHead(204);
          res.end();
        } catch (error) {
          if (!res.headersSent) {
            const text = error instanceof Error ? shorten(error.message) : "Could not type.";
            log(text);
            res.writeHead(500, { "Content-Type": "text/plain; charset=utf-8" });
            res.end(text);
          }
        }
      })();
      return;
    }
    if (req.method === "GET" && url.pathname === "/events") {
      res.writeHead(200, {
        "Content-Type": "text/event-stream",
        "Cache-Control": "no-cache",
        Connection: "keep-alive",
      });
      res.write("\n");
      subscribers.add(res);
      req.on("close", () => subscribers.delete(res));
      return;
    }
    if (req.method === "POST" && url.pathname === "/line") {
      void (async () => {
        if (!deviceOpen) {
          res.writeHead(503);
          res.end("The board is not open.");
          return;
        }
        try {
          const body = (await readBody(req)).trim();
          if (!body) {
            res.writeHead(400);
            res.end("Missing command.");
            return;
          }
          await writeLine(body);
          res.writeHead(204);
          res.end();
        } catch {
          if (!res.headersSent) {
            res.writeHead(500);
            res.end("Could not write to the board.");
          }
        }
      })();
      return;
    }
    res.writeHead(404);
    res.end();
  });
  server.listen(portNumber, "127.0.0.1", () => {
    log(`USB typer listening on 127.0.0.1:${portNumber}.`);
  });
  server.on("error", (error) => {
    if (error && error.code === "EADDRINUSE") {
      console.error("The USB typer is already running.");
    } else {
      console.error(error instanceof Error ? error.message : "Could not listen.");
    }
    process.exit(1);
  });
  return server;
}

async function main() {
  if (process.platform === "darwin") {
    try {
      await ensureTyper();
    } catch (error) {
      log(error instanceof Error ? error.message : "Could not compile the keystroke helper.");
    }
    try {
      await runProcess(swiftBinary, ["--check"], null);
    } catch {
      log("macOS will ask for Accessibility access the first time a password is typed.");
    }
  }
  const server = startServer();
  let announcedWait = false;
  let announcedBusy = false;
  while (!stopping) {
    try {
      const path = await findPort();
      if (!path) {
        if (!announcedWait) {
          log("Waiting for an Espressif USB serial port.");
          announcedWait = true;
        }
        await new Promise((resolve) => setTimeout(resolve, 1000));
        continue;
      }
      announcedWait = false;
      await session(path);
    } catch (error) {
      deviceOpen = false;
      activePort = null;
      const text = error instanceof Error ? error.message : "Serial port failed.";
      if (portIsBusy(text)) {
        if (!announcedBusy) {
          log("The editor has the serial port. A double tap will type through this program.");
          announcedBusy = true;
          announcedWait = false;
        }
      } else {
        announcedBusy = false;
        log(text);
      }
      await new Promise((resolve) => setTimeout(resolve, 1000));
    }
  }
  server.close();
}

process.on("SIGINT", () => {
  stopping = true;
  if (activePort && activePort.isOpen) activePort.close();
  setTimeout(() => process.exit(0), 200).unref();
});

main().catch((error) => {
  console.error(error instanceof Error ? error.message : "The USB typer stopped.");
  process.exit(1);
});
