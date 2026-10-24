#!/usr/bin/env node
// gates11: real-payload tables for the send-back of round gates10 (brief docs/tasks/gates10.md and the 12 + 1 decisions of the main session on docs/progress/gates10/AUDIT.md).
//   A  time-limit guard (decision 13, contract item 26 made mechanical): REFUSE a long job with no limit, ALLOW the limited forms and commands that only MENTION the names; second copy of a
//      running test; process lookup bounded to 300 ms and failing open; -TimeoutSec in the unreal scripts; the hook is wired in settings.json
//   B  launch gate (decision 1): a NO-CONTRACT launch is refused by its DESCRIPTION (or a prompt line that orders an audit of a progress folder), not by words anywhere in the reason
//   C  phase checks (decisions 2-5): build phase reads suite_node.log (no suite run inside the hook), needs a code commit, fail_first in the runner's format; measure phase needs real photo
//      files, M equal to the brief's count, one table per case of the brief
//   D  first-look file (decision 6): placeholders fail, a reference photo under docs/reference and a picture of the round are both needed
//   E  [unreal] frames (decision 7): frame in the round's _compare or hand-in folder, cited in STATUS next to a playtest log newer than the brief that names the frame
//   F  lock_guard (decisions 8, 9): chained fs.rm forms, git stash / reset --hard / checkout -- / revert against locked files; patch --dry-run allowed
//   G  delete_guard (decision 11): a heredoc written to a file is text, a heredoc fed to an interpreter is code
// Where the expected values come from: docs/progress/gates10/AUDIT.md (items 1-12) and the main-session decisions quoted in the round brief; the real descriptions of 23 Oct 2026 come from
// the NO-CONTRACT lines of the main gate.log (embedded below, not read at run time). Nothing pins a fault. Every library call is wrapped so that on the gates10 code each case fails on its own
// (fail_first_*.txt is made by running this file against a scratch export of agent/gates10). No test writes the live gate.log.
// Usage: node tools/hooks/test_guards11.mjs
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import zlib from 'node:zlib';
import crypto from 'node:crypto';
import { spawnSync } from 'node:child_process';
import { fileURLToPath, pathToFileURL } from 'node:url';
import { isolateState, cleanupTmp } from '../test_isolation.mjs';
import { logRun } from './test_record_lib.mjs';
import { signRecord } from './lib.mjs';   // gates12: a suite_node.log is made through the real hooks (a typed log has no run record)

const src = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..', '..');
const tmp = fs.mkdtempSync(path.join(os.tmpdir(), 'guards11-'));
const isolation = isolateState(tmp, src);
let pass = 0, fail = 0; const failedNames = [];
const t = (name, cond, detail = '') => { if (cond) { pass++; console.log('  ok   ' + name); } else { fail++; failedNames.push(name); console.log('  FAIL ' + name + (detail ? '\n       ' + String(detail).replace(/\n/g, '\n       ') : '')); } };
const tt = async (name, fn) => { try { const r = await fn(); t(name, r === true || (r && r.ok), r && r.detail); } catch (e) { t(name, false, 'THREW ' + String((e && e.message) || e).slice(0, 200)); } };
const mk = (p) => fs.mkdirSync(p, { recursive: true });
const wr = (p, s = 'x') => { mk(path.dirname(p)); fs.writeFileSync(p, s); };
const imp = (rel) => import(pathToFileURL(path.join(src, rel)).href);
const sleepSync = (ms) => { const a = new Int32Array(new SharedArrayBuffer(4)); Atomics.wait(a, 0, 0, ms); };

// ---------------------------------------------------------------- pictures
const crcT = (() => { const tb = []; for (let n = 0; n < 256; n++) { let c = n; for (let k = 0; k < 8; k++) c = c & 1 ? 0xedb88320 ^ (c >>> 1) : c >>> 1; tb[n] = c >>> 0; } return tb; })();
const crc32 = (buf) => { let c = 0xffffffff; for (const b of buf) c = crcT[(c ^ b) & 255] ^ (c >>> 8); return (c ^ 0xffffffff) >>> 0; };
const chunk = (type, data) => { const len = Buffer.alloc(4); len.writeUInt32BE(data.length); const td = Buffer.concat([Buffer.from(type), data]); const crc = Buffer.alloc(4); crc.writeUInt32BE(crc32(td)); return Buffer.concat([len, td, crc]); };
const makePng = (w = 90, h = 90) => { const rows = []; for (let y = 0; y < h; y++) rows.push(Buffer.concat([Buffer.from([0]), crypto.randomBytes(w * 3)])); const ihdr = Buffer.alloc(13); ihdr.writeUInt32BE(w, 0); ihdr.writeUInt32BE(h, 4); ihdr[8] = 8; ihdr[9] = 2;
  return Buffer.concat([Buffer.from('89504e470d0a1a0a', 'hex'), chunk('IHDR', ihdr), chunk('IDAT', zlib.deflateSync(Buffer.concat(rows), { level: 0 })), chunk('IEND', Buffer.alloc(0))]); };
const pngFile = (p, w, h) => { mk(path.dirname(p)); fs.writeFileSync(p, makePng(w, h)); return p; };

// ---------------------------------------------------------------- fixture: a temp main checkout with copies of the tools
const root = path.join(tmp, 'main11'); mk(path.join(root, '.git')); mk(path.join(root, 'tools', 'hooks')); mk(path.join(root, 'docs', 'templates')); mk(path.join(root, 'docs', 'reference', 'aerial')); mk(path.join(root, 'docs', 'reference', '_compare'));
for (const f of fs.readdirSync(path.join(src, 'tools'))) if (/\.mjs$/.test(f)) fs.copyFileSync(path.join(src, 'tools', f), path.join(root, 'tools', f));
for (const f of fs.readdirSync(path.join(src, 'tools', 'hooks'))) if (/\.mjs$/.test(f)) fs.copyFileSync(path.join(src, 'tools', 'hooks', f), path.join(root, 'tools', 'hooks', f));
for (const f of fs.readdirSync(path.join(src, 'docs', 'templates'))) fs.copyFileSync(path.join(src, 'docs', 'templates', f), path.join(root, 'docs', 'templates', f));
fs.copyFileSync(path.join(src, 'docs', 'AGENT_CONTRACT.md'), path.join(root, 'docs', 'AGENT_CONTRACT.md'));
const state = path.join(tmp, 'state-11'); mk(state);
const env = { ...process.env, CP_GATE_STATE_DIR: state }; delete env.CP_MAIN_ROOT; delete env.CP_TIME_GUARD_PROCS; delete env.CP_TIME_GUARD_PROC_CMD;
const elsewhere = path.join(tmp, 'elsewhere'); mk(elsewhere);
const logOf = (dir = state) => { try { return fs.readFileSync(path.join(dir, 'gate.log'), 'utf8').trim().split('\n').filter(Boolean).map((l) => JSON.parse(l)); } catch { return []; } };
const photoDir = path.join(root, 'docs', 'reference', 'aerial');
const photos = ['ph1.png', 'ph2.png', 'ph3.png'].map((n) => pngFile(path.join(photoDir, n), 60, 60));
const git = (cwd, ...a) => spawnSync('git', a, { cwd, encoding: 'utf8', env: { ...process.env, GIT_AUTHOR_NAME: 't', GIT_AUTHOR_EMAIL: 't@t', GIT_COMMITTER_NAME: 't', GIT_COMMITTER_EMAIL: 't@t' } });
function repo(name) { const w = path.join(tmp, name); mk(w); git(w, 'init', '-q'); git(w, 'config', 'user.email', 't@t'); git(w, 'config', 'user.name', 't'); wr(path.join(w, 'README.md'), 'r'); git(w, 'add', '-A'); git(w, 'commit', '-q', '-m', 'init'); return w; }
const commit = (w, msg) => { git(w, 'add', '-A'); git(w, 'commit', '-q', '-m', msg); return git(w, 'rev-parse', 'HEAD').stdout.trim(); };

