// Test helper (gates12): make a suite_build.log the way a real run makes it, through the real hooks. The time guard (PreToolUse) records the start of
// `timeout 900 <suite> > <log> 2>&1`, the test writes the log itself (standing in for the suite), the post hook (PostToolUse or PostToolUseFailure) records the end, the exit code and the hash.
// Never a hand-made record line: what a build phase check accepts is exactly what the hooks write.
import fs from 'node:fs';
import path from 'node:path';
import { spawnSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';

const HOOKS = path.dirname(fileURLToPath(import.meta.url));
export function logRun(wt, logFile, content, { exit = 0, stateDir = process.env.CP_GATE_STATE_DIR, id = `toolu_${Math.random().toString(36).slice(2)}`, cmd = null, tool = 'Bash', skipPost = false, hookDir = HOOKS } = {}) {
  const env = { ...process.env, CP_GATE_STATE_DIR: stateDir, CP_TIME_GUARD_PROCS: '' }; delete env.CP_TIME_GUARD_PROC_CMD; delete env.CP_TIME_GUARD_PATHS; delete env.CP_TIME_GUARD_PATH_CMD;
  const command = cmd || `timeout 900 ./build/tests/sim_core_tests.exe > "${logFile.replace(/\\/g, '/')}" 2>&1`;
  const pre = spawnSync(process.execPath, [path.join(hookDir, 'time_guard.mjs')], { input: JSON.stringify({ hook_event_name: 'PreToolUse', tool_name: tool, tool_use_id: id, session_id: 's1', cwd: wt, tool_input: { command } }), encoding: 'utf8', env, timeout: 60000 });
  fs.mkdirSync(path.dirname(logFile), { recursive: true });
  if (content !== null) fs.writeFileSync(logFile, content);
  let post = null;
  if (!skipPost) post = spawnSync(process.execPath, [path.join(hookDir, 'time_guard_post.mjs')], {
    input: JSON.stringify(exit === 0 ? { hook_event_name: 'PostToolUse', tool_name: tool, tool_use_id: id, session_id: 's1', cwd: wt, tool_input: { command }, tool_response: { stdout: '', stderr: '', interrupted: false } }
      : { hook_event_name: 'PostToolUseFailure', tool_name: tool, tool_use_id: id, session_id: 's1', cwd: wt, tool_input: { command }, error: `Exit code ${exit}\n` }), encoding: 'utf8', env, timeout: 60000 });
  return { pre, post, id };
}
