import {
  ESPRESSIF_USB_VENDOR_ID,
  parseDeviceMessage,
  USB_TYPER_URL,
  type DeviceCommand,
  type DeviceEvent,
  type DeviceResponse,
} from "@/lib/protocol";

export type VaultSession = {
  request(command: DeviceCommand, timeoutMs?: number): Promise<DeviceResponse>;
  close(): Promise<void>;
};

export class DeviceError extends Error {
  constructor(message: string) {
    super(message);
    this.name = "DeviceError";
  }
}

type Waiter = {
  req: number;
  resolve: (response: DeviceResponse) => void;
  reject: (error: Error) => void;
  timer: ReturnType<typeof setTimeout>;
};

export function serialSupported(): boolean {
  return typeof navigator !== "undefined" && "serial" in navigator;
}

export function isPortCancel(error: unknown): boolean {
  if (!error || typeof error !== "object") return false;
  const name = "name" in error ? String(error.name) : "";
  return name === "NotFoundError" || name === "AbortError";
}

export class SerialVault {
  private port: SerialPort | null = null;
  private reader: ReadableStreamDefaultReader<Uint8Array> | null = null;
  private writer: WritableStreamDefaultWriter<Uint8Array> | null = null;
  private buffer = "";
  private decoder = new TextDecoder();
  private nextReq = 0;
  private waiter: Waiter | null = null;
  private closed = false;
  private writeChain: Promise<void> = Promise.resolve();
  private usbTimer: ReturnType<typeof setInterval> | null = null;

  constructor(
    private readonly onEvent: (event: DeviceEvent) => void,
    private readonly onDisconnect: (message: string) => void,
  ) {}

  async connect(anyPort: boolean): Promise<void> {
    if (!serialSupported()) {
      throw new Error(
        "This browser cannot open a USB serial port. Use Chrome or Edge.",
      );
    }
    const port = await navigator.serial.requestPort(
      anyPort ? undefined : { filters: [{ usbVendorId: ESPRESSIF_USB_VENDOR_ID }] },
    );
    await port.open({ baudRate: 115200 });
    try {
      await port.setSignals({ dataTerminalReady: false, requestToSend: false });
    } catch {
      // Some hosts ignore line signals on USB CDC. The port is still open.
    }
    this.port = port;
    port.addEventListener("disconnect", () => {
      void this.shutdown("The device was unplugged.");
    });
    if (!port.readable || !port.writable) {
      throw new Error("The serial port did not open for reading and writing.");
    }
    this.reader = port.readable.getReader();
    this.writer = port.writable.getWriter();
    void this.readLoop();
    void this.watchTyper();
  }

  async request(command: DeviceCommand, timeoutMs = 3000): Promise<DeviceResponse> {
    if (!this.writer || this.closed) {
      throw new Error("The serial port is not open.");
    }
    if (this.waiter) {
      throw new Error("Another command is still running.");
    }
    const req = ++this.nextReq;
    return new Promise((resolve, reject) => {
      const timer = setTimeout(() => {
        if (this.waiter?.req !== req) return;
        this.waiter = null;
        reject(
          new Error(
            "The device did not answer. If it just rebooted, wait a second and try again.",
          ),
        );
      }, timeoutMs);
      this.waiter = { req, resolve, reject, timer };
      void this.writeRaw(JSON.stringify({ ...command, req })).catch((error: unknown) => {
        if (this.waiter?.req !== req) return;
        clearTimeout(timer);
        this.waiter = null;
        reject(error instanceof Error ? error : new Error("Could not write to the board."));
      });
    });
  }

  async close(): Promise<void> {
    await this.shutdown(null);
  }

  private async shutdown(message: string | null): Promise<void> {
    if (this.closed) return;
    this.closed = true;
    if (this.usbTimer) {
      clearInterval(this.usbTimer);
      this.usbTimer = null;
    }
    if (this.waiter) {
      clearTimeout(this.waiter.timer);
      const waiter = this.waiter;
      this.waiter = null;
      waiter.reject(new Error(message ?? "The serial port closed."));
    }
    try {
      await this.reader?.cancel();
    } catch {
      // The reader may already be canceled.
    }
    try {
      this.reader?.releaseLock();
    } catch {
      // Lock was already released.
    }
    try {
      await this.writer?.close();
    } catch {
      // The writer may already be closed.
    }
    try {
      await this.port?.close();
    } catch {
      // The port may already be closed.
    }
    if (message) this.onDisconnect(message);
  }

