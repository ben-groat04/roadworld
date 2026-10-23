// Phase agents and first looks (gates10): the markers a brief carries and the first-look file check. Used by brief_gate (launch) and handin_gate (finish); the measure and build
// checks themselves live in tools/phase_lib.mjs (run through check_handin --phase).
//
// Markers (a line that STARTS with the word, so the contract text that mentions them mid-line does not trigger):
//   PHASE: measure | build | render        a builder round split into short fresh agents (make_brief --phase)
//   FIRSTLOOK-OF: <absolute progress folder>   an Opus first-look agent (make_brief --first-look): reads images, writes FIRSTLOOK_OPUS.md, nothing else
import fs from 'node:fs';
import path from 'node:path';
import { imageDecodable } from '../image_lib.mjs';

export const FIRSTLOOK_FILE = 'FIRSTLOOK_OPUS.md';
export const PHASES = ['measure', 'build', 'render'];
export function phaseIn(text) {
  const m = String(text || '').match(/^PHASE:[ \t]*(measure|build|render)[ \t]*$/mi);
  return m ? m[1].toLowerCase() : null;
}
export function firstLookOfIn(text) {
  const m = String(text || '').match(/^FIRSTLOOK-OF:[ \t]*(.+?)[ \t]*$/m);
  return m ? m[1].replace(/^`|`$/g, '') : null;
}
// A NO-CONTRACT launch that is really an audit or a first look. The description or the stated reason names it. Clerks, mergers, researchers and read-only checks mention audits in
// passing ("follows the gates9 STATUS merge checklist", "records round states"): only the job itself is matched.
export function disguisedJob(description, reason) {
  const d = String(description || ''), r = String(reason || '');
  const firstLook = /first[- ]?look/i.test(d) || /first[- ]?look/i.test(r) || /FIRSTLOOK_OPUS/i.test(r);
  const audit = /^\s*(?:fresh\s+|opus\s+|independent\s+)?audit(?:or|ing)?\b/i.test(d) || /\bauditor\s+for\b/i.test(d) || /\b(?:fresh|independent)\s+auditor\b/i.test(r) || /\baudit\s+job\b/i.test(r) || /^\s*(?:an?\s+)?auditor\b/i.test(r) || /\bwrites?\s+AUDIT\.md\b/i.test(r);
  return firstLook ? 'first look' : audit ? 'audit' : null;
}

