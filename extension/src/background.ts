import { hostMatches } from "@shared/hosts";
import { entryForPage } from "@shared/page-entry";
import {
  snapshotFrom,
  type DeviceCommand,
  type DeviceResponse,
  type VaultEntry,
  type VaultSnapshot,
} from "@shared/protocol";
import {
  messageOf,
  type ClassifyResult,
  type ExtensionRequest,
  type MatchResult,
  type PendingSave,
  type StatusResponse,
  type TyperPhase,
  type VaultResult,
} from "./messages";
import { pageHost } from "./status";

const TYPER_URL = "http://127.0.0.1:4318";
const NATIVE_HOST = "com.kchain.usb";
const IGNORED_KEY = "ignoredHosts";
const PENDING_KEY = "pendingByTab";

let ensuring: Promise<void> | null = null;

chrome.runtime.onInstalled.addListener(() => {
  void chrome.sidePanel.setPanelBehavior({ openPanelOnActionClick: false }).catch(() => {});
  void ensureTyper();
  void refreshActiveLogin();
});

chrome.runtime.onStartup.addListener(() => {
  void ensureTyper();
  void refreshActiveLogin();
});

chrome.tabs.onActivated.addListener(() => {
  void refreshActiveLogin();
});

chrome.tabs.onRemoved.addListener((tabId) => {
  loginsByTab.delete(tabId);
  pokeFocus();
});

chrome.tabs.onUpdated.addListener((tabId, info) => {
  if (info.status === "loading" || info.url) {
    loginsByTab.delete(tabId);
    pokeFocus();
  }
  if (info.status === "complete" || info.url) void requestScan(tabId);
});

chrome.windows.onFocusChanged.addListener((windowId) => {
  if (windowId === chrome.windows.WINDOW_ID_NONE) return;
  void refreshActiveLogin();
});

chrome.runtime.onMessage.addListener((message: ExtensionRequest, sender, sendResponse) => {
  void handle(message, sender)
    .then(sendResponse)
    .catch((error: unknown) => {
      sendResponse({ ok: false, message: messageOf(error) });
    });
  return true;
});

async function handle(
  message: ExtensionRequest,
  sender: chrome.runtime.MessageSender,
): Promise<unknown> {
  switch (message.type) {
    case "status":
      if (!fromExtensionPage(sender)) {
        return { phase: "down", snapshot: null, message: "Not available on this page." };
      }
      return readStatus();
    case "matches":
      return matchesFor(hostForMatches(message.host, sender));
    case "command":
      if (!commandAllowed(message.command, sender)) {
        return { ok: false, message: "This page cannot change that entry." };
      }
      return runCommand(message.command, fromExtensionPage(sender));
    case "classify": {
      const host = frameHost(sender);
      if (!host) return { action: "unavailable", message: "This page has no hostname." };
      return classify(host, message.username, message.password);
    }
    case "pending-get": {
      const pending = await pendingFor(sender.tab?.id);
      const host = frameHost(sender);
      if (!pending || !host || !hostMatches(host, pending.host)) return null;
      return pending;
    }
    case "pending-set": {
      const host = frameHost(sender);
      if (!host || !hostMatches(host, message.pending.host)) return { ok: false };
      await setPending(sender.tab?.id, message.pending);
      return { ok: true };
    }
    case "pending-clear":
      await clearPending(sender.tab?.id);
      return { ok: true };
    case "ignore-host": {
      const host = frameHost(sender);
      if (host) await ignoreHost(host);
      if (message.host && host && hostMatches(host, message.host)) await ignoreHost(message.host);
      await clearPending(sender.tab?.id);
      return { ok: true };
    }
    case "open-vault": {
      const tabId = sender.tab?.id;
      if (tabId == null) return { ok: false, message: "Open kChain from the toolbar." };
      void chrome.sidePanel.open({ tabId });
      return { ok: true };
    }
    case "page-login": {
      const tabId = sender.tab?.id;
      const host = frameHost(sender);
      if (tabId == null || !host) return { ok: false };
      rememberLogin(tabId, sender.frameId ?? 0, {
        host,
        present: message.present,
        username: message.username.slice(0, 128),
      });
      return { ok: true };
    }
    default:
      return { ok: false, message: "Unknown request." };
  }
}

