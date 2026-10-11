#!/usr/bin/env node
// PreToolUse hook, matcher "Agent": refuses to launch a subagent whose prompt lacks the agent contract.
//
// FAILURE POLICY: FAIL OPEN. Any internal error (bad JSON on stdin, unreadable file, exception, stdin timeout)
// allows the launch, exits 0 and is logged to .claude/state/gate.log. Reason: the main session delegates ALL code
// changes to agents, so a broken gate that blocked every launch would deadlock the whole session.
//
// Allowed: exempt types (Explore, Plan, claude-code-guide, statusline-setup); a prompt that passes checkBrief();
// a prompt with a "NO-CONTRACT: <reason, >= 20 chars>" line (research, merges, read-only checks);
// a prompt with "BRIEF-FILE: <absolute path>" whose file passes checkBrief().
// Denied: exit 2 with the list of what is missing on stderr (the main session sees it as the tool error).
// Every decision is one JSON line in .claude/state/gate.log of the main checkout.
import fs from 'node:fs';
import { log, readStdin, parseJson, checkBrief, noContractReason, briefFileOf, EXEMPT_TYPES } from './lib.mjs';

async function main() {
  const raw = await readStdin();
  const input = raw === null ? undefined : parseJson(raw);
  if (!input || typeof input !== 'object') {
    log({ gate: 'brief', decision: 'allow', reason: 'FAIL-OPEN: stdin missing or not JSON', raw: String(raw).slice(0, 200) });
    return 0;
  }
  const ti = input.tool_input || {};
  const type = ti.subagent_type || 'general-purpose';
  const base = { gate: 'brief', agent_type: type, description: ti.description, session_id: input.session_id, cwd: input.cwd };
  if (EXEMPT_TYPES.includes(type)) {
    log({ ...base, decision: 'allow', reason: 'exempt type' });
    return 0;
  }
  const prompt = typeof ti.prompt === 'string' ? ti.prompt : '';
  const nc = noContractReason(prompt);
  if (nc) {
    log({ ...base, decision: 'allow', reason: 'NO-CONTRACT', no_contract_reason: nc });
    return 0;
  }
  const own = checkBrief(prompt);
  if (!own.missing.length) {
    log({ ...base, decision: 'allow', reason: 'contract present', handin_dir: own.handinDir });
    return 0;
  }
  let missing = own.missing;
  const bf = briefFileOf(prompt);
  if (bf) {
    let fileText = null;
    try { fileText = fs.readFileSync(bf, 'utf8'); } catch (e) { missing = [`BRIEF-FILE ${bf} cannot be read (${e.code || e.message})`, ...own.missing]; }
    if (fileText !== null) {
      const f = checkBrief(fileText);
      if (!f.missing.length) {
        log({ ...base, decision: 'allow', reason: 'BRIEF-FILE passes', brief_file: bf, handin_dir: f.handinDir });
        return 0;
      }
      missing = f.missing.map((m) => `BRIEF-FILE ${bf} lacks ${m}`);
    }
  }
  log({ ...base, decision: 'deny', missing });
  process.stderr.write(
    'LAUNCH REFUSED by the brief gate (tools/hooks/brief_gate.mjs). The subagent prompt is missing:\n - ' + missing.join('\n - ') +
    '\nFix: build the brief with `node tools/make_brief.mjs <task-file> --round <name>` and send its output as the prompt. ' +
    'For work that is not a build/review hand-in (research, merge, read-only check) add a line `NO-CONTRACT: <reason, at least 20 characters>`. ' +
    'To point at a file instead, add `BRIEF-FILE: <absolute path>` (the file must pass the same test).\n');
  return 2;
}

main().then((c) => process.exit(c), (e) => {
  log({ gate: 'brief', decision: 'allow', reason: 'FAIL-OPEN: exception', error: String(e && e.stack || e).slice(0, 500) });
  process.exit(0);
});
