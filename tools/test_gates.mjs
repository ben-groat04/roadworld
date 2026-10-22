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
let pass = 0, fail = 0; const failed = [];
const t = (name, cond, detail = '') => { if (cond) { pass++; console.log('  ok   ' + name); } else { fail++; failed.push(name); console.log('  FAIL ' + name + (detail ? '\n       ' + String(detail).replace(/\n/g, '\n       ').slice(0, 900) : '')); } };
const git = (cwd, ...a) => { const r = spawnSync('git', a, { cwd, encoding: 'utf8' }); if (r.status !== 0) throw new Error('git ' + a.join(' ') + ': ' + r.stderr); return r.stdout.trim(); };
const w = (p, s) => { fs.mkdirSync(path.dirname(p), { recursive: true }); fs.writeFileSync(p, s); };
const mkPng = (wd, ht) => { const b = Buffer.alloc(33); Buffer.from([0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a]).copy(b, 0); b.writeUInt32BE(13, 8); b.write('IHDR', 12, 'latin1'); b.writeUInt32BE(wd, 16); b.writeUInt32BE(ht, 20); return b; };
import zlib from 'node:zlib';
// a real 8-bit RGBA PNG (filter 0), pixel colour from fn(x, y) -> [r, g, b]
const realPng = (wd, ht, fn) => {
  const raw = Buffer.alloc((wd * 4 + 1) * ht);
  for (let y = 0; y < ht; y++) for (let x = 0; x < wd; x++) { const [r, g, b] = fn(x, y); const o = y * (wd * 4 + 1) + 1 + x * 4; raw[o] = r; raw[o + 1] = g; raw[o + 2] = b; raw[o + 3] = 255; }
  const chunk = (type, data) => { const len = Buffer.alloc(4); len.writeUInt32BE(data.length); const body = Buffer.concat([Buffer.from(type, 'latin1'), data]); const crc = Buffer.alloc(4); crc.writeInt32BE(crc32(body)); return Buffer.concat([len, body, crc]); };
  const ihdr = Buffer.alloc(13); ihdr.writeUInt32BE(wd, 0); ihdr.writeUInt32BE(ht, 4); ihdr[8] = 8; ihdr[9] = 6;
  return Buffer.concat([Buffer.from([0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a]), chunk('IHDR', ihdr), chunk('IDAT', zlib.deflateSync(raw)), chunk('IEND', Buffer.alloc(0))]);
};
const crc32 = (buf) => { let c, crc = -1; for (let n = 0; n < buf.length; n++) { c = (crc ^ buf[n]) & 255; for (let k = 0; k < 8; k++) c = c & 1 ? (c >>> 1) ^ 0xedb88320 : c >>> 1; crc = (crc >>> 8) ^ c; } return ~crc; };
const sha1 = (buf) => crypto.createHash('sha1').update(buf).digest('hex');

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
sp = GL.statusProblems('ok\n', '## Pass 1\nthe kerb stops 4 m short of the island on the south arm\n## Pass 2\nthe give-way line is a stub, not a span across the whole arm\n## Residual\nthe pit remains\nResidual only unfixable: item 1, the pit needs a tunnel the engine cannot model and I tried a bridge and a wall.\n'); t('2 passes + a residual-only-unfixable line: allowed', sp.length === 0, sp.join('|'));
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
const stats = (name, o = {}, png = mkPng(o.width || 1600, o.height || 900)) => { w(path.join(pf, name + '.png'), png); w(path.join(pf, name + '.stats.json'), JSON.stringify({ view: name, width: 1600, height: 900, diagnostic: false, road_px: 60000, debug_px: 0, png_sha1: sha1(png), debug_by_class: { needs: 0, rock: 0 }, ...o })); };
const cmpText = (...views) => '| case | render |\n|---|---|\n' + views.map((v) => `| c | ${v}.png | r | c | b | x | y | z |`).join('\n') + '\n';
stats('ok_aerial'); stats('ok_street');
let pp = GL.pictureProblems(pf, cmpText('ok_aerial')); t('road in frame, no debug colour: passes', pp.problems.length === 0, pp.problems.join('|'));
stats('empty_street', { road_px: 0 }); pp = GL.pictureProblems(pf, cmpText('empty_street')); t('no road in frame (round 9 s6 street): refused, says the subject is not in the picture', pp.problems.some((x) => /empty_street/.test(x) && /not in the picture/.test(x)), pp.problems.join('|'));
stats('orange_aerial', { debug_px: 727680, debug_by_class: { needs: 397719, rock: 329961 } }); pp = GL.pictureProblems(pf, cmpText('ok_street'));
t('a debug-colour view in the folder is judged even when COMPARE.md does not list it (50% orange/rock, round 9 s4 aerial)', pp.problems.some((x) => /orange_aerial/.test(x) && /debug colours/.test(x)), pp.problems.join('|'));
stats('orange_aerial', { debug_px: 800, debug_by_class: { needs: 800, rock: 0 } }); pp = GL.pictureProblems(pf, cmpText('ok_street')); t('a few debug pixels (0.06%) are tolerated', !pp.problems.some((x) => /orange/.test(x)), pp.problems.join('|'));
stats('orange_aerial', { diagnostic: true, debug_px: 727680 }); pp = GL.pictureProblems(pf, cmpText('ok_street')); t('a headline-named view (*_aerial) declared diagnostic: REFUSED, rename it (round 9 s4 aerial could not hide behind the flag)', pp.problems.some((x) => /orange_aerial/.test(x) && /headline view/.test(x)), pp.problems.join('|'));
fs.rmSync(path.join(pf, 'orange_aerial.stats.json')); fs.rmSync(path.join(pf, 'orange_aerial.png'));
stats('wallface_close', { diagnostic: true, debug_px: 727680, road_px: 0 }); pp = GL.pictureProblems(pf, cmpText('ok_street'));
t('a diagnostic view with no explanation in STATUS.md: refused', pp.problems.some((x) => /wallface_close/.test(x) && /STATUS\.md has no line/.test(x)), pp.problems.join('|'));
pp = GL.pictureProblems(pf, cmpText('ok_street'), { statusText: '- wallface_close is a diagnostic view of the retaining wall face only, no road in frame by design\n' }); t('...explained in STATUS.md (name, "diagnostic", a reason): exempt from the road and debug checks', !pp.problems.some((x) => /wallface_close/.test(x)), pp.problems.join('|'));
pp = GL.pictureProblems(pf, cmpText('wallface_close'), { statusText: 'wallface_close diagnostic view of the wall face only for the section' }); t('...but cannot be listed in COMPARE.md', pp.problems.some((x) => /diagnostic but is listed/.test(x)), pp.problems.join('|'));
fs.rmSync(path.join(pf, 'wallface_close.stats.json')); fs.rmSync(path.join(pf, 'wallface_close.png'));
w(path.join(pf, 'ghost.png'), mkPng(1600, 900)); pp = GL.pictureProblems(pf, cmpText('ghost')); t('a COMPARE.md render with no stats file: refused, names render.mjs', pp.problems.some((x) => /ghost/.test(x) && /render\.mjs/.test(x)), pp.problems.join('|'));
fs.rmSync(path.join(pf, 'ghost.png'));
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
  for (let i = 1; i <= nTests; i++) txt += `${i}/${nTests} Testing: suite${i}\n${i}/${nTests} Test: suite${i}\n[doctest] test cases: 10 | 10 passed | 0 failed | 0 skipped\n[doctest] assertions: 100 | ${100 - (i === 1 ? failed : 0)} passed | ${i === 1 ? failed : 0} failed |\nTest ${failed && i === 1 ? 'Failed' : 'Passed'}.\n`;
  w(path.join(wt, 'build/tests/CTestTestfile.cmake'), Array.from({ length: nTests }, (_, i) => `add_test(suite${i + 1} "x")`).join('\n') + '\n');
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
for (const f of ['comp.png', 'before.png']) w(path.join(vf, f), 'x');
w(path.join(vf, 's6_street.png'), mkPng(1600, 900));
const row = '| case | render | reference | composite | before | subject/type match | scale match | viewpoint match |\n|---|---|---|---|---|---|---|---|\n| s6 | s6_street.png | aerial/a.jpg | comp.png | before.png | embankment, same type as photo | both about 40 m across | street level, both |\n';
w(path.join(vf, 'COMPARE.md'), row);
const checkV = (flags = []) => spawnSync(process.execPath, [path.join(src, 'tools/check_handin.mjs'), vf, '--main', main, '--worktree', wt, ...flags], { encoding: 'utf8', cwd: tmp, env: { ...process.env, CP_FROM_HOOK: '1' } });
c = checkV(['--gates-version', '2']); t('visible v2 hand-in whose COMPARE render has no stats record: refused', c.status === 1 && /s6_street/.test(c.stderr) && /stats\.json/.test(c.stderr), c.stderr);
const shaX = sha1(mkPng(1600, 900));
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
let b = mb(tf('## Gates\nscene_check >= 20/21 (baseline 20/21, target 21/21)\nctest failed_assertions <= 2\nallow_waiver is_rough:foo\nNo new waiver in prose.\n'));
t('brief carries GATES-VERSION: 2 and one GATE: line per gate line (prose lines dropped)', b.status === 0 && /^GATES-VERSION: 2$/m.test(b.stdout) && /^GATE: scene_check >= 20\/21 \(baseline 20\/21, target 21\/21\)$/m.test(b.stdout) && /^GATE: ctest failed_assertions <= 2$/m.test(b.stdout) && /^GATE: ctest failed_tests <= 2$/m.test(b.stdout) && /^GATE: allow_waiver is_rough:foo$/m.test(b.stdout) && !/^GATE: No new/m.test(b.stdout), b.stdout.slice(0, 600) + b.stderr);
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
  const ok3 = '## Pass 1\nthe kerb stops 4 m short of the island on the south arm\n## Pass 2\nthe line is a stub and not a span across the arm\n## Pass 3\nthe bars cross the kerb line at the island nose\n## Residual\nnone\n';
  for (const phrase of ['not done: the curl', 'deferred to a later round', 'postponed', 'I did not get to the pit', 'for the next round', 'not started', 'ran short of time', 'session limit reached', 'token limit', 'low on time']) {
    t('STATUS wording refused: "' + phrase + '"', GL.statusProblems('- ' + phrase + '\n', ok3).length >= 1, GL.statusProblems('- ' + phrase + '\n', ok3).join('|'));
  }
  t('ordinary STATUS text is not mistaken for an excuse', GL.statusProblems('- the wall height is rounded\n- the kerb radius is 8 m\n- nothing skipped, refused or failed\n', ok3).length === 0);
  let sp = GL.statusProblems('ok\n', '## Pass 1\n\n## Pass 2\n\n## Pass 3\n\n## Residual\nnone\n'); t('three EMPTY "## Pass N" headings are not three passes', sp.some((x) => /empty, filler or under 40/.test(x)) && sp.some((x) => /need 3/.test(x)), sp.join('|'));
  // stats bound to the picture bytes
  const pf3 = path.join(tmp, 'pic3'); fs.mkdirSync(pf3);
  const realPng = mkPng(800, 450); w(path.join(pf3, 'v_aerial.png'), realPng);
  const sha = (s) => crypto.createHash('sha1').update(s).digest('hex');
  const st = (o) => w(path.join(pf3, 'v_aerial.stats.json'), JSON.stringify({ width: 800, height: 450, diagnostic: false, road_px: 9000, debug_px: 0, ...o }));
  st({ png_sha1: sha(realPng) }); let pp2 = GL.pictureProblems(pf3, cmpText('v_aerial')); t('stats with the hash of the PNG next to it: passes', pp2.problems.length === 0, pp2.problems.join('|'));
  st({ png_sha1: sha('another picture') }); pp2 = GL.pictureProblems(pf3, cmpText('v_aerial')); t('stats made for another picture (hash differs): refused, says to render again', pp2.problems.some((x) => /not made for v_aerial\.png/.test(x)), pp2.problems.join('|'));
  st({}); pp2 = GL.pictureProblems(pf3, cmpText('v_aerial')); t('a hand-typed stats file without png_sha1: refused', pp2.problems.some((x) => /no png_sha1/.test(x)), pp2.problems.join('|'));
  w(path.join(pf3, 'v_aerial.png'), mkPng(800, 451)); st({ png_sha1: sha(realPng) }); pp2 = GL.pictureProblems(pf3, cmpText('v_aerial')); t('the PNG edited after the stats were written: refused', pp2.problems.some((x) => /hash differs/.test(x)), pp2.problems.join('|'));
  // --accept does not fail open
  fakeScene(21, 21);
  fs.rmSync(path.join(wt, 'prototypes/road-draw/node_modules'), { recursive: true });
  let ca = check([...SC, '--accept', '--builder', 'nobody']); t('--accept: a gate that could not be measured (no node_modules) is a refusal, not a FAIL-OPEN', ca.status === 1 && /GATE could not be measured \(scene_check\)/.test(ca.stderr), ca.stderr);
  fs.mkdirSync(path.join(wt, 'prototypes/road-draw/node_modules'));
  // method override is logged where the main session can see it (make_brief writes gate.log only in a main checkout, covered by the hook tests)
  const mbSrc = fs.readFileSync(path.join(src, 'tools/make_brief.mjs'), 'utf8'); t('make_brief logs the method-change override into gate.log', /method_override/.test(mbSrc));
}


