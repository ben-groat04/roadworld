#!/usr/bin/env node
// The one way to run the test suite (gates13, main-session decision 2).
//   node tools/run_suite.mjs <worktree> [--filter <doctest test-case filter>] [--timeout <seconds, at most 900, default 600>] [--compiler gcc|msvc] [--round <name>]
//   node tools/run_suite.mjs <worktree> --script tests_or_tools/test_x.mjs [--timeout s] [--round name]     (a node suite of the repo, compiler "node")
// What it does: resolves the worktree's OWN test binary by absolute path (build/tests/sim_core_tests.exe for gcc, build-msvc/... for msvc), runs it under the time limit, writes the output to
// <worktree>/docs/progress/<round>/suite_<compiler>.log (suite_<compiler>.filter.log for a filtered run, which never counts as the suite), and when the run ENDS writes a signed record to the gate
// state folder (<state>/suite_records.jsonl): worktree, binary path and sha256, exe mtime, arguments, start, end, exit code, log path and sha256. A background run works: the record is written by this
// process at its end, not by a hook. `check_handin --phase build` accepts a suite log only when such a record matches it (log hash, worktree, binary inside that worktree, end after the last code commit).
// An agent cannot write the state folder (state_guard), so a log made by any other command, an edited log, or a `node -e` printer has no record.
// <round> defaults to the last path segment of the worktree's branch (agent/gates13 -> gates13).
import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import { spawn, spawnSync } from 'node:child_process';
import { STATE_DIR, signRecord } from './hooks/lib.mjs';

const MAX = 900;
const argv = process.argv.slice(2);
const die = (m, code = 2) => { console.error('run_suite: ' + m); process.exit(code); };
const toNative = (p) => (process.platform === 'win32' ? String(p).replace(/^\/([a-zA-Z])(?=\/|$)/, '$1:') : String(p));
let wtArg = null, filter = null, timeout = 600, compiler = 'gcc', round = null, script = null;
for (let i = 0; i < argv.length; i++) {
  const a = argv[i]; const val = () => { const v = argv[++i]; if (v === undefined) die(`${a} needs a value`); return v; };
  if (a === '--filter') filter = val(); else if (a === '--timeout') timeout = Number(val()); else if (a === '--compiler') compiler = val();
  else if (a === '--round') round = val(); else if (a === '--script') script = val();
  else if (a.startsWith('--timeout=')) timeout = Number(a.slice(10)); else if (a.startsWith('--compiler=')) compiler = a.slice(11);
  else if (a.startsWith('--filter=')) filter = a.slice(9); else if (a.startsWith('--round=')) round = a.slice(8); else if (a.startsWith('--script=')) script = a.slice(9);
  else if (a.startsWith('--')) die(`unknown option ${a}`); else if (wtArg === null) wtArg = a; else die(`unexpected argument ${a}`);
}
if (!wtArg) die('usage: node tools/run_suite.mjs <worktree> [--filter tc] [--timeout s] [--compiler gcc|msvc] [--round name] | --script <file.mjs>');
if (!Number.isFinite(timeout) || timeout <= 0 || timeout > MAX) die(`--timeout must be 1..${MAX} seconds (contract item 26), got ${timeout}`);
if (!['gcc', 'msvc'].includes(compiler)) die(`--compiler must be gcc or msvc, got ${compiler}`);
const wt = path.resolve(toNative(wtArg));
if (!fs.existsSync(path.join(wt, '.git'))) die(`${wt} is not a git worktree (no .git)`);
const inside = (p) => { const r = path.relative(fs.realpathSync(wt), fs.realpathSync(p)); return r !== '' && !r.startsWith('..') && !path.isAbsolute(r); };
if (!round) { const r = spawnSync('git', ['-C', wt, 'rev-parse', '--abbrev-ref', 'HEAD'], { encoding: 'utf8' }); round = String(r.stdout || '').trim().split('/').pop() || 'round'; }
if (!/^[\w.-]+$/.test(round)) die(`bad --round ${round}`);

