// Builder / auditor split, made mechanical. Shared by brief_gate (launch), handin_gate (finish), notice_lib (audit-owed
// notice) and check_handin --accept. Rule (owner, 11 Oct): the agent that builds a round never audits it.
//
// State: .claude/state/builders/<key>.json = { handin_dir, agent_id, agent_type, passed_at }, written by handin_gate when a
// builder's hand-in passes check_handin. An audit is OWED for every record that has no valid AUDIT.md (by a different
// agent, written after the hand-in passed). Nothing but that file clears it.
import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import { ROOT, STATE_DIR, parseJson, signRecord, verifyRecord } from './lib.mjs';

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

// Records are HMAC-signed (lib.mjs signRecord, v:3). An unsigned or edited record reads as null, so typing one by hand achieves nothing.
export function readBuilder(handinDir, stateDir = STATE_DIR) {
  try { const r = parseJson(fs.readFileSync(path.join(buildersDir(stateDir), builderKey(handinDir) + '.json'), 'utf8')); return r && verifyRecord(r, stateDir) ? r : null; } catch { return null; }
}
// Every PASSED hand-in renews passed_at (audit fault 3: a builder resumed after "send back" keeps its agent id, so a new pass
// must invalidate the audit of the old work). changed = a new record, another agent id, or an audit was recorded after the previous pass
// (the hand-in hook runs twice back to back at one finish: the second pass is not a new hand-in, so it logs nothing). v:2 marks records made by this
// code: only those need an audit record (see recordAudit) so that audits made under the older gate are not owed again.
export function recordBuilder(handinDir, agentId, agentType, stateDir = STATE_DIR, now = Date.now()) {
  const prev = readBuilder(handinDir, stateDir);
  const ar = prev && readAudit(handinDir, stateDir);
  const changed = !prev || prev.agent_id !== agentId || !!(ar && Date.parse(ar.recorded_at) >= Date.parse(prev.passed_at));
  const rec = signRecord({ v: 3, handin_dir: path.resolve(handinDir), agent_id: agentId, agent_type: agentType || '', passed_at: new Date(now).toISOString() }, stateDir);
  fs.mkdirSync(buildersDir(stateDir), { recursive: true });
  fs.writeFileSync(path.join(buildersDir(stateDir), builderKey(handinDir) + '.json'), JSON.stringify(rec));
  return { changed, rec };
}

