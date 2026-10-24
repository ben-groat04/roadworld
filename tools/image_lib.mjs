// Shared picture checks (gates10). imageDims: frame size from the header bytes, null when the header is not valid. imageEndOk: the file is not truncated (PNG ends with an IEND chunk,
// JPEG with the EOI marker FFD9, GIF with the trailer 3B; BMP and WEBP carry their size in the header, so the declared size must fit the file). A picture cut off mid-download has a valid
// header and fails here (gates9 audit, truncated-PNG fault: the old check read only the first 64 KB).
import fs from 'node:fs';

export function imageDims(b) {
  const sane = (w, h) => (w > 0 && h > 0 && w <= 100000 && h <= 100000 ? { w, h } : null);
  if (b.length >= 33 && b.subarray(0, 8).equals(Buffer.from('89504e470d0a1a0a', 'hex')) && b.readUInt32BE(8) === 13 && b.subarray(12, 16).toString('latin1') === 'IHDR') return sane(b.readUInt32BE(16), b.readUInt32BE(20));
  if (b.length >= 4 && b[0] === 0xff && b[1] === 0xd8 && b[2] === 0xff) {
    let i = 2;
    while (i + 9 < b.length) {
      if (b[i] !== 0xff) { i++; continue; }
      const m = b[i + 1];
      if (m === 0xd8 || m === 0x01 || (m >= 0xd0 && m <= 0xd7) || m === 0xff) { i += m === 0xff ? 1 : 2; continue; }
      if (m >= 0xc0 && m <= 0xcf && m !== 0xc4 && m !== 0xc8 && m !== 0xcc) return sane(b.readUInt16BE(i + 7), b.readUInt16BE(i + 5));
      i += 2 + b.readUInt16BE(i + 2);
    }
    return null;
  }
  if (b.length >= 10 && /^GIF8[79]a/.test(b.subarray(0, 6).toString('latin1'))) return sane(b.readUInt16LE(6), b.readUInt16LE(8));
  if (b.length >= 26 && b[0] === 0x42 && b[1] === 0x4d && b.readUInt32LE(14) >= 12) return sane(Math.abs(b.readInt32LE(18)), Math.abs(b.readInt32LE(22)));
  if (b.length >= 30 && b.subarray(0, 4).toString('latin1') === 'RIFF' && b.subarray(8, 12).toString('latin1') === 'WEBP') {
    const k = b.subarray(12, 16).toString('latin1');
    if (k === 'VP8X') return sane(1 + b.readUIntLE(24, 3), 1 + b.readUIntLE(27, 3));
    if (k === 'VP8L') { const v = b.readUInt32LE(21); return sane((v & 0x3fff) + 1, ((v >> 14) & 0x3fff) + 1); }
    if (k === 'VP8 ') return sane(b.readUInt16LE(26) & 0x3fff, b.readUInt16LE(28) & 0x3fff);
  }
  return null;
}

export function imageEndOk(file) {
  let st, fd;
  try { st = fs.statSync(file); fd = fs.openSync(file, 'r'); } catch { return false; }
  try {
    const n = Math.min(st.size, 4096), tail = Buffer.alloc(n), head = Buffer.alloc(Math.min(st.size, 64));
    fs.readSync(fd, tail, 0, n, st.size - n); fs.readSync(fd, head, 0, head.length, 0);
    let t = tail.length; while (t > 0 && tail[t - 1] === 0) t--;      // trailing zero padding is tolerated
    if (head.subarray(0, 8).equals(Buffer.from('89504e470d0a1a0a', 'hex'))) return tail.subarray(Math.max(0, tail.length - 12)).subarray(-8, -4).toString('latin1') === 'IEND' && tail.subarray(-12, -8).readUInt32BE(0) === 0;
    if (head[0] === 0xff && head[1] === 0xd8) return t >= 2 && tail[t - 2] === 0xff && tail[t - 1] === 0xd9;
    if (/^GIF8[79]a/.test(head.subarray(0, 6).toString('latin1'))) return t >= 1 && tail[t - 1] === 0x3b;
    if (head[0] === 0x42 && head[1] === 0x4d && head.length >= 6) return head.readUInt32LE(2) <= st.size;
    if (head.subarray(0, 4).toString('latin1') === 'RIFF' && head.subarray(8, 12).toString('latin1') === 'WEBP') return head.readUInt32LE(4) + 8 <= st.size;
    return false;
  } catch { return false; } finally { try { fs.closeSync(fd); } catch { /* closed */ } }
}
// header and end both valid
export function imageDecodable(file) {
  try { const fd = fs.openSync(file, 'r'); const st = fs.fstatSync(fd); const head = Buffer.alloc(Math.min(st.size, 65536)); fs.readSync(fd, head, 0, head.length, 0); fs.closeSync(fd); return !!imageDims(head) && imageEndOk(file); } catch { return false; }
}
