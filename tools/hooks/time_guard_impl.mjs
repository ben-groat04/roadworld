// PreToolUse hook (matcher Bash|PowerShell): contract item 26 made mechanical (gates11, main-session decision 13). 23 Oct 2026: two agents waited over an hour on hung test runs (one test ran 5.5 hours).
//
// RULES (every session, main or agent; the same command text is judged the same way):
//   T1  a command that RUNS a long job without a time limit is refused. The jobs (the verb of a command segment, after wrappers such as `cd x &&`, `env`, `time`, `&`, `bash -c`, `powershell -Command`):
//       sim_core_tests, ctest, `cmake --build`, ninja, msbuild, Run-Playtest.ps1, Build-Editor.ps1, Run-EditorPython.ps1, `node ... scene.mjs`, `node ... render.mjs`, any `*_tests` or `t_full*` program.
//       A command that only MENTIONS a name (grep ctest, cat sim_core_tests.log, git log --grep ninja, echo ..., a commit message) is not running it and passes.
//       A LIMIT is: a GNU `timeout <N>` prefix (Bash tool; N from 1 to 900 seconds, suffixes s m h d read, so `timeout 15m` is 900); for the three Unreal .ps1 scripts their own `-TimeoutSec <N>`
//       (Build-Editor.ps1 up to 5400, the others up to 1800: an Unreal build is allowed to be longer than a suite); in PowerShell `$p = Start-Process -PassThru ...; Wait-Process -Id $p.Id -Timeout <N>`
//       (N up to 900). `timeout` typed in the PowerShell tool is Windows timeout.exe (a pause, not a limit) and does not count.
//       `timeout 0` (no limit) and a limit over the cap are refused too. Quick read-only forms are not runs: `--help`, `--version`, `--list-test-cases`, `ctest -N`, `cmake --build --help`.
//   T2  (gates12) a second copy of a TEST program (sim_core_tests, ctest, *_tests, t_full*) is refused only when it would be a second copy FROM THIS WORKTREE. The guard writes a "start" line to
//       state/runs.jsonl for every test or suite run it lets through (id, image, worktree root, time, limit, background flag, output files); no end line is written any more (gates13: the runner records the end);
//       a start line is OPEN while it is inside its limit and has no end line (a background run stays open, its Post fires at once). With an open line of this worktree and this image:
//       1. the image-name lookup (tasklist, pgrep off Windows) runs, bounded to 300 ms; not running -> allowed; could not tell -> allowed with a FAIL-OPEN line in gate.log;
//       2. the image IS running: its executable path and command line are read (PowerShell Get-CimInstance, pgrep -a), bounded to 4 s because PowerShell alone needs 0.6 to 1.3 s to start here
//          (measured 23 Oct 2026); a copy whose path or command line lies under this worktree's root refuses and the refusal names that path; copies under another worktree are ignored; a lookup
//          that fails is a FAIL-OPEN allow. A run of the same image in another worktree, or after this worktree's own run ended, never refuses.
//       CP_TIME_GUARD_PROCS="a,b" replaces step 1, CP_TIME_GUARD_PATHS="p1;p2" replaces step 2, CP_TIME_GUARD_PROC_CMD / CP_TIME_GUARD_PATH_CMD='["cmd","arg"]' replace the commands (tests).
//   T3  (gates12) the same walk also knows `xargs [flags] <prog>`, `timeout N -- <prog>`, make / gmake / mingw32-make / nmake, Unreal Build.bat (under Engine/Build/BatchFiles), RunUAT, RunUBT,
//       UnrealBuildTool (an Unreal build may run up to 5400 s) and the repo's node suites (`node ... test_*.mjs`, `*_test(s).mjs`), which are limited and recorded like the C++ suite.
// WHAT IT CANNOT SEE: a job started by a script file (`bash run_all.sh`), a variable or command substitution holding the program name, an alias, a `-TimeoutMinutes` backstop typed with no -TimeoutSec
// (the .ps1 scripts' old parameter stays, but it is not accepted as the limit: the guard wants seconds, like the contract). FAIL OPEN (logged) on any internal error.
import fs from 'node:fs';
import path from 'node:path';
import { spawnSync } from 'node:child_process';
import { STATE_DIR, log, readStdin, parseJson } from './lib.mjs';
import { parseCommand, shellInner, WRAPPER_VERBS, skipRunnerArgs } from './shell_lex.mjs';

