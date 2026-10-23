#!/usr/bin/env node
// gates12: real-payload tables for the send-back of round gates11 (docs/progress/gates11/AUDIT.md and decisions 1-8 of the main session in the gates12 brief).
//   A  second copy of a test (decision 1): the check is PER WORKTREE and matches the running process by its executable path; a run of the same image in another worktree is allowed
//   B  suite run record (decision 2): the hooks (time guard PreToolUse, time_guard_post PostToolUse / PostToolUseFailure) record each suite run; a build phase accepts suite_build.log only when a
//      record of a run in this worktree has the log's hash; a typed or edited log is refused
//   C  launch refusal by meaning (decision 3): every review of another agent's work launched as NO-CONTRACT is refused (the four phrasings the gates11 audit found, and the live descriptions of 23 Oct)
//   D  make_brief run from inside a worktree (decision 4)
//   E  time guard gaps (decision 5): xargs, `timeout N -- cmd`, make family, Unreal Build.bat / RunUAT / UnrealBuildTool, node suites
// Where the expected values come from: docs/progress/gates11/AUDIT.md (items 1-4), the main-session decisions in the brief, the real 23 Oct launch descriptions (tools/hooks/live_launches_23oct.json,
// copied from the main gate.log). Nothing pins a fault. No test writes the live gate.log (CP_GATE_STATE_DIR; the isolation check at the end).
// Usage: node tools/hooks/test_guards12.mjs
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import zlib from 'node:zlib';
import crypto from 'node:crypto';
import { spawnSync } from 'node:child_process';
import { fileURLToPath, pathToFileURL } from 'node:url';
import { isolateState, cleanupTmp } from '../test_isolation.mjs';
import { logRun } from './test_record_lib.mjs';

const src = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..', '..');
const tmp = fs.mkdtempSync(path.join(os.tmpdir(), 'guards12-'));
const isolation = isolateState(tmp, src);
let pass = 0, fail = 0; const failedNames = [];
const t = (name, cond, detail = '') => { if (cond) { pass++; console.log('  ok   ' + name); } else { fail++; failedNames.push(name); console.log('  FAIL ' + name + (detail ? '\n       ' + String(detail).replace(/\n/g, '\n       ') : '')); } };
const tt = async (name, fn) => { try { const r = await fn(); t(name, r === true || (r && r.ok), r && r.detail); } catch (e) { t(name, false, 'THREW ' + String((e && e.message) || e).slice(0, 200)); } };
const mk = (p) => fs.mkdirSync(p, { recursive: true });
const wr = (p, s = 'x') => { mk(path.dirname(p)); fs.writeFileSync(p, s); };
const imp = (rel) => import(pathToFileURL(path.join(src, rel)).href).catch(() => new Proxy({}, { get: (_, k) => k === 'then' ? undefined : () => { throw new Error('module missing: ' + rel); } }));   // a missing module fails the cases that use it, it does not end the run
const sleepSync = (ms) => { const a = new Int32Array(new SharedArrayBuffer(4)); Atomics.wait(a, 0, 0, ms); };
const R = String.raw;

// ---------------------------------------------------------------- pictures and fixture (a temp main checkout with copies of the tools)
const crcT = (() => { const tb = []; for (let n = 0; n < 256; n++) { let c = n; for (let k = 0; k < 8; k++) c = c & 1 ? 0xedb88320 ^ (c >>> 1) : c >>> 1; tb[n] = c >>> 0; } return tb; })();
const crc32 = (buf) => { let c = 0xffffffff; for (const b of buf) c = crcT[(c ^ b) & 255] ^ (c >>> 8); return (c ^ 0xffffffff) >>> 0; };
const chunk = (type, data) => { const len = Buffer.alloc(4); len.writeUInt32BE(data.length); const td = Buffer.concat([Buffer.from(type), data]); const crc = Buffer.alloc(4); crc.writeUInt32BE(crc32(td)); return Buffer.concat([len, td, crc]); };
const makePng = (w = 90, h = 90) => { const rows = []; for (let y = 0; y < h; y++) rows.push(Buffer.concat([Buffer.from([0]), crypto.randomBytes(w * 3)])); const ihdr = Buffer.alloc(13); ihdr.writeUInt32BE(w, 0); ihdr.writeUInt32BE(h, 4); ihdr[8] = 8; ihdr[9] = 2;
  return Buffer.concat([Buffer.from('89504e470d0a1a0a', 'hex'), chunk('IHDR', ihdr), chunk('IDAT', zlib.deflateSync(Buffer.concat(rows), { level: 0 })), chunk('IEND', Buffer.alloc(0))]); };