// ======================================================= gates3: the sixteen items of the gates2 audit, each with its own test
console.log('waiver guard: the cheap evasions (gates2 audit fault 6)');
{
  const dd = (file, removed, added) => `diff --git a/${file} b/${file}\n--- a/${file}\n+++ b/${file}\n@@ -1 +1 @@\n${removed.map((l) => '-' + l).join('\n')}${removed.length ? '\n' : ''}${added.map((l) => '+' + l).join('\n')}\n`;
  const kk = (d, allowed = [], contents = null) => GL.waiverFindings(d, allowed, contents).refused.map((f) => f.key);
  const T = 'tests/test_a.cpp';
  const has = (k, prefix) => k.some((x) => x.startsWith(prefix));
  let k = kk(dd(T, ['    CHECK(a == 1);'], ['    CHECK(a == 1 || true);'])); t('"|| true" added to a check: found', has(k, 'or_true:'), JSON.stringify(k));
  k = kk(dd(T, ['    CHECK(a == 1);'], ['    CHECK(true);'])); t('CHECK(true) swapped in for a real check: found', has(k, 'trivial_check:'), JSON.stringify(k));
  k = kk(dd(T, ['    REQUIRE(a == 1);'], ['    REQUIRE(1);'])); t('REQUIRE(1): found', has(k, 'trivial_check:'), JSON.stringify(k));
  k = kk(dd(T, ['    CHECK(a);', '    CHECK(b);'], ['    /* CHECK(a);', '    CHECK(b); */']), [], { [T]: { old: '    CHECK(a);\n    CHECK(b);\n', new: '    /* CHECK(a);\n    CHECK(b); */\n' } });
  t('two checks block-commented with /* */ (the diff alone still shows them): found as fewer_checks from the file contents', has(k, 'fewer_checks:'), JSON.stringify(k));
  k = kk(dd(T, ['    CHECK(a);'], ['    #if 0', '    CHECK(a);', '    #endif']), [], { [T]: { old: '    CHECK(a);\n', new: '    #if 0\n    CHECK(a);\n    #endif\n' } });
  t('a check wrapped in #if 0: found (if0 and fewer_checks)', has(k, 'if0:') && has(k, 'fewer_checks:'), JSON.stringify(k));
  k = kk(dd(T, ['    CHECK(a);'], ['    if (false) { CHECK(a); }']), [], { [T]: { old: '    CHECK(a);\n', new: '    if (false) { CHECK(a); }\n' } });
  t('a check inside if (false): found (dead_branch and fewer_checks)', has(k, 'dead_branch:') && has(k, 'fewer_checks:'), JSON.stringify(k));
  k = kk(dd(T, [], ['#ifdef NEVER_DEFINED', 'CHECK(a);', '#endif'])); t('#ifdef NEVER added: found', has(k, 'preproc:'), JSON.stringify(k));
  k = kk(dd(T, [], ['#ifndef SOME_FLAG', '#endif'])); t('#ifndef added: found', has(k, 'preproc:'), JSON.stringify(k));
  k = kk(dd(T, ['    CHECK(x == doctest::Approx(1.0).epsilon(0.001));'], ['    CHECK(x == doctest::Approx(1.0).epsilon(0.5));'])); t('Approx epsilon widened 0.001 -> 0.5: found', has(k, 'approx:'), JSON.stringify(k));
  k = kk(dd(T, [], ['    CHECK(x == doctest::Approx(1.0).margin(0.3));'])); t('a new Approx margin: found', has(k, 'approx:'), JSON.stringify(k));
  t('Approx epsilon tightened: not found', kk(dd(T, ['    CHECK(x == doctest::Approx(1.0).epsilon(0.1));'], ['    CHECK(x == doctest::Approx(1.0).epsilon(0.01));'])).filter((x) => x.startsWith('approx:')).length === 0);
  k = kk(dd(T, [], ['    return;'])); t('an early bare `return;` added in a test: found', has(k, 'early_return:'), JSON.stringify(k));
  t('`return;` in a JS file is not the C++ early-return waiver', kk(dd('prototypes/road-draw/test-geom.mjs', [], ['  return;'])).filter((x) => x.startsWith('early_return')).length === 0);
  k = kk(dd(T, [], ['#define CHECK(x) (void)0'])); t('#define CHECK(x) (void)0: found', has(k, 'macro_redefined:'), JSON.stringify(k));
  k = kk(dd(T, ['    CHECK(n == 5);'], ['    CHECK(n == 6);'])); t('an edited expected value (5 -> 6): found, with the rule behind it in the message', has(k, 'expected_value:'), JSON.stringify(k));
  t('...and the message says a photo measurement or written rule is needed', GL.waiverFindings(dd(T, ['    CHECK(n == 5);'], ['    CHECK(n == 6);'])).refused[0].what.includes('photo measurement'));
  t('...allowed by the prefix "expected_value:test_a.cpp" in the task file', kk(dd(T, ['    CHECK(n == 5);'], ['    CHECK(n == 6);']), ['expected_value:test_a.cpp']).length === 0);
  k = kk(dd(T, [], ['    if (b) CHECK(a == 1);'])); t('a check moved under an if on the same line: found', has(k, 'guarded_check:'), JSON.stringify(k));
  k = kk(dd('tests/CMakeLists.txt', ['    test_markings.cpp', '    test_lanes.cpp'], ['    #test_markings.cpp', '    test_lanes.cpp'])); t('a CMake source commented out with # (the name is still in the text): found', k.includes('deleted_file:test_markings.cpp'), JSON.stringify(k));
  k = kk(dd('tests/CMakeLists.txt', ['    test_markings.cpp'], ['    test_markings.cpp # still here'])); t('a CMake source with a trailing comment but still listed: not found', !k.includes('deleted_file:test_markings.cpp'), JSON.stringify(k));
  k = kk(dd('tests/CMakeLists.txt', [], ['set_tests_properties(sim_core_tests PROPERTIES LABELS "skip")'])); t('LABELS added to a ctest test: found', has(k, 'ctest_labels:'), JSON.stringify(k));
  k = kk(dd(T, ['    CHECK(w < 0.3);'], ['    int extra = 1;', '    CHECK(w < 0.9);'])); t('a loosened threshold plus an extra added line in the same hunk (shapes differ): found', has(k, 'threshold:'), JSON.stringify(k));
  k = kk(dd(T, ['    CHECK(w < 0.3);'], ['    CHECK(w < 0.3 * 3);'])); t('0.3 -> 0.3 * 3 (an expression is evaluated): found', has(k, 'threshold:'), JSON.stringify(k));
  k = kk(dd(T, ['    CHECK(n == 0);'], ['    CHECK(n <= 5);'])); t('== 0 -> <= 5: found', has(k, 'threshold:'), JSON.stringify(k));
  k = kk(dd(T, ['    CHECK(n > 5);'], ['    CHECK(n > -5);'])); t('> 5 -> > -5 (the sign is read): found', has(k, 'threshold:'), JSON.stringify(k));
  k = kk(dd(T, ['    CHECK(n < 5);'], ['    CHECK(n <= 5);'])); t('< 5 -> <= 5 (the same bound made inclusive): found', has(k, 'threshold:'), JSON.stringify(k));
  k = kk(dd(T, ['    for (int i = 0; i < 100; ++i) {'], ['    for (int i = 0; i < 10; ++i) {'])); t('a loop bound narrowed 100 -> 10: found', has(k, 'loop_bound:'), JSON.stringify(k));
  t('a threshold tightened (< 0.3 -> < 0.1, > 5 -> > 9, <= 5 -> == 5): not found', ['    CHECK(w < 0.1);', '    CHECK(w > 9);'].every((n, i) => kk(dd(T, [i ? '    CHECK(w > 5);' : '    CHECK(w < 0.3);'], [n])).length === 0) && kk(dd(T, ['    CHECK(n <= 5);'], ['    CHECK(n == 5);'])).filter((x) => x.startsWith('threshold')).length === 0);
  k = kk(dd(T, [], ['    std::vector<int> v;', '    std::map<int, int> m = {};'])); t('template brackets and plain code are not read as comparisons', k.length === 0, JSON.stringify(k));
  t('WAIVER_PATHS now watch the Unreal test folders, prototype tests and the gates\' own tests', ['unreal', 'prototypes/*/test', 'tools/test_gates.mjs', 'tools/hooks/test_hooks.mjs'].every((x) => GL.WAIVER_PATHS.some((p) => p.includes(x))), JSON.stringify(GL.WAIVER_PATHS));
  // the real collector supplies old and new contents of every changed guarded code file
  w(path.join(wt, 'tests/test_a.cpp'), 'TEST_CASE("alpha") {\n  CHECK(1 == 1);\n}\n');
  const gd = GL.collectWaiverDiff(wt);
  t('collectWaiverDiff returns the merge-base content and the working content of a changed guarded file', gd.contents && gd.contents['tests/test_a.cpp'] && /alpha/.test(gd.contents['tests/test_a.cpp'].old) && /CHECK\(1 == 1\)/.test(gd.contents['tests/test_a.cpp'].new), JSON.stringify(gd.contents));
  git(wt, 'checkout', '-q', '--', 'tests/test_a.cpp');
}

