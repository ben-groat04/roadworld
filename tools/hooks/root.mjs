// Resolves the MAIN checkout for every hook. Imports only node built-ins (guard.mjs needs it even when everything else is broken).
// Why: CLAUDE_PROJECT_DIR is "the project root where the session started" (Claude Code hooks documentation, code.claude.com/docs/en/hooks: "stays put",
// still the main checkout after Claude enters a worktree or runs cd; the `cwd` field in the hook input follows Claude instead). The live gate.log of the
// main checkout agrees: hook events of subagents carry the main checkout as cwd. A hook file that a worktree carries (after a merge, or when a worktree's own
// settings are used) would otherwise take ITS location as the root, and records, the contract and the state folder would split per worktree. So the root is
// found from the hook file's location through git's own link files: `.git` a folder = main checkout; `.git` a file ("gitdir: <main>/.git/worktrees/<n>") =
// a worktree, whose `commondir` leads back to <main>/.git. State is written ONLY under the folder this returns.
// Override for tests and odd setups: env CP_MAIN_ROOT.
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

export function mainRootFrom(startDir) {
  let d = path.resolve(startDir);
  for (let i = 0; i < 12; i++) {
    const g = path.join(d, '.git');
    let st = null; try { st = fs.statSync(g); } catch { /* none here */ }
    if (st && st.isDirectory()) return d;
    if (st && st.isFile()) {
      try {
        const m = fs.readFileSync(g, 'utf8').match(/^gitdir:\s*(.+)$/m);
        if (m) {
          const gitdir = path.resolve(d, m[1].trim());
          let common = gitdir;
          try { common = path.resolve(gitdir, fs.readFileSync(path.join(gitdir, 'commondir'), 'utf8').trim()); } catch { /* no commondir: gitdir is the common dir */ }
          if (path.basename(common).toLowerCase() === '.git') return path.dirname(common);
        }
      } catch { /* fall through to the parent */ }
    }
    const up = path.dirname(d); if (up === d) break; d = up;
  }
  return null;
}

export function resolveMainRoot(hookDir = path.dirname(fileURLToPath(import.meta.url))) {
  if (process.env.CP_MAIN_ROOT) return path.resolve(process.env.CP_MAIN_ROOT);
  // the hook file's location first, then the folder the harness says the session started in, last the old two-levels-up rule
  return mainRootFrom(hookDir) || (process.env.CLAUDE_PROJECT_DIR && mainRootFrom(process.env.CLAUDE_PROJECT_DIR)) || path.resolve(hookDir, '..', '..');
}

// every registered worktree root of a main checkout, read from <main>/.git/worktrees/*/gitdir (no process is started)
export function worktreeRoots(main) {
  const out = [];
  const base = path.join(main, '.git', 'worktrees');
  let names = []; try { names = fs.readdirSync(base); } catch { return out; }
  for (const n of names) {
    try {
      const gd = fs.readFileSync(path.join(base, n, 'gitdir'), 'utf8').trim();   // <worktree>/.git
      out.push(path.dirname(gd));
    } catch { /* skip */ }
  }
  return out;
}
