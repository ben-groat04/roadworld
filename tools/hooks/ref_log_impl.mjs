// PostToolUse hook (matcher Bash|PowerShell|WebFetch|Write|Edit): after a tool call, registers every picture that newly appeared (or changed) under the main checkout's
// docs/reference in .claude/state/ref_log.jsonl (see ref_lib.mjs). This is the "hook-written download log" the hand-in gate compares reference photos against.
// Prints nothing, never blocks. FAIL OPEN (logged) on any error.
import { ROOT, STATE_DIR, log, readStdin, parseJson } from './lib.mjs';
import { scanReferences } from './ref_lib.mjs';

export async function main() {
  const raw = await readStdin();
  const input = raw === null ? undefined : parseJson(raw);
  if (!input || typeof input !== 'object') { log({ gate: 'ref_log', decision: 'skip', reason: 'FAIL-OPEN: stdin missing or not JSON' }); return 0; }
  const r = scanReferences(ROOT, STATE_DIR, input.agent_id ? 'agent:' + String(input.agent_id).replace(/[^\w.-]/g, '_') : 'main');
  if (r.added) log({ gate: 'ref_log', decision: 'registered', reason: 'new reference pictures registered', added: r.added, total: r.total, agent_id: input.agent_id || '', tool: input.tool_name });
  return 0;
}