console.log('STATUS reading: excuses, honest blocks, false positives (gates2 audit fault 7)');
{
  const ok3 = '## Pass 1\nthe kerb stops 4 m short of the island on the south arm\n## Pass 2\nthe line is a stub and not a span across the arm\n## Pass 3\nthe bars cross the kerb line at the island nose\n## Residual\nnone\n';
  const bad = (txt) => GL.statusProblems('## Skipped, refused or failed\n' + txt + '\n', ok3);
  for (const phrase of ['not implemented', 'not finished', 'still open', 'out of scope', 'skipped the curl', 'TODO: curl', 'a follow-up for the curl', 'future work', 'ran short of tokens', 'usage cap hit', 'usage allowance used up', 'not enough time for the pit', 'no time for the pit', 'time pressure', 'token budget gone', 'untouched', 'not addressed', 'not fixed', "wasn't attempted", 'wasn\u2019t attempted', 'would need a bigger change, later round', 'left for later', 'will do next round', 'budget ran out', 'could not finish the curl', 'unable to implement the pit', 'gave up on the curl', 'the pit is unfinished', 'the kerb is partly done', 'half implemented']) {
    t('excuse refused: "' + phrase + '"', bad('- ' + phrase).length >= 1, bad('- ' + phrase).join('|'));
  }
  for (const okp of ['deferred shading is used for the sky dome', 'a time constraint of 5 ms per frame applies to the tile build', 'the API rate limit is 100 requests per minute', 'the context window of the camera is 90 degrees', 'not done by design: the road ends at the island', 'Nothing skipped, refused or failed.', 'no tests were skipped', 'the engine cannot model a tunnel under a river in one mesh', 'the scene check failed to find node_modules and I installed it', 'incomplete data in the fixture is handled by a default']) {
    t('honest text is not mistaken for an excuse: "' + okp + '"', bad('- ' + okp).length === 0, bad('- ' + okp).join('|'));
  }
  const blk = 'BLOCKED: wall curl at the road ends | tried: a ramp, a fillet and a longer fade, three renders | blocked by: the crossing road keeps the ground 2 m high (see s4 render) | needs: a decision on the crossing level';
  t('a complete BLOCKED line passes the status reader', bad(blk).length === 0, bad(blk).join('|'));
  t('...even when its own words would be an excuse elsewhere ("not done", "skipped")', bad('BLOCKED: skipped curl | tried: the fade and the ramp, not done yet | blocked by: the crossing road holds the ground at 2 m | needs: a decision').length === 0);
  t('BLOCKED without "tried:" is refused and the message shows the form', bad('BLOCKED: wall curl | blocked by: the crossing road keeps the ground high').some((x) => /tried/.test(x) && /BLOCKED: <item>/.test(x)));
  t('BLOCKED without "blocked by:" is refused', bad('BLOCKED: wall curl | tried: a ramp and a longer fade').some((x) => /blocked by/.test(x)));
  t('BLOCKED with "blocked by: time" is refused (an excuse in the form of a block)', bad('BLOCKED: wall curl | tried: a ramp and a fillet | blocked by: time').some((x) => /not a block|too short/.test(x)));
  t('BLOCKED with "blocked by: the usage window ran out" is refused', bad('BLOCKED: wall curl | tried: a ramp and a fillet | blocked by: the usage window ran out of room').some((x) => /not a block/.test(x)));
  t('BLOCKED with "blocked by: nothing" is refused', bad('BLOCKED: wall curl | tried: a ramp and a fillet | blocked by: nothing').length >= 1);
  t('the old exemption "tried ...; blocked by nothing" no longer hides a not-attempted item', bad('Not attempted: item 4 (tried: it; blocked by nothing)').length >= 1);
  t('parseBlocked returns item, tried, blocked by, needs', (() => { const b = GL.parseBlocked(blk)[0]; return b.ok && b.item === 'wall curl at the road ends' && /ramp/.test(b.tried) && /crossing road/.test(b.blockedBy) && /decision/.test(b.needs); })());
  t('a BLOCKED line in an HTML comment (the template) is not read', GL.parseBlocked('<!-- BLOCKED: x | tried: y | blocked by: z -->').length === 0);
  // partial reads
  const stPart = '## Skipped, refused or failed\n- hooks, scene.mjs and the audit read partly (heads and greps)\n\nReferences opened: 12 of 12 listed\n\n## Reference files opened\n';
  t('"12 of 12 opened" next to "read partly": refused (a partial read counts as not opened)', GL.statusProblems(stPart, ok3).some((x) => /partial read counts as not opened/.test(x)));
  t('"11 of 12" with a not-opened line is not touched by that rule', !GL.statusProblems(stPart.replace('12 of 12', '11 of 12'), ok3).some((x) => /partial read/.test(x)));
  // residual-only line names items; pass filler
  const two = '## Pass 1\nthe kerb stops 4 m short of the island on the south arm\n## Pass 2\nthe line is a stub and not a span across the arm\n## Residual\n1. the pit\n';
  t('"Residual only unfixable:" that names no item: refused', GL.statusProblems('ok\n', two + 'Residual only unfixable: the pit needs a tunnel the engine cannot model at all\n').some((x) => /must name the residual items/.test(x)));
  t('"Residual only unfixable:" that names item 1: accepted', GL.statusProblems('ok\n', two + 'Residual only unfixable: item 1, the pit needs a tunnel the engine cannot model at all\n').length === 0);
  t('a Pass body of filler ("n/a", "none", a dozen characters) is not a pass', GL.statusProblems('ok\n', '## Pass 1\nn/a n/a n/a n/a n/a n/a n/a n/a n/a n/a n/a\n## Pass 2\nnone\n## Pass 3\ntwelve chars\n## Residual\nnone\n').some((x) => /empty, filler/.test(x)));
  t('a BLOCKED self-audit item lets fewer than 3 passes through', GL.statusProblems('BLOCKED: the self-audit passes 2 and 3 | tried: a render of both views and a composite | blocked by: the viewer exits with code 4 on launch (log in scratch) | needs: a working GPU\n', two).length === 0);
}

