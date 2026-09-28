import { useEffect, useRef, useState, type FormEvent } from "react";
import { generatePassword } from "@shared/generate-password";
import type { EntryType, VaultEntry, VaultSnapshot } from "@shared/protocol";
import { entryCommand, entryError, nameLabel, typeLabel } from "../entries";
import { FOCUS_IMPORT_KEY, type StatusResponse, type VaultResult } from "../messages";
import { deviceLine, typingLine } from "../status";
import { ImportChrome } from "./ImportChrome";

type Busy = null | "add" | "edit" | "delete";

export function SidePanel() {
  const [status, setStatus] = useState<StatusResponse | null>(null);
  const [busy, setBusy] = useState<Busy>(null);
  const [kind, setKind] = useState<EntryType>("website");
  const [name, setName] = useState("");
  const [username, setUsername] = useState("");
  const [password, setPassword] = useState("");
  const [phrase, setPhrase] = useState("");
  const [editing, setEditing] = useState<VaultEntry | null>(null);
  const [formError, setFormError] = useState<string | null>(null);
  const [revealed, setRevealed] = useState<Record<number, boolean>>({});
  const [deleting, setDeleting] = useState<number | null>(null);
  const [importLocked, setImportLocked] = useState(false);
  const [importNonce, setImportNonce] = useState(0);
  const importBusy = useRef(false);

  useEffect(() => {
    let stop = false;
    async function refresh() {
      const next = (await chrome.runtime.sendMessage({ type: "status" })) as StatusResponse;
      if (!stop && !importBusy.current) setStatus(next);
    }
    void refresh();
    const timer = setInterval(() => void refresh(), 3000);
    return () => {
      stop = true;
      clearInterval(timer);
    };
  }, []);

  useEffect(() => {
    function onChanged(changes: Record<string, chrome.storage.StorageChange>, area: string) {
      if (area === "session" && changes[FOCUS_IMPORT_KEY]?.newValue === true) {
        setImportNonce((current) => current + 1);
        void chrome.storage.session.remove(FOCUS_IMPORT_KEY);
      }
    }
    chrome.storage.onChanged.addListener(onChanged);
    void chrome.storage.session.get(FOCUS_IMPORT_KEY).then((stored) => {
      if (stored[FOCUS_IMPORT_KEY] === true) {
        setImportNonce((current) => current + 1);
        void chrome.storage.session.remove(FOCUS_IMPORT_KEY);
      }
    });
    return () => chrome.storage.onChanged.removeListener(onChanged);
  }, []);

  const snapshot = status?.snapshot ?? null;

  async function mutate(nextBusy: Exclude<Busy, null>, run: () => Promise<VaultResult>) {
    setBusy(nextBusy);
    setFormError(null);
    try {
      const result = await run();
      if (!result.ok) {
        setFormError(result.message);
        return;
      }
      if (result.snapshot) {
        setStatus({ phase: "ready", snapshot: result.snapshot, message: null });
      }
      if (nextBusy === "add") {
        setName("");
        setUsername("");
        setPassword("");
        setPhrase("");
      }
      if (nextBusy === "edit") setEditing(null);
      if (nextBusy === "delete") setDeleting(null);
    } finally {
      setBusy(null);
    }
  }

  function onSubmit(event: FormEvent) {
    event.preventDefault();
    const currentKind = editing?.type ?? kind;
    const fieldError = entryError(currentKind, name, username, password, phrase);
    if (fieldError) {
      setFormError(fieldError);
      return;
    }
    const command = entryCommand(
      editing ? "edit" : "add",
      currentKind,
      name,
      username,
      password,
      phrase,
      editing?.id,
    );
    void mutate(editing ? "edit" : "add", () =>
      chrome.runtime.sendMessage({ type: "command", command }),
    );
  }

  function startEdit(entry: VaultEntry) {
    setEditing(entry);
    setKind(entry.type);
    setName(entry.name);
    setUsername(entry.username);
    setPassword(entry.password);
    setPhrase(entry.phrase);
    setFormError(null);
    setDeleting(null);
  }

  function cancelEdit() {
    setEditing(null);
    setName("");
    setUsername("");
    setPassword("");
    setPhrase("");
    setFormError(null);
  }

  const formKind = editing?.type ?? kind;
  const typing = typingLine(snapshot);
  const locked = busy !== null || importLocked;

  return (
    <div>
      <div className="bar" />
      <div className="app">
        <h1>kChain</h1>
        <p className="muted">{status ? deviceLine(status) : "Connecting…"}</p>
        {typing && (
          <p className="muted">
            <span className={snapshot?.usbTyping ? "dot on" : "dot"} />
            {typing}
          </p>
        )}

        <div className="stack">
          <ImportChrome
            openToken={importNonce}
            deviceReady={status?.phase === "ready" && snapshot != null}
            entries={snapshot?.entries ?? []}
            onSnapshot={(next) => setStatus({ phase: "ready", snapshot: next, message: null })}
            onBusy={(next) => {
              importBusy.current = next;
              setImportLocked(next);
            }}
          />

          <EntryList
            snapshot={snapshot}
            revealed={revealed}
            deleting={deleting}
            busy={locked}
            onReveal={(id) => setRevealed((current) => ({ ...current, [id]: !current[id] }))}
            onEdit={startEdit}
            onAskDelete={setDeleting}
            onCancelDelete={() => setDeleting(null)}
            onDelete={(id) =>
              void mutate("delete", () =>
                chrome.runtime.sendMessage({ type: "command", command: { op: "delete", id } }),
              )
            }
          />

          <form className="card stack" onSubmit={onSubmit}>
            <h2>{editing ? "Edit entry" : "Add entry"}</h2>
            <div className="switch" role="group" aria-label="Entry type">
              {(["generic", "website", "crypto"] as const).map((option) => (
                <button
                  key={option}
                  type="button"
                  className="quiet"
                  aria-pressed={formKind === option}
                  disabled={locked || editing !== null}
                  onClick={() => setKind(option)}
                >
                  {typeLabel(option)}
                </button>
              ))}
            </div>
            <Field label={nameLabel(formKind)} value={name} onChange={setName} disabled={locked} />
            {formKind === "website" && (
              <Field label="Username" value={username} onChange={setUsername} disabled={locked} />
            )}
            {formKind !== "crypto" && (
              <Field
                label="Password"
                value={password}
                onChange={setPassword}
                disabled={locked}
                secret
                onGenerate={() => setPassword(generatePassword())}
              />
            )}
            {formKind === "crypto" && (
              <Field label="Phrase" value={phrase} onChange={setPhrase} disabled={locked} />
            )}
            {formError && <p className="error">{formError}</p>}
            <div className="actions">
              <button type="submit" className="button" disabled={locked || status?.phase !== "ready"}>
                {editing ? "Save changes" : "Add entry"}
              </button>
              {editing && (
                <button type="button" className="quiet" onClick={cancelEdit} disabled={locked}>
                  Cancel
                </button>
              )}
            </div>
          </form>
        </div>
      </div>
    </div>
  );
}