// ================================================================ A  time-limit guard
console.log('gates11 A: time-limit guard (contract item 26): REFUSE a long job with no limit, ALLOW limited forms and commands that only mention the names');
const tgRepo = path.join(tmp, 'tg-wt'); mk(path.join(tgRepo, '.git'));
const tgRepo2 = path.join(tmp, 'tg-wt2'); mk(path.join(tgRepo2, '.git'));
const tgState = path.join(tmp, 'state-tg'); mk(tgState);
const tg = (command, tool = 'Bash', { cwd = tgRepo, e = {}, stateDir = tgState } = {}) => {
  const ev = { ...env, CP_GATE_STATE_DIR: stateDir, CP_TIME_GUARD_PROCS: '' }; delete ev.CP_TIME_GUARD_PROC_CMD;   // no process counts as running unless a case says so: the machine may really be running a suite
  for (const [k, v] of Object.entries(e)) { if (v === null) delete ev[k]; else ev[k] = v; }
  return spawnSync(process.execPath, [path.join(src, 'tools', 'hooks', 'time_guard.mjs')], { input: JSON.stringify({ hook_event_name: 'PreToolUse', tool_name: tool, session_id: 's1', cwd, agent_id: 'a1', tool_input: { command } }), cwd: elsewhere, encoding: 'utf8', timeout: 60000, env: ev });
};
const R = String.raw;
const tgRefuse = (r) => r.status === 2 && /REFUSED by the time-limit guard/.test(r.stderr);
const tgAllow = (r) => r.status === 0 && r.stderr === '';
{
  const refuse = [
    ['build/tests/sim_core_tests.exe', 'Bash', 'the suite with no limit'], ['cd build && ctest --output-on-failure', 'Bash', 'ctest with no limit'], ['timeout 1200 ctest', 'Bash', 'a limit over 900 s'],
    ['timeout 0 ctest', 'Bash', 'timeout 0 is no limit'], ['cmake --build build -j4', 'Bash', 'cmake --build with no limit'], ['ninja -C build', 'Bash', 'ninja with no limit'], ['msbuild build/x.sln /m', 'Bash', 'msbuild with no limit'],
    ['node prototypes/road-draw/tools/scene.mjs --check', 'Bash', 'scene.mjs with no limit'], ['node prototypes/road-draw/tools/render.mjs --all', 'Bash', 'render.mjs with no limit'],
    ['./build/tests/geometry_tests.exe -tc=abc', 'Bash', 'any *_tests program'], ['./build/tests/t_full_run.exe', 'Bash', 'a t_full* program'], ['bash -c "ctest --test-dir build"', 'Bash', 'a nested shell does not hide it'],
    ['time ctest', 'Bash', 'time is not a limit'], ['ctest 2>&1 | tail -20', 'Bash', 'ctest piped to tail with no limit'], ['cd build && cmake --build . && ctest', 'Bash', 'two jobs, none limited'],
    [R`powershell -File unreal\tools\Run-Playtest.ps1 -Out x`, 'PowerShell', 'Run-Playtest.ps1 with no -TimeoutSec'], [R`& .\unreal\tools\Build-Editor.ps1 -TimeoutMinutes 30`, 'PowerShell', '-TimeoutMinutes is not the limit the guard wants'],
    [R`& .\unreal\tools\Run-EditorPython.ps1 -Script x.py`, 'PowerShell', 'Run-EditorPython.ps1 with no limit'], [R`& .\unreal\tools\Build-Editor.ps1 -TimeoutSec 9000`, 'PowerShell', 'Build-Editor.ps1 -TimeoutSec over its 5400 s cap'],
    [R`& .\unreal\tools\Run-Playtest.ps1 -TimeoutSec 0`, 'PowerShell', '-TimeoutSec 0'], [R`& build\tests\sim_core_tests.exe`, 'PowerShell', 'the suite in PowerShell with no limit'],
    [R`$p = Start-Process -PassThru build\tests\sim_core_tests.exe; Wait-Process -Id $p.Id`, 'PowerShell', 'Start-Process + Wait-Process with no -Timeout'], ['timeout 900 build/tests/sim_core_tests.exe', 'PowerShell', 'timeout typed in PowerShell is Windows timeout.exe, a pause'],
    [R`Start-Process -FilePath ninja -ArgumentList '-C','build' -Wait`, 'PowerShell', 'Start-Process -Wait has no limit'],
  ];
  for (const [c, tool, label] of refuse) { const r = tg(c, tool); t(`A REFUSE (${tool}): ${label}`, tgRefuse(r) && /timeout 900|TimeoutSec|Wait-Process/.test(r.stderr), `status ${r.status} ${r.stderr.slice(0, 200)}`); }
  const allow = [
    ['timeout 900 build/tests/sim_core_tests.exe > suite.log 2>&1', 'Bash', 'timeout 900 suite'], ['cd build && timeout 600 ctest --output-on-failure', 'Bash', 'timeout 600 ctest'], ['timeout 15m ctest', 'Bash', 'timeout 15m = 900 s'],
    ['timeout 900 cmake --build build -j4', 'Bash', 'timeout cmake --build'], ['timeout -k 10 600 ninja -C build', 'Bash', 'timeout -k 10 600 ninja'], ['timeout 120 node prototypes/road-draw/tools/scene.mjs --check', 'Bash', 'timeout scene.mjs'],
    ['timeout 900 bash -c "ctest --test-dir build"', 'Bash', 'timeout around a nested shell'], ['timeout 600 ./build/tests/t_full_run.exe', 'Bash', 'timeout t_full'], ['cmake -S . -B build', 'Bash', 'cmake configure is not a run'],
    ['sim_core_tests.exe --list-test-cases', 'Bash', 'listing test cases is not a run'], ['ctest -N', 'Bash', 'ctest -N lists'], ['cmake --build --help', 'Bash', 'cmake --build --help'],
    ['grep -n ctest log.txt', 'Bash', 'grep that mentions ctest'], ['cat sim_core_tests.log | tail -5', 'Bash', 'cat of a log named after the suite'], ['git log --grep ninja --oneline', 'Bash', 'git log --grep ninja'],
    ['git commit -m "run ctest and ninja under timeout"', 'Bash', 'a commit message that names them'], ['echo "cmake --build is slow"', 'Bash', 'echo'], ['git diff -- tests/sim_core_tests.cpp', 'Bash', 'git diff of a file named like the suite'],
    ['node tools/check_handin.mjs docs/progress/x', 'Bash', 'an unrelated node tool'], [R`powershell -File unreal\tools\Run-Playtest.ps1 -Out x -TimeoutSec 600`, 'PowerShell', 'Run-Playtest.ps1 -TimeoutSec 600'],
    [R`& .\unreal\tools\Build-Editor.ps1 -TimeoutSec 4000`, 'PowerShell', 'Build-Editor.ps1 -TimeoutSec 4000 (cap 5400)'], [R`& .\unreal\tools\Run-EditorPython.ps1 -Script x.py -TimeoutSec:900`, 'PowerShell', '-TimeoutSec:900 form'],
    [R`$p = Start-Process -PassThru -NoNewWindow -FilePath build\tests\sim_core_tests.exe; Wait-Process -Id $p.Id -Timeout 600`, 'PowerShell', 'Start-Process + Wait-Process -Timeout 600'],
    ['bash -c "timeout 900 ctest"', 'PowerShell', 'a nested bash with GNU timeout'], [R`Select-String ctest x.txt`, 'PowerShell', 'Select-String that mentions ctest'], [R`Get-Content sim_core_tests.log -Tail 5`, 'PowerShell', 'Get-Content of the suite log'],
    [R`git log --grep "Run-Playtest"`, 'PowerShell', 'git log --grep Run-Playtest'], [R`Get-ChildItem unreal\tools\Build-Editor.ps1`, 'PowerShell', 'listing the script'],
  ];
  for (const [c, tool, label] of allow) { const r = tg(c, tool, { stateDir: path.join(tmp, 'state-tg-allow') }); t(`A ALLOW (${tool}): ${label}`, tgAllow(r), `status ${r.status} ${r.stderr.slice(0, 200)}`); }
  t('A the refusal is on record in gate.log (gate time_guard, decision deny)', logOf(tgState).some((l) => l.gate === 'time_guard' && l.decision === 'deny'), JSON.stringify(logOf(tgState).slice(-1)));
  // the refusal text says exactly how to add the limit
  const rb = tg('ctest'), rp = tg(R`& .\unreal\tools\Run-Playtest.ps1`, 'PowerShell');
  t('A the Bash refusal names `timeout 900 <cmd>` and the PowerShell refusal names -TimeoutSec and Wait-Process -Timeout', /timeout 900 <cmd>/.test(rb.stderr) && /-TimeoutSec 600/.test(rp.stderr) && /Wait-Process -Id \$p\.Id -Timeout 600/.test(rp.stderr), rb.stderr + rp.stderr);
}
console.log('gates11 A: a second copy of a running test, and the 300 ms process lookup');
{
  const ds = path.join(tmp, 'state-tg-dup'); mk(ds);
  const cmd = 'timeout 900 build/tests/sim_core_tests.exe > suite.log 2>&1';
  const procs = { CP_TIME_GUARD_PROCS: 'sim_core_tests', CP_TIME_GUARD_PATHS: path.join(tgRepo, 'build', 'tests', 'sim_core_tests.exe') };   // gates12: the second-copy check matches the executable path under the worktree
  let r = tg(cmd, 'Bash', { stateDir: ds, e: procs });
  t('A dup: the first limited run is allowed (and recorded)', tgAllow(r) && fs.existsSync(path.join(ds, 'runs.jsonl')), `status ${r.status} ${r.stderr}`);
  r = tg(cmd, 'Bash', { stateDir: ds, e: procs });
  t('A dup: a second copy of the same test from the same worktree while the process is still running is refused', tgRefuse(r) && /second copy/.test(r.stderr), `status ${r.status} ${r.stderr.slice(0, 300)}`);
  r = tg('timeout 600 ctest', 'Bash', { stateDir: ds, e: { CP_TIME_GUARD_PROCS: 'ctest', CP_TIME_GUARD_PATHS: path.join(tgRepo, 'build', 'ctest.exe') } }); const r2 = tg('timeout 600 ctest', 'Bash', { stateDir: ds, e: { CP_TIME_GUARD_PROCS: 'ctest', CP_TIME_GUARD_PATHS: path.join(tgRepo, 'build', 'ctest.exe') } });
  t('A dup: the same holds for ctest (first allowed, second refused)', tgAllow(r) && tgRefuse(r2), `${r.status} ${r2.status} ${r2.stderr.slice(0, 200)}`);
  r = tg(cmd, 'Bash', { stateDir: ds, e: { CP_TIME_GUARD_PROCS: 'something_else' } });
  t('A dup: the same command is allowed once the process is gone (the earlier run ended)', tgAllow(r), `status ${r.status} ${r.stderr}`);
  r = tg(cmd, 'Bash', { cwd: tgRepo2, stateDir: ds, e: procs });
  t('A dup: another worktree may run the same test at the same time', tgAllow(r), `status ${r.status} ${r.stderr}`);
  r = tg('timeout 900 build/tests/other_tests.exe', 'Bash', { stateDir: ds, e: procs });
  t('A dup: a different test program is not a second copy', tgAllow(r), `status ${r.status} ${r.stderr}`);
  // the lookup is bounded to 300 ms: a lookup command that hangs is cut, the command passes, one FAIL-OPEN line says so
  const ds2 = path.join(tmp, 'state-tg-slow'); mk(ds2);
  tg(cmd, 'Bash', { stateDir: ds2, e: procs });   // records a run
  const t0 = Date.now();
  r = tg(cmd, 'Bash', { stateDir: ds2, e: { CP_TIME_GUARD_PROCS: null, CP_TIME_GUARD_PROC_CMD: JSON.stringify([process.execPath, '-e', 'setTimeout(function(){}, 20000)']) } });
  const took = Date.now() - t0;
  t('A dup: a process lookup that hangs is cut at 300 ms; the command is allowed and a FAIL-OPEN line is logged', tgAllow(r) && took < 6000 && logOf(ds2).some((l) => l.gate === 'time_guard' && /FAIL-OPEN/.test(l.reason || '') && /300 ms/.test(l.reason || '')), `status ${r.status} took ${took} ms ${r.stderr} ${JSON.stringify(logOf(ds2).slice(-2))}`);
  // the library functions the hook uses
  await tt('A dup: processRunning(name) with a stuck lookup command returns null within 1 s (the bound)', async () => {
    const m = await imp('tools/hooks/time_guard_impl.mjs'); process.env.CP_TIME_GUARD_PROC_CMD = JSON.stringify([process.execPath, '-e', 'setTimeout(function(){}, 20000)']);
    const a = Date.now(); const v = m.processRunning('sim_core_tests'); delete process.env.CP_TIME_GUARD_PROC_CMD; return { ok: v === null && Date.now() - a < 1500, detail: String(v) + ' ' + (Date.now() - a) };
  });
}
console.log('gates11 A: wiring and the -TimeoutSec parameter of the unreal scripts');
{
  const st = JSON.parse(fs.readFileSync(path.join(src, '.claude', 'settings.json'), 'utf8'));
  const bp = (st.hooks.PreToolUse || []).filter((x) => /Bash/.test(x.matcher) && /PowerShell/.test(x.matcher)).flatMap((x) => x.hooks || []);
  t('A settings.json wires tools/hooks/time_guard.mjs on Bash|PowerShell', bp.some((h) => (h.args || []).some((a) => /time_guard\.mjs$/.test(a))), JSON.stringify(bp.map((h) => h.args)));
  for (const f of ['Build-Editor.ps1', 'Run-Playtest.ps1', 'Run-EditorPython.ps1']) { const txt = fs.readFileSync(path.join(src, 'unreal', 'tools', f), 'utf8'); t(`A ${f} has a -TimeoutSec parameter that sets its time limit`, /\[int\]\$TimeoutSec/.test(txt) && /\$TimeoutSec -gt 0/.test(txt) && /\$TimeoutMinutes = /.test(txt)); }
}

