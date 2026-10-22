#!/usr/bin/env node
// Tests for gates v2 (tools/gates_lib.mjs, the GATE parts of check_handin.mjs and make_brief.mjs).
// Usage: node tools/test_gates.mjs   (exit 1 on any failure). The hook-side audit tests are in tools/hooks/test_hooks.mjs.
// Builds a throwaway git repo (a "main" checkout on a branch NOT called main, plus a linked worktree) under the temp dir.
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import crypto from 'node:crypto';
import { spawnSync } from 'node:child_process';
import { fileURLToPath, pathToFileURL } from 'node:url';

const src = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const GL = await import(pathToFileURL(path.join(src, 'tools/gates_lib.mjs')).href);
const tmp = fs.mkdtempSync(path.join(os.tmpdir(), 'gatetest-'));
let pass = 0, fail = 0;
const t = (name, cond, detail = '') => { if (cond) { pass++; console.log('  ok   ' + name); } else { fail++; console.log('  FAIL ' + name + (detail ? '\n       ' + String(detail).replace(/\n/g, '\n       ').slice(0, 900) : '')); } };
const git = (cwd, ...a) => { const r = spawnSync('git', a, { cwd, encoding: 'utf8' }); if (r.status !== 0) throw new Error('git ' + a.join(' ') + ': ' + r.stderr); return r.stdout.trim(); };
const w = (p, s) => { fs.mkdirSync(path.dirname(p), { recursive: true }); fs.writeFileSync(p, s); };

// ======================================================= grammar
console.log('gate grammar');
let g = GL.parseGateLine('scene_check >= 20/21 (baseline 20/21, target 21/21)');
t('scene_check line with baseline and target', g.kind === 'metric' && g.metric === 'scene_check' && g.op === '>=' && g.floor === '20/21' && g.baseline === '20/21' && g.target === '21/21', JSON.stringify(g));
g = GL.parseGateLine('ctest failed_assertions == 2'); t('ctest failed_assertions == 2', g.kind === 'metric' && g.metric === 'ctest failed_assertions' && g.floor === '2' && g.op === '==');
g = GL.parseGateLine('- ctest failed_tests <= 0 (target 0)'); t('leading dash, <=, target only', g.kind === 'metric' && g.op === '<=' && g.target === '0');
g = GL.parseGateLine('allow_waiver is_rough:over_height_cliff'); t('allow_waiver line', g.kind === 'allow' && g.key === 'is_rough:over_height_cliff');
g = GL.parseGateLine('picture_min_road 0.02'); t('picture_min_road override', g.kind === 'picture' && g.name === 'picture_min_road' && g.value === 0.02);
g = GL.parseGateLine('frame_rate >= 30'); t('unknown metric is "bad" with the known list', g.kind === 'bad' && /unknown metric/.test(g.why) && /scene_check/.test(g.why));
g = GL.parseGateLine('No new test waiver, skip or should_fail.'); t('prose line is "bad" (make_brief ignores prose lines)', g.kind === 'bad');
t('blank line is null', GL.parseGateLine('   ') === null);
t('satisfies P/T >=', GL.satisfies('17/21', '>=', '20/21') === false && GL.satisfies('20/21', '>=', '20/21') === true && GL.satisfies('21/21', '>=', '20/21') === true);
t('satisfies numbers ==, <=', GL.satisfies(2, '==', '2') && !GL.satisfies(3, '==', '2') && GL.satisfies(0, '<=', '2') && !GL.satisfies(5, '<=', '2'));