const pngFile = (p, w, h) => { mk(path.dirname(p)); fs.writeFileSync(p, makePng(w, h)); return p; };
const root = path.join(tmp, 'main12'); mk(path.join(root, '.git')); mk(path.join(root, 'tools', 'hooks')); mk(path.join(root, 'docs', 'templates')); mk(path.join(root, 'docs', 'reference', 'aerial'));
for (const f of fs.readdirSync(path.join(src, 'tools'))) if (/\.mjs$/.test(f)) fs.copyFileSync(path.join(src, 'tools', f), path.join(root, 'tools', f));
for (const f of fs.readdirSync(path.join(src, 'tools', 'hooks'))) if (/\.(?:mjs|json)$/.test(f)) fs.copyFileSync(path.join(src, 'tools', 'hooks', f), path.join(root, 'tools', 'hooks', f));
for (const f of fs.readdirSync(path.join(src, 'docs', 'templates'))) fs.copyFileSync(path.join(src, 'docs', 'templates', f), path.join(root, 'docs', 'templates', f));
fs.copyFileSync(path.join(src, 'docs', 'AGENT_CONTRACT.md'), path.join(root, 'docs', 'AGENT_CONTRACT.md'));
const state = path.join(tmp, 'state-12'); mk(state);
const env = { ...process.env, CP_GATE_STATE_DIR: state }; delete env.CP_MAIN_ROOT; delete env.CP_TIME_GUARD_PROCS; delete env.CP_TIME_GUARD_PROC_CMD; delete env.CP_TIME_GUARD_PATHS; delete env.CP_TIME_GUARD_PATH_CMD;
const elsewhere = path.join(tmp, 'elsewhere'); mk(elsewhere);
const logOf = (dir = state) => { try { return fs.readFileSync(path.join(dir, 'gate.log'), 'utf8').trim().split('\n').filter(Boolean).map((l) => JSON.parse(l)); } catch { return []; } };
const photoDir = path.join(root, 'docs', 'reference', 'aerial');
const photos = ['ph1.png', 'ph2.png', 'ph3.png'].map((n) => pngFile(path.join(photoDir, n), 60, 60));
const git = (cwd, ...a) => spawnSync('git', a, { cwd, encoding: 'utf8', env: { ...process.env, GIT_AUTHOR_NAME: 't', GIT_AUTHOR_EMAIL: 't@t', GIT_COMMITTER_NAME: 't', GIT_COMMITTER_EMAIL: 't@t' } });
function repo(dir) { const w = path.isAbsolute(dir) ? dir : path.join(tmp, dir); mk(w); git(w, 'init', '-q'); git(w, 'config', 'user.email', 't@t'); git(w, 'config', 'user.name', 't'); wr(path.join(w, 'README.md'), 'r'); git(w, 'add', '-A'); git(w, 'commit', '-q', '-m', 'init'); return w; }
const commit = (w, msg) => { git(w, 'add', '-A'); git(w, 'commit', '-q', '-m', msg); return git(w, 'rev-parse', 'HEAD').stdout.trim(); };

