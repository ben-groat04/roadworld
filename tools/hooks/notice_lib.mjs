// Gate notices (gate_notice.mjs, gate_ack.mjs) and the hooks-are-dead canary (make_brief.mjs, check_handin.mjs).
// Everything here works on parsed gate.log lines and takes the state directory as a parameter, so tests and the two
// tools can point it at any checkout.
import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import { ROOT, STATE_DIR, parseJson, readSecret, ensureSecret } from './lib.mjs';
import { auditOwed } from './audit_lib.mjs';

export const MIN_ACK_CHARS = 20;
export const CANARY_GRACE_MS = 120 * 1000;          // a brief made less than 2 minutes ago may simply not be launched yet
export const NOTICE_STALE_MS = 3 * 3600 * 1000;     // the notice hook runs at least once per prompt / background report
export const HANDIN_STALE_MS = 6 * 3600 * 1000;      // no agent has finished for this long while agents were started
export const ACK_CMD = 'node tools/hooks/gate_ack.mjs "<n>:<code> [<n>:<code> ...]" "<what was done about it>"';

// Only the main checkout (where .git is a directory) owns the live gate.log; a worktree's copy is never fed by hooks.
export function isMainCheckout(root = ROOT) {
  try { return fs.statSync(path.join(root, '.git')).isDirectory(); } catch { return false; }
}

// All parsable gate.log lines as { n, ...entry }, n = 1-based line number. Unreadable file: [].
export function readGateLog(stateDir = STATE_DIR) { // n = 1-based number of the non-empty line
  let raw;
  try { raw = fs.readFileSync(path.join(stateDir, 'gate.log'), 'utf8'); } catch { return []; }
  const out = [];
  let n = 0;
  for (const line of raw.split('\n')) {
    if (!line.trim()) continue;
    n++; // numbered over non-empty lines, as gate_ack counts them
    const o = parseJson(line);
    if (o && typeof o === 'object') out.push({ n, ...o });
  }
  return out;
}

const clip = (s, k = 160) => { s = String(s == null ? '' : s).replace(/\s+/g, ' ').trim(); return s.length > k ? s.slice(0, k - 1) + '...' : s; };

// Unacknowledged items = log lines not covered by an ack (per item: { gate: 'ack', items: [n...] }; legacy blanket: through_line).
export function pendingItems(entries, opts = {}) {
  let through = 0;                  // legacy blanket acks (through_line) still honoured for old logs
  const acked = new Set();          // per-item acks: { gate: 'ack', items: [line numbers] }
  for (const e of entries) {
    if (e.gate !== 'ack') continue;
    if (Number.isFinite(e.through_line)) through = Math.max(through, e.through_line);
    if (Array.isArray(e.items)) for (const n of e.items) acked.add(n);
  }
  const live = entries.filter((e) => e.n > through && e.gate !== 'ack');
  const failOpen = live.filter((e) => /FAIL-OPEN/.test(String(e.reason || '')) && !acked.has(e.n));
  // one item per HANDIN_DIR: the NEWEST release stands for the group, so a fresh release after an ack shows up again
  const seen = new Set();
  const handinFailed = live.filter((e) => e.gate === 'handin' && /^check still failing after/.test(String(e.reason || ''))).reverse()
    .filter((e) => { const k = e.handin_dir || e.agent_id; if (seen.has(k)) return false; seen.add(k); return true; }).reverse()
    .filter((e) => !acked.has(e.n));
  // NO-CONTRACT launches are ONE item (the newest line stands for the group; acking it acks every line in `ns`): a summary, not one item per launch
  const ncAll = live.filter((e) => e.gate === 'brief' && e.reason === 'NO-CONTRACT' && !acked.has(e.n));
  const noContract = ncAll.length ? [{ ...ncAll[ncAll.length - 1], ns: ncAll.map((e) => e.n), count: ncAll.length, reasons: [...new Set(ncAll.map((e) => e.no_contract_reason))] }] : [];
  // an agent that finished with BLOCKED items (an honest "cannot do this") is news for the main session; a NO-CONTRACT launch of an audit was refused
  const blocked = live.filter((e) => e.gate === 'blocked' && e.reason === 'BLOCKED' && !acked.has(e.n));
  const ncAudit = live.filter((e) => e.gate === 'brief' && e.reason === 'NO-CONTRACT audit launch refused' && !acked.has(e.n));
  // auditOwed (opts.stateDir/opts.root, default the main checkout): hand-ins that passed with no valid AUDIT.md by a different
  // agent. Computed from builder records + the files themselves, NOT from the log, so no ack can clear it.
  let owed = [];
  try { owed = auditOwed(opts.stateDir || STATE_DIR, opts.root || ROOT); } catch { /* fail open: logged by the caller if it matters */ }
  return { failOpen, handinFailed, noContract, blocked, ncAudit, auditOwed: owed, total: failOpen.length + handinFailed.length + noContract.length + blocked.length + ncAudit.length + owed.length };
}

