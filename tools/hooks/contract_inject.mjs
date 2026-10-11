#!/usr/bin/env node
// SubagentStart hook: injects docs/AGENT_CONTRACT.md (from the main checkout) into every non-exempt subagent as
// additionalContext, so even a NO-CONTRACT launch or a stale worktree copy of the rules still sees the contract.
// SubagentStart cannot block, so there is nothing to deny here.
//
// FAILURE POLICY: FAIL OPEN. Any error: print nothing, exit 0, log to .claude/state/gate.log (a broken injector must
// never stop agents from starting; the main session delegates all code changes to agents).
import fs from 'node:fs';
import path from 'node:path';
import { ROOT, log, readStdin, parseJson, EXEMPT_TYPES } from './lib.mjs';

async function main() {
  const raw = await readStdin();
  const input = raw === null ? undefined : parseJson(raw);
  if (!input || typeof input !== 'object') {
    log({ gate: 'inject', decision: 'skip', reason: 'FAIL-OPEN: stdin missing or not JSON' });
    return;
  }
  const type = input.agent_type || '';
  const base = { gate: 'inject', agent_type: type, agent_id: input.agent_id, cwd: input.cwd };
  if (EXEMPT_TYPES.includes(type)) { log({ ...base, decision: 'skip', reason: 'exempt type' }); return; }
  const contract = fs.readFileSync(path.join(ROOT, 'docs', 'AGENT_CONTRACT.md'), 'utf8').trim();
  const aid = String(input.agent_id || '').replace(/[^\w.-]/g, '_');
  const idLine = aid ? `YOUR AGENT ID: ${aid}  (an auditor writes it as "Auditor agent: ${aid}" in AUDIT.md; the finish gate compares it with the recorded builder's id)\n\n` : '';
  const ctx = idLine + 'PROJECT RULES (injected by the SubagentStart hook; these apply to you even if your brief does not repeat them):\n\n' + contract;
  process.stdout.write(JSON.stringify({ hookSpecificOutput: { hookEventName: 'SubagentStart', additionalContext: ctx } }));
  log({ ...base, decision: 'inject', chars: ctx.length });
}

main().then(() => process.exit(0), (e) => {
  log({ gate: 'inject', decision: 'skip', reason: 'FAIL-OPEN: exception', error: String(e && e.stack || e).slice(0, 500) });
  process.exit(0);
});