// ================================================================ A  second copy of a test: per worktree, by executable path
console.log('gates12 A: a second copy of a running test is refused only when the running process is THIS worktree\'s (decision 1)');
const tgRepo = path.join(tmp, 'tg-wt'); mk(path.join(tgRepo, '.git'));
const tgRepo2 = path.join(tmp, 'tg-wt2'); mk(path.join(tgRepo2, '.git'));
const tg = (command, tool = 'Bash', { cwd = tgRepo, e = {}, stateDir = state, id, bg = false } = {}) => {   // never undefined: an undefined state dir would send the hook to the LIVE gate.log
  const ev = { ...env, CP_GATE_STATE_DIR: stateDir, CP_TIME_GUARD_PROCS: '' }; delete ev.CP_TIME_GUARD_PROC_CMD;
  for (const [k, v] of Object.entries(e)) { if (v === null) delete ev[k]; else ev[k] = v; }
  return spawnSync(process.execPath, [path.join(src, 'tools', 'hooks', 'time_guard.mjs')], { input: JSON.stringify({ hook_event_name: 'PreToolUse', tool_name: tool, session_id: 's1', cwd, agent_id: 'a1', ...(id ? { tool_use_id: id } : {}), tool_input: { command, ...(bg ? { run_in_background: true } : {}) } }), cwd: elsewhere, encoding: 'utf8', timeout: 60000, env: ev });
};
const tgPost = (id, stateDir, { cwd = tgRepo, payload = null } = {}) => spawnSync(process.execPath, [path.join(src, 'tools', 'hooks', 'time_guard_post.mjs')], { input: JSON.stringify(payload || { hook_event_name: 'PostToolUse', tool_name: 'Bash', tool_use_id: id, session_id: 's1', cwd, tool_input: {}, tool_response: { stdout: '', stderr: '', interrupted: false } }), cwd: elsewhere, encoding: 'utf8', timeout: 60000, env: { ...env, CP_GATE_STATE_DIR: stateDir } });
const tgRefuse = (r) => r.status === 2 && /REFUSED by the time-limit guard/.test(r.stderr);
const tgAllow = (r) => r.status === 0 && r.stderr === '';
const exeIn = path.join(tgRepo, 'build', 'tests', 'sim_core_tests.exe'), exeOther = path.join(tgRepo2, 'build', 'tests', 'sim_core_tests.exe');
const cmdA = 'timeout 900 build/tests/sim_core_tests.exe > suite.log 2>&1';
const runsOf = (ds) => { try { return fs.readFileSync(path.join(ds, 'runs.jsonl'), 'utf8').split('\n').filter(Boolean).map((l) => JSON.parse(l)); } catch { return []; } };   // a missing record file is an empty list, not a crash
const dstate = (n) => { const d = path.join(tmp, 'state-' + n); mk(d); return d; };
{
  let ds = dstate('dupA');
  let r = tg(cmdA, 'Bash', { stateDir: ds, id: 'u1' });
  t('A the first limited run is allowed and recorded (a start line with the id, worktree and image)', tgAllow(r) && runsOf(ds).some((o) => o.id === 'u1'), r.stderr);
  r = tg(cmdA, 'Bash', { stateDir: ds, id: 'u2', e: { CP_TIME_GUARD_PROCS: 'sim_core_tests', CP_TIME_GUARD_PATHS: exeIn } });
  t('A REFUSE: a second copy while this worktree\'s own process is running; the refusal names the executable path it matched', tgRefuse(r) && r.stderr.includes(exeIn) && /second copy/.test(r.stderr), `status ${r.status} ${r.stderr.slice(0, 400)}`);
  ds = dstate('dupB');
  tg(cmdA, 'Bash', { stateDir: ds, id: 'u1' });
  r = tg(cmdA, 'Bash', { stateDir: ds, id: 'u2', e: { CP_TIME_GUARD_PROCS: 'sim_core_tests', CP_TIME_GUARD_PATHS: exeOther } });
  t('A ALLOW: the same image is running, but from ANOTHER worktree (the gates11 audit\'s false refusal)', tgAllow(r), `status ${r.status} ${r.stderr.slice(0, 400)}`);
  r = tg(cmdA, 'Bash', { stateDir: ds, id: 'u3', e: { CP_TIME_GUARD_PROCS: 'sim_core_tests', CP_TIME_GUARD_PATHS: exeOther + ';' + exeIn } });
  t('A REFUSE: both worktrees run it; only this worktree\'s path is named in the refusal', tgRefuse(r) && r.stderr.includes(exeIn) && !r.stderr.includes(exeOther), `status ${r.status} ${r.stderr.slice(0, 400)}`);
  ds = dstate('dupC');
  tg(cmdA, 'Bash', { stateDir: ds, id: 'u1' }); const post = tgPost('u1', ds);
  r = tg(cmdA, 'Bash', { stateDir: ds, id: 'u2', e: { CP_TIME_GUARD_PROCS: 'sim_core_tests', CP_TIME_GUARD_PATHS: exeIn } });
  t('A ALLOW: the earlier run of this worktree has an end record (finished); a process of the same image in this worktree is not it', post.status === 0 && tgAllow(r), `status ${r.status} ${r.stderr.slice(0, 400)} ${post.stderr}`);
  ds = dstate('dupD');
  tg(cmdA, 'Bash', { stateDir: ds, id: 'u1', bg: true }); tgPost('u1', ds);
  r = tg(cmdA, 'Bash', { stateDir: ds, id: 'u2', e: { CP_TIME_GUARD_PROCS: 'sim_core_tests', CP_TIME_GUARD_PATHS: exeIn } });
  t('A REFUSE: a BACKGROUND run counts until its process is gone (its Post hook fires when the call returns, not when the process ends)', tgRefuse(r) && r.stderr.includes(exeIn), `status ${r.status} ${r.stderr.slice(0, 400)}`);
  ds = dstate('dupE');
  tg(cmdA, 'Bash', { stateDir: ds, id: 'u1', cwd: tgRepo2 });
  r = tg(cmdA, 'Bash', { stateDir: ds, id: 'u2', e: { CP_TIME_GUARD_PROCS: 'sim_core_tests', CP_TIME_GUARD_PATHS: exeIn } });
  t('A ALLOW: the only unfinished record belongs to another worktree', tgAllow(r), `status ${r.status} ${r.stderr.slice(0, 400)}`);
  ds = dstate('dupF');
  tg(cmdA, 'Bash', { stateDir: ds, id: 'u1' });
  r = tg(cmdA, 'Bash', { stateDir: ds, id: 'u2', e: { CP_TIME_GUARD_PROCS: 'something_else', CP_TIME_GUARD_PATHS: exeIn } });
  t('A ALLOW: the image is no longer in the process list (nothing running at all)', tgAllow(r), `status ${r.status} ${r.stderr.slice(0, 400)}`);
  ds = dstate('dupG');
  tg(cmdA, 'Bash', { stateDir: ds, id: 'u1' });
  const t0 = Date.now();
  r = tg(cmdA, 'Bash', { stateDir: ds, id: 'u2', e: { CP_TIME_GUARD_PROCS: 'sim_core_tests', CP_TIME_GUARD_PATHS: null, CP_TIME_GUARD_PATH_CMD: JSON.stringify([process.execPath, '-e', 'setTimeout(function(){}, 30000)']) } });
  t('A a stuck executable-path lookup is cut (bounded), the command is allowed and one FAIL-OPEN line names the path lookup', tgAllow(r) && Date.now() - t0 < 20000 && logOf(ds).some((l) => l.gate === 'time_guard' && /FAIL-OPEN/.test(l.reason || '') && /executable-path/.test(l.reason || '')), `status ${r.status} ${r.stderr} took ${Date.now() - t0} ${JSON.stringify(logOf(ds).slice(-1))}`);
  const m = await imp('tools/hooks/time_guard_impl.mjs');
  await tt('A underRoot: matches by executable path or by a command line under the worktree, in any slash form, not by a sibling folder', () => {
    const w = path.join(tmp, 'tg-wt'); const sib = path.join(tmp, 'tg-wt-other', 'build', 'sim_core_tests.exe');
    const a = m.underRoot([{ exe: exeIn, cmdline: '' }], w).length === 1;
    const b = m.underRoot([{ exe: 'C:\\Windows\\System32\\cmd.exe', cmdline: `"${exeIn.replace(/\\/g, '/')}" -tc=x` }], w).length === 1;
    const c = m.underRoot([{ exe: sib, cmdline: sib }], w).length === 0;
    const d = m.underRoot([{ exe: '/' + exeIn.replace(/\\/g, '/').replace(/^([A-Za-z]):/, (x, l) => l.toLowerCase()), cmdline: '' }], w).length === 1;
    return { ok: a && b && c && d, detail: [a, b, c, d].join(' ') };
  });
  await tt('A processPaths reads one "<exe>|<command line>" per line from the lookup command (CP_TIME_GUARD_PATH_CMD)', () => {
    process.env.CP_TIME_GUARD_PATH_CMD = JSON.stringify([process.execPath, '-e', `console.log(${JSON.stringify(exeIn + '|"' + exeIn + '" -tc=a')})`]); const v = m.processPaths('sim_core_tests'); delete process.env.CP_TIME_GUARD_PATH_CMD;
    return { ok: Array.isArray(v) && v.length === 1 && v[0].exe === exeIn && /-tc=a/.test(v[0].cmdline), detail: JSON.stringify(v) };
  });
}

