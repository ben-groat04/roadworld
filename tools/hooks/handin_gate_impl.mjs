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
import crypto from 'node:crypto';
import { spawnSync } from 'node:child_process';
import { ROOT, STATE_DIR, log, readStdin, parseJson, firstUserText, checkBrief, briefFileOf, agentTranscript } from './lib.mjs';
import { phaseIn, firstLookOfIn, checkFirstLookFile, readOrderProblem, FIRSTLOOK_FILE } from './phase_marks.mjs';
import { auditOfIn, readBuilder, recordBuilder, checkAuditFile, AUDIT_FILE, findBuilderInLog, recordAudit, parseAudit } from './audit_lib.mjs';

const MAX_BLOCKS = 3;
// Measured gates build and run the scene check: allow minutes (settings.json hook timeout must be larger; a timeout fails open and is logged).
const GATE_TIMEOUT_MS = 270000;
export function suitesIn(text) { return [...String(text || '').replace(/\r\n/g, '\n').matchAll(/^SUITE:[ \t]*(.+?)[ \t]*$/gm)].map((m) => m[1]); }
export function priorAuditIn(text) { const m = String(text || '').match(/^PRIOR-AUDIT:[ 	]*(.+?)[ 	]*$/m); return m ? m[1].replace(/^`|`$/g, '') : null; }
export function gatesIn(text) {
  const t = String(text || '').split('\r\n').join('\n');
  const v = t.match(/^GATES-VERSION:[ \t]*(\d+)[ \t]*$/m);
  if (!v) return null;
  return { version: Number(v[1]), gates: [...t.matchAll(/^GATE:[ \t]*(.+?)[ \t]*$/gm)].map((m) => m[1]), items: [...t.matchAll(/^ITEM:[ \t]*(.+?)[ \t]*$/gm)].map((m) => m[1]) };
}

// When this agent's run began: timestamp of the first transcript line, else the transcript file's creation time.
// Files the gates judge must have been written after it (minus slack), so a stale passing folder from an earlier round
// cannot satisfy a new agent (audit fault 1).
export const SLACK_MS = 2000;
export const CHECKPOINT_WINDOW_MS = 30000;   // the finish hook runs twice at one finish, seconds apart; a second call this soon, by the same agent, for the unchanged CHECKPOINT.md, is the same finish
// Time of the latest commit of the worktree that holds the hand-in folder, ignoring commits that only touch the hand-in folder itself (committing the checkpoint and its picture must not void it); null when git cannot say.
export function latestWorkCommitMs(handinDir) {
  try {
    const n = String(handinDir).replace(/\\/g, '/'); const k = n.toLowerCase().indexOf('/docs/progress/');
    if (k < 0) return null;
    const wt = n.slice(0, k);
    const run = (extra) => spawnSync('git', ['-C', wt, 'log', '-1', '--format=%ct', '--', ...extra], { encoding: 'utf8', timeout: 5000 });
    const rel = n.slice(k + 1).replace(/\/+$/, '');
    let r = run(['.', ':(exclude)' + rel]);
    if (r.status !== 0) r = run([]);
    if (r.status !== 0) return null;
    const v = Number(String(r.stdout).trim());
    return Number.isFinite(v) && v > 0 ? v * 1000 : null;
  } catch { return null; }
}
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

