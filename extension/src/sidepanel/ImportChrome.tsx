import { useEffect, useMemo, useRef, useState } from "react";
import {
  CHROME_PASSWORD_SETTINGS,
  importActionKey,
  parseChromePasswordCsv,
  planChromeImport,
  type ChromeImportAction,
  type ChromeLogin,
  type ChromeSkip,
} from "@shared/chrome-csv";
import { LIMITS, type VaultEntry, type VaultSnapshot } from "@shared/protocol";
import type { VaultResult } from "../messages";

const FILE_LIMIT = 2_000_000;

type Report = {
  name: string;
  logins: ChromeLogin[];
  skipped: ChromeSkip[];
  repeated: number;
  notes: number;
};

type Outcome = {
  added: number;
  updated: number;
  error: string | null;
};

export function ImportChrome({
  openToken,
  deviceReady,
  entries,
  onSnapshot,
  onBusy,
}: {
  openToken: number;
  deviceReady: boolean;
  entries: VaultEntry[];
  onSnapshot: (snapshot: VaultSnapshot) => void;
  onBusy: (busy: boolean) => void;
}) {
  const rootRef = useRef<HTMLElement>(null);
  const fileRef = useRef<HTMLInputElement>(null);
  const [open, setOpen] = useState(false);
  const [hint, setHint] = useState<string | null>(null);
  const [fileError, setFileError] = useState<string | null>(null);
  const [report, setReport] = useState<Report | null>(null);
  const [checked, setChecked] = useState<Set<string>>(new Set());
  const entriesRef = useRef(entries);
  const checkedRef = useRef(checked);
  const seenRef = useRef(new Set<string>());
  entriesRef.current = entries;
  checkedRef.current = checked;
  const [query, setQuery] = useState("");
  const [importing, setImporting] = useState(false);
  const [progress, setProgress] = useState<string | null>(null);
  const [outcome, setOutcome] = useState<Outcome | null>(null);

  useEffect(() => {
    if (openToken === 0) return;
    setOpen(true);
    requestAnimationFrame(() => rootRef.current?.scrollIntoView({ block: "start" }));
  }, [openToken]);

  const entryStamp = [...entries]
    .sort((left, right) => left.id - right.id)
    .map((entry) => [entry.id, entry.type, entry.name, entry.username, entry.password].join("\u0000"))
    .join("\n");

  const plan = useMemo(() => {
    if (!report) return null;
    return planChromeImport(report.logins, entries);
  }, [report, entries]);

  useEffect(() => {
    if (!report || importing) return;
    const current = entriesRef.current;
    const next = planChromeImport(report.logins, current);
    const free = Math.max(0, LIMITS.entryMax - current.length);
    const existing = checkedRef.current;
    const seen = seenRef.current;
    const chosen = new Set<string>();
    let adds = 0;
    for (const action of next.actions) {
      const key = importActionKey(action);
      const want = seen.has(key) ? existing.has(key) : true;
      seen.add(key);
      if (!want) continue;
      if (action.kind === "add") {
        if (adds >= free) continue;
        adds += 1;
      }
      chosen.add(key);
    }
    setChecked(chosen);
  }, [entryStamp, importing, report]);

  const freeSlots = Math.max(0, LIMITS.entryMax - entries.length);
  const selected = plan?.actions.filter((action) => checked.has(importActionKey(action))) ?? [];
  const selectedAdds = selected.filter((action) => action.kind === "add").length;
  const selectedUpdates = selected.length - selectedAdds;
  const overCapacity = selectedAdds > freeSlots;
  const needle = query.trim().toLowerCase();
  const visible =
    plan?.actions
      .map((action, index) => ({ action, index }))
      .filter(({ action }) => {
        if (!needle) return true;
        return (
          action.hostname.toLowerCase().includes(needle) ||
          action.username.toLowerCase().includes(needle)
        );
      }) ?? [];

  async function openSettings() {
    setHint(null);
    try {
      await chrome.tabs.create({ url: CHROME_PASSWORD_SETTINGS });
      setHint("Password settings are open. Beside Export passwords, choose Download file.");
    } catch {
      try {
        await navigator.clipboard.writeText(CHROME_PASSWORD_SETTINGS);
        setHint("Chrome blocked that page. The address is copied — paste it in the address bar.");
      } catch {
        setHint(`Paste ${CHROME_PASSWORD_SETTINGS} in the address bar.`);
      }
    }
  }

  async function onFile(file: File | undefined) {
    if (!file) return;
    setOutcome(null);
    setHint(null);
    setQuery("");
    if (file.size > FILE_LIMIT) {
      setReport(null);
      setFileError("That file is too large to be a Chrome password export.");
      return;
    }
    const parsed = parseChromePasswordCsv(await file.text());
    if (!parsed.ok) {
      setReport(null);
      setFileError(parsed.message);
      return;
    }
    const nextPlan = planChromeImport(parsed.result.logins, entries);
    setFileError(null);
    seenRef.current = new Set();
    setReport({ name: file.name, ...parsed.result });
    setChecked(seedChecks(nextPlan.actions, Math.max(0, LIMITS.entryMax - entries.length)));
  }

  function toggle(action: ChromeImportAction) {
    const key = importActionKey(action);
    setChecked((current) => {
      const next = new Set(current);
      if (next.has(key)) {
        next.delete(key);
        return next;
      }
      if (action.kind === "add") {
        const adds = plan?.actions.filter(
          (item) => item.kind === "add" && next.has(importActionKey(item)),
        ).length;
        if ((adds ?? 0) >= freeSlots) return current;
      }
      next.add(key);
      return next;
    });
  }

  async function runImport() {
    if (!plan || selected.length === 0 || overCapacity || !deviceReady) return;
    const actions = plan.actions.filter((action) => checked.has(importActionKey(action)));
    setImporting(true);
    onBusy(true);
    setOutcome(null);
    let added = 0;
    let updated = 0;
    try {
      for (let index = 0; index < actions.length; index++) {
        const action = actions[index];
        setProgress(`Saving ${index + 1} of ${actions.length}…`);
        const result = (await chrome.runtime.sendMessage({
          type: "command",
          command:
            action.kind === "add"
              ? {
                  op: "add",
                  type: "website",
                  name: action.hostname,
                  username: action.username,
                  password: action.password,
                }
              : {
                  op: "edit",
                  id: action.id,
                  type: "website",
                  name: action.hostname,
                  username: action.username,
                  password: action.password,
                },
        })) as VaultResult;
        if (!result.ok) {
          setReport(null);
          setOutcome({ added, updated, error: result.message });
          return;
        }
        if (action.kind === "add") added += 1;
        else updated += 1;
        if (result.snapshot) onSnapshot(result.snapshot);
      }
      setReport(null);
      setOutcome({ added, updated, error: null });
    } finally {
      setProgress(null);
      setImporting(false);
      onBusy(false);
      if (fileRef.current) fileRef.current.value = "";
    }
  }

  return (
    <section className="card stack" ref={rootRef}>
      <div className="row">
        <h2>Import from Chrome</h2>
        <button type="button" className="quiet" onClick={() => setOpen((current) => !current)}>
          {open ? "Hide" : "Show"}
        </button>
      </div>
      {open && (
        <>
          <p className="muted">
            Chrome keeps saved passwords private from extensions. Export a file, then import it here.
          </p>
          <ol className="steps">
            <li>
              Open <span className="address">{CHROME_PASSWORD_SETTINGS}</span>
            </li>
            <li>Beside Export passwords, choose Download file and confirm with your computer password.</li>
            <li>Choose that CSV below. Delete the file when you are done. It is plain text.</li>
          </ol>
          <div className="actions">
            <button type="button" className="button" onClick={() => void openSettings()} disabled={importing}>
              Open password settings
            </button>
            <button
              type="button"
              className="quiet"
              onClick={() => fileRef.current?.click()}
              disabled={importing}
            >
              Choose CSV
            </button>
          </div>
          <input
            ref={fileRef}
            type="file"
            accept=".csv,text/csv,text/plain"
            hidden
            onChange={(event) => {
              const file = event.target.files?.[0];
              event.target.value = "";
              void onFile(file);
            }}
          />
          {hint && <p className="muted">{hint}</p>}
          {fileError && <p className="error">{fileError}</p>}
          {outcome && <OutcomeNote outcome={outcome} />}
          {report && plan && (
            <Review
              report={report}
              plan={plan}
              visible={visible}
              checked={checked}
              freeSlots={freeSlots}
              selectedAdds={selectedAdds}
              selectedUpdates={selectedUpdates}
              overCapacity={overCapacity}
              query={query}
              deviceReady={deviceReady}
              importing={importing}
              progress={progress}
              onQuery={setQuery}
              onToggle={toggle}
              onImport={() => void runImport()}
            />
          )}
        </>
      )}
    </section>
  );
}

