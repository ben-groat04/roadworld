// A small shell lexer shared by the two PreToolUse guards (state_guard, delete_guard). Node built-ins only.
// It reads a Bash or PowerShell command line the way a person reads it: quotes removed, adjacent quoted and bare pieces joined (rm -rf docs/ref""erence is
// rm -rf docs/reference), heredoc bodies dropped (they are text), segments split at ; && || | & newline and at PowerShell blocks ( ) { }, redirects pulled
// out of the words, leading VAR=x and $x = assignments recorded, `if (...) { ... }`, `try { ... } catch { ... }`, `foreach (...)` wrappers reduced to their statements.
// gates7: a { or } glued to a word is a block token too (try {Remove-Item x} catch {}, if (1){..}, & {..}); {} (find placeholder), {a,b} brace lists and ${x} stay words, except that {rm,-rf,docs} as the
// first word of a command is split into its three words (bash expands it). The statements inside the body of a piped ForEach-Object / Where-Object carry piped and fe.
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
// index of the } that closes the { at i (quotes skipped), or -1
function matchingBrace(s, i) {
  let depth = 0;
  for (let j = i; j < s.length; j++) {
    const ch = s[j];
    if (ch === "'" || ch === '"') { const k = s.indexOf(ch, j + 1); if (k < 0) return -1; j = k; continue; }
    if (ch === '{') depth++;
    else if (ch === '}') { depth--; if (depth === 0) return j; }
  }
  return -1;
}

export function parseCommand(command) {
  const s = stripHeredocs(command);
  const segs = []; let cur = { words: [], redirects: [], assigns: {} };
  let w = null;                  // word under construction { v, q }
  let pipeNext = false, curPiped = false, pendingHere = false;   // seg.piped = receives the previous segment's output; seg.here = the word after <<<
  const KEYW = new Set(['then', 'do', 'else', 'elif', 'if', 'while', 'until', '!']);
  let pendingRedirect = null;    // op waiting for its target word
  const blocks = [];             // open blocks { fe }: fe = the body of a piped ForEach-Object / Where-Object (its statements read what the pipeline fed)
  let dollarBraces = 0;          // open ${...} inside the word under construction
  const FE = new Set(['foreach-object', 'foreach', '%', 'where-object', 'where', '?']);
  const endWord = () => {
    if (!w) return;
    if (pendingHere) { cur.here = w.v; pendingHere = false; w = null; return; }
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
      if (ws.length >= 2 && /^\$[A-Za-z_][\w:]*$/.test(ws[0].v) && ws[1].v === '=') { const nm = ws[0].v.slice(1).toLowerCase(); if (ws.length === 3) { cur.assigns[nm] = (/::|^\[|^[A-Za-z]+-[A-Za-z]+$/.test(ws[2].v) && !ws[2].q) ? null : ws[2].v; ws = []; }
          else if (ws[2] && ws[2].v.toLowerCase() === 'join-path') { const pos = []; for (let k = 3; k < ws.length; k++) { if (/^-(?:path|childpath|additionalchildpath)$/i.test(ws[k].v)) continue; pos.push(ws[k].v); } cur.assigns[nm] = pos.length >= 2 ? pos.join(String.fromCharCode(92)) : null; ws = []; } else { cur.assigns[nm] = null; ws = ws.slice(2); } continue; }
      break;
    }
    while (ws.length > 1 && KEYW.has(ws[0].v) && !ws[0].q) ws = ws.slice(1);
    if (ws.length === 1 && KEYW.has(ws[0].v) && !ws[0].q) ws = [];
    cur.words = ws; cur.piped = curPiped; curPiped = pipeNext; pipeNext = false;
    if (blocks.length && blocks[blocks.length - 1].fe) { cur.piped = true; cur.fe = true; }
    if (cur.words.length || cur.redirects.length || Object.keys(cur.assigns).length) segs.push(cur);
    cur = { words: [], redirects: [], assigns: {} };
  };
  const n = s.length; const subs = [];
  for (let i = 0; i < n; i++) {
    const c = s[i];
    if (c === "'" && w && !w.q && w.v.endsWith('$')) w.v = w.v.slice(0, -1);   // bash $'...' quoting
    if (c === "'" || c === '"') {
      const j = s.indexOf(c, i + 1);
      const body = j < 0 ? s.slice(i + 1) : s.slice(i + 1, j);
      w = w || { v: '', q: false }; w.v += body; w.q = true;
      i = j < 0 ? n : j;
      continue;
    }
    if (c === '`' && s.indexOf('`', i + 1) > 0) { const j = s.indexOf('`', i + 1); const inner = s.slice(i + 1, j); w = w || { v: '', q: false }; w.v += '$(' + inner + ')'; subs.push(inner); i = j; continue; }   // bash backtick substitution: value unknown, inside judged
    if (c === '`' && s[i + 1] !== undefined) { w = w || { v: '', q: false }; w.v += s[i + 1]; i++; continue; }   // PowerShell escape; bash command substitution backticks are rare here
    if (c === ' ' || c === '\t') { endWord(); continue; }
    if (c === '#' && w === null && !pendingRedirect && s[i + 1] !== '>') { while (i < n && s[i] !== '\n') i++; i--; continue; }   // a comment starts at a word start
    if (c === '\n' || c === ';') { endSeg(); continue; }
    if (c === '|' || c === '&') {
      if (c === '&' && (s[i + 1] === '>' )) { endWord(); pendingRedirect = '&>'; i += s[i + 2] === '>' ? 2 : 1; continue; }
      if (c === '&' && w === null && cur.words.length === 0) { /* PowerShell call operator: keep as a word */ w = { v: '&', q: false }; endWord(); continue; }
      if (c === '|' && s[i + 1] !== '|') pipeNext = true;
      endSeg(); if (s[i + 1] === c) i++; continue;
    }
    if (c === '(' && w && w.v.endsWith('$')) {   // command substitution: the word stays (its value is unknown), its inside is also judged as commands
      let depth = 1, j = i + 1; while (j < n && depth) { if (s[j] === '(') depth++; else if (s[j] === ')') depth--; j++; }
      const inner = s.slice(i + 1, depth ? n : j - 1); w.v += '(' + inner + ')'; subs.push(inner); i = j - 1; continue;
    }
    if (c === '(' || c === ')') { endSeg(); continue; }
    if (c === '{' || c === '}') {
      if (c === '{' && w && w.v.endsWith('$')) { w.v += c; dollarBraces++; continue; }                  // ${name}
      if (c === '}' && dollarBraces > 0 && w) { dollarBraces--; w.v += c; continue; }
      if (c === '{') {
        // a block opener (spaced or glued to the verb: try {Remove-Item x}, if (1){...}, & {...}, ForEach-Object{...}), a brace expansion {a,b} or the find placeholder {}
        const close = matchingBrace(s, i);
        const inner = close < 0 ? '' : s.slice(i + 1, close);
        const lone = !w && (s[i + 1] === undefined || /[\s;)]/.test(s[i + 1]));
        if (close >= 0 && inner === '') { w = w || { v: '', q: false }; w.v += '{}'; i = close; continue; }
        if (close >= 0 && /,/.test(inner) && !/\s/.test(inner)) {
          // {rm,-rf,docs} as the FIRST word of a command is a command line in bash (brace expansion makes it three words): split it
          const after = s[close + 1];
          if (!w && cur.words.length === 0 && !pendingRedirect && !pendingHere && (after === undefined || /[\s;|&)]/.test(after))) { for (const it of inner.split(',')) cur.words.push({ v: it, q: false }); i = close; continue; }
          w = w || { v: '', q: false }; w.v += s.slice(i, close + 1); i = close; continue;
        }
        if (lone || (close >= 0 && /[\s;|&(]/.test(inner))) {
          endSeg();
          const prev = segs[segs.length - 1];
          const fe = !!(prev && prev.piped && prev.words.length === 1 && FE.has(String(prev.words[0].v).toLowerCase()));
          blocks.push({ fe }); continue;
        }
        w = w || { v: '', q: false }; w.v += c; continue;
      }
      // c === '}'
      if (blocks.length) { endSeg(); blocks.pop(); continue; }
      if (!w) { endSeg(); continue; }
      w.v += c; continue;
    }
    if (c === '>' || c === '<') {
      // 2>&1, >&2: no target
      if (c === '>' && s[i + 1] === '&' && /\d|-/.test(s[i + 2] || '')) { // fd duplication
        if (w && /^\d+$/.test(w.v)) w = null; endWord(); i += 2; continue;
      }
      if (c === '<' && s.startsWith('<<<', i)) { endWord(); pendingHere = true; i += 2; continue; }
      let op = c; if (w && /^\d+$/.test(w.v) && !w.q) w = null; else endWord();
      if (s[i + 1] === c) { op += c; i++; }
      if (c === '<') continue;            // input redirect: the next word is read as an ordinary word
      pendingRedirect = op; continue;
    }
    w = w || { v: '', q: false }; w.v += c;
  }
  endSeg();
  for (const inner of subs) segs.push(...parseCommand(inner).segments);
  return { segments: segs };
}

