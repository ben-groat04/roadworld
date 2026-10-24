#!/usr/bin/env node
// gates15: real-payload tables for the send-back of round gates14 (docs/progress/gates14/AUDIT.md and the main-session ruling of 24 Oct 2026).
//   A  fail-open of 24 Oct 08:46Z: a glob word with an unclosed "[" crashed a hook; a pattern the engine rejects now REFUSES
//   B  ruling 1: ACCEPTED RESIDUAL (deliberate code can read the secret); DETECTION: code that imports a hooks file or names the secret, written or run outside tools/ and the tests, is refused
//   C  ruling 2: run_suite records: the import tree of the script, a clean child environment, the gcc exe tied to the CMake build
//   D  ruling 3: the home folder (any folder above ~/.roadworld-gate) as the whole source of a copy, move, archive, recursive search; Grep and Glob rooted there; git on it
//   E  ruling 4: the lane lock is decided by gate.log, not owner.txt; preloads of the tool are refused
//   F  ruling 5: the unmapped NO-CONTRACT descriptions are in the table
// Where the expected values come from: the probes of docs/progress/gates14/AUDIT.md (scratchpad aud14 c1 to c5 and suite.sh) and the main-session ruling; nothing pins a fault. No test writes the live
// gate.log or the real home folder (CP_GATE_STATE_DIR, USERPROFILE and HOME point into the temp folder).
// Usage: node tools/hooks/test_guards15.mjs
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { spawnSync } from 'node:child_process';
import { fileURLToPath, pathToFileURL } from 'node:url';
import { isolateState, cleanupTmp } from '../test_isolation.mjs';

const src = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..', '..');
const tmp = fs.mkdtempSync(path.join(os.tmpdir(), 'guards15-'));
const isolation = isolateState(tmp, src);
let pass = 0, fail = 0; const failedNames = [];
const t = (name, cond, detail = '') => { if (cond) { pass++; console.log('  ok   ' + name); } else { fail++; failedNames.push(name); console.log('  FAIL ' + name + (detail ? '\n       ' + String(detail).slice(0, 700) : '')); } };
const mk = (p) => fs.mkdirSync(p, { recursive: true });
const fwd = (p) => String(p).replace(/\\/g, '/');
const home = path.join(tmp, 'home'); mk(home);
const baseEnv = (extra = {}) => { const e = { ...process.env, CP_GATE_STATE_DIR: isolation.stateDir, USERPROFILE: home, HOME: home, CP_TIME_GUARD_PROCS: '' }; delete e.CP_MAIN_ROOT; delete e.CP_GATE_SECRET_DIR; delete e.CP_SUITE_BASE; delete e.NODE_OPTIONS; for (const [k, v] of Object.entries(extra)) { if (v === null) delete e[k]; else e[k] = v; } return e; };
const gitEnv = { ...process.env, GIT_AUTHOR_NAME: 't', GIT_AUTHOR_EMAIL: 't@t', GIT_COMMITTER_NAME: 't', GIT_COMMITTER_EMAIL: 't@t' };
const gitq = (cwd, ...a) => spawnSync('git', a, { cwd, encoding: 'utf8', env: gitEnv });
const proj = path.join(tmp, 'g_main'); mk(path.join(proj, '.claude', 'state')); mk(path.join(proj, 'docs')); mk(path.join(proj, 'tools', 'hooks')); fs.writeFileSync(path.join(proj, '.claude', 'settings.json'), '{}');
const P = fwd(proj); const H = fwd(home); const scratch = path.join(tmp, 'g_scratch'); mk(scratch); const SC = fwd(scratch);
const guardEnv = (e = {}) => baseEnv({ CP_MAIN_ROOT: proj, ...e });
const sg = (payload, agent = 'ag5', cwd = proj) => spawnSync(process.execPath, [path.join(src, 'tools', 'hooks', 'state_guard.mjs')], { input: JSON.stringify({ hook_event_name: 'PreToolUse', cwd, session_id: 's15', agent_id: agent, agent_type: 'general-purpose', ...payload }), encoding: 'utf8', env: guardEnv(), timeout: 60000 });
const bash = (command, agent, cwd) => sg({ tool_name: 'Bash', tool_input: { command } }, agent, cwd);
const ps = (command, agent) => sg({ tool_name: 'PowerShell', tool_input: { command } }, agent);
const tool = (name, input, agent) => sg({ tool_name: name, tool_input: input }, agent);
const label = (g) => (x) => x;
const R = (g) => (name, r) => t(`${g} REFUSE: ${name}`, r.status === 2 && /REFUSED|state guard/i.test(r.stderr), `status ${r.status} ${r.stderr.slice(0, 260)}`);
const A = (g) => (name, r) => t(`${g} ALLOW: ${name}`, r.status === 0, `status ${r.status} ${r.stderr.slice(0, 260)}`);
void label;

