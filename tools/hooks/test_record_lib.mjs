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
export function logRun(wt, logFile, content, { exit = 0, stateDir = process.env.CP_GATE_STATE_DIR, runner = RUNNER, timeout = 60, scriptBody = null, round = '_runs' } = {}) {
  const env = { ...process.env, CP_GATE_STATE_DIR: stateDir };
  const dotgit = path.join(wt, '.git');
  const fakeDir = fs.statSync(dotgit).isDirectory() ? path.join(dotgit, 'fake_suite') : path.join(path.dirname(path.resolve(wt)), path.basename(wt) + '_fake_suite');
  fs.mkdirSync(fakeDir, { recursive: true });
  const script = path.join(fakeDir, 'test_fake_suite.mjs');
  fs.writeFileSync(script, scriptBody || `process.stdout.write(${JSON.stringify(String(content))}); process.exit(${Number(exit) || 0});\n`);
  const r = spawnSync(process.execPath, [runner, wt, '--script', script, '--round', round, '--timeout', String(timeout)], { encoding: 'utf8', env, timeout: 90000 });
  const own = path.join(wt, 'docs', 'progress', round, 'suite_node.log');
  fs.mkdirSync(path.dirname(logFile), { recursive: true });
  if (round === '_runs') {
    if (fs.existsSync(own)) fs.copyFileSync(own, logFile);
    try { fs.rmSync(path.join(wt, 'docs', 'progress', round), { recursive: true, force: true }); } catch { /* best effort */ }
  }
  return { run: r, ok: r.status === (Number(exit) || 0), log: logFile };
}
