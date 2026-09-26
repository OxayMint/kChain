const LETTERS = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ";
const DIGITS = "0123456789";
const SYMBOLS = "!?@#$%&(){}";
const ALPHABET = `${LETTERS}${DIGITS}${SYMBOLS}`;
const LENGTH = 20;

function randomBelow(limit: number): number {
  const span = Math.floor(256 / limit) * limit;
  const buffer = new Uint8Array(1);
  while (true) {
    crypto.getRandomValues(buffer);
    if (buffer[0] < span) return buffer[0] % limit;
  }
}

function pick(source: string): string {
  return source[randomBelow(source.length)];
}

export function generatePassword(): string {
  const chars = Array.from({ length: LENGTH }, () => pick(ALPHABET));
  chars[0] = pick(LETTERS);
  chars[1] = pick(DIGITS);
  chars[2] = pick(SYMBOLS);
  for (let i = chars.length - 1; i > 0; i -= 1) {
    const j = randomBelow(i + 1);
    const current = chars[i];
    chars[i] = chars[j];
    chars[j] = current;
  }
  return chars.join("");
}
