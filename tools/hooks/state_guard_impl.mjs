// PreToolUse hook (matcher Read|Write|Edit|NotebookEdit|Grep|Glob|Bash|PowerShell): a SUBAGENT (the event carries an agent_id) may not read the secret or write
// anything under the main checkout's .claude/state, and may not run the scripts that write or acknowledge gate records. The main session (no agent_id) is not limited.
//
// Why (gates3 audit fault 2): records of who built and who audited a round are only worth anything if the agents being judged cannot write them. They are written
// by the hooks (handin_gate on SubagentStop / SubagentHandback, which receive the harness's own agent_id), never by an agent; this hook closes the agent's own
// route to them. A record is also signed with .claude/state/ack_secret as TAMPER EVIDENCE only (a hand-edited record does not verify); the trust comes from
// who writes it, and from this guard keeping agents away from the secret and the state folder.
//
// Allowed for a subagent: reading .claude/state/gate.log (diagnosis), plain read-only commands on it (cat/type/head/tail/grep/rg/Get-Content/Select-String).
// WHAT IT CANNOT SEE: a script file that touches the state indirectly (the command only says `node x.mjs`), a path built from pieces, a tool other than the ones
// matched. It reads tool input text and nothing else. FAIL OPEN (logged) on any internal error.
import { log, readStdin, parseJson } from './lib.mjs';

const norm = (p) => String(p || '').replace(/\\/g, '/').replace(/\/+/g, '/').toLowerCase();
const STATE_RE = /(^|[\/"'\s=:(])\.claude\/state(\/|["'\s)]|$)/;
const FORBIDDEN_WORDS = /\b(?:ack_secret|ref_log\.jsonl|recordAudit|recordBuilder|signRecord|verifyRecord|gate_ack(?:\.mjs)?|ref_seed(?:\.mjs)?)\b/;
const READ_VERBS = /^(?:cat|type|head|tail|grep|rg|get-content|select-string|less|more|gc)$/i;

export function judge(input) {
  const tool = input.tool_name || '';
  const ti = input.tool_input || {};
  if (!input.agent_id) return [];   // the main session
  const reasons = [];
  if (/^(Read|Write|Edit|NotebookEdit|Grep|Glob)$/.test(tool)) {
    const p = norm(ti.file_path || ti.notebook_path || ti.path || '');
    if (p && STATE_RE.test(p + '/')) {
      const readOnly = /^(Read)$/.test(tool) && /\/gate\.log$/.test(p);
      if (!readOnly) reasons.push(`${tool} on ${ti.file_path || ti.notebook_path || ti.path}: the gate records under .claude/state are written by the hooks only (reading gate.log is allowed)`);
    }
    if (tool === 'Grep' && ti.pattern && /ack_secret/.test(String(ti.pattern))) reasons.push('searching for the ack secret');
  } else if (/^(Bash|PowerShell)$/.test(tool)) {
    const cmd = String(ti.command || '');
    const n = norm(cmd);
    const mentions = STATE_RE.test(n) || /(^|[\/"'\s])state\/(builders|audits|handin)\b/.test(n);
    const words = cmd.match(FORBIDDEN_WORDS);
    if (mentions || words) {
      const single = !/[;&|\n>]/.test(cmd.trim());
      const first = (cmd.trim().split(/\s+/)[0] || '').replace(/^.*[\\/]/, '');
      const readOnly = single && READ_VERBS.test(first) && /gate\.log/.test(n) && !/ack_secret/.test(n);
      if (!readOnly) reasons.push(`a command that ${words ? 'names ' + words[0] : 'touches .claude/state'}: gate records are written by the hooks only; an agent does not read the secret, write records or acknowledge notices (plain reads of gate.log are allowed)`);
    }
  }
  return reasons;
}

export async function main() {
  const raw = await readStdin();
  const input = raw === null ? undefined : parseJson(raw);
  if (!input || typeof input !== 'object') { log({ gate: 'state_guard', decision: 'allow', reason: 'FAIL-OPEN: stdin missing or not JSON' }); return 0; }
  const reasons = judge(input);
  if (!reasons.length) return 0;
  log({ gate: 'state_guard', decision: 'deny', reason: 'agent touched the gate state', agent_id: input.agent_id, tool: input.tool_name, reasons });
  process.stderr.write('REFUSED by the state guard (tools/hooks/state_guard.mjs):\n - ' + reasons.join('\n - ') + '\nIf this blocks work you were told to do, say so in your report (BLOCKED line); do not work around it.\n');
  return 2;
}
