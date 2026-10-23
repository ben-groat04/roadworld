#!/usr/bin/env node
// Hand-in check. Usage: node tools/check_handin.mjs <progress-folder> [--main <main-checkout>] [--no-visual] [--accept [--builder <agent-id>] [--legacy "<reason>"]] | node tools/check_handin.mjs <progress-folder> --checkpoint
// --checkpoint (gates8): validates <folder>/CHECKPOINT.md instead of a hand-in (first line `CHECKPOINT: <what is done> | images: <path>[, <path>...]`, at least one existing image). The finish hook lets a valid one through once per agent and does not count it as a hand-in.
// --accept (the main session, before accepting a round): ALSO fails unless a valid AUDIT.md by an agent other than the recorded builder exists AND
// the finish gate's signed audit record backs it (tools/hooks/audit_lib.mjs). Every path needs the record: --builder only names the builder, it
// never replaces the record; a hand-in that predates the audit gate needs --legacy "<reason, 20+ chars>" (logged to gate.log). Builders never pass it.
// Fails (exit 1) with a list unless: STATUS.md starts with the "Skipped, refused or failed" heading with a body;
// STATUS.md names >=3 existing reference files per "Case:" line; FAULTS.md has >=2 "Pass" sections and a Residual
// section; STATUS.md has "References opened: N of M" (N < M needs a "not opened" line with a file name in the Skipped section);
// --no-visual skips the COMPARE.md checks (engine-only work); COMPARE.md has a table whose files exist (composites under <main>/docs/reference/_compare/).
// With a gates-version 2 brief (GATES-VERSION: 2 line, passed by the hook as --gates-version 2 --gate "<line>"...) the work is MEASURED as well: see tools/gates_lib.mjs.
// A STATUS whose open items are all in the BLOCKED form ("BLOCKED: <item> | tried: ... | blocked by: ... | needs: ...") passes; each is printed as a
// "BLOCKED-ITEM:" line on stdout, which the hand-in hook logs as news for the main session.
import fs from 'node:fs';
import path from 'node:path';
import { execSync, spawnSync } from 'node:child_process';
import { imageDims, imageEndOk } from './image_lib.mjs';

const args = process.argv.slice(2);
const noVisual = args.includes('--no-visual');
const mi = args.indexOf('--main');
const accept = args.includes('--accept');
const bi = args.indexOf('--builder');
const si = args.indexOf('--since');
const since = si >= 0 ? Number(args[si + 1]) : 0;
const builderArg = bi >= 0 ? args[bi + 1] : null;
const mainArg = mi >= 0 ? args[mi + 1] : null;
// Gates v2 (tools/gates_lib.mjs): switched on by the brief's `GATES-VERSION: 2`; the hook passes the brief's `GATE:` lines as --gate "<line>".
// --task <task-file> reads the "## Gates" section of a task file instead (manual runs). --worktree overrides the worktree root.
const VALUE_OPTS = ['--main', '--builder', '--since', '--gates-version', '--gate', '--task', '--worktree', '--legacy', '--prior-audit', '--item', '--phase', '--suite', '--refs', '--case', '--allow-fail'];
const optAll = (n) => args.flatMap((a, i) => (a === n ? [args[i + 1]] : []));
const gatesVersion = Number(optAll('--gates-version')[0] || 1);
const gateLines = optAll('--gate');
const taskArg = optAll('--task')[0];
const worktreeArg = optAll('--worktree')[0];
const legacyArg = optAll('--legacy')[0];
const priorAuditArg = optAll('--prior-audit')[0];   // previous AUDIT.md of a send-back round: STATUS must carry the "Prior audit items" table (contract item 17)
// --item "<id>|<text>": an item the brief lists (ITEM: lines); STATUS "## Items" must hold a row for each (done with proof, or a valid BLOCKED line)
const briefItems = optAll('--item').map((x) => { const k = String(x).indexOf('|'); return k > 0 ? { id: x.slice(0, k).trim(), text: x.slice(k + 1).trim() } : { id: String(x).trim(), text: '' }; });
const gatesV2 = gatesVersion >= 2 || !!taskArg;
const gatesV3 = gatesVersion >= 3;   // v3 (23 Oct 2026): BLOCKED lines carry see: <log file> and the quoted error must be found in it; "needs:" must name a thing
const folder = args.find((a, i) => !a.startsWith('--') && !(i > 0 && VALUE_OPTS.includes(args[i - 1])));
if (!folder) {
  console.error('usage: node tools/check_handin.mjs <progress-folder> [--main <path>] [--no-visual] [--accept [--builder <id>] [--legacy "<reason>"]] [--since <epoch-ms>] [--gates-version 2 [--gate "<GATE: line>"]... | --task <task-file>] [--worktree <root>] [--prior-audit <AUDIT.md>]');
  process.exit(2);
}

