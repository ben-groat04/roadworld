// Shared helpers for the agent-rule hooks. See brief_gate.mjs for the failure policy (FAIL OPEN).
import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import { fileURLToPath } from 'node:url';
import { resolveMainRoot } from './root.mjs';

// Main checkout, resolved explicitly (root.mjs: through git's worktree link files, never just "where this file sits"). State and the contract are read and written ONLY here.
export const ROOT = resolveMainRoot(path.dirname(fileURLToPath(import.meta.url)));
// CP_GATE_STATE_DIR redirects ALL state (log, records, secret) for tests: a test run must never write into the real gate.log of the main checkout (23 Oct 2026: it did, and raised gate notices).
export const STATE_DIR = process.env.CP_GATE_STATE_DIR ? path.resolve(process.env.CP_GATE_STATE_DIR) : path.join(ROOT, '.claude', 'state');
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

// gates13: NO-CONTRACT is an allow-list: `NO-CONTRACT: <category>: <reason, 20+ chars>`. form: 'none' (no line) | 'ok' | 'short' (category fine, reason under 20) | 'free' (no category: the old free text)
export const NO_CONTRACT_CATEGORIES = ['clerk', 'merge', 'research', 'status', 'measure'];
export function noContractParse(text) {
  const m = String(text || '').match(/^NO-CONTRACT:[ \t]*(.+?)[ \t]*$/m);
  if (!m) return { form: 'none' };
  const c = m[1].match(new RegExp('^(' + NO_CONTRACT_CATEGORIES.join('|') + ')[ \\t]*:[ \\t]*(.*)$', 'i'));
  if (!c) return { form: 'free', text: m[1] };
  const reason = c[2].trim();
  return reason.length >= 20 ? { form: 'ok', category: c[1].toLowerCase(), reason } : { form: 'short', category: c[1].toLowerCase(), text: m[1] };
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

// ---- secret + signed records: TAMPER EVIDENCE ONLY. Builder and audit records are HMAC-signed with .claude/state/ack_secret so that a hand-edited record does not verify.
// The trust in a record comes from WHO writes it (only the hooks, with the harness's agent_id; see audit_lib.mjs) and from tools/hooks/state_guard.mjs keeping subagents away from
// the state folder and the secret. LIMIT: an agent that bypasses the guard (a script file that touches the state) could sign; that is not claimed to be impossible.
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

// The transcript file of the subagent that raised a hook event (SubagentStop carries it; a PreToolUse event carries only the main transcript_path: the subagent file sits beside it).
export function agentTranscript(input) {
  if (input.agent_transcript_path && fs.existsSync(input.agent_transcript_path)) return input.agent_transcript_path;
  if (input.transcript_path && input.agent_id) {
    const tp = input.transcript_path;
    const guess = path.join(tp.replace(/\.jsonl$/, ''), 'subagents', `agent-${input.agent_id}.jsonl`);
    if (fs.existsSync(guess)) return guess;
  }
  return null;
}

// The HANDIN_DIR a subagent was launched with (its brief, or the BRIEF-FILE the brief names), or null when it carried none or cannot be read.
export function handinDirOfAgent(input) {
  try {
    const tp = agentTranscript(input);
    if (!tp) return null;
    const text = firstUserText(tp);
    const own = checkBrief(text).handinDir;
    if (own) return own;
    const bf = briefFileOf(text);
    if (bf) return checkBrief(fs.readFileSync(bf, 'utf8')).handinDir || null;
  } catch { /* unreadable: no HANDIN_DIR known */ }
  return null;
}

// gates8: everything the launch of a subagent says that the lock guard needs: its HANDIN_DIR (a builder has one), the PRIOR-AUDIT path of a send-back round, and whether it is an auditor
// (AUDIT-OF). Read from the agent's own transcript (first user message) and the BRIEF-FILE it names. Cached per agent id under STATE_DIR/launch_cache (the transcript of a long run can be big
// and a hook runs on every tool call). Returns null when nothing can be read.
export function launchInfoOfAgent(input) {
  try {
    const id = String(input.agent_id || '').replace(/[^\w.-]/g, '_');
    if (!id) return null;
    const cf = path.join(STATE_DIR, 'launch_cache', id + '.json');
    try { const c = parseJson(fs.readFileSync(cf, 'utf8')); if (c && c.v === 1) return c.info; } catch { /* not cached yet */ }
    const tp = agentTranscript(input);
    if (!tp) return null;
    const text = firstUserText(tp);
    let all = text;
    const bf = briefFileOf(text);
    if (bf) { try { all += '\n' + fs.readFileSync(bf, 'utf8'); } catch { /* the brief file is unreadable: judge by the prompt */ } }
    const m = all.match(/^PRIOR-AUDIT:[ \t]*(.+?)[ \t]*$/m);
    const info = { handinDir: checkBrief(all).handinDir || null, priorAudit: m ? m[1].replace(/^`|`$/g, '') : null, auditor: /^AUDIT-OF:[ \t]*\S/m.test(all) };
    try { fs.mkdirSync(path.dirname(cf), { recursive: true }); fs.writeFileSync(cf, JSON.stringify({ v: 1, info })); } catch { /* cache only */ }
    return info;
  } catch { return null; }
}