export async function main() {
  const raw = await readStdin();
  const input = raw === null ? undefined : parseJson(raw);
  const isPre = input && input.hook_event_name === 'PreToolUse';
  const allow = (entry) => { log({ gate: 'handin', ...entry }); return 0; };
  if (!input || typeof input !== 'object') return allow({ decision: 'allow', reason: 'FAIL-OPEN: stdin missing or not JSON' });
  const id = String(input.agent_id || '').replace(/[^\w.-]/g, '_');
  const base = { event: input.hook_event_name, agent_id: id, agent_type: input.agent_type, cwd: input.cwd };
  if (!id) return allow({ ...base, decision: 'allow', reason: 'no agent_id (internal agent or main session)' });
  const tp = agentTranscript(input);
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
  // gates10: an Opus first-look agent (make_brief --first-look): may not finish until FIRSTLOOK_OPUS.md passes (its own 3-refusal count, FIRSTLOOK_FAILED.txt on release)
  let flo = firstLookOfIn(text);
  if (!flo) { const bf1 = briefFileOf(text); if (bf1) { try { flo = firstLookOfIn(fs.readFileSync(bf1, 'utf8')); } catch { /* handled as a normal brief below */ } } }
  if (flo) {
    const probs = checkFirstLookFile(flo, { actualId: id, notBefore: runStart - SLACK_MS, main: ROOT });
    const ord = readOrderProblem(tp);
    if (ord) { let ft = ''; try { ft = fs.readFileSync(path.join(flo, FIRSTLOOK_FILE), 'utf8'); } catch { /* reported above */ } if (!/^Order:[ \t]*\S.{19,}/m.test(ft)) probs.push(ord + ' (or add a line "Order: <what you read first and why>", 20+ characters, which the main session will see)'); }
    if (!probs.length) { resetCounter(id); return allow({ ...base, decision: 'allow', reason: 'first look written', firstlook_of: flo }); }
    return blockOrRelease({ isPre, base, id, dir: flo, failures: probs.map((p) => ' - ' + p).join('\n'), kind: 'firstlook',
      intro: 'FIRST-LOOK GATE: you may not finish yet', fix: 'write ' + path.join(flo, FIRSTLOOK_FILE) + ' (template docs/templates/FIRSTLOOK.md; your agent id is ' + id + ')', failedFile: 'FIRSTLOOK_FAILED.txt' });
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

  // gates8 CHECKPOINT hand-back: a builder may stop early with <HANDIN_DIR>/CHECKPOINT.md (first line "CHECKPOINT: <what is done> | images: <paths>", one existing image at least). The stop is let
  // through ONCE per agent, logged as decision=checkpoint, and is NOT a hand-in: no builder record, no audit owed, no counter. The hook runs twice at one finish (PreToolUse on
  // SubagentHandback, then Stop): the second call within CHECKPOINT_WINDOW_MS of the first, for the same unchanged file, is the same finish. Any later stop is a hand-in and gets the full check.
  const phase = phaseIn(text) || (briefFileText ? phaseIn(briefFileText) : null);
  const phased = phase === 'measure' || phase === 'build';   // a phase agent of a split round: its own check, its own counter, no CHECKPOINT allowance, no audit owed
  let checkpointNote = '';
  if (!phased) try {
    const cpFile = path.join(handinDir, 'CHECKPOINT.md');
    // gates9 (gates8 audit H5): a CHECKPOINT.md older than the latest commit of the worktree is a leftover (the builder worked on after it): ignored and logged, the stop is a hand-in.
    const cpMtime0 = fs.existsSync(cpFile) ? fs.statSync(cpFile).mtimeMs : 0;
    const lastCommit = cpMtime0 ? latestWorkCommitMs(handinDir) : null;
    if (cpMtime0 && lastCommit !== null && Math.floor(cpMtime0 / 1000) < Math.floor(lastCommit / 1000)) {
      checkpointNote = 'CHECKPOINT.md ignored: it is older than the latest commit of this worktree (' + new Date(lastCommit).toISOString() + '; the file was written ' + new Date(cpMtime0).toISOString() + '), so it is a leftover and this stop is a hand-in with the full check. Delete CHECKPOINT.md before the final hand-in.';
      log({ gate: 'checkpoint', ...base, decision: 'ignored', reason: 'CHECKPOINT.md ignored: older than the latest commit of the worktree (final hand-in)', handin_dir: handinDir });
    } else if (cpMtime0) {
      // one checkpoint per ROUND (the hand-in folder), whichever agent takes it
      const mf = path.join(STATE_DIR, 'handin', 'checkpoint_' + crypto.createHash('sha1').update(String(handinDir).replace(/\\/g, '/').toLowerCase()).digest('hex').slice(0, 12) + '.json');
      let prevRec = null; try { prevRec = parseJson(fs.readFileSync(mf, 'utf8')) || null; } catch { /* none yet */ }
      const cpMtime = cpMtime0;
      if (prevRec && prevRec.agent_id === id && cpMtime <= prevRec.at && Date.now() - prevRec.at < CHECKPOINT_WINDOW_MS) {
        return allow({ ...base, decision: 'allow', reason: 'checkpoint hand-back (second hook call of the same finish)', handin_dir: handinDir });
      }
      if (!prevRec) {
        const cr = spawnSync(process.execPath, [path.join(ROOT, 'tools', 'check_handin.mjs'), handinDir, '--checkpoint', '--main', ROOT, '--since', String(Math.round(runStart - SLACK_MS))], { cwd: ROOT, encoding: 'utf8', timeout: 30000, env: { ...process.env, CP_FROM_HOOK: '1' } });
        if (cr.status === 0 && /CHECKPOINT PASSED/.test(cr.stdout || '')) {
          fs.mkdirSync(path.dirname(mf), { recursive: true });
          fs.writeFileSync(mf, JSON.stringify({ at: Date.now(), agent_id: id, handin_dir: handinDir }));
          return allow({ ...base, decision: 'checkpoint', reason: 'checkpoint hand-back: stop allowed once, NOT a hand-in (no audit owed); the final hand-in still needs the full check', handin_dir: handinDir });
        }
        // an invalid CHECKPOINT.md is not a reason to stop: the full check follows, and the message says why the checkpoint did not count
        checkpointNote = 'CHECKPOINT.md did not count: ' + String(cr.stderr || cr.stdout || '').trim().replace(/\s+/g, ' ').slice(0, 500);
      } else checkpointNote = 'a checkpoint hand-back was already used for this round (once per round, by agent ' + (prevRec.agent_id || 'unknown') + ' at ' + new Date(prevRec.at).toISOString() + '): this stop is a hand-in and gets the full check. Delete CHECKPOINT.md before the final hand-in.';
    }
  } catch (e) { log({ gate: 'handin', ...base, decision: 'allow', reason: 'FAIL-OPEN: the checkpoint check crashed (the full check follows)', error: String((e && e.message) || e).slice(0, 300) }); }

  const args = [path.join(ROOT, 'tools', 'check_handin.mjs'), handinDir, '--main', ROOT];
  if (noVisual) args.push('--no-visual');
  if (phased) { args.push('--phase', phase); for (const sc of [...suitesIn(text), ...(briefFileText ? suitesIn(briefFileText) : [])]) args.push('--suite', sc); }
  // Gates v2: only a brief that carries `GATES-VERSION: 2` (make_brief.mjs) gets the measured checks; its `GATE:` lines are passed on.
  const gv = phased ? null : (gatesIn(text) || (briefFileText ? gatesIn(briefFileText) : null));
  if (gv) { args.push('--gates-version', String(gv.version)); for (const g of gv.gates) args.push('--gate', g); for (const it of gv.items || []) args.push('--item', it); }
  const pa = priorAuditIn(text) || (briefFileText ? priorAuditIn(briefFileText) : null);
  if (pa) args.push('--prior-audit', pa);
  if (runStart) args.push('--since', String(Math.round(runStart - SLACK_MS)));
  const r = spawnSync(process.execPath, args, { cwd: ROOT, encoding: 'utf8', timeout: gv || phased ? GATE_TIMEOUT_MS : 30000, env: { ...process.env, CP_FROM_HOOK: '1' } });
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
    if (phased) return allow({ ...base, decision: 'allow', reason: 'phase check passed (' + phase + '): not a hand-in, no audit owed until the render phase', handin_dir: handinDir, phase });
    try {
      const { changed, rec } = recordBuilder(handinDir, id, input.agent_type, STATE_DIR);
      if (changed) log({ gate: 'audit', decision: 'owed', reason: 'audit owed: hand-in passed, no AUDIT.md by a different agent yet', handin_dir: handinDir, builder_id: id, builder_type: rec.agent_type });
    } catch (e) { log({ gate: 'audit', decision: 'skip', reason: 'FAIL-OPEN: could not record the builder', error: String(e && e.message || e).slice(0, 300) }); }
    return allow({ ...base, decision: 'allow', reason: 'hand-in check passed', handin_dir: handinDir });
  }

  const failures = ((checkpointNote ? checkpointNote + String.fromCharCode(10) : '') + (r.stderr || r.stdout || '')).trim();
  return blockOrRelease({ isPre, base, id, dir: handinDir, failures, kind: 'handin',
    intro: phased ? `PHASE GATE (${phase}): you may not finish yet` : 'HAND-IN GATE: you may not finish yet',
    fix: `run \`node tools/check_handin.mjs ${handinDir}${phased ? ' --phase ' + phase : noVisual ? ' --no-visual' : ''}\` until it passes`, failedFile: 'HANDIN_FAILED.txt' });
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
        `The ${audit ? 'AUDIT.md' : kind === 'firstlook' ? 'FIRSTLOOK_OPUS.md' : 'hand-in'} check still failed after ${MAX_BLOCKS} refusals; the agent was allowed to stop (${new Date().toISOString()}).\n\n${failures}\n`);
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