// ======================================================= waiver guard: every form, on diffs shaped like the real ones
console.log('waiver guard (diff forms)');
const diff = (file, removed, added) => `diff --git a/${file} b/${file}\n--- a/${file}\n+++ b/${file}\n@@ -1 +1 @@\n${removed.map((l) => '-' + l).join('\n')}${removed.length ? '\n' : ''}${added.map((l) => '+' + l).join('\n')}\n`;
const keys = (d, allowed = []) => GL.waiverFindings(d, allowed).refused.map((f) => f.key);
const T = 'tests/test_grading.cpp';
const oldRough = 'bool is_rough(const std::string& n) { return n == "adjacent_levels" || n == "short_run_between_bridges"; }';
let k = keys(diff(T, [oldRough], ['bool is_rough(const std::string& n) { return n == "adjacent_levels" || n == "short_run_between_bridges" || n == "over_height_cliff" || n == "stress_gradient_085"; }']));
t('is_rough list gains two names: both found (the real round 9 form)', k.includes('is_rough:over_height_cliff') && k.includes('is_rough:stress_gradient_085') && !k.some((x) => /adjacent_levels/.test(x)), JSON.stringify(k));
t('the same is_rough line unchanged: nothing found', keys(diff(T, [oldRough], [oldRough])).length === 0);
k = keys(diff(T, ['    GRADE_CHECK(is_rough(name), a == 0, "x");'], ['    GRADE_CHECK(is_rough(name), a == 0, "x");', '    GRADE_CHECK(is_rough(name), b == 0, "y");']));
t('one more GRADE_CHECK(is_rough(...)) than before: found', k.includes('grade_check'), JSON.stringify(k));
k = keys(diff(T, ['TEST_CASE("structures: the interchange bridge") {'], ['TEST_CASE("structures: the interchange bridge" * doctest::should_fail()) {']));
t('should_fail added to an existing test: found, keyed by the test name', k.some((x) => x === 'should_fail:structures: the interchange bridge'), JSON.stringify(k));
t('should_fail already there (unchanged context): not found', keys(diff(T, ['TEST_CASE("a" * doctest::should_fail()) {'], ['TEST_CASE("a" * doctest::should_fail()) { // reworded'])).length === 0);
k = keys(diff(T, [], ['TEST_CASE("w: previews" * doctest::skip(true)) {'])); t('doctest::skip added: found', k.some((x) => x.startsWith('skip:')), JSON.stringify(k));
k = keys(diff(T, [], ['TEST_CASE("w" * doctest::may_fail()) {'])); t('doctest::may_fail added: found', k.some((x) => x.startsWith('may_fail:')), JSON.stringify(k));
k = keys(diff('tests/CMakeLists.txt', [], ['set_tests_properties(foo PROPERTIES WILL_FAIL TRUE)'])); t('WILL_FAIL in tests/CMakeLists.txt: found', k.some((x) => x.startsWith('ctest_disabled')), JSON.stringify(k));
k = keys(diff(T, ['constexpr double kTolHeight = 1.5e-4;'], ['constexpr double kTolHeight = 1.0e-3;'])); t('kTolHeight loosened 1.5e-4 -> 1e-3: found', k.includes('tolerance:kTolHeight'), JSON.stringify(k));
t('kTolHeight tightened: not found', keys(diff(T, ['constexpr double kTolHeight = 1.5e-4;'], ['constexpr double kTolHeight = 1.0e-5;'])).length === 0);
k = keys(diff(T, [], ['constexpr double kTolNew = 0.5;'])); t('a new tolerance constant: found', k.includes('tolerance:kTolNew'), JSON.stringify(k));
t('"reps = 3" and "steps = 4" are not tolerance constants (round 9 false positive)', keys(diff(T, [], ['const int reps = 3;', 'int steps = 4;'])).length === 0);
k = keys(diff(T, ['TEST_CASE("grading: crease") {', 'TEST_CASE("grading: keep") {'], ['TEST_CASE("grading: keep") {'])); t('a deleted TEST_CASE: found', k.includes('deleted_test:grading: crease') && !k.includes('deleted_test:grading: keep'), JSON.stringify(k));
t('a TEST_CASE moved to another file is not "deleted"', keys(diff(T, ['TEST_CASE("moved") {'], []) + diff('tests/test_new.cpp', [], ['TEST_CASE("moved") {'])).length === 0);
k = keys(diff(T, ['    CHECK_MESSAGE(x, "m");'], ['    WARN_MESSAGE(x, "m");'])); t('CHECK turned into WARN_MESSAGE: found', k.some((x) => x.startsWith('warn:')), JSON.stringify(k));
const S = 'prototypes/road-draw/tools/scene.mjs';
k = keys(diff(S, [], ["    results.push({ name: 'new_check', pass: true, msg: 'ok' });"])); t('scene.mjs: a check with hard-coded pass: true added: found', k.includes('pass_true:new_check'), JSON.stringify(k));
k = keys(diff(S, ["    results.push({ name: 'median_flat', pass: ok, msg });"], [])); t('scene.mjs: a check removed: found', k.includes('deleted_check:median_flat'), JSON.stringify(k));
k = keys(diff('prototypes/road-draw/test-geom.mjs', [], ["it.skip('x', () => {});"])); t('JS it.skip added: found', k.some((x) => x.startsWith('skip:')), JSON.stringify(k));
const dw = diff(T, [oldRough], ['bool is_rough(const std::string& n) { return n == "adjacent_levels" || n == "over_height_cliff"; }']);
t('allowed by the task file via the full key', keys(dw, ['is_rough:over_height_cliff']).length === 0);
t('allowed by the bare name', keys(dw, ['over_height_cliff']).length === 0);
t('allowing one name does not allow another', keys(diff(T, [oldRough], ['bool is_rough(const std::string& n) { return n == "adjacent_levels" || n == "a" || n == "b"; }']), ['a']).join() === 'is_rough:b');
t('empty diff: nothing found', GL.waiverFindings('').findings.length === 0);

// ======================================================= STATUS reading
console.log('status reading');
const good3 = '## Pass 1\nthe kerb stops 4 m short of the island on the south arm\n## Pass 2\nthe give-way line is a stub, not a span across the whole arm\n## Pass 3\nthe hatching bars cross the kerb line at the island nose\n## Residual\nnone, all fixed\n';
t('clean STATUS and 3 passes: no problems', GL.statusProblems('## Skipped, refused or failed\n\nNothing skipped.\n', good3).length === 0);
let sp = GL.statusProblems('## Skipped, refused or failed\n- Not attempted: wall curl at road ends\n', good3); t('"Not attempted" item: refused', sp.length === 1 && /not attempted/i.test(sp[0]), sp.join('|'));
sp = GL.statusProblems('- I did not attempt a fix of the pit\n', good3); t('"did not attempt": refused', sp.length === 1);
for (const ok of ['- the time limit of the scene check is 240 s', '- the quota of lane widths per arm is 3']) { sp = GL.statusProblems(ok + '\n', good3); t('ordinary use of "time limit"/"quota" is not a usage reason: ' + ok, sp.length === 0, sp.join('|')); }
for (const bad of ['hit the time limit', 'will do next round']) { sp = GL.statusProblems('- ' + bad + '\n', good3); t('refused: ' + bad, sp.length >= 1, sp.join('|')); }
sp = GL.statusProblems('- never tried the curl\n', good3); t('"never tried": refused', sp.length === 1);
sp = GL.statusProblems('- wall curl: tried a ramp, blocked by the crossing road (the ground stays 2 m high)\n', good3); t('"tried ... blocked by ...": allowed', sp.length === 0, sp.join('|'));
for (const phrase of ['Pass 3 not done (usage window)', 'stopped because of the usage limit', 'ran out of tokens', 'running out of time', 'context window nearly full', 'hit the rate limit', 'token budget', 'no time left']) {
  sp = GL.statusProblems('- ' + phrase + '\n', good3); t('usage/time reason refused: "' + phrase + '"', sp.some((x) => /usage\/time limit/.test(x)), sp.join('|'));
}
sp = GL.statusProblems('ok\n', '## Pass 1\nthe kerb stops 4 m short of the island on the south arm\n## Pass 2\nthe give-way line is a stub, not a span across the whole arm\n## Residual\nthe pit remains\n'); t('2 passes, no residual-only line: refused', sp.length === 1 && /3 unless/.test(sp[0]), sp.join('|'));
sp = GL.statusProblems('ok\n', '## Pass 1\nthe kerb stops 4 m short of the island on the south arm\n## Pass 2\nthe give-way line is a stub, not a span across the whole arm\n## Residual\nthe pit remains\nResidual only unfixable: the pit needs a tunnel the engine cannot model and I tried a bridge and a wall.\n'); t('2 passes + a residual-only-unfixable line: allowed', sp.length === 0, sp.join('|'));
sp = GL.statusProblems('ok\n', '## Pass 1\nthe kerb stops 4 m short of the island on the south arm\n## Pass 2\nthe give-way line is a stub, not a span across the whole arm\n## Residual\nResidual only unfixable: none\n'); t('residual-only line too short (under 40 chars): refused', sp.length === 1);
sp = GL.statusProblems('ok\n', good3 + '\nPass 4 skipped, usage window\n'); t('usage wording in FAULTS.md: refused', sp.some((x) => /^FAULTS/.test(x)));
t('usage words inside an HTML comment (the template) are not read', GL.statusProblems('<!-- e.g. "usage window" -->\nok\n', good3).length === 0);

