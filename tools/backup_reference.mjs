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
// The top-level folder _damaged (quarantine of files known to be bad) is skipped: not validated, not mirrored.
// The mirror copies new and changed files (size or mtime) and removes from the backup only files that no longer exist in main (within the 5 % limit). It never uses a
// recursive delete: single files are unlinked, emptied folders removed with a non-recursive rmdir.
// --verify-only is QUICK once a manifest exists: a file whose size and modification time equal its manifest entry is trusted without being read (the mirror run, which writes the manifest,
// always hashes every file, so silent corruption is caught there); --verify-only --full reads and hashes every file as well. Files that changed, or are not in the manifest, are validated in full.
// Usage: node tools/backup_reference.mjs [--main <dir>] [--backup <dir>] [--verify-only [--full]] [--dry-run] [--accept-loss "<reason>"]
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
const ZERO_BLOCK = Buffer.alloc(ZERO_RUN);
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

// Decodes the ENTROPY-CODED part of a baseline (SOF0/SOF1) Huffman JPEG as a decoder would, without producing pixels: every scan must decode exactly the number of MCUs the image
// header implies (restart markers where the restart interval says), with only valid Huffman codes and coefficient runs, and must end exactly where the next marker begins. A zero run
// or flipped bytes in the middle of the scan change the code lengths, so the stream stops decoding to the right count (gates5 audit item 27a). Progressive and arithmetic-coded JPEGs
// are not decoded: for them only a run of 32 or more zero bytes inside entropy data is refused (honest limit, listed in FAULTS). Returns null when fine, else a reason.
// gates7 item 11: 32 or more zero bytes in a row inside entropy-coded data (not the 0 of an FF 00 stuffing pair) never occur in a photo and are what a wiped stretch looks like. Applied to
// baseline scans as well as progressive ones; scans the data from `pos` up to the next real marker. Returns a reason or null.
function scanZeroRun(buf, pos, kind) {
  let run = 0;
  while (pos < buf.length) {
    const b = buf[pos];
    if (b === 0xff && buf[pos + 1] !== 0 && !(buf[pos + 1] >= 0xd0 && buf[pos + 1] <= 0xd7) && buf[pos + 1] !== 0xff) break;
    if (b === 0) { if (++run >= 32 && buf[pos - 1] !== 0xff) return `a run of 32 or more zero bytes inside the scan data (the entropy-coded data of a ${kind} JPEG)`; } else run = 0;
    pos++;
  }
  return null;
}
function nextMarker(buf, pos) { while (pos < buf.length) { const b = buf[pos]; if (b === 0xff && buf[pos + 1] !== 0 && !(buf[pos + 1] >= 0xd0 && buf[pos + 1] <= 0xd7) && buf[pos + 1] !== 0xff) break; pos++; } return pos; }
export function validateJpegScan(buf) {
  const tables = {}; let sof = null, ri = 0, p = 2; let progressive = false;
  const build = (counts, syms) => {
    const maxcode = new Array(18).fill(-1), valptr = new Array(17).fill(0), mincode = new Array(17).fill(0); let code = 0, k = 0;
    for (let l = 1; l <= 16; l++) { valptr[l] = k; mincode[l] = code; code += counts[l - 1]; k += counts[l - 1]; maxcode[l] = counts[l - 1] ? code - 1 : -1; code <<= 1; }
    return { maxcode, valptr, mincode, syms };
  };
  while (p + 4 <= buf.length) {
    if (buf[p] !== 0xff) return 'marker structure broken at byte ' + p;
    const m = buf[p + 1];
    if (m === 0xff) { p++; continue; }
    if (m === 0xd9) return null;
    if ((m >= 0xd0 && m <= 0xd7) || m === 0x01 || m === 0xd8) { p += 2; continue; }
    const len = buf.readUInt16BE(p + 2);
    if (len < 2 || p + 2 + len > buf.length) return 'a segment runs past the end of the file';
    const seg = buf.subarray(p + 4, p + 2 + len);
    if (m === 0xc4) {
      let q = 0;
      while (q + 17 <= seg.length) { const tc = seg[q] >> 4, th = seg[q] & 15; const counts = Array.from(seg.subarray(q + 1, q + 17)); const n = counts.reduce((a, b) => a + b, 0); if (q + 17 + n > seg.length) return 'DHT segment is cut short'; tables[tc * 4 + th] = build(counts, seg.subarray(q + 17, q + 17 + n)); q += 17 + n; }
    } else if (m === 0xdd) ri = seg.readUInt16BE(0);
    else if (m === 0xc0 || m === 0xc1) {
      if (seg[0] !== 8) return null;   // 12-bit: not decoded
      const h = seg.readUInt16BE(1), w = seg.readUInt16BE(3), nf = seg[5]; const comps = [];
      for (let i = 0; i < nf; i++) comps.push({ id: seg[6 + i * 3], h: seg[7 + i * 3] >> 4, v: seg[7 + i * 3] & 15 });
      sof = { h, w, comps };
    } else if (m === 0xc2) progressive = true;
    else if (m >= 0xc9 && m <= 0xcf) return null;   // arithmetic coding
    if (m === 0xda) {
      if (!sof) { if (progressive) { /* heuristic below */ } else return 'SOS before an SOF0/SOF1 header'; }
      const ns = seg[0]; const sc = []; for (let i = 0; i < ns; i++) sc.push({ id: seg[1 + i * 2], td: seg[2 + i * 2] >> 4, ta: seg[2 + i * 2] & 15 });
      let pos = p + 2 + len;
      if (progressive) {
        // heuristic only: scan the entropy data up to the next real marker for a long zero run
        const zr = scanZeroRun(buf, pos, 'progressive'); if (zr) return zr;
        pos = nextMarker(buf, pos); p = pos; continue;
      }
      { const zr = scanZeroRun(buf, pos, 'baseline'); if (zr) return zr; }   // gates7 item 11: before the decode, the same 32-byte zero-run rule the progressive path has
      const hmax = Math.max(...sof.comps.map((c) => c.h)), vmax = Math.max(...sof.comps.map((c) => c.v));
      const mcuW = 8 * hmax, mcuH = 8 * vmax; let mcus, blocksOf;
      const cs = sc.map((s) => { const c = sof.comps.find((x) => x.id === s.id); return c ? { ...s, h: c.h, v: c.v } : null; });
      if (cs.some((c) => !c)) return 'SOS names a component that SOF does not define';
      if (ns === 1) { const c = cs[0]; const bw = Math.ceil(Math.ceil(sof.w * c.h / hmax) / 8), bh = Math.ceil(Math.ceil(sof.h * c.v / vmax) / 8); mcus = bw * bh; blocksOf = [1]; }
      else { mcus = Math.ceil(sof.w / mcuW) * Math.ceil(sof.h / mcuH); blocksOf = cs.map((c) => c.h * c.v); }
      if (!mcus) return 'SOF has a zero size';
      let bitbuf = 0, bitcnt = 0, fake = 0; const pred = cs.map(() => 0); let bad = null;
      const getBit = () => {
        if (!bitcnt) {
          if (pos >= buf.length) { fake++; bitbuf = 0; bitcnt = 8; }
          else { let b = buf[pos]; if (b === 0xff) { const nb = buf[pos + 1]; if (nb === 0) { pos += 2; } else { fake++; b = 0; /* marker: no data */ bitbuf = 0; bitcnt = 8; return (bitbuf >> --bitcnt) & 1; } } else pos++; bitbuf = b; bitcnt = 8; }
        }
        return (bitbuf >> --bitcnt) & 1;
      };
      const getBits = (n) => { let v = 0; for (let i = 0; i < n; i++) v = (v << 1) | getBit(); return v; };
      const huff = (tb) => { let code = 0; for (let l = 1; l <= 16; l++) { code = (code << 1) | getBit(); if (tb.maxcode[l] >= 0 && code <= tb.maxcode[l] && code >= tb.mincode[l]) return tb.syms[tb.valptr[l] + code - tb.mincode[l]]; } bad = 'invalid Huffman code in the scan data'; return 0; };
      for (let mcu = 0; mcu < mcus && !bad; mcu++) {
        if (ri && mcu && mcu % ri === 0) {
          bitcnt = 0; while (pos < buf.length && buf[pos] === 0xff && buf[pos + 1] === 0xff) pos++;
          if (buf[pos] === 0xff && buf[pos + 1] >= 0xd0 && buf[pos + 1] <= 0xd7) { pos += 2; pred.fill(0); } else return `restart marker missing at MCU ${mcu} (scan data damaged)`;
        }
        for (let ci = 0; ci < cs.length && !bad; ci++) {
          const dc = tables[cs[ci].td], ac = tables[4 + cs[ci].ta];
          if (!dc || !ac) return 'a scan uses a Huffman table that no DHT defines';
          for (let b = 0; b < blocksOf[ci] && !bad; b++) {
            const s = huff(dc); if (s > 16) return 'DC category out of range (scan data damaged)'; if (s) getBits(s);
            for (let k = 1; k < 64 && !bad;) { const rs = huff(ac); const r = rs >> 4, sz = rs & 15; if (sz === 0) { if (r === 15) { k += 16; continue; } break; } k += r; if (k > 63) return 'AC run past the end of a block (scan data damaged)'; getBits(sz); k++; }
          }
        }
        if (fake > 16) return 'scan data ends before the image is complete (truncated or damaged)';
      }
      if (bad) return bad + ' (scan data damaged)';
      if (fake > 7) return 'scan data ends before the image is complete (truncated or damaged)';
      bitcnt = 0;
      for (;;) { while (pos < buf.length && buf[pos] === 0xff && buf[pos + 1] === 0xff) pos++; if (buf[pos] === 0xff && buf[pos + 1] >= 0xd0 && buf[pos + 1] <= 0xd7) { pos += 2; continue; } break; }   // a restart marker after the last MCU is legal
      if (!(buf[pos] === 0xff && buf[pos + 1] !== 0)) return `the scan decodes completely but does not end at a marker (data left over from byte ${pos}: scan data damaged)`;
      p = pos; continue;
    }
    p += 2 + len;
  }
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
  const z = buf.includes(ZERO_BLOCK) ? longestZeroRun(buf) : 0;   // native search first: the byte loop only runs on a suspect file
  if (z >= ZERO_RUN) return `a run of ${z} zero bytes (>= 64 KB: wiped or half-written)`;
  if (/\.jpe?g$/i.test(name)) { const r = validateJpegScan(buf); if (r) return r; }
  return null;
}

