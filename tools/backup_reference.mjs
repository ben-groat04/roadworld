#!/usr/bin/env node
// Validated backup of the private reference photos: main checkout docs/reference -> C:\Users\bgbg0\roadworld-reference-backup (the real protection against a repeat of
// 22 Oct 2026, when a recursive delete wiped every photo; main session decision 23 Oct 2026). The backup is only worth anything if a bad copy can never replace a good one,
// so this script VALIDATES first and mirrors second:
//   - every PNG is decoded as far as a decoder would without pixel output: signature, every chunk's CRC, IHDR, an IDAT stream that inflates, the inflated size equal to
//     height x row bytes (non-interlaced), IEND last; every JPEG has SOI, a SOF and SOS marker, a trailing EOI (up to 16 zero bytes of padding); GIF header and trailer;
//     WebP RIFF size; any other picture type is at least non-empty;
//   - any picture file with a run of 64 KB or more of zero bytes is refused (a wiped or half-written file looks like that), as is a zero-length file of any type;
//   - if even one picture fails, NOTHING is mirrored (the previous backup stays as it was) and the bad files are listed;
//   - if docs/reference holds more than 5 % fewer files than the last backup recorded, NOTHING is mirrored (a delete is in progress or has happened) unless the main
//     session passes --accept-loss "<reason of 20+ characters>"; the same limit applies to files that would be removed from the backup.
// The mirror copies new and changed files (size or mtime) and removes from the backup only files that no longer exist in main (within the 5 % limit). It never uses a
// recursive delete: single files are unlinked, emptied folders removed with a non-recursive rmdir.
// Usage: node tools/backup_reference.mjs [--main <dir>] [--backup <dir>] [--verify-only] [--dry-run] [--accept-loss "<reason>"]
// Exit 0 = validated and mirrored (or verified), 1 = refused (nothing changed in the backup), 2 = internal error.
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import zlib from 'node:zlib';
import crypto from 'node:crypto';
import { fileURLToPath } from 'node:url';

export const DEFAULT_BACKUP = 'C:\\Users\\bgbg0\\roadworld-reference-backup';
export const MAX_LOSS = 0.05;
export const ZERO_RUN = 64 * 1024;
const MANIFEST = '.backup_manifest.json';
const PIC = /\.(png|jpe?g|gif|webp|bmp|tiff?)$/i;

export function longestZeroRun(buf) {
  let best = 0, run = 0;
  for (let i = 0; i < buf.length; i++) { if (buf[i] === 0) { run++; if (run > best) best = run; } else run = 0; }
  return best;
}

export function validatePng(buf) {
  const sig = Buffer.from([0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a]);
  if (buf.length < 8 + 25 + 12 || !buf.subarray(0, 8).equals(sig)) return 'not a PNG (bad signature)';
  let p = 8, w = 0, h = 0, depth = 0, ctype = -1, interlace = 0; const idat = []; let sawEnd = false, first = true;
  while (p + 12 <= buf.length) {
    const len = buf.readUInt32BE(p), type = buf.toString('latin1', p + 4, p + 8);
    if (p + 12 + len > buf.length) return `chunk ${type} runs past the end of the file (truncated)`;
    const data = buf.subarray(p + 8, p + 8 + len), crc = buf.readUInt32BE(p + 8 + len);
    const want = zlib.crc32(buf.subarray(p + 4, p + 8 + len)) >>> 0;
    if (want !== crc) return `chunk ${type} has a bad CRC (damaged)`;
    if (first && type !== 'IHDR') return 'first chunk is not IHDR';
    first = false;
    if (type === 'IHDR') { w = data.readUInt32BE(0); h = data.readUInt32BE(4); depth = data[8]; ctype = data[9]; interlace = data[12]; }
    else if (type === 'IDAT') idat.push(data);
    else if (type === 'IEND') { sawEnd = true; p += 12 + len; break; }
    p += 12 + len;
  }
  if (!sawEnd) return 'no IEND chunk (truncated)';
  if (!w || !h) return 'IHDR has a zero size';
  if (!idat.length) return 'no IDAT chunk';
  let raw; try { raw = zlib.inflateSync(Buffer.concat(idat)); } catch (e) { return 'IDAT does not inflate: ' + e.message; }
  const spp = { 0: 1, 2: 3, 3: 1, 4: 2, 6: 4 }[ctype];
  if (!spp) return 'unknown colour type ' + ctype;
  if (!interlace) {
    const rowBytes = Math.ceil((w * spp * depth) / 8) + 1;
    if (raw.length !== rowBytes * h) return `decoded size ${raw.length} is not ${h} rows of ${rowBytes} bytes (truncated or wrong)`;
  }
  return null;
}