export const MAX_SECS = 900;
const PS_CAP = { 'build-editor': 5400, 'run-playtest': 1800, 'run-editorpython': 1800 };
const secsOf = (txt) => {
  const m = String(txt).match(/^(\d+(?:\.\d+)?)([smhd]?)$/i); if (!m) return null;
  return Number(m[1]) * ({ '': 1, s: 1, m: 60, h: 3600, d: 86400 })[m[2].toLowerCase()];
};
const baseVerb = (v) => String(v).replace(/^\\/, '').toLowerCase().replace(/^.*[\/\\]/, '').replace(/\.(?:exe|bat|cmd)$/, '');
const isTestName = (b) => /_tests$/.test(b) || /^t_full/.test(b) || b === 'ctest';
const UBT_NAMES = /^(?:runuat|runubt|unrealbuildtool|ubt)$/;   // gates12: Build.bat / RunUAT / UnrealBuildTool: an Unreal build may run longer than a suite
const UBT_CAP = 5400;
const QUICK =/^(?:--help|-h|-\?|--version|-v|--list-test-cases|-ltc|--list-test-suites|-lts|--list-reporters|-lr)$/i;

// the walk over wrappers: returns { i (index of the program word), tm (seconds of the tightest timeout wrapper, null if none, 0 = unlimited), startProc, psTimeout }
function walk(words, shell, inheritedTm) {
  let i = 0, tm = inheritedTm, startProc = false;
  while (i < words.length) {
    const v = words[i].v.replace(/^\\/, ''); const lv = baseVerb(v);
    if (lv === 'xargs') {   // gates12: `... | xargs ninja`, `xargs -n1 timeout 900 ctest`: the program is the first word after xargs' own flags
      i++;
      while (i < words.length && /^-/.test(words[i].v) && words[i].v !== '--') i += /^-(?:[nIPLdsEaR]|-max-args|-max-procs|-replace|-delimiter|-arg-file)$/.test(words[i].v) ? 2 : 1;
      if (i < words.length && words[i].v === '--') i++;
      continue;
    }
    if (v === '--' && i > 0) { i++; continue; }   // gates12: `timeout 900 -- ninja`
    if (WRAPPER_VERBS.has(lv) || WRAPPER_VERBS.has(v.toLowerCase())) {
      i++;
      if (lv === 'timeout') {
        if (shell !== 'bash') { while (i < words.length && /^[-/]/.test(words[i].v)) i++; if (i < words.length && secsOf(words[i].v) !== null) i++; continue; }   // Windows timeout.exe: a pause, never a limit
        while (i < words.length && /^-/.test(words[i].v)) i += /^-[ks]$/.test(words[i].v) ? 2 : 1;
        const s = i < words.length ? secsOf(words[i].v) : null;
        if (s !== null) { i++; tm = tm === null ? s : (s === 0 ? tm : (tm === 0 ? s : Math.min(tm, s))); }
      } else if (lv === 'taskset' || lv === 'chrt' || lv === 'stdbuf' || lv === 'env' || lv === 'start') { i = skipRunnerArgs(lv, words, i); }   // gates13: prefix runners, by position
      else if (lv === 'nice' || lv === 'ionice') { while (i < words.length && /^-/.test(words[i].v)) i += /^-[nc]$/.test(words[i].v) ? 2 : 1; }
      else if (lv === 'sudo' || lv === 'doas') { while (i < words.length && /^-/.test(words[i].v)) i += /^-[ugCT]$/.test(words[i].v) ? 2 : 1; }
      else if (lv === 'start-process') {
        startProc = true;
        while (i < words.length && /^-/.test(words[i].v)) {
          const f = words[i].v.toLowerCase(); i++;
          if (f === '-filepath') break;
          if (/^-(?:argumentlist|workingdirectory|verb|redirectstandardoutput|redirectstandarderror|redirectstandardinput|windowstyle)$/.test(f)) i++;
        }
      }
      continue;
    }
    if (/^[A-Za-z_]\w*=/.test(v) && !words[i].q) { i++; continue; }
    if (i > 0 && /^-/.test(v) && WRAPPER_VERBS.has(String(words[i - 1].v).toLowerCase())) { i++; continue; }
    break;
  }
  return { i, tm, startProc };
}