function fromExtensionPage(sender: chrome.runtime.MessageSender): boolean {
  return (sender.url ?? "").startsWith(`chrome-extension://${chrome.runtime.id}/`);
}

function commandAllowed(command: DeviceCommand, sender: chrome.runtime.MessageSender): boolean {
  if (fromExtensionPage(sender)) return true;
  const host = frameHost(sender);
  if (!host) return false;
  if (command.op === "add" && command.type === "website") return hostMatches(host, command.name);
  if (command.op === "edit" && command.type === "website") return hostMatches(host, command.name);
  return false;
}

function frameHost(sender: chrome.runtime.MessageSender): string | null {
  return hostnameOf(sender.url);
}

function hostForMatches(
  requested: string | undefined,
  sender: chrome.runtime.MessageSender,
): string | null {
  if ((sender.url ?? "").startsWith("chrome-extension://")) {
    return requested ? hostnameOf(requested) ?? requested : null;
  }
  return frameHost(sender);
}

function hostnameOf(value: string | undefined): string | null {
  if (!value) return null;
  try {
    if (value.includes("://")) return new URL(value).hostname;
  } catch {
    return null;
  }
  return value.replace(/^\[|\]$/g, "") || null;
}

async function readStatus(): Promise<StatusResponse> {
  let phase = await typerPhase();
  if (phase === "down") {
    await ensureTyper();
    phase = await typerPhase();
  }
  if (phase === "down") {
    return {
      phase,
      snapshot: null,
      message: "The USB typer is not running. Run npm start in host/, or open the web editor.",
    };
  }
  if (phase === "waiting") {
    return {
      phase,
      snapshot: null,
      message: "Plug in the device. This connects when it shows up.",
    };
  }
  try {
    const snapshot = await readVault();
    return { phase: "ready", snapshot, message: null };
  } catch (error) {
    return { phase: "waiting", snapshot: null, message: messageOf(error) };
  }
}

async function matchesFor(host: string | null): Promise<MatchResult> {
  if (!host) return { ok: false, message: "Open a website to see its logins." };
  try {
    const snapshot = await readVault();
    const entries = snapshot.entries.filter(
      (entry) => entry.type === "website" && hostMatches(host, entry.name),
    );
    return { ok: true, entries };
  } catch (error) {
    return { ok: false, message: messageOf(error) };
  }
}

async function runCommand(command: DeviceCommand, includeSnapshot: boolean): Promise<VaultResult> {
  try {
    const snapshot = snapshotFrom(await deviceCommand(command));
    return includeSnapshot ? { ok: true, snapshot } : { ok: true };
  } catch (error) {
    return { ok: false, message: messageOf(error) };
  }
}

async function classify(
  host: string,
  username: string,
  password: string,
): Promise<ClassifyResult> {
  if (!username || !password) return { action: "none" };
  if (await isIgnored(host)) return { action: "ignored" };
  let entries: VaultEntry[];
  try {
    entries = (await readVault()).entries;
  } catch (error) {
    return { action: "unavailable", message: messageOf(error) };
  }
  const existing = entries.find(
    (entry) =>
      entry.type === "website" &&
      hostMatches(host, entry.name) &&
      entry.username.toLowerCase() === username.toLowerCase(),
  );
  if (!existing) return { action: "save" };
  if (existing.password !== password) return { action: "update", id: existing.id };
  return { action: "none" };
}

async function readVault(): Promise<VaultSnapshot> {
  return snapshotFrom(await deviceCommand({ op: "list" }));
}

