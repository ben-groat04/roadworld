// Gate notices (gate_notice.mjs, gate_ack.mjs) and the hooks-are-dead canary (make_brief.mjs, check_handin.mjs).
// Everything here works on parsed gate.log lines and takes the state directory as a parameter, so tests and the two
// tools can point it at any checkout.
import fs from 'node:fs';
import path from 'node:path';
import { ROOT, STATE_DIR, parseJson } from './lib.mjs';
import { auditOwed } from './audit_lib.mjs';

export const MIN_ACK_CHARS = 20;
export const CANARY_GRACE_MS = 120 * 1000;          // a brief made less than 2 minutes ago may simply not be launched yet
export const NOTICE_STALE_MS = 3 * 3600 * 1000;     // the notice hook runs at least once per prompt / background report
export const ACK_CMD = 'node tools/hooks/gate_ack.mjs "<what was done about it>"';

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

// Unacknowledged items = everything after the newest { gate: 'ack' } line's through_line.
// auditOwed (opts.stateDir/opts.root, default the main checkout): hand-ins that passed with no valid AUDIT.md by a different
// agent. Computed from builder records + the files themselves, NOT from the log, so no ack can clear it.
export function pendingItems(entries, opts = {}) {
  let through = 0;
  for (const e of entries) if (e.gate === 'ack' && Number.isFinite(e.through_line)) through = Math.max(through, e.through_line);
  const live = entries.filter((e) => e.n > through && e.gate !== 'ack');
  const failOpen = live.filter((e) => /FAIL-OPEN/.test(String(e.reason || '')));
  const seen = new Set();
  const handinFailed = live.filter((e) => e.gate === 'handin' && /^check still failing after/.test(String(e.reason || '')))
    .filter((e) => { const k = e.handin_dir || e.agent_id; if (seen.has(k)) return false; seen.add(k); return true; });
  const noContract = live.filter((e) => e.gate === 'brief' && e.reason === 'NO-CONTRACT');
  let owed = [];
  try { owed = auditOwed(opts.stateDir || STATE_DIR, opts.root || ROOT); } catch { /* fail open: logged by the caller if it matters */ }
  return { failOpen, handinFailed, noContract, auditOwed: owed, total: failOpen.length + handinFailed.length + noContract.length + owed.length };
}

// Factual text (no imperatives: the hooks docs warn that command-style context can trip prompt-injection defences).
export function formatNotice(p) {
  if (!p.total) return '';
  const L = [`GATE NOTICE from tools/hooks/gate_notice.mjs (source: .claude/state/gate.log of the main checkout). ${p.total} open item(s).`];
  if (p.auditOwed.length) {
    L.push(`- AUDIT OWED x${p.auditOwed.length}: a builder's hand-in passed the check and no independent auditor has written a valid AUDIT.md. Builder and auditor are never the same agent; there is no cap on auditors. Cleared only by that file (an ack does nothing):`);
    for (const e of p.auditOwed.slice(0, 10)) L.push(`    ${e.handin_dir} (builder ${e.builder_type || '?'}/${e.builder_id}, hand-in passed ${e.passed_at}) - ${clip(e.problems[0], 110)}`);
    L.push('    start one with: node tools/make_brief.mjs <task-file> --audit <that folder>');
  }
  if (p.failOpen.length) {
    L.push(`- FAIL-OPEN x${p.failOpen.length}: a gate did NOT run or did not decide (gates fail open so the session cannot deadlock). Newest first:`);
    for (const e of p.failOpen.slice(-8).reverse()) L.push(`    ${e.ts} gate=${e.gate} ${clip(e.reason, 90)}${e.error ? ' | ' + clip(e.error, 120) : ''}`);
  }
  if (p.handinFailed.length) {
    L.push(`- HAND-IN RELEASED after 3 refusals x${p.handinFailed.length} (the work did not pass tools/check_handin.mjs; HANDIN_FAILED.txt is in that folder):`);
    for (const e of p.handinFailed.slice(-8).reverse()) L.push(`    ${e.ts} HANDIN_DIR=${e.handin_dir} agent=${e.agent_type || '?'}/${e.agent_id}`);
  }
  if (p.noContract.length) {
    L.push(`- NO-CONTRACT launches x${p.noContract.length} (no contract, no hand-in gate; reasons given):`);
    for (const e of p.noContract.slice(-8).reverse()) L.push(`    ${e.ts} "${clip(e.no_contract_reason, 120)}" (${clip(e.description, 50)})`);
  }
  if (p.total - p.auditOwed.length > 0) L.push(`${p.auditOwed.length ? 'The log items (all except AUDIT OWED) repeat' : 'This notice is repeated'} on every prompt until acknowledged. The only thing that clears them: ${ACK_CMD} (text of at least ${MIN_ACK_CHARS} characters; recorded in the log with a timestamp).`);
  const s = L.join('\n');
  return s.length > 6000 ? s.slice(0, 5990) + '\n[cut]' : s;
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
  const lines = [];
  if (problems.length) lines.push('!!! HOOK CANARY: the enforcement hooks may NOT be running !!!', ...problems.map((p) => ' - ' + p));
  if (pending.total) lines.push(formatNotice(pending));
  return { problems, pending, text: lines.join('\n') };
}
