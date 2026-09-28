import { useEffect, useState } from "react";
import type { VaultEntry } from "@shared/protocol";
import { FOCUS_IMPORT_KEY, type MatchResult, type StatusResponse } from "../messages";
import { deviceLine, pageHost, typingLine } from "../status";

export function Popup() {
  const [status, setStatus] = useState<StatusResponse | null>(null);
  const [matches, setMatches] = useState<MatchResult | null>(null);
  const [host, setHost] = useState<string | null>(null);
  const [tabId, setTabId] = useState<number | null>(null);
  const [notice, setNotice] = useState<string | null>(null);

  useEffect(() => {
    void load();
  }, []);

  async function load() {
    const [tab] = await chrome.tabs.query({ active: true, currentWindow: true });
    const nextHost = pageHost(tab?.url);
    setTabId(tab?.id ?? null);
    setHost(nextHost);
    const nextStatus = (await chrome.runtime.sendMessage({ type: "status" })) as StatusResponse;
    setStatus(nextStatus);
    if (!nextHost) {
      setMatches({ ok: false, message: "Open a website to see its logins." });
      return;
    }
    const nextMatches = (await chrome.runtime.sendMessage({
      type: "matches",
      host: nextHost,
    })) as MatchResult;
    setMatches(nextMatches);
  }

  async function openVault() {
    if (tabId == null) return;
    await chrome.sidePanel.open({ tabId });
    window.close();
  }

  async function importChrome() {
    await chrome.storage.session.set({ [FOCUS_IMPORT_KEY]: true });
    if (tabId == null) return;
    await chrome.sidePanel.open({ tabId });
    window.close();
  }

  async function fill(entry: VaultEntry) {
    if (tabId == null) return;
    setNotice(null);
    try {
      const response = (await chrome.tabs.sendMessage(tabId, {
        type: "fill",
        username: entry.username,
        password: entry.password,
      })) as { filled?: boolean } | undefined;
      if (!response?.filled) {
        setNotice("Click the key icon on the login field.");
        return;
      }
      window.close();
    } catch {
      setNotice("Reload the page, then try again.");
    }
  }

  const typing = typingLine(status?.snapshot ?? null);

  return (
    <div>
      <div className="bar" />
      <div className="app">
        <h1>kChain</h1>
        <p className="muted">{status ? deviceLine(status) : "Connecting…"}</p>
        {typing && (
          <p className="muted">
            <span className={status?.snapshot?.usbTyping ? "dot on" : "dot"} />
            {typing}
          </p>
        )}
        <div className="stack">
          <section className="card">
            <h2>{host ?? "This site"}</h2>
            {matches?.ok && matches.entries.length === 0 && (
              <p className="muted">No logins for this site.</p>
            )}
            {matches && !matches.ok && <p className="muted">{matches.message}</p>}
            {matches?.ok &&
              matches.entries.map((entry) => (
                <button key={entry.id} type="button" className="login" onClick={() => void fill(entry)}>
                  <strong>{entry.username}</strong>
                  <span className="muted">{entry.name}</span>
                </button>
              ))}
            {notice && <p className="error">{notice}</p>}
          </section>
          <button type="button" className="button" onClick={() => void openVault()}>
            Open vault
          </button>
          <button type="button" className="quiet" onClick={() => void importChrome()}>
            Import from Chrome
          </button>
        </div>
      </div>
    </div>
  );
}
