// Gates v2: hand-in checks that MEASURE the work instead of reading what the builder wrote about it.
// Used by tools/check_handin.mjs (when run with --gates-version 2) and tools/make_brief.mjs (grammar, scope, history rules).
// A brief carries `GATES-VERSION: 2` plus `GATE: <line>` lines (make_brief.mjs emits them from the task file's "## Gates"
// section); briefs written before this have neither and none of the checks below run for them.
//
// Gate line grammar (one per line, a leading "- " is ignored):
//   <metric> <op> <value> [(baseline <v>[, target <v>])]      metric: scene_check | ctest failed_assertions | ctest failed_tests
//        op: >= <= ==   value: "P/T" for scene_check (P passed of T checks), a number otherwise.
//        The value after the op is the FLOOR: a measurement on the wrong side of it refuses the hand-in. A target (if given)
//        that is not reached still passes, but only when the STATUS "Skipped, refused or failed" section names the metric.
//   allow_waiver <key>                                          lets the waiver guard pass one named finding (see waiverFindings)
//   picture_min_road <fraction> | picture_max_debug <fraction>  overrides for the picture sanity check
// Internal errors never refuse: callers log FAIL-OPEN and carry on (a gate that blocks wrongly would trap the agent).
import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import { spawnSync } from 'node:child_process';

export const METRICS = ['scene_check', 'ctest failed_assertions', 'ctest failed_tests'];
const sh = (cmd, args, cwd, timeout = 60000) => spawnSync(cmd, args, { cwd, encoding: 'utf8', timeout, maxBuffer: 64 << 20 });

// ---------- grammar ----------
export function parseGateLine(raw) {
  const line = String(raw).trim().replace(/^[-*]\s+/, '');
  if (!line) return null;
  let m = line.match(/^allow_waiver[:\s]+(\S.*?)\s*$/i);
  if (m) return { kind: 'allow', key: m[1].trim(), raw: line };
  m = line.match(/^(picture_min_road|picture_max_debug)\s*[:=]?\s*([0-9.]+)\s*$/i);
  if (m) return { kind: 'picture', name: m[1].toLowerCase(), value: Number(m[2]), raw: line };
  m = line.match(/^([a-z_]+(?:\s+[a-z_]+)?)\s*(>=|<=|==)\s*(\d+(?:\/\d+)?)\s*(?:\((.*)\))?\s*$/i);
  if (m) {
    const metric = m[1].toLowerCase().replace(/\s+/g, ' ');
    if (!METRICS.includes(metric)) return { kind: 'bad', raw: line, why: `unknown metric "${metric}" (known: ${METRICS.join(', ')})` };
    const grab = (w) => { const g = (m[4] || '').match(new RegExp(w + '\\s+(\\d+(?:/\\d+)?)', 'i')); return g ? g[1] : null; };
    return { kind: 'metric', metric, op: m[2], floor: m[3], baseline: grab('baseline'), target: grab('target'), raw: line };
  }
  return { kind: 'bad', raw: line, why: 'not a gate line (expected "<metric> <op> <value> [(baseline X, target Y)]", "allow_waiver <key>", "picture_min_road <f>" or "picture_max_debug <f>")' };
}
export const parseGates = (lines) => (lines || []).map(parseGateLine).filter(Boolean);

const splitVal = (v) => { const [a, b] = String(v).split('/'); return { n: Number(a), t: b === undefined ? null : Number(b) }; };
// true when `measured` satisfies `op value`. For P/T values the totals must agree (a suite that grew or shrank is reported by the caller).
export function satisfies(measured, op, value) {
  const a = splitVal(measured).n, b = splitVal(value).n;
  return op === '>=' ? a >= b : op === '<=' ? a <= b : a === b;
}

