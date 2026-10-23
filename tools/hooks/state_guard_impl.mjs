// PreToolUse hook (matcher Read|Write|Edit|NotebookEdit|Grep|Glob|Bash|PowerShell): a SUBAGENT (the event carries an agent_id) may not read the secret, write
// anything under the main checkout's .claude/state, run the scripts that write or acknowledge gate records, or change the main checkout's hook wiring. The main session
// (no agent_id) is not limited.
//
// agent_id on a subagent's tool events (gates5 audit item 23, SETTLED from the Claude Code hooks documentation, code.claude.com/docs/en/hooks, Configuration section): "When a
// subagent calls a tool, tool events such as PreToolUse and PostToolUse fire the same configured hooks as in the main conversation, and the input carries the agent_id and
// agent_type common input fields that identify the subagent." So the guard can tell the two apart. The page does not say the field is absent for the main thread; this guard
// treats "no agent_id" as the main session, so IF a harness ever omitted the field for a subagent the guard would protect nothing: that is the design's one dependency. Records are
// therefore also trusted by who writes them (the hooks), not only by this guard; the HMAC is tamper evidence. A live probe was not needed (documentation is explicit).
//
// WHAT IT JUDGES (main session decision 23 Oct 2026): ACTUAL TARGETS, never words. A word in a commit message, an echo, a grep pattern or a heredoc body is text and is ignored.
// Always allowed for every agent:
//   - .claude/state/unreal_lane.lock: create, write, read, remove (the single Unreal lane), by any tool;
//   - .claude/state/gate.log and .claude/state/handin/**: read by any tool (gate.log is never written; handin/ holds refusal counts and HANDIN_FAILED.txt, written by hooks only);
//   - NAMES of the state folder: ls, dir, Get-ChildItem, tree, find without -exec, test -f (names only, nothing of the content).
// Refused: any other path under .claude/state (records, ref_log, ack_secret), a wildcard into it, a bare ack_secret argument, a redirect into it, a recursive read (grep -r, rg
// --hidden/-uu, find -exec cat, Get-ChildItem -Recurse | Select-String, find | xargs cat) rooted at .claude, the main checkout or above unless it excludes the state folder or its
// name/include filter cannot match ack_secret, a listing of the state folder piped into a content reader, an actual run (any position after node/npx/bash ...) of tools/hooks/gate_ack.mjs,
// tools/ref_seed.mjs or tools/hooks/audit_lib.mjs (also by wildcard, gate_a*.mjs), a copy or move of those files (copy-then-run), inline node/python code that names the record
// writers or the state, and a Write/Edit/redirect/sed -i/cp/mv/rm onto the MAIN checkout's .claude/settings*.json or tools/hooks/* (builders edit their own worktree copies; only the
// main session changes the main checkout's wiring).
//
// WHAT IT CANNOT SEE: a script file that touches the state indirectly (the command only says `node x.mjs`), a path built from pieces or variables, a state path with a space in it,
// a tool other than the ones matched, a read loop that cats names from a listing (`find ... | while read f; do cat "$f"; done`), a copy of the hook files made by a tool this hook does not
// match, a settings change made by a program. It reads tool input text (and the cwd of the event) and nothing else. FAIL OPEN (logged) on any internal error.
import fs from 'node:fs';
import path from 'node:path';
import { ROOT, log, readStdin, parseJson } from './lib.mjs';
import { parseCommand, verbOf } from './shell_lex.mjs';

