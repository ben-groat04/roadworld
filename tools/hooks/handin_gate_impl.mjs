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
import { auditOfIn, readBuilder, recordBuilder, checkAuditFile, AUDIT_FILE, findBuilderInLog, recordAudit, parseAudit } from './audit_lib.mjs';

const MAX_BLOCKS = 3;
// Measured gates build and run the scene check: allow minutes (settings.json hook timeout must be larger; a timeout fails open and is logged).
const GATE_TIMEOUT_MS = 270000;
export function priorAuditIn(text) { const m = String(text || '').match(/^PRIOR-AUDIT:[ 	]*(.+?)[ 	]*$/m); return m ? m[1].replace(/^`|`$/g, '') : null; }
export function gatesIn(text) {
  const t = String(text || '').split('\r\n').join('\n');
  const v = t.match(/^GATES-VERSION:[ \t]*(\d+)[ \t]*$/m);
  if (!v) return null;
  return { version: Number(v[1]), gates: [...t.matchAll(/^GATE:[ \t]*(.+?)[ \t]*$/gm)].map((m) => m[1]) };
}

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

// Did the SubagentStart hook (contract_inject) log a launch for this agent id?
function launchedAgent(id) {
  try {
    const txt = fs.readFileSync(path.join(STATE_DIR, 'gate.log'), 'utf8');
    return txt.split(String.fromCharCode(10)).some((l) => l.includes('"gate":"inject"') && l.includes('"agent_id":"' + id + '"'));
  } catch { return false; }
}

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