// Audit records (audit faults 1, 2, 4): written by the finish gate (handin_gate) when a real agent id passes as auditor of a folder.
// .claude/state/audits/<key>.json = { v:2, handin_dir, auditor_id, builder_id, verdict, sha1 (of AUDIT.md, line endings normalised), recorded_at }.
// An AUDIT.md clears the owed audit only with such a record whose auditor_id equals the file's "Auditor agent:" line, whose sha1
// matches the file now and whose recorded_at is after the builder's latest passed hand-in. Typing an AUDIT.md by hand records nothing.
const auditsDir = (stateDir) => path.join(stateDir, 'audits');
export const auditSha = (text) => crypto.createHash('sha1').update(String(text).split(String.fromCharCode(13) + String.fromCharCode(10)).join(String.fromCharCode(10))).digest('hex');
export function recordAudit(handinDir, { auditorId, builderId, verdict, text }, stateDir = STATE_DIR, now = Date.now()) {
  const rec = signRecord({ v: 3, handin_dir: path.resolve(handinDir), auditor_id: sanitizeId(auditorId), builder_id: builderId ? sanitizeId(builderId) : null, verdict, sha1: auditSha(text), recorded_at: new Date(now).toISOString() }, stateDir);
  fs.mkdirSync(auditsDir(stateDir), { recursive: true });
  fs.writeFileSync(path.join(auditsDir(stateDir), builderKey(handinDir) + '.json'), JSON.stringify(rec));
  return rec;
}
export function readAudit(handinDir, stateDir = STATE_DIR) {
  try { const r = parseJson(fs.readFileSync(path.join(auditsDir(stateDir), builderKey(handinDir) + '.json'), 'utf8')); return r && verifyRecord(r, stateDir) ? r : null; } catch { return null; }
}
// Audit fault 4: no builder record. Recover the builder id from the gate log (a "hand-in check passed" line for this folder).
export function findBuilderInLog(handinDir, stateDir = STATE_DIR) {
  const norm = (p) => path.resolve(p).split(path.sep).join('/').replace(/\/+$/, '').toLowerCase();
  const want = norm(handinDir);
  let lines = [];
  try { lines = fs.readFileSync(path.join(stateDir, 'gate.log'), 'utf8').split(String.fromCharCode(10)); } catch { return null; }
  for (let i = lines.length - 1; i >= 0; i--) {
    const e = lines[i] && parseJson(lines[i]);
    if (e && e.gate === 'handin' && e.reason === 'hand-in check passed' && e.agent_id && e.handin_dir && norm(e.handin_dir) === want) return { agent_id: e.agent_id, agent_type: e.agent_type || '', passed_at: e.ts, from_log: true };
  }
  return null;
}
export function allBuilders(stateDir = STATE_DIR) {
  let names = [];
  try { names = fs.readdirSync(buildersDir(stateDir)); } catch { return []; }
  return names.filter((n) => n.endsWith('.json'))
    .map((n) => parseJson(fs.readFileSync(path.join(buildersDir(stateDir), n), 'utf8')))
    .filter((r) => r && r.handin_dir && r.agent_id && verifyRecord(r, stateDir));
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
// opts: { stateDir, requireRecord (default TRUE in every path: a typed AUDIT.md never clears an audit; only check_handin --legacy turns it off, logged), requireAccept }.
export function auditProblems(handinDir, rec, root = ROOT, actualId = null, opts = {}) {
  const stateDir = opts.stateDir || STATE_DIR;
  const notBefore = rec && rec.passed_at ? Date.parse(rec.passed_at) - 2000 : 0;
  const needRecord = opts.requireRecord ?? true;
  let best = null;
  for (const f of auditFiles(handinDir, root)) {
    const p = checkAuditFile(f, { builderId: rec && rec.agent_id, actualId, notBefore });
    if (!p.length) {
      let text = ''; try { text = fs.readFileSync(f, 'utf8'); } catch { /* checked above */ }
      const parsed = parseAudit(text);
      if (opts.requireAccept && parsed.verdict !== 'accept') { best = [`the audit's verdict is "${parsed.verdict}", not "accept": a send-back audit never accepts a round`]; continue; }
      if (needRecord) {
        const ar = readAudit(handinDir, stateDir);
        const q = [];
        if (!ar) q.push('no audit record: this AUDIT.md was not written by an auditor that passed the finish gate (a typed file does not count); start a real auditor with make_brief --audit');
        else {
          if (ar.auditor_id !== parsed.auditor) q.push(`the audit record says auditor "${ar.auditor_id}", the file says "${parsed.auditor}"`);
          if (ar.sha1 !== auditSha(text)) q.push('AUDIT.md was edited after the auditor finished (its hash differs from the audit record)');
          if (rec && rec.passed_at && Date.parse(ar.recorded_at) < Date.parse(rec.passed_at)) q.push('the audit record is older than the builder\'s latest passed hand-in (a fresh audit is owed)');
          if (rec && ar.auditor_id === sanitizeId(rec.agent_id)) q.push('the recorded auditor is the builder');
          if (rec && ar.builder_id && ar.builder_id !== sanitizeId(rec.agent_id)) q.push(`the audit record is of builder ${ar.builder_id}'s hand-in, the current builder is ${sanitizeId(rec.agent_id)}: a fresh audit is owed`);
        }
        if (q.length) { best = q; continue; }
      }
      return [];
    }
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
    const problems = auditProblems(rec.handin_dir, rec, root, null, { stateDir });
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