// ======================================================= method-change rule
console.log('method-change rule');
const R = (...v) => v.map((x, i) => ({ round: 'r' + i, values: x }));
t('fewer than 3 rounds: no rule', GL.methodChangeProblem(R({ scene_check: 20 }, { scene_check: 19 })) === null);
let mc = GL.methodChangeProblem(R({ scene_check: 20, ctest_failed_assertions: 2 }, { scene_check: 20, ctest_failed_assertions: 2 }, { scene_check: 17, ctest_failed_assertions: 2 }));
t('last two rounds did not improve (flat, then worse): method review demanded', /method review/.test(mc) && /r1/.test(mc) && /r2/.test(mc) && /--override/.test(mc), mc);
t('the last round improved: no refusal', GL.methodChangeProblem(R({ scene_check: 18 }, { scene_check: 18 }, { scene_check: 20 })) === null);
t('the round before improved: no refusal', GL.methodChangeProblem(R({ scene_check: 17 }, { scene_check: 19 }, { scene_check: 19 })) === null);
t('lower-is-better metric (failed assertions) worsening twice: refusal', GL.methodChangeProblem(R({ ctest_failed_assertions: 2 }, { ctest_failed_assertions: 3 }, { ctest_failed_assertions: 4 })) !== null);
t('lower-is-better improving: no refusal', GL.methodChangeProblem(R({ ctest_failed_assertions: 5 }, { ctest_failed_assertions: 3 }, { ctest_failed_assertions: 3 })) === null);
const pr = GL.parseRounds('- round7: scene_check=19, ctest failed_assertions=2\nround8: scene_check 20/21; ctest_failed_assertions 2\nprose line without a colon number');
t('parseRounds reads "k=v" and "k v" and P/T values', pr.length === 2 && pr[0].values.scene_check === 19 && pr[0].values.ctest_failed_assertions === 2 && pr[1].values.scene_check === 20, JSON.stringify(pr));

// ======================================================= picture sanity
console.log('picture sanity');
const pf = path.join(tmp, 'pic'); fs.mkdirSync(pf);
const stats = (name, o) => w(path.join(pf, name + '.stats.json'), JSON.stringify({ view: name, width: 1600, height: 900, diagnostic: false, road_px: 60000, debug_px: 0, png_sha1: 'abc', debug_by_class: { needs: 0, rock: 0 }, ...o }));
const cmpText = (...views) => '| case | render |\n|---|---|\n' + views.map((v) => `| c | ${v}.png | r | c | b | x | y | z |`).join('\n') + '\n';
stats('ok_aerial', {}); stats('ok_street', {});
let pp = GL.pictureProblems(pf, cmpText('ok_aerial')); t('road in frame, no debug colour: passes', pp.problems.length === 0, pp.problems.join('|'));
stats('empty_street', { road_px: 0 }); pp = GL.pictureProblems(pf, cmpText('empty_street')); t('no road in frame (round 9 s6 street): refused, says the subject is not in the picture', pp.problems.some((x) => /empty_street/.test(x) && /not in the picture/.test(x)), pp.problems.join('|'));
stats('orange_aerial', { debug_px: 727680, debug_by_class: { needs: 397719, rock: 329961 } }); pp = GL.pictureProblems(pf, cmpText('ok_street'));
t('a debug-colour view in the folder is judged even when COMPARE.md does not list it (50% orange/rock, round 9 s4 aerial)', pp.problems.some((x) => /orange_aerial/.test(x) && /debug colours/.test(x)), pp.problems.join('|'));
stats('orange_aerial', { debug_px: 800, debug_by_class: { needs: 800, rock: 0 } }); pp = GL.pictureProblems(pf, cmpText('ok_street')); t('a few debug pixels (0.06%) are tolerated', !pp.problems.some((x) => /orange/.test(x)), pp.problems.join('|'));
stats('orange_aerial', { diagnostic: true, debug_px: 727680 }); pp = GL.pictureProblems(pf, cmpText('ok_street')); t('a view declared diagnostic is exempt', !pp.problems.some((x) => /orange/.test(x)), pp.problems.join('|'));
pp = GL.pictureProblems(pf, cmpText('orange_aerial')); t('...but cannot be listed in COMPARE.md', pp.problems.some((x) => /diagnostic but is listed/.test(x)), pp.problems.join('|'));
fs.rmSync(path.join(pf, 'orange_aerial.stats.json'));
pp = GL.pictureProblems(pf, cmpText('ghost')); t('a COMPARE.md render with no stats file: refused, names render.mjs', pp.problems.some((x) => /ghost/.test(x) && /render\.mjs/.test(x)), pp.problems.join('|'));
w(path.join(pf, 'x_aerial.png'), 'png'); pp = GL.pictureProblems(pf, cmpText('ok_aerial')); t('an *_aerial.png with no stats file is refused even when COMPARE.md does not list it', pp.problems.some((x) => /x_aerial/.test(x)), pp.problems.join('|'));
fs.rmSync(path.join(pf, 'x_aerial.png'));
const pf2 = path.join(tmp, 'pic2'); fs.mkdirSync(pf2);
for (const n of ['a', 'b', 'c', 'd']) w(path.join(pf2, n + '.stats.json'), JSON.stringify({ width: 100, height: 100, diagnostic: n !== 'a', road_px: 0, debug_px: 9000, png_sha1: 'abc' }));
pp = GL.pictureProblems(pf2, cmpText('a')); t('a diagnostic flag on 3 of 4 views: refused (cannot be the way past)', pp.problems.some((x) => /3 of 4 views are declared diagnostic/.test(x)), pp.problems.join('|'));
pp = GL.pictureProblems(pf, cmpText('ok_aerial'), { minRoad: 0.5 }); t('picture_min_road override applies', pp.problems.some((x) => /need at least 50/.test(x)));

