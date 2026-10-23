// PostToolUse and PostToolUseFailure hook (matcher Bash|PowerShell): the other half of the suite record (gates12, main-session decision 2).
// The time guard (PreToolUse) writes a "start" line to state/runs.jsonl for every suite or test run it lets through: command, worktree, start time, the files the command redirects its output to.
// This hook finds that line by tool_use_id and appends the matching "end" line: end time, exit code, and the sha256 of each output file as it is NOW. `check_handin --phase build`
// accepts suite_build.log only when a record holds that exact hash (a log typed by hand, or edited afterwards, has no record), the end time is after the last code commit and the exit code
// is 0 or the failures are only the brief's allowed ones. State lives in .claude/state (agents cannot write it: state_guard); CP_GATE_STATE_DIR moves it for tests.
//
// Exit code: PostToolUse is delivered for a call that succeeded, so the code is 0; PostToolUseFailure carries `error` text that starts with "Exit code N" (the Bash tool's own wording) or a
// tool_response with exit_code / exitCode / returncode; anything else is recorded as exit null (unknown), which the check treats as a failure it cannot excuse.
// NOT VERIFIED LIVE: the payload fields of a real Claude Code Bash failure were not captured in this round (no live hook run is possible from a test); the parsing is tolerant and the test
// suite feeds it each shape. It never blocks anything and fails open (a gate.log line).
import fs from 'node:fs';
import crypto from 'node:crypto';
import { STATE_DIR, log, readStdin, parseJson } from './lib.mjs';
import { runsFile } from './time_guard_impl.mjs';

export function exitCodeOf(input) {
  const ev = String(input.hook_event_name || '');
  const tr = input.tool_response;
  if (tr && typeof tr === 'object') for (const k of ['exit_code', 'exitCode', 'returncode', 'returnCode', 'code']) if (Number.isInteger(tr[k])) return tr[k];
  const err = typeof input.error === 'string' ? input.error : (tr && typeof tr === 'string' ? tr : (tr && typeof tr.error === 'string' ? tr.error : ''));
  const m = String(err).match(/(?:^|\n)\s*Exit code[: ]+(-?\d+)/i);
  if (m) return Number(m[1]);
  if (/failure/i.test(ev) || input.is_error === true || (tr && tr.is_error === true)) return null;
  if (input.interrupted === true || (tr && tr.interrupted === true)) return null;
  return 0;
}
export const sha256File = (p) => { try { return crypto.createHash('sha256').update(fs.readFileSync(p)).digest('hex'); } catch { return null; } };

export async function main() {
  const raw = await readStdin();
  const input = raw === null ? undefined : parseJson(raw);
  if (!input || typeof input !== 'object') { log({ gate: 'time_guard_post', decision: 'skip', reason: 'FAIL-OPEN: stdin missing or not JSON' }); return 0; }
  if (!/^(Bash|PowerShell)$/.test(input.tool_name || '') || !input.tool_use_id) return 0;
  let lines = []; try { lines = fs.readFileSync(runsFile(), 'utf8').split('\n').filter(Boolean).slice(-400); } catch { return 0; }
  let start = null;
  for (const l of lines) { try { const o = JSON.parse(l); if (o.ev === 'start' && o.id === input.tool_use_id) start = o; } catch { /* skip */ } }
  if (!start) return 0;                                        // not a suite run the guard recorded
  const files = (start.files || []).map((p) => ({ path: p, sha256: sha256File(p) })).filter((f) => f.sha256);
  try { fs.mkdirSync(STATE_DIR, { recursive: true }); fs.appendFileSync(runsFile(), JSON.stringify({ ev: 'end', id: start.id, ts: Date.now(), root: start.root, name: start.name, exit: exitCodeOf(input), files }) + '\n'); }
  catch (e) { log({ gate: 'time_guard_post', decision: 'skip', reason: 'FAIL-OPEN: end record could not be written (' + (e && e.message) + ')' }); }
  return 0;
}
