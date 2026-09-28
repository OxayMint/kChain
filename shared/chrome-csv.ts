import { hostMatches } from "./hosts.ts";
import {
  validateHostname,
  validatePassword,
  validateUsername,
  type VaultEntry,
} from "./protocol.ts";

export const CHROME_PASSWORD_SETTINGS = "chrome://password-manager/settings";

const SKIP_INCOMPLETE = "Missing a URL, username, or password.";
const SKIP_NOT_WEBSITE = "Not a website login.";

export type ChromeLogin = {
  hostname: string;
  username: string;
  password: string;
};

export type ChromeSkip = {
  reason: string;
  count: number;
};

export type ChromeCsvResult = {
  logins: ChromeLogin[];
  skipped: ChromeSkip[];
  repeated: number;
  notes: number;
};

export type ChromeImportAction =
  | { kind: "add"; hostname: string; username: string; password: string }
  | { kind: "update"; id: number; hostname: string; username: string; password: string };

export function parseChromePasswordCsv(
  text: string,
): { ok: true; result: ChromeCsvResult } | { ok: false; message: string } {
  const table = parseCsv(text.replace(/^\uFEFF/, ""));
  if (table == null) return { ok: false, message: "This CSV is missing a closing quote." };
  const records = table.filter((row) => row.some((cell) => cell.trim().length > 0));
  if (records.length === 0) return { ok: false, message: "The file has no passwords." };

  const columns = columnsOf(records);
  if (columns == null) {
    return {
      ok: false,
      message: "Choose a Chrome password CSV. It needs url, username, and password columns.",
    };
  }

  const skipCounts = new Map<string, number>();
  const accepted = new Map<string, ChromeLogin & { hadNote: boolean }>();
  let repeated = 0;

  for (const row of columns.rows) {
    const url = cell(row, columns.url);
    const username = cell(row, columns.username).trim();
    const password = cell(row, columns.password);
    const note = cell(row, columns.note).trim();
    if (url.trim().length === 0 && username.length === 0 && password.length === 0 && note.length === 0) {
      continue;
    }
    if (url.trim().length === 0 || username.length === 0 || password.length === 0) {
      bump(skipCounts, SKIP_INCOMPLETE);
      continue;
    }
    const hostname = websiteHostname(url);
    if (hostname == null) {
      bump(skipCounts, SKIP_NOT_WEBSITE);
      continue;
    }
    const error =
      validateHostname(hostname) ?? validateUsername(username) ?? validatePassword(password);
    if (error) {
      bump(skipCounts, error);
      continue;
    }
    const key = `${hostname}\n${username.toLowerCase()}`;
    if (accepted.has(key)) repeated += 1;
    accepted.set(key, { hostname, username, password, hadNote: note.length > 0 });
  }

  const logins = [...accepted.values()].map(({ hostname, username, password }) => ({
    hostname,
    username,
    password,
  }));
  const notes = [...accepted.values()].filter((login) => login.hadNote).length;
  if (logins.length === 0 && skipCounts.size === 0) {
    return { ok: false, message: "The file has no passwords." };
  }
  return {
    ok: true,
    result: {
      logins,
      skipped: [...skipCounts.entries()].map(([reason, count]) => ({ reason, count })),
      repeated,
      notes,
    },
  };
}

export function planChromeImport(
  logins: ChromeLogin[],
  entries: VaultEntry[],
): { actions: ChromeImportAction[]; alreadySaved: number } {
  const slots = entries
    .filter((entry) => entry.type === "website")
    .map((entry) => ({
      id: entry.id,
      hostname: entry.name,
      username: entry.username,
      password: entry.password,
    }));
  let tempId = -1;
  const actions: ChromeImportAction[] = [];
  let alreadySaved = 0;

  for (const login of logins) {
    const index = findSlot(slots, login.hostname, login.username);
    if (index === -1) {
      slots.push({
        id: tempId,
        hostname: login.hostname,
        username: login.username,
        password: login.password,
      });
      tempId -= 1;
      actions.push({
        kind: "add",
        hostname: login.hostname,
        username: login.username,
        password: login.password,
      });
      continue;
    }
    const slot = slots[index];
    if (slot.password === login.password) {
      if (slot.id >= 0) alreadySaved += 1;
      continue;
    }
    slot.password = login.password;
    if (slot.id < 0) {
      const pending = actions.find(
        (action) =>
          action.kind === "add" &&
          exactHost(action.hostname, slot.hostname) &&
          action.username.toLowerCase() === slot.username.toLowerCase(),
      );
      if (pending && pending.kind === "add") pending.password = login.password;
      continue;
    }
    const pending = actions.find((action) => action.kind === "update" && action.id === slot.id);
    if (pending && pending.kind === "update") {
      pending.password = login.password;
      continue;
    }
    actions.push({
      kind: "update",
      id: slot.id,
      hostname: slot.hostname,
      username: slot.username,
      password: login.password,
    });
  }

  return { actions, alreadySaved };
}