// ================================================================ A  glob words that cannot be compiled
console.log('gates15 A: a word with an unclosed [ (the 24 Oct 08:46Z fail-open) never crashes a hook; a pattern the engine rejects refuses');
{
  const gp = await import(pathToFileURL(path.join(src, 'tools', 'hooks', 'guard_paths.mjs')).href);
  const impl = await import(pathToFileURL(path.join(src, 'tools', 'hooks', 'state_guard_impl.mjs')).href);
  const lib = await import(pathToFileURL(path.join(src, 'tools', 'hooks', 'lib.mjs')).href);
  const words = ['[..*Linking', '[', '[a-', '[\\', '[]', '[z-a]*', '[[]', '[^', '*[', 'x[a-z', '[\\]'];
  for (const w of words) {
    let threw = null; let r;
    try { r = impl.judgeCommand(`node tools/${w} take --agent ag5`, 0, { mainRoot: proj, cwd: proj, agent: 'ag5' }); impl.judgeCommand(`cat ${w}`, 0, { mainRoot: proj, cwd: proj, agent: 'ag5' }); impl.judgeCommand(`grep -r x ${w}`, 0, { mainRoot: proj, cwd: proj, agent: 'ag5' }); gp.globSegRe(w); gp.globReaches(path.join(proj, w), path.join(proj, '.claude', 'state'), 'into'); } catch (e) { threw = e; }
    t(`A the word ${JSON.stringify(w)} through judgeCommand and the glob helpers throws nothing`, threw === null, threw && threw.message);
    const h = bash(`cat ${w}`); t(`A the real hook on \`cat ${w}\` exits 0 or 2, writes no FAIL-OPEN line and no SyntaxError`, (h.status === 0 || h.status === 2) && !/SyntaxError|FAIL-OPEN/.test(h.stderr + h.stdout), `status ${h.status} ${h.stderr.slice(0, 200)}`);
    void r;
  }
  t('A the exact word of the live failure ([..*Linking) is judged without an exception', (() => { try { impl.judgeCommand('node tools/[..*Linking', 0, { mainRoot: proj, cwd: proj, agent: 'ag5' }); return true; } catch { return false; } })());
  t('A globRe on an invalid pattern returns a regex that matches everything (the caller then refuses)', (() => { try { return lib.globRe('[z-a]', 'i').test('ack_secret') && lib.globRe('(', '').test('x'); } catch { return false; } })());
  t('A globSegRe of an invalid class ([z-a]*) matches everything: a glob that cannot be read is treated as able to name the secret', gp.globSegRe('[z-a]*').test('ack_secret'));
  const fo = fs.existsSync(path.join(isolation.stateDir, 'gate.log')) ? fs.readFileSync(path.join(isolation.stateDir, 'gate.log'), 'utf8') : '';
  t('A no FAIL-OPEN line in the redirected gate.log after all the words', !/FAIL-OPEN/.test(fo), fo.split('\n').filter((l) => /FAIL-OPEN/.test(l)).join('\n').slice(0, 300));
  R('A')('a glob that cannot be compiled and can only mean the secret folder: cat ~/.[z-a]*/ack*', bash('cat ~/.[z-a]*/ack*'));
}

