#!/usr/bin/env node
// Acknowledge gate notices ITEM BY ITEM: node tools/hooks/gate_ack.mjs "<n>:<code> [<n>:<code> ...]" "<what was done about it>"
// <n>:<code> is printed next to each item in the gate notice (#n[code]); the code is sha1(secret:n) where the secret lives in
// .claude/state/ack_secret and is shown nowhere but as these codes in the main session's notice, so a subagent that never saw the
// notice cannot ack (audit fault 2). Each listed item must currently be pending and its code must match. The text must say what was
// done (20+ characters, 4+ words, 10+ different characters). Records { gate: 'ack', items: [n...], text, cleared } in
// .claude/state/gate.log of the main checkout. Nothing else clears a notice item; AUDIT OWED items are not log items and only a
// valid AUDIT.md clears them.
import { log } from './lib.mjs';
import { readGateLog, pendingItems, ackItems, readSecret, ackCode, ackTextProblem, isMainCheckout, ACK_CMD } from './notice_lib.mjs';

const refuse = (m) => { console.error('REFUSED: ' + m + `\nUsage: ${ACK_CMD}`); process.exit(1); };
if (!isMainCheckout()) refuse('this copy of gate_ack.mjs is in a git worktree, whose .claude/state/gate.log is not the one the hooks write. Run it from the main checkout (the roadworld repository root).');
const tokens = String(process.argv[2] || '').split(/[\s,]+/).filter(Boolean);
const text = process.argv.slice(3).join(' ').trim();
if (!tokens.length || !tokens.every((x) => /^\d+:[0-9a-f]{6}$/.test(x))) refuse('the first argument must be the item(s) to acknowledge as <n>:<code> (copy them from the gate notice: #n[code]).');
const textProblem = ackTextProblem(text);
if (textProblem) refuse(`the acknowledgement text says what was done about the item(s); it needs ${textProblem}.`);
const secret = readSecret();
if (!secret) refuse('there is no ack secret yet (.claude/state/ack_secret): the notice hook has not shown any item, so there is nothing to acknowledge.');
const entries = readGateLog();
const p = pendingItems(entries);
const pending = new Map(ackItems(p).map((e) => [e.n, e]));
const items = [];
for (const tok of tokens) {
  const [ns, code] = tok.split(':'); const n = +ns;
  if (!pending.has(n)) refuse(`item #${n} is not a pending notice item (already acknowledged, or not one of the items in the current notice).`);
  if (code !== ackCode(secret, n)) refuse(`the code for #${n} does not match the one in the notice.`);
  items.push(n);
}
const sel = items.map((n) => pending.get(n));
log({ gate: 'ack', items, text, cleared: { fail_open: sel.filter((e) => /FAIL-OPEN/.test(String(e.reason || ''))).length, handin_failed: sel.filter((e) => e.gate === 'handin').length, no_contract: sel.filter((e) => e.reason === 'NO-CONTRACT').length } });
const left = p.total - p.auditOwed.length - items.length;
console.log(`Acknowledged ${items.length} item(s): ${items.map((n) => '#' + n).join(', ')}. Recorded in .claude/state/gate.log. ${left} log item(s) still open.` +
  (p.auditOwed.length ? `\nStill open (an ack cannot clear it, only a valid AUDIT.md can): AUDIT OWED x${p.auditOwed.length}: ${p.auditOwed.map((e) => e.handin_dir).join(', ')}` : ''));
