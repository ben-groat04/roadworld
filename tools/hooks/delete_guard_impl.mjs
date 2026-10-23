// PreToolUse hook (matcher Bash|PowerShell): a best-effort NET against recursive deletes that would wipe the private reference photos. It is NOT the protection:
// the protection is structural (main session decision, 23 Oct 2026): worktrees no longer get a docs\reference junction (tools\new_agent_worktree.ps1 creates none;
// tools\unlink_reference_junctions.ps1 removes the old ones), and tools\backup_reference.mjs keeps a validated mirror in C:\Users\bgbg0\roadworld-reference-backup.
// Why a net at all: on 22 Oct 2026 a recursive delete of a worktree followed that junction and wiped every reference photo. Applies to the main session and to every subagent.
//
// SMALL RULE SET. A command is refused when it does a recursive delete (rm -r/-rf/-R/--recursive, Remove-Item/ri/rm with -Recurse or -Recurse:$true, rmdir/rd /s,
// find -delete or -exec rm, xargs rm -r, rmtree / fs.rmSync recursive / rimraf / Directory.Delete(.., true) one-liners near a tree) and the resolved target is
//   R1  docs\reference anywhere (the main checkout's photos), or inside it;
//   R2  the MAIN checkout, its docs folder, or any folder above it (the main checkout comes from root.mjs: git's worktree link files, never "where this hook file sits");
//   R3  a worktree root (conventional roadworld-wt\<n> and roadworld-wt-<n>, plus every worktree git lists), the roadworld-wt folder, or a folder above one;
//   R4  the backup folder C:\Users\bgbg0\roadworld-reference-backup, inside it, or above it;
//   R5  a folder below a worktree that, on disk, still holds a junction or symlink (walked WITHOUT following links), or whose wildcard matches one;
// or a target that cannot be resolved (variable, pipeline, command substitution) while the working directory is inside the main checkout or a worktree.
// Also refused: `git clean` with -x, -X or -d (not -n) in the main checkout or any worktree; `del /s` or `erase /s` on a path in a worktree; `git worktree remove|prune`
// outside tools\remove_agent_worktree.ps1; robocopy /MIR or /PURGE whose DESTINATION (the second path, never the source) is R1-R4 or inside a worktree; and a /MIR into the backup
// from anything but main's docs\reference. Robocopy main docs\reference -> backup with /MIR is the prescribed backup and is allowed.
// Resolved as far as is cheap: quotes and "" concatenation, cd and Set-Location chains, ~ and simple $x = "literal" / foreach ($d in "a","b") variables, comma lists,
// -Recurse:$true, env/sudo/xargs/find -exec wrappers, if/try/foreach/{ } blocks, bash -c / powershell -Command / cmd /c payloads.
// WHAT IT CANNOT SEE (accepted: structural protection + validated backup, main session decision 23 Oct 2026): a delete done inside a script file the command only runs
// (node x.mjs, a .ps1 or .bat), a path assembled from variables over several steps or read from a file, command substitution results, aliases and functions defined
// elsewhere, Get-ChildItem | ForEach-Object { [IO.File]::Delete() } style loops, a path argument given in parentheses (Remove-Item (Join-Path a b) -Recurse), a delete started
// by a tool other than Bash/PowerShell, xcopy /E /Y over files, robocopy with /MOVE, and the other ways a program can walk a directory. It reads the command text and the
// disk, nothing else. FAIL OPEN (logged) on any internal error.
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { ROOT, log, readStdin, parseJson } from './lib.mjs';
import { worktreeRoots } from './root.mjs';
import { parseCommand, verbOf } from './shell_lex.mjs';

export const SCRIPT_HINT = 'Remove a worktree only with: powershell -NoProfile -ExecutionPolicy Bypass -File tools\\remove_agent_worktree.ps1 <name> (it unlinks any docs\\reference junction first and checks the photos are still there). Older worktrees still hold a junction (docs\\reference) to the private reference photos: a recursive delete follows it and wipes them (22 Oct 2026). Deleting build\\, node_modules or a scratch folder with no link inside is allowed.';

