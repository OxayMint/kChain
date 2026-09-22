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
import {
  LIMITS,
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
} from "@/lib/serial-session";

type Status =
  | { phase: "disconnected" }
  | { phase: "connecting" }
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
  const sessionRef = useRef<SerialVault | null>(null);
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

  function applyEvent(event: DeviceEvent) {
    setStatus((current) => {
      if (current.phase !== "ready") return current;
      if (event.event === "keyboard") {
        return {
          ...current,
          snapshot: { ...current.snapshot, keyboardConnected: event.connected },
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

  async function readVault(session: SerialVault): Promise<VaultSnapshot> {
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

  async function connect(anyPort: boolean) {
    setFormError(null);
    setStatus({ phase: "connecting" });
    const session = new SerialVault(
      (event) => applyEvent(event),
      (message) => {
        sessionRef.current = null;
        setBusy(null);
        setStatus({ phase: "error", message });
      },
    );
    try {
      await session.connect(anyPort);
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
    run: (session: SerialVault) => Promise<VaultSnapshot>,
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
  const showUnsupported = !browserSerial && status.phase === "disconnected";

  return (
    <div className="flex min-h-full flex-1 flex-col">
      <div className="h-1 bg-primary" />
      <div className="mx-auto flex w-full max-w-3xl flex-1 flex-col px-4 py-8 sm:px-6 sm:py-12">
        <header className="flex flex-col gap-6 border-b border-border pb-8 sm:flex-row sm:items-end sm:justify-between">
          <div className="max-w-xl">
            <p className="font-mono text-xs tracking-[0.16em] text-muted-foreground uppercase">
              ESP32-C3 Super Mini
            </p>
            <h1 className="mt-2 font-display text-5xl tracking-tight text-foreground">
              kChain
            </h1>
            <p className="mt-3 text-base leading-7 text-muted-foreground">
              Passwords stay on the board. Pair kChain over Bluetooth, choose an
              entry with the buttons, and it types that password into the
              focused field. This page writes the vault over the USB cable.
            </p>
          </div>
          <KeyboardStatus snapshot={snapshot} />
        </header>

        <div className="mt-8 flex flex-1 flex-col gap-6" aria-live="polite">
          {showUnsupported && (
            <Alert variant="destructive">
              <AlertTitle>This browser cannot open USB serial</AlertTitle>
              <AlertDescription>
                Open kChain in Chrome or Edge on the computer the ESP32-C3 is
                plugged into. Web Serial is how this page talks to the board.
              </AlertDescription>
            </Alert>
          )}

          {status.phase === "disconnected" && !showUnsupported && (
            <section className="rounded-xl border border-border bg-card p-5 sm:p-6">
              <h2 className="font-display text-3xl">Connect the board</h2>
              <p className="mt-2 max-w-xl text-sm leading-6 text-muted-foreground">
                Plug in the Super Mini, then choose its USB serial port. Chrome
                and Edge list Espressif devices (vendor 303A) first. Entries
                are stored in the clear. There is no PIN.
              </p>
              <p className="mt-3 max-w-xl text-sm leading-6 text-muted-foreground">
                On the device, the three buttons are previous, type, and next.
                Type sends the password characters only, then stops. It does
                not press Enter.
              </p>
              <div className="mt-5 flex flex-col gap-2 sm:flex-row">
                <Button type="button" onClick={() => void connect(false)}>
                  Connect device
                </Button>
                <Button
                  type="button"
                  variant="outline"
                  onClick={() => void connect(true)}
                >
                  Show every serial port
                </Button>
              </div>
            </section>
          )}

          {status.phase === "connecting" && (
            <StatusBlock title="Waiting for a serial port">
              Choose the ESP32-C3 in the browser prompt. Cancel returns you
              here.
            </StatusBlock>
          )}

          {status.phase === "loading" && (
            <div role="status" className="space-y-3">
              <p className="text-sm text-muted-foreground">
                Reading entries from the device…
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
                <Button type="button" onClick={() => void connect(false)}>
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
                  <AlertTitle>From the device</AlertTitle>
                  <AlertDescription>{status.notice}</AlertDescription>
                </Alert>
              )}

              {snapshot.entries.length === 0 ? (
                <section className="rounded-xl border border-dashed border-border px-5 py-10 text-center">
                  <h2 className="font-display text-3xl">No passwords stored yet</h2>
                  <p className="mx-auto mt-2 max-w-md text-sm leading-6 text-muted-foreground">
                    Add the first one below. It is written to flash on the
                    ESP32-C3. An empty vault is fine until you do.
                  </p>
                </section>
              ) : (
                <section className="space-y-3">
                  <h2 className="text-sm text-muted-foreground">
                    {snapshot.entries.length === 1
                      ? "1 entry on the device"
                      : `${snapshot.entries.length} entries on the device`}
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
                <h2 className="font-display text-3xl">Add an entry</h2>
                <p className="mt-1 text-sm text-muted-foreground">
                  Up to {LIMITS.entryMax} entries. Names can be {LIMITS.nameMaxBytes}{" "}
                  bytes. Passwords are printable ASCII, up to {LIMITS.passwordMax}{" "}
                  characters, because that is what the keyboard can type.
                </p>
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
                  </div>
                </div>
                {formError && (
                  <p className="mt-3 text-sm text-destructive" role="alert">
                    {formError}
                  </p>
                )}
                <div className="mt-4 flex justify-end">
                  <Button type="submit" disabled={busy !== null}>
                    {busy === "add" ? "Writing…" : "Add to device"}
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
              This replaces the name and password stored on the device.
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
            <DialogDescription>
              The password is removed from the device flash. This cannot be
              undone.
            </DialogDescription>
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
              {busy === "delete" ? "Deleting…" : "Delete from device"}
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
          snapshot.keyboardConnected
            ? "mr-2 inline-block size-2 rounded-full bg-primary"
            : "mr-2 inline-block size-2 rounded-full bg-muted-foreground"
        }
        aria-hidden
      />
      {snapshot.keyboardConnected
        ? "Bluetooth keyboard connected"
        : "Waiting for a Bluetooth host to pair with kChain"}
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