// ================================================================ B  ruling 1: detection of code that touches the secret
console.log('gates15 B: ACCEPTED RESIDUAL: deliberate code reads the secret. DETECTION: code outside tools/ and the tests that imports a hooks file or names the secret is refused');
{
  const r = R('B'), a = A('B');
  const lib = fwd(path.join(src, 'tools', 'hooks', 'lib.mjs'));
  // the auditor's three forgeries of gates14 AUDIT.md fault 1
  fs.writeFileSync(path.join(scratch, 'forge.mjs'), `import os from 'node:os';\nimport fs from 'node:fs';\nconsole.log(fs.readFileSync(os.homedir()+'/.road'+'world-gate/ack_secret','utf8'));\n// ack_secret`);
  fs.writeFileSync(path.join(scratch, 'sig.mjs'), `import { signRecord } from '${lib}';\nconsole.log(JSON.stringify(signRecord({ v: 1 })));\n`);
  fs.writeFileSync(path.join(scratch, 'innocent.mjs'), `console.log('hello');\n`);
  r('node <scratch>/forge.mjs (a script file naming ack_secret; the audit\'s fault 1 form 1)', bash(`node ${SC}/forge.mjs`));
  r('cd <scratch> && node forge.mjs (relative after a cd)', bash(`cd ${SC} && node forge.mjs`));
  r('node <scratch>/sig.mjs (a script file that imports lib.mjs and signs; form 3)', bash(`node ${SC}/sig.mjs`));
  r('node -e import(lib.mjs).then(m => m.readSecret()) (form 2)', bash(`node -e "import('${lib}').then(m=>console.log(m.readSecret()))"`));
  r('node -e import of audit_lib.mjs (any hooks file)', bash(`node -e "import('${fwd(path.join(src, 'tools', 'hooks', 'audit_lib.mjs'))}')"`));
  r('node --input-type=module -e with an import of a hooks file', bash(`node --input-type=module -e "import {x} from '../tools/hooks/notice_lib.mjs'"`));
  r('python -c naming the secret folder', bash(`python -c "print(open('/x/.roadworld-gate/ack_secret').read())"`));
  r('node - <<EOF with an import of lib.mjs (a heredoc fed to the interpreter)', bash(`node - <<'EOF'\nimport('${lib}').then((m) => console.log(m.readSecret()));\nEOF`));
  r('cat > f.mjs <<EOF ... import lib.mjs ... EOF (a script written by a heredoc)', bash(`cat > ${SC}/h.mjs <<'EOF'\nimport { signRecord } from '${lib}';\nEOF`));
  r('the Write tool: a new .mjs outside tools that imports lib.mjs', tool('Write', { file_path: path.join(scratch, 'w.mjs'), content: `import {signRecord} from '../wt/tools/hooks/lib.mjs'; signRecord({});` }));
  r('the Write tool: a new .py naming the secret folder', tool('Write', { file_path: path.join(scratch, 'w.py'), content: `open('/home/u/.roadworld-gate/ack_secret')` }));
  r('the Write tool: a new tools/forge.mjs (not a test, not hooks, not existing) naming readSecret', tool('Write', { file_path: path.join(proj, 'tools', 'forge.mjs'), content: `export const x = readSecret();` }));
  r('the Edit tool: new_string that imports a hooks file into a scratch script', tool('Edit', { file_path: path.join(scratch, 'sig.mjs'), old_string: 'console', new_string: `import('../hooks/lib.mjs'); console` }));
  r('NODE_OPTIONS=--import <file> node tools/run_suite.mjs (preload of a gate tool)', bash(`NODE_OPTIONS='--import file:///${SC}/pre.mjs' node tools/run_suite.mjs ${P} --script tests/test_real.mjs`));
  r('node --import ./x.mjs tools/lane_lock.mjs take (preload of the lane tool)', bash('node --import ./x.mjs tools/lane_lock.mjs take --agent ag5 --round r --purpose x'));
  r('node --require ./x.cjs tools/check_handin.mjs (preload)', bash('node --require ./x.cjs tools/check_handin.mjs docs/progress/x'));
  r('node -r ./x.cjs tools/make_brief.mjs (preload)', bash('node -r ./x.cjs tools/make_brief.mjs docs/tasks/x.md --round x'));
  r('$env:NODE_OPTIONS = "--import x" in PowerShell', ps('$env:NODE_OPTIONS = "--import file:///x/pre.mjs"; node tools/run_suite.mjs x'));
  // what stays open
  a('node <scratch>/innocent.mjs (a script that names nothing)', bash(`node ${SC}/innocent.mjs`));
  fs.writeFileSync(path.join(proj, 'tools', 'check_x.mjs'), `import { signRecord } from './hooks/lib.mjs'; // a tool of the repo`);
  a('node tools/check_x.mjs (a script under tools/ may import the hooks library: run_suite, check_handin and the suites do)', bash('node tools/check_x.mjs'));
  a('node tools/hooks/test_guards15.mjs (a test of the hooks)', bash(`node ${fwd(path.join(src, 'tools', 'hooks', 'test_guards15.mjs'))} --list`));
  a('node tools/run_suite.mjs <wt> --round r (plain)', bash(`node tools/run_suite.mjs ${P} --round r`));
  a('node --check tools/x.mjs (a flag that is not a preload)', bash('node --check tools/x.mjs'));
  a('NODE_OPTIONS=--max-old-space-size=4096 node tools/run_suite.mjs (no preload in it)', bash(`NODE_OPTIONS=--max-old-space-size=4096 node tools/run_suite.mjs ${P}`));
  a('the Write tool: a .md that names ack_secret and signRecord (text, not code)', tool('Write', { file_path: path.join(scratch, 'n.md'), content: 'the secret is ~/.roadworld-gate/ack_secret, signed by signRecord' }));
  a('the Write tool: a new test file under tools/hooks naming the secret', tool('Write', { file_path: path.join(scratch, 'tools', 'hooks', 'test_x.mjs'), content: `import { signRecord } from './lib.mjs'; // ack_secret` }));
  a('the Write tool: a new test_*.mjs anywhere (a test of the hooks)', tool('Write', { file_path: path.join(scratch, 'test_y.mjs'), content: `import { signRecord } from '../tools/hooks/lib.mjs';` }));
  a('the Edit tool: an edit of an EXISTING tools file that names readSecret', tool('Edit', { file_path: path.join(proj, 'tools', 'check_x.mjs'), old_string: 'a', new_string: 'readSecret()' }));
  a('the Write tool: a scratch script with no secret in it', tool('Write', { file_path: path.join(scratch, 'ok.mjs'), content: `console.log(1);` }));
  a('git commit -m "state_guard now refuses signRecord imports" (the words are text)', bash('git commit -m "state_guard now refuses signRecord imports and .roadworld-gate reads"'));
  a('echo naming the secret folder into a .md note', bash(`echo 'secret in ~/.roadworld-gate' > ${SC}/note.md`));
  a('the main session (no agent id) runs a forging-looking script', spawnSync(process.execPath, [path.join(src, 'tools', 'hooks', 'state_guard.mjs')], { input: JSON.stringify({ hook_event_name: 'PreToolUse', tool_name: 'Bash', tool_input: { command: `node ${SC}/sig.mjs` }, cwd: proj, session_id: 's15' }), encoding: 'utf8', env: guardEnv() }));
}