// ---------- A: measured gates ----------
function newestMtime(dir, skip = /^(build|\.git|node_modules)/) {
  let newest = { t: 0, f: '' };
  const walk = (d) => {
    let ents; try { ents = fs.readdirSync(d, { withFileTypes: true }); } catch { return; }
    for (const e of ents) {
      if (skip.test(e.name)) continue;
      const p = path.join(d, e.name);
      if (e.isDirectory()) walk(p);
      else if (/\.(cpp|h|hpp|cc|txt|cmake|json)$/.test(e.name)) { const t = fs.statSync(p).mtimeMs; if (t > newest.t) newest = { t, f: p }; }
    }
  };
  walk(dir);
  return newest;
}

export function measureSceneCheck(wt) {
  const dir = path.join(wt, 'prototypes', 'road-draw');
  if (!fs.existsSync(path.join(dir, 'tools', 'scene.mjs'))) return { error: 'no prototypes/road-draw/tools/scene.mjs in ' + wt };
  if (!fs.existsSync(path.join(dir, 'node_modules'))) return { error: 'prototypes/road-draw/node_modules is missing in ' + wt + ' (run `npm install` there); the scene check could not be measured' };
  if (fs.existsSync(path.join(dir, 'build.mjs'))) { const b = sh(process.execPath, ['build.mjs'], dir, 120000); if (b.status !== 0) return { fail: 'node build.mjs failed in prototypes/road-draw: ' + String(b.stderr || b.stdout).slice(0, 400) }; }
  const t0 = Date.now();
  const r = sh(process.execPath, ['tools/scene.mjs', '--check'], dir, 240000);
  if (r.error) return { error: 'scene check did not run: ' + r.error.message };
  const out = String(r.stdout || '');
  const sum = out.match(/(\d+)\/(\d+) passed in/);
  if (!sum) return { error: 'scene check output not understood (no "N/M passed" line): ' + (String(r.stderr || out).slice(0, 300)) };
  const failed = [...out.matchAll(/^FAIL\s+(\S+)\s+(.*)$/gm)].map((x) => x[1] + ' (' + x[2].slice(0, 90) + ')');
  return { value: `${sum[1]}/${sum[2]}`, passed: +sum[1], total: +sum[2], failed, seconds: (Date.now() - t0) / 1000 };
}

export function measureCtest(wt) {
  let dirs = [];
  try { dirs = fs.readdirSync(wt).filter((n) => /^build/.test(n)).map((n) => path.join(wt, n)); } catch { /* none */ }
  const logs = dirs.map((d) => path.join(d, 'Testing', 'Temporary', 'LastTest.log')).filter((f) => fs.existsSync(f))
    .map((f) => ({ f, t: fs.statSync(f).mtimeMs })).sort((a, b) => b.t - a.t);
  if (!logs.length) return { fail: 'no ctest result: no build*/Testing/Temporary/LastTest.log in ' + wt + '; build and run `ctest --test-dir build` in your worktree' };
  const { f, t } = logs[0];
  const src = ['sim_core', 'tests'].map((d) => newestMtime(path.join(wt, d))).sort((a, b) => b.t - a.t)[0];
  if (src && src.t > t + 1000) return { fail: `ctest result ${f} is older than the sources (${src.f}); rebuild and run the whole suite again` };
  const text = fs.readFileSync(f, 'utf8');
  const started = new Set([...text.matchAll(/^\s*\d+\/\d+ Testing: (.+)$/gm)].map((x) => x[1].trim()));
  const nTests = sh('ctest', ['--test-dir', path.dirname(path.dirname(path.dirname(f))), '-N'], wt, 60000);
  const total = nTests.status === 0 ? Number((String(nTests.stdout).match(/Total Tests:\s*(\d+)/) || [])[1]) : NaN;
  if (Number.isFinite(total) && started.size < total) return { fail: `ctest result ${f} covers ${started.size} of ${total} tests (a subset run): run the whole suite` };
  let assertions = 0, failedAssertions = 0;
  for (const m of text.matchAll(/\[doctest\] assertions:\s*(\d+)\s*\|\s*(\d+) passed\s*\|\s*(\d+) failed/g)) { assertions += +m[1]; failedAssertions += +m[3]; }
  const failedTests = (text.match(/^Test Failed\./gm) || []).length;
  return { failed_assertions: failedAssertions, failed_tests: failedTests, assertions, tests: started.size, file: f };
}