// ======================================================= end to end: a real git repo, a main checkout on a branch not called main, a worktree
console.log('check_handin end to end');
const main = path.join(tmp, 'main');
fs.mkdirSync(main);
git(main, 'init', '-q', '-b', 'trunk'); git(main, 'config', 'user.email', 't@t'); git(main, 'config', 'user.name', 't');
for (const f of ['aerial/a.jpg', 'street/b.jpg', 'gsv/c.jpg']) w(path.join(main, 'docs/reference', f), 'x');
w(path.join(main, 'tests/test_a.cpp'), 'TEST_CASE("alpha") {}\nTEST_CASE("beta" * doctest::should_fail()) {}\nbool is_rough(const std::string& n) { return n == "old_case"; }\nconstexpr double kTolHeight = 1.5e-4;\n');
w(path.join(main, 'sim_core/src/a.cpp'), 'int a;\n');
w(path.join(main, '.gitignore'), 'build*/\nnode_modules/\n');
git(main, 'add', '-A'); git(main, 'commit', '-qm', 'base');
const wt = path.join(tmp, 'wt');
git(main, 'worktree', 'add', '-q', '-b', 'agent/x', wt);
// main moves ON after the branch point (like the live main during a long round): its own later waiver must not be blamed on the branch
w(path.join(main, 'tests/test_b.cpp'), 'TEST_CASE("later" * doctest::should_fail()) {}\n'); git(main, 'add', '-A'); git(main, 'commit', '-qm', 'main moves on');
// fake scene check: prints a configurable result
const fakeScene = (passed, total, failing = []) => {
  const lines = [];
  for (let i = 0; i < total; i++) lines.push((i < total - (total - passed) ? 'PASS ' : 'FAIL ') + ('check_' + i).padEnd(24) + ' detail ' + i);
  w(path.join(wt, 'prototypes/road-draw/tools/scene.mjs'), `console.log(${JSON.stringify(lines.join('\n'))}); console.log('${passed}/${total} passed in 0.1s'); process.exit(${passed === total ? 0 : 1});\n`);
};
fs.mkdirSync(path.join(wt, 'prototypes/road-draw/node_modules'), { recursive: true });
const folder = path.join(wt, 'docs/progress/x');
const statusBase = (skip = 'Nothing skipped, refused or failed. I did not verify the Unreal side (not part of this round).') => `## Skipped, refused or failed\n\n${skip}\n\nReferences opened: 3 of 3 listed\n\n## Reference files opened\n\nCase: A | Opened: aerial/a.jpg, street/b.jpg, gsv/c.jpg\n`;
const faults3 = '## Pass 1\nthe kerb stops 4 m short of the island on the south arm\n## Pass 2\nthe give-way line is a stub, not a span across the whole arm\n## Pass 3\nthe hatching bars cross the kerb line at the island nose\n## Residual\nnone, all fixed\n';
w(path.join(folder, 'STATUS.md'), statusBase()); w(path.join(folder, 'FAULTS.md'), faults3);
const check = (flags = [], extra = {}) => spawnSync(process.execPath, [path.join(src, 'tools/check_handin.mjs'), folder, '--main', main, '--worktree', wt, '--no-visual', ...flags], { encoding: 'utf8', cwd: tmp, env: { ...process.env, CP_FROM_HOOK: '1' }, ...extra });
const SC = ['--gates-version', '2', '--gate', 'scene_check >= 20/21 (baseline 20/21, target 21/21)'];

let c = check(); t('v1 (no GATES-VERSION): the old checks only, passes', c.status === 0 && !/GATE/.test(c.stderr), c.stderr);
fakeScene(21, 21); c = check(SC); t('v2, scene 21/21 measured: passes and prints what was measured', c.status === 0 && /gate: measured scene_check 21\/21/.test(c.stdout), c.stdout + c.stderr);
fakeScene(20, 21); c = check(SC); t('scene 20/21 (at the floor, below target) without a STATUS line: refused, asks for the shortfall in STATUS', c.status === 1 && /below the target 21\/21/.test(c.stderr) && /Skipped, refused or failed/.test(c.stderr), c.stderr);
w(path.join(folder, 'STATUS.md'), statusBase('- scene_check 20/21 not 21/21: ground_hidden_by_walls still fails at the lower road end; tried a longer fade, blocked by the crossing road.'));
c = check(SC); t('scene 20/21 with the shortfall named in STATUS: passes', c.status === 0 && /below the target/.test(c.stdout), c.stdout + c.stderr);
fakeScene(17, 21);
w(path.join(folder, 'STATUS.md'), statusBase('Nothing skipped. Scene check 21/21 pass.'));
c = check(SC); t('STATUS claims 21/21 but the measurement is 17/21: refused, message says the typed number is not read and lists failing checks', c.status === 1 && /measured 17\/21/.test(c.stderr) && /not read/.test(c.stderr) && /FAIL|check_|failing/.test(c.stderr), c.stderr);
w(path.join(folder, 'STATUS.md'), statusBase());
fakeScene(21, 23); c = check(SC); t('the scene check grew to 23 checks but the gate says /21: refused (main session must update the gate)', c.status === 1 && /now has 23 checks/.test(c.stderr), c.stderr);
fs.rmSync(path.join(wt, 'prototypes/road-draw/node_modules'), { recursive: true }); fakeScene(21, 21);
c = check(SC); t('node_modules missing: cannot measure -> FAIL-OPEN printed (and logged), hand-in not refused for it', c.status === 0 && /FAIL-OPEN: gate "scene_check"/.test(c.stderr), c.stderr);
fs.mkdirSync(path.join(wt, 'prototypes/road-draw/node_modules'));

