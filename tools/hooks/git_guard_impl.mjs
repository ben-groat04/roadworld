// PreToolUse hook (matcher Bash|PowerShell): keeps SUBAGENTS from committing in the MAIN checkout by accident (gates7 item 15; incident of 23 Oct 2026: a builder's cwd was reset to the main
// checkout, it ran `git add -A; git commit` there and committed 710 untracked raw asset files; another builder then merged that main).
//
// RULES (the main session, which has no agent_id on its events, is not limited):
//   G1  any subagent: `git add -A`, `git add --all`, `git add .` (or `*`, `:/`), `git commit -a / -am / --all` and `git stash -u / -a / --include-untracked / --all` are refused when the
//       EFFECTIVE directory is the main checkout. Effective directory = the event cwd, changed by `cd` / `Set-Location` / `pushd` earlier in the command and by `git -C <dir>` (also
//       --git-dir / --work-tree). A merger or docs clerk (a NO-CONTRACT launch) may still commit in main with explicit paths: `git add docs/QUEUE.md`, `git commit -m x docs/QUEUE.md`.
//   G2  a subagent whose launch carried a HANDIN_DIR OUTSIDE the main checkout (a builder in its own worktree) may not run a git write command in the main checkout at all
//       (add, commit, merge, reset, rebase, cherry-pick, push, pull, revert, am, stash, rm, mv, restore, checkout, switch, branch -D/-m/<name>, tag <name>/-d, gc, worktree add|remove|prune|move, config <k> <v>|--unset, update-ref, apply, clean; `fetch` and the read forms stay allowed: see isWriteSub). The launch is read from the agent's own transcript (its first
//       user message, or the BRIEF-FILE that names the HANDIN_DIR); when that cannot be read G2 does not apply (G1 still does).
// "Main checkout" = the folder root.mjs resolves, minus every registered worktree of it. Reads only the command text, the cwd and the agent's transcript.
// WHAT IT CANNOT SEE: a directory or path held in a variable, a command substitution or a script file (`git -C $wt add -A` is left alone: unknown is not main), a git alias, a tool other
// than Bash/PowerShell, a commit made by a program. FAIL OPEN (logged) on any internal error.
import path from 'node:path';
import { ROOT, log, readStdin, parseJson, handinDirOfAgent } from './lib.mjs';
import { worktreeRoots } from './root.mjs';
import { parseCommand, verbOf, shellInner } from './shell_lex.mjs';