export const BACKUP_DIRS = [...new Set(['C:/Users/bgbg0/roadworld-reference-backup', path.join(os.homedir(), 'roadworld-reference-backup')].map((p) => p.replace(/\\/g, '/').toLowerCase()))];

const norm = (p) => String(p).replace(/\\/g, '/').replace(/\/+/g, '/').toLowerCase();
const strip = (p) => norm(p).replace(/\/+$/, '');
// bash-on-Windows paths: /c/Users/x -> c:/users/x
const winPath = (p0) => { const p = /^\/{2,}[a-zA-Z]\//.test(p0) ? p0.replace(/^\/+/, '/') : p0; return /^\/([a-zA-Z])(\/|$)/.test(p) ? p[1] + ':' + p.slice(2) : p; };

// every reparse point (junction / symlink) at or below `p`, found without descending into links. Returns paths, [] when p does not exist.
export function linksBelow(p, limit = 20000) {
  const found = []; let seen = 0;
  const walk = (d) => {
    let ents; try { ents = fs.readdirSync(d, { withFileTypes: true }); } catch { return; }
    for (const e of ents) {
      if (++seen > limit) return;
      const f = path.join(d, e.name);
      if (e.isSymbolicLink()) { found.push(f); continue; }
      let st; try { st = fs.lstatSync(f); } catch { continue; }
      if (st.isSymbolicLink()) { found.push(f); continue; }
      if (e.isDirectory()) {
        // Windows junctions report isDirectory() through readdir; realpath tells them apart from a real folder
        try { const rp = fs.realpathSync(f); if (norm(rp) !== norm(path.resolve(f))) { found.push(f); continue; } } catch { /* unreadable: ignore */ }
        walk(f);
      }
    }
  };
  let st; try { st = fs.lstatSync(p); } catch { return []; }
  if (st.isSymbolicLink()) return [p];
  if (st.isDirectory()) walk(p);
  return found;
}

function expandWild(abs) {
  // only a wildcard in the last segment is expanded: dir/* , dir/*.log
  const base = path.dirname(abs), pat = path.basename(abs);
  if (!/[*?]/.test(pat)) return [abs];
  const re = new RegExp('^' + pat.replace(/[.+^${}()|[\]\\]/g, '\\$&').replace(/\*/g, '.*').replace(/\?/g, '.') + '$', 'i');
  try { return fs.readdirSync(base).filter((n) => re.test(n)).map((n) => path.join(base, n)); } catch { return []; }
}

const WT_NAME = /(^|\/)roadworld-wt(-[^/]+)?(\/|$)/;
export function context(mainRoot) {
  const main = strip(mainRoot);
  const wts = worktreeRoots(mainRoot).map(strip);
  const underWt = (p) => { const n = strip(p); return WT_NAME.test(n + '/') || wts.some((w) => n === w || n.startsWith(w + '/')); };
  const underMain = (p) => { const n = strip(p); return n === main || n.startsWith(main + '/'); };
  const isWtRoot = (p) => { const n = strip(p); return /(^|\/)roadworld-wt$/.test(n) || /(^|\/)roadworld-wt\/[^/]+$/.test(n) || /(^|\/)roadworld-wt-[^/]+$/.test(n) || wts.includes(n); };
  const above = (n, of) => !!n && of.some((o) => o === n || o.startsWith(n + '/'));
  const guarded = [main, ...BACKUP_DIRS, ...wts];
  return {
    main, wts, underWt, underMain,
    near: (p) => underWt(p) || underMain(p),
    // returns a refusal reason or null for one RESOLVED absolute path
    classify(abs) {
      const n = strip(abs);
      if (/(^|\/)docs\/reference(\/|$)/.test(n)) return 'is or lies inside docs\\reference (the private reference photos)';
      if (n === main || n === main + '/docs') return 'is the main checkout root or its docs folder (holds the private reference photos)';
      if (above(n, guarded) && !underMain(n) && !underWt(n)) return 'is or contains the main checkout, a worktree or the reference backup';
      if (BACKUP_DIRS.some((b) => n === b || n.startsWith(b + '/'))) return 'is the reference backup folder or inside it';
      if (isWtRoot(n)) return 'is a worktree root or the roadworld-wt folder';
      if (underWt(n)) {
        const links = linksBelow(abs);
        if (links.length) return `holds ${links.length} junction/symlink(s) (${links.slice(0, 2).join(', ')}): a recursive delete follows them into their targets`;
      }
      return null;
    },
  };
}

// resolve one word against vars/env/cwd. Returns { values: [abs paths], unknown: bool, raw }
function resolveWord(raw, cwd, vars) {
  let t = String(raw);
  const home = process.env.USERPROFILE || process.env.HOME || '~';
  t = t.replace(/^~(?=\/|\\|$)/, home);
  let unknown = t === '{}' || /\$\(|`/.test(t);
  const sub = (word, depth = 0) => {
    const m = word.match(/\$\{?(env:)?([A-Za-z_]\w*)\}?|%([A-Za-z_]\w*)%/);
    if (!m || depth > 5) return [word];
    const name = (m[2] || m[3]).toLowerCase();
    let val = vars[name];
    if (val === undefined && (m[1] || m[3])) val = process.env[m[2] || m[3]] ?? process.env[(m[2] || m[3]).toUpperCase()];
    if (val === undefined && (name === 'home' || name === 'userprofile')) val = home;
    if (val === undefined || val === null) { unknown = true; return []; }
    return (Array.isArray(val) ? val : [val]).flatMap((x) => sub(word.replace(m[0], x), depth + 1));
  };
  const values = sub(t);
  const abs = values.map((v) => { const w = winPath(v); return path.resolve(/^[a-zA-Z]:|^\//.test(w) ? w : path.join(cwd, w)); });
  return { values: abs, unknown, raw: t };
}

const DEL_VERBS = new Set(['rm', 'rmdir', 'rd', 'del', 'erase', 'remove-item', 'ri', 'find']);
const isFlag = (x) => /^-/.test(x) || /^\/[a-zA-Z](:\S*)?$/.test(x);
const psRecurse = (f) => /^-r(?:e(?:c(?:u(?:r(?:s(?:e)?)?)?)?)?)?(?::\$?true)?$/i.test(f);

// returns [] or an array of refusal reasons
export function analyse(command, cwd = process.cwd(), depth = 0, ctx = null, mainRoot = ROOT) {
  const reasons = [];
  const cmd = String(command || '');
  if (depth > 4) return reasons;
  ctx = ctx || context(mainRoot);
  const ncmd = norm(cmd);
  const usesScript = /remove_agent_worktree/i.test(cmd);
  const nearText = () => ctx.near(cwd) || /roadworld/.test(ncmd) || ncmd.includes(ctx.main) || ctx.wts.some((w) => ncmd.includes(w)) || /docs[\\/]+reference/i.test(cmd);
  // recursive deletes written as a one-liner in another language near a tree
  if (/shutil\.rmtree|\brmtree\s*\(|\brm(?:dir)?(?:Sync)?\s*\([^)]*recursive\s*:\s*true|\brimraf\b|Directory\]::Delete\s*\([^)]*,\s*\$?true|Directory\.Delete\s*\([^)]*,\s*\$?true/i.test(cmd) && nearText()) reasons.push('a recursive delete written as a script one-liner (rmtree / fs.rmSync recursive / rimraf / Directory.Delete(.., true)) in or near the main checkout or a worktree: it follows a docs\\reference junction');
  if (/\bgit(?:\.exe)?\s+(?:-C\s+\S+\s+)?worktree\s+(?:remove|prune)\b/i.test(cmd) && !usesScript) reasons.push('`git worktree remove|prune` outside tools\\remove_agent_worktree.ps1');

  let cur = cwd; let uncertain = false; const vars = {};
  const { segments } = parseCommand(cmd);
  for (const seg of segments) {
    for (const [k, v] of Object.entries(seg.assigns)) vars[k] = v === null ? null : v;
    const words = seg.words;
    if (!words.length) continue;
    // foreach ($d in "a","b") { ... } arrives as a segment  $d in a,b
    if (/^\$[A-Za-z_]\w*$/.test(words[0].v) && words[1] && words[1].v.toLowerCase() === 'in') { vars[words[0].v.slice(1).toLowerCase()] = words.slice(2).flatMap((w) => w.v.split(',')).filter(Boolean); continue; }
    const { verb, args } = verbOf(words);
    const av = args.map((a) => a.v);
    // directory changes
    if (['cd', 'chdir', 'set-location', 'sl', 'pushd'].includes(verb)) {
      const tgt = av.filter((x) => !isFlag(x) || x === '-')[0];
      if (tgt) { const r = resolveWord(tgt, cur, vars); if (r.unknown || !r.values.length) uncertain = true; else cur = r.values[0]; }
      continue;
    }
    // nested shells
    if (['bash', 'sh', 'zsh', 'pwsh', 'powershell', 'cmd'].includes(verb)) {
      const k = av.findIndex((a) => /^(?:-[a-z]*c|-command|\/c|\/k)$/i.test(a));
      if (k >= 0 && av[k + 1]) reasons.push(...analyse(av.slice(k + 1).join(' '), cur, depth + 1, ctx, mainRoot));
      continue;
    }
    // git clean
    if (verb === 'git') {
      let repo = cur; let i = 0;
      while (i < av.length && /^-/.test(av[i])) {
        if (av[i] === '-C' && av[i + 1]) { const r = resolveWord(av[i + 1], cur, vars); if (r.values[0] && !r.unknown) repo = r.values[0]; else uncertain = true; i += 2; }
        else if (av[i] === '-c') i += 2; else i++;
      }
      if (/^clean$/i.test(av[i] || '')) {
        const fl = av.slice(i + 1).filter((x) => /^-/.test(x));
        const dry = fl.some((f) => /^--dry-run$/.test(f) || /^-[a-zA-Z]*n[a-zA-Z]*$/.test(f));
        const bad = fl.some((f) => /^-[a-zA-Z]*[xXd][a-zA-Z]*$/.test(f));
        if (bad && !dry && (ctx.near(repo) || uncertain)) reasons.push('`git clean` with -x, -X or -d in the main checkout or a worktree removes ignored and untracked folders (the docs\\reference junction of an older worktree among them)');
      }
      continue;
    }
    // robocopy: judge the destination only
    if (verb === 'robocopy') {
      const opts = av.filter((x) => /^\/[a-zA-Z]+(?::\S*)?$/.test(x) && x.length < 12);
      const mirror = opts.some((f) => /^\/(?:mir|purge)$/i.test(f));
      if (!mirror) continue;
      const pos = av.filter((x) => !opts.includes(x));
      if (pos.length < 2) { if (nearText()) reasons.push('robocopy /MIR or /PURGE whose destination cannot be read'); continue; }
      const src = resolveWord(pos[0], cur, vars), dst = resolveWord(pos[1], cur, vars);
      if (dst.unknown || !dst.values.length) { if (nearText()) reasons.push(`robocopy /MIR or /PURGE into "${pos[1]}" (a variable: the destination cannot be checked)`); continue; }
      const d = dst.values[0]; const dn = strip(d);
      if (BACKUP_DIRS.some((b) => dn === b)) {
        const s = src.values[0] && !src.unknown ? strip(src.values[0]) : '';
        if (!(s === ctx.main + '/docs/reference' || s.startsWith(ctx.main + '/docs/reference/'))) reasons.push('robocopy /MIR into the reference backup from anything but the main checkout\'s docs\\reference (it would delete the backup\'s photos)');
        continue;
      }
      const why = ctx.classify(d) || (ctx.underWt(d) ? 'is inside a worktree' : null);
      if (why) reasons.push(`robocopy /MIR or /PURGE into "${pos[1]}" (the destination ${why}): /MIR deletes in the destination`);
      continue;
    }
    // delete verbs, also behind xargs
    let verbName = verb, targetsRaw = null, pipedFed = false, recursive = false;
    if (verb === 'xargs') {
      const k = av.findIndex((a) => DEL_VERBS.has(a.toLowerCase().replace(/^.*[\\/]/, '')));
      if (k < 0) continue;
      verbName = av[k].toLowerCase().replace(/^.*[\\/]/, ''); targetsRaw = av.slice(k + 1); pipedFed = true;
    } else if (DEL_VERBS.has(verb)) targetsRaw = av;
    else continue;
    const flags = targetsRaw.filter(isFlag);
    if (verbName === 'rm') recursive = flags.some((f) => psRecurse(f) || /^--recursive$/i.test(f) || (/^-[rRfivId]{1,6}$/.test(f) && /[rR]/.test(f)));
    else if (['rmdir', 'rd', 'del', 'erase'].includes(verbName)) recursive = flags.some((f) => /^\/s$/i.test(f) || psRecurse(f));
    else if (verbName === 'remove-item' || verbName === 'ri') recursive = flags.some(psRecurse);
    let targets;
    if (verbName === 'find') {
      const exec = targetsRaw.findIndex((a) => /^-(?:exec|execdir|ok)$/.test(a));
      recursive = targetsRaw.includes('-delete') || (exec >= 0 && /^(?:rm|rmdir|unlink)$/.test((targetsRaw[exec + 1] || '').replace(/^.*[\\/]/, '')));
      const lead = []; for (const a of targetsRaw) { if (/^[-(!]/.test(a)) break; lead.push(a); }
      targets = lead.length ? lead : ['.'];
    } else {
      targets = []; const skipVal = new Set(['-include', '-exclude', '-filter', '-credential', '-stream']);
      for (let k = 0; k < targetsRaw.length; k++) {
        const a = targetsRaw[k];
        if (/^-(?:path|literalpath)$/i.test(a)) { if (targetsRaw[k + 1]) targets.push(targetsRaw[++k]); continue; }
        const m = a.match(/^-(?:path|literalpath):(.+)$/i); if (m) { targets.push(m[1]); continue; }
        if (skipVal.has(a.toLowerCase())) { k++; continue; }
        if (isFlag(a)) continue;
        targets.push(a);
      }
      targets = targets.flatMap((x) => (['remove-item', 'ri'].includes(verbName) ? x.split(',') : [x])).filter(Boolean);
    }
    // Remove-Item fed by a recursive Get-ChildItem walks the junction even without its own -Recurse
    if (!recursive && ['remove-item', 'ri', 'rm', 'del', 'erase'].includes(verbName) && !targets.length && /\|/.test(cmd) && /(?:get-childitem|gci|\bls\b|\bdir\b|\bfind\b)[^|]*(?:-recurse|\s-r\b)/i.test(cmd)) recursive = true;
    if (!recursive) continue;
    if ((pipedFed || !targets.length) && /\|/.test(cmd) && !targets.length) { if (nearText()) reasons.push('a recursive delete fed by a pipeline (its targets cannot be checked) near the main checkout or a worktree'); continue; }
    for (const raw of targets) {
      const r = resolveWord(raw, cur, vars);
      if (r.unknown || !r.values.length) { if (nearText() || ctx.near(cur)) reasons.push(`a recursive delete of "${raw}" (a variable or substitution: it cannot be checked) near the main checkout or a worktree`); continue; }
      if (uncertain && !path.isAbsolute(winPath(r.raw)) && ctx.near(cwd)) { reasons.push(`a recursive delete of the relative path "${raw}" after a cd whose target cannot be resolved`); continue; }
      if (/[*?]/.test(path.dirname(r.raw.replace(/\\/g, '/')))) { if (nearText() || ctx.near(cur)) reasons.push(`a recursive delete of "${raw}" (a wildcard in a folder name cannot be expanded here) near the main checkout or a worktree`); continue; }
      for (const abs of r.values) {
        let refused = false;
        for (const p of [abs, ...expandWild(abs)]) {
          const why = ctx.classify(p);
          if (why) { reasons.push(`a recursive delete of "${raw}" (${p}): the target ${why}`); refused = true; break; }
          if (['del', 'erase'].includes(verbName) && ctx.underWt(p)) { reasons.push(`\`${verbName} /s\` on "${raw}" in a worktree (it walks into any junction)`); refused = true; break; }
        }
        if (refused) break;
      }
    }
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
  log({ gate: 'delete_guard', decision: 'deny', reason: 'recursive delete near the reference photos refused', agent_id: input.agent_id || '', cwd: input.cwd, command: String(command).slice(0, 300), reasons });
  process.stderr.write('REFUSED by the delete guard (tools/hooks/delete_guard.mjs):\n - ' + reasons.join('\n - ') + '\n' + SCRIPT_HINT + '\n');
  return 2;
}