console.log('prior audit items table (contract item 17)');
{
  const prior = '## Faults\nx\n## For the next builder\n\n1. fix the escape\n2. fix the picture gate\n3. fix the ctest gate\n\n## Exclusions\nnone\n';
  const head = '## Skipped, refused or failed\nok\n\n## Prior audit items\n\n| Item | Status | Proof |\n|---|---|---|\n';
  const rows = ['| 1 | fixed | tools/test_gates.mjs "accept with a typed fake id" |', '| 2 | fixed | test "picture gate finds records in after/" passes |', '| 3 | open (b) | the audit read ctest -N; my own check in build/ shows add_test count 1 and the log covers it, numbers in FAULTS.md |'];
  t('a complete table: no problems', GL.priorAuditProblems(head + rows.join('\n') + '\n', prior).length === 0, GL.priorAuditProblems(head + rows.join('\n') + '\n', prior).join('|'));
  t('no "Prior audit items" section at all: refused', GL.priorAuditProblems('## Skipped, refused or failed\nok\n', prior)[0].includes('no "## Prior audit items" section'));
  t('fewer rows than audit items: refused', GL.priorAuditProblems(head + rows.slice(0, 2).join('\n') + '\n', prior).some((x) => /2 row\(s\) but the previous AUDIT.md lists 3/.test(x)));
  t('"fixed" without proof: refused', GL.priorAuditProblems(head + ['| 1 | fixed | done |', rows[1], rows[2]].join('\n') + '\n', prior).some((x) => /needs the test, run or image/.test(x)));
  t('"open" without reason (a) or (b): refused', GL.priorAuditProblems(head + [rows[0], rows[1], '| 3 | open | later round, needs a bigger change to the build system than there is room for here |'].join('\n') + '\n', prior).some((x) => /reason \(a\)/.test(x)));
  t('a status that is neither fixed nor open: refused', GL.priorAuditProblems(head + [rows[0], rows[1], '| 3 | skipped | none |'].join('\n') + '\n', prior).some((x) => /must start with "fixed" or "open"/.test(x)));
  // end to end through check_handin --prior-audit
  const pa = path.join(tmp, 'prior_AUDIT.md'); w(pa, prior);
  w(path.join(folder, 'STATUS.md'), statusBase());
  c = check(['--gates-version', '2', '--prior-audit', pa]); t('check_handin --prior-audit: a STATUS without the table is refused', c.status === 1 && /GATE prior audit/.test(c.stderr), c.stderr);
  w(path.join(folder, 'STATUS.md'), statusBase() + '\n' + head.replace('## Skipped, refused or failed\nok\n\n', '') + rows.join('\n') + '\n');
  c = check(['--gates-version', '2', '--prior-audit', pa]); t('...and with the table: passes', c.status === 0, c.stderr);
  w(path.join(folder, 'STATUS.md'), statusBase());
}

