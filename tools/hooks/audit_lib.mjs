// Builder / auditor split, made mechanical. Shared by brief_gate (launch), handin_gate (finish), notice_lib (audit-owed
// notice) and check_handin --accept. Rule (owner, 11 Oct): the agent that builds a round never audits it.
//
// State: .claude/state/builders/<key>.json = { handin_dir, agent_id, agent_type, passed_at }, written by handin_gate when a
// builder's hand-in passes check_handin. An audit is OWED for every record that has no valid AUDIT.md (by a different
// agent, written after the hand-in passed). Nothing but that file clears it.
import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import { ROOT, STATE_DIR, parseJson } from './lib.mjs';

export const AUDIT_FILE = 'AUDIT.md';
export const sanitizeId = (s) => String(s || '').replace(/[^\w.-]/g, '_');

// "AUDIT-OF: <absolute progress folder>" at the start of a line (the contract text only mentions it mid-line).
export function auditOfIn(text) {
  const m = String(text || '').match(/^AUDIT-OF:[ \t]*(.+?)[ \t]*$/m);
  return m ? m[1].replace(/^`|`$/g, '') : null;
}

export const builderKey = (dir) => crypto.createHash('sha1')
  .update(path.resolve(dir).replace(/\\/g, '/').replace(/\/+$/, '').toLowerCase()).digest('hex').slice(0, 16);
const buildersDir = (stateDir) => path.join(stateDir, 'builders');

export function readBuilder(handinDir, stateDir = STATE_DIR) {
  try { return parseJson(fs.readFileSync(path.join(buildersDir(stateDir), builderKey(handinDir) + '.json'), 'utf8')) || null; } catch { return null; }
}
// Returns { changed, rec }. changed = no record yet, or a different agent id passed (a fresh builder after "send back").
export function recordBuilder(handinDir, agentId, agentType, stateDir = STATE_DIR) {
  const prev = readBuilder(handinDir, stateDir);
  if (prev && prev.agent_id === agentId) return { changed: false, rec: prev };
  const rec = { handin_dir: path.resolve(handinDir), agent_id: agentId, agent_type: agentType || '', passed_at: new Date().toISOString() };
  fs.mkdirSync(buildersDir(stateDir), { recursive: true });
  fs.writeFileSync(path.join(buildersDir(stateDir), builderKey(handinDir) + '.json'), JSON.stringify(rec));
  return { changed: true, rec };
}
export function allBuilders(stateDir = STATE_DIR) {
  let names = [];
  try { names = fs.readdirSync(buildersDir(stateDir)); } catch { return []; }
  return names.filter((n) => n.endsWith('.json'))
    .map((n) => parseJson(fs.readFileSync(path.join(buildersDir(stateDir), n), 'utf8')))
    .filter((r) => r && r.handin_dir && r.agent_id);
}

