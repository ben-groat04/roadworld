// Path helpers of state_guard (gates14): home expansion, brace expansion, glob reach, the secret's folder. Node built-ins and lib.mjs only. No regular expression is built from input text
// except through reEsc / safeRe (gates13 decision 8).
import os from 'node:os';
import path from 'node:path';
import { reEsc, safeRe, SECRET_HOME_DIR } from './lib.mjs';

export const norm = (p) => String(p || '').replace(/\\/g, '/').replace(/\/+/g, '/').toLowerCase();
export const winPath = (p) => (/^\/([a-zA-Z])(\/|$)/.test(p) ? p[1] + ':' + p.slice(2) : p);
export const hasGlob = (s) => /[*?]|\[[^\]]+\]/.test(s);

// ~, $HOME, ${HOME}, $USERPROFILE, $env:USERPROFILE and %USERPROFILE% at the start of a path are the user's home (where the ack secret lives)
export const expandHome = (p) => String(p).replace(/^(?:~|\$\{?HOME\}?|\$\{?USERPROFILE\}?|\$env:USERPROFILE|%USERPROFILE%)(?=[\\/]|$)/i, () => os.homedir().replace(/\\/g, '/'));
export const absOf = (p, cwd) => { const w = winPath(expandHome(String(p)).replace(/\\/g, '/')); return path.resolve(/^[A-Za-z]:|^\//.test(w) ? w : path.join(cwd || process.cwd(), w)); };

// bash brace expansion of one word ({state,x}/ack_secret, a{b,c}d); at most 64 results
export function braceExpand(w) {
  let out = [String(w)];
  for (let guard = 0; guard < 6; guard++) {
    const next = []; let changed = false;
    for (const x of out) {
      const m = x.match(/^(.*?)\{([^{}]*,[^{}]*)\}(.*)$/s);
      if (!m) { next.push(x); continue; }
      changed = true; for (const alt of m[2].split(',')) next.push(m[1] + alt + m[3]);
    }
    out = next.slice(0, 64); if (!changed) break;
  }
  return out;
}

// one glob segment (* ? [abc] [a-c] [!a]) as a regular expression, built only from reEsc'd pieces
export function globSegRe(g) {
  let r = '^';
  for (let i = 0; i < g.length; i++) {
    const c = g[i];
    if (c === '*') r += '.*';
    else if (c === '?') r += '.';
    else if (c === '[') {
      const j = g.indexOf(']', i + 1);
      if (j > i + 1) { const cls = g.slice(i + 1, j).replace(/^!/, '^').replace(/\\/g, '\\\\'); r += '[' + cls + ']'; i = j; } else r += '\\[';
    } else r += reEsc(c);
  }
  return safeRe(r + '$', 'i');
}

// Can the glob (an absolute path whose segments may hold wildcards) name the target folder, or something below it ('into'), or the folder or one of its parents ('above', so that a
// recursive command on it reaches the folder)? Both are compared segment by segment, case-insensitively.
export function globReaches(absPattern, targetAbs, mode = 'into') {
  const P = norm(absPattern).replace(/\/$/, '').split('/'), T = norm(targetAbs).replace(/\/$/, '').split('/');
  if (mode === 'into' && P.length < T.length) return false;
  if (mode === 'above' && P.length > T.length) return false;
  const n = mode === 'into' ? T.length : P.length;
  for (let i = 0; i < n; i++) { const g = P[i]; if (g === T[i]) continue; if (!hasGlob(g)) return false; if (!globSegRe(g).test(T[i])) return false; }
  return true;
}

// the secret's own folder (outside every repo tree): any text, path or glob that can resolve into it
export function touchesSecretHome(p, cwd) {
  const raw = String(p);
  if (/\.roadworld-gate/i.test(raw)) return true;
  const abs = absOf(raw, cwd); const sh = norm(SECRET_HOME_DIR); const n = norm(abs);
  if (n === sh || n.startsWith(sh + '/')) return true;
  return hasGlob(raw) && globReaches(abs, SECRET_HOME_DIR, 'into');
}
