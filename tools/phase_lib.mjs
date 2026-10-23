// Phase checks (gates10, gates11): `check_handin --phase measure|build`. A builder round may run as up to three short fresh agents in sequence; each hands over through a file in the hand-in folder.
//   measure -> MEASURE.md   build -> BUILD.md (+ suite_build.log)   render -> the ordinary full hand-in (STATUS, FAULTS, COMPARE, INDEX), checked as before.
// Every function returns a list of problems (empty = passes). Nothing here quotes an agent: files are opened, images decoded, git read, the suite LOG parsed (the suite itself is run by the agent
// under its own time limit: a hook that ran it would be killed at its own limit and fail open).
import fs from 'node:fs';
import path from 'node:path';
import { spawnSync } from 'node:child_process';
import { imageDecodable, imageDims } from './image_lib.mjs';
import { findSuiteRun } from './hooks/suite_record.mjs';

const sh = (cmd, args, cwd, timeout = 60000) => spawnSync(cmd, args, { cwd, encoding: 'utf8', timeout, maxBuffer: 64 * 1024 * 1024 });
const stripComments = (s) => s.replace(/<!--[\s\S]*?-->/g, '');

// ---- measure
// MEASURE.md: a `References opened: N of M listed` line (M must equal the brief's REFS: count when the brief has one; N < M needs a "not opened" line); one "## Case ..." section per case (one per
// CASE: line of the brief, matched by name), each with a measurement table (every data row names a photo file that EXISTS under docs/reference of the main checkout and a value in metres, centimetres or
// lane widths read from a cell that is not the file name; at least 3 different photos per case) and its own trace or overlay image (complete, decodable, at least 32 x 32 px, written after the brief,
// not shared with another case). gates11: photo files are looked up on disk, M and the case names are compared with the brief, a unit inside a file name ("a_3m.jpg") is no value.
const refIndexCache = new Map();
function refIndex(main) {
  if (!main) return null;
  const key = path.resolve(main);
  if (refIndexCache.has(key)) return refIndexCache.get(key);
  const idx = new Map(); const root = path.join(key, 'docs', 'reference');
  const walk = (d, depth) => {
    if (depth > 6) return;
    let ents; try { ents = fs.readdirSync(d, { withFileTypes: true }); } catch { return; }
    for (const e of ents) {
      if (e.name === '_compare') continue;
      const p = path.join(d, e.name);
      if (e.isDirectory()) walk(p, depth + 1);
      else if (e.isFile() && /\.(?:jpe?g|png|webp|gif)$/i.test(e.name)) { const k = e.name.toLowerCase(); if (!idx.has(k)) idx.set(k, []); idx.get(k).push(p); }
    }
  };
  walk(root, 0);
  refIndexCache.set(key, idx);
  return idx;
}
export const photoExists = (name, main, extraBases = []) => {
  const raw = String(name).replace(/^file:\/\/\/?/i, '');
  if (path.isAbsolute(raw) || /^[A-Za-z]:[\\/]/.test(raw)) { try { return fs.statSync(raw).isFile(); } catch { return false; } }
  for (const b of extraBases) { try { if (fs.statSync(path.join(b, raw)).isFile()) return true; } catch { /* next */ } }
  const idx = refIndex(main); if (!idx) return false;
  const hits = idx.get(path.basename(raw).toLowerCase()) || [];
  const tail = raw.replace(/\\/g, '/').toLowerCase();
  return hits.some((h) => h.replace(/\\/g, '/').toLowerCase().endsWith('/' + tail) || !/[\\/]/.test(raw));
};
const PHOTO_TOKEN = /[^\s`|,;()]+\.(?:jpe?g|png|webp|gif)\b/i;
const VALUE = /(?<![\w.])\d+(?:\.\d+)?\s*(?:m|metres?|meters?|cm|lanes?(?:\s*widths?)?|lw)\b/i;
const caseKey = (s) => String(s).toLowerCase().replace(/[^a-z0-9]+/g, ' ').trim();
// does the section title answer the brief's case? The case label ("Case 1", "s2", "A") or the longest word (4+ letters) after it must appear in the title.
export function caseMatches(title, briefCase) {
  const t = ' ' + caseKey(title) + ' ';
  const c = String(briefCase).trim();
  const label = c.split(/\s*[:|]\s*/)[0];
  const lk = caseKey(label).replace(/^(?:case|scenario|view|item|test)\s+/, '');
  if (lk && t.includes(' ' + lk + ' ')) return true;
  const rest = caseKey(c.slice(label.length)).split(' ').filter((w) => w.length >= 4).sort((x, y) => y.length - x.length)[0];
  return !!rest && t.includes(rest);
}
export function measureProblems(folder, { since = 0, cases = null, refs = null, main = null } = {}) {
  const probs = [];
  const file = path.join(folder, 'MEASURE.md');
  let text; try { text = stripComments(fs.readFileSync(file, 'utf8').replace(/\r\n/g, '\n')); } catch { return [`MEASURE.md is missing in ${folder}: the measure phase hands over through it`]; }
  try { if (since > 0 && fs.statSync(file).mtimeMs < since) probs.push(`MEASURE.md is older than this agent's run (written before ${new Date(since).toISOString()})`); } catch { /* read above */ }
  const caseList = Array.isArray(cases) ? cases : null; const caseCount = Array.isArray(cases) ? cases.length : (typeof cases === 'number' ? cases : null);
  const ro = text.match(/^References opened:[ \t]*(\d+)[ \t]*of[ \t]*(\d+)\b/mi);
  if (!ro) probs.push('no line "References opened: N of M listed"');
  else {
    const n = Number(ro[1]), m = Number(ro[2]);
    if (n === 0) probs.push('"References opened: 0 of ' + m + ' listed": nothing was opened');
    if (refs !== null && m !== Number(refs)) probs.push(`"References opened: ${n} of ${m} listed": the brief lists ${refs} reference file(s), so M must be ${refs}`);
    if (n < m && !/not opened/i.test(text)) probs.push(`"References opened: ${n} of ${m}": the ${m - n} unopened file(s) need a line saying "not opened" with the file name and why`);
  }
  const parts = text.split(/^(?=#{2,3}\s)/m).filter((p) => /^#{2,3}\s+(?:case\b|scenario\b|view\b)/i.test(p));
  if (!parts.length) probs.push('no "## Case <name>" section: one section per case, each with its measurement table and trace image');
  if (caseCount !== null && parts.length < caseCount) probs.push(`the brief has ${caseCount} case(s) but MEASURE.md has ${parts.length} "## Case" section(s): one table per case`);
  if (caseList) for (const c of caseList) if (!parts.some((p) => caseMatches(p.split('\n')[0].replace(/^#+\s*/, ''), c))) probs.push(`the brief's case "${String(c).slice(0, 60)}" has no "## Case" section of that name in MEASURE.md`);
  const bases = [folder, path.dirname(folder), path.dirname(path.dirname(path.dirname(folder)))];
  const usedImgs = new Map();
  for (const part of parts) {
    const title = part.split('\n')[0].replace(/^#+\s*/, '').trim();
    const rows = part.split('\n').filter((l) => l.trim().startsWith('|') && !/^\s*\|[\s:|-]+\|\s*$/.test(l)).slice(1);
    if (!rows.length) { probs.push(`${title}: no measurement table (rows: photo file | what was measured | value in m or lane widths)`); }
    const photos = new Set();
    for (const r of rows) {
      const cells = r.split('|').slice(1, -1).map((c) => c.trim());
      const photo = cells.map((c) => (c.match(PHOTO_TOKEN) || [])[0]).find(Boolean);
      const value = cells.some((c) => VALUE.test(c.replace(new RegExp(PHOTO_TOKEN.source, 'gi'), ' ')));
      if (!photo) probs.push(`${title}: a table row names no photo file: ${r.trim().slice(0, 100)}`);
      else { photos.add(path.basename(photo).toLowerCase()); if (!photoExists(photo, main, bases)) probs.push(`${title}: the photo "${photo}" does not exist (looked in docs/reference of the main checkout${main ? ' ' + main : ''}): name a real file`); }
      if (!value) probs.push(`${title}: a table row has no measured value in m, cm or lane widths (a unit inside the file name does not count): ${r.trim().slice(0, 100)}`);
    }
    if (rows.length && photos.size < 3 && !/no photo (?:exists|covers)/i.test(part)) probs.push(`${title}: the table names ${photos.size} different photo(s); at least 3 per case (or a line "no photo exists" and a fetched one)`);
    const imgs = [...new Set((part.match(/[^\s`|,;()<>"']+\.(?:png|jpe?g|webp|gif)\b/gi) || []))];
    const ok = [];
    for (const p of imgs) {
      const r = path.isAbsolute(p) ? p : bases.map((b) => path.join(b, p)).find((x) => fs.existsSync(x));
      if (!r || !fs.existsSync(r)) continue;
      // a trace or overlay is this round's own picture: not a reference photo, decodable, at least 32 x 32 px, written after the brief, not another case's
      if (/[\\/]docs[\\/]reference[\\/](?!_compare)/i.test(r) || !imageDecodable(r) || (since > 0 && fs.statSync(r).mtimeMs < since)) continue;
      let d = null; try { const fd = fs.openSync(r, 'r'); const b = Buffer.alloc(65536); const n = fs.readSync(fd, b, 0, b.length, 0); fs.closeSync(fd); d = imageDims(b.subarray(0, n)); } catch { /* unreadable */ }
      if (!d || d.w < 32 || d.h < 32) continue;
      const key = path.resolve(r).toLowerCase();
      if (usedImgs.has(key) && usedImgs.get(key) !== title) { probs.push(`${title}: the trace image ${path.basename(r)} is also the image of "${usedImgs.get(key)}": every case has its own trace or overlay`); continue; }
      usedImgs.set(key, title); ok.push(r);
    }
    if (!ok.length) probs.push(`${title}: no trace or overlay image (a complete, decodable picture, at least 32 x 32 px, of this case's own, written after the brief, named in the section)`);
  }
  return probs;
}

// ---- build (gates11)
// The suite is NOT run by this check (a hook is killed at its time limit and then fails open: the gate was off exactly when the suite was real). The build agent runs the suite under its own
// `timeout 900` and leaves the output in <hand-in>/suite_build.log; this check reads the log. It must: be newer than the last code commit; hold a real summary (doctest "test cases:", ctest
// "tests passed", or the repo's node suites "N passed, M failed"); show failures only for tests the brief allows (ALLOW-FAIL: lines) and name them; not be a timeout (exit=124); and come from a binary
// newer than the last code commit (BUILD.md "Suite binary: <path>"; a script path is exempt from the mtime test). The round's auditor re-runs the suite itself (audit brief template).
// gates12 (decision 2): the log is also matched against the suite-run record the time-limit hooks write (tools/hooks/suite_record.mjs: start by time_guard, end + exit + sha256 by time_guard_post):
// no record of a run in this worktree with exactly these bytes, ended after the last code commit, means a typed or edited log, and the check fails.
export const SUITE_LOG = 'suite_build.log';
const SCRIPT_EXT = /\.(?:mjs|cjs|js|ts|py|ps1|sh|bat|cmd)$/i;
export function parseSuiteLog(text) {
  const t = String(text || '').replace(/\r\n/g, '\n');
  const out = { summary: null, failed: 0, total: null, names: [], timedOut: false };
  const m = t.match(/(\d+)% tests passed, (\d+) tests? failed out of (\d+)/);
  if (m) { out.summary = 'ctest'; out.failed = Number(m[2]); out.total = Number(m[3]); }
  const dm = t.match(/\[doctest\] test cases:\s*(\d+)\s*\|\s*(\d+) passed\s*\|\s*(\d+) failed/);
  if (dm) { out.summary = out.summary || 'doctest'; out.failed += Number(dm[3]); out.total = (out.total || 0) + Number(dm[1]); }
  const nm = t.match(/^\s*(\d+) passed, (\d+) failed\s*$/m);
  if (nm && !out.summary) { out.summary = 'node suite'; out.failed = Number(nm[2]); out.total = Number(nm[1]) + Number(nm[2]); }
  const names = new Set();
  const ft = t.split('The following tests FAILED:')[1];
  if (ft) for (const l of ft.split('\n').slice(1)) { const x = /^\s*\d+ - (\S.*?) \([^)]*\)\s*$/.exec(l); if (x) names.add(x[1]); else if (l.trim() === '') continue; else break; }
  for (const x of t.matchAll(/^TEST CASE:\s+(.+)$/gm)) names.add(x[1].trim());
  for (const x of t.matchAll(/^\s*FAIL\s+(\S.*)$/gm)) names.add(x[1].trim());
  out.names = [...names];
  out.timedOut = /(?:^|\n)\s*exit(?:\s*code)?\s*[=:]\s*(?:124|137)\b/i.test(t) || /timed out after/i.test(t.slice(-400));
  return out;
}
const FAIL_MARK = [/^\s*(?:FAIL|FAILED|not ok|✗|✘)\b/m, /^TEST CASE:[\s\S]*?\bERROR:/m, /\[doctest\] test cases:.*\|\s*[1-9]\d* failed/, /\*\*\*Failed|\(Failed\)|[1-9]\d* tests? failed out of/, /(?:^|[,|;])\s*[1-9]\d* failed\b/m, /\bassertions?:.*\|\s*[1-9]\d* failed/i];
export const failFirstShowsFailure = (text) => FAIL_MARK.some((re) => re.test(String(text || '')));
const TEST_PATH = /(?:^|\/)tests?\/|(?:^|\/)test_[^/]*\.(?:mjs|cjs|js|ts|py|cpp|cc)$|_tests?\.(?:mjs|cjs|js|ts|py|cpp|cc)$|\.(?:test|spec)\.(?:mjs|cjs|js|ts)$|(?:^|\/)Tests\//;
// content that registers tests wherever the file lives: doctest TEST_CASE, node:test, describe / test / it, or the repo's own runner shape (a t( "name", cond ) helper printing PASS / FAIL)
const TEST_BODY = /\bTEST_CASE(?:_TEMPLATE)?\s*\(|\bSUBCASE\s*\(|from ['"]node:test['"]|^\s*(?:describe|test|it)\s*\(\s*['"`]|\bCHECK(?:_EQ|_FALSE)?\s*\(|\bt\(\s*['"`][^'"`]+['"`]\s*,/m;
export function isTestFile(file, content = '') { return TEST_PATH.test(String(file).replace(/\\/g, '/')) || TEST_BODY.test(content); }
export function buildProblems(folder, wt, { since = 0, GL = null, log = () => {}, allowFail = [] } = {}) {
  const probs = [];
  const file = path.join(folder, 'BUILD.md');
  let text; try { text = stripComments(fs.readFileSync(file, 'utf8').replace(/\r\n/g, '\n')); } catch { return ['BUILD.md is missing in ' + folder + ': the build phase hands over through it']; }
  try { if (since > 0 && fs.statSync(file).mtimeMs < since) probs.push(`BUILD.md is older than this agent's run (written before ${new Date(since).toISOString()})`); } catch { /* read above */ }
  let measureAt = 0; try { measureAt = fs.statSync(path.join(folder, 'MEASURE.md')).mtimeMs; } catch { probs.push('MEASURE.md is missing: the build phase starts from it'); }
  const git = (...a) => sh('git', a, wt, 60000);
  const listed = [...new Set([...text.matchAll(/\b([0-9a-f]{7,40})\b/g)].map((m) => m[1]))];
  const relMeasure = path.relative(wt, path.join(folder, 'MEASURE.md')).split(path.sep).join('/');
  const mc = String(git('log', '-1', '--format=%H', '--', relMeasure).stdout || '').trim();
  const since0 = measureAt ? new Date(Math.floor(measureAt / 1000) * 1000).toISOString() : null;
  const lg = mc ? git('log', mc + '..HEAD', '--format=%H %s') : since0 ? git('log', '--since=' + since0, '--format=%H %s') : git('log', '-20', '--format=%H %s');
  const made = String(lg.stdout || '').split('\n').filter(Boolean).map((l) => ({ sha: l.slice(0, 40), subject: l.slice(41) }));
  if (!/^Commits:/mi.test(text)) probs.push('no "Commits:" line: list the commits you made since the measure phase (short hashes)');
  if (!made.length) probs.push('git log shows no commit since MEASURE.md was written: the build phase commits after each verified step');
  const relFolder = path.relative(wt, folder).split(path.sep).join('/') + '/';
  const filesOf = (sha) => String(git('show', '--name-only', '--format=', sha).stdout || '').split('\n').filter(Boolean);
  const onlyHandin = (sha) => { const fl = filesOf(sha); return fl.length > 0 && fl.every((f) => f.startsWith(relFolder)); };
  for (const c of made) if (!onlyHandin(c.sha) && !listed.some((l) => c.sha.startsWith(l))) probs.push(`commit ${c.sha.slice(0, 8)} (${c.subject.slice(0, 60)}) was made since the measure phase but BUILD.md does not list it`);
  for (const l of listed) {
    if (/^[0-9a-f]{7,40}$/.test(l) && l.length >= 7) { const r = git('merge-base', '--is-ancestor', l, 'HEAD'); const ex = git('cat-file', '-e', l + '^{commit}'); if (ex.status === 0 && r.status !== 0) probs.push(`${l} is a commit that is not on this branch`); }
  }
  // a build with no code commit: at least one commit since MEASURE.md must touch code or a test (not the hand-in folder, not docs/, not a .md or .txt note)
  const isCode = (f) => !f.startsWith(relFolder) && !/^docs\//i.test(f) && !/\.(?:md|txt)$/i.test(f);
  const codeCommits = made.filter((c) => filesOf(c.sha).some(isCode));
  if (made.length && !codeCommits.length) probs.push('no commit since MEASURE.md touches code or tests outside the hand-in folder (docs/ and .md/.txt files do not count): the build phase writes the code and its tests');
  const lastCode = codeCommits.length ? Number(String(git('log', '-1', '--format=%ct', codeCommits[0].sha).stdout || '').trim()) * 1000 : 0;
  // the suite log written by the agent
  const logFile = path.join(folder, SUITE_LOG);
  let logText = null; try { logText = fs.readFileSync(logFile, 'utf8'); } catch { /* reported below */ }
  if (logText === null) probs.push(`${SUITE_LOG} is missing in the hand-in folder: run the brief's SUITE: command yourself under "timeout 900 <cmd> > ${SUITE_LOG} 2>&1" (the check reads the log; it does not run the suite)`);
  else {
    let lst = null; try { lst = fs.statSync(logFile).mtimeMs; } catch { /* read above */ }
    if (lst !== null && lastCode && lst < lastCode) probs.push(`${SUITE_LOG} (written ${new Date(lst).toISOString()}) is older than the last code commit (${new Date(lastCode).toISOString()}): run the suite again on the committed code`);
    if (lst !== null && since > 0 && lst < since) probs.push(`${SUITE_LOG} is older than this agent's run`);
    // gates12 (decision 2): the log counts only when a suite run that the time-limit hooks SAW wrote exactly these bytes (hash), in this worktree, after the last code commit
    const rec = findSuiteRun(logFile, wt, { after: lastCode });
    if (!rec.ok) probs.push(`${SUITE_LOG}: ${rec.why}`);
    const ps = parseSuiteLog(logText);
    if (rec.ok) {
      const ex = rec.run.exit;
      if (ex === null || ex === undefined) probs.push(`${SUITE_LOG}: the recorded run has no known exit code (the hook saw it end without one): run the suite again`);
      else if (ex !== 0 && ps.summary && ps.failed === 0 && !ps.timedOut) probs.push(`${SUITE_LOG}: the recorded run exited with code ${ex} but the log shows no failing test: the run died or was killed (124 = time limit); run it again`);
      else if (ex === 0 && ps.summary && ps.failed > 0 && !allowFail.length) probs.push(`${SUITE_LOG}: the recorded run exited 0 but the log shows ${ps.failed} failing test(s): the log and the record disagree`);
    }
    if (!ps.summary) probs.push(`${SUITE_LOG} holds no doctest ("[doctest] test cases: ..."), ctest ("N% tests passed, ...") or node-suite ("N passed, M failed") summary: it is not the output of a finished suite run`);
    else if (ps.timedOut) probs.push(`${SUITE_LOG} shows the run hit its time limit (exit 124): find the slow or hung case (-tc filters, --durations) and fix it, or write it in STATUS`);
    else if (ps.failed > 0) {
      const allow = allowFail.map((x) => String(x).toLowerCase()).filter(Boolean);
      if (!ps.names.length) probs.push(`${SUITE_LOG} reports ${ps.failed} failed but names no failing test: the check cannot tell which failures are the allowed ones`);
      const bad = ps.names.filter((n) => !allow.some((a) => n.toLowerCase().includes(a)));
      if (bad.length) probs.push(`${SUITE_LOG}: ${bad.length} failing test(s) are not in the brief's allowed list${allow.length ? ' (' + allowFail.join('; ').slice(0, 120) + ')' : ' (the brief allows none)'}: ${bad.slice(0, 4).join(' | ').slice(0, 300)}`);
    }
    const bm = text.match(/^Suite binary:[ \t]*(\S.*?)[ \t]*$/mi);
    let bin = bm ? bm[1].replace(/^[`"']|[`"']$/g, '') : null;
    if (!bin && fs.existsSync(path.join(wt, 'sim_core'))) bin = ['build/tests/sim_core_tests.exe', 'build/tests/sim_core_tests', 'build/sim_core_tests.exe', 'build/sim_core_tests'].find((c) => fs.existsSync(path.join(wt, c))) || null;
    if (!bin) probs.push('no "Suite binary: <path>" line in BUILD.md (the test executable or script the suite ran; it is checked against the last code commit)');
    else {
      const abs = path.isAbsolute(bin) ? bin : path.join(wt, bin);
      let st = null; try { st = fs.statSync(abs); } catch { /* reported below */ }
      if (!st || !st.isFile()) probs.push(`Suite binary "${bin}" does not exist`);
      else if (!SCRIPT_EXT.test(bin) && lastCode && st.mtimeMs < lastCode) probs.push(`Suite binary "${bin}" (built ${new Date(st.mtimeMs).toISOString()}) is older than the last code commit (${new Date(lastCode).toISOString()}): the log comes from a stale build; rebuild and run the suite again`);
    }
  }
  // fail_first for every test added since the measure phase: added files that are tests by path OR by content, and test registrations added to an existing test file
  const base = mc || (made.length ? made[made.length - 1].sha + '^' : null);
  const newTests = [];
  if (base) {
    const ns = String(git('diff', '--name-status', base, 'HEAD').stdout || '').split('\n').filter(Boolean).map((l) => l.split('\t'));
    for (const [st, ...fl] of ns) {
      const f = fl[fl.length - 1]; if (!f || /^docs\//i.test(f)) continue;
      let content = ''; try { content = fs.readFileSync(path.join(wt, f), 'utf8'); } catch { /* deleted */ }
      if (st.startsWith('A') && isTestFile(f, content)) newTests.push(f);
      else if (st.startsWith('M') && TEST_PATH.test(f)) { const d = String(git('diff', '-U0', base, 'HEAD', '--', f).stdout || ''); if (d.split('\n').some((l) => l.startsWith('+') && !l.startsWith('+++') && TEST_BODY.test(l))) newTests.push(f); }
    }
  }
  let ffs = []; try { ffs = fs.readdirSync(folder).filter((n) => /^fail_first_.*\.txt$/i.test(n)).map((n) => ({ n, t: fs.readFileSync(path.join(folder, n), 'utf8') })); } catch { /* none */ }
  for (const f of newTests) {
    const bn = path.basename(f).replace(/\.[^.]+$/, '');
    const hit = ffs.find((x) => x.t.toLowerCase().includes(bn.toLowerCase()) && x.t.trim().length >= 40);
    if (!hit) probs.push(`new test ${f}: no fail_first_*.txt in the hand-in folder that names "${bn}" and shows the new tests failing on the code before the change`);
  }
  for (const x of ffs) if (!failFirstShowsFailure(x.t)) probs.push(`${x.n} shows no failing case in the format of the test runner (a "FAIL <name>" line, a doctest "TEST CASE: ... ERROR:", a ctest "***Failed" / "N tests failed out of", or a "N failed" summary above zero): paste the real failing output of the new test on the old code`);
  // no new waivers
  if (GL) {
    try {
      const d = GL.collectWaiverDiff(wt);
      if (d.error) log('waiver-error', d.error);
      else GL.waiverFindings(d.text, [], d.contents).refused.forEach((f) => probs.push(`waiver guard (${f.file}): ${f.what}`));
    } catch (e) { log('waiver-error', String(e && e.message || e)); }
  }
  const st = git('status', '--porcelain');
  const dirty = String(st.stdout || '').split('\n').filter(Boolean);
  if (dirty.length) probs.push(`git status is not empty (${dirty.length} line(s), e.g. ${dirty.slice(0, 3).join(' ; ')}): commit or remove everything before the phase ends`);
  return probs;
}
