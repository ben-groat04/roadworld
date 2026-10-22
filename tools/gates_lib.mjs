// Gates v2/v3: hand-in checks that MEASURE the work instead of reading what the builder wrote about it.
// Used by tools/check_handin.mjs (when run with --gates-version 2) and tools/make_brief.mjs (grammar, scope, history rules).
// A brief carries `GATES-VERSION: 2` plus `GATE: <line>` lines (make_brief.mjs emits them from the task file's "## Gates"
// section); briefs written before this have neither and none of the checks below run for them.
//
// Gate line grammar (one per line, a leading "- " is ignored):
//   <metric> <op> <value> [(baseline <v>[, target <v>])]      metric: scene_check | ctest failed_assertions | ctest failed_tests
//        op: >= <= ==   value: "P/T" for scene_check (P passed of T checks), a number otherwise.
//        The value after the op is the FLOOR: a measurement on the wrong side of it refuses the hand-in. A target (if given)
//        that is not reached still passes, but only when the STATUS "Skipped, refused or failed" section names the metric.
//        On the two ctest failure metrics `==` behaves as `<=` (an improvement is never punished); make_brief refuses `==` there.
//   allow_waiver <key>                                          lets the waiver guard pass one named finding (see waiverFindings)
//   picture_min_road <fraction> | picture_max_debug <fraction>  overrides for the picture sanity check
// BLOCKED form (STATUS.md, first section): `BLOCKED: <item> | tried: <what> | blocked by: <exact error or missing thing> | needs: <what unblocks it>`.
//   An honest "I cannot do this" is a legitimate way to finish. It is never counted as an excuse by the status reader and raises a
//   gate notice (tools/hooks/handin_gate_impl.mjs logs it). Excuses without tried/blocked-by (time, scope, later round) still refuse.
// Internal errors never refuse: callers log FAIL-OPEN and carry on (a gate that blocks wrongly would trap the agent). The exceptions are a
// gate that STARTED and failed (scene check crashed or timed out, build broke): that is a refusal naming the cause.
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import crypto from 'node:crypto';
import { spawnSync } from 'node:child_process';

export const METRICS = ['scene_check', 'ctest failed_assertions', 'ctest failed_tests'];
export const FAILURE_METRICS = ['ctest failed_assertions', 'ctest failed_tests'];
export const METRIC_DIRECTION = { scene_check: 'higher', ctest_failed_assertions: 'lower', ctest_failed_tests: 'lower' };
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

// Time budget: build 40 s + check 150 s = 190 s, inside the 270 s spawn limit of the hand-in hook (and its 300 s settings timeout).
export const SCENE_BUILD_MS = 40000, SCENE_CHECK_MS = 150000;
// The check runs on a COPY of prototypes/road-draw in the OS temp dir (node_modules linked), so the worktree under inspection is never written
// to (audit fault 5: `node build.mjs` regenerated the tracked index.html). Missing infrastructure -> { error } (callers fail open);
// anything that STARTED and broke (build error, timeout, no summary line, crash) -> { fail } (a refusal naming the cause).
export function measureSceneCheck(wt, opts = {}) {
  const dir = path.join(wt, 'prototypes', 'road-draw');
  if (!fs.existsSync(path.join(dir, 'tools', 'scene.mjs'))) return { error: 'no prototypes/road-draw/tools/scene.mjs in ' + wt };
  if (!fs.existsSync(path.join(dir, 'node_modules'))) return { error: 'prototypes/road-draw/node_modules is missing in ' + wt + ' (run `npm install` there); the scene check could not be measured' };
  const tmp = fs.mkdtempSync(path.join(os.tmpdir(), 'scenegate-'));
  try {
    const copy = path.join(tmp, 'road-draw');
    fs.cpSync(dir, copy, { recursive: true, filter: (s) => !/[\\/](node_modules|screenshots|x|\.git)$/.test(s) });
    try { fs.symlinkSync(path.join(dir, 'node_modules'), path.join(copy, 'node_modules'), 'junction'); } catch (e) { return { error: 'cannot link node_modules into the temp copy: ' + e.message }; }
    if (fs.existsSync(path.join(copy, 'build.mjs'))) {
      const b = sh(process.execPath, ['build.mjs'], copy, opts.buildMs || SCENE_BUILD_MS);
      if (b.error && b.error.code === 'ETIMEDOUT') return { fail: `node build.mjs timed out after ${(opts.buildMs || SCENE_BUILD_MS) / 1000} s in prototypes/road-draw` };
      if (b.status !== 0) return { fail: 'node build.mjs failed in prototypes/road-draw: ' + String(b.stderr || b.stdout).slice(0, 400) };
    }
    const t0 = Date.now();
    const r = sh(process.execPath, ['tools/scene.mjs', '--check'], copy, opts.checkMs || SCENE_CHECK_MS);
    if (r.error && r.error.code === 'ETIMEDOUT') return { fail: `the scene check timed out after ${(opts.checkMs || SCENE_CHECK_MS) / 1000} s (it normally takes seconds): the prototype hangs or crashes the browser` };
    if (r.error) return { error: 'scene check did not start: ' + r.error.message };
    const out = String(r.stdout || '');
    const sum = out.match(/(\d+)\/(\d+) passed in/);
    if (!sum) return { fail: 'the scene check ran but printed no "N/M passed" line (crashed?): ' + (String(r.stderr || out).replace(/\s+/g, ' ').slice(0, 300)) };
    const failed = [...out.matchAll(/^FAIL\s+(\S+)\s+(.*)$/gm)].map((x) => x[1] + ' (' + x[2].slice(0, 90) + ')');
    return { value: `${sum[1]}/${sum[2]}`, passed: +sum[1], total: +sum[2], failed, seconds: (Date.now() - t0) / 1000 };
  } finally { try { fs.rmSync(tmp, { recursive: true, force: true }); } catch { /* temp */ } }
}

