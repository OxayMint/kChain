"use client";

import { useEffect, useRef, useState, useSyncExternalStore } from "react";

import { Alert, AlertDescription, AlertTitle } from "@/components/ui/alert";
import { Button } from "@/components/ui/button";
import {
  Dialog,
  DialogContent,
  DialogDescription,
  DialogFooter,
  DialogHeader,
  DialogTitle,
} from "@/components/ui/dialog";
import { Input } from "@/components/ui/input";
import { Label } from "@/components/ui/label";
import { AgentVault, usbTyperStatus, type UsbTyperStatus } from "@/lib/agent-session";
import { generatePassword } from "@/lib/generate-password";
import {
  snapshotFrom,
  validateHostname,
  validateLabel,
  validateName,
  validatePassword,
  validatePhrase,
  validateUsername,
  type DeviceCommand,
  type DeviceEvent,
  type EntryType,
  type VaultEntry,
  type VaultSnapshot,
} from "@/lib/protocol";
import {
  DeviceError,
  delay,
  isPortCancel,
  SerialVault,
  serialSupported,
  type VaultSession,
} from "@/lib/serial-session";

type Status =
  | { phase: "disconnected" }
  | { phase: "connecting"; via: "agent" | "serial" }
  | { phase: "loading" }
  | { phase: "ready"; snapshot: VaultSnapshot; notice: string | null }
  | { phase: "error"; message: string };

type Busy = null | "add" | "edit" | "delete";

function messageOf(error: unknown): string {
  if (error instanceof Error && error.message) return error.message;
  return "Something went wrong talking to the device.";
}