// Evaluates metric gates. Returns { problems, notes, shortfalls, errors } ; shortfalls = metrics below target but at/above floor.
export function runMeasuredGates(gates, wt, log = () => {}) {
  const out = { problems: [], notes: [], shortfalls: [], errors: [] };
  const metrics = gates.filter((g) => g.kind === 'metric');
  let scene = null, ctest = null;
  for (const g of metrics) {
    let value = null, extra = '';
    if (g.metric === 'scene_check') {
      scene ??= measureSceneCheck(wt);
      if (scene.error) { out.errors.push(`${g.metric}: ${scene.error}`); continue; }
      if (scene.fail) { out.problems.push(`gate ${g.raw}: ${scene.fail}`); continue; }
      value = scene.value; extra = scene.failed.length ? ` failing: ${scene.failed.join('; ')}` : '';
      const ft = splitVal(g.floor).t;
      if (ft !== null && ft !== scene.total) { out.problems.push(`gate ${g.raw}: the scene check now has ${scene.total} checks, the gate expects ${ft}; the main session must update the gate, do not remove checks`); continue; }
      out.notes.push(`measured scene_check ${value} in ${scene.seconds.toFixed(1)} s${extra}`);
    } else {
      ctest ??= measureCtest(wt);
      if (ctest.fail) { out.problems.push(`gate ${g.raw}: ${ctest.fail}`); continue; }
      value = g.metric === 'ctest failed_assertions' ? ctest.failed_assertions : ctest.failed_tests;
      out.notes.push(`measured ${g.metric} ${value} (${ctest.assertions} assertions, ${ctest.tests} tests, from ${path.basename(path.dirname(path.dirname(path.dirname(ctest.file))))})`);
    }
    if (!satisfies(value, g.op, g.floor)) {
      out.problems.push(`gate ${g.raw}: measured ${value}, which is on the wrong side of ${g.op} ${g.floor} (the floor)${extra}. The number you typed in STATUS is not read; fix the cause or stop and report it, do not hand in below the floor`);
    } else if (g.target && !satisfies(value, g.metric === 'scene_check' || g.op === '>=' ? '>=' : g.op === '<=' ? '<=' : '==', g.target)) {
      out.shortfalls.push({ metric: g.metric, measured: value, target: g.target });
    }
  }
  return out;
}