export const WRAPPER_VERBS = new Set(['&', 'call', 'sudo', 'doas', 'command', 'time', 'nohup', 'nice', 'env', 'exec', 'builtin', 'busybox', 'timeout', 'stdbuf', 'setsid', 'ionice', 'start-process', 'invoke-expression', 'iex', 'start-job']);
// the verb of a segment: first word after wrappers and VAR=x prefixes (env FOO=1 rm ..., timeout 5 rm ..., busybox rm ..., \rm), lowercased, no directory, no .exe
export function verbOf(words) {
  let i = 0;
  while (i < words.length) {
    const v = words[i].v.replace(/^\\/, '');
    const lv = v.toLowerCase();
    if (WRAPPER_VERBS.has(lv)) {
      i++;
      if (lv === 'timeout') { while (i < words.length && /^-/.test(words[i].v)) i += /^-[ks]$/.test(words[i].v) ? 2 : 1; if (i < words.length && /^\d+(\.\d+)?[smhd]?$/.test(words[i].v)) i++; }
      else if (lv === 'nice' || lv === 'ionice') { while (i < words.length && /^-/.test(words[i].v)) { i += /^-[nc]$/.test(words[i].v) ? 2 : 1; } }
      else if (lv === 'sudo' || lv === 'doas') { while (i < words.length && /^-/.test(words[i].v)) i += /^-[ugCT]$/.test(words[i].v) ? 2 : 1; }
      continue;
    }
    if (/^[A-Za-z_]\w*=/.test(v) && !words[i].q) { i++; continue; }
    if (i > 0 && /^-/.test(v) && WRAPPER_VERBS.has(String(words[i - 1].v).toLowerCase())) { i++; continue; }   // env -i
    break;
  }
  const raw = words[i] ? words[i].v.replace(/^\\/, '') : '';
  return { verb: raw.toLowerCase().replace(/^.*[\/\\]/, '').replace(/\.exe$/, ''), index: i, args: words.slice(i + 1) };
}