// ================================================================ B  suite run record
console.log('gates12 B: the suite record: start (time guard), end + exit + hash (post hook); the build phase accepts only a log a recorded run wrote (decision 2)');
{
  const ds = dstate('rec');
  const logp = path.join(tmp, 'rec', 'suite.log'); mk(path.dirname(logp));
  const cmd = `timeout 900 node tools/hooks/test_guards12.mjs > "${logp.replace(/\\/g, '/')}" 2>&1`;
  let r = tg(cmd, 'Bash', { stateDir: ds, id: 'r1' });
  const start = runsOf(ds);
  t('B a limited node suite run is recorded at its start with the output file it redirects to', tgAllow(r) && start.length === 1 && start[0].ev === 'start' && start[0].kind === 'suite' && path.resolve(start[0].files[0]) === path.resolve(logp), JSON.stringify(start));
  fs.writeFileSync(logp, '5 passed, 0 failed\n');
  const p = tgPost('r1', ds);
  const all = runsOf(ds); const end = all.find((o) => o.ev === 'end');
  t('B the post hook appends an end line: exit 0 and the sha256 of the log as it is then', p.status === 0 && end && end.id === 'r1' && end.exit === 0 && end.files[0].sha256 === crypto.createHash('sha256').update('5 passed, 0 failed\n').digest('hex'), JSON.stringify(end));
  const p2 = tgPost('not-a-suite-run', ds); t('B a call the guard did not record leaves no end line', p2.status === 0 && runsOf(ds).length === 2);
  const pm = await imp('tools/hooks/time_guard_post_impl.mjs'); const ec = (i) => { try { return pm.exitCodeOf(i); } catch (e) { return 'THREW ' + e.message; } };
  const shapes = [
    [{ hook_event_name: 'PostToolUse', tool_response: { stdout: 'x', interrupted: false } }, 0, 'PostToolUse success'],
    [{ hook_event_name: 'PostToolUseFailure', error: 'Exit code 3\nsome output' }, 3, 'PostToolUseFailure "Exit code 3"'],
    [{ hook_event_name: 'PostToolUseFailure', error: 'Command timed out after 600000ms' }, null, 'PostToolUseFailure with no code'],
    [{ hook_event_name: 'PostToolUse', tool_response: { exit_code: 7, stdout: '' } }, 7, 'tool_response.exit_code'],
    [{ hook_event_name: 'PostToolUse', tool_response: { stdout: '', interrupted: true } }, null, 'interrupted'],
  ];
  for (const [inp, want, label] of shapes) t(`B exitCodeOf: ${label} -> ${want}`, ec(inp) === want, String(ec(inp)));
  // the build phase check against records
  const chk = (hd, ...a) => spawnSync(process.execPath, [path.join(root, 'tools', 'check_handin.mjs'), hd, '--main', root, ...a], { cwd: elsewhere, encoding: 'utf8', env, timeout: 600000 });
  const failedWith = (rr, re) => rr.status === 1 && /HAND-IN CHECK FAILED/.test(rr.stderr) && re.test(rr.stderr);
  const passed = (rr) => rr.status === 0 && /HAND-IN CHECK PASSED/.test(rr.stdout);
  const GOOD_MEASURE = (hd) => `# MEASURE\nReferences opened: 3 of 3 listed\n\n## Case A one-lane road\n| photo | measured | value |\n|---|---|---|\n| ${photos[0]} | carriageway width | 3.4 m |\n| ${photos[1]} | lane width | 1.0 lane widths |\n| ${photos[2]} | verge | 150 cm |\n\nOverlay: ${path.join(hd, 'trace_a.png')}\n`;
  function buildOf(name, { run = 'good', logText = '12 passed, 0 failed\n', tamper = null, otherWt = false, lateCode = false, exit = 0 } = {}) {
    const w = repo(name); const hd = path.join(w, 'docs', 'progress', 'rr'); mk(hd); pngFile(path.join(hd, 'trace_a.png'), 80, 60); wr(path.join(hd, 'MEASURE.md'), GOOD_MEASURE(hd)); commit(w, 'measure phase');
    const logFile = path.join(hd, 'suite_build.log');
    wr(path.join(w, 'src.mjs'), 'code'); const sha = commit(w, 'code step');
    if (run === 'good') logRun(w, logFile, logText, { stateDir: state, exit });
    else if (run === 'typed') wr(logFile, logText);
    else if (run === 'other') { const ow = repo(name + '_other'); logRun(ow, logFile, logText, { stateDir: state }); }
    else if (run === 'nopost') logRun(w, logFile, logText, { stateDir: state, skipPost: true });
    else if (run === 'nocode') logRun(w, logFile, logText, { stateDir: state, exit: null });
    else if (run === 'early') { logRun(w, logFile, logText, { stateDir: state }); sleepSync(1300); wr(path.join(w, 'src2.mjs'), 'more code'); commit(w, 'code step two'); }
    if (tamper) fs.appendFileSync(logFile, tamper);
    wr(path.join(hd, 'BUILD.md'), `# BUILD\nCommits: ${sha.slice(0, 10)}\nSuite binary: src.mjs\n`); commit(w, 'BUILD.md and log');
    return { w, hd };
  }
  let rr = chk(buildOf('b12_good').hd, '--phase', 'build');
  t('B a GOOD build hand-in (the log is the redirected output of a recorded run in this worktree, newer than the code, exit 0) passes', passed(rr), rr.stderr + rr.stdout);
  rr = chk(buildOf('b12_typed', { run: 'typed', logText: '[doctest] test cases: 5 | 5 passed | 0 failed | 0 skipped\n' }).hd, '--phase', 'build');
  t('B a log TYPED BY HAND with a valid doctest summary and a "Suite binary" script line FAILS (the gates11 audit\'s forged log)', failedWith(rr, /on record|no run record/), rr.stderr + rr.stdout);
  rr = chk(buildOf('b12_tamper', { tamper: '[doctest] test cases: 9 | 9 passed | 0 failed\n' }).hd, '--phase', 'build');
  t('B a log edited AFTER its run (appended line) FAILS with a hash mismatch', failedWith(rr, /hash mismatch/), rr.stderr + rr.stdout);
  rr = chk(buildOf('b12_other', { run: 'other' }).hd, '--phase', 'build');
  t('B a log written by a recorded run of ANOTHER worktree FAILS', failedWith(rr, /no suite run of this worktree/), rr.stderr + rr.stdout);
  rr = chk(buildOf('b12_nopost', { run: 'nopost' }).hd, '--phase', 'build');
  t('B a run whose end the post hook never recorded (no hash) FAILS', failedWith(rr, /on record|no run record/), rr.stderr + rr.stdout);
  rr = chk(buildOf('b12_early', { run: 'early' }).hd, '--phase', 'build');
  t('B a recorded run that ended BEFORE the last code commit FAILS', failedWith(rr, /before the last code commit|older than the last code commit/), rr.stderr + rr.stdout);
  rr = chk(buildOf('b12_died', { exit: 1 }).hd, '--phase', 'build');
  t('B a recorded run that exited 1 while the log shows no failing test FAILS (it died)', failedWith(rr, /exited with code 1 but the log shows no failing test/), rr.stderr + rr.stdout);
  rr = chk(buildOf('b12_nocode', { run: 'nocode' }).hd, '--phase', 'build');
  t('B a recorded run with an unknown exit code FAILS', failedWith(rr, /no known exit code/), rr.stderr + rr.stdout);
  rr = chk(buildOf('b12_allowed', { logText: 'TEST CASE:  pier clearance\nsrc/pier.cpp:10: ERROR: CHECK( a == b ) is NOT correct!\n[doctest] test cases: 5 | 4 passed | 1 failed | 0 skipped\n', exit: 1 }).hd, '--phase', 'build', '--allow-fail', 'pier');
  t('B a recorded run that failed (exit 1) with only the brief\'s allowed failure passes', passed(rr), rr.stderr + rr.stdout);
}

