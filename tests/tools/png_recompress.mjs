// Recompresses the stored-deflate RGB8 PNGs written by tests/world_preview.h (zlib + Sub filter), in place.
//   node tests/tools/png_recompress.mjs <dir>
import { readFileSync, writeFileSync, readdirSync } from 'node:fs';
import { inflateSync, deflateSync, crc32 } from 'node:zlib';
import { join } from 'node:path';
const dir = process.argv[2];
const chunk = (type, data) => {
  const t = Buffer.concat([Buffer.from(type), data]);
  const len = Buffer.alloc(4); len.writeUInt32BE(data.length);
  const c = Buffer.alloc(4); c.writeUInt32BE(crc32(t) >>> 0);
  return Buffer.concat([len, t, c]);
};
for (const f of readdirSync(dir).filter(n => n.endsWith('.png'))) {
  const b = readFileSync(join(dir, f));
  let p = 8, w = 0, h = 0; const idat = [];
  while (p < b.length) {
    const n = b.readUInt32BE(p), t = b.toString('latin1', p + 4, p + 8), d = b.subarray(p + 8, p + 8 + n);
    if (t === 'IHDR') { w = d.readUInt32BE(0); h = d.readUInt32BE(4); }
    if (t === 'IDAT') idat.push(d);
    p += 12 + n;
  }
  const raw = inflateSync(Buffer.concat(idat));
  const stride = w * 3 + 1, out = Buffer.alloc(raw.length);
  for (let y = 0; y < h; y++) {
    out[y * stride] = 1;
    for (let x = 0; x < w * 3; x++) {
      const left = x >= 3 ? raw[y * stride + 1 + x - 3] : 0;
      out[y * stride + 1 + x] = (raw[y * stride + 1 + x] - left) & 255;
    }
  }
  const ihdr = Buffer.alloc(13); ihdr.writeUInt32BE(w, 0); ihdr.writeUInt32BE(h, 4); ihdr[8] = 8; ihdr[9] = 2;
  writeFileSync(join(dir, f), Buffer.concat([b.subarray(0, 8), chunk('IHDR', ihdr), chunk('IDAT', deflateSync(out, { level: 9 })), chunk('IEND', Buffer.alloc(0))]));
}
