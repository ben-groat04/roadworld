// Test helper (gates13, replaces the gates12 hook-based helper): make a suite log the way a real run makes it, through the real runner tools/run_suite.mjs.
// A fake node "suite" (a test_*.mjs file kept inside the worktree's .git folder, so the worktree stays clean) prints the wanted output and exits with the wanted code; run_suite.mjs runs it
// under its time limit, writes the log and the signed run record at the run's END. The log is then copied to where the caller wants it (the hand-in folder), which is what a builder does when the
// round folder differs; the record matches by the log's content. Never a hand-made record line: what a build phase check accepts is exactly what run_suite.mjs writes.
import fs from 'node:fs';
import path from 'node:path';
import { spawnSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';

const HOOKS = path.dirname(fileURLToPath(import.meta.url));
export const RUNNER = path.resolve(HOOKS, '..', 'run_suite.mjs');
// logFile: where the log should end up (any name; the runner itself writes suite_node.log). exit: the fake suite's exit code. runner: another copy of run_suite.mjs (fixtures).
// gates14 (decision 2): run_suite --script runs only a script that exists at the merge base with main, unchanged. The fixture makes that true the way main would: the fake script lives in the worktree at
// tools/_fake_suite/ (excluded in .git/info/exclude, so git status stays clean) and a base commit that holds it (plumbing, the branch itself is untouched) is named by CP_SUITE_BASE (honoured only with
// CP_GATE_STATE_DIR set). atBase: false leaves the script out of the base commit: the forgery case (a script added on the branch), which run_suite refuses.
export function logRun(wt, logFile, content, { exit = 0, stateDir = process.env.CP_GATE_STATE_DIR, runner = RUNNER, timeout = 60, scriptBody = null, round = '_runs', atBase = true } = {}) {
  const gitEnv = { ...process.env, GIT_AUTHOR_NAME: 't', GIT_AUTHOR_EMAIL: 't@t', GIT_COMMITTER_NAME: 't', GIT_COMMITTER_EMAIL: 't@t' };
  const git = (...a) => spawnSync('git', ['-C', wt, ...a], { encoding: 'utf8', env: gitEnv });
  const rel = 'tools/_fake_suite/test_fake_suite.mjs';
  const script = path.join(wt, rel);
  fs.mkdirSync(path.dirname(script), { recursive: true });
  fs.writeFileSync(script, scriptBody || `process.stdout.write(${JSON.stringify(String(content))}); process.exit(${Number(exit) || 0});\n`);
  try { const ex = path.resolve(wt, String(git('rev-parse', '--git-path', 'info/exclude').stdout || '').trim()); fs.mkdirSync(path.dirname(ex), { recursive: true }); const cur = fs.existsSync(ex) ? fs.readFileSync(ex, 'utf8') : ''; if (!cur.includes('tools/_fake_suite/')) fs.appendFileSync(ex, '\ntools/_fake_suite/\n'); } catch { /* the folder just shows as untracked */ }
  let base = String(git('rev-parse', 'HEAD').stdout || '').trim();
  if (atBase) {
    const blob = String(git('hash-object', '-w', script).stdout || '').trim();
    const idx = path.join(wt, '.git_fake_index_' + process.pid + '_' + Date.now());
    const withIdx = (...a) => spawnSync('git', ['-C', wt, ...a], { encoding: 'utf8', env: { ...gitEnv, GIT_INDEX_FILE: idx } });
    if (base) withIdx('read-tree', 'HEAD'); else withIdx('read-tree', '--empty');
    withIdx('update-index', '--add', '--cacheinfo', `100644,${blob},${rel}`);
    const tree = String(withIdx('write-tree').stdout || '').trim();
    const c = spawnSync('git', ['-C', wt, 'commit-tree', tree, ...(base ? ['-p', base] : []), '-m', 'fake suite base'], { encoding: 'utf8', env: gitEnv });
    try { fs.rmSync(idx, { force: true }); } catch { /* temp */ }
    base = String(c.stdout || '').trim() || base;
  }
  const env = { ...process.env, CP_GATE_STATE_DIR: stateDir, CP_SUITE_BASE: base };
  const r = spawnSync(process.execPath, [runner, wt, '--script', script, '--round', round, '--timeout', String(timeout)], { encoding: 'utf8', env, timeout: 90000 });
  const own = path.join(wt, 'docs', 'progress', round, 'suite_node.log');
  fs.mkdirSync(path.dirname(logFile), { recursive: true });
  if (round === '_runs') {
    if (fs.existsSync(own)) fs.copyFileSync(own, logFile);
    try { fs.rmSync(path.join(wt, 'docs', 'progress', round), { recursive: true, force: true }); } catch { /* best effort */ }
  }
  return { run: r, ok: r.status === (Number(exit) || 0), log: logFile };
}