// ctest
const ctestLog = (failed, nTests = 2, mtime = null) => {
  const f = path.join(wt, 'build/Testing/Temporary/LastTest.log');
  let txt = '';
  for (let i = 1; i <= nTests; i++) txt += `${i}/${nTests} Testing: suite${i}\n[doctest] assertions: 100 | ${100 - (i === 1 ? failed : 0)} passed | ${i === 1 ? failed : 0} failed |\nTest ${failed && i === 1 ? 'Failed' : 'Passed'}.\n`;
  w(f, txt); if (mtime) fs.utimesSync(f, mtime, mtime);
};
const CT = ['--gates-version', '2', '--gate', 'ctest failed_assertions == 2'];
c = check(CT); t('ctest gate with no build dir: refused, says to build and run ctest', c.status === 1 && /no ctest result/.test(c.stderr), c.stderr);
ctestLog(2); c = check(CT); t('ctest result with exactly 2 failed assertions: passes', c.status === 0 && /measured ctest failed_assertions 2/.test(c.stdout), c.stdout + c.stderr);
ctestLog(5); c = check(CT); t('ctest result with 5 failed assertions: refused, shows measured value', c.status === 1 && /measured 5/.test(c.stderr), c.stderr);
ctestLog(2, 2, new Date(Date.now() - 3600 * 1000)); w(path.join(wt, 'sim_core/src/a.cpp'), 'int a2;\n'); c = check(CT); t('ctest result older than a source edit: refused (stale)', c.status === 1 && /older than the sources/.test(c.stderr), c.stderr);
ctestLog(2);

// waiver guard end to end
w(path.join(wt, 'tests/test_a.cpp'), 'TEST_CASE("alpha") {}\nTEST_CASE("beta" * doctest::should_fail()) {}\nTEST_CASE("gamma" * doctest::should_fail()) {}\nbool is_rough(const std::string& n) { return n == "old_case" || n == "new_case"; }\nconstexpr double kTolHeight = 1.0e-3;\n');
const WG = ['--gates-version', '2'];
c = check(WG);
t('waiver guard: new should_fail, new is_rough name and a loosened tolerance in the worktree: refused with one line each', c.status === 1 && /should_fail:gamma/.test(c.stderr) && /is_rough:new_case/.test(c.stderr) && /tolerance:kTolHeight/.test(c.stderr), c.stderr);
t('...and the waiver that main added AFTER the branch point ("later") is not blamed on the branch (merge base, not main tip)', !/later/.test(c.stderr), c.stderr);
t('...with the base taken from the main checkout HEAD even though no branch is called main', !/not a git worktree|no merge base/.test(c.stderr), c.stderr);
c = check([...WG, '--gate', 'allow_waiver should_fail:gamma', '--gate', 'allow_waiver is_rough:new_case', '--gate', 'allow_waiver tolerance:kTolHeight']); t('each finding allowed by a task-file allow_waiver line: passes', c.status === 0 && /3 allowed/.test(c.stdout), c.stderr + c.stdout);
c = check([...WG, '--gate', 'allow_waiver should_fail:gamma']); t('allowing only one of them: still refused for the others', c.status === 1 && /is_rough:new_case/.test(c.stderr) && /tolerance:kTolHeight/.test(c.stderr) && !/should_fail:gamma/.test(c.stderr), c.stderr);
w(path.join(wt, 'tests/test_a.cpp'), 'TEST_CASE("beta" * doctest::should_fail()) {}\nbool is_rough(const std::string& n) { return n == "old_case"; }\nconstexpr double kTolHeight = 1.5e-4;\n');
c = check(WG); t('a deleted test case (alpha): refused', c.status === 1 && /test case "alpha" was deleted/.test(c.stderr), c.stderr);
git(wt, 'checkout', '-q', '--', 'tests/test_a.cpp');
w(path.join(wt, 'tests/test_new.cpp'), 'TEST_CASE("fresh" * doctest::skip(true)) {}\n'); c = check(WG); t('an UNTRACKED new test file with a skip is read too', c.status === 1 && /skip:fresh/.test(c.stderr), c.stderr);
fs.rmSync(path.join(wt, 'tests/test_new.cpp'));
c = check(WG); t('clean worktree: waiver guard passes and says what it compared against', c.status === 0 && /no new waiver/.test(c.stdout), c.stderr + c.stdout);
w(path.join(wt, 'tests/test_a.cpp'), 'TEST_CASE("alpha") {}\nTEST_CASE("beta" * doctest::should_fail()) {}\nbool is_rough(const std::string& n) { return n == "old_case"; }\nconstexpr double kTolHeight = 1.5e-4;\nTEST_CASE("added" * doctest::should_fail()) {}\n'); git(wt, 'add', '-A'); git(wt, 'commit', '-qm', 'committed waiver');
c = check(WG); t('a waiver that is COMMITTED on the branch is found too (diff against the merge base, not the working tree only)', c.status === 1 && /should_fail:added/.test(c.stderr), c.stderr);
git(wt, 'reset', '-q', '--hard', 'HEAD~1');

