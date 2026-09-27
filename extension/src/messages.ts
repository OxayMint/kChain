import type { DeviceCommand, VaultEntry, VaultSnapshot } from "@shared/protocol";

export type TyperPhase = "down" | "waiting" | "ready";

export type StatusResponse = {
  phase: TyperPhase;
  snapshot: VaultSnapshot | null;
  message: string | null;
};

export type VaultResult =
  | { ok: true; snapshot?: VaultSnapshot }
  | { ok: false; message: string };

export type MatchResult =
  | { ok: true; entries: VaultEntry[] }
  | { ok: false; message: string };

export type PendingSave = {
  host: string;
  username: string;
  password: string;
  kind: "save" | "update";
  id?: number;
};

export type ClassifyResult =
  | { action: "none" }
  | { action: "ignored" }
  | { action: "save" }
  | { action: "update"; id: number }
  | { action: "unavailable"; message: string };

export type ExtensionRequest =
  | { type: "status" }
  | { type: "matches"; host?: string }
  | { type: "command"; command: DeviceCommand }
  | { type: "classify"; username: string; password: string }
  | { type: "pending-get" }
  | { type: "pending-set"; pending: PendingSave }
  | { type: "pending-clear" }
  | { type: "ignore-host"; host?: string }
  | { type: "open-vault" }
  | { type: "fill"; username: string; password: string };

export function messageOf(error: unknown): string {
  if (error instanceof Error && error.message) return error.message;
  return "Something went wrong talking to the device.";
}
