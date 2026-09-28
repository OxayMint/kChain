import { hostMatches } from "./hosts.ts";
import type { VaultEntry } from "./protocol.ts";

// The website entry a tap should wake onto. A filled username or email
// picks that login when several entries share the host. Otherwise the
// first saved login for the host is used.
export function entryForPage(
  entries: VaultEntry[],
  host: string,
  username: string,
): VaultEntry | null {
  const matches = entries.filter(
    (entry) => entry.type === "website" && hostMatches(host, entry.name),
  );
  if (matches.length === 0) return null;
  const typed = username.trim().toLowerCase();
  if (typed) {
    const exact = matches.find((entry) => entry.username.toLowerCase() === typed);
    if (exact) return exact;
  }
  return matches[0];
}