// ================================================================ C  launch refusal by meaning
console.log('gates12 C: a NO-CONTRACT launch that reviews another agent\'s work is refused by meaning (decision 3)');
{
  const launch = (description, prompt, type = 'general-purpose') => spawnSync(process.execPath, [path.join(root, 'tools', 'hooks', 'brief_gate.mjs')], { input: JSON.stringify({ hook_event_name: 'PreToolUse', tool_name: 'Agent', session_id: 's1', cwd: elsewhere, tool_input: { description, prompt, subagent_type: type } }), cwd: elsewhere, encoding: 'utf8', timeout: 120000, env });
  const nc = (reason) => `NO-CONTRACT: ${reason}\nDo the job.`;
  const refuse = [
    // the four phrasings the gates11 audit found
    'Auditing gates9', 'Second-opinion reviewer of gates11', 'Opus look at grade-r12 images', 'Independent verification of gates9',
    // other forms of the same meaning
    'Please audit gates9', 'Cold read of the gates9 hand-in', 'Verify the gates9 round', 'Reviewer for the roundabout5 round', 'Review the tjunction6 renders', 'Fresh eyes: look at the m2fix5b composites', 'Strict audit of grade-r13', 'Opus first look at the m3ui-r1 frames',
    // the real descriptions of 23 Oct
    'Auditor for gates9 hand-in', 'Opus first look on grade-r12 images'];
  for (const d of refuse) { const r = launch(d, nc('clerk or merge work, 20+ characters, nothing else')); t(`C REFUSE "${d}"`, r.status === 2 && /LAUNCH REFUSED/.test(r.stderr) && /make_brief\.mjs/.test(r.stderr) && /--audit|--first-look/.test(r.stderr), `status ${r.status} ${r.stderr.slice(0, 300)}`); }
  for (const [d, why] of [['Quick job', 'independent cold-read audit of another agent\'s finished round; writes only AUDIT.md'], ['Docs check', 'fresh auditor for the gates9 hand-in (audit job, writes AUDIT.md only)'], ['Image pass', 'short first-look image check for the grade-r12 round (reads images, writes one file)'], ['Second pair of eyes', 'second-opinion review of the junction round; writes one report']]) { const r = launch(d, nc(why)); t(`C REFUSE a neutral description ("${d}") whose stated reason opens with the review job`, r.status === 2 && /LAUNCH REFUSED/.test(r.stderr), `status ${r.status} ${r.stderr.slice(0, 300)}`); }
  const pf = path.join(tmp, 'x', 'docs', 'progress', 'gates9');
  for (const [d, p] of [['Check it', `Review ${pf} and write a report\nNO-CONTRACT: just a quick pass for the main session today`], ['Quick job', `You are the reviewer of ${pf}.\nNO-CONTRACT: nothing special at all about this one`]]) { const r = launch(d, p); t(`C REFUSE a prompt line that orders a review of a progress folder (${d})`, r.status === 2 && /LAUNCH REFUSED/.test(r.stderr), `status ${r.status} ${r.stderr.slice(0, 300)}`); }
  const allow = ['Write audit first-look rule into docs', 'Clerk: photos-outrank-tests rule, commit review', 'Merger: verify playtest, merge integ to main', 'Merge gates7 then gates9 into main', 'Research: Unreal plugin facts', 'Fetch matched junction reference photos', 'Clerk: contract item 26 time limits',
    'Add every-audit-item-fixed rule to docs', 'Earthworks method review (read-only)', 'Measure phase: tjunction6 photos', 'Queue and worktree status check (read-only)', 'Recovery round 2: validate and re-fetch photos', 'Clerk: QUEUE and ACCEPTANCE update'];
  for (const d of allow) { const r = launch(d, nc('clerk, merge, research or read-only status job; mentions audit and review rules only in passing, no build or review hand-in')); t(`C ALLOW "${d}"`, r.status === 0, `status ${r.status} ${r.stderr.slice(0, 300)}`); }
  const hdA = path.join(tmp, 'fl_a', 'docs', 'progress', 'fl'); mk(hdA);
  let r = launch('Auditing gates9', `AUDIT-OF: ${hdA}\nAGENT-CONTRACT-MARKER: v1\n## Owner's words\n"x" (1 Oct)\n`);
  t('C ALLOW "Auditing gates9" when the launch carries AUDIT-OF (the real audit launch)', r.status === 0, r.stderr.slice(0, 300));
  r = launch('Opus look at grade-r12 images', `FIRSTLOOK-OF: ${hdA}\nAGENT-CONTRACT-MARKER: v1\n## Owner's words\n"x" (1 Oct)\n`);
  t('C ALLOW "Opus look at grade-r12 images" when the launch carries FIRSTLOOK-OF', r.status === 0, r.stderr.slice(0, 300));
  // the live sweep: every NO-CONTRACT description of the main gate.log up to 23 Oct (embedded file)
  await tt('C sweep of the live launches: every refused one is a real audit or first look, nothing else is refused (no false refusal)', async () => {
    const pm = await imp('tools/hooks/phase_marks.mjs');
    const rows = JSON.parse(fs.readFileSync(path.join(src, 'tools', 'hooks', 'live_launches_23oct.json'), 'utf8')).rows;
    const isRealReview = (d) => /^(?:Audit\b|Auditor\b|Opus first look\b)/.test(d);
    const bad = rows.filter(([d, why]) => (pm.disguisedJob(d, why, '') !== null) !== isRealReview(d)).map(([d]) => d);
    const refused = rows.filter(([d, why]) => pm.disguisedJob(d, why, '') !== null).length;
    return { ok: bad.length === 0 && rows.length >= 70 && refused >= 40, detail: `rows ${rows.length} refused ${refused} wrong: ${bad.join(' | ')}` };
  });
}