export function walkFiles(root) {
  const out = [];
  const walk = (d, rel) => {
    let ents; try { ents = fs.readdirSync(d, { withFileTypes: true }); } catch { return; }
    for (const e of ents) {
      const f = path.join(d, e.name), r = rel ? rel + '/' + e.name : e.name;
      if (e.isSymbolicLink()) continue;                  // a link is never followed
      if (e.isDirectory()) { if (!rel && /^_damaged/i.test(e.name)) continue; walk(f, r); } else if (e.isFile()) out.push({ rel: r, abs: f });   // _damaged: the quarantine of known-bad files, neither validated nor mirrored
    }
  };
  walk(root, '');
  return out;
}

// Validates every file. With the last manifest: a file whose size and mtime match its entry and whose sha1 matches was valid at the last mirror (not decoded again); one whose size
// and mtime match but whose sha1 differs changed in place without a normal write (silent corruption: refused); anything else is validated in full. Fills f.size, f.mtime, f.sha1.
export function validateTree(root, files = walkFiles(root), man = null, { trustStat = false, stats = null } = {}) {
  const bad = [];
  const prev = man && man.files ? man.files : {};
  for (const f of files) {
    // gates7 item 12 (verify-only): a file whose size and modification time equal its manifest entry is not read at all; the manifest's hash stands in (the mirror run and --full still hash it)
    if (trustStat) {
      const pe0 = prev[f.rel]; let st0 = null; try { st0 = fs.statSync(f.abs); } catch { /* read below reports it */ }
      if (pe0 && st0 && pe0[0] === st0.size && Math.abs(pe0[1] - Math.round(st0.mtimeMs)) <= 1) { f.size = st0.size; f.mtime = Math.round(st0.mtimeMs); f.sha1 = pe0[2]; if (stats) stats.trusted++; continue; }
    }
    if (stats) stats.read++;
    let buf; try { buf = fs.readFileSync(f.abs); } catch (e) { bad.push({ rel: f.rel, why: 'unreadable: ' + e.message }); continue; }
    const st = fs.statSync(f.abs); f.size = st.size; f.mtime = Math.round(st.mtimeMs); f.sha1 = crypto.createHash('sha1').update(buf).digest('hex');
    const pe = prev[f.rel];
    if (pe && pe[0] === f.size && Math.abs(pe[1] - f.mtime) <= 1) {
      if (pe[2] === f.sha1) continue;   // unchanged since the last validated mirror
      bad.push({ rel: f.rel, why: 'content changed although size and modification time did not (silent corruption: the manifest sha1 of the last mirror differs)' }); continue;
    }
    const why = validateFile(buf, f.rel);
    if (why) bad.push({ rel: f.rel, why });
  }
  return bad;
}

