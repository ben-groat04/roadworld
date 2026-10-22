// Shared helpers for the agent-rule hooks. See brief_gate.mjs for the failure policy (FAIL OPEN).
import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
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
  // the owner's verbatim words are exempt (an owner quote may mention a time-box or a whole task)
  const tNoOwner = ow ? t.replace(ow[0], '') : t;
  if (/time-box|whole task/i.test(tNoOwner)) missing.push('remove "time-box" / "whole task" wording: the finish line is the photo, not an item list');
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

// ---- secret + signed records. The secret (.claude/state/ack_secret) is created by the notice hook and read by nothing a subagent is
// told about; builder and audit records are HMAC-signed with it so a hand-typed JSON file does not verify. LIMIT: an agent that reads
// the secret file (or writes gate.log, which findBuilderInLog reads) has gone out of its way; this stops accidents and shortcuts, not sabotage.
export function readSecret(stateDir = STATE_DIR) { try { return fs.readFileSync(path.join(stateDir, 'ack_secret'), 'utf8').trim() || null; } catch { return null; } }
export function ensureSecret(stateDir = STATE_DIR) {
  const have = readSecret(stateDir);
  if (have) return have;
  const s = crypto.randomBytes(12).toString('hex');
  fs.mkdirSync(stateDir, { recursive: true });
  // exclusive create: two hook processes starting together must end up with ONE secret (the loser reads the winner's), or one of their signed records would not verify
  try { fs.writeFileSync(path.join(stateDir, 'ack_secret'), s, { flag: 'wx' }); } catch (e) { const other = readSecret(stateDir); if (other) return other; throw e; }
  return s;
}
const canon = (o) => JSON.stringify(Object.keys(o).filter((k) => k !== 'sig').sort().map((k) => [k, o[k]]));
export function signRecord(rec, stateDir = STATE_DIR) { const sec = ensureSecret(stateDir); return { ...rec, sig: crypto.createHmac('sha256', sec).update(canon(rec)).digest('hex') }; }
export function verifyRecord(rec, stateDir = STATE_DIR) {
  const sec = readSecret(stateDir);
  if (!rec || !sec || typeof rec.sig !== 'string') return false;
  const want = crypto.createHmac('sha256', sec).update(canon(rec)).digest('hex');
  return want.length === rec.sig.length && crypto.timingSafeEqual(Buffer.from(want), Buffer.from(rec.sig));
}

// Fallback logger for guard.mjs: uses nothing but fs/path so a broken library cannot stop the FAIL-OPEN line being written.