// ================================================================ B  launch gate
console.log('gates11 B: the launch gate refuses a NO-CONTRACT launch by its DESCRIPTION (real descriptions of 23 Oct 2026)');
{
  const launch = (description, prompt, type = 'general-purpose') => spawnSync(process.execPath, [path.join(root, 'tools', 'hooks', 'brief_gate.mjs')], { input: JSON.stringify({ hook_event_name: 'PreToolUse', tool_name: 'Agent', session_id: 's1', cwd: elsewhere, tool_input: { description, prompt, subagent_type: type } }), cwd: elsewhere, encoding: 'utf8', timeout: 120000, env });
  const nc = (reason) => `NO-CONTRACT: ${reason}\nDo the job.`;
  // descriptions refused by the decision-1 pattern: auditor | audit(ing) of/for | opus first look | first look, plus "Audit <x> independently|round|hand-in" (all real descriptions in the log)
  const refuse = ['Audit gate-notice hooks independently', 'Audit earthworks round 9', 'Audit T-junction round', 'Audit road tool round 3', 'Auditor for gates9 hand-in', 'Auditor for m2fix5a hand-in', 'Audit of the earthworks hand-in', 'Auditing for the roundabout round', 'Audit the audit-gate tooling', 'Audit repo organisation, read-only', 'Audit gradelayer WorldCache layers',   /* gates12 decision 3: audits of agents' work that are not named hand-in or round are still audits */
    'Opus first look on grade-r12 images', 'Opus first look: tjunction5 images', 'First look at the m2fix hand-in', 'Fresh auditor for the junction round', 'AUDITOR for x'];
  for (const d of refuse) { const r = launch(d, nc('clerk work with no audit in it, 20+ characters')); t(`B REFUSE "${d}" (a reason that says nothing about audits does not save it)`, r.status === 2 && /free-text NO-CONTRACT form is not accepted/.test(r.stderr) && /make_brief\.mjs/.test(r.stderr), r.stderr.slice(0, 300)); }
  // a prompt that opens a line with the audit order and names a progress folder
  const pf = path.join(tmp, 'x', 'docs', 'progress', 'gates9');
  for (const [d, p] of [['Check the round', `Audit ${pf} and write AUDIT.md\nNO-CONTRACT: just a quick look for the main session today`], ['Quick job', `You are the auditor of ${pf}.\nNO-CONTRACT: nothing special at all about this one`]]) { const r = launch(d, p); t(`B REFUSE a prompt line that orders an audit of a progress folder (${d})`, r.status === 2 && /audit/.test(r.stderr), r.stderr.slice(0, 300)); }
  const allow = ['Write audit first-look rule into docs', 'Earthworks method review (read-only)', 'Clerk: photos-outrank-tests rule, commit review', 'Add every-audit-item-fixed rule to docs',
    'Clerk: contract speed rules, queue, acceptance', 'Merge gates7 then gates9 into main', 'Research: Unreal plugin facts', 'Re-fetch 18 truncated photos and back up', 'Queue catch-up for 22-23 Oct events'];
  for (const d of allow) { const r = launch(d, nc('clerk: clerk or merge or research job; mentions the audit and first look rules only in passing')); t(`B ALLOW "${d}"`, r.status === 0, r.stderr.slice(0, 300)); }
  let r = launch('Docs clerk', `Docs clerk.\nNO-CONTRACT: clerk: update docs/QUEUE.md and record that the audit of ${pf} was accepted`);
  t('B ALLOW a clerk prompt that only says an audit of a progress folder was accepted', r.status === 0, r.stderr.slice(0, 300));
  const hdA = path.join(tmp, 'fl_a', 'docs', 'progress', 'fl'); mk(hdA);
  r = launch('Auditor for x hand-in', `AUDIT-OF: ${hdA}\nAGENT-CONTRACT-MARKER: v1\n## Owner's words\n"x" (1 Oct)\n`);
  t('B ALLOW an "Auditor for ..." description that carries AUDIT-OF (the real audit launch)', r.status === 0, r.stderr.slice(0, 300));
  r = launch('Opus first look on x images', `FIRSTLOOK-OF: ${hdA}\nAGENT-CONTRACT-MARKER: v1\n## Owner's words\n"x" (1 Oct)\n`);
  t('B ALLOW an "Opus first look ..." description that carries FIRSTLOOK-OF (the real first-look launch)', r.status === 0, r.stderr.slice(0, 300));
}