// ---- per-item acknowledgement. An item is acked by its log line number n AND a code only the notice shows: sha1(secret:n).
// The secret is .claude/state/ack_secret, created by the notice hook (the main session's context) and never printed. A subagent
// that does not read that file cannot ack; one that does has gone out of its way (documented limit, audit fault 2).
export const ackItems = (p) => [...p.failOpen, ...p.handinFailed, ...p.noContract, ...(p.blocked || []), ...(p.ncAudit || [])];
export { readSecret, ensureSecret };
export const ackCode = (secret, n) => crypto.createHash('sha1').update(`${secret}:${n}`).digest('hex').slice(0, 6);
// Text must say what was done: enough length, words and variety (rejects "aaaaaaaaaaaaaaaaaaaa" and filler).
export function ackTextProblem(text) {
  const t = String(text || '').trim();
  if (t.length < MIN_ACK_CHARS) return `at least ${MIN_ACK_CHARS} characters (got ${t.length})`;
  if (t.split(/\s+/).length < 4) return 'at least 4 words';
  if (new Set(t.toLowerCase().replace(/\s/g, '')).size < 10) return 'at least 10 different characters (not filler)';
  return null;
}

// Factual text (no imperatives: the hooks docs warn that command-style context can trip prompt-injection defences).
export function formatNotice(p, secret = null) {
  if (!p.total) return '';
  const id = (e) => `#${e.n}${secret ? '[' + ackCode(secret, e.n) + ']' : ''}`;
  const L = [`GATE NOTICE from tools/hooks/gate_notice.mjs (source: .claude/state/gate.log and .claude/state/builders of the main checkout). ${p.total} open item(s).`];
  if (p.auditOwed.length) {
    L.push(`- AUDIT OWED x${p.auditOwed.length}: a builder's hand-in passed the check and no independent auditor has written a valid AUDIT.md. Builder and auditor are never the same agent; there is no cap on auditors. Cleared only by that file (an ack does nothing):`);
    for (const e of p.auditOwed.slice(0, 10)) L.push(`    ${e.handin_dir} (builder ${e.builder_type || '?'}/${e.builder_id}, hand-in passed ${e.passed_at}) - ${clip(e.problems[0], 110)}`);
    L.push('    start one with: node tools/make_brief.mjs <task-file> --audit <that folder>');
  }
  const more = (arr, k) => (arr.length > k ? `    (+${arr.length - k} older not shown)` : null);
  if (p.failOpen.length) {
    L.push(`- FAIL-OPEN x${p.failOpen.length}: a gate did NOT run or did not decide (gates fail open so the session cannot deadlock). Newest first:`);
    for (const e of p.failOpen.slice(-12).reverse()) L.push(`    ${id(e)} ${e.ts} gate=${e.gate} ${clip(e.reason, 90)}${e.error ? ' | ' + clip(e.error, 120) : ''}`);
    if (more(p.failOpen, 12)) L.push(more(p.failOpen, 12));
  }
  if (p.handinFailed.length) {
    L.push(`- HAND-IN RELEASED after 3 refusals x${p.handinFailed.length} (the work did not pass tools/check_handin.mjs; HANDIN_FAILED.txt is in that folder):`);
    for (const e of p.handinFailed.slice(-12).reverse()) L.push(`    ${id(e)} ${e.ts} HANDIN_DIR=${e.handin_dir} agent=${e.agent_type || '?'}/${e.agent_id}`);
    if (more(p.handinFailed, 12)) L.push(more(p.handinFailed, 12));
  }
  if (p.blocked && p.blocked.length) {
    L.push(`- BLOCKED x${p.blocked.length}: an agent finished with items it says it cannot do (STATUS.md "BLOCKED: <item> | tried | blocked by | needs"); this is news, not noise:`);
    for (const e of p.blocked.slice(-8).reverse()) { L.push(`    ${id(e)} ${e.ts} ${clip(e.handin_dir, 90)} agent=${e.agent_type || '?'}/${e.agent_id}`); for (const it of (e.items || []).slice(0, 4)) L.push('      ' + clip(it, 200)); }
  }
  if (p.ncAudit && p.ncAudit.length) {
    L.push(`- NO-CONTRACT AUDIT LAUNCH REFUSED x${p.ncAudit.length} (a launch that named an audit of a progress folder without AUDIT-OF was denied; use make_brief.mjs --audit):`);
    for (const e of p.ncAudit.slice(-8).reverse()) L.push(`    ${id(e)} ${e.ts} ${clip(e.description, 60)} ${clip(e.no_contract_reason, 80)}`);
  }
  if (p.noContract.length) {
    const g = p.noContract[0];
    L.push(`- NO-CONTRACT launches x${g.count} since the last ack (no contract, no hand-in gate); ${g.reasons.length} distinct reason(s), newest: ${g.reasons.slice(-3).reverse().map((r) => '"' + clip(r, 80) + '"').join('; ')} (acks as one item ${id(g)}, last launch ${g.ts})`);
  }
  if (p.total - p.auditOwed.length > 0) {
    L.push(`${p.auditOwed.length ? 'The log items (all except AUDIT OWED) repeat' : 'This notice is repeated'} on every prompt until each is acknowledged BY ITS OWN #n[code]. The only thing that clears an item: ${ACK_CMD} (text of at least ${MIN_ACK_CHARS} characters, 4 words, saying what was done; recorded in the log with a timestamp).`);
  }
  const s = L.join('\n');
  return s.length > 7000 ? s.slice(0, 6990) + '\n[cut]' : s;
}

