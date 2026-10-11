#!/usr/bin/env node
// UserPromptSubmit hook: puts a short notice into the main session's context whenever .claude/state/gate.log holds
// something unacknowledged: a FAIL-OPEN line (a gate did not run), a hand-in released after 3 refusals (HANDIN_DIR),
// NO-CONTRACT launches (count and reasons). Repeats on every prompt until `node tools/hooks/gate_ack.mjs "<text>"`.
//
// WHY UserPromptSubmit: per https://code.claude.com/docs/en/hooks it runs before Claude processes each submitted prompt
// AND when a background subagent reports back to the session that started it; its additionalContext is injected as a
// system reminder next to the prompt. The main session runs agents in the background, so every agent finishing (the
// moment a HANDIN_FAILED happens) is followed by one of these events. Stop was rejected: additionalContext there
// "continues the conversation", forcing an extra turn each time. PostToolUse on Agent only fires for the launch.
//
// FAILURE POLICY: FAIL OPEN. Any error: print nothing, exit 0, log FAIL-OPEN with gate "notice". That line is itself an
// unacknowledged item for the next successful run, and a notice hook that is not running at all is caught by the canary
// in make_brief.mjs / check_handin.mjs (notice_lib.mjs canaryProblems, RULE 2).
import { log, readStdin, parseJson } from './lib.mjs';
import { readGateLog, pendingItems, formatNotice } from './notice_lib.mjs';

async function main() {
  const raw = await readStdin();
  const input = raw === null ? undefined : parseJson(raw);
  if (!input || typeof input !== 'object') {
    log({ gate: 'notice', decision: 'skip', reason: 'FAIL-OPEN: stdin missing or not JSON' });
    return;
  }
  const p = pendingItems(readGateLog());
  log({ gate: 'notice', decision: p.total ? 'notice' : 'none', reason: 'ran', session_id: input.session_id, pending: p.total });
  if (!p.total) return;
  process.stdout.write(JSON.stringify({ hookSpecificOutput: { hookEventName: 'UserPromptSubmit', additionalContext: formatNotice(p) } }));
}

main().then(() => process.exit(0), (e) => {
  log({ gate: 'notice', decision: 'skip', reason: 'FAIL-OPEN: exception', error: String(e && e.stack || e).slice(0, 500) });
  process.exit(0);
});
