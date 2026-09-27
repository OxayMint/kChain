import { deflateSync } from "node:zlib";
import { mkdirSync, writeFileSync } from "node:fs";
import { dirname, join } from "node:path";
import { fileURLToPath } from "node:url";

const gold = [228, 181, 74, 255];
const ink = [42, 36, 28, 255];
const clear = [0, 0, 0, 0];

function crc32(buf) {
  let c = ~0;
  for (const byte of buf) {
    c ^= byte;
    for (let i = 0; i < 8; i++) c = (c >>> 1) ^ (0xedb88320 & -(c & 1));
  }
  return ~c >>> 0;
}

function chunk(type, data) {
  const body = Buffer.concat([Buffer.from(type), data]);
  const length = Buffer.alloc(4);
  length.writeUInt32BE(data.length);
  const crc = Buffer.alloc(4);
  crc.writeUInt32BE(crc32(body));
  return Buffer.concat([length, body, crc]);
}

function png(size, rgba) {
  const raw = Buffer.alloc((size * 4 + 1) * size);
  for (let y = 0; y < size; y++) {
    const row = y * (size * 4 + 1);
    raw[row] = 0;
    rgba.copy(raw, row + 1, y * size * 4, (y + 1) * size * 4);
  }
  const ihdr = Buffer.alloc(13);
  ihdr.writeUInt32BE(size, 0);
  ihdr.writeUInt32BE(size, 4);
  ihdr[8] = 8;
  ihdr[9] = 6;
  return Buffer.concat([
    Buffer.from([137, 80, 78, 71, 13, 10, 26, 10]),
    chunk("IHDR", ihdr),
    chunk("IDAT", deflateSync(raw)),
    chunk("IEND", Buffer.alloc(0)),
  ]);
}

function inRoundRect(x, y, size, radius) {
  const px = x + 0.5;
  const py = y + 0.5;
  if (px >= radius && px < size - radius && py >= 0 && py < size) return true;
  if (py >= radius && py < size - radius && px >= 0 && px < size) return true;
  const cx = px < size / 2 ? radius : size - radius;
  const cy = py < size / 2 ? radius : size - radius;
  const dx = px - cx;
  const dy = py - cy;
  return dx * dx + dy * dy <= radius * radius;
}

function isKey(nx, ny) {
  const dx = nx - 0.38;
  const dy = ny - 0.42;
  const distance = Math.hypot(dx, dy);
  if (distance < 0.16 && distance > 0.07) return true;
  if (nx >= 0.38 && nx <= 0.8 && ny >= 0.37 && ny <= 0.47) return true;
  if (nx >= 0.62 && nx <= 0.7 && ny >= 0.47 && ny <= 0.58) return true;
  if (nx >= 0.72 && nx <= 0.8 && ny >= 0.47 && ny <= 0.56) return true;
  return false;
}

function draw(size) {
  const rgba = Buffer.alloc(size * size * 4);
  const radius = size * 0.22;
  for (let y = 0; y < size; y++) {
    for (let x = 0; x < size; x++) {
      const offset = (y * size + x) * 4;
      if (!inRoundRect(x, y, size, radius)) {
        rgba.set(clear, offset);
        continue;
      }
      const color = isKey((x + 0.5) / size, (y + 0.5) / size) ? ink : gold;
      rgba.set(color, offset);
    }
  }
  return png(size, rgba);
}

const dir = join(dirname(fileURLToPath(import.meta.url)), "..", "public", "icons");
mkdirSync(dir, { recursive: true });
for (const size of [16, 32, 48, 128]) {
  writeFileSync(join(dir, `${size}.png`), draw(size));
}