  private async readLoop(): Promise<void> {
    if (!this.reader) return;
    try {
      while (!this.closed) {
        const { value, done } = await this.reader.read();
        if (done) break;
        if (!value) continue;
        this.buffer += this.decoder.decode(value, { stream: true });
        let newline = this.buffer.indexOf("\n");
        while (newline >= 0) {
          const line = this.buffer.slice(0, newline).replace(/\r$/, "");
          this.buffer = this.buffer.slice(newline + 1);
          if (line.trim()) this.dispatch(line);
          newline = this.buffer.indexOf("\n");
        }
      }
      if (!this.closed) {
        await this.shutdown("The device closed the serial port.");
      }
    } catch {
      if (!this.closed) {
        await this.shutdown("The device closed the serial port.");
      }
    }
  }

  private writeRaw(line: string): Promise<void> {
    const text = line.endsWith("\n") ? line : `${line}\n`;
    const bytes = new TextEncoder().encode(text);
    const run = this.writeChain.then(async () => {
      if (!this.writer || this.closed) {
        throw new Error("The serial port is not open.");
      }
      await this.writer.write(bytes);
    });
    this.writeChain = run.then(
      () => undefined,
      () => undefined,
    );
    return run;
  }

  private async watchTyper(): Promise<void> {
    while (!this.closed) {
      if (await typerIsUp()) {
        try {
          await this.writeRaw('{"op":"usb_ready"}');
        } catch {
          return;
        }
        if (this.closed || this.usbTimer) return;
        this.usbTimer = setInterval(() => {
          void this.writeRaw('{"op":"usb_ready"}').catch(() => {});
        }, 1000);
        return;
      }
      await delay(1000);
    }
  }

  private async answerTypeUsb(password: string): Promise<void> {
    try {
      const response = await fetch(`${USB_TYPER_URL}/type`, {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify({ password }),
      });
      if (!response.ok) {
        const text = (await response.text()).replace(/\s+/g, " ").trim();
        const error = text.slice(0, 90) || "The USB typer could not type.";
        await this.writeRaw(JSON.stringify({ op: "type_ack", ok: false, error }));
        return;
      }
      await this.writeRaw(JSON.stringify({ op: "type_ack", ok: true }));
    } catch {
      await this.writeRaw(
        JSON.stringify({
          op: "type_ack",
          ok: false,
          error: "The USB typer is not running.",
        }),
      );
    }
  }

  private dispatch(line: string): void {
    const password = typeUsbPassword(line);
    if (password != null) {
      void this.answerTypeUsb(password);
      return;
    }
    const message = parseDeviceMessage(line);
    if (!message) return;
    if ("ok" in message) {
      if (!this.waiter || message.req !== this.waiter.req) return;
      clearTimeout(this.waiter.timer);
      const waiter = this.waiter;
      this.waiter = null;
      if (message.ok) waiter.resolve(message);
      else {
        waiter.reject(
          new DeviceError(message.error || "The device rejected the command."),
        );
      }
      return;
    }
    this.onEvent(message);
  }
}

function typeUsbPassword(line: string): string | null {
  let parsed: unknown;
  try {
    parsed = JSON.parse(line);
  } catch {
    return null;
  }
  if (!parsed || typeof parsed !== "object") return null;
  const record = parsed as Record<string, unknown>;
  if (record.event !== "type_usb" || typeof record.password !== "string") return null;
  return record.password;
}

async function typerIsUp(): Promise<boolean> {
  try {
    const response = await fetch(`${USB_TYPER_URL}/health`, { cache: "no-store" });
    return response.ok;
  } catch {
    return false;
  }
}

export function delay(ms: number): Promise<void> {
  return new Promise((resolve) => setTimeout(resolve, ms));
}