console.log('picture gate: layouts, forged records, diagnostic flags (gates2 audit fault 2)');
{
  const root3 = path.join(tmp, 'picgate'); fs.mkdirSync(root3);
  const put = (rel, o = {}, png = mkPng(o.width || 800, o.height || 450)) => { w(path.join(root3, rel + '.png'), png); w(path.join(root3, rel + '.stats.json'), JSON.stringify({ view: path.basename(rel), width: 800, height: 450, diagnostic: false, road_px: 30000, debug_px: 0, png_sha1: sha1(png), ...o })); };
  const cmpFor = (...paths) => '| case | render |\n|---|---|\n' + paths.map((p) => `| c | ${p} | r | c | b | x | y | z |`).join('\n') + '\n';
  put('after/t_aerial'); put('after/t_street'); put('before/t_aerial', { road_px: 0 });
  let pp = GL.pictureProblems(root3, cmpFor('after/t_aerial.png', 'after/t_street.png'));
  t('the real tjunction layout (renders and stats in after/ and before/): passes; before/ history is not judged', pp.problems.length === 0, pp.problems.join('|'));
  t('...and both after/ views were measured', pp.notes.length === 2, JSON.stringify(pp.notes));
  fs.rmSync(path.join(root3, 'after/t_street.stats.json'));
  pp = GL.pictureProblems(root3, cmpFor('after/t_aerial.png', 'after/t_street.png')); t('a COMPARE render in after/ without its record beside it: refused, names the file', pp.problems.some((x) => /after\/t_street\.png/.test(x) && /t_street\.stats\.json/.test(x)), pp.problems.join('|'));
  put('after/t_street', { png_sha1: 'f'.repeat(40) }); pp = GL.pictureProblems(root3, cmpFor('after/t_street.png')); t('a typed hash that does not match the PNG: refused', pp.problems.some((x) => /hash differs/.test(x)), pp.problems.join('|'));
  put('after/t_street'); fs.rmSync(path.join(root3, 'after/t_street.png'));
  pp = GL.pictureProblems(root3, cmpFor('after/t_aerial.png')); t('a stats record with NO png beside it (typed hash, picture elsewhere): refused', pp.problems.some((x) => /has no t_street\.png beside it/.test(x)), pp.problems.join('|'));
  fs.rmSync(path.join(root3, 'after/t_street.stats.json'));
  put('after/big_street', { road_px: 400000 }); pp = GL.pictureProblems(root3, cmpFor('after/big_street.png')); t('road_px larger than the 800x450 frame: refused as not fitting the frame', pp.problems.some((x) => /do not fit the 800x450 frame/.test(x)), pp.problems.join('|'));
  put('after/big_street', { debug_px: 360000, road_px: 30000 }); pp = GL.pictureProblems(root3, cmpFor('after/big_street.png')); t('road + debug together larger than the frame: refused', pp.problems.some((x) => /do not fit/.test(x)), pp.problems.join('|'));
  put('after/big_street', { width: 640, height: 480 }, mkPng(800, 450)); w(path.join(root3, 'after/big_street.stats.json'), JSON.stringify({ width: 640, height: 480, diagnostic: false, road_px: 30000, debug_px: 0, png_sha1: sha1(mkPng(800, 450)) }));
  pp = GL.pictureProblems(root3, cmpFor('after/big_street.png')); t('stats width/height that disagree with the PNG header: refused', pp.problems.some((x) => /says 640x480 but the PNG is 800x450/.test(x)), pp.problems.join('|'));
  fs.rmSync(path.join(root3, 'after/big_street.stats.json')); fs.rmSync(path.join(root3, 'after/big_street.png'));
  put('after/hidden_aerial', { diagnostic: true, road_px: 0, debug_px: 400000 });
  pp = GL.pictureProblems(root3, cmpFor('after/t_aerial.png'), { statusText: 'hidden_aerial is a diagnostic view of nothing in particular, listed here' }); t('round 9\'s s4 aerial flagged diagnostic and not in COMPARE.md: still refused (a headline-named view cannot be diagnostic)', pp.problems.some((x) => /hidden_aerial/.test(x) && /headline view/.test(x)), pp.problems.join('|'));
  put('after/section_close', { diagnostic: true, road_px: 0 });
  pp = GL.pictureProblems(root3, cmpFor('after/t_aerial.png'), { statusText: 'section_close: diagnostic view of the wall face, no road in frame on purpose, for the section sheet' });
  t('a diagnostic close-up explained in STATUS.md (name + reason): accepted', !pp.problems.some((x) => /section_close/.test(x)), pp.problems.join('|'));
  t('the thresholds are documented in the source with their origin (no photo behind them, stated)', /no photo measurement stands behind these numbers/.test(fs.readFileSync(path.join(src, 'tools/gates_lib.mjs'), 'utf8')));
  // scene.mjs writes the same record (task C named scene.mjs)
  const sceneSrc = fs.readFileSync(path.join(src, 'prototypes/road-draw/tools/scene.mjs'), 'utf8');
  t('scene.mjs writes <view>.stats.json next to each render (png_sha1, road_px, debug_px, width, height)', /stats\.json/.test(sceneSrc) && /png_sha1/.test(sceneSrc) && /road_px/.test(sceneSrc) && /debug_px/.test(sceneSrc) && /writeStats\(name\)/.test(sceneSrc));
}

console.log('picture plausibility: size and flat pictures');
{
  const pq = path.join(tmp, 'picq'); fs.mkdirSync(pq);
  const put = (name, png, o = {}) => { w(path.join(pq, name + '.png'), png); w(path.join(pq, name + '.stats.json'), JSON.stringify({ width: o.width || 800, height: o.height || 450, diagnostic: false, road_px: o.road_px ?? 30000, debug_px: 0, png_sha1: sha1(png) })); };
  const cm = (n) => '| c | ' + n + '.png | r | c | b | x | y | z |';
  put('real_aerial', realPng(800, 450, (x, y) => [(x * 7) & 255, (y * 5) & 255, ((x + y) * 3) & 255]));
  let pp = GL.pictureProblems(pq, '| case | render |\n|---|---|\n' + cm('real_aerial') + '\n'); t('a decodable 800x450 picture with many colours: passes', pp.problems.length === 0, pp.problems.join('|'));
  t('pngColourCount decodes the writer\'s PNG', GL.pngColourCount(path.join(pq, 'real_aerial.png')).decoded === true && GL.pngColourCount(path.join(pq, 'real_aerial.png')).colours > 100);
  put('flat_aerial', realPng(800, 450, () => [90, 120, 60]));
  pp = GL.pictureProblems(pq, '| case | render |\n|---|---|\n' + cm('flat_aerial') + '\n'); t('a flat single-colour PNG with a matching typed record: refused ("flat or blank picture")', pp.problems.some((x) => /flat or blank picture/.test(x)), pp.problems.join('|'));
  put('tiny_aerial', realPng(40, 20, (x, y) => [x * 6, y * 12, 7]), { width: 40, height: 20, road_px: 100 });
  pp = GL.pictureProblems(pq, '| case | render |\n|---|---|\n' + cm('tiny_aerial') + '\n'); t('a 40x20 picture: refused as too small', pp.problems.some((x) => /tiny_aerial/.test(x) && /only 40x20/.test(x)), pp.problems.join('|'));
  put('hdr_aerial', mkPng(800, 450));
  pp = GL.pictureProblems(pq, '| case | render |\n|---|---|\n' + cm('hdr_aerial') + '\n'); t('a header-only PNG cannot be decoded: the colour check is skipped, not failed (size and hash still apply)', !pp.problems.some((x) => /hdr_aerial/.test(x)), pp.problems.join('|'));
}

console.log('ctest gate (gates2 audit fault 3)');
{
  const ct = path.join(tmp, 'ctw'); fs.mkdirSync(path.join(ct, 'build/Testing/Temporary'), { recursive: true }); fs.mkdirSync(path.join(ct, 'tests'), { recursive: true });
  const reg = (n) => w(path.join(ct, 'build/tests/CTestTestfile.cmake'), Array.from({ length: n }, (_, i) => `add_test(t${i} "x")`).join('\n') + '\n');
  const block = (i, n, o = {}) => `${i}/${n} Testing: t${i}\n${i}/${n} Test: t${i}\n${o.crash ? 'terminate called\n' : `[doctest] test cases: ${o.cases ?? 5} | 5 passed | 0 failed | 0 skipped\n[doctest] assertions: 50 | ${50 - (o.fa || 0)} passed | ${o.fa || 0} failed |\n`}${o.crash ? 'Test Failed.\n' : o.fa ? 'Test Failed.\n' : 'Test Passed.\n'}`;
  const log = (txt) => w(path.join(ct, 'build/Testing/Temporary/LastTest.log'), txt);
  w(path.join(ct, 'tests/a.cpp'), 'TEST_CASE("a") {}\nTEST_CASE("b") {}\nTEST_CASE("c") {}\nTEST_CASE("d") {}\nTEST_CASE("e") {}\n');
  reg(1); log(block(1, 1, { fa: 2 }));
  let m = GL.measureCtest(ct); t('one suite, 2 failed assertions measured from LastTest.log (no ctest binary needed)', m.failed_assertions === 2 && m.registered === 1 && m.failed_tests === 1, JSON.stringify(m));
  let g = GL.runMeasuredGates(GL.parseGates(['ctest failed_assertions == 2']), ct);
  t('"== 2" with 2 measured: passes', g.problems.length === 0, g.problems.join('|'));
  log(block(1, 1, {})); g = GL.runMeasuredGates(GL.parseGates(['ctest failed_assertions == 2']), ct);
  t('"== 2" with an IMPROVEMENT to 0 measured: passes (== behaves as <= on failure counts)', g.problems.length === 0, g.problems.join('|'));
  log(block(1, 1, { fa: 3 })); g = GL.runMeasuredGates(GL.parseGates(['ctest failed_assertions == 2']), ct); t('3 measured: refused', g.problems.length === 1 && /measured 3/.test(g.problems[0]));
  log(block(1, 1, { crash: true }));
  m = GL.measureCtest(ct); t('a crashed test (no doctest summary) shows 0 failed assertions but counts as a failed TEST', m.failed_assertions === 0 && m.failed_tests === 1, JSON.stringify(m));
  g = GL.runMeasuredGates(GL.parseGates(['ctest failed_assertions <= 2', 'ctest failed_tests <= 0']), ct); t('...so the paired failed_tests gate refuses the crash', g.problems.some((x) => /ctest failed_tests/.test(x)), g.problems.join('|'));
  log(block(1, 1, { crash: true }).replace('Test Failed.', 'Test Passed.')); m = GL.measureCtest(ct); t('a non-doctest test that ctest reports as Passed is not counted as a failure', m.failed_tests === 0, JSON.stringify(m));
  reg(3); log(block(1, 3) + block(2, 3));
  m = GL.measureCtest(ct); t('the log covers 2 of the 3 registered tests (a subset run): refused with the numbers', /covers 2 of 3 registered tests/.test(m.fail || ''), JSON.stringify(m));
  fs.rmSync(path.join(ct, 'build/tests/CTestTestfile.cmake')); log(block(1, 1));
  m = GL.measureCtest(ct); t('no add_test anywhere in the build (the total cannot be found): refused, never silently skipped', /cannot tell how many tests the build registers/.test(m.fail || ''), JSON.stringify(m));
  reg(1); log(block(1, 1, { cases: 2 })); m = GL.measureCtest(ct); t('doctest ran 2 cases but the sources hold 5 TEST_CASEs (a -tc= filtered run): refused', /ran 2 doctest cases but the sources hold 5/.test(m.fail || ''), JSON.stringify(m));
  log(block(1, 1, { cases: 5 })); m = GL.measureCtest(ct); t('5 of 5 cases: accepted', m.failed_tests === 0 && !m.fail, JSON.stringify(m));
  t('make_brief refuses "ctest failed_assertions == 2" and says to write <= 2', (() => { const b2 = mb(tf('## Gates\nctest failed_assertions == 2\n')); return b2.status === 1 && /write "ctest failed_assertions <= 2"/.test(b2.stderr); })());
  t('make_brief pairs ctest failed_assertions <= 2 with ctest failed_tests <= 2 automatically', (() => { const b2 = mb(tf('## Gates\nctest failed_assertions <= 2\n')); return b2.status === 0 && /^GATE: ctest failed_tests <= 2$/m.test(b2.stdout); })());
  t('an explicit failed_tests line is not duplicated', (() => { const b2 = mb(tf('## Gates\nctest failed_assertions <= 2\nctest failed_tests <= 1\n')); return b2.status === 0 && (b2.stdout.match(/^GATE: ctest failed_tests/gm) || []).length === 1; })());
}

