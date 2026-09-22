import {
  ESPRESSIF_USB_VENDOR_ID,
  parseDeviceMessage,
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
  }

  async request(command: DeviceCommand, timeoutMs = 3000): Promise<DeviceResponse> {
    if (!this.writer || this.closed) {
      throw new Error("The serial port is not open.");
    }
    if (this.waiter) {
      throw new Error("Another command is still running.");
    }
    const req = ++this.nextReq;
    const payload = JSON.stringify({ ...command, req }) + "\n";
    await this.writer.write(new TextEncoder().encode(payload));
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
    });
  }

  async close(): Promise<void> {
    await this.shutdown(null);
  }

  private async shutdown(message: string | null): Promise<void> {
    if (this.closed) return;
    this.closed = true;
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

  private dispatch(line: string): void {
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

export function delay(ms: number): Promise<void> {
  return new Promise((resolve) => setTimeout(resolve, ms));
}