export function importActionKey(action: ChromeImportAction): string {
  if (action.kind === "update") return `update:${action.id}`;
  return `add:${action.hostname}\n${action.username.toLowerCase()}`;
}

function findSlot(
  slots: Array<{ hostname: string; username: string }>,
  hostname: string,
  username: string,
): number {
  const user = username.toLowerCase();
  let fuzzy = -1;
  for (let index = 0; index < slots.length; index++) {
    const slot = slots[index];
    if (slot.username.toLowerCase() !== user) continue;
    if (!hostMatches(slot.hostname, hostname)) continue;
    if (exactHost(slot.hostname, hostname)) return index;
    if (fuzzy === -1) fuzzy = index;
  }
  return fuzzy;
}

function exactHost(left: string, right: string): boolean {
  return normalizeHost(left) === normalizeHost(right);
}

function normalizeHost(host: string): string {
  return host.trim().toLowerCase().replace(/\.$/, "").replace(/^\[|\]$/g, "");
}

function websiteHostname(value: string): string | null {
  const trimmed = value.trim();
  if (trimmed.length === 0) return null;
  let url: URL;
  try {
    url = new URL(trimmed.includes("://") ? trimmed : `https://${trimmed}`);
  } catch {
    return null;
  }
  if (url.protocol !== "http:" && url.protocol !== "https:") return null;
  const host = url.hostname.trim().toLowerCase().replace(/\.$/, "");
  return host.length > 0 ? host : null;
}

type Columns = {
  url: number;
  username: number;
  password: number;
  note: number;
  rows: string[][];
};

function columnsOf(records: string[][]): Columns | null {
  const header = records[0].map((cell) => cell.trim().toLowerCase());
  const url = indexOf(header, ["url", "website", "origin", "login_uri", "login_url"]);
  const username = indexOf(header, ["username", "user", "login", "login_username"]);
  const password = indexOf(header, ["password", "login_password"]);
  if (url !== -1 && username !== -1 && password !== -1) {
    return {
      url,
      username,
      password,
      note: indexOf(header, ["note", "notes"]),
      rows: records.slice(1),
    };
  }
  const first = records[0];
  if (first.length >= 4 && websiteHostname(first[1])) {
    return { url: 1, username: 2, password: 3, note: 4, rows: records };
  }
  if (first.length >= 3 && websiteHostname(first[0])) {
    return { url: 0, username: 1, password: 2, note: -1, rows: records };
  }
  return null;
}

function indexOf(header: string[], names: string[]): number {
  for (const name of names) {
    const index = header.indexOf(name);
    if (index !== -1) return index;
  }
  return -1;
}

function cell(row: string[], index: number): string {
  if (index < 0 || index >= row.length) return "";
  return row[index];
}

function bump(counts: Map<string, number>, reason: string): void {
  counts.set(reason, (counts.get(reason) ?? 0) + 1);
}

// RFC 4180, including quotes, escaped quotes, and newlines inside quotes.
function parseCsv(text: string): string[][] | null {
  const rows: string[][] = [];
  let row: string[] = [];
  let field = "";
  let quoted = false;
  for (let index = 0; index < text.length; index++) {
    const char = text[index];
    if (quoted) {
      if (char === '"') {
        if (text[index + 1] === '"') {
          field += '"';
          index += 1;
        } else {
          quoted = false;
        }
      } else {
        field += char;
      }
      continue;
    }
    if (char === '"') {
      if (field.length === 0) {
        quoted = true;
        continue;
      }
      field += char;
      continue;
    }
    if (char === ",") {
      row.push(field);
      field = "";
      continue;
    }
    if (char === "\n" || char === "\r") {
      if (char === "\r" && text[index + 1] === "\n") index += 1;
      row.push(field);
      field = "";
      if (!(row.length === 1 && row[0] === "")) rows.push(row);
      row = [];
      continue;
    }
    field += char;
  }
  if (quoted) return null;
  if (field.length > 0 || row.length > 0) {
    row.push(field);
    rows.push(row);
  }
  return rows;
}
