import assert from "node:assert/strict";
import test from "node:test";
import { entryForPage } from "./page-entry.ts";
import type { VaultEntry } from "./protocol.ts";

function website(id: number, name: string, username: string): VaultEntry {
  return { id, type: "website", name, username, password: "secret", phrase: "" };
}

const entries: VaultEntry[] = [
  { id: 1, type: "generic", name: "wifi", username: "", password: "secret", phrase: "" },
  website(2, "github.com", "ada@example.com"),
  website(3, "www.github.com", "grace@example.com"),
  website(4, "bank.example", "ada"),
];

test("picks the first saved login for the site", () => {
  assert.equal(entryForPage(entries, "github.com", "")?.id, 2);
  assert.equal(entryForPage(entries, "www.github.com", "")?.id, 2);
});

test("prefers the login whose username is filled in", () => {
  assert.equal(entryForPage(entries, "github.com", "Grace@example.com")?.id, 3);
});

test("falls back to the first login when the username is not saved", () => {
  assert.equal(entryForPage(entries, "github.com", "nobody")?.id, 2);
});

test("returns nothing for a site with no saved login", () => {
  assert.equal(entryForPage(entries, "example.com", "ada"), null);
  assert.equal(entryForPage(entries, "", ""), null);
});