function Review({
  report,
  plan,
  visible,
  checked,
  freeSlots,
  selectedAdds,
  selectedUpdates,
  overCapacity,
  query,
  deviceReady,
  importing,
  progress,
  onQuery,
  onToggle,
  onImport,
}: {
  report: Report;
  plan: { actions: ChromeImportAction[]; alreadySaved: number };
  visible: Array<{ action: ChromeImportAction; index: number }>;
  checked: Set<string>;
  freeSlots: number;
  selectedAdds: number;
  selectedUpdates: number;
  overCapacity: boolean;
  query: string;
  deviceReady: boolean;
  importing: boolean;
  progress: string | null;
  onQuery: (value: string) => void;
  onToggle: (action: ChromeImportAction) => void;
  onImport: () => void;
}) {
  const readyCount = selectedAdds + selectedUpdates;
  return (
    <div className="import-body">
      <p className="muted">
        {report.name}: {report.logins.length} website {report.logins.length === 1 ? "login" : "logins"}.
        The device holds {LIMITS.entryMax} entries, with {freeSlots} free.
      </p>
      {report.repeated > 0 && (
        <p className="muted">
          {report.repeated} extra {report.repeated === 1 ? "row repeats" : "rows repeat"} a login.
          The last password in the file is kept.
        </p>
      )}
      {plan.alreadySaved > 0 && (
        <p className="muted">
          {plan.alreadySaved} already {plan.alreadySaved === 1 ? "matches" : "match"} the device.
        </p>
      )}
      {report.notes > 0 && <p className="muted">Notes in the file are not stored on the device.</p>}
      {report.skipped.length > 0 && (
        <ul className="skips">
          {report.skipped.map((skip) => (
            <li key={skip.reason}>
              {skip.count} skipped: {skip.reason}
            </li>
          ))}
        </ul>
      )}
      {importing && progress && <p className="muted">{progress}</p>}
      {!importing && plan.actions.length > 0 && (
        <>
          <p className="muted">
            {selectedAdds} new, {selectedUpdates} {selectedUpdates === 1 ? "update" : "updates"}.
            {freeSlots === 0
              ? " The vault is full, so only password updates fit."
              : " New logins use a free slot."}
          </p>
          {plan.actions.length > 8 && (
            <label>
              Filter
              <input
                value={query}
                autoComplete="off"
                disabled={importing}
                onChange={(event) => onQuery(event.target.value)}
              />
            </label>
          )}
          {visible.length === 0 && query.trim().length > 0 && (
            <p className="muted">No logins match that filter.</p>
          )}
          <div className="import-list">
            {visible.map(({ action }) => {
              const key = importActionKey(action);
              const isChecked = checked.has(key);
              const blocked = action.kind === "add" && !isChecked && selectedAdds >= freeSlots;
              return (
                <label className="choice" key={key}>
                  <input
                    type="checkbox"
                    checked={isChecked}
                    disabled={blocked}
                    onChange={() => onToggle(action)}
                  />
                  <span>
                    <strong>{action.hostname}</strong>
                    <span className="muted"> {action.username}</span>
                    {action.kind === "update" && <span className="badge"> New password</span>}
                  </span>
                </label>
              );
            })}
          </div>
          {overCapacity && (
            <p className="error">
              Choose at most {freeSlots} new {freeSlots === 1 ? "login" : "logins"}.
            </p>
          )}
          {!deviceReady && <p className="error">Connect the device before importing.</p>}
          <button
            type="button"
            className="button"
            disabled={!deviceReady || readyCount === 0 || overCapacity}
            onClick={onImport}
          >
            Import {readyCount}
          </button>
        </>
      )}
      {!importing && plan.actions.length === 0 && <p className="muted">Nothing new to import.</p>}
    </div>
  );
}