function EntryList({
  snapshot,
  revealed,
  deleting,
  busy,
  onReveal,
  onEdit,
  onAskDelete,
  onCancelDelete,
  onDelete,
}: {
  snapshot: VaultSnapshot | null;
  revealed: Record<number, boolean>;
  deleting: number | null;
  busy: boolean;
  onReveal: (id: number) => void;
  onEdit: (entry: VaultEntry) => void;
  onAskDelete: (id: number) => void;
  onCancelDelete: () => void;
  onDelete: (id: number) => void;
}) {
  if (!snapshot) return null;
  if (snapshot.entries.length === 0) {
    return (
      <section className="card">
        <p className="muted">The vault is empty.</p>
      </section>
    );
  }
  return (
    <section className="stack">
      {snapshot.entries.map((entry) => {
        const visible = revealed[entry.id] === true;
        return (
          <article key={entry.id} className="card">
            <div className="row">
              <strong>{entry.name}</strong>
              <span className="badge">{typeLabel(entry.type)}</span>
            </div>
            {entry.type === "website" && (
              <p className="secret">
                <span className="muted">Username </span>
                {visible ? entry.username : "••••••••"}
              </p>
            )}
            {entry.type !== "crypto" && (
              <p className="secret">
                <span className="muted">Password </span>
                {visible ? entry.password : "••••••••"}
              </p>
            )}
            {entry.type === "crypto" && (
              <p className="secret">{visible ? entry.phrase : "••••••••"}</p>
            )}
            <div className="actions">
              <button type="button" className="quiet" onClick={() => onReveal(entry.id)}>
                {visible ? "Hide" : "Show"}
              </button>
              <button type="button" className="quiet" onClick={() => onEdit(entry)} disabled={busy}>
                Edit
              </button>
              {deleting === entry.id ? (
                <>
                  <button type="button" className="button" onClick={() => onDelete(entry.id)} disabled={busy}>
                    Delete
                  </button>
                  <button type="button" className="quiet" onClick={onCancelDelete} disabled={busy}>
                    Cancel
                  </button>
                </>
              ) : (
                <button type="button" className="quiet" onClick={() => onAskDelete(entry.id)} disabled={busy}>
                  Delete
                </button>
              )}
            </div>
          </article>
        );
      })}
    </section>
  );
}

function Field({
  label,
  value,
  onChange,
  disabled,
  secret,
  onGenerate,
}: {
  label: string;
  value: string;
  onChange: (value: string) => void;
  disabled: boolean;
  secret?: boolean;
  onGenerate?: () => void;
}) {
  return (
    <div>
      <label>
        {label}
        <input
          value={value}
          type={secret ? "password" : "text"}
          autoComplete="off"
          disabled={disabled}
          onChange={(event) => onChange(event.target.value)}
        />
      </label>
      {onGenerate && (
        <span className="actions">
          <button type="button" className="quiet" onClick={onGenerate} disabled={disabled}>
            Generate password
          </button>
        </span>
      )}
    </div>
  );
}
