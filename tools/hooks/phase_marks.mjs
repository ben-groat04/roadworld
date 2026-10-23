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
// A NO-CONTRACT launch that is really a review of another agent's work. Clerks, mergers, researchers and read-only status checks mention audits in passing ("Write audit first-look rule into
// docs", "Clerk: ... commit review", "Merger: verify playtest, merge integ to main"): only the JOB itself is matched.
// gates12 (main-session decision 3, after the gates11 audit passed "Auditing gates9", "Second-opinion reviewer of gates11", "Opus look at grade-r12 images" and "Independent verification of gates9"
// as NO-CONTRACT): refused BY MEANING, not by the exact wording of the 23 Oct launches. A NO-CONTRACT launch is refused when
//  1. the DESCRIPTION opens (after fresh / independent / opus / cold / second / strict / short ...) with a review word: audit, auditor, auditing, review, reviewer, reviewing, second opinion, cold read,
//     first look, verification, "look at|over", or "verify ..." that goes on to name a round, hand-in, images or a round id;
//  2. or the DESCRIPTION holds a review word anywhere (audit*, review*, verif*, first look, second opinion, cold read, look at|over) AND names the thing reviewed: a hand-in, round, progress folder,
//     renders, images, composites, frames, screenshots or a round id such as gates9, grade-r12, m2fix5a (a letter-led word ending in digits). "Write audit first-look rule into docs", "Add
//     every-audit-item-fixed rule to docs" and "Clerk: ... commit review" name no such thing and pass;
//  3. or the stated REASON itself opens with the review job ("independent cold-read audit of ...", "fresh auditor for ...", "short first-look image check ..."); a clerk's reason that only says
//     "no build or review hand-in" does not open that way;
//  4. or a LINE of the PROMPT opens with the order (audit, auditor, reviewer, review, verify, first look, second opinion) and names a docs/progress/<round> folder.
// Allowed by this rule: clerks, mergers (merge, "verify playtest"), research and fetch jobs, measure agents, and read-only status checks that review nothing.
// The reason text is NOT searched for the words anywhere: clerk reasons say "no build or review hand-in" (gates11 decision 1).
const LEAD = String.raw`^\s*(?:(?:fresh|independent|opus|cold|cold-read|second|another|strict|final|short|quick|read-only|new|separate)[ -]+)*`;
const VERB = String.raw`(?:audit(?:or|ors|ing|ed)?|review(?:er|ers|ing|ed)?|re-review|second[- ]opinion|cold[- ]read(?:er|ing)?|first[- ]look|verification|look\s+(?:at|over))`;
const WORD = String.raw`(?:audit(?:or|ors|ing|ed)?|review(?:er|ers|ing|ed)?|re-review|second[- ]opinion|cold[- ]read(?:er|ing)?|first[- ]look|verif(?:y|ies|ied|ication|ying|ier)|look\s+(?:at|over))`;
const THING = String.raw`(?:hand-?ins?|rounds?|progress|renders?|images?|composites?|frames?|screenshots?|[a-z][a-z-]*\d+[a-z]?)`;
const RE_HEAD = new RegExp(LEAD + VERB + String.raw`\b`, 'i');
const RE_HEAD_VERIFY = new RegExp(LEAD + String.raw`verif(?:y|ying|ier)\b[^\n]*\b` + THING + String.raw`\b`, 'i');
const RE_WORD = new RegExp(String.raw`\b` + WORD + String.raw`\b`, 'i');
const RE_THING = new RegExp(String.raw`\b` + THING + String.raw`\b`, 'i');
const RE_REASON = new RegExp(LEAD + String.raw`(?:audit(?:or|ors|ing)?|review(?:er|ers|ing)?|second[- ]opinion|cold[- ]read|first[- ]look|verification)\b`, 'i');
const RE_LOOKISH = /first[- ]look/i;
const PROG = String.raw`[\`"']?\S*docs[\\/]+progress[\\/]+[\w.-]+`;
const ORDER = String.raw`(?:audit(?:or|ing)?|review(?:er|ing)?|second[- ]opinion|verif(?:y|ier|ication))`;
// the PROMPT names a folder to review only when a LINE of it opens with the order ("Audit <folder>", "Auditor: ...", "You are the reviewer of <folder>"); a clerk line that merely says that an
// audit of a folder was accepted does not.
const RE_AUDIT_PROMPT = new RegExp(String.raw`^[ \t>*-]*(?:you are (?:the |an? )?(?:fresh |independent )?)?` + ORDER + String.raw`\b[^\n]{0,60}?` + PROG, 'im');
const RE_LOOK_PROMPT = new RegExp(String.raw`^[ \t>*-]*(?:you are (?:the |an? )?(?:opus )?)?first[- ]look\b[^\n]{0,60}?` + PROG, 'im');
export function disguisedJob(description, reason, prompt = '') {
  const d = String(description || '').trim(); const p = String(prompt || ''); const r = String(reason || '').trim();
  const kind = (s) => (RE_LOOKISH.test(s) ? 'first look' : 'audit');
  if (RE_LOOK_PROMPT.test(p)) return 'first look';
  if (RE_HEAD.test(d) || RE_HEAD_VERIFY.test(d)) return kind(d);
  if (RE_WORD.test(d) && RE_THING.test(d)) return kind(d);
  if (r && RE_REASON.test(r)) return kind(r);
  if (RE_AUDIT_PROMPT.test(p)) return 'audit';
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
  const nb = text.match(/^\W*what here would never be built[ \t]*:?[ \t]*(.*)$/mi);
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