// ---- the first-look file
const IMG = /\.(?:png|jpe?g|webp|gif|bmp)$/i;
const clean = (p) => p.trim().replace(/^[`"'(\[]+|[`"')\].,;]+$/g, '');
export function checkFirstLookFile(folder, { actualId = null, notBefore = 0, main = null } = {}) {
  const probs = [];
  const file = path.join(folder, FIRSTLOOK_FILE);
  let text;
  try { text = fs.readFileSync(file, 'utf8').replace(/\r\n/g, '\n'); } catch { return [`${file} does not exist: write it (template docs/templates/FIRSTLOOK.md)`]; }
  try { if (notBefore > 0 && fs.statSync(file).mtimeMs < notBefore) probs.push(`${FIRSTLOOK_FILE} is older than this agent's run (written before ${new Date(notBefore).toISOString()}): write your own`); } catch { /* read above */ }
  const idm = text.match(/^First-look agent:[ \t]*(\S+)/mi);
  if (!idm) probs.push('no line "First-look agent: <your agent id>" (the id is at the top of your context)');
  else if (actualId && idm[1] !== actualId) probs.push(`"First-look agent: ${idm[1]}" is not your agent id (${actualId})`);
  const om = text.match(/^Opened:[ \t]*(.+)$/mi);
  const bases = [folder, path.dirname(folder), main, main && path.join(main, 'docs', 'reference'), main && path.join(main, 'docs', 'reference', '_compare')].filter(Boolean);
  if (!om) probs.push('no line "Opened: <image path>, <image path>, ..." naming the images and photos you looked at');
  else {
    const paths = [...new Set(om[1].split(/\s*[,;]\s*|\s{2,}/).map(clean).filter((x) => IMG.test(x)))];
    const found = []; const missing = [];
    for (const p of paths) { const r = path.isAbsolute(p) ? p : bases.map((b) => path.join(b, p)).find((x) => fs.existsSync(x)); if (r && fs.existsSync(r)) found.push(path.resolve(r)); else missing.push(p); }
    if (missing.length) probs.push('Opened: names files that do not exist: ' + missing.slice(0, 5).join(', '));
    if (found.length < 3) probs.push(`Opened: names ${found.length} existing image(s); a first look opens at least 3 (the headline images and at least one reference photo)`);
    else {
      const n = (p) => p.replace(/\\/g, '/').toLowerCase();
      const f0 = n(path.resolve(folder)), cmp = '/docs/reference/_compare/';
      const own = found.filter((p) => n(p).startsWith(f0 + '/') || n(p).includes(cmp) || n(p).includes('/docs/progress/'));
      if (!own.length) probs.push('Opened: names no picture of this round (under the progress folder or docs/reference/_compare): the first look is of the round\'s headline images');
      if (own.length === found.length) probs.push('Opened: names no reference photo (a first look compares the round\'s images with the photos)');
      const bad = found.filter((p) => !imageDecodable(p));
      if (bad.length) probs.push('Opened: these are not complete decodable pictures: ' + bad.slice(0, 3).join(', '));
    }
  }
  const t3 = text.split('\n'); const hi = t3.findIndex((l) => /^\W*top[ -]?3\b/i.test(l));
  if (hi < 0) probs.push('no "Top 3:" heading followed by three numbered lines (1. 2. 3.)');
  else {
    const items = []; for (let i = hi + 1; i < t3.length && items.length < 3; i++) { const m = /^\s*(?:[-*]\s*)?([1-3])[.)]\s+(\S.*)$/.exec(t3[i]); if (m) items.push(m); else if (/^\W*what here would never be built/i.test(t3[i])) break; }
    const sub = items.filter((m) => m[2].replace(/[^A-Za-z0-9]/g, '').length >= 20);
    if (items.length < 3 || sub.length < 3) probs.push('"Top 3:" needs three numbered lines (1. 2. 3.), each a real observation (20+ letters)');
  }
  const nb = text.match(/^\W*what here would never be built\W*:?[ \t]*(.*)$/mi);
  if (!nb) probs.push('no line "What here would never be built: <answer>"');
  else {
    const idx = t3.findIndex((l) => /^\W*what here would never be built/i.test(l));
    const answer = (nb[1] + ' ' + t3.slice(idx + 1).join(' ')).replace(/[^A-Za-z0-9 ]/g, '');
    if (answer.trim().length < 20) probs.push('"What here would never be built:" is empty or a placeholder: say what it is');
  }
  return probs;
}

// Read order (the brief says: do not open AUDIT.md, STATUS.md or FAULTS.md before the images). The agent's transcript is read in order: a tool call that names one of those files before the
// first call that names an image is refused. Returns a problem text or null; any parse trouble returns null (the file check above is the gate, this one only adds order).
export function readOrderProblem(transcriptPath) {
  try {
    const lines = fs.readFileSync(transcriptPath, 'utf8').split('\n').filter(Boolean);
    let firstImg = -1, firstBad = -1, badName = '';
    for (let i = 0; i < lines.length; i++) {
      let o; try { o = JSON.parse(lines[i]); } catch { continue; }
      const content = o && o.message && o.message.content;
      if (!Array.isArray(content)) continue;
      for (const c of content) {
        if (!c || c.type !== 'tool_use') continue;
        const s = JSON.stringify(c.input || {});
        if (firstImg < 0 && /\.(?:png|jpe?g|webp)\b/i.test(s) && /^(?:Read|Bash|PowerShell)$/.test(c.name)) firstImg = i;
        const m = /(?:^|[\\/"'\s])((?:AUDIT|STATUS|FAULTS)\.md)\b/i.exec(s.replace(/\\\\/g, '/'));
        if (firstBad < 0 && m && /^(?:Read|Bash|PowerShell|Grep)$/.test(c.name) && !/FIRSTLOOK/i.test(m[0])) { firstBad = i; badName = m[1]; }
      }
    }
    if (firstBad >= 0 && (firstImg < 0 || firstBad < firstImg)) return `the transcript shows ${badName} was opened before the first image: a first look is done on the pictures alone, before any claim is read. Redo the first look from the images and say so in the file`;
  } catch { /* unreadable: no order verdict */ }
  return null;
}