let bin, args, label;
if (script) {
  bin = path.resolve(wt, toNative(script)); compiler = 'node'; label = 'node';
  if (!/(?:^|[\\/])(?:test_[\w.-]*|[\w.-]*_tests?)\.mjs$/i.test(bin)) die(`--script must be a test_*.mjs or *_test(s).mjs file, got ${script}`);
  if (!fs.existsSync(bin)) die(`${bin} does not exist`);
  args = [bin]; bin = process.execPath;   // the record names the script, below
} else {
  const cand = compiler === 'gcc'
    ? ['build/tests/sim_core_tests.exe', 'build/tests/sim_core_tests']
    : ['build-msvc/tests/Release/sim_core_tests.exe', 'build-msvc/tests/sim_core_tests.exe', 'build-msvc/tests/Debug/sim_core_tests.exe', 'build-msvc/tests/RelWithDebInfo/sim_core_tests.exe'];
  const found = cand.map((c) => path.join(wt, c)).filter((p) => fs.existsSync(p)).sort((x, y) => fs.statSync(y).mtimeMs - fs.statSync(x).mtimeMs)[0];
  if (!found) die(`no ${compiler} test binary in ${wt} (looked for ${cand.join(', ')}); build it first`);
  bin = found; label = compiler; args = filter ? [`-tc=${filter}`] : [];
}
const target = script ? path.resolve(wt, toNative(script)) : bin;   // what the record names as "the binary"
if (!inside(target)) die(`${target} is not inside the worktree ${wt}`);
const dir = path.join(wt, 'docs', 'progress', round); fs.mkdirSync(dir, { recursive: true });
const logFile = path.join(dir, `suite_${label}${filter ? '.filter' : ''}.log`);
const sha = (f) => { try { return crypto.createHash('sha256').update(fs.readFileSync(f)).digest('hex'); } catch { return null; } };
const targetSha = sha(target); const targetMtime = fs.statSync(target).mtimeMs;
const start = Date.now();
const fd = fs.openSync(logFile, 'w');
const child = spawn(bin, args, { cwd: wt, stdio: ['ignore', fd, fd], windowsHide: true });
let timedOut = false;
const timer = setTimeout(() => { timedOut = true; try { if (process.platform === 'win32') spawnSync('taskkill', ['/PID', String(child.pid), '/T', '/F'], { windowsHide: true }); else child.kill('SIGKILL'); } catch { /* gone */ } }, timeout * 1000);
const finish = (code, err) => {
  clearTimeout(timer);
  let exit = timedOut ? 124 : (typeof code === 'number' ? code : 1);
  try { fs.writeSync(fd, timedOut ? `\nexit=124 timed out after ${timeout} s\n` : err ? `\nrun_suite: could not run ${bin}: ${err.message}\n` : ''); fs.closeSync(fd); } catch { /* closed */ }
  const rec = { v: 1, id: crypto.randomBytes(8).toString('hex'), worktree: wt, binary: target, binarySha: targetSha, exeMtime: new Date(targetMtime).toISOString(), args, filter: filter || null, compiler: label, round,
    start: new Date(start).toISOString(), end: new Date().toISOString(), endMs: Date.now(), exit, log: logFile, logSha: sha(logFile) };
  try { fs.mkdirSync(STATE_DIR, { recursive: true }); fs.appendFileSync(path.join(STATE_DIR, 'suite_records.jsonl'), JSON.stringify(signRecord(rec)) + '\n'); }
  catch (e) { console.error('run_suite: the run record could not be written (' + e.message + '): this run will not count at a build-phase check'); }
  console.log(`run_suite: ${label} suite ${filter ? '(filtered: ' + filter + ') ' : ''}exit ${exit}${timedOut ? ' (time limit)' : ''}; log ${logFile}`);
  process.exit(exit);
};
child.on('error', (e) => finish(1, e));
child.on('close', (code) => finish(code));
