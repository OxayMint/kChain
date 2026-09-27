import {
  validateHostname,
  validateLabel,
  validateName,
  validatePassword,
  validatePhrase,
  validateUsername,
  type DeviceCommand,
  type EntryType,
} from "@shared/protocol";

export function entryError(
  type: EntryType,
  name: string,
  username: string,
  password: string,
  phrase: string,
): string | null {
  if (type === "website") {
    return validateHostname(name) ?? validateUsername(username) ?? validatePassword(password);
  }
  if (type === "crypto") return validateLabel(name) ?? validatePhrase(phrase);
  return validateName(name) ?? validatePassword(password);
}

export function entryCommand(
  op: "add" | "edit",
  type: EntryType,
  name: string,
  username: string,
  password: string,
  phrase: string,
  id?: number,
): DeviceCommand {
  if (type === "website") {
    return op === "add"
      ? { op, type, name, username, password }
      : { op, id: id ?? 0, type, name, username, password };
  }
  if (type === "crypto") {
    return op === "add" ? { op, type, name, phrase } : { op, id: id ?? 0, type, name, phrase };
  }
  return op === "add"
    ? { op, type: "generic", name, password }
    : { op, id: id ?? 0, type: "generic", name, password };
}

export function typeLabel(type: EntryType): string {
  if (type === "website") return "Website";
  if (type === "crypto") return "Crypto";
  return "Generic";
}

export function nameLabel(type: EntryType): string {
  if (type === "website") return "Hostname";
  if (type === "crypto") return "Label";
  return "Name";
}
