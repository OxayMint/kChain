import { generatePassword } from "@shared/generate-password";
import { hostMatches } from "@shared/hosts";
import { validateHostname, validatePassword, validateUsername } from "@shared/protocol";
import {
  isPasswordField,
  isUsernameField,
  loginPage,
  pairFor,
  pairInForm,
  pairToFill,
  setNativeValue,
  type CredentialPair,
} from "./fields";
import { createPageUi, type BannerState } from "./ui";
import type { ClassifyResult, MatchResult, PendingSave, VaultResult } from "../messages";

const ui = createPageUi(document, {
  onIcon: () => {
    if (ui.menuOpen()) closeMenu();
    else void openMenu();
  },
  onFill: (username, password) => fillLogin(username, password),
  onGenerate: () => fillGenerated(),
  onManage: () => {
    void chrome.runtime.sendMessage({ type: "open-vault" });
  },
  onSave: () => void saveBanner(),
  onDismiss: () => void dismissBanner(),
  onNever: () => void neverBanner(),
});

let anchored: HTMLInputElement | null = null;
let pair: CredentialPair | null = null;
let banner: PendingSave | null = null;
let menuToken = 0;
let lastCapture = "";
let lastCaptureAt = 0;

document.addEventListener(
  "focusin",
  (event) => {
    if (event.target === ui.host) return;
    if (event.target instanceof HTMLInputElement) {
      const next = pairFor(event.target);
      if (next) {
        focusField(event.target, next);
        return;
      }
    }
    anchored = null;
    pair = null;
    ui.hideIcon();
    closeMenu();
  },
  true,
);

document.addEventListener(
  "mousedown",
  (event) => {
    if (event.target !== ui.host) closeMenu();
  },
  true,
);

document.addEventListener(
  "keydown",
  (event) => {
    if (event.key === "Escape") closeMenu();
    if (event.key !== "Enter" || !(event.target instanceof HTMLInputElement)) return;
    const next = pairFor(event.target);
    if (next) void considerSave(next);
  },
  true,
);

document.addEventListener(
  "submit",
  (event) => {
    if (!(event.target instanceof HTMLFormElement)) return;
    const next = pairInForm(event.target);
    if (next) void considerSave(next);
  },
  true,
);

document.addEventListener(
  "click",
  (event) => {
    if (!(event.target instanceof Element)) return;
    const control = event.target.closest("button, input");
    if (!control || !isSubmitControl(control)) return;
    const form =
      control instanceof HTMLButtonElement || control instanceof HTMLInputElement
        ? control.form
        : null;
    const next = form ? pairInForm(form) : pairToFill(document);
    if (next) void considerSave(next);
  },
  true,
);

document.addEventListener("scroll", reposition, true);
window.addEventListener("resize", reposition);

new MutationObserver(() => {
  if (anchored && !anchored.isConnected) {
    anchored = null;
    pair = null;
    ui.hideIcon();
    closeMenu();
  }
  scheduleLoginReport(false);
}).observe(document.documentElement, { childList: true, subtree: true });

document.addEventListener(
  "input",
  (event) => {
    if (!(event.target instanceof HTMLInputElement) || !isUsernameField(event.target)) return;
    scheduleLoginReport(false);
  },
  true,
);

let loginTimer = 0;
let loginReport = "";

function scheduleLoginReport(force: boolean) {
  window.clearTimeout(loginTimer);
  loginTimer = window.setTimeout(() => {
    const page = loginPage(document);
    const key = `${page.present}\n${page.username}`;
    if (!force && key === loginReport) return;
    loginReport = key;
    void chrome.runtime.sendMessage({
      type: "page-login",
      present: page.present,
      username: page.username,
    });
  }, force ? 0 : 200);
}

scheduleLoginReport(true);

chrome.runtime.onMessage.addListener((message, _sender, sendResponse) => {
  if (!message) return;
  if (message.type === "login-scan") {
    scheduleLoginReport(true);
    return;
  }
  if (message.type !== "fill") return;
  const next = pair ?? pairToFill(document);
  if (!next) {
    sendResponse({ filled: false });
    return;
  }
  pair = next;
  fillLogin(message.username, message.password);
  sendResponse({ filled: true });
});

void restoreBanner();

function focusField(input: HTMLInputElement, next: CredentialPair) {
  const changed = anchored !== input;
  anchored = input;
  pair = next;
  ui.showIcon(input.getBoundingClientRect());
  if (changed) closeMenu();
}

