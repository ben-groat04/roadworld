// PreToolUse hook (matcher Bash|PowerShell): refuses recursive deletes that would follow a worktree's docs\reference junction into the private reference photos.
// Why: on 22 Oct 2026 a recursive delete of a worktree followed that junction and wiped every reference photo. Applies to the main session and to every subagent.
//
// A command is refused when it does a recursive delete (rm -r/-rf/-R/--recursive, Remove-Item/ri/del/erase/rd/rmdir with -Recurse or /s, find -delete, rmtree,
// fs.rmSync recursive, rimraf, Directory.Delete(.., true), git clean with x/X) and
//   - a target path contains docs/reference, or
//   - a target is a worktree root, the roadworld-wt folder itself, or the main checkout root / its docs folder, or
//   - a target lies under roadworld-wt\* or roadworld-wt-* AND (on disk) something below it is a junction or symlink (walked WITHOUT following links), or the target
//     holds a wildcard or a shell variable that cannot be resolved here;
// or the command is `git worktree remove|prune` without tools\remove_agent_worktree.ps1, or robocopy /MIR | /PURGE with docs/reference or a roadworld-wt path in it.
// Deleting build\, node_modules or a scratch folder with no link inside stays allowed (agents do clean builds).
// WHAT IT CANNOT SEE: a delete done inside a script file the command only runs (node x.mjs), a path assembled from variables in several steps, a delete started by
// a tool other than Bash/PowerShell. It reads the command text and the disk, nothing else. FAIL OPEN (logged) on any internal error.
import fs from 'node:fs';
import path from 'node:path';
import { ROOT, log, readStdin, parseJson } from './lib.mjs';

export const SCRIPT_HINT = 'Remove a worktree only with: powershell -NoProfile -ExecutionPolicy Bypass -File tools\\remove_agent_worktree.ps1 <name> (it unlinks the docs\\reference junction first and checks the photos are still there). docs\\reference in every worktree is a junction to the private reference photos: a recursive delete follows it and wipes them (22 Oct 2026).';

const norm = (p) => String(p).replace(/\\/g, '/').replace(/\/+/g, '/').toLowerCase();
const unq = (s) => String(s).replace(/^(['"])([\s\S]*)\1$/, '$2');
// bash-on-Windows paths: /c/Users/x -> c:/users/x
const winPath = (p) => (/^\/([a-zA-Z])\//.test(p) ? p[1] + ':' + p.slice(2) : p);

function tokens(seg) {
  const out = []; const re = /"([^"]*)"|'([^']*)'|(\S+)/g; let m;
  while ((m = re.exec(seg))) out.push(m[1] ?? m[2] ?? m[3]);
  return out;
}
function segments(cmd) { return cmd.split(/&&|\|\||;|\r?\n|\|/).map((s) => s.trim()).filter(Boolean); }

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
        // Windows junctions report isDirectory() through readdir; lstat + realpath tell them apart from a real folder
        try { const rp = fs.realpathSync(f); if (norm(rp) !== norm(path.resolve(f))) { found.push(f); continue; } } catch { /* unreadable: ignore */ }
        walk(f);
      }
    }
  };
  let st; try { st = fs.lstatSync(p); } catch { return []; }
  if (st.isSymbolicLink()) return [p];
  try { const rp = fs.realpathSync(p); if (norm(rp) !== norm(path.resolve(p))) { /* the target itself reached through a link higher up: judged by its text */ } } catch { /* ignore */ }
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

const protectedWtRe = /(^|\/)roadworld-wt(-[^/]+)?(\/|$)/;
function underWorktree(p) { return protectedWtRe.test(norm(p)); }
function isWtRootOrAbove(p) {
  const n = norm(p).replace(/\/+$/, '');
  return /(^|\/)roadworld-wt$/.test(n) || /(^|\/)roadworld-wt\/[^/]+$/.test(n) || /(^|\/)roadworld-wt-[^/]+$/.test(n) || /(^|\/)roadworld-wt-?[^/]*\/\.\.?$/.test(n);
}

