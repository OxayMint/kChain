export const LIMITS = {
  nameMaxBytes: 48,
  hostnameMax: 253,
  usernameMax: 128,
  passwordMax: 128,
  phraseMax: 256,
  entryMax: 32,
} as const;

export const ESPRESSIF_USB_VENDOR_ID = 0x303a;

export const USB_TYPER_URL = "http://127.0.0.1:4318";

export type EntryType = "generic" | "website" | "crypto";

export type VaultEntry = {
  id: number;
  type: EntryType;
  name: string;
  username: string;
  password: string;
  phrase: string;
};

export type VaultSnapshot = {
  entries: VaultEntry[];
  selectedId: number | null;
  active: boolean;
  keyboardConnected: boolean;
  usbTyping: boolean;
};

export type DeviceCommand =
  | { op: "list" }
  | { op: "add"; type: "generic"; name: string; password: string }
  | {
      op: "add";
      type: "website";
      name: string;
      username: string;
      password: string;
    }
  | { op: "add"; type: "crypto"; name: string; phrase: string }
  | { op: "edit"; id: number; type: "generic"; name: string; password: string }
  | {
      op: "edit";
      id: number;
      type: "website";
      name: string;
      username: string;
      password: string;
    }
  | { op: "edit"; id: number; type: "crypto"; name: string; phrase: string }
  | { op: "delete"; id: number }
  | { op: "focus"; id: number | null };

export type DeviceResponse = {
  op: string;
  ok: boolean;
  req?: number;
  error?: string;
  id?: number;
  entries?: unknown;
  selectedId?: unknown;
  active?: unknown;
  keyboardConnected?: unknown;
  usbTyping?: unknown;
};

export type DeviceEvent =
  | {
      event: "ready";
      version?: number;
      keyboardConnected?: boolean;
      usbTyping?: boolean;
      active?: boolean;
      selectedId?: number | null;
      error?: string;
    }
  | { event: "selected"; id: number | null; name: string | null; index: number }
  | { event: "idle" }
  | { event: "typed"; id: number; name: string }
  | { event: "keyboard"; connected: boolean }
  | { event: "usb"; ready: boolean }
  | { event: "type_failed"; error: string };

function printableAscii(value: string): boolean {
  for (let i = 0; i < value.length; i++) {
    const code = value.charCodeAt(i);
    if (code < 0x20 || code > 0x7e) return false;
  }
  return true;
}

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

export function validateHostname(hostname: string): string | null {
  if (hostname.length === 0) return "Hostname is required.";
  if (hostname.length > LIMITS.hostnameMax) {
    return "Hostname must be 253 characters or fewer.";
  }
  if (!printableAscii(hostname)) return "Hostname must be printable ASCII.";
  return null;
}

export function validateUsername(username: string): string | null {
  if (username.length === 0) return "Username is required.";
  if (username.length > LIMITS.usernameMax) {
    return "Username must be 128 characters or fewer.";
  }
  if (!printableAscii(username)) {
    return "Username must be printable ASCII so the device can type it.";
  }
  return null;
}

export function validatePassword(password: string): string | null {
  if (password.length === 0) return "Password is required.";
  if (password.length > LIMITS.passwordMax) {
    return "Password must be 128 characters or fewer.";
  }
  if (!printableAscii(password)) {
    return "Password must be printable ASCII so the device can type it.";
  }
  return null;
}

export function validatePhrase(phrase: string): string | null {
  if (phrase.length === 0) return "Phrase is required.";
  if (phrase.length > LIMITS.phraseMax) {
    return "Phrase must be 256 characters or fewer.";
  }
  if (!printableAscii(phrase)) return "Phrase must be printable ASCII.";
  if (phrase.startsWith(" ") || phrase.endsWith(" ")) {
    return "Phrase cannot start or end with a space.";
  }
  const words = phrase.split(" ");
  if (words.some((word) => word.length === 0)) {
    return "Phrase words must be separated by a single space.";
  }
  if (words.length < 2) return "Phrase must be at least two words.";
  return null;
}

export function validateLabel(label: string): string | null {
  const error = validateName(label);
  if (error === "Name is required.") return "Label is required.";
  if (error === "Name must be 48 bytes or fewer.") {
    return "Label must be 48 bytes or fewer.";
  }
  if (error === "Name cannot include control characters.") {
    return "Label cannot include control characters.";
  }
  return error;
}

function textField(record: Record<string, unknown>, key: string): string | null {
  if (!(key in record) || record[key] == null) return "";
  if (typeof record[key] !== "string") return null;
  return record[key];
}

function entryFrom(value: unknown): VaultEntry {
  if (!value || typeof value !== "object") {
    throw new Error("The device sent an unexpected entry.");
  }
  const entry = value as Record<string, unknown>;
  if (typeof entry.id !== "number") {
    throw new Error("The device sent an unexpected entry.");
  }
  const typeValue = entry.type == null ? "generic" : entry.type;
  if (typeValue !== "generic" && typeValue !== "website" && typeValue !== "crypto") {
    throw new Error("The device sent an unexpected entry.");
  }
  const name = textField(entry, "name");
  const username = textField(entry, "username");
  const password = textField(entry, "password");
  const phrase = textField(entry, "phrase");
  if (name == null || username == null || password == null || phrase == null) {
    throw new Error("The device sent an unexpected entry.");
  }
  if (name.length === 0) throw new Error("The device sent an unexpected entry.");
  if (typeValue === "website" && (username.length === 0 || password.length === 0)) {
    throw new Error("The device sent an unexpected entry.");
  }
  if (typeValue === "crypto" && phrase.length === 0) {
    throw new Error("The device sent an unexpected entry.");
  }
  if (typeValue === "generic" && password.length === 0) {
    throw new Error("The device sent an unexpected entry.");
  }
  return {
    id: entry.id,
    type: typeValue,
    name,
    username,
    password,
    phrase,
  };
}

export function snapshotFrom(response: DeviceResponse): VaultSnapshot {
  if (!Array.isArray(response.entries)) {
    throw new Error("The device sent an unexpected vault.");
  }
  const entries = response.entries.map(entryFrom);
  const selectedId =
    typeof response.selectedId === "number" ? response.selectedId : null;
  return {
    entries,
    selectedId,
    active: response.active === true,
    keyboardConnected: response.keyboardConnected === true,
    usbTyping: response.usbTyping === true,
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
      usbTyping:
        typeof record.usbTyping === "boolean" ? record.usbTyping : undefined,
      active: record.active === true,
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
  if (record.event === "idle") return { event: "idle" };
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
  if (record.event === "usb" && typeof record.ready === "boolean") {
    return { event: "usb", ready: record.ready };
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
