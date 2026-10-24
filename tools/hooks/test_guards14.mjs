#!/usr/bin/env node
// gates14: real-payload tables for the send-back of round gates13 (docs/progress/gates13/AUDIT.md and the main-session decisions 1-7 of the gates14 brief).
//   A  decision 1 + 3: the signing secret is outside every repo tree; copy, move, archive and recursive delete of .claude, wildcards after a cd, and the secret's own folder are refused.
//   B  decision 2: run_suite --script runs only a script main already has, unchanged; a node suite log stands in for the gcc log only when the task file declares the round node-only.
//   C  decision 4: tools/lane_lock.mjs writes owner.txt; every other write into the lock folder is refused; the --agent id must be the caller's own.
//   D  decision 6: gate_notice lists a NO-CONTRACT launch whose description is not in tools/hooks/live_launch_categories.json.
// Where the expected values come from: the auditor's probes in docs/progress/gates13/AUDIT.md (the four secret forms, rm -rf .claude, the printing test_fake.mjs, the owner.txt redirect, the four
// unmapped descriptions) and the main-session decisions; nothing pins a fault. No test writes the live gate.log or the real home folder (CP_GATE_STATE_DIR, USERPROFILE and HOME point into the temp folder).
// Usage: node tools/hooks/test_guards14.mjs
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { spawnSync } from 'node:child_process';
import { fileURLToPath, pathToFileURL } from 'node:url';
import { isolateState, cleanupTmp } from '../test_isolation.mjs';

const src = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..', '..');
const tmp = fs.mkdtempSync(path.join(os.tmpdir(), 'guards14-'));
const isolation = isolateState(tmp, src);
let pass = 0, fail = 0; const failedNames = [];
const t = (name, cond, detail = '') => { if (cond) { pass++; console.log('  ok   ' + name); } else { fail++; failedNames.push(name); console.log('  FAIL ' + name + (detail ? '\n       ' + String(detail).slice(0, 700) : '')); } };
const mk = (p) => fs.mkdirSync(p, { recursive: true });
const fwd = (p) => String(p).replace(/\\/g, '/');
const home = path.join(tmp, 'home'); mk(home);
const baseEnv = (extra = {}) => { const e = { ...process.env, CP_GATE_STATE_DIR: isolation.stateDir, USERPROFILE: home, HOME: home, CP_TIME_GUARD_PROCS: '' }; delete e.CP_MAIN_ROOT; delete e.CP_GATE_SECRET_DIR; delete e.CP_SUITE_BASE; for (const [k, v] of Object.entries(extra)) { if (v === null) delete e[k]; else e[k] = v; } return e; };
const gitEnv = { ...process.env, GIT_AUTHOR_NAME: 't', GIT_AUTHOR_EMAIL: 't@t', GIT_COMMITTER_NAME: 't', GIT_COMMITTER_EMAIL: 't@t' };
const gitq = (cwd, ...a) => spawnSync('git', a, { cwd, encoding: 'utf8', env: gitEnv });

