// PreToolUse hook (matcher Write|Edit|NotebookEdit|Bash|PowerShell): LOCKED TESTS (main session decision, 23 Oct 2026). A test written by the auditor may not be weakened by the builder
// whose work it judges.
//
// LOCKED = any file under a folder `tests/locked/` (in any repository or worktree), and any file listed (one path per line, `#` comments; relative to the repository root or absolute) in a
// `LOCKED_TESTS.txt` in the hand-in folder of the PRIOR round (the folder of the brief's `PRIOR-AUDIT:` file).
// WHO IS LIMITED: a BUILDER subagent, i.e. an agent whose launch carried a HANDIN_DIR (read from its own transcript or the BRIEF-FILE it names; an `AUDIT-OF:` launch is an auditor). Auditors
// (NO-CONTRACT launches), agents with no HANDIN_DIR and the main session (no agent_id) may write locked tests: that is how they get there.
// WHAT IS REFUSED: Write / Edit / NotebookEdit of a locked file, and in Bash / PowerShell any write, delete or rename of one: redirects (> >>), rm / del / Remove-Item / unlink, mv / move /
// Move-Item / ren / Rename-Item (source and destination), cp / Copy-Item / robocopy-free copies ONTO it, tee, touch, truncate, Set-Content / Add-Content / Clear-Content / Out-File,
// sed -i / perl -i, git rm, git mv, git checkout -- <file>, git restore <file>, a delete or move of a folder that holds locked files (`rm -rf tests`), and inline python / node code that names a
// locked path next to a write, delete or rename call.
// WHAT IT CANNOT SEE: a path built from variables or pieces, a script file that edits the test (`node x.mjs`), an editor or build tool that rewrites it, a copy of the test made elsewhere and
// swapped in by a rename of its folder (judged: a rename of any folder above it is refused), a test deleted by `git reset --hard` / `git clean` (those only undo the builder's own edits), and
// anything outside Bash, PowerShell, Write, Edit, NotebookEdit. A builder with no readable transcript is treated as not a builder (nothing to judge by). FAIL OPEN (logged) on any internal error.
import fs from 'node:fs';
import path from 'node:path';
import { log, readStdin, parseJson, launchInfoOfAgent } from './lib.mjs';
import { parseCommand, verbOf, shellInner } from './shell_lex.mjs';

