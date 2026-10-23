// Phase checks (gates10): `check_handin --phase measure|build`. A builder round may run as up to three short fresh agents in sequence; each hands over through a file in the hand-in folder.
//   measure -> MEASURE.md   build -> BUILD.md   render -> the ordinary full hand-in (STATUS, FAULTS, COMPARE, INDEX), checked as before.
// Every function returns a list of problems (empty = passes). Nothing here quotes an agent: files are opened, images decoded, git and the suite run by the check itself.
import fs from 'node:fs';
import path from 'node:path';
import { spawnSync } from 'node:child_process';
import { imageDecodable } from './image_lib.mjs';

const IMG = /\.(?:png|jpe?g|webp|gif|bmp)\b/i;
const sh = (cmd, args, cwd, timeout = 60000) => spawnSync(cmd, args, { cwd, encoding: 'utf8', timeout, maxBuffer: 64 * 1024 * 1024 });
const stripComments = (s) => s.replace(/<!--[\s\S]*?-->/g, '');

// ---- measure
// MEASURE.md: a `References opened: N of M listed` line (N < M needs a "not opened" line); one "## Case ..." section per case, each with a measurement table (every data row names a photo file and a
// value in metres, centimetres or lane widths; at least 3 different photos per case) and at least one trace or overlay image (complete, decodable, written after the brief).
export function measureProblems(folder, { since = 0, cases = null } = {}) {
  const probs = [];
  const file = path.join(folder, 'MEASURE.md');
  let text; try { text = stripComments(fs.readFileSync(file, 'utf8').replace(/\r\n/g, '\n')); } catch { return [`MEASURE.md is missing in ${folder}: the measure phase hands over through it`]; }
  try { if (since > 0 && fs.statSync(file).mtimeMs < since) probs.push(`MEASURE.md is older than this agent's run (written before ${new Date(since).toISOString()})`); } catch { /* read above */ }
  const ro = text.match(/^References opened:[ \t]*(\d+)[ \t]*of[ \t]*(\d+)\b/mi);
  if (!ro) probs.push('no line "References opened: N of M listed"');
  else {
    const n = Number(ro[1]), m = Number(ro[2]);
    if (n === 0) probs.push('"References opened: 0 of ' + m + ' listed": nothing was opened');
    if (n < m && !/not opened/i.test(text)) probs.push(`"References opened: ${n} of ${m}": the ${m - n} unopened file(s) need a line saying "not opened" with the file name and why`);
  }
  // sections
  const parts = text.split(/^(?=#{2,3}\s)/m).filter((p) => /^#{2,3}\s+(?:case\b|scenario\b|view\b)/i.test(p));
  if (!parts.length) probs.push('no "## Case <name>" section: one section per case, each with its measurement table and trace image');
  if (cases !== null && parts.length < cases) probs.push(`the brief has ${cases} case(s) but MEASURE.md has ${parts.length} "## Case" section(s)`);
  const bases = [folder, path.dirname(folder), path.dirname(path.dirname(path.dirname(folder)))];
  for (const part of parts) {
    const title = part.split('\n')[0].replace(/^#+\s*/, '').trim();
    const rows = part.split('\n').filter((l) => l.trim().startsWith('|') && !/^\s*\|[\s:|-]+\|\s*$/.test(l)).slice(1);
    if (!rows.length) { probs.push(`${title}: no measurement table (rows: photo file | what was measured | value in m or lane widths)`); }
    const photos = new Set();
    for (const r of rows) {
      const cells = r.split('|').slice(1, -1).map((c) => c.trim());
      const photo = cells.map((c) => (c.match(/[^\s`|,;()]+\.(?:jpe?g|png|webp|gif)\b/i) || [])[0]).find(Boolean);
      const value = cells.some((c) => /\d+(?:\.\d+)?\s*(?:m|metres?|meters?|cm|lanes?(?:\s*widths?)?|lw)\b/i.test(c));
      if (!photo) probs.push(`${title}: a table row names no photo file: ${r.trim().slice(0, 100)}`);
      else photos.add(path.basename(photo).toLowerCase());
      if (!value) probs.push(`${title}: a table row has no measured value in m, cm or lane widths: ${r.trim().slice(0, 100)}`);
    }
    if (rows.length && photos.size < 3 && !/no photo (?:exists|covers)/i.test(part)) probs.push(`${title}: the table names ${photos.size} different photo(s); at least 3 per case (or a line "no photo exists" and a fetched one)`);
    const imgs = [...new Set((part.match(/[^\s`|,;()<>"']+\.(?:png|jpe?g|webp|gif)\b/gi) || []))];
    const ok = [];
    for (const p of imgs) {
      const r = path.isAbsolute(p) ? p : bases.map((b) => path.join(b, p)).find((x) => fs.existsSync(x));
      if (!r || !fs.existsSync(r)) continue;
      // a trace or overlay is this round's own picture: inside the hand-in folder or its worktree, not a reference photo
      if (!/[\\/]docs[\\/]reference[\\/](?!_compare)/i.test(r) && imageDecodable(r) && !(since > 0 && fs.statSync(r).mtimeMs < since)) ok.push(r);
    }
    if (!ok.length) probs.push(`${title}: no trace or overlay image (a complete, decodable picture of this round's own, written after the brief, named in the section)`);
  }
  return probs;
}

// ---- build
// BUILD.md: `Commits:` listing every commit made since MEASURE.md was written (each must exist on HEAD; a commit made since and not listed is refused); the suite is RUN HERE (`--suite` lines
// of the brief, else the ctest build when sim_core exists) and must exit 0; each test file added since the measure phase needs a fail_first_*.txt naming it; no new waivers (the same guard
// the full hand-in uses); an empty `git status`.
export function buildProblems(folder, wt, { since = 0, suites = [], GL = null, log = () => {} } = {}) {
  const probs = [];
  const file = path.join(folder, 'BUILD.md');
  let text; try { text = stripComments(fs.readFileSync(file, 'utf8').replace(/\r\n/g, '\n')); } catch { return ['BUILD.md is missing in ' + folder + ': the build phase hands over through it']; }
  try { if (since > 0 && fs.statSync(file).mtimeMs < since) probs.push(`BUILD.md is older than this agent's run (written before ${new Date(since).toISOString()})`); } catch { /* read above */ }
  let measureAt = 0; try { measureAt = fs.statSync(path.join(folder, 'MEASURE.md')).mtimeMs; } catch { probs.push('MEASURE.md is missing: the build phase starts from it'); }
  const git = (...a) => sh('git', a, wt, 60000);
  // commits since the measure phase
  const listed = [...new Set([...text.matchAll(/\b([0-9a-f]{7,40})\b/g)].map((m) => m[1]))];
  // the measure phase's own commit is the starting point: the commits made AFTER the last commit that touched MEASURE.md are the build phase's
  const relMeasure = path.relative(wt, path.join(folder, 'MEASURE.md')).split(path.sep).join('/');
  const mc = String(git('log', '-1', '--format=%H', '--', relMeasure).stdout || '').trim();
  const since0 = measureAt ? new Date(Math.floor(measureAt / 1000) * 1000).toISOString() : null;
  const lg = mc ? git('log', mc + '..HEAD', '--format=%H %s') : since0 ? git('log', '--since=' + since0, '--format=%H %s') : git('log', '-20', '--format=%H %s');
  const made = String(lg.stdout || '').split('\n').filter(Boolean).map((l) => ({ sha: l.slice(0, 40), subject: l.slice(41) }));
  if (!/^Commits:/mi.test(text)) probs.push('no "Commits:" line: list the commits you made since the measure phase (short hashes)');
  if (!made.length) probs.push('git log shows no commit since MEASURE.md was written: the build phase commits after each verified step');
  // a commit that touches only files inside the hand-in folder (BUILD.md itself, fail_first files) cannot list itself, so it is not required in the list
  const relFolder = path.relative(wt, folder).split(path.sep).join('/') + '/';
  const onlyHandin = (sha) => { const fl = String(git('show', '--name-only', '--format=', sha).stdout || '').split('\n').filter(Boolean); return fl.length > 0 && fl.every((f) => f.startsWith(relFolder)); };
  for (const c of made) if (!onlyHandin(c.sha) && !listed.some((l) => c.sha.startsWith(l))) probs.push(`commit ${c.sha.slice(0, 8)} (${c.subject.slice(0, 60)}) was made since the measure phase but BUILD.md does not list it`);
  for (const l of listed) {
    if (/^[0-9a-f]{7,40}$/.test(l) && l.length >= 7) { const r = git('merge-base', '--is-ancestor', l, 'HEAD'); const ex = git('cat-file', '-e', l + '^{commit}'); if (ex.status === 0 && r.status !== 0) probs.push(`${l} is a commit that is not on this branch`); }
  }
  // the suite, run by this check
  let cmds = suites.slice();
  if (!cmds.length && fs.existsSync(path.join(wt, 'sim_core'))) cmds = ['cmake -S . -B build && cmake --build build && ctest --test-dir build --output-on-failure'];
  if (!cmds.length) probs.push('no suite to run: the brief has no SUITE: line and the worktree has no sim_core; the build phase check runs the suite itself, so the brief must name one');
  for (const c of cmds) {
    const r = spawnSync(c, { cwd: wt, shell: true, encoding: 'utf8', timeout: 20 * 60 * 1000, maxBuffer: 64 * 1024 * 1024, env: { ...process.env, CP_PHASE_CHECK: '1' } });
    log('suite', c, r.status);
    if (r.error) probs.push(`suite "${c}" could not run: ${r.error.message}`);
    else if (r.status !== 0) probs.push(`suite "${c}" exited ${r.status} when the check ran it: ${(String(r.stdout || '') + String(r.stderr || '')).trim().split('\n').slice(-6).join(' | ').slice(0, 400)}`);
  }
  // fail_first for every test file added since the measure phase
  const base = mc || (made.length ? made[made.length - 1].sha + '^' : null);
  const added = base ? String(git('diff', '--name-only', '--diff-filter=A', base, 'HEAD').stdout || '').split('\n').filter((f) => /(?:^|\/)tests?\/|(?:^|\/)test_[^/]*\.(?:mjs|js|cpp)$|_test\.(?:mjs|js|cpp)$/i.test(f) && !/docs\/progress\//.test(f)) : [];
  let ffs = []; try { ffs = fs.readdirSync(folder).filter((n) => /^fail_first_.*\.txt$/i.test(n)).map((n) => ({ n, t: fs.readFileSync(path.join(folder, n), 'utf8') })); } catch { /* none */ }
  for (const f of added) {
    const bn = path.basename(f).replace(/\.[^.]+$/, '');
    const hit = ffs.find((x) => x.t.toLowerCase().includes(bn.toLowerCase()) && x.t.trim().length >= 40);
    if (!hit) probs.push(`new test file ${f}: no fail_first_*.txt in the hand-in folder that names "${bn}" and shows the new tests failing on the code before the change`);
  }
  // no new waivers
  if (GL) {
    try {
      const d = GL.collectWaiverDiff(wt);
      if (d.error) log('waiver-error', d.error);
      else GL.waiverFindings(d.text, [], d.contents).refused.forEach((f) => probs.push(`waiver guard (${f.file}): ${f.what}`));
    } catch (e) { log('waiver-error', String(e && e.message || e)); }
  }
  // empty git status
  const st = git('status', '--porcelain');
  const dirty = String(st.stdout || '').split('\n').filter(Boolean);
  if (dirty.length) probs.push(`git status is not empty (${dirty.length} line(s), e.g. ${dirty.slice(0, 3).join(' ; ')}): commit or remove everything before the phase ends`);
  return probs;
}
void IMG;
