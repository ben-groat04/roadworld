// PreToolUse hook (matcher Bash|PowerShell): keeps SUBAGENTS from committing in the MAIN checkout by accident (gates7 item 15; incident of 23 Oct 2026: a builder's cwd was reset to the main
// checkout, it ran `git add -A; git commit` there and committed 710 untracked raw asset files; another builder then merged that main).
//
// RULES (the main session, which has no agent_id on its events, is not limited):
//   G1  any subagent: `git add -A`, `git add --all`, `git add .` (or `*`, `:/`), `git commit -a / -am / --all` and `git stash -u / -a / --include-untracked / --all` are refused when the
//       EFFECTIVE directory is the main checkout. Effective directory = the event cwd, changed by `cd` / `Set-Location` / `pushd` earlier in the command and by `git -C <dir>` (also
//       --git-dir / --work-tree). A merger or docs clerk (a NO-CONTRACT launch) may still commit in main with explicit paths: `git add docs/QUEUE.md`, `git commit -m x docs/QUEUE.md`.
//   G2  a subagent whose launch carried a HANDIN_DIR OUTSIDE the main checkout (a builder in its own worktree) may not run a git write command in the main checkout at all
//       (add, commit, merge, reset, rebase, cherry-pick, push, pull, revert, am, stash, rm, mv, restore, checkout, switch). The launch is read from the agent's own transcript (its first
//       user message, or the BRIEF-FILE that names the HANDIN_DIR); when that cannot be read G2 does not apply (G1 still does).
// "Main checkout" = the folder root.mjs resolves, minus every registered worktree of it. Reads only the command text, the cwd and the agent's transcript.
// WHAT IT CANNOT SEE: a directory or path held in a variable, a command substitution or a script file (`git -C $wt add -A` is left alone: unknown is not main), a git alias, a tool other
// than Bash/PowerShell, a commit made by a program. FAIL OPEN (logged) on any internal error.
import path from 'node:path';
import { ROOT, log, readStdin, parseJson, handinDirOfAgent } from './lib.mjs';
import { worktreeRoots } from './root.mjs';
import { parseCommand, verbOf } from './shell_lex.mjs';

const norm = (p) => String(p).replace(/\\/g, '/').replace(/\/+/g, '/').replace(/\/$/, '').toLowerCase();
const winPath = (p) => (/^\/([a-zA-Z])(\/|$)/.test(p) ? p[1] + ':' + p.slice(2) : p);
const WRITE_SUBS = new Set(['add', 'commit', 'merge', 'reset', 'rebase', 'cherry-pick', 'push', 'pull', 'revert', 'am', 'stash', 'rm', 'mv', 'restore', 'checkout', 'switch']);
const SHELLS = new Set(['bash', 'sh', 'zsh', 'pwsh', 'powershell', 'cmd']);
const CD = new Set(['cd', 'chdir', 'set-location', 'sl', 'pushd', 'push-location']);

export function ctxOf(mainRoot) {
  const main = norm(mainRoot);
  const wts = worktreeRoots(mainRoot).map(norm);
  const inWt = (n) => wts.some((w) => n === w || n.startsWith(w + '/'));
  const underMain = (p) => { const n = norm(p); return n === main || n.startsWith(main + '/'); };
  return { main, wts, underMain, isMain: (p) => { const n = norm(p); return underMain(n) && !inWt(n); } };
}

