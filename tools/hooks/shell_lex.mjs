// A small shell lexer shared by the two PreToolUse guards (state_guard, delete_guard). Node built-ins only.
// It reads a Bash or PowerShell command line the way a person reads it: quotes removed, adjacent quoted and bare pieces joined (rm -rf docs/ref""erence is
// rm -rf docs/reference), heredoc bodies dropped (they are text), segments split at ; && || | & newline and at PowerShell blocks ( ) { }, redirects pulled
// out of the words, leading VAR=x and $x = assignments recorded, `if (...) { ... }`, `try { ... } catch { ... }`, `foreach (...)` wrappers reduced to their statements.
// WHAT IT CANNOT SEE: variables built in several steps, command substitution results, aliases, script files the command only runs, here-strings other than
// the heredoc form below. Callers treat what they cannot resolve as unknown and decide per rule.

export function stripHeredocs(cmd) {
  // <<EOF ... EOF, <<'EOF', <<-EOF, and PowerShell here-strings @' ... '@ / @" ... "@
  let s = String(cmd).replace(/\r\n/g, '\n');
  s = s.replace(/<<-?[ \t]*(['"]?)([A-Za-z_][\w]*)\1[^\n]*\n[\s\S]*?\n[ \t]*\2[ \t]*(?=\n|$)/g, (m) => m.slice(0, m.indexOf('\n')).replace(/<<-?[ \t]*(['"]?)[A-Za-z_]\w*\1/, ''));
  s = s.replace(/@(['"])[ \t]*\n[\s\S]*?\n\1@/g, '"here-string"');
  return s;
}

const SEP_BASH = new Set([';', '&', '|', '\n']);

// returns { segments: [{ words: [{v, q}], redirects: [{op, target}], assigns: {name: value} }] }
export function parseCommand(command) {
  const s = stripHeredocs(command);
  const segs = []; let cur = { words: [], redirects: [], assigns: {} };
  let w = null;                  // word under construction { v, q }
  let pendingRedirect = null;    // op waiting for its target word
  const endWord = () => {
    if (!w) return;
    if (pendingRedirect) { cur.redirects.push({ op: pendingRedirect, target: w.v }); pendingRedirect = null; }
    else cur.words.push(w);
    w = null;
  };
  const endSeg = () => {
    endWord(); pendingRedirect = null;
    // leading assignments: VAR=value, $x=value (PowerShell: `$x = value` arrives as `$x`, `=`, value)
    let ws = cur.words;
    for (;;) {
      if (ws.length && /^\$?[A-Za-z_][\w:]*=/.test(ws[0].v) && !ws[0].q) { const m = ws[0].v.match(/^\$?([A-Za-z_][\w:]*)=([\s\S]*)$/); cur.assigns[m[1].toLowerCase()] = m[2]; ws = ws.slice(1); if (!m[2] && ws.length) { cur.assigns[m[1].toLowerCase()] = ws[0].v; ws = ws.slice(1); } continue; }
      if (ws.length >= 2 && /^\$[A-Za-z_][\w:]*$/.test(ws[0].v) && ws[1].v === '=') { const nm = ws[0].v.slice(1).toLowerCase(); if (ws.length === 3) { cur.assigns[nm] = ws[2].v; ws = []; } else { cur.assigns[nm] = null; ws = ws.slice(2); } continue; }
      break;
    }
    cur.words = ws;
    if (cur.words.length || cur.redirects.length || Object.keys(cur.assigns).length) segs.push(cur);
    cur = { words: [], redirects: [], assigns: {} };
  };
  const n = s.length;
  for (let i = 0; i < n; i++) {
    const c = s[i];
    if (c === "'" || c === '"') {
      const j = s.indexOf(c, i + 1);
      const body = j < 0 ? s.slice(i + 1) : s.slice(i + 1, j);
      w = w || { v: '', q: false }; w.v += body; w.q = true;
      i = j < 0 ? n : j;
      continue;
    }
    if (c === '`' && s[i + 1] !== undefined) { w = w || { v: '', q: false }; w.v += s[i + 1]; i++; continue; }   // PowerShell escape; bash command substitution backticks are rare here
    if (c === ' ' || c === '\t') { endWord(); continue; }
    if (c === '\n' || c === ';') { endSeg(); continue; }
    if (c === '|' || c === '&') {
      if (c === '&' && (s[i + 1] === '>' )) { endWord(); pendingRedirect = '&>'; i += s[i + 2] === '>' ? 2 : 1; continue; }
      if (c === '&' && w === null && cur.words.length === 0) { /* PowerShell call operator: keep as a word */ w = { v: '&', q: false }; endWord(); continue; }
      endSeg(); if (s[i + 1] === c) i++; continue;
    }
    if (c === '(' || c === ')') { if (c === '(' && w && w.v === '$') { w = null; } endSeg(); continue; }
    if (c === '{' || c === '}') {
      const prevWs = !w; const nextWs = s[i + 1] === undefined || /[\s;)]/.test(s[i + 1]);
      if ((prevWs && nextWs) || (c === '}' && !w)) { endSeg(); continue; }
      if (c === '{' && w && w.v === '$') { w.v += c; continue; }
      w = w || { v: '', q: false }; w.v += c; continue;
    }
    if (c === '>' || c === '<') {
      // 2>&1, >&2: no target
      if (c === '>' && s[i + 1] === '&' && /\d|-/.test(s[i + 2] || '')) { // fd duplication
        if (w && /^\d+$/.test(w.v)) w = null; endWord(); i += 2; continue;
      }
      let op = c; if (w && /^\d+$/.test(w.v) && !w.q) w = null; else endWord();
      if (s[i + 1] === c) { op += c; i++; }
      if (c === '<') continue;            // input redirect: the next word is read as an ordinary word
      pendingRedirect = op; continue;
    }
    w = w || { v: '', q: false }; w.v += c;
  }
  endSeg();
  return { segments: segs };
}

export const WRAPPER_VERBS = new Set(['&', 'call', 'sudo', 'command', 'time', 'nohup', 'nice', 'env', 'exec', 'builtin', 'start-process', 'invoke-expression', 'iex']);
// the verb of a segment: first word after wrappers and VAR=x prefixes (env FOO=1 rm ...), lowercased, no directory, no .exe
export function verbOf(words) {
  let i = 0;
  while (i < words.length) {
    const v = words[i].v;
    if (WRAPPER_VERBS.has(v.toLowerCase())) { i++; continue; }
    if (/^[A-Za-z_]\w*=/.test(v) && !words[i].q) { i++; continue; }
    if (i > 0 && /^-/.test(v) && WRAPPER_VERBS.has(String(words[i - 1].v).toLowerCase())) { i++; continue; }   // env -i
    break;
  }
  const raw = words[i] ? words[i].v : '';
  return { verb: raw.toLowerCase().replace(/^.*[\/]/, '').replace(/\.exe$/, ''), index: i, args: words.slice(i + 1) };
}