// ================================================================ C  phase checks
const GOOD_MEASURE = (hd) => `# MEASURE\nReferences opened: 3 of 3 listed\n\n## Case A one-lane road\n| photo | measured | value |\n|---|---|---|\n| ${photos[0]} | carriageway width | 3.4 m |\n| ${photos[1]} | lane width | 1.0 lane widths |\n| ${photos[2]} | verge | 150 cm |\n\nOverlay: ${path.join(hd, 'trace_a.png')}\n`;
function measureHandin(name, mutate = (x) => x) { const w = repo(name); const hd = path.join(w, 'docs', 'progress', 'rr'); mk(hd); pngFile(path.join(hd, 'trace_a.png'), 80, 60); wr(path.join(hd, 'MEASURE.md'), mutate(GOOD_MEASURE(hd))); return { w, hd }; }
const chk = (hd, ...a) => spawnSync(process.execPath, [path.join(root, 'tools', 'check_handin.mjs'), hd, '--main', root, ...a, ...(a.includes('build') && !a.includes('node-only') ? ['--suite', 'node-only'] : [])], { cwd: elsewhere, encoding: 'utf8', env, timeout: 600000 });
const failedWith = (r, re) => r.status === 1 && /HAND-IN CHECK FAILED/.test(r.stderr) && re.test(r.stderr);
const passed = (r) => r.status === 0 && /HAND-IN CHECK PASSED/.test(r.stdout);
console.log('gates11 C: measure phase (decision 5): photo files must exist, M = the brief\'s count, one table per case of the brief');
{
  const ARGS = ['--phase', 'measure', '--refs', '3', '--case', 'Case A one-lane road'];
  let r = chk(measureHandin('c_m_good').hd, ...ARGS);
  t('C measure: a GOOD MEASURE.md with the brief\'s REFS and CASE lines passes', passed(r), r.stderr + r.stdout);
  const bad = [
    ['a table row names a photo file that does not exist', (s) => s.replace(photos[1], path.join(photoDir, 'ghost_never_there.jpg')), ARGS, /ghost_never_there\.jpg" does not exist/],
    ['M is not the brief\'s count (3 listed in the file, 5 in the brief)', (s) => s, ['--phase', 'measure', '--refs', '5', '--case', 'Case A one-lane road'], /the brief lists 5 reference file/],
    ['"opened 3 of 4" when the brief lists 3', (s) => s.replace('3 of 3', '3 of 4'), ARGS, /the brief lists 3 reference file/],
    ['the brief has two cases, MEASURE.md one table', (s) => s, ['--phase', 'measure', '--refs', '3', '--case', 'Case A one-lane road', '--case', 'Case B roundabout'], /has 2 case\(s\) but MEASURE\.md has 1|no "## Case" section of that name/],
    ['a case of the brief with no section of that name', (s) => s.replace('## Case A one-lane road', '## Case Z something else'), ARGS, /has no "## Case" section of that name/],
  ];
  for (const [label, mut, a, re] of bad) { const rr = chk(measureHandin('c_m_' + label.replace(/\W+/g, '').slice(0, 14), mut).hd, ...a); t(`C measure: BAD (${label}) is refused with the matching reason`, failedWith(rr, re), rr.stderr + rr.stdout); }
}
console.log('gates11 C: build phase (decisions 2-4): suite_node.log is read, never run; a code commit is required; fail_first shows a failure in the runner format');
const DOC_FAIL = 'TEST CASE:  pier clearance\nsrc/pier.cpp:10: ERROR: CHECK( a == b ) is NOT correct!\n[doctest] test cases: 5 | 4 passed | 1 failed | 0 skipped\n';
function buildHandin(name, o = {}) {
  const x = measureHandin(name); commit(x.w, 'measure phase'); const shas = [];
  if (o.code === false) { wr(path.join(x.w, 'notes.md'), 'n'); shas.push(commit(x.w, 'docs only')); } else { wr(path.join(x.w, 'src.mjs'), 'code'); shas.push(commit(x.w, 'code step')); }
  if (o.testFile) { wr(path.join(x.w, 'tests', 'test_new_thing.mjs'), 'tests'); shas.push(commit(x.w, 'add test_new_thing')); }
  const bin = o.bin || 'src.mjs'; if (o.bin) { wr(path.join(x.w, o.bin), 'binary'); if (o.binOld) { const old = new Date(Date.now() - 3600 * 1000); fs.utimesSync(path.join(x.w, o.bin), old, old); } shas.push(commit(x.w, 'add binary')); }
  if (o.ff !== undefined) wr(path.join(x.hd, 'fail_first_new_thing.txt'), o.ff);
  wr(path.join(x.hd, 'BUILD.md'), `# BUILD\nCommits: ${shas.map((s) => s.slice(0, 10)).join(' ')}\nSuite binary: ${bin}\n`);
  if (o.log !== null) { const lg = o.log === undefined ? '12 passed, 0 failed\n' : o.log; logRun(x.w, path.join(x.hd, 'suite_node.log'), lg, { stateDir: state, exit: /[1-9]\d* failed|\bFAIL\b|ERROR:|Failed/.test(lg) ? 1 : 0 }); if (o.bin) { const rf = path.join(state, 'suite_records.jsonl'); const ls = fs.readFileSync(rf, 'utf8').trim().split('\n'); const last = JSON.parse(ls.pop()); delete last.sig; last.binary = path.join(x.w, o.bin); last.binarySha = null; fs.writeFileSync(rf, ls.concat(JSON.stringify(signRecord(last, state))).join('\n') + '\n'); }   /* a recorded run whose binary is a real file of this worktree (the fake suite is a script, which the age rule skips) */ if (o.logOld) { const old = new Date(Date.now() - 7200 * 1000); fs.utimesSync(path.join(x.hd, 'suite_node.log'), old, old); } }
  commit(x.w, 'BUILD.md and log'); return x;
}
{
  const B = ['--phase', 'build'];
  let r = chk(buildHandin('c_b_good', { testFile: true, ff: 'test_new_thing.mjs on the old code:\nFAIL new thing joins up\n10 passed, 1 failed\n' }).hd, ...B);
  t('C build: GOOD (code commit, log newer than it, real summary, fail_first in the runner format) passes', passed(r), r.stderr + r.stdout);
  r = chk(buildHandin('c_b_doc', { log: DOC_FAIL }).hd, ...B, '--allow-fail', 'pier');
  t('C build: a doctest log whose only failure is the brief\'s allowed pier test passes', passed(r), r.stderr + r.stdout);
  r = chk(buildHandin('c_b_ctest', { log: '98% tests passed, 1 tests failed out of 50\n\nThe following tests FAILED:\n\t 12 - pier_clearance (Failed)\n' }).hd, ...B, '--allow-fail', 'pier_clearance');
  t('C build: a ctest log with only the allowed failure passes', passed(r), r.stderr + r.stdout);
  const cases = [
    ['no commit touches code (a .md note only)', { code: false }, B, /touches code or tests outside the hand-in folder/],
    ['no suite_node.log', { log: null }, B, /no suite_node.log in the hand-in folder/],
    ['log older than the last code commit', { logOld: true }, B, /older than the last code commit/],
    ['log with no summary (a half-written file)', { log: 'compiling...\nrunning...\n' }, B, /holds no doctest/],
    ['log is a timeout (exit=124)', { log: '[doctest] test cases: 5 | 5 passed | 0 failed | 0 skipped\nexit=124\n' }, B, /time limit/],
    ['a failing test that the brief does not allow', { log: DOC_FAIL }, ['--phase', 'build', '--allow-fail', 'something else'], /not in the brief's allowed list/],
    ['a failing test and no allow list at all', { log: DOC_FAIL }, B, /the brief allows none/],
    ['a failure count with no failing name', { log: '[doctest] test cases: 5 | 4 passed | 1 failed | 0 skipped\n' }, ['--phase', 'build', '--allow-fail', 'pier'], /names no failing test/],
    ['the suite binary is older than the last code commit', { bin: 'bin/suite.exe', binOld: true }, B, /older than the last code commit/],
    ['a new test file with a fail_first that shows no failure', { testFile: true, ff: 'test_new_thing.mjs ran against the old code and everything passed, 12 of 12 ok\n' }, B, /shows no failing case/],
    ['a fail_first that says "all passed" in a long sentence', { testFile: true, ff: 'All the cases of test_new_thing.mjs passed on the old code too, nothing to show here at all today\n' }, B, /shows no failing case/],
  ];
  for (const [label, o, a, re] of cases) { const rr = chk(buildHandin('c_b_' + label.replace(/\W+/g, '').slice(0, 14), o).hd, ...a); t(`C build: BAD (${label}) is refused with the matching reason`, failedWith(rr, re), rr.stderr + rr.stdout); }
  for (const [label, text] of [['a doctest ERROR block', 'TEST CASE:  new thing\nfile.cpp:3: ERROR: CHECK( x ) is NOT correct!\n'], ['a ctest ***Failed line', '1/3 Test #1: new_thing ......***Failed\n'], ['a FAIL line of the repo runner', 'FAIL new thing joins up\n'], ['a "N failed" summary above zero', '9 passed, 2 failed\n']]) {
    const rr = chk(buildHandin('c_b_ff_' + label.replace(/\W+/g, '').slice(0, 10), { testFile: true, ff: 'test_new_thing.mjs on the old code:\n' + text }).hd, ...B);
    t(`C build: fail_first in the format "${label}" is accepted`, passed(rr), rr.stderr + rr.stdout);
  }
  // the check does not run the suite: a --suite option is ignored and a suite command is never started
  const sentinel = path.join(tmp, 'suite_ran.flag');
  r = chk(buildHandin('c_b_nosuiterun').hd, ...B, '--suite', `node -e "require('fs').writeFileSync(${JSON.stringify(sentinel)}, 'ran')"`);
  t('C build: the check never runs a suite command (a --suite sentinel command was not started)', !fs.existsSync(sentinel), r.stderr + r.stdout);
  await tt('C make_brief --phase measure|build: the brief carries REFS: (the count), one CASE: line per case and the ALLOW-FAIL: lines of the task file; the finish hook reads them back', () => {
    const task = path.join(tmp, 'task_ph.md');
    fs.writeFileSync(task, `# T round\n\n## Owner's words\n"and ofc it needs to work with the gate system, have you been sidestepping it?" (23 Oct 2026)\n\n## Cases\nCase A: one-lane road |${photos.join(' ')}\nFiles: ${photos[0]}\n\n## Allowed failures\n- pier clearance\n- pier overhang\n\n## Faults\nnone known\n\n## Launched\nthis\n\n## Waiting\nnone\n\n## Suite\nnode -e "process.exit(0)"\n`);
    const gw = measureHandin('c_mb'); commit(gw.w, 'measure');
    const mb = (...a) => spawnSync(process.execPath, [path.join(src, 'tools', 'make_brief.mjs'), task, ...a], { cwd: elsewhere, encoding: 'utf8', env, timeout: 600000 });
    const rb = mb('--round', 'rr', '--worktree', gw.w, '--phase', 'build', '--no-visual'), rm = mb('--round', 'rr', '--worktree', gw.w, '--phase', 'measure', '--no-visual');
    const okb = rb.status === 0 && /^REFS: 3$/m.test(rb.stdout) && /^ALLOW-FAIL: pier clearance$/m.test(rb.stdout) && /^ALLOW-FAIL: pier overhang$/m.test(rb.stdout);
    const okm = rm.status === 0 && /^REFS: 3$/m.test(rm.stdout) && /^CASE: Case A/m.test(rm.stdout) && !/^ALLOW-FAIL:/m.test(rm.stdout);
    return { ok: okb && okm, detail: rb.stderr + rb.stdout.split('# AGENT CONTRACT')[0].slice(0, 900) + '\n---\n' + rm.stderr + rm.stdout.split('# AGENT CONTRACT')[0].slice(0, 500) };
  });
  await tt('C make_brief: the build-phase text tells the agent to run the suite with tools/run_suite.mjs and names the allowed-failure list', async () => { const txt = fs.readFileSync(path.join(src, 'tools', 'make_brief.mjs'), 'utf8'); return /node tools\/run_suite\.mjs/.test(txt) && /--compiler msvc/.test(txt) && /ALLOW-FAIL/.test(txt); });
  await tt('C audit brief template: the auditor re-runs the suite itself', async () => /Re-run the suite yourself/.test(fs.readFileSync(path.join(src, 'docs', 'templates', 'AUDIT_BRIEF.md'), 'utf8')));
}

// ================================================================ D  first-look file
console.log('gates11 D: first-look file (decision 6): placeholders fail; a reference photo under docs/reference and a picture of the round are both needed');
{
  const PM = await imp('tools/hooks/phase_marks.mjs').catch(() => null);
  const hd = path.join(tmp, 'fl_d', 'docs', 'progress', 'fl'); mk(hd);
  const imgs = ['a.png', 'b.png'].map((n) => pngFile(path.join(hd, n), 90, 90));
  const stray = pngFile(path.join(tmp, 'fl_d', 'not_a_reference', 'x.png'), 90, 90);
  const text = (opened, top = ['The kerb line on the left of a.png stops short of the junction mouth', 'The centre line in b.png is drawn over the stop bar position', 'The grass in a.png reaches the carriageway edge with no verge strip'], never = 'a junction mouth with no radius at all on either corner') =>
    `First-look agent: fl_d1\nOpened: ${opened.join(', ')}\nTop 3:\n1. ${top[0]}\n2. ${top[1]}\n3. ${top[2]}\nWhat here would never be built: ${never}\n`;
  const probs = (txt) => { wr(path.join(hd, 'FIRSTLOOK_OPUS.md'), txt); return PM.checkFirstLookFile(hd, { actualId: 'fl_d1', main: root }); };
  const GOODOPEN = [...imgs, photos[0]];
  await tt('D a GOOD first look (round pictures and a reference photo, real observations) passes', () => { const p = probs(text(GOODOPEN)); return { ok: p.length === 0, detail: p.join('\n') }; });
  const bad = [
    ['Top 3 line 1 is a <placeholder>', text(GOODOPEN, ['<describe the first fault you see here>', 'The centre line in b.png is drawn over the stop bar position', 'The grass in a.png reaches the carriageway edge with no verge strip']), /placeholder/],
    ['Top 3 line 2 is TODO', text(GOODOPEN, ['The kerb line on the left of a.png stops short of the junction mouth', 'TODO fill this in after the images are open', 'The grass in a.png reaches the carriageway edge with no verge strip']), /TODO|tbd/],
    ['Top 3 line 3 is tbd', text(GOODOPEN, ['The kerb line on the left of a.png stops short of the junction mouth', 'The centre line in b.png is drawn over the stop bar position', 'tbd tbd tbd tbd tbd tbd tbd tbd tbd tbd']), /tbd|placeholder/],
    ['Top 3 line is one word repeated', text(GOODOPEN, ['The kerb line on the left of a.png stops short of the junction mouth', 'looks looks looks looks looks looks looks looks', 'The grass in a.png reaches the carriageway edge with no verge strip']), /placeholder/],
    ['"would never be built" is TODO', text(GOODOPEN, undefined, 'TODO say what here would never be built'), /placeholder/],
    ['"would never be built" is a <placeholder>', text(GOODOPEN, undefined, '<what here would never be built on a real road>'), /placeholder/],
    ['"would never be built" is a short non-answer (under 15 characters)', text(GOODOPEN, undefined, 'nothing at all x'), /placeholder|empty/],
    ['no reference photo under docs/reference (an image from elsewhere on disk)', text([...imgs, stray]), /no reference photo under docs\/reference/],
    ['only reference photos, no picture of the round', text(photos), /no picture of this round/],
  ];
  for (const [label, txt, re] of bad) await tt(`D BAD first look (${label}) is refused`, () => { const p = probs(txt); return { ok: p.some((x) => re.test(x)), detail: p.join('\n') || '(no problem reported)' }; });
}

// ================================================================ E  unreal frames
console.log('gates11 E: [unreal] frames (decision 7): in the round\'s _compare or hand-in folder, cited in STATUS next to a playtest log newer than the brief that names the frame');
{
  const GL = await imp('tools/gates_lib.mjs').catch(() => null);
  const hd = path.join(tmp, 'ue', 'docs', 'progress', 'ur'); mk(hd);
  const cmpDir = path.join(tmp, 'ue', 'docs', 'reference', '_compare', 'ur'); mk(cmpDir);
  const frame = pngFile(path.join(hd, 'junction_aerial.png'), 120, 120);
  const frameCmp = pngFile(path.join(cmpDir, 'junction_eye.png'), 120, 120);
  const stray = pngFile(path.join(tmp, 'ue', 'somewhere', 'junction_x.png'), 120, 120);
  const log = path.join(hd, 'playtest_run1.log'); wr(log, `[playtest] frame written ${frame}\n[playtest] frame written ${frameCmp}\n[playtest] frame written ${stray}\n`);
  const oldLog = path.join(hd, 'playtest_old.log'); wr(oldLog, `frame ${frame}\n`); const past = new Date(Date.now() - 86400000); fs.utimesSync(oldLog, past, past);
  const otherLog = path.join(hd, 'playtest_other.log'); wr(otherLog, 'frame some_other_frame.png\n');
  const since = Date.now() - 600000;
  const ctx = (status) => ({ folder: hd, compareDirs: [cmpDir], statusText: status, resolve: (p) => (fs.existsSync(p) ? p : fs.existsSync(path.join(hd, p)) ? path.join(hd, p) : null) });
  const row = (f) => [{ kase: 'c1', render: f, unreal: true }];
  const probsOf = (f, status) => GL.unrealFrameProblems(row(f), since, ctx(status));
  await tt('E a frame in the hand-in folder, cited in STATUS with a fresh log that names it, passes', () => { const p = probsOf(frame, `Playtest output: junction_aerial.png from ${log}`); return { ok: p.length === 0, detail: p.join('\n') }; });
  await tt('E a frame in the round\'s _compare folder passes the same way', () => { const p = probsOf(frameCmp, `Frame junction_eye.png, playtest log ${log}`); return { ok: p.length === 0, detail: p.join('\n') }; });
  await tt('E a frame outside the round\'s _compare and hand-in folders is refused', () => probsOf(stray, `junction_x.png ${log}`).some((x) => /not in the hand-in folder or the round's/.test(x)));
  await tt('E STATUS that does not cite the frame by name is refused', () => probsOf(frame, `Playtest ran, log ${log}`).some((x) => /does not cite the frame junction_aerial\.png/.test(x)));
  await tt('E STATUS that cites no playtest log file is refused', () => probsOf(frame, 'junction_aerial.png looks fine, I checked it').some((x) => /cites no playtest log/.test(x)));
  await tt('E a cited log that does not exist is refused', () => probsOf(frame, `junction_aerial.png, log ${path.join(hd, 'nope.log')}`).some((x) => /cites no playtest log file that exists/.test(x)));
  await tt('E a cited log older than the brief is refused', () => probsOf(frame, `junction_aerial.png, log ${oldLog}`).some((x) => /older than this run's brief/.test(x)));
  await tt('E a fresh log that does not name the frame is refused', () => probsOf(frame, `junction_aerial.png, log ${otherLog}`).some((x) => /names the frame junction_aerial\.png/.test(x)));
  await tt('E without a context only the old integrity checks run (library callers)', () => GL.unrealFrameProblems(row(frame), since).length === 0);
}

// ================================================================ F  lock guard
console.log('gates11 F: lock_guard (decisions 8 and 9): chained fs.rm, git stash / reset --hard / checkout -- / revert against locked files; patch --dry-run allowed');
{
  const main = path.join(tmp, 'roadworld'); mk(path.join(main, '.git', 'worktrees')); mk(path.join(main, '.claude', 'state'));
  const w = repo('lockrepo'); wr(path.join(w, 'tests', 'listed_test.mjs'), 'a'); wr(path.join(w, 'tests', 'other.mjs'), 'a');
  const prior = path.join(w, 'docs', 'progress', 'prev'); wr(path.join(prior, 'AUDIT.md'), '# audit\n'); wr(path.join(prior, 'LOCKED_TESTS.txt'), 'tests/listed_test.mjs\n');
  wr(path.join(w, 'upd_locked.diff'), '--- a/tests/listed_test.mjs\n+++ b/tests/listed_test.mjs\n@@ -1 +1 @@\n-a\n+z\n');
  commit(w, 'tests and lock list');
  const hand = path.join(w, 'docs', 'progress', 'r11'); mk(hand);
  const trDir = path.join(tmp, 'tr11'); mk(path.join(trDir, 'subagents')); const trMain = trDir + '.jsonl'; fs.writeFileSync(trMain, '');
  const id = 'b11_lock'; fs.writeFileSync(path.join(trDir, 'subagents', 'agent-' + id + '.jsonl'), JSON.stringify({ type: 'user', timestamp: new Date().toISOString(), message: { content: `AGENT-CONTRACT-MARKER: v1\nHANDIN_DIR: ${hand}\nPRIOR-AUDIT: ${path.join(prior, 'AUDIT.md')}\n## Owner's words\n"x"` } }) + '\n');
  const lk = (command) => spawnSync(process.execPath, [path.join(src, 'tools', 'hooks', 'lock_guard.mjs')], { input: JSON.stringify({ session_id: 's1', transcript_path: trMain, cwd: w, hook_event_name: 'PreToolUse', tool_name: 'Bash', tool_input: { command }, tool_use_id: 'toolu_01', agent_id: id, agent_type: 'general-purpose' }), cwd: elsewhere, encoding: 'utf8', timeout: 60000, env: { ...process.env, CP_MAIN_ROOT: main, CP_GATE_STATE_DIR: path.join(tmp, 'state-lock') } });
  const refused = (r) => r.status === 2 && /REFUSED by the lock guard/.test(r.stderr);
  const sweep = (title, list, want) => { const bad = []; for (const [c, label] of list) { const r = lk(c); const ok = want === 2 ? refused(r) : (r.status === 0 && r.stderr === ''); if (!ok) bad.push(`${label}: ${c.slice(0, 100)}: status ${r.status} ${r.stderr.slice(0, 120)}`); } t(`${title}: ${list.length} real payloads`, bad.length === 0, bad.join('\n')); };
  sweep('F REFUSE chained and aliased fs write forms on a locked test', [
    [`node -e "require('fs').rmSync('tests/listed_test.mjs')"`, "require('fs').rmSync"], [`node -e "require('node:fs').rm('tests/listed_test.mjs',()=>{})"`, "require('node:fs').rm"], [`node -e "require('fs').promises.rm('tests/listed_test.mjs')"`, "require('fs').promises.rm"],
    [`node -e "require('fs/promises').rm('tests/listed_test.mjs')"`, "require('fs/promises').rm"], [`node -e "require('node:fs/promises').unlink('tests/listed_test.mjs')"`, "require('node:fs/promises').unlink"], [`node -e "import('fs').then(m=>m.rmSync('tests/listed_test.mjs'))"`, "import('fs').then(m => m.rmSync)"],
    [`node -e "const {rmSync}=require('fs'); rmSync('tests/listed_test.mjs')"`, 'destructured rmSync'], [`node -e "const fs=require('fs'); fs['rmSync']('tests/listed_test.mjs')"`, "fs['rmSync']"], [`node -e "require('fs').writeFileSync('tests/listed_test.mjs','')"`, "require('fs').writeFileSync"],
    [`node -e "const f=require('fs'); f.truncateSync('tests/listed_test.mjs')"`, 'an alias of the module (f.truncateSync)'],
  ], 2);
  sweep('F ALLOW the same forms on an unlocked file and reads', [
    [`node -e "require('fs').rmSync('tests/other.mjs')"`, 'rmSync of an unlocked file'], [`node -e "require('fs').readFileSync('tests/listed_test.mjs')"`, 'readFileSync of the locked file'], [`node -e "import('fs').then(m=>m.rmSync('tests/other.mjs'))"`, 'import().then rmSync of an unlocked file'],
  ], 0);
  // git commands that rewrite tracked files: judged from git's own list of what they would change
  fs.writeFileSync(path.join(w, 'tests', 'listed_test.mjs'), 'edited');   // the locked file is now modified in the working tree
  sweep('F REFUSE git stash / reset --hard / checkout -- / restore against a MODIFIED locked test', [
    ['git stash', 'git stash'], ['git stash push', 'git stash push'], ['git stash save wip', 'git stash save'], ['git stash push -m wip', 'git stash push -m'], ['git stash -- tests/listed_test.mjs', 'git stash -- <locked>'], ['git stash push -- tests/listed_test.mjs', 'git stash push -- <locked>'],
    ['git reset --hard', 'git reset --hard'], ['git reset --hard HEAD', 'git reset --hard HEAD'], ['git checkout -- tests/listed_test.mjs', 'git checkout -- <locked>'], ['git checkout HEAD -- tests/listed_test.mjs', 'git checkout HEAD -- <locked>'], ['git restore tests/listed_test.mjs', 'git restore <locked>'],
  ], 2);
  sweep('F ALLOW git commands that read, or that do not touch the locked test', [['git stash list', 'git stash list'], ['git stash show', 'git stash show'], ['git status', 'git status'], ['git reset --soft HEAD~1', 'git reset --soft'], ['git reset HEAD tests/other.mjs', 'git reset (mixed) of an unlocked file'], ['git checkout -- tests/other.mjs', 'git checkout -- <unlocked>'], ['git diff', 'git diff'], ['git log --oneline', 'git log']], 0);
  git(w, 'checkout', '--', 'tests/listed_test.mjs'); wr(path.join(w, 'tests', 'other.mjs'), 'edited other');   // only an unlocked file is modified now
  sweep('F ALLOW git stash and reset --hard while only an UNLOCKED file is modified', [['git stash', 'git stash'], ['git reset --hard', 'git reset --hard'], ['git stash push -m x', 'git stash push -m']], 0);
  git(w, 'checkout', '--', 'tests/other.mjs');
  fs.writeFileSync(path.join(w, 'tests', 'listed_test.mjs'), 'committed change'); commit(w, 'a commit that changes the locked test');
  sweep('F REFUSE git revert of a commit that changed a locked test', [['git revert HEAD', 'git revert HEAD'], ['git revert --no-edit HEAD', 'git revert --no-edit HEAD'], ['git revert -n HEAD', 'git revert -n HEAD']], 2);
  wr(path.join(w, 'tests', 'other.mjs'), 'another change'); commit(w, 'a commit that leaves the locked test alone');
  sweep('F ALLOW git revert --abort / --continue and a revert of a commit that left the locked test alone', [['git revert --abort', 'git revert --abort'], ['git revert --continue', 'git revert --continue'], ['git revert HEAD', 'git revert HEAD (only an unlocked file)']], 0);
  sweep('F decision 9: patch --dry-run on a diff of a locked test is only a read (ALLOW)', [['patch --dry-run -p1 -i upd_locked.diff', 'patch --dry-run -i'], ['patch --dry-run -p1 < upd_locked.diff', 'patch --dry-run < diff'], ['cat upd_locked.diff | patch --dry-run -p1', 'cat diff | patch --dry-run'], ['patch -C -p1 -i upd_locked.diff', 'patch -C (check)'], ['patch --check -p1 -i upd_locked.diff', 'patch --check']], 0);
  sweep('F ... and a real patch of the locked test is still refused', [['patch -p1 -i upd_locked.diff', 'patch -i'], ['patch -p1 < upd_locked.diff', 'patch < diff'], ['cat upd_locked.diff | patch -p1', 'cat diff | patch']], 2);
}

// ================================================================ G  delete_guard heredoc
console.log('gates11 G: delete_guard (decision 11): a heredoc written to a file is text; a heredoc fed to an interpreter is code');
{
  const mainG = path.join(tmp, 'roadworld-g'); mk(path.join(mainG, '.git', 'worktrees')); mk(path.join(mainG, 'docs', 'reference', 'aerial')); wr(path.join(mainG, 'docs', 'reference', 'aerial', 'p1.jpg'), 'photo');
  const dgm = await imp('tools/hooks/delete_guard_impl.mjs');
  const dg = (command) => dgm.analyse(command, mainG, 0, null, mainG).length ? 2 : 0;
  const body = "const fs = require('fs'); fs.rmSync(dirx, { recursive: true });";
  const allow = [
    [`cat > /tmp/s.mjs <<'EOF'\n${body}\nEOF`, 'cat > file <<EOF'], [`node -v && cat > /tmp/s.mjs <<'EOF'\n${body}\nEOF`, 'a command before it, then cat > file <<EOF'], [`cat >> notes.md <<'END-OF-FILE'\n${body}\nEND-OF-FILE`, 'cat >> file <<END-OF-FILE'],
    [`tee out.mjs <<'EOF' > /dev/null\n${body}\nEOF`, 'tee file <<EOF'], [`cat > s.mjs <<'EOF'\n${body}\nEOF\nnode tools/check_handin.mjs x`, 'the file is written, a later line runs another tool'], [`git commit -F - <<'EOF'\nfix: fs.rmSync(dirx, { recursive: true }) was refused\nEOF`, 'git commit -F - <<EOF'],
    ["Set-Content s.mjs @'\n" + body + "\n'@", 'PowerShell Set-Content here-string'], ["$code = @'\n" + body + "\n'@\nSet-Content s.mjs $code", 'PowerShell here-string into a variable then Set-Content'],
  ];
  for (const [c, label] of allow) await tt(`G ALLOW: ${label} (a recursive delete named inside text)`, () => ({ ok: dg(c) === 0, detail: 'refused: ' + c.slice(0, 160) }));
  const deny = [
    [`node - <<'EOF'\n${body}\nEOF`, 'node - <<EOF (the heredoc is the program)'], [`cat <<'EOF' | node -\n${body}\nEOF`, 'cat <<EOF | node -'], [`bash <<'EOF'\nrm -rf docs/reference\nEOF`, 'bash <<EOF with rm -rf'],
    [`cat > s.mjs <<'EOF' && node s.mjs\n${body}\nEOF`, 'write the file, then run it in the same command'], [`node <<'EOF'\n${body}\nEOF`, 'node <<EOF'], ["@'\n" + body + "\n'@ | node -", 'PowerShell here-string piped to node -'],
    [`python - <<'EOF'\nimport shutil\nshutil.rmtree(dirx)\nEOF`, 'python - <<EOF with rmtree'],
  ];
  for (const [c, label] of deny) await tt(`G REFUSE: ${label} (the code runs)`, () => ({ ok: dg(c) === 2, detail: 'allowed: ' + c.slice(0, 160) }));
}

// ---------------------------------------------------------------- end
console.log('\nclean_test_temp prefix: guards11- is cleaned with the other suites');
await tt('clean_test_temp lists the guards11- prefix', async () => { const m = await imp('tools/clean_test_temp.mjs'); return m.PREFIXES.includes('guards11-'); });
console.log('test isolation and cleanup');
t('no line mentioning this run\'s temp folder reached the real gate.log', isolation.leaks().length === 0, isolation.leaks().slice(0, 3).join('\n'));
const gone = cleanupTmp(tmp);
t(`the temp folder is removed${process.env.KEEP_HOOKTEST ? ' [KEEP_HOOKTEST set: kept]' : ''}`, gone || !!process.env.KEEP_HOOKTEST, tmp);
console.log(`\n${pass} passed, ${fail} failed`);
if (failedNames.length) console.log('FAILED TESTS:\n - ' + failedNames.join('\n - '));
process.exit(fail ? 1 : 0);