// a resolved absolute directory, or null when the word holds a variable or substitution (unknown)
function resolveDir(word, from) {
  if (from === null) return null;
  const w = String(word);
  if (/\$|`|%[A-Za-z_]+%|^~/.test(w)) return null;
  const x = winPath(w.replace(/\\/g, '/'));
  return path.resolve(/^[A-Za-z]:|^\//.test(x) ? x : path.join(from, x));
}

// a short-option cluster scan: does one of `flags` appear before a letter that takes a value (git commit -am, -ma)?
function clusterHas(rest, flags, valueLetters, longFlags, longValue) {
  for (let k = 0; k < rest.length; k++) {
    const x = rest[k];
    if (x === '--') break;
    if (longFlags.includes(x)) return true;
    if (longValue.includes(x)) { k++; continue; }
    const m = /^-([a-zA-Z]+)$/.exec(x);
    if (!m) continue;
    const c = m[1];
    for (let i = 0; i < c.length; i++) {
      if (flags.includes(c[i])) return true;
      if (valueLetters.includes(c[i])) { if (i === c.length - 1) k++; break; }
    }
  }
  return false;
}

export function analyse(command, cwd, opts = {}) {
  const mainRoot = opts.mainRoot || ROOT;
  const ctx = opts.ctx || ctxOf(mainRoot);
  const depth = opts.depth || 0;
  const reasons = [];
  if (depth > 4) return reasons;
  const handin = opts.handinDir || null;
  const g2 = !!handin && !ctx.underMain(path.resolve(String(handin).replace(/\\/g, '/')));
  let cur = cwd ? path.resolve(winPath(String(cwd).replace(/\\/g, '/'))) : null;
  const stack = [];
  const { segments } = parseCommand(String(command || ''));
  for (const seg of segments) {
    if (!seg.words.length) continue;
    const { verb, args, index } = verbOf(seg.words);
    const av = args.map((a) => a.v);
    if (CD.has(verb)) {
      const tgt = av.filter((x) => !/^-/.test(x))[0];
      if (['pushd', 'push-location'].includes(verb)) stack.push(cur);
      if (tgt) cur = resolveDir(tgt, cur);
      continue;
    }
    if (['popd', 'pop-location'].includes(verb)) { cur = stack.length ? stack.pop() : null; continue; }
    if (SHELLS.has(verb) || verb === 'eval') {
      const joined = av.join(' ');
      const m = verb === 'eval' ? [null, joined] : joined.match(/(?:^|\s)(?:-[a-z]*c|-command|\/{1,2}[ck])\s+([\s\S]*)$/i);
      if (m && m[1]) reasons.push(...analyse(m[1], cur, { ...opts, ctx, depth: depth + 1 }));
      if (seg.here) reasons.push(...analyse(seg.here, cur, { ...opts, ctx, depth: depth + 1 }));
      continue;
    }
    if (verb !== 'git' || index < 0) continue;
    // global options: -C <dir> (cumulative), -c k=v, --git-dir, --work-tree
    let repo = cur; let i = 0; let viaGitDir = false;
    while (i < av.length && /^-/.test(av[i])) {
      const a = av[i];
      if (a === '-C' && av[i + 1] !== undefined) { repo = resolveDir(av[i + 1], repo); i += 2; continue; }
      if (a === '-c' || a === '--namespace' || a === '--exec-path') { i += 2; continue; }
      let m = /^--(?:git-dir|work-tree)(?:=(.*))?$/.exec(a);
      if (m) { const v = m[1] !== undefined ? m[1] : av[++i]; const d = resolveDir(v, repo); i++; if (d && ctx.underMain(d) && !/\/\.git\/worktrees\//.test(norm(d))) viaGitDir = true; else if (!d) repo = null; continue; }
      i++;
    }
    const sub = String(av[i] || '').toLowerCase();
    const rest = av.slice(i + 1);
    const inMain = viaGitDir || (repo !== null && ctx.isMain(repo));
    if (!inMain || !sub) continue;
    // G1: blanket staging in the main checkout, by any subagent
    if (sub === 'add') {
      const dd = rest.indexOf('--');
      const flags = (dd >= 0 ? rest.slice(0, dd) : rest).filter((x) => /^-/.test(x));
      const paths = [...(dd >= 0 ? rest.slice(0, dd).filter((x) => !/^-/.test(x)) : rest.filter((x) => !/^-/.test(x))), ...(dd >= 0 ? rest.slice(dd + 1) : [])];
      const all = flags.some((f) => f === '--all' || /^-[a-zA-Z]*A[a-zA-Z]*$/.test(f));
      const blanket = paths.some((x) => /^(?:\.\/?|\*|:\/|:\(top\)|:\(top,[^)]*\))$/.test(x) || /^\.[\\/]\*$/.test(x));
      if (blanket || (all && !paths.length)) reasons.push('`git add -A` / `--all` / `.` in the MAIN checkout stages every untracked file there (23 Oct 2026: 710 raw asset files were committed this way). Name the files: `git add docs/QUEUE.md`, or work in your own worktree with `git -C <worktree> ...`');
    }
    if (sub === 'commit' && clusterHas(rest, 'a', 'mFCct', ['--all'], ['--message', '--file', '--author', '--date', '--template', '--reuse-message', '--reedit-message', '--fixup', '--squash', '--cleanup', '--gpg-sign', '--trailer'])) reasons.push('`git commit -a` / `-am` / `--all` in the MAIN checkout commits whatever is modified there. Name the paths (`git commit -m "..." docs/QUEUE.md`) or work in your own worktree with `git -C <worktree> ...`');
    if (sub === 'stash' && clusterHas(rest, 'ua', 'm', ['--include-untracked', '--all'], ['--message'])) reasons.push('`git stash -u` / `-a` in the MAIN checkout sweeps up every untracked file there (the raw asset folders among them)');
    // G2: a builder with its own worktree does not write to the main checkout at all
    if (g2 && WRITE_SUBS.has(sub)) reasons.push(`your launch carried HANDIN_DIR ${handin} (outside the main checkout), so you are a builder in your own worktree; \`git ${sub}\` in the MAIN checkout is refused (your cwd was probably reset there). Run it as \`git -C <your worktree> ${sub} ...\``);
  }
  return [...new Set(reasons)];
}

export async function main() {
  const raw = await readStdin();
  const input = raw === null ? undefined : parseJson(raw);
  if (!input || typeof input !== 'object') { log({ gate: 'git_guard', decision: 'allow', reason: 'FAIL-OPEN: stdin missing or not JSON' }); return 0; }
  if (!/^(Bash|PowerShell)$/.test(input.tool_name || '')) return 0;
  if (!input.agent_id) return 0;     // the main session
  const command = (input.tool_input && input.tool_input.command) || '';
  if (!/\bgit\b/i.test(command)) return 0;
  // G1 needs no transcript. G2 does: it is read only when the command WOULD be refused for a builder (probe with a made-up HANDIN_DIR outside main), so ordinary git calls cost nothing
  const cwd = input.cwd || process.cwd();
  let reasons = analyse(command, cwd, {});
  if (!reasons.length && analyse(command, cwd, { handinDir: 'Z:/__probe_outside_main__/x' }).length) {
    const handin = handinDirOfAgent(input);
    if (handin) reasons = analyse(command, cwd, { handinDir: handin });
  }
  if (!reasons.length) return 0;
  log({ gate: 'git_guard', decision: 'deny', reason: 'git write in the main checkout refused', agent_id: input.agent_id, cwd: input.cwd, command: String(command).slice(0, 300), reasons });
  process.stderr.write('REFUSED by the git guard (tools/hooks/git_guard.mjs):\n - ' + reasons.join('\n - ') + '\n');
  return 2;
}