// ================================================================ C  ruling 2: run_suite records
console.log('gates15 C: run_suite: the whole import tree, a clean child environment, the gcc exe tied to the CMake build');
{
  const rs = path.join(src, 'tools', 'run_suite.mjs');
  const sr = await import(pathToFileURL(path.join(src, 'tools', 'hooks', 'suite_record.mjs')).href);
  const main = path.join(tmp, 'c_main'); mk(path.join(main, 'tests')); mk(path.join(main, 'tools')); mk(path.join(main, '.claude', 'state'));
  gitq(main, 'init', '-q', '-b', 'main');
  fs.writeFileSync(path.join(main, 'tests', 'helper.mjs'), "export const summary = () => '[doctest] test cases: 3 | 3 passed | 0 failed | 0 skipped';\n");
  fs.writeFileSync(path.join(main, 'tests', 'deep.mjs'), "export const deep = 1;\n");
  fs.writeFileSync(path.join(main, 'tests', 'mid.mjs'), "import { deep } from './deep.mjs'; export const mid = deep;\n");
  fs.writeFileSync(path.join(main, 'tests', 'test_real.mjs'), "import { summary } from './helper.mjs';\nimport { mid } from './mid.mjs';\nconsole.log(summary(), mid);\n");
  fs.writeFileSync(path.join(main, 'tests', 'test_spawn.mjs'), "import { spawnSync } from 'node:child_process';\nconst r = spawnSync(process.execPath, ['-e', \"console.log('[doctest] test cases: 1 | 1 passed | 0 failed | 0 skipped')\"], { encoding: 'utf8' }); console.log(r.stdout);\n");
  fs.writeFileSync(path.join(main, 'tests', 'test_env.mjs'), "console.log(JSON.stringify({ foo: process.env.FOO_X || '', keep: process.env.CP_TEST_KEEP || '', np: process.env.NODE_PATH || '', no: process.env.NODE_OPTIONS || '', ex: process.execArgv }));\n");
  fs.writeFileSync(path.join(main, 'tools', 'util.mjs'), 'export const u = 1;\n');
  fs.writeFileSync(path.join(main, '.gitattributes'), '* text=auto eol=lf\n');   // as the real repo: the checkout bytes equal git show
  fs.writeFileSync(path.join(main, '.gitignore'), '.claude/\ndocs/progress/\nbuild/\n');
  gitq(main, 'add', '-A'); gitq(main, 'commit', '-q', '-m', 'base');
  const wt = path.join(tmp, 'c_wt'); gitq(main, 'worktree', 'add', '-q', '-b', 'agent/c1', wt);
  const stateC = path.join(main, '.claude', 'state');
  const secDir = path.join(tmp, 'c_secret');
  const prodEnv = baseEnv({ CP_GATE_STATE_DIR: null, CP_MAIN_ROOT: main, CP_GATE_SECRET_DIR: secDir });
  const run = (script, e = {}, extra = []) => spawnSync(process.execPath, [rs, wt, '--script', path.join(wt, script), '--round', 'rc', '--timeout', '60', ...extra], { encoding: 'utf8', env: { ...prodEnv, ...e }, timeout: 120000 });
  const recs = () => { try { return fs.readFileSync(path.join(stateC, 'suite_records.jsonl'), 'utf8').trim().split('\n').filter(Boolean).length; } catch { return 0; } };
  const logC = path.join(wt, 'docs', 'progress', 'rc', 'suite_node.log');
  let r = run('tests/test_real.mjs');
  t('C an unchanged script with an unchanged import tree runs and leaves a record', r.status === 0 && recs() === 1 && /3 passed/.test(fs.readFileSync(logC, 'utf8')), r.stdout + r.stderr);
  const before = recs();
  // fault 3a: the dependency is changed, the script is not (the auditor's forgery: 999 passed (forged))
  fs.writeFileSync(path.join(wt, 'tests', 'helper.mjs'), "export const summary = () => '[doctest] test cases: 999 | 999 passed | 0 failed | 0 skipped (forged)';\n");
  r = run('tests/test_real.mjs');
  t('C REFUSE: a direct import (helper.mjs) changed, the script unchanged (audit fault 3a): exit 3, names the helper, no record', r.status === 3 && /helper\.mjs/.test(r.stderr) && /differs from the merge base/.test(r.stderr) && recs() === before, r.stdout + r.stderr);
  gitq(wt, 'checkout', '--', 'tests/helper.mjs');
  fs.writeFileSync(path.join(wt, 'tests', 'deep.mjs'), "export const deep = 2;\n");
  r = run('tests/test_real.mjs');
  t('C REFUSE: an import of an import (deep.mjs, two levels down) changed', r.status === 3 && /deep\.mjs/.test(r.stderr) && recs() === before, r.stdout + r.stderr);
  gitq(wt, 'checkout', '--', 'tests/deep.mjs');
  fs.writeFileSync(path.join(wt, 'tests', 'mid.mjs'), "import { deep } from './deep.mjs'; import './added.mjs'; export const mid = deep;\n"); fs.writeFileSync(path.join(wt, 'tests', 'added.mjs'), 'console.log("[doctest] test cases: 7 | 7 passed | 0 failed | 0 skipped (added)");\n');
  r = run('tests/test_real.mjs');
  t('C REFUSE: a helper added on the branch and imported through a changed import', r.status === 3 && recs() === before, r.stdout + r.stderr);
  gitq(wt, 'checkout', '--', 'tests/mid.mjs'); fs.rmSync(path.join(wt, 'tests', 'added.mjs'), { force: true });
  r = run('tests/test_real.mjs');
  t('C ALLOW: everything restored to main\'s bytes runs again', r.status === 0 && recs() === before + 1, r.stdout + r.stderr);
  // a script that spawns processes runs files the literal graph cannot name: tools/ must be unchanged too
  r = run('tests/test_spawn.mjs');
  t('C a spawning script with tools/ unchanged runs', r.status === 0, r.stdout + r.stderr);
  const n1 = recs();
  fs.writeFileSync(path.join(wt, 'tools', 'util.mjs'), 'export const u = 2;\n');
  r = run('tests/test_spawn.mjs');
  t('C REFUSE: a script that spawns processes while a file under tools/ changed (it may run that file)', r.status === 3 && /tools\//.test(r.stderr) && recs() === n1, r.stdout + r.stderr);
  r = run('tests/test_real.mjs');
  t('C ALLOW: a script with only literal imports is not held back by a change under tools/ it cannot reach', r.status === 0, r.stdout + r.stderr);
  gitq(wt, 'checkout', '--', 'tools/util.mjs');
  // fault 3b: a preload and the child's environment
  const pre = path.join(scratch, 'pre.mjs'); fs.writeFileSync(pre, "console.log('[doctest] test cases: 777 | 777 passed | 0 failed | 0 skipped (child preload)');\n");
  const n2 = recs();
  r = run('tests/test_real.mjs', { NODE_OPTIONS: `--import ${pathToFileURL(pre).href}` });
  t('C REFUSE: NODE_OPTIONS with --import around run_suite (audit fault 3b): exit 2, no record, the log is not the preload\'s', r.status === 2 && /NODE_OPTIONS/.test(r.stderr) && recs() === n2, r.stdout + r.stderr);
  r = spawnSync(process.execPath, ['--import', pathToFileURL(pre).href, rs, wt, '--script', path.join(wt, 'tests', 'test_real.mjs'), '--round', 'rc'], { encoding: 'utf8', env: prodEnv, timeout: 60000 });
  t('C REFUSE: node --import <x> tools/run_suite.mjs: exit 2, no record', r.status === 2 && recs() === n2, r.stdout + r.stderr);
  r = run('tests/test_env.mjs', { FOO_X: 'leak', CP_TEST_KEEP: 'kept' });
  const envLine = (() => { try { return JSON.parse(fs.readFileSync(logC, 'utf8').trim().split('\n').pop()); } catch { return {}; } })();
  t('C the child runs in a clean environment: FOO_X is gone, a CP_* variable is kept, no NODE_OPTIONS, no execArgv', r.status === 0 && envLine.foo === '' && envLine.keep === 'kept' && envLine.no === '' && Array.isArray(envLine.ex) && envLine.ex.length === 0, JSON.stringify(envLine) + r.stderr);
  r = run('tests/test_env.mjs', { NODE_PATH: '/x' });
  t('C REFUSE: NODE_PATH set around run_suite', r.status === 2, r.stdout + r.stderr);
  // fault 3c: the gcc exe is the one the CMake build produced
  const gcc = (e = {}) => spawnSync(process.execPath, [rs, wt, '--round', 'rg', '--timeout', '60'], { encoding: 'utf8', env: { ...prodEnv, ...e }, timeout: 120000 });
  const bdir = path.join(wt, 'build'); const exe = path.join(bdir, 'tests', 'sim_core_tests.exe');
  mk(path.join(bdir, 'tests')); fs.copyFileSync(process.execPath, exe);
  const n3 = recs();
  r = gcc();
  t('C REFUSE: a dropped-in sim_core_tests.exe with no CMakeCache.txt (audit fault 3c): exit 3, no record', r.status === 3 && /CMakeCache/.test(r.stderr) && recs() === n3, r.stdout + r.stderr);
  fs.writeFileSync(path.join(bdir, 'CMakeCache.txt'), `CMAKE_HOME_DIRECTORY:INTERNAL=${fwd(path.join(tmp, 'somewhere_else'))}\n`);
  r = gcc();
  t('C REFUSE: a CMakeCache.txt whose source dir is another tree', r.status === 3 && /not this worktree/.test(r.stderr) && recs() === n3, r.stdout + r.stderr);
  fs.writeFileSync(path.join(bdir, 'CMakeCache.txt'), `CMAKE_HOME_DIRECTORY:INTERNAL=${fwd(wt)}\n`);
  r = gcc();
  t('C REFUSE: the right CMakeCache.txt but no object file under build/tests', r.status === 3 && /object files/.test(r.stderr) && recs() === n3, r.stdout + r.stderr);
  const objDir = path.join(bdir, 'tests', 'CMakeFiles', 'sim_core_tests.dir'); mk(objDir); const obj = path.join(objDir, 'a.cpp.obj'); fs.writeFileSync(obj, 'o');
  const old = new Date(Date.now() - 3600 * 1000); fs.utimesSync(exe, old, old);
  r = gcc();
  t('C REFUSE: the exe is older than the newest object file (stale or dropped in before the objects)', r.status === 3 && /older than the newest object/.test(r.stderr) && recs() === n3, r.stdout + r.stderr);
  const nw = new Date(); fs.utimesSync(exe, nw, nw); fs.utimesSync(obj, new Date(Date.now() - 60000), new Date(Date.now() - 60000));
  r = gcc();
  t('C ALLOW: CMakeCache of this worktree, objects, exe newer than them: runs and leaves a record naming the exe hash', r.status === 0 && recs() === n3 + 1 && (() => { const last = JSON.parse(fs.readFileSync(path.join(stateC, 'suite_records.jsonl'), 'utf8').trim().split('\n').pop()); return /^[0-9a-f]{64}$/.test(last.binarySha || ''); })(), r.stdout + r.stderr);
  void sr;
}

// ================================================================ D  ruling 3: bulk operations over the home folder
console.log('gates15 D: the home folder (any folder above ~/.roadworld-gate) as the whole source of a copy, move, archive, recursive search; Grep and Glob rooted there; git on it');
{
  const r = R('D'), a = A('D');
  mk(path.join(home, '.roadworld-gate')); fs.writeFileSync(path.join(home, '.roadworld-gate', 'ack_secret'), 'deadbeef');
  const dest = `${SC}/q`;
  r('cp -r ~ <dest> (audit fault 2)', bash(`cp -r ~ ${dest}`));
  r('cp -r $HOME <dest>', bash(`cp -r $HOME ${dest}`));
  r('cp -r <home absolute path> <dest>', bash(`cp -r ${H} ${dest}`));
  r('cd ~ && cp -r . <dest>', bash(`cd ~ && cp -r . ${dest}`));
  r('cd ~ && cp -r * <dest> (a glob over the home folder)', bash(`cd ~ && cp -r * ${dest}`));
  r('mv ~ <dest>', bash(`mv ~ ${dest}`));
  r('tar cf x.tar ~', bash(`tar cf ${SC}/x.tar ~`));
  r('tar cf x.tar <drive-level parent of home>', bash(`tar cf ${SC}/x.tar ${fwd(path.dirname(home))}`));
  r('zip -r h.zip ~', bash('zip -r h.zip ~'));
  r('rsync -a ~/ <dest>', bash(`rsync -a ~/ ${dest}/`));
  r('rm -rf ~ (a recursive delete of the home folder)', bash('rm -rf ~'));
  r('grep -r . ~ (audit fault 2)', bash('grep -r . ~'));
  r('grep -rl deadbeef <parent of home>', bash(`grep -rl deadbeef ${fwd(path.dirname(home))}`));
  r('cd ~ && grep -rn x (the default root is the home folder)', bash('cd ~ && grep -rn x'));
  r('rg --hidden x ~', bash('rg --hidden x ~'));
  r('find ~ -name ack_secret', bash('find ~ -name ack_secret'));
  r('find ~ -type f | xargs cat', bash('find ~ -type f | xargs cat'));
  r('find ~ -name ack_secret -exec cat {} ;', bash('find ~ -name ack_secret -exec cat {} \\;'));
  r('ls -R ~', bash('ls -R ~'));
  r('du ~', bash('du ~'));
  r('echo ~/.roadworld-gate/ack_secret | xargs cat (audit fault 2)', bash('echo ~/.roadworld-gate/ack_secret | xargs cat'));
  r('git clone ~/.roadworld-gate <dest> (audit fault 2)', bash(`git clone ~/.roadworld-gate ${dest}`));
  r('git -C ~/.roadworld-gate init (audit fault 2)', bash('git -C ~/.roadworld-gate init'));
  r('git -C ~ status', bash('git -C ~ status'));
  r('git clone ~ <dest>', bash(`git clone ~ ${dest}`));
  r('Compress-Archive -Path $HOME (audit fault 2)', ps('Compress-Archive -Path $HOME -DestinationPath x.zip'));
  r('gci -Recurse $HOME -Filter ack_secret | gc (audit fault 2)', ps('gci -Recurse $HOME -Filter ack_secret | gc'));
  r('Get-ChildItem -Recurse ~', ps('Get-ChildItem -Recurse ~'));
  r('Copy-Item -Recurse $env:USERPROFILE C:\\x', ps('Copy-Item -Recurse $env:USERPROFILE C:\\x'));
  r('robocopy $HOME C:\\x /E', ps('robocopy $HOME C:\\x /E'));
  r('the Grep tool with path = the home folder, pattern . (audit fault 2)', tool('Grep', { pattern: '.', path: home }));
  r('the Grep tool with path = home, glob ack_*', tool('Grep', { pattern: '[a-f0-9]{8}', path: home, glob: 'ack_*' }));
  r('the Glob tool **/ack_secret under the home folder (audit fault 2)', tool('Glob', { pattern: '**/ack_secret', path: home }));
  r('the Glob tool .roadworld-gate/* under the home folder', tool('Glob', { pattern: '.roadworld-gate/*', path: home }));
  a('the Grep tool over the home folder limited to *.md files', tool('Grep', { pattern: 'x', path: home, glob: '*.md' }));
  a('the Grep tool over the home folder with type js', tool('Grep', { pattern: 'x', path: home, type: 'js' }));
  a('the Glob tool **/*.mjs under the home folder (cannot end on ack_secret)', tool('Glob', { pattern: '**/*.mjs', path: home }));
  a('the Grep and Glob tools on the repo', tool('Grep', { pattern: 'x', path: proj }));
  a('ls ~', bash('ls ~'));
  a('cd ~ && ls -la', bash('cd ~ && ls -la'));
  a('cat ~/.gitconfig', bash('cat ~/.gitconfig'));
  a('find ~ -maxdepth 1 -name "*.json" (one level)', bash('find ~ -maxdepth 1 -name "*.json"'));
  a('find ~ -name "*.png" (a name filter that cannot be ack_secret)', bash('find ~ -name "*.png"'));
  a('grep -rn x ~/Documents', bash('grep -rn x ~/Documents'));
  a('grep -r --include=*.md x ~ (files limited to *.md)', bash('grep -r --include=*.md x ~'));
  a('cp -r <repo>/docs ~/AppData/x (a sub-folder destination)', bash(`cp -r ${P}/docs ~/AppData/x`));
  a('git clone <repo> <dest>', bash(`git clone ${P} ${SC}/cl`));
  a('git commit -m "fix .roadworld-gate handling"', bash('git commit -m "fix .roadworld-gate handling"'));
  a('Get-ChildItem $HOME (not recursive)', ps('Get-ChildItem $HOME'));
  a('Compress-Archive of a repo folder', ps(`Compress-Archive -Path ${P}\\docs -DestinationPath x.zip`));
  a('the main session (no agent id) copies the home folder', spawnSync(process.execPath, [path.join(src, 'tools', 'hooks', 'state_guard.mjs')], { input: JSON.stringify({ hook_event_name: 'PreToolUse', tool_name: 'Bash', tool_input: { command: `cp -r ~ ${dest}` }, cwd: proj, session_id: 's15' }), encoding: 'utf8', env: guardEnv() }));
  // git clean -x of the main checkout deletes the ignored state folder
  r('git -C <main> clean -fdx (deletes .claude/state)', bash(`git -C ${P} clean -fdx`));
  a('git -C <elsewhere> clean -fd', bash(`git -C ${SC} clean -fd`));
}

// ================================================================ E  ruling 4: the lane lock is decided by gate.log
console.log('gates15 E: the lane lock holder is the fold of the lane lines in gate.log; owner.txt is a hint; preloads of the tool are refused');
{
  const ds = path.join(tmp, 'state-e'); mk(ds);
  const toolF = path.join(src, 'tools', 'lane_lock.mjs');
  const lt = (args, e = {}) => spawnSync(process.execPath, [toolF, ...args], { encoding: 'utf8', env: baseEnv({ CP_GATE_STATE_DIR: ds, ...e }), timeout: 30000 });
  const lock = path.join(ds, 'unreal_lane.lock'); const glog = path.join(ds, 'gate.log');
  const laneLines = () => (fs.existsSync(glog) ? fs.readFileSync(glog, 'utf8').split('\n').filter((l) => l.includes('"gate":"lane"')) : []);
  t('E status of a free lane says free', /^free/.test(lt(['status']).stdout));
  let r = lt(['take', '--agent', 'ag5', '--round', 'm3', '--purpose', 'playtest of the road tool']);
  t('E take appends a gate.log lane line (agent, id, round, purpose) and writes the hint owner.txt', r.status === 0 && laneLines().length === 1 && /"agent":"ag5"/.test(laneLines()[0]) && /"decision":"take"/.test(laneLines()[0]) && /^agent: ag5$/m.test(fs.readFileSync(path.join(lock, 'owner.txt'), 'utf8')), r.stderr + laneLines().join('|'));
  // the audit's spoof: a hand-written owner.txt naming another agent must change nothing
  fs.writeFileSync(path.join(lock, 'owner.txt'), 'agent: ag9\nround: x\n');
  const st = lt(['status']).stdout;
  t('E a hand-written owner.txt naming ag9 does not change who status says holds the lane (still ag5)', /agent: ag5/.test(st) && !/agent: ag9\nround/.test(st) && /owner\.txt says ag9/.test(st), st);
  r = lt(['take', '--agent', 'ag9', '--round', 'x', '--purpose', 'taking it because owner.txt says me']);
  t('E ag9 cannot take the lane by forging owner.txt: exit 1, names ag5, no new lane line', r.status === 1 && /ag5/.test(r.stderr) && laneLines().length === 1, r.stderr);
  r = lt(['release', '--agent', 'ag9']);
  t('E ag9 cannot release it either (the forged owner.txt names ag9, the log says ag5)', r.status === 1 && /ag5/.test(r.stderr) && laneLines().length === 1, r.stderr);
  fs.rmSync(lock, { recursive: true, force: true });
  t('E deleting the lock folder does not free the lane (the log decides)', /agent: ag5/.test(lt(['status']).stdout) && lt(['take', '--agent', 'ag9', '--round', 'x', '--purpose', 'taking it after the folder was deleted']).status === 1);
  t('E the holder taking it again is idempotent', lt(['take', '--agent', 'ag5', '--round', 'm3', '--purpose', 'again by the holder']).status === 0 && laneLines().length === 1);
  r = lt(['release', '--agent', 'main']);
  t('E the main session (--agent main) releases any holder; a release line is logged; status says free', r.status === 0 && laneLines().length === 2 && /^free/.test(lt(['status']).stdout), r.stderr);
  // a lost race: two take lines in the log, the first wins
  fs.appendFileSync(glog, JSON.stringify({ ts: new Date().toISOString(), gate: 'lane', decision: 'take', agent: 'a1', id: 'id1', round: 'r', purpose: 'p' }) + '\n' + JSON.stringify({ ts: new Date().toISOString(), gate: 'lane', decision: 'take', agent: 'a2', id: 'id2', round: 'r', purpose: 'p' }) + '\n');
  t('E two take lines in a row: the first holds, the second is ignored (a lost race)', /agent: a1/.test(lt(['status']).stdout));
  fs.appendFileSync(glog, JSON.stringify({ ts: new Date().toISOString(), gate: 'lane', decision: 'release', agent: 'a2', round: 'r' }) + '\n');
  t('E a release line from a non-holder is ignored', /agent: a1/.test(lt(['status']).stdout));
  fs.appendFileSync(glog, JSON.stringify({ ts: new Date().toISOString(), gate: 'lane', decision: 'release', agent: 'a1', round: 'r' }) + '\n');
  t('E the holder\'s release frees it', /^free/.test(lt(['status']).stdout));
  // preloads
  r = lt(['status'], { NODE_OPTIONS: '--import ' + pathToFileURL(path.join(scratch, 'innocent.mjs')).href }); t('E the tool refuses to run under NODE_OPTIONS (a preload could rewrite argv)', r.status === 2 && /NODE_OPTIONS/.test(r.stderr), r.stderr);
  r = spawnSync(process.execPath, ['--import', pathToFileURL(path.join(scratch, 'innocent.mjs')).href, toolF, 'status'], { encoding: 'utf8', env: baseEnv({ CP_GATE_STATE_DIR: ds }) }); t('E the tool refuses to run under node --import', r.status === 2, r.stderr);
  // the guard, with the real hook script
  const lockRel = '.claude/state/unreal_lane.lock'; const rr = R('E'), aa = A('E');
  const stateP = path.join(proj, '.claude', 'state');
  rr('the audit\'s relative-after-cd form: cd <lock> && echo \'agent: other\' > owner.txt is harmless now; but a gate.log write is refused: echo {"gate":"lane"} >> gate.log', bash(`echo '{"gate":"lane","decision":"take","agent":"x","id":"1"}' >> ${P}/.claude/state/gate.log`));
  rr('tee -a gate.log with a forged lane line', bash(`echo '{"gate":"lane"}' | tee -a ${P}/.claude/state/gate.log`));
  rr('the Write tool on gate.log', tool('Write', { file_path: path.join(stateP, 'gate.log'), content: '{"gate":"lane"}\n' }));
  rr('node --import ./x.mjs tools/lane_lock.mjs take --agent ag5 (the audit\'s preload form)', bash('node --import ./x.mjs tools/lane_lock.mjs take --agent ag5 --round r --purpose "playtest"'));
  rr('NODE_OPTIONS=\'--import ./x.mjs\' node tools/lane_lock.mjs take --agent ag5', bash('NODE_OPTIONS=\'--import ./x.mjs\' node tools/lane_lock.mjs take --agent ag5 --round r --purpose "playtest"'));
  rr('lane_lock take --agent ag9 by agent ag5', bash('node tools/lane_lock.mjs take --agent ag9 --round r --purpose "playtest"'));
  aa('lane_lock take --agent ag5 by agent ag5', bash('node tools/lane_lock.mjs take --agent ag5 --round r --purpose "playtest"'));
  aa('lane_lock status', bash('node tools/lane_lock.mjs status'));
  // the owner check of an rm of the lock folder uses the log, not owner.txt
  mk(path.join(stateP, 'unreal_lane.lock')); fs.writeFileSync(path.join(stateP, 'unreal_lane.lock', 'owner.txt'), 'agent: ag7\n');
  fs.writeFileSync(path.join(stateP, 'gate.log'), JSON.stringify({ ts: new Date().toISOString(), gate: 'lane', decision: 'take', agent: 'ag5', id: 'zz', round: 'r', purpose: 'p' }) + '\n');
  rr('rm -r <lock> by ag7 whose forged owner.txt names ag7 while the log says ag5 holds it', bash(`rm -r ${P}/${lockRel}`, 'ag7'));
  aa('rm -r <lock> by ag5, the holder per the log', bash(`rm -r ${P}/${lockRel}`, 'ag5'));
}

// ================================================================ F  ruling 5: the table
console.log('gates15 F: the NO-CONTRACT descriptions the gates14 notice listed as unmapped are in the table');
{
  const table = JSON.parse(fs.readFileSync(path.join(src, 'tools', 'hooks', 'live_launch_categories.json'), 'utf8')).rows;
  for (const d of ['Clerk: suitespeed brief and bookkeeping', 'M6 research: lighting columns, cover', 'Clerk: update pvetrees task, worktree', 'Clerk: tjunction7 waiver keys, re-check', 'Clerk: draft m6lights task file', 'Performance architecture review (whole pipeline)', 'Clerk: free disk from finished worktrees', 'Trace photo asphalt edges', 'Clerk: remove merged old worktrees']) t(`F "${d}" is in the table with a launch form`, table.some(([x, c, w]) => x === d && c && w), '');
}

// ---------------------------------------------------------------- end
t('no line of this run reached the real gate.log', isolation.leaks().length === 0, isolation.leaks().join('\n'));
console.log(`\n${pass} passed, ${fail} failed`);
if (fail) console.log(failedNames.map((n) => ' - ' + n).join('\n'));
cleanupTmp(tmp);
process.exit(fail ? 1 : 0);