export async function main() {
  const raw = await readStdin();
  const input = raw === null ? undefined : parseJson(raw);
  const isPre = input && input.hook_event_name === 'PreToolUse';
  const allow = (entry) => { log({ gate: 'handin', ...entry }); return 0; };
  if (!input || typeof input !== 'object') return allow({ decision: 'allow', reason: 'FAIL-OPEN: stdin missing or not JSON' });
  const id = String(input.agent_id || '').replace(/[^\w.-]/g, '_');
  const base = { event: input.hook_event_name, agent_id: id, agent_type: input.agent_type, cwd: input.cwd };
  if (!id) return allow({ ...base, decision: 'allow', reason: 'no agent_id (internal agent or main session)' });
  const tp = transcriptFor(input);
  if (!tp) {
    // Claude Code's own helper agents finish through this hook too: empty agent_type, no transcript, never launched through SubagentStart. They are not
    // agents of ours: a separate non-alarm log entry (no FAIL-OPEN word, so no gate notice). A real agent (an agent type, or an id the launch hook saw)
    // with no transcript is a gate that did not run: FAIL-OPEN.
    if (!input.agent_type && !launchedAgent(id)) return allow({ ...base, decision: 'allow', reason: 'internal helper agent (no agent type, no transcript, no launch record): not checked' });
    return allow({ ...base, decision: 'allow', reason: 'FAIL-OPEN: no readable agent transcript' });
  }

  const runStart = runStartMs(tp);
  let text = firstUserText(tp);
  let info = checkBrief(text);
  let aof = auditOfIn(text);
  if (!aof) {
    const bf0 = briefFileOf(text);
    if (bf0) { try { aof = auditOfIn(fs.readFileSync(bf0, 'utf8')); } catch { /* the builder branch below reports an unreadable BRIEF-FILE */ } }
  }
  if (aof) {
    // Audit fault 4: no builder record is no longer "nothing to compare": recover it from the gate log, else refuse (fail closed).
    const rec = readBuilder(aof, STATE_DIR) || findBuilderInLog(aof, STATE_DIR);
    const auditPath = path.join(aof, AUDIT_FILE);
    const probs = checkAuditFile(auditPath, { builderId: rec && rec.agent_id, actualId: id, notBefore: runStart - SLACK_MS });
    if (!rec) probs.push('no record of who built this folder (no .claude/state/builders entry and no "hand-in check passed" line in gate.log for it): the independence of the auditor cannot be shown. Check the AUDIT-OF path is exactly the HANDIN_DIR of the builder; if the folder was never handed in through the gate, tell the main session in your report and stop');
    if (!probs.length) {
      // Audit faults 1 and 2: the finish gate is the only thing that records an auditor (its id comes from the harness, not from the file).
      try { const text = fs.readFileSync(auditPath, 'utf8'); recordAudit(aof, { auditorId: id, builderId: rec.agent_id, verdict: parseAudit(text).verdict, text }, STATE_DIR); }
      catch (e) { log({ gate: 'audit', decision: 'skip', reason: 'FAIL-OPEN: could not record the audit', error: String((e && e.message) || e).slice(0, 300) }); }
      resetCounter(id);
      return allow({ ...base, decision: 'allow', reason: 'audit written', audit_of: aof, builder_id: rec.agent_id });
    }
    return blockOrRelease({ isPre, base, id, dir: aof, failures: probs.map((p) => ' - ' + p).join('\n'), kind: 'audit',
      intro: `AUDIT GATE: you are the auditor of ${aof} and may not finish yet`,
      fix: `write ${path.join(aof, AUDIT_FILE)} (template docs/templates/AUDIT.md; your agent id is ${id})`, failedFile: 'AUDIT_FAILED.txt' });
  }
  let briefFileText = null;
  let handinDir = info.handinDir;
  let noVisual = info.noVisual;
  // The BRIEF-FILE is read whenever the prompt names one, also when the prompt itself already holds HANDIN_DIR: a coordinator that pastes the HANDIN_DIR line into the
  // prompt but keeps the "HANDIN: no-visual" and GATE lines in the file would otherwise be held to COMPARE.md (found when this very round's hand-in was refused for it).
  {
    const bf = briefFileOf(text);
    if (bf) {
      try { const ft = fs.readFileSync(bf, 'utf8'); briefFileText = ft; const fi = checkBrief(ft); handinDir = handinDir || fi.handinDir; noVisual = noVisual || fi.noVisual; }
      catch (e) { if (!handinDir) return allow({ ...base, decision: 'allow', reason: 'FAIL-OPEN: BRIEF-FILE unreadable', brief_file: bf, error: e.code || e.message }); }
    }
  }
  if (!handinDir) return allow({ ...base, decision: 'allow', reason: 'no HANDIN_DIR in brief' });

  const args = [path.join(ROOT, 'tools', 'check_handin.mjs'), handinDir, '--main', ROOT];
  if (noVisual) args.push('--no-visual');
  // Gates v2: only a brief that carries `GATES-VERSION: 2` (make_brief.mjs) gets the measured checks; its `GATE:` lines are passed on.
  const gv = gatesIn(text) || (briefFileText ? gatesIn(briefFileText) : null);
  if (gv) { args.push('--gates-version', String(gv.version)); for (const g of gv.gates) args.push('--gate', g); }
  const pa = priorAuditIn(text) || (briefFileText ? priorAuditIn(briefFileText) : null);
  if (pa) args.push('--prior-audit', pa);
  if (runStart) args.push('--since', String(Math.round(runStart - SLACK_MS)));
  const r = spawnSync(process.execPath, args, { cwd: ROOT, encoding: 'utf8', timeout: gv ? GATE_TIMEOUT_MS : 30000, env: { ...process.env, CP_FROM_HOOK: '1' } });
  const passed = r.status === 0 && /HAND-IN CHECK PASSED/.test(r.stdout || "");
  const failed = r.status === 1 && /HAND-IN CHECK FAILED/.test(r.stderr || "");
  if (r.error || (!passed && !failed)) {
    return allow({ ...base, decision: 'allow', reason: 'FAIL-OPEN: check_handin crashed or timed out', handin_dir: handinDir, status: r.status, error: String(r.error || r.stderr).slice(0, 300) });
  }
  if (passed) {
    resetCounter(id);
    // BLOCKED items (STATUS.md "BLOCKED: <item> | tried: ... | blocked by: ... | needs: ..."): an honest "cannot do this" is news for the main session.
    const NL = String.fromCharCode(10);
    let blockedLines = String(r.stdout || '').split(NL).filter((l) => l.startsWith('BLOCKED-ITEM:')).map((l) => l.slice(13).trim());
    if (!blockedLines.length) {
      // a brief of the older layout (no measured gates): check_handin does not read the form, but the news still goes to the main session
      try { blockedLines = fs.readFileSync(path.join(handinDir, 'STATUS.md'), 'utf8').split(NL).map((l) => l.trim().replace(/^[-*]\s+/, '')).filter((l) => /^BLOCKED\s*:/i.test(l)).map((l) => l.slice(0, 400)); } catch { /* no STATUS.md: nothing to report */ }
    }
    if (blockedLines.length) {
      // the finish hook runs twice at one finish (PreToolUse, then Stop): the same items are one notice
      let dup = false;
      try { const last = fs.readFileSync(path.join(STATE_DIR, 'gate.log'), 'utf8').split(NL).filter((l) => l.includes('"gate":"blocked"') && l.includes(JSON.stringify(handinDir).slice(1, -1))).pop(); dup = !!last && JSON.stringify(parseJson(last).items) === JSON.stringify(blockedLines.slice(0, 10)); } catch { /* no log yet */ }
      if (!dup) log({ gate: 'blocked', ...base, decision: 'allow', reason: 'BLOCKED', handin_dir: handinDir, items: blockedLines.slice(0, 10), count: blockedLines.length });
    }
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

