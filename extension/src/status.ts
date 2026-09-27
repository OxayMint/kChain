import type { VaultSnapshot } from "@shared/protocol";
import type { StatusResponse } from "./messages";

export function deviceLine(status: StatusResponse): string {
  if (status.message) return status.message;
  const snapshot = status.snapshot;
  if (!snapshot) return "Not connected";
  return selectionLine(snapshot);
}

export function selectionLine(snapshot: VaultSnapshot): string {
  if (!snapshot.active || snapshot.selectedId == null) return "Device is idle";
  const index = snapshot.entries.findIndex((entry) => entry.id === snapshot.selectedId);
  if (index < 0) return "Device is idle";
  return `On the device: ${index + 1}. ${snapshot.entries[index].name}`;
}

export function typingLine(snapshot: VaultSnapshot | null): string | null {
  if (!snapshot) return null;
  return snapshot.usbTyping ? "Types on this computer" : "Waiting for the USB typer";
}

export function pageHost(url: string | undefined): string | null {
  if (!url) return null;
  try {
    const parsed = new URL(url);
    if (parsed.protocol !== "http:" && parsed.protocol !== "https:") return null;
    return parsed.hostname;
  } catch {
    return null;
  }
}