async function deviceCommand(command: DeviceCommand): Promise<DeviceResponse> {
  let response: Response;
  try {
    response = await fetch(`${TYPER_URL}/command`, {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify(command),
    });
  } catch {
    throw new Error("The USB typer is not running.");
  }
  if (response.status === 503) throw new Error("Plug in the device.");
  if (!response.ok) {
    const text = (await response.text()).replace(/\s+/g, " ").trim();
    throw new Error(text || "The device did not answer.");
  }
  const body: unknown = await response.json();
  if (!body || typeof body !== "object") {
    throw new Error("The device sent an unexpected reply.");
  }
  const record = body as DeviceResponse;
  if (typeof record.ok !== "boolean" || typeof record.op !== "string") {
    throw new Error("The device sent an unexpected reply.");
  }
  if (!record.ok) throw new Error(record.error || "The device rejected the command.");
  return record;
}

async function typerPhase(): Promise<TyperPhase> {
  try {
    const response = await fetch(`${TYPER_URL}/health`, { cache: "no-store" });
    if (!response.ok) return "down";
    const body: unknown = await response.json();
    if (!body || typeof body !== "object") return "waiting";
    return (body as { device?: unknown }).device === true ? "ready" : "waiting";
  } catch {
    return "down";
  }
}

function ensureTyper(): Promise<void> {
  if (ensuring) return ensuring;
  let port: chrome.runtime.Port | null = null;
  const pending = new Promise<void>((resolve) => {
    try {
      port = chrome.runtime.connectNative(NATIVE_HOST);
    } catch {
      resolve();
      return;
    }
    let settled = false;
    const finish = () => {
      if (settled) return;
      settled = true;
      clearTimeout(timer);
      try {
        port?.disconnect();
      } catch {
        // The host already closed the port.
      }
      resolve();
    };
    const timer = setTimeout(finish, 8000);
    port.onMessage.addListener(() => finish());
    port.onDisconnect.addListener(() => finish());
    try {
      port.postMessage({ op: "ensure" });
    } catch {
      finish();
    }
  }).finally(() => {
    if (ensuring === pending) ensuring = null;
  });
  ensuring = pending;
  return pending;
}

async function ignoredHosts(): Promise<string[]> {
  const stored = await chrome.storage.local.get(IGNORED_KEY);
  const value = stored[IGNORED_KEY];
  if (!Array.isArray(value)) return [];
  return value.filter((item): item is string => typeof item === "string");
}

async function isIgnored(host: string): Promise<boolean> {
  const hosts = await ignoredHosts();
  return hosts.some((item) => hostMatches(host, item));
}

async function ignoreHost(host: string): Promise<void> {
  const hosts = await ignoredHosts();
  const normalized = host.toLowerCase();
  if (hosts.some((item) => item.toLowerCase() === normalized)) return;
  await chrome.storage.local.set({ [IGNORED_KEY]: [...hosts, normalized] });
}

async function pendingMap(): Promise<Record<string, PendingSave>> {
  const stored = await chrome.storage.session.get(PENDING_KEY);
  const value = stored[PENDING_KEY];
  if (!value || typeof value !== "object") return {};
  return value as Record<string, PendingSave>;
}

function tabKey(tabId: number | undefined): string | null {
  return tabId == null ? null : String(tabId);
}

async function pendingFor(tabId: number | undefined): Promise<PendingSave | null> {
  const key = tabKey(tabId);
  if (!key) return null;
  const pending = (await pendingMap())[key];
  if (!pending || typeof pending.host !== "string") return null;
  if (typeof pending.username !== "string" || typeof pending.password !== "string") return null;
  if (pending.kind !== "save" && pending.kind !== "update") return null;
  return pending;
}

async function setPending(tabId: number | undefined, pending: PendingSave): Promise<void> {
  const key = tabKey(tabId);
  if (!key) return;
  const map = await pendingMap();
  map[key] = pending;
  await chrome.storage.session.set({ [PENDING_KEY]: map });
}

async function clearPending(tabId: number | undefined): Promise<void> {
  const key = tabKey(tabId);
  if (!key) return;
  const map = await pendingMap();
  delete map[key];
  await chrome.storage.session.set({ [PENDING_KEY]: map });
}