// STATUS reading in a hand-in
w(path.join(folder, 'STATUS.md'), statusBase('- Not attempted: the S-curl at wall ends.\n- Self-audit: 2 passes, not 3 (usage window).'));
w(path.join(folder, 'FAULTS.md'), '## Pass 1\nthe kerb stops 4 m short of the island on the south arm\n## Pass 2\nthe give-way line is a stub, not a span across the whole arm\n## Residual\nthe curl\n');
c = check(WG); t('hand-in with "not attempted", 2 passes and a usage-window reason: refused for all three', c.status === 1 && /not attempted/i.test(c.stderr) && /usage\/time limit/.test(c.stderr) && /3 unless/.test(c.stderr), c.stderr);
fs.rmSync(path.join(folder, 'FAULTS.md')); w(path.join(folder, 'STATUS.md'), statusBase());
c = check(WG); t('v2 engine-only hand-in without FAULTS.md: refused (v1 does not ask)', c.status === 1 && /FAULTS\.md is missing/.test(c.stderr), c.stderr);
c = check(); t('...and v1 without FAULTS.md still passes (--no-visual)', c.status === 0, c.stderr);
w(path.join(folder, 'FAULTS.md'), faults3);

// picture gate in a visible hand-in
const vf = path.join(wt, 'docs/progress/vis'); w(path.join(vf, 'STATUS.md'), statusBase()); w(path.join(vf, 'FAULTS.md'), faults3);
for (const f of ['s6_street.png', 'comp.png', 'before.png']) w(path.join(vf, f), 'x');
const row = '| case | render | reference | composite | before | subject/type match | scale match | viewpoint match |\n|---|---|---|---|---|---|---|---|\n| s6 | s6_street.png | aerial/a.jpg | comp.png | before.png | embankment, same type as photo | both about 40 m across | street level, both |\n';
w(path.join(vf, 'COMPARE.md'), row);
const checkV = (flags = []) => spawnSync(process.execPath, [path.join(src, 'tools/check_handin.mjs'), vf, '--main', main, '--worktree', wt, ...flags], { encoding: 'utf8', cwd: tmp, env: { ...process.env, CP_FROM_HOOK: '1' } });
c = checkV(['--gates-version', '2']); t('visible v2 hand-in whose COMPARE render has no stats record: refused', c.status === 1 && /s6_street/.test(c.stderr) && /stats\.json/.test(c.stderr), c.stderr);
const shaX = crypto.createHash('sha1').update('x').digest('hex');
w(path.join(vf, 's6_street.stats.json'), JSON.stringify({ width: 1600, height: 900, diagnostic: false, road_px: 0, debug_px: 0, png_sha1: shaX }));
c = checkV(['--gates-version', '2']); t('...stats say no road in frame (round 9 s6 street): refused with the percentage', c.status === 1 && /covers 0\.00% of the frame/.test(c.stderr), c.stderr);
w(path.join(vf, 's6_street.stats.json'), JSON.stringify({ width: 1600, height: 900, diagnostic: false, road_px: 60000, debug_px: 0, png_sha1: shaX }));
c = checkV(['--gates-version', '2']); t('...with road in frame and no debug colour: passes', c.status === 0 && /picture s6_street: road 4\.2%/.test(c.stdout), c.stderr + c.stdout);
c = checkV(); t('the same folder under v1: 8-column COMPARE accepted too, no picture gate (older briefs unaffected)', c.status === 0 && !/picture/.test(c.stdout), c.stderr);
w(path.join(vf, 'COMPARE.md'), '| case | render | reference | composite | before |\n|---|---|---|---|---|\n| s6 | s6_street.png | aerial/a.jpg | comp.png | before.png |\n');
c = checkV(); t('v1: the old 5-column COMPARE.md still passes (running agents with the old layout are not refused)', c.status === 0, c.stderr);
c = checkV(['--gates-version', '2']); t('v2: the same 5-column COMPARE.md is refused (needs 8)', c.status === 1 && /needs 8 columns/.test(c.stderr), c.stderr);

// --task reads the Gates section of a task file
w(path.join(tmp, 'task_gates.md'), '# T\n## Gates\nscene_check >= 20/21 (baseline 20/21, target 21/21)\nNo new test waiver.\n');
fakeScene(17, 21); c = check(['--task', path.join(tmp, 'task_gates.md')]); t('--task <task-file> runs the Gates section lines (manual run)', c.status === 1 && /measured 17\/21/.test(c.stderr), c.stderr);

