// lock_guard helpers added in gates10 (gates9 audit faults 2 and 3). Called once per command segment from lock_guard_impl.judgeCommand through extraJudge(); everything it needs from the guard
// comes in through `H`, so this file imports nothing of it. It returns { reasons, done, av }: reasons to refuse, done = the segment is fully judged (the old branches are skipped), av = a rewritten
// argument list (brace lists and $(ls|find|git ls-files ...) substitutions expanded to the paths they stand for).
//
// WHAT IS NEW
//  - bulk forms of the in-place tools: `find <dir> -exec clang-format -i {} +`, `find ... | xargs clang-format -i`, `git ls-files tests | xargs ...`, `clang-format -i $(ls tests/*.cpp)`
//  - sed / perl / ruby -i with the script given by -e / -f / --expression (the first positional is then a FILE, not the script)
//  - robocopy: file filters after the destination and /MOV or /MOVE moving files OUT of a source folder
//  - patch and git apply: the diff names its files (--- / +++ / diff --git lines, read from the diff file the command names or from a heredoc in the command text); a name that, with 0 to 5 leading
//    components stripped, is a locked file refuses the command (git apply --check, --stat, --numstat, --summary only read)
//  - brace lists: rm tests/{locked_a.cpp,free.cpp}
// STILL NOT SEEN (said plainly): a path built from variables or `$(...)` forms other than ls, find, git ls-files, echo and printf, `$PWD`, a diff piped in from another command, a script file.
import fs from 'node:fs';

const XARGS_VALUE_FLAGS = new Set(['-i', '-n', '-p', '-l', '-s', '-d', '-e', '-a', '-I']);
const quote = (t) => (/\s/.test(t) ? `"${t.replace(/"/g, '')}"` : t);

