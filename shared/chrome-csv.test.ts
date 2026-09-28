import assert from "node:assert/strict";
import test from "node:test";
import { importActionKey, parseChromePasswordCsv, planChromeImport } from "./chrome-csv.ts";
import type { VaultEntry } from "./protocol.ts";

function website(id: number, name: string, username: string, password: string): VaultEntry {
  return { id, type: "website", name, username, password, phrase: "" };
}

const chromeFile = [
  "name,url,username,password,note",
  "GitHub,https://github.com/login,Ada@example.com,correct horse,",
  '"Bank, NA",https://www.bank.example/path,ada,"pa""ss,word","hello',
  'there"',
  "App,android://hash@com.example.app/,ada,secret,",
  "GitHub,https://github.com/login,ada@example.com,newer,",
  "Blank,https://blank.example/,,secret,",
  "Long,https://long.example/,ada," + "x".repeat(129) + ",",
].join("\n");

test("parses a Chrome password export", () => {
  const parsed = parseChromePasswordCsv(`\uFEFF${chromeFile}`);
  assert.equal(parsed.ok, true);
  if (!parsed.ok) return;
  assert.deepEqual(parsed.result.logins, [
    { hostname: "github.com", username: "ada@example.com", password: "newer" },
    { hostname: "www.bank.example", username: "ada", password: 'pa"ss,word' },
  ]);
  assert.equal(parsed.result.repeated, 1);
  assert.equal(parsed.result.notes, 1);
  assert.deepEqual(
    parsed.result.skipped.map((skip) => skip.reason),
    [
      "Not a website login.",
      "Missing a URL, username, or password.",
      "Password must be 128 characters or fewer.",
    ],
  );
});

test("reads a headerless Chrome export", () => {
  const parsed = parseChromePasswordCsv(
    "Example,https://accounts.example.com/signin,ada,secret,\n",
  );
  assert.equal(parsed.ok, true);
  if (!parsed.ok) return;
  assert.deepEqual(parsed.result.logins, [
    { hostname: "accounts.example.com", username: "ada", password: "secret" },
  ]);
});

test("reads url, username, and password without a header", () => {
  const parsed = parseChromePasswordCsv("https://example.com,ada,secret\n");
  assert.equal(parsed.ok, true);
  if (!parsed.ok) return;
  assert.equal(parsed.result.logins[0]?.hostname, "example.com");
});

test("rejects a file that is not a password export", () => {
  const parsed = parseChromePasswordCsv("title,body\nhello,world\n");
  assert.deepEqual(parsed, {
    ok: false,
    message: "Choose a Chrome password CSV. It needs url, username, and password columns.",
  });
});

test("rejects an empty file and a broken quote", () => {
  assert.equal(parseChromePasswordCsv("").ok, false);
  assert.equal(parseChromePasswordCsv('name,url,username,password\n"oops,https://x.test,a,b\n').ok, false);
});

test("plans adds, updates, and logins already on the device", () => {
  const parsed = parseChromePasswordCsv(
    [
      "url,username,password",
      "https://github.com,Ada,same",
      "https://www.news.example,ada,fresh",
      "https://login.example.com,ada,changed",
      "https://example.com,ada,other",
      "https://other.example,ada,new",
    ].join("\n"),
  );
  assert.equal(parsed.ok, true);
  if (!parsed.ok) return;
  const plan = planChromeImport(parsed.result.logins, [
    website(4, "github.com", "ada", "same"),
    website(7, "example.com", "Ada", "old"),
    website(8, "login.example.com", "ada", "old"),
    { id: 9, type: "generic", name: "other.example", username: "", password: "new", phrase: "" },
  ]);
  assert.equal(plan.alreadySaved, 1);
  assert.deepEqual(
    plan.actions.map((action) =>
      action.kind === "add"
        ? `add ${action.hostname} ${action.password}`
        : `update ${action.id} ${action.hostname} ${action.password}`,
    ),
    [
      "add www.news.example fresh",
      "update 8 login.example.com changed",
      "update 7 example.com other",
      "add other.example new",
    ],
  );
  assert.equal(importActionKey(plan.actions[1]), "update:8");
});