// Structural check of AUDIT.md text. Returns { problems, auditor, verdict, opened, total }.
export function parseAudit(raw) {
  const t = String(raw || '').replace(/\r\n/g, '\n').replace(/<!--[\s\S]*?-->/g, '');
  const problems = [];
  const head = t.split('\n').filter((l) => l.trim()).slice(0, 12).join('\n');
  const a = head.match(/^Auditor agent:[ \t]*(\S+)[ \t]*$/m);
  const auditor = a ? sanitizeId(a[1]) : null;
  if (!a || !/^[\w.-]+$/.test(a[1])) problems.push('first lines must hold "Auditor agent: <your agent id>" (your id is given at the top of your context)');
  const v = head.match(/^Verdict:[ \t]*(accept|send back)[ \t]*$/mi);
  if (!v) problems.push('first lines must hold "Verdict: accept" or "Verdict: send back"');
  const ro = head.match(/^References opened:[ \t]*(\d+)[ \t]+of[ \t]+(\d+)\b/m);
  if (!ro) problems.push('first lines must hold "References opened: N of M" (integers)');
  else if (+ro[1] < +ro[2] && !t.split('\n').some((l) => /not opened/i.test(l) && /[\w-]+\.\w{2,5}\b/.test(l))) {
    problems.push(`References opened ${ro[1]} of ${ro[2]}: add a "not opened: <file> (reason)" line per unopened file`);
  }
  const heads = [...t.matchAll(/^##[ \t]+(.*)$/gm)];
  const rankedAt = heads.map((h) => /ranked/i.test(h[1])).lastIndexOf(true);
  const fi = rankedAt >= 0 ? rankedAt : heads.map((h) => /fault/i.test(h[1])).lastIndexOf(true);
  if (fi < 0) problems.push('no "## ... faults ..." section (faults ranked, worst first; write "none found" with what you checked if true)');
  else {
    const start = heads[fi].index + heads[fi][0].length;
    const end = heads[fi + 1] ? heads[fi + 1].index : t.length;
    if (t.slice(start, end).trim().length < 40) problems.push('the faults section is empty or under 40 characters');
  }
  return { problems, auditor, verdict: v ? v[1].toLowerCase() : null, opened: ro ? +ro[1] : null, total: ro ? +ro[2] : null };
}

// Problems with one AUDIT.md file. builderId: the recorded builder (must differ); actualId: the finishing agent (must match the claim).
export function checkAuditFile(file, { builderId = null, actualId = null, notBefore = 0 } = {}) {
  let raw;
  try { raw = fs.readFileSync(file, 'utf8'); } catch { return [`${AUDIT_FILE} is missing (${file})`]; }
  const parsed = parseAudit(raw);
  const p = parsed.problems.slice();
  const a = parsed.auditor;
  if (a && builderId && a === sanitizeId(builderId)) p.push(`the auditor id "${a}" is the builder's id: builder and auditor are never the same agent`);
  if (a && actualId && a !== sanitizeId(actualId)) p.push(`"Auditor agent: ${a}" is not you (your agent id is ${sanitizeId(actualId)})`);
  if (notBefore) {
    try { if (fs.statSync(file).mtimeMs < notBefore) p.push('AUDIT.md is older than the builder\'s latest passed hand-in (a fresh audit is owed)'); } catch { /* handled above */ }
  }
  return p;
}

// Where an AUDIT.md for a hand-in folder may live: the folder itself, or the same round folder in the main checkout (after merge).
export function auditFiles(handinDir, root = ROOT) {
  return [...new Set([path.join(handinDir, AUDIT_FILE), path.join(root, 'docs', 'progress', path.basename(handinDir), AUDIT_FILE)])];
}
// Problems for a hand-in folder across its candidate files: [] = a valid independent audit exists.
export function auditProblems(handinDir, rec, root = ROOT, actualId = null) {
  const notBefore = rec && rec.passed_at ? Date.parse(rec.passed_at) - 2000 : 0;
  let best = null;
  for (const f of auditFiles(handinDir, root)) {
    const p = checkAuditFile(f, { builderId: rec && rec.agent_id, actualId, notBefore });
    if (!p.length) return [];
    if (!best || best.some((x) => /is missing/.test(x))) best = p;
  }
  return best || [`${AUDIT_FILE} is missing`];
}

// Every recorded hand-in with no valid AUDIT.md. Records whose folder is gone everywhere are dropped (nothing left to audit).
export function auditOwed(stateDir = STATE_DIR, root = ROOT) {
  const out = [];
  for (const rec of allBuilders(stateDir)) {
    const folders = auditFiles(rec.handin_dir, root).map((f) => path.dirname(f));
    if (!folders.some((d) => fs.existsSync(d))) continue;
    const problems = auditProblems(rec.handin_dir, rec, root);
    if (problems.length) out.push({ handin_dir: rec.handin_dir, builder_id: rec.agent_id, builder_type: rec.agent_type, passed_at: rec.passed_at, problems });
  }
  return out.sort((a, b) => String(a.passed_at).localeCompare(String(b.passed_at)));
}

// Did a launch prompt paste the builder's fault list? Returns a reason string or null.
export function pastedFaultList(prompt, folder, root = ROOT) {
  const p = String(prompt || '').replace(/\r\n/g, '\n');
  if (/^##[ \t]+(Pass[ \t]+\d+|Residual)\b/im.test(p)) return 'it has a "## Pass N" or "## Residual" heading (the structure of the builder\'s FAULTS.md)';
  for (const f of [path.join(folder, 'FAULTS.md'), path.join(root, 'docs', 'progress', path.basename(folder), 'FAULTS.md')]) {
    let txt;
    try { txt = fs.readFileSync(f, 'utf8'); } catch { continue; }
    const hits = [...new Set(txt.replace(/\r\n/g, '\n').split('\n').map((l) => l.trim()).filter((l) => l.length >= 60))].filter((l) => p.includes(l));
    if (hits.length >= 2) return `it repeats ${hits.length} long lines of ${f}`;
  }
  return null;
}
