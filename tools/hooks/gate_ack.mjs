#!/usr/bin/env node
// Acknowledge the gate notices: node tools/hooks/gate_ack.mjs "<what was done about it>"
// Records { gate: 'ack', text, through_line, cleared } in .claude/state/gate.log of the main checkout. Everything
// logged before this line stops being reported; nothing else clears a notice. Text under 20 characters is refused (exit 1).
import fs from 'node:fs';
import path from 'node:path';
import { log, STATE_DIR } from './lib.mjs';
import { readGateLog, pendingItems, MIN_ACK_CHARS } from './notice_lib.mjs';

const text = process.argv.slice(2).join(' ').trim();
if (text.length < MIN_ACK_CHARS) {
  console.error(`REFUSED: the acknowledgement needs at least ${MIN_ACK_CHARS} characters saying what was done about the notice (got ${text.length}).\nUsage: node tools/hooks/gate_ack.mjs "<what was done about it>"`);
  process.exit(1);
}
const p = pendingItems(readGateLog());
let lines = 0;
try { lines = fs.readFileSync(path.join(STATE_DIR, 'gate.log'), 'utf8').split('\n').filter((l) => l.trim()).length; } catch { /* no log yet */ }
// through_line counts non-empty lines; readGateLog numbers by physical line, which is the same while the log has no blank lines.
log({ gate: 'ack', text, through_line: lines, cleared: { fail_open: p.failOpen.length, handin_failed: p.handinFailed.length, no_contract: p.noContract.length } });
console.log(`Acknowledged ${p.total} item(s) (fail-open ${p.failOpen.length}, hand-in released ${p.handinFailed.length}, no-contract ${p.noContract.length}). Recorded in .claude/state/gate.log.`);