// is this program word + its arguments a long job? returns { name, kind } or null
function classify(verb, args) {
  const b = baseVerb(verb);
  const av = args.map((a) => a.v);
  if (isTestName(b)) return av.some((a) => QUICK.test(a)) || (b === 'ctest' && av.some((a) => /^(?:-N|--show-only(?:=.*)?|--help|--version)$/.test(a))) ? null : { name: b, kind: 'test' };
  if (/^(?:make|gmake|mingw32-make|nmake)$/.test(b)) return av.some((a) => QUICK.test(a) || /^(?:-n|--dry-run|--just-print|--recon|-q|--question|-p|--print-data-base)$/.test(a)) ? null : { name: b, kind: 'build' };   // gates12
  if (UBT_NAMES.test(b) || (b === 'build' && /\/engine\/build\/batchfiles\/build\.(?:bat|sh)$/i.test(String(verb).replace(/\\/g, '/')))) return av.some((a) => QUICK.test(a)) ? null : { name: b === 'build' ? 'Build.bat (UnrealBuildTool)' : b, kind: 'build', cap: UBT_CAP, capAny: true };   // gates12
  if (b === 'ninja' || b === 'msbuild') return av.some((a) => QUICK.test(a)) || av.includes('-t') ? null : { name: b, kind: 'build' };
  if (b === 'cmake') return av.includes('--build') && !av.some((a) => /^(?:--help|-h)$/.test(a)) ? { name: 'cmake --build', kind: 'build' } : null;
  const ps = b.replace(/\.ps1$/, '');
  if (PS_CAP[ps]) return av.some((a) => /^(?:-\?|-help|--help)$/i.test(a)) ? null : { name: ps + '.ps1', kind: 'ps', cap: PS_CAP[ps] };
  if (/^(?:node|nodejs)$/.test(b)) {
    const f = av.find((a) => /\.(?:mjs|js|cjs)$/i.test(a));
    if (f && /^run_suite\.mjs$/i.test(baseVerbRaw(f))) return runnerJob(av.slice(av.indexOf(f) + 1));   // gates13: tools/run_suite.mjs, the one suite runner: it limits itself (--timeout, default 600, at most 900)
    if (f && /^(?:scene|render)\.mjs$/i.test(baseVerbRaw(f))) return av.some((a) => QUICK.test(a)) ? null : { name: baseVerbRaw(f), kind: 'tool' };
    if (f && /^test_[\w.-]*\.mjs$|^[\w.-]*_tests?\.mjs$/i.test(baseVerbRaw(f))) return av.some((a) => QUICK.test(a)) ? null : { name: baseVerbRaw(f), kind: 'suite' };   // gates12: the repo's node suites (tools/test_gates.mjs, tools/hooks/test_guards12.mjs): limited and recorded like the C++ suite
  }
  return null;
}
// tools/run_suite.mjs <worktree> [--filter tc] [--timeout s] [--compiler gcc|msvc] [--script rel] [--round name]: the positional word is the worktree; its own limit stands in for the `timeout` prefix
const RUNNER_VALUE_FLAGS = /^--(?:filter|timeout|compiler|script|round)$/;
function runnerJob(rest) {
  let wtArg = null, tmo = null, script = false;
  for (let k = 0; k < rest.length; k++) {
    const a = rest[k];
    if (/^--timeout$/.test(a)) { tmo = rest[k + 1]; k++; continue; }
    if (/^--timeout=/.test(a)) { tmo = a.slice(10); continue; }
    if (/^--script(?:=|$)/.test(a)) { script = true; if (a === '--script') k++; continue; }
    if (RUNNER_VALUE_FLAGS.test(a)) { k++; continue; }
    if (/^--/.test(a)) continue;
    if (wtArg === null) wtArg = a;
  }
  return { name: script ? 'run_suite.mjs (node suite)' : 'sim_core_tests', kind: script ? 'suite' : 'test', runner: true, runnerTimeout: tmo, worktree: wtArg };
}
const baseVerbRaw = (f) => String(f).toLowerCase().replace(/^.*[\/\\]/, '');
const PS_FILE_FLAG = /^-(?:f|file)$/i;

