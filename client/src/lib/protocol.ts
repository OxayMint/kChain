export const LIMITS = {
  nameMaxBytes: 48,
  passwordMax: 128,
  entryMax: 32,
} as const;

export const ESPRESSIF_USB_VENDOR_ID = 0x303a;

export type VaultEntry = {
  id: number;
  name: string;
  password: string;
};

export type VaultSnapshot = {
  entries: VaultEntry[];
  selectedId: number | null;
  keyboardConnected: boolean;
};

export type DeviceCommand =
  | { op: "list" }
  | { op: "add"; name: string; password: string }
  | { op: "edit"; id: number; name: string; password: string }
  | { op: "delete"; id: number };

export type DeviceResponse = {
  op: string;
  ok: boolean;
  req?: number;
  error?: string;
  id?: number;
  entries?: unknown;
  selectedId?: unknown;
  keyboardConnected?: unknown;
};

export type DeviceEvent =
  | {
      event: "ready";
      version?: number;
      keyboardConnected?: boolean;
      selectedId?: number | null;
      error?: string;
    }
  | { event: "selected"; id: number | null; name: string | null; index: number }
  | { event: "typed"; id: number; name: string }
  | { event: "keyboard"; connected: boolean }
  | { event: "type_failed"; error: string };

export function validateName(name: string): string | null {
  if (name.length === 0) return "Name is required.";
  const bytes = new TextEncoder().encode(name).length;
  if (bytes > LIMITS.nameMaxBytes) return "Name must be 48 bytes or fewer.";
  for (let i = 0; i < name.length; i++) {
    const code = name.charCodeAt(i);
    if (code < 0x20 || code === 0x7f) {
      return "Name cannot include control characters.";
    }
  }
  return null;
}

export function validatePassword(password: string): string | null {
  if (password.length === 0) return "Password is required.";
  if (password.length > LIMITS.passwordMax) {
    return "Password must be 128 characters or fewer.";
  }
  for (let i = 0; i < password.length; i++) {
    const code = password.charCodeAt(i);
    if (code < 0x20 || code > 0x7e) {
      return "Password must be printable ASCII so the device can type it.";
    }
  }
  return null;
}

export function snapshotFrom(response: DeviceResponse): VaultSnapshot {
  if (!Array.isArray(response.entries)) {
    throw new Error("The device sent an unexpected vault.");
  }
  const entries: VaultEntry[] = response.entries.map((value) => {
    if (!value || typeof value !== "object") {
      throw new Error("The device sent an unexpected entry.");
    }
    const entry = value as Record<string, unknown>;
    if (
      typeof entry.id !== "number" ||
      typeof entry.name !== "string" ||
      typeof entry.password !== "string"
    ) {
      throw new Error("The device sent an unexpected entry.");
    }
    return { id: entry.id, name: entry.name, password: entry.password };
  });
  const selectedId =
    typeof response.selectedId === "number" ? response.selectedId : null;
  return {
    entries,
    selectedId,
    keyboardConnected: response.keyboardConnected === true,
  };
}

export function parseDeviceMessage(
  line: string,
): DeviceResponse | DeviceEvent | null {
  let parsed: unknown;
  try {
    parsed = JSON.parse(line);
  } catch {
    return null;
  }
  if (!parsed || typeof parsed !== "object") return null;
  const record = parsed as Record<string, unknown>;
  if (typeof record.ok === "boolean" && typeof record.op === "string") {
    return record as DeviceResponse;
  }
  if (typeof record.event !== "string") return null;
  if (record.event === "ready") {
    return {
      event: "ready",
      version: typeof record.version === "number" ? record.version : undefined,
      keyboardConnected:
        typeof record.keyboardConnected === "boolean"
          ? record.keyboardConnected
          : undefined,
      selectedId:
        typeof record.selectedId === "number" ? record.selectedId : null,
      error: typeof record.error === "string" ? record.error : undefined,
    };
  }
  if (record.event === "selected") {
    return {
      event: "selected",
      id: typeof record.id === "number" ? record.id : null,
      name: typeof record.name === "string" ? record.name : null,
      index: typeof record.index === "number" ? record.index : -1,
    };
  }
  if (record.event === "typed" && typeof record.id === "number") {
    return {
      event: "typed",
      id: record.id,
      name: typeof record.name === "string" ? record.name : "entry",
    };
  }
  if (record.event === "keyboard" && typeof record.connected === "boolean") {
    return { event: "keyboard", connected: record.connected };
  }
  if (record.event === "type_failed") {
    return {
      event: "type_failed",
      error:
        typeof record.error === "string" ? record.error : "Could not type.",
    };
  }
  return null;
}