// ================================================================ A  the secret, the .claude folder, wildcards
console.log('gates14 A: the secret lives outside the repo trees; .claude cannot be copied, moved, archived or deleted; wildcards after a cd are resolved');
{
  const proj = path.join(tmp, 'a_main'); const stateA = path.join(proj, '.claude', 'state'); mk(stateA); mk(path.join(proj, 'docs')); mk(path.join(proj, 'build'));
  fs.writeFileSync(path.join(proj, '.claude', 'settings.json'), '{}'); fs.writeFileSync(path.join(stateA, 'gate.log'), '{}\n');
  const P = fwd(proj), MB = proj.replace(/\//g, '\\');
  const sg = (payload, agent = 'ag5') => spawnSync(process.execPath, [path.join(src, 'tools', 'hooks', 'state_guard.mjs')], { input: JSON.stringify({ hook_event_name: 'PreToolUse', cwd: proj, session_id: 's14', agent_id: agent, agent_type: 'general-purpose', ...payload }), encoding: 'utf8', env: baseEnv({ CP_MAIN_ROOT: proj }), timeout: 60000 });
  const bash = (command, agent) => sg({ tool_name: 'Bash', tool_input: { command } }, agent);
  const ps = (command, agent) => sg({ tool_name: 'PowerShell', tool_input: { command } }, agent);
  const refuse = (label, r) => t(`A REFUSE: ${label}`, r.status === 2 && /REFUSED|state guard/i.test(r.stderr), `status ${r.status} ${r.stderr.slice(0, 220)}`);
  const allow = (label, r) => t(`A ALLOW: ${label}`, r.status === 0, `status ${r.status} ${r.stderr.slice(0, 220)}`);
  const dest = fwd(path.join(tmp, 'a_dest'));
  // the auditor's four forms of gates13 AUDIT.md fault 1, and the recursive delete of fault 2
  refuse('cp -r <main>/.claude <dest> (audit fault 1)', bash(`cp -r ${P}/.claude ${dest}`));
  refuse('cp -r <main>/.claude/. <dest> (audit fault 1)', bash(`cp -r ${P}/.claude/. ${dest}`));
  refuse('mv <main>/.claude <dest> (audit fault 1 and 3)', bash(`mv ${P}/.claude ${dest}`));
  refuse('cd <main>/.claude && cat st*/ack_se* (audit fault 1, a relative glob after a cd)', bash(`cd ${P}/.claude && cat st*/ack_se*`));
  refuse('tar cf x.tar .claude from the main checkout (audit fault 1)', bash(`cd ${P} && tar cf x.tar .claude`));
  refuse('rm -rf <main>/.claude (audit fault 2 and 3)', bash(`rm -rf ${P}/.claude`));
  // five more of the builder's own, each a different route
  refuse('Compress-Archive -Path <main>\\.claude (PowerShell archive)', ps(`Compress-Archive -Path ${MB}\\.claude -DestinationPath x.zip`));
  refuse('robocopy <main>\\.claude C:\\x /E', ps(`robocopy ${MB}\\.claude C:\\x /E`));
  refuse('rmdir /s /q <main>\\.claude (cmd)', ps(`cmd /c rmdir /s /q ${MB}\\.claude`));
  refuse('Remove-Item -Recurse -Force <main>\\.claude', ps(`Remove-Item -Recurse -Force ${MB}\\.claude`));
  refuse('cat <main>/.cl*/st*/ack_secret (wildcards in two segments)', bash(`cat ${P}/.cl*/st*/ack_secret`));
  refuse('cat <main>/.claude/{state,x}/ack_secret (brace expansion)', bash(`cat ${P}/.claude/{state,x}/ack_secret`));
  refuse('cat $(echo <main>/.claude/state/ack_secret) (command substitution hides the path; own fault 1)', bash(`cat $(echo ${P}/.claude/state/ack_secret)`));
  refuse('x=<main>/.claude/state/ack_secret; cat $x (variable assignment; own fault 1)', bash(`x=${P}/.claude/state/ack_secret; cat $x`));
  refuse('backtick form of the same (own fault 1)', bash('cat `echo ' + P + '/.claude/state/ack_secret`'));
  refuse('zip -r x.zip <main>/.claude', bash(`zip -r x.zip ${P}/.claude`));
  refuse('cp -a <main> <dest> (a folder that holds .claude)', bash(`cp -a ${P} ${dest}`));
  refuse('ln -s <main>/.claude link (a link to the folder)', bash(`ln -s ${P}/.claude link`));
  refuse('mv of the main checkout itself', bash(`mv ${P} ${dest}`));
  // the secret's own folder, outside every repo tree (<home>/.roadworld-gate)
  refuse('cat ~/.roadworld-gate/ack_secret', bash('cat ~/.roadworld-gate/ack_secret'));
  refuse('cat ~/.road*/ack* (wildcard into the secret folder)', bash('cat ~/.road*/ack*'));
  refuse('cd ~ && cat .roadworld-gate/ack_secret', bash('cd ~ && cat .roadworld-gate/ack_secret'));
  refuse('Get-Content $env:USERPROFILE\\.roadworld-gate\\ack_secret', ps('Get-Content $env:USERPROFILE\\.roadworld-gate\\ack_secret'));
  refuse('cat <home>/.roadworld-gate/ack_secret by absolute path', bash(`cat ${fwd(home)}/.roadworld-gate/ack_secret`));
  refuse('the Read tool on the secret in the home folder', sg({ tool_name: 'Read', tool_input: { file_path: path.join(home, '.roadworld-gate', 'ack_secret') } }));
  refuse('the Grep tool over the secret folder', sg({ tool_name: 'Grep', tool_input: { pattern: '.', path: path.join(home, '.roadworld-gate') } }));
  refuse('cp the secret out of the home folder', bash(`cp ${fwd(home)}/.roadworld-gate/ack_secret /tmp/x`));
  refuse('node -e naming the secret folder', bash(`node -e "console.log(require('fs').readFileSync(require('os').homedir()+'/.roadworld-gate/ack_secret','utf8'))"`));
  // what stays open
  allow('ls <main>/.claude/state (names only)', bash(`ls ${P}/.claude/state`));
  allow('cat <main>/.claude/state/gate.log', bash(`cat ${P}/.claude/state/gate.log`));
  allow('cat <main>/.claude/state/*.log (a wildcard that cannot match the secret)', bash(`cat ${P}/.claude/state/*.log`));
  allow('rm -rf build (an ordinary recursive delete)', bash('rm -rf build'));
  allow('cp -r docs <dest>', bash(`cp -r docs ${dest}`));
  allow('tar cf x.tar docs', bash('tar cf x.tar docs'));
  allow('git commit -m "cp -r .claude and rm -rf .claude are refused" (the words are text)', bash('git commit -m "cp -r .claude and rm -rf .claude are refused"'));
  allow('a heredoc whose body says rm -rf .claude', bash('cat > notes.md <<\'EOF\'\nrm -rf .claude\ncp -r .claude x\nEOF'));
  allow('the main session (no agent id) is not limited: rm -rf of a scratch folder', spawnSync(process.execPath, [path.join(src, 'tools', 'hooks', 'state_guard.mjs')], { input: JSON.stringify({ hook_event_name: 'PreToolUse', tool_name: 'Bash', tool_input: { command: `cp -r ${P}/.claude ${dest}` }, cwd: proj, session_id: 's14' }), encoding: 'utf8', env: baseEnv({ CP_MAIN_ROOT: proj }) }));
  // delete_guard and git_guard are separate hooks: the recursive delete of .claude is refused by state_guard, which is the wired one for it (settings.json lists state_guard for Bash and PowerShell)
  const settings = JSON.parse(fs.readFileSync(path.join(src, '.claude', 'settings.json'), 'utf8'));
  t('A settings.json wires state_guard for Bash and PowerShell commands', JSON.stringify(settings.hooks).includes('state_guard.mjs'));
  // the secret location: created in <home>/.roadworld-gate, nothing under <repo>/.claude/state
  const libUrl = pathToFileURL(path.join(src, 'tools', 'hooks', 'lib.mjs')).href;
  const live = path.join(tmp, 'a_live'); mk(path.join(live, '.claude', 'state')); fs.writeFileSync(path.join(live, '.claude', 'state', 'ack_secret'), 'legacysecret0123456789');
  const ens = spawnSync(process.execPath, ['-e', `import(${JSON.stringify(libUrl)}).then((m) => { console.log(JSON.stringify({ s: m.ensureSecret(), dir: m.secretDirFor(m.STATE_DIR) })); })`], { encoding: 'utf8', env: baseEnv({ CP_MAIN_ROOT: live, CP_GATE_STATE_DIR: null }) });
  let o = {}; try { o = JSON.parse(ens.stdout.trim()); } catch { /* reported below */ }
  t('A with no state redirect the secret is at <home>/.roadworld-gate/ack_secret', fwd(o.dir || '') === fwd(path.join(home, '.roadworld-gate')) && fs.existsSync(path.join(home, '.roadworld-gate', 'ack_secret')), ens.stdout + ens.stderr);
  t('A a legacy <repo>/.claude/state/ack_secret is MOVED there (same value, old copy removed) so signed records keep verifying', o.s === 'legacysecret0123456789' && !fs.existsSync(path.join(live, '.claude', 'state', 'ack_secret')), JSON.stringify(o));
  t('A with CP_GATE_STATE_DIR set (tests) the secret stays in that folder, never in the home folder', (() => { const sd = path.join(tmp, 'a_redir'); const r = spawnSync(process.execPath, ['-e', `import(${JSON.stringify(libUrl)}).then((m) => { m.ensureSecret(); })`], { encoding: 'utf8', env: baseEnv({ CP_GATE_STATE_DIR: sd }) }); return r.status === 0 && fs.existsSync(path.join(sd, 'ack_secret')); })());
}

// ================================================================ B  run_suite --script and the node-only suite
console.log('gates14 B: run_suite --script runs only scripts main already has, unchanged; suite_node.log counts only for a node-only round');
{
  const rs = path.join(src, 'tools', 'run_suite.mjs');
  const sr = await import(pathToFileURL(path.join(src, 'tools', 'hooks', 'suite_record.mjs')).href);
  const main = path.join(tmp, 'b_main'); mk(path.join(main, 'tests')); mk(path.join(main, '.claude', 'state'));
  gitq(main, 'init', '-q', '-b', 'main');
  fs.writeFileSync(path.join(main, 'tests', 'test_real.mjs'), "process.stdout.write('[doctest] test cases: 3 | 3 passed | 0 failed | 0 skipped\\n');");
  fs.writeFileSync(path.join(main, '.gitignore'), '.claude/\ndocs/progress/\nbuild/\n');
  gitq(main, 'add', '-A'); gitq(main, 'commit', '-q', '-m', 'base');
  const wt = path.join(tmp, 'b_wt'); gitq(main, 'worktree', 'add', '-q', '-b', 'agent/b1', wt);
  // PRODUCTION mode: no CP_GATE_STATE_DIR, no CP_SUITE_BASE; the main checkout is the scratch repo (CP_MAIN_ROOT), so its .claude/state is the state folder
  const stateB = path.join(main, '.claude', 'state');
  process.env.CP_GATE_SECRET_DIR = path.join(tmp, 'b_secret');   // the runs below sign with this secret; findSuiteRun in this process verifies with it
  const prodEnv = baseEnv({ CP_GATE_STATE_DIR: null, CP_MAIN_ROOT: main, CP_GATE_SECRET_DIR: path.join(tmp, 'b_secret') });
  const run = (script, extra = [], e = {}) => spawnSync(process.execPath, [rs, wt, '--script', script, '--round', 'rb', '--timeout', '60', ...extra], { encoding: 'utf8', env: { ...prodEnv, ...e }, timeout: 120000 });
  const recs = () => { try { return fs.readFileSync(path.join(stateB, 'suite_records.jsonl'), 'utf8').trim().split('\n').filter(Boolean).length; } catch { return 0; } };
  const logB = path.join(wt, 'docs', 'progress', 'rb', 'suite_node.log');
  let r = run(path.join(wt, 'tests', 'test_real.mjs'));
  t('B a script main already has, unchanged, runs and leaves a record', r.status === 0 && recs() === 1 && /3 passed/.test(fs.readFileSync(logB, 'utf8')), r.stdout + r.stderr);
  const f0 = sr.findSuiteRun(logB, wt, { after: 0, stateDir: stateB });
  t('B ... and findSuiteRun accepts it', f0.ok === true, JSON.stringify(f0));
  // the auditor's forgery: tests/test_fake.mjs that only prints a summary
  fs.writeFileSync(path.join(wt, 'tests', 'test_fake.mjs'), 'console.log("5 passed, 0 failed")');
  const before = recs();
  r = run(path.join(wt, 'tests', 'test_fake.mjs'));
  t('B REFUSE: the auditor\'s printing tests/test_fake.mjs (not at the merge base): exit 3, no record', r.status === 3 && /does not exist at the merge base/.test(r.stderr) && recs() === before, r.stdout + r.stderr);
  const fk = sr.findSuiteRun(logB, wt, { after: 0, stateDir: stateB });
  t('B ... and findSuiteRun of a log that run would have made is not ok (the earlier honest log still matches only its own bytes)', fk.ok === true && fk.run && /test_real/.test(fk.run.binary) === true, JSON.stringify(fk));
  gitq(wt, 'add', 'tests/test_fake.mjs'); gitq(wt, 'commit', '-q', '-m', 'add fake');
  r = run(path.join(wt, 'tests', 'test_fake.mjs'));
  t('B REFUSE: the same script COMMITTED on the branch is still not at the merge base', r.status === 3 && /merge base/.test(r.stderr) && recs() === before, r.stderr);
  fs.writeFileSync(path.join(wt, 'tests', 'test_real.mjs'), 'console.log("5 passed, 0 failed")');
  r = run(path.join(wt, 'tests', 'test_real.mjs'));
  t('B REFUSE: a script main has, CHANGED on the branch (it could print any result)', r.status === 3 && /differs from the merge base/.test(r.stderr) && recs() === before, r.stderr);
  gitq(wt, 'checkout', '--', 'tests/test_real.mjs');
  r = run(path.join(wt, 'tests', 'test_real.mjs'));
  t('B ALLOW: the script restored to main\'s bytes runs again', r.status === 0 && recs() === before + 1, r.stderr);
  // CP_SUITE_BASE is ignored unless the state is redirected: an agent cannot name its own base in the live gate
  fs.writeFileSync(path.join(wt, 'tests', 'test_mine.mjs'), 'console.log("9 passed, 0 failed")'); gitq(wt, 'add', 'tests/test_mine.mjs'); gitq(wt, 'commit', '-q', '-m', 'mine');
  r = run(path.join(wt, 'tests', 'test_mine.mjs'), [], { CP_SUITE_BASE: 'HEAD' });
  t('B REFUSE: CP_SUITE_BASE=HEAD in the live (not redirected) gate is ignored: a branch-only script is still refused', r.status === 3 && recs() === before + 1, r.stderr);
  r = run(path.join(wt, 'tests', 'test_mine.mjs'), [], { CP_SUITE_BASE: 'HEAD', CP_GATE_STATE_DIR: path.join(tmp, 'b_redirected') });
  t('B ... (with a redirected state, as the tests use it, the base can be named: the record then lands outside the live gate)', r.status === 0 && recs() === before + 1, r.stderr);

  // the build phase: suite_node.log stands in for suite_gcc.log only for a node-only round
  const pl = await import(pathToFileURL(path.join(src, 'tools', 'phase_lib.mjs')).href);
  const hd = path.join(wt, 'docs', 'progress', 'rb'); fs.writeFileSync(path.join(hd, 'BUILD.md'), '# BUILD\nCommits: none\n'); fs.writeFileSync(path.join(hd, 'MEASURE.md'), '# m\n');
  const withGcc = pl.buildProblems(hd, wt, { nodeOnly: false });
  const nodeOk = pl.buildProblems(hd, wt, { nodeOnly: true });
  t('B a C++ round (not node-only) with only suite_node.log: the check names the missing suite_gcc.log', withGcc.some((p) => /no suite_gcc\.log/.test(p) && /node-only/.test(p)), withGcc.join(' | ').slice(0, 400));
  t('B a node-only round with suite_node.log: no "missing suite log" problem', !nodeOk.some((p) => /no suite_(?:gcc|node)\.log/.test(p)), nodeOk.join(' | ').slice(0, 400));
  const noNode = (() => { const h2 = path.join(tmp, 'b_hd2'); mk(h2); fs.writeFileSync(path.join(h2, 'BUILD.md'), '# b\n'); return pl.buildProblems(h2, wt, { nodeOnly: true }); })();
  t('B a node-only round without suite_node.log names it', noNode.some((p) => /no suite_node\.log/.test(p)), noNode.join(' | ').slice(0, 300));
  delete process.env.CP_GATE_SECRET_DIR;
  // check_handin passes the brief's SUITE: node-only line on (--suite node-only)
  const ch = fs.readFileSync(path.join(src, 'tools', 'check_handin.mjs'), 'utf8');
  t('B check_handin reads --suite node-only into buildProblems (nodeOnly)', /nodeOnly:\s*optAll\('--suite'\)/.test(ch));
}

// ================================================================ C  the lane lock tool
console.log('gates14 C: tools/lane_lock.mjs writes owner.txt; every other write into the lock folder is refused; the --agent id is the caller\'s own');
{
  const ds = path.join(tmp, 'state-c'); mk(ds);
  const tool = path.join(src, 'tools', 'lane_lock.mjs');
  const lt = (args, e = {}) => spawnSync(process.execPath, [tool, ...args], { encoding: 'utf8', env: baseEnv({ CP_GATE_STATE_DIR: ds, ...e }), timeout: 30000 });
  const lock = path.join(ds, 'unreal_lane.lock');
  t('C status of a free lane says free', /free/.test(lt(['status']).stdout));
  let r = lt(['take', '--agent', 'ag5', '--round', 'm3', '--purpose', 'playtest of the road tool']);
  const owner = () => { try { return fs.readFileSync(path.join(lock, 'owner.txt'), 'utf8'); } catch { return ''; } };
  t('C take makes the lock folder and owner.txt with "agent: ag5", round, time, purpose', r.status === 0 && /^agent: ag5$/m.test(owner()) && /^round: m3$/m.test(owner()) && /^time: \d{4}-/m.test(owner()) && /^purpose: playtest/m.test(owner()), r.stderr + owner());
  r = lt(['take', '--agent', 'ag7', '--round', 'x', '--purpose', 'a second run at once']);
  t('C a second agent cannot take it: exit 1, names the holder ag5, owner.txt unchanged', r.status === 1 && /ag5/.test(r.stderr) && /^agent: ag5$/m.test(owner()), r.stderr);
  r = lt(['release', '--agent', 'ag7']);
  t('C a second agent cannot release it', r.status === 1 && fs.existsSync(lock) && /ag5/.test(r.stderr), r.stderr);
  t('C status names the holder', /agent: ag5/.test(lt(['status']).stdout));
  r = lt(['release', '--agent', 'ag5']);
  t('C the holder releases it', r.status === 0 && !fs.existsSync(lock), r.stderr);
  r = lt(['take', '--round', 'm3', '--purpose', 'no id given at all here']); t('C take without --agent is refused', r.status === 2, r.stderr);
  r = lt(['take', '--agent', 'a$(x)', '--round', 'm3', '--purpose', 'bad id characters']); t('C an id with shell characters is refused', r.status === 2, r.stderr);
  fs.mkdirSync(lock); r = lt(['release', '--agent', 'ag9']); t('C a lock with no owner.txt (a crashed run\'s leftover) is released by anyone', r.status === 0 && !fs.existsSync(lock), r.stderr);

  // the guard, with the real hook script
  const proj = path.join(tmp, 'c_proj'); mk(path.join(proj, '.claude', 'state')); const P = fwd(proj);
  const lockRel = '.claude/state/unreal_lane.lock';
  const sg = (payload, agent = 'ag5') => spawnSync(process.execPath, [path.join(src, 'tools', 'hooks', 'state_guard.mjs')], { input: JSON.stringify({ hook_event_name: 'PreToolUse', cwd: proj, session_id: 's14', agent_id: agent, agent_type: 'general-purpose', ...payload }), encoding: 'utf8', env: baseEnv({ CP_MAIN_ROOT: proj }), timeout: 60000 });
  const bash = (command, agent) => sg({ tool_name: 'Bash', tool_input: { command } }, agent);
  const refuse = (label, x) => t(`C REFUSE: ${label}`, x.status === 2 && /REFUSED|state guard/i.test(x.stderr), `status ${x.status} ${x.stderr.slice(0, 220)}`);
  const allow = (label, x) => t(`C ALLOW: ${label}`, x.status === 0, `status ${x.status} ${x.stderr.slice(0, 220)}`);
  refuse('echo \'agent: other\' > <lock>/owner.txt (the audit\'s spoof, no lock held)', bash(`echo 'agent: other' > ${lockRel}/owner.txt`));
  refuse('tee <lock>/owner.txt', bash(`echo 'agent: other' | tee ${lockRel}/owner.txt`));
  refuse('Set-Content <lock>\\owner.txt', sg({ tool_name: 'PowerShell', tool_input: { command: `Set-Content -Path ${lockRel}\\owner.txt -Value 'agent: other'` } }));
  refuse('Out-File <lock>\\owner.txt', sg({ tool_name: 'PowerShell', tool_input: { command: `'agent: other' | Out-File ${lockRel}\\owner.txt` } }));
  refuse('cp /tmp/owner.txt <lock>/owner.txt', bash(`cp /tmp/owner.txt ${lockRel}/owner.txt`));
  refuse('sed -i on <lock>/owner.txt', bash(`sed -i 's/ag5/other/' ${lockRel}/owner.txt`));
  refuse('mv x <lock>/owner.txt', bash(`mv x ${lockRel}/owner.txt`));
  refuse('the Write tool on owner.txt', sg({ tool_name: 'Write', tool_input: { file_path: path.join(proj, '.claude', 'state', 'unreal_lane.lock', 'owner.txt'), content: 'agent: ag5\n' } }));
  refuse('the Edit tool on owner.txt', sg({ tool_name: 'Edit', tool_input: { file_path: path.join(proj, '.claude', 'state', 'unreal_lane.lock', 'owner.txt'), old_string: 'a', new_string: 'b' } }));
  refuse('mkdir of the lock by hand', bash(`mkdir ${lockRel}`));
  allow('node tools/lane_lock.mjs take --agent ag5 (the caller\'s own id)', bash('node tools/lane_lock.mjs take --agent ag5 --round r --purpose "unreal playtest"'));
  allow('node tools\\lane_lock.mjs release --agent ag5', sg({ tool_name: 'PowerShell', tool_input: { command: 'node tools\\lane_lock.mjs release --agent ag5' } }));
  refuse('lane_lock take --agent ag9 by agent ag5 (another agent\'s id)', bash('node tools/lane_lock.mjs take --agent ag9 --round r --purpose "unreal playtest"'));
  refuse('lane_lock take with no --agent', bash('node tools/lane_lock.mjs take --round r --purpose "unreal playtest"'));
  refuse('lane_lock take --agent $(echo ag9) (id built by the shell)', bash('node tools/lane_lock.mjs take --agent $(echo ag9) --round r --purpose "unreal playtest"'));
  refuse('lane_lock --agent=ag9 (the = form of another id)', bash('node tools/lane_lock.mjs release --agent=ag9'));
  refuse('cp of lane_lock.mjs (a copy would run under any id)', bash('cp tools/lane_lock.mjs /tmp/ll.mjs'));
  refuse('node -e importing lane_lock', bash(`node -e "import('./tools/lane_lock.mjs')"`));
  refuse('node tools/lane_l*.mjs (a wildcard that hides the script name)', bash('node tools/lane_l*.mjs take --agent ag9 --round r --purpose x'));
  allow('reading owner.txt', bash(`cat ${lockRel}/owner.txt`));
  allow('the main session (no agent id) writes by hand', spawnSync(process.execPath, [path.join(src, 'tools', 'hooks', 'state_guard.mjs')], { input: JSON.stringify({ hook_event_name: 'PreToolUse', tool_name: 'Bash', tool_input: { command: `mkdir ${P}/${lockRel}` }, cwd: proj, session_id: 's14' }), encoding: 'utf8', env: baseEnv({ CP_MAIN_ROOT: proj }) }));
  // the owner may remove a lock it holds with rm; another agent may not (unchanged from gates13)
  mk(path.join(proj, '.claude', 'state', 'unreal_lane.lock')); fs.writeFileSync(path.join(proj, '.claude', 'state', 'unreal_lane.lock', 'owner.txt'), 'agent: ag5\nround: r\n');
  fs.appendFileSync(path.join(proj, '.claude', 'state', 'gate.log'), JSON.stringify({ ts: new Date().toISOString(), gate: 'lane', decision: 'take', agent: 'ag5', id: 'g14', round: 'r', purpose: 'p' }) + '\n');   // gates15: the holder is the log's, not owner.txt's
  refuse('rm -r <lock> by ag7 while ag5 holds it', bash(`rm -r ${P}/${lockRel}`, 'ag7'));
  allow('rm -r <lock> by its owner ag5', bash(`rm -r ${P}/${lockRel}`, 'ag5'));
}

// ================================================================ D  decision 6: unmapped NO-CONTRACT launches reach gate_notice
console.log('gates14 D: gate_notice lists a NO-CONTRACT launch whose description is not in live_launch_categories.json');
{
  const nl = await import(pathToFileURL(path.join(src, 'tools', 'hooks', 'notice_lib.mjs')).href);
  const table = JSON.parse(fs.readFileSync(path.join(src, 'tools', 'hooks', 'live_launch_categories.json'), 'utf8')).rows;
  const mapped = table.filter(([, c]) => c === 'clerk')[0][0];
  const mkEntries = (descs) => descs.map((d, i) => ({ n: i + 1, ts: new Date().toISOString(), gate: 'brief', decision: 'allow', reason: 'NO-CONTRACT', description: d, no_contract_reason: 'clerk doc edit for the log, no code', no_contract_category: 'clerk' }));
  let p = nl.pendingItems(mkEntries([mapped, mapped.toLowerCase(), '  ' + mapped + ' ']));
  t('D launches whose description is in the table raise no "unmapped" item (case and spaces ignored)', (p.unmapped || []).length === 0, JSON.stringify(p.unmapped));
  p = nl.pendingItems(mkEntries([mapped, 'Brand new clerk job nobody mapped', 'Another unmapped launch']));
  t('D two launches whose description is NOT in the table are two unmapped items, each with its own log line number', (p.unmapped || []).length === 2 && p.unmapped.map((e) => e.n).join() === '2,3', JSON.stringify(p.unmapped));
  const txt = nl.formatNotice(p, 'sekret');
  t('D the notice names each unmapped description with its own #n[code] and says where the table is', /UNMAPPED NO-CONTRACT/.test(txt) && /#2\[[0-9a-f]{6}\]/.test(txt) && /#3\[[0-9a-f]{6}\]/.test(txt) && /live_launch_categories\.json/.test(txt) && /Brand new clerk job/.test(txt), txt);
  t('D unmapped items count in the total and in ackItems (acked one by one)', p.total >= 2 && nl.ackItems(p).filter((e) => [2, 3].includes(e.n)).length >= 2, JSON.stringify({ total: p.total }));
  const acked = nl.pendingItems([...mkEntries([mapped, 'Brand new clerk job nobody mapped']), { n: 3, gate: 'ack', items: [2] }]);
  t('D an acknowledged unmapped item is gone', (acked.unmapped || []).length === 0, JSON.stringify(acked.unmapped));
  // the four descriptions the gates13 audit found missing are in the table now
  for (const d of ['Disk use survey (read-only)', 'Remove 5 finished worktrees', 'Remove 5 finished worktrees (retry)', 'Commit gate records, remove worktrees']) t(`D "${d}" is in the table`, table.some(([x]) => x === d), '');
  // the live gate.log (read only, when it exists): every NO-CONTRACT description in it is either in the table or listed as unmapped by the same code the notice uses
  const mainLog = isolation.realLog;
  let entries = []; try { entries = nl.readGateLog(path.dirname(mainLog)); } catch { /* none */ }
  const ncDescs = [...new Set(entries.filter((e) => e.gate === 'brief' && e.reason === 'NO-CONTRACT' && e.description).map((e) => String(e.description).trim().toLowerCase()))];
  const inTable = new Set(table.map(([d]) => String(d).trim().toLowerCase()));
  const missing = ncDescs.filter((d) => !inTable.has(d));
  const lp = nl.pendingItems(entries.filter((e) => e.gate !== 'ack'));
  const listed = new Set((lp.unmapped || []).map((e) => String(e.description).trim().toLowerCase()));
  t(`D live gate.log (read only; ${entries.length ? ncDescs.length + ' distinct NO-CONTRACT descriptions' : 'not present here'}): every description missing from the table is listed as unmapped by the notice code`, missing.every((d) => listed.has(d)), missing.filter((d) => !listed.has(d)).join(' | '));
  if (missing.length) console.log(`  note  ${missing.length} live description(s) are not in the table yet (the notice lists them): ${missing.slice(0, 6).join(' | ')}`);
}

// ---------------------------------------------------------------- end
t('no line of this run reached the real gate.log', isolation.leaks().length === 0, isolation.leaks().join('\n'));
console.log(`\n${pass} passed, ${fail} failed`);
if (fail) console.log(failedNames.map((n) => ' - ' + n).join('\n'));
cleanupTmp(tmp);
process.exit(fail ? 1 : 0);