type FrameLogin = { host: string; present: boolean; username: string };

const loginsByTab = new Map<number, Map<number, FrameLogin>>();
let sentFocusId: number | null | undefined;
let focusGeneration = 0;
let focusTimer: ReturnType<typeof setTimeout> | null = null;
let focusRetry: ReturnType<typeof setTimeout> | null = null;
let retryArmed = true;
let deviceEvents: EventSource | null = null;
let deviceEventRetry: ReturnType<typeof setTimeout> | null = null;

function rememberLogin(tabId: number, frameId: number, login: FrameLogin) {
  let frames = loginsByTab.get(tabId);
  if (!frames) {
    frames = new Map();
    loginsByTab.set(tabId, frames);
  }
  const previous = frames.get(frameId);
  frames.set(frameId, login);
  if (
    previous &&
    previous.host === login.host &&
    previous.present === login.present &&
    previous.username === login.username
  ) {
    return;
  }
  pokeFocus();
}

function pokeFocus() {
  retryArmed = true;
  scheduleFocus();
}

function scheduleFocus() {
  watchDevice();
  if (focusTimer) clearTimeout(focusTimer);
  focusTimer = setTimeout(() => {
    focusTimer = null;
    void pushFocus();
  }, 200);
}

function armFocusRetry() {
  if (!retryArmed || focusRetry) return;
  retryArmed = false;
  focusRetry = setTimeout(() => {
    focusRetry = null;
    scheduleFocus();
  }, 2000);
}

async function pushFocus() {
  const generation = ++focusGeneration;
  let id: number | null;
  try {
    id = await desiredFocusId();
  } catch {
    armFocusRetry();
    return;
  }
  if (generation !== focusGeneration || id === sentFocusId) return;
  try {
    await deviceCommand({ op: "focus", id });
  } catch {
    armFocusRetry();
    return;
  }
  if (generation !== focusGeneration) return;
  sentFocusId = id;
}

async function desiredFocusId(): Promise<number | null> {
  const [tab] = await chrome.tabs.query({ active: true, lastFocusedWindow: true });
  const host = pageHost(tab?.url);
  if (!host || tab?.id == null) return null;
  const frames = loginsByTab.get(tab.id);
  if (!frames) return null;
  let present = false;
  let username = "";
  for (const frame of frames.values()) {
    if (!frame.present || !hostMatches(host, frame.host)) continue;
    present = true;
    if (!username && frame.username) username = frame.username;
  }
  if (!present) return null;
  return entryForPage((await readVault()).entries, host, username)?.id ?? null;
}

async function requestScan(tabId: number) {
  try {
    await chrome.tabs.sendMessage(tabId, { type: "login-scan" });
  } catch {
    // This page has no content script.
  }
}

async function refreshActiveLogin() {
  const [tab] = await chrome.tabs.query({ active: true, lastFocusedWindow: true });
  if (tab?.id != null) await requestScan(tab.id);
  pokeFocus();
}

function watchDevice() {
  if (deviceEvents || deviceEventRetry) return;
  let source: EventSource;
  try {
    source = new EventSource(`${TYPER_URL}/events`);
  } catch {
    deviceEventRetry = setTimeout(() => {
      deviceEventRetry = null;
      watchDevice();
    }, 10000);
    return;
  }
  deviceEvents = source;
  source.onmessage = (message) => {
    let parsed: unknown;
    try {
      parsed = JSON.parse(message.data);
    } catch {
      return;
    }
    if (!parsed || typeof parsed !== "object" || (parsed as { event?: unknown }).event !== "ready") {
      return;
    }
    sentFocusId = undefined;
    pokeFocus();
  };
  source.onerror = () => {
    source.close();
    if (deviceEvents === source) deviceEvents = null;
    if (deviceEventRetry) return;
    deviceEventRetry = setTimeout(() => {
      deviceEventRetry = null;
      watchDevice();
    }, 10000);
  };
}

watchDevice();
void refreshActiveLogin();
