// PreToolUse hook (matcher Bash|PowerShell): a best-effort NET against deletes, moves and truncations that would wipe the private reference photos. It is NOT the protection:
// the protection is structural (main session decision, 23 Oct 2026): worktrees no longer get a docs\reference junction (tools\new_agent_worktree.ps1 creates none;
// tools\unlink_reference_junctions.ps1 removes the old ones), and tools\backup_reference.mjs keeps a validated mirror in C:\Users\bgbg0\roadworld-reference-backup.
// Why a net at all: on 22 Oct 2026 a recursive delete of a worktree followed that junction and wiped every reference photo. Applies to the main session and to every subagent.
//
// RULES. A command is refused when it does a recursive delete (rm -r/-rf/-R/--recursive, Remove-Item/ri with -Recurse, rmdir/rd /s (also //s), find -delete or -exec rm,
// xargs rm -r, a pipeline into Remove-Item, rmtree / fs.rmSync recursive / rimraf / Directory.Delete(.., true) one-liners near a tree) and a resolved target is
//   R1  docs\reference anywhere (the main checkout's photos), or inside it (EXCEPT strictly below docs\reference\_compare\<topic>: your own composites may be removed);
//   R2  the MAIN checkout, its docs folder, or any folder above it (main comes from root.mjs: git's worktree link files);
//   R3  a worktree root (conventional roadworld-wt\<n> and roadworld-wt-<n>, plus every worktree git lists), the roadworld-wt folder, or a folder above one;
//   R4  the backup folder C:\Users\bgbg0\roadworld-reference-backup, inside it, or above it;
//   R5  a folder below a worktree that, on disk, still holds a junction or symlink (walked WITHOUT following links, time-capped: on a cap hit the command is REFUSED
//       with the numbers in the message, never passed silently), or whose wildcard matches one;
// or a target that cannot be resolved (unknown variable, command substitution, a pipeline whose source is not a listing) while the working directory is in the main
// checkout, in a tree with a docs\reference link, or the command text names docs\reference or the main checkout. A name-filtered `find ... -delete` is judged by the
// links it would walk (R5) and by whether its filter can match a picture, not by the start folder alone.
// Also refused (gates6): non-recursive MASS deletes inside docs\reference (a wildcard or a loop: rm -f docs/reference/aerial/*.jpg, del docs\reference\aerial\*, a for/while loop
// over the folder, a listing piped into Remove-Item or .Delete()); mv/Move-Item/Rename-Item whose SOURCE is the main checkout, its docs, docs\reference, a subfolder of
// it (not _compare), a worktree root or the backup (a single named photo, or files moved into docs\reference\_damaged, stay allowed); cp/mv/Copy-Item onto an existing picture
// in docs\reference, a truncating redirect (>) or truncate/Set-Content/Clear-Content/Out-File on an existing picture there; rmdir of main's docs\reference; `git clean` with
// -x, -X or -d (not -n) whose path arguments (or, with none, the repository) are in main or a worktree; `del /s` in a worktree; ANY `git worktree remove|prune` (use
// tools\remove_agent_worktree.ps1, which is a script file and so not seen here); robocopy /MIR or /PURGE whose DESTINATION is R1-R4 or inside a worktree; robocopy /MOV or /MOVE
// from main or docs\reference; rsync --delete into those; a /MIR into the backup from anything but main's docs\reference. Robocopy main docs\reference -> backup /MIR is the
// prescribed backup and is allowed.
// Resolved as far as is cheap: quotes and "" concatenation, comments (# ...), \ prefix and wrapper verbs (timeout N, busybox, env, sudo, nice, command ...), cd/pushd/Push-Location chains,
// ~ and $HOME, every environment variable (bash $TEMP/${TMP} and PowerShell $env:TEMP, %TEMP%), ${X:-default}, $PWD/${PWD}/$(pwd)/$PSScriptRoot (the working directory), $(mktemp ...) (a
// temp path), $x = "literal" and Join-Path assignments, for/foreach lists, comma lists, brace {a,b} and [class] and * ? wildcards (expanded against the disk), trailing dots and
// spaces of Windows names, \\?\ prefixes, \ as an escape (second reading of a mixed-slash word), if/then/else/fi/do/done and PowerShell blocks, and nested commands in bash -c, eval,
// echo ... | bash, bash <<< "...", heredocs to a shell, cmd /c and //c, powershell -Command / -EncodedCommand, Invoke-Expression/iex, Start-Process -ArgumentList, python/node/perl/ruby -c/-e strings.
// gates7 (23 Oct 2026, gates6 audit items 1-3, 6-8, 10): a one-liner (python -c rmtree / node -e rmSync / Directory.Delete) whose FIRST argument is a plain string literal is judged like a delete target (build is
// fine in a clean worktree; docs, main, a link tree are not; a non-literal path keeps the refusal); `del -Recurse -Force x` is Remove-Item, not cmd del /s; a name-filtered listing piped into Remove-Item
// (gci -Filter build* | Remove-Item) is judged by the links it would walk, like find -name; PowerShell blocks with glued braces (try {Remove-Item ..}) are split by the lexer; xargs rm / truncate / shred and
// find -exec truncate|shred|dd over a listing that reaches the photos (docs/reference, main, main/docs, with a name filter that can match a picture) are mass deletes; abbreviated PowerShell parameters
// (-Rec -Pa -Fo -Lit -Fi -Inc -Exc) are read; git clean skips the value of -e / --exclude; an unknown verb (unbuffer, watch, parallel, flock, taskset, strace, script -c, any <cmd> <args> -- <verb>) that
// carries an unquoted delete or move verb as one of its words has the rest judged as a command (text verbs like echo, grep, curl, npm are left alone).
// WHAT IT CANNOT SEE (accepted: structural protection + validated backup, main session decision 23 Oct 2026): a delete done inside a script file the command only runs (node x.mjs, a
// .ps1 or .bat), a path assembled from variables over several steps or read from a file, command substitution RESULTS, aliases and functions defined elsewhere, loops whose list or body
// is computed (while read ... < file with a list in another file), variable indirection (${!x}, eval of a built string), brace expansion with nesting, 8.3 short names, xcopy /E /Y and cp
// --remove-destination over files, a delete started by a tool other than Bash/PowerShell, and the other ways a program can walk a directory. It reads the command text and the disk, nothing
// else. FAIL OPEN (logged) on any internal error.
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { ROOT, log, readStdin, parseJson } from './lib.mjs';
import { worktreeRoots } from './root.mjs';
import { parseCommand, verbOf } from './shell_lex.mjs';