async function openMenu() {
  if (!anchored || !pair) return;
  const token = ++menuToken;
  const showGenerate = isPasswordField(anchored);
  ui.showMenu(anchored.getBoundingClientRect(), {
    loading: true,
    message: null,
    entries: [],
    showGenerate,
  });
  const result = (await chrome.runtime.sendMessage({ type: "matches" })) as MatchResult;
  if (token !== menuToken || !ui.menuOpen() || !anchored) return;
  if (!result.ok) {
    ui.showMenu(anchored.getBoundingClientRect(), {
      loading: false,
      message: result.message,
      entries: [],
      showGenerate,
    });
    return;
  }
  ui.showMenu(anchored.getBoundingClientRect(), {
    loading: false,
    message: result.entries.length === 0 ? "No logins for this site." : null,
    entries: result.entries,
    showGenerate,
  });
}

function closeMenu() {
  menuToken += 1;
  ui.hideMenu();
}

function fillLogin(username: string, password: string) {
  if (!pair) return;
  if (pair.username) setNativeValue(pair.username, username);
  setNativeValue(pair.password, password);
  closeMenu();
}

function fillGenerated() {
  if (!pair) return;
  const password = generatePassword();
  setNativeValue(pair.password, password);
  if (pair.confirm) setNativeValue(pair.confirm, password);
  closeMenu();
}

function reposition() {
  if (!anchored || !anchored.isConnected) return;
  ui.move(anchored.getBoundingClientRect());
}

async function considerSave(next: CredentialPair) {
  const username = next.username?.value ?? "";
  const password = next.password.value;
  if (!username || !password) return;
  const key = `${location.hostname}\n${username}\n${password}`;
  const now = Date.now();
  if (key === lastCapture && now - lastCaptureAt < 1500) return;
  lastCapture = key;
  lastCaptureAt = now;
  const result = (await chrome.runtime.sendMessage({
    type: "classify",
    username,
    password,
  })) as ClassifyResult;
  if (result.action !== "save" && result.action !== "update") return;
  const pending: PendingSave = {
    host: location.hostname,
    username,
    password,
    kind: result.action,
    id: result.action === "update" ? result.id : undefined,
  };
  banner = pending;
  await chrome.runtime.sendMessage({ type: "pending-set", pending });
  showPending(pending, null, false);
}

async function restoreBanner() {
  const pending = (await chrome.runtime.sendMessage({ type: "pending-get" })) as PendingSave | null;
  if (!pending || !hostMatches(location.hostname, pending.host)) return;
  banner = pending;
  showPending(pending, null, false);
}

function showPending(pending: PendingSave, error: string | null, busy: boolean) {
  const state: BannerState = {
    kind: pending.kind,
    host: pending.host,
    username: pending.username,
    error,
    busy,
  };
  ui.showBanner(state);
}

async function saveBanner() {
  if (!banner) return;
  const fieldError =
    validateHostname(banner.host) ??
    validateUsername(banner.username) ??
    validatePassword(banner.password);
  if (fieldError) {
    showPending(banner, fieldError, false);
    return;
  }
  showPending(banner, null, true);
  const command =
    banner.kind === "update" && banner.id != null
      ? {
          op: "edit" as const,
          id: banner.id,
          type: "website" as const,
          name: banner.host,
          username: banner.username,
          password: banner.password,
        }
      : {
          op: "add" as const,
          type: "website" as const,
          name: banner.host,
          username: banner.username,
          password: banner.password,
        };
  const result = (await chrome.runtime.sendMessage({
    type: "command",
    command,
  })) as VaultResult;
  if (!result.ok) {
    showPending(banner, result.message, false);
    return;
  }
  await chrome.runtime.sendMessage({ type: "pending-clear" });
  banner = null;
  ui.hideBanner();
}

async function dismissBanner() {
  await chrome.runtime.sendMessage({ type: "pending-clear" });
  banner = null;
  ui.hideBanner();
}

async function neverBanner() {
  await chrome.runtime.sendMessage({ type: "ignore-host", host: banner?.host });
  banner = null;
  ui.hideBanner();
}

function isSubmitControl(element: Element): boolean {
  if (element instanceof HTMLInputElement) {
    return element.type === "submit" || element.type === "image";
  }
  if (element instanceof HTMLButtonElement) return element.type === "submit";
  return false;
}
