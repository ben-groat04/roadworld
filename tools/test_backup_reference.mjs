#!/usr/bin/env node
// Tests for tools/backup_reference.mjs on scratch folders (never the real photos). Expected values: the rules written in the script header (main session decision 23 Oct 2026:
// validate every picture before mirroring, 64 KB zero runs and bad headers/trailers refused, more than 5 % loss refused). The fixtures mimic what the real incident and the real
// photo folder show: a PNG whose first bytes are zeros but whose end is intact, a JPEG without its EOI, a PNG with a damaged chunk (all three found in the real docs/reference
// on 23 Oct 2026 by `--verify-only`, 302 of 421 files).
// Usage: node tools/test_backup_reference.mjs  (exit 1 on any failure)
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import zlib from 'node:zlib';
import { spawnSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';
import { backup, validateFile, validatePng, validateJpeg, longestZeroRun } from './backup_reference.mjs';

const here = path.dirname(fileURLToPath(import.meta.url));
let pass = 0, fail = 0; const failed = [];
const t = (name, cond, detail = '') => { if (cond) { pass++; console.log('  ok   ' + name); } else { fail++; failed.push(name); console.log('  FAIL ' + name + (detail ? '\n       ' + String(detail).replace(/\n/g, '\n       ').slice(0, 900) : '')); } };
const tmp = fs.mkdtempSync(path.join(os.tmpdir(), 'bkref-'));

const chunk = (type, data) => { const len = Buffer.alloc(4); len.writeUInt32BE(data.length); const body = Buffer.concat([Buffer.from(type, 'latin1'), data]); const crc = Buffer.alloc(4); crc.writeUInt32BE(zlib.crc32(body) >>> 0); return Buffer.concat([len, body, crc]); };
const png = (w, h, seed = 1) => {
  const raw = Buffer.alloc((w * 3 + 1) * h);
  for (let y = 0; y < h; y++) for (let x = 0; x < w; x++) { const o = y * (w * 3 + 1) + 1 + x * 3; raw[o] = (x * 7 + seed) & 255; raw[o + 1] = (y * 5 + seed * 3) & 255; raw[o + 2] = (x * y + seed) & 255; }
  const ihdr = Buffer.alloc(13); ihdr.writeUInt32BE(w, 0); ihdr.writeUInt32BE(h, 4); ihdr[8] = 8; ihdr[9] = 2;
  return Buffer.concat([Buffer.from([0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a]), chunk('IHDR', ihdr), chunk('IDAT', zlib.deflateSync(raw)), chunk('IEND', Buffer.alloc(0))]);
};
const jpeg = (dataLen = 300, seed = 1, eoi = true) => {
  const data = Buffer.alloc(dataLen); for (let i = 0; i < dataLen; i++) data[i] = 1 + ((i * 31 + seed) % 250);
  const app0 = Buffer.concat([Buffer.from([0xff, 0xe0, 0x00, 0x10]), Buffer.from('JFIF\0\x01\x01\x00\x00\x01\x00\x01\x00\x00')]);
  const sof = Buffer.from([0xff, 0xc0, 0x00, 0x0b, 8, 0, 8, 0, 8, 1, 1, 0x11, 0]);
  const sos = Buffer.from([0xff, 0xda, 0x00, 0x08, 1, 1, 0, 0, 0x3f, 0]);
  return Buffer.concat([Buffer.from([0xff, 0xd8]), app0, sof, sos, data, eoi ? Buffer.from([0xff, 0xd9]) : Buffer.alloc(0)]);
};
const put = (root, rel, buf) => { const f = path.join(root, 'docs', 'reference', ...rel.split('/')); fs.mkdirSync(path.dirname(f), { recursive: true }); fs.writeFileSync(f, buf); return f; };
const fresh = (name, n = 40) => { const main = path.join(tmp, name, 'main'), bk = path.join(tmp, name, 'backup'); for (let i = 0; i < n; i++) put(main, `aerial/p${i}.${i % 2 ? 'png' : 'jpg'}`, i % 2 ? png(16 + i, 12, i) : jpeg(300 + i, i)); put(main, 'aerial/INDEX.md', Buffer.from('index\n')); return { main, bk }; };
const count = (d) => { let n = 0; const walk = (p) => { for (const e of fs.readdirSync(p, { withFileTypes: true })) { if (e.isDirectory()) walk(path.join(p, e.name)); else n++; } }; if (fs.existsSync(d)) walk(d); return n; };

console.log('validators (fixtures built like the real damage)');
t('a real PNG (CRCs, inflated size, IEND) validates', validatePng(png(20, 10)) === null);
t('a PNG with zeros for its first 16 bytes but an intact end (the real damage): refused, bad signature', /bad signature/.test(validatePng(Buffer.concat([Buffer.alloc(16), png(20, 10).subarray(16)]))));
{ const b = png(20, 10); b[b.length - 20] ^= 0xff; t('a PNG with one damaged byte inside a chunk: refused, bad CRC', /bad CRC/.test(validatePng(b))); }
t('a PNG cut off before IEND: refused', /truncated|IEND/.test(validatePng(png(20, 10).subarray(0, png(20, 10).length - 12))));
{ const w = 20, h = 10; const raw = Buffer.alloc((w * 3 + 1) * (h - 3)); const ihdr = Buffer.alloc(13); ihdr.writeUInt32BE(w, 0); ihdr.writeUInt32BE(h, 4); ihdr[8] = 8; ihdr[9] = 2; const b = Buffer.concat([Buffer.from([0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a]), chunk('IHDR', ihdr), chunk('IDAT', zlib.deflateSync(raw)), chunk('IEND', Buffer.alloc(0))]); t('a PNG whose pixel data is 3 rows short of its header: refused, decoded size', /decoded size/.test(validatePng(b))); }
t('a JPEG with SOI, SOF, SOS and EOI validates', validateJpeg(jpeg()) === null);
t('a JPEG without EOI (the real truncated files): refused', /no EOI/.test(validateJpeg(jpeg(300, 1, false))));
t('a JPEG that does not start with SOI: refused', /no SOI/.test(validateJpeg(Buffer.concat([Buffer.from([0, 0]), jpeg().subarray(2)]))));
t('a JPEG whose entropy data is a 70 KB zero run: refused by the zero-run rule', /zero bytes/.test(validateFile(Buffer.concat([jpeg().subarray(0, jpeg().length - 2), Buffer.alloc(70000), Buffer.from([0xff, 0xd9])]), 'x.jpg') || ''));
t('longestZeroRun counts the longest run', longestZeroRun(Buffer.from([1, 0, 0, 0, 2, 0, 0, 3])) === 3);
t('a zero-length file of any type is refused', validateFile(Buffer.alloc(0), 'INDEX.md') === 'zero-length file');
t('a text file with content passes (only pictures are decoded)', validateFile(Buffer.from('hello'), 'INDEX.md') === null);

console.log('mirror');
{
  const { main, bk } = fresh('good');
  let r = backup({ main, backupDir: bk });
  t('41 valid files: mirrored, backup holds 41 files plus the manifest', r.ok && r.copied === 41 && count(bk) === 42 && fs.existsSync(path.join(bk, '.backup_manifest.json')), JSON.stringify(r));
  r = backup({ main, backupDir: bk }); t('a second run copies nothing (all unchanged)', r.ok && r.copied === 0 && r.skippedUnchanged === 41, JSON.stringify(r));
  const before = fs.readFileSync(path.join(bk, 'aerial', 'p3.png'));
  // damage one photo in main: the zero-headed PNG of the real incident
  put(main, 'aerial/p3.png', Buffer.concat([Buffer.alloc(16), png(19, 12, 3).subarray(16)]));
  r = backup({ main, backupDir: bk });
  t('one damaged photo in main: NOTHING mirrored, the refusal names the file and the reason', !r.ok && /aerial\/p3\.png: not a PNG/.test(r.refused.join()) && /NOTHING was mirrored/.test(r.refused.join()), JSON.stringify(r));
  t('...and the good copy in the backup is untouched (a bad file never replaces a good one)', fs.readFileSync(path.join(bk, 'aerial', 'p3.png')).equals(before));
  put(main, 'aerial/p3.png', png(19, 12, 3));
  put(main, 'aerial/p4.jpg', jpeg(300, 4, false));
  r = backup({ main, backupDir: bk }); t('a truncated JPEG in main: refused', !r.ok && /p4\.jpg: no EOI/.test(r.refused.join()), JSON.stringify(r));
  put(main, 'aerial/p4.jpg', jpeg(310, 4));
  const bad = png(19, 12, 9); bad[40] ^= 1; put(main, 'aerial/p5.png', bad);
  r = backup({ main, backupDir: bk }); t('a PNG with a damaged chunk: refused', !r.ok && /p5\.png/.test(r.refused.join()), JSON.stringify(r));
  put(main, 'aerial/p5.png', png(21, 12, 5));
  r = backup({ main, backupDir: bk }); t('everything repaired: mirrored again, only the file that really differs (p4) is copied', r.ok && r.copied === 1 && fs.readFileSync(path.join(bk, 'aerial', 'p4.jpg')).equals(jpeg(310, 4)), JSON.stringify(r));
  const vo = backup({ main, backupDir: bk, verifyOnly: true }); t('--verify-only validates without writing', vo.ok && vo.copied === 0);
}
{
  const { main, bk } = fresh('loss');
  let r = backup({ main, backupDir: bk });
  fs.unlinkSync(path.join(main, 'docs', 'reference', 'aerial', 'p0.jpg'));
  r = backup({ main, backupDir: bk });
  t('1 of 41 files gone (2.4 %): allowed, and the file is removed from the backup (a mirror)', r.ok && r.removed === 1 && !fs.existsSync(path.join(bk, 'aerial', 'p0.jpg')), JSON.stringify(r));
  for (let i = 1; i < 6; i += 2) fs.unlinkSync(path.join(main, 'docs', 'reference', 'aerial', `p${i}.png`));
  for (let i = 2; i < 8; i += 2) fs.unlinkSync(path.join(main, 'docs', 'reference', 'aerial', `p${i}.jpg`));
  const before = count(bk);
  r = backup({ main, backupDir: bk });
  t('6 more files gone (15 %): REFUSED, backup unchanged, says what to do', !r.ok && /fewer/.test(r.refused.join()) && /--accept-loss/.test(r.refused.join()) && count(bk) === before, JSON.stringify(r));
  r = backup({ main, backupDir: bk, acceptLoss: 'short' }); t('--accept-loss with a short reason: still refused', !r.ok);
  r = backup({ main, backupDir: bk, acceptLoss: 'the owner deleted these on purpose after review' }); t('--accept-loss with a real reason (20+ chars): mirrored, reason written into the manifest', r.ok && /owner deleted/.test(fs.readFileSync(path.join(bk, '.backup_manifest.json'), 'utf8')), JSON.stringify(r));
}
{
  const { main } = fresh('empty', 3);
  for (const f of fs.readdirSync(path.join(main, 'docs', 'reference', 'aerial'))) fs.unlinkSync(path.join(main, 'docs', 'reference', 'aerial', f));
  const r = backup({ main, backupDir: path.join(tmp, 'empty', 'backup') });
  t('an empty docs/reference is never mirrored over a backup', !r.ok && /no files/.test(r.refused.join()), JSON.stringify(r));
  const r2 = backup({ main: path.join(tmp, 'nomain'), backupDir: path.join(tmp, 'x') }); t('a missing docs/reference: refused', !r2.ok);
  const { main: m3 } = fresh('nested', 3);
  const r3 = backup({ main: m3, backupDir: path.join(m3, 'docs', 'reference', 'bk') }); t('a backup folder inside docs/reference: refused', !r3.ok && /separate folders/.test(r3.refused.join()));
}
{
  const { main, bk } = fresh('quarantine', 4);
  put(main, '_damaged/broken.png', Buffer.alloc(100)); put(main, '_damaged/sub/also.jpg', Buffer.from('x'));
  const r = backup({ main, backupDir: bk });
  t('the top-level _damaged quarantine is neither validated nor mirrored (the real docs/reference holds 284 known-bad files there)', r.ok && r.total === 5 && !fs.existsSync(path.join(bk, '_damaged')), JSON.stringify(r));
}
console.log('command line');
{
  const { main, bk } = fresh('cli', 5);
  const run = (...a) => spawnSync(process.execPath, [path.join(here, 'backup_reference.mjs'), '--main', main, '--backup', bk, ...a], { encoding: 'utf8' });
  let r = run(); t('CLI: valid tree: exit 0, prints the counts', r.status === 0 && /mirrored: 6 file/.test(r.stdout), r.stdout + r.stderr);
  put(main, 'aerial/p0.jpg', jpeg(300, 0, false));
  r = run(); t('CLI: a damaged file: exit 1, REFUSED, names it', r.status === 1 && /REFUSED/.test(r.stdout) && /p0\.jpg/.test(r.stdout), r.stdout + r.stderr);
  const src = fs.readFileSync(path.join(here, 'backup_reference.mjs'), 'utf8').replace(/^\s*\/\/.*$/gm, '');
  t('the script has no recursive delete (no rmSync, no rm -r)', !/rmSync|\brm\s+-r|recursive:\s*true\s*,\s*force/.test(src.replace(/mkdirSync\([^)]*recursive: true[^)]*\)/g, '')));
}
console.log(`\n${pass} passed, ${fail} failed (temp dir ${tmp})`);
if (failed.length) console.log('FAILED TESTS:\n - ' + failed.join('\n - '));
if (!process.env.KEEP_BKTEST) fs.rmSync(tmp, { recursive: true, force: true });
process.exit(fail ? 1 : 0);
