// PreToolUse hook (matcher Write|Edit|NotebookEdit|Bash|PowerShell): LOCKED TESTS (main session decision, 23 Oct 2026). A test written by the auditor may not be weakened by the builder
// whose work it judges.
//
// LOCKED = every file listed in `LOCKED_TESTS.txt` in the hand-in folder of the PRIOR round (the folder of the brief's `PRIOR-AUDIT:` file), and every file under a folder
// `tests/locked/` of the builder's worktree that exists on disk. The list: one path or glob per line (`*`, `?`, `**`), `#` comments, relative to the worktree root or absolute; UTF-8 or UTF-16 (BOM).
// PROTECTED the same way (gates9, audit H1): the list file itself and the prior round's AUDIT.md: a builder may not write, delete, move or truncate them, nor delete or move their folder.
// A list that cannot be used (an entry that matches no file, a list with no entry, a missing list that the audit speaks of, text that cannot be decoded) is logged ONCE per agent as
// `LOCK-LIST-BAD ... FAIL-OPEN` (gate notice for the main session) and shown to the builder as a warning: it never fails silently.
// NO LIST AND NO tests/locked FOLDER MEANS NOTHING IS PROTECTED (gates9, audit F1): a glob, a folder delete, a find or a move is refused only when it can really reach a locked file (a glob that
// matches one, a folder whose tree holds one, a destination that IS one); a move is judged by its SOURCE (the destination only when it is itself a locked file).
// gates11 (gates10 audit fault 8): also refused when they would rewrite a locked file: chained and aliased fs calls (require('fs').rm, require('fs/promises').rm, import('fs').then(m => m.rmSync(..)), fs['rmSync']),
// `git stash` (push/save/bare, with or without -- <path>), `git stash pop|apply|branch`, `git reset --hard`, `git checkout [<rev>] -- <locked>`, `git restore`, `git revert <commit that changed a locked file>`; the files a git
// command would change are asked from git itself (read-only calls, 5 s each; when git cannot answer nothing is refused). `patch --dry-run|--check|-C` is only a read.
// WHO IS LIMITED: a BUILDER subagent, i.e. an agent whose launch carried a HANDIN_DIR (read from its own transcript or the BRIEF-FILE it names; an `AUDIT-OF:` launch is an auditor). Auditors
// (NO-CONTRACT launches), agents with no HANDIN_DIR and the main session (no agent_id) may write locked tests: that is how they get there.
// WHAT IS REFUSED: Write / Edit / NotebookEdit of a locked file, and in Bash / PowerShell any write, delete or rename of one: redirects (> >>), rm / del / Remove-Item / unlink, mv / move /
// Move-Item / ren / Rename-Item (source), cp / Copy-Item / mv onto it, tee, touch, truncate, Set-Content / Add-Content / Clear-Content / Out-File, in-place editors and formatters (sed -i, perl -i, ruby -i,
// awk -i inplace, clang-format -i, prettier --write, dos2unix, black, gofmt -w ...), git rm, git mv, git checkout -- <file>, git restore <file>, a delete or move of a folder that holds locked files, and
// inline python / node code whose WRITE call (write, append, unlink, rename, copy onto, open for write) takes a locked path as the argument it writes to; reading one is free.
// WHAT IT CANNOT SEE: a path built from variables or pieces, a script file that edits the test (`node x.mjs`), an editor or build tool that rewrites it, a diff piped into patch / git apply (a diff FILE or heredoc is read: tools/hooks/lock_extra.mjs), `$(...)` forms other than ls, find, git ls-files, echo, printf, a test deleted by
// `git reset --hard` / `git clean` (those only undo the builder's own edits), and anything outside Bash, PowerShell, Write, Edit, NotebookEdit. A builder with no readable transcript is treated as
// not a builder (nothing to judge by). FAIL OPEN (logged) on any internal error.
import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import { spawnSync } from 'node:child_process';
import { log, readStdin, parseJson, launchInfoOfAgent, STATE_DIR, reEsc, safeRe } from './lib.mjs';
import { parseCommand, verbOf, shellInner } from './shell_lex.mjs';
import { extraJudge } from './lock_extra.mjs';