export function validateJpeg(buf) {
  if (buf.length < 125 || buf[0] !== 0xff || buf[1] !== 0xd8) return 'not a JPEG (no SOI)';
  let end = buf.length; while (end > 2 && buf[end - 1] === 0 && buf.length - end < 16) end--;
  if (!(buf[end - 2] === 0xff && buf[end - 1] === 0xd9)) return 'no EOI marker at the end (truncated)';
  let p = 2, sof = false, sos = false;
  while (p + 4 <= buf.length) {
    if (buf[p] !== 0xff) return 'marker structure broken at byte ' + p;
    const m = buf[p + 1];
    if (m === 0xff) { p++; continue; }
    if (m === 0xd9) break;
    if (m >= 0xd0 && m <= 0xd7 || m === 0x01) { p += 2; continue; }
    const len = buf.readUInt16BE(p + 2);
    if (len < 2 || p + 2 + len > buf.length) return 'a segment runs past the end of the file';
    if ((m >= 0xc0 && m <= 0xcf) && m !== 0xc4 && m !== 0xc8 && m !== 0xcc) { sof = true; if (!buf.readUInt16BE(p + 5) || !buf.readUInt16BE(p + 7)) return 'SOF has a zero size'; }
    if (m === 0xda) { sos = true; break; }
    p += 2 + len;
  }
  if (!sof) return 'no SOF marker (no image header)';
  if (!sos) return 'no SOS marker (no image data)';
  return null;
}

// returns null when fine, else a reason string
export function validateFile(buf, name) {
  if (!buf.length) return 'zero-length file';
  if (!PIC.test(name)) return null;
  if (/\.png$/i.test(name)) { const r = validatePng(buf); if (r) return r; }
  else if (/\.jpe?g$/i.test(name)) { const r = validateJpeg(buf); if (r) return r; }
  else if (/\.gif$/i.test(name)) { if (!/^GIF8[79]a/.test(buf.toString('latin1', 0, 6)) || buf[buf.length - 1] !== 0x3b) return 'GIF header or trailer missing'; }
  else if (/\.webp$/i.test(name)) { if (buf.toString('latin1', 0, 4) !== 'RIFF' || buf.toString('latin1', 8, 12) !== 'WEBP' || buf.readUInt32LE(4) + 8 > buf.length) return 'WebP RIFF header missing or size past the end'; }
  const z = longestZeroRun(buf);
  if (z >= ZERO_RUN) return `a run of ${z} zero bytes (>= 64 KB: wiped or half-written)`;
  return null;
}

export function walkFiles(root) {
  const out = [];
  const walk = (d, rel) => {
    let ents; try { ents = fs.readdirSync(d, { withFileTypes: true }); } catch { return; }
    for (const e of ents) {
      const f = path.join(d, e.name), r = rel ? rel + '/' + e.name : e.name;
      if (e.isSymbolicLink()) continue;                  // a link is never followed
      if (e.isDirectory()) walk(f, r); else if (e.isFile()) out.push({ rel: r, abs: f });
    }
  };
  walk(root, '');
  return out;
}

export function validateTree(root, files = walkFiles(root)) {
  const bad = [];
  for (const f of files) {
    let buf; try { buf = fs.readFileSync(f.abs); } catch (e) { bad.push({ rel: f.rel, why: 'unreadable: ' + e.message }); continue; }
    const why = validateFile(buf, f.rel);
    if (why) bad.push({ rel: f.rel, why });
  }
  return bad;
}

function readManifest(backup) { try { return JSON.parse(fs.readFileSync(path.join(backup, MANIFEST), 'utf8')); } catch { return null; } }