function OutcomeNote({ outcome }: { outcome: Outcome }) {
  const saved = describe(outcome.added, outcome.updated);
  if (outcome.error) {
    return (
      <p className="error">
        {saved ? `${saved} Then the device stopped: ${outcome.error}` : outcome.error} Choose the CSV
        again to retry what is left.
      </p>
    );
  }
  return (
    <p className="muted">
      {saved} Delete the CSV from your computer. Chrome exports passwords without encryption.
    </p>
  );
}

function describe(added: number, updated: number): string {
  if (added > 0 && updated > 0) {
    return `Added ${added} ${added === 1 ? "login" : "logins"} and updated ${updated} ${updated === 1 ? "password" : "passwords"}.`;
  }
  if (added > 0) return `Added ${added} ${added === 1 ? "login" : "logins"}.`;
  if (updated > 0) return `Updated ${updated} ${updated === 1 ? "password" : "passwords"}.`;
  return "";
}

function seedChecks(actions: ChromeImportAction[], freeSlots: number): Set<string> {
  const checked = new Set<string>();
  let adds = 0;
  for (const action of actions) {
    if (action.kind === "update") {
      checked.add(importActionKey(action));
      continue;
    }
    if (adds >= freeSlots) continue;
    checked.add(importActionKey(action));
    adds += 1;
  }
  return checked;
}