function readManifest(backup) { try { return JSON.parse(fs.readFileSync(path.join(backup, MANIFEST), 'utf8')); } catch { return null; } }

// --accept-loss is the MAIN SESSION's decision (gates5 audit item 27b): it is honoured only when the file <state>/accept_loss exists (the state guard refuses any write by a subagent into
// .claude/state, so only the main session can create it), is not older than an hour, and is then consumed. The reason on the command line must still be 20+ characters.
export function acceptLossAllowed(stateDir) {
  try { const f = path.join(stateDir, 'accept_loss'); const st = fs.statSync(f); return Date.now() - st.mtimeMs < 3600 * 1000; } catch { return false; }
}
function stateDirOf(main) { return process.env.CP_GATE_STATE_DIR ? path.resolve(process.env.CP_GATE_STATE_DIR) : path.join(main, '.claude', 'state'); }

// Returns { ok, refused: string[], copied, removed, total }. Never throws for a refusal.
export function backup({ main, backupDir = DEFAULT_BACKUP, dryRun = false, verifyOnly = false, full = false, acceptLoss = '', stateDir = stateDirOf(main) }) {
  const src = path.join(main, 'docs', 'reference');
  const res = { ok: false, refused: [], copied: 0, removed: 0, total: 0, skippedUnchanged: 0, keptDamagedCopies: 0, trusted: 0, read: 0 };
  if (!fs.existsSync(src)) { res.refused.push(`${src} does not exist`); return res; }
  const rs = path.resolve(src).toLowerCase(), rb = path.resolve(backupDir).toLowerCase();
  if (rb === rs || rb.startsWith(rs + path.sep) || rs.startsWith(rb + path.sep)) { res.refused.push('the backup folder and docs/reference must be separate folders (neither inside the other)'); return res; }
  const files = walkFiles(src); res.total = files.length;
  if (!files.length) { res.refused.push('docs/reference holds no files: refusing to mirror an empty folder over the backup'); return res; }
  const man = readManifest(backupDir);
  const vstats = { trusted: 0, read: 0 };
  const bad = validateTree(src, files, man, { trustStat: verifyOnly && !full, stats: vstats }); res.trusted = vstats.trusted; res.read = vstats.read;
  if (bad.length) { res.refused.push(`${bad.length} file(s) failed validation, NOTHING was mirrored:\n   ` + bad.slice(0, 40).map((b) => `${b.rel}: ${b.why}`).join('\n   ') + (bad.length > 40 ? `\n   ... and ${bad.length - 40} more` : '')); res.bad = bad; return res; }
  const prevCount = man && Number.isInteger(man.count) ? man.count : null;
  const wantsLoss = acceptLoss.trim().length >= 20;
  const lossOk = wantsLoss && acceptLossAllowed(stateDir);
  if (acceptLoss && !lossOk) res.notes = [wantsLoss ? `--accept-loss ignored: ${path.join(stateDir, 'accept_loss')} does not exist or is older than an hour (only the main session creates it)` : '--accept-loss ignored: the reason must be 20+ characters'];
  if (prevCount && files.length < prevCount * (1 - MAX_LOSS) && !lossOk) {
    res.refused.push(`docs/reference holds ${files.length} file(s), the last backup recorded ${prevCount} (more than ${MAX_LOSS * 100} % fewer): something has been deleted. NOTHING was mirrored. If the loss is intended, the MAIN SESSION creates ${path.join(stateDir, 'accept_loss')} and reruns with --accept-loss "<reason of 20+ characters>".`);
    return res;
  }
  // good photos moved into _damaged count as loss: the quarantine is for files that FAIL validation, so a file that was in the last manifest, is gone from the folder and sits in _damaged
  // in a state that validates is a good photo hidden away (zero tolerance, not inside the 5 % limit)
  const have = new Map(files.map((f) => [f.rel, f]));
  const dmgRoot = path.join(src, '_damaged'); const damaged = [];
  const walkDmg = (d, rel) => { let ents; try { ents = fs.readdirSync(d, { withFileTypes: true }); } catch { return; } for (const e of ents) { if (e.isSymbolicLink()) continue; const f = path.join(d, e.name); if (e.isDirectory()) walkDmg(f, rel + e.name + '/'); else if (e.isFile()) damaged.push({ rel: rel + e.name, abs: f, base: e.name.toLowerCase() }); } };
  walkDmg(dmgRoot, '');
  const inDamaged = (rel) => damaged.filter((d) => d.rel.toLowerCase() === rel.toLowerCase() || d.base === path.basename(rel).toLowerCase());
  const goodMoved = [];
  if (man && man.files) for (const rel of Object.keys(man.files)) {
    if (have.has(rel)) continue;
    for (const d of inDamaged(rel)) { let b; try { b = fs.readFileSync(d.abs); } catch { continue; } if (!validateFile(b, d.rel)) { goodMoved.push(`${rel} (now _damaged/${d.rel})`); break; } }
  }
  if (goodMoved.length && !lossOk) {
    res.refused.push(`${goodMoved.length} photo(s) that the last backup recorded are gone from docs/reference but sit in _damaged in a state that validates (a good photo hidden in the quarantine counts as loss; move it back, or the MAIN SESSION creates ${path.join(stateDir, 'accept_loss')} and reruns with --accept-loss "<reason>"):\n   ` + goodMoved.slice(0, 20).join('\n   '));
    return res;
  }
  if (verifyOnly) { res.ok = true; return res; }
  // plan
  const existing = fs.existsSync(backupDir) ? walkFiles(backupDir).filter((f) => f.rel !== MANIFEST) : [];
  let toRemove = existing.filter((f) => !have.has(f.rel));
  // a file now in _damaged (it failed validation there): keep the backup's copy when that copy is good, it may be the only good one
  const keep = (f) => inDamaged(f.rel).length > 0 && (() => { try { return !validateFile(fs.readFileSync(f.abs), f.rel); } catch { return false; } })();
  const kept = toRemove.filter(keep); toRemove = toRemove.filter((f) => !kept.includes(f)); res.keptDamagedCopies = kept.length;
  if (existing.length && toRemove.length > existing.length * MAX_LOSS && !lossOk) {
    res.refused.push(`the mirror would remove ${toRemove.length} of ${existing.length} files from the backup (more than ${MAX_LOSS * 100} %). NOTHING was changed. The MAIN SESSION creates ${path.join(stateDir, 'accept_loss')} and reruns with --accept-loss "<reason of 20+ characters>" if intended.`);
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
    const mfiles = {}; for (const f of files) mfiles[f.rel] = [f.size, f.mtime, f.sha1];
    fs.writeFileSync(path.join(backupDir, MANIFEST), JSON.stringify({ ts: new Date().toISOString(), count: files.length, names_sha1: sha, source: src, accepted_loss: lossOk ? acceptLoss.trim() : undefined, files: mfiles }, null, 1));
    if (lossOk) { try { fs.unlinkSync(path.join(stateDir, 'accept_loss')); } catch { /* already gone */ } }
  }
  res.ok = true;
  return res;
}

