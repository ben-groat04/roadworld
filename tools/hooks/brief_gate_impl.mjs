#!/usr/bin/env node
// PreToolUse hook, matcher "Agent": refuses to launch a subagent whose prompt lacks the agent contract.
//
// FAILURE POLICY: FAIL OPEN. Any internal error (bad JSON on stdin, unreadable file, exception, stdin timeout)
// allows the launch, exits 0 and is logged to .claude/state/gate.log. Reason: the main session delegates ALL code
// changes to agents, so a broken gate that blocked every launch would deadlock the whole session.
//
// Allowed: exempt types (Explore, Plan, claude-code-guide, statusline-setup); a prompt that passes checkBrief();
// a prompt with a "NO-CONTRACT: <category>: <reason, >= 20 chars>" line, category one of clerk, merge, research, status, measure (gates13: the allow-list; free text is refused);
// a prompt with "BRIEF-FILE: <absolute path>" whose file passes checkBrief().
// a prompt (or BRIEF-FILE) with "AUDIT-OF: <absolute progress folder>" is an auditor launch (tools/make_brief.mjs --audit):
// allowed without NO-CONTRACT, but refused when the folder is not absolute, when it also carries HANDIN_DIR (an auditor is not a builder),
// or when it pastes the builder's fault list (the auditor writes its own list first).
// Denied: exit 2 with the list of what is missing on stderr (the main session sees it as the tool error).
// Every decision is one JSON line in .claude/state/gate.log of the main checkout.
import fs from 'node:fs';
import path from 'node:path';
import { log, readStdin, parseJson, checkBrief, noContractParse, NO_CONTRACT_CATEGORIES, briefFileOf, EXEMPT_TYPES } from './lib.mjs';
import { auditOfIn, pastedFaultList } from './audit_lib.mjs';
import { firstLookOfIn } from './phase_marks.mjs';

// True when the text asks an agent to write / create an AUDIT.md (or to be "the auditor" that writes one). Mentions of the file are not enough.
export function asksToWriteAudit(text) {
  const t = String(text || '');
  const file = String.raw`(?:[\w./\\:-]*[\\/])?AUDIT\.md\b`;
  return new RegExp(String.raw`\b(?:write|create|produce|author|draft|save|compose|fill\s+in)\s+(?:out\s+)?(?:an?\s+|the\s+|your\s+|its\s+|a\s+new\s+|a\s+fresh\s+)?${file}`, 'i').test(t)
    || new RegExp(String.raw`\b(?:write|save|put|record|put\s+down)\b[^\n.]{0,60}\b(?:in|into|to|as)\s+(?:an?\s+|the\s+|your\s+)?${file}`, 'i').test(t)
    || new RegExp(String.raw`${file}[^\n.]{0,40}\b(?:must|should|needs?\s+to|has\s+to|is\s+to)\s+be\s+(?:written|created|produced)`, 'i').test(t)
    || /^Auditor agent:/m.test(t) || /docs[\\/]templates[\\/]AUDIT\.md/i.test(t) || /\byou\s+are\s+the\s+(?:fresh\s+)?auditor\b/i.test(t);
}

