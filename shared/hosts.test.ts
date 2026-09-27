import assert from "node:assert/strict";
import test from "node:test";
import { hostMatches } from "./hosts.ts";

test("matches a host and a www prefix", () => {
  assert.equal(hostMatches("github.com", "github.com"), true);
  assert.equal(hostMatches("www.github.com", "github.com"), true);
  assert.equal(hostMatches("github.com", "www.github.com"), true);
  assert.equal(hostMatches("GitHub.com.", "github.com"), true);
});

test("does not match a different site", () => {
  assert.equal(hostMatches("login.microsoftonline.com", "github.com"), false);
  assert.equal(hostMatches("", "github.com"), false);
  assert.equal(hostMatches("github.com", ""), false);
});