async function cli() {
  const a = process.argv.slice(2); const get = (n) => { const i = a.indexOf(n); return i >= 0 ? a[i + 1] : undefined; };
  let main = get('--main');
  if (!main) { const { resolveMainRoot } = await import('./hooks/root.mjs'); main = resolveMainRoot(path.dirname(fileURLToPath(import.meta.url))); }
  const r = backup({ main, backupDir: get('--backup') || DEFAULT_BACKUP, dryRun: a.includes('--dry-run'), verifyOnly: a.includes('--verify-only'), full: a.includes('--full'), acceptLoss: get('--accept-loss') || '' });
  if (r.notes) console.log(r.notes.join('\n'));
  if (!r.ok) { console.log('REFUSED (the backup was not changed):\n ' + r.refused.join('\n ')); process.exit(1); }
  console.log(`${a.includes('--verify-only') ? 'verified' : a.includes('--dry-run') ? 'dry run' : 'mirrored'}: ${r.total} file(s) in docs/reference all valid${a.includes('--verify-only') ? ` (${r.trusted} trusted by size+mtime against the manifest, ${r.read} read)` : ''}; copied ${r.copied}, unchanged ${r.skippedUnchanged}, removed from backup ${r.removed}${r.keptDamagedCopies ? `, ${r.keptDamagedCopies} good backup copies kept because main moved the file to _damaged` : ''}.`);
}
if (process.argv[1] && path.resolve(process.argv[1]) === path.resolve(fileURLToPath(import.meta.url))) cli().catch((e) => { console.error('ERROR: ' + (e && e.stack || e)); process.exit(2); });
void os;