// Number of tests ctest knows in a build dir: add_test( calls in every CTestTestfile.cmake below it (no ctest binary needed: it is not on PATH here).
export function countRegisteredTests(buildDir) {
  const names = new Set();
  const walk = (d, depth) => {
    if (depth > 4) return;
    let ents; try { ents = fs.readdirSync(d, { withFileTypes: true }); } catch { return; }
    for (const e of ents) {
      const p = path.join(d, e.name);
      if (e.isDirectory()) { if (!/^(CMakeFiles|_deps|Testing)$/.test(e.name)) walk(p, depth + 1); }
      else if (e.name === 'CTestTestfile.cmake') {
        try { for (const m of fs.readFileSync(p, 'utf8').matchAll(/^\s*add_test\s*\(\s*(?:\[=*\[([^\]]+)\]=*\]|"?([^\s")]+))/gm)) names.add(m[1] || m[2]); } catch { /* skip */ }
      }
    }
  };
  walk(buildDir, 0);
  return names.size;
}
const countTestCases = (wt) => {
  let n = 0;
  const walk = (d) => { let ents; try { ents = fs.readdirSync(d, { withFileTypes: true }); } catch { return; } for (const e of ents) { const p = path.join(d, e.name); if (e.isDirectory()) walk(p); else if (/\.cpp$/.test(e.name)) { try { n += (fs.readFileSync(p, 'utf8').match(/^\s*(?:DOCTEST_)?TEST_CASE(?:_TEMPLATE)?\s*\(/gm) || []).length; } catch { /* skip */ } } } };
  walk(path.join(wt, 'tests'));
  return n;
};

// Reads the newest build*/Testing/Temporary/LastTest.log (the whole suite takes minutes: the gate never runs it, it reads a fresh result).
// A crashed test (no doctest summary in its block, or ctest "***Exception"/"Test Failed") counts as a failed TEST. A result that covers fewer
// ctest tests than the build registers, or fewer doctest cases than the sources hold, is a subset run and is refused; so is a result for which the
// number of registered tests cannot be found (never skip that check silently).
export function measureCtest(wt) {
  let dirs = [];
  try { dirs = fs.readdirSync(wt).filter((n) => /^build/.test(n)).map((n) => path.join(wt, n)); } catch { /* none */ }
  const logs = dirs.map((d) => path.join(d, 'Testing', 'Temporary', 'LastTest.log')).filter((f) => fs.existsSync(f))
    .map((f) => ({ f, t: fs.statSync(f).mtimeMs })).sort((a, b) => b.t - a.t);
  if (!logs.length) return { fail: 'no ctest result: no build*/Testing/Temporary/LastTest.log in ' + wt + '; build and run `ctest --test-dir build` in your worktree' };
  const { f, t } = logs[0];
  const buildDir = path.dirname(path.dirname(path.dirname(f)));
  const src = ['sim_core', 'tests'].map((d) => newestMtime(path.join(wt, d))).sort((a, b) => b.t - a.t)[0];
  if (src && src.t > t + 1000) return { fail: `ctest result ${f} is older than the sources (${src.f}); rebuild and run the whole suite again` };
  const text = fs.readFileSync(f, 'utf8').replace(/\r\n/g, '\n');
  const blocks = text.split(/^\d+\/\d+ Test:\s*/m).slice(1);
  const names = blocks.map((b) => b.split('\n')[0].trim());
  const total = countRegisteredTests(buildDir);
  if (total === 0) return { fail: `cannot tell how many tests the build registers (no add_test in ${buildDir}/**/CTestTestfile.cmake): the result cannot be shown to be a whole-suite run; configure the build with cmake` };
  if (blocks.length < total) return { fail: `ctest result ${f} covers ${blocks.length} of ${total} registered tests (a subset run): run the whole suite` };
  let assertions = 0, failedAssertions = 0, cases = 0, failedTests = 0;
  const crashed = [];
  blocks.forEach((b, i) => {
    const as = [...b.matchAll(/\[doctest\] assertions:\s*(\d+)\s*\|\s*(\d+) passed\s*\|\s*(\d+) failed/g)];
    for (const m of as) { assertions += +m[1]; failedAssertions += +m[3]; }
    for (const m of b.matchAll(/\[doctest\] test cases:\s*(\d+)\s*\|/g)) cases += +m[1];
    const failed = /^Test Failed\./m.test(b) || /\*\*\*(Exception|Failed|Timeout|SegFault)/.test(b);
    const noSummary = !as.length && !/^Test Passed\./m.test(b);
    if (failed || noSummary) { failedTests++; if (!failed) crashed.push(names[i]); }
  });
  const sources = countTestCases(wt);
  if (cases && sources && cases < sources * 0.95) return { fail: `ctest result ${f} ran ${cases} doctest cases but the sources hold ${sources} TEST_CASEs (a filtered or partial run): run the whole suite` };
  return { failed_assertions: failedAssertions, failed_tests: failedTests, crashed, assertions, tests: blocks.length, registered: total, cases, file: f };
}

// Evaluates metric gates. Returns { problems, notes, shortfalls, errors, blocked } ; shortfalls = metrics below target but at/above floor.
// blockedItems: parseBlocked(STATUS): a measured floor miss that a valid BLOCKED item names (metric name in its item text) is reported in `blocked`, not as a refusal.
export function runMeasuredGates(gates, wt, log = () => {}, blockedItems = []) {
  const out = { problems: [], notes: [], shortfalls: [], errors: [], blocked: [] };
  const metrics = gates.filter((g) => g.kind === 'metric');
  let scene = null, ctest = null;
  const coveredByBlocked = (g) => blockedItems.some((b) => b.ok && new RegExp(g.metric.replace(/[_ ]/g, '[ _]'), 'i').test(b.item));
  for (const g of metrics) {
    let value = null, extra = '';
    const op = FAILURE_METRICS.includes(g.metric) && g.op === '==' ? '<=' : g.op;
    if (g.metric === 'scene_check') {
      scene ??= measureSceneCheck(wt);
      if (scene.error) { out.errors.push(`${g.metric}: ${scene.error}`); continue; }
      if (scene.fail) { if (coveredByBlocked(g)) out.blocked.push(`${g.metric}: ${scene.fail}`); else out.problems.push(`gate ${g.raw}: ${scene.fail}`); continue; }
      value = scene.value; extra = scene.failed.length ? ` failing: ${scene.failed.join('; ')}` : '';
      const ft = splitVal(g.floor).t;
      if (ft !== null && ft !== scene.total) { out.problems.push(`gate ${g.raw}: the scene check now has ${scene.total} checks, the gate expects ${ft}; the main session must update the gate, do not remove checks`); continue; }
      out.notes.push(`measured scene_check ${value} in ${scene.seconds.toFixed(1)} s${extra}`);
    } else {
      ctest ??= measureCtest(wt);
      if (ctest.fail) { if (coveredByBlocked(g)) out.blocked.push(`${g.metric}: ${ctest.fail}`); else out.problems.push(`gate ${g.raw}: ${ctest.fail}`); continue; }
      value = g.metric === 'ctest failed_assertions' ? ctest.failed_assertions : ctest.failed_tests;
      out.notes.push(`measured ${g.metric} ${value} (${ctest.assertions} assertions, ${ctest.tests}/${ctest.registered} tests, ${ctest.cases} cases, from ${path.basename(path.dirname(path.dirname(path.dirname(ctest.file))))}${ctest.crashed.length ? '; CRASHED: ' + ctest.crashed.join(', ') : ''})`);
    }
    if (!satisfies(value, op, g.floor)) {
      const msg = `gate ${g.raw}: measured ${value}, which is on the wrong side of ${op} ${g.floor} (the floor)${extra}. The number you typed in STATUS is not read; fix the cause, or finish with a BLOCKED line in STATUS.md naming ${g.metric} ("BLOCKED: <item> | tried: ... | blocked by: ... | needs: ...")`;
      if (coveredByBlocked(g)) out.blocked.push(`${g.metric} measured ${value}, floor ${op} ${g.floor}${extra}`); else out.problems.push(msg);
    } else if (g.target && !satisfies(value, g.metric === 'scene_check' || op === '>=' ? '>=' : op === '<=' ? '<=' : '==', g.target)) {
      out.shortfalls.push({ metric: g.metric, measured: value, target: g.target });
    }
  }
  return out;
}

// ---------- B: waiver guard ----------
// Test locations watched: the engine tests, the scene check and the prototype's geometry test, the Unreal test folders, every prototype test*.mjs,
// and the gates' own tests (a weakened gate test is a waiver too).
export const WAIVER_PATHS = ['tests', 'prototypes/road-draw/tools/scene.mjs', 'prototypes/road-draw/test-geom.mjs', ':(glob)prototypes/*/test*.mjs', ':(glob)unreal/**/Tests/**', ':(glob)unreal/**/test_*.cpp', 'tools/test_gates.mjs', 'tools/hooks/test_hooks.mjs'];
const KIND_RE = [
  ['should_fail', /\bshould_fail\b/], ['skip', /doctest::skip|\bDOCTEST_\w*SKIP|\bxit\(|\bit\.skip\(|\btest\.skip\(|\.skip\(/],
  ['may_fail', /\bmay_fail\b|expected_failures/], ['ctest_disabled', /\bDISABLED\b|\bWILL_FAIL\b/], ['warn', /\bWARN(?:_MESSAGE|_EQ|_FALSE)?\(/],
  ['ctest_labels', /\bLABELS\b/],
];
// A tolerance constant: kTolHeight, k_eps, max_tol, tol_m, epsilon, EPS, tolerance... (not "reps", "steps").
const TOL_RE = /\b(k\w*(?:Tol|Eps|Tolerance)\w*|\w*_(?:tol|eps|tolerance|epsilon)\w*|(?:tol|eps|epsilon|tolerance|TOL|EPS|Tol|Eps)\w*)\s*(?:=|\{)\s*([0-9]*\.?[0-9]+(?:[eE][-+]?\d+)?)/;
const testName = (l) => { const m = l.match(/^\s*(?:DOCTEST_)?TEST_CASE(?:_TEMPLATE)?\s*\(\s*"((?:[^"\\]|\\.)*)"/); return m ? m[1] : null; };

// What the guard looks for (forms real waivers took in tests/test_grading.cpp and scene.mjs, plus the cheap evasions the gates2 audit found):
//   should_fail / doctest::skip / may_fail / WILL_FAIL / LABELS / WARN added; a name added to is_rough() or more GRADE_CHECK(is_rough(...)); a loosened or
//   new tolerance constant; a loosened threshold on an edited line (sign-aware, `0.3 * 3`, `== 0` -> `<= 5`, extra lines in the hunk, a narrowed loop bound);
//   fewer CHECK/REQUIRE lines that still count after comments, `#if 0` and `if (false)` blocks are removed; `#if`/`#ifdef`/`#ifndef` added; `|| true`;
//   CHECK(true); `if (false)`; Approx epsilon/margin widened; an early `return;`; a redefined CHECK macro; a CHECK moved under an `if`; an edited
//   expected value on an assertion line; a test case or scene check deleted or renamed; a test source dropped from the test build (CMake `#` comments
//   stripped first); a scene check with a hard-coded `pass: true`.
// WHAT IT CANNOT SEE: a test that is made vacuous without touching an assertion line (an empty fixture, a loop over zero cases, input changed so the
// check passes trivially); a reworded expectation spread over several lines; a waiver in a test location outside WAIVER_PATHS; anything done with
// the build dir or CTest flags instead of the sources. diffText: unified diff (-U0); contents: { file: { old, new } } enables the effective-count check.
const ASSERT_RE = /\b(?:GRADE_CHECK|(?:DOCTEST_)?(?:CHECK|REQUIRE)(?:_MESSAGE|_EQ|_NE|_FALSE|_THROWS\w*|_NOTHROW)?)\s*\(/;
const numbers = (l) => [...l.matchAll(/(?<![\w.])(\d+\.?\d*(?:[eE][-+]?\d+)?)(?![\w.])/g)].map((m) => Number(m[1]));
const skeleton = (l) => l.replace(/(?<![\w.])\d+\.?\d*(?:[eE][-+]?\d+)?(?![\w.])/g, '#').replace(/\s+/g, ' ').trim();
const isCodeFile = (f) => /\.(cpp|cc|h|hpp|mjs|js)$/.test(f);
export function stripCode(text) {
  let t = String(text).replace(/\/\*[\s\S]*?\*\//g, ' ').replace(/(^|[^:"'\\])\/\/.*$/gm, '$1');
  t = t.replace(/^[ \t]*#\s*if\s+(?:0|false)\b[\s\S]*?^[ \t]*#\s*(?:endif|else)\b/gm, '');
  t = t.replace(/\bif\s*\(\s*(?:false|0)\s*\)\s*\{[^{}]*(?:\{[^{}]*\}[^{}]*)*\}/g, '');
  return t;
}
const countAssertions = (text) => stripCode(text).split('\n').filter((l) => ASSERT_RE.test(l)).length;
// comparisons on a line: [{ lhs, op, val }] ; val is null when the right side is not plain arithmetic on numbers
export function comparisons(line) {
  if (/\w<[\w:,\s]+>/.test(line.replace(/<\s*\d/g, ' '))) return [];
  const out = [];
  const re = /(<=|>=|==|!=|(?<![<\-])<(?![<=])|(?<![\->])>(?![>=]))/g;
  let m;
  while ((m = re.exec(line))) {
    const lhs = line.slice(Math.max(0, m.index - 30), m.index).replace(/\s+/g, ' ').trim();
    const rest = line.slice(m.index + m[1].length);
    let depth = 0, end = rest.length;
    for (let i = 0; i < rest.length; i++) { const c = rest[i]; if (c === '(') depth++; else if (c === ')') { if (depth === 0) { end = i; break; } depth--; } else if ((c === ';' || c === ',' || c === '&' || c === '|' || c === '{' || c === '?' || c === ':') && depth === 0) { end = i; break; } }
    const rhs = rest.slice(0, end).trim();
    let val = null;
    if (/^[-+*/().\d\seE]+$/.test(rhs) && /\d/.test(rhs)) { try { val = Function('"use strict"; return (' + rhs + ')')(); if (!Number.isFinite(val)) val = null; } catch { val = null; } }
    out.push({ lhs, op: m[1], val });
  }
  return out;
}
const holds = (op, v, x) => (op === '<' ? x < v : op === '<=' ? x <= v : op === '>' ? x > v : op === '>=' ? x >= v : op === '==' ? x === v : x !== v);
// the new comparison accepts everything the old one did, and more
export function looserCmp(o, n) {
  const pts = []; for (const v of [o.val, n.val]) pts.push(v - 1, v - 1e-6, v, v + 1e-6, v + 1);
  let more = false;
  for (const x of pts) { const a = holds(o.op, o.val, x), b = holds(n.op, n.val, x); if (a && !b) return false; if (!a && b) more = true; }
  return more;
}

export function waiverFindings(diffText, allowed = [], contents = null) {
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
  const add = (key, file, what) => { if (!findings.some((x) => x.key === key && x.file === file)) findings.push({ key, file, what }); };
  for (const { file: f, add: A, rem: R, hunks } of files) {
    const base = path.basename(f), code = isCodeFile(f), cmake = /CMakeLists\.txt$|\.cmake$/.test(f);
    // keyword waivers: more occurrences than before in the file
    for (const [kind, re] of KIND_RE) {
      const a = A.filter((l) => re.test(l)), r = R.filter((l) => re.test(l));
      if (a.length > r.length) for (const l of a.slice(r.length)) add(`${kind}:${testName(l) || (l.match(/["']([\w ./-]{3,60})["']/) || [])[1] || l.trim().slice(0, 40)}`, f, `${kind} added: ${l.trim().slice(0, 110)}`);
    }
    if (A.filter((l) => /^\s*#\s*if\s+0\b/.test(l)).length > R.filter((l) => /^\s*#\s*if\s+0\b/.test(l)).length) add('if0:' + base, f, '"#if 0" added: code (a test?) is switched off');
    if (code) {
      // conditional compilation added in a test file (#ifdef NEVER, #if SOMETHING_UNDEFINED): the guarded check may never be compiled
      const pre = (L) => L.filter((l) => /^\s*#\s*(?:if|ifdef|ifndef)\b/.test(l) && !/^\s*#\s*if\s+0\b/.test(l));
      if (pre(A).length > pre(R).length) for (const l of pre(A).slice(pre(R).length)) add('preproc:' + base + ':' + l.trim().slice(0, 30), f, `conditional compilation added: "${l.trim().slice(0, 70)}" (the code under it may never be built)`);
      // the cheap evasions of the assertion itself
      for (const l of A) {
        if (R.includes(l)) continue;
        if (/\|\|\s*(?:true|1)\b(?![.\w])/.test(l)) add('or_true:' + base + ':' + l.trim().slice(0, 30), f, `"|| true" added: ${l.trim().slice(0, 90)} (the condition can no longer fail)`);
        if (/\b(?:GRADE_CHECK|(?:DOCTEST_)?(?:CHECK|REQUIRE))(?:_MESSAGE)?\s*\(\s*(?:true|1|!false|!0)\s*[,)]/.test(l)) add('trivial_check:' + base + ':' + l.trim().slice(0, 30), f, `an assertion that cannot fail: ${l.trim().slice(0, 90)}`);
        if (/\b(?:if|while)\s*(?:constexpr\s*)?\(\s*(?:false|0)\s*\)/.test(l)) add('dead_branch:' + base + ':' + l.trim().slice(0, 30), f, `a branch that never runs: ${l.trim().slice(0, 90)}`);
        if (/\bif\s*\([^;{]*\)\s*\{?\s*(?:GRADE_CHECK|(?:DOCTEST_)?(?:CHECK|REQUIRE))/.test(l)) add('guarded_check:' + base + ':' + l.trim().slice(0, 30), f, `a check now sits under an if: ${l.trim().slice(0, 90)} (it only runs when the condition holds)`);
        if (/^\s*#\s*define\s+(?:CHECK|REQUIRE|GRADE_CHECK|DOCTEST_\w+|WARN)\w*\b/.test(l)) add('macro_redefined:' + base + ':' + l.trim().slice(0, 40), f, `an assertion macro is redefined: ${l.trim().slice(0, 90)}`);
      }
      if (/\.(cpp|cc|h|hpp)$/.test(f) && A.filter((l) => /^\s*return\s*;\s*$/.test(l)).length > R.filter((l) => /^\s*return\s*;\s*$/.test(l)).length) add('early_return:' + base, f, 'a bare `return;` was added: the rest of the test case after it never runs');
      // Approx epsilon / margin widened
      const eps = (L) => L.flatMap((l) => [...l.matchAll(/\.(?:epsilon|margin)\s*\(\s*([-+]?[0-9.]+(?:[eE][-+]?\d+)?)\s*\)/g)].map((m) => Number(m[1])));
      const eo = eps(R), en = eps(A);
      if (en.length && (!eo.length || Math.max(...en) > Math.max(...eo))) add('approx:' + base, f, `an Approx epsilon/margin was ${eo.length ? 'widened ' + Math.max(...eo) + ' -> ' + Math.max(...en) : 'added: ' + Math.max(...en)}`);
    }
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
    // an edited line: a comparison loosened (sign-aware; `0.3 * 3` is evaluated; `== 0` -> `<= 5`; extra lines in the hunk do not hide it), a loop bound
    // narrowed, or an expected value changed on an assertion line. Old and new comparisons are paired by their left-hand side inside one hunk.
    if (code) for (const hk of hunks) {
      const oldC = hk.rem.flatMap((l) => comparisons(l).map((c) => ({ ...c, line: l })));
      const flaggedLines = new Set();
      for (const nl of hk.add) {
        for (const nc of comparisons(nl)) {
          const oc = oldC.find((c) => c.lhs === nc.lhs && c.val !== null && nc.val !== null && (c.op !== nc.op || c.val !== nc.val));
          if (!oc) continue;
          if (/^\s*for\s*\(/.test(nl) && /^<=?$/.test(nc.op) && /^<=?$/.test(oc.op) && nc.val < oc.val) { add(`loop_bound:${base}:${skeleton(nl).slice(0, 40)}`, f, `a loop bound was narrowed: "${oc.line.trim().slice(0, 60)}" -> "${nl.trim().slice(0, 60)}"`); flaggedLines.add(nl); }
          else if (looserCmp(oc, nc)) { add(`threshold:${base}:${skeleton(nl).slice(0, 50)}`, f, `a threshold was loosened on an edited line: "${oc.line.trim().slice(0, 70)}" -> "${nl.trim().slice(0, 70)}"`); flaggedLines.add(nl); }
        }
      }
      // expected value edited on an assertion line (same shape, other numbers), when not already reported as a loosened threshold
      const remAssert = hk.rem.filter((l) => ASSERT_RE.test(l));
      for (const nl of hk.add.filter((l) => ASSERT_RE.test(l) && !flaggedLines.has(l))) {
        const same = remAssert.find((ol) => ol !== nl && skeleton(ol) === skeleton(nl) && numbers(ol).join() !== numbers(nl).join());
        // a comparison made STRICTER (< 0.3 -> < 0.1) is a better test, not an edited expectation
        const tightened = comparisons(nl).some((nc) => oldC.some((oc) => oc.lhs === nc.lhs && oc.val !== null && nc.val !== null && looserCmp(nc, oc)));
        if (same && !tightened) add(`expected_value:${base}:${skeleton(nl).slice(0, 40)}`, f, `an expected value on an assertion line was edited: "${same.trim().slice(0, 70)}" -> "${nl.trim().slice(0, 70)}" (a changed expectation needs a photo measurement or a written rule behind it, and an allow_waiver line)`);
      }
    }
    // fewer assertions than before: from the files themselves when they are known (counts only what still runs), else from the diff
    const deletedHere = R.filter((l) => testName(l) && !addedTests.has(testName(l))).length;
    if (!cmake && contents && contents[f]) {
      const nOld = countAssertions(contents[f].old || ''), nNew = countAssertions(contents[f].new || '');
      if (nNew < nOld && !deletedHere) add('fewer_checks:' + base, f, `${nOld - nNew} fewer CHECK/REQUIRE assertion(s) that still run than before in this file (removed, commented out, block-commented, or inside #if 0 / if (false))`);
    } else {
      const nA = A.filter((l) => ASSERT_RE.test(l) && !/^\s*\/\//.test(l)).length, nR = R.filter((l) => ASSERT_RE.test(l) && !/^\s*\/\//.test(l)).length;
      if (!cmake && nR > nA && !deletedHere) add('fewer_checks:' + base, f, `${nR - nA} fewer CHECK/REQUIRE assertion line(s) than before in this file (a check removed or commented out)`);
    }
    // a test source dropped from the test build (CMake comments stripped first: a `#`-commented line is a dropped line)
    if (/CMakeLists\.txt$/.test(f)) {
      const Aclean = A.map((l) => l.replace(/#.*$/, ''));
      for (const l of R) { const m = l.replace(/#.*$/, '').match(/([\w./-]+\.cpp)/); if (m && !Aclean.some((x) => x.includes(m[1]))) add('deleted_file:' + m[1], f, `${m[1]} was removed from the test build (or only commented out)`); }
    }
    // scene.mjs: hard-coded pass
    if (/scene\.mjs$/.test(f)) {
      const pt = (L) => L.filter((l) => /pass:\s*true\b/.test(l));
      for (const l of pt(A).filter((x) => !pt(R).includes(x))) add(`pass_true:${(l.match(/name:\s*'([^']+)'/) || [])[1] || 'unnamed'}`, f, 'check with hard-coded pass: true added: ' + l.trim().slice(0, 100));
      for (const l of R) { const n = (l.match(/name:\s*'([^']+)'/) || [])[1]; if (n && !addedJsNames.has(n) && /results\.push/.test(l)) add(`deleted_check:${n}`, f, `scene check "${n}" removed`); }
    }
    // deleted test cases
    for (const l of R) { const n = testName(l); if (n && !addedTests.has(n)) add(`deleted_test:${n}`, f, `test case "${n}" was deleted (or renamed: a renamed test whose expectation changed needs the same allow_waiver line)`); }
  }
  // an allow_waiver line names the full key, the key without its kind prefix, or a prefix of the key ("expected_value:test_grading.cpp" allows every finding under it)
  const ok = (fd) => allowed.some((a) => a === fd.key || a === fd.key.split(':').slice(1).join(':') || fd.key.startsWith(a + ':'));
  return { findings, refused: findings.filter((f) => !ok(f)), allowedHits: findings.filter(ok) };
}

export function collectWaiverDiff(wt) {
  const git = (...a) => sh('git', a, wt, 60000);
  const top = git('rev-parse', '--show-toplevel'); if (top.status !== 0) return { error: 'not a git worktree: ' + wt };
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
  // old and new contents of every changed guarded code file (for the effective assertion count)
  const contents = {};
  const names = String(git('diff', '--name-only', base, '--', ...WAIVER_PATHS).stdout).split('\n').filter(Boolean);
  for (const f of names) {
    if (!isCodeFile(f)) continue;
    const o = git('show', `${base}:${f}`);
    let nw = ''; try { nw = fs.readFileSync(path.join(wt, f), 'utf8'); } catch { /* deleted */ }
    contents[f] = { old: o.status === 0 ? String(o.stdout) : '', new: nw };
  }
  return { text, base, mainPath, contents };
}

// ---------- C: picture sanity ----------
export function headlineViews(compareText) {
  if (!compareText) return [];
  const rows = compareText.split('\n').filter((l) => l.trim().startsWith('|') && !/^\s*\|[\s:|-]+\|\s*$/.test(l)).slice(1);
  return rows.map((r) => (r.split('|')[2] || '').trim().replace(/`/g, '')).filter(Boolean);
}
function pngSize(file) {
  try { const b = fs.readFileSync(file); if (b.length < 24 || b.toString('latin1', 1, 4) !== 'PNG') return null; return { width: b.readUInt32BE(16), height: b.readUInt32BE(20) }; } catch { return null; }
}
function walkFiles(dir, depth = 0, out = []) {
  if (depth > 4) return out;
  let ents; try { ents = fs.readdirSync(dir, { withFileTypes: true }); } catch { return out; }
  for (const e of ents) { const p = path.join(dir, e.name); if (e.isDirectory()) { if (!/^(node_modules|\.git)$/.test(e.name)) walkFiles(p, depth + 1, out); } else out.push(p); }
  return out;
}
const reEsc = (s) => s.replace(/[.*+?^${}()|[\]\\]/g, '\\$&');
// THRESHOLDS: no photo measurement stands behind these numbers; they are documented defaults sitting in the empty gap between "subject missing" and
// "subject shown" in the one real data set (round 9, re-rendered): s6 street 0.00 % road, s6 close 0.00 %, s6 along 0.35 % road, s4 aerial 50.53 % debug colour,
// s4 street 23.51 %, whereas a view of the subject has road on several percent of the frame and no debug colour at all. 1 % road and 0.1 % debug are the
// least strict values that still refuse every one of those. A legitimate close-up of something else (a wall face, a tunnel interior) declares itself
// diagnostic with a reason in STATUS.md, or the task file overrides with picture_min_road / picture_max_debug.
//
// Where the records are: next to each render, at any depth below the hand-in folder (renders in after/ and before/ are found there). The views judged:
// every COMPARE.md render (resolved to its file) and every *_aerial.png / *_street.png below the folder, except pictures under a folder called before/
// (history). Each judged view needs <name>.stats.json beside <name>.png; the record must match the PNG (sha1, width, height), have counts that fit the
// frame, show road and no debug colour. A `diagnostic` view may not be named *_aerial/*_street, may not be a COMPARE.md render, and must be explained in
// STATUS.md (a line with the view name, the word diagnostic and a reason of 20+ characters); at most maxDiagShare of the records may be diagnostic.
export function pictureProblems(folder, compareText, opts = {}) {
  const minRoad = opts.minRoad ?? 0.01, maxDebug = opts.maxDebug ?? 0.001, maxDiagShare = opts.maxDiagShare ?? 0.4;
  const statusText = opts.statusText || '';
  const problems = [], notes = [];
  const all = walkFiles(folder);
  const rel = (p) => path.relative(folder, p).split(path.sep).join('/');
  const png = (p) => p.replace(/\.stats\.json$/, '.png');
  const records = new Map();
  for (const p of all.filter((x) => x.endsWith('.stats.json') && !/(^|\/)before\//i.test(rel(x)))) {   // pictures under before/ are history, not the round's work
    try { records.set(path.resolve(p), JSON.parse(fs.readFileSync(p, 'utf8'))); } catch { problems.push(`${rel(p)} is not valid JSON`); }
  }
  // which pictures must have a record
  const must = new Map();
  for (const p of all.filter((x) => /_(aerial|street)\.png$/i.test(x) && !/(^|\/)before\//i.test(rel(x)))) must.set(path.resolve(p), 'an aerial/street view');
  const bases = [folder, path.dirname(folder)];
  const compareFiles = new Set();
  for (const cell of headlineViews(compareText)) {
    let p = path.isAbsolute(cell) ? cell : bases.map((b) => path.join(b, cell)).find((x) => fs.existsSync(x));
    if (!p) { const bn = path.basename(cell); p = all.find((x) => path.basename(x) === bn && !/(^|\/)before\//i.test(rel(x))); }
    if (!p) continue; // a missing render is check_handin's own COMPARE.md complaint
    must.set(path.resolve(p), 'listed in COMPARE.md'); compareFiles.add(path.resolve(p));
  }
  for (const [p, why] of must) {
    const rec = path.resolve(p.replace(/\.(png|jpe?g)$/i, '.stats.json'));
    if (!records.has(rec)) problems.push(`view "${rel(p)}" (${why}) has no ${path.basename(rec)} next to the render: render it with prototypes/mesh-view/render.mjs or prototypes/road-draw/tools/scene.mjs, which write the record (road pixels, debug-colour pixels)`);
  }
  for (const [file, s] of records) {
    const b = rel(file).replace(/\.stats\.json$/, '');
    const pngFile = png(file);
    const isCompare = compareFiles.has(path.resolve(pngFile));
    const nm = reEsc(path.basename(b));
    const explained = new RegExp(`${nm}[^\\n]*diagnostic[^\\n]{20,}|diagnostic[^\\n]*${nm}[^\\n]{20,}`, 'i').test(statusText);
    if (s.diagnostic) {
      if (isCompare) problems.push(`view "${b}" is declared diagnostic but is listed as a COMPARE.md render: a compared render must be a real view of the subject`);
      else if (/_(aerial|street)$/i.test(b)) problems.push(`view "${b}" is named like a headline view (*_aerial / *_street) but is declared diagnostic: rename it (a headline view cannot be a diagnostic one)`);
      else if (!explained) problems.push(`view "${b}" is declared diagnostic but STATUS.md has no line with its name, the word "diagnostic" and a reason (20+ characters)`);
      continue;
    }
    const W = s.width, H = s.height, px = W * H;
    if (!(px > 0) || typeof s.road_px !== 'number') { problems.push(`${b}.stats.json: no pixel counts`); continue; }
    if (typeof s.png_sha1 !== 'string') { problems.push(`${b}.stats.json has no png_sha1: it was not written by the current render tool; render the view again`); continue; }
    if (!fs.existsSync(pngFile)) { problems.push(`${b}.stats.json has no ${path.basename(pngFile)} beside it: a record without its picture proves nothing (a typed hash)`); continue; }
    const sha = crypto.createHash('sha1').update(fs.readFileSync(pngFile)).digest('hex');
    if (sha !== s.png_sha1) { problems.push(`${b}.stats.json was not made for ${path.basename(pngFile)} (picture hash differs): the stats must come from rendering exactly this picture; run the render tool again and do not edit the PNG afterwards`); continue; }
    const sz = pngSize(pngFile);
    if (!sz) { problems.push(`${path.basename(pngFile)} is not a readable PNG`); continue; }
    if (sz.width !== W || sz.height !== H) { problems.push(`${b}.stats.json says ${W}x${H} but the PNG is ${sz.width}x${sz.height}`); continue; }
    const counts = [s.road_px, s.marking_px || 0, s.ground_px || 0, s.debug_px || 0];
    if (counts.some((c) => !(c >= 0) || !Number.isFinite(c)) || s.road_px > px || (s.debug_px || 0) > px || (s.road_px + (s.debug_px || 0)) > px) { problems.push(`${b}.stats.json: pixel counts do not fit the ${W}x${H} frame (road ${s.road_px}, debug ${s.debug_px || 0}, frame ${px})`); continue; }
    const roadFrac = s.road_px / px, dbg = (s.debug_px || 0) / px;
    if (roadFrac < minRoad) problems.push(`view "${b}": road surface covers ${(roadFrac * 100).toFixed(2)}% of the frame (need at least ${(minRoad * 100).toFixed(1)}%): the subject is not in the picture. If this is deliberately a close-up of something else, declare it "diagnostic": true in the views json, keep it out of COMPARE.md and explain it in STATUS.md. If you cannot produce a view of the subject, write a BLOCKED line in STATUS.md instead of handing in a picture without it`);
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
const AP = "['\u2019]";
// Reasons that are never accepted (contract item 5 and 17): usage, time, budget, context. Written as reasons, so "a time limit of 240 s" or "the API rate limit" are not hit.
const USAGE_RE = new RegExp([
  String.raw`usage\s+(?:limit|cap|allowance|window|warning|quota)`, String.raw`session\s+limit`, String.raw`\btoken\s+(?:limit|cap)\b`,String.raw`(?:token|usage|time|context|session)\s+budget`, String.raw`\bbudget\s+(?:ran\s+out|is\s+gone|gone|spent|exhausted)`,
  String.raw`(?:short|low)\s+(?:of|on)\s+(?:tokens|time|context)`, String.raw`ran\s+(?:short|out)\s+of\s+(?:time|tokens|context)`, String.raw`running\s+(?:out\s+of|low\s+on)\s+(?:time|tokens|context)`, String.raw`out\s+of\s+(?:time|tokens|context)`,
  String.raw`(?:hit|reached|exceeded|ran\s+into)\s+(?:the|my|a)\s+(?:quota|rate\s+limit|time\s+limit|token\s+limit|usage)`,
  String.raw`(?:due\s+to|because\s+of|owing\s+to|given|under)\s+(?:the\s+)?(?:time|usage|token|context)\s+(?:constraints?|limits?|pressure)`,
  String.raw`(?:not\s+enough|insufficient|no|little)\s+time\s+(?:for|to|left)`, String.raw`no\s+time\s+left`, String.raw`time\s+pressure`, String.raw`(?:short|low)\s+on\s+time`, String.raw`limited\s+time`,
  String.raw`context\s+window\s+(?:is\s+|was\s+)?(?:full|nearly\s+full|exhausted|limit)`, String.raw`ran\s+out\s+of\s+context`,
].join('|'), 'i');
// "Not done" in any of its disguises. Contract item 17 names "out of scope", "time", "would need a bigger change" and "later round" as non-reasons.
const NOT_DONE_RE = new RegExp([
  String.raw`\bnot\s+(?:attempted|tried|started|implemented|finished|completed|fixed|addressed|handled|built|done(?!\s+by\s+design))\b`,
  String.raw`\b(?:did|have|had|was|were|is|are|has)\s*(?:not|n${AP}t)\s+(?:been\s+)?(?:attempt|try|tried|get\s+to|implement|finish|complet|fix|address|start|built|done)\w*`,
  String.raw`\b(?:didn|wasn|weren|hasn|haven|isn|aren|hadn)${AP}t\s+(?:been\s+)?(?:attempt|try|tried|get\s+to|implement|finish|complet|fix|address|start|built|done)\w*`,
  String.raw`\bnever\s+(?:attempted|tried|started)`, String.raw`\bstill\s+open\b`, String.raw`\bout\s+of\s+scope\b`, String.raw`\bbigger\s+change\b`, String.raw`\b(?:later|next)\s+round\b`,
  String.raw`\bleft\s+(?:undone|for\s+later|open)\b`, String.raw`\bfollow[- ]?ups?\b`, String.raw`\bfuture\s+work\b`, String.raw`\buntouched\b`, String.raw`\bTODO\b`, String.raw`\bpostponed\b`,
  String.raw`\b(?:was|were|is|are|been|being|get|got)\s+deferred\b`, String.raw`\bdeferred\s+(?:to|until|for)\b`, String.raw`\bwill\s+do\s+(?:this\s+)?(?:next\s+round|later)`,
].join('|'), 'i');

// "skipped" is an excuse unless the sentence negates it ("nothing skipped", "no tests were skipped", "not skipped")
const notDone = (l) => NOT_DONE_RE.test(l) || (/(?<![\d%]\s?)\bskipped\b/i.test(l) && !/\b(?:no|nothing|none|not|never|without)\b[^.\n]{0,40}\bskipped\b/i.test(l));

// BLOCKED form: `BLOCKED: <item> | tried: <what> | blocked by: <exact error or missing thing> | needs: <what unblocks it>`
const FILLER_RE = /^(?:nothing|none|n\/?a|no|tbd|todo|unknown|time|\?+|-+|\.+|see above|same)\W*$/i;
export function parseBlocked(text) {
  const out = [];
  String(text || '').replace(/<!--[\s\S]*?-->/g, '').split('\n').forEach((raw, i) => {
    const l = raw.trim().replace(/^[-*]\s+/, '');
    const m = l.match(/^BLOCKED\s*:\s*(.*)$/i);
    if (!m) return;
    const parts = m[1].split('|').map((x) => x.trim());
    const item = parts[0] || '';
    const field = (re) => { const p = parts.slice(1).find((x) => re.test(x)); return p ? p.replace(re, '').trim() : ''; };
    const tried = field(/^tried\s*:\s*/i), blockedBy = field(/^blocked\s+by\s*:\s*/i), needs = field(/^needs\s*:\s*/i);
    const why = [];
    if (item.length < 4) why.push('no item before the first |');
    if (tried.length < 12 || FILLER_RE.test(tried)) why.push('"tried:" is missing or too short (say what you attempted, 12+ characters)');
    if (blockedBy.length < 12 || FILLER_RE.test(blockedBy)) why.push('"blocked by:" is missing or too short (the exact error, refusal or missing thing, 12+ characters)');
    else if (USAGE_RE.test(blockedBy) || /^(?:time|scope|later|next round|usage)\b/i.test(blockedBy)) why.push('"blocked by:" names time, usage, scope or a later round, which is not a block');
    out.push({ line: i + 1, item, tried, blockedBy, needs, ok: why.length === 0, why, raw: l });
  });
  return out;
}

export function statusProblems(statusText, faultsText, opts = {}) {
  const problems = [];
  const stat = String(statusText || '').replace(/<!--[\s\S]*?-->/g, '');
  const blocked = parseBlocked(stat);
  const blockedLines = new Set(blocked.map((b) => b.line));
  for (const b of blocked) if (!b.ok) problems.push(`STATUS.md line ${b.line}: a BLOCKED line is incomplete (${b.why.join('; ')}): "BLOCKED: <item> | tried: <what you attempted> | blocked by: <exact error or missing thing> | needs: <what would unblock it>"`);
  const lines = stat.split('\n');
  lines.forEach((l, i) => {
    if (/^\s*#/.test(l) || blockedLines.has(i + 1)) return;
    if (notDone(l) && !/^\s*(?:Case|References opened|\|)/i.test(l)) problems.push(`STATUS.md line ${i + 1}: an item is left undone or deferred ("${l.trim().slice(0, 100)}"): do it (contract items 5 and 17), or, if something really blocks it, write "BLOCKED: <item> | tried: <what> | blocked by: <exact error or missing thing> | needs: <what unblocks it>"; "out of scope", "later round" and "bigger change" are not reasons`);
    if (USAGE_RE.test(l)) problems.push(`STATUS.md line ${i + 1}: a usage/time limit is given as a reason ("${l.trim().slice(0, 100)}"): that is not an accepted reason; finish the item or write a BLOCKED line with the technical block`);
  });
  // partial reads do not count as opened (contract item 1)
  const ro = stat.match(/References opened:\s*(\d+)\s+of\s+(\d+)/i);
  if (ro && +ro[1] >= +ro[2] && /\b(?:partly|partially|read\s+(?:only\s+)?(?:the\s+)?(?:head|first|top)|skimmed|heads\s+and\s+greps|first\s+\d+\s+(?:lines|characters))\b/i.test(stat.split(/^##\s+Reference files opened/im)[0])) problems.push('STATUS.md: "References opened" says all files were opened but the text says some were only partly read; a partial read counts as not opened (contract item 1): open them in full, or write N < M with a "not opened: <file> (reason)" line');
  const f = String(faultsText || '').replace(/<!--[\s\S]*?-->/g, '');
  f.split('\n').forEach((l, i) => { if (USAGE_RE.test(l)) problems.push(`FAULTS.md line ${i + 1}: a usage/time limit is given as a reason ("${l.trim().slice(0, 100)}")`); });
  if (faultsText !== null && faultsText !== undefined) {
    // a "## Pass N" heading with no real body is not a pass (a builder in a hurry writes the headings only, or filler)
    const passBodies = [...f.matchAll(/^##\s+Pass\s+\d+[^\n]*\n([\s\S]*?)(?=^##\s|(?![\s\S]))/gim)].map((m) => m[1].trim());
    const real = (b) => b.length >= 40 && b.split(/\s+/).length >= 8 && !/^(?:n\/?a|none|tbd|todo|placeholder|see above|no faults?\b)/i.test(b);
    const passes = passBodies.filter(real).length;
    if (passBodies.length > passes) problems.push('FAULTS.md: ' + (passBodies.length - passes) + ' "## Pass N" section(s) are empty, filler or under 40 characters / 8 words: write the faults found in that pass (if you cannot run the pass, write a BLOCKED line in STATUS.md)');
    if (passes < 3 && !blocked.some((b) => b.ok && /self-?audit|pass(?:es)?\b/i.test(b.item))) {
      const ro2 = f.match(/^[ \t]*Residual only unfixable:[ \t]*(.{40,})$/im);
      if (!ro2) problems.push(`FAULTS.md: ${passes} self-audit pass(es), need 3 unless the residual list holds only unfixable items: then add a line "Residual only unfixable: <for each residual item (by its number), why it cannot be fixed>" (40+ characters)`);
      else if (!/(?:\b(?:item|residual|fault|pass)\s*#?\s*\d+|#\d+|\bR\d+\b)/i.test(ro2[1])) problems.push('FAULTS.md: "Residual only unfixable:" must name the residual items it covers by number ("item 2", "R3", "#4") and say why each cannot be fixed');
    }
  }
  return problems;
}

// ---------- prior audit items (contract item 17) ----------
// priorAuditText: the previous AUDIT.md ("For the next builder" numbered list). The STATUS must carry a "Prior audit items" table with a row per item;
// each row is "fixed" with proof or "open" with reason (a) or (b) and evidence.
export function priorAuditProblems(statusText, priorAuditText) {
  const problems = [];
  const prior = String(priorAuditText || '').replace(/\r\n/g, '\n');
  const sec = prior.match(/^##\s+For the next builder[^\n]*\n([\s\S]*?)(?=^##\s|(?![\s\S]))/im);
  const n = sec ? (sec[1].match(/^\s*\d+\.\s+\S/gm) || []).length : 0;
  const st = String(statusText || '').replace(/\r\n/g, '\n').replace(/<!--[\s\S]*?-->/g, '');
  const tb = st.match(/^##\s+Prior audit items[^\n]*\n([\s\S]*?)(?=^##\s|(?![\s\S]))/im);
  if (!tb) return ['STATUS.md has no "## Prior audit items" section (contract item 17): a table with one row per item of the previous AUDIT.md "For the next builder" list (' + n + ' item(s))'];
  const rows = tb[1].split('\n').filter((l) => l.trim().startsWith('|') && !/^\s*\|[\s:|-]+\|\s*$/.test(l)).slice(1);
  if (rows.length < n) problems.push(`"Prior audit items" has ${rows.length} row(s) but the previous AUDIT.md lists ${n} item(s) for the next builder: one row per item`);
  rows.forEach((r, i) => {
    const cells = r.split('|').slice(1, -1).map((c) => c.trim());
    if (cells.length < 3) { problems.push(`"Prior audit items" row ${i + 1}: needs 3 cells (item | fixed or open | proof or reason)`); return; }
    const status = cells[1], proof = cells.slice(2).join(' ');
    if (/^fixed\b/i.test(status)) { if (proof.length < 15 || /^(?:done|ok|fixed|yes)\W*$/i.test(proof)) problems.push(`"Prior audit items" row ${i + 1}: "fixed" needs the test, run or image that proves it`); }
    else if (/^open\b/i.test(status)) { if (!/\((?:a|b)\)|\breason\s*[ab]\b|\b(?:a|b)\)/i.test(status + ' ' + proof) || proof.length < 40) problems.push(`"Prior audit items" row ${i + 1}: "open" needs reason (a) too pedantic to notice or (b) the audit's reading is doubtful, with your own evidence (40+ characters)`); }
    else problems.push(`"Prior audit items" row ${i + 1}: the status cell must start with "fixed" or "open"`);
  });
  return problems;
}

// ---------- COMPARE.md placeholder rows ----------
export const PLACEHOLDER_RE = /\bevidence\b|\bn\/?a\b|placeholder|no match|not applicable|\btbd\b|\btodo\b|engine[- ]only|no claim|not claimed|^none\b/i;
export const BLOCKED_HINT = 'do not fill tables to get through the gate: if you cannot produce the render, reference or composite, finish with a BLOCKED line in STATUS.md ("BLOCKED: <item> | tried: <what> | blocked by: <exact error or missing thing> | needs: <what unblocks it>")';

// ---------- E: method-change rule (make_brief) ----------
// Direction is stated, never guessed from the name: known gate metrics have a table; any other metric needs "(higher)" or "(lower)" in the Rounds line.
// rounds: [{ round, values: { metric: number }, dirs: { metric: 'higher'|'lower' } }] oldest first. Returns a refusal string or null.
export function methodChangeProblem(rounds) {
  if (rounds.length < 3) return null;
  const [a, b, c] = rounds.slice(-3);
  const metrics = Object.keys(c.values).filter((m) => m in b.values && m in a.values);
  if (!metrics.length) return null;
  const dir = (m) => (c.dirs && c.dirs[m]) || (b.dirs && b.dirs[m]) || METRIC_DIRECTION[m] || 'higher';
  const improved = (x, y, m) => (dir(m) === 'lower' ? y < x : y > x);
  const anyImproved = metrics.some((m) => improved(a.values[m], b.values[m], m) || improved(b.values[m], c.values[m], m));
  if (anyImproved) return null;
  return `the last two rounds (${b.round}, ${c.round}) did not improve any gate number (${metrics.map((m) => `${m}: ${a.values[m]} -> ${b.values[m]} -> ${c.values[m]}`).join('; ')}). Another round on the same method is not commissioned: commission a method review first. To override, rerun with --override "<reason, 20+ characters>".`;
}
// Round lines: `round8: scene_check=20, bare_steps=3 (lower)`. Metrics outside METRIC_DIRECTION need (higher) or (lower); a missing direction is listed in `undirected`.
export function parseRounds(sectionText) {
  const out = [];
  for (const raw of String(sectionText || '').split('\n')) {
    const l = raw.trim().replace(/^[-*]\s+/, '');
    const m = l.match(/^([\w.-]+)\s*:\s*(.+)$/);
    if (!m) continue;
    const values = {}, dirs = {}, undirected = [];
    for (const kv of m[2].split(/[,;]/)) {
      const x = kv.trim().match(/^([a-z_ ]+?)\s*[=:]?\s*(\d+(?:\.\d+)?)(?:\/\d+)?\s*(?:\((higher|lower)\))?$/i);
      if (x) {
        const key = x[1].trim().toLowerCase().replace(/\s+/g, '_');
        values[key] = Number(x[2]);
        if (x[3]) dirs[key] = x[3].toLowerCase(); else if (!METRIC_DIRECTION[key]) undirected.push(key);
      }
    }
    out.push({ round: m[1], values, dirs, undirected, raw: l });
  }
  return out;
}
