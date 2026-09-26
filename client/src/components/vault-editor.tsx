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
  validateName,
  validatePassword,
  type DeviceEvent,
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
  const [name, setName] = useState("");
  const [password, setPassword] = useState("");
  const [showAddPassword, setShowAddPassword] = useState(false);
  const [formError, setFormError] = useState<string | null>(null);
  const [revealed, setRevealed] = useState<Record<number, boolean>>({});
  const [editing, setEditing] = useState<VaultEntry | null>(null);
  const [editName, setEditName] = useState("");
  const [editPassword, setEditPassword] = useState("");
  const [editError, setEditError] = useState<string | null>(null);
  const [deleting, setDeleting] = useState<VaultEntry | null>(null);
  const sessionRef = useRef<VaultSession | null>(null);
  const [typer, setTyper] = useState<UsbTyperStatus>("down");
  const browserSerial = useSyncExternalStore(
    () => () => {},
    serialSupported,
    () => true,
  );

  useEffect(() => {
    return () => {
      void sessionRef.current?.close();
    };
  }, []);

  useEffect(() => {
    if (status.phase !== "disconnected") return;
    let stop = false;
    async function probe() {
      const next = await usbTyperStatus();
      if (!stop) setTyper(next);
    }
    void probe();
    const timer = setInterval(() => void probe(), 2000);
    return () => {
      stop = true;
      clearInterval(timer);
    };
  }, [status.phase]);

  function applyEvent(event: DeviceEvent) {
    setStatus((current) => {
      if (current.phase !== "ready") return current;
      if (event.event === "keyboard") {
        return {
          ...current,
          snapshot: { ...current.snapshot, keyboardConnected: event.connected },
        };
      }
      if (event.event === "usb") {
        return {
          ...current,
          snapshot: { ...current.snapshot, usbTyping: event.ready },
        };
      }
      if (event.event === "selected") {
        return {
          ...current,
          snapshot: { ...current.snapshot, selectedId: event.id },
          notice: event.name ? `Selected ${event.name}.` : current.notice,
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
        setPassword("");
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
    const nameError = validateName(name);
    const passwordError = validatePassword(password);
    if (nameError || passwordError) {
      setFormError(nameError ?? passwordError);
      return;
    }
    await mutate("add", async (session) => {
      const response = await session.request({
        op: "add",
        name,
        password,
      });
      return snapshotFrom(response);
    });
  }

  async function onEdit(event: React.FormEvent) {
    event.preventDefault();
    if (!editing) return;
    const nameError = validateName(editName);
    const passwordError = validatePassword(editPassword);
    if (nameError || passwordError) {
      setEditError(nameError ?? passwordError);
      return;
    }
    const id = editing.id;
    await mutate("edit", async (session) => {
      const response = await session.request({
        op: "edit",
        id,
        name: editName,
        password: editPassword,
      });
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
    setEditPassword(entry.password);
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
              A password device. Turn the wheel to choose a password, then
              double-tap to type it. Use this page to add and change passwords.
            </p>
          </div>
          <KeyboardStatus snapshot={snapshot} />
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
                Plug in the device, then connect.
              </p>
              <div className="mt-5 flex flex-col gap-2 sm:flex-row">
                {typer === "ready" && (
                  <Button type="button" onClick={() => void connect("agent")}>
                    Connect
                  </Button>
                )}
                {browserSerial && (
                  <Button
                    type="button"
                    variant={typer === "ready" ? "outline" : "default"}
                    onClick={() => void connect("espressif")}
                  >
                    {typer === "ready" ? "Choose a port" : "Connect"}
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
                Loading passwords…
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
                  onClick={() => setStatus({ phase: "disconnected" })}
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

              {snapshot.entries.length === 0 ? (
                <section className="rounded-xl border border-dashed border-border px-5 py-10 text-center">
                  <h2 className="font-display text-3xl">No passwords yet</h2>
                  <p className="mx-auto mt-2 max-w-md text-sm leading-6 text-muted-foreground">
                    Add one below.
                  </p>
                </section>
              ) : (
                <section className="space-y-3">
                  <h2 className="text-sm text-muted-foreground">
                    {snapshot.entries.length === 1
                      ? "1 password"
                      : `${snapshot.entries.length} passwords`}
                  </h2>
                  <ul className="space-y-3">
                    {snapshot.entries.map((entry) => {
                      const selected = entry.id === snapshot.selectedId;
                      const visible = revealed[entry.id] === true;
                      return (
                        <li
                          key={entry.id}
                          className={
                            selected
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
                                {selected && (
                                  <span className="rounded-full bg-primary px-2 py-0.5 text-xs font-medium text-primary-foreground">
                                    Selected
                                  </span>
                                )}
                              </div>
                              <p className="mt-2 font-mono text-sm break-all text-foreground">
                                {visible ? entry.password : "••••••••"}
                              </p>
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
                <h2 className="font-display text-3xl">Add a password</h2>
                <div className="mt-4 grid gap-4 sm:grid-cols-2">
                  <div className="grid gap-2">
                    <Label htmlFor="add-name">Name</Label>
                    <Input
                      id="add-name"
                      name="name"
                      autoComplete="off"
                      value={name}
                      disabled={busy !== null}
                      onChange={(event) => setName(event.target.value)}
                    />
                  </div>
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
            <DialogDescription>
              Change the name or password.
            </DialogDescription>
          </DialogHeader>
          <form onSubmit={(event) => void onEdit(event)} noValidate>
            <div className="grid gap-4">
              <div className="grid gap-2">
                <Label htmlFor="edit-name">Name</Label>
                <Input
                  id="edit-name"
                  value={editName}
                  disabled={busy === "edit"}
                  onChange={(event) => setEditName(event.target.value)}
                />
              </div>
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

function KeyboardStatus({ snapshot }: { snapshot: VaultSnapshot | null }) {
  if (!snapshot) {
    return (
      <p className="text-sm text-muted-foreground sm:text-right">Not connected</p>
    );
  }
  return (
    <p className="text-sm sm:max-w-56 sm:text-right">
      <span
        className={
          snapshot.usbTyping || snapshot.keyboardConnected
            ? "mr-2 inline-block size-2 rounded-full bg-primary"
            : "mr-2 inline-block size-2 rounded-full bg-muted-foreground"
        }
        aria-hidden
      />
      {snapshot.usbTyping
        ? "Types on this computer"
        : snapshot.keyboardConnected
          ? "Types over Bluetooth"
          : "Not paired"}
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
