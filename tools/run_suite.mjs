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
import { fileURLToPath } from 'node:url';
import { spawn, spawnSync } from 'node:child_process';
import { STATE_DIR, ROOT, signRecord } from './hooks/lib.mjs';

// gates15 (ruling 2b): refuse to run under a preload; a preload runs before this file and could make the record say anything
if (process.env.NODE_OPTIONS || process.env.NODE_PATH || process.execArgv.some((a) => /^(?:--import|--require|-r|--loader|--experimental-loader)(?:=|$)/.test(a))) { console.error('run_suite: refusing to run under NODE_OPTIONS, NODE_PATH or a node --import / --require preload; run `node tools/run_suite.mjs ...` plainly'); process.exit(2); }
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

const gitOut = (cwd, ...a) => spawnSync('git', a, { cwd, encoding: 'utf8', maxBuffer: 64 * 1024 * 1024 });
function suiteBase(wt) {
  if (process.env.CP_GATE_STATE_DIR && process.env.CP_SUITE_BASE) return { ref: process.env.CP_SUITE_BASE, direct: true };
  const br = String(gitOut(ROOT, 'rev-parse', '--abbrev-ref', 'HEAD').stdout || '').trim();
  if (!br || br === 'HEAD') return { error: `cannot name the branch of the main checkout ${ROOT} (detached or not a repository)` };
  const mb = gitOut(wt, 'merge-base', 'HEAD', br);
  const sha = String(mb.stdout || '').trim();
  return sha ? { ref: sha, direct: true } : { error: `no merge base of HEAD and the main branch "${br}": ${String(mb.stderr || '').trim()}` };
}
// gates15 (ruling 2a): the script's whole LOCAL IMPORT TREE must be unchanged at the merge base, not only the script (the gates14 audit changed a helper the unchanged script imported and forged "999 passed").
// Literal specifiers are followed transitively (import ... from './x', export ... from, import('./x'), require('./x'), side-effect imports). A script whose tree spawns processes, builds an import
// path at run time or uses createRequire runs files the literal graph cannot name: for it the whole top-level folder of every file in the tree, and tools/, must be unchanged too.
const IMPORT_RES = [/\bimport\s+(?:[^'"`()]*?\bfrom\s*)?['"]([^'"]+)['"]/g, /\bexport\s+[^'"`;]*?\bfrom\s*['"]([^'"]+)['"]/g, /\bimport\s*\(\s*['"]([^'"]+)['"]\s*\)/g, /\brequire\s*\(\s*['"]([^'"]+)['"]\s*\)/g];
const DYNAMIC_RE = /\bimport\s*\(\s*(?!['"])|\bcreateRequire\b|\b(?:spawn|spawnSync|exec|execSync|execFile|execFileSync|fork)\s*\(|new\s+Worker\b|\brequire\s*\(\s*(?!['"])/;
export function importClosure(entry) {
  const seen = new Set(); const stack = [path.resolve(entry)]; let dynamic = false; const missing = [];
  const resolveSpec = (spec, from) => {
    if (/^(?:node:|data:|https?:)/.test(spec)) return null;
    let base;
    if (/^file:/i.test(spec)) { try { base = fileURLToPath(spec); } catch { return null; } }
    else if (/^\.{1,2}[\\/]/.test(spec) || path.isAbsolute(spec) || /^[A-Za-z]:[\\/]/.test(spec)) base = path.resolve(path.dirname(from), spec);
    else return null;   // a bare package name: node_modules is not part of the repository
    for (const c of [base, base + '.mjs', base + '.js', base + '.cjs', base + '.json', path.join(base, 'index.js'), path.join(base, 'index.mjs')]) { try { if (fs.statSync(c).isFile()) return c; } catch { /* next */ } }
    missing.push(spec); return null;
  };
  while (stack.length) {
    const f = stack.pop(); if (seen.has(f)) continue; seen.add(f);
    if (!/\.(?:mjs|js|cjs)$/i.test(f)) continue;
    let txt; try { txt = fs.readFileSync(f, 'utf8'); } catch { continue; }
    if (DYNAMIC_RE.test(txt)) dynamic = true;
    for (const re of IMPORT_RES) { re.lastIndex = 0; let m; while ((m = re.exec(txt))) { const r = resolveSpec(m[1], f); if (r && !seen.has(r)) stack.push(r); } }
  }
  return { files: [...seen], dynamic, missing };
}
export function scriptBaseProblem(wt, file) {
  const wtReal = fs.realpathSync(wt);
  const relOf = (f) => path.relative(wtReal, fs.realpathSync(f)).split(path.sep).join('/');
  const rel = relOf(file);
  const b = suiteBase(wt);
  if (b.error) return `--script refused: ${b.error}`;
  const r = spawnSync('git', ['-C', wt, 'show', `${b.ref}:${rel}`], { encoding: 'buffer', maxBuffer: 64 * 1024 * 1024 });
  if (r.status !== 0) return `--script refused: ${rel} does not exist at the merge base with main (${b.ref.slice(0, 10)}): only a test script that main already has can be run as a node suite; a script added on this branch could print any result`;
  let now; try { now = fs.readFileSync(file); } catch { return `--script refused: ${file} cannot be read`; }
  if (Buffer.compare(now, r.stdout) !== 0) return `--script refused: ${rel} differs from the merge base with main (${b.ref.slice(0, 10)}): a script changed on this branch is not run as the suite (it could print any result); run the unchanged scripts of main, or write the result in STATUS`;
  // the import tree (ruling 2a)
  const cl = importClosure(file); const tops = new Set(['tools']);
  for (const f of cl.files) {
    let fr; try { fr = relOf(f); } catch { return `--script refused: ${f} (imported by ${rel}) cannot be resolved`; }
    if (fr.startsWith('..')) return `--script refused: ${rel} imports ${f}, which is outside the worktree: only files of the repository are part of a suite`;
    tops.add(fr.split('/')[0]);
    const rb = spawnSync('git', ['-C', wt, 'show', `${b.ref}:${fr}`], { encoding: 'buffer', maxBuffer: 64 * 1024 * 1024 });
    if (rb.status !== 0) return `--script refused: ${fr}, imported (directly or through another import) by ${rel}, does not exist at the merge base with main (${b.ref.slice(0, 10)}): a helper added on this branch could print any result`;
    let nowF; try { nowF = fs.readFileSync(f); } catch { return `--script refused: ${f} cannot be read`; }
    if (Buffer.compare(nowF, rb.stdout) !== 0) return `--script refused: ${fr}, imported (directly or through another import) by ${rel}, differs from the merge base with main (${b.ref.slice(0, 10)}): a changed helper can make an unchanged script print any result`;
  }
  if (cl.dynamic) {
    for (const top of tops) {
      const d = spawnSync('git', ['-C', wt, 'diff', '--name-only', b.ref, '--', top], { encoding: 'utf8' });
      const u = spawnSync('git', ['-C', wt, 'ls-files', '--others', '--exclude-standard', '--', top], { encoding: 'utf8' });
      const changed = [...String(d.stdout || '').split(/\r?\n/), ...String(u.stdout || '').split(/\r?\n/)].map((x) => x.trim()).filter(Boolean);
      if (changed.length) return `--script refused: ${rel} runs other files (it spawns processes, builds import paths at run time or uses createRequire), so ${top}/ must be unchanged since the merge base with main (${b.ref.slice(0, 10)}); changed: ${changed.slice(0, 5).join(', ')}${changed.length > 5 ? ' ...' : ''}`;
    }
  }
  return null;
}

let bin, args, label;
if (script) {
  bin = path.resolve(wt, toNative(script)); compiler = 'node'; label = 'node';
  if (!/(?:^|[\\/])(?:test_[\w.-]*|[\w.-]*_tests?)\.mjs$/i.test(bin)) die(`--script must be a test_*.mjs or *_test(s).mjs file, got ${script}`);
  if (!fs.existsSync(bin)) die(`${bin} does not exist`);
  // gates14 (main-session decision 2): only a script that ALREADY exists at the merge base with main, byte for byte, is run as a node suite. A script added or changed on the branch could simply print
  // "N passed, 0 failed" (gates13 audit fault 3: tests/test_fake.mjs forged a valid record). The base is the merge base of HEAD with the main checkout's branch; the test fixtures name a base
  // commit with CP_SUITE_BASE, honoured only when CP_GATE_STATE_DIR redirects the state (a record in a redirected state folder counts for nothing in the live gate).
  const baseProblem = scriptBaseProblem(wt, bin);
  if (baseProblem) die(baseProblem, 3);
  args = [bin]; bin = process.execPath;   // the record names the script, below
} else {
  const cand = compiler === 'gcc'
    ? ['build/tests/sim_core_tests.exe', 'build/tests/sim_core_tests']
    : ['build-msvc/tests/Release/sim_core_tests.exe', 'build-msvc/tests/sim_core_tests.exe', 'build-msvc/tests/Debug/sim_core_tests.exe', 'build-msvc/tests/RelWithDebInfo/sim_core_tests.exe'];
  const found = cand.map((c) => path.join(wt, c)).filter((p) => fs.existsSync(p)).sort((x, y) => fs.statSync(y).mtimeMs - fs.statSync(x).mtimeMs)[0];
  if (!found) die(`no ${compiler} test binary in ${wt} (looked for ${cand.join(', ')}); build it first`);
  // gates15 (ruling 2c): the exe must be the one this worktree's CMake build produced: the build folder's CMakeCache.txt names this worktree as its source dir, object files exist under its tests folder,
  // and the exe is not older than the newest of them (a copied or dropped-in executable has no build behind it). The exe's sha256 goes into the record (binarySha).
  const buildProblem = (exe) => {
    const bdir = path.dirname(exe).replace(/[\\/](?:Release|Debug|RelWithDebInfo|MinSizeRel)$/i, '').replace(/[\\/]tests$/i, '');
    let cache; try { cache = fs.readFileSync(path.join(bdir, 'CMakeCache.txt'), 'utf8'); } catch { return `${bdir} has no CMakeCache.txt: the test binary was not produced by a CMake build of this worktree`; }
    const m = cache.match(/^CMAKE_HOME_DIRECTORY:\w+=(.+)$/m);
    const norm = (p) => path.resolve(String(p).trim()).replace(/\\/g, '/').toLowerCase().replace(/\/$/, '');
    if (!m || norm(m[1]) !== norm(wt)) return `${path.join(bdir, 'CMakeCache.txt')} names ${m ? m[1].trim() : 'no source dir'} as its source, not this worktree (${wt}): the binary belongs to another tree`;
    let newest = 0, count = 0; const walk = (d) => { let es = []; try { es = fs.readdirSync(d, { withFileTypes: true }); } catch { return; } for (const e of es) { const f = path.join(d, e.name); if (e.isDirectory()) walk(f); else if (/\.(?:o|obj)$/i.test(e.name)) { count++; newest = Math.max(newest, fs.statSync(f).mtimeMs); } } };
    walk(path.join(bdir, 'tests'));
    if (!count) return `no object files under ${path.join(bdir, 'tests')}: the test binary was not compiled by this build`;
    if (fs.statSync(exe).mtimeMs + 2000 < newest) return `the test binary is older than the newest object file under ${path.join(bdir, 'tests')}: it is a stale or dropped-in file, rebuild it`;
    return null;
  };
  const bp = buildProblem(found); if (bp) die(`the test binary is refused: ${bp}`, 3);
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
// gates15 (ruling 2b): the child gets a CLEAN environment: no NODE_OPTIONS, NODE_PATH or other NODE_* variable (a `--import` preload could print any summary), nothing but the path, system, temp and home
// variables and the CP_* switches the tests need. process.execArgv is not passed to a spawned node either.
const KEEP_ENV = /^(?:PATH|Path|PATHEXT|SystemRoot|SYSTEMROOT|SystemDrive|windir|ComSpec|TEMP|TMP|TMPDIR|HOME|USERPROFILE|HOMEDRIVE|HOMEPATH|APPDATA|LOCALAPPDATA|ProgramData|ProgramFiles|ProgramFiles\(x86\)|ProgramW6432|NUMBER_OF_PROCESSORS|PROCESSOR_ARCHITECTURE|OS|LANG|LC_ALL|TZ|CP_[A-Z0-9_]+)$/;
const cleanEnv = {}; for (const [k, v] of Object.entries(process.env)) if (KEEP_ENV.test(k)) cleanEnv[k] = v;
const child = spawn(bin, args, { cwd: wt, stdio: ['ignore', fd, fd], windowsHide: true, env: cleanEnv });
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