// returns [] or an array of refusal reasons
export function analyse(command, cwd = process.cwd(), depth = 0) {
  const reasons = [];
  const cmd = String(command || '');
  if (depth > 3) return reasons;
  // nested payloads: bash -c "..." / powershell -Command "..." / cmd /c "..."
  for (const m of cmd.matchAll(/(?:\s-c|\s-command|\s\/c)\s+(["'])([\s\S]*?)\1/gi)) reasons.push(...analyse(m[2], cwd, depth + 1));
  const ncmd = norm(cmd);
  const mainRoot = norm(ROOT).replace(/\/+$/, '');
  const usesScript = /remove_agent_worktree/i.test(cmd);
  if (/\bgit(?:\.exe)?\s+(?:-C\s+\S+\s+)?worktree\s+(?:remove|prune)\b/i.test(cmd) && !usesScript) reasons.push('`git worktree remove|prune` outside tools\\remove_agent_worktree.ps1');
  if (/\brobocopy\b/i.test(cmd) && /\/(?:mir|purge)\b/i.test(cmd) && (/docs\/reference/.test(ncmd) || /roadworld-wt/.test(ncmd))) reasons.push('robocopy /MIR or /PURGE with docs\\reference or a worktree path in the command (it deletes in the destination)');
  for (const seg of segments(cmd)) {
    const tk = tokens(seg);
    let i = 0; while (i < tk.length && /^(?:&|call|sudo|command|time|cmd(?:\.exe)?|\/c|powershell(?:\.exe)?|pwsh|bash|sh|-noprofile|-nologo|-executionpolicy|bypass)$/i.test(tk[i])) i++;
    const verb = (tk[i] || '').toLowerCase().replace(/^.*[\\/]/, '').replace(/\.exe$/, '');
    const rest = tk.slice(i + 1);
    const isFlag = (x) => /^-/.test(x) || /^\/[a-zA-Z]$/.test(x);
    const flags = rest.filter(isFlag), targets = rest.filter((x) => !isFlag(x)).map(unq).filter(Boolean);
    const psRecurse = (f) => /^-r(?:e(?:c(?:u(?:r(?:s(?:e)?)?)?)?)?)?$/i.test(f);
    let recursive = false;
    if (verb === 'rm') recursive = flags.some((f) => psRecurse(f) || /^--recursive$/i.test(f) || (/^-[rRfivId]{1,5}$/.test(f) && /[rR]/.test(f)));
    else if (['rmdir', 'rd', 'del', 'erase'].includes(verb)) recursive = flags.some((f) => /^\/s$/i.test(f) || psRecurse(f));
    else if (verb === 'remove-item' || verb === 'ri') recursive = flags.some(psRecurse);
    else if (verb === 'find' && rest.includes('-delete')) recursive = true;
    else if (verb === 'git' && /^clean$/i.test(rest[0] || '') && flags.some((f) => /^-[a-z]*[xX][a-z]*$/.test(f))) { reasons.push('`git clean` with -x/-X removes ignored files such as the docs\\reference junction'); continue; }
    if (!recursive) continue;
    const pipedIn = /\|/.test(cmd) && !targets.length;
    if (pipedIn && (underWorktree(cwd) || /roadworld/.test(ncmd))) reasons.push('a recursive delete fed by a pipeline (its targets cannot be checked) near a worktree');
    for (const raw of targets) {
      let t = winPath(raw.replace(/^~(?=\/|\\|$)/, process.env.USERPROFILE || process.env.HOME || '~'));
      if (/[$%`]|\$\(/.test(t)) { if (underWorktree(cwd) || /roadworld/.test(ncmd) || /docs[\\/]+reference/i.test(cmd)) reasons.push(`a recursive delete of "${raw}" (a variable: it cannot be checked) near a worktree`); continue; }
      const abs = path.resolve(/^[a-zA-Z]:|^\//.test(t) ? t : path.join(cwd, t));
      const na = norm(abs);
      if (/(^|\/)docs\/reference(\/|$)/.test(na) || /docs\/reference/.test(norm(t))) { reasons.push(`a recursive delete of "${raw}", which is or contains docs\\reference`); continue; }
      if (isWtRootOrAbove(abs)) { reasons.push(`a recursive delete of "${raw}": a worktree root or the roadworld-wt folder (every worktree holds the docs\\reference junction)`); continue; }
      if (na === mainRoot || na === mainRoot + '/docs') { reasons.push(`a recursive delete of "${raw}": the main checkout root or its docs folder (holds the private reference photos)`); continue; }
      if (underWorktree(abs) || underWorktree(cwd)) {
        const list = expandWild(abs);
        if (/[*?]/.test(path.basename(abs)) && !list.length) continue;
        for (const p of list) {
          const links = linksBelow(p);
          if (links.length) { reasons.push(`a recursive delete of "${raw}" which holds ${links.length} junction/symlink(s) (${links.slice(0, 2).join(', ')}): a recursive delete follows them into their targets`); break; }
        }
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