// fail open
c = check(['--gates-version', '2', '--gate', 'scene_check >= 20/21'], { env: { ...process.env, CP_FROM_HOOK: '1', PATH: '' } });
t('git missing from PATH: waiver guard fails open (printed), not a refusal for that', !/waiver guard \(/.test(c.stderr), c.stderr);

// ======================================================= make_brief
console.log('make_brief');
const tf = (extra = '', cases = 'case A | a.jpg b.jpg c.jpg') => { const p = path.join(tmp, 'task_' + Math.random().toString(36).slice(2) + '.md'); w(p, `# T\n## Owner's words\n"The photo is the target." (11 Oct)\n## Cases\n${cases}\n## Faults\nnone known\n## Launched\nx\n## Waiting\nnone\n${extra}`); return p; };
const mb = (p, ...a) => spawnSync(process.execPath, [path.join(src, 'tools/make_brief.mjs'), p, '--round', 'rr', '--worktree', path.join(tmp, 'wtrr'), ...a], { encoding: 'utf8' });
let b = mb(tf('## Gates\nscene_check >= 20/21 (baseline 20/21, target 21/21)\nctest failed_assertions == 2\nallow_waiver is_rough:foo\nNo new waiver in prose.\n'));
t('brief carries GATES-VERSION: 2 and one GATE: line per gate line (prose lines dropped)', b.status === 0 && /^GATES-VERSION: 2$/m.test(b.stdout) && /^GATE: scene_check >= 20\/21 \(baseline 20\/21, target 21\/21\)$/m.test(b.stdout) && /^GATE: ctest failed_assertions == 2$/m.test(b.stdout) && /^GATE: allow_waiver is_rough:foo$/m.test(b.stdout) && !/^GATE: No new/m.test(b.stdout), b.stdout.slice(0, 600) + b.stderr);
b = mb(tf()); t('no Gates section: still GATES-VERSION: 2 (waiver, picture and STATUS checks) and no GATE lines', b.status === 0 && /^GATES-VERSION: 2$/m.test(b.stdout) && !/^GATE:/m.test(b.stdout));
b = mb(tf('## Gates\nscene_check >= banana\n')); t('a malformed gate line is refused at brief time', b.status === 1 && /Gates/.test(b.stderr) && /scene_check >= banana/.test(b.stderr), b.stderr);
b = mb(tf('## Gates\nframe_rate >= 30 (baseline 30)\n')); t('an unknown metric is refused at brief time', b.status === 1 && /unknown metric/.test(b.stderr), b.stderr);
b = mb(tf('', 'Engine and tooling work, no renders: HANDIN: no-visual.\ncase A | a.jpg b.jpg c.jpg')); t('Cases say "no renders" without --no-visual: refused', b.status === 1 && /--no-visual/.test(b.stderr), b.stderr);
b = mb(tf('', 'Engine and tooling work, no renders: HANDIN: no-visual.\ncase A | a.jpg b.jpg c.jpg'), '--no-visual'); t('...with --no-visual: allowed, emits HANDIN: no-visual', b.status === 0 && /^HANDIN: no-visual$/m.test(b.stdout), b.stderr);
const four = 'Case 1: a\n- a.jpg\nCase 2: b\n- b.jpg\nCase 3: c\n- c.jpg\nCase 4: d\n- d.jpg\n';
b = mb(tf('', four)); t('4 cases without a Scope section: refused', b.status === 1 && /at most 3/.test(b.stderr), b.stderr);
b = mb(tf('## Scope\nFour cases because they are four views of the same single junction.\n', four)); t('4 cases with a Scope section of 20+ characters: allowed', b.status === 0, b.stderr);
b = mb(tf('', 'Case 1: a\n- a.jpg\nCase 2: b\n- b.jpg\nCase 3: c\n- c.jpg\n')); t('3 cases: allowed', b.status === 0, b.stderr);
b = mb(tf('Previous round: earthworks-r9\n')); t('a previous round named without "## Rounds" baseline numbers: refused', b.status === 1 && /## Rounds/.test(b.stderr), b.stderr);
const flat = '## Rounds\nr7: scene_check=20, ctest_failed_assertions=2\nr8: scene_check=20, ctest_failed_assertions=2\nr9: scene_check=17, ctest_failed_assertions=2\n';
b = mb(tf('Previous round: r9\n' + flat)); t('last two rounds did not improve: METHOD REVIEW FIRST, exit 1, no brief', b.status === 1 && /METHOD REVIEW FIRST/.test(b.stderr) && b.stdout === '', b.stderr);
b = mb(tf('Previous round: r9\n' + flat), '--override', 'owner asked for another round on the same method'); t('...overridden with a written reason (20+ chars): brief printed, override echoed', b.status === 0 && /overridden/.test(b.stderr), b.stderr);
b = mb(tf('Previous round: r9\n' + flat), '--override', 'short'); t('...an override under 20 characters does not count', b.status === 1);
b = mb(tf('Previous round: r9\n## Rounds\nr8: scene_check=18\nr9: scene_check=20\n')); t('numbers improving: allowed', b.status === 0, b.stderr);
b = mb(tf('Previous round: r9\n## Rounds\nr8: scene_check=18\nr9: prose only\n')); t('a Rounds line with no numbers: refused', b.status === 1 && /no numbers/.test(b.stderr), b.stderr);
t('the brief text mentions no usage limit to agents (scripts and templates are clean)', !/usage (limit|window)|token budget/i.test(b.stdout + fs.readFileSync(path.join(src, 'docs/templates/BRIEF.md'), 'utf8') + fs.readFileSync(path.join(src, 'tools/make_brief.mjs'), 'utf8').replace(/.*USAGE_RE.*\n/g, '')));
b = mb(tf()); t('the brief template tells the agent how to pass the gates (--gates-version 2, 8 columns, 3 passes, at most 3 cases)', /--gates-version 2/.test(b.stdout) && /3 passes/.test(b.stdout) && /at most 3 per round/.test(b.stdout), b.stdout.slice(0, 200));

// ======================================================= second pass: holes found in the self-audit
console.log('self-audit hardening');
{
  const kk = (d, allowed = []) => GL.waiverFindings(d, allowed).refused.map((f) => f.key);
  const dd = (file, removed, added) => `diff --git a/${file} b/${file}\n--- a/${file}\n+++ b/${file}\n@@ -1 +1 @@\n${removed.map((l) => '-' + l).join('\n')}${removed.length ? '\n' : ''}${added.map((l) => '+' + l).join('\n')}\n`;
  let q = kk(dd('tests/test_grading.cpp', ['    CHECK(worst < 0.3);'], ['    CHECK(worst < 0.5);']));
  t('a threshold edited on a CHECK line (worst < 0.3 -> 0.5): found', q.some((x) => x.startsWith('threshold:')), JSON.stringify(q));
  t('a threshold tightened (< 0.3 -> < 0.1): not found', kk(dd('tests/test_grading.cpp', ['    CHECK(worst < 0.3);'], ['    CHECK(worst < 0.1);'])).length === 0);
  q = kk(dd('tests/test_grading.cpp', ['    CHECK(cover > 0.9);'], ['    CHECK(cover > 0.5);'])); t('a ">" threshold lowered (> 0.9 -> 0.5): found', q.some((x) => x.startsWith('threshold:')), JSON.stringify(q));
  q = kk(dd('prototypes/road-draw/tools/scene.mjs', ["    results.push({ name: 'median_flat', pass: worst < 0.05, msg });"], ["    results.push({ name: 'median_flat', pass: worst < 1.5, msg });"])); t('scene.mjs: median_flat bound 0.05 -> 1.5: found', q.some((x) => x.startsWith('threshold:')), JSON.stringify(q));
  t('a line that changes shape (not a pure number edit) is not a "threshold" finding', kk(dd('tests/test_a.cpp', ['    CHECK(a < 3);'], ['    CHECK(b < 5);'])).length === 0);
  q = kk(dd('tests/test_markings.cpp', ['    CHECK(a == 1);', '    CHECK(b == 2);', '    REQUIRE(c);'], ['    CHECK(a == 1);'])); t('two CHECK/REQUIRE lines deleted from a test: found as fewer_checks', q.includes('fewer_checks:test_markings.cpp'), JSON.stringify(q));
  t('a CHECK moved within the file (same count): not found', kk(dd('tests/test_markings.cpp', ['    CHECK(a == 1);'], ['    CHECK(a == 1); // moved'])).length === 0);
  q = kk(dd('tests/test_markings.cpp', ['    CHECK(a == 1);'], ['    // CHECK(a == 1);'])); t('a CHECK commented out: found', q.includes('fewer_checks:test_markings.cpp'), JSON.stringify(q));
  q = kk(dd('tests/test_markings.cpp', [], ['#if 0', 'TEST_CASE("x") {}', '#endif'])); t('#if 0 added: found', q.some((x) => x.startsWith('if0:')), JSON.stringify(q));
  q = kk(dd('tests/CMakeLists.txt', ['    test_markings.cpp', '    test_lanes.cpp'], ['    test_lanes.cpp'])); t('a test source dropped from tests/CMakeLists.txt: found', q.includes('deleted_file:test_markings.cpp'), JSON.stringify(q));
  q = kk(`diff --git a/tests/test_gone.cpp b/tests/test_gone.cpp\ndeleted file mode 100644\n--- a/tests/test_gone.cpp\n+++ /dev/null\n@@ -1,2 +0,0 @@\n-TEST_CASE("gone one") {}\n-TEST_CASE("gone two") {}\n`);
  t('a whole test file deleted (+++ /dev/null): both test cases found, attributed to the file', q.includes('deleted_test:gone one') && q.includes('deleted_test:gone two') && GL.waiverFindings('--- a/tests/test_gone.cpp\n+++ /dev/null\n@@ -1 +0,0 @@\n-TEST_CASE("g") {}\n').findings[0].file === 'tests/test_gone.cpp', JSON.stringify(q));
  // STATUS reading: wording a builder in a hurry would use instead
  const ok3 = '## Pass 1\nthe kerb stops 4 m short\n## Pass 2\nthe line is a stub\n## Pass 3\nthe bars cross the kerb\n## Residual\nnone\n';
  for (const phrase of ['not done: the curl', 'deferred to a later round', 'postponed', 'I did not get to the pit', 'for the next round', 'not started', 'ran short of time', 'session limit reached', 'token limit', 'low on time']) {
    t('STATUS wording refused: "' + phrase + '"', GL.statusProblems('- ' + phrase + '\n', ok3).length >= 1, GL.statusProblems('- ' + phrase + '\n', ok3).join('|'));
  }
  t('ordinary words are not mistaken: "the work was not done by hand", only if it says so? (tolerated: "not done" is refused on purpose)', GL.statusProblems('- the wall height is rounded\n', ok3).length === 0);
  let sp = GL.statusProblems('ok\n', '## Pass 1\n\n## Pass 2\n\n## Pass 3\n\n## Residual\nnone\n'); t('three EMPTY "## Pass N" headings are not three passes', sp.some((x) => /empty or under 12/.test(x)) && sp.some((x) => /need 3/.test(x)), sp.join('|'));
  // stats bound to the picture bytes
  const pf3 = path.join(tmp, 'pic3'); fs.mkdirSync(pf3);
  w(path.join(pf3, 'v_aerial.png'), 'real picture bytes');
  const sha = (s) => crypto.createHash('sha1').update(s).digest('hex');
  const st = (o) => w(path.join(pf3, 'v_aerial.stats.json'), JSON.stringify({ width: 100, height: 100, diagnostic: false, road_px: 900, debug_px: 0, ...o }));
  st({ png_sha1: sha('real picture bytes') }); let pp2 = GL.pictureProblems(pf3, cmpText('v_aerial')); t('stats with the hash of the PNG next to it: passes', pp2.problems.length === 0, pp2.problems.join('|'));
  st({ png_sha1: sha('another picture') }); pp2 = GL.pictureProblems(pf3, cmpText('v_aerial')); t('stats made for another picture (hash differs): refused, says to render again', pp2.problems.some((x) => /not made for v_aerial\.png/.test(x)), pp2.problems.join('|'));
  st({}); pp2 = GL.pictureProblems(pf3, cmpText('v_aerial')); t('a hand-typed stats file without png_sha1: refused', pp2.problems.some((x) => /no png_sha1/.test(x)), pp2.problems.join('|'));
  w(path.join(pf3, 'v_aerial.png'), 'edited picture after rendering'); st({ png_sha1: sha('real picture bytes') }); pp2 = GL.pictureProblems(pf3, cmpText('v_aerial')); t('the PNG edited after the stats were written: refused', pp2.problems.some((x) => /hash differs/.test(x)), pp2.problems.join('|'));
  // --accept does not fail open
  fakeScene(21, 21);
  fs.rmSync(path.join(wt, 'prototypes/road-draw/node_modules'), { recursive: true });
  let ca = check([...SC, '--accept', '--builder', 'nobody']); t('--accept: a gate that could not be measured (no node_modules) is a refusal, not a FAIL-OPEN', ca.status === 1 && /GATE could not be measured \(scene_check\)/.test(ca.stderr), ca.stderr);
  fs.mkdirSync(path.join(wt, 'prototypes/road-draw/node_modules'));
  // method override is logged where the main session can see it (make_brief writes gate.log only in a main checkout, covered by the hook tests)
  const mbSrc = fs.readFileSync(path.join(src, 'tools/make_brief.mjs'), 'utf8'); t('make_brief logs the method-change override into gate.log', /method_override/.test(mbSrc));
}


console.log(`\n${pass} passed, ${fail} failed (temp dir ${tmp})`);
process.exit(fail ? 1 : 0);