export const LOCK_MESSAGE = 'locked test: written by the auditor; make it pass, or write BLOCKED';
const norm = (p) => String(p).replace(/\\/g, '/').replace(/\/+/g, '/').replace(/\/$/, '').toLowerCase();
const winPath = (p) => (/^\/([a-zA-Z])(\/|$)/.test(p) ? p[1] + ':' + p.slice(2) : p);
const absOf = (p, cwd) => { const w = winPath(String(p).replace(/\\/g, '/')); return path.resolve(/^[A-Za-z]:|^\//.test(w) ? w : path.join(cwd || process.cwd(), w)); };
const hasGlob = (s) => /[*?]|\[[^\]]+\]/.test(s);

// the entries of <prior round hand-in folder>/LOCKED_TESTS.txt
export function lockedList(priorAudit) {
  if (!priorAudit) return [];
  try {
    let dir = path.resolve(String(priorAudit));
    try { if (fs.statSync(dir).isFile()) dir = path.dirname(dir); } catch { if (/\.\w{1,5}$/.test(dir)) dir = path.dirname(dir); }
    const txt = fs.readFileSync(path.join(dir, 'LOCKED_TESTS.txt'), 'utf8');
    return txt.split(/\r?\n/).map((l) => l.replace(/#.*$/, '').trim().replace(/^`|`$/g, '')).filter(Boolean);
  } catch { return []; }
}

// a lock context for one builder: matches an absolute path, or a folder above locked files
export function lockCtx({ handinDir, priorAudit }) {
  const list = lockedList(priorAudit);
  const wtRoot = (() => { const n = String(handinDir || '').replace(/\\/g, '/'); const k = n.toLowerCase().indexOf('/docs/progress/'); return k >= 0 ? n.slice(0, k) : null; })();
  const entries = list.map((e) => { const abs = /^[A-Za-z]:|^[\\/]/.test(e); return { raw: e, abs, n: norm(abs ? winPath(e.replace(/\\/g, '/')) : e), res: !abs && wtRoot ? norm(path.resolve(wtRoot, e)) : null }; });
  const lockedAbs = [...entries.map((e) => (e.abs ? e.n : e.res)).filter(Boolean)];
  if (wtRoot) lockedAbs.push(norm(path.join(wtRoot, 'tests', 'locked')));
  const direct = (p) => {
    const n = norm(p);
    if (/(^|\/)tests\/locked(\/|$)/.test(n)) return 'is under tests/locked';
    for (const e of entries) {
      const t = e.abs ? e.n : null;
      if (t && (n === t || n.startsWith(t + '/'))) return `is listed in LOCKED_TESTS.txt (${e.raw})`;
      if (!e.abs) { if ((e.res && (n === e.res || n.startsWith(e.res + '/'))) || n.endsWith('/' + e.n) || n.includes('/' + e.n + '/')) return `is listed in LOCKED_TESTS.txt (${e.raw})`; }
    }
    return null;
  };
  // a folder above locked files (rm -rf tests, mv tests tests2): a delete or move of it takes them along
  const above = (p) => {
    const n = norm(p);
    if (!n) return null;
    for (const l of lockedAbs) if (l.startsWith(n + '/')) return 'holds locked tests';
    try { if (path.basename(n) === 'tests' && fs.existsSync(path.join(p, 'locked'))) return 'holds tests/locked'; if (fs.existsSync(path.join(p, 'tests', 'locked'))) return 'holds tests/locked'; } catch { /* none */ }
    return null;
  };
  return { list, entries, direct, above };
}

const DEL = new Set(['rm', 'del', 'erase', 'rmdir', 'rd', 'remove-item', 'ri', 'unlink', 'shred', 'truncate']);
const MOVE = new Set(['mv', 'move', 'move-item', 'mi', 'ren', 'rename-item', 'rni']);
const COPY = new Set(['cp', 'copy', 'copy-item', 'cpi', 'install']);
const WRITE = new Set(['tee', 'tee-object', 'touch', 'set-content', 'sc', 'add-content', 'ac', 'clear-content', 'clc', 'out-file', 'new-item', 'ni', 'set-itemproperty', 'dd']);
const SHELLS = new Set(['bash', 'sh', 'zsh', 'pwsh', 'powershell', 'cmd']);
const INLINE = new Set(['python', 'python3', 'py', 'node', 'nodejs', 'perl', 'ruby', 'bun', 'deno']);
const isFlag = (x) => /^-/.test(x) || /^\/{1,2}[a-zA-Z](:\S*)?$/.test(x);

// judge a PATH argument: [] or a reason. `whole`: the operation takes a folder with everything in it (delete, move, rename)
function judgeTarget(raw, cur, lc, verbName, whole) {
  const out = [];
  const piece = String(raw).replace(/^-\w+:/, '');
  if (!piece || /\s/.test(piece) && !/^[A-Za-z]:[\\/]/.test(piece) && !/[\\/]/.test(piece)) return out;   // prose with spaces, not a path
  const globbed = hasGlob(piece);
  const abs = absOf(piece, cur);
  const base = globbed ? (() => { const root = path.parse(abs).root; const keep = []; for (const s of abs.slice(root.length).split(/[\\/]/).filter(Boolean)) { if (hasGlob(s)) break; keep.push(s); } return path.join(root, ...keep); })() : abs;
  const d = lc.direct(abs) || (globbed ? lc.direct(base) : null);
  if (d) { out.push(`${verbName} ${raw}: the path ${d}`); return out; }
  if (whole || globbed) { const a = lc.above(base); if (a) out.push(`${verbName} ${raw}: the folder ${a}`); }
  return out;
}

export function judgeCommand(cmd, cwd, lc, depth = 0) {
  const reasons = [];
  if (depth > 4) return reasons;
  let cur = cwd || process.cwd();
  // PowerShell's own .NET calls: [IO.File]::WriteAllText('tests\locked\a.mjs', ...), [System.IO.Directory]::Delete(...)
  if (/\[(?:System\.)?IO\.(?:File|Directory|Path)\]::\w*(?:Write|Append|Delete|Move|Copy|Replace|Create)/i.test(String(cmd))) {
    const low = String(cmd).toLowerCase().replace(/\\/g, '/');
    const hit = /tests\/locked/.test(low) ? 'tests/locked' : (lc.entries.find((e) => low.includes(e.raw.replace(/\\/g, '/').toLowerCase())) || {}).raw;
    if (hit) reasons.push(`a .NET file call that names a locked test (${hit}) next to a write, delete, move or copy`);
  }
  const stack = [];
  let prevPaths = [], nextPrev = [];
  const { segments } = parseCommand(String(cmd || ''));
  for (const seg of segments) {
    for (const rd of seg.redirects) { if (rd.op === '>' || rd.op === '>>' || rd.op === '&>') reasons.push(...judgeTarget(rd.target, cur, lc, 'a redirect into', false)); }
    if (!seg.words.length) continue;
    const { verb, args, index, line } = verbOf(seg.words);
    const av = args.map((a) => a.v);
    prevPaths = nextPrev; nextPrev = av.filter((x) => !isFlag(x));
    if (line) { reasons.push(...judgeCommand(seg.words.slice(index).map((w) => w.v).join(' '), cur, lc, depth + 1)); continue; }
    if (['cd', 'chdir', 'set-location', 'sl', 'pushd', 'push-location'].includes(verb)) { if (['pushd', 'push-location'].includes(verb)) stack.push(cur); const t = av.filter((x) => !/^-/.test(x) && !/^\/d$/i.test(x))[0]; if (t && !/\$|`|%/.test(t)) cur = absOf(t, cur); continue; }
    if (['popd', 'pop-location'].includes(verb)) { if (stack.length) cur = stack.pop(); continue; }
    if (SHELLS.has(verb) || verb === 'eval') {
      const inner = verb === 'eval' ? av.join(' ') : shellInner(verb, av);
      if (inner) reasons.push(...judgeCommand(inner, cur, lc, depth + 1));
      if (seg.here) reasons.push(...judgeCommand(seg.here, cur, lc, depth + 1));
      continue;
    }
    if (verb === 'xargs' || verb === 'sudo') { const k = av.findIndex((a) => DEL.has(a) || MOVE.has(a)); if (k >= 0) reasons.push(...judgeCommand(av.slice(k).join(' '), cur, lc, depth + 1)); }
    // positional (non-flag) arguments, with -Path / -LiteralPath / -Destination / -Value-less handling
    const pos = []; let named = null;
    for (let k = 0; k < av.length; k++) {
      const a = av[k];
      const m = /^-(path|literalpath|destination|newname|pa|lit\w*)(?::(.*))?$/i.exec(a);
      if (m) { const v = m[2] !== undefined ? m[2] : av[++k]; if (/^dest/i.test(m[1])) named = v; else if (v !== undefined) pos.push(v); continue; }
      if (/^-(?:value|include|exclude|filter|encoding|stream|credential|inputobject)$/i.test(a)) { k++; continue; }
      if (isFlag(a)) continue;
      pos.push(a);
    }
    // a Remove-Item / Move-Item / Set-Content with no path of its own, fed by the segment before it (`Get-ChildItem tests\locked | Remove-Item`): the listed folder is what it acts on
    if ((DEL.has(verb) || MOVE.has(verb) || WRITE.has(verb) || verb === 'clear-content') && seg.piped && !pos.length && prevPaths.length) { for (const p of prevPaths) reasons.push(...judgeTarget(p, cur, lc, verb + ' (fed by a listing of it)', true)); continue; }
    if (DEL.has(verb)) { for (const p of pos) reasons.push(...judgeTarget(p, cur, lc, verb, true)); continue; }
    if (MOVE.has(verb)) { for (const p of pos) reasons.push(...judgeTarget(p, cur, lc, verb, true)); if (named) reasons.push(...judgeTarget(named, cur, lc, verb, false)); continue; }
    if (COPY.has(verb)) { const dest = named || (pos.length >= 2 ? pos[pos.length - 1] : null); if (dest) reasons.push(...judgeTarget(dest, cur, lc, verb, false)); continue; }
    // gates8 self-audit pass 1: other programs that write a destination: robocopy / xcopy (destination = second positional), rsync / ln / scp (last positional)
    if (verb === 'robocopy' || verb === 'xcopy') { const p2 = av.filter((x) => !/^\/[a-zA-Z]+(?::\S*)?$/.test(x) && !/^-/.test(x)); if (p2.length >= 2) reasons.push(...judgeTarget(p2[1], cur, lc, verb, verb === 'robocopy' && av.some((x) => /^\/(?:mir|purge|mov|move)$/i.test(x)))); continue; }
    if (verb === 'rsync' || verb === 'ln' || verb === 'scp') { if (pos.length >= 2) reasons.push(...judgeTarget(pos[pos.length - 1], cur, lc, verb, false)); continue; }
    // find <start> ... -delete / -exec rm|mv|truncate|shred: the start folder is deleted from; git clean <path>: the path is
    if (verb === 'find' && av.some((a) => a === '-delete' || ((a === '-exec' || a === '-execdir' || a === '-ok') && av.some((b) => /^(?:rm|mv|unlink|truncate|shred|sed|tee)$/.test(b))))) { for (const p of av.slice(0, av.findIndex((x) => /^[-!(]/.test(x)) < 0 ? av.length : av.findIndex((x) => /^[-!(]/.test(x)))) reasons.push(...judgeTarget(p, cur, lc, 'find ... -delete/-exec', true)); continue; }
    if (WRITE.has(verb)) { for (const p of pos) reasons.push(...judgeTarget(p, cur, lc, verb, false)); if (verb === 'dd') for (const a of av) { const m = /^of=(.*)$/.exec(a); if (m) reasons.push(...judgeTarget(m[1], cur, lc, verb, false)); } continue; }
    if ((verb === 'sed' || verb === 'perl') && av.some((a) => /^-[a-zA-Z]*i/.test(a) || /^--in-place/.test(a))) { for (const p of pos.slice(1)) reasons.push(...judgeTarget(p, cur, lc, verb + ' -i', false)); continue; }
    if (verb === 'git') {
      let repo = cur; let i = 0;
      while (i < av.length && /^-/.test(av[i])) { if (av[i] === '-C' && av[i + 1] !== undefined) { repo = absOf(av[i + 1], repo); i += 2; continue; } if (av[i] === '-c' || av[i] === '--namespace' || av[i] === '--exec-path') { i += 2; continue; } i++; }
      const sub = String(av[i] || '').toLowerCase(); const rest = av.slice(i + 1);
      const dd = rest.indexOf('--');
      if (sub === 'rm') for (const p of rest.filter((x) => !/^-/.test(x))) reasons.push(...judgeTarget(p, repo, lc, 'git rm', true));
      else if (sub === 'mv') for (const p of rest.filter((x) => !/^-/.test(x))) reasons.push(...judgeTarget(p, repo, lc, 'git mv', true));
      else if (sub === 'checkout' && dd >= 0) for (const p of rest.slice(dd + 1)) { if (p !== '.') reasons.push(...judgeTarget(p, repo, lc, 'git checkout --', false)); }
      else if (sub === 'restore') for (const p of rest.filter((x) => !/^-/.test(x))) { if (p !== '.') reasons.push(...judgeTarget(p, repo, lc, 'git restore', false)); }
      else if (sub === 'clean') for (const p of rest.filter((x) => !/^-/.test(x))) reasons.push(...judgeTarget(p, repo, lc, 'git clean', true));
      else if (sub === 'checkout' && dd < 0) { /* branch switch or `checkout <ref> <file>`: the file form is judged below */ const nf = rest.filter((x) => !/^-/.test(x)); for (const p of nf) reasons.push(...judgeTarget(p, repo, lc, 'git checkout', false)); }
      continue;
    }
    if (INLINE.has(verb)) {
      const k = av.findIndex((a) => /^(?:-e|-c|--eval|-p|-pe|-r)$/.test(a));
      if (k >= 0 && av[k + 1]) {
        const code = av[k + 1];
        const names = (code.match(/[^\s"'`()]*(?:tests[\\/]+locked)[^\s"'`()]*/gi) || []);
        for (const e of lc.entries) if (code.toLowerCase().includes(e.raw.replace(/\\/g, '/').toLowerCase()) || code.toLowerCase().includes(e.raw.replace(/\//g, '\\\\').toLowerCase())) names.push(e.raw);
        const writes = /write|append|unlink|\brm(?:dir|Sync)?\b|rename|copyFile|truncate|createWriteStream|shutil|os\.remove|os\.rename|\bopen\s*\([^)]*,\s*['"][^'"]*[wax+]|\.dump\s*\(|move|replace/i.test(code);
        if (names.length && writes) reasons.push(`inline ${verb} code that names a locked test (${names[0]}) next to a write, delete or rename call`);
      }
    }
  }
  return [...new Set(reasons)];
}

export function judge(input, lc) {
  const tool = input.tool_name || ''; const ti = input.tool_input || {};
  if (tool === 'Write' || tool === 'Edit' || tool === 'NotebookEdit') {
    const p = ti.file_path || ti.notebook_path || ti.path || '';
    if (!p) return [];
    const abs = absOf(p, input.cwd);
    const d = lc.direct(abs);
    return d ? [`${tool} ${p}: the path ${d}`] : [];
  }
  if (/^(Bash|PowerShell)$/.test(tool)) return judgeCommand(String(ti.command || ''), input.cwd, lc);
  return [];
}

export async function main() {
  const raw = await readStdin();
  const input = raw === null ? undefined : parseJson(raw);
  if (!input || typeof input !== 'object') { log({ gate: 'lock_guard', decision: 'allow', reason: 'FAIL-OPEN: stdin missing or not JSON' }); return 0; }
  if (!/^(Write|Edit|NotebookEdit|Bash|PowerShell)$/.test(input.tool_name || '')) return 0;
  if (!input.agent_id) return 0;     // the main session
  const info = launchInfoOfAgent(input);
  if (!info || !info.handinDir || info.auditor) return 0;     // an auditor, a clerk or an agent whose launch cannot be read: not a builder
  const lc = lockCtx(info);
  const reasons = judge(input, lc);
  if (!reasons.length) return 0;
  log({ gate: 'lock_guard', decision: 'deny', reason: 'builder touched a locked test', agent_id: input.agent_id, tool: input.tool_name, command: String((input.tool_input && (input.tool_input.command || input.tool_input.file_path)) || '').slice(0, 300), reasons });
  process.stderr.write(`REFUSED by the lock guard (tools/hooks/lock_guard.mjs): ${LOCK_MESSAGE}\n - ${reasons.join('\n - ')}\nA test under tests/locked/, or listed in LOCKED_TESTS.txt of the prior round's hand-in folder, was written by the auditor to judge your work. Do not edit, delete or rename it: fix the code until it passes. If it cannot pass, write a BLOCKED line in STATUS.md (BLOCKED: <item> | tried: | blocked by: | needs:).\n`);
  return 2;
}