const norm = (p) => String(p || '').replace(/\\/g, '/').replace(/\/+/g, '/').toLowerCase();
const winPath = (p) => (/^\/([a-zA-Z])(\/|$)/.test(p) ? p[1] + ':' + p.slice(2) : p);
const ALLOWED_ANY = new Set(['unreal_lane.lock']);
const WRITE_VERBS = new Set(['rm', 'del', 'erase', 'rmdir', 'rd', 'remove-item', 'ri', 'mv', 'move', 'move-item', 'mi', 'cp', 'copy', 'copy-item', 'cpi', 'tee', 'tee-object', 'set-content', 'add-content', 'out-file', 'new-item', 'ni', 'touch', 'mkdir', 'md', 'sc', 'ac', 'clear-content', 'rename-item', 'rni', 'ren', 'truncate', 'dd', 'install', 'ln', 'set-itemproperty']);
const TEXT_VERBS = new Set(['echo', 'printf', 'write-host', 'write-output', 'write-error', 'write-warning', 'write-verbose', 'git', 'gh', 'cmd', 'set', 'export']);
const PATTERN_VERBS = new Set(['grep', 'egrep', 'fgrep', 'rg', 'ag', 'select-string', 'sls', 'sed', 'awk', 'gawk', 'findstr']);
const GREPS = new Set(['grep', 'egrep', 'fgrep']);
const RIPGREPS = new Set(['rg', 'ag']);
const LIST_VERBS = new Set(['ls', 'dir', 'get-childitem', 'gci', 'tree', 'stat', 'test', '[', '[[', 'realpath', 'readlink', 'du', 'wc', 'file', 'get-item', 'gi', 'test-path', 'find']);
const READERS = new Set(['cat', 'type', 'head', 'tail', 'less', 'more', 'strings', 'xxd', 'od', 'base64', 'sed', 'awk', 'gawk', 'grep', 'egrep', 'fgrep', 'rg', 'ag', 'bat', 'jq', 'findstr', 'select-string', 'sls', 'get-content', 'gc', 'cp', 'copy', 'copy-item', 'cpi', 'mv', 'move', 'tee']);
const PIPE_READERS = new Set(['get-content', 'gc', 'select-string', 'sls', 'type']);   // read the FILES a piped listing names (bash cat/head read stdin text instead)
const RUNNERS = new Set(['node', 'nodejs', 'bun', 'deno', 'npx', 'tsx', 'sh', 'bash', 'zsh', 'pwsh', 'powershell', 'python', 'python3', 'py']);
const INLINE = new Set(['node', 'nodejs', 'python', 'python3', 'py', 'deno', 'bun']);
const SHELLS = new Set(['bash', 'sh', 'zsh', 'pwsh', 'powershell', 'cmd']);
const COPY_VERBS = new Set(['cp', 'copy', 'copy-item', 'cpi', 'mv', 'move', 'move-item', 'mi', 'ln', 'install', 'rsync']);
const WRITER_WORDS = /\b(?:recordAudit|recordBuilder|signRecord|verifyRecord|ack_secret|gate_ack|ref_seed|audit_lib)\b/;
const WRITER_FILES = ['gate_ack.mjs', 'ref_seed.mjs', 'audit_lib.mjs'];
const RUN_SCRIPT = /^(?:gate_ack|ref_seed|audit_lib)(?:\.mjs)?$/i;

const globToRe = (g) => new RegExp('^' + String(g).replace(/[.+^${}()|\\]/g, '\\$&').replace(/\*/g, '.*').replace(/\?/g, '.') + '$', 'i');
const hasGlob = (s) => /[*?]|\[[^\]]+\]/.test(s);
// does a glob word (possibly with a directory part) name a writer script?
const globNamesWriter = (w) => { const base = String(w).replace(/^.*[\\/]/, ''); return hasGlob(base) && WRITER_FILES.some((f) => globToRe(base.replace(/\[[^\]]*\]/g, '?')).test(f)); };