export const SCRIPT_HINT = 'Remove a worktree only with: powershell -NoProfile -ExecutionPolicy Bypass -File tools\\remove_agent_worktree.ps1 <name> (it unlinks any docs\\reference junction first and checks the photos are still there). Older worktrees still hold a junction (docs\\reference) to the private reference photos: a recursive delete follows it and wipes them (22 Oct 2026). Deleting build\\, node_modules, a temp folder or your own composites under docs\\reference\\_compare\\<topic> is allowed.';

export const BACKUP_DIRS = [...new Set(['C:/Users/bgbg0/roadworld-reference-backup', path.join(os.homedir(), 'roadworld-reference-backup')].map((p) => p.replace(/\\/g, '/').toLowerCase()))];

const norm = (p) => String(p).replace(/\\/g, '/').replace(/\/+/g, '/').toLowerCase();
const strip = (p) => norm(p).replace(/\/+$/, '');
// bash-on-Windows paths: /c/Users/x -> c:/users/x ; \\?\C:\x -> C:\x
const winPath = (p0) => {
  let p = String(p0).replace(/^(?:\\\\\?\\|\/\/\?\/)/, '');
  p = /^\/{2,}[a-zA-Z]\//.test(p) ? p.replace(/^\/+/, '/') : p;
  return /^\/([a-zA-Z])(\/|$)/.test(p) ? p[1] + ':' + p.slice(2) : p;
};
// Windows ignores trailing dots and spaces of a name: docs\reference. is docs\reference
const fixDots = (p) => { const r = path.parse(p).root; return r + p.slice(r.length).split(/[\\/]/).map((s) => (s === '.' || s === '..' ? s : s.replace(/[. ]+$/, ''))).join(path.sep); };

// every reparse point (junction / symlink) at or below `p`, found without descending into links, only directory entries are read (a Dirent says symlink for a junction too).
// Returns an array with .capped (true when the time cap ended the walk) and .seen. [] when p does not exist.
export function linksBelow(p, { ms = 5000, limit = 5000000 } = {}) {
  const found = []; found.seen = 0; found.capped = false;
  const t0 = Date.now();
  const walk = (d) => {
    if (found.capped) return;
    let ents; try { ents = fs.readdirSync(d, { withFileTypes: true }); } catch { return; }
    for (const e of ents) {
      if ((++found.seen & 1023) === 0 && Date.now() - t0 > ms) { found.capped = true; return; }
      if (found.seen > limit) { found.capped = true; return; }
      const f = d + path.sep + e.name;
      if (e.isSymbolicLink()) { found.push(f); continue; }
      if (e.isDirectory()) walk(f);
      if (found.capped) return;
    }
  };
  let st; try { st = fs.lstatSync(p); } catch { return found; }
  if (st.isSymbolicLink()) { found.push(p); return found; }
  if (st.isDirectory()) walk(p);
  return found;
}

function braceExpand(s) {
  const m = String(s).match(/\{([^{}]*,[^{}]*)\}/);
  if (!m) return [s];
  return m[1].split(',').flatMap((x) => braceExpand(s.replace(m[0], x))).slice(0, 64);
}
const hasGlob = (s) => /[*?]|\[[^\]]+\]/.test(s);
function segRe(pat) {
  let re = '^';
  for (let i = 0; i < pat.length; i++) {
    const c = pat[i];
    if (c === '*') re += '.*'; else if (c === '?') re += '.';
    else if (c === '[') { const j = pat.indexOf(']', i + 1); if (j < 0) re += '\\['; else { let cls = pat.slice(i + 1, j).replace(/^!/, '^').replace(/\\/g, '\\\\'); re += '[' + cls + ']'; i = j; } }
    else re += c.replace(/[.+^${}()|\\]/g, '\\$&');
  }
  return new RegExp(re + '$', 'i');
}
// expand * ? [class] in any segment of an absolute path against the disk (case-insensitive); returns [] when nothing matches
export function expandGlob(abs, limit = 400) {
  if (!hasGlob(abs)) return [abs];
  const root = path.parse(abs).root;
  const segs = abs.slice(root.length).split(/[\\/]/).filter(Boolean);
  let cur = [root];
  for (const s of segs) {
    const next = [];
    for (const base of cur) {
      if (!hasGlob(s)) { next.push(path.join(base, s)); continue; }
      let names = []; try { names = fs.readdirSync(base); } catch { /* none */ }
      const re = segRe(s);
      for (const n of names) if (re.test(n)) next.push(path.join(base, n));
    }
    cur = next.slice(0, limit);
    if (!cur.length) break;
  }
  return cur;
}

const WT_NAME = /(^|\/)roadworld-wt(-[^/]+)?(\/|$)/;
const PIC_EXT = /\.(jpe?g|png|gif|webp|bmp|tiff?)$/i;
export function context(mainRoot) {
  const main = strip(mainRoot);
  const wts = worktreeRoots(mainRoot).map(strip);
  const underWt = (p) => { const n = strip(p); return WT_NAME.test(n + '/') || wts.some((w) => n === w || n.startsWith(w + '/')); };
  const underMain = (p) => { const n = strip(p); return n === main || n.startsWith(main + '/'); };
  const isWtRoot = (p) => { const n = strip(p); return /(^|\/)roadworld-wt$/.test(n) || /(^|\/)roadworld-wt\/[^/]+$/.test(n) || /(^|\/)roadworld-wt-[^/]+$/.test(n) || wts.includes(n); };
  const above = (n, of) => !!n && of.some((o) => o === n || o.startsWith(n + '/'));
  const guarded = [main, ...BACKUP_DIRS, ...wts];
  // docs\reference (not _compare\<topic>...): the photos
  const inRef = (p) => /(^|\/)docs\/reference(\/|$)/.test(strip(p)) && !/(^|\/)docs\/reference\/_compare\/[^/]+/.test(strip(p));
  const refRoot = main + '/docs/reference';
  return {
    main, wts, underWt, underMain, inRef, refRoot, isWtRoot,
    near: (p) => underWt(p) || underMain(p),
    // returns a refusal reason or null for one RESOLVED absolute path. opts.filtered: a name-filtered find (judged by links and R1, not by the start folder)
    classify(abs, opts = {}) {
      const n = strip(abs);
      if (/(^|\/)docs\/reference(\/|$)/.test(n)) {
        if (/(^|\/)docs\/reference\/_compare\/[^/]+/.test(n)) return null;   // own composites
        return 'is or lies inside docs\\reference (the private reference photos)';
      }
      if (n === main || n === main + '/docs') { if (opts.filtered && n === main && !opts.picFilter) return null; return 'is the main checkout root or its docs folder (holds the private reference photos)'; }
      if (above(n, guarded) && !underMain(n) && !underWt(n)) return 'is or contains the main checkout, a worktree or the reference backup';
      if (BACKUP_DIRS.some((b) => n === b || n.startsWith(b + '/'))) return 'is the reference backup folder or inside it';
      if (isWtRoot(n) && !opts.filtered) return 'is a worktree root or the roadworld-wt folder';
      if (underWt(n)) {
        const links = linksBelow(abs);
        if (links.length) return `holds ${links.length} junction/symlink(s) (${links.slice(0, 2).join(', ')}): a recursive delete follows them into their targets`;
        if (links.capped) return `is too large to check for junctions in time (${links.seen} entries read before the time cap): refused because it could not be shown safe; delete a smaller folder, or run the delete from a plain shell yourself`;
      }
      return null;
    },
  };
}