console.log('scene gate: broken run is a refusal, no write into the worktree (gates2 audit faults 4 and 5)');
{
  const sw = path.join(tmp, 'sw'); const rd = path.join(sw, 'prototypes/road-draw');
  fs.mkdirSync(path.join(rd, 'tools'), { recursive: true }); fs.mkdirSync(path.join(rd, 'node_modules'), { recursive: true });
  w(path.join(rd, 'build.mjs'), "import fs from 'node:fs'; fs.writeFileSync('index.html', '<html></html>'); console.log('wrote index.html');\n");
  const sceneJs = (body) => w(path.join(rd, 'tools/scene.mjs'), body);
  sceneJs("console.log('PASS a ok'); console.log('1/1 passed in 0.1s');\n");
  let m = GL.measureSceneCheck(sw); t('a good scene check is measured', m.value === '1/1', JSON.stringify(m));
  t('...and the worktree under inspection was NOT written to (no index.html appeared; the build ran in a temp copy)', !fs.existsSync(path.join(rd, 'index.html')));
  sceneJs("console.log('PASS a ok'); throw new Error('browser crashed');\n");
  m = GL.measureSceneCheck(sw); t('scene.mjs crashes without a summary line: a REFUSAL ("fail"), not a FAIL-OPEN error', /printed no "N\/M passed" line/.test(m.fail || '') && !m.error, JSON.stringify(m));
  sceneJs('setTimeout(() => {}, 60000);\n');
  m = GL.measureSceneCheck(sw, { checkMs: 600 }); t('scene check hangs past the cap: a refusal that names the timeout', /timed out after/.test(m.fail || '') && !m.error, JSON.stringify(m));
  w(path.join(rd, 'build.mjs'), "process.exit(3);\n"); sceneJs("console.log('1/1 passed in 0.1s');\n");
  m = GL.measureSceneCheck(sw); t('node build.mjs fails: a refusal naming the build', /node build\.mjs failed/.test(m.fail || ''), JSON.stringify(m));
  w(path.join(rd, 'build.mjs'), "setTimeout(() => {}, 60000);\n"); m = GL.measureSceneCheck(sw, { buildMs: 500 }); t('node build.mjs hangs: a refusal naming the timeout', /build\.mjs timed out/.test(m.fail || ''), JSON.stringify(m));
  fs.rmSync(path.join(rd, 'node_modules'), { recursive: true }); m = GL.measureSceneCheck(sw); t('node_modules missing is infrastructure: { error } (fail open), not { fail }', !!m.error && !m.fail, JSON.stringify(m));
  t('the time budget fits inside the spawn limit (build + check < 270 s)', GL.SCENE_BUILD_MS + GL.SCENE_CHECK_MS < 270000, GL.SCENE_BUILD_MS + ' + ' + GL.SCENE_CHECK_MS);
  // the real gate on a clean copy of the real road-draw leaves git status clean
  const rg = path.join(tmp, 'realrd'); fs.mkdirSync(path.join(rg, 'prototypes'), { recursive: true });
  fs.cpSync(path.join(src, 'prototypes/road-draw'), path.join(rg, 'prototypes/road-draw'), { recursive: true, filter: (s) => !/[\\/](node_modules|screenshots|x|\.git)$/.test(s) });
  git(rg, 'init', '-q'); git(rg, 'config', 'user.email', 't@t'); git(rg, 'config', 'user.name', 't'); git(rg, 'add', '-A'); git(rg, 'commit', '-qm', 'base');
  if (fs.existsSync(path.join(src, 'prototypes/road-draw/node_modules'))) {
    fs.symlinkSync(path.join(src, 'prototypes/road-draw/node_modules'), path.join(rg, 'prototypes/road-draw/node_modules'), 'junction');
    m = GL.measureSceneCheck(rg);
    t('the REAL scene check on a clean copy of road-draw: measured (' + (m.value || m.fail || m.error) + ') and `git status` is still clean afterwards', (m.value || m.fail) !== undefined && git(rg, 'status', '--porcelain').replace(/\?\? prototypes\/road-draw\/node_modules\/?/, '').trim() === '', git(rg, 'status', '--porcelain'));
  } else t('the REAL scene check on a clean copy of road-draw (skipped: no node_modules in this checkout)', true);
}