export function VaultEditor() {
  const [status, setStatus] = useState<Status>({ phase: "disconnected" });
  const [busy, setBusy] = useState<Busy>(null);
  const [kind, setKind] = useState<EntryType>("generic");
  const [name, setName] = useState("");
  const [username, setUsername] = useState("");
  const [password, setPassword] = useState("");
  const [phrase, setPhrase] = useState("");
  const [showAddPassword, setShowAddPassword] = useState(false);
  const [formError, setFormError] = useState<string | null>(null);
  const [revealed, setRevealed] = useState<Record<number, boolean>>({});
  const [editing, setEditing] = useState<VaultEntry | null>(null);
  const [editName, setEditName] = useState("");
  const [editUsername, setEditUsername] = useState("");
  const [editPassword, setEditPassword] = useState("");
  const [editPhrase, setEditPhrase] = useState("");
  const [editError, setEditError] = useState<string | null>(null);
  const [deleting, setDeleting] = useState<VaultEntry | null>(null);
  const sessionRef = useRef<VaultSession | null>(null);
  const usbSeen = useRef<boolean | null>(null);
  const connectRef = useRef<(mode: "agent" | "espressif" | "any") => Promise<void>>(
    async () => { },
  );
  const autoStarted = useRef(false);
  const [typer, setTyper] = useState<UsbTyperStatus>("down");
  const browserSerial = useSyncExternalStore(
    () => () => { },
    serialSupported,
    () => true,
  );

  useEffect(() => {
    return () => {
      void sessionRef.current?.close();
    };
  }, []);

  useEffect(() => {
    let stop = false;
    async function probe() {
      let next = await usbTyperStatus();
      if (next === "down") {
        try {
          await fetch("/api/typer", { method: "POST" });
        } catch {
          // The next probe tries again.
        }
        next = await usbTyperStatus();
      }
      if (!stop) setTyper(next);
    }
    void probe();
    const timer = setInterval(() => void probe(), 2000);
    return () => {
      stop = true;
      clearInterval(timer);
    };
  }, []);

  useEffect(() => {
    if (typer !== "ready" || status.phase !== "disconnected") return;
    if (autoStarted.current) return;
    autoStarted.current = true;
    void connectRef.current("agent");
  }, [typer, status.phase]);

  useEffect(() => {
    if (typer === "down" || status.phase !== "ready") return;
    const session = sessionRef.current;
    if (!session) return;
    let stop = false;
    async function refreshUsb() {
      await delay(1500);
      if (stop || sessionRef.current !== session) return;
      try {
        const response = await session.request({ op: "list" });
        const next = snapshotFrom(response);
        if (stop) return;
        setStatus((current) => {
          if (current.phase !== "ready") return current;
          if (current.snapshot.usbTyping === next.usbTyping) return current;
          return {
            ...current,
            snapshot: { ...current.snapshot, usbTyping: next.usbTyping },
          };
        });
      } catch {
        // A later heartbeat or the next probe updates the same flag.
      }
    }
    void refreshUsb();
    return () => {
      stop = true;
    };
  }, [typer, status.phase]);

  function applyEvent(event: DeviceEvent) {
    if (event.event === "usb") usbSeen.current = event.ready;
    setStatus((current) => {
      if (current.phase !== "ready") return current;
      if (event.event === "usb") {
        return {
          ...current,
          snapshot: { ...current.snapshot, usbTyping: event.ready },
        };
      }
      if (event.event === "selected") {
        return {
          ...current,
          snapshot: {
            ...current.snapshot,
            selectedId: event.id,
            active: true,
          },
        };
      }
      if (event.event === "idle") {
        return {
          ...current,
          snapshot: { ...current.snapshot, active: false },
        };
      }
      if (event.event === "typed") {
        return { ...current, notice: `Typed ${event.name}.` };
      }
      if (event.event === "type_failed") {
        return { ...current, notice: event.error };
      }
      return current;
    });
  }

  async function readVault(session: VaultSession): Promise<VaultSnapshot> {
    let last: unknown = null;
    await delay(200);
    for (let attempt = 0; attempt < 4; attempt++) {
      try {
        if (attempt > 0) await delay(500);
        const response = await session.request({ op: "list" });
        return snapshotFrom(response);
      } catch (error) {
        if (error instanceof DeviceError) throw error;
        last = error;
      }
    }
    throw last instanceof Error
      ? last
      : new Error("The device did not answer.");
  }

  async function connect(mode: "agent" | "espressif" | "any") {
    setFormError(null);
    usbSeen.current = null;
    setStatus({ phase: "connecting", via: mode === "agent" ? "agent" : "serial" });
    const onDisconnect = (message: string) => {
      sessionRef.current = null;
      setBusy(null);
      setStatus({ phase: "error", message });
    };
    const session: VaultSession =
      mode === "agent"
        ? new AgentVault((event) => applyEvent(event), onDisconnect)
        : new SerialVault((event) => applyEvent(event), onDisconnect);
    try {
      if (session instanceof AgentVault) await session.connect();
      else await (session as SerialVault).connect(mode === "any");
      setStatus({ phase: "loading" });
      const snapshot = await readVault(session);
      if (usbSeen.current != null) snapshot.usbTyping = usbSeen.current;
      sessionRef.current = session;
      setStatus({ phase: "ready", snapshot, notice: null });
    } catch (error) {
      await session.close();
      if (isPortCancel(error)) {
        setStatus({ phase: "disconnected" });
        return;
      }
      setStatus({ phase: "error", message: messageOf(error) });
    }
  }

  connectRef.current = connect;

  async function mutate(
    kind: Exclude<Busy, null>,
    run: (session: VaultSession) => Promise<VaultSnapshot>,
  ) {
    const session = sessionRef.current;
    if (!session) {
      setStatus({
        phase: "error",
        message: "The serial port is not open.",
      });
      return;
    }
    setBusy(kind);
    setFormError(null);
    try {
      const snapshot = await run(session);
      setStatus({ phase: "ready", snapshot, notice: null });
      if (kind === "add") {
        setName("");
        setUsername("");
        setPassword("");
        setPhrase("");
        setShowAddPassword(false);
      }
      if (kind === "edit") setEditing(null);
      if (kind === "delete") setDeleting(null);
    } catch (error) {
      const message = messageOf(error);
      if (kind === "add") setFormError(message);
      else if (kind === "edit") setEditError(message);
      else setFormError(message);
    } finally {
      setBusy(null);
    }
  }

  async function onAdd(event: React.FormEvent) {
    event.preventDefault();
    const fieldError = entryError(kind, name, username, password, phrase);
    if (fieldError) {
      setFormError(fieldError);
      return;
    }
    const command = entryCommand("add", kind, name, username, password, phrase);
    await mutate("add", async (session) => {
      const response = await session.request(command);
      return snapshotFrom(response);
    });
  }

  async function onEdit(event: React.FormEvent) {
    event.preventDefault();
    if (!editing) return;
    const fieldError = entryError(
      editing.type,
      editName,
      editUsername,
      editPassword,
      editPhrase,
    );
    if (fieldError) {
      setEditError(fieldError);
      return;
    }
    const command = entryCommand(
      "edit",
      editing.type,
      editName,
      editUsername,
      editPassword,
      editPhrase,
      editing.id,
    );
    await mutate("edit", async (session) => {
      const response = await session.request(command);
      return snapshotFrom(response);
    });
  }

  async function onDelete() {
    if (!deleting) return;
    const id = deleting.id;
    await mutate("delete", async (session) => {
      const response = await session.request({ op: "delete", id });
      return snapshotFrom(response);
    });
  }

  function openEdit(entry: VaultEntry) {
    setEditName(entry.name);
    setEditUsername(entry.username);
    setEditPassword(entry.password);
    setEditPhrase(entry.phrase);
    setEditError(null);
    setEditing(entry);
  }

  const snapshot = status.phase === "ready" ? status.snapshot : null;
  const showUnsupported =
    !browserSerial && typer === "down" && status.phase === "disconnected";

  return (
    <div className="flex min-h-full flex-1 flex-col">
      <div className="h-1 bg-primary" />
      <div className="mx-auto flex w-full max-w-3xl flex-1 flex-col px-4 py-8 sm:px-6 sm:py-12">
        <header className="flex flex-col gap-6 border-b border-border pb-8 sm:flex-row sm:items-end sm:justify-between">
          <div className="max-w-xl">
            <h1 className="font-display text-5xl tracking-tight text-foreground">
              kChain
            </h1>
            <p className="mt-3 text-base leading-7 text-muted-foreground">
              Turn the wheel to choose an entry, then double-tap to type it.
              This page shows which entry is on the device, and adds and changes
              entries.
            </p>
          </div>
          <KeyboardStatus snapshot={snapshot} typer={typer} />
        </header>

        <div className="mt-8 flex flex-1 flex-col gap-6" aria-live="polite">
          {showUnsupported && (
            <Alert variant="destructive">
              <AlertTitle>Use Chrome or Edge</AlertTitle>
              <AlertDescription>
                Open this page in Chrome or Edge, with the device plugged in.
              </AlertDescription>
            </Alert>
          )}

          {status.phase === "disconnected" && !showUnsupported && (
            <section className="rounded-xl border border-border bg-card p-5 sm:p-6">
              <h2 className="font-display text-3xl">Connect</h2>
              <p className="mt-2 max-w-xl text-sm leading-6 text-muted-foreground">
                {typer === "waiting"
                  ? "Plug in the device. This page connects when it shows up."
                  : typer === "down"
                    ? "Starting USB typing…"
                    : "Connecting to the device…"}
              </p>
              <div className="mt-5 flex flex-col gap-2 sm:flex-row">
                {typer === "ready" && (
                  <Button type="button" onClick={() => void connect("agent")}>
                    Connect
                  </Button>
                )}
                {browserSerial && typer !== "down" && (
                  <Button
                    type="button"
                    variant="outline"
                    onClick={() => void connect("espressif")}
                  >
                    Choose a port
                  </Button>
                )}
                {browserSerial && (
                  <Button
                    type="button"
                    variant="outline"
                    onClick={() => void connect("any")}
                  >
                    Other ports
                  </Button>
                )}
              </div>
            </section>
          )}

          {status.phase === "connecting" && (
            <StatusBlock title="Connecting">
              {status.via === "agent"
                ? "Connecting to the device."
                : "Choose the device if the browser asks."}
            </StatusBlock>
          )}

          {status.phase === "loading" && (
            <div role="status" className="space-y-3">
              <p className="text-sm text-muted-foreground">
                Loading entries…
              </p>
              <div className="h-20 animate-pulse rounded-xl bg-muted" />
              <div className="h-20 animate-pulse rounded-xl bg-muted" />
              <div className="h-20 animate-pulse rounded-xl bg-muted" />
            </div>
          )}

          {status.phase === "error" && (
            <section className="space-y-4">
              <Alert variant="destructive">
                <AlertTitle>Cannot reach the device</AlertTitle>
                <AlertDescription>{status.message}</AlertDescription>
              </Alert>
              <div className="flex flex-col gap-2 sm:flex-row">
                <Button
                  type="button"
                  onClick={() =>
                    void connect(typer === "down" && browserSerial ? "espressif" : "agent")
                  }
                >
                  Try again
                </Button>
                <Button
                  type="button"
                  variant="outline"
                  onClick={() => {
                    autoStarted.current = false;
                    setStatus({ phase: "disconnected" });
                  }}
                >
                  Back
                </Button>
              </div>
            </section>
          )}

          {snapshot && (
            <>
              {status.phase === "ready" && status.notice && (
                <Alert>
                  <AlertDescription>{status.notice}</AlertDescription>
                </Alert>
              )}
              {status.phase === "ready" && !snapshot.usbTyping && typer === "down" && (
                <Alert>
                  <AlertTitle>USB typing is not ready</AlertTitle>
                  <AlertDescription>
                    Reload this page. It starts USB typing on this computer.
                  </AlertDescription>
                </Alert>
              )}

              <p className="text-sm text-muted-foreground">{deviceStatus(snapshot)}</p>

              {snapshot.entries.length === 0 ? (
                <section className="rounded-xl border border-dashed border-border px-5 py-10 text-center">
                  <h2 className="font-display text-3xl">No entries yet</h2>
                  <p className="mx-auto mt-2 max-w-md text-sm leading-6 text-muted-foreground">
                    Add one below.
                  </p>
                </section>
              ) : (
                <section className="space-y-3">
                  <h2 className="text-sm text-muted-foreground">
                    {snapshot.entries.length === 1
                      ? "1 entry"
                      : `${snapshot.entries.length} entries`}
                  </h2>
                  <ul className="space-y-3">
                    {snapshot.entries.map((entry) => {
                      const awake = snapshot.active && entry.id === snapshot.selectedId;
                      const visible = revealed[entry.id] === true;
                      return (
                        <li
                          key={entry.id}
                          className={
                            awake
                              ? "rounded-xl border border-primary bg-card p-4"
                              : "rounded-xl border border-border bg-card p-4"
                          }
                        >
                          <div className="flex flex-col gap-3 sm:flex-row sm:items-start sm:justify-between">
                            <div className="min-w-0">
                              <div className="flex flex-wrap items-center gap-2">
                                <h3 className="truncate text-base font-medium">
                                  {entry.name}
                                </h3>
                                <span className="rounded-full border border-border px-2 py-0.5 text-xs text-muted-foreground">
                                  {typeLabel(entry.type)}
                                </span>
                                {awake && (
                                  <span className="rounded-full bg-primary px-2 py-0.5 text-xs font-medium text-primary-foreground">
                                    On device
                                  </span>
                                )}
                              </div>
                              <EntrySecrets entry={entry} visible={visible} />
                            </div>
                            <div className="flex flex-wrap gap-2">
                              <Button
                                type="button"
                                variant="ghost"
                                size="sm"
                                onClick={() =>
                                  setRevealed((current) => ({
                                    ...current,
                                    [entry.id]: !visible,
                                  }))
                                }
                              >
                                {visible ? "Hide" : "Show"}
                              </Button>
                              <Button
                                type="button"
                                variant="outline"
                                size="sm"
                                disabled={busy !== null}
                                onClick={() => openEdit(entry)}
                              >
                                Edit
                              </Button>
                              <Button
                                type="button"
                                variant="destructive"
                                size="sm"
                                disabled={busy !== null}
                                onClick={() => setDeleting(entry)}
                              >
                                Delete
                              </Button>
                            </div>
                          </div>
                        </li>
                      );
                    })}
                  </ul>
                </section>
              )}

              <form
                onSubmit={(event) => void onAdd(event)}
                noValidate
                className="rounded-xl border border-border bg-card p-4 sm:p-5"
              >
                <h2 className="font-display text-3xl">Add an entry</h2>
                <div className="mt-4">
                  <TypeSwitch
                    value={kind}
                    disabled={busy !== null}
                    onChange={(next) => {
                      setKind(next);
                      setFormError(null);
                    }}
                  />
                </div>
                <div className="mt-4 grid gap-4 sm:grid-cols-2">
                  <div className="grid gap-2">
                    <Label htmlFor="add-name">{nameLabel(kind)}</Label>
                    <Input
                      id="add-name"
                      name="name"
                      autoComplete="off"
                      value={name}
                      disabled={busy !== null}
                      onChange={(event) => setName(event.target.value)}
                    />
                  </div>
                  {kind === "website" && (
                    <div className="grid gap-2">
                      <Label htmlFor="add-username">Username or email</Label>
                      <Input
                        id="add-username"
                        name="username"
                        autoComplete="off"
                        spellCheck={false}
                        value={username}
                        disabled={busy !== null}
                        onChange={(event) => setUsername(event.target.value)}
                      />
                    </div>
                  )}
                  {kind === "crypto" ? (
                    <div className="grid gap-2 sm:col-span-2">
                      <Label htmlFor="add-phrase">Words</Label>
                      <textarea
                        id="add-phrase"
                        name="phrase"
                        autoComplete="off"
                        spellCheck={false}
                        rows={3}
                        value={phrase}
                        disabled={busy !== null}
                        onChange={(event) => setPhrase(event.target.value)}
                        className="w-full min-w-0 rounded-lg border border-input bg-transparent px-2.5 py-2 font-mono text-sm transition-colors outline-none placeholder:text-muted-foreground focus-visible:border-ring focus-visible:ring-3 focus-visible:ring-ring/50 disabled:pointer-events-none disabled:cursor-not-allowed disabled:opacity-50 dark:bg-input/30"
                      />
                    </div>
                  ) : (
                    <div className="grid gap-2">
                      <div className="flex items-center justify-between gap-2">
                        <Label htmlFor="add-password">Password</Label>
                        <button
                          type="button"
                          className="text-xs text-muted-foreground underline-offset-4 hover:underline"
                          onClick={() => setShowAddPassword((current) => !current)}
                        >
                          {showAddPassword ? "Hide" : "Show"}
                        </button>
                      </div>
                      <Input
                        id="add-password"
                        name="password"
                        type={showAddPassword ? "text" : "password"}
                        autoComplete="off"
                        spellCheck={false}
                        value={password}
                        disabled={busy !== null}
                        onChange={(event) => setPassword(event.target.value)}
                      />
                      <div>
                        <Button
                          type="button"
                          variant="outline"
                          size="sm"
                          disabled={busy !== null}
                          onClick={() => {
                            setPassword(generatePassword());
                            setShowAddPassword(true);
                            setFormError(null);
                          }}
                        >
                          Generate password
                        </Button>
                      </div>
                    </div>
                  )}
                </div>
                {formError && (
                  <p className="mt-3 text-sm text-destructive" role="alert">
                    {formError}
                  </p>
                )}
                <div className="mt-4 flex justify-end">
                  <Button type="submit" disabled={busy !== null}>
                    {busy === "add" ? "Adding…" : "Add"}
                  </Button>
                </div>
              </form>
            </>
          )}
        </div>
      </div>

      <Dialog
        open={editing !== null}
        onOpenChange={(open) => {
          if (!open && busy !== "edit") {
            setEditing(null);
            setEditError(null);
          }
        }}
      >
        <DialogContent>
          <DialogHeader>
            <DialogTitle>Edit entry</DialogTitle>
            <DialogDescription>{editDescription(editing?.type ?? "generic")}</DialogDescription>
          </DialogHeader>
          <form onSubmit={(event) => void onEdit(event)} noValidate>
            <div className="grid gap-4">
              <div className="grid gap-2">
                <Label htmlFor="edit-name">{nameLabel(editing?.type ?? "generic")}</Label>
                <Input
                  id="edit-name"
                  value={editName}
                  disabled={busy === "edit"}
                  onChange={(event) => setEditName(event.target.value)}
                />
              </div>
              {editing?.type === "website" && (
                <div className="grid gap-2">
                  <Label htmlFor="edit-username">Username or email</Label>
                  <Input
                    id="edit-username"
                    autoComplete="off"
                    spellCheck={false}
                    value={editUsername}
                    disabled={busy === "edit"}
                    onChange={(event) => setEditUsername(event.target.value)}
                  />
                </div>
              )}
              {editing?.type === "crypto" ? (
                <div className="grid gap-2">
                  <Label htmlFor="edit-phrase">Words</Label>
                  <textarea
                    id="edit-phrase"
                    autoComplete="off"
                    spellCheck={false}
                    rows={3}
                    value={editPhrase}
                    disabled={busy === "edit"}
                    onChange={(event) => setEditPhrase(event.target.value)}
                    className="w-full min-w-0 rounded-lg border border-input bg-transparent px-2.5 py-2 font-mono text-sm transition-colors outline-none focus-visible:border-ring focus-visible:ring-3 focus-visible:ring-ring/50 disabled:pointer-events-none disabled:cursor-not-allowed disabled:opacity-50 dark:bg-input/30"
                  />
                </div>
              ) : (
                <div className="grid gap-2">
                  <Label htmlFor="edit-password">Password</Label>
                  <Input
                    id="edit-password"
                    type="text"
                    autoComplete="off"
                    spellCheck={false}
                    value={editPassword}
                    disabled={busy === "edit"}
                    onChange={(event) => setEditPassword(event.target.value)}
                  />
                  <div>
                    <Button
                      type="button"
                      variant="outline"
                      size="sm"
                      disabled={busy === "edit"}
                      onClick={() => {
                        setEditPassword(generatePassword());
                        setEditError(null);
                      }}
                    >
                      Generate password
                    </Button>
                  </div>
                </div>
              )}
              {editError && (
                <p className="text-sm text-destructive" role="alert">
                  {editError}
                </p>
              )}
            </div>
            <DialogFooter className="mt-4">
              <Button
                type="button"
                variant="outline"
                disabled={busy === "edit"}
                onClick={() => setEditing(null)}
              >
                Cancel
              </Button>
              <Button type="submit" disabled={busy === "edit"}>
                {busy === "edit" ? "Saving…" : "Save"}
              </Button>
            </DialogFooter>
          </form>
        </DialogContent>
      </Dialog>

      <Dialog
        open={deleting !== null}
        onOpenChange={(open) => {
          if (!open && busy !== "delete") setDeleting(null);
        }}
      >
        <DialogContent>
          <DialogHeader>
            <DialogTitle>Delete {deleting?.name ?? "this entry"}?</DialogTitle>
            <DialogDescription>This cannot be undone.</DialogDescription>
          </DialogHeader>
          <DialogFooter>
            <Button
              type="button"
              variant="outline"
              disabled={busy === "delete"}
              onClick={() => setDeleting(null)}
            >
              Cancel
            </Button>
            <Button
              type="button"
              variant="destructive"
              disabled={busy === "delete"}
              onClick={() => void onDelete()}
            >
              {busy === "delete" ? "Deleting…" : "Delete"}
            </Button>
          </DialogFooter>
        </DialogContent>
      </Dialog>
    </div>
  );
}

