// Shared by the three test suites: a test run must never write into the REAL .claude/state/gate.log of the main checkout (23 Oct 2026 it did: three handin_gates FAIL-OPEN lines
// from a test worktree raised gate notices in the live main session). Usage in a suite, before anything runs:
//   const guard = isolateState(tmpDir, srcRoot);      // sets CP_GATE_STATE_DIR for this process and its children (a folder inside tmpDir) and starts watching the real log
//   ... at the end:  t('no line of this run reached the real gate.log', guard.leaks().length === 0, guard.leaks().join('\n'));
import fs from 'node:fs';
import path from 'node:path';
import { resolveMainRoot } from './hooks/root.mjs';

export function isolateState(tmpDir, srcRoot, { setEnv = true } = {}) {
  const real = path.join(resolveMainRoot(path.join(srcRoot, 'tools', 'hooks')), '.claude', 'state', 'gate.log');
  let start = 0; try { start = fs.statSync(real).size; } catch { /* no log yet */ }
  const stateDir = path.join(tmpDir, 'state-isolated'); fs.mkdirSync(stateDir, { recursive: true });
  if (setEnv) process.env.CP_GATE_STATE_DIR = stateDir;
  const marker = path.basename(tmpDir);
  return {
    stateDir, realLog: real,
    // lines appended to the real log since the start that mention this run's temp folder (the live hooks keep writing their own lines there: those are not ours)
    leaks() {
      let buf; try { const fd = fs.openSync(real, 'r'); const size = fs.fstatSync(fd).size; const len = Math.max(0, size - start); buf = Buffer.alloc(len); fs.readSync(fd, buf, 0, len, start); fs.closeSync(fd); } catch { return []; }
      return buf.toString('utf8').split('\n').filter((l) => l.includes(marker));
    },
  };
}