console.log('make_brief: case counting, no-render wording, directions, broken library (gates2 audit faults 9 and 10)');
{
  const cnt = (cases) => mb(tf('', cases));
  t('five cases written "s1: ... s5:" are counted (round 9 named its views s1..s8): refused without a Scope', (() => { const b2 = cnt('s1: a\n- a.jpg\ns2: b\n- b.jpg\ns3: c\n- c.jpg\ns4: d\n- d.jpg\ns5: e\n- e.jpg\n'); return b2.status === 1 && /names 5 cases/.test(b2.stderr); })());
  t('five cases written "1. ... 5." are counted', cnt('1. a a.jpg\n2. b b.jpg\n3. c c.jpg\n4. d d.jpg\n5. e e.jpg\n').status === 1);
  t('four cases written "A) B) C) D)" are counted', cnt('A) a a.jpg\nB) b b.jpg\nC) c c.jpg\nD) d d.jpg\n').status === 1);
  t('"Item 1:" lines are counted', cnt('Item 1: a a.jpg\nItem 2: b b.jpg\nItem 3: c c.jpg\nItem 4: d d.jpg\n').status === 1);
  t('an explicit "Cases: 6" statement is counted', cnt('Cases: 6 views of one junction\n- a.jpg b.jpg c.jpg\n').status === 1);
  t('a list of drive-path files is not a list of cases', cnt('Files to open:\n- C:\\a\\one.md\n- C:\\a\\two.md\n- C:\\a\\three.md\n- C:\\a\\four.md\n- D:\\x\\five.md\n').status === 0);
  t('three cases in any of those forms are allowed', cnt('s1: a\ns2: b\ns3: c\n- a.jpg b.jpg c.jpg\n').status === 0);
  for (const phrase of ['Engine work, headless: no views', 'no screenshots are produced', 'nothing is rendered by this task', 'no images at all', 'no visuals']) {
    t('Cases saying "' + phrase + '" without --no-visual: refused', mb(tf('', phrase + '\ncase A | a.jpg b.jpg c.jpg')).status === 1);
  }
  t('a round line with an unknown metric and no direction is refused ("bare_steps=3")', (() => { const b2 = mb(tf('Previous round: r9\n## Rounds\nr7: bare_steps=3\nr8: bare_steps=3\nr9: bare_steps=3\n')); return b2.status === 1 && /no stated direction: bare_steps/.test(b2.stderr); })());
  t('...with "(lower)" stated and the count RISING: method review demanded (the direction is stated, not guessed from the name)', (() => { const b2 = mb(tf('Previous round: r9\n## Rounds\nr7: bare_steps=3 (lower)\nr8: bare_steps=4 (lower)\nr9: bare_steps=5 (lower)\n')); return b2.status === 1 && /METHOD REVIEW FIRST/.test(b2.stderr); })());
  t('...and a count that falls with "(lower)" is an improvement', mb(tf('Previous round: r9\n## Rounds\nr7: bare_steps=5 (lower)\nr8: bare_steps=4 (lower)\nr9: bare_steps=4 (lower)\n')).status === 0);
  t('a metric named like a failure but stated "(higher)" is read as higher', mb(tf('Previous round: r9\n## Rounds\nr7: faults_found=3 (higher)\nr8: faults_found=4 (higher)\nr9: faults_found=5 (higher)\n')).status === 0);
  // broken gates_lib: the brief is still printed, FAIL-OPEN on stderr
  const bt = path.join(tmp, 'brokentools'); fs.mkdirSync(path.join(bt, 'tools/hooks'), { recursive: true }); fs.mkdirSync(path.join(bt, 'docs/templates'), { recursive: true });
  for (const f of ['tools/make_brief.mjs', 'tools/hooks/lib.mjs']) fs.copyFileSync(path.join(src, f), path.join(bt, f));
  fs.copyFileSync(path.join(src, 'docs/AGENT_CONTRACT.md'), path.join(bt, 'docs/AGENT_CONTRACT.md')); fs.copyFileSync(path.join(src, 'docs/templates/BRIEF.md'), path.join(bt, 'docs/templates/BRIEF.md'));
  w(path.join(bt, 'tools/gates_lib.mjs'), 'export const x = ;;; this is not javascript\n');
  const bb = spawnSync(process.execPath, [path.join(bt, 'tools/make_brief.mjs'), tf(), '--round', 'rr', '--worktree', path.join(tmp, 'wtrr')], { encoding: 'utf8' });
  t('a SYNTAX ERROR in gates_lib.mjs: make_brief still prints the brief (exit 0), without GATES-VERSION, and says FAIL-OPEN', bb.status === 0 && /^HANDIN_DIR:/m.test(bb.stdout) && !/^GATES-VERSION/m.test(bb.stdout) && /FAIL-OPEN: gates_lib\.mjs could not be loaded/.test(bb.stderr), bb.stdout.slice(0, 200) + bb.stderr);
}

