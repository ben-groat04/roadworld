#!/usr/bin/env node
// SubagentStop hook: a subagent whose brief carries "HANDIN_DIR: <abs path>" may not stop until
// `node tools/check_handin.mjs <HANDIN_DIR>` passes (with --no-visual if the brief has the line "HANDIN: no-visual").
// ALSO wired as a PreToolUse hook on the SubagentHandback tool (same logic, deny with exit 2): live test showed that for
// agents running with SubagentHandback (auto mode) the report is delivered BEFORE SubagentStop, so only this one turns them back.
//
// Flow: find the brief in the agent's own transcript (agent_transcript_path; JSONL, first "user" line) -> no HANDIN_DIR
// (and no BRIEF-FILE that has one): allow. Check fails: block with the failure list as the reason, at most 3 times per
// agent_id (counter in .claude/state/handin/<agent_id>.json); after that allow the stop, write
// <HANDIN_DIR>/HANDIN_FAILED.txt with the list and log it.
//
// FAILURE POLICY: FAIL OPEN. Any internal error (bad JSON, missing transcript, exception, check script crash or
// timeout) allows the stop, exits 0 and is logged to .claude/state/gate.log. Reason: a gate that blocks wrongly would
// trap agents (and the session that waits on them) in a loop it cannot leave.
import fs from 'node:fs';
import path from 'node:path';
import { spawnSync } from 'node:child_process';
import { ROOT, STATE_DIR, log, readStdin, parseJson, firstUserText, checkBrief, briefFileOf } from './lib.mjs';

const MAX_BLOCKS = 3;

function transcriptFor(input) {
  if (input.agent_transcript_path && fs.existsSync(input.agent_transcript_path)) return input.agent_transcript_path;
  // PreToolUse events carry only the main transcript_path; the subagent file sits beside it.
  if (input.transcript_path && input.agent_id) {
    const tp = input.transcript_path;
    const guess = path.join(tp.replace(/\.jsonl$/, ''), 'subagents', `agent-${input.agent_id}.jsonl`);
    if (fs.existsSync(guess)) return guess;
  }
  return null;
}

async function main() {
  const raw = await readStdin();
  const input = raw === null ? undefined : parseJson(raw);
  const isPre = input && input.hook_event_name === 'PreToolUse';
  const allow = (entry) => { log({ gate: 'handin', ...entry }); return 0; };
  if (!input || typeof input !== 'object') return allow({ decision: 'allow', reason: 'FAIL-OPEN: stdin missing or not JSON' });
  const id = String(input.agent_id || '').replace(/[^\w.-]/g, '_');
  const base = { event: input.hook_event_name, agent_id: id, agent_type: input.agent_type, cwd: input.cwd };
  if (!id) return allow({ ...base, decision: 'allow', reason: 'no agent_id (internal agent or main session)' });
  const tp = transcriptFor(input);
  if (!tp) return allow({ ...base, decision: 'allow', reason: 'FAIL-OPEN: no readable agent transcript' });

  let text = firstUserText(tp);
  let info = checkBrief(text);
  let handinDir = info.handinDir;
  let noVisual = info.noVisual;
  if (!handinDir) {
    const bf = briefFileOf(text);
    if (bf) {
      try { const ft = fs.readFileSync(bf, 'utf8'); const fi = checkBrief(ft); handinDir = fi.handinDir; noVisual = noVisual || fi.noVisual; }
      catch (e) { return allow({ ...base, decision: 'allow', reason: 'FAIL-OPEN: BRIEF-FILE unreadable', brief_file: bf, error: e.code || e.message }); }
    }
  }
  if (!handinDir) return allow({ ...base, decision: 'allow', reason: 'no HANDIN_DIR in brief' });

  const args = [path.join(ROOT, 'tools', 'check_handin.mjs'), handinDir, '--main', ROOT];
  if (noVisual) args.push('--no-visual');
  const r = spawnSync(process.execPath, args, { cwd: ROOT, encoding: 'utf8', timeout: 30000, env: { ...process.env, CP_FROM_HOOK: '1' } });
  const passed = r.status === 0 && /HAND-IN CHECK PASSED/.test(r.stdout || "");
  const failed = r.status === 1 && /HAND-IN CHECK FAILED/.test(r.stderr || "");
  if (r.error || (!passed && !failed)) {
    return allow({ ...base, decision: 'allow', reason: 'FAIL-OPEN: check_handin crashed or timed out', handin_dir: handinDir, status: r.status, error: String(r.error || r.stderr).slice(0, 300) });
  }
  if (passed) return allow({ ...base, decision: 'allow', reason: 'hand-in check passed', handin_dir: handinDir });

  const failures = (r.stderr || r.stdout || '').trim();
  const cf = path.join(STATE_DIR, 'handin', id + '.json');
  let blocks = 0;
  try { blocks = parseJson(fs.readFileSync(cf, 'utf8')).blocks || 0; } catch { /* first time */ }
  if (blocks >= MAX_BLOCKS) {
    let wrote = false;
    try {
      fs.mkdirSync(handinDir, { recursive: true });
      fs.writeFileSync(path.join(handinDir, 'HANDIN_FAILED.txt'),
        `The hand-in check still failed after ${MAX_BLOCKS} refusals; the agent was allowed to stop (${new Date().toISOString()}).\n\n${failures}\n`);
      wrote = true;
    } catch { /* logged below */ }
    return allow({ ...base, decision: 'allow', reason: `check still failing after ${MAX_BLOCKS} blocks; stop allowed`, handin_failed_written: wrote, handin_dir: handinDir, failures });
  }
  fs.mkdirSync(path.dirname(cf), { recursive: true });
  fs.writeFileSync(cf, JSON.stringify({ blocks: blocks + 1, handin_dir: handinDir, updated: new Date().toISOString() }));
  const reason = `HAND-IN GATE: you may not finish yet (refusal ${blocks + 1} of ${MAX_BLOCKS}). Fix the following, run \`node tools/check_handin.mjs ${handinDir}${noVisual ? ' --no-visual' : ''}\` until it passes, then finish.\n${failures}`;
  log({ gate: 'handin', ...base, decision: 'block', block_number: blocks + 1, handin_dir: handinDir, failures });
  if (isPre) { process.stderr.write(reason + '\n'); return 2; }
  process.stdout.write(JSON.stringify({ decision: 'block', reason }));
  return 0;
}

main().then((c) => process.exit(c), (e) => {
  log({ gate: 'handin', decision: 'allow', reason: 'FAIL-OPEN: exception', error: String(e && e.stack || e).slice(0, 500) });
  process.exit(0);
});
