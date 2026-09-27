function normalizeHost(host: string): string {
  return host.trim().toLowerCase().replace(/\.$/, "").replace(/^\[|\]$/g, "");
}

// Exact host, or one host is the other plus a dot-prefix such as www.
export function hostMatches(pageHost: string, entryHost: string): boolean {
  const page = normalizeHost(pageHost);
  const entry = normalizeHost(entryHost);
  if (!page || !entry) return false;
  if (page === entry) return true;
  if (page.endsWith(`.${entry}`)) return true;
  if (entry.endsWith(`.${page}`)) return true;
  return false;
}