// Canary: hooks that never run cannot report themselves, so look at the log from the other side.
// RULE 1 (launch hooks): the OLDEST make_brief entry that has no launch-hook line (gate brief or inject) after it is
//   older than CANARY_GRACE_MS: a brief was made and nothing saw an agent launch afterwards.
// RULE 2 (notice hook): the newest successful gate_notice line is older than NOTICE_STALE_MS (or there is none although
//   launch hooks logged more than NOTICE_STALE_MS ago): the notice hook is not running.
// Returns [] when fine, else an array of messages.
export function canaryProblems(entries, now = Date.now()) {
  const out = [];
  const t = (e) => Date.parse(e.ts) || 0;
  const last = (f) => entries.filter(f).reduce((a, e) => (!a || t(e) >= t(a) ? e : a), null);
  const launchHook = last((e) => e.gate === 'brief' || e.gate === 'inject');
  const brief = entries.filter((e) => e.gate === 'make_brief' && (!launchHook || t(e) > t(launchHook))).reduce((a, e) => (!a || t(e) < t(a) ? e : a), null);
  if (brief && now - t(brief) > CANARY_GRACE_MS) {
    out.push(`RULE 1: the oldest brief with no launch after it (round "${brief.round}", made ${brief.ts}) has no launch-hook line (gate brief or inject) after it in gate.log. Either it was never launched, or the PreToolUse/SubagentStart hooks are not running (node missing from the hook environment, settings not loaded).`);
  }
  // RULE 3 (hand-in gate): the OLDEST agent start (gate inject) with no hand-in gate line ('handin', any decision, from any agent) after
  //   it, older than HANDIN_STALE_MS: agents started and none has finished through the gate since, so SubagentStop /
  //   SubagentHandback wiring is probably dead. (A single agent running for hours alone does not trip it after others finish.)
  const lastHandin = last((e) => e.gate === 'handin' && !/FAIL-OPEN/.test(String(e.reason || '')));
  const oldestStart = entries.filter((e) => e.gate === 'inject' && e.decision === 'inject' && (!lastHandin || t(e) > t(lastHandin))).reduce((a, e) => (!a || t(e) < t(a) ? e : a), null);
  if (oldestStart && now - t(oldestStart) > HANDIN_STALE_MS) {
    out.push(`RULE 3: an agent started ${oldestStart.ts} (agent ${oldestStart.agent_type || '?'}/${oldestStart.agent_id || '?'}) and no hand-in gate line (SubagentStop / SubagentHandback) has been logged since, more than ${HANDIN_STALE_MS / 3600000} h later${lastHandin ? ' (last one ' + lastHandin.ts + ')' : ' (none ever)'}. The hand-in gate is not running or agents are not finishing through it.`);
  }
  const notice = last((e) => e.gate === 'notice' && !/FAIL-OPEN/.test(String(e.reason || '')));
  if (notice ? now - t(notice) > NOTICE_STALE_MS : (launchHook && now - t(launchHook) > NOTICE_STALE_MS)) {
    out.push(`RULE 2: no successful gate_notice line ${notice ? 'since ' + notice.ts : 'ever'} (limit ${NOTICE_STALE_MS / 3600000} h). The UserPromptSubmit notice hook is not running or keeps failing, so gate notices cannot reach the main session.`);
  }
  return out;
}

// What make_brief.mjs and check_handin.mjs print on stderr. `problems` non-empty = canary tripped.
export function canaryReport(stateDir = STATE_DIR, now = Date.now()) {
  const entries = readGateLog(stateDir);
  const problems = canaryProblems(entries, now);
  const pending = pendingItems(entries, { stateDir });
  pending.secret = readSecret(stateDir);
  const lines = [];
  if (problems.length) lines.push('!!! HOOK CANARY: the enforcement hooks may NOT be running !!!', ...problems.map((p) => ' - ' + p));
  if (pending.total) lines.push(formatNotice(pending, pending.secret));
  return { problems, pending, text: lines.join('\n') };
}