// ---------- B: waiver guard ----------
const WAIVER_PATHS = ['tests', 'prototypes/road-draw/tools/scene.mjs', 'prototypes/road-draw/test-geom.mjs'];
const KIND_RE = [
  ['should_fail', /\bshould_fail\b/], ['skip', /doctest::skip|\bDOCTEST_\w*SKIP|\bxit\(|\bit\.skip\(|\btest\.skip\(|\.skip\(/],
  ['may_fail', /\bmay_fail\b|expected_failures/], ['ctest_disabled', /\bDISABLED\b|\bWILL_FAIL\b/], ['warn', /\bWARN(?:_MESSAGE|_EQ|_FALSE)?\(/],
];
// A tolerance constant: kTolHeight, k_eps, max_tol, tol_m, epsilon, EPS, tolerance... (not "reps", "steps").
const TOL_RE = /\b(k\w*(?:Tol|Eps|Tolerance)\w*|\w*_(?:tol|eps|tolerance|epsilon)\w*|(?:tol|eps|epsilon|tolerance|TOL|EPS|Tol|Eps)\w*)\s*(?:=|\{)\s*([0-9]*\.?[0-9]+(?:[eE][-+]?\d+)?)/;
const testName = (l) => { const m = l.match(/^\s*(?:DOCTEST_)?TEST_CASE(?:_TEMPLATE)?\s*\(\s*"((?:[^"\\]|\\.)*)"/); return m ? m[1] : null; };

// What the guard looks for (the forms real waivers took in tests/test_grading.cpp and prototypes/road-draw/tools/scene.mjs):
//   should_fail / doctest::skip / may_fail / WILL_FAIL / WARN added; a name added to is_rough() or more GRADE_CHECK(is_rough(...)); a loosened or
//   new tolerance constant; a loosened numeric threshold on an edited line (< 0.3 -> < 0.5); fewer CHECK/REQUIRE assertions than before; `#if 0`;
//   a test case or scene check deleted or renamed; a test source dropped from tests/CMakeLists.txt; a scene check with a hard-coded `pass: true`.
// diffText: unified diff (-U0) of the guarded paths plus untracked guarded files appended as pure additions.
const ASSERT_RE = /\b(?:GRADE_CHECK|(?:DOCTEST_)?(?:CHECK|REQUIRE)(?:_MESSAGE|_EQ|_NE|_FALSE|_THROWS\w*|_NOTHROW)?)\s*\(/;
const numbers = (l) => [...l.matchAll(/(?<![\w.])(\d+\.?\d*(?:[eE][-+]?\d+)?)(?![\w.])/g)].map((m) => Number(m[1]));
const skeleton = (l) => l.replace(/(?<![\w.])\d+\.?\d*(?:[eE][-+]?\d+)?(?![\w.])/g, '#').replace(/\s+/g, ' ').trim();
export function waiverFindings(diffText, allowed = []) {
  const findings = [];
  const files = [];
  let cur = null, oldName = '', hunk = null;
  for (const line of String(diffText).split('\n')) {
    if (line.startsWith('--- ')) { oldName = line.slice(4).replace(/^a\//, ''); continue; }
    const h = line.match(/^\+\+\+ (?:b\/)?(.*)$/);
    if (h) { cur = { file: h[1] === '/dev/null' ? oldName : h[1], add: [], rem: [], hunks: [] }; files.push(cur); hunk = null; continue; }
    if (!cur) continue;
    if (line.startsWith('@@')) { hunk = { rem: [], add: [] }; cur.hunks.push(hunk); continue; }
    if (line.startsWith('diff ') || line.startsWith('index ')) continue;
    if (!hunk) { hunk = { rem: [], add: [] }; cur.hunks.push(hunk); }
    if (line.startsWith('+')) { cur.add.push(line.slice(1)); hunk.add.push(line.slice(1)); } else if (line.startsWith('-')) { cur.rem.push(line.slice(1)); hunk.rem.push(line.slice(1)); }
  }
  const addedTests = new Set(files.flatMap((f) => f.add.map(testName).filter(Boolean)));
  const addedJsNames = new Set(files.flatMap((f) => f.add.map((l) => (l.match(/name:\s*'([^']+)'/) || [])[1]).filter(Boolean)));
  const add = (key, file, what) => findings.push({ key, file, what });
  for (const { file: f, add: A, rem: R, hunks } of files) {
    // keyword waivers: more occurrences than before in the file
    for (const [kind, re] of KIND_RE) {
      const a = A.filter((l) => re.test(l)), r = R.filter((l) => re.test(l));
      if (a.length > r.length) for (const l of a.slice(r.length)) add(`${kind}:${testName(l) || (l.match(/["']([\w ./-]{3,60})["']/) || [])[1] || l.trim().slice(0, 40)}`, f, `${kind} added: ${l.trim().slice(0, 110)}`);
    }
    if (A.filter((l) => /^\s*#\s*if\s+0\b/.test(l)).length > R.filter((l) => /^\s*#\s*if\s+0\b/.test(l)).length) add('if0:' + path.basename(f), f, '"#if 0" added: code (a test?) is switched off');
    // is_rough(...) list and more softened checks
    const roughNames = (L) => new Set(L.filter((l) => /is_rough|n\s*==\s*"/.test(l)).flatMap((l) => [...l.matchAll(/"([\w]+)"/g)].map((x) => x[1])));
    const oldR = roughNames(R); for (const n of roughNames(A)) if (!oldR.has(n)) add(`is_rough:${n}`, f, `is_rough() waiver list gained "${n}" (turns EVERY GRADE_CHECK(is_rough(name),...) for that case into a warning, not just the check you describe)`);
    const gc = (L) => L.filter((l) => /GRADE_CHECK\s*\(\s*is_rough/.test(l)).length;
    if (gc(A) > gc(R)) add('grade_check', f, 'more checks are wrapped in GRADE_CHECK(is_rough(...)) than before');
    if (A.some((l) => /#define\s+GRADE_CHECK/.test(l)) && R.some((l) => /#define\s+GRADE_CHECK/.test(l)) && A.find((l) => /#define\s+GRADE_CHECK/.test(l)) !== R.find((l) => /#define\s+GRADE_CHECK/.test(l))) add('grade_check_macro', f, 'the GRADE_CHECK waiver macro itself was changed');
    // loosened tolerance constants
    const tolOf = (L) => { const m = new Map(); for (const l of L) { const t = l.match(TOL_RE); if (t) m.set(t[1], Number(t[2])); } return m; };
    const oldT = tolOf(R), newT = tolOf(A);
    for (const [n, v] of newT) { if (!oldT.has(n)) add(`tolerance:${n}`, f, `new tolerance constant ${n} = ${v}`); else if (v > oldT.get(n)) add(`tolerance:${n}`, f, `tolerance ${n} loosened ${oldT.get(n)} -> ${v}`); }
    // a threshold loosened on an edited line: same line shape, one number moved the easy way (these are 1:1 edits inside one hunk)
    for (const hk of hunks) {
      if (!hk.rem.length || hk.rem.length !== hk.add.length) continue;
      hk.rem.forEach((ol, i) => {
        const nl = hk.add[i];
        if (ol === nl || skeleton(ol) !== skeleton(nl) || !/[<>]/.test(nl)) return;
        const on = numbers(ol), nn = numbers(nl);
        // "x < N" loosens when N grows, "x > N" when N shrinks (a line with both, or with template brackets only, is left alone)
        const lessThan = /[^<]<=?[^<=]/.test(nl), greater = /[^->]>=?[^>=]/.test(nl) && !/\w<[\w:]+>/.test(nl);
        for (let j = 0; j < on.length; j++) {
          if ((lessThan && nn[j] > on[j]) || (greater && !lessThan && nn[j] < on[j])) { add(`threshold:${path.basename(f)}:${skeleton(nl).slice(0, 50)}`, f, `a threshold was loosened on an edited line: "${ol.trim().slice(0, 70)}" -> "${nl.trim().slice(0, 70)}"`); break; }
        }
      });
    }
    // fewer assertions than before (a CHECK commented out or deleted, outside a deleted test case)
    const nA = A.filter((l) => ASSERT_RE.test(l) && !/^\s*\/\//.test(l)).length, nR = R.filter((l) => ASSERT_RE.test(l) && !/^\s*\/\//.test(l)).length;
    const deletedHere = R.filter((l) => testName(l) && !addedTests.has(testName(l))).length;
    if (!/CMakeLists/.test(f) && nR > nA && !deletedHere) add('fewer_checks:' + path.basename(f), f, `${nR - nA} fewer CHECK/REQUIRE assertion line(s) than before in this file (a check removed or commented out)`);
    // a test source dropped from the test build
    if (/CMakeLists\.txt$/.test(f)) for (const l of R) { const m = l.match(/([\w./-]+\.cpp)/); if (m && !A.some((x) => x.includes(m[1]))) add('deleted_file:' + m[1], f, `${m[1]} was removed from the test build`); }
    // scene.mjs: hard-coded pass
    if (/scene\.mjs$/.test(f)) {
      const pt = (L) => L.filter((l) => /pass:\s*true\b/.test(l));
      for (const l of pt(A).filter((x) => !pt(R).includes(x))) add(`pass_true:${(l.match(/name:\s*'([^']+)'/) || [])[1] || 'unnamed'}`, f, 'check with hard-coded pass: true added: ' + l.trim().slice(0, 100));
      for (const l of R) { const n = (l.match(/name:\s*'([^']+)'/) || [])[1]; if (n && !addedJsNames.has(n) && /results\.push/.test(l)) add(`deleted_check:${n}`, f, `scene check "${n}" removed`); }
    }
    // deleted test cases
    for (const l of R) { const n = testName(l); if (n && !addedTests.has(n)) add(`deleted_test:${n}`, f, `test case "${n}" was deleted (or renamed: a renamed test whose expectation changed needs the same allow_waiver line)`); }
  }
  const ok = (fd) => allowed.some((a) => a === fd.key || a === fd.key.split(':').slice(1).join(':'));
  return { findings, refused: findings.filter((f) => !ok(f)), allowedHits: findings.filter(ok) };
}

export function collectWaiverDiff(wt) {
  const git = (...a) => sh('git', a, wt, 60000);
  let top = git('rev-parse', '--show-toplevel'); if (top.status !== 0) return { error: 'not a git worktree: ' + wt };
  const wl = git('worktree', 'list', '--porcelain');
  const mainPath = (String(wl.stdout).match(/^worktree (.+)$/m) || [])[1];
  if (!mainPath) return { error: 'git worktree list gave no main checkout' };
  const head = sh('git', ['rev-parse', 'HEAD'], mainPath, 30000);
  if (head.status !== 0) return { error: 'cannot read HEAD of the main checkout ' + mainPath };
  const mb = git('merge-base', 'HEAD', String(head.stdout).trim());
  if (mb.status !== 0) return { error: 'no merge base with the main checkout HEAD' };
  const base = String(mb.stdout).trim();
  const d = git('diff', '-U0', '--no-color', base, '--', ...WAIVER_PATHS);
  if (d.status !== 0) return { error: 'git diff failed: ' + String(d.stderr).slice(0, 200) };
  let text = String(d.stdout);
  const un = git('ls-files', '--others', '--exclude-standard', '--', ...WAIVER_PATHS);
  for (const f of String(un.stdout).split('\n').filter(Boolean)) {
    try { text += `\n+++ b/${f}\n` + fs.readFileSync(path.join(wt, f), 'utf8').split('\n').map((l) => '+' + l).join('\n'); } catch { /* skip */ }
  }
  return { text, base, mainPath };
}

// ---------- C: picture sanity ----------
export function headlineViews(compareText) {
  if (!compareText) return [];
  const rows = compareText.split('\n').filter((l) => l.trim().startsWith('|') && !/^\s*\|[\s:|-]+\|\s*$/.test(l)).slice(1);
  return rows.map((r) => (r.split('|')[2] || '').trim().replace(/`/g, '')).filter(Boolean);
}
// Which views are judged: every COMPARE.md render and every *_aerial.png / *_street.png in the folder MUST have a stats record (those are the
// pictures a cold viewer opens first); every stats record that is not declared diagnostic must show road and no debug colour. A COMPARE.md
// render may not be diagnostic, and at most maxDiagShare of the records may be (a flag on every view is not a way past).
export function pictureProblems(folder, compareText, opts = {}) {
  const minRoad = opts.minRoad ?? 0.01, maxDebug = opts.maxDebug ?? 0.001, maxDiagShare = opts.maxDiagShare ?? 0.4;
  const problems = [], notes = [];
  const baseOf = (h) => path.basename(h).replace(/\.(png|jpe?g)$/i, '');
  const compareBases = new Set(headlineViews(compareText).map(baseOf));
  let names = []; try { names = fs.readdirSync(folder); } catch { /* none */ }
  const must = new Set([...compareBases, ...names.filter((n) => /_(aerial|street)\.png$/i.test(n)).map(baseOf)]);
  const records = new Map();
  for (const n of names.filter((x) => x.endsWith('.stats.json'))) {
    try { records.set(n.replace(/\.stats\.json$/, ''), JSON.parse(fs.readFileSync(path.join(folder, n), 'utf8'))); } catch { problems.push(`${n} is not valid JSON`); }
  }
  for (const b of must) {
    if (!records.has(b)) problems.push(`view "${b}"${compareBases.has(b) ? ' (listed in COMPARE.md)' : ' (an aerial/street view)'} has no ${b}.stats.json next to the render: render it with prototypes/mesh-view/render.mjs, which writes the record (road pixels, debug-colour pixels)`);
  }
  for (const [b, s] of records) {
    if (s.diagnostic) { if (compareBases.has(b)) problems.push(`view "${b}" is declared diagnostic but is listed as a COMPARE.md render: a compared render must be a real view of the subject`); continue; }
    const px = s.width * s.height;
    if (!(px > 0) || typeof s.road_px !== 'number') { problems.push(`${b}.stats.json: no pixel counts`); continue; }
    // the record is bound to the picture bytes: a record made for another render (or typed by hand) does not match this PNG
    if (typeof s.png_sha1 !== 'string') { problems.push(`${b}.stats.json has no png_sha1: it was not written by the current prototypes/mesh-view/render.mjs; render the view again`); continue; }
    const pngFile = path.join(folder, b + '.png');
    if (fs.existsSync(pngFile)) {
      const sha = crypto.createHash('sha1').update(fs.readFileSync(pngFile)).digest('hex');
      if (sha !== s.png_sha1) { problems.push(`${b}.stats.json was not made for ${b}.png (picture hash differs): the stats must come from rendering exactly this picture; run render.mjs again and do not edit the PNG afterwards`); continue; }
    }
    const roadFrac = s.road_px / px, dbg = (s.debug_px || 0) / px;
    if (roadFrac < minRoad) problems.push(`view "${b}": road surface covers ${(roadFrac * 100).toFixed(2)}% of the frame (need at least ${(minRoad * 100).toFixed(1)}%): the subject is not in the picture. If this is deliberately a close-up of ground only, declare it "diagnostic": true in the views json and keep it out of COMPARE.md`);
    if (dbg > maxDebug) problems.push(`view "${b}": ${(dbg * 100).toFixed(2)}% of the frame is in debug colours (${Object.entries(s.debug_by_class || {}).map(([k, v]) => k + ' ' + v + ' px').join(', ')}; allowed ${(maxDebug * 100).toFixed(2)}%): a placeholder "needs structure" or rock-cut surface is on screen where a structure or ground should be`);
    notes.push(`${b}: road ${(roadFrac * 100).toFixed(1)}%, debug ${(dbg * 100).toFixed(2)}%`);
  }
  if (records.size >= 3) {
    const diag = [...records].filter(([, s]) => s.diagnostic);
    if (diag.length / records.size > maxDiagShare) problems.push(`${diag.length} of ${records.size} views are declared diagnostic (more than ${maxDiagShare * 100}%): a diagnostic flag cannot be the way past the picture check`);
  }
  return { problems, notes };
}

// ---------- D: STATUS reading ----------
const USAGE_RE = /usage\s+(?:limit|window|warning)|session\s+limit|token\s+(?:budget|limit)|\bquota\b|out\s+of\s+(?:time|tokens|context)|running\s+(?:out\s+of|low\s+on)\s+(?:time|tokens|context)|context\s+(?:limit|window)|rate\s+limit|ran\s+out\s+of\s+(?:time|tokens)|no\s+time\s+left|time\s+limit|(?:short|low)\s+on\s+time|ran\s+short\s+of\s+time|limited\s+time|time\s+constraints?/i;
const NOT_ATTEMPTED_RE = /\bnot\s+(?:attempted|tried|started|done)\b|\b(?:did|have|had)\s+not\s+(?:attempt|try|get\s+to)|\bdidn'?t\s+(?:attempt|try|get\s+to)|\bnever\s+(?:attempted|tried)|\bleft\s+(?:undone|for\s+later)\b|\bdeferred\b|\bpostponed\b|\bfor\s+the\s+next\s+round\b/i;
export function statusProblems(statusText, faultsText) {
  const problems = [];
  const lines = String(statusText || '').replace(/<!--[\s\S]*?-->/g, '').split('\n');
  lines.forEach((l, i) => {
    if (NOT_ATTEMPTED_RE.test(l) && !(/\btried\b/i.test(l) && /\bblocked\b/i.test(l))) problems.push(`STATUS.md line ${i + 1}: an item is "not attempted" ("${l.trim().slice(0, 100)}"): attempt it (contract item 5), or write that you tried and what blocked it ("tried: ...; blocked by: ...")`);
    if (USAGE_RE.test(l)) problems.push(`STATUS.md line ${i + 1}: a usage/time limit is given as a reason ("${l.trim().slice(0, 100)}"): that is not an accepted reason; finish the item or list what blocks it technically`);
  });
  const f = String(faultsText || '').replace(/<!--[\s\S]*?-->/g, '');
  f.split('\n').forEach((l, i) => { if (USAGE_RE.test(l)) problems.push(`FAULTS.md line ${i + 1}: a usage/time limit is given as a reason ("${l.trim().slice(0, 100)}")`); });
  if (faultsText !== null && faultsText !== undefined) {
    // a "## Pass N" heading with no body is not a pass (a builder in a hurry writes the headings only)
    const passBodies = [...f.matchAll(/^##\s+Pass\s+\d+[^\n]*\n([\s\S]*?)(?=^##\s|(?![\s\S]))/gim)].map((m) => m[1].trim());
    const passes = passBodies.filter((b) => b.length >= 12).length;
    if (passBodies.length > passes) problems.push('FAULTS.md: ' + (passBodies.length - passes) + ' "## Pass N" section(s) are empty or under 12 characters: write the faults found in that pass');
    if (passes < 3) {
      const ro = f.match(/^[ \t]*Residual only unfixable:[ \t]*(.{40,})$/im);
      if (!ro) problems.push(`FAULTS.md: ${passes} self-audit pass(es), need 3 unless the residual list holds only unfixable items: then add a line "Residual only unfixable: <for each residual item, why it cannot be fixed>" (40+ characters)`);
    }
  }
  return problems;
}

// ---------- E: method-change rule (make_brief) ----------
const lowerIsBetter = (metric) => /fail|waiver|error|bad|fault|warn/i.test(metric);
// rounds: [{ round, values: { metric: number } }] oldest first. Returns a refusal string or null.
export function methodChangeProblem(rounds) {
  if (rounds.length < 3) return null;
  const [a, b, c] = rounds.slice(-3);
  const metrics = Object.keys(c.values).filter((m) => m in b.values && m in a.values);
  if (!metrics.length) return null;
  const improved = (x, y, m) => (lowerIsBetter(m) ? y < x : y > x);
  const anyImproved = metrics.some((m) => improved(a.values[m], b.values[m], m) || improved(b.values[m], c.values[m], m));
  if (anyImproved) return null;
  return `the last two rounds (${b.round}, ${c.round}) did not improve any gate number (${metrics.map((m) => `${m}: ${a.values[m]} -> ${b.values[m]} -> ${c.values[m]}`).join('; ')}). Another round on the same method is not commissioned: commission a method review first. To override, rerun with --override "<reason, 20+ characters>".`;
}
export function parseRounds(sectionText) {
  const out = [];
  for (const raw of String(sectionText || '').split('\n')) {
    const l = raw.trim().replace(/^[-*]\s+/, '');
    const m = l.match(/^([\w.-]+)\s*:\s*(.+)$/);
    if (!m) continue;
    const values = {};
    for (const kv of m[2].split(/[,;]/)) {
      const x = kv.trim().match(/^([a-z_ ]+?)\s*[=:]?\s*(\d+(?:\.\d+)?)(?:\/\d+)?$/i);
      if (x) values[x[1].trim().toLowerCase().replace(/\s+/g, '_')] = Number(x[2]);
    }
    out.push({ round: m[1], values, raw: l });
  }
  return out;
}