// a value substitution for one word. Returns { values: [abs paths], unknown: bool, raw }
const MKTEMP = /\$\(\s*mktemp\b[^)]*\)/g;
function resolveWord(raw, cwd, vars) {
  let t = String(raw);
  const home = process.env.USERPROFILE || process.env.HOME || '~';
  t = t.replace(/^~(?=\/|\\|$)/, home);
  t = t.replace(MKTEMP, () => path.join(os.tmpdir(), 'mktemp-x'));
  t = t.replace(/\$\(\s*pwd\s*\)|\$\(\$PWD\.Path\)|\$\(\s*\(Get-Location\)\.Path\s*\)|\$\(Get-Location\)/gi, () => cwd);
  let unknown = t === '{}' || t === '@' || t === '$' || /\$\(|`/.test(t);
  const envVal = (name) => { const v = process.env[name] ?? process.env[name.toUpperCase()]; return v; };
  const special = (name) => (/^(pwd|psscriptroot|psscriptroot|psscript)$/i.test(name) ? cwd : undefined);
  const sub = (word, depth = 0) => {
    if (depth > 6) return [word];
    let m = word.match(/\$\{([A-Za-z_]\w*)(?::?[-=])([^}]*)\}/);
    if (m) {
      const nm = m[1].toLowerCase();
      let val = vars[nm]; if (val === undefined) val = special(nm); if (val === undefined || val === null || val === '') val = envVal(m[1]);
      if (val === undefined || val === '') val = m[2];
      return (Array.isArray(val) ? val : [val]).flatMap((x) => sub(word.replace(m[0], x), depth + 1));
    }
    m = word.match(/\$\{?(env:)?([A-Za-z_]\w*)\}?(?:\.(?:path|fullname))?|%([A-Za-z_]\w*)%/i);
    if (!m) return [word];
    const nm = (m[2] || m[3]);
    const name = nm.toLowerCase();
    let val = Object.prototype.hasOwnProperty.call(vars, name) ? vars[name] : undefined;
    if (val === undefined) val = special(name);
    if (val === undefined && name !== 'pwd') val = envVal(nm);
    if (val === undefined && (name === 'home' || name === 'userprofile')) val = home;
    if (val === undefined || val === null) { unknown = true; return []; }
    return (Array.isArray(val) ? val : [val]).flatMap((x) => { const r = word.replace(m[0], x); if (/\$\(|`/.test(String(x))) unknown = true; return sub(r, depth + 1); });
  };
  const values = sub(t).flatMap(braceExpand);
  const toAbs = (v) => { const w = winPath(v); return fixDots(path.resolve(/^[a-zA-Z]:|^\//.test(w) ? w : path.join(cwd, w))); };
  const abs = values.map(toAbs);
  // a word that mixes / and \ may use \ as an escape (docs/ref\erence): read it a second way as well
  for (const v of values) if (v.includes('/') && /\\[A-Za-z]/.test(v)) abs.push(toAbs(v.replace(/\\([A-Za-z])/g, '$1')));
  return { values: abs, unknown, raw: t };
}

const DEL_VERBS = new Set(['rm', 'rmdir', 'rd', 'del', 'erase', 'remove-item', 'ri', 'find', 'unlink']);
const MOVE_VERBS = new Set(['mv', 'move', 'move-item', 'mi', 'rename-item', 'rni', 'ren']);
const COPY_VERBS = new Set(['cp', 'copy', 'copy-item', 'cpi']);
const TRUNC_VERBS = new Set(['truncate', 'set-content', 'sc', 'clear-content', 'clc', 'out-file']);
const LIST_VERBS = new Set(['get-childitem', 'gci', 'ls', 'dir', 'find', 'get-item', 'gi', 'tree']);
const SHELL_VERBS = new Set(['bash', 'sh', 'zsh', 'pwsh', 'powershell', 'cmd']);
const isFlag = (x) => /^-/.test(x) || /^\/{1,2}[a-zA-Z](:\S*)?$/.test(x);
// an abbreviated PowerShell parameter (gates7 item 8): -Pa, -Lit, -Fi, -Inc, -Exc ... with a minimum length that keeps bash flags (rm -i) out
const psp = (a, full, min) => { const m = /^-([a-z]+)(?::.*)?$/i.exec(String(a)); return !!m && m[1].length >= min && full.startsWith(m[1].toLowerCase()); };
const psPathFlag = (a) => psp(a, 'path', 2) || psp(a, 'literalpath', 2);
const psNameFilterFlag = (a) => psp(a, 'filter', 2) || psp(a, 'include', 3);
const psRecurse = (f) => /^-r(?:e(?:c(?:u(?:r(?:s(?:e)?)?)?)?)?)?(?::\$?true)?$/i.test(f);
const globRe = (g) => segRe(g);
const nameMayMatchPic = (pats) => !pats.length || pats.some((p) => ['a.jpg', 'a.png', 'a.jpeg', 'a.webp', 'a.gif'].some((n) => globRe(p).test(n)));
const DELETEISH = /remove-item|\bri\b|\.delete\s*\(|\[io\.file\]::delete|\bunlink\b|(^|[\s;|&])(?:rm|del|erase|truncate|shred)(?:\s|$)/i;   // gates7 item 7: "| xargs rm" at the end of the line, truncate, shred

// returns [] or an array of refusal reasons
export function analyse(command, cwd = process.cwd(), depth = 0, ctx = null, mainRoot = ROOT) {
  const reasons = [];
  const cmd = String(command || '');
  if (depth > 5) return reasons;
  ctx = ctx || context(mainRoot);
  const ncmd = norm(cmd);
  const mentions = () => /roadworld/.test(ncmd) || ncmd.includes(ctx.main) || ctx.wts.some((w) => ncmd.includes(w)) || /docs[\\/]+reference/i.test(cmd);
  const nearText = () => ctx.near(cwd) || mentions();
  const sub = (c, at) => analyse(c, at, depth + 1, ctx, mainRoot);
  // a link at <ancestor>\docs\reference of the cwd: this tree still holds the junction
  const holdsLinkAt = (d) => { let x = path.resolve(d); for (let i = 0; i < 10; i++) { try { if (fs.lstatSync(path.join(x, 'docs', 'reference')).isSymbolicLink()) return true; } catch { /* none */ } const up = path.dirname(x); if (up === x) break; x = up; } return false; };
  const risky = (at) => ctx.underMain(at) || holdsLinkAt(at) || mentions();

  // PowerShell: Remove-Item with -Recurse and a parenthesised (computed) path, in either order: the parentheses split the statement, so it is read as text
  if (/\b(?:remove-item|ri)\b[^;|\n]*\([^;\n]*\)[^;\n]*\s-r(?:e(?:c(?:u(?:r(?:s(?:e)?)?)?)?)?)?\b|\b(?:remove-item|ri)\b[^;\n]*\s-r(?:e(?:c(?:u(?:r(?:s(?:e)?)?)?)?)?)?\b[^;\n]*\(/i.test(cmd) && risky(cwd)) reasons.push('Remove-Item -Recurse with a computed path in parentheses (Join-Path, Resolve-Path, Get-Item ...): its target cannot be read, in the main checkout, a tree with a docs\\reference link, or a command naming the photos');
  // recursive deletes written as a one-liner in another language near a tree (gates7 item 1: a call whose FIRST argument is a string literal is judged like a delete target below,
  // so python -c "shutil.rmtree('build')" is allowed in a clean worktree; a call with no literal first argument keeps the refusal)
  const ONE_ALL = /shutil\.rmtree|\brmtree\s*\(|\brm(?:dir)?(?:Sync)?\s*\([^)]*recursive\s*:\s*true|\brimraf\b|Directory\]::Delete\s*\(.*,\s*\$?true\s*\)|Directory\.Delete\s*\(.*,\s*\$?true\s*\)|\.Delete\s*\(\s*\$?true\s*\)|FileUtils\.rm_rf|File\.unlink|rm_rf\(/gi;
  const ONE_LIT = /(shutil\.rmtree|\brmtree|\brm(?:dir)?Sync|\brimraf(?:Sync)?|Directory\]?::?\.?Delete|FileUtils\.rm_rf|\brm_rf)\s*\(\s*\\*(["'])([^"'\n]*?)\\*\2([^)\n]*)/gi;
  const oneLits = [];
  for (const m of cmd.matchAll(ONE_LIT)) {
    const fn = m[1].toLowerCase(); const rest = m[4] || '';
    const rec = /rmtree|rimraf|rm_rf/.test(fn) || (/rm(?:dir)?sync/.test(fn) && /recursive\s*:\s*true/i.test(rest)) || (/delete/.test(fn) && /,\s*\$?true/i.test(rest));
    if (rec && /^\s*(?:,|$)/.test(rest)) oneLits.push(m[3]);   // 'build' + x, 'build'.join(..), f"build": not a plain literal, so the call stays unresolved
  }
  if ((cmd.match(ONE_ALL) || []).length > oneLits.length && nearText()) reasons.push('a recursive delete written as a script one-liner (rmtree / fs.rmSync recursive / rimraf / Directory.Delete(.., true)) with a path that is not a string literal, in or near the main checkout or a worktree: it follows a docs\\reference junction');
  // heredocs and here-strings handed to a shell run as commands
  for (const m of cmd.replace(/\r\n/g, '\n').matchAll(/([^\n]*?)<<-?[ \t]*(['"]?)([A-Za-z_]\w*)\2([^\n]*)\n([\s\S]*?)\n[ \t]*\3[ \t]*(?=\n|$)/g)) {
    if (/(?:^|[\s|;&])(?:bash|sh|zsh|pwsh|powershell)(?:\s|$)/.test(m[1] + ' ' + m[4])) reasons.push(...sub(m[5], cwd));
  }

  const { segments } = parseCommand(cmd);
  let cur = cwd; let uncertain = false; const vars = {}; const cdStack = [];
  // listings piped into a delete: the listing is the target
  const originOf = (idx) => { let j = idx; while (j > 0 && segments[j].piped) j--; return j < idx ? j : -1; };
  const listFilter = (seg) => { try { const lt = seg && listTargets(seg); return (lt && lt.filterNames) || []; } catch { return []; } };
  const listTargets = (seg) => {
    const { verb, args } = verbOf(seg.words); const av = args.map((a) => a.v);
    if (verb === 'echo' || verb === 'printf') return av.filter((x) => !isFlag(x));
    if (!LIST_VERBS.has(verb)) return null;
    if (verb === 'find') { const lead = []; for (const a of av) { if (/^[-(!]/.test(a)) break; lead.push(a); } const res = lead.length ? lead : ['.']; res.filterNames = []; av.forEach((a, k) => { if (/^-(?:name|iname)$/.test(a) && av[k + 1]) res.filterNames.push(av[k + 1]); }); return res; }
    const out = []; const names = []; for (let k = 0; k < av.length; k++) { const a = av[k]; if (psPathFlag(a) && !/:/.test(a)) { if (av[k + 1]) out.push(av[++k]); continue; } if (psNameFilterFlag(a) && !/:/.test(a)) { if (av[k + 1]) names.push(av[++k]); continue; } if (psp(a, 'exclude', 3) || /^-(?:depth|attributes)$/i.test(a)) { k++; continue; } if (isFlag(a)) continue; out.push(a); }
    const res = out.length ? out : ['.']; res.filterNames = names; return res;
  };
  // pre-pass: a listing of docs\reference feeding a delete (Remove-Item, .Delete(), unlink): mass delete of photos
  segments.forEach((seg, idx) => {
    if (!seg.words.length || seg.piped) return;
    const lt = listTargets(seg); if (!lt || !/\|/.test(cmd) || !DELETEISH.test(cmd.replace(/^[^|]*\|/, ''))) return;
    const v = verbOf(seg.words).verb; if (v === 'echo' || v === 'printf') return;
    for (const raw of lt) { const r = resolveWord(raw, cur, vars); for (const p of r.values) if (ctx.inRef(p)) { reasons.push(`a listing of ${p} feeding a delete: a mass delete of the reference photos`); return; } }
    void idx;
  });

  const judgeDeleteTarget = (raw, verbName, recursive, opts = {}) => {
    const r = resolveWord(raw, cur, vars);
    if (r.unknown || !r.values.length) { if (recursive && risky(cur)) reasons.push(`a recursive delete of "${raw}" (a variable or substitution that cannot be resolved) in the main checkout, in a tree with a docs\\reference link, or in a command that names the photos`); return; }
    if (uncertain && !path.isAbsolute(winPath(r.raw)) && ctx.near(cwd) && recursive) { reasons.push(`a recursive delete of the relative path "${raw}" after a cd whose target cannot be resolved`); return; }
    for (const abs of r.values) {
      const globbed = hasGlob(abs);
      const list = globbed ? expandGlob(abs) : [abs];
      if (globbed && !list.length) continue;
      for (const p of (recursive ? [abs, ...list] : list)) {
        if (recursive) {
          const why = ctx.classify(p, opts);
          if (why) { reasons.push(`a recursive delete of "${raw}" (${p}): the target ${why}`); return; }
          if (['del', 'erase'].includes(verbName) && ctx.underWt(p)) { reasons.push(`\`${verbName} /s\` on "${raw}" in a worktree (it walks into any junction)`); return; }
        } else if (ctx.inRef(p) && (globbed ? list.length > 1 : false)) { reasons.push(`a mass delete of "${raw}" (${list.length} files inside docs\\reference): a wildcard or loop over the photos`); return; }
        else if (/^rmdir$|^rd$/.test(verbName) && strip(p) === ctx.refRoot) { reasons.push('rmdir of the main checkout\'s docs\\reference'); return; }
      }
      if (recursive && globbed && strip(path.dirname(abs)).endsWith('docs/reference/_compare')) { reasons.push(`a recursive delete of "${raw}": a wildcard directly under docs\reference\_compare could remove other agents' composites; name your own topic folder`); return; }
    }
  };

  for (const lit of oneLits) judgeDeleteTarget(lit, 'rmtree', true);   // gates7 item 1

  // gates7 item 10: a generic wrapper look-through. A segment whose verb is not one this guard knows (unbuffer, watch, parallel, flock, taskset, strace, script -c, any "<cmd> <args> -- <verb>")
  // may still carry a delete: if an unquoted word of it IS a delete or move verb, or the words after a "--" are a command, the rest is judged as a command of its own; for the few
  // wrappers that take a command as ONE quoted string (script -c, watch, parallel, su -c, ssh) that string is judged too. Text verbs (echo, grep, curl, npm ...) are left alone.
  const LOOK_VERBS = new Set(['rm', 'rmdir', 'rd', 'del', 'erase', 'remove-item', 'ri', 'unlink', 'mv', 'move-item', 'rename-item']);
  const TEXT_VERBS = new Set(['echo', 'printf', 'write-host', 'write-output', 'write-error', 'write-warning', 'cat', 'type', 'grep', 'egrep', 'fgrep', 'rg', 'ag', 'findstr', 'select-string', 'sls', 'sed', 'awk', 'gawk', 'less', 'more', 'head', 'tail', 'wc', 'ls', 'dir', 'gci', 'get-childitem', 'get-content', 'gc', 'curl', 'wget', 'gh', 'npm', 'npx', 'yarn', 'pnpm', 'pip', 'pip3', 'jq', 'tee', 'test', '[', '[[', 'mkdir', 'touch', 'stat', 'file', 'du', 'which', 'where', 'man', 'help', 'cmake', 'ninja', 'make', 'msbuild', 'dotnet', 'cargo', 'go', 'tsc', 'code', 'set', 'export', 'true', 'false', 'sleep', 'start-sleep', 'date', 'ctest', 'clang', 'g++', 'gcc', 'cl', 'link', 'sort', 'uniq', 'cut', 'tr', 'diff', 'comm', 'basename', 'dirname', 'realpath', 'readlink', 'id', 'whoami', 'hostname', 'uname', 'ps', 'tasklist', 'git-lfs', 'tar', 'zip', 'unzip', '7z']);
  const QUOTED_CMD_WRAPPERS = new Set(['script', 'watch', 'parallel', 'su', 'runuser', 'ssh', 'sg', 'pkexec', 'sudoedit', 'flock', 'unbuffer', 'chroot', 'proot', 'dbus-launch', 'xvfb-run', 'wsl']);
  const requote = (w) => (w.v === '' || /[\s"'*?\[\]{}()&|;<>$`]/.test(w.v) ? (w.v.includes('"') ? "'" + w.v + "'" : '"' + w.v + '"') : w.v);
  const lookThrough = (words, verb) => {
    if (TEXT_VERBS.has(verb)) return;
    const vi = verbOf(words).index;
    for (let i = vi + 1; i < words.length; i++) {
      const raw = words[i].v;
      const base = raw.toLowerCase().replace(/^.*[\\/]/, '').replace(/\.exe$/, '');
      if (!words[i].q && LOOK_VERBS.has(base)) { reasons.push(...sub(words.slice(i).map(requote).join(' '), cur)); return; }
      if (raw === '--' && !words[i].q && words[i + 1]) { reasons.push(...sub(words.slice(i + 1).map(requote).join(' '), cur)); return; }
      if (words[i].q && /\s/.test(raw) && QUOTED_CMD_WRAPPERS.has(verb) && /(^|[\s;&|(])(?:rm|rmdir|rd|del|erase|remove-item|ri|unlink|mv|move-item)\s/i.test(raw)) { reasons.push(...sub(raw, cur)); return; }
    }
  };

  for (let si = 0; si < segments.length; si++) {
    const seg = segments[si];
    for (const [k, v] of Object.entries(seg.assigns)) vars[k] = v === null ? null : v;
    // truncating redirect onto an existing picture in docs\reference
    for (const rd of seg.redirects) {
      if (rd.op !== '>' && rd.op !== '&>') continue;
      const r = resolveWord(rd.target, cur, vars);
      for (const p of r.values) if (ctx.inRef(p) && PIC_EXT.test(p) && fs.existsSync(p)) reasons.push(`a redirect (>) that overwrites the existing photo ${p}`);
    }
    const words = seg.words;
    if (!words.length) continue;
    // for NAME in a b c ... : bash loop list;  foreach ($d in a,b) arrives as  $d in a,b
    if (words[0].v === 'for' && words[2] && words[2].v === 'in') { vars[words[1].v.toLowerCase()] = words.slice(3).map((w) => w.v); continue; }
    if (/^\$[A-Za-z_]\w*$/.test(words[0].v) && words[1] && words[1].v.toLowerCase() === 'in') {
      const rest = words.slice(2).map((w) => w.v);
      vars[words[0].v.slice(1).toLowerCase()] = (/^[A-Za-z]+-[A-Za-z]+$/.test(rest[0] || '') || rest.some((x) => /\$\(|`/.test(x))) ? null : rest.flatMap((x) => x.split(',')).filter(Boolean);
      continue;
    }
    if (words[0].v === 'read') { const last = words[words.length - 1].v; if (!/^-/.test(last) && last !== 'read') vars[last.toLowerCase()] = null; continue; }
    const { verb, args } = verbOf(words);
    const av = args.map((a) => a.v);
    // a verb that is a whole command line (iex "Remove-Item ..." leaves the string as the verb)
    if (/\s/.test(verb) && words[verbOf(words).index].q) { reasons.push(...sub(words.slice(verbOf(words).index).map((w) => w.v).join(' '), cur)); continue; }
    // directory changes
    if (['cd', 'chdir', 'set-location', 'sl', 'pushd', 'push-location'].includes(verb)) {
      const tgt = av.filter((x) => !isFlag(x) || x === '-')[0];
      if (['pushd', 'push-location'].includes(verb)) cdStack.push({ cur, uncertain });
      if (tgt) { const r = resolveWord(tgt, cur, vars); if (r.unknown || !r.values.length) uncertain = true; else cur = r.values[0]; }
      continue;
    }
    if (['popd', 'pop-location'].includes(verb)) { const top = cdStack.pop(); if (top) { cur = top.cur; uncertain = top.uncertain; } else uncertain = true; continue; }
    // nested shells and evaluators
    if (verb === 'eval') { reasons.push(...sub(av.join(' '), cur)); continue; }
    if (SHELL_VERBS.has(verb)) {
      const joined = av.join(' ');
      const em = joined.match(/(?:^|\s)-e(?:nc(?:odedcommand)?)?\s+([A-Za-z0-9+/=]{8,})/i);
      if (em) { try { reasons.push(...sub(Buffer.from(em[1], 'base64').toString('utf16le'), cur)); } catch { /* not base64 */ } }
      const m = joined.match(/(?:^|\s)(?:-[a-z]*c|-command|\/{1,2}[ck])\s+([\s\S]*)$/i);
      if (m) reasons.push(...sub(m[1], cur));
      else if (seg.here) reasons.push(...sub(seg.here, cur));
      else if (seg.piped) { const j = originOf(si); for (let k = si - 1; k >= Math.max(j, 0) && j >= 0; k--) { const pv = verbOf(segments[k].words); if (pv.verb === 'echo' || pv.verb === 'printf') reasons.push(...sub(pv.args.map((a) => a.v).join(' '), cur)); } }
      continue;
    }
    if (seg.here && SHELL_VERBS.has(verb)) reasons.push(...sub(seg.here, cur));
    // inline code in another language: shell strings inside it are judged as commands
    if (['python', 'python3', 'py', 'node', 'nodejs', 'perl', 'ruby', 'bun', 'deno'].includes(verb)) {
      const k = av.findIndex((a) => /^(?:-e|-c|--eval|-p|-pe|-r)$/.test(a));
      if (k >= 0 && av[k + 1]) {
        const code = av[k + 1];
        for (const q of code.matchAll(/(["'`])((?:\\.|(?!\1)[^\\])*)\1/g)) if (/\b(?:rm|rmdir|rd|del|erase|remove-item|git)\b/i.test(q[2])) reasons.push(...sub(q[2], cur));
      }
      continue;
    }
    // git
    if (verb === 'git') {
      let repo = cur; let i = 0;
      while (i < av.length && /^-/.test(av[i])) {
        if (av[i] === '-C' && av[i + 1]) { const r = resolveWord(av[i + 1], cur, vars); if (r.values[0] && !r.unknown) repo = r.values[0]; else uncertain = true; i += 2; }
        else if (av[i] === '-c') i += 2; else i++;
      }
      if (/^worktree$/i.test(av[i] || '') && /^(?:remove|prune)$/i.test(av[i + 1] || '')) reasons.push('`git worktree remove|prune` outside tools\\remove_agent_worktree.ps1 (run that script: powershell -NoProfile -ExecutionPolicy Bypass -File tools\\remove_agent_worktree.ps1 <name>)');
      if (/^clean$/i.test(av[i] || '')) {
        const rest = av.slice(i + 1);
        const exVal = new Set(); rest.forEach((x, ix) => { if (/^(?:-e|--exclude)$/.test(x) && rest[ix + 1] !== undefined) exVal.add(ix + 1); });   // gates7 item 8: the value of -e / --exclude is not a path
        const fl = rest.filter((x, ix) => /^-/.test(x) && x !== '--' && !exVal.has(ix));
        const dry = fl.some((f) => /^--dry-run$/.test(f) || /^-[a-zA-Z]*n[a-zA-Z]*$/.test(f));
        const bad = fl.some((f) => /^-[a-zA-Z]*[xXd][a-zA-Z]*$/.test(f));
        const specs = rest.filter((x, ix) => !/^-/.test(x) && !exVal.has(ix));
        if (bad && !dry) {
          if (!specs.length) { if (ctx.near(repo) || uncertain) reasons.push('`git clean` with -x, -X or -d in the main checkout or a worktree removes ignored and untracked folders (the docs\\reference junction of an older worktree among them): name the folder, e.g. git clean -fd build'); }
          else {
            const saved = cur; cur = repo;
            for (const sp of specs) { const r = resolveWord(sp, repo, vars); if (r.unknown || !r.values.length) { if (risky(repo)) reasons.push(`git clean of "${sp}" (cannot be resolved)`); continue; } for (const p of r.values.flatMap((x) => expandGlob(x))) { const why = ctx.classify(p, {}); if (why) { reasons.push(`git clean of "${sp}" (${p}): the path ${why}`); break; } if (ctx.underMain(p) && !ctx.underWt(p) && strip(p) === ctx.main) { reasons.push('git clean of the main checkout root'); break; } } }
            cur = saved;
          }
        }
      }
      continue;
    }
    // robocopy
    if (verb === 'robocopy') {
      const opts = av.filter((x) => /^\/[a-zA-Z]+(?::\S*)?$/.test(x) && x.length < 12);
      const pos = av.filter((x) => !opts.includes(x));
      const moving = opts.some((f) => /^\/mov(?:e)?$/i.test(f));
      if (moving && pos[0]) { const s0 = resolveWord(pos[0], cur, vars); if (s0.unknown || s0.values.some((p) => ctx.inRef(p) || strip(p) === ctx.main || strip(p) === ctx.main + '/docs')) reasons.push('robocopy /MOV or /MOVE from the main checkout or docs\\reference deletes the source after copying'); }
      const mirror = opts.some((f) => /^\/(?:mir|purge)$/i.test(f));
      if (!mirror) continue;
      if (pos.length < 2) { if (nearText()) reasons.push('robocopy /MIR or /PURGE whose destination cannot be read'); continue; }
      const src = resolveWord(pos[0], cur, vars), dst = resolveWord(pos[1], cur, vars);
      if (dst.unknown || !dst.values.length) { if (nearText()) reasons.push(`robocopy /MIR or /PURGE into "${pos[1]}" (a variable: the destination cannot be checked)`); continue; }
      const d = dst.values[0]; const dn = strip(d);
      if (BACKUP_DIRS.some((b) => dn === b)) {
        const s = src.values[0] && !src.unknown ? strip(src.values[0]) : '';
        if (!(s === ctx.refRoot || s.startsWith(ctx.refRoot + '/'))) reasons.push('robocopy /MIR into the reference backup from anything but the main checkout\'s docs\\reference (it would delete the backup\'s photos)');
        else if (opts.some((f) => /^\/mov(?:e)?$/i.test(f))) reasons.push('robocopy /MIR /MOV into the backup would delete the photos after copying');
        continue;
      }
      const why = ctx.classify(d) || (ctx.underWt(d) ? 'is inside a worktree' : null);
      if (why) reasons.push(`robocopy /MIR or /PURGE into "${pos[1]}" (the destination ${why}): /MIR deletes in the destination`);
      continue;
    }
    if (verb === 'rsync' && av.some((a) => /^--delete/.test(a))) {
      const pos = av.filter((x) => !/^-/.test(x)); const d = pos[pos.length - 1] && resolveWord(pos[pos.length - 1], cur, vars);
      if (d && (d.unknown ? nearText() : d.values.some((p) => ctx.classify(p) || ctx.inRef(p)))) reasons.push('rsync --delete into the main checkout, docs\\reference, a worktree or the backup');
      continue;
    }
    // moves and renames: the SOURCE must not be the photos, their folder, main, a worktree root or the backup
    if (MOVE_VERBS.has(verb) || COPY_VERBS.has(verb)) {
      const pos = []; let named = {};
      for (let k = 0; k < av.length; k++) { const a = av[k]; const m = a.match(/^-(path|literalpath|destination|newname):(.*)$/i); if (m) { named[m[1].toLowerCase()] = m[2]; continue; } if (/^-(?:path|literalpath|destination|newname)$/i.test(a)) { named[a.slice(1).toLowerCase()] = av[++k]; continue; } if (/^-(?:include|exclude|filter)$/i.test(a)) { k++; continue; } if (isFlag(a)) continue; pos.push(a); }
      if (named.path) pos.unshift(named.path); if (named.literalpath) pos.unshift(named.literalpath);
      let dest = named.destination || null; let srcs = pos;
      if (!dest && pos.length >= 2 && !['rename-item', 'rni', 'ren'].includes(verb)) { dest = pos[pos.length - 1]; srcs = pos.slice(0, -1); }
      if (['rename-item', 'rni', 'ren'].includes(verb)) srcs = pos.slice(0, 1);
      const dres = dest ? resolveWord(dest, cur, vars) : { values: [], unknown: false };
      const toDamaged = dres.values.length && dres.values.every((p) => /(^|\/)docs\/reference\/_damaged(\/|$)/.test(strip(p)));
      if (MOVE_VERBS.has(verb)) {
        for (const raw of srcs) {
          const r = resolveWord(raw, cur, vars);
          if (r.unknown || !r.values.length) { if (risky(cur)) reasons.push(`a move of "${raw}" (cannot be resolved) in the main checkout or a tree with a docs\\reference link`); continue; }
          for (const abs of r.values) {
            const list = hasGlob(abs) ? expandGlob(abs) : [abs];
            if (!list.length) continue;
            const n = strip(abs);
            const why = (n === ctx.main || n === ctx.main + '/docs' || n === ctx.refRoot) ? 'is the main checkout, its docs folder or docs\\reference' : (ctx.isWtRoot(n) ? 'is a worktree root' : (BACKUP_DIRS.some((b) => n === b || n.startsWith(b + '/')) ? 'is the reference backup' : (ctx.classify(abs) && /contains the main checkout|worktree root|roadworld-wt folder/.test(ctx.classify(abs)) ? 'is a folder above main or a worktree' : null)));
            if (why) { reasons.push(`a move or rename of "${raw}" (${abs}): the source ${why}`); break; }
            if (list.some((p) => ctx.inRef(p))) {
              if (toDamaged) continue;
              if (list.length > 1 && hasGlob(abs)) { reasons.push(`a move of ${list.length} files of docs\\reference at once ("${raw}") anywhere but into docs\\reference\\_damaged`); break; }
              if (list.some((p) => { try { return fs.statSync(p).isDirectory(); } catch { return false; } })) { reasons.push(`a move or rename of the folder "${raw}" inside docs\\reference (name single photos, or move into docs\\reference\\_damaged)`); break; }
            }
          }
        }
      }
      // overwriting an existing photo
      for (const p of dres.values) if (ctx.inRef(p) && PIC_EXT.test(p) && fs.existsSync(p) && !['rename-item', 'rni', 'ren'].includes(verb)) reasons.push(`${verb} onto the existing photo ${p} (it would be overwritten)`);
      continue;
    }
    if (TRUNC_VERBS.has(verb)) {
      for (const raw of av.filter((x) => !isFlag(x))) { const r = resolveWord(raw, cur, vars); for (const p of r.values) if (ctx.inRef(p) && PIC_EXT.test(p) && fs.existsSync(p) && !(verb === 'truncate' && false)) reasons.push(`${verb} on the existing photo ${p} (truncation)`); }
      continue;
    }
    // delete verbs, also behind xargs
    let verbName = verb, targetsRaw = null, pipedFed = false, recursive = false;
    if (verb === 'xargs') {
      const k = av.findIndex((a) => DEL_VERBS.has(a.toLowerCase().replace(/^.*[\\/]/, '')) || /^(?:truncate|shred)$/i.test(a.replace(/^.*[\\/]/, '')));
      if (k < 0) continue;
      verbName = av[k].toLowerCase().replace(/^.*[\\/]/, ''); targetsRaw = av.slice(k + 1); pipedFed = true;
    } else if (DEL_VERBS.has(verb)) targetsRaw = av;
    else { lookThrough(words, verb); continue; }
    const flags = targetsRaw.filter(isFlag);
    // gates7 item 2: `del -Recurse -Force build` (a dash switch) is PowerShell's alias of Remove-Item, not cmd's `del /s`
    if (['del', 'erase'].includes(verbName) && flags.some((f) => /^-/.test(f)) && !flags.some((f) => /^\/{1,2}s$/i.test(f))) verbName = 'remove-item';
    // gates7 item 8: `rm -fo` / `del -fo` (the abbreviated PowerShell -Force) marks a Remove-Item style call: a listing piped in is then judged like Remove-Item's
    const psForce = ['rm', 'del', 'erase'].includes(verb) && !pipedFed && flags.some((f) => /^-fo(?:r(?:c(?:e)?)?)?$/i.test(f));
    if (verbName === 'rm') recursive = flags.some((f) => psRecurse(f) || /^--recursive$/i.test(f) || (/^-[rRfivId]{1,6}$/.test(f) && /[rR]/.test(f)));
    else if (['rmdir', 'rd', 'del', 'erase'].includes(verbName)) recursive = flags.some((f) => /^\/{1,2}s$/i.test(f) || psRecurse(f));
    else if (verbName === 'remove-item' || verbName === 'ri') recursive = flags.some(psRecurse);
    let targets; let findOpts = {};
    if (verbName === 'find') {
      const exec = targetsRaw.findIndex((a) => /^-(?:exec|execdir|ok)$/.test(a));
      recursive = targetsRaw.includes('-delete') || (exec >= 0 && /^(?:rm|rmdir|unlink|truncate|shred|dd)$/.test((targetsRaw[exec + 1] || '').replace(/^.*[\\/]/, '')));   // gates7 item 7: -exec truncate|shred|dd too
      const lead = []; for (const a of targetsRaw) { if (/^[-(!]/.test(a)) break; lead.push(a); }
      targets = lead.length ? lead : ['.'];
      const names = []; targetsRaw.forEach((a, k) => { if (/^-(?:name|iname|path|ipath|regex|iregex)$/.test(a) && targetsRaw[k + 1]) names.push(targetsRaw[k + 1]); });
      const filtered = targetsRaw.some((a) => /^-(?:name|iname|path|ipath|regex|iregex|newer|mtime|mmin|size|empty|user)$/.test(a)) || (targetsRaw.some((a) => a === '-type') && targetsRaw[targetsRaw.indexOf('-type') + 1] === 'f');
      if (filtered) findOpts = { filtered: true, picFilter: nameMayMatchPic(names.filter((n) => !/^-?regex/.test(n))) };
    } else {
      targets = [];
      for (let k = 0; k < targetsRaw.length; k++) {
        const a = targetsRaw[k];
        if (psPathFlag(a) && !/:/.test(a)) { if (targetsRaw[k + 1]) targets.push(targetsRaw[++k]); continue; }
        const m = a.match(/^(-[a-z]+):(.+)$/i); if (m && psPathFlag(m[1])) { targets.push(m[2]); continue; }
        if (!/:/.test(a) && (psNameFilterFlag(a) || psp(a, 'exclude', 3) || psp(a, 'credential', 2) || psp(a, 'stream', 2))) { k++; continue; }
        if (isFlag(a)) continue;
        targets.push(a);
      }
      targets = targets.flatMap((x) => (['remove-item', 'ri'].includes(verbName) ? x.split(',') : [x])).filter(Boolean);
    }
    // targets that come from a pipeline: the listing in front is the target
    let viaPipe = false;
    const fed = seg.piped || pipedFed;
    if (fed && (!targets.length || targets.every((x) => /\$_|\$input|\$PSItem/i.test(x)))) {
      const j = originOf(si);
      if (j >= 0) {
        const lt = listTargets(segments[j]);
        if (lt) { targets = lt; viaPipe = true; if (lt.filterNames && lt.filterNames.length) findOpts = { filtered: true, picFilter: nameMayMatchPic(lt.filterNames) }; }   // gates7 item 3: a filtered listing (-Filter "build*") is judged by the links it would walk, like find -name
        else if (recursive || verbName === 'remove-item') { if (risky(cur)) reasons.push('a recursive delete fed by a pipeline whose source is not a listing (its targets cannot be checked) in the main checkout, a tree with a docs\\reference link, or a command naming the photos'); continue; }
      }
    }
    if (viaPipe && (['remove-item', 'ri'].includes(verbName) || psForce) && !recursive) {
      // Get-ChildItem x | Remove-Item deletes the listed folders with their content (after a prompt) and the files: judge like a recursive delete
      recursive = true;
    }
    if (!recursive && !targets.length) continue;
    if (!recursive && !['rm', 'del', 'erase', 'remove-item', 'ri', 'unlink', 'rmdir', 'rd', 'truncate', 'shred'].includes(verbName)) continue;
    if (recursive && !targets.length) {
      if (risky(cur)) reasons.push(`a recursive ${verbName} with a target that cannot be read (a parenthesised or computed path) in the main checkout, a tree with a docs\\reference link, or a command naming the photos`);
      continue;
    }
    // gates7 item 7: a listing of docs\reference fed to rm / del / truncate / shred (xargs rm with no flags) deletes photos one by one: a mass delete
    if (viaPipe && !recursive) for (const raw of targets) { const r = resolveWord(raw, cur, vars); for (const p of r.values) if (ctx.inRef(p) || ((strip(p) === ctx.main || strip(p) === ctx.main + '/docs' || ctx.refRoot.startsWith(strip(p) + '/')) && nameMayMatchPic(listFilter(segments[originOf(si)])))) { reasons.push(`a listing of ${p} fed to ${verbName}: a mass delete or truncation of the reference photos`); break; } }
    for (const raw of targets) judgeDeleteTarget(raw, verbName, recursive, findOpts);
  }
  return [...new Set(reasons)];
}

export async function main() {
  const raw = await readStdin();
  const input = raw === null ? undefined : parseJson(raw);
  if (!input || typeof input !== 'object') { log({ gate: 'delete_guard', decision: 'allow', reason: 'FAIL-OPEN: stdin missing or not JSON' }); return 0; }
  const tool = input.tool_name || '';
  if (!/^(Bash|PowerShell)$/.test(tool)) return 0;
  const command = (input.tool_input && input.tool_input.command) || '';
  const reasons = analyse(command, input.cwd || process.cwd());
  if (!reasons.length) return 0;
  log({ gate: 'delete_guard', decision: 'deny', reason: 'delete, move or truncation near the reference photos refused', agent_id: input.agent_id || '', cwd: input.cwd, command: String(command).slice(0, 300), reasons });
  process.stderr.write('REFUSED by the delete guard (tools/hooks/delete_guard.mjs):\n - ' + reasons.join('\n - ') + '\n' + SCRIPT_HINT + '\n');
  return 2;
}