// Returns { ok, refused: string[], copied, removed, total }. Never throws for a refusal.
export function backup({ main, backupDir = DEFAULT_BACKUP, dryRun = false, verifyOnly = false, acceptLoss = '' }) {
  const src = path.join(main, 'docs', 'reference');
  const res = { ok: false, refused: [], copied: 0, removed: 0, total: 0, skippedUnchanged: 0 };
  if (!fs.existsSync(src)) { res.refused.push(`${src} does not exist`); return res; }
  const rs = path.resolve(src).toLowerCase(), rb = path.resolve(backupDir).toLowerCase();
  if (rb === rs || rb.startsWith(rs + path.sep) || rs.startsWith(rb + path.sep)) { res.refused.push('the backup folder and docs/reference must be separate folders (neither inside the other)'); return res; }
  const files = walkFiles(src); res.total = files.length;
  if (!files.length) { res.refused.push('docs/reference holds no files: refusing to mirror an empty folder over the backup'); return res; }
  const man = readManifest(backupDir);
  const bad = validateTree(src, files);
  if (bad.length) { res.refused.push(`${bad.length} file(s) failed validation, NOTHING was mirrored:\n   ` + bad.slice(0, 40).map((b) => `${b.rel}: ${b.why}`).join('\n   ') + (bad.length > 40 ? `\n   ... and ${bad.length - 40} more` : '')); res.bad = bad; return res; }
  const prevCount = man && Number.isInteger(man.count) ? man.count : null;
  const lossOk = acceptLoss.trim().length >= 20;
  if (prevCount && files.length < prevCount * (1 - MAX_LOSS) && !lossOk) {
    res.refused.push(`docs/reference holds ${files.length} file(s), the last backup recorded ${prevCount} (more than ${MAX_LOSS * 100} % fewer): something has been deleted. NOTHING was mirrored. If the loss is intended, rerun with --accept-loss "<reason of 20+ characters>".`);
    return res;
  }
  if (verifyOnly) { res.ok = true; return res; }
  // plan
  const have = new Map(files.map((f) => [f.rel, f]));
  const existing = fs.existsSync(backupDir) ? walkFiles(backupDir).filter((f) => f.rel !== MANIFEST) : [];
  const toRemove = existing.filter((f) => !have.has(f.rel));
  if (existing.length && toRemove.length > existing.length * MAX_LOSS && !lossOk) {
    res.refused.push(`the mirror would remove ${toRemove.length} of ${existing.length} files from the backup (more than ${MAX_LOSS * 100} %). NOTHING was changed. Rerun with --accept-loss "<reason of 20+ characters>" if intended.`);
    return res;
  }
  const same = (a, b) => { try { const x = fs.statSync(a), y = fs.statSync(b); return x.size === y.size && Math.abs(x.mtimeMs - y.mtimeMs) < 2000; } catch { return false; } };
  for (const f of files) {
    const dst = path.join(backupDir, ...f.rel.split('/'));
    if (same(f.abs, dst)) { res.skippedUnchanged++; continue; }
    res.copied++;
    if (dryRun) continue;
    fs.mkdirSync(path.dirname(dst), { recursive: true });
    const tmpName = dst + '.part'; fs.copyFileSync(f.abs, tmpName);
    const st = fs.statSync(f.abs); fs.utimesSync(tmpName, st.atime, st.mtime);
    fs.renameSync(tmpName, dst);
  }
  for (const f of toRemove) { res.removed++; if (!dryRun) fs.unlinkSync(f.abs); }
  if (!dryRun) {
    // emptied folders: non-recursive rmdir, deepest first
    const dirs = []; const walkD = (d) => { for (const e of fs.readdirSync(d, { withFileTypes: true })) if (e.isDirectory() && !e.isSymbolicLink()) { const f = path.join(d, e.name); walkD(f); dirs.push(f); } };
    walkD(backupDir); for (const d of dirs) { try { fs.rmdirSync(d); } catch { /* not empty */ } }
    const sha = crypto.createHash('sha1').update(files.map((f) => f.rel).sort().join('\n')).digest('hex');
    fs.writeFileSync(path.join(backupDir, MANIFEST), JSON.stringify({ ts: new Date().toISOString(), count: files.length, names_sha1: sha, source: src, accepted_loss: lossOk ? acceptLoss.trim() : undefined }, null, 1));
  }
  res.ok = true;
  return res;
}

async function cli() {
  const a = process.argv.slice(2); const get = (n) => { const i = a.indexOf(n); return i >= 0 ? a[i + 1] : undefined; };
  let main = get('--main');
  if (!main) { const { resolveMainRoot } = await import('./hooks/root.mjs'); main = resolveMainRoot(path.dirname(fileURLToPath(import.meta.url))); }
  const r = backup({ main, backupDir: get('--backup') || DEFAULT_BACKUP, dryRun: a.includes('--dry-run'), verifyOnly: a.includes('--verify-only'), acceptLoss: get('--accept-loss') || '' });
  if (!r.ok) { console.log('REFUSED (the backup was not changed):\n ' + r.refused.join('\n ')); process.exit(1); }
  console.log(`${a.includes('--verify-only') ? 'verified' : a.includes('--dry-run') ? 'dry run' : 'mirrored'}: ${r.total} file(s) in docs/reference all valid; copied ${r.copied}, unchanged ${r.skippedUnchanged}, removed from backup ${r.removed}.`);
}
if (process.argv[1] && path.resolve(process.argv[1]) === path.resolve(fileURLToPath(import.meta.url))) cli().catch((e) => { console.error('ERROR: ' + (e && e.stack || e)); process.exit(2); });
void os;