// walk a command text; collects jobs [{ name, kind, limit: {ok, why}, shell }]
export function jobsIn(command, shell = 'bash', inheritedTm = null, depth = 0, ctx = { cwd: null }) {
  const jobs = []; if (depth > 4) return jobs;
  let parsed; try { parsed = parseCommand(command); } catch { return jobs; }
  const segs = parsed.segments || [];
  // PowerShell Start-Process is limited by a Wait-Process -Timeout N anywhere in the same command
  let waitSecs = null;
  for (const s of segs) {
    const w = s.words.map((x) => x.v); const k = w.findIndex((x) => /^wait-process$/i.test(baseVerb(x)));
    if (k >= 0) { const t = w.findIndex((x, j) => j > k && /^-timeout$/i.test(x)); if (t >= 0 && secsOf(w[t + 1]) !== null) waitSecs = secsOf(w[t + 1]); }
  }
  for (const seg of segs) {
    if (!seg.words.length) continue;
    const { i, tm, startProc } = walk(seg.words, shell, inheritedTm);
    if (i >= seg.words.length) continue;
    const verb = seg.words[i].v; const args = seg.words.slice(i + 1); const b = baseVerb(verb); const av = args.map((a) => a.v);
    // gates13 (decision 3): the worktree comes from the command, not from the payload cwd (an agent's tool call starts in the MAIN checkout): `cd <path>` moves the working folder for the segments after it
    if (/^(?:cd|chdir|pushd|set-location|sl)$/.test(b)) { const t = av.find((a) => !/^-/.test(a)); if (t) ctx.cwd = resolveDir(t, ctx.cwd); continue; }
    // nested shells
    if (/^(?:bash|sh|zsh|dash|ksh)$/.test(b) || /^(?:powershell|pwsh|cmd)$/.test(b)) {
      const inner = shellInner(b, av);
      const sh2 = /^(?:powershell|pwsh|cmd)$/.test(b) ? 'ps' : 'bash';
      if (inner) { jobs.push(...jobsIn(inner, sh2, tm, depth + 1, ctx)); continue; }
      if (sh2 === 'ps') {   // -File script.ps1 args
        const fi = av.findIndex((a) => PS_FILE_FLAG.test(a));
        if (fi >= 0 && av[fi + 1]) { const rest = seg.words.slice(i + 1 + fi + 2); const c = classify(av[fi + 1], rest); if (c) jobs.push(withRoot(finish(c, rest, tm, shell === 'ps' ? 'ps' : 'bash', startProc, waitSecs), av[fi + 1], c, ctx.cwd)); }
        continue;
      }
    }
    const c = classify(verb, args);
    if (c) { jobs.push(withRoot(finish(c, args, tm, shell, startProc, waitSecs), verb, c, ctx.cwd)); continue; }
    // iex "..." / invoke-expression "...": a command line in a string
    if (seg.words[i].q && /\s/.test(verb)) jobs.push(...jobsIn(verb, shell, tm, depth + 1, ctx));
  }
  return jobs;
}
function finish(c, args, tm, shell, startProc, waitSecs) {
  const av = args.map((a) => a.v);
  let secs = null, how = null;
  if (c.kind === 'ps') {
    const k = av.findIndex((a) => /^-timeoutsec(?::|=)?/i.test(a));
    if (k >= 0) { const m = av[k].match(/^-timeoutsec[:=](.+)$/i); const s = secsOf(m ? m[1] : av[k + 1]); if (s !== null) { secs = s; how = '-TimeoutSec'; } }
  }
  if (c.runner) { const sv = c.runnerTimeout === null ? 600 : secsOf(c.runnerTimeout); secs = sv === null ? 0 : sv; how = 'run_suite.mjs --timeout' + (c.runnerTimeout === null ? ' (default 600)' : ''); }
  if (secs === null && tm !== null && shell === 'bash') { secs = tm; how = 'timeout'; }
  if (secs === null && startProc && waitSecs !== null) { secs = waitSecs; how = 'Wait-Process -Timeout'; }
  const cap = (c.kind === 'ps' && how === '-TimeoutSec') || c.capAny ? c.cap : MAX_SECS;
  let why = null;
  if (secs === null) why = 'none';
  else if (secs <= 0) why = `zero (${how} 0 means no limit)`;
  else if (secs > cap) why = `${how} ${secs} s is over the ${cap} s cap`;
  return { name: c.name, kind: c.kind, shell, secs: secs === null ? null : secs, how, ok: why === null, why };
}

