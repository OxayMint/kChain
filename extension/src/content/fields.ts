const USER_HINT = /\b(user(name)?|e-?mail|login|log-in|sign-?in|identifier)\b/i;

export type CredentialPair = {
  form: HTMLFormElement | null;
  username: HTMLInputElement | null;
  password: HTMLInputElement;
  confirm: HTMLInputElement | null;
};

export function isPasswordField(input: HTMLInputElement): boolean {
  if (tokens(input).includes("one-time-code")) return false;
  const auto = tokens(input);
  if (auto.includes("current-password") || auto.includes("new-password")) return true;
  return input.type === "password";
}

export function isUsernameField(input: HTMLInputElement): boolean {
  if (isPasswordField(input) || input.type === "password") return false;
  if (tokens(input).includes("one-time-code")) return false;
  const type = input.type;
  if (type !== "text" && type !== "email" && type !== "tel" && type !== "") return false;
  const auto = tokens(input);
  if (auto.includes("username") || auto.includes("email")) return true;
  if (type === "email") return true;
  return USER_HINT.test(fieldHint(input));
}

export function isVisibleField(input: HTMLInputElement): boolean {
  if (input.disabled || input.type === "hidden") return false;
  if (input.getAttribute("aria-hidden") === "true") return false;
  const view = input.ownerDocument.defaultView;
  const style = view?.getComputedStyle(input);
  if (!style || style.display === "none" || style.visibility === "hidden") return false;
  const rect = input.getBoundingClientRect();
  return rect.width >= 2 && rect.height >= 2;
}

export function pairFor(input: HTMLInputElement): CredentialPair | null {
  if (!isVisibleField(input)) return null;
  if (isPasswordField(input)) return pairFromPassword(input);
  if (!isUsernameField(input)) return null;
  const password = passwordAfter(input);
  if (!password) return null;
  return pairFromPassword(password);
}

export function pairInForm(form: HTMLFormElement): CredentialPair | null {
  const password = passwordsIn(form, form).find((input) => input.form === form);
  return password ? pairFromPassword(password) : null;
}

export function pairToFill(doc: Document): CredentialPair | null {
  const active = doc.activeElement;
  if (active instanceof HTMLInputElement) {
    const focused = pairFor(active);
    if (focused) return focused;
  }
  const password = passwordsIn(doc, null)[0];
  return password ? pairFromPassword(password) : null;
}

export function setNativeValue(input: HTMLInputElement, value: string): void {
  const descriptor = Object.getOwnPropertyDescriptor(HTMLInputElement.prototype, "value");
  descriptor?.set?.call(input, value);
  input.dispatchEvent(new Event("input", { bubbles: true }));
  input.dispatchEvent(new Event("change", { bubbles: true }));
}

function pairFromPassword(password: HTMLInputElement): CredentialPair {
  const group = passwordsIn(password.form ?? password.ownerDocument, password.form);
  const primary = group[0] ?? password;
  const confirm = group.length > 1 ? group[1] : null;
  return {
    form: primary.form,
    username: usernameFor(primary),
    password: primary,
    confirm,
  };
}

function passwordAfter(username: HTMLInputElement): HTMLInputElement | null {
  return (
    passwordsIn(username.form ?? username.ownerDocument, username.form).find(
      (password) => username.compareDocumentPosition(password) & Node.DOCUMENT_POSITION_FOLLOWING,
    ) ?? null
  );
}

function passwordsIn(root: ParentNode, form: HTMLFormElement | null): HTMLInputElement[] {
  return [...root.querySelectorAll("input")].filter((input): input is HTMLInputElement => {
    if (!(input instanceof HTMLInputElement)) return false;
    if (!isPasswordField(input) || !isVisibleField(input)) return false;
    if (form) return input.form === form;
    return input.form == null;
  });
}

function usernameFor(password: HTMLInputElement): HTMLInputElement | null {
  const form = password.form;
  const root: ParentNode = form ?? password.ownerDocument;
  const preceding = [...root.querySelectorAll("input")].filter((input): input is HTMLInputElement => {
    if (!(input instanceof HTMLInputElement)) return false;
    if (!isUsernameField(input) || !isVisibleField(input)) return false;
    if (form ? input.form !== form : input.form != null) return false;
    return Boolean(password.compareDocumentPosition(input) & Node.DOCUMENT_POSITION_PRECEDING);
  });
  return preceding.length > 0 ? preceding[preceding.length - 1] : null;
}

function tokens(input: HTMLInputElement): string[] {
  return (input.getAttribute("autocomplete") ?? "")
    .toLowerCase()
    .split(/\s+/)
    .filter(Boolean);
}

function fieldHint(input: HTMLInputElement): string {
  return [input.name, input.id, input.placeholder, input.getAttribute("aria-label"), labelText(input)]
    .filter(Boolean)
    .join(" ");
}

function labelText(input: HTMLInputElement): string {
  const doc = input.ownerDocument;
  if (input.id) {
    const label = doc.querySelector(`label[for="${CSS.escape(input.id)}"]`);
    if (label?.textContent) return label.textContent;
  }
  return input.closest("label")?.textContent ?? "";
}