// ================================================================ D  make_brief from inside a worktree
console.log('gates12 D: make_brief --phase build|render run from a worktree finds the sibling worktree folder (decision 4)');
await tt('D from a worktree cwd, --phase build builds <roadworld-wt>/<round> (no doubled roadworld-wt) and finds MEASURE.md there', () => {
  const pp = path.join(tmp, 'pp'); const mainD = path.join(pp, 'roadworld'); const wtA = path.join(pp, 'roadworld-wt', 'wtA'); const target = path.join(pp, 'roadworld-wt', 'rr');
  mk(path.join(mainD, '.git', 'worktrees', 'wtA')); fs.writeFileSync(path.join(mainD, '.git', 'worktrees', 'wtA', 'gitdir'), path.join(wtA, '.git') + '\n'); fs.writeFileSync(path.join(mainD, '.git', 'worktrees', 'wtA', 'commondir'), '../..\n');
  mk(wtA); fs.writeFileSync(path.join(wtA, '.git'), 'gitdir: ' + path.join(mainD, '.git', 'worktrees', 'wtA') + '\n');
  for (const d of ['tools/hooks', 'docs/templates']) mk(path.join(wtA, d));
  for (const f of fs.readdirSync(path.join(root, 'tools'))) if (/\.mjs$/.test(f)) fs.copyFileSync(path.join(root, 'tools', f), path.join(wtA, 'tools', f));
  for (const f of fs.readdirSync(path.join(root, 'tools', 'hooks'))) if (/\.(?:mjs|json)$/.test(f)) fs.copyFileSync(path.join(root, 'tools', 'hooks', f), path.join(wtA, 'tools', 'hooks', f));
  for (const f of fs.readdirSync(path.join(root, 'docs', 'templates'))) fs.copyFileSync(path.join(root, 'docs', 'templates', f), path.join(wtA, 'docs', 'templates', f));
  fs.copyFileSync(path.join(root, 'docs', 'AGENT_CONTRACT.md'), path.join(wtA, 'docs', 'AGENT_CONTRACT.md'));
  const hd = path.join(target, 'docs', 'progress', 'rr'); mk(hd); pngFile(path.join(hd, 'trace_a.png'), 80, 60);
  wr(path.join(hd, 'MEASURE.md'), `# MEASURE\nReferences opened: 3 of 3 listed\n\n## Case A one-lane road\n| photo | measured | value |\n|---|---|---|\n| ${photos[0]} | carriageway width | 3.4 m |\n| ${photos[1]} | lane width | 1.0 lane widths |\n| ${photos[2]} | verge | 150 cm |\n\nOverlay: ${path.join(hd, 'trace_a.png')}\n`);
  const task = path.join(tmp, 'task_wt.md');
  fs.writeFileSync(task, `# T round\n\n## Owner's words\n"and ofc it needs to work with the gate system, have you been sidestepping it?" (23 Oct 2026)\n\n## Cases\nCase A: one-lane road |${photos.join(' ')}\n\n## Faults\nnone known\n\n## Launched\nthis\n\n## Waiting\nnone\n`);
  const run = (...a) => spawnSync(process.execPath, [path.join(wtA, 'tools', 'make_brief.mjs'), task, ...a], { cwd: wtA, encoding: 'utf8', env, timeout: 600000 });
  const rb = run('--round', 'rr', '--phase', 'build', '--no-visual');
  const hline = (rb.stdout.match(/^HANDIN_DIR:[ \t]*(.+)$/m) || [])[1] || '';
  const ok = rb.status === 0 && path.resolve(hline.trim()) === path.resolve(hd) && !/roadworld-wt[\\/]+roadworld-wt/.test(rb.stdout + rb.stderr) && !/does not exist/.test(rb.stderr);
  return { ok, detail: `status ${rb.status} HANDIN_DIR ${hline} expected ${hd}\n${rb.stderr.slice(0, 600)}` };
});

