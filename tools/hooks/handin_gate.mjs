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
// Builder/auditor split (tools/hooks/audit_lib.mjs): when a builder's hand-in PASSES, its agent id is recorded per HANDIN_DIR
// (.claude/state/builders/) and an "audit owed" line is logged; gate_notice.mjs then lists it on every prompt until a valid
// AUDIT.md exists. A brief with "AUDIT-OF: <folder>" (make_brief.mjs --audit) is an AUDITOR: it may not finish until
// <folder>/AUDIT.md has the required first lines, names the finishing agent's own id, and that id differs from the recorded
// builder's (same 3-refusal counter, then released with AUDIT_FAILED.txt; the audit stays owed).
//
// FAILURE POLICY: FAIL OPEN. Any internal error (bad JSON, missing transcript, exception, check script crash or
// timeout) allows the stop, exits 0 and is logged to .claude/state/gate.log. Reason: a gate that blocks wrongly would
// trap agents (and the session that waits on them) in a loop it cannot leave.
import fs from 'node:fs';
import path from 'node:path';
import { spawnSync } from 'node:child_process';
import { ROOT, STATE_DIR, log, readStdin, parseJson, firstUserText, checkBrief, briefFileOf } from './lib.mjs';
import { auditOfIn, readBuilder, recordBuilder, checkAuditFile, AUDIT_FILE } from './audit_lib.mjs';

const MAX_BLOCKS = 3;

// When this agent's run began: timestamp of the first transcript line, else the transcript file's creation time.
// Files the gates judge must have been written after it (minus slack), so a stale passing folder from an earlier round
// cannot satisfy a new agent (audit fault 1).
export const SLACK_MS = 2000;
function runStartMs(tp) {
  try {
    const first = fs.readFileSync(tp, 'utf8').split('\n').find((l) => l.trim());
    const ts = first && Date.parse((parseJson(first) || {}).timestamp);
    if (Number.isFinite(ts)) return ts;
  } catch { /* fall back */ }
  try { return fs.statSync(tp).birthtimeMs; } catch { return 0; }
}
const resetCounter = (id) => { try { fs.rmSync(path.join(STATE_DIR, 'handin', id + '.json'), { force: true }); } catch { /* best effort */ } };

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

  const runStart = runStartMs(tp);
  let text = firstUserText(tp);
  let info = checkBrief(text);
  let aof = auditOfIn(text);
  if (!aof) {
    const bf0 = briefFileOf(text);
    if (bf0) { try { aof = auditOfIn(fs.readFileSync(bf0, 'utf8')); } catch { /* the builder branch below reports an unreadable BRIEF-FILE */ } }
  }
  if (aof) {
    const rec = readBuilder(aof, STATE_DIR);
    const probs = checkAuditFile(path.join(aof, AUDIT_FILE), { builderId: rec && rec.agent_id, actualId: id, notBefore: runStart - SLACK_MS });
    if (!probs.length) resetCounter(id);
    if (!probs.length) return allow({ ...base, decision: 'allow', reason: 'audit written', audit_of: aof, builder_id: rec ? rec.agent_id : null });
    return blockOrRelease({ isPre, base, id, dir: aof, failures: probs.map((p) => ' - ' + p).join('\n'), kind: 'audit',
      intro: `AUDIT GATE: you are the auditor of ${aof} and may not finish yet`,
      fix: `write ${path.join(aof, AUDIT_FILE)} (template docs/templates/AUDIT.md; your agent id is ${id})`, failedFile: 'AUDIT_FAILED.txt' });
  }
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
  if (runStart) args.push('--since', String(Math.round(runStart - SLACK_MS)));
  const r = spawnSync(process.execPath, args, { cwd: ROOT, encoding: 'utf8', timeout: 30000, env: { ...process.env, CP_FROM_HOOK: '1' } });
  const passed = r.status === 0 && /HAND-IN CHECK PASSED/.test(r.stdout || "");
  const failed = r.status === 1 && /HAND-IN CHECK FAILED/.test(r.stderr || "");
  if (r.error || (!passed && !failed)) {
    return allow({ ...base, decision: 'allow', reason: 'FAIL-OPEN: check_handin crashed or timed out', handin_dir: handinDir, status: r.status, error: String(r.error || r.stderr).slice(0, 300) });
  }
  if (passed) {
    resetCounter(id);
    try {
      const { changed, rec } = recordBuilder(handinDir, id, input.agent_type, STATE_DIR);
      if (changed) log({ gate: 'audit', decision: 'owed', reason: 'audit owed: hand-in passed, no AUDIT.md by a different agent yet', handin_dir: handinDir, builder_id: id, builder_type: rec.agent_type });
    } catch (e) { log({ gate: 'audit', decision: 'skip', reason: 'FAIL-OPEN: could not record the builder', error: String(e && e.message || e).slice(0, 300) }); }
    return allow({ ...base, decision: 'allow', reason: 'hand-in check passed', handin_dir: handinDir });
  }

  const failures = (r.stderr || r.stdout || '').trim();
  return blockOrRelease({ isPre, base, id, dir: handinDir, failures, kind: 'handin',
    intro: 'HAND-IN GATE: you may not finish yet',
    fix: `run \`node tools/check_handin.mjs ${handinDir}${noVisual ? ' --no-visual' : ''}\` until it passes`, failedFile: 'HANDIN_FAILED.txt' });
}

// Shared by the builder (hand-in check) and auditor (AUDIT.md) branches: refuse up to MAX_BLOCKS times per agent_id, then release.
function blockOrRelease({ isPre, base, id, dir, failures, kind, intro, fix, failedFile }) {
  const cf = path.join(STATE_DIR, 'handin', id + '.json');
  let blocks = 0;
  try { blocks = parseJson(fs.readFileSync(cf, 'utf8')).blocks || 0; } catch { /* first time */ }
  const audit = kind === 'audit';
  if (blocks >= MAX_BLOCKS) {
    let wrote = false;
    try {
      fs.mkdirSync(dir, { recursive: true });
      fs.writeFileSync(path.join(dir, failedFile),
        `The ${audit ? 'AUDIT.md' : 'hand-in'} check still failed after ${MAX_BLOCKS} refusals; the agent was allowed to stop (${new Date().toISOString()}).\n\n${failures}\n`);
      wrote = true;
    } catch { /* logged below */ }
    log({ gate: kind, ...base, decision: 'allow',
      reason: audit ? `audit still failing after ${MAX_BLOCKS} blocks; stop allowed (the audit stays owed)` : `check still failing after ${MAX_BLOCKS} blocks; stop allowed`,
      [audit ? 'audit_failed_written' : 'handin_failed_written']: wrote, handin_dir: dir, failures });
    return 0;
  }
  fs.mkdirSync(path.dirname(cf), { recursive: true });
  fs.writeFileSync(cf, JSON.stringify({ blocks: blocks + 1, handin_dir: dir, updated: new Date().toISOString() }));
  const reason = `${intro} (refusal ${blocks + 1} of ${MAX_BLOCKS}). Fix the following, ${fix}, then finish.\n${failures}`;
  log({ gate: kind, ...base, decision: 'block', block_number: blocks + 1, handin_dir: dir, failures });
  if (isPre) { process.stderr.write(reason + '\n'); return 2; }
  process.stdout.write(JSON.stringify({ decision: 'block', reason }));
  return 0;
}

main().then((c) => process.exit(c), (e) => {
  log({ gate: 'handin', decision: 'allow', reason: 'FAIL-OPEN: exception', error: String(e && e.stack || e).slice(0, 500) });
  process.exit(0);
});