export function extraJudge(env) {
  const { verb, cur, lc, cmd, prev, depth, H } = env;
  let av = env.av;
  const out = { reasons: [], done: false, av: null };
  // ---- expansion: $(ls ...) / $(find ...) / `git ls-files` substitutions and {a,b} lists
  let changed = false; const av2 = [];
  for (const a of av) {
    const m = /^\$\(([\s\S]*)\)$/.exec(a);
    if (m) {
      let paths = [];
      try { const seg0 = H.parseCommand(m[1]).segments[0]; if (seg0 && seg0.words.length) { const v = H.verbOf(seg0.words); paths = H.producerPaths(v.verb, v.args.map((x) => x.v)); } } catch { /* unreadable: left as it is */ }
      if (paths.length) { av2.push(...paths); changed = true; continue; }
    }
    if (/\{[^{}]*,[^{}]*\}/.test(a)) { const ex = H.braceExpand(a); if (ex.length > 1 || ex[0] !== a) { av2.push(...ex); changed = true; continue; } }
    av2.push(a);
  }
  if (changed) { av = av2; out.av = av2; }

  // ---- xargs <program> ...: the program runs over what the previous command listed
  if (verb === 'xargs') {
    let k = 0;
    while (k < av.length && /^-/.test(av[k])) k += XARGS_VALUE_FLAGS.has(av[k]) ? 2 : 1;
    const inner = av.slice(k);
    if (inner.length) {
      const prod = prev ? H.producerPaths(prev.verb, prev.av) : [];
      out.reasons.push(...H.judgeCommand([...inner, ...prod].map(quote).join(' '), cur, lc, depth + 1));
      out.done = true;
    }
    return out;
  }

  // ---- find ... -exec <program> ... {} + : the program runs over everything below the start folders
  if (verb === 'find') {
    const globs = H.producerPaths('find', av);
    for (let i = 0; i < av.length; i++) {
      if (!/^-(?:exec|execdir|ok|okdir)$/.test(av[i])) continue;
      const grp = [];
      for (let j = i + 1; j < av.length && av[j] !== ';' && av[j] !== '+' && av[j] !== '\\;'; j++) grp.push(av[j]);
      if (!grp.length) continue;
      for (const g of globs) out.reasons.push(...H.judgeCommand(grp.map((x) => (x === '{}' ? g : x)).map(quote).join(' '), cur, lc, depth + 1));
    }
    return out;
  }

  // ---- sed / perl / ruby -i with the script given by a flag: every positional is a file
  if ((verb === 'sed' || verb === 'perl' || verb === 'ruby') && av.some((a) => /^-[a-zA-Z]*i/.test(a) || /^--in-place/.test(a))) {
    const files = []; let viaFlag = false;
    for (let k = 0; k < av.length; k++) {
      const a = av[k];
      if (a === '-e' || a === '-f' || a === '--expression' || a === '--file') { viaFlag = true; k++; continue; }
      if (/^--(?:expression|file)=/.test(a)) { viaFlag = true; continue; }
      if (/^-[a-zA-Z]*e$/.test(a) && !/i/.test(a)) { viaFlag = true; k++; continue; }
      if (H.isFlag(a)) continue;
      files.push(a);
    }
    if (viaFlag) { for (const p of files) out.reasons.push(...H.judgeTarget(p, cur, lc, verb + ' -i', false)); out.done = true; }
    return out;
  }

  // ---- robocopy: filters after the destination, /MOV moving files away from the source
  if (verb === 'robocopy') {
    const keep = []; let skip = false;
    for (const a of av) { if (/^\/(?:xf|xd)$/i.test(a)) { skip = true; continue; } if (/^\/[a-zA-Z]/.test(a)) { skip = false; continue; } if (!skip) keep.push(a); }
    if (keep.length >= 2) {
      const mov = av.some((x) => /^\/(?:mov|move)$/i.test(x));
      const filters = keep.slice(2);
      for (const f of filters) out.reasons.push(...H.judgeTarget(keep[1].replace(/[\\/]+$/, '') + '/' + f, cur, lc, 'robocopy (file filter in the destination)', false));
      if (mov) {
        if (filters.length) for (const f of filters) out.reasons.push(...H.judgeTarget(keep[0].replace(/[\\/]+$/, '') + '/' + f, cur, lc, 'robocopy /MOV (moves its source files away)', false));
        else out.reasons.push(...H.judgeTarget(keep[0], cur, lc, 'robocopy /MOV (moves its source files away)', true));
      }
    }
    return out;
  }

  // ---- patch / git apply: what the diff changes
  const diffIn = (args, readOnly) => {
    if (readOnly) return;
    let strip = null;
    for (const a of args) { const m = /^-p(\d+)$/.exec(a); if (m) strip = Number(m[1]); }
    const files = [];
    for (let k = 0; k < args.length; k++) {
      const a = args[k];
      if ((a === '-i' || a === '--input') && args[k + 1]) { files.push(args[++k]); continue; }
      if (/^--input=/.test(a)) { files.push(a.slice(8)); continue; }
      if (/^-/.test(a)) { if (a === '-p' || a === '-d' || a === '-o' || a === '-r') k++; continue; }
      files.push(a);
    }
    const texts = []; const explicit = [];
    for (const f of files) {
      let t = null; try { const p = H.absOf(f, cur); const st = fs.statSync(p); if (st.isFile() && st.size < 4e6) t = fs.readFileSync(p, 'utf8'); } catch { /* not a readable file */ }
      if (t !== null && H.looksLikeDiff(t)) texts.push(t); else explicit.push(f);
    }
    if (/^(?:\+\+\+|---) \S/m.test(cmd) || /^diff --git /m.test(cmd)) texts.push(cmd);
    if (verb === 'patch') for (const f of explicit) out.reasons.push(...H.judgeTarget(f, cur, lc, 'patch', false));
    const what = verb === 'patch' ? 'patch' : 'git apply';
    for (const t of texts) for (const name of H.diffTargets(t)) {
      const parts = name.replace(/\\/g, '/').split('/').filter(Boolean);
      const levels = strip !== null ? [strip] : [0, 1, 2, 3, 4, 5];
      for (const n of levels) {
        if (n >= parts.length) continue;
        const rel = parts.slice(n).join('/');
        const hit = H.judgeTarget(rel, cur, lc, what, false);
        if (hit.length) { out.reasons.push(`${what}: the diff changes ${rel}: ` + hit[0].replace(/^[^:]*:\s*/, '')); break; }
      }
    }
  };
  if (verb === 'patch') { diffIn(av, false); out.done = out.reasons.length > 0; return out; }
  if (verb === 'git') {
    let i = 0;
    while (i < av.length && /^-/.test(av[i])) i += (av[i] === '-C' || av[i] === '-c') ? 2 : 1;
    if (String(av[i] || '').toLowerCase() === 'apply') {
      const rest = av.slice(i + 1);
      diffIn(rest, rest.some((x) => /^--(?:check|stat|numstat|summary)$/.test(x)));
    }
  }
  return out;
}