const norm = (p) => String(p).replace(/\\/g, '/').replace(/\/+/g, '/').replace(/\/$/, '').toLowerCase();
const winPath = (p) => (/^\/([a-zA-Z])(\/|$)/.test(p) ? p[1] + ':' + p.slice(2) : p);
// G2 write list (gates8: the exact list; `fetch`, `status`, `log`, `diff`, `show`, `worktree list`, `stash list|show`, `branch` listings, `tag -l`, `config --get|--list`, `apply --check|--stat` and `clean -n` stay allowed)
export const WRITE_SUBS = new Set(['add', 'commit', 'merge', 'reset', 'rebase', 'cherry-pick', 'push', 'pull', 'revert', 'am', 'stash', 'rm', 'mv', 'restore', 'checkout', 'switch', 'branch', 'tag', 'gc', 'worktree', 'config', 'update-ref', 'apply', 'clean']);
const hasFlag = (rest, re) => rest.some((x) => x !== '--' && re.test(x));
const nonFlags = (rest) => { const dd = rest.indexOf('--'); return [...(dd >= 0 ? rest.slice(0, dd) : rest).filter((x) => !/^-/.test(x)), ...(dd >= 0 ? rest.slice(dd + 1) : [])]; };
// does `git <sub> <rest>` WRITE (a ref, the config, the index, the working tree, the object store)? Read forms of the new subcommands are allowed (gates7 audit F1 and item 10).
export function isWriteSub(sub, rest) {
  if (!WRITE_SUBS.has(sub)) return false;
  const nf = nonFlags(rest);
  switch (sub) {
    case 'stash': return !['list', 'show'].includes(nf[0]);
    case 'branch': {
      if (hasFlag(rest, /^(?:-[a-zA-Z]*[dDmMcCfu][a-zA-Z]*|--delete|--move|--copy|--force|--set-upstream-to(?:=.*)?|--unset-upstream|--edit-description|--track|--no-track)$/)) return true;
      if (hasFlag(rest, /^(?:-l|--list|--show-current|-a|--all|-r|--remotes|-v|-vv|--verbose|--contains|--no-contains|--merged|--no-merged|--points-at)$/)) return false;
      return nf.length > 0;
    }
    case 'tag': {
      if (hasFlag(rest, /^(?:-[a-zA-Z]*[dsafu][a-zA-Z]*|--delete|--sign|--annotate|--force)$/)) return true;
      return nf.length > 0 && !hasFlag(rest, /^(?:-l|--list|-n|-v|--verify|--contains|--no-contains|--merged|--no-merged|--points-at)$/);
    }
    case 'worktree': return /^(?:add|remove|prune|move|lock|unlock|repair)$/.test(nf[0] || '');
    case 'config': {
      if (hasFlag(rest, /^(?:--unset|--unset-all|--add|--replace-all|--edit|-e|--rename-section|--remove-section)$/)) return true;
      if (hasFlag(rest, /^(?:--get|--get-all|--get-regexp|--get-urlmatch|-l|--list|--show-origin|--show-scope|--name-only)$/)) return false;
      return nf.length >= 2;
    }
    case 'apply': return !hasFlag(rest, /^(?:--check|--stat|--numstat|--summary)$/);
    case 'clean': return !hasFlag(rest, /^(?:--dry-run|-[a-zA-Z]*n[a-zA-Z]*)$/);
    default: return true;
  }
}
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
  // gates8: literal GIT_DIR / GIT_WORK_TREE set earlier in the command (`$env:GIT_DIR='..'; git add -A`, `export GIT_WORK_TREE=..; git add -A`, `set GIT_DIR=.. && git ...`) or as a prefix (`GIT_DIR=.. git add -A`)
  const penv = {};
  const takeAssign = (word, into) => { const m = /^(?:\$env:)?([A-Za-z_]\w*)=([\s\S]*)$/.exec(word); if (m) { into[m[1].toLowerCase()] = m[2]; return true; } return false; };
  for (const seg of segments) {
    const words = seg.words.slice(); const local = {};
    for (const [k, v] of Object.entries(seg.assigns || {})) local[k.replace(/^env:/, '')] = v;
    while (words.length && /^(?:\$env:)?[A-Za-z_]\w*=/.test(words[0].v)) takeAssign(words.shift().v, local);
    if (!words.length) { Object.assign(penv, local); continue; }
    const { verb, args, index, line } = verbOf(words);
    const av = args.map((a) => a.v);
    if (verb === 'export' || verb === 'set' || verb === 'setx') { for (const a of av) takeAssign(a, penv); continue; }
    if (line) { reasons.push(...analyse(words.slice(index).map((w) => w.v).join(' '), cur, { ...opts, ctx, depth: depth + 1 })); continue; }   // iex "git add -A"
    if (CD.has(verb)) {
      const tgt = av.filter((x) => !/^-/.test(x) && !/^\/d$/i.test(x))[0];   // gates8: cmd's `cd /d <dir>`: /d is a switch, not the target
      if (['pushd', 'push-location'].includes(verb)) stack.push(cur);
      if (tgt) cur = resolveDir(tgt, cur);
      continue;
    }
    if (['popd', 'pop-location'].includes(verb)) { cur = stack.length ? stack.pop() : null; continue; }
    if (SHELLS.has(verb) || verb === 'eval') {
      const inner = verb === 'eval' ? av.join(' ') : shellInner(verb, av);   // gates8 hole 4: powershell "git add -A" without -Command
      if (inner) reasons.push(...analyse(inner, cur, { ...opts, ctx, depth: depth + 1 }));
      if (seg.here) reasons.push(...analyse(seg.here, cur, { ...opts, ctx, depth: depth + 1 }));
      continue;
    }
    if (verb !== 'git' || index < 0) continue;
    const env = { ...penv, ...local };
    // global options: -C <dir> (cumulative), -c k=v, --git-dir, --work-tree
    let repo = cur; let i = 0; let viaGitDir = false;
    if (typeof env.git_work_tree === 'string') repo = resolveDir(env.git_work_tree, repo);
    if (typeof env.git_dir === 'string') { const gd = resolveDir(env.git_dir, cur); if (gd && ctx.underMain(gd) && !/\/\.git\/worktrees\//.test(norm(gd))) viaGitDir = true; else repo = null; }
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
      const blanket = paths.some((x) => /^(?:\.(?:[\\/]+\.?)*[\\/]*|\*+|:\/|:\(top\)|:\(top,[^)]*\))$/.test(x) || /^\.[\\/]+\*$/.test(x));   // gates8: `.`, `./`, `.\`, `./.`, `.\*`, `**`
      const dry = flags.some((f) => f === '--dry-run' || /^-[a-zA-Z]*n[a-zA-Z]*$/.test(f));            // gates8 (gates7 audit F5): a dry run stages nothing
      const upd = flags.some((f) => f === '--update' || /^-[a-zA-Z]*u[a-zA-Z]*$/.test(f));             // `-u .` stages tracked files only (untracked raw assets are untouched)
      if (!dry && (all || !upd) && (blanket || (all && !paths.length))) reasons.push('`git add -A` / `--all` / `.` in the MAIN checkout stages every untracked file there (23 Oct 2026: 710 raw asset files were committed this way). Name the files: `git add docs/QUEUE.md`, or work in your own worktree with `git -C <worktree> ...` (`git add -u .` and `git add -n .` are fine)');
    }
    if (sub === 'commit' && clusterHas(rest, 'a', 'mFCct', ['--all'], ['--message', '--file', '--author', '--date', '--template', '--reuse-message', '--reedit-message', '--fixup', '--squash', '--cleanup', '--gpg-sign', '--trailer'])) reasons.push('`git commit -a` / `-am` / `--all` in the MAIN checkout commits whatever is modified there. Name the paths (`git commit -m "..." docs/QUEUE.md`) or work in your own worktree with `git -C <worktree> ...`');
    if (sub === 'stash' && clusterHas(rest, 'ua', 'm', ['--include-untracked', '--all'], ['--message'])) reasons.push('`git stash -u` / `-a` in the MAIN checkout sweeps up every untracked file there (the raw asset folders among them)');
    // G2: a builder with its own worktree does not write to the main checkout at all
    if (g2 && isWriteSub(sub, rest)) reasons.push(`your launch carried HANDIN_DIR ${handin} (outside the main checkout), so you are a builder in your own worktree; \`git ${sub}\` in the MAIN checkout is refused (your cwd was probably reset there). Run it as \`git -C <your worktree> ${sub} ...\``);
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
