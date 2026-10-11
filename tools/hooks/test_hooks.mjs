#!/usr/bin/env node
// Unit tests for the three hooks with synthetic stdin JSON. Usage: node tools/hooks/test_hooks.mjs
// Copies the hook files, check_handin.mjs, make_brief.mjs, the contract and templates into a temp "main checkout" and
// runs every script as a child process from a different cwd, as the harness would. Exit 1 if any test fails.
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { spawnSync } from 'node:child_process';
import { fileURLToPath, pathToFileURL } from 'node:url';

const src = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..', '..');
const tmp = fs.mkdtempSync(path.join(os.tmpdir(), 'hooktest-'));
const root = path.join(tmp, 'main');
const wt = path.join(tmp, 'wt');
const cp = (rel) => { const d = path.join(root, rel); fs.mkdirSync(path.dirname(d), { recursive: true }); fs.copyFileSync(path.join(src, rel), d); };
['tools/hooks/lib.mjs', 'tools/hooks/audit_lib.mjs', 'tools/hooks/brief_gate.mjs', 'tools/hooks/handin_gate.mjs', 'tools/hooks/contract_inject.mjs', 'tools/hooks/notice_lib.mjs', 'tools/hooks/gate_notice.mjs', 'tools/hooks/gate_ack.mjs',
  'tools/check_handin.mjs', 'tools/make_brief.mjs', 'docs/AGENT_CONTRACT.md', 'docs/templates/BRIEF.md', 'docs/templates/AUDIT_BRIEF.md'].forEach(cp);
// three reference files in the temp main checkout
for (const f of ['aerial/a.jpg', 'street/b.jpg', 'gsv/c.jpg']) { const p = path.join(root, 'docs/reference', f); fs.mkdirSync(path.dirname(p), { recursive: true }); fs.writeFileSync(p, 'x'); }
fs.mkdirSync(path.join(root, '.git'), { recursive: true }); // main-checkout marker for make_brief's canary
const elsewhere = path.join(tmp, 'elsewhere'); fs.mkdirSync(elsewhere);
const logPath = path.join(root, '.claude/state/gate.log');
const lastLog = () => { const l = fs.readFileSync(logPath, 'utf8').trim().split('\n'); return JSON.parse(l[l.length - 1]); };

const run = (script, stdin) => {
  const r = spawnSync(process.execPath, [path.join(root, 'tools/hooks', script)], { input: typeof stdin === 'string' ? stdin : JSON.stringify(stdin), cwd: elsewhere, encoding: 'utf8', timeout: 60000 });
  return { code: r.status, out: r.stdout, err: r.stderr };
};
let pass = 0, fail = 0;
const t = (name, cond, detail = '') => { if (cond) { pass++; console.log('  ok   ' + name); } else { fail++; console.log('  FAIL ' + name + (detail ? '\n       ' + String(detail).replace(/\n/g, '\n       ') : '')); } };
const J = (s) => { try { return JSON.parse(s); } catch { return {}; } };

// ---- briefs
const task = (owner = '"The photo is the target." (11 Oct)') => `# T\n## Owner's words\n${owner}\n## Cases\ncase A | a.jpg b.jpg c.jpg\n## Faults\nnone known\n## Launched\nx\n## Waiting\nnone\n`;
const mk = (round, flags = []) => {
  const tf = path.join(tmp, round + '.md'); fs.writeFileSync(tf, task());
  const r = spawnSync(process.execPath, [path.join(root, 'tools/make_brief.mjs'), tf, '--round', round, '--worktree', path.join(wt, round), ...flags], { encoding: 'utf8' });
  if (r.status !== 0) throw new Error('make_brief failed: ' + r.stderr);
  return r.stdout;
};
const launch = (prompt, type = 'general-purpose') => run('brief_gate.mjs', { session_id: 's1', hook_event_name: 'PreToolUse', tool_name: 'Agent', cwd: elsewhere, tool_input: { prompt, description: 'test', subagent_type: type } });
const good = mk('r1');