export async function main() {
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
  // auditor launch: AUDIT-OF in the prompt or in its BRIEF-FILE
  let auditText = prompt;
  let ao = auditOfIn(prompt);
  if (!ao && briefFileOf(prompt)) { try { auditText = fs.readFileSync(briefFileOf(prompt), 'utf8'); ao = auditOfIn(auditText); } catch { /* handled below as a normal brief */ } }
  if (ao) {
    const why = [];
    if (!path.win32.isAbsolute(ao) && !path.posix.isAbsolute(ao)) why.push(`AUDIT-OF must be an absolute path (got "${ao}")`);
    if (/^HANDIN_DIR:/m.test(auditText)) why.push('an AUDIT-OF brief must not also carry HANDIN_DIR (an auditor is not a builder; make the brief with make_brief.mjs --audit)');
    const pasted = why.length ? null : pastedFaultList(auditText + '\n' + prompt, ao);
    if (pasted) why.push(`the brief pastes the builder's fault list (${pasted}); the auditor writes its own list BEFORE reading the builder's FAULTS.md`);
    if (!why.length) { log({ ...base, decision: 'allow', reason: 'AUDIT-OF', audit_of: ao }); return 0; }
    log({ ...base, decision: 'deny', reason: 'AUDIT-OF refused', missing: why });
    process.stderr.write('LAUNCH REFUSED by the brief gate (AUDIT-OF launch):\n - ' + why.join('\n - ') +
      '\nBuild the auditor brief with `node tools/make_brief.mjs <task-file> --audit <progress-folder>`.\n');
    return 2;
  }
  // gates10: a first-look launch (make_brief --first-look): FIRSTLOOK-OF in the prompt or its BRIEF-FILE. Same shape as an auditor: absolute folder, never a builder (no HANDIN_DIR).
  let flText = prompt;
  let fo = firstLookOfIn(prompt);
  if (!fo && briefFileOf(prompt)) { try { flText = fs.readFileSync(briefFileOf(prompt), 'utf8'); fo = firstLookOfIn(flText); } catch { /* handled below as a normal brief */ } }
  if (fo) {
    const why = [];
    if (!path.win32.isAbsolute(fo) && !path.posix.isAbsolute(fo)) why.push(`FIRSTLOOK-OF must be an absolute path (got "${fo}")`);
    if (/^HANDIN_DIR:/m.test(flText)) why.push('a FIRSTLOOK-OF brief must not also carry HANDIN_DIR (a first look is not a builder; make the brief with make_brief.mjs --first-look)');
    if (!why.length) { log({ ...base, decision: 'allow', reason: 'FIRSTLOOK-OF', firstlook_of: fo }); return 0; }
    log({ ...base, decision: 'deny', reason: 'FIRSTLOOK-OF refused', missing: why });
    process.stderr.write('LAUNCH REFUSED by the brief gate (FIRSTLOOK-OF launch):\n - ' + why.join('\n - ') + '\nBuild the first-look brief with `node tools/make_brief.mjs <task-file> --first-look <progress-folder>`.\n');
    return 2;
  }
  // gates13 (main-session decision 1): launches by ALLOW-LIST, not by word list. A launch carries the contract (or BRIEF-FILE), AUDIT-OF, FIRSTLOOK-OF, or a line
  // `NO-CONTRACT: <category>: <reason, 20+ characters>` with the category exactly one of clerk, merge, research, status, measure. Anything else is refused whatever its wording (the 23 Oct audits
  // and first looks all passed as free-text NO-CONTRACT; a vocabulary list was evaded by "Vet", "QA", "Proof-read"...). The old free-text form is refused with the categories named.
  const ncp = noContractParse(prompt);
  if (ncp.form === 'free' || ncp.form === 'short') {
    log({ ...base, decision: 'deny', reason: 'NO-CONTRACT free-text form refused', no_contract_reason: ncp.text });
    process.stderr.write('LAUNCH REFUSED by the brief gate: ' + (ncp.form === 'short' ? 'the NO-CONTRACT reason is under 20 characters.' : 'the free-text NO-CONTRACT form is not accepted any more.') +
      ' A launch carries ONE of: the contract (build it with `node tools/make_brief.mjs <task-file> --round <name>`, or a BRIEF-FILE line), `AUDIT-OF: <progress folder>` (`make_brief.mjs --audit`), `FIRSTLOOK-OF: <progress folder>` (`make_brief.mjs --first-look`), or a line `NO-CONTRACT: <category>: <reason, 20+ characters>` with category exactly one of ' +
      NO_CONTRACT_CATEGORIES.join(', ') + '. An audit or first look is never one of those categories.
');
    return 2;
  }
  const nc = ncp.form === 'ok' ? ncp.reason : null;
  // A NO-CONTRACT launch cannot be asked to WRITE an AUDIT.md: that file only counts when an auditor launched with make_brief --audit (marker AUDIT-OF) writes it and the finish
  // gate records it. Only prompts that ask for the file to be written are refused: mergers, clerks and readers may mention AUDIT.md, audits and progress folders freely
  // (gates3 audit fault 3: the wording rule refused merge and bookkeeping prompts and was evaded by synonyms). A synonym that slips past this wording buys nothing:
  // an audit clears the owed-audit item only through the hook-written audit record, which a NO-CONTRACT agent never produces (it stays owed and on the notice).
  if (nc) {
    const hay = prompt + ' ' + String(ti.description || '');
    if (asksToWriteAudit(hay)) {
      const why = 'the prompt asks for an AUDIT.md to be written, but a NO-CONTRACT launch has no AUDIT-OF marker: the audit would not be recorded and the round would stay owed';
      log({ ...base, decision: 'deny', reason: 'NO-CONTRACT audit launch refused', no_contract_reason: nc });
      process.stderr.write('LAUNCH REFUSED by the brief gate: ' + why + '.\nBuild the auditor brief with `node tools/make_brief.mjs <task-file> --audit <progress-folder>` (marker line AUDIT-OF). Prompts that only mention AUDIT.md (merge, bookkeeping, reading it) are not refused.\n');
      return 2;
    }
  }
  if (nc) {
    log({ ...base, decision: 'allow', reason: 'NO-CONTRACT', no_contract_reason: nc, no_contract_category: ncp.category });
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
    'Allowed launch forms: the contract, `AUDIT-OF: <folder>` (make_brief.mjs --audit), `FIRSTLOOK-OF: <folder>` (make_brief.mjs --first-look), or for non-hand-in work a line `NO-CONTRACT: <category>: <reason, at least 20 characters>` with category exactly one of clerk, merge, research, status, measure. ' +
    'To point at a file instead, add `BRIEF-FILE: <absolute path>` (the file must pass the same test).\n');
  return 2;
}