console.log('wrapper gate: every sim_core source needs its Unreal wrapper');
{
  const CW = await import(pathToFileURL(path.join(src, 'tools/check_wrappers.mjs')).href);
  const rw = path.join(tmp, 'wraproot'); const wd = path.join(rw, CW.WRAP_DIR);
  for (const n of ['alpha', 'beta']) { w(path.join(rw, 'sim_core/src', n + '.cpp'), 'int x;\n'); w(path.join(wd, 'Wrap_' + n + '.cpp'), '#include "x"\n'); }
  let r = CW.wrapperProblems(rw); t('every source has a wrapper: nothing missing', r.missing.length === 0 && r.checked === 2, JSON.stringify(r));
  w(path.join(rw, 'sim_core/src/road_heat.cpp'), 'int h;\n'); w(path.join(rw, 'sim_core/src/tool_hud.cpp'), 'int h;\n'); w(path.join(rw, 'sim_core/src/tool_tuning.cpp'), 'int h;\n');
  r = CW.wrapperProblems(rw); t('road_heat, tool_hud and tool_tuning added without wrappers (the roadtool3 merge state): all three reported by name', r.missing.length === 3 && ['road_heat', 'tool_hud', 'tool_tuning'].every((n) => r.missing.some((m) => m.includes(`Wrap_${n}.cpp`))), JSON.stringify(r));
  const cli = (root) => spawnSync(process.execPath, [path.join(src, 'tools/check_wrappers.mjs'), root], { encoding: 'utf8' });
  let cr = cli(rw); t('the standalone script exits 1 and lists them', cr.status === 1 && /WRAPPER CHECK FAILED \(3 of 5/.test(cr.stderr), cr.stderr);
  for (const n of ['road_heat', 'tool_hud', 'tool_tuning']) w(path.join(wd, 'Wrap_' + n + '.cpp'), '#include "x"\n');
  cr = cli(rw); t('...and exits 0 once the wrappers exist (the fixed state)', cr.status === 0 && /WRAPPER CHECK PASSED: 5 sim_core sources/.test(cr.stdout), cr.stderr + cr.stdout);
  w(path.join(wd, 'Wrap_old.cpp'), 'x'); cr = cli(rw); t('an orphan wrapper is only a warning', cr.status === 0 && /warning: Wrap_old\.cpp/.test(cr.stderr));
  t('no sim_core/src folder: exit 2 with a message (not a false pass)', cli(path.join(tmp, 'nowhere')).status === 2);
  // check_handin runs it when a round ADDS a sim_core source (in the e2e repo)
  w(path.join(wt, CW.WRAP_DIR, 'Wrap_a.cpp'), '#include "x"\n'); git(wt, 'add', '-A'); git(wt, 'commit', '-qm', 'wrapper for a');
  w(path.join(wt, 'sim_core/src/newthing.cpp'), 'int n;\n'); git(wt, 'add', '-A'); git(wt, 'commit', '-qm', 'adds newthing.cpp without a wrapper');
  c = check(); t('check_handin: this round ADDS sim_core/src/newthing.cpp with no wrapper: refused, names the file and the wrapper path', c.status === 1 && /WRAPPER GATE/.test(c.stderr) && /newthing\.cpp has no .*Wrap_newthing\.cpp/.test(c.stderr), c.stderr);
  w(path.join(wt, CW.WRAP_DIR, 'Wrap_newthing.cpp'), '#include "x"\n'); git(wt, 'add', '-A'); git(wt, 'commit', '-qm', 'wrapper');
  c = check(); t('...with the wrapper added: passes', c.status === 0, c.stderr);
  git(wt, 'reset', '-q', '--hard', 'HEAD~3');
}

console.log('BLOCKED form and placeholder refusal, end to end (coordinator addition)');
{
  const SCg = ['--gates-version', '2', '--gate', 'scene_check >= 20/21 (baseline 20/21, target 21/21)'];
  w(path.join(folder, 'FAULTS.md'), faults3);
  fakeScene(17, 21);
  w(path.join(folder, 'STATUS.md'), statusBase('Nothing else skipped.\nBLOCKED: scene_check below the floor (17/21) | tried: three changes to the wall fade and a longer ramp, each re-measured | blocked by: ground_under_road keeps failing because the crossing road holds the ground 2 m high | needs: a decision on the crossing level'));
  c = check(SCg);
  t('scene 17/21 (below the floor) with a complete BLOCKED line that names scene_check: PASSES, the measurement is still printed as BLOCKED', c.status === 0 && /BLOCKED \(named in STATUS\.md\): scene_check measured 17\/21/.test(c.stdout), c.stdout + c.stderr);
  t('...and prints one BLOCKED-ITEM line per item for the hand-in hook to log', /^BLOCKED-ITEM: BLOCKED: scene_check below the floor/m.test(c.stdout), c.stdout);
  w(path.join(folder, 'STATUS.md'), statusBase('Nothing else skipped.\nBLOCKED: the wall curl | tried: a ramp and a fade, re-measured twice | blocked by: the crossing road holds the ground 2 m high here | needs: a decision'));
  c = check(SCg); t('...a BLOCKED line about something ELSE does not excuse the floor miss: refused', c.status === 1 && /measured 17\/21/.test(c.stderr), c.stderr);
  w(path.join(folder, 'STATUS.md'), statusBase('Nothing else skipped.\nBLOCKED: scene_check below the floor | tried: it | blocked by: time'));
  c = check(SCg); t('...an incomplete BLOCKED line (no real tried / blocked by) excuses nothing and is itself refused', c.status === 1 && /BLOCKED line is incomplete/.test(c.stderr) && /measured 17\/21/.test(c.stderr), c.stderr);
  w(path.join(folder, 'STATUS.md'), statusBase('- scene_check is below the floor, out of scope for this round, a later round will fix it'));
  c = check(SCg); t('...excuses without tried / blocked by (scope, later round) refuse as before', c.status === 1 && /left undone or deferred/.test(c.stderr), c.stderr);
  w(path.join(folder, 'STATUS.md'), statusBase('BLOCKED: wall curl at the road ends | tried: a ramp, a fillet and a longer fade, three renders | blocked by: the crossing road keeps the ground 2 m high | needs: a decision on the crossing level'));
  fakeScene(21, 21); c = check(SCg); t('an item that only the STATUS reader sees (no metric involved) in the BLOCKED form: passes at 21/21 and is reported', c.status === 0 && /BLOCKED-ITEM: BLOCKED: wall curl/.test(c.stdout), c.stdout + c.stderr);
  w(path.join(folder, 'STATUS.md'), statusBase());

  // placeholder COMPARE tables (gates2's own COMPARE row, roadtool3 and gradelayer): refused firmly, pointing to the BLOCKED form
  const vf2 = path.join(wt, 'docs/progress/vis2'); w(path.join(vf2, 'STATUS.md'), statusBase()); w(path.join(vf2, 'FAULTS.md'), faults3);
  for (const f of ['comp.png', 'before.png']) w(path.join(vf2, f), 'x');
  w(path.join(vf2, 'r.png'), mkPng(800, 450)); w(path.join(vf2, 'r.stats.json'), JSON.stringify({ width: 800, height: 450, diagnostic: false, road_px: 30000, debug_px: 0, png_sha1: sha1(mkPng(800, 450)) }));
  const hdr = '| case | render | reference | composite | before | subject/type match | scale match | viewpoint match |\n|---|---|---|---|---|---|---|---|\n';
  const cv = (rows) => { w(path.join(vf2, 'COMPARE.md'), hdr + rows + '\n'); return spawnSync(process.execPath, [path.join(src, 'tools/check_handin.mjs'), vf2, '--main', main, '--worktree', wt, '--gates-version', '2'], { encoding: 'utf8', cwd: tmp, env: { ...process.env, CP_FROM_HOOK: '1' } }); };
  c = cv('| s | r.png | aerial/a.jpg | comp.png | before.png | evidence row only, no match claimed | evidence row only, no match claimed | evidence row only, no match claimed |');
  t('gates2\'s own placeholder row ("evidence row only, no match claimed" x3): refused, and the message points to the BLOCKED form', c.status === 1 && /placeholder/.test(c.stderr) && /BLOCKED: <item> \| tried/.test(c.stderr), c.stderr);
  c = cv('| s | r.png | aerial/a.jpg | comp.png | before.png | placeholder until the render exists | n/a for this case | to be done |');
  t('"placeholder" and "n/a" in the match cells: refused, both cells named, BLOCKED form shown', c.status === 1 && /subject\/type match cell is a placeholder/.test(c.stderr) && /scale match/.test(c.stderr) && /BLOCKED: <item>/.test(c.stderr), c.stderr);
  c = cv('| s | r.png | aerial/a.jpg | comp.png | before.png | same junction type as the photo | same junction type as the photo | same junction type as the photo |');
  t('one sentence copied into the three match cells: refused', c.status === 1 && /identical/.test(c.stderr), c.stderr);
  c = cv('');
  t('a COMPARE.md with a header and no rows: refused, with the BLOCKED hint', c.status === 1 && /no table rows/.test(c.stderr) && /BLOCKED: <item>/.test(c.stderr), c.stderr);
  fs.rmSync(path.join(vf2, 'COMPARE.md'));
  c = spawnSync(process.execPath, [path.join(src, 'tools/check_handin.mjs'), vf2, '--main', main, '--worktree', wt, '--gates-version', '2'], { encoding: 'utf8', cwd: tmp, env: { ...process.env, CP_FROM_HOOK: '1' } });
  t('COMPARE.md missing: refused, with the BLOCKED hint', c.status === 1 && /COMPARE\.md is missing/.test(c.stderr) && /BLOCKED: <item>/.test(c.stderr), c.stderr);
  c = cv('| s | r.png | aerial/a.jpg | comp.png | before.png | two-lane road, same junction type | both about 40 m across | top-down view, both |');
  t('a real row passes the same gate', c.status === 0, c.stderr);
  const v1 = spawnSync(process.execPath, [path.join(src, 'tools/check_handin.mjs'), vf2, '--main', main, '--worktree', wt], { encoding: 'utf8', cwd: tmp, env: { ...process.env, CP_FROM_HOOK: '1' } });
  t('under a v1 brief (no GATES-VERSION) the placeholder rules do not apply: running agents are not newly refused', v1.status === 0 && !/placeholder/.test(v1.stderr), v1.stderr);
}

console.log('small gates3 additions: multi-config ctest, --since on FAULTS.md, template wording');
{
  const ct2 = path.join(tmp, 'ctw2'); fs.mkdirSync(path.join(ct2, 'build/Testing/Temporary'), { recursive: true }); fs.mkdirSync(path.join(ct2, 'tests'), { recursive: true });
  w(path.join(ct2, 'build/tests/CTestTestfile.cmake'), ['Debug', 'Release', 'MinSizeRel'].map((cfg) => `if(CTEST_CONFIGURATION_TYPE MATCHES "${cfg}")\n  add_test([=[sim_core_tests]=] "C:/x/${cfg}/sim_core_tests.exe")\nendif()`).join('\n') + '\n');
  w(path.join(ct2, 'build/Testing/Temporary/LastTest.log'), '1/1 Testing: sim_core_tests\n1/1 Test: sim_core_tests\n[doctest] assertions: 10 | 10 passed | 0 failed |\nTest Passed.\n');
  t('a multi-config build registers ONE test under three configurations: counted once (the roadtool3 build-audit-msvc case), so the whole-suite log passes', GL.countRegisteredTests(path.join(ct2, 'build')) === 1 && !GL.measureCtest(ct2).fail, JSON.stringify(GL.measureCtest(ct2)));
  // --since applies to FAULTS.md on a v2 engine-only hand-in
  const sf = path.join(wt, 'docs/progress/since'); w(path.join(sf, 'STATUS.md'), statusBase()); w(path.join(sf, 'FAULTS.md'), faults3);
  const old = new Date(Date.now() - 7200 * 1000); fs.utimesSync(path.join(sf, 'FAULTS.md'), old, old);
  const run = (flags) => spawnSync(process.execPath, [path.join(src, 'tools/check_handin.mjs'), sf, '--main', main, '--worktree', wt, '--no-visual', '--since', String(Date.now() - 3600 * 1000), ...flags], { encoding: 'utf8', cwd: tmp, env: { ...process.env, CP_FROM_HOOK: '1' } });
  let r = run(['--gates-version', '2']); t('v2 engine-only: a FAULTS.md from before the agent started is refused as stale', r.status === 1 && /older than this agent's run/.test(r.stderr) && /FAULTS\.md/.test(r.stderr), r.stderr);
  r = run([]); t('v1 engine-only: FAULTS.md is not asked for, so its age is not either (running agents unchanged)', !/FAULTS\.md/.test(r.stderr), r.stderr);
  const bt = fs.readFileSync(path.join(src, 'docs/templates/BRIEF.md'), 'utf8') + fs.readFileSync(path.join(src, 'docs/templates/STATUS.md'), 'utf8');
  t('the brief and STATUS templates show the BLOCKED form and the wrapper check', /BLOCKED: <item> \| tried: <what you attempted> \| blocked by:/.test(bt) && /check_wrappers\.mjs/.test(bt));
}

console.log(`\n${pass} passed, ${fail} failed (temp dir ${tmp})`);
if (failed.length) console.log('FAILED TESTS:\n - ' + failed.join('\n - '));
process.exit(fail ? 1 : 0);