function mainCheckout() {
  if (mainArg) return path.resolve(mainArg);
  const cwds = [fs.existsSync(folder) ? path.resolve(folder) : null, process.cwd()].filter(Boolean);
  for (const cwd of cwds) {
    try {
      const common = execSync("git rev-parse --path-format=absolute --git-common-dir", { cwd, stdio: ["ignore", "pipe", "ignore"] }).toString().trim();
      return path.dirname(common);
    } catch { /* try next */ }
  }
  return process.cwd();
}
const main = mainCheckout();
const refRoot = path.join(main, 'docs', 'reference');
// The hand-in's own worktree root (so prototype screenshots / spec docs under docs/progress/... resolve).
function worktreeRoot() {
  const abs = path.resolve(folder);
  const fwd = abs.split(path.sep).join('/');
  const k = fwd.toLowerCase().indexOf('/docs/progress/');
  if (k >= 0) return fwd.slice(0, k);
  try { return execSync("git rev-parse --show-toplevel", { cwd: fs.existsSync(abs) ? abs : process.cwd(), stdio: ["ignore", "pipe", "ignore"] }).toString().trim(); } catch { return main; }
}
const wtRoot = worktreeArg ? path.resolve(worktreeArg) : worktreeRoot();
// gates6 (audit item 24): the BLOCKED evidence file must be this round's own output: inside this worktree or its scratch folder, newer than the round's start (--since, else
// CP_ROUND_START, else the creation time of the worktree's .git file), and not a tracked file. Only for gates v3.
const scratchBase = path.join(path.dirname(main), 'roadworld-scratch');
const roundStart = (() => { if (since > 0) return since; if (Number(process.env.CP_ROUND_START) > 0) return Number(process.env.CP_ROUND_START); try { const st = fs.statSync(path.join(wtRoot, '.git')); return st.birthtimeMs || 0; } catch { return 0; } })();
const ownRoots = (() => { const names = new Set([path.basename(wtRoot), path.basename(path.resolve(folder))].filter(Boolean)); const out = [wtRoot]; try { for (const e of fs.readdirSync(scratchBase, { withFileTypes: true })) if (e.isDirectory() && [...names].some((n) => e.name === n || e.name.startsWith(n + '-') || e.name.startsWith(n + '_'))) out.push(path.join(scratchBase, e.name)); } catch { /* no scratch folder */ } return out; })();
const blockedCtx = { version: gatesV3 ? 3 : 2, bases: [path.resolve(folder), wtRoot, path.dirname(wtRoot), scratchBase, main], ...(gatesV3 ? { roundStart, ownRoots, repoRoot: wtRoot } : {}) };
// An opened reference resolves if it is (a) an absolute path that exists, (b) relative to the main checkout or the hand-in's worktree, or (c) a name under docs/reference.
const refBases = [refRoot, main, wtRoot];
const fails = [];
const read = (f) => {
  const p = path.join(folder, f);
  return fs.existsSync(p) ? fs.readFileSync(p, 'utf8').replace(/\r\n/g, '\n') : null;
};
const stripComments = (s) => s.replace(/<!--[\s\S]*?-->/g, '');
const compareRows = [];
const refCandidates = new Set();   // absolute paths of reference files named by the round (STATUS Case lines, COMPARE.md reference cells): provenance-checked in the v2 gates
function resolveFile(name, bases) {
  name = name.trim().replace(/^`|`$/g, '');
  if (!name) return null;
  if (path.isAbsolute(name)) return fs.existsSync(name) ? name : null;
  const b = bases.find((x) => fs.existsSync(path.join(x, name)));
  return b ? path.join(b, name) : null;
}

function findFile(name, bases) {
  name = name.trim().replace(/^`|`$/g, '');
  if (!name) return false;
  if (path.isAbsolute(name)) return fs.existsSync(name);
  return bases.some((b) => fs.existsSync(path.join(b, name)));
}

