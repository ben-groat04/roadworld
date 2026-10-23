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
import zlib from 'node:zlib';
import { spawnSync } from 'node:child_process';

export const METRICS = ['scene_check', 'ctest failed_assertions', 'ctest failed_tests', 'ctest assertions'];
export const FAILURE_METRICS = ['ctest failed_assertions', 'ctest failed_tests'];
export const METRIC_DIRECTION = { scene_check: 'higher', ctest_failed_assertions: 'lower', ctest_failed_tests: 'lower' };
const sh = (cmd, args, cwd, timeout = 60000) => spawnSync(cmd, args, { cwd, encoding: 'utf8', timeout, maxBuffer: 64 << 20 });

// ---------- grammar ----------
export function parseGateLine(raw) {
  const line = String(raw).trim().replace(/^[-*]\s+/, '');
  if (!line) return null;
  let m = line.match(/^allow_waiver[:\s]+(\S.*?)\s*$/i);
  if (m) return { kind: 'allow', key: m[1].trim(), raw: line };
  m = line.match(/^allow_floor_miss[:\s]+(scene_check|ctest failed_assertions|ctest failed_tests)\s*$/i);
  if (m) return { kind: 'floormiss', metric: m[1].toLowerCase().replace(/\s+/g, ' '), raw: line };   // only the main session writes this into a task file: a BLOCKED line never waives a measured floor
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
// A measured floor miss is NEVER waived by a BLOCKED line in STATUS.md (an agent writes those); only the task file can, with a line `allow_floor_miss <metric>`, written by the main session.
// `ctest assertions >= N` (N = the base's whole-suite assertion count printed by doctest in LastTest.log) is the backstop against a test made vacuous: assertions that no longer run show as a drop.
export function runMeasuredGates(gates, wt, log = () => {}) {
  const out = { problems: [], notes: [], shortfalls: [], errors: [], blocked: [] };
  const metrics = gates.filter((g) => g.kind === 'metric');
  let scene = null, ctest = null;
  const coveredByBlocked = (g) => gates.some((x) => x.kind === 'floormiss' && x.metric === g.metric);
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
      value = g.metric === 'ctest failed_assertions' ? ctest.failed_assertions : g.metric === 'ctest assertions' ? ctest.assertions : ctest.failed_tests;
      out.notes.push(`measured ${g.metric} ${value} (${ctest.assertions} assertions, ${ctest.tests}/${ctest.registered} tests, ${ctest.cases} cases, from ${path.basename(path.dirname(path.dirname(path.dirname(ctest.file))))}${ctest.crashed.length ? '; CRASHED: ' + ctest.crashed.join(', ') : ''})`);
    }
    if (!satisfies(value, op, g.floor)) {
      const msg = `gate ${g.raw}: measured ${value}, which is on the wrong side of ${op} ${g.floor} (the floor)${extra}. The number you typed in STATUS is not read; fix the cause. A BLOCKED line does not waive a measured floor; only the main session can, with an allow_floor_miss line in the task file`;
      if (coveredByBlocked(g)) out.blocked.push(`${g.metric} measured ${value}, floor ${op} ${g.floor}${extra} (allowed by allow_floor_miss in the task file)`); else out.problems.push(msg);
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
// WHAT IT CANNOT SEE: a check moved into a helper that is never called; a test made vacuous without touching an assertion line (an empty fixture, a loop over zero cases, input changed so the
// check passes trivially); a reworded expectation spread over several lines; a waiver in a test location outside WAIVER_PATHS; anything done with
// the build dir or CTest flags instead of the sources. diffText: unified diff (-U0); contents: { file: { old, new } } enables the effective-count check.
const ASSERT_RE = /\b(?:GRADE_CHECK|(?:DOCTEST_)?(?:CHECK|REQUIRE)(?:_MESSAGE|_EQ|_NE|_FALSE|_THROWS\w*|_NOTHROW)?|Test(?:True|False|Equal|NotEqual|Null|NotNull|Valid|Invalid)|UTEST_\w+|assert(?:\.\w+)?)\s*\(/;
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
  const re = /(===|!==|<=|>=|==|!=|(?<![<\-])<(?![<=])|(?<![\->])>(?![>=]))/g;
  let m;
  while ((m = re.exec(line))) {
    const lhs = line.slice(Math.max(0, m.index - 30), m.index).replace(/\s+/g, ' ').trim();
    const rest = line.slice(m.index + m[1].length);
    let depth = 0, end = rest.length;
    for (let i = 0; i < rest.length; i++) { const c = rest[i]; if (c === '(') depth++; else if (c === ')') { if (depth === 0) { end = i; break; } depth--; } else if ((c === ';' || c === ',' || c === '&' || c === '|' || c === '{' || c === '}' || c === '?' || c === ':') && depth === 0) { end = i; break; } }
    const rhs = rest.slice(0, end).trim();
    let val = null;
    if (/^[-+*/().\d\seE]+$/.test(rhs) && /\d/.test(rhs)) { try { val = Function('"use strict"; return (' + rhs + ')')(); if (!Number.isFinite(val)) val = null; } catch { val = null; } }
    out.push({ lhs, op: m[1] === '===' ? '==' : m[1] === '!==' ? '!=' : m[1], val });
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
    // The gates' OWN tests (tools/test_gates.mjs, tools/hooks/test_hooks.mjs) are test DATA for the guard (they contain every waiver word as a string), so the line
    // analysis below is meaningless there. What is watched is the list of test names: a removed or renamed test is a finding (a weakened gate is a waiver).
    if (/(^|\/)test_(gates|hooks)\.mjs$/.test(f)) {
      const names = (L) => new Set(L.flatMap((l) => [...l.matchAll(/(?<![\w.])t\(\s*(['"`])((?:(?!\1).)+)\1/g)].map((m) => m[2])));
      const addedN = names(A);
      for (const n of names(R)) if (!addedN.has(n)) add(`deleted_gate_test:${base}:${n.slice(0, 60)}`, f, `gate test "${n.slice(0, 90)}" was removed or renamed (a weakened gate test is a waiver: say why in an allow_waiver line)`);
      continue;
    }
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
    // ---- gates4 (gates3 audit faults 6 and 7): more evasions of the assertion, its expected value and the test run
    const isTestData = /(^|\/)tests\/.+\.(?:json|txt|golden|csv|dat|expected|ref|bin|tsv|yml|yaml|ini)$/i.test(f) || /(^|\/)(?:golden|expected|fixtures?)\//i.test(f);
    if (isTestData && !cmake && (A.length || R.length)) add('test_data:' + f, f, `a test data / golden file was edited (${A.length} line(s) added, ${R.length} removed): expected values live there; a changed expectation needs a photo measurement or a written rule`);
    if (cmake) {
      for (const l of A) if (/\badd_test\b|\bset_tests_properties\b|\bCTEST_\w+/.test(l) && /(?:^|\s)(?:-tc=|-tce=|-ts=|-sc=|-sfe=|--test-case|--dt-\w+|--no-run|-E\s|-R\s|--exclude|--tests-regex|--filter)|\bWILL_FAIL\b|\bDISABLED\b/.test(l) && !R.includes(l)) add('ctest_args:' + base + ':' + l.trim().slice(0, 40), f, `a test registration gained a filter, exclusion or disable argument: ${l.trim().slice(0, 100)} (it can run fewer tests than before)`);
    }
    if (code) {
      const orCount = (L) => L.reduce((n, l) => n + (l.match(/\|\|/g) || []).length, 0), andCount = (L) => L.reduce((n, l) => n + (l.match(/&&/g) || []).length, 0);
      const argKey = (l) => (l.match(/\(\s*([A-Za-z_][\w.>-]*)/) || [])[1];
      const assertish = (l) => ASSERT_RE.test(l) || /\bpass\s*:/.test(l);
      for (const hk of hunks) {
        const remA = hk.rem.filter(assertish), addA = hk.add.filter(assertish);
        if (remA.length) {
          if (orCount(addA) > orCount(remA)) add('or_widened:' + base + ':' + skeleton(addA[0]).slice(0, 40), f, `an "||" clause was added to an assertion (it can only pass more often): "${remA[0].trim().slice(0, 60)}" -> "${addA[0].trim().slice(0, 70)}"`);
          if (andCount(addA) < andCount(remA)) add('and_dropped:' + base + ':' + skeleton(addA[0] || remA[0]).slice(0, 40), f, `an "&&" condition was dropped from an assertion: "${remA[0].trim().slice(0, 60)}"`);
          const fl = (L) => L.filter((l) => /_FALSE\s*\(/.test(l)).length;
          if (fl(addA) > fl(remA) && remA.some((l) => !/_FALSE\s*\(/.test(l))) add('check_false_swap:' + base + ':' + skeleton(addA[0]).slice(0, 40), f, `a CHECK was swapped for a CHECK_FALSE: "${remA[0].trim().slice(0, 60)}" -> "${addA.find((l) => /_FALSE\s*\(/.test(l)).trim().slice(0, 70)}" (a new negative claim can pass where the old positive one failed)`);
          for (const nl of addA) {
            // the same assertion now checks a DIFFERENT value (CHECK(a.width > 3.0) -> CHECK(b.width > 1.0)): old and new comparisons share no left side
            const nc = ASSERT_RE.test(nl) && !R.includes(nl) ? comparisons(nl) : [];
            if (nc.length) {
              const cand = remA.filter((o) => ASSERT_RE.test(o) && comparisons(o).length === nc.length);
              if (cand.length && !cand.some((o) => comparisons(o).every((c, i) => c.lhs === nc[i].lhs))) add('assertion_retargeted:' + base + ':' + skeleton(nl).slice(0, 40), f, `an assertion now checks a different value: "${cand[0].trim().slice(0, 60)}" -> "${nl.trim().slice(0, 60)}"`);
            }
            const ol = remA.find((o) => argKey(o) && argKey(o) === argKey(nl) && comparisons(o).length > comparisons(nl).length && comparisons(nl).length === 0);
            if (ol && !R.includes(nl)) add('comparison_removed:' + base + ':' + skeleton(nl).slice(0, 40), f, `an assertion lost its comparison: "${ol.trim().slice(0, 60)}" -> "${nl.trim().slice(0, 60)}" (it now only checks that the value is truthy)`);
          }
        }
        for (const nl of hk.add) {
          if (!assertish(nl) || R.includes(nl)) continue;
          if (/([A-Za-z_][\w.[\]()]*)\s*(?:===|==|>=|<=)\s*\1(?![\w.(])/.test(nl) || /\bTest\w+\s*\([^,]*,\s*([\w.]+)\s*,\s*\1\s*\)/.test(nl)) add('self_compare:' + base + ':' + skeleton(nl).slice(0, 40), f, `an assertion compares a value with itself: ${nl.trim().slice(0, 90)}`);
          if (/\bTest(?:True|False)\s*\([^,]*,\s*(?:true|1|!false)\s*\)/.test(nl) || /\bTest(?:Equal|NotEqual)\s*\([^,]*,\s*(\w+)\s*,\s*\1\s*\)/.test(nl)) add('unreal_trivial:' + base + ':' + skeleton(nl).slice(0, 40), f, `an Unreal automation assertion that cannot fail: ${nl.trim().slice(0, 90)}`);
        }
        // an input or fixture made empty on an edited line (make_roads(5) -> make_roads(0)), without touching an assertion
        for (const nl of hk.add) {
          if (assertish(nl)) continue;
          const ol = hk.rem.find((o) => o !== nl && skeleton(o) === skeleton(nl) && numbers(o).some((v, i) => v > 0 && numbers(nl)[i] === 0));
          if (ol && /\w\s*\(/.test(nl)) add('zeroed_input:' + base + ':' + skeleton(nl).slice(0, 40), f, `an input was set to 0 on an edited line: "${ol.trim().slice(0, 60)}" -> "${nl.trim().slice(0, 60)}" (a test over an empty input passes without checking anything)`);
        }
      }
      if (A.filter((l) => /\btry\s*\{|\bcatch\s*\(/.test(l)).length > R.filter((l) => /\btry\s*\{|\bcatch\s*\(/.test(l)).length) add('try_catch_added:' + base, f, 'a try/catch was added to a test file: an assertion inside it can be swallowed (check by hand that no assertion moved under it)');
      for (const l of A) {
        const m = l.match(/(?:auto|const\s+auto)\s+(\w+)\s*=\s*\[[^\]]*\]\s*\(/);
        if (!m || R.includes(l)) continue;
        const nc = contents && contents[f] ? contents[f].new : null;
        if (nc === null || !new RegExp('\\b' + m[1] + '\\s*\\(').test(nc)) add('uncalled_lambda:' + base + ':' + m[1], f, `a lambda "${m[1]}" was added ${nc === null ? '(its calls could not be checked)' : 'and is never called'}: assertions inside it never run`);
      }
      for (const l of A) if (!R.includes(l) && (/for\s*\([^)]*:\s*(?:std::vector<[^>]*>\s*)?\{\s*\}\s*\)/.test(l) || /for\s*\(\s*(?:const|let)\s+\w+\s+of\s+\[\s*\]\s*\)/.test(l))) add('vacuous_loop:' + base + ':' + skeleton(l).slice(0, 40), f, `a loop over nothing: ${l.trim().slice(0, 90)} (its body never runs)`);
      const EXPECT_RE = /\b((?:k\w*)?(?:[Ee]xpect(?:ed)?\w*|[Gg]olden\w*)|\w*_expect(?:ed)?\w*)\s*(?:=|\{)\s*(-?[0-9]*\.?[0-9]+(?:[eE][-+]?\d+)?)/;
      const expOf = (L) => { const m = new Map(); for (const l of L) { const t = l.match(EXPECT_RE); if (t) m.set(t[1], Number(t[2])); } return m; };
      const oldE = expOf(R), newE = expOf(A);
      for (const [n, v] of newE) if (oldE.has(n) && oldE.get(n) !== v) add(`expected_const:${n}`, f, `expected-value constant ${n} changed ${oldE.get(n)} -> ${v} (a changed expectation needs a photo measurement or a written rule, and an allow_waiver line)`);
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
      const pt = (L) => L.filter((l) => /pass:\s*(?:true|1|!0|!!1)\b/.test(l));
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

/// ---------- C: picture sanity ----------
export function headlineViews(compareText) {
  if (!compareText) return [];
  const rows = compareText.split('\n').filter((l) => l.trim().startsWith('|') && !/^\s*\|[\s:|-]+\|\s*$/.test(l)).slice(1);
  return rows.map((r) => (r.split('|')[2] || '').trim().replace(/`/g, '')).filter(Boolean);
}
export const sha1File = (p) => { try { return crypto.createHash('sha1').update(fs.readFileSync(p)).digest('hex'); } catch { return null; } };
function pngSize(file) {
  try { const b = fs.readFileSync(file); if (b.length < 24 || b.toString('latin1', 1, 4) !== 'PNG') return null; return { width: b.readUInt32BE(16), height: b.readUInt32BE(20) }; } catch { return null; }
}
// Decodes an 8-bit, non-interlaced RGB/RGBA PNG into a W*H*3 RGB buffer. Anything else (palette, 16-bit, interlaced, truncated, header-only) returns { decoded: false }
// and the checks that need pixels are skipped, never failed: this is a measurement of the pictures our own render tools write (Chromium PNGs are 8-bit RGBA).
export function decodePng(file) {
  try {
    const b = fs.readFileSync(file);
    if (b.length < 33 || b.toString('latin1', 1, 4) !== 'PNG') return { decoded: false };
    const W = b.readUInt32BE(16), H = b.readUInt32BE(20), depth = b[24], ctype = b[25], interlace = b[28];
    if (depth !== 8 || (ctype !== 2 && ctype !== 6) || interlace !== 0 || W * H > 40e6) return { decoded: false };
    const bpp = ctype === 6 ? 4 : 3, idat = [];
    for (let o = 8; o + 12 <= b.length;) { const len = b.readUInt32BE(o), type = b.toString('latin1', o + 4, o + 8); if (type === 'IDAT') idat.push(b.subarray(o + 8, o + 8 + len)); if (type === 'IEND') break; o += 12 + len; }
    if (!idat.length) return { decoded: false };
    const raw = zlib.inflateSync(Buffer.concat(idat)), stride = W * bpp;
    if (raw.length < (stride + 1) * H) return { decoded: false };
    const prev = Buffer.alloc(stride), cur = Buffer.alloc(stride), rgb = Buffer.alloc(W * H * 3);
    for (let y = 0; y < H; y++) {
      const ft = raw[y * (stride + 1)], line = raw.subarray(y * (stride + 1) + 1, (y + 1) * (stride + 1));
      for (let i = 0; i < stride; i++) {
        const a = i >= bpp ? cur[i - bpp] : 0, up = prev[i], c = i >= bpp ? prev[i - bpp] : 0;
        let v = line[i];
        if (ft === 1) v += a; else if (ft === 2) v += up; else if (ft === 3) v += (a + up) >> 1;
        else if (ft === 4) { const p = a + up - c, pa = Math.abs(p - a), pb = Math.abs(p - up), pc = Math.abs(p - c); v += pa <= pb && pa <= pc ? a : pb <= pc ? up : c; }
        else if (ft !== 0) return { decoded: false };
        cur[i] = v & 255;
      }
      for (let x = 0; x < W; x++) { const o = (y * W + x) * 3; rgb[o] = cur[x * bpp]; rgb[o + 1] = cur[x * bpp + 1]; rgb[o + 2] = cur[x * bpp + 2]; }
      cur.copy(prev);
    }
    return { decoded: true, width: W, height: H, rgb };
  } catch { return { decoded: false }; }
}
// distinct colours (5 bits per channel) on a sparse sample of a decoded picture
export function colourCountOf(img) {
  if (!img || !img.decoded) return { decoded: false };
  const seen = new Set();
  for (let y = 0; y < img.height; y += 7) for (let x = 0; x < img.width; x += 3) { const o = (y * img.width + x) * 3; seen.add((img.rgb[o] >> 3) << 10 | (img.rgb[o + 1] >> 3) << 5 | (img.rgb[o + 2] >> 3)); }
  return { decoded: true, colours: seen.size, width: img.width, height: img.height };
}
export function pngColourCount(file) { return colourCountOf(decodePng(file)); }

// The flat-colour class palettes of the two render tools. The gate trusts the TOOL NAME in the record only for choosing the palette: it then counts the pixels in the
// mask picture itself, which must be a flat-colour picture of exactly that palette, the same size as the render, and consistent with the render (see pictureProblems).
export const RENDER_TOOLS = {
  'prototypes/mesh-view/render.mjs': { classes: { road: [255, 0, 0], marking: [0, 255, 0], needs: [0, 0, 255], rock: [255, 255, 0], ground: [255, 0, 255], other: [128, 128, 128] }, debug: ['needs', 'rock'] },
  'prototypes/road-draw/tools/scene.mjs': { classes: { road: [255, 0, 0], structure: [0, 255, 0], other: [255, 0, 255] }, debug: [] },
};
// Counts the pixels of every class in a mask picture (tolerance 24 per channel, like the tools). Black is empty/sky. `stray` = share of pixels that are neither black nor a class colour.
export function measureMask(maskImg, palette, tol = 24) {
  const { width: W, height: H, rgb } = maskImg, counts = {}, names = Object.keys(palette);
  let black = 0, stray = 0;
  const road = [];
  for (let i = 0, n = W * H; i < n; i++) {
    const r = rgb[i * 3], g = rgb[i * 3 + 1], b = rgb[i * 3 + 2];
    if (r < tol && g < tol && b < tol) { black++; continue; }
    let hit = null;
    for (const nm of names) { const k = palette[nm]; if (Math.abs(r - k[0]) < tol && Math.abs(g - k[1]) < tol && Math.abs(b - k[2]) < tol) { hit = nm; break; } }
    if (hit) { counts[hit] = (counts[hit] || 0) + 1; if (hit === 'road') road.push(i); } else stray++;
  }
  return { counts, black, stray: stray / (W * H), roadIdx: road };
}
const isGrass = (r, g, b) => g > 70 && g > r * 1.12 && g > b * 1.15;
const isOrange = (r, g, b) => { const mx = Math.max(r, g, b), mn = Math.min(r, g, b); if (mx < 140 || (mx - mn) / mx < 0.5) return false; const d = mx - mn; let h; if (mx === r) h = (((g - b) / d) % 6 + 6) % 6; else if (mx === g) h = (b - r) / d + 2; else h = (r - g) / d + 4; h *= 60; return h >= 15 && h <= 32; };

function walkFiles(dir, depth = 0, out = []) {
  if (depth > 4) return out;
  let ents; try { ents = fs.readdirSync(dir, { withFileTypes: true }); } catch { return out; }
  for (const e of ents) { const p = path.join(dir, e.name); if (e.isDirectory()) { if (!/^(node_modules|\.git)$/.test(e.name)) walkFiles(p, depth + 1, out); } else out.push(p); }
  return out;
}
const reEsc = (s) => s.replace(/[.*+?^${}()|[\]\\]/g, '\\$&');
// THRESHOLDS (measured, round 9 + the roadtool and gates3 renders; the table is in docs/progress/gates4/STATUS.md): the road share of the frame is counted in the MASK picture.
// Round 9 views with no subject: s6 street 0.00 %, s6 side_close 0.00 %, s6 along 0.35 %, s6 toe_profile 0.70 % (a profile close-up). Views of the subject: 1.74 % (s6 end),
// 2.01 % (s6 aerial), 2.71 %, 3.31 %, 4.19 % (s4 aerial), 23.9 % (cross_slope). Debug colour (needs structure, rock cut): 50.53 % s4 aerial, 42.61 % s4 end, 23.5-27.0 % on the
// three street/along views, 0 on every other view. 1 % road and 0.1 % debug refuse every view of the no-subject / debug set and pass every subject view. They are the
// DEFAULTS: a task file overrides them per round with `picture_min_road <f>` / `picture_max_debug <f>` (a close-up round sets a lower floor and says why).
//
// Where the records are: next to each render, at any depth below the hand-in folder (renders in after/ and before/ are found there). The views judged: every COMPARE.md render
// (resolved to its file) and every *_aerial.png / *_street.png below the folder, except pictures under a folder called before/ (history). Each judged view needs
// <name>.stats.json AND <name>.mask.png beside <name>.png, both hash-bound; the road and debug pixels are COUNTED BY THIS FUNCTION in the mask, the typed numbers must agree
// with them, and the mask must be a flat-colour picture of the tool's palette, the render's size, and consistent with the render (road pixels are not grass, no orange the mask does
// not account for). A `diagnostic` view may not be named *_aerial/*_street, may not be a COMPARE.md render, and must be explained in STATUS.md (a line with the view name,
// the word diagnostic and a reason of 20+ characters); at most maxDiagShare of the records may be diagnostic.
// WHAT IT CANNOT SEE: a render AND a matching mask that were both drawn by hand to fit (it reads pictures, not how they were made); a real render of the wrong subject.
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
    if (!records.has(rec)) problems.push(`view "${rel(p)}" (${why}) has no ${path.basename(rec)} next to the render: render it with prototypes/mesh-view/render.mjs or prototypes/road-draw/tools/scene.mjs, which write the record and its mask picture`);
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
    const renderSha = sha1File(pngFile);
    if (renderSha !== s.png_sha1) { problems.push(`${b}.stats.json was not made for ${path.basename(pngFile)} (picture hash differs): the stats must come from rendering exactly this picture; run the render tool again and do not edit the PNG afterwards`); continue; }
    const sz = pngSize(pngFile);
    if (!sz) { problems.push(`${path.basename(pngFile)} is not a readable PNG`); continue; }
    if (sz.width !== W || sz.height !== H) { problems.push(`${b}.stats.json says ${W}x${H} but the PNG is ${sz.width}x${sz.height}`); continue; }
    if (W < 320 || H < 180) { problems.push(`${path.basename(pngFile)} is only ${W}x${H}: a render the cold viewer opens is at least 320x180`); continue; }
    // ---- the mask picture: where the road and debug pixels are counted
    const tool = RENDER_TOOLS[s.written_by];
    if (!tool) { problems.push(`${b}.stats.json: written_by "${s.written_by}" is not a known render tool (${Object.keys(RENDER_TOOLS).join(', ')}): the road pixels could not be measured`); continue; }
    if (typeof s.mask_png !== 'string' || path.basename(s.mask_png) !== s.mask_png || typeof s.mask_sha1 !== 'string') { problems.push(`${b}.stats.json names no mask picture (mask_png, mask_sha1): the gate counts road pixels itself, from <view>.mask.png; render the view again with the current render tool`); continue; }
    const maskFile = path.join(path.dirname(file), s.mask_png);
    if (!fs.existsSync(maskFile)) { problems.push(`${b}: the mask picture ${s.mask_png} is missing next to the render`); continue; }
    const maskSha = sha1File(maskFile);
    if (maskSha !== s.mask_sha1) { problems.push(`${b}: ${s.mask_png} does not match the record (hash differs): do not edit pictures after the render tool wrote them`); continue; }
    if (maskSha === renderSha) { problems.push(`${b}: the mask picture is the render itself (identical bytes): it is not a class pass`); continue; }
    const mask = decodePng(maskFile), render = decodePng(pngFile);
    if (!mask.decoded) { problems.push(`${b}: ${s.mask_png} is not a plain 8-bit PNG the gate can read`); continue; }
    if (mask.width !== W || mask.height !== H) { problems.push(`${b}: the mask is ${mask.width}x${mask.height}, the render ${W}x${H}`); continue; }
    const mm = measureMask(mask, tool.classes);
    if (mm.stray > 0.05) { problems.push(`${b}: ${(mm.stray * 100).toFixed(1)}% of the mask is neither black nor a class colour of ${s.written_by}: it is not that tool's flat-colour class pass`); continue; }
    const road = mm.counts.road || 0, debug = tool.debug.reduce((a, c) => a + (mm.counts[c] || 0), 0);
    const typedDebug = s.debug_px || 0;
    if (Math.abs(road - s.road_px) > 0.01 * px || Math.abs(debug - typedDebug) > 0.01 * px) { problems.push(`${b}.stats.json says road ${s.road_px} px and debug ${typedDebug} px; the mask picture, counted here, has road ${road} px and debug ${debug} px: the typed numbers are not what decides, and they disagree with the picture`); continue; }
    // ---- the render must agree with the mask (a fake mask beside a real render, or the other way round)
    if (render.decoded) {
      const pc = colourCountOf(render);
      if (pc.colours < 16) { problems.push(`${path.basename(pngFile)} has ${pc.colours} distinct colours: a flat or blank picture is not a render of the subject`); continue; }
      let onGrass = 0; for (const i of mm.roadIdx) if (isGrass(render.rgb[i * 3], render.rgb[i * 3 + 1], render.rgb[i * 3 + 2])) onGrass++;
      if (road >= 500 && onGrass / road > 0.35) { problems.push(`${b}: ${(100 * onGrass / road).toFixed(0)}% of the mask's road pixels are grass-coloured in the render: the mask does not belong to this render`); continue; }
      let orange = 0; for (let i = 0, n = W * H; i < n; i++) if (isOrange(render.rgb[i * 3], render.rgb[i * 3 + 1], render.rgb[i * 3 + 2])) orange++;
      if (orange / px > maxDebug + debug / px + 0.002) { problems.push(`${b}: ${(100 * orange / px).toFixed(2)}% of the render is debug-orange ("needs structure" colour) but the mask accounts for only ${(100 * debug / px).toFixed(2)}% debug pixels: placeholder surfaces are on screen`); continue; }
    }
    const roadFrac = road / px, dbg = debug / px;
    if (roadFrac < minRoad) problems.push(`view "${b}": road surface covers ${(roadFrac * 100).toFixed(2)}% of the frame, counted in ${s.mask_png} (need at least ${(minRoad * 100).toFixed(1)}%): the subject is not in the picture. If this is deliberately a close-up of something else, declare it "diagnostic": true in the views json, keep it out of COMPARE.md and explain it in STATUS.md. If you cannot produce a view of the subject, write a BLOCKED line in STATUS.md instead of handing in a picture without it`);
    if (dbg > maxDebug) problems.push(`view "${b}": ${(dbg * 100).toFixed(2)}% of the frame is in debug colours, counted in ${s.mask_png} (${tool.debug.map((k) => k + ' ' + (mm.counts[k] || 0) + ' px').join(', ')}; allowed ${(maxDebug * 100).toFixed(2)}%): a placeholder "needs structure" or rock-cut surface is on screen where a structure or ground should be`);
    notes.push(`${b}: road ${(roadFrac * 100).toFixed(1)}%, debug ${(dbg * 100).toFixed(2)}% (counted in the mask)`);
  }
  if (records.size >= 3) {
    const diag = [...records].filter(([, s]) => s.diagnostic);
    if (diag.length / records.size > maxDiagShare) problems.push(`${diag.length} of ${records.size} views are declared diagnostic (more than ${maxDiagShare * 100}%): a diagnostic flag cannot be the way past the picture check`);
  }
  return { problems, notes };
}

// Rows of COMPARE.md as files: [{ kase, render, reference, composite, before }] where each is an absolute path or null. Every role is a DIFFERENT file by content:
// a render copied over as the reference, a composite that is the render, a "before" identical to the render (nothing changed), the same render for two cases: refused.
// 0.05 % of a 1400x800 frame is 560 pixels: more than any stray anti-aliasing change, fewer than a moved marking line or a changed kerb. No photo measurement: a floor against the one-pixel edit.
export const MIN_MOVED = 0.0005;
export function compareIntegrity(rows) {
  const problems = [];
  const roles = ['render', 'reference', 'composite', 'before'];
  const hashes = rows.map((r) => Object.fromEntries(roles.map((k) => [k, r[k] && fs.existsSync(r[k]) ? sha1File(r[k]) : null])));
  rows.forEach((r, i) => {
    for (let a = 0; a < roles.length; a++) for (let c = a + 1; c < roles.length; c++) {
      const ha = hashes[i][roles[a]], hc = hashes[i][roles[c]];
      if (ha && hc && ha === hc) problems.push(`COMPARE.md row "${r.kase}": the ${roles[a]} and the ${roles[c]} are the same file (identical bytes): ${roles[a] === 'render' && roles[c] === 'before' ? 'the render shows no change from the before picture; a round that changed something moves the picture' : 'a copy is not a ' + roles[c]}`);
    }
    // render vs before from the SAME camera: when both decode and have the same size, at least MIN_MOVED of the pixels must differ clearly (a one-pixel edit is not a change)
    if (r.render && r.before && hashes[i].render && hashes[i].before && hashes[i].render !== hashes[i].before) {
      const a = decodePng(r.render), b = decodePng(r.before);
      if (a.decoded && b.decoded && a.width === b.width && a.height === b.height) {
        let moved = 0; const n = a.width * a.height;
        for (let k = 0; k < n; k++) { const o = k * 3; if (Math.abs(a.rgb[o] - b.rgb[o]) > 16 || Math.abs(a.rgb[o + 1] - b.rgb[o + 1]) > 16 || Math.abs(a.rgb[o + 2] - b.rgb[o + 2]) > 16) moved++; }
        if (moved / n < MIN_MOVED) problems.push(`COMPARE.md row "${r.kase}": the render differs from the before picture in only ${(100 * moved / n).toFixed(3)}% of its pixels (need at least ${(MIN_MOVED * 100).toFixed(2)}%): same-camera before/after must show real movement, else change the method (CLAUDE.md gate 4)`);
      }
    }
  });
  for (const role of ['render', 'composite']) {
    const seen = new Map();
    rows.forEach((r, i) => { const h = hashes[i][role]; if (!h) return; if (seen.has(h)) problems.push(`COMPARE.md rows "${seen.get(h)}" and "${r.kase}" use the same ${role} file (identical bytes): each case needs its own ${role}`); else seen.set(h, r.kase); });
  }
  return problems;
}

const IMG_RE = /\.(?:jpe?g|png|webp|gif|bmp|tiff?)$/i;
// Reference provenance. files: absolute paths of reference IMAGES named by the round (COMPARE.md reference cells, STATUS.md Case lines). ctx: { refRoot, folder, known (Set of sha1 | null) }.
// Each must be a real file under the main checkout's docs/reference (not _compare), different in content from every picture the round itself wrote, and (when a hash log
// exists) registered: its sha1 appears in docs/reference/**/INDEX.md or in the hook-written .claude/state/ref_log.jsonl. A reference outside docs/reference passes only when it
// is not inside the hand-in folder (prototype screenshots and spec images live elsewhere): the gate says so in a note.
export function referenceProblems(files, ctx) {
  const problems = [], notes = [];
  const real = (p) => { try { return fs.realpathSync(p); } catch { return path.resolve(p); } };
  const lc = (p) => real(p).split(path.sep).join('/').toLowerCase();
  const refReal = lc(ctx.refRoot);
  const own = new Map();
  for (const p of walkFiles(ctx.folder)) if (IMG_RE.test(p)) { const h = sha1File(p); if (h) own.set(h, path.relative(ctx.folder, p).split(path.sep).join('/')); }
  const seen = new Set();
  for (const f of files) {
    if (!IMG_RE.test(f) || seen.has(f)) continue; seen.add(f);
    if (!fs.existsSync(f)) continue;   // a missing file is check_handin's own complaint
    const n = lc(f), h = sha1File(f);
    const inRef = n.startsWith(refReal + '/') && !n.startsWith(refReal + '/_compare/');
    if (n.startsWith(refReal + '/_compare/')) problems.push(`reference ${path.basename(f)} is a composite from docs/reference/_compare: a composite is the round's own picture, not a reference photo`);
    if (h && own.has(h)) problems.push(`reference ${path.basename(f)} is byte-identical to ${own.get(h)} in the hand-in folder: a copy of the round's own picture is not a reference photo`);
    if (n.startsWith(lc(ctx.folder) + '/')) problems.push(`reference ${path.basename(f)} lies inside the hand-in folder: a reference photo comes from docs/reference of the main checkout`);
    else if (!inRef) notes.push(`reference ${path.basename(f)} is outside docs/reference (${f}): not checked for provenance`);
    else if (ctx.known && h && !ctx.known.has(h)) problems.push(`reference ${path.basename(f)} (sha1 ${h.slice(0, 10)}) is not a registered reference photo: its hash is in no docs/reference INDEX.md and not in the reference log (.claude/state/ref_log.jsonl, written by the PostToolUse hook when a file lands in docs/reference); register it with the reference INDEX or download it through a tool call`);
  }
  return { problems, notes };
}
// Case lines of STATUS.md: each case needs >= 3 reference IMAGES that differ in content (three copies of one picture count once).
export function distinctReferenceProblems(caseName, files) {
  const hs = new Set();
  for (const f of files) { if (!IMG_RE.test(f) || !fs.existsSync(f)) continue; const h = sha1File(f); if (h) hs.add(h); }
  const imgs = files.filter((f) => IMG_RE.test(f) && fs.existsSync(f)).length;
  return imgs >= 3 && hs.size < 3 ? [`STATUS.md: case "${caseName}" names ${imgs} reference images but only ${hs.size} different picture(s) by content: copies of one image count once; at least 3 different photos are needed`] : [];
}
// known reference hashes: every 40-hex token in docs/reference/**/INDEX.md plus the hook-written log. null = nothing to compare with (the caller says FAIL-OPEN: not checkable).
export function knownReferenceHashes(main) {
  const set = new Set(); let any = false;
  const logFile = path.join(main, '.claude', 'state', 'ref_log.jsonl');
  try { for (const l of fs.readFileSync(logFile, 'utf8').split('\n')) { if (!l.trim()) continue; try { const o = JSON.parse(l); if (o && o.replaced === true) { any = true; continue; }   /* an overwritten picture (gates6, audit item 26) is not a reference */ if (o && /^[0-9a-f]{40}$/.test(o.sha1)) { set.add(o.sha1); any = true; } } catch { /* skip */ } } } catch { /* no log */ }
  for (const f of walkFiles(path.join(main, 'docs', 'reference'), 0).filter((x) => /(^|[\\/])INDEX\.md$/.test(x))) {
    try { for (const m of fs.readFileSync(f, 'utf8').matchAll(/\b[0-9a-f]{40}\b/g)) { set.add(m[0]); any = true; } } catch { /* skip */ }
  }
  return any ? set : null;
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
// Further disguises of "not done": could not / unable to + a work verb, gave up, unfinished / unresolved / incomplete, partly done. (A bare "cannot" or "incomplete" in
// technical prose about the engine is not matched: the verb or the word "work" must be there.)
const NOT_DONE_MORE_RE = new RegExp([
  String.raw`\b(?:could\s*not|couldn${AP}t|unable\s+to)\s+(?:finish|complete|implement|fix|do|get|make|build|address|add|write|run|render|attempt)\b`,
  String.raw`\bgave\s+up\b`, String.raw`\bunfinished\b`, String.raw`\bunresolved\b`, String.raw`\b(?:partial(?:ly)?|partly|half)[- ](?:done|implemented|finished|fixed|complete|completed)\b`,
  String.raw`\bnot\s+(?:yet\s+)?(?:been\s+)?(?:done|implemented|finished|fixed|addressed|written|built)\s+(?:yet|at\s+all)\b`,
  // gates3 audit fault 13: rewordings of "not done"
  String.raw`\b(?:was|were|is|are|been|being|got)\s+(?:dropped|omitted|descoped|parked|shelved|abandoned|ignored|left\s+out)\b`,
  String.raw`\b(?:dropped|omitted|descoped|parked|shelved|abandoned)\s+(?:for\s+now|this\s+round|from\s+(?:this|the)\s+round)\b`,
  String.raw`\bleft\s+as\s+(?:is|it\s+was|they\s+were)\b`, String.raw`\bremains?\s+as\s+(?:it\s+was|they\s+were|before|is)\b`, String.raw`\bremain(?:s|ed)?\s+(?:unchanged|as\s+they\s+were)\b`,
  String.raw`\bchose\s+not\s+to\b`, String.raw`\b(?:decided|elected)\s+(?:not\s+to|against)\b`, String.raw`\bno\s+(?:further|more)\s+work\b`,
  String.raw`\b(?:postpon\w*|deprioriti[sz]\w*|descop\w*)\b`, String.raw`\bwill\s+be\s+(?:revisited|picked\s+up|handled|done|addressed)\b`, String.raw`\bto\s+be\s+(?:revisited|picked\s+up)\b`,
  String.raw`\bout\s+of\s+reach\b`, String.raw`\b(?:would|will)\s+(?:require|need)\s+(?:a\s+)?(?:larger|bigger|major|separate|new)\b`, String.raw`\bparked\b`, String.raw`\bfor\s+now\b`,
  // gates4 pass 2: a second batch found by attacking the reader with 24 fresh phrasings (a phrase list is never complete: the ITEM rows are the structural check)
  String.raw`\bno\s+attempt\b`, String.raw`\bnothing\s+(?:was\s+)?done\s+(?:about|for|on|to)\b`, String.raw`\bn/a\s+(?:for|this)\b`,
  String.raw`(?:^|[.:;]\s*)(?:ignored|skipping)\b`, String.raw`\b(?:will\s+not|won${AP}t)\s+(?:fix|change|touch|address|do)\b`, String.raw`\bleaving\b[^.\n]{0,40}\b(?:as\s+is|alone|unchanged|as\s+it\s+is)\b`,
  String.raw`\bstays?\s+as\s+(?:it\s+is|they\s+are|before)\b`, String.raw`\bdid\s+not\s+get\s+(?:round|around)\s+to\b`, String.raw`\b(?:is|are)\s+(?:still\s+)?(?:missing|absent|pending)\b`,
  String.raw`:\s*(?:pending|open|n/a|tbd)\s*\.?\s*$`, String.raw`\bstopped\s+at\b`, String.raw`\bpartial\s*:`, String.raw`\bonly\s+(?:the\s+)?\w+\s+was\s+done\b`, String.raw`\bremains\s*\.?\s*$`,
  String.raw`\b(?:was|were|is|are|has|have)\s+not\s+touched\b`, String.raw`\bnot\s+in\s+this\s+round\b`, String.raw`\b(?:won${AP}t|will\s+not)\s+(?:be\s+)?(?:built|done|fixed|addressed|implemented)\b`,
].join('|'), 'i');
const notDone = (l) => NOT_DONE_RE.test(l) || NOT_DONE_MORE_RE.test(l) || (/(?<![\d%]\s?)\bskipped\b/i.test(l) && !/\b(?:no|nothing|none|not|never|without)\b[^.\n]{0,40}\bskipped\b/i.test(l));

// BLOCKED form: `BLOCKED: <item> | tried: <what was run> | blocked by: <the concrete thing> | needs: <what unblocks it>` (a table row `| BLOCKED: ... | tried: ... |` is the same form).
// "Concrete" = something a reader can look up: a quoted error text, a command in backticks, a file path or file:line, an error code (ENOENT, C2065, exit code 1, HTTP 403), a refused
// command, a missing file path. "The complexity", "the geometry", "difficult", "a larger rewrite", time, usage, scope and later-round words are not blocks. At most 3 BLOCKED lines
// unless each names a DIFFERENT concrete cause. A BLOCKED line never waives a measured gate floor (runMeasuredGates) and never replaces a missing item the brief listed (itemsProblems).
const FILLER_RE = /^(?:nothing|none|n\/?a|no|tbd|todo|unknown|time|\?+|-+|\.+|see above|same)\W*$/i;
const VAGUE_RE = /\b(?:complexity|complex|complicated|difficult(?:y|ies)?|hard|tricky|geometry|infeasible|not feasible|unfeasible|too (?:much|many|big|large|long|slow)|larger? (?:refactor|rewrite|change)|bigger|rewrite|refactor|effort|inherent|nature of|a lot of work|beyond)\b/i;
const TIME_RE = /\b(?:(?:more|extra|additional|enough)\s+(?:time|tokens|context|budget)|tokens?|usage|quota|rate limit|budget)\b/i;
export function anchorsOf(text) {
  const t = String(text || ''), out = new Set();
  // quoted or backticked text counts only when it looks like something that can be looked up (a path, a command, an identifier, a number), not prose ("`the complexity`")
  const lookable = (x) => /[\\/._:=()\d-]/.test(x) && !VAGUE_RE.test(x);
  for (const m of t.matchAll(/`([^`]{3,})`/g)) if (lookable(m[1])) out.add(m[1].trim().toLowerCase());
  for (const m of t.matchAll(/"([^"]{8,})"|“([^”]{8,})”/g)) if (lookable(m[1] || m[2])) out.add((m[1] || m[2]).trim().toLowerCase());
  for (const m of t.matchAll(/(?:[A-Za-z]:)?[\w.-]*[\\/][\w./\\-]*[\w-]\.\w{1,6}\b|\b[\w-]+\.(?:mjs|js|cpp|h|hpp|json|md|ps1|cmake|txt|log|png|jpe?g|exe|uasset|py|yml|yaml)(?::\d+)?\b/g)) out.add(m[0].toLowerCase());
  for (const m of t.matchAll(/\b(?:E[A-Z]{3,}|C\d{4}|LNK\d{4}|exit (?:code|status) -?\d+|HTTP \d{3}|status (?:code )?\d{3})\b/g)) out.add(m[0].toLowerCase());
  return [...out];
}
const blankAnchors = (t) => String(t || '').replace(/`[^`]*`/g, ' ').replace(/"[^"]*"/g, ' ').replace(/“[^”]*”/g, ' ');
// gates v3 (main session decision, 23 Oct 2026): the BLOCKED line also carries `see: <path>`, a log or output file that exists, and every quoted error (backticks or quotes) in
// "blocked by:" must be found in that file (whitespace and case ignored); with no quoted text the error codes, else the named paths, must be found. A made-up quote therefore needs
// a made-up file, which must look like a log (3+ lines, the quote not more than half of it). "needs:" must name a thing, not "help" or "a decision".
// WHAT IT CANNOT SEE: a log file written by hand with real-looking lines (the 3-line and half-of-the-file rules only stop the laziest echo); STATUS says so.
export function anchorKinds(text) {
  const t = String(text || ''); const quoted = [], codes = [], paths = [];
  const lookable = (x) => /[\\/._:=()\d-]/.test(x) && !VAGUE_RE.test(x);
  for (const m of t.matchAll(/`([^`]{3,})`/g)) if (lookable(m[1])) quoted.push(m[1].trim());
  for (const m of t.matchAll(/"([^"]{8,})"|“([^”]{8,})”/g)) if (lookable(m[1] || m[2])) quoted.push((m[1] || m[2]).trim());
  for (const m of t.matchAll(/\b(?:E[A-Z]{3,}|C\d{4}|LNK\d{4}|exit (?:code|status) -?\d+|HTTP \d{3}|status (?:code )?\d{3})\b/g)) codes.push(m[0]);
  for (const m of t.matchAll(/(?:[A-Za-z]:)?[\w.-]*[\\/][\w./\\-]*[\w-]\.\w{1,6}\b|\b[\w-]+\.(?:mjs|js|cpp|h|hpp|json|md|ps1|cmake|txt|log|png|jpe?g|exe|uasset|py|yml|yaml)(?::\d+)?\b/g)) paths.push(m[0].replace(/:\d+$/, ''));
  return { quoted, codes, paths };
}
export function blockedEvidence(blockedBy, seeRaw, ctx) {
  const why = [];
  const see = String(seeRaw || '').trim().replace(/^[`"']|[`"']$/g, '');
  if (!see) return ['"see:" is missing: give the path of the log or output file that holds the error (see: ..\\roadworld-scratch\\<round>\\build.log)'];
  let file = null;
  const cands = path.isAbsolute(see) ? [see] : (ctx.bases || []).map((b) => path.resolve(b, see));
  for (const c of cands) { try { if (fs.statSync(c).isFile()) { file = c; break; } } catch { /* next */ } }
  if (!file) return [`"see: ${see}" is not an existing file (looked in ${cands.length ? cands.join(', ') : 'no folder'}): the quoted error must be in a log or output file that is there`];
  // gates6 (audit item 24): the file must be THIS round's own output: inside the agent's own worktree or scratch folder (ctx.ownRoots), written after the round began (ctx.roundStart),
  // and not a tracked file of the repository. A quote planted in an older or foreign file is refused.
  if (ctx.roundStart || (ctx.ownRoots && ctx.ownRoots.length)) {
    let real = file; try { real = fs.realpathSync(file); } catch { /* keep */ }
    const low = (x) => path.resolve(x).toLowerCase().split(path.sep).join('/');
    if (ctx.ownRoots && ctx.ownRoots.length && !ctx.ownRoots.some((r) => { const a = low(real), b = low(r); return a === b || a.startsWith(b + '/'); })) return [`"see: ${see}" is not inside this round's own worktree or scratch folder (${ctx.ownRoots.join(', ')}): evidence from a foreign file is not accepted`];
    if (ctx.roundStart) { let mt = 0; try { mt = fs.statSync(file).mtimeMs; } catch { /* none */ } if (mt < ctx.roundStart) return [`"see: ${see}" was last written ${new Date(mt).toISOString()}, before this round began (${new Date(ctx.roundStart).toISOString()}): an older file cannot show a block of this round`]; }
    if (ctx.repoRoot) { const r = spawnSync('git', ['-C', ctx.repoRoot, 'ls-files', '--error-unmatch', '--', file], { encoding: 'utf8', timeout: 8000 }); if (r.status === 0) return [`"see: ${see}" is a tracked file of the repository, not the output of a run: point at the log you captured (build.log, a .out file in your scratch folder)`]; }
  }
  let body = ''; try { body = fs.readFileSync(file, 'utf8'); } catch (e) { return [`"see: ${see}" cannot be read: ${e.message}`]; }
  const flat = (x) => String(x).replace(/\s+/g, ' ').trim().toLowerCase();
  const hay = flat(body);
  const kinds = anchorKinds(blockedBy);
  const need = kinds.quoted.length ? kinds.quoted : kinds.codes.length ? kinds.codes : kinds.paths;
  if (!need.length) return ['"blocked by:" has nothing that can be looked up in the "see:" file'];
  const missing = need.filter((q) => !hay.includes(flat(q)));
  if (missing.length) why.push(`"${missing[0].slice(0, 80)}" is not in ${see} (the quoted error must be found in the log the line points to${missing.length > 1 ? '; ' + missing.length + ' of ' + need.length + ' quotes are missing' : ''})`);
  if (body.split('\n').filter((l) => l.trim()).length < 3) why.push(`${see} has fewer than 3 non-empty lines: it is not a log or output file`);
  else if (need.reduce((n, q) => n + q.length, 0) * 2 > hay.length) why.push(`the quoted text is more than half of ${see}: it looks written to match, not captured from a run`);
  return why;
}
const NEEDS_GENERIC = /^(?:(?:a|an|the|some|more|any|your|owner'?s?)\s+)*(?:fix|decision|help|input|guidance|clarification|info|information|approach|different approach|review|answer|answers|advice|permission|access|resources?|support|direction|feedback|solution|workaround|change|changes)\W*$/i;
// gates6 (audit item 24): in strict mode (ctx.roundStart set) "needs:" must also be CONCRETE: a file, command, path or error in it, or a question that says WHICH decision (whether / which of /
// choose between / either ... or) in 6+ content words. "owner decision about the roads" names no decision and is refused.
export function needsIsConcrete(needs) {
  const n = String(needs || '');
  if (anchorsOf(n).length || /`[^`]{3,}`|"[^"]{6,}"/.test(n)) return true;
  if (!/\b(?:whether|which of|which one|choose between|either|between)\b/i.test(n)) return false;
  const stop = /^(?:a|an|the|to|of|on|in|for|and|or|with|from|by|is|are|be|should|would|could|can|it|that|this|owner|owners|decision|about|whether|which|between|either|please)$/i;
  return n.replace(/[^\w\s]/g, ' ').split(/\s+/).filter((w) => w && !stop.test(w)).length >= 6;
}
export function needsNamesAThing(needs) {
  const n = String(needs || '').trim();
  if (FILLER_RE.test(n) || NEEDS_GENERIC.test(n)) return false;
  if (anchorsOf(n).length) return true;
  if (n.length < 8) return false;
  const words = n.replace(/[^\w\s./\\:-]/g, ' ').split(/\s+/).filter(Boolean);
  const content = words.filter((w) => !/^(?:a|an|the|to|of|on|in|for|and|or|with|from|by|is|are|be|more|some|any|new|please|need|needs|needed|decision|help|fix|input)$/i.test(w));
  return content.length >= 2;
}
export function parseBlocked(text, ctx) {
  const out = [];
  String(text || '').replace(/<!--[\s\S]*?-->/g, '').split('\n').forEach((raw, i) => {
    let l0 = raw.trim();
    if (l0.startsWith('|')) { const k = l0.search(/\|\s*BLOCKED\s*:/i); if (k >= 0) l0 = l0.slice(k); }   // a BLOCKED cell anywhere in a table row (the item id may come first)
    const l = l0.replace(/^\|\s*/, '').replace(/\s*\|\s*$/, '').replace(/^[-*]\s+/, '');
    const m = l.match(/^BLOCKED\s*:\s*(.*)$/i);
    if (!m) return;
    const parts = m[1].split('|').map((x) => x.trim());
    const item = parts[0] || '';
    const field = (re) => { const p = parts.slice(1).find((x) => re.test(x)); return p ? p.replace(re, '').trim() : ''; };
    const tried = field(/^tried\s*:\s*/i), blockedBy = field(/^blocked\s+by\s*:\s*/i), needs = field(/^needs\s*:\s*/i), see = field(/^see\s*:\s*/i);
    const why = [];
    if (item.length < 4) why.push('no item before the first |');
    const ta = anchorsOf(tried), ba = anchorsOf(blockedBy);
    if (tried.length < 12 || FILLER_RE.test(tried)) why.push('"tried:" is missing or too short (say what you ran, 12+ characters)');
    else if (!ta.length) why.push('"tried:" names nothing that was run (a command in backticks, a file path, a test name): "what I attempted" in general words is not a record');
    else if (USAGE_RE.test(tried) || TIME_RE.test(tried)) why.push('"tried:" gives time, usage or tokens as a reason');
    if (blockedBy.length < 12 || FILLER_RE.test(blockedBy)) why.push('"blocked by:" is missing or too short (the exact error, refusal or missing thing, 12+ characters)');
    else if (USAGE_RE.test(blockedBy) || TIME_RE.test(blockedBy) || /^(?:time|scope|later|next round|usage)\b/i.test(blockedBy)) why.push('"blocked by:" names time, usage, scope or a later round, which is not a block');
    else if (!ba.length) why.push('"blocked by:" names no concrete thing: quote the error text, command output, refused command, or the missing file path (in backticks or quotes, or a path or error code)');
    else if (VAGUE_RE.test(blankAnchors(blockedBy))) why.push('"blocked by:" is a judgement ("complexity", "difficult", "the geometry", "a larger rewrite"...), not a block: quote the failing thing');
    if (needs && (USAGE_RE.test(needs) || TIME_RE.test(needs))) why.push('"needs:" asks for more time, tokens or usage, which is not something that unblocks work');
    if (ctx && ctx.version >= 3) {
      if (needsNamesAThing(needs) && ctx.roundStart && !needsIsConcrete(needs)) why.push('"needs:" names no concrete thing: say WHICH decision ("whether X or Y"), or name the file, command or path that would unblock it, not "owner decision about <topic>"');
      if (!needsNamesAThing(needs)) why.push('"needs:" must name a thing that would unblock the work (a file, a command, a named decision with its subject), not "help", "a decision", "nothing" or time');
      // the evidence check runs once the line is otherwise concrete (one clear message at a time)
      if (!why.some((w) => /blocked by/.test(w))) why.push(...blockedEvidence(blockedBy, see, ctx));
    }
    out.push({ line: i + 1, item, tried, blockedBy, needs, anchors: ba, ok: why.length === 0, why, raw: l });
  });
  const okOnes = out.filter((b) => b.ok);
  if (out.length > 3) {
    const keys = out.map((b) => b.anchors.slice().sort().join('\u0001'));
    if (new Set(keys).size < out.length) out.forEach((b) => { b.ok = false; b.why.push(`${out.length} BLOCKED lines: more than 3 need a DIFFERENT concrete cause each, and some share one (one cause = one line)`); });
  }
  void okOnes;
  return out;
}

// The items the brief lists (ITEM: lines) against the STATUS "## Items" section: every item has a row that says done with a proof, or a valid BLOCKED line. An item that is
// simply absent (the usual way work is silently dropped) is refused without any phrase list.
const blockedSee = (ctx) => (ctx && ctx.version >= 3 ? "see: <path of the log or output file that holds the error> | " : "");
export function itemsProblems(statusText, items, blockedCtx) {
  if (!items || !items.length) return [];
  const st = String(statusText || '').replace(/\r\n/g, '\n').replace(/<!--[\s\S]*?-->/g, '');
  const sec = st.match(/^##\s+Items[^\n]*\n([\s\S]*?)(?=^##\s|(?![\s\S]))/im);
  if (!sec) return [`STATUS.md has no "## Items" section: the brief lists ${items.length} item(s) (${items.map((i) => i.id).join(', ')}); give one row per item, "| <id> | done | <the test, run or image that proves it> |" or a BLOCKED line`];
  const lines = sec[1].split('\n');
  const problems = [];
  for (const it of items) {
    const idRe = new RegExp('^\\s*(?:[-*]\\s*)?\\|?\\s*\\**' + it.id.replace(/[.*+?^${}()|[\]\\]/g, '\\$&') + '\\b');
    const row = lines.find((l) => idRe.test(l));
    if (!row) { problems.push(`item ${it.id} ("${it.text.slice(0, 60)}") has no row in the STATUS "## Items" section: every listed item is done with proof or BLOCKED, none is left out`); continue; }
    const bl = parseBlocked(row, blockedCtx)[0];
    if (bl) { if (!bl.ok) problems.push(`item ${it.id}: its BLOCKED line is incomplete (${bl.why.join('; ')})`); continue; }
    const cells = row.split('|').map((c) => c.trim()).filter(Boolean);
    const status = (cells[1] || '').toLowerCase(), proof = cells.slice(2).join(' ');
    if (!/^(?:done|fixed)\b/.test(status)) problems.push(`item ${it.id}: the row says "${(cells[1] || '').slice(0, 30)}"; use "done" with its proof, or a BLOCKED line`);
    else if (proof.length < 15 || /^(?:done|ok|fixed|yes)\W*$/i.test(proof) || !(anchorsOf(proof).length || /\d/.test(proof))) problems.push(`item ${it.id}: "done" needs the test, run or image that proves it (15+ characters, naming a file, a test in quotes, a command or a number: "it works" proves nothing)`);
  }
  return problems;
}

// Section headings whose table rows are bookkeeping, not statements of work (they may quote the words the reader looks for)
const BOOKKEEPING_HEAD = /prior audit|proof|reference files|images that carry|main session additions|items|gate tests removed|changes \(/i;
export function statusProblems(statusText, faultsText, opts = {}) {
  const problems = [];
  const stat = String(statusText || '').replace(/<!--[\s\S]*?-->/g, '');
  const blocked = parseBlocked(stat, opts.blockedCtx);
  const blockedLines = new Set(blocked.map((b) => b.line));
  for (const b of blocked) if (!b.ok) problems.push(`STATUS.md line ${b.line}: a BLOCKED line is not acceptable (${b.why.join('; ')}): "BLOCKED: <item> | tried: <the command or file you ran> | blocked by: <the exact error, refused command or missing path> | ${blockedSee(opts.blockedCtx)}needs: <what would unblock it>"`);
  const lines = stat.split('\n');
  let head = '';
  lines.forEach((l, i) => {
    if (/^\s*#/.test(l)) { head = l; return; }
    if (blockedLines.has(i + 1)) return;
    const isRow = /^\s*\|/.test(l);
    const bookkeeping = isRow && BOOKKEEPING_HEAD.test(head);
    if (notDone(l) && !/^\s*(?:Case|References opened)/i.test(l) && !bookkeeping) problems.push(`STATUS.md line ${i + 1}: an item is left undone or deferred ("${l.trim().slice(0, 100)}"): do it (contract items 5 and 17), or, if something really blocks it, write "BLOCKED: <item> | tried: <what you ran> | blocked by: <exact error or missing path> | ${blockedSee(opts.blockedCtx)}needs: <what unblocks it>"; "out of scope", "later round" and "bigger change" are not reasons`);
    if (USAGE_RE.test(l)) problems.push(`STATUS.md line ${i + 1}: a usage/time limit is given as a reason ("${l.trim().slice(0, 100)}"): that is not an accepted reason; finish the item or write a BLOCKED line with the technical block`);
  });
  // partial reads do not count as opened (contract item 1)
  const ro = stat.match(/References opened:\s*(\d+)\s+of\s+(\d+)/i);
  if (ro && +ro[1] >= +ro[2] && /\b(?:partly|partially|read\s+(?:only\s+)?(?:the\s+)?(?:head|first|top)|skimmed|heads\s+and\s+greps|first\s+\d+\s+(?:lines|characters))\b/i.test(stat.split(/^##\s+Reference files opened/im)[0])) problems.push('STATUS.md: "References opened" says all files were opened but the text says some were only partly read; a partial read counts as not opened (contract item 1): open them in full, or write N < M with a "not opened: <file> (reason)" line');
  // the count must match the files named in the Reference files section (gates3 audit: "10 of 12" next to 11 named files)
  const refSec = stat.match(/^##\s+Reference files opened[^\n]*\n([\s\S]*?)(?=^##\s|(?![\s\S]))/im);
  if (ro && refSec) {
    const named = new Set();
    for (const c of refSec[1].matchAll(/^\s*Case:[^|\n]*\|\s*Opened:\s*(.*)$/gim)) for (const f of c[1].split(',').map((x) => x.trim()).filter(Boolean)) named.add(f.toLowerCase());
    if (named.size && (+ro[1] + (stat.match(/^\s*not opened:/gim) || []).length !== +ro[2] || +ro[1] > named.size)) problems.push(`STATUS.md: "References opened: ${ro[1]} of ${ro[2]}" but the "Reference files opened" section names ${named.size} distinct file(s) (plus ${(stat.match(/^\s*not opened:/gim) || []).length} "not opened" line(s)): the count must match the files named`);
  }
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
export const BLOCKED_HINT = 'do not fill tables to get through the gate: if you cannot produce the render, reference or composite, finish with a BLOCKED line in STATUS.md ("BLOCKED: <item> | tried: <what> | blocked by: <exact error or missing thing> | see: <log file path, gates v3> | needs: <what unblocks it>")';

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
