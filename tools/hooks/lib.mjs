// Shared helpers for the agent-rule hooks. See brief_gate.mjs for the failure policy (FAIL OPEN).
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

// Main checkout = two levels above tools/hooks. Hooks are wired through ${CLAUDE_PROJECT_DIR}, so this is the
// main checkout once merged. State and the contract are always read from here, never from the agent's cwd.
export const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..', '..');
export const STATE_DIR = path.join(ROOT, '.claude', 'state');
export const EXEMPT_TYPES = ['Explore', 'Plan', 'claude-code-guide', 'statusline-setup'];
export const MARKER = 'AGENT-CONTRACT-MARKER: v1';

export function log(entry) {
  try {
    fs.mkdirSync(STATE_DIR, { recursive: true });
    fs.appendFileSync(path.join(STATE_DIR, 'gate.log'), JSON.stringify({ ts: new Date().toISOString(), ...entry }) + '\n');
  } catch { /* logging must never break a gate */ }
}

// Read all of stdin; resolves null on timeout (caller fails open).
export function readStdin(ms = 5000) {
  return new Promise((resolve) => {
    let buf = '';
    const t = setTimeout(() => resolve(null), ms);
    process.stdin.setEncoding('utf8');
    process.stdin.on('data', (d) => { buf += d; });
    process.stdin.on('end', () => { clearTimeout(t); resolve(buf); });
    process.stdin.on('error', () => { clearTimeout(t); resolve(null); });
  });
}

export function parseJson(s) { try { return JSON.parse(s); } catch { return undefined; } }

const isAbs = (p) => path.win32.isAbsolute(p) || path.posix.isAbsolute(p);

// Run the brief tests on a text. Returns { missing: string[], handinDir, noVisual, briefFile }.
export function checkBrief(text) {
  const missing = [];
  const t = text.replace(/\r\n/g, '\n');
  const handinMatch = t.match(/^HANDIN_DIR:[ \t]*(.+?)[ \t]*$/m);
  const handinDir = handinMatch ? handinMatch[1].replace(/^`|`$/g, '') : null;
  if (!t.includes(MARKER)) missing.push(`the agent contract (marker line "${MARKER}"; build the brief with: node tools/make_brief.mjs <task-file> --round <name>)`);
  if (!handinDir) missing.push('a line "HANDIN_DIR: <absolute path>" (make_brief.mjs emits it)');
  else if (!isAbs(handinDir)) missing.push(`HANDIN_DIR must be an absolute path (got "${handinDir}")`);
  const ow = t.match(/^##[ \t]+Owner['’]s words[^\n]*\n([\s\S]*?)(?=^##[ \t]|^---[ \t]*$|(?![\s\S]))/m);
  const owBody = ow ? ow[1].trim() : '';
  if (!ow || owBody.length < 10 || /\{\{/.test(owBody) || !/["“”]/.test(owBody)) {
    missing.push('a non-empty "## Owner\'s words" section containing the owner\'s verbatim quote in quotation marks');
  }
  if (/time-box|whole task/i.test(t)) missing.push('remove "time-box" / "whole task" wording: the finish line is the photo, not an item list');
  return { missing, handinDir, noVisual: /^HANDIN:[ \t]*no-visual[ \t]*$/m.test(t) };
}

export function noContractReason(text) {
  const m = text.match(/^NO-CONTRACT:[ \t]*(.+?)[ \t]*$/m);
  return m && m[1].length >= 20 ? m[1] : null;
}

export function briefFileOf(text) {
  const m = text.match(/^BRIEF-FILE:[ \t]*(.+?)[ \t]*$/m);
  return m ? m[1].replace(/^`|`$/g, '') : null;
}

// First user message of a subagent transcript (JSONL, first line type "user", message.content string or blocks).
export function firstUserText(transcriptPath) {
  const raw = fs.readFileSync(transcriptPath, 'utf8');
  for (const line of raw.split('\n')) {
    if (!line.trim()) continue;
    const o = parseJson(line);
    if (!o || o.type !== 'user' || !o.message) continue;
    const c = o.message.content;
    if (typeof c === 'string') return c;
    if (Array.isArray(c)) return c.map((b) => (typeof b === 'string' ? b : b && b.text) || '').join('\n');
  }
  return '';
}
