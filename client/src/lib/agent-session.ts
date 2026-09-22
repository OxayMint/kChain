import {
  parseDeviceMessage,
  type DeviceCommand,
  type DeviceEvent,
  type DeviceResponse,
} from "@/lib/protocol";
import { DeviceError, type VaultSession } from "@/lib/serial-session";

export const USB_TYPER_URL = "http://127.0.0.1:4318";

export type UsbTyperStatus = "down" | "waiting" | "ready";

type Waiter = {
  req: number;
  resolve: (response: DeviceResponse) => void;
  reject: (error: Error) => void;
  timer: ReturnType<typeof setTimeout>;
};

export async function usbTyperStatus(): Promise<UsbTyperStatus> {
  try {
    const response = await fetch(`${USB_TYPER_URL}/health`, { cache: "no-store" });
    if (!response.ok) return "down";
    const body: unknown = await response.json();
    if (!body || typeof body !== "object") return "waiting";
    return (body as { device?: unknown }).device === true ? "ready" : "waiting";
  } catch {
    return "down";
  }
}

// Talks to the USB typer on this computer. The typer owns the serial port
// and types passwords when the button is held.
export class AgentVault implements VaultSession {
  private events: EventSource | null = null;
  private waiter: Waiter | null = null;
  private nextReq = 0;
  private closed = false;

  constructor(
    private readonly onEvent: (event: DeviceEvent) => void,
    private readonly onDisconnect: (message: string) => void,
  ) {}

  async connect(): Promise<void> {
    const status = await usbTyperStatus();
    if (status === "down") throw new Error("The USB typer is not running.");
    if (status === "waiting") {
      throw new Error("The USB typer is waiting for the board.");
    }
    await new Promise<void>((resolve, reject) => {
      const events = new EventSource(`${USB_TYPER_URL}/events`);
      this.events = events;
      let opened = false;
      events.onopen = () => {
        opened = true;
        resolve();
      };
      events.onerror = () => {
        if (!opened) {
          events.close();
          reject(new Error("Could not reach the USB typer."));
          return;
        }
        void this.shutdown("The USB typer stopped.");
      };
      events.onmessage = (message) => {
        if (message.data.trim()) this.dispatch(message.data);
      };
    });
  }

  async request(command: DeviceCommand, timeoutMs = 3000): Promise<DeviceResponse> {
    if (this.closed || !this.events) {
      throw new Error("The USB typer is not connected.");
    }
    if (this.waiter) throw new Error("Another command is still running.");
    const req = ++this.nextReq;
    const response = await fetch(`${USB_TYPER_URL}/line`, {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({ ...command, req }),
    });
    if (response.status === 503) {
      throw new Error("The USB typer is waiting for the board.");
    }
    if (!response.ok) {
      throw new Error("The USB typer did not accept the command.");
    }
    return new Promise((resolve, reject) => {
      const timer = setTimeout(() => {
        if (this.waiter?.req !== req) return;
        this.waiter = null;
        reject(new Error("The device did not answer."));
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
    this.events?.close();
    this.events = null;
    if (this.waiter) {
      clearTimeout(this.waiter.timer);
      const waiter = this.waiter;
      this.waiter = null;
      waiter.reject(new Error(message ?? "The USB typer stopped."));
    }
    if (message) this.onDisconnect(message);
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
