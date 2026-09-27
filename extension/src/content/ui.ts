export type MenuEntry = {
  id: number;
  username: string;
  password: string;
  name: string;
};

export type MenuState = {
  loading: boolean;
  message: string | null;
  entries: MenuEntry[];
  showGenerate: boolean;
};

export type BannerState = {
  kind: "save" | "update";
  host: string;
  username: string;
  error: string | null;
  busy: boolean;
};

export type PageUi = {
  host: HTMLElement;
  showIcon: (rect: DOMRect) => void;
  hideIcon: () => void;
  showMenu: (rect: DOMRect, state: MenuState) => void;
  hideMenu: () => void;
  move: (rect: DOMRect) => void;
  showBanner: (state: BannerState) => void;
  hideBanner: () => void;
  menuOpen: () => boolean;
};

const STYLE = `
  :host { all: initial; }
  * { box-sizing: border-box; }
  button { font: 13px/1.4 ui-sans-serif, system-ui, sans-serif; cursor: pointer; }
  .icon, .menu, .banner { pointer-events: auto; }
  .icon {
    position: fixed;
    z-index: 2147483646;
    width: 22px;
    height: 22px;
    padding: 0;
    border: 0;
    border-radius: 6px;
    background: #e4b54a;
    color: #2a241c;
    display: none;
    place-items: center;
    box-shadow: 0 1px 4px rgba(0, 0, 0, 0.35);
  }
  .icon.on { display: grid; }
  .icon svg { width: 14px; height: 14px; display: block; }
  .menu, .banner {
    position: fixed;
    z-index: 2147483646;
    display: none;
    background: #312b24;
    color: #f6f1e6;
    border: 1px solid rgba(246, 241, 230, 0.14);
    border-radius: 12px;
    box-shadow: 0 12px 40px rgba(0, 0, 0, 0.35);
    font: 13px/1.4 ui-sans-serif, system-ui, sans-serif;
  }
  .menu.on, .banner.on { display: block; }
  .menu { width: 280px; max-height: 320px; overflow: auto; padding: 6px; }
  .row, .action {
    display: block;
    width: 100%;
    text-align: left;
    background: transparent;
    color: inherit;
    border: 0;
    border-radius: 8px;
    padding: 8px 10px;
  }
  .row:hover, .action:hover { background: rgba(228, 181, 74, 0.16); }
  .user { display: block; font-weight: 600; }
  .host, .note { color: #c9bfae; }
  .host { display: block; font-size: 12px; }
  .note { padding: 8px 10px; }
  .split { margin-top: 4px; border-top: 1px solid rgba(246, 241, 230, 0.14); padding-top: 4px; }
  .banner {
    top: 16px;
    left: 50%;
    transform: translateX(-50%);
    width: min(420px, calc(100vw - 24px));
    padding: 14px;
  }
  .banner h2 {
    margin: 0 0 4px;
    font: 22px/1.2 Palatino, Georgia, serif;
    font-weight: 400;
  }
  .actions { display: flex; flex-wrap: wrap; gap: 8px; margin-top: 12px; }
  .primary, .quiet { border-radius: 8px; padding: 6px 10px; }
  .primary { background: #e4b54a; color: #2a241c; border: 0; }
  .quiet { background: transparent; color: #f6f1e6; border: 1px solid rgba(246, 241, 230, 0.2); }
  .error { color: #f0a090; margin: 8px 0 0; }
  button:disabled { opacity: 0.55; cursor: default; }
`;

export function createPageUi(
  doc: Document,
  handlers: {
    onIcon: () => void;
    onFill: (username: string, password: string) => void;
    onGenerate: () => void;
    onManage: () => void;
    onSave: () => void;
    onDismiss: () => void;
    onNever: () => void;
  },
): PageUi {
  const host = doc.createElement("div");
  host.setAttribute("data-kchain", "");
  host.style.cssText =
    "position:fixed;top:0;left:0;width:0;height:0;overflow:visible;z-index:2147483646;pointer-events:none;";
  const shadow = host.attachShadow({ mode: "closed" });
  const style = doc.createElement("style");
  style.textContent = STYLE;
  const icon = doc.createElement("button");
  icon.type = "button";
  icon.className = "icon";
  icon.setAttribute("aria-label", "kChain passwords");
  icon.append(keyIcon(doc));
  const menu = doc.createElement("div");
  menu.className = "menu";
  menu.setAttribute("role", "listbox");
  menu.setAttribute("aria-label", "kChain passwords");
  const banner = doc.createElement("div");
  banner.className = "banner";
  banner.setAttribute("role", "status");
  shadow.append(style, icon, menu, banner);
  doc.documentElement.append(host);

  keepFieldFocus(icon);
  icon.addEventListener("click", () => handlers.onIcon());

  let menuVisible = false;

  return {
    host,
    showIcon(rect) {
      placeIcon(icon, rect);
      icon.classList.add("on");
    },
    hideIcon() {
      icon.classList.remove("on");
    },
    showMenu(rect, state) {
      menuVisible = true;
      renderMenu(doc, menu, state, handlers);
      menu.classList.add("on");
      placeMenu(menu, rect);
    },
    hideMenu() {
      menuVisible = false;
      menu.classList.remove("on");
      menu.replaceChildren();
    },
    move(rect) {
      if (icon.classList.contains("on")) placeIcon(icon, rect);
      if (menuVisible) placeMenu(menu, rect);
    },
    showBanner(state) {
      renderBanner(doc, banner, state, handlers);
      banner.classList.add("on");
    },
    hideBanner() {
      banner.classList.remove("on");
      banner.replaceChildren();
    },
    menuOpen: () => menuVisible,
  };
}