const advice = (j) => j.shell === 'ps'
  ? 'In PowerShell: run the Unreal scripts with their own limit (`-TimeoutSec 600`; Build-Editor.ps1 may use up to 5400, Run-Playtest.ps1 and Run-EditorPython.ps1 up to 1800), or start the program and bound the wait: `$p = Start-Process -PassThru -NoNewWindow <exe> -ArgumentList ...; Wait-Process -Id $p.Id -Timeout 600` (900 at most for a full suite), or run it from the Bash tool as `timeout 900 <cmd>`. `timeout` typed in PowerShell is Windows timeout.exe, a pause, not a limit.'
  : 'Put GNU timeout in front: `timeout 900 <cmd>` for a full suite (15 minutes at most), `timeout 600 <cmd>` for anything else, e.g. `timeout 900 build/tests/sim_core_tests.exe > suite.log 2>&1`; for the Unreal scripts add `-TimeoutSec 600`.';

// ---- gates13 (decision 3): the worktree of a job, read from the command. An MSYS "/c/x" is "c:/x" on Windows.
const toNative = (p) => (process.platform === 'win32' ? String(p).replace(/^\/([a-zA-Z])(?=\/|$)/, '$1:') : String(p));
export function resolveDir(t, cwd) { const base = cwd || process.cwd(); return path.resolve(base, toNative(String(t).replace(/^["']|["']$/g, ''))); }
// the root a job names: a run_suite worktree argument, else the program's own absolute (or working-folder-relative) path, else the folder the command has cd'd to, else the payload cwd
function withRoot(job, verb, c, cwd) {
  let probe = null;
  if (c.runner && c.worktree) probe = resolveDir(c.worktree, cwd);
  else if (/[\\/]/.test(String(verb))) probe = resolveDir(String(verb), cwd);
  job.probe = probe; job.cdCwd = cwd;
  return job;
}
export function rootOfJob(job, payloadCwd) { return findRoot(job.probe || job.cdCwd || payloadCwd || process.cwd()); }
// ---- second copy of a test program (gates12: per worktree, by executable path)
export function findRoot(cwd) {
  let d = path.resolve(cwd || process.cwd());
  for (let k = 0; k < 40; k++) { if (fs.existsSync(path.join(d, '.git'))) return d; const p = path.dirname(d); if (p === d) break; d = p; }
  return path.resolve(cwd || process.cwd());
}
// one spelling for a path: forward slashes, lower case, no trailing slash, an MSYS "/c/x" read as "c:/x"
export const normP = (p) => String(p).replace(/\\/g, '/').replace(/^\/([a-zA-Z])\//, '$1:/').replace(/\/+/g, '/').toLowerCase().replace(/\/+$/, '');
const LOOKUP_MS = 300;       // the image-name lookup (tasklist), run whenever an unfinished run of this worktree is on record
const PATH_LOOKUP_MS = 4000; // the executable-path lookup, run only when that image IS running: PowerShell needs 0.6 to 1.3 s to start on this machine, so it cannot fit 300 ms
export function processRunning(name) {
  // returns true / false / null (null = could not tell within 300 ms)
  const forced = process.env.CP_TIME_GUARD_PROCS;
  if (forced !== undefined) return forced.split(',').map((x) => x.trim().toLowerCase().replace(/\.exe$/, '')).includes(name);
  let cmd;
  try { cmd = process.env.CP_TIME_GUARD_PROC_CMD ? JSON.parse(process.env.CP_TIME_GUARD_PROC_CMD) : (process.platform === 'win32' ? ['tasklist', '/FI', `IMAGENAME eq ${name}.exe`, '/FO', 'CSV', '/NH'] : ['pgrep', '-x', name]); } catch { return null; }
  const r = spawnSync(cmd[0], cmd.slice(1), { timeout: LOOKUP_MS, encoding: 'utf8', windowsHide: true });
  if (r.error || r.signal) return null;
  if (process.platform === 'win32' || process.env.CP_TIME_GUARD_PROC_CMD) return String(r.stdout).toLowerCase().includes(name);
  return r.status === 0;
}
// the executable path and command line of every running copy of the image: [{ exe, cmdline }] or null (could not tell). CP_TIME_GUARD_PATHS="p1;p2" replaces the lookup, CP_TIME_GUARD_PATH_CMD='["cmd","arg"]'
// replaces the command (its stdout: one "<exe>|<command line>" per line), both for tests.
export function processPaths(name) {
  const forced = process.env.CP_TIME_GUARD_PATHS;
  if (forced !== undefined) return forced.split(';').map((x) => x.trim()).filter(Boolean).map((p) => ({ exe: p, cmdline: p }));
  let cmd;
  try {
    cmd = process.env.CP_TIME_GUARD_PATH_CMD ? JSON.parse(process.env.CP_TIME_GUARD_PATH_CMD)
      : process.platform === 'win32' ? ['powershell', '-NoProfile', '-NonInteractive', '-Command', `Get-CimInstance Win32_Process -Filter "Name='${name}.exe'" | ForEach-Object { $_.ExecutablePath + '|' + $_.CommandLine }`]
      : ['pgrep', '-a', '-x', name];
  } catch { return null; }
  const r = spawnSync(cmd[0], cmd.slice(1), { timeout: PATH_LOOKUP_MS, encoding: 'utf8', windowsHide: true });
  if (r.error || r.signal) return null;
  const lines = String(r.stdout).split(/\r?\n/).map((l) => l.trim()).filter(Boolean);
  if (process.platform === 'win32' || process.env.CP_TIME_GUARD_PATH_CMD) return lines.map((l) => { const k = l.indexOf('|'); return { exe: k >= 0 ? l.slice(0, k) : l, cmdline: k >= 0 ? l.slice(k + 1) : l }; });
  return lines.map((l) => ({ exe: l.replace(/^\d+\s+/, '').split(/\s+/)[0], cmdline: l.replace(/^\d+\s+/, '') }));
}
// a copy of this worktree's own image: its executable path or its command line lies under the worktree root
export function underRoot(procs, root) { const r = normP(root) + '/'; return procs.filter((p) => normP(p.exe).startsWith(r) || normP(p.cmdline).includes(r)); }
export const runsFile = () => path.join(STATE_DIR, 'runs.jsonl');
function readRuns(n = 400) { try { return fs.readFileSync(runsFile(), 'utf8').split('\n').filter(Boolean).slice(-n).map((l) => { try { return JSON.parse(l); } catch { return null; } }).filter(Boolean); } catch { return []; } }
// start records of THIS worktree and image that are inside their time limit and have not ended (a Post hook "end" record with the same id); a background run counts until its limit is over
export function openRuns(root, name, now = Date.now()) {
  const all = readRuns(); const ended = new Set(all.filter((o) => o.ev === 'end').map((o) => o.id));
  return all.filter((o) => o.ev !== 'end' && o.name === name && normP(o.root) === normP(root) && now - o.ts < (o.secs || MAX_SECS) * 1000 && (o.background || !ended.has(o.id)));
}
export const recentRuns = openRuns;
export function recordRun(root, j, extra = {}) { try { fs.mkdirSync(STATE_DIR, { recursive: true }); fs.appendFileSync(runsFile(), JSON.stringify({ ev: 'start', ts: Date.now(), name: j.name, kind: j.kind, root, secs: j.secs || MAX_SECS, ...extra }) + '\n'); } catch { /* never break a command */ } }

export function judge(command, tool, cwd) {
  const shell = /^powershell$/i.test(tool) ? 'ps' : 'bash';
  const jobs = jobsIn(command, shell, null, 0, { cwd: cwd ? path.resolve(cwd) : null });
  const reasons = jobs.filter((j) => !j.ok).map((j) => `\`${j.name}\` runs without a time limit (${j.why === 'none' ? 'no limit found' : j.why}). ${advice(j)}`);
  return { jobs, reasons };
}

// the files a command writes its output to: `> f`, `>> f`, `&> f`, `2> f`, `| tee [-a] f`, Out-File / Set-Content / Add-Content (resolved against cwd)
export function outputFiles(command, cwd) {
  const out = new Set(); const c = String(command);
  const add = (w) => { const p = String(w).replace(/^["']|["']$/g, ''); if (!p || /^(?:\/dev\/null|nul|&\d|\$null)$/i.test(p) || /[$`*?]/.test(p)) return; out.add(path.resolve(cwd || process.cwd(), p)); };
  for (const m of c.matchAll(/(?:^|[\s;&|(])(?:\d|&)?>{1,2}\s*("[^"]+"|'[^']+'|[^\s;&|<>()]+)/g)) add(m[1]);
  for (const m of c.matchAll(/\btee\s+(?:-[a-z]+\s+)*("[^"]+"|'[^']+'|[^\s;&|<>()]+)/g)) add(m[1]);
  for (const m of c.matchAll(/\b(?:Out-File|Set-Content|Add-Content)\s+(?:-(?:FilePath|Path)\s+)?("[^"]+"|'[^']+'|[^\s;&|<>()]+)/gi)) add(m[1]);
  return [...out];
}

export async function main() {
  const raw = await readStdin();
  const input = raw === null ? undefined : parseJson(raw);
  if (!input || typeof input !== 'object') { log({ gate: 'time_guard', decision: 'allow', reason: 'FAIL-OPEN: stdin missing or not JSON' }); return 0; }
  if (!/^(Bash|PowerShell)$/.test(input.tool_name || '')) return 0;
  const command = (input.tool_input && input.tool_input.command) || '';
  if (!command || !/(?:_tests?|test_|t_full|ctest|ninja|msbuild|cmake|\b[ng]?make\b|-make|Build\.bat|Build\.sh|RunUAT|RunUBT|UnrealBuildTool|Run-Playtest|Build-Editor|Run-EditorPython|scene\.mjs|render\.mjs|run_suite)/i.test(command)) return 0;
  const { jobs, reasons } = judge(command, input.tool_name, input.cwd);
  const dup = [];
  if (!reasons.length) {
    // gates13 (decision 3): every job has its own root, read from the command (cd <path>, the program's absolute path, the run_suite worktree argument); the payload cwd is the last fallback
    const tests = [...new Map(jobs.filter((j) => j.kind === 'test').map((j) => [j.name + '|' + normP(rootOfJob(j, input.cwd)), j])).values()];
    for (const j of tests) {
      const root = rootOfJob(j, input.cwd);
      if (!openRuns(root, j.name).length) continue;       // no unfinished run of this image from this worktree: nothing to compare (a finished run, or one of another worktree, is no reason)
      const up = processRunning(j.name);
      if (up === null) { log({ gate: 'time_guard', decision: 'allow', reason: 'FAIL-OPEN: process lookup for ' + j.name + ' timed out or failed (300 ms bound); second-copy check skipped', cwd: input.cwd }); continue; }
      if (!up) continue;
      const procs = processPaths(j.name);
      if (procs === null) { log({ gate: 'time_guard', decision: 'allow', reason: 'FAIL-OPEN: executable-path lookup for ' + j.name + ' timed out or failed (' + PATH_LOOKUP_MS + ' ms bound); second-copy check skipped', cwd: input.cwd }); continue; }
      const mine = underRoot(procs, root);
      if (mine.length) { const rec = openRuns(root, j.name).slice(-1)[0]; dup.push(`a copy of \`${j.name}\` from this worktree is still running: ${mine[0].exe || mine[0].cmdline} (matched under ${root}; its record started ${Math.round((Date.now() - rec.ts) / 1000)} s ago). Never start a second copy. Wait for it (its own time limit ends it), read its log, or find the slow case with filters (-tc, --durations). Copies running from other worktrees are not counted.`); }
    }
  }
  const all = [...reasons, ...dup];
  if (!all.length) {
    // gates13: every test program run that passes is recorded as a start line (the second-copy check reads it). The SUITE record that check_handin --phase build reads is written by tools/run_suite.mjs itself when its run ends.
    for (const j of jobs.filter((x) => x.kind === 'test')) recordRun(rootOfJob(j, input.cwd), j, { id: input.tool_use_id || `${Date.now()}-${process.pid}`, cwd: input.cwd || null, background: !!(input.tool_input && input.tool_input.run_in_background), command: String(command).slice(0, 400), files: outputFiles(command, input.cwd) });
    return 0;
  }
  log({ gate: 'time_guard', decision: 'deny', reason: dup.length && !reasons.length ? 'second copy of a running test refused' : 'long run without a time limit refused', agent_id: input.agent_id || null, cwd: input.cwd, command: String(command).slice(0, 300), reasons: all });
  process.stderr.write('REFUSED by the time-limit guard (tools/hooks/time_guard.mjs; contract item 26):\n - ' + all.join('\n - ') + '\n');
  return 2;
}