// ================================================================ E  time guard gaps
console.log('gates12 E: time guard: xargs, `timeout N -- cmd`, make family, Unreal Build.bat / RunUAT / UnrealBuildTool, node suites');
{
  const es = dstate('gaps'); const es2 = dstate('gaps2');
  const refuse = [
    ['echo build | xargs ninja', 'Bash', 'xargs ninja with no limit'], ['ls | xargs -n1 ctest', 'Bash', 'xargs -n1 ctest with no limit'], ['timeout 1200 -- ninja -C build', 'Bash', '`timeout 1200 -- ninja` is over the cap'],
    ['make -C build', 'Bash', 'make with no limit'], ['mingw32-make -j4', 'Bash', 'mingw32-make with no limit'], ['gmake all', 'Bash', 'gmake'], ['cd build && make && ctest', 'Bash', 'make and ctest, none limited'],
    [R`"C:\UE_5.8\Engine\Build\BatchFiles\Build.bat" CityPainterEditor Win64 Development`, 'Bash', 'Unreal Build.bat with no limit'], ['C:/UE_5.8/Engine/Build/BatchFiles/RunUAT.bat BuildCookRun -project=x', 'Bash', 'RunUAT with no limit'],
    ['UnrealBuildTool.exe CityPainterEditor Win64 Development', 'Bash', 'UnrealBuildTool with no limit'], ['timeout 9000 RunUAT.bat BuildCookRun', 'Bash', 'RunUAT over the 5400 s cap'], ['cmd /c "C:/UE_5.8/Engine/Build/BatchFiles/Build.bat X Win64 Development"', 'Bash', 'cmd /c Build.bat'],
    [R`& "C:\UE_5.8\Engine\Build\BatchFiles\Build.bat" X Win64 Development`, 'PowerShell', 'Build.bat in PowerShell with no limit'], ['node tools/test_gates.mjs', 'Bash', 'a repo node suite with no limit'], ['node tools/hooks/test_guards12.mjs 2>&1 | tail -5', 'Bash', 'a node suite piped to tail'],
    ['timeout 0 -- ninja', 'Bash', '`timeout 0 --` is no limit'],
  ];
  for (const [c, tool, label] of refuse) { const r = tg(c, tool, { stateDir: es }); t(`E REFUSE (${tool}): ${label}`, tgRefuse(r), `status ${r.status} ${r.stderr.slice(0, 200)}`); }
  const allow = [
    ['timeout 900 -- ninja -C build', 'Bash', '`timeout 900 -- ninja`'], ['timeout 900 xargs ninja', 'Bash', 'timeout around xargs'], ['printf "a\\nb" | xargs -n1 timeout 900 ctest', 'Bash', 'xargs timeout 900 ctest'], ['timeout 600 make -C build', 'Bash', 'timeout make'],
    ['make -n', 'Bash', 'make -n is a dry run'], ['make --help', 'Bash', 'make --help'], ['timeout 5400 RunUAT.bat BuildCookRun -project=x', 'Bash', 'RunUAT with its 5400 s cap'],
    [R`timeout 3000 "C:\UE_5.8\Engine\Build\BatchFiles\Build.bat" X Win64 Development`, 'Bash', 'Build.bat with timeout 3000'], ['timeout 900 node tools/hooks/test_guards12.mjs > suite.log 2>&1', 'Bash', 'a node suite with timeout 900'],
    ['node tools/hooks/test_guards12.mjs --help', 'Bash', 'a node suite --help'], ['git log --grep make --oneline', 'Bash', 'git log --grep make'], ['echo "make is slow"', 'Bash', 'echo'], ['grep -n make Makefile', 'Bash', 'grep for make'],
    ['cmake -S . -B build', 'Bash', 'cmake configure'], ['git commit -m "run make and RunUAT under timeout"', 'Bash', 'a commit message'], ['cat build.ninja | head', 'Bash', 'cat of a file named build.ninja'],
    [R`Select-String make x.txt`, 'PowerShell', 'Select-String that mentions make'], ['node tools/check_handin.mjs docs/progress/x', 'Bash', 'an unrelated node tool'],
  ];
  for (const [c, tool, label] of allow) { const r = tg(c, tool, { stateDir: es2 }); t(`E ALLOW (${tool}): ${label}`, tgAllow(r), `status ${r.status} ${r.stderr.slice(0, 200)}`); }
  const rr = tg('make -C build'); t('E the refusal for make names the way to add the limit (`timeout 900 <cmd>`)', /timeout 900 <cmd>/.test(rr.stderr), rr.stderr);
}

// ---------------------------------------------------------------- end
console.log('\nclean_test_temp prefix: guards12- is cleaned with the other suites');
await tt('clean_test_temp lists the guards12- prefix', async () => { const m = await imp('tools/clean_test_temp.mjs'); return m.PREFIXES.includes('guards12-'); });
console.log('test isolation and cleanup');
t('no line mentioning this run\'s temp folder reached the real gate.log', isolation.leaks().length === 0, isolation.leaks().slice(0, 3).join('\n'));
const gone = cleanupTmp(tmp);
t(`the temp folder is removed${process.env.KEEP_HOOKTEST ? ' [KEEP_HOOKTEST set: kept]' : ''}`, gone || !!process.env.KEEP_HOOKTEST, tmp);
console.log(`\n${pass} passed, ${fail} failed`);
if (failedNames.length) console.log('FAILED TESTS:\n - ' + failedNames.join('\n - '));
process.exit(fail ? 1 : 0);