export const LOCK_MESSAGE = 'locked test: written by the auditor; make it pass, or write BLOCKED';
export const PROTECTED_WHY = 'is the lock list or the prior audit (read-only for a builder)';
const norm = (p) => String(p).replace(/\\/g, '/').replace(/\/+/g, '/').replace(/\/$/, '').toLowerCase();
const winPath = (p) => (/^\/([a-zA-Z])(\/|$)/.test(p) ? p[1] + ':' + p.slice(2) : p);
const absOf = (p, cwd) => { const w = winPath(String(p).replace(/\\/g, '/')); return path.resolve(/^[A-Za-z]:|^\//.test(w) ? w : path.join(cwd || process.cwd(), w)); };
const hasGlob = (s) => /[*?]|\[[^\]]+\]/.test(s);
const slash = (p) => String(p).replace(/\\/g, '/');

// a glob (on a lowercased, forward-slash path) as a RegExp: ** crosses folders, * and ? do not
export function globToRe(g) {
  let re = '';
  for (let i = 0; i < g.length; i++) {
    const c = g[i];
    if (c === '*') { if (g[i + 1] === '*') { i++; if (g[i + 1] === '/') { i++; re += '(?:.*/)?'; } else re += '.*'; } else re += '[^/]*'; }
    else if (c === '?') re += '[^/]';
    else if (c === '[') { const k = g.indexOf(']', i + 1); if (k > i + 1) { re += '[' + g.slice(i + 1, k).replace(/\\/g, '\\\\') + ']'; i = k; } else re += '\\['; }
    else re += reEsc(c);
  }
  return safeRe('^' + re + '$');
}

// every file below a folder (bounded: depth, count); links are not followed
function walkFiles(dir, { max = 20000, depth = 10 } = {}) {
  const out = []; const SKIP = new Set(['.git', 'node_modules']);
  const go = (d, lvl) => {
    if (out.length >= max || lvl > depth) return;
    let ents; try { ents = fs.readdirSync(d, { withFileTypes: true }); } catch { return; }
    for (const e of ents) {
      if (out.length >= max) return;
      const f = path.join(d, e.name);
      if (e.isSymbolicLink()) continue;
      if (e.isDirectory()) { if (!SKIP.has(e.name)) go(f, lvl + 1); } else out.push(f);
    }
  };
  go(dir, 0);
  return out;
}

// the text of a list file: UTF-8 (BOM stripped), UTF-16 LE / BE with a BOM, or UTF-16 without a BOM (a NUL in every second byte)
const swap16 = (b) => { const sw = Buffer.from(b); for (let i = 0; i + 1 < sw.length; i += 2) { const a = sw[i]; sw[i] = sw[i + 1]; sw[i + 1] = a; } return sw; };
export function decodeList(buf) {
  if (buf.length >= 2 && buf[0] === 0xff && buf[1] === 0xfe) return { text: buf.subarray(2).toString('utf16le'), enc: 'utf-16le' };
  if (buf.length >= 2 && buf[0] === 0xfe && buf[1] === 0xff) return { text: swap16(buf.subarray(2)).toString('utf16le'), enc: 'utf-16be' };
  if (buf.length >= 3 && buf[0] === 0xef && buf[1] === 0xbb && buf[2] === 0xbf) return { text: buf.subarray(3).toString('utf8'), enc: 'utf-8' };
  if (buf.length >= 4) {
    let nulOdd = 0, nulEven = 0; const n = Math.min(buf.length, 400);
    for (let i = 0; i < n; i++) if (buf[i] === 0) { if (i % 2) nulOdd++; else nulEven++; }
    if (nulOdd > n / 4 && nulEven === 0) return { text: buf.toString('utf16le'), enc: 'utf-16le' };
    if (nulEven > n / 4 && nulOdd === 0) return { text: swap16(buf).toString('utf16le'), enc: 'utf-16be' };
  }
  return { text: buf.toString('utf8'), enc: 'utf-8' };
}

// the prior round's folder, the list in it, the audit, and what is wrong with the list
export function lockedListInfo(priorAudit) {
  const res = { entries: [], file: null, audit: null, problems: [] };
  if (!priorAudit) return res;
  try {
    const given = path.resolve(String(priorAudit));
    let dir = given; let isFile = false;
    try { isFile = fs.statSync(given).isFile(); } catch { isFile = /\.\w{1,5}$/.test(given); }
    if (isFile) dir = path.dirname(given);
    res.file = path.join(dir, 'LOCKED_TESTS.txt'); res.audit = isFile ? given : path.join(dir, 'AUDIT.md');
    let auditText = ''; try { auditText = fs.readFileSync(res.audit, 'utf8').slice(0, 400000); } catch { /* no audit text */ }
    let buf = null;
    try { buf = fs.readFileSync(res.file); } catch (e) { if (e && e.code === 'ENOENT') { if (/LOCKED_TESTS|tests[\\/]locked/i.test(auditText)) res.problems.push(`the audit (${res.audit}) speaks of locked tests but ${res.file} does not exist: nothing is locked`); } else res.problems.push(`${res.file} cannot be read (${(e && e.code) || e})`); }
    if (buf) {
      const { text, enc } = decodeList(buf);
      if (/\u0000/.test(text) || /�/.test(text)) res.problems.push(`${res.file} could not be decoded as text (read as ${enc}); write it as UTF-8, one path per line`);
      res.entries = text.split(/\r?\n/).map((l) => l.replace(/\u0000/g, '').replace(/#.*$/, '').trim().replace(/^`|`$/g, '').replace(/^["']|["']$/g, '')).filter(Boolean);
      if (!res.entries.length) res.problems.push(`${res.file} has no entry: nothing is locked`);
    }
  } catch (e) { res.problems.push(`the lock list could not be read: ${(e && e.message) || e}`); }
  return res;
}
export function lockedList(priorAudit) { return lockedListInfo(priorAudit).entries; }

// a lock context for one builder
export function lockCtx({ handinDir, priorAudit }) {
  const info = lockedListInfo(priorAudit);
  const problems = [...info.problems];
  const wtRoot = (() => { const n = String(handinDir || '').replace(/\\/g, '/'); const k = n.toLowerCase().indexOf('/docs/progress/'); return k >= 0 ? n.slice(0, k) : null; })();
  const relOf = (abs) => { try { return wtRoot ? slash(path.relative(wtRoot, abs)) : slash(abs); } catch { return slash(abs); } };
  const files = [];             // existing locked / protected files: { n, rel, kind, why }
  const seen = new Set();
  const addFile = (abs, kind, why) => { const n = norm(abs); if (seen.has(n)) return; seen.add(n); files.push({ n, rel: relOf(abs), kind, why }); };
  const directs = [];           // paths that are locked whether or not they exist yet: { n, rel, prefix, kind, why, loose }
  // the tests/locked folder of the builder's worktree, if it exists
  if (wtRoot) { const ld = path.join(wtRoot, 'tests', 'locked'); try { if (fs.statSync(ld).isDirectory()) for (const f of walkFiles(ld)) addFile(f, 'test', 'is under tests/locked'); } catch { /* no such folder: nothing protected there */ } }
  for (const e of info.entries) {
    const isAbs = /^[A-Za-z]:|^[\\/]/.test(e);
    const abs = isAbs ? path.resolve(winPath(slash(e))) : (wtRoot ? path.resolve(wtRoot, e) : null);
    const why = `is listed in LOCKED_TESTS.txt (${e})`;
    if (hasGlob(e)) {
      if (!abs) { problems.push(`entry "${e}" is a glob and the worktree root is unknown: it cannot be expanded`); continue; }
      const parts = slash(abs).split('/'); const keep = []; for (const s of parts) { if (hasGlob(s)) break; keep.push(s); }
      const startDir = keep.join('/') || '/'; const re = globToRe(norm(abs)); let n = 0;
      for (const f of walkFiles(startDir)) if (re.test(norm(f))) { addFile(f, 'test', why); directs.push({ n: norm(f), rel: e, prefix: false, kind: 'test', why }); n++; }
      if (!n) problems.push(`entry "${e}" (a glob) matches no file`);
      continue;
    }
    directs.push({ n: abs ? norm(abs) : norm(e), rel: e, prefix: true, kind: 'test', why, loose: !abs });
    let st = null; if (abs) { try { st = fs.statSync(abs); } catch { /* missing */ } }
    if (!st) problems.push(`entry "${e}" names no existing file`);
    else if (st.isDirectory()) for (const f of walkFiles(abs)) addFile(f, 'test', why);
    else addFile(abs, 'test', why);
  }
  for (const p of [info.file, info.audit]) {
    if (!p) continue;
    directs.push({ n: norm(p), rel: relOf(p), prefix: false, kind: 'protected', why: PROTECTED_WHY });
    try { if (fs.statSync(p).isFile()) addFile(p, 'protected', PROTECTED_WHY); } catch { /* not there */ }
  }
  const direct = (p) => {
    const n = norm(p);
    if (/(^|\/)tests\/locked(\/|$)/.test(n)) return { why: 'is under tests/locked', rel: 'tests/locked', kind: 'test' };
    for (const e of directs) {
      if (n === e.n || (e.prefix && n.startsWith(e.n + '/')) || (e.loose && (n.endsWith('/' + e.n) || n.includes('/' + e.n + '/')))) return e;
    }
    return null;
  };
  // a folder whose tree really holds a locked file (rm -rf tests, mv tests tests2)
  const above = (p) => { const n = norm(p); if (!n) return null; for (const f of files) if (f.n.startsWith(n + '/')) return f; return null; };
  // a glob that reaches a locked file; whole: or a folder holding one (rm tests/* takes tests/locked along)
  const globHit = (patternAbs, whole) => {
    let re; try { re = globToRe(norm(patternAbs)); } catch { return null; }
    for (const f of files) {
      if (re.test(f.n)) return f;
      if (whole) { let a = f.n; for (let i = a.lastIndexOf('/'); i > 0; i = a.lastIndexOf('/')) { a = a.slice(0, i); if (re.test(a)) return f; } }
    }
    return null;
  };
  return { list: info.entries, entries: info.entries.map((raw) => ({ raw })), files, problems, listFile: info.file, direct, above, globHit };
}

const DEL = new Set(['rm', 'del', 'erase', 'rmdir', 'rd', 'remove-item', 'ri', 'unlink', 'shred', 'truncate']);
const MOVE = new Set(['mv', 'move', 'move-item', 'mi', 'ren', 'rename-item', 'rni']);
const COPY = new Set(['cp', 'copy', 'copy-item', 'cpi', 'install']);
const WRITE = new Set(['tee', 'tee-object', 'touch', 'set-content', 'sc', 'add-content', 'ac', 'clear-content', 'clc', 'out-file', 'new-item', 'ni', 'set-itemproperty', 'dd', 'chmod', 'chown', 'chgrp', 'icacls', 'attrib', 'takeown', 'setfacl', 'set-acl']);
const SHELLS = new Set(['bash', 'sh', 'zsh', 'pwsh', 'powershell', 'cmd']);
const INLINE = new Set(['python', 'python3', 'py', 'node', 'nodejs', 'perl', 'ruby', 'bun', 'deno']);
const RUNNERS = new Set(['npx', 'pnpx', 'bunx']);
const isFlag = (x) => /^-/.test(x) || /^\/{1,2}[a-zA-Z](:\S*)?$/.test(x);
// programs that rewrite their file arguments (gates9, audit H7); folders: the argument may be a folder that is processed with everything in it
const FORMATTERS = {
  'clang-format': { on: (av) => av.includes('-i') || av.includes('--in-place') },
  prettier: { on: (av) => av.includes('--write') || av.includes('-w'), folders: true },
  eslint: { on: (av) => av.includes('--fix'), folders: true },
  dos2unix: { on: () => true }, unix2dos: { on: () => true }, mac2unix: { on: () => true },
  black: { on: (av) => !av.includes('--check') && !av.includes('--diff'), folders: true }, isort: { on: (av) => !av.includes('--check') && !av.includes('--check-only') && !av.includes('--diff'), folders: true },
  gofmt: { on: (av) => av.includes('-w'), folders: true }, rustfmt: { on: (av) => !av.includes('--check') }, shfmt: { on: (av) => av.includes('-w'), folders: true },
  autopep8: { on: (av) => av.includes('-i') || av.includes('--in-place'), folders: true }, yapf: { on: (av) => av.includes('-i') || av.includes('--in-place'), folders: true }, 'cmake-format': { on: (av) => av.includes('-i') },
  // gates10 (gates9 audit fault 2): more in-place tools. astyle rewrites unless --dry-run; uncrustify with --replace / --no-backup; clang-tidy with --fix; ed, ex and the editors run in batch mode write what a script says
  astyle: { on: (av) => !av.includes('--dry-run') && !av.includes('-h') && !av.includes('--help'), folders: true },
  uncrustify: { on: (av) => av.includes('--replace') || av.includes('--no-backup') || av.includes('-i') },
  'clang-tidy': { on: (av) => av.some((a) => /^--fix(?:-errors|-notes)?$/.test(a)) },
  ed: { on: () => true }, ex: { on: () => true }, red: { on: () => true },
  vim: { on: (av) => av.some((a) => /^(?:-es?|-E|-s|--headless|-c|-S)$/.test(a) || /^\+/.test(a)) }, vi: { on: (av) => av.some((a) => /^(?:-es?|-E|-s|-c|-S)$/.test(a) || /^\+/.test(a)) },
  nvim: { on: (av) => av.some((a) => /^(?:-es?|-E|-s|--headless|-c|-S)$/.test(a) || /^\+/.test(a)) }, gvim: { on: (av) => av.some((a) => /^(?:-es?|-E|-s|-c)$/.test(a)) },
};
// brace lists a shell would expand: tests/{a.cpp,b.cpp} -> two words (the lexer keeps them glued)
function braceExpand(s) {
  const m = String(s).match(/\{([^{}]*,[^{}]*)\}/);
  if (!m) return [s];
  return m[1].split(',').flatMap((x) => braceExpand(String(s).replace(m[0], x))).slice(0, 64);
}
// paths a PRODUCER command lists, as globs that reach what it would list (gates10): find <dir> [-name N] -> <dir>/**[/N], git ls-files <p> -> <p> and <p>/**, ls / echo / printf <globs>.
// Used for `$(find tests -name '*.cpp')`, `ls tests/*.cpp | xargs clang-format -i`, `find ... | xargs ...`. Relative names that a bare `ls tests` prints are NOT paths and are skipped.
export function producerPaths(verb, av) {
  const out = [];
  if (verb === 'find') {
    const stop = av.findIndex((x) => /^[-!(]/.test(x)); const starts = av.slice(0, stop < 0 ? av.length : stop); const names = [];
    for (let k = 0; k < av.length - 1; k++) if (av[k] === '-name' || av[k] === '-iname') names.push(av[k + 1]);
    for (const s of (starts.length ? starts : ['.'])) { const b = s.replace(/[\\/]+$/, ''); if (names.length) for (const nm of names) out.push(`${b}/**/${nm}`); else out.push(`${b}/**`); }
  } else if (verb === 'git') {
    const i = av.indexOf('ls-files');
    if (i >= 0) for (const p of av.slice(i + 1).filter((x) => !/^-/.test(x))) { out.push(p); if (!hasGlob(p)) out.push(p.replace(/[\\/]+$/, '') + '/**'); }
  } else if (verb === 'ls' || verb === 'echo' || verb === 'printf' || verb === 'dir' || verb === 'get-childitem' || verb === 'gci') {
    for (const p of av.filter((x) => !isFlag(x))) if (hasGlob(p) || /[\\/]/.test(p)) out.push(p);
  }
  return out;
}
// the file names a unified diff (patch, git apply) changes
export function diffTargets(text) {
  const out = new Set();
  for (const l of String(text).replace(/\r\n/g, '\n').split('\n')) {
    let m = /^(?:\+\+\+|---)[ \t]+(\S+)/.exec(l); if (m && m[1] !== '/dev/null') { out.add(m[1].replace(/^"|"$/g, '')); continue; }
    m = /^diff --git[ \t]+(\S+)[ \t]+(\S+)/.exec(l); if (m) { out.add(m[1]); out.add(m[2]); }
  }
  return [...out];
}
// is this text a diff?
const looksLikeDiff = (t) => /^(?:diff --git |--- \S+[\s\S]*\n\+\+\+ )/m.test(t) || /^--- .*\n\+\+\+ /m.test(t);

// judge a PATH argument: [] or reasons. `whole`: the operation takes a folder with everything in it (delete, move, formatters)
function judgeTarget(raw, cur, lc, verbName, whole) {
  const out = [];
  const piece = String(raw).replace(/^-\w+:/, '');
  if (!piece || /\s/.test(piece) && !/^[A-Za-z]:[\\/]/.test(piece) && !/[\\/]/.test(piece)) return out;   // prose with spaces, not a path
  const abs = absOf(piece, cur);
  if (hasGlob(piece)) { const h = lc.globHit(abs, whole); if (h) out.push(`${verbName} ${raw}: the glob reaches the locked file ${h.rel} (${h.why})`); return out; }
  const d = lc.direct(abs);
  if (d) { out.push(`${verbName} ${raw}: the path ${d.why}`); return out; }
  if (whole) { const a = lc.above(abs); if (a) out.push(`${verbName} ${raw}: the folder holds ${a.kind === 'protected' ? 'the lock list or the prior audit (read-only for a builder)' : 'the locked file ' + a.rel}`); }
  return out;
}
// the destination of mv / cp: refused only when it IS a locked file (a folder destination counts through <folder>/<name of the source>)
function judgeDest(raw, cur, lc, verbName, srcs) {
  const out = judgeTarget(raw, cur, lc, verbName, false);
  if (out.length || hasGlob(String(raw))) return out;
  try {
    const abs = absOf(String(raw).replace(/^-\w+:/, ''), cur);
    if (fs.statSync(abs).isDirectory()) for (const s of srcs) { const b = path.basename(String(s).replace(/[\\/]+$/, '')); if (!b || hasGlob(b)) continue; const d = lc.direct(path.join(abs, b)); if (d) { out.push(`${verbName} ${raw}: the file it would write there ${d.why}`); break; } }
  } catch { /* the destination is not an existing folder */ }
  return out;
}

// ---- inline python / node: only a WRITE call whose written-to argument names a locked path is a write; a read (or a copy FROM it) is free
function matchParen(s, i) { let d = 0; for (let j = i; j < s.length; j++) { const ch = s[j]; if (ch === '"' || ch === "'" || ch === '`') { const k = s.indexOf(ch, j + 1); if (k < 0) return -1; j = k; continue; } if (ch === '(') d++; else if (ch === ')') { d--; if (d === 0) return j; } } return -1; }
function splitArgs(s) {
  const out = []; let d = 0, cur = '';
  for (let j = 0; j < s.length; j++) {
    const ch = s[j];
    if (ch === '"' || ch === "'" || ch === '`') { const k = s.indexOf(ch, j + 1); const e = k < 0 ? s.length - 1 : k; cur += s.slice(j, e + 1); j = e; continue; }
    if ('([{'.includes(ch)) d++; else if (')]}'.includes(ch)) d--;
    if (ch === ',' && d === 0) { out.push(cur.trim()); cur = ''; } else cur += ch;
  }
  if (cur.trim()) out.push(cur.trim());
  return out;
}
const FIRST_ARG = /(?:^|[^\w.])(?:fs(?:\.promises)?\.)?(?:writeFileSync|writeFile|appendFileSync|appendFile|unlinkSync|unlink|rmSync|rmdirSync|rmdir|truncateSync|truncate|createWriteStream)\s*\(|(?:^|[^\w.])(?:fs(?:\.promises)?|fsp|fsPromises)\.rm\s*\(|(?:^|[^\w.])(?:os\.remove|os\.unlink|os\.rmdir|os\.truncate|shutil\.rmtree|rmtree|File\.write|File\.delete|File\.unlink|FileUtils\.rm\w*|Deno\.writeTextFileSync|Deno\.writeFileSync|Deno\.removeSync|Deno\.remove|Bun\.write)\s*\(/g;
const ANY_RECV = /(?:^|[^\w.])[A-Za-z_$][\w$]*\s*\.\s*(?:writeFileSync|writeFile|appendFileSync|appendFile|unlinkSync|unlink|rmSync|rm|rmdirSync|rmdir|truncateSync|truncate|createWriteStream)\s*\(/g;
const BOTH_ARGS =/(?:^|[^\w.])(?:fs(?:\.promises)?\.renameSync|fs(?:\.promises)?\.rename|renameSync|os\.rename|os\.replace|shutil\.move|File\.rename|FileUtils\.mv|Deno\.renameSync|Deno\.rename)\s*\(/g;
const DEST_ARG = /(?:^|[^\w.])(?:fs(?:\.promises)?\.)?(?:copyFileSync|copyFile|cpSync|cp)\s*\(|(?:^|[^\w.])(?:shutil\.(?:copy|copy2|copyfile|copytree|copymode)|FileUtils\.cp\w*|Deno\.copyFileSync|Deno\.copyFile)\s*\(/g;
const OPEN_CALL = /(?:^|[^\w.])(?:io\.|codecs\.|os\.)?open\s*\(/g;
const FS_OPEN = /(?:^|[^\w.])fs(?:\.promises)?\.open(?:Sync)?\s*\(/g;
const PATH_METHOD =/Path\s*\(([^)]*)\)\s*\.\s*(?:write_text|write_bytes|unlink|touch|rename|replace|rmdir)\b/g;
// gates11 (gates10 audit fault 8): spellings that name the same call as the bare fs.<call>: require('fs').rm(...), require('node:fs/promises').rm(...), (await import('fs')).rmSync(...),
// fs['rmSync'](...), process.binding-free. The call is rewritten to fs.<call> / fs.promises.<call> before the patterns below read it.
export function normalizeFsRefs(code) {
  let c = String(code);
  c = c.replace(/\brequire\s*\(\s*['"](?:node:)?fs\/promises['"]\s*\)/g, 'fs.promises');
  c = c.replace(/\brequire\s*\(\s*['"](?:node:)?fs['"]\s*\)/g, 'fs');
  c = c.replace(/\(?\s*await\s+import\s*\(\s*['"](?:node:)?fs\/promises['"]\s*\)\s*\)?/g, 'fs.promises');
  c = c.replace(/\(?\s*await\s+import\s*\(\s*['"](?:node:)?fs['"]\s*\)\s*\)?/g, 'fs');
  c = c.replace(/\b(fs(?:\.promises)?)\s*\[\s*['"](\w+)['"]\s*\]/g, '$1.$2');
  return c;
}
export function inlineWrites(code0, lc) {
  const code = normalizeFsRefs(code0);
  const hits = [];
  const lockedIn = (txt) => {
    const low = slash(txt).replace(/\/+/g, '/').toLowerCase();
    const m = low.match(/[^\s"'`()]*tests\/locked[^\s"'`()]*/); if (m) return m[0];
    for (const f of lc.files) if (low.includes(f.rel.toLowerCase())) return f.rel;
    for (const e of lc.list) if (low.includes(slash(e).toLowerCase())) return e;
    if (lc.listFile && /locked_tests\.txt/.test(low)) return 'LOCKED_TESTS.txt';
    return null;
  };
  const calls = (re, pick) => { re.lastIndex = 0; let m; while ((m = re.exec(code))) { const open = m.index + m[0].length - 1; const close = matchParen(code, open); const args = splitArgs(code.slice(open + 1, close < 0 ? code.length : close)); for (const a of pick(args)) { const n = lockedIn(a); if (n) hits.push(n); } } };
  calls(FIRST_ARG, (a) => a.slice(0, 1));
  // the fs module under any variable name (import('fs').then((m) => m.rmSync(p)), const f = await import('fs'); f.rm(p)): when the code names the fs module, a write method on ANY receiver counts
  if (/['"](?:node:)?fs(?:\/promises)?['"]/.test(String(code0))) calls(ANY_RECV, (a) => a.slice(0, 1));
  calls(BOTH_ARGS, (a) => a.slice(0, 2));
  calls(DEST_ARG, (a) => a.slice(1, 2));
  calls(OPEN_CALL, (a) => (a.slice(1).some((x) => /^(?:mode\s*=\s*)?['"][^'"]*[wax+]/.test(x)) ? a.slice(0, 1) : []));
  calls(FS_OPEN, (a) => (a.slice(1).some((x) => /^['"][^'"]*[wa+]/.test(x)) ? a.slice(0, 1) : []));
  PATH_METHOD.lastIndex = 0; let m; while ((m = PATH_METHOD.exec(code))) { const n = lockedIn(m[1]); if (n) hits.push(n); }
  return hits;
}

// gates11 (gates10 audit fault 8): git commands that rewrite tracked files from history or from a stash. The files they would change are asked from git itself (read-only calls,
// 5 s each); a locked file among them refuses the command. When git cannot answer (not a repository, unknown revision) nothing is refused: the command itself fails then.
function gitNames(repo, args) {
  try {
    const r = spawnSync('git', ['-C', repo, ...args], { encoding: 'utf8', timeout: 5000 });
    if (r.status !== 0) return [];
    return String(r.stdout || '').split('\0').join('\n').split('\n').map((x) => x.trim()).filter(Boolean);
  } catch { return []; }
}
const porcelainPath = (l) => { const p = l.replace(/^[MADRCUT?! ]{1,2}\s+/, ''); const k = p.indexOf(' -> '); return (k >= 0 ? p.slice(k + 4) : p).replace(/^"|"$/g, ''); };
function judgeGitHistory(sub, rest, repo, lc, dd) {
  const out = []; const hit = (names, what) => { for (const n of names) { const h = judgeTarget(n, repo, lc, what, false); if (h.length) { out.push(...h); break; } } };
  const flags = rest.filter((x) => /^-/.test(x)); const pos = (dd >= 0 ? rest.slice(0, dd) : rest).filter((x) => !/^-/.test(x));
  if (sub === 'reset') {
    if (!flags.includes('--hard')) return out;
    const target = pos[0] || 'HEAD';
    hit(gitNames(repo, ['diff', '--name-only', target]), 'git reset --hard (would rewrite)');
  } else if (sub === 'revert') {
    if (flags.some((x) => /^--(?:abort|continue|quit|skip)$/.test(x))) return out;
    for (const c of pos) hit(gitNames(repo, ['log', '--name-only', '--format=', ...(c.includes('..') ? [] : ['-1']), c]), 'git revert ' + c + ' (would rewrite)');
  } else if (sub === 'stash') {
    const act = pos[0] && !/^--/.test(pos[0]) ? pos[0].toLowerCase() : 'push';
    if (['push', 'save'].includes(act) || (!pos.length) || (pos[0] && /^(?:-|--)/.test(pos[0]))) {
      const spec = dd >= 0 ? rest.slice(dd + 1) : [];
      if (spec.length) { for (const p of spec) { const h = judgeTarget(p, repo, lc, 'git stash push --', true); if (h.length) { out.push(...h); break; } } }
      else hit(gitNames(repo, ['status', '--porcelain=v1', '-uall']).map(porcelainPath), 'git stash (would move away)');
    } else if (['pop', 'apply', 'branch'].includes(act)) {
      const which = pos.slice(1).find((x) => /^stash@/.test(x)) || 'stash@{0}';
      hit(gitNames(repo, ['stash', 'show', '--name-only', which]), 'git stash ' + act + ' (would rewrite)');
    }
  }
  return out;
}
const EXTRA_H = { hasGlob, isFlag, absOf, parseCommand, verbOf, producerPaths, braceExpand, diffTargets, looksLikeDiff, judgeTarget, judgeCommand: (...a) => judgeCommand(...a) };
export function judgeCommand(cmd, cwd, lc, depth = 0) {
  const reasons = [];
  if (depth > 4) return reasons;
  let cur = cwd || process.cwd();
  // PowerShell's own .NET calls: [IO.File]::WriteAllText('tests\locked\a.mjs', ...), [System.IO.Directory]::Delete(...)
  if (/\[(?:System\.)?IO\.(?:File|Directory|Path)\]::\w*(?:Write|Append|Delete|Move|Copy|Replace|Create)/i.test(String(cmd))) {
    const low = String(cmd).toLowerCase().replace(/\\/g, '/');
    const hit = /tests\/locked/.test(low) ? 'tests/locked' : ((lc.files.find((f) => low.includes(f.rel.toLowerCase())) || {}).rel || (lc.list.find((e) => low.includes(slash(e).toLowerCase())) || null) || (lc.listFile && /locked_tests\.txt/.test(low) ? 'LOCKED_TESTS.txt' : null));
    if (hit) reasons.push(`a .NET file call that names a locked test (${hit}) next to a write, delete, move or copy`);
  }
  const stack = [];
  let prevPaths = [], nextPrev = [], prevCmd = null;
  const { segments } = parseCommand(String(cmd || ''));
  for (const seg of segments) {
    for (const rd of seg.redirects) { if (rd.op === '>' || rd.op === '>>' || rd.op === '&>') reasons.push(...judgeTarget(rd.target, cur, lc, 'a redirect into', false)); }
    if (!seg.words.length) continue;
    let { verb, args, index, line } = verbOf(seg.words);
    let av = args.map((a) => a.v);
    prevPaths = nextPrev; nextPrev = av.filter((x) => !isFlag(x));
    const prevSeg = prevCmd; prevCmd = { verb, av };
    if (line) { reasons.push(...judgeCommand(seg.words.slice(index).map((w) => w.v).join(' '), cur, lc, depth + 1)); continue; }
    if (['cd', 'chdir', 'set-location', 'sl', 'pushd', 'push-location'].includes(verb)) { if (['pushd', 'push-location'].includes(verb)) stack.push(cur); const t = av.filter((x) => !/^-/.test(x) && !/^\/d$/i.test(x))[0]; if (t && !/\$|`|%/.test(t)) cur = absOf(t, cur); continue; }
    if (['popd', 'pop-location'].includes(verb)) { if (stack.length) cur = stack.pop(); continue; }
    if (SHELLS.has(verb) || verb === 'eval') {
      const inner = verb === 'eval' ? av.join(' ') : shellInner(verb, av);
      if (inner) reasons.push(...judgeCommand(inner, cur, lc, depth + 1));
      if (seg.here) reasons.push(...judgeCommand(seg.here, cur, lc, depth + 1));
      continue;
    }
    // gates10: bulk and indirect forms (xargs, find -exec, $(ls ..), brace lists, sed -e -i, robocopy filters, patch, git apply); see lock_extra.mjs
    { const ex = extraJudge({ verb, av, cur, lc, cmd: String(cmd), prev: prevSeg, depth, H: EXTRA_H }); reasons.push(...ex.reasons); if (ex.av) av = ex.av; if (ex.done) continue; }
    if (verb === 'xargs' || verb === 'sudo') { const k = av.findIndex((a) => DEL.has(a) || MOVE.has(a)); if (k >= 0) reasons.push(...judgeCommand(av.slice(k).join(' '), cur, lc, depth + 1)); }
    // npx prettier ..., pnpm exec prettier ..., yarn dlx ...: the program after the runner
    if (RUNNERS.has(verb) || (/^(?:pnpm|yarn|npm)$/.test(verb) && /^(?:exec|dlx|x)$/.test(av[0] || ''))) {
      const rest = RUNNERS.has(verb) ? av : av.slice(1); const k = rest.findIndex((x) => !/^-/.test(x));
      if (k >= 0) { verb = rest[k].toLowerCase().replace(/^.*[\/\\]/, '').replace(/\.(?:cmd|exe|ps1)$/, ''); av = rest.slice(k + 1); }
    }
    // positional (non-flag) arguments, with -Path / -LiteralPath / -Destination / -Value-less handling
    const pos = []; let named = null;
    for (let k = 0; k < av.length; k++) {
      const a = av[k];
      const m = /^-(path|literalpath|destination|newname|pa|lit\w*)(?::(.*))?$/i.exec(a);
      if (m) { const v = m[2] !== undefined ? m[2] : av[++k]; if (/^dest/i.test(m[1])) named = v; else if (/^newname$/i.test(m[1])) { /* a new name, not a path */ } else if (v !== undefined) pos.push(v); continue; }
      if (/^-(?:value|include|exclude|filter|encoding|stream|credential|inputobject)$/i.test(a)) { k++; continue; }
      if (isFlag(a)) continue;
      pos.push(a);
    }
    // a Remove-Item / Move-Item / Set-Content with no path of its own, fed by the segment before it (`Get-ChildItem tests\locked | Remove-Item`): the listed folder is what it acts on
    if ((DEL.has(verb) || MOVE.has(verb) || WRITE.has(verb) || verb === 'clear-content') && seg.piped && !pos.length && prevPaths.length) { for (const p of prevPaths) reasons.push(...judgeTarget(p, cur, lc, verb + ' (fed by a listing of it)', true)); continue; }
    if (DEL.has(verb)) { for (const p of pos) reasons.push(...judgeTarget(p, cur, lc, verb, true)); continue; }
    if (MOVE.has(verb)) {
      // judged by the SOURCE; the destination only when it is itself a locked file (gates9, audit F1)
      const srcs = named ? pos : pos.slice(0, Math.max(1, pos.length - 1)); const dest = named || (pos.length >= 2 ? pos[pos.length - 1] : null);
      for (const p of srcs) reasons.push(...judgeTarget(p, cur, lc, verb, true));
      if (dest) reasons.push(...judgeDest(dest, cur, lc, verb, srcs));
      continue;
    }
    if (COPY.has(verb)) { const dest = named || (pos.length >= 2 ? pos[pos.length - 1] : null); if (dest) reasons.push(...judgeDest(dest, cur, lc, verb, named ? pos : pos.slice(0, -1))); continue; }
    // other programs that write a destination: robocopy / xcopy (destination = second positional), rsync / ln / scp (last positional)
    if (verb === 'robocopy' || verb === 'xcopy') { const p2 = av.filter((x) => !/^\/[a-zA-Z]+(?::\S*)?$/.test(x) && !/^-/.test(x)); if (p2.length >= 2) reasons.push(...judgeTarget(p2[1], cur, lc, verb, verb === 'robocopy' && av.some((x) => /^\/(?:mir|purge|mov|move)$/i.test(x)))); continue; }
    if (verb === 'rsync' || verb === 'ln' || verb === 'scp') { if (pos.length >= 2) reasons.push(...judgeTarget(pos[pos.length - 1], cur, lc, verb, false)); continue; }
    // find <start> ... -delete / -exec rm|mv|truncate|shred: judged by what really lies below the start folder (with -name/-iname: by the names it can match)
    if (verb === 'find' && av.some((a) => a === '-delete' || ((a === '-exec' || a === '-execdir' || a === '-ok') && av.some((b) => /^(?:rm|mv|unlink|truncate|shred|sed|tee)$/.test(b))))) {
      const stop = av.findIndex((x) => /^[-!(]/.test(x)); const starts = av.slice(0, stop < 0 ? av.length : stop);
      const names = []; for (let k = 0; k < av.length - 1; k++) if (av[k] === '-name' || av[k] === '-iname') names.push(av[k + 1].toLowerCase());
      const complex = av.some((x) => x === '-path' || x === '-ipath' || x === '-regex' || x === '-iregex' || x === '-o' || x === '-not' || x === '!');
      for (const p of starts) {
        if (!names.length || complex || hasGlob(p)) { reasons.push(...judgeTarget(p, cur, lc, 'find ... -delete/-exec', true)); continue; }
        const abs = absOf(p, cur); const nb = norm(abs);
        const d = lc.direct(abs);
        const hit = d ? { rel: d.rel, why: d.why } : lc.files.find((f) => (f.n === nb || f.n.startsWith(nb + '/')) && names.some((nm) => globToRe(nm).test(f.n.slice(f.n.lastIndexOf('/') + 1))));
        if (hit) reasons.push(`find ${p} ... -delete/-exec: it can reach the locked file ${hit.rel}`);
      }
      continue;
    }
    if (WRITE.has(verb)) { for (const p of pos) reasons.push(...judgeTarget(p, cur, lc, verb, false)); if (verb === 'dd') for (const a of av) { const m = /^of=(.*)$/.exec(a); if (m) reasons.push(...judgeTarget(m[1], cur, lc, verb, false)); } continue; }
    // in-place editors: sed -i, perl -i, ruby -i (the first positional is the script), awk -i inplace (same, unless -f)
    if (((verb === 'sed' || verb === 'perl' || verb === 'ruby') && av.some((a) => /^-[a-zA-Z]*i/.test(a) || /^--in-place/.test(a)))) { for (const p of pos.slice(1)) reasons.push(...judgeTarget(p, cur, lc, verb + ' -i', false)); continue; }
    if (verb === 'awk' || verb === 'gawk') {
      const ix = av.findIndex((a, k) => (a === '-i' && /^inplace$/i.test(av[k + 1] || '')) || /^-i\s*inplace$/i.test(a) || /^--inplace$/i.test(a));
      if (ix >= 0) { const hasF = av.includes('-f') || av.includes('--file'); const fl = pos.filter((p) => !/^inplace$/i.test(p)); for (const p of (hasF ? fl : fl.slice(1))) reasons.push(...judgeTarget(p, cur, lc, verb + ' -i inplace', false)); }
      continue;
    }
    if (FORMATTERS[verb] && FORMATTERS[verb].on(av)) { for (const p of pos) reasons.push(...judgeTarget(p, cur, lc, verb + ' (rewrites its files in place)', !!FORMATTERS[verb].folders)); continue; }
    if (verb === 'git') {
      let repo = cur; let i = 0;
      while (i < av.length && /^-/.test(av[i])) { if (av[i] === '-C' && av[i + 1] !== undefined) { repo = absOf(av[i + 1], repo); i += 2; continue; } if (av[i] === '-c' || av[i] === '--namespace' || av[i] === '--exec-path') { i += 2; continue; } i++; }
      const sub = String(av[i] || '').toLowerCase(); const rest = av.slice(i + 1);
      const dd = rest.indexOf('--');
      const nonFlag = rest.filter((x) => !/^-/.test(x));
      if (sub === 'rm') for (const p of nonFlag) reasons.push(...judgeTarget(p, repo, lc, 'git rm', true));
      else if (sub === 'mv') { for (const p of nonFlag.slice(0, Math.max(1, nonFlag.length - 1))) reasons.push(...judgeTarget(p, repo, lc, 'git mv', true)); if (nonFlag.length >= 2) reasons.push(...judgeDest(nonFlag[nonFlag.length - 1], repo, lc, 'git mv', nonFlag.slice(0, -1))); }
      else if (sub === 'checkout' && dd >= 0) { const withRev = rest.slice(0, dd).some((x) => !/^-/.test(x)); for (const p of rest.slice(dd + 1)) { if (p !== '.' || withRev) reasons.push(...judgeTarget(p, repo, lc, 'git checkout' + (withRev ? ' <revision> --' : ' --'), withRev)); } }
      else if (sub === 'restore') { const src = rest.some((x) => /^(?:--source|-s)/.test(x)); for (const p of nonFlag) { if (p !== '.' || src) reasons.push(...judgeTarget(p, repo, lc, 'git restore' + (src ? ' --source' : ''), src)); } }
      else if (sub === 'reset' || sub === 'revert' || sub === 'stash') reasons.push(...judgeGitHistory(sub, rest, repo, lc, dd));
      else if (sub === 'clean') { if (!rest.some((x) => (/^-[a-zA-Z]*n/.test(x) && !/^--/.test(x)) || x === '--dry-run')) for (const p of nonFlag) reasons.push(...judgeTarget(p, repo, lc, 'git clean', true)); }
      else if (sub === 'checkout' && dd < 0) { /* branch switch or `checkout <ref> <file>`: the file form is judged here */ for (const p of nonFlag) reasons.push(...judgeTarget(p, repo, lc, 'git checkout', nonFlag.length >= 2)); }
      continue;
    }
    if (INLINE.has(verb)) {
      const k = av.findIndex((a) => /^(?:-e|-c|--eval|-p|-pe|-r)$/.test(a));
      if (k >= 0 && av[k + 1]) {
        const code = av[k + 1];
        if (/^(?:python|python3|py|node|nodejs|bun|deno)$/.test(verb)) {
          const hits = inlineWrites(code, lc);
          if (hits.length) reasons.push(`inline ${verb} code writes, deletes, renames or copies onto a locked path (${hits[0]})`);
        } else {
          // perl / ruby: a name next to a write word (their calls are too varied to split)
          const low = slash(code).toLowerCase();
          const names = [...(low.match(/[^\s"'`()]*tests\/+locked[^\s"'`()]*/g) || []), ...lc.list.filter((e) => low.includes(slash(e).toLowerCase())), ...(lc.listFile && /locked_tests\.txt/.test(low) ? ['LOCKED_TESTS.txt'] : [])];
          if (names.length && /write|append|unlink|\brm(?:dir)?\b|rename|copy|truncate|delete|remove|\bmv\b|\bmove\b|\bopen\s*\(?[^;]*['"]\s*[>+]/i.test(code)) reasons.push(`inline ${verb} code that names a locked test (${names[0]}) next to a write, delete or rename call`);
        }
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
    return d ? [`${tool} ${p}: the path ${d.why}`] : [];
  }
  if (/^(Bash|PowerShell)$/.test(tool)) return judgeCommand(String(ti.command || ''), input.cwd, lc);
  return [];
}

// LOCK-LIST-BAD: logged (and shown to the builder) once per agent and distinct set of problems
function reportBadList(input, lc) {
  if (!lc.problems.length) return null;
  const id = String(input.agent_id || '').replace(/[^\w.-]/g, '_');
  const key = crypto.createHash('sha1').update(JSON.stringify(lc.problems)).digest('hex').slice(0, 16);
  const mf = path.join(STATE_DIR, 'lock_bad', id + '.json');
  try { if (JSON.parse(fs.readFileSync(mf, 'utf8')).key === key) return null; } catch { /* first time */ }
  const text = `LOCK-LIST-BAD: ${lc.problems.slice(0, 5).join(' | ')}`;
  log({ gate: 'lock_guard', decision: 'allow', reason: `${text} (FAIL-OPEN: the locked-test list is unusable or incomplete, so the lock does not hold as the auditor meant; the main session should fix the list)`, agent_id: id, list: lc.listFile || null, problems: lc.problems.slice(0, 10) });
  try { fs.mkdirSync(path.dirname(mf), { recursive: true }); fs.writeFileSync(mf, JSON.stringify({ key })); } catch { /* the line is logged; the marker only avoids repeats */ }
  return text;
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
  const warn = reportBadList(input, lc);
  const reasons = judge(input, lc);
  // gates10 (gates9 audit fault 3): the warning goes to the user (systemMessage) AND to the builder (additionalContext)
  const warnOut = warn ? { systemMessage: warn + ' (see .claude/state/gate.log; tell the main session)', hookSpecificOutput: { hookEventName: 'PreToolUse', additionalContext: warn + ' (the lock list is unusable or incomplete; say so in the first lines of STATUS.md)' } } : null;
  if (!reasons.length) { if (warnOut) process.stdout.write(JSON.stringify(warnOut)); return 0; }
  log({ gate: 'lock_guard', decision: 'deny', reason: 'builder touched a locked test', agent_id: input.agent_id, tool: input.tool_name, command: String((input.tool_input && (input.tool_input.command || input.tool_input.file_path)) || '').slice(0, 300), reasons });
  const realTest = reasons.some((r) => !/read-only for a builder/.test(r));
  const head = realTest ? `REFUSED by the lock guard (tools/hooks/lock_guard.mjs): ${LOCK_MESSAGE}` : 'REFUSED by the lock guard (tools/hooks/lock_guard.mjs): the lock list (LOCKED_TESTS.txt) and the prior round\'s AUDIT.md are read-only for a builder';
  const tail = realTest
    ? 'A file listed in LOCKED_TESTS.txt of the prior round\'s hand-in folder (or under tests/locked/) was written by the auditor to judge your work. Do not edit, delete or rename it: fix the code until it passes. If it cannot pass, write a BLOCKED line in STATUS.md (BLOCKED: <item> | tried: | blocked by: | needs:).'
    : 'These files record what the auditor locked and found; they are not yours to change. Read them as much as you like; put your own notes in your own hand-in folder.';
  process.stderr.write(`${head}\n - ${reasons.join('\n - ')}\n${tail}\n${warn ? warn + '\n' : ''}`);
  return 2;
}