function entryError(
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

function entryCommand(
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

function deviceStatus(snapshot: VaultSnapshot): string {
  if (!snapshot.active || snapshot.selectedId == null) return "Device is idle";
  const index = snapshot.entries.findIndex((entry) => entry.id === snapshot.selectedId);
  if (index < 0) return "Device is idle";
  return `On the device: ${index + 1}. ${snapshot.entries[index].name}`;
}

function typeLabel(type: EntryType): string {
  if (type === "website") return "Website";
  if (type === "crypto") return "Crypto";
  return "Generic";
}

function nameLabel(type: EntryType): string {
  if (type === "website") return "Hostname";
  if (type === "crypto") return "Label";
  return "Name";
}

function editDescription(type: EntryType): string {
  if (type === "website") return "Change the hostname, username, or password.";
  if (type === "crypto") return "Change the label or words.";
  return "Change the name or password.";
}

function TypeSwitch({
  value,
  disabled,
  onChange,
}: {
  value: EntryType;
  disabled: boolean;
  onChange: (value: EntryType) => void;
}) {
  const options: { id: EntryType; label: string }[] = [
    { id: "generic", label: "Generic" },
    { id: "website", label: "Website" },
    { id: "crypto", label: "Crypto" },
  ];
  return (
    <div className="flex flex-wrap gap-2" role="group" aria-label="Entry type">
      {options.map((option) => (
        <Button
          key={option.id}
          type="button"
          size="sm"
          variant={value === option.id ? "default" : "outline"}
          disabled={disabled}
          aria-pressed={value === option.id}
          onClick={() => onChange(option.id)}
        >
          {option.label}
        </Button>
      ))}
    </div>
  );
}

function EntrySecrets({ entry, visible }: { entry: VaultEntry; visible: boolean }) {
  if (entry.type === "crypto") {
    return <Secret value={entry.phrase} visible={visible} />;
  }
  if (entry.type === "website") {
    return (
      <div className="mt-2 space-y-2">
        <Secret label="Username" value={entry.username} visible={visible} />
        <Secret label="Password" value={entry.password} visible={visible} />
      </div>
    );
  }
  return (
    <div className="mt-2">
      <Secret value={entry.password} visible={visible} />
    </div>
  );
}

function Secret({
  label,
  value,
  visible,
}: {
  label?: string;
  value: string;
  visible: boolean;
}) {
  return (
    <p className="font-mono text-sm break-all text-foreground">
      {label && <span className="mr-2 font-sans text-muted-foreground">{label}</span>}
      {visible ? value : "••••••••"}
    </p>
  );
}

function KeyboardStatus({
  snapshot,
  typer,
}: {
  snapshot: VaultSnapshot | null;
  typer: UsbTyperStatus;
}) {
  if (!snapshot) {
    return (
      <p className="text-sm text-muted-foreground sm:text-right">Not connected</p>
    );
  }
  const typing = snapshot.usbTyping;
  const label = typing
    ? "Types on this computer"
    : typer === "down"
      ? "USB typer is not running"
      : "Waiting for the USB typer";
  return (
    <p className="text-sm sm:max-w-56 sm:text-right">
      <span
        className={
          typing
            ? "mr-2 inline-block size-2 rounded-full bg-primary"
            : "mr-2 inline-block size-2 rounded-full bg-muted-foreground"
        }
        aria-hidden
      />
      {label}
    </p>
  );
}

function StatusBlock({
  title,
  children,
}: {
  title: string;
  children: string;
}) {
  return (
    <section role="status" className="rounded-xl border border-border bg-card p-5">
      <h2 className="font-display text-3xl">{title}</h2>
      <p className="mt-2 text-sm text-muted-foreground">{children}</p>
    </section>
  );
}
