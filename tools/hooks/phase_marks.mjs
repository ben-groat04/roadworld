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
// gates11 (main-session decision 1, after the gates10 audit found "Write audit first-look rule into docs" refused): only the JOB is matched, never the words anywhere in the text.
//  - the DESCRIPTION starts with an audit or first-look phrase: `^(auditor|audit(ing)? (of|for)|opus first look|first look)\b`, plus the forms the log shows for real round audits
//    ("Audit gate-notice hooks independently", "Audit T-junction round", "Audit X hand-in": `audit ... independently | round | hand-in`), a leading fresh / independent / opus allowed;
//  - or the PROMPT names a progress folder to audit ("Audit of <...>/docs/progress/<round>", "audit the hand-in folder <...>/docs/progress/<round>").
// A description that merely mentions the words later ("Write audit first-look rule into docs", "Clerk: ... first look notes") passes, as does "Audit repo organisation, read-only"
// (a read-only check of something that is not a hand-in). The reason text is not read any more.
const LEAD = String.raw`^\s*(?:(?:fresh|independent|opus)\s+)*`;
const RE_LOOK_DESC = new RegExp(LEAD + String.raw`first[- ]look\b`, 'i');
const RE_AUDIT_DESC = new RegExp(LEAD + String.raw`(?:auditor\b|audit(?:ing)?\s+(?:of|for)\b|audit(?:ing)?\b[^\n]*\b(?:independently|round|hand-?in)\b)`, 'i');
const PROG = String.raw`[\`"']?\S*docs[\\/]+progress[\\/]+[\w.-]+`;
// the PROMPT names a folder to audit only when a LINE of it opens with the audit order ("Audit <folder>", "Auditor: ...", "You are the auditor of <folder>"); a clerk line that merely says that an
// audit of a folder was accepted does not.
const RE_AUDIT_PROMPT = new RegExp(String.raw`^[ \t>*-]*(?:you are (?:the |an? )?(?:fresh |independent )?)?(?:audit(?:or|ing)?)\b[^\n]{0,60}?` + PROG, 'im');
const RE_LOOK_PROMPT = new RegExp(String.raw`^[ \t>*-]*(?:you are (?:the |an? )?(?:opus )?)?first[- ]look\b[^\n]{0,60}?` + PROG, 'im');
export function disguisedJob(description, reason, prompt = '') {
  const d = String(description || ''); void reason;
  const p = String(prompt || '');
  if (RE_LOOK_DESC.test(d) || RE_LOOK_PROMPT.test(p)) return 'first look';
  if (RE_AUDIT_DESC.test(d) || RE_AUDIT_PROMPT.test(p)) return 'audit';
  return null;
}

// ---- the first-look file
const IMG = /\.(?:png|jpe?g|webp|gif|bmp)$/i;
const clean = (p) => p.trim().replace(/^[`"'(\[]+|[`"')\].,;]+$/g, '');
// gates11: a line that is a placeholder, not an observation: <...>, TODO, tbd, under 15 characters, one character repeated, or the same word over and over
export function placeholderWhy(line) {
  const x = String(line || '').trim();
  if (/<[^<>]*>/.test(x)) return 'a <placeholder> left in';
  if (/\b(?:todo|tbd|fixme|lorem ipsum|xxx+)\b/i.test(x)) return 'a TODO / tbd marker';
  if (x.length < 15) return 'under 15 characters';
  const letters = x.toLowerCase().replace(/[^a-z0-9]/g, '');
  if (letters.length && new Set(letters).size <= 3) return 'one or two characters repeated';
  if (/(.)\1{5,}/i.test(letters)) return 'a character repeated';
  const words = x.toLowerCase().match(/[a-z0-9']+/g) || [];
  if (words.length >= 4 && new Set(words).size <= Math.max(2, Math.floor(words.length / 3))) return 'the same few words repeated';
  return null;
}
// the paths of an "Opened:" line: quoted paths may hold spaces, the rest is split at commas, semicolons or runs of spaces
export function openedPaths(line) {
  const quoted = []; let rest = String(line);
  rest = rest.replace(/"([^"]+)"|'([^']+)'|\x60([^\x60]+)\x60/g, (m, a, b, c) => { quoted.push(a || b || c); return ' , '; });
  return [...quoted, ...rest.split(/\s*[,;]\s*|\s{2,}/)];
}
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
    const paths = [...new Set(openedPaths(om[1]).map(clean).filter((x) => IMG.test(x)))];
    const found = []; const missing = [];
    for (const p of paths) { const r = path.isAbsolute(p) ? p : bases.map((b) => path.join(b, p)).find((x) => fs.existsSync(x)); if (r && fs.existsSync(r)) found.push(path.resolve(r)); else missing.push(p); }
    if (missing.length) probs.push('Opened: names files that do not exist: ' + missing.slice(0, 5).join(', '));
    if (found.length < 3) probs.push(`Opened: names ${found.length} existing image(s); a first look opens at least 3 (the headline images and at least one reference photo)`);
    else {
      const n = (p) => p.replace(/\\/g, '/').toLowerCase();
      const f0 = n(path.resolve(folder)), cmp = '/docs/reference/_compare/';
      const isPhoto = (p) => n(p).includes('/docs/reference/') && !n(p).includes(cmp);
      const own = found.filter((p) => !isPhoto(p) && (n(p).startsWith(f0 + '/') || n(p).includes(cmp) || n(p).includes('/docs/progress/')));
      const photosFound = found.filter(isPhoto);
      if (!own.length) probs.push('Opened: names no picture of this round (under the progress folder or docs/reference/_compare): the first look is of the round\'s headline images');
      if (!photosFound.length) probs.push('Opened: names no reference photo under docs/reference (not _compare): a first look compares the round\'s images with the photos, and an image from anywhere else on disk is not one');
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
    for (const m of items) { const why = placeholderWhy(m[2]); if (why) probs.push('"Top 3:" line ' + m[1] + ' is a placeholder, not an observation (' + why + ')'); }
  }
  const nb = text.match(/^\W*what here would never be built\W*:?[ \t]*(.*)$/mi);
  if (!nb) probs.push('no line "What here would never be built: <answer>"');
  else {
    const idx = t3.findIndex((l) => /^\W*what here would never be built/i.test(l));
    const answer = (nb[1] + ' ' + t3.slice(idx + 1).join(' ')).replace(/[^A-Za-z0-9 ]/g, '');
    if (answer.trim().length < 20) probs.push('"What here would never be built:" is empty or a placeholder: say what it is');
    else { const why = placeholderWhy((nb[1] + ' ' + t3.slice(idx + 1).join(' ')).trim()); if (why) probs.push('"What here would never be built:" is a placeholder (' + why + '): say what it is'); }
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