// Libraries load dynamically: a broken one is a logged FAIL-OPEN, never a crash before any line is written (gates2 audit fault 9).
let GL = null, hooksLib = null;
try { hooksLib = await import('./hooks/lib.mjs'); } catch (e) { console.error('FAIL-OPEN: hooks/lib.mjs could not be loaded: ' + (e && e.message)); }
try { GL = await import('./gates_lib.mjs'); } catch (e) { if (gatesV2) console.error('FAIL-OPEN: gates_lib could not be loaded: ' + (e && e.message)); }
const failOpen = (what, e) => {
  if (accept) fails.push('GATE could not be measured (' + what + '): ' + String((e && e.message) || e).slice(0, 300) + ' (--accept does not fail open: measure it or say why not)');
  const m = 'FAIL-OPEN: gate "' + what + '" did not run: ' + String((e && e.message) || e).slice(0, 300);
  console.error(m);
  try { hooksLib && hooksLib.log({ gate: 'handin_gates', decision: 'allow', reason: m, handin_dir: folder }); } catch { /* log only */ }
};
if (gatesV2 && !GL) failOpen('gates_lib', new Error('gates_lib.mjs did not load'));

// --since <epoch ms> (passed by the hand-in gate): the files below must have been written during the agent's run, so a stale
// passing folder from an earlier round cannot let a new agent stop at once.
if (since > 0) {
  const stale = ['STATUS.md', ...(noVisual && !gatesV2 ? [] : ['FAULTS.md']), ...(noVisual ? [] : ['COMPARE.md'])].filter((f) => { try { return fs.statSync(path.join(folder, f)).mtimeMs < since; } catch { return false; } });
  if (stale.length) fails.push('older than this agent\'s run (written before ' + new Date(since).toISOString() + '; a hand-in must be written by the agent that hands it in): ' + stale.join(', '));
}