console.log('brief_gate');
for (const ty of ['Explore', 'Plan', 'claude-code-guide', 'statusline-setup']) { const r = launch('anything', ty); t(`exempt ${ty} allowed`, r.code === 0 && lastLog().reason === 'exempt type'); }
let r = launch('Just do it. No contract.');
t('no contract: denied (exit 2)', r.code === 2);
t('deny lists every missing item', /agent contract/.test(r.err) && /HANDIN_DIR/.test(r.err) && /Owner's words/.test(r.err), r.err);
t('deny is logged', lastLog().decision === 'deny' && lastLog().missing.length === 3);
r = run('brief_gate.mjs', { tool_input: { prompt: 'x' } }); t('missing subagent_type treated as general-purpose: denied', r.code === 2);
r = launch(good); t('make_brief output allowed', r.code === 0 && lastLog().reason === 'contract present', r.err);
r = launch(good.replace('AGENT-CONTRACT-MARKER: v1', '')); t('marker removed: denied', r.code === 2 && /agent contract/.test(r.err));
r = launch(good.replace(/^HANDIN_DIR:.*$/m, '')); t('HANDIN_DIR removed: denied', r.code === 2 && /HANDIN_DIR/.test(r.err));
r = launch(good.replace(/^HANDIN_DIR:.*$/m, 'HANDIN_DIR: docs/progress/x')); t('relative HANDIN_DIR: denied', r.code === 2 && /absolute/.test(r.err));
r = launch(good + '\nTime-box about 3 hours.'); t('"time-box" wording: denied', r.code === 2 && /time-box/.test(r.err));
r = launch(good + '\nThese are the whole task.'); t('"whole task" wording: denied', r.code === 2);
const emptyOwner = good.replace(/(## Owner's words[^\n]*\n)[\s\S]*?(\n## Finish line)/, '$1\n$2'); r = launch(emptyOwner); t('empty Owner\'s words: denied', r.code === 2 && /Owner's words/.test(r.err));
const noQuote = good.replace('"The photo is the target." (11 Oct)', 'The photo is the target said the owner'); r = launch(noQuote); t('owner words without quotation marks: denied', r.code === 2);
r = launch('Research the docs.\nNO-CONTRACT: read-only research, no hand-in expected'); t('NO-CONTRACT with reason: allowed + logged', r.code === 0 && lastLog().reason === 'NO-CONTRACT' && /research/.test(lastLog().no_contract_reason));
r = launch('Research.\nNO-CONTRACT: too short'); t('NO-CONTRACT with short reason: denied', r.code === 2);
const bf = path.join(tmp, 'brief_ok.txt'); fs.writeFileSync(bf, good);
r = launch(`Your brief is in the file.\nBRIEF-FILE: ${bf}`); t('BRIEF-FILE that passes: allowed', r.code === 0 && lastLog().reason === 'BRIEF-FILE passes', r.err);
const bf2 = path.join(tmp, 'brief_bad.txt'); fs.writeFileSync(bf2, 'no contract here');
r = launch(`Your brief is in the file.\nBRIEF-FILE: ${bf2}`); t('BRIEF-FILE that fails: denied', r.code === 2 && /BRIEF-FILE .* lacks/.test(r.err));
r = launch(`Your brief is in the file.\nBRIEF-FILE: ${path.join(tmp, 'nope.txt')}`); t('BRIEF-FILE unreadable: denied', r.code === 2 && /cannot be read/.test(r.err));
r = launch('Your brief is in file X'); t('prompt pointing at a file without BRIEF-FILE: denied', r.code === 2);
r = run('brief_gate.mjs', '{not json'); t('bad JSON: fail open (exit 0, logged)', r.code === 0 && /FAIL-OPEN/.test(lastLog().reason));
r = run('brief_gate.mjs', ''); t('empty stdin: fail open', r.code === 0 && /FAIL-OPEN/.test(lastLog().reason));
r = run('brief_gate.mjs', { tool_input: null }); t('tool_input null: no crash (denied or failed open)', r.code === 2 || r.code === 0);
r = run('brief_gate.mjs', { tool_input: { prompt: 12345, subagent_type: 'general-purpose' } }); t('non-string prompt: no crash', r.code === 2 || r.code === 0, r.err);

// ---- contract_inject
console.log('contract_inject');
r = run('contract_inject.mjs', { hook_event_name: 'SubagentStart', agent_id: 'a1', agent_type: 'general-purpose', cwd: elsewhere });
const o = J(r.out);
t('non-exempt: additionalContext holds the contract', r.code === 0 && o.hookSpecificOutput && o.hookSpecificOutput.hookEventName === 'SubagentStart' && o.hookSpecificOutput.additionalContext.includes('AGENT-CONTRACT-MARKER: v1'), r.out.slice(0, 200));
r = run('contract_inject.mjs', { hook_event_name: 'SubagentStart', agent_id: 'a2', agent_type: 'Explore' }); t('exempt type: no output', r.code === 0 && r.out === '');
r = run('contract_inject.mjs', { hook_event_name: 'SubagentStart', agent_id: 'a3', agent_type: '' }); t('empty agent_type (internal agent): no crash', r.code === 0);
r = run('contract_inject.mjs', 'garbage'); t('bad JSON: fail open', r.code === 0 && r.out === '');
fs.renameSync(path.join(root, 'docs/AGENT_CONTRACT.md'), path.join(root, 'docs/AGENT_CONTRACT.bak'));
r = run('contract_inject.mjs', { agent_id: 'a4', agent_type: 'general-purpose' }); t('contract file missing: fail open, logged', r.code === 0 && r.out === '' && /FAIL-OPEN/.test(lastLog().reason));
fs.renameSync(path.join(root, 'docs/AGENT_CONTRACT.bak'), path.join(root, 'docs/AGENT_CONTRACT.md'));

// ---- handin_gate
console.log('handin_gate');
const transcript = (name, firstText) => { const p = path.join(tmp, name + '.jsonl'); fs.writeFileSync(p, JSON.stringify({ type: 'user', isSidechain: true, message: { role: 'user', content: firstText } }) + '\n' + JSON.stringify({ type: 'assistant', message: { role: 'assistant', content: [{ type: 'text', text: 'done' }] } }) + '\n'); return p; };
const stop = (id, tp, extra = {}) => run('handin_gate.mjs', { hook_event_name: 'SubagentStop', session_id: 's1', cwd: elsewhere, agent_id: id, agent_type: 'general-purpose', agent_transcript_path: tp, stop_hook_active: false, last_assistant_message: 'done', ...extra });
const goodStatus = '## Skipped, refused or failed\n\nNothing skipped; did not verify the live editor.\n\nReferences opened: 3 of 3 listed\n\n## Reference files opened\n\nCase: A | Opened: aerial/a.jpg, street/b.jpg, gsv/c.jpg\n';
const hd = (round) => path.join(wt, round, 'docs', 'progress', round);

r = stop('n1', transcript('plain', 'Please research X. No hand-in.')); t('brief without HANDIN_DIR: allowed', r.code === 0 && r.out === '' && lastLog().reason === 'no HANDIN_DIR in brief');
const passTp = transcript('pass', mk('pass', ['--no-visual']));
r = stop('p1', passTp);
t('HANDIN_DIR set but folder empty (no-visual brief): blocked with list', r.code === 0 && J(r.out).decision === 'block' && /STATUS\.md is missing/.test(J(r.out).reason), r.out);
t('no-visual flag honoured: FAULTS/COMPARE not demanded', !/FAULTS\.md is missing/.test(J(r.out).reason || ''));
fs.mkdirSync(hd('pass'), { recursive: true }); fs.writeFileSync(path.join(hd('pass'), 'STATUS.md'), goodStatus);
r = stop('p1', passTp); t('after fixing: passes, stop allowed', r.code === 0 && r.out === '' && lastLog().reason === 'hand-in check passed', r.out + r.err);
const visTp = transcript('vis', mk('vis'));
fs.mkdirSync(hd('vis'), { recursive: true }); fs.writeFileSync(path.join(hd('vis'), 'STATUS.md'), goodStatus);
r = stop('v1', visTp); const vr = J(r.out).reason || '';
t('visual brief without FAULTS.md/COMPARE.md: blocked, both named', /FAULTS\.md is missing/.test(vr) && /COMPARE\.md is missing/.test(vr), vr);
const cdir = path.join(root, '.claude/state/handin');
t('counter after 1st block = 1', J(fs.readFileSync(path.join(cdir, 'v1.json'), 'utf8')).blocks === 1);
r = stop('v1', visTp); t('2nd block', J(r.out).decision === 'block' && /refusal 2 of 3/.test(J(r.out).reason));
r = stop('v1', visTp); t('3rd block', J(r.out).decision === 'block' && /refusal 3 of 3/.test(J(r.out).reason));
t('no HANDIN_FAILED.txt yet', !fs.existsSync(path.join(hd('vis'), 'HANDIN_FAILED.txt')));
r = stop('v1', visTp); t('4th attempt: stop allowed (empty stdout, exit 0)', r.code === 0 && r.out === '');
t('HANDIN_FAILED.txt written with the list', fs.existsSync(path.join(hd('vis'), 'HANDIN_FAILED.txt')) && /FAULTS\.md is missing/.test(fs.readFileSync(path.join(hd('vis'), 'HANDIN_FAILED.txt'), 'utf8')));
t('4th attempt logged with failures', lastLog().handin_failed_written === true && /FAULTS/.test(lastLog().failures));
r = stop('v2', visTp); t('counter is per agent_id: another agent starts at refusal 1', (J(r.out).reason || '').includes('refusal 1 of 3'));
const indBrief = path.join(tmp, 'ind_brief.txt'); fs.writeFileSync(indBrief, mk('ind', ['--no-visual']));
r = stop('i1', transcript('ind', `Your brief is in the file.\nBRIEF-FILE: ${indBrief}`)); t('HANDIN_DIR found via BRIEF-FILE: checked and blocked', J(r.out).decision === 'block' && /STATUS\.md is missing/.test(J(r.out).reason), r.out);
const sess = path.join(tmp, 'proj'); fs.mkdirSync(path.join(sess, 'sess1', 'subagents'), { recursive: true });
fs.copyFileSync(visTp, path.join(sess, 'sess1', 'subagents', 'agent-h1.jsonl'));
r = run('handin_gate.mjs', { hook_event_name: 'PreToolUse', tool_name: 'SubagentHandback', agent_id: 'h1', transcript_path: path.join(sess, 'sess1.jsonl'), tool_input: { message: 'report' } });
t('PreToolUse (SubagentHandback) variant: transcript found beside main transcript, denied with exit 2', r.code === 2 && /FAULTS\.md is missing/.test(r.err), r.err + r.out);
r = stop('f1', path.join(tmp, 'does-not-exist.jsonl')); t('transcript missing: fail open', r.code === 0 && r.out === '' && /FAIL-OPEN/.test(lastLog().reason));
r = stop('f2', transcript('empty', '')); t('empty brief text: allowed', r.code === 0 && r.out === '');
r = run('handin_gate.mjs', 'not json'); t('bad JSON: fail open', r.code === 0 && /FAIL-OPEN/.test(lastLog().reason));
r = run('handin_gate.mjs', { hook_event_name: 'SubagentStop', agent_type: '' }); t('no agent_id: allowed', r.code === 0 && r.out === '');
fs.renameSync(path.join(root, 'tools/check_handin.mjs'), path.join(root, 'tools/check_handin.bak'));
r = stop('c1', visTp); t('check_handin.mjs missing (node exits 1): fail open, NOT a block', r.code === 0 && r.out === '' && /FAIL-OPEN: check_handin/.test(lastLog().reason), r.out);
fs.renameSync(path.join(root, 'tools/check_handin.bak'), path.join(root, 'tools/check_handin.mjs'));

// ---- check_handin: references that are not under docs/reference (prototype screenshots, spec documents)
console.log('check_handin non-photo references');
{
  const shotsRoot = path.join(wt, 'shots');
  const shotDir = path.join(shotsRoot, 'docs', 'progress', '2026-10-11-prototype-ui');
  fs.mkdirSync(shotDir, { recursive: true });
  const names = Array.from({ length: 26 }, (_, i) => `s${String(i + 1).padStart(2, '0')}.png`);
  names.forEach((n) => fs.writeFileSync(path.join(shotDir, n), 'x'));
  const handin = path.join(shotsRoot, 'docs', 'progress', 'shots');
  fs.mkdirSync(handin, { recursive: true });
  const status = (files, opened) => `## Skipped, refused or failed\n\nNothing skipped; not verified in the live editor.\n\nReferences opened: ${opened} of ${opened} listed\n\n## Reference files opened\n\nCase: ui | Opened: ${files.join(', ')}\n`;
  const check = () => spawnSync(process.execPath, [path.join(root, 'tools/check_handin.mjs'), handin, '--main', root, '--no-visual'], { cwd: elsewhere, encoding: 'utf8' });
  fs.writeFileSync(path.join(handin, 'STATUS.md'), status(names.map((n) => 'docs/progress/2026-10-11-prototype-ui/' + n), 26));
  let c = check(); t('26 screenshot paths relative to the hand-in worktree: passes', c.status === 0, c.stderr);
  fs.writeFileSync(path.join(handin, 'STATUS.md'), status(names.map((n) => path.join(shotDir, n)), 26));
  c = check(); t('26 absolute screenshot paths: passes', c.status === 0, c.stderr);
  fs.writeFileSync(path.join(handin, 'STATUS.md'), status([...names.slice(0, 25), 'docs/progress/2026-10-11-prototype-ui/missing_shot.png'].map((n) => n.includes('/') || n.includes('\\') ? n : 'docs/progress/2026-10-11-prototype-ui/' + n), 26));
  c = check(); t('one non-existent path: fails and names the file', c.status === 1 && /missing_shot\.png/.test(c.stderr), c.stderr);
  fs.writeFileSync(path.join(handin, 'STATUS.md'), status(['docs/progress/2026-10-11-prototype-ui/s01.png', 'docs/progress/2026-10-11-prototype-ui/s02.png'], 2));
  c = check(); t('only 2 existing files in a case: still fails (needs 3)', c.status === 1 && /need at least 3/.test(c.stderr), c.stderr);
}

// ---- audit gate: builder and auditor are never the same agent
console.log('audit gate');
{
  const al = await import(pathToFileURL(path.join(src, 'tools/hooks/audit_lib.mjs')).href);
  const stateDir = path.join(root, '.claude/state');
  const auditText = (id, o = {}) => `Auditor agent: ${id}\nVerdict: ${o.verdict || 'accept'}\nReferences opened: ${o.n ?? 3} of ${o.m ?? 3} listed\n\n## Own fault list\n\nfrom the photos alone, before reading the builder files\n\n## Faults, ranked\n\n${o.faults ?? '1. The kerb line in render r3.png misses the photo by about one lane width.'}\n${o.extra || ''}`;
  const mkAudit = (folder, { owner, flags = [] } = {}) => {
    const tf = path.join(tmp, 'audit_task_' + path.basename(folder) + '.md');
    fs.writeFileSync(tf, `# T audit\n## Owner's words\n${owner ?? '"The photo is the target." (11 Oct)'}\n## Cases\ncase A | ${path.join(root, 'docs/reference/aerial/a.jpg')} ${path.join(root, 'docs/reference/street/b.jpg')} docs/reference/gsv/c.jpg\n`);
    return spawnSync(process.execPath, [path.join(root, 'tools/make_brief.mjs'), tf, '--audit', folder, ...flags], { cwd: elsewhere, encoding: 'utf8' });
  };
  const noticeCtx = (rt = root) => ptxtA(spawnSync(process.execPath, [path.join(rt, 'tools/hooks/gate_notice.mjs')], { input: JSON.stringify({ hook_event_name: 'UserPromptSubmit', session_id: 's9', prompt: 'hi', cwd: elsewhere }), cwd: elsewhere, encoding: 'utf8' }).stdout);
  function ptxtA(o) { return (J(o).hookSpecificOutput || {}).additionalContext || ''; }
  const folderP = hd('pass');

  // -- the auditor brief (make_brief --audit)
  let ab = mkAudit(folderP);
  t('make_brief --audit: exit 0', ab.status === 0, ab.stderr);
  t('audit brief carries the AUDIT-OF marker line with the absolute folder', new RegExp('^AUDIT-OF: ' + folderP.replace(/[\\^$.*+?()[\]{}|]/g, '\\$&') + '$', 'm').test(ab.stdout), ab.stdout.slice(0, 300));
  t('audit brief carries the contract, the owner words and the cases', /AGENT-CONTRACT-MARKER: v1/.test(ab.stdout) && /The photo is the target/.test(ab.stdout) && /case A \|/.test(ab.stdout));
  t('audit brief has no HANDIN_DIR (an auditor is not a builder) and no builder fault list', !/^HANDIN_DIR:/m.test(ab.stdout) && !/none known/.test(ab.stdout) && !/Known faults/.test(ab.stdout));
  t('audit brief counts the reference files for "N of M" (3 named)', /M = 3 distinct files/.test(ab.stdout), ab.stdout.match(/M = \d+/));
  t('audit brief orders the work: own fault list BEFORE the builder files; checks the measuring tools; names AUDIT.md first lines', /BEFORE opening the builder's FAULTS\.md/.test(ab.stdout) && /measuring tools/.test(ab.stdout) && /Auditor agent: <your agent id>/.test(ab.stdout) && /Verdict: accept \| send back/.test(ab.stdout) && /References opened: N of M/.test(ab.stdout));
  t('audit brief states builder and auditor are never the same agent, with the owner quote', /never the same agent/.test(ab.stdout) && /is one earthworks agent creating AND auditing/.test(ab.stdout));
  t('make_brief --audit needs only owner words and cases (no Faults/Launched/Waiting)', ab.status === 0);
  ab = mkAudit(folderP, { owner: 'no quotation marks here' }); t('make_brief --audit without a quoted owner words: refused (exit 1)', ab.status === 1 && /Owner's words/.test(ab.stderr) && ab.stdout === '');
  t('make_brief --audit logs an audit make_brief line (canary sees it)', fs.readFileSync(logPath, 'utf8').includes('"round":"audit:pass"'));
  const auditBrief = mkAudit(folderP).stdout;

  // -- launch gate
  r = launch(auditBrief); t('launch gate: make_brief --audit output allowed without NO-CONTRACT', r.code === 0 && lastLog().reason === 'AUDIT-OF' && lastLog().audit_of === folderP, r.err);
  r = launch(`Audit it.\nAUDIT-OF: ${folderP}`); t('launch gate: bare AUDIT-OF line allowed', r.code === 0 && lastLog().reason === 'AUDIT-OF');
  r = launch('Audit it.\nAUDIT-OF: docs/progress/x'); t('launch gate: relative AUDIT-OF denied', r.code === 2 && /absolute/.test(r.err) && lastLog().reason === 'AUDIT-OF refused');
  r = launch(auditBrief + `\nHANDIN_DIR: ${folderP}`); t('launch gate: AUDIT-OF together with HANDIN_DIR denied', r.code === 2 && /HANDIN_DIR/.test(r.err));
  r = launch(auditBrief + '\n## Pass 1\n- the kerb is wrong\n'); t('launch gate: pasted "## Pass N" heading denied', r.code === 2 && /fault list/.test(r.err) && /Pass/.test(r.err), r.err);
  r = launch(auditBrief + '\n## Residual\nnothing\n'); t('launch gate: pasted "## Residual" heading denied', r.code === 2);
  const L1 = 'The verge between the kerb and the footway is 0.9 m wide in the render but the photo shows roughly 2.5 m of grass.';
  const L2 = 'The give-way line sits 4 m behind the junction mouth while the photo shows it 1 m behind the kerb return.';
  fs.writeFileSync(path.join(folderP, 'FAULTS.md'), `# F\n\n## Pass 1\n\n${L1}\n${L2}\nshort\n`);
  r = launch(auditBrief + `\nBuilder said: ${L1}\n${L1}\n${L2}\n`); t('launch gate: two long lines of the builder FAULTS.md pasted: denied, names the file', r.code === 2 && /repeats 2 long lines/.test(r.err), r.err);
  r = launch(auditBrief + `\nOne thing only: ${L1}\n`); t('launch gate: a single long line is not a paste: allowed', r.code === 0, r.err);
  fs.rmSync(path.join(folderP, 'FAULTS.md'));
  const abf = path.join(tmp, 'audit_brief_file.txt'); fs.writeFileSync(abf, auditBrief);
  r = launch(`Your brief is in the file.\nBRIEF-FILE: ${abf}`); t('launch gate: AUDIT-OF inside a BRIEF-FILE allowed', r.code === 0 && lastLog().reason === 'AUDIT-OF', r.err);
  fs.writeFileSync(abf, auditBrief + '\n## Pass 2\nx\n'); r = launch(`BRIEF-FILE: ${abf}`); t('launch gate: BRIEF-FILE with a pasted fault list denied', r.code === 2);
  r = launch('Builder text only, and the words AUDIT-OF: appear mid-line here'); t('AUDIT-OF mid-line is not a marker: still denied as a contract-less brief', r.code === 2 && /agent contract/.test(r.err));

  // -- contract_inject gives the agent its id
  r = run('contract_inject.mjs', { hook_event_name: 'SubagentStart', agent_id: 'abc123', agent_type: 'general-purpose', cwd: elsewhere });
  t('SubagentStart context starts with YOUR AGENT ID and keeps the contract', J(r.out).hookSpecificOutput.additionalContext.startsWith('YOUR AGENT ID: abc123') && J(r.out).hookSpecificOutput.additionalContext.includes('AGENT-CONTRACT-MARKER: v1'), r.out.slice(0, 200));

  // -- the builder passed earlier (round "pass", agent p1): its id was recorded and the audit logged as owed
  const rec = al.readBuilder(folderP, stateDir);
  t('builder hand-in pass recorded the builder agent id per HANDIN_DIR', rec && rec.agent_id === 'p1' && rec.agent_type === 'general-purpose' && Date.parse(rec.passed_at) > 0, JSON.stringify(rec));
  const owedLines = fs.readFileSync(logPath, 'utf8').trim().split('\n').map(J).filter((e) => e.gate === 'audit' && e.decision === 'owed');
  t('exactly one "audit owed" log line for it (a repeat pass by the same agent logs nothing new)', owedLines.length === 1 && owedLines[0].handin_dir === folderP && owedLines[0].builder_id === 'p1', JSON.stringify(owedLines));
  r = stop('p1', passTp); t('builder passes again (Stop after Handback): allowed, no new owed line', r.code === 0 && fs.readFileSync(logPath, 'utf8').trim().split('\n').map(J).filter((e) => e.gate === 'audit' && e.decision === 'owed').length === 1);
  const cA = noticeCtx();
  t('notice lists the folder as AUDIT OWED with the builder id', /AUDIT OWED x1/.test(cA) && cA.includes(folderP) && /builder general-purpose\/p1/.test(cA) && /--audit/.test(cA), cA);
  t('notice says an ack cannot clear it', /an ack does nothing/.test(cA));
  t('the failed hand-in (vis) was never recorded as a builder: no audit owed for it', !al.readBuilder(hd('vis'), stateDir));

  // -- finish gate for the auditor
  const audTp = transcript('aud', auditBrief);
  const auditStop = (id, tp = audTp) => stop(id, tp);
  r = auditStop('a2'); t('auditor without AUDIT.md: blocked, says it is missing', J(r.out).decision === 'block' && /AUDIT GATE/.test(J(r.out).reason) && /AUDIT\.md is missing/.test(J(r.out).reason), r.out);
  t('the block names the auditor its own agent id', /your agent id is a2/.test(J(r.out).reason));
  const af = path.join(folderP, 'AUDIT.md');
  fs.writeFileSync(af, 'Verdict: accept\nReferences opened: 3 of 3\n\n## Faults\n\nlots of nice things to say about the kerb line here, none wrong\n');
  r = auditStop('a3'); t('AUDIT.md without "Auditor agent:" line: blocked', J(r.out).decision === 'block' && /Auditor agent:/.test(J(r.out).reason));
  fs.writeFileSync(af, 'Auditor agent: a4\nReferences opened: 3 of 3\n\n## Faults\n\nlots of nice things to say about the kerb line here, none wrong\n');
  r = auditStop('a4'); t('AUDIT.md without Verdict: blocked', J(r.out).decision === 'block' && /Verdict/.test(J(r.out).reason));
  fs.writeFileSync(af, 'Auditor agent: a5\nVerdict: maybe\nReferences opened: 3 of 3\n\n## Faults\n\nlots of nice things to say about the kerb line here, none wrong\n');
  r = auditStop('a5'); t('Verdict other than accept / send back: blocked', J(r.out).decision === 'block' && /Verdict/.test(J(r.out).reason));
  fs.writeFileSync(af, 'Auditor agent: a6\nVerdict: send back\n\n## Faults\n\nlots of nice things to say about the kerb line here, none wrong\n');
  r = auditStop('a6'); t('AUDIT.md without "References opened: N of M": blocked', J(r.out).decision === 'block' && /References opened/.test(J(r.out).reason));
  fs.writeFileSync(af, auditText('a7', { n: 2, m: 3 }));
  r = auditStop('a7'); t('References opened 2 of 3 without a "not opened" line: blocked', J(r.out).decision === 'block' && /not opened/.test(J(r.out).reason));
  fs.writeFileSync(af, auditText('a8', { n: 2, m: 3, extra: '\nnot opened: gsv/c.jpg (file could not be decoded)\n' }));
  r = auditStop('a8'); t('References opened 2 of 3 WITH a "not opened: file (reason)" line: allowed', r.code === 0 && r.out === '' && lastLog().reason === 'audit written', r.out);
  fs.writeFileSync(af, auditText('a9', { faults: 'ok' }));
  r = auditStop('a9'); t('faults section under 40 characters: blocked', J(r.out).decision === 'block' && /faults section/.test(J(r.out).reason));
  fs.writeFileSync(af, 'Auditor agent: a10\nVerdict: accept\nReferences opened: 3 of 3\n\nall good\n');
  r = auditStop('a10'); t('no faults section at all: blocked', J(r.out).decision === 'block' && /faults/.test(J(r.out).reason));
  fs.writeFileSync(af, auditText('<agent id>'));
  r = auditStop('a11'); t('the template placeholder as the id: blocked', J(r.out).decision === 'block' && /Auditor agent:/.test(J(r.out).reason));
  fs.writeFileSync(af, auditText('someone-else'));
  r = auditStop('a12'); t('AUDIT.md claims an id that is not the finishing agent: blocked ("is not you")', J(r.out).decision === 'block' && /is not you/.test(J(r.out).reason) && /a12/.test(J(r.out).reason), r.out);
  fs.writeFileSync(af, auditText('p1'));
  r = auditStop('p1'); t('auditor id equals the recorded builder id (same agent audits its own round): blocked', J(r.out).decision === 'block' && /never the same agent/.test(J(r.out).reason), r.out);
  fs.writeFileSync(af, auditText('b1'));
  r = auditStop('b1', transcript('aud_b1', auditBrief)); t('a different agent writing a valid AUDIT.md: allowed, logged with the builder id', r.code === 0 && r.out === '' && lastLog().reason === 'audit written' && lastLog().builder_id === 'p1', r.out + JSON.stringify(lastLog()));
  t('stop counter is per agent_id (b1 was never blocked)', !fs.existsSync(path.join(stateDir, 'handin', 'b1.json')));
  const bfAud = path.join(tmp, 'aud_bf.txt'); fs.writeFileSync(bfAud, auditBrief);
  fs.rmSync(af);
  r = auditStop('bf1', transcript('aud_bf', `Brief is in the file.\nBRIEF-FILE: ${bfAud}`)); t('auditor brief reached through BRIEF-FILE: also gated', J(r.out).decision === 'block' && /AUDIT GATE/.test(J(r.out).reason), r.out);
  // pre-tool variant
  fs.copyFileSync(audTp, path.join(sess, 'sess1', 'subagents', 'agent-h2.jsonl'));
  r = run('handin_gate.mjs', { hook_event_name: 'PreToolUse', tool_name: 'SubagentHandback', agent_id: 'h2', transcript_path: path.join(sess, 'sess1.jsonl'), tool_input: { message: 'report' } });
  t('auditor on SubagentHandback (PreToolUse): denied with exit 2 until AUDIT.md exists', r.code === 2 && /AUDIT GATE/.test(r.err), r.err + r.out);
  // 3 refusals then release
  for (let i = 1; i <= 3; i++) { r = auditStop('rel1'); t(`auditor refusal ${i} of 3`, J(r.out).decision === 'block' && new RegExp(`refusal ${i} of 3`).test(J(r.out).reason)); }
  r = auditStop('rel1'); t('4th attempt: released (exit 0, no output), AUDIT_FAILED.txt written', r.code === 0 && r.out === '' && fs.existsSync(path.join(folderP, 'AUDIT_FAILED.txt')) && /AUDIT\.md is missing/.test(fs.readFileSync(path.join(folderP, 'AUDIT_FAILED.txt'), 'utf8')));
  t('release is logged as an audit that stays owed, not as a released hand-in', lastLog().gate === 'audit' && /audit still failing after 3 blocks/.test(lastLog().reason) && lastLog().audit_failed_written === true);
  t('...and the folder is STILL listed as AUDIT OWED (the release does not clear it)', /AUDIT OWED x1/.test(noticeCtx()));
  fs.rmSync(path.join(folderP, 'AUDIT_FAILED.txt'));
  // no builder record: nothing to compare against
  const folderN = path.join(wt, 'norec', 'docs', 'progress', 'norec'); fs.mkdirSync(folderN, { recursive: true });
  fs.writeFileSync(path.join(folderN, 'AUDIT.md'), auditText('n1'));
  r = auditStop('n1', transcript('aud_norec', mkAudit(folderN).stdout)); t('no builder record for the folder: valid AUDIT.md by the finishing agent still allowed (id cannot be compared)', r.code === 0 && lastLog().reason === 'audit written' && lastLog().builder_id === null, r.out);

  // -- the owed notice is driven by the files, not by the log
  fs.writeFileSync(af, auditText('p1'));
  t('AUDIT.md written by the builder id does not clear the notice', /AUDIT OWED x1/.test(noticeCtx()) && /is the builder.s id/.test(noticeCtx()));
  fs.writeFileSync(af, 'Auditor agent: a1\nVerdict: accept\n');
  t('malformed AUDIT.md does not clear the notice', /AUDIT OWED x1/.test(noticeCtx()));
  fs.writeFileSync(af, auditText('b1'));
  const past = new Date(Date.now() - 3600 * 1000); fs.utimesSync(af, past, past);
  t('AUDIT.md older than the builder\'s passed hand-in does not clear it (stale audit)', /AUDIT OWED x1/.test(noticeCtx()) && /older than the builder/.test(noticeCtx()));
  const now2 = new Date(); fs.utimesSync(af, now2, now2);
  t('a valid AUDIT.md by a different agent clears the notice', noticeCtx() === '' || !/AUDIT OWED/.test(noticeCtx()));
  fs.rmSync(af);
  const mainCopy = path.join(root, 'docs', 'progress', 'pass'); fs.mkdirSync(mainCopy, { recursive: true });
  fs.writeFileSync(path.join(mainCopy, 'AUDIT.md'), auditText('m1'));
  t('a valid AUDIT.md in the same round folder of the main checkout (after merge) clears it too', !/AUDIT OWED/.test(noticeCtx()));
  fs.rmSync(path.join(root, 'docs', 'progress'), { recursive: true, force: true });
  t('removing the AUDIT.md brings the notice back', /AUDIT OWED x1/.test(noticeCtx()));
  // a fresh builder after "send back" owes a new audit
  fs.writeFileSync(af, auditText('b1')); fs.utimesSync(af, past, past);
  const nb = al.recordBuilder(folderP, 'p9', 'general-purpose', stateDir); t('a different builder passing the same folder replaces the record (changed)', nb.changed === true && al.readBuilder(folderP, stateDir).agent_id === 'p9');
  t('and the earlier audit no longer counts: owed again', /AUDIT OWED x1/.test(noticeCtx()) && /p9/.test(noticeCtx()));
  fs.writeFileSync(af, auditText('b2'));
  t('the new audit by b2 clears it', !/AUDIT OWED/.test(noticeCtx()));
  // a record whose folder is gone everywhere is dropped
  al.recordBuilder(path.join(wt, 'gone', 'docs', 'progress', 'gone'), 'g1', 'general-purpose', stateDir);
  t('a record whose progress folder no longer exists anywhere is not listed', !/AUDIT OWED/.test(noticeCtx()));
  // many owed at once: all listed, no cap
  for (let i = 0; i < 6; i++) { const d = path.join(wt, 'many' + i, 'docs', 'progress', 'many' + i); fs.mkdirSync(d, { recursive: true }); al.recordBuilder(d, 'mb' + i, 'general-purpose', stateDir); }
  const cm = noticeCtx(); t('six owed audits: all six listed (no cap on auditors needing to run)', /AUDIT OWED x6/.test(cm) && [0, 1, 2, 3, 4, 5].every((i) => cm.includes('many' + i)), cm);
  for (let i = 0; i < 6; i++) { const d = path.join(wt, 'many' + i, 'docs', 'progress', 'many' + i); fs.writeFileSync(path.join(d, 'AUDIT.md'), auditText('ma' + i)); }
  t('six valid audits clear all six', !/AUDIT OWED/.test(noticeCtx()));

  // -- ack cannot clear it (own temp checkout so the shared log keeps its counts)
  const rootA = path.join(tmp, 'mainAudit');
  ['tools/hooks/lib.mjs', 'tools/hooks/audit_lib.mjs', 'tools/hooks/notice_lib.mjs', 'tools/hooks/gate_notice.mjs', 'tools/hooks/gate_ack.mjs'].forEach((rel) => { const d = path.join(rootA, rel); fs.mkdirSync(path.dirname(d), { recursive: true }); fs.copyFileSync(path.join(src, rel), d); });
  fs.mkdirSync(path.join(rootA, '.git'), { recursive: true });
  const folderA = path.join(tmp, 'wtA', 'docs', 'progress', 'ackround'); fs.mkdirSync(folderA, { recursive: true });
  al.recordBuilder(folderA, 'x1', 'general-purpose', path.join(rootA, '.claude/state'));
  t('own checkout: owed shows up', /AUDIT OWED x1/.test(noticeCtx(rootA)));
  let ak = spawnSync(process.execPath, [path.join(rootA, 'tools/hooks/gate_ack.mjs'), 'I looked at it and decided to do nothing about it'], { cwd: elsewhere, encoding: 'utf8' });
  t('ack accepted but reports the audit as still open and counts 0 acknowledged', ak.status === 0 && /Acknowledged 0 item/.test(ak.stdout) && /Still open/.test(ak.stdout) && /AUDIT OWED x1/.test(ak.stdout), ak.stdout + ak.stderr);
  t('after the ack the audit is STILL on the notice, on every prompt', /AUDIT OWED x1/.test(noticeCtx(rootA)) && /AUDIT OWED x1/.test(noticeCtx(rootA)));
  t('the notice with only an owed audit does not offer gate_ack as the way out', !/gate_ack\.mjs/.test(noticeCtx(rootA)));
  fs.writeFileSync(path.join(folderA, 'AUDIT.md'), auditText('y1'));
  t('only the file clears it', noticeCtx(rootA) === '');
  // gate_notice with a broken builders dir fails open
  fs.rmSync(path.join(rootA, '.claude/state/builders'), { recursive: true }); fs.writeFileSync(path.join(rootA, '.claude/state/builders'), 'i am a file, not a directory');
  t('unreadable builders state: notice hook still exits 0 and stays silent', noticeCtx(rootA) === '');

  // -- fail-open of the recorder
  const foTp = transcript('fo', mk('fo', ['--no-visual']));
  fs.mkdirSync(hd('fo'), { recursive: true }); fs.writeFileSync(path.join(hd('fo'), 'STATUS.md'), goodStatus);
  fs.mkdirSync(path.join(stateDir, 'builders'), { recursive: true }); fs.rmSync(path.join(stateDir, 'builders'), { recursive: true }); fs.writeFileSync(path.join(stateDir, 'builders'), 'file in the way');
  r = stop('fo1', foTp); t('builder record cannot be written: the stop is still allowed (fail open), the failure is logged FAIL-OPEN', r.code === 0 && r.out === '' && fs.readFileSync(logPath, 'utf8').includes('could not record the builder'), r.out + r.err);
  fs.rmSync(path.join(stateDir, 'builders')); fs.mkdirSync(path.join(stateDir, 'builders'));
  // the pass folder stays covered by its recorded audit for the tests that follow
  fs.writeFileSync(af, auditText('b1'));
  al.recordBuilder(folderP, 'p1', 'general-purpose', stateDir);
  t('after the recorder test the "pass" round is restored and its AUDIT.md still clears the notice', !/AUDIT OWED/.test(noticeCtx()));

  // -- check_handin --accept
  const acc = (folder, extra = [], rt = root) => spawnSync(process.execPath, [path.join(rt, 'tools/check_handin.mjs'), folder, '--main', rt, '--no-visual', ...extra], { cwd: elsewhere, encoding: 'utf8' });
  fs.rmSync(af);
  let c = acc(folderP); t('check_handin without --accept: unchanged, passes with no AUDIT.md', c.status === 0 && /HAND-IN CHECK PASSED/.test(c.stdout), c.stderr);
  // the recorded builder was replaced by 'fo' test above? "pass" record is p1 (record dir was recreated empty) -> restore
  al.recordBuilder(folderP, 'p1', 'general-purpose', stateDir);
  c = acc(folderP, ['--accept']); t('--accept with no AUDIT.md: fails and says it is missing', c.status === 1 && /--accept: AUDIT\.md: AUDIT\.md is missing/.test(c.stderr), c.stderr);
  fs.writeFileSync(af, auditText('p1'));
  c = acc(folderP, ['--accept']); t('--accept with AUDIT.md written by the builder: fails, names "never the same agent"', c.status === 1 && /never the same agent/.test(c.stderr), c.stderr);
  fs.writeFileSync(af, 'Auditor agent: a1\nVerdict: accept\n');
  c = acc(folderP, ['--accept']); t('--accept with a malformed AUDIT.md: fails with the missing fields', c.status === 1 && /References opened/.test(c.stderr));
  fs.writeFileSync(af, auditText('q1'));
  c = acc(folderP, ['--accept']); t('--accept with a valid AUDIT.md by a different agent: passes', c.status === 0 && /HAND-IN CHECK PASSED/.test(c.stdout) && /independent of builder p1/.test(c.stdout), c.stderr + c.stdout);
  c = acc(folderP, ['--accept', '--builder', 'q1']); t('--accept --builder <the auditor id>: fails (override names the same agent)', c.status === 1 && /never the same agent/.test(c.stderr));
  al.recordBuilder(path.join(wt, 'orphan'), 'o1', 'general-purpose', path.join(tmp, 'nostate'));
  const orph = path.join(wt, 'orphan', 'docs', 'progress', 'orphan'); fs.mkdirSync(orph, { recursive: true }); fs.writeFileSync(path.join(orph, 'STATUS.md'), goodStatus); fs.writeFileSync(path.join(orph, 'AUDIT.md'), auditText('q2'));
  c = acc(orph, ['--accept']); t('--accept with no builder record and no --builder: fails (independence cannot be shown)', c.status === 1 && /no builder record/.test(c.stderr), c.stderr);
  c = acc(orph, ['--accept', '--builder', 'o1']); t('--accept --builder <id> with a valid AUDIT.md by another agent: passes', c.status === 0, c.stderr);
  c = acc(orph, ['--builder', 'o1']); t('--builder without --accept changes nothing', c.status === 0);
  const old2 = new Date(Date.now() - 3600 * 1000); fs.utimesSync(af, old2, old2);
  c = acc(folderP, ['--accept']); t('--accept with an AUDIT.md older than the builder hand-in: fails (stale)', c.status === 1 && /older than the builder/.test(c.stderr));
  fs.writeFileSync(af, auditText('q1'));
  t('--accept still runs the ordinary checks: a broken STATUS.md fails it', (() => { const s0 = fs.readFileSync(path.join(folderP, 'STATUS.md'), 'utf8'); fs.writeFileSync(path.join(folderP, 'STATUS.md'), 'nothing'); const x = acc(folderP, ['--accept']); fs.writeFileSync(path.join(folderP, 'STATUS.md'), s0); return x.status === 1 && /STATUS\.md/.test(x.stderr); })());
}

// ---- gate_notice / gate_ack / canary
console.log('gate_notice, gate_ack, canary');
{
  const nl = await import(pathToFileURL(path.join(src, 'tools/hooks/notice_lib.mjs')).href);
  const ptxt = (o) => (J(o).hookSpecificOutput || {}).additionalContext || '';
  const prompt = (rt = root) => spawnSync(process.execPath, [path.join(rt, 'tools/hooks/gate_notice.mjs')], { input: JSON.stringify({ hook_event_name: 'UserPromptSubmit', session_id: 's9', prompt: 'hi', cwd: elsewhere }), cwd: elsewhere, encoding: 'utf8' });
  const ack = (txt, rt = root) => spawnSync(process.execPath, [path.join(rt, 'tools/hooks/gate_ack.mjs'), ...(txt === null ? [] : [txt])], { cwd: elsewhere, encoding: 'utf8' });

  // The log of this run already holds real FAIL-OPEN lines, a NO-CONTRACT launch and a released hand-in from the tests above.
  let n = prompt();
  const ctx = ptxt(n.stdout);
  t('real log: notice emitted as UserPromptSubmit additionalContext', n.status === 0 && J(n.stdout).hookSpecificOutput.hookEventName === 'UserPromptSubmit' && ctx.startsWith('GATE NOTICE'), n.stdout.slice(0, 300));
  t('notice names FAIL-OPEN lines', /FAIL-OPEN x\d+/.test(ctx));
  t('notice names the released hand-in with its HANDIN_DIR', /HAND-IN RELEASED after 3 refusals x1/.test(ctx) && ctx.includes(hd('vis')), ctx);
  t('notice counts NO-CONTRACT launches with the reason', /NO-CONTRACT launches x1/.test(ctx) && /read-only research, no hand-in expected/.test(ctx));
  t('notice states how to clear it', ctx.includes('gate_ack.mjs') && /20 characters/.test(ctx));
  t('notice under the 10000 character cap', ctx.length < 6100);
  n = prompt(); t('repeats on the next prompt until acknowledged', ptxt(n.stdout).startsWith('GATE NOTICE'));
  t('every run logs a heartbeat line (gate notice)', lastLog().gate === 'notice' && lastLog().decision === 'notice');

  let a = ack(null); t('ack without text: refused, exit 1', a.status === 1 && /REFUSED/.test(a.stderr));
  a = ack('too short'); t('ack with short text: refused, exit 1', a.status === 1 && /at least 20/.test(a.stderr));
  a = ack('x'.repeat(19)); t('ack with 19 characters: refused', a.status === 1);
  a = ack('   ' + 'y'.repeat(10) + '   '); t('ack padded with spaces to 20+: refused (trimmed)', a.status === 1);
  n = prompt(); t('refused acks do not clear the notice', ptxt(n.stdout).startsWith('GATE NOTICE'));
  t('refused ack writes nothing to the log', !fs.readFileSync(logPath, 'utf8').includes('"gate":"ack"'));

  a = ack('read HANDIN_FAILED.txt, re-ran the check by hand, reasons judged harmless');
  t('ack with 20+ characters: accepted and counted', a.status === 0 && /Acknowledged \d+ item/.test(a.stdout), a.stderr + a.stdout);
  t('ack logged with text, timestamp, through_line', lastLog().gate === 'ack' && /re-ran the check/.test(lastLog().text) && !!lastLog().ts && lastLog().through_line > 0 && lastLog().cleared.handin_failed === 1);
  n = prompt(); t('after ack: no output at all', n.status === 0 && n.stdout === '');
  t('after ack: heartbeat still logged (decision none)', lastLog().gate === 'notice' && lastLog().decision === 'none');
  n = prompt(); t('still silent on the next prompt', n.stdout === '');

  // new events after the ack show up again, and only those
  launch('Research again.\nNO-CONTRACT: a second read-only research launch here');
  n = prompt(); const c2 = ptxt(n.stdout);
  t('a new NO-CONTRACT after the ack is reported alone', /x1/.test(c2) && /second read-only research/.test(c2) && !/HAND-IN RELEASED/.test(c2) && !/read-only research, no hand-in expected/.test(c2), c2);
  run('handin_gate.mjs', 'not json');
  n = prompt(); t('a new FAIL-OPEN after the ack is reported', /FAIL-OPEN x1/.test(ptxt(n.stdout)));
  ack('second ack: both new items were looked at and judged fine');
  t('ack clears both', prompt().stdout === '');
  t('acking with nothing pending is allowed', ack('acknowledged again with nothing pending at all').status === 0);

  // hand-in released repeatedly for the same HANDIN_DIR is one item
  const dupTp = transcript('dup', mk('dup'));
  fs.mkdirSync(hd('dup'), { recursive: true }); fs.writeFileSync(path.join(hd('dup'), 'STATUS.md'), goodStatus);
  for (let i = 0; i < 6; i++) stop('d1', dupTp);
  n = prompt(); t('same HANDIN_DIR released repeatedly: reported once', /HAND-IN RELEASED after 3 refusals x1/.test(ptxt(n.stdout)), ptxt(n.stdout));
  ack('dup hand-in looked at, FAULTS and COMPARE missing, accepted');

  // fail-open of the notice hook itself
  n = spawnSync(process.execPath, [path.join(root, 'tools/hooks/gate_notice.mjs')], { input: 'garbage', cwd: elsewhere, encoding: 'utf8' });
  t('notice hook with bad stdin: exit 0, no output, logged FAIL-OPEN', n.status === 0 && n.stdout === '' && lastLog().gate === 'notice' && /FAIL-OPEN/.test(lastLog().reason));
  n = prompt(); t('and that failure shows up in the next notice', /FAIL-OPEN x1/.test(ptxt(n.stdout)) && /gate=notice/.test(ptxt(n.stdout)));
  ack('notice hook failure was a test with bad stdin, nothing to fix');
  // a log that cannot be parsed in places must not break the notice
  fs.appendFileSync(logPath, 'this is not json\n{"gate":"brief","reason":"FAIL-OPEN: test line","ts":"2026-01-01T00:00:00Z"}\n');
  n = prompt(); t('garbage line in the log is skipped, valid FAIL-OPEN after it still reported', n.status === 0 && /FAIL-OPEN x1/.test(ptxt(n.stdout)));
  ack('garbage log line test, nothing to fix here at all');
  // unreadable state: gate.log missing entirely
  const rootN = path.join(tmp, 'mainN'); ['tools/hooks/lib.mjs', 'tools/hooks/audit_lib.mjs', 'tools/hooks/notice_lib.mjs', 'tools/hooks/gate_notice.mjs'].forEach((rel) => { const d = path.join(rootN, rel); fs.mkdirSync(path.dirname(d), { recursive: true }); fs.copyFileSync(path.join(src, rel), d); });
  n = prompt(rootN); t('no gate.log yet: exit 0, output empty', n.status === 0 && n.stdout === '');
  // state dir is a file: the log cannot be written; hook must still exit 0
  fs.rmSync(path.join(rootN, '.claude'), { recursive: true, force: true }); fs.writeFileSync(path.join(rootN, '.claude'), 'i am a file');
  n = prompt(rootN); t('state dir unusable: exit 0, silent', n.status === 0 && n.stdout === '');

  // ---- canary rules (pure function)
  const T0 = Date.parse('2026-10-11T12:00:00Z');
  const at = (sec, e) => ({ ts: new Date(T0 + sec * 1000).toISOString(), ...e });
  const cp1 = (es, nowSec) => nl.canaryProblems(es.map((e, i) => ({ n: i + 1, ...e })), T0 + nowSec * 1000);
  t('canary: empty log is fine', cp1([], 0).length === 0);
  t('canary: brief made 10 s ago, no launch yet: grace, fine', cp1([at(0, { gate: 'make_brief', round: 'a' }), at(5, { gate: 'notice', reason: 'ran' })], 10).length === 0);
  let pr = cp1([at(0, { gate: 'notice', reason: 'ran' }), at(10, { gate: 'make_brief', round: 'r7' })], 600);
  t('canary RULE 1: brief 10 min old and no launch hook line after it: tripped, names the round', pr.length === 1 && /RULE 1/.test(pr[0]) && /r7/.test(pr[0]), pr.join('|'));
  t('canary RULE 1: launch hook line (brief) after the brief: fine', cp1([at(10, { gate: 'make_brief', round: 'r7' }), at(20, { gate: 'brief', decision: 'allow' }), at(500, { gate: 'notice', reason: 'ran' })], 600).length === 0);
  t('canary RULE 1: inject line after the brief also counts', cp1([at(10, { gate: 'make_brief', round: 'r7' }), at(20, { gate: 'inject' }), at(500, { gate: 'notice', reason: 'ran' })], 600).length === 0);
  t('canary RULE 1: a launch line BEFORE the brief does not count', cp1([at(1, { gate: 'brief' }), at(10, { gate: 'make_brief', round: 'r7' }), at(500, { gate: 'notice', reason: 'ran' })], 600).length === 1);
  t('canary RULE 1: handin/notice lines do not count as launch hooks', cp1([at(10, { gate: 'make_brief', round: 'r7' }), at(20, { gate: 'handin' }), at(30, { gate: 'notice', reason: 'ran' })], 600).length === 1);
  pr = cp1([at(0, { gate: 'brief' }), at(100, { gate: 'notice', reason: 'ran' })], 100 + 4 * 3600);
  t('canary RULE 2: last notice line older than 3 h: tripped', pr.length === 1 && /RULE 2/.test(pr[0]) && /since/.test(pr[0]), pr.join('|'));
  t('canary RULE 2: notice 2 h ago: fine', cp1([at(0, { gate: 'brief' }), at(100, { gate: 'notice', reason: 'ran' })], 100 + 2 * 3600).length === 0);
  pr = cp1([at(0, { gate: 'brief' })], 4 * 3600);
  t('canary RULE 2: notice hook never logged but launch hooks old: tripped ("ever")', pr.length === 1 && /ever/.test(pr[0]));
  t('canary RULE 2: notice never logged and launch hooks young: fine', cp1([at(0, { gate: 'brief' })], 3600).length === 0);
  pr = cp1([at(0, { gate: 'notice', reason: 'ran' }), at(100, { gate: 'notice', reason: 'FAIL-OPEN: exception' })], 4 * 3600);
  t('canary RULE 2: FAIL-OPEN notice lines do not count as the hook running', pr.length === 1 && /RULE 2/.test(pr[0]));
  t('canary: both rules can trip together', cp1([at(0, { gate: 'brief' }), at(10, { gate: 'make_brief', round: 'z' })], 4 * 3600).length === 2);

  // ---- canary through the real tools
  const mkRoot = (name) => {
    const rt = path.join(tmp, name);
    ['tools/hooks/lib.mjs', 'tools/hooks/audit_lib.mjs', 'tools/hooks/notice_lib.mjs', 'tools/hooks/gate_notice.mjs', 'tools/hooks/gate_ack.mjs', 'tools/make_brief.mjs', 'tools/check_handin.mjs', 'docs/AGENT_CONTRACT.md', 'docs/templates/BRIEF.md', 'docs/templates/AUDIT_BRIEF.md']
      .forEach((rel) => { const d = path.join(rt, rel); fs.mkdirSync(path.dirname(d), { recursive: true }); fs.copyFileSync(path.join(src, rel), d); });
    fs.mkdirSync(path.join(rt, '.git'), { recursive: true });
    fs.mkdirSync(path.join(rt, '.claude/state'), { recursive: true });
    return rt;
  };
  const brief = (rt, round) => { const tf = path.join(tmp, round + '.md'); fs.writeFileSync(tf, task()); return spawnSync(process.execPath, [path.join(rt, 'tools/make_brief.mjs'), tf, '--round', round, '--worktree', path.join(wt, round)], { cwd: elsewhere, encoding: 'utf8' }); };
  const old = (minAgo, e) => JSON.stringify({ ts: new Date(Date.now() - minAgo * 60000).toISOString(), ...e }) + '\n';
  const rC = mkRoot('mainC');
  let b = brief(rC, 'c1'); t('make_brief on a clean checkout: exit 0, brief printed, no canary text', b.status === 0 && /AGENT-CONTRACT-MARKER/.test(b.stdout) && !/CANARY/.test(b.stderr), b.stderr);
  t('make_brief logs a make_brief line', fs.readFileSync(path.join(rC, '.claude/state/gate.log'), 'utf8').includes('"gate":"make_brief"'));
  fs.writeFileSync(path.join(rC, '.claude/state/gate.log'), old(30, { gate: 'make_brief', round: 'old' }) + old(1, { gate: 'notice', reason: 'ran' }));
  b = brief(rC, 'c2');
  t('make_brief: previous brief 30 min old with no launch hook line: exit 3, loud warning, brief still printed', b.status === 3 && /HOOK CANARY/.test(b.stderr) && /RULE 1/.test(b.stderr) && /AGENT-CONTRACT-MARKER/.test(b.stdout), b.stderr);
  b = brief(rC, 'c3'); t('make_brief again: still tripped until a launch hook line appears', b.status === 3);
  fs.appendFileSync(path.join(rC, '.claude/state/gate.log'), old(0, { gate: 'brief', decision: 'allow', reason: 'contract present' }) + old(0, { gate: 'notice', reason: 'ran' }));
  b = brief(rC, 'c4'); t('after a launch hook line the canary clears', b.status === 0 && !/CANARY/.test(b.stderr), b.stderr);
  fs.appendFileSync(path.join(rC, '.claude/state/gate.log'), old(0, { gate: 'brief', decision: 'allow', reason: 'FAIL-OPEN: stdin missing or not JSON' }));
  b = brief(rC, 'c5'); t('make_brief prints pending gate notices too (exit stays 0)', b.status === 0 && /GATE NOTICE/.test(b.stderr) && /FAIL-OPEN x1/.test(b.stderr), b.stderr);
  const rW = mkRoot('mainW'); fs.rmSync(path.join(rW, '.git'), { recursive: true }); fs.writeFileSync(path.join(rW, '.git'), 'gitdir: elsewhere');
  fs.writeFileSync(path.join(rW, '.claude/state/gate.log'), old(60, { gate: 'make_brief', round: 'x' }));
  b = brief(rW, 'w1'); t('worktree checkout (.git is a file): canary skipped and no log line written', b.status === 0 && !/CANARY/.test(b.stderr) && fs.readFileSync(path.join(rW, '.claude/state/gate.log'), 'utf8').trim().split('\n').length === 1, b.stderr);
  const aw = ack('worktree ack must be refused although the text is long enough', rW);
  t('gate_ack from a worktree copy (.git is a file): refused, exit 1, log untouched', aw.status === 1 && /worktree/.test(aw.stderr) && !fs.readFileSync(path.join(rW, '.claude/state/gate.log'), 'utf8').includes('"gate":"ack"'), aw.stderr);
  // check_handin prints the canary but keeps its own exit code
  const hdC = path.join(tmp, 'wtC', 'docs', 'progress', 'cc'); fs.mkdirSync(hdC, { recursive: true });
  fs.writeFileSync(path.join(rC, '.claude/state/gate.log'), old(30, { gate: 'make_brief', round: 'old' }) + old(1, { gate: 'notice', reason: 'ran' }));
  let ch = spawnSync(process.execPath, [path.join(rC, 'tools/check_handin.mjs'), hdC, '--main', rC, '--no-visual'], { cwd: elsewhere, encoding: 'utf8' });
  t('check_handin: loud canary warning on stderr, exit code still 1 for the missing STATUS.md', ch.status === 1 && /HOOK CANARY/.test(ch.stderr) && /HAND-IN CHECK FAILED/.test(ch.stderr), ch.stderr);
  ch = spawnSync(process.execPath, [path.join(rC, 'tools/check_handin.mjs'), hdC, '--main', rC, '--no-visual'], { cwd: elsewhere, encoding: 'utf8', env: { ...process.env, CP_FROM_HOOK: '1' } });
  t('check_handin called by the hand-in gate (CP_FROM_HOOK): no canary text', ch.status === 1 && !/CANARY/.test(ch.stderr));
  fs.writeFileSync(path.join(rC, '.claude/state/gate.log'), old(0, { gate: 'brief', decision: 'allow', reason: 'FAIL-OPEN: x' }));
  ch = spawnSync(process.execPath, [path.join(rC, 'tools/check_handin.mjs'), hdC, '--main', rC, '--no-visual'], { cwd: elsewhere, encoding: 'utf8' });
  t('check_handin does not show pending gate notices to the agent (canary only)', !/GATE NOTICE/.test(ch.stderr) && !/CANARY/.test(ch.stderr));
  fs.writeFileSync(path.join(rC, '.claude/state/gate.log'), '');
  ch = spawnSync(process.execPath, [path.join(rC, 'tools/check_handin.mjs'), hdC, '--main', rC, '--no-visual'], { cwd: elsewhere, encoding: 'utf8' });
  t('check_handin with an empty gate.log: no canary text', !/CANARY/.test(ch.stderr));
  // settings wiring
  const sj = JSON.parse(fs.readFileSync(path.join(src, '.claude/settings.json'), 'utf8'));
  t('settings.json wires UserPromptSubmit to gate_notice.mjs', JSON.stringify(sj.hooks.UserPromptSubmit || []).includes('tools/hooks/gate_notice.mjs'));
}

console.log(`\n${pass} passed, ${fail} failed (temp dir ${tmp})`);
if (!process.env.KEEP_HOOKTEST) fs.rmSync(tmp, { recursive: true, force: true });
process.exit(fail ? 1 : 0);