function renderMenu(
  doc: Document,
  menu: HTMLElement,
  state: MenuState,
  handlers: {
    onFill: (username: string, password: string) => void;
    onGenerate: () => void;
    onManage: () => void;
  },
) {
  menu.replaceChildren();
  if (state.loading) menu.append(note(doc, "Looking up logins…"));
  else if (state.message) menu.append(note(doc, state.message));
  for (const entry of state.entries) {
    const button = doc.createElement("button");
    button.type = "button";
    button.className = "row";
    button.setAttribute("role", "option");
    const user = doc.createElement("span");
    user.className = "user";
    user.textContent = entry.username;
    const host = doc.createElement("span");
    host.className = "host";
    host.textContent = entry.name;
    button.append(user, host);
    keepFieldFocus(button);
    button.addEventListener("click", () => handlers.onFill(entry.username, entry.password));
    menu.append(button);
  }
  const footer = doc.createElement("div");
  footer.className = "split";
  if (state.showGenerate) footer.append(action(doc, "Generate password", handlers.onGenerate));
  footer.append(action(doc, "Manage in kChain", handlers.onManage));
  menu.append(footer);
}

function renderBanner(
  doc: Document,
  banner: HTMLElement,
  state: BannerState,
  handlers: { onSave: () => void; onDismiss: () => void; onNever: () => void },
) {
  banner.replaceChildren();
  const title = doc.createElement("h2");
  title.textContent = state.kind === "update" ? "Update password?" : "Save password to kChain?";
  const detail = doc.createElement("p");
  detail.className = "host";
  detail.textContent = `${state.username} · ${state.host}`;
  const actions = doc.createElement("div");
  actions.className = "actions";
  actions.append(
    labeled(doc, "Save", "primary", state.busy, handlers.onSave),
    labeled(doc, "Not now", "quiet", state.busy, handlers.onDismiss),
    labeled(doc, "Never for this site", "quiet", state.busy, handlers.onNever),
  );
  banner.append(title, detail, actions);
  if (state.error) {
    const error = doc.createElement("p");
    error.className = "error";
    error.textContent = state.error;
    banner.append(error);
  }
}

function action(doc: Document, label: string, onClick: () => void): HTMLButtonElement {
  const button = doc.createElement("button");
  button.type = "button";
  button.className = "action";
  button.textContent = label;
  keepFieldFocus(button);
  button.addEventListener("click", onClick);
  return button;
}

function labeled(
  doc: Document,
  label: string,
  kind: "primary" | "quiet",
  busy: boolean,
  onClick: () => void,
): HTMLButtonElement {
  const button = doc.createElement("button");
  button.type = "button";
  button.className = kind;
  button.textContent = label;
  button.disabled = busy;
  button.addEventListener("click", onClick);
  return button;
}

function note(doc: Document, text: string): HTMLParagraphElement {
  const paragraph = doc.createElement("p");
  paragraph.className = "note";
  paragraph.textContent = text;
  return paragraph;
}

function keepFieldFocus(button: HTMLButtonElement) {
  button.addEventListener("mousedown", (event) => event.preventDefault());
}

function placeIcon(icon: HTMLElement, rect: DOMRect) {
  icon.style.left = `${Math.max(8, rect.right - 26)}px`;
  icon.style.top = `${rect.top + (rect.height - 22) / 2}px`;
}

function placeMenu(menu: HTMLElement, rect: DOMRect) {
  const width = 280;
  const margin = 8;
  const left = Math.min(Math.max(margin, rect.right - width), window.innerWidth - width - margin);
  menu.style.left = `${left}px`;
  menu.style.top = `${rect.bottom + 6}px`;
  const box = menu.getBoundingClientRect();
  if (box.bottom > window.innerHeight - margin) {
    menu.style.top = `${Math.max(margin, rect.top - box.height - 6)}px`;
  }
}

function keyIcon(doc: Document): SVGSVGElement {
  const svg = doc.createElementNS("http://www.w3.org/2000/svg", "svg");
  svg.setAttribute("viewBox", "0 0 24 24");
  svg.setAttribute("aria-hidden", "true");
  const circle = doc.createElementNS("http://www.w3.org/2000/svg", "circle");
  circle.setAttribute("cx", "8");
  circle.setAttribute("cy", "11");
  circle.setAttribute("r", "3.2");
  circle.setAttribute("fill", "none");
  circle.setAttribute("stroke", "currentColor");
  circle.setAttribute("stroke-width", "2");
  const path = doc.createElementNS("http://www.w3.org/2000/svg", "path");
  path.setAttribute("d", "M11 11h9v2.2h-2.2V16H15.4v-2.8H11");
  path.setAttribute("fill", "none");
  path.setAttribute("stroke", "currentColor");
  path.setAttribute("stroke-width", "2");
  path.setAttribute("stroke-linejoin", "round");
  svg.append(circle, path);
  return svg;
}