// what a path says about the state folder: null (not under it) or the part after .claude/state/ ('' for the folder itself)
export function stateRemainder(p) {
  let n = norm(p);
  if (!n.includes('.claude/state')) return null;
  n = n.replace(/\/\.\//g, '/');
  const i = n.indexOf('.claude/state');
  const before = n.slice(0, i), after = n.slice(i + '.claude/state'.length);
  if (before && !/(^|\/|:)$/.test(before) && !/[=("']$/.test(before)) return null;       // x.claude/state is another name
  if (after && !after.startsWith('/')) return null;                                       // .claude/state-old
  const rest = after.replace(/^\/+/, '');
  const parts = rest.split('/'); const out = [];
  for (const q of parts) { if (q === '..') out.pop(); else if (q && q !== '.') out.push(q); }
  return out.join('/');
}

const absOf = (p, cwd) => { const w = winPath(String(p).replace(/\\/g, '/')); return path.resolve(/^[A-Za-z]:|^\//.test(w) ? w : path.join(cwd || process.cwd(), w)); };
// the path is the state folder, inside it, or a folder that contains it (.claude, the main checkout, its parents)
function reachesState(p, cwd, mainRoot) {
  const c = norm(absOf(p, cwd)).replace(/\/$/, '');
  const stateAbs = norm(path.join(mainRoot, '.claude', 'state'));
  if (stateAbs === c || stateAbs.startsWith(c + '/') || c.startsWith(stateAbs + '/')) return true;
  try { if (fs.existsSync(path.join(absOf(p, cwd), '.claude', 'state'))) return true; } catch { /* no */ }
  return /(^|\/)\.claude$/.test(c);
}
// a name/include filter that cannot match ack_secret, or an exclusion of the state folder
function excludesState(args) {
  const a = args.map((x) => x.v !== undefined ? x.v : x);
  for (let i = 0; i < a.length; i++) {
    const v = a[i]; const nx = a[i + 1] || '';
    if (/^--exclude-dir(?:=|$)/.test(v) && /state/i.test(v.includes('=') ? v : nx)) return true;
    if (/^(?:-g|--glob)$/.test(v) && /^!.*state/.test(nx)) return true;
    if (/^--glob=!.*state/.test(v)) return true;
    if (/^(?:-not|!)$/.test(v) && /^-path$/.test(nx) && /state/i.test(a[i + 2] || '')) return true;
    if (/^-prune$/.test(v)) return true;
  }
  const filters = [];
  for (let i = 0; i < a.length; i++) {
    const v = a[i];
    let m = v.match(/^--include=(.+)$/); if (m) { filters.push(m[1]); continue; }
    if (/^(?:--include|-name|-iname|-Filter|-Include|-g|--glob)$/i.test(v) && a[i + 1] && !/^!/.test(a[i + 1])) { filters.push(a[i + 1]); i++; }
  }
  if (filters.length && !filters.some((f) => f.split(',').some((g) => globToRe(g.replace(/^\*\*\//, '')).test('ack_secret')))) return true;
  return false;
}

// one path of a tool or command: [] when fine, else a reason. `write` = the path is written, moved or removed.
function judgePath(raw, { write = false, what = 'path', names = false } = {}) {
  const r = stateRemainder(raw);
  if (r === null) return /(^|[\\/])ack_secret$/i.test(String(raw)) && !write && !names ? [`${what} ${raw}: the ack secret is never read by an agent`] : [];
  if (names && !write) return [];
  if (r === 'handin' || r.startsWith('handin/')) return write ? [`${what} ${raw}: .claude/state/handin is written by the hooks only (reading it is allowed: refusal counts and HANDIN_FAILED.txt)`] : [];
  if (/[*?]/.test(r)) {
    // gates7 item 4: a READ through a wildcard whose last part cannot match ack_secret (state/*.log) is fine; a write, or a pattern that can match the secret, is not
    const parts = r.split('/'); const last = parts[parts.length - 1];
    const safeRead = !write && !names && (parts.length === 1 || parts[0] === 'handin') && !globToRe(last.replace(/\[[^\]]*\]/g, '?')).test('ack_secret');
    if (!safeRead) return [`${what} ${raw}: a wildcard into .claude/state (it could match the secret)`];
    return [];
  }
  if (ALLOWED_ANY.has(r)) return [];
  if (r === 'gate.log') return write ? [`${what} ${raw}: gate.log is written by the hooks only (reading it is allowed)`] : [];
  return [`${what} ${raw}: the gate records under .claude/state are written by the hooks only (allowed: reading gate.log and handin/, names of the folder, using unreal_lane.lock; the main session reads other records)`];
}

// the MAIN checkout's hook wiring: .claude/settings*.json and tools/hooks/*
export function protectedMain(p, mainRoot, cwd) {
  const n = norm(absOf(p, cwd)); const m = norm(mainRoot).replace(/\/$/, '');
  if (n.startsWith(m + '/tools/hooks/')) return true;
  return /^\.claude\/settings[^/]*\.json$/.test(n.startsWith(m + '/') ? n.slice(m.length + 1) : '');
}
const protectedMsg = (p, what) => `${what} ${p}: the main checkout's hook wiring (.claude/settings*.json, tools/hooks/*) is changed by the main session only; edit your own worktree's copy`;

export function judgeCommand(cmd, depth = 0, opts = {}) {
  const mainRoot = opts.mainRoot || ROOT;
  let cur = opts.cwd || process.cwd();
  const reasons = [];
  if (depth > 4) return reasons;
  const { segments } = parseCommand(cmd);
  const lastOf = (i) => { let j = i; while (j > 0 && segments[j].piped) j--; return j; };
  for (let si = 0; si < segments.length; si++) {
    const seg = segments[si];
    for (const rd of seg.redirects) {
      reasons.push(...judgePath(rd.target, { write: true, what: 'a redirect into' }));
      if (protectedMain(rd.target, mainRoot, cur)) reasons.push(protectedMsg(rd.target, 'a redirect into'));
    }
    if (!seg.words.length) continue;
    const { verb, index, args } = verbOf(seg.words);
    const av = args.map((a) => a.v);
    // directory changes (relative arguments after a cd are judged against the new folder)
    if (['cd', 'chdir', 'set-location', 'sl', 'pushd', 'push-location'].includes(verb)) { const tgt = av.filter((x) => !/^-/.test(x))[0]; if (tgt) cur = absOf(tgt, cur); continue; }
    // nested shells, eval and inline code
    if (SHELLS.has(verb) || verb === 'eval') {
      const joined = av.join(' ');
      const m = verb === 'eval' ? [null, joined] : joined.match(/(?:^|\s)(?:-[a-z]*c|-command|\/{1,2}[ck])\s+([\s\S]*)$/i);
      if (m && m[1]) reasons.push(...judgeCommand(m[1], depth + 1, { mainRoot, cwd: cur }));
      if (seg.here) reasons.push(...judgeCommand(seg.here, depth + 1, { mainRoot, cwd: cur }));
    }
    let inlineCodeArg = null;
    if (INLINE.has(verb)) {
      const k = av.findIndex((a) => /^(?:-e|--eval|-p|--print|-c)$/.test(a));
      if (k >= 0 && av[k + 1]) {
        const code = av[k + 1];
        inlineCodeArg = args[k + 1];     // judged here as code; the generic path loop below must not read it as a path
        if (WRITER_WORDS.test(code)) reasons.push(`inline ${verb} code that names the record writers or the secret (${(code.match(WRITER_WORDS) || [''])[0]})`);
        else {
          // gates7 item 4: inline code that only READS (no write, append, delete, rename, copy call in it) may name gate.log and handin/; the secret and every other record stay refused
          const writes = /write|append|unlink|\brm(?:dir|Sync)?\b|rename|copyFile|truncate|createWriteStream|shutil|os\.remove|os\.rename|\bopen\s*\([^)]*,\s*['"][^'"]*[wax+]|\.dump\s*\(|\bmkdir/i.test(code);
          const m = code.match(/[^\s"'`]*\.claude[\\/]+state[^\s"'`]*/gi);
          for (const x of m || []) { if (!writes && stateRemainder(x) === '') continue; reasons.push(...judgePath(x, { write: writes, what: 'inline code touching' })); }
        }
      }
    }
    const hasExec = verb === 'find' && av.some((a) => /^-(?:exec|execdir|ok|okdir|fprint|fprintf|fls)$/.test(a));
    const isList = LIST_VERBS.has(verb) && !hasExec;
    // ---- recursive and piped reads that reach the secret from above
    const pathArgs = (skipPattern) => { const out = []; let patternSeen = !skipPattern; for (let a = 0; a < av.length; a++) { const v = av[a]; if (/^-(?:e|f|g|t|m|A|B|C|-glob|-include|-exclude-dir|-exclude|-type|-iglob)$/i.test(v) && skipPattern) { a++; if (/^-e$/.test(v)) patternSeen = true; continue; } if (/^-/.test(v)) continue; if (!patternSeen) { patternSeen = true; continue; } out.push(v); } return out; };
    if (GREPS.has(verb)) {
      const rec = av.some((a) => /^-[a-zA-Z]*[rR][a-zA-Z]*$/.test(a) || /^--(?:recursive|dereference-recursive)$/.test(a));
      if (rec && !excludesState(av)) { const roots = pathArgs(true); for (const p of (roots.length ? roots : ['.'])) if (stateRemainder(p) === null && reachesState(p, cur, mainRoot)) { reasons.push(`a recursive ${verb} rooted at ${p}: it walks into .claude/state and reads the ack secret (exclude it with --exclude-dir=state, or limit the files with --include=*.ext)`); break; } }
    }
    if (RIPGREPS.has(verb) && av.some((a) => /^(?:--hidden|-uu+|-\.)$/.test(a) || /^-[a-zA-Z]*u{2,}[a-zA-Z]*$/.test(a))) {
      if (!excludesState(av)) { const roots = pathArgs(true); for (const p of (roots.length ? roots : ['.'])) if (stateRemainder(p) === null && reachesState(p, cur, mainRoot)) { reasons.push(`${verb} with --hidden or -uu rooted at ${p}: it reads .claude/state (the ack secret)`); break; } }
    }
    if (verb === 'find' && hasExec) {
      const lead = []; for (const a of av) { if (/^[-(!]/.test(a)) break; lead.push(a); }
      const ei = av.findIndex((a) => /^-(?:exec|execdir|ok|okdir)$/.test(a)); const target = (av[ei + 1] || '').replace(/^.*[\\/]/, '').toLowerCase();
      if ((READERS.has(target) || /^-f(?:print|ls)/.test(av[ei] || '')) && !excludesState(av)) for (const p of (lead.length ? lead : ['.'])) if (reachesState(p, cur, mainRoot)) { reasons.push(`find ${p} -exec ${target}: it reads every file below, the ack secret among them (limit it with -name "*.ext" or exclude state)`); break; }
    }
    // a listing (names) piped into a reader of the files it names
    // gates7 item 9: the body of `... | ForEach-Object { Get-Content $_.FullName }` (the lexer marks it seg.fe) reads the files the listing names, with cat or type as well
    const isPipeReader = (verb === 'xargs' && av.some((a) => READERS.has(a.toLowerCase().replace(/^.*[\\/]/, '')))) || PIPE_READERS.has(verb) || (!!seg.fe && READERS.has(verb));
    if (seg.piped && isPipeReader) {
      const j = lastOf(si);
      for (let k = j; k < si; k++) {
        const ov = verbOf(segments[k].words); const oa = ov.args.map((a) => a.v);
        if (!LIST_VERBS.has(ov.verb) || ov.verb === 'test') continue;
        const roots = oa.filter((x) => !/^-/.test(x)); const rec = ov.verb !== 'ls' && ov.verb !== 'dir' || oa.some((a) => /^-[a-zA-Z]*R/.test(a));
        for (const p of (roots.length ? roots : ['.'])) if (reachesState(p, cur, mainRoot) && (stateRemainder(p) !== null || rec) && !excludesState(oa)) { reasons.push(`a listing of ${p} piped into a reader (${verb}): it reads the files named, the ack secret among them (limit the listing with -Filter/-name, or exclude state)`); break; }
      }
    }
    // ---- scripts that write records, copies of them, and the main checkout's hook wiring
    for (const a of args) {
      const v = a.v;
      if (!/^-/.test(v) && (RUN_SCRIPT.test(v.replace(/^.*[\\/]/, '')) || globNamesWriter(v)) && RUNNERS.has(verb) && !av.some((x) => /^(?:--check|-c)$/.test(x))) {
        reasons.push(`running ${v.replace(/^.*[\\/]/, '')}: an agent does not write or acknowledge gate records or the reference register`);
      }
      if (COPY_VERBS.has(verb) && !/^-/.test(v) && (RUN_SCRIPT.test(v.replace(/^.*[\\/]/, '')) || globNamesWriter(v)) && av.indexOf(v) < av.length - 1) {
        reasons.push(`${verb} of ${v.replace(/^.*[\\/]/, '')}: a copy of a record-writing script would run the same code (an agent does not write gate records)`);
      }
    }
    let list = args;
    if (TEXT_VERBS.has(verb)) list = [];
    else if ((verb === 'select-string' || verb === 'sls')) {
      // gates7 item 9: Select-String [-Pattern] p -Path x : the value of -Path / -LiteralPath is a path to judge, not the pattern; positionals after the pattern are paths
      const out = []; const patGiven = args.some((a) => /^-patt(?:e(?:r(?:n)?)?)?$/i.test(a.v)); let patternSeen = patGiven;
      for (let a = 0; a < list.length; a++) {
        const v = list[a].v;
        if (/^-patt(?:e(?:r(?:n)?)?)?$/i.test(v)) { a++; continue; }
        if (/^-(?:path|literalpath|pa|lit\w*)$/i.test(v)) { if (list[a + 1]) out.push(list[++a]); continue; }
        if (/^-(?:context|encoding|include|exclude|culture|inputobject|input\w*|enc\w*|inc\w*|exc\w*)$/i.test(v)) { a++; continue; }
        if (/^-/.test(v)) continue;
        if (!patternSeen) { patternSeen = true; continue; }
        out.push(list[a]);
      }
      list = out;
    }
    else if (PATTERN_VERBS.has(verb)) {
      const out = []; let patternSeen = false;
      for (let a = 0; a < list.length; a++) {
        const v = list[a].v;
        if (/^-(?:e|f|pattern|regexp)$/i.test(v)) { a++; patternSeen = true; continue; }
        if (/^-/.test(v)) continue;
        if (!patternSeen) { patternSeen = true; continue; }
        out.push(list[a]);
      }
      list = out;
    }
    const writeVerb = WRITE_VERBS.has(verb) || (verb === 'sed' && args.some((a) => /^-i/.test(a.v)));
    // gates7 item 4: cp / copy / Copy-Item READ their sources and write only the destination (the last path, or -Destination): copying gate.log out to analyse it is fine
    const copyOnly = ['cp', 'copy', 'copy-item', 'cpi'].includes(verb);
    const destArgs = new Set();
    if (copyOnly) {
      const posArgs = list.filter((x) => !/^-/.test(x.v) && !/\s/.test(x.v));
      const di = list.findIndex((x, ix) => /^-dest/i.test(x.v) && list[ix + 1]);
      if (di >= 0) destArgs.add(list[di + 1]); else if (posArgs.length >= 2) destArgs.add(posArgs[posArgs.length - 1]); else if (posArgs.length === 1) destArgs.add(posArgs[0]);
    }
    let firstNonFlag = true;
    for (const a of list) {
      const v = a.v;
      if (a === inlineCodeArg) continue;
      const wr = copyOnly ? destArgs.has(a) : writeVerb;
      if (/\s/.test(v) && !/^[A-Za-z]:[\\/]\S*$/.test(v)) continue;     // text with spaces: a message, a script body
      if (!/^-/.test(v)) firstNonFlag = false;
      const vv = /^-\w+:/.test(v) ? v.replace(/^-\w+:/, '') : v;   // -Path:x
      if (/^-/.test(vv)) continue;
      for (const piece of vv.split(',')) {
        reasons.push(...judgePath(piece, { write: wr, names: isList }));
        // relative to a folder entered with cd earlier in the command, or an event cwd inside the state folder
        if (!/^(?:[A-Za-z]:|[\\/])/.test(piece) && (cur !== (opts.cwd || process.cwd()) || stateRemainder(cur) !== null)) reasons.push(...judgePath(absOf(piece, cur), { write: wr, names: isList }));
        if (wr && protectedMain(piece, mainRoot, cur)) reasons.push(protectedMsg(piece, verb));
      }
    }
    void firstNonFlag;
    // the verb itself may be the script (./tools/ref_seed.mjs) or a path inside the state folder
    const vraw = seg.words[index] ? seg.words[index].v : '';
    if (RUN_SCRIPT.test(vraw.replace(/^.*[\\/]/, ''))) reasons.push(`running ${vraw.replace(/^.*[\\/]/, '')}: an agent does not write or acknowledge gate records or the reference register`);
    else if (vraw && !/\s/.test(vraw)) reasons.push(...judgePath(vraw, { write: true, what: 'the command' }));
  }
  return [...new Set(reasons)];
}

export function judge(input, mainRoot = ROOT) {
  const tool = input.tool_name || '';
  const ti = input.tool_input || {};
  if (!input.agent_id) return [];   // the main session
  if (tool === 'Read') return judgePath(ti.file_path || ti.path || '', { what: 'Read of' });
  if (tool === 'Write' || tool === 'Edit' || tool === 'NotebookEdit') {
    const p = ti.file_path || ti.notebook_path || ti.path || '';
    const rs = judgePath(p, { write: true, what: tool + ' on' });
    if (protectedMain(p, mainRoot, input.cwd)) rs.push(protectedMsg(p, tool + ' on'));
    return rs;
  }
  if (tool === 'Glob') return [];     // names only: never file content
  if (tool === 'Grep') {
    const reasons = [];
    const p = ti.path || '';
    const r = p ? stateRemainder(p) : null;
    if (r !== null) {
      const g = String(ti.glob || '');
      const ok = ALLOWED_ANY.has(r) || r === 'gate.log' || r === 'handin' || r.startsWith('handin/') || (r === '' && /^(?:\*\*\/)?(?:gate\.log|unreal_lane\.lock)$/.test(g));
      if (!ok) reasons.push(`Grep over ${p}: the state folder holds the ack secret (a Grep of gate.log, handin/ or unreal_lane.lock is allowed)`);
    }
    if (ti.glob && /ack_secret/i.test(String(ti.glob))) reasons.push('Grep with a glob that selects the ack secret');
    return reasons;
  }
  if (/^(Bash|PowerShell)$/.test(tool)) {
    const cwd = input.cwd || process.cwd();
    const rs = judgeCommand(String(ti.command || ''), 0, { mainRoot, cwd });
    return rs;
  }
  return [];
}

export async function main() {
  const raw = await readStdin();
  const input = raw === null ? undefined : parseJson(raw);
  if (!input || typeof input !== 'object') { log({ gate: 'state_guard', decision: 'allow', reason: 'FAIL-OPEN: stdin missing or not JSON' }); return 0; }
  const reasons = judge(input);
  if (!reasons.length) return 0;
  log({ gate: 'state_guard', decision: 'deny', reason: 'agent touched the gate state', agent_id: input.agent_id, tool: input.tool_name, reasons });
  process.stderr.write('REFUSED by the state guard (tools/hooks/state_guard.mjs):\n - ' + reasons.join('\n - ') + '\nAllowed: reading .claude/state/gate.log and .claude/state/handin/ with any tool, listing the folder, using .claude/state/unreal_lane.lock. If this blocks work you were told to do, say so in your report (BLOCKED line); do not work around it.\n');
  return 2;
}
