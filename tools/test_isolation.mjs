// Shared by the three test suites: a test run must never write into the REAL .claude/state/gate.log of the main checkout (23 Oct 2026 it did: three handin_gates FAIL-OPEN lines
// from a test worktree raised gate notices in the live main session). Usage in a suite, before anything runs:
//   const guard = isolateState(tmpDir, srcRoot);      // sets CP_GATE_STATE_DIR for this process and its children (a folder inside tmpDir) and starts watching the real log
//   ... at the end:  t('no line of this run reached the real gate.log', guard.leaks().length === 0, guard.leaks().join('\n'));
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { resolveMainRoot } from './hooks/root.mjs';

const OS_TMP = path.resolve(os.tmpdir());   // read at import time: a suite may point TEMP at its own folder later

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

// Removes a test's temp folder at the end of a suite (gates6 audit item 13: ~100 old gatetest-* / guards6-* folders were left in %TEMP% on a disk that is short). Links (junctions,
// symlinks) inside it are unlinked FIRST and never followed, so a fixture junction can never lead the delete into real data; refuses anything that is not a direct child of the OS temp
// folder. Returns true when the folder is gone. Set KEEP_HOOKTEST=1 to keep it for a post-mortem.
export function cleanupTmp(dir) {
  try {
    if (process.env.KEEP_HOOKTEST) return false;
    const d = path.resolve(dir);
    if (path.dirname(d).toLowerCase() !== OS_TMP.toLowerCase() || !/^[\w-]+$/.test(path.basename(d))) return false;
    const links = [];
    const walk = (x) => { let ents; try { ents = fs.readdirSync(x, { withFileTypes: true }); } catch { return; } for (const e of ents) { const f = path.join(x, e.name); if (e.isSymbolicLink()) links.push(f); else if (e.isDirectory()) walk(f); } };
    walk(d);
    for (const l of links) { try { fs.rmdirSync(l); } catch { try { fs.unlinkSync(l); } catch { /* left */ } } }
    fs.rmSync(d, { recursive: true, force: true, maxRetries: 3 });
    return !fs.existsSync(d);
  } catch { return false; }
}