// --checkpoint (gates8): validate a CHECKPOINT.md instead of a hand-in. A builder may stop early with <folder>/CHECKPOINT.md whose first line is
// `CHECKPOINT: <what is done> | images: <path>[, <path>...]` and at least one of the named paths is an existing image. The finish hook lets such a stop through once per agent and does NOT
// count it as a hand-in (no audit owed); the final hand-in is still checked in full.
// the frame size from the first bytes of an image file, or null when the header is not valid
if (args.includes('--checkpoint')) {
  const cp = read('CHECKPOINT.md');
  const why = [];
  if (cp === null) why.push('no CHECKPOINT.md in ' + folder);
  else {
    const first = cp.split('\n').find((l) => l.trim()) || '';
    const m = first.trim().match(/^CHECKPOINT:\s*(.+?)\s*\|\s*images:\s*(.+)$/i);
    if (!m) why.push('the first line must be: CHECKPOINT: <what is done> | images: <path>[, <path>...] (got "' + first.trim().slice(0, 120) + '")');
    else {
      if (m[1].replace(/[^A-Za-z0-9]/g, '').length < 8) why.push('"what is done" is empty or a placeholder: say what is finished');
      const paths = m[2].split(/\s*[,;]\s*|\s{2,}/).map((x) => x.trim().replace(/^`|`$/g, '').replace(/^["']|["']$/g, '')).filter(Boolean);
      const imgs = paths.filter((p) => /\.(?:png|jpe?g|webp|gif|bmp)$/i.test(p));
      // gates9 (gates8 audit H4): the image must be this round's own picture: inside the hand-in folder or docs/reference/_compare/<round> (the round = the hand-in folder name), written during
      // this run (--since), at least 10 KB, and carry a valid PNG / JPEG / GIF / BMP / WEBP header with a frame size. A reference photo, a system icon or an old render does not count.
      const round = path.basename(path.resolve(folder));
      const realOf = (p) => { try { return fs.realpathSync(p); } catch { return path.resolve(p); } };
      const nrm = (p) => realOf(p).replace(/\\/g, '/').toLowerCase();
      const okDirs = [path.resolve(folder), path.join(refRoot, '_compare', round), path.join(wtRoot, 'docs', 'reference', '_compare', round)].flatMap((d) => [d, realOf(d)]).map((d) => d.replace(/\\/g, '/').toLowerCase().replace(/\/$/, ''));
      const problemsOf = (p) => {
        const r = resolveFile(p, [path.resolve(folder), wtRoot, main, refRoot]);
        if (!r) return 'does not exist';
        const n = nrm(r);
        if (!okDirs.some((d) => n.startsWith(d + '/'))) return 'is not inside the hand-in folder (' + path.resolve(folder) + ') or docs\\reference\\_compare\\' + round + ' (a checkpoint image is a picture this round made)';
        let st; try { st = fs.statSync(r); } catch { return 'cannot be read'; }
        if (since > 0 && st.mtimeMs < since) return 'is older than this agent\'s run (written before ' + new Date(since).toISOString() + '): a picture from an earlier round is not a checkpoint';
        if (st.size < 10240) return 'is only ' + st.size + ' bytes: under 10 KB, not a real render';
        let head; try { const fd = fs.openSync(r, 'r'); head = Buffer.alloc(Math.min(st.size, 65536)); fs.readSync(fd, head, 0, head.length, 0); fs.closeSync(fd); } catch { return 'cannot be read'; }
        const dims = imageDims(head);
        if (!dims) return 'has no valid PNG / JPEG / GIF / BMP / WEBP header (a real picture is needed)';
        if (!imageEndOk(r)) return 'is cut off (no end marker: PNG IEND, JPEG EOI, GIF trailer, or a size bigger than the file): a truncated picture is not a real render';
        return null;
      };
      const results = imgs.map((p) => [p, problemsOf(p)]);
      if (!imgs.length) why.push('"images:" names no image path (.png .jpg .jpeg .webp .gif .bmp)');
      else if (!results.some((x) => !x[1])) {
        if (results.every((x) => x[1] === 'does not exist')) why.push('none of the named images exists: ' + imgs.join(', '));
        else for (const [p, pr] of results) why.push('image ' + p + ' ' + pr);
      }
    }
    if (since > 0) { try { if (fs.statSync(path.join(folder, 'CHECKPOINT.md')).mtimeMs < since) why.push('CHECKPOINT.md is older than this agent\'s run (written before ' + new Date(since).toISOString() + ')'); } catch { /* checked above */ } }
  }
  if (why.length) { console.error('CHECKPOINT FAILED\n - ' + why.join('\n - ')); process.exit(1); }
  console.log('CHECKPOINT PASSED (' + path.join(folder, 'CHECKPOINT.md') + '): this is not a hand-in; the final hand-in still needs the full check');
  process.exit(0);
}

// --phase measure|build (gates10): a phase agent of a split round hands over through MEASURE.md / BUILD.md, checked by tools/phase_lib.mjs. The render phase is the ordinary hand-in below.
// The output uses the same PASSED / FAILED lines as the full check, so the finish hook reads it unchanged.
{
  const ph = optAll('--phase')[0];
  if (ph === 'measure' || ph === 'build') {
    let PL = null;
    try { PL = await import('./phase_lib.mjs'); } catch (e) { console.error('FAIL-OPEN: phase_lib.mjs could not be loaded: ' + (e && e.message)); process.exit(1); }
    const lines = [];
    const probs = ph === 'measure'
      ? PL.measureProblems(folder, { since, main, refs: optAll('--refs')[0] !== undefined ? Number(optAll('--refs')[0]) : null, cases: optAll('--case').length ? optAll('--case') : null })
      : PL.buildProblems(folder, wtRoot, { since, allowFail: optAll('--allow-fail'), GL, log: (...a) => lines.push(a.join(' ')) });
    lines.forEach((l) => console.error('phase check: ' + l));
    if (probs.length) { console.error(`HAND-IN CHECK FAILED (${probs.length}) for ${folder} (phase ${ph}):\n - ` + probs.join('\n - ')); process.exit(1); }
    console.log(`HAND-IN CHECK PASSED for ${folder} (phase ${ph})`);
    process.exit(0);
  }
}

// STATUS.md
const status = read('STATUS.md');
let skipBodyAll = '';
if (status === null) {
  fails.push('STATUS.md is missing');
} else {
  const s = stripComments(status);
  const heads = [...s.matchAll(/^##\s+(.*)$/gm)];
  if (!heads.length || !/^skipped, refused or failed/i.test(heads[0][1].trim())) {
    fails.push('STATUS.md: first "##" heading must be "Skipped, refused or failed"');
  } else {
    const start = heads[0].index + heads[0][0].length;
    const end = heads[1] ? heads[1].index : s.length;
    const skipBody = s.slice(start, end);
    skipBodyAll = skipBody;
    if (skipBody.trim().length < 10) fails.push('STATUS.md: "Skipped, refused or failed" section is empty');
    const ro = s.match(/References opened:\s*(\d+)\s+of\s+(\d+)/i);
    if (!ro) fails.push('STATUS.md: no "References opened: N of M" line (integers)');
    else if (+ro[1] < +ro[2]) {
      const lines = skipBody.split('\n').filter((l) => /not opened/i.test(l) && /[\w-]+\.\w{2,5}\b/.test(l));
      if (!lines.length) fails.push(`STATUS.md: References opened ${ro[1]} of ${ro[2]}: the Skipped section needs a "not opened" line naming each unopened file with a reason`);
    }
  }
  const cases = [...s.matchAll(/^\s*Case:\s*(.*?)\s*\|\s*Opened:\s*(.*)$/gim)];
  if (!cases.length) fails.push('STATUS.md: no "Case: <name> | Opened: <file>, <file>, <file>" lines');
  for (const c of cases) {
    const files = [...new Set(c[2].split(',').map((x) => x.trim()).filter(Boolean))];
    const ok = files.filter((f) => findFile(f, refBases));
    const bad = files.filter((f) => !ok.includes(f));
    if (ok.length < 3) {
      fails.push(`STATUS.md: case "${c[1]}" names ${ok.length} existing reference files, need at least 3` +
        (bad.length ? ` (not found as absolute path, under ${wtRoot}, ${main} or ${refRoot}: ${bad.join(', ')})` : ''));
    } else if (bad.length) {
      fails.push(`STATUS.md: case "${c[1]}": files not found (absolute, under ${wtRoot}, ${main} or ${refRoot}): ${bad.join(', ')}`);
    }
    if (GL && gatesV2) {
      const abs = ok.map((f) => resolveFile(f, refBases)).filter(Boolean);
      abs.forEach((a) => refCandidates.add(a));
      try { GL.distinctReferenceProblems(c[1], abs).forEach((p) => fails.push(p)); } catch (e) { failOpen('distinct references', e); }
    }
  }
}

// FAULTS.md
const faults = noVisual ? '' : read('FAULTS.md');
if (noVisual) { /* skipped */ } else if (faults === null) {
  fails.push('FAULTS.md is missing');
} else {
  const passes = [...faults.matchAll(/^##\s+Pass\s+\d+/gim)].length;
  if (passes < 2) fails.push(`FAULTS.md: ${passes} "## Pass N" section(s), need at least 2`);
  const idx = faults.search(/^##\s+Residual/im);
  if (idx < 0) {
    fails.push('FAULTS.md: no "## Residual" section');
  } else {
    const rest = faults.slice(idx).split('\n').slice(1);
    const next = rest.findIndex((l) => /^##\s/.test(l));
    const body = (next < 0 ? rest : rest.slice(0, next)).join('\n').trim();
    if (body.length < 5) fails.push('FAULTS.md: "## Residual" is empty (list what remains, or "none" and why)');
  }
}

// COMPARE.md
const compare = noVisual ? '' : read('COMPARE.md');
if (noVisual) { /* skipped */ } else if (compare === null) {
  fails.push('COMPARE.md is missing' + (GL && gatesV2 ? ' (' + GL.BLOCKED_HINT + ')' : ''));
} else {
  const rows = compare.split('\n')
    .filter((l) => l.trim().startsWith('|') && !/^\s*\|[\s:|-]+\|\s*$/.test(l))
    .slice(1);
  if (!rows.length) fails.push('COMPARE.md: no table rows (case | render | reference | composite | before-same-camera | subject/type match | scale match | viewpoint match)' + (GL && gatesV2 ? '; ' + GL.BLOCKED_HINT : ''));
  rows.forEach((r, i) => {
    const cells = r.split('|').slice(1, -1).map((c) => c.trim());
    if (!gatesV2) {
      // Briefs written before gates v2 (no GATES-VERSION line) used the 5-column layout: case | render | reference | composite | before.
      if (cells.length < 5) { fails.push(`COMPARE.md row ${i + 1}: needs at least 5 columns (case | render | reference | composite | before-same-camera), got ${cells.length}`); return; }
      const [kase5, render5, ref5, comp5, before5] = cells;
      const bases5 = [folder, path.join(refRoot, '_compare'), refRoot, main];
      if (!findFile(render5, bases5)) fails.push(`COMPARE.md row "${kase5}": render not found: ${render5}`);
      if (!findFile(ref5, refBases)) fails.push(`COMPARE.md row "${kase5}": reference not found: ${ref5}`);
      if (!findFile(comp5, bases5)) fails.push(`COMPARE.md row "${kase5}": composite not found (expected under docs/reference/_compare/<topic>/): ${comp5}`);
      if (!findFile(before5, bases5)) fails.push(`COMPARE.md row "${kase5}": before image not found: ${before5}`);
      return;
    }
    if (cells.length < 8) { fails.push(`COMPARE.md row ${i + 1}: needs 8 columns (case | render | reference | composite | before-same-camera | subject/type match | scale match | viewpoint match), got ${cells.length}`); return; }
    const [kase, render, ref, comp, before, mSubject, mScale, mView] = cells;
    // The reference must be relevant (owner, 11 Oct: "it isn't even getting relevant ref images!!!"): each row says, in words, that
    // reference and render show the same kind of subject, at the same scale, from the same viewpoint.
    for (const [label, v] of [['subject/type', mSubject], ['scale', mScale], ['viewpoint', mView]]) {
      if (v.length < 12 || /^(no|not|differs|mismatch|n.a|none|unknown|tbd|[?])([^a-z0-9]|$)/i.test(v)) {
        fails.push(`COMPARE.md row "${kase}": the ${label} match is missing or says it does not match ("${v}"); state that reference and render match in subject/type, scale and viewpoint (12+ characters, e.g. "two-lane urban road, same junction type"), or replace the reference. If they do not match, ${GL ? GL.BLOCKED_HINT : 'finish with a BLOCKED line in STATUS.md'}`);
      } else if (GL && GL.PLACEHOLDER_RE.test(v)) {
        fails.push(`COMPARE.md row "${kase}": the ${label} match cell is a placeholder ("${v}"): ${GL.BLOCKED_HINT}`);
      }
    }
    if (mSubject === mScale && mScale === mView) fails.push(`COMPARE.md row "${kase}": the subject, scale and viewpoint cells are identical ("${mSubject}"): one sentence copied three times is not a comparison; ${GL ? GL.BLOCKED_HINT : 'finish with a BLOCKED line in STATUS.md'}`);
    const bases = [folder, path.join(refRoot, '_compare'), refRoot, main];
    if (!findFile(render, bases)) fails.push(`COMPARE.md row "${kase}": render not found: ${render}`);
    if (!findFile(ref, refBases)) fails.push(`COMPARE.md row "${kase}": reference not found: ${ref}`);
    if (!findFile(comp, bases)) fails.push(`COMPARE.md row "${kase}": composite not found (expected under docs/reference/_compare/<topic>/): ${comp}`);
    if (!findFile(before, bases)) fails.push(`COMPARE.md row "${kase}": before image not found: ${before}`);
    const row = { kase, render: resolveFile(render, bases), reference: resolveFile(ref, refBases), composite: resolveFile(comp, bases), before: resolveFile(before, bases), unreal: !!(GL && GL.isUnrealRow && GL.isUnrealRow(cells)) };
    compareRows.push(row);
    if (row.reference) refCandidates.add(row.reference);
  });
  // different files by content, in every role and across rows (a copy of one image in four columns is the fake the gates3 audit built)
  if (GL && gatesV2) { try { GL.compareIntegrity(compareRows).forEach((p) => fails.push('GATE picture: ' + p)); } catch (e) { failOpen('compare integrity', e); } }
  if (GL && GL.unrealFrameProblems) { try { GL.unrealFrameProblems(compareRows, since, { folder: path.resolve(folder), compareDirs: [path.join(refRoot, '_compare', path.basename(path.resolve(folder))), path.join(wtRoot, 'docs', 'reference', '_compare', path.basename(path.resolve(folder)))], statusText: status || '', resolve: (p) => resolveFile(p, [path.resolve(folder), wtRoot, main, refRoot, scratchBase]) }).forEach((p) => fails.push('GATE picture: ' + p)); } catch (e) { failOpen('unreal frames', e); } }
}

// ---- Wrapper gate (any brief version): a round that adds a sim_core/src/*.cpp needs its Unreal wrapper (tools/check_wrappers.mjs).
try {
  const git = (cwd, ...a) => spawnSync('git', a, { cwd, encoding: 'utf8', timeout: 30000 });
  if (git(wtRoot, 'rev-parse', '--show-toplevel').status === 0) {
    const headMain = git(main, 'rev-parse', 'HEAD');
    const mb = headMain.status === 0 ? git(wtRoot, 'merge-base', 'HEAD', String(headMain.stdout).trim()) : { status: 1 };
    if (mb.status === 0) {
      const base = String(mb.stdout).trim();
      const added = [...String(git(wtRoot, 'diff', '--name-only', '--diff-filter=AR', base, '--', 'sim_core/src').stdout).split('\n'), ...String(git(wtRoot, 'ls-files', '--others', '--exclude-standard', '--', 'sim_core/src').stdout).split('\n')].filter((f) => /^sim_core\/src\/[^/]+\.cpp$/.test(f));
      if (added.length) {
        const cw = await import('./check_wrappers.mjs');
        const wp = cw.wrapperProblems(wtRoot);
        if (wp.error) console.error('note: wrapper check skipped: ' + wp.error);
        else wp.missing.forEach((m) => fails.push('WRAPPER GATE (this round adds ' + added.join(', ') + '): ' + m));
      }
    }
  }
} catch (e) { failOpen('wrapper gate', e); }

// ---- Gates v2: measured, not read (tools/gates_lib.mjs). Only for briefs that carry GATES-VERSION: 2.
const gateNotes = [];
let blockedItems = [];
if (gatesV2 && GL) {
  try { blockedItems = GL.parseBlocked(status === null ? '' : status, blockedCtx); } catch (e) { failOpen('BLOCKED reader', e); }
  let lines = gateLines;
  if (taskArg) {
    try {
      const t = fs.readFileSync(taskArg, 'utf8').replace(/\r\n/g, '\n');
      const m = t.match(/^##\s+Gates\s*\n([\s\S]*?)(?=^##\s|(?![\s\S]))/m);
      lines = (m ? m[1] : '').split('\n').filter((l) => { const g = GL.parseGateLine(l); return g && g.kind !== 'bad'; });
    } catch (e) { failOpen('task file', e); }
  }
  const gates = GL.parseGates(lines).filter((g) => g.kind !== 'bad');
  // A: measured gates
  try {
    const mg = GL.runMeasuredGates(gates, wtRoot, () => {});
    mg.problems.forEach((p) => fails.push('GATE: ' + p));
    mg.errors.forEach((e) => failOpen(e.split(':')[0], e));
    mg.notes.forEach((n) => gateNotes.push(n));
    mg.blocked.forEach((b) => gateNotes.push('FLOOR MISS ALLOWED by the task file: ' + b));
    for (const sf of mg.shortfalls) {
      const key = sf.metric.split(' ').pop().replace(/_/g, '[ _]');
      if (!new RegExp(key, 'i').test(skipBodyAll)) fails.push(`GATE: ${sf.metric} measured ${sf.measured}, below the target ${sf.target} (at or above the floor, so it may pass) but the "Skipped, refused or failed" section of STATUS.md does not mention it: state the shortfall there, first`);
      else gateNotes.push(`${sf.metric} ${sf.measured} is below the target ${sf.target}; STATUS names it`);
    }
  } catch (e) { failOpen('measured gates', e); }
  // B: waiver guard
  try {
    const d = GL.collectWaiverDiff(wtRoot);
    if (d.error) failOpen('waiver guard', d.error);
    else {
      const allowed = gates.filter((g) => g.kind === 'allow').map((g) => g.key);
      const w = GL.waiverFindings(d.text, allowed, d.contents);
      w.refused.forEach((f) => fails.push(`GATE waiver guard (${f.file}): ${f.what}. Remove it and fix the cause; only a task-file gate line "allow_waiver ${f.key}" lets it through`));
      if (w.findings.length) gateNotes.push(`waiver guard: ${w.findings.length} finding(s) against merge base ${d.base.slice(0, 8)}, ${w.allowedHits.length} allowed by the task file`);
      else gateNotes.push(`waiver guard: no new waiver, skip, should_fail, loosened tolerance or deleted test against ${d.base.slice(0, 8)}`);
    }
  } catch (e) { failOpen('waiver guard', e); }
  // C: picture sanity (visible rounds only)
  if (!noVisual) {
    try {
      const num = (n) => { const g = gates.find((x) => x.kind === 'picture' && x.name === n); return g ? g.value : undefined; };
      const pp = GL.pictureProblems(folder, compare, { minRoad: num('picture_min_road'), maxDebug: num('picture_max_debug'), statusText: status || '' });
      pp.problems.forEach((p) => fails.push('GATE picture: ' + p));
      pp.notes.forEach((n) => gateNotes.push('picture ' + n));
    } catch (e) { failOpen('picture sanity', e); }
  }
  // C2: reference provenance (files named as references are real, registered photos, not the round's own pictures)
  try {
    const known = GL.knownReferenceHashes(main);
    if (known === null && refCandidates.size) failOpen('reference provenance', new Error('no reference hash log or hashes in docs/reference INDEX.md under ' + main + ': run node tools/ref_seed.mjs in the main checkout once'));
    const rp = GL.referenceProblems([...refCandidates], { refRoot, folder: path.resolve(folder), known });
    rp.problems.forEach((p) => fails.push('GATE reference: ' + p));
    rp.notes.forEach((n) => gateNotes.push(n));
  } catch (e) { failOpen('reference provenance', e); }
  // D: STATUS reading (+ BLOCKED form)
  try {
    const ff = noVisual ? read('FAULTS.md') : faults;
    if (noVisual && ff === null) fails.push('FAULTS.md is missing (gates v2 asks for it on every hand-in, engine-only ones too: Pass 1..3 and Residual)');
    GL.statusProblems(status === null ? '' : status, ff === null ? null : ff, { blockedCtx }).forEach((p) => fails.push('GATE status: ' + p));
    GL.itemsProblems(status === null ? '' : status, briefItems, blockedCtx).forEach((p) => fails.push('GATE items: ' + p));
  } catch (e) { failOpen('status reading', e); }
  // prior audit items (send-back rounds)
  if (priorAuditArg) {
    try {
      const pa = fs.readFileSync(priorAuditArg, 'utf8');
      GL.priorAuditProblems(status === null ? '' : status, pa).forEach((p) => fails.push('GATE prior audit: ' + p));
    } catch (e) { failOpen('prior audit table', e); }
  }
}

// --accept: the builder/auditor split (tools/hooks/audit_lib.mjs). Every path needs the finish gate's signed audit record.
if (accept) {
  try {
    const al = await import('./hooks/audit_lib.mjs');
    const stateDir = path.join(main, '.claude', 'state');
    const rec = al.readBuilder(folder, stateDir) || al.readReleased(folder, stateDir) || al.findBuilderInLog(folder, stateDir);
    const ar = al.readAudit(folder, stateDir);
    const builderId = rec ? rec.agent_id : builderArg;
    const legacy = legacyArg && legacyArg.trim().length >= 20 ? legacyArg.trim() : null;
    if (legacyArg && !legacy) fails.push('--accept: --legacy needs a reason of at least 20 characters');
    if (builderArg && rec && al.sanitizeId(builderArg) !== al.sanitizeId(rec.agent_id)) fails.push('--accept: --builder ' + builderArg + ' is not the recorded builder of this folder (' + rec.agent_id + ')');
    if (!builderId) {
      fails.push('--accept: no builder record for this folder (.claude/state/builders of ' + main + ') and no --builder <id>: independence of the auditor cannot be shown.');
    } else {
      const probs = al.auditProblems(path.resolve(folder), { agent_id: builderId, passed_at: rec && rec.passed_at }, main, null, { stateDir, requireAccept: true, requireRecord: !legacy });
      if (!rec && builderArg && ar && ar.builder_id && ar.builder_id !== al.sanitizeId(builderArg)) probs.push('the audit record is of builder ' + ar.builder_id + ', not ' + builderArg);
      if (legacy && !probs.length) { try { hooksLib && hooksLib.log({ gate: 'accept', decision: 'legacy', reason: 'check_handin --accept --legacy: ' + legacy, handin_dir: path.resolve(folder), builder_id: builderId }); } catch { /* log only */ } }
      for (const p of probs) fails.push('--accept: AUDIT.md: ' + p);
      if (!probs.length) console.log('AUDIT.md present and independent of builder ' + builderId + ' for ' + folder + (legacy ? ' (LEGACY accept without a signed audit record: ' + legacy + ')' : ' (signed audit record verified)'));
    }
  } catch (e) { fails.push('--accept: the audit check could not run (' + (e && e.message) + ')'); }
}

// Canary (tools/hooks/notice_lib.mjs): loud warning on stderr when gate.log suggests the hooks are not running.
// Does not change the exit code (the hand-in is judged on its own); skipped when the hand-in gate itself calls this.
if (!process.env.CP_FROM_HOOK) {
  try {
    const nl = await import('./hooks/notice_lib.mjs');
    const rep = nl.canaryReport(path.join(main, '.claude', 'state'));
    // only the canary: pending gate notices are the main session's business, not the agent's
    if (rep.problems.length) console.error('\n!!! HOOK CANARY: the enforcement hooks may NOT be running (say so in your report) !!!\n' + rep.problems.map((p) => ' - ' + p).join('\n') + '\n');
  } catch { /* the canary must never break the check */ }
}

if (fails.length) {
  console.error(`HAND-IN CHECK FAILED (${fails.length}) for ${folder}:\n - ` + fails.join('\n - '));
  console.error('Send this list back to the agent unread; do not read the report.');
  process.exit(1);
}
gateNotes.forEach((n) => console.log('gate: ' + n));
// One line per valid BLOCKED item: the hand-in hook logs them (gate "blocked") and the main session gets a notice.
blockedItems.filter((b) => b.ok).forEach((b) => console.log('BLOCKED-ITEM: ' + b.raw.replace(/\s+/g, ' ').slice(0, 400)));
console.log(`HAND-IN CHECK PASSED for ${folder}`);
