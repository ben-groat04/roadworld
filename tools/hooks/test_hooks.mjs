#!/usr/bin/env node
// Unit tests for the three hooks with synthetic stdin JSON. Usage: node tools/hooks/test_hooks.mjs
// Copies the hook files, check_handin.mjs, make_brief.mjs, the contract and templates into a temp "main checkout" and
// runs every script as a child process from a different cwd, as the harness would. Exit 1 if any test fails.
import fs from 'node:fs';
import crypto from 'node:crypto';
import os from 'node:os';
import path from 'node:path';
import { spawnSync } from 'node:child_process';
import { fileURLToPath, pathToFileURL } from 'node:url';

const src = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..', '..');
const tmp = fs.mkdtempSync(path.join(os.tmpdir(), 'hooktest-'));
const root = path.join(tmp, 'main');
const wt = path.join(tmp, 'wt');
const cp = (rel) => { const d = path.join(root, rel); fs.mkdirSync(path.dirname(d), { recursive: true }); fs.copyFileSync(path.join(src, rel), d); };
['tools/hooks/guard.mjs', 'tools/hooks/brief_gate_impl.mjs', 'tools/hooks/handin_gate_impl.mjs', 'tools/hooks/gate_notice_impl.mjs', 'tools/hooks/contract_inject_impl.mjs', 'tools/check_wrappers.mjs', 'tools/hooks/lib.mjs', 'tools/hooks/audit_lib.mjs', 'tools/hooks/brief_gate.mjs', 'tools/hooks/handin_gate.mjs', 'tools/hooks/contract_inject.mjs', 'tools/hooks/notice_lib.mjs', 'tools/hooks/gate_notice.mjs', 'tools/hooks/gate_ack.mjs',
  'tools/check_handin.mjs', 'tools/make_brief.mjs', 'tools/gates_lib.mjs', 'docs/AGENT_CONTRACT.md', 'docs/templates/BRIEF.md', 'docs/templates/AUDIT_BRIEF.md'].forEach(cp);
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
let pass = 0, fail = 0; const failedNames = [];
const t = (name, cond, detail = '') => { if (cond) { pass++; console.log('  ok   ' + name); } else { fail++; failedNames.push(name); console.log('  FAIL ' + name + (detail ? '\n       ' + String(detail).replace(/\n/g, '\n       ') : '')); } };
const J = (s) => { try { return JSON.parse(s); } catch { return {}; } };

// ---- briefs
const task = (owner = '"The photo is the target." (11 Oct)') => `# T\n## Owner's words\n${owner}\n## Cases\ncase A | a.jpg b.jpg c.jpg\n## Faults\nnone known\n## Launched\nx\n## Waiting\nnone\n`;
const mk = (round, flags = []) => {
  const tf = path.join(tmp, round + '.md'); fs.writeFileSync(tf, task());
  const r = spawnSync(process.execPath, [path.join(root, 'tools/make_brief.mjs'), tf, '--round', round, '--worktree', path.join(wt, round), ...flags.filter((f) => f !== '--v2')], { encoding: 'utf8' });
  if (r.status !== 0) throw new Error('make_brief failed: ' + r.stderr);
  // The older tests below describe briefs written BEFORE gates v2 (no GATES-VERSION line): the measured gates must not run for them.
  return flags.includes('--v2') ? r.stdout : r.stdout.split('\n').filter((l) => !/^GATES-VERSION:|^GATE:/.test(l)).join('\n');
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
  // a builder record at the fixed time B0 (clock-free tests), signed like the finish gate's
  const B0 = Date.UTC(2026, 0, 1, 12, 0, 0);
  const legacyRec = (d, id, ty, sd, at = B0) => al.recordBuilder(d, id, ty, sd, at);
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
  legacyRec(folderP, 'p1', 'general-purpose', stateDir);
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
  r = auditStop('n1', transcript('aud_norec', mkAudit(folderN).stdout)); t('no builder record for the folder: the auditor is REFUSED (fail closed; independence cannot be shown)', J(r.out).decision === 'block' && /no record of who built/.test(J(r.out).reason), r.out);

  // -- the owed notice is driven by the signed records and the files, never by the log or a typed file. CLOCK-FREE: every time below is explicit
  // (builder passed at B, files given explicit mtimes, audit records given explicit recorded_at); nothing here reads the wall clock.
  const B = Date.UTC(2026, 0, 1, 12, 0, 0);
  const setM = (f, ms) => fs.utimesSync(f, new Date(ms), new Date(ms));
  const signAudit = (folder, auditor, builder, text, at = B + 120000) => al.recordAudit(folder, { auditorId: auditor, builderId: builder, verdict: 'accept', text }, stateDir, at);
  legacyRec(folderP, 'p1', 'general-purpose', stateDir);   // re-record the builder at the fixed time B
  const recFile = path.join(stateDir, 'audits', al.builderKey(folderP) + '.json');
  fs.rmSync(recFile, { force: true });   // the signed record the real finish gate wrote for auditor b1 above: gone, so the typed-file tests start clean
  fs.writeFileSync(af, auditText('p1')); setM(af, B + 60000);
  t('AUDIT.md written by the builder id does not clear the notice', /AUDIT OWED x1/.test(noticeCtx()) && /is the builder.s id/.test(noticeCtx()));
  fs.writeFileSync(af, 'Auditor agent: a1\nVerdict: accept\n'); setM(af, B + 60000);
  t('malformed AUDIT.md does not clear the notice', /AUDIT OWED x1/.test(noticeCtx()));
  fs.writeFileSync(af, auditText('b1')); setM(af, B - 3600 * 1000);
  t('AUDIT.md older than the builder\'s passed hand-in does not clear it (stale audit)', /AUDIT OWED x1/.test(noticeCtx()) && /older than the builder/.test(noticeCtx()));
  setM(af, B + 60000);
  t('a TYPED valid-looking AUDIT.md by a different agent id does NOT clear the notice (no signed record: a made-up id is worthless)', /AUDIT OWED x1/.test(noticeCtx()) && /no audit record/.test(noticeCtx()), noticeCtx());
  signAudit(folderP, 'b1', 'p1', auditText('b1'));
  t('the same file WITH the finish gate\'s signed record clears the notice', !/AUDIT OWED/.test(noticeCtx()), noticeCtx());
  // a hand-typed record (valid JSON, right fields, no or wrong signature) is worth nothing
  const good = fs.readFileSync(recFile, 'utf8');
  const forged = JSON.parse(good); delete forged.sig; fs.writeFileSync(recFile, JSON.stringify(forged));
  t('an audit record without its signature is ignored: owed again', /AUDIT OWED x1/.test(noticeCtx()), noticeCtx());
  const forged2 = JSON.parse(good); forged2.auditor_id = 'someone-new'; fs.writeFileSync(recFile, JSON.stringify(forged2));
  t('an audit record edited after signing (auditor id changed) is ignored: owed again', /AUDIT OWED x1/.test(noticeCtx()));
  fs.writeFileSync(recFile, good);
  t('the genuine signed record again clears it', !/AUDIT OWED/.test(noticeCtx()));
  fs.rmSync(af); fs.rmSync(recFile);
  const mainCopy = path.join(root, 'docs', 'progress', 'pass'); fs.mkdirSync(mainCopy, { recursive: true });
  fs.writeFileSync(path.join(mainCopy, 'AUDIT.md'), auditText('m1')); setM(path.join(mainCopy, 'AUDIT.md'), B + 60000);
  signAudit(folderP, 'm1', 'p1', auditText('m1'));
  t('a valid AUDIT.md in the same round folder of the main checkout (after merge) with its record clears it too', !/AUDIT OWED/.test(noticeCtx()), noticeCtx());
  fs.rmSync(path.join(root, 'docs', 'progress'), { recursive: true, force: true }); fs.rmSync(recFile);
  t('removing the AUDIT.md brings the notice back', /AUDIT OWED x1/.test(noticeCtx()));
  // a fresh builder after "send back" owes a new audit
  fs.writeFileSync(af, auditText('b1')); setM(af, B + 60000); signAudit(folderP, 'b1', 'p1', auditText('b1'));
  const nb = al.recordBuilder(folderP, 'p9', 'general-purpose', stateDir, B + 600000); t('a different builder passing the same folder replaces the record (changed)', nb.changed === true && al.readBuilder(folderP, stateDir).agent_id === 'p9');
  t('and the earlier audit no longer counts: owed again', /AUDIT OWED x1/.test(noticeCtx()) && /p9/.test(noticeCtx()));
  const same = al.recordBuilder(folderP, 'p9', 'general-purpose', stateDir, B + 700000);
  t('the same builder passing again before any audit: not a new hand-in (changed false)', same.changed === false);
  fs.writeFileSync(af, auditText('b2')); setM(af, B + 800000); signAudit(folderP, 'b2', 'p9', auditText('b2'), B + 900000);
  t('the new audit by b2 (record after the new pass) clears it', !/AUDIT OWED/.test(noticeCtx()));
  const resumed = al.recordBuilder(folderP, 'p9', 'general-purpose', stateDir, B + 1000000);
  t('the SAME builder id resumed after "send back" and passing again invalidates the old audit (changed true, owed again)', resumed.changed === true && /AUDIT OWED x1/.test(noticeCtx()));
  fs.writeFileSync(af, auditText('b3')); setM(af, B + 1100000); signAudit(folderP, 'b3', 'p9', auditText('b3'), B + 1200000);
  t('and a fresh audit clears it again', !/AUDIT OWED/.test(noticeCtx()));
  // a record whose folder is gone everywhere is dropped
  legacyRec(path.join(wt, 'gone', 'docs', 'progress', 'gone'), 'g1', 'general-purpose', stateDir);
  t('a record whose progress folder no longer exists anywhere is not listed', !/AUDIT OWED/.test(noticeCtx()));
  // an unsigned builder record (hand-typed) is not a record at all
  const ufolder = path.join(wt, 'unsigned', 'docs', 'progress', 'unsigned'); fs.mkdirSync(ufolder, { recursive: true });
  fs.mkdirSync(path.join(stateDir, 'builders'), { recursive: true });
  fs.writeFileSync(path.join(stateDir, 'builders', al.builderKey(ufolder) + '.json'), JSON.stringify({ v: 3, handin_dir: ufolder, agent_id: 'u1', agent_type: 'x', passed_at: new Date(B).toISOString() }));
  t('a hand-typed (unsigned) builder record is ignored: readBuilder null, nothing owed for it', al.readBuilder(ufolder, stateDir) === null && !/unsigned/.test(noticeCtx()));
  // many owed at once: all listed, no cap
  for (let i = 0; i < 6; i++) { const d = path.join(wt, 'many' + i, 'docs', 'progress', 'many' + i); fs.mkdirSync(d, { recursive: true }); legacyRec(d, 'mb' + i, 'general-purpose', stateDir); }
  const cm = noticeCtx(); t('six owed audits: all six listed (no cap on auditors needing to run)', /AUDIT OWED x6/.test(cm) && [0, 1, 2, 3, 4, 5].every((i) => cm.includes('many' + i)), cm);
  for (let i = 0; i < 6; i++) { const d = path.join(wt, 'many' + i, 'docs', 'progress', 'many' + i); fs.writeFileSync(path.join(d, 'AUDIT.md'), auditText('ma' + i)); setM(path.join(d, 'AUDIT.md'), B + 60000); signAudit(d, 'ma' + i, 'mb' + i, auditText('ma' + i)); }
  t('six valid audits (with signed records) clear all six', !/AUDIT OWED/.test(noticeCtx()));

  // -- ack cannot clear it (own temp checkout so the shared log keeps its counts)
  const rootA = path.join(tmp, 'mainAudit');
  ['tools/hooks/lib.mjs', 'tools/hooks/audit_lib.mjs', 'tools/hooks/notice_lib.mjs', 'tools/hooks/gate_notice.mjs', 'tools/hooks/gate_notice_impl.mjs', 'tools/hooks/guard.mjs', 'tools/hooks/gate_ack.mjs'].forEach((rel) => { const d = path.join(rootA, rel); fs.mkdirSync(path.dirname(d), { recursive: true }); fs.copyFileSync(path.join(src, rel), d); });
  fs.mkdirSync(path.join(rootA, '.git'), { recursive: true });
  const folderA = path.join(tmp, 'wtA', 'docs', 'progress', 'ackround'); fs.mkdirSync(folderA, { recursive: true });
  fs.mkdirSync(path.join(rootA, '.claude/state'), { recursive: true }); fs.writeFileSync(path.join(rootA, '.claude/state/ack_secret'), 'abcdef0123456789abcdef');
  legacyRec(folderA, 'x1', 'general-purpose', path.join(rootA, '.claude/state'));
  t('own checkout: owed shows up', /AUDIT OWED x1/.test(noticeCtx(rootA)));
  let ak = spawnSync(process.execPath, [path.join(rootA, 'tools/hooks/gate_ack.mjs'), '1:abcdef', 'I looked at it and decided to do nothing about it'], { cwd: elsewhere, encoding: 'utf8' });
  t('ack cannot name an audit: there is no log item to acknowledge, the ack is refused and nothing is logged', ak.status === 1 && /not a pending/.test(ak.stderr) && !fs.readFileSync(path.join(rootA, '.claude/state/gate.log'), 'utf8').includes('"gate":"ack"'), ak.stdout + ak.stderr);
  t('after the ack the audit is STILL on the notice, on every prompt', /AUDIT OWED x1/.test(noticeCtx(rootA)) && /AUDIT OWED x1/.test(noticeCtx(rootA)));
  t('the notice with only an owed audit does not offer gate_ack as the way out', !/gate_ack\.mjs/.test(noticeCtx(rootA)));
  fs.writeFileSync(path.join(folderA, 'AUDIT.md'), auditText('y1')); fs.utimesSync(path.join(folderA, 'AUDIT.md'), new Date(B0 + 60000), new Date(B0 + 60000));
  al.recordAudit(folderA, { auditorId: 'y1', builderId: 'x1', verdict: 'accept', text: auditText('y1') }, path.join(rootA, '.claude/state'), B0 + 120000);
  t('only the file WITH its signed record clears it', noticeCtx(rootA) === '');
  // gate_notice with a broken builders dir fails open
  fs.rmSync(path.join(rootA, '.claude/state/builders'), { recursive: true }); fs.writeFileSync(path.join(rootA, '.claude/state/builders'), 'i am a file, not a directory');
  t('unreadable builders state: notice hook still exits 0 and stays silent', noticeCtx(rootA) === '');

  // -- fail-open of the recorder
  const foTp = transcript('fo', mk('fo', ['--no-visual']));
  fs.mkdirSync(hd('fo'), { recursive: true }); fs.writeFileSync(path.join(hd('fo'), 'STATUS.md'), goodStatus);
  fs.mkdirSync(path.join(stateDir, 'builders'), { recursive: true }); fs.rmSync(path.join(stateDir, 'builders'), { recursive: true }); fs.writeFileSync(path.join(stateDir, 'builders'), 'file in the way');
  r = stop('fo1', foTp); t('builder record cannot be written: the stop is still allowed (fail open), the failure is logged FAIL-OPEN', r.code === 0 && r.out === '' && fs.readFileSync(logPath, 'utf8').includes('could not record the builder'), r.out + r.err);
  fs.rmSync(path.join(stateDir, 'builders')); fs.mkdirSync(path.join(stateDir, 'builders'));
  // the pass round stays covered by a signed audit record for the tests that follow (explicit times: B0 builder, B0+60s file, B0+120s record)
  fs.writeFileSync(af, auditText('b1')); fs.utimesSync(af, new Date(B0 + 60000), new Date(B0 + 60000));
  legacyRec(folderP, 'p1', 'general-purpose', stateDir);
  al.recordAudit(folderP, { auditorId: 'b1', builderId: 'p1', verdict: 'accept', text: auditText('b1') }, stateDir, B0 + 120000);
  t('after the recorder test the "pass" round is restored and its AUDIT.md (with its signed record) still clears the notice', !/AUDIT OWED/.test(noticeCtx()));

  // -- check_handin --accept. Every path needs the signed record; --builder only names the builder; --legacy "<reason>" is the logged escape for pre-audit-gate folders.
  const acc = (folder, extra = [], rt = root) => spawnSync(process.execPath, [path.join(rt, 'tools/check_handin.mjs'), folder, '--main', rt, '--no-visual', ...extra], { cwd: elsewhere, encoding: 'utf8' });
  const fileAt = (f, text, ms) => { fs.writeFileSync(f, text); fs.utimesSync(f, new Date(ms), new Date(ms)); };
  const audRec = (folder, auditor, builder, text, verdict = 'accept') => al.recordAudit(folder, { auditorId: auditor, builderId: builder, verdict, text }, stateDir, B0 + 120000);
  fs.rmSync(af);
  let c = acc(folderP); t('check_handin without --accept: unchanged, passes with no AUDIT.md', c.status === 0 && /HAND-IN CHECK PASSED/.test(c.stdout), c.stderr);
  legacyRec(folderP, 'p1', 'general-purpose', stateDir);
  fs.rmSync(path.join(stateDir, 'audits', al.builderKey(folderP) + '.json'), { force: true });
  c = acc(folderP, ['--accept']); t('--accept with no AUDIT.md: fails and says it is missing', c.status === 1 && /--accept: AUDIT\.md: AUDIT\.md is missing/.test(c.stderr), c.stderr);
  fileAt(af, auditText('p1'), B0 + 60000);
  c = acc(folderP, ['--accept']); t('--accept with AUDIT.md written by the builder: fails, names "never the same agent"', c.status === 1 && /never the same agent/.test(c.stderr), c.stderr);
  fileAt(af, 'Auditor agent: a1\nVerdict: accept\n', B0 + 60000);
  c = acc(folderP, ['--accept']); t('--accept with a malformed AUDIT.md: fails with the missing fields', c.status === 1 && /References opened/.test(c.stderr));
  fileAt(af, auditText('q1'), B0 + 60000);
  c = acc(folderP, ['--accept']); t('--accept with a TYPED AUDIT.md by a made-up auditor id (q1) and no signed record: REFUSED ("no audit record")', c.status === 1 && /no audit record/.test(c.stderr), c.stderr + c.stdout);
  audRec(folderP, 'q1', 'p1', auditText('q1'));
  c = acc(folderP, ['--accept']); t('--accept with the same file AND the finish gate\'s signed record: passes', c.status === 0 && /HAND-IN CHECK PASSED/.test(c.stdout) && /independent of builder p1/.test(c.stdout) && /signed audit record verified/.test(c.stdout), c.stderr + c.stdout);
  c = acc(folderP, ['--accept', '--builder', 'q1']); t('--accept --builder <the auditor id>: fails (the override names a different builder than the record)', c.status === 1 && /not the recorded builder/.test(c.stderr), c.stderr);
  c = acc(folderP, ['--accept', '--builder', 'whoever']); t('--accept --builder whoever does NOT remove the check (gates2 audit fault 1): refused', c.status === 1 && /not the recorded builder/.test(c.stderr), c.stderr);
  audRec(folderP, 'q1', 'p1', auditText('q1', { verdict: 'send back' }), 'send back');
  fileAt(af, auditText('q1', { verdict: 'send back' }), B0 + 60000);
  c = acc(folderP, ['--accept']); t('--accept with Verdict: send back (even with a record): refused, a send-back audit never accepts a round', c.status === 1 && /send-back audit never accepts/.test(c.stderr), c.stderr);
  fileAt(af, auditText('q1'), B0 + 60000); audRec(folderP, 'q1', 'p1', auditText('q1'));
  // a folder with no builder record anywhere
  const orph = path.join(wt, 'orphan', 'docs', 'progress', 'orphan'); fs.mkdirSync(orph, { recursive: true }); fs.writeFileSync(path.join(orph, 'STATUS.md'), goodStatus); fileAt(path.join(orph, 'AUDIT.md'), auditText('q2'), B0 + 60000);
  c = acc(orph, ['--accept']); t('--accept with no builder record and no --builder: fails (independence cannot be shown)', c.status === 1 && /no builder record/.test(c.stderr), c.stderr);
  c = acc(orph, ['--accept', '--builder', 'o1']); t('--accept --builder o1, NO builder record and a typed AUDIT.md with a fake id: REFUSED (the gates2 audit\'s reproduced escape)', c.status === 1 && /no audit record/.test(c.stderr), c.stderr + c.stdout);
  audRec(orph, 'q2', 'o1', auditText('q2'));
  c = acc(orph, ['--accept', '--builder', 'o1']); t('--accept --builder o1 WITH a signed audit record of builder o1: passes', c.status === 0, c.stderr);
  c = acc(orph, ['--accept', '--builder', 'someone-else']); t('...but a record made for builder o1 does not pass for --builder someone-else', c.status === 1 && /not someone-else|record is of builder o1/.test(c.stderr), c.stderr);
  const orph2 = path.join(wt, 'orphan2', 'docs', 'progress', 'orphan2'); fs.mkdirSync(orph2, { recursive: true }); fs.writeFileSync(path.join(orph2, 'STATUS.md'), goodStatus); fileAt(path.join(orph2, 'AUDIT.md'), auditText('q3'), B0 + 60000);
  c = acc(orph2, ['--accept', '--builder', 'o2', '--legacy', 'x']); t('--legacy with a short reason: refused', c.status === 1 && /--legacy needs a reason/.test(c.stderr), c.stderr);
  c = acc(orph2, ['--accept', '--builder', 'o2', '--legacy', 'folder handed in before the audit gate existed, audited by hand']); t('--legacy "<reason>" lets a pre-audit-gate folder through (file independent, verdict accept) and says so', c.status === 0 && /LEGACY accept/.test(c.stdout), c.stderr + c.stdout);
  t('...and the legacy accept is logged to gate.log', fs.readFileSync(logPath, 'utf8').includes('"decision":"legacy"') && fs.readFileSync(logPath, 'utf8').includes('audited by hand'));
  c = acc(orph, ['--builder', 'o1']); t('--builder without --accept changes nothing', c.status === 0);
  // the gate.log recovery path (builder recovered from the log, no builder record): still needs the signed record
  const rec3 = path.join(wt, 'logrec', 'docs', 'progress', 'logrec'); fs.mkdirSync(rec3, { recursive: true }); fs.writeFileSync(path.join(rec3, 'STATUS.md'), goodStatus); fileAt(path.join(rec3, 'AUDIT.md'), auditText('zzfake'), B0 + 60000);
  fs.appendFileSync(logPath, JSON.stringify({ ts: new Date(B0).toISOString(), gate: 'handin', reason: 'hand-in check passed', agent_id: 'lb1', agent_type: 'general-purpose', handin_dir: rec3 }) + '\n');
  c = acc(rec3, ['--accept']); t('--accept with the builder recovered from gate.log and a typed fake-id AUDIT.md (gates2 audit reproduction): REFUSED', c.status === 1 && /no audit record/.test(c.stderr), c.stderr + c.stdout);
  audRec(rec3, 'zzfake', 'lb1', auditText('zzfake'));
  c = acc(rec3, ['--accept']); t('...and passes once the signed record exists', c.status === 0, c.stderr);
  fileAt(af, auditText('q1'), B0 - 3600 * 1000);
  c = acc(folderP, ['--accept']); t('--accept with an AUDIT.md older than the builder hand-in: fails (stale)', c.status === 1 && /older than the builder/.test(c.stderr));
  fileAt(af, auditText('q1'), B0 + 60000);
  t('--accept still runs the ordinary checks: a broken STATUS.md fails it', (() => { const s0 = fs.readFileSync(path.join(folderP, 'STATUS.md'), 'utf8'); fs.writeFileSync(path.join(folderP, 'STATUS.md'), 'nothing'); const x = acc(folderP, ['--accept']); fs.writeFileSync(path.join(folderP, 'STATUS.md'), s0); return x.status === 1 && /STATUS\.md/.test(x.stderr); })());
}

// ---- gate_notice / gate_ack / canary
console.log('gate_notice, gate_ack, canary');
{
  const nl = await import(pathToFileURL(path.join(src, 'tools/hooks/notice_lib.mjs')).href);
  const ptxt = (o) => (J(o).hookSpecificOutput || {}).additionalContext || '';
  const prompt = (rt = root) => spawnSync(process.execPath, [path.join(rt, 'tools/hooks/gate_notice.mjs')], { input: JSON.stringify({ hook_event_name: 'UserPromptSubmit', session_id: 's9', prompt: 'hi', cwd: elsewhere }), cwd: elsewhere, encoding: 'utf8' });
  const stateDir = path.join(root, '.claude/state');
  const ack = (tokens, txt, rt = root) => spawnSync(process.execPath, [path.join(rt, 'tools/hooks/gate_ack.mjs'), ...(tokens === null ? [] : [tokens]), ...(txt === null || txt === undefined ? [] : [txt])], { cwd: elsewhere, encoding: 'utf8' });
  const ackAll = (txt, rt = root) => ack([...ptxt(prompt(rt).stdout).matchAll(/#(\d+)\[([0-9a-f]{6})\]/g)].map((m) => m[1] + ':' + m[2]).join(' '), txt, rt);

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

  const itemLines = (c) => c.split('\n').filter((l) => /#\d+\[[0-9a-f]{6}\]/.test(l));
  const tokOf = (l) => { const m = l.match(/#(\d+)\[([0-9a-f]{6})\]/); return m[1] + ':' + m[2]; };
  t('notice prints #n[code] next to every item (fail-open, released hand-in, no-contract)', itemLines(ctx).length >= 3 && itemLines(ctx).some((l) => l.includes(hd('vis'))) && itemLines(ctx).some((l) => /read-only research/.test(l)), ctx);
  const secretNow = fs.readFileSync(path.join(stateDir, 'ack_secret'), 'utf8').trim();
  t('the ack secret itself is never printed in the notice', secretNow.length >= 16 && !ctx.includes(secretNow));
  const allToks = itemLines(ctx).map(tokOf).join(' ');

  let a = ack(null, null); t('ack without arguments: refused, exit 1, usage shown', a.status === 1 && /REFUSED/.test(a.stderr) && /gate_ack\.mjs/.test(a.stderr));
  a = ack(allToks); t('ack with items but no text: refused', a.status === 1 && /at least 20/.test(a.stderr));
  a = ack(allToks, 'too short'); t('ack with short text: refused, exit 1', a.status === 1 && /at least 20/.test(a.stderr));
  a = ack(allToks, 'x'.repeat(19)); t('ack with 19 characters: refused', a.status === 1);
  a = ack(allToks, 'aaaaaaaaaaaaaaaaaaaaaaaaa'); t('ack with 25 junk characters in one word: refused (needs 4 words)', a.status === 1 && /4 words/.test(a.stderr), a.stderr);
  a = ack(allToks, 'a a a a a a a a a a a a'); t('ack with 12 words of one letter: refused (needs different characters)', a.status === 1 && /different characters/.test(a.stderr), a.stderr);
  a = ack(allToks, '   ' + 'y'.repeat(10) + '   '); t('ack padded with spaces to 20+: refused (trimmed)', a.status === 1);
  a = ack('please clear everything', 'looked at all of the items and judged them harmless'); t('ack without <n>:<code> tokens: refused', a.status === 1 && /<n>:<code>/.test(a.stderr), a.stderr);
  const firstN = allToks.split(' ')[0].split(':')[0];
  a = ack(`${firstN}:000000`, 'looked at the item and judged it harmless here'); t('ack with a wrong code: refused ("does not match")', a.status === 1 && /does not match/.test(a.stderr), a.stderr);
  a = ack('999999:abcdef', 'looked at the item and judged it harmless here'); t('ack of an item that is not pending: refused', a.status === 1 && /not a pending/.test(a.stderr), a.stderr);
  a = ack('', 'looked at all of the items and judged them harmless'); t('ack with an empty item list: refused', a.status === 1);
  n = prompt(); t('refused acks do not clear the notice', ptxt(n.stdout).startsWith('GATE NOTICE'));
  t('refused ack writes nothing to the log', !fs.readFileSync(logPath, 'utf8').includes('"gate":"ack"'));
  // a root that never showed a notice has no secret: ack refused
  const rootS = path.join(tmp, 'mainS'); ['tools/hooks/lib.mjs', 'tools/hooks/audit_lib.mjs', 'tools/hooks/notice_lib.mjs', 'tools/hooks/gate_ack.mjs'].forEach((rel) => { const d = path.join(rootS, rel); fs.mkdirSync(path.dirname(d), { recursive: true }); fs.copyFileSync(path.join(src, rel), d); });
  fs.mkdirSync(path.join(rootS, '.git'), { recursive: true }); fs.mkdirSync(path.join(rootS, '.claude/state'), { recursive: true });
  fs.writeFileSync(path.join(rootS, '.claude/state/gate.log'), JSON.stringify({ ts: new Date().toISOString(), gate: 'brief', reason: 'FAIL-OPEN: x' }) + '\n');
  a = ack('1:abcdef', 'guessing a code without ever seeing the notice', rootS); t('no ack secret yet (notice never shown): ack refused', a.status === 1 && /no ack secret/.test(a.stderr), a.stderr);

  // ack ONE item (the released hand-in): only that one disappears
  const hl = itemLines(ctx).find((l) => l.includes(hd('vis')));
  a = ack(tokOf(hl), 'read HANDIN_FAILED.txt, re-ran the check by hand, reasons judged harmless');
  t('ack of one item: accepted, names it, says how many log items are still open', a.status === 0 && /Acknowledged 1 item/.test(a.stdout) && /still open/.test(a.stdout), a.stderr + a.stdout);
  t('ack logged with the item number, text, timestamp', lastLog().gate === 'ack' && JSON.stringify(lastLog().items) === JSON.stringify([+tokOf(hl).split(':')[0]]) && /re-ran the check/.test(lastLog().text) && !!lastLog().ts && lastLog().cleared.handin_failed === 1, JSON.stringify(lastLog()));
  n = prompt(); const cAfter1 = ptxt(n.stdout);
  t('after acking one item: that item is gone, the others are still reported', !/HAND-IN RELEASED/.test(cAfter1) && /FAIL-OPEN x\d+/.test(cAfter1) && /NO-CONTRACT launches x1/.test(cAfter1), cAfter1);
  a = ack(tokOf(hl), 'acknowledging the same item a second time again'); t('acking the same item again: refused (not pending)', a.status === 1 && /not a pending/.test(a.stderr));
  a = ackAll('read all remaining items, judged harmless in this test run');
  t('ack of the remaining items together: accepted', a.status === 0 && /Acknowledged \d+ item/.test(a.stdout), a.stderr + a.stdout);
  n = prompt(); t('after ack: no output at all', n.status === 0 && n.stdout === '');
  t('after ack: heartbeat still logged (decision none)', lastLog().gate === 'notice' && lastLog().decision === 'none');
  n = prompt(); t('still silent on the next prompt', n.stdout === '');

  // new events after the ack show up again, and only those
  launch('Research again.\nNO-CONTRACT: a second read-only research launch here');
  n = prompt(); const c2 = ptxt(n.stdout);
  t('a new NO-CONTRACT after the ack is reported alone', /x1/.test(c2) && /second read-only research/.test(c2) && !/HAND-IN RELEASED/.test(c2) && !/read-only research, no hand-in expected/.test(c2), c2);
  run('handin_gate.mjs', 'not json');
  n = prompt(); t('a new FAIL-OPEN after the ack is reported', /FAIL-OPEN x1/.test(ptxt(n.stdout)));
  ackAll('second ack: both new items were looked at and judged fine');
  t('ack clears both', prompt().stdout === '');
  a = ackAll('acknowledged again with nothing pending at all'); t('acking with nothing pending: refused (there is no item to name)', a.status === 1);

  // hand-in released repeatedly for the same HANDIN_DIR is one item
  const dupTp = transcript('dup', mk('dup'));
  fs.mkdirSync(hd('dup'), { recursive: true }); fs.writeFileSync(path.join(hd('dup'), 'STATUS.md'), goodStatus);
  for (let i = 0; i < 6; i++) stop('d1', dupTp);
  n = prompt(); t('same HANDIN_DIR released repeatedly: reported once', /HAND-IN RELEASED after 3 refusals x1/.test(ptxt(n.stdout)), ptxt(n.stdout));
  ackAll('dup hand-in looked at, FAULTS and COMPARE missing, accepted');

  // fail-open of the notice hook itself
  n = spawnSync(process.execPath, [path.join(root, 'tools/hooks/gate_notice.mjs')], { input: 'garbage', cwd: elsewhere, encoding: 'utf8' });
  t('notice hook with bad stdin: exit 0, no output, logged FAIL-OPEN', n.status === 0 && n.stdout === '' && lastLog().gate === 'notice' && /FAIL-OPEN/.test(lastLog().reason));
  n = prompt(); t('and that failure shows up in the next notice', /FAIL-OPEN x1/.test(ptxt(n.stdout)) && /gate=notice/.test(ptxt(n.stdout)));
  ackAll('notice hook failure was a test with bad stdin, nothing to fix');
  // a log that cannot be parsed in places must not break the notice
  fs.appendFileSync(logPath, 'this is not json\n{"gate":"brief","reason":"FAIL-OPEN: test line","ts":"2026-01-01T00:00:00Z"}\n');
  n = prompt(); t('garbage line in the log is skipped, valid FAIL-OPEN after it still reported', n.status === 0 && /FAIL-OPEN x1/.test(ptxt(n.stdout)));
  ackAll('garbage log line test, nothing to fix here at all');
  // unreadable state: gate.log missing entirely
  const rootN = path.join(tmp, 'mainN'); ['tools/hooks/lib.mjs', 'tools/hooks/audit_lib.mjs', 'tools/hooks/notice_lib.mjs', 'tools/hooks/gate_notice.mjs', 'tools/hooks/gate_notice_impl.mjs', 'tools/hooks/guard.mjs'].forEach((rel) => { const d = path.join(rootN, rel); fs.mkdirSync(path.dirname(d), { recursive: true }); fs.copyFileSync(path.join(src, rel), d); });
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
    ['tools/hooks/lib.mjs', 'tools/hooks/audit_lib.mjs', 'tools/hooks/notice_lib.mjs', 'tools/hooks/gate_notice.mjs', 'tools/hooks/gate_ack.mjs', 'tools/make_brief.mjs', 'tools/check_handin.mjs', 'tools/gates_lib.mjs', 'docs/AGENT_CONTRACT.md', 'docs/templates/BRIEF.md', 'docs/templates/AUDIT_BRIEF.md']
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
  const aw = ack('1:abcdef', 'worktree ack must be refused although the text is long enough', rW);
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


// ---- audit-of-the-hooks faults: stale hand-in (1), counter reset (5), canary for the hand-in gate (4), owner quotes (10), COMPARE relevance
console.log('hand-in freshness, counter reset, owner quotes, COMPARE relevance, hand-in canary');
{
  const nl2 = await import(pathToFileURL(path.join(src, 'tools/hooks/notice_lib.mjs')).href);
  const hour = new Date(Date.now() - 3600 * 1000);
  // fault 1: a passing folder left over from an earlier round cannot satisfy a new agent
  fs.mkdirSync(hd('stale'), { recursive: true }); fs.writeFileSync(path.join(hd('stale'), 'STATUS.md'), goodStatus); fs.utimesSync(path.join(hd('stale'), 'STATUS.md'), hour, hour);
  const staleTp = transcript('stale', mk('stale', ['--no-visual']));
  r = stop('st1', staleTp);
  t('stale passing STATUS.md (written an hour before this agent started): blocked, says it predates the run', J(r.out).decision === 'block' && /older than this agent's run/.test(J(r.out).reason) && /STATUS\.md/.test(J(r.out).reason), r.out);
  fs.writeFileSync(path.join(hd('stale'), 'STATUS.md'), goodStatus);
  r = stop('st1', staleTp); t('the same agent rewrites STATUS.md during its run: passes', r.code === 0 && r.out === '' && lastLog().reason === 'hand-in check passed', r.out);
  const tsTp = path.join(tmp, 'ts.jsonl'); fs.writeFileSync(tsTp, JSON.stringify({ type: 'user', timestamp: new Date(Date.now() + 5000).toISOString(), message: { role: 'user', content: mk('tsround', ['--no-visual']) } }) + '\n');
  fs.mkdirSync(hd('tsround'), { recursive: true }); fs.writeFileSync(path.join(hd('tsround'), 'STATUS.md'), goodStatus);
  r = stop('st2', tsTp); t('run start is taken from the first transcript line timestamp when present: a STATUS.md older than it is blocked', J(r.out).decision === 'block' && /older than this agent's run/.test(J(r.out).reason), r.out);
  let ch = spawnSync(process.execPath, [path.join(root, 'tools/check_handin.mjs'), hd('stale'), '--main', root, '--no-visual', '--since', String(Date.now() + 60000)], { cwd: elsewhere, encoding: 'utf8' });
  t('check_handin --since <future>: fails and names STATUS.md; without --since it is unchanged', ch.status === 1 && /older than this agent's run/.test(ch.stderr) && spawnSync(process.execPath, [path.join(root, 'tools/check_handin.mjs'), hd('stale'), '--main', root, '--no-visual'], { cwd: elsewhere, encoding: 'utf8' }).status === 0, ch.stderr);
  // an auditor cannot reuse an AUDIT.md left in the folder before it started
  fs.mkdirSync(hd('staleaud'), { recursive: true });
  const saf = path.join(hd('staleaud'), 'AUDIT.md'); fs.writeFileSync(saf, `Auditor agent: sa1\nVerdict: accept\nReferences opened: 3 of 3\n\n## Faults, ranked\n\nan old audit of an earlier version of this round, kept in the folder\n`); fs.utimesSync(saf, hour, hour);
  r = stop('sa1', transcript('staleaud', `Audit.\nAUDIT-OF: ${hd('staleaud')}\n`)); t('auditor: an AUDIT.md older than the auditor\'s run does not satisfy the gate', J(r.out).decision === 'block' && /older than the builder|older than/.test(J(r.out).reason), r.out);

  // fault 5: the block counter restarts after a pass
  const rsTp = transcript('reset', mk('reset', ['--no-visual']));
  r = stop('rs1', rsTp); t('counter reset: first block is refusal 1', /refusal 1 of 3/.test(J(r.out).reason || ''));
  r = stop('rs1', rsTp); t('counter reset: second block is refusal 2', /refusal 2 of 3/.test(J(r.out).reason || ''));
  fs.mkdirSync(hd('reset'), { recursive: true }); fs.writeFileSync(path.join(hd('reset'), 'STATUS.md'), goodStatus);
  r = stop('rs1', rsTp); t('counter reset: pass allowed', r.code === 0 && r.out === '');
  t('counter reset: the pass deleted the counter file', !fs.existsSync(path.join(root, '.claude/state/handin/rs1.json')));
  fs.writeFileSync(path.join(hd('reset'), 'STATUS.md'), 'broken');
  r = stop('rs1', rsTp); t('counter reset: a resumed agent that fails again starts at refusal 1, not 3', /refusal 1 of 3/.test(J(r.out).reason || ''), r.out);

  // fault 10: the owner's quote may contain "time-box" and "whole task"
  const ownerQ = '"No time-box please, and never treat the whole task as an item list." (11 Oct)';
  const tfq = path.join(tmp, 'ownerq.md'); fs.writeFileSync(tfq, task(ownerQ));
  const mq = spawnSync(process.execPath, [path.join(root, 'tools/make_brief.mjs'), tfq, '--round', 'ownerq', '--worktree', path.join(wt, 'ownerq')], { encoding: 'utf8' });
  t('make_brief accepts an owner quote that contains "time-box" / "whole task"', mq.status === 0, mq.stderr);
  r = launch(mq.stdout); t('brief gate accepts the same quote', r.code === 0 && lastLog().reason === 'contract present', r.err);
  r = launch(mq.stdout + '\nPlease time-box this to 3 hours.'); t('"time-box" outside the owner words is still refused', r.code === 2 && /time-box/.test(r.err));
  const tfq2 = path.join(tmp, 'ownerq2.md'); fs.writeFileSync(tfq2, task(ownerQ).replace('none known', 'treat these as the whole task'));
  const mq2 = spawnSync(process.execPath, [path.join(root, 'tools/make_brief.mjs'), tfq2, '--round', 'ownerq2'], { encoding: 'utf8' });
  t('make_brief still refuses "whole task" in the Faults section', mq2.status === 1 && /whole task/.test(mq2.stderr));

  // COMPARE relevance: subject/type, scale, viewpoint
  const cmpDir = path.join(wt, 'cmp', 'docs', 'progress', 'cmp'); fs.mkdirSync(cmpDir, { recursive: true });
  const mkPng = (w, h) => { const b = Buffer.alloc(33); Buffer.from([0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a]).copy(b, 0); b.writeUInt32BE(13, 8); b.write('IHDR', 12, 'latin1'); b.writeUInt32BE(w, 16); b.writeUInt32BE(h, 20); return b; };
  for (const f of ['c.png', 'b.png']) fs.writeFileSync(path.join(cmpDir, f), 'x');
  fs.writeFileSync(path.join(cmpDir, 'r.png'), mkPng(800, 450));
  fs.writeFileSync(path.join(cmpDir, 'r.stats.json'), JSON.stringify({ view: 'r', width: 800, height: 450, diagnostic: false, road_px: 9000, debug_px: 0, png_sha1: crypto.createHash('sha1').update(mkPng(800, 450)).digest('hex') }));
  fs.writeFileSync(path.join(cmpDir, 'STATUS.md'), goodStatus);
  fs.writeFileSync(path.join(cmpDir, 'FAULTS.md'), '## Pass 1\n\nthe kerb stops 4 m short of the island on the south arm\n\n## Pass 2\n\nthe give-way line is a stub, not a span across the whole arm\n\n## Pass 3\n\nthe hatching bars cross the kerb line at the island nose\n\n## Residual\n\nnone, all fixed\n');
  const cmp = (rows) => { fs.writeFileSync(path.join(cmpDir, 'COMPARE.md'), '| case | render | reference | composite | before | subject/type | scale | viewpoint |\n|---|---|---|---|---|---|---|---|\n' + rows + '\n'); return spawnSync(process.execPath, [path.join(root, 'tools/check_handin.mjs'), cmpDir, '--main', root, '--gates-version', '2'], { cwd: elsewhere, encoding: 'utf8' }); };
  const okRow = '| A | r.png | aerial/a.jpg | c.png | b.png | two-lane road, same junction type | both about 40 m across | top-down, both |';
  let cc = cmp(okRow); t('COMPARE row with subject/type, scale and viewpoint statements: passes', cc.status === 0, cc.stderr);
  cc = cmp('| A | r.png | aerial/a.jpg | c.png | b.png |'); t('COMPARE row with only the old 5 columns: fails (needs 8)', cc.status === 1 && /needs 8 columns/.test(cc.stderr), cc.stderr);
  cc = cmp(okRow.replace('top-down, both', '')); t('COMPARE row with an empty viewpoint cell: fails and names the viewpoint', cc.status === 1 && /the viewpoint match is missing/.test(cc.stderr), cc.stderr);
  cc = cmp(okRow.replace('both about 40 m across', 'no, reference is far wider')); t('COMPARE row whose scale cell says "no": fails (replace the reference)', cc.status === 1 && /the scale match/.test(cc.stderr) && /replace the reference/.test(cc.stderr), cc.stderr);
  cc = cmp(okRow.replace('two-lane road, same junction type', 'yes')); t('COMPARE row with a one-word "yes": fails (must say what matches)', cc.status === 1 && /subject\/type match/.test(cc.stderr));
  cc = cmp(okRow.replace('two-lane road, same junction type', 'N/A for this case')); t('COMPARE row with "N/A": fails', cc.status === 1);
  cc = cmp(okRow.replace('two-lane road, same junction type', 'north-side road, same junction type')); t('a statement that merely starts with the letters "no" ("north-side ...") is not mistaken for "no"', cc.status === 0, cc.stderr);
  cc = cmp(okRow + '\n| B | r.png | aerial/a.jpg | c.png | b.png | street view of a bus stop | 5 m wide frame | eye-level, both |'); t('every row is checked, not just the first', cc.status === 0, cc.stderr);
  cc = cmp(okRow + '\n| B | r.png | aerial/a.jpg | c.png | b.png | street view of a bus stop | 5 m wide frame |'); t('a second incomplete row fails and names its case', cc.status === 1 && /needs 8 columns/.test(cc.stderr));

  // fault 4: canary for the hand-in gate (RULE 3)
  const T3 = Date.parse('2026-10-11T12:00:00Z');
  const at3 = (sec, e) => ({ ts: new Date(T3 + sec * 1000).toISOString(), ...e });
  const c3 = (es, nowSec) => nl2.canaryProblems(es.map((e, i) => ({ n: i + 1, ...e })), T3 + nowSec * 1000).filter((x) => /RULE 3/.test(x));
  const H = 3600;
  t('canary RULE 3: agent started 7 h ago, no hand-in gate line since: tripped, names the agent', (() => { const p = c3([at3(0, { gate: 'inject', decision: 'inject', agent_id: 'zz', agent_type: 'general-purpose' }), at3(60, { gate: 'notice', reason: 'ran' })], 7 * H); return p.length === 1 && /zz/.test(p[0]) && /none ever/.test(p[0]); })());
  t('canary RULE 3: agent started 5 h ago: not yet', c3([at3(0, { gate: 'inject', decision: 'inject', agent_id: 'zz' })], 5 * H).length === 0);
  t('canary RULE 3: a hand-in gate line (allow) after the start clears it', c3([at3(0, { gate: 'inject', decision: 'inject', agent_id: 'zz' }), at3(100, { gate: 'handin', decision: 'allow', reason: 'no HANDIN_DIR in brief' })], 9 * H).length === 0);
  t('canary RULE 3: a hand-in line BEFORE the start does not help', c3([at3(0, { gate: 'handin', decision: 'allow', reason: 'x' }), at3(50, { gate: 'inject', decision: 'inject', agent_id: 'zz' })], 9 * H).length === 1);
  t('canary RULE 3: FAIL-OPEN hand-in lines do not count as the gate running', c3([at3(0, { gate: 'inject', decision: 'inject', agent_id: 'zz' }), at3(100, { gate: 'handin', decision: 'allow', reason: 'FAIL-OPEN: no readable agent transcript' })], 9 * H).length === 1);
  t('canary RULE 3: exempt-type skips (decision skip) are not agent starts', c3([at3(0, { gate: 'inject', decision: 'skip', reason: 'exempt type' })], 9 * H).length === 0);
  t('canary RULE 3: audit-gate lines (gate audit) do not count as hand-in lines', c3([at3(0, { gate: 'inject', decision: 'inject', agent_id: 'zz' }), at3(100, { gate: 'audit', decision: 'owed' })], 9 * H).length === 1);
  const rC3 = path.join(tmp, 'mainC3');
  ['tools/hooks/lib.mjs', 'tools/hooks/audit_lib.mjs', 'tools/hooks/notice_lib.mjs', 'tools/make_brief.mjs', 'tools/check_handin.mjs', 'tools/gates_lib.mjs', 'docs/AGENT_CONTRACT.md', 'docs/templates/BRIEF.md'].forEach((rel) => { const d = path.join(rC3, rel); fs.mkdirSync(path.dirname(d), { recursive: true }); fs.copyFileSync(path.join(src, rel), d); });
  fs.mkdirSync(path.join(rC3, '.git'), { recursive: true }); fs.mkdirSync(path.join(rC3, '.claude/state'), { recursive: true });
  const oldL = (h, e) => JSON.stringify({ ts: new Date(Date.now() - h * 3600000).toISOString(), ...e }) + '\n';
  fs.writeFileSync(path.join(rC3, '.claude/state/gate.log'), oldL(8, { gate: 'inject', decision: 'inject', agent_id: 'old1', agent_type: 'general-purpose' }) + oldL(0.1, { gate: 'notice', reason: 'ran' }));
  const tfc = path.join(tmp, 'c3task.md'); fs.writeFileSync(tfc, task());
  const bc = spawnSync(process.execPath, [path.join(rC3, 'tools/make_brief.mjs'), tfc, '--round', 'c3', '--worktree', path.join(wt, 'c3')], { cwd: elsewhere, encoding: 'utf8' });
  t('make_brief through the real tool: RULE 3 shows, exit 3, brief still printed', bc.status === 3 && /RULE 3/.test(bc.stderr) && /AGENT-CONTRACT-MARKER/.test(bc.stdout), bc.stderr);
}


// ---- gates2: audit records (the four faults of the audit gate) and the measured gates through the finish hook
console.log('audit gate v2: records, verdict, resumed builder, no builder record');
{
  const al = await import(pathToFileURL(path.join(src, 'tools/hooks/audit_lib.mjs')).href);
  const stateDir = path.join(root, '.claude/state');
  const notice = () => { const o = J(spawnSync(process.execPath, [path.join(root, 'tools/hooks/gate_notice.mjs')], { input: JSON.stringify({ hook_event_name: 'UserPromptSubmit', session_id: 's9', prompt: 'hi', cwd: elsewhere }), cwd: elsewhere, encoding: 'utf8' }).stdout); return (o.hookSpecificOutput && o.hookSpecificOutput.additionalContext) || ''; };
  const aText = (id, verdict = 'accept') => `Auditor agent: ${id}\nVerdict: ${verdict}\nReferences opened: 3 of 3 listed\n\n## Own fault list\n\nfrom the photos alone, before reading anything the builder wrote about it\n\n## Faults, ranked worst first\n\n1. the kerb line stops 4 m short of the island in the street view, which no driver would accept\n`;
  const aBrief = (folder) => { const tfa = path.join(tmp, 'f_audit_task_' + path.basename(folder) + '.md'); fs.writeFileSync(tfa, `# T audit\n## Owner's words\n"The photo is the target." (11 Oct)\n## Cases\ncase A | ${path.join(root, 'docs/reference/aerial/a.jpg')} ${path.join(root, 'docs/reference/street/b.jpg')} ${path.join(root, 'docs/reference/gsv/c.jpg')}\n`); return spawnSync(process.execPath, [path.join(root, 'tools/make_brief.mjs'), tfa, '--audit', folder], { cwd: elsewhere, encoding: 'utf8' }).stdout; };
  const accept = (folder, extra = []) => spawnSync(process.execPath, [path.join(root, 'tools/check_handin.mjs'), folder, '--main', root, '--no-visual', '--accept', ...extra], { cwd: elsewhere, encoding: 'utf8' });
  const build = (round, id) => { const tp = transcript('f_' + round, mk(round, ['--no-visual'])); fs.mkdirSync(hd(round), { recursive: true }); fs.writeFileSync(path.join(hd(round), 'STATUS.md'), goodStatus); const o = stop(id, tp); return { tp, o }; };
  const audit = (folder, id, text) => { fs.writeFileSync(path.join(folder, 'AUDIT.md'), text); return stop(id, transcript('f_aud_' + id, aBrief(folder))); };
  const owedFor = (folder) => notice().includes(folder);

  // fault 1: a made-up auditor id
  let { o } = build('fa', 'fb1'); const fa = hd('fa');
  t('builder (signed v3 record) passes', o.code === 0 && al.readBuilder(fa, stateDir).v === 3 && typeof al.readBuilder(fa, stateDir).sig === 'string' && owedFor(fa));
  fs.writeFileSync(path.join(fa, 'AUDIT.md'), aText('zzz'));
  t('fault 1: a hand-typed AUDIT.md with an invented auditor id does NOT clear the owed audit (no audit record)', owedFor(fa) && /no audit record/.test(notice()));
  let a = accept(fa); t('fault 1: --accept refuses it too and names the missing record', a.status === 1 && /no audit record/.test(a.stderr), a.stderr);
  a = accept(fa, ['--builder', 'fb1']); t('fault 1: --accept --builder fb1 does not bypass the record', a.status === 1 && /no audit record/.test(a.stderr), a.stderr);
  // a real auditor finishing through the gate
  let r2 = audit(fa, 'fa1', aText('fa1'));
  t('a real auditor finishing through the gate: allowed, AUDIT RECORD written with its harness id and the builder id', r2.code === 0 && al.readAudit(fa, stateDir).auditor_id === 'fa1' && al.readAudit(fa, stateDir).builder_id === 'fb1', r2.out);
  t('...and the owed audit is cleared', !owedFor(fa), notice());
  a = accept(fa); t('--accept with a recorded audit with verdict accept: passes', a.status === 0 && /independent of builder fb1/.test(a.stdout), a.stderr);
  a = accept(fa, ['--builder', 'someone-else']); t('--accept --builder naming another agent than the audit record: refused', a.status === 1 && /not the recorded builder/.test(a.stderr), a.stderr);
  // edited after the finish gate
  fs.writeFileSync(path.join(fa, 'AUDIT.md'), aText('fa1') + '\nextra line added later by someone\n');
  t('an AUDIT.md edited after the auditor finished is owed again (hash differs from the record)', owedFor(fa) && /edited after the auditor finished/.test(notice()));
  fs.writeFileSync(path.join(fa, 'AUDIT.md'), aText('fa1'));
  t('...restored to the recorded text: cleared again', !owedFor(fa));

  // fault 2: verdict
  ({ o } = build('fv', 'fvb')); const fv = hd('fv');
  r2 = audit(fv, 'fva', aText('fva', 'send back'));
  t('verdict "send back": the audit was done, the notice clears', r2.code === 0 && !owedFor(fv));
  a = accept(fv); t('fault 2: --accept refuses a "send back" audit', a.status === 1 && /not "accept"/.test(a.stderr) && /send back/.test(a.stderr), a.stderr);

  // fault 3: resumed builder keeps its id; a new passed hand-in must invalidate the old audit
  const before = Date.parse(al.readBuilder(fa, stateDir).passed_at);
  const owedBefore = fs.readFileSync(logPath, 'utf8').split('\n').filter((l) => l.includes('"decision":"owed"') && l.includes(fa.replace(/\\/g, '\\\\'))).length;
  const resume = (name) => { const tp2 = transcript(name, mk('fa', ['--no-visual'])); fs.writeFileSync(path.join(fa, 'STATUS.md'), goodStatus); return stop('fb1', tp2); };
  const resumed = resume('f_fa_resume');
  t('fault 3: the SAME builder id passes the hand-in again after the audit (resumed after send back)', resumed.code === 0 && Date.parse(al.readBuilder(fa, stateDir).passed_at) >= before);
  t('...the old audit no longer counts: owed again and the log says so', owedFor(fa) && /older than the builder|no audit record|fresh audit/.test(notice()) && fs.readFileSync(logPath, 'utf8').split('\n').filter((l) => l.includes('"decision":"owed"') && l.includes(fa.replace(/\\/g, '\\\\'))).length === owedBefore + 1);
  a = accept(fa); t('...and --accept refuses until a new audit is recorded', a.status === 1, a.stderr);
  const again = resume('f_fa_resume2');
  t('a second pass back to back (PreToolUse then Stop at one finish) logs no extra owed line', again.code === 0 && fs.readFileSync(logPath, 'utf8').split('\n').filter((l) => l.includes('"decision":"owed"') && l.includes(fa.replace(/\\/g, '\\\\'))).length === owedBefore + 1);
  r2 = audit(fa, 'fa2', aText('fa2'));
  t('a new auditor after the re-hand-in clears it and is the recorded one', r2.code === 0 && !owedFor(fa) && al.readAudit(fa, stateDir).auditor_id === 'fa2');

  // fault 4: no builder record
  const fn = path.join(wt, 'fn', 'docs', 'progress', 'fn'); fs.mkdirSync(fn, { recursive: true });
  r2 = audit(fn, 'fn1', aText('fn1'));
  t('fault 4: AUDIT-OF a folder with no builder record and no log line: the auditor is REFUSED (fail closed)', J(r2.out).decision === 'block' && /no record of who built/.test(J(r2.out).reason) && !al.readAudit(fn, stateDir), r2.out);
  fs.rmSync(path.join(stateDir, 'builders', al.builderKey(fv) + '.json'));
  fs.rmSync(path.join(stateDir, 'audits', al.builderKey(fv) + '.json'));
  r2 = audit(fv, 'fva2', aText('fva2'));
  t('fault 4: builder record deleted but the gate log still shows who passed it: the id is recovered from the log, the audit is accepted', r2.code === 0 && al.readAudit(fv, stateDir).builder_id === 'fvb', r2.out);
  r2 = audit(fv, 'fvb', aText('fvb'));
  t('...and the builder id (recovered from the log) still cannot audit its own round', J(r2.out).decision === 'block' && /never the same agent/.test(J(r2.out).reason), r2.out);
}

console.log('measured gates through the finish hook');
{
  const gate = (round) => path.join(wt, round, 'prototypes/road-draw');
  const scene = (round, passed, total) => { const lines = []; for (let i = 0; i < total; i++) lines.push((i < passed ? 'PASS ' : 'FAIL ') + 'check_' + i + ' detail'); fs.mkdirSync(path.join(gate(round), 'tools'), { recursive: true }); fs.mkdirSync(path.join(gate(round), 'node_modules'), { recursive: true }); fs.writeFileSync(path.join(gate(round), 'tools/scene.mjs'), `console.log(${JSON.stringify(lines.join('\n'))}); console.log('${passed}/${total} passed in 0.1s');\n`); };
  const tfg = path.join(tmp, 'gate_task.md'); fs.writeFileSync(tfg, task() + '## Gates\nscene_check >= 20/21 (baseline 20/21, target 21/21)\n');
  const mkg = (round) => { const r = spawnSync(process.execPath, [path.join(root, 'tools/make_brief.mjs'), tfg, '--round', round, '--worktree', path.join(wt, round), '--no-visual'], { encoding: 'utf8' }); if (r.status !== 0) throw new Error(r.stderr); return r.stdout; };
  const brief = mkg('gh');
  t('make_brief wrote GATES-VERSION and the GATE line into the brief the hook reads', /^GATES-VERSION: 2$/m.test(brief) && /^GATE: scene_check >= 20\/21/m.test(brief));
  const tp = transcript('gh', brief); fs.mkdirSync(hd('gh'), { recursive: true }); fs.writeFileSync(path.join(hd('gh'), 'STATUS.md'), goodStatus);
  fs.writeFileSync(path.join(hd('gh'), 'FAULTS.md'), '## Pass 1\nthe kerb stops 4 m short of the island on the south arm\n## Pass 2\nthe give-way line is a stub, not a span across the whole arm\n## Pass 3\nthe hatching bars cross the kerb line at the island nose\n## Residual\nnone, all fixed\n');
  scene('gh', 17, 21);
  let g = stop('gb1', tp);
  t('hook: the brief\'s GATE line is passed to check_handin; 17/21 measured: stop BLOCKED with the measured number', J(g.out).decision === 'block' && /measured 17\/21/.test(J(g.out).reason), g.out + g.err);
  scene('gh', 21, 21);
  g = stop('gb1', tp);
  t('hook: 21/21 measured: stop allowed, hand-in recorded', g.code === 0 && g.out === '' && lastLog().reason === 'hand-in check passed', g.out + g.err);
  const tpOld = transcript('gh_old', mk('gh2', ['--no-visual'])); fs.mkdirSync(hd('gh2'), { recursive: true }); fs.writeFileSync(path.join(hd('gh2'), 'STATUS.md'), goodStatus);
  scene('gh2', 3, 21);
  g = stop('gb2', tpOld);
  t('hook: a brief WITHOUT GATES-VERSION (agent started before gates v2) is not measured: 3/21 on disk does not block it', g.code === 0 && g.out === '' && lastLog().reason === 'hand-in check passed', g.out + g.err);
  const bfFile = path.join(tmp, 'gh3_brief.txt'); fs.writeFileSync(bfFile, mkg('gh3'));
  const tp3 = transcript('gh3', `Your brief is in the file.\nBRIEF-FILE: ${bfFile}`); fs.mkdirSync(hd('gh3'), { recursive: true }); fs.writeFileSync(path.join(hd('gh3'), 'STATUS.md'), goodStatus); fs.writeFileSync(path.join(hd('gh3'), 'FAULTS.md'), '## Pass 1\nthe kerb stops 4 m short of the island on the south arm\n## Pass 2\nthe give-way line is a stub, not a span across the whole arm\n## Pass 3\nthe hatching bars cross the kerb line at the island nose\n## Residual\nnone\n');
  scene('gh3', 15, 21); g = stop('gb3', tp3);
  t('hook: gates in a BRIEF-FILE are read as well', J(g.out).decision === 'block' && /measured 15\/21/.test(J(g.out).reason), g.out + g.err);
}

// ---- gates3: guard (fail open on a broken library), helper agents, NO-CONTRACT grouping and auditors, BLOCKED, PRIOR-AUDIT
console.log('gates3 hooks');
{
  const al = await import(pathToFileURL(path.join(src, 'tools/hooks/audit_lib.mjs')).href);
  const stateDir = path.join(root, '.claude/state');
  const ptxt3 = (o) => (J(o).hookSpecificOutput || {}).additionalContext || '';
  const noticeOf = (rt = root) => ptxt3(spawnSync(process.execPath, [path.join(rt, 'tools/hooks/gate_notice.mjs')], { input: JSON.stringify({ hook_event_name: 'UserPromptSubmit', session_id: 's9', prompt: 'hi', cwd: elsewhere }), cwd: elsewhere, encoding: 'utf8' }).stdout);
  const mkCheckout = (name) => {
    const rt = path.join(tmp, name);
    for (const f of fs.readdirSync(path.join(src, 'tools/hooks')).filter((n) => n.endsWith('.mjs') && !/^test_/.test(n))) { fs.mkdirSync(path.join(rt, 'tools/hooks'), { recursive: true }); fs.copyFileSync(path.join(src, 'tools/hooks', f), path.join(rt, 'tools/hooks', f)); }
    for (const f of ['tools/check_handin.mjs', 'tools/make_brief.mjs', 'tools/gates_lib.mjs', 'tools/check_wrappers.mjs', 'docs/AGENT_CONTRACT.md', 'docs/templates/BRIEF.md']) { fs.mkdirSync(path.dirname(path.join(rt, f)), { recursive: true }); fs.copyFileSync(path.join(src, f), path.join(rt, f)); }
    fs.mkdirSync(path.join(rt, '.git'), { recursive: true });
    return rt;
  };
  const logOf = (rt) => { try { return fs.readFileSync(path.join(rt, '.claude/state/gate.log'), 'utf8').trim().split('\n').map(J); } catch { return []; } };
  const runIn = (rt, script, stdin) => spawnSync(process.execPath, [path.join(rt, 'tools/hooks', script)], { input: JSON.stringify(stdin), cwd: elsewhere, encoding: 'utf8', timeout: 60000 });

  // -- fail open on a broken library or a broken implementation (gates2 audit fault 9)
  const stdins = { 'brief_gate.mjs': { hook_event_name: 'PreToolUse', tool_name: 'Agent', tool_input: { prompt: 'x', subagent_type: 'general-purpose' } }, 'handin_gate.mjs': { hook_event_name: 'SubagentStop', agent_id: 'g1', agent_type: 'general-purpose' }, 'gate_notice.mjs': { hook_event_name: 'UserPromptSubmit', session_id: 's' }, 'contract_inject.mjs': { hook_event_name: 'SubagentStart', agent_id: 'g1', agent_type: 'general-purpose' } };
  for (const broken of ['lib.mjs', 'audit_lib.mjs']) {
    for (const [script, input] of Object.entries(stdins)) {
      if (broken === 'audit_lib.mjs' && script === 'contract_inject.mjs') continue;
      const rt = mkCheckout('guard_' + broken + '_' + script);
      fs.writeFileSync(path.join(rt, 'tools/hooks', broken), 'export const broken = ;;; syntax error\n');
      const g = runIn(rt, script, input);
      const l = logOf(rt).filter((e) => /FAIL-OPEN: hook implementation could not load or crashed/.test(e.reason || ''));
      t(`${script} with a syntax error in ${broken}: exit 0 (fail open) and ONE logged FAIL-OPEN line`, g.status === 0 && l.length === 1 && l[0].error.length > 0, g.status + ' ' + g.stderr.slice(0, 200) + JSON.stringify(logOf(rt)));
    }
  }
  { const rt = mkCheckout('guard_impl'); fs.writeFileSync(path.join(rt, 'tools/hooks/brief_gate_impl.mjs'), 'export async function main() { throw new Error("boom in the implementation"); }\n'); const g = runIn(rt, 'brief_gate.mjs', stdins['brief_gate.mjs']); t('an exception thrown by the implementation: exit 0, FAIL-OPEN logged with the message', g.status === 0 && logOf(rt).some((e) => /FAIL-OPEN/.test(e.reason || '') && /boom in the implementation/.test(e.error || '')), JSON.stringify(logOf(rt))); }
  { const rt = mkCheckout('guard_missing'); fs.rmSync(path.join(rt, 'tools/hooks/handin_gate_impl.mjs')); const g = runIn(rt, 'handin_gate.mjs', stdins['handin_gate.mjs']); t('an implementation file that is missing: exit 0, FAIL-OPEN logged', g.status === 0 && logOf(rt).some((e) => /FAIL-OPEN/.test(e.reason || '')), JSON.stringify(logOf(rt))); }
  t('guard.mjs imports only node built-ins (so no library can stop its FAIL-OPEN line)', [...fs.readFileSync(path.join(src, 'tools/hooks/guard.mjs'), 'utf8').matchAll(/^import .* from '([^']+)'/gm)].every((m) => m[1].startsWith('node:')));
  { const g = spawnSync(process.execPath, [path.join(src, 'tools/check_handin.mjs'), tmp, '--no-visual', '--main', root], { cwd: elsewhere, encoding: 'utf8' }); t('check_handin has no static import of gates_lib or the hook libraries (dynamic imports inside try)', !/^import .* from '\.\/(gates_lib|hooks\/)/m.test(fs.readFileSync(path.join(src, 'tools/check_handin.mjs'), 'utf8')) && g.status !== null); }

  // -- helper agents vs real agents without a transcript
  let h = run('handin_gate.mjs', { hook_event_name: 'SubagentStop', session_id: 's1', cwd: elsewhere, agent_id: 'helper1', agent_type: '' });
  t('empty agent_type, no transcript, never launched: a separate non-alarm log entry (no FAIL-OPEN)', h.code === 0 && /internal helper agent/.test(lastLog().reason) && !/FAIL-OPEN/.test(lastLog().reason), JSON.stringify(lastLog()));
  t('...and it raises no gate notice item', !/helper1/.test(noticeOf()));
  h = run('handin_gate.mjs', { hook_event_name: 'SubagentStop', session_id: 's1', cwd: elsewhere, agent_id: 'real1', agent_type: 'general-purpose' });
  t('a real agent type with no transcript: still FAIL-OPEN', h.code === 0 && /FAIL-OPEN: no readable agent transcript/.test(lastLog().reason), JSON.stringify(lastLog()));
  run('contract_inject.mjs', { hook_event_name: 'SubagentStart', agent_id: 'known1', agent_type: '', cwd: elsewhere });
  h = run('handin_gate.mjs', { hook_event_name: 'SubagentStop', session_id: 's1', cwd: elsewhere, agent_id: 'known1', agent_type: '' });
  t('an empty agent_type whose id the launch hook saw (a known agent id): FAIL-OPEN', /FAIL-OPEN: no readable agent transcript/.test(lastLog().reason), JSON.stringify(lastLog()));
  t('twelve helper agents in a row raise no notice item of their own', (() => { const before = (noticeOf().match(/FAIL-OPEN x(\d+)/) || [0, 0])[1]; for (let i = 0; i < 12; i++) run('handin_gate.mjs', { hook_event_name: 'SubagentStop', agent_id: 'hlp' + i, agent_type: '' }); return (noticeOf().match(/FAIL-OPEN x(\d+)/) || [0, 0])[1] === before; })());

  // -- NO-CONTRACT launches are ONE summary item per notice
  { const rt = mkCheckout('nc_group');
    fs.mkdirSync(path.join(rt, '.claude/state'), { recursive: true });
    const lines = []; for (let i = 0; i < 5; i++) lines.push(JSON.stringify({ ts: '2026-01-01T00:00:0' + i + 'Z', gate: 'brief', agent_type: 'general-purpose', description: 'job ' + i, decision: 'allow', reason: 'NO-CONTRACT', no_contract_reason: i % 2 ? 'read-only research of the docs tree' : 'merge of branch X into main, no hand-in' }));
    fs.writeFileSync(path.join(rt, '.claude/state/gate.log'), lines.join('\n') + '\n');
    const c1 = noticeOf(rt); const ncLines = c1.split('\n').filter((l) => /NO-CONTRACT/.test(l));
    t('five NO-CONTRACT launches: ONE summary line carrying the count and the distinct reasons', ncLines.length === 1 && /x5/.test(ncLines[0]) && /2 distinct reason/.test(ncLines[0]) && /read-only research|merge of branch X/.test(ncLines[0]), c1);
    t('...with exactly one #n[code] token for the whole group', (ncLines[0].match(/#\d+\[[0-9a-f]{6}\]/g) || []).length === 1);
    t('...and the notice says 1 open item', /1 open item/.test(c1), c1.slice(0, 200));
    const tok = ncLines[0].match(/#(\d+)\[([0-9a-f]{6})\]/); const ak = spawnSync(process.execPath, [path.join(rt, 'tools/hooks/gate_ack.mjs'), tok[1] + ':' + tok[2], 'looked at the five launches, all were research and merges'], { cwd: elsewhere, encoding: 'utf8' });
    t('acking that one token clears all five launches', ak.status === 0 && noticeOf(rt) === '', ak.stderr + ak.stdout + noticeOf(rt));
    fs.appendFileSync(path.join(rt, '.claude/state/gate.log'), JSON.stringify({ ts: '2026-01-02T00:00:00Z', gate: 'brief', description: 'later', decision: 'allow', reason: 'NO-CONTRACT', no_contract_reason: 'another research job started later' }) + '\n');
    t('a launch after the ack shows up again, alone (x1)', /NO-CONTRACT launches x1/.test(noticeOf(rt)), noticeOf(rt)); }

  // -- a NO-CONTRACT launch cannot carry an auditor (gates2 audit fault 8)
  let l3 = launch('Audit the round for me.\nNO-CONTRACT: independent audit of the hand-in in docs/progress/gates2 please');
  t('NO-CONTRACT launch naming an audit of docs/progress/...: DENIED (exit 2), says to use make_brief --audit', l3.code === 2 && /make_brief\.mjs <task-file> --audit/.test(l3.err) && lastLog().reason === 'NO-CONTRACT audit launch refused', l3.err);
  l3 = launch('You are the auditor.\nRead C:\\x\\roadworld-wt\\gates2\\docs\\progress\\gates2\\STATUS.md\nNO-CONTRACT: read-only check of a hand-in folder for the main session');
  t('...an "auditor" prompt with a Windows docs\\progress path: denied too', l3.code === 2, l3.err);
  l3 = launch('Second look.\nNO-CONTRACT: read-only review of the work in docs/progress/earthworks r9 by someone else');
  t('...a "review of ... docs/progress" prompt: denied', l3.code === 2, l3.err);
  l3 = launch('Review the code.\nNO-CONTRACT: read-only review of the hook scripts, no hand-in expected');
  t('a review that names no progress folder: allowed', l3.code === 0 && lastLog().reason === 'NO-CONTRACT', l3.err);
  l3 = launch('Format check.\nNO-CONTRACT: read the docs/progress/README naming rules and report them');
  t('a read of docs/progress without audit/review wording: allowed', l3.code === 0, l3.err);
  const withAof = launch(mkAuditBrief());
  t('a proper AUDIT-OF launch is unaffected', withAof.code === 0 && lastLog().reason === 'AUDIT-OF', withAof.err);
  t('the refused audit launch is on the notice (news to the main session)', /NO-CONTRACT AUDIT LAUNCH REFUSED x\d/.test(noticeOf()), noticeOf());
  function mkAuditBrief() { const tf = path.join(tmp, 'aud_nc.md'); fs.writeFileSync(tf, '# T\n## Owner\'s words\n"The photo is the target." (11 Oct)\n## Cases\ncase A | a.jpg b.jpg c.jpg\n'); return spawnSync(process.execPath, [path.join(root, 'tools/make_brief.mjs'), tf, '--audit', hd('ncaud')], { encoding: 'utf8' }).stdout; }

  // -- BLOCKED through the finish hook
  const faults3 = '## Pass 1\nthe kerb stops 4 m short of the island on the south arm\n## Pass 2\nthe give-way line is a stub, not a span across the whole arm\n## Pass 3\nthe hatching bars cross the kerb line at the island nose\n## Residual\nnone, all fixed\n';
  const blkLine = 'BLOCKED: wall curl at the road ends | tried: a ramp, a fillet and a longer fade, three renders | blocked by: the crossing road keeps the ground 2 m high (render s4) | needs: a decision on the crossing level';
  const blkTp = transcript('blk', mk('blk', ['--no-visual', '--v2']));
  fs.mkdirSync(hd('blk'), { recursive: true }); fs.writeFileSync(path.join(hd('blk'), 'STATUS.md'), goodStatus.replace('Nothing skipped; did not verify the live editor.', blkLine)); fs.writeFileSync(path.join(hd('blk'), 'FAULTS.md'), faults3);
  let bl = stop('blk1', blkTp);
  const lg = logOf(root);
  t('a STATUS whose only open item is a complete BLOCKED line: the stop is ALLOWED', bl.code === 0 && bl.out === '' && lg.some((e) => e.gate === 'handin' && e.reason === 'hand-in check passed' && e.agent_id === 'blk1'), bl.out + bl.err);
  const bev = lg.filter((e) => e.gate === 'blocked');
  t('...and a `blocked` line is written to gate.log with the item text', bev.length === 1 && bev[0].reason === 'BLOCKED' && /wall curl at the road ends/.test(bev[0].items[0]) && bev[0].handin_dir === hd('blk'), JSON.stringify(bev));
  const nb = noticeOf(); t('...which raises a gate notice item (news, not noise) with the item', /BLOCKED x1/.test(nb) && /wall curl at the road ends/.test(nb), nb);
  const btok = nb.split('\n').find((l) => /BLOCKED x1/.test(l) === false && /#\d+\[[0-9a-f]{6}\]/.test(l) && /blk/.test(l));
  const allToks = [...nb.matchAll(/#(\d+)\[([0-9a-f]{6})\]/g)].map((m) => m[1] + ':' + m[2]);
  const blockedTok = (nb.split('\n').find((l, i, a) => /#\d+\[[0-9a-f]{6}\]/.test(l) && /blk/.test(l)) || '').match(/#(\d+)\[([0-9a-f]{6})\]/);
  t('...whose token can be acknowledged and then disappears', (() => { if (!blockedTok) return false; const ak = spawnSync(process.execPath, [path.join(root, 'tools/hooks/gate_ack.mjs'), blockedTok[1] + ':' + blockedTok[2], 'saw the blocked wall curl item, will take it to the owner'], { cwd: elsewhere, encoding: 'utf8' }); return ak.status === 0 && !/BLOCKED x1/.test(noticeOf()); })());
  // excuses still refuse, and the refusal points at the BLOCKED form
  const exTp = transcript('exc', mk('exc', ['--no-visual', '--v2']));
  fs.mkdirSync(hd('exc'), { recursive: true }); fs.writeFileSync(path.join(hd('exc'), 'STATUS.md'), goodStatus.replace('Nothing skipped; did not verify the live editor.', 'The wall curl is out of scope and a later round will do it.')); fs.writeFileSync(path.join(hd('exc'), 'FAULTS.md'), faults3);
  bl = stop('exc1', exTp); t('an excuse ("out of scope", "later round") with no tried / blocked by: the stop is BLOCKED, the reason shows the BLOCKED form', J(bl.out).decision === 'block' && /left undone or deferred/.test(J(bl.out).reason) && /BLOCKED: <item> \| tried/.test(J(bl.out).reason), bl.out);
  fs.writeFileSync(path.join(hd('exc'), 'STATUS.md'), goodStatus.replace('Nothing skipped; did not verify the live editor.', 'BLOCKED: wall curl | tried: a ramp and a fade | blocked by: time'));
  bl = stop('exc1', exTp); t('"blocked by: time" is an excuse in the BLOCKED form: still refused', J(bl.out).decision === 'block' && /BLOCKED line is incomplete/.test(J(bl.out).reason), bl.out);
  // placeholder COMPARE through the hook
  // PRIOR-AUDIT line reaches check_handin
  const priorF = path.join(tmp, 'prior_for_hook_AUDIT.md'); fs.writeFileSync(priorF, '## Faults\nx\n## For the next builder\n\n1. fix the first thing\n2. fix the second thing\n');
  const paTp = transcript('pa', mk('pa', ['--no-visual', '--v2', '--prior-audit', priorF]));
  t('make_brief --prior-audit emits a PRIOR-AUDIT: line', /^PRIOR-AUDIT: /m.test(mk('pa', ['--no-visual', '--v2', '--prior-audit', priorF])));
  fs.mkdirSync(hd('pa'), { recursive: true }); fs.writeFileSync(path.join(hd('pa'), 'STATUS.md'), goodStatus); fs.writeFileSync(path.join(hd('pa'), 'FAULTS.md'), faults3);
  bl = stop('pa1', paTp); t('send-back brief (PRIOR-AUDIT): a STATUS without the "Prior audit items" table is refused by the finish hook', J(bl.out).decision === 'block' && /Prior audit items/.test(J(bl.out).reason), bl.out);
  fs.writeFileSync(path.join(hd('pa'), 'STATUS.md'), goodStatus + '\n## Prior audit items\n\n| Item | Status | Proof |\n|---|---|---|\n| 1 | fixed | test "x" in tools/test_gates.mjs passes |\n| 2 | fixed | run of the real round 9 folder refuses, see STATUS table |\n');
  bl = stop('pa1', paTp); t('...with the table: allowed', bl.code === 0 && bl.out === '', bl.out);
}

console.log('gates3 hooks, part 2: BLOCKED for old-layout briefs, once per finish; secret creation');
{
  const logLines = () => fs.readFileSync(logPath, 'utf8').trim().split('\n').map(J);
  const blkLine = 'BLOCKED: slip road taper | tried: three taper lengths, each rendered and measured | blocked by: the roundabout branch is not merged so the arm type does not exist yet | needs: the roundabout merge';
  const tp1 = transcript('blkv1', mk('blkv1', ['--no-visual']));   // a brief of the older layout: no GATES-VERSION line, no status reader
  fs.mkdirSync(hd('blkv1'), { recursive: true }); fs.writeFileSync(path.join(hd('blkv1'), 'STATUS.md'), goodStatus.replace('Nothing skipped; did not verify the live editor.', blkLine));
  let b1 = stop('bv1', tp1);
  t('old-layout brief: a STATUS with a BLOCKED line passes as before and the BLOCKED news is logged anyway (read from STATUS.md by the hook)', b1.code === 0 && b1.out === '' && logLines().filter((e) => e.gate === 'blocked' && e.handin_dir === hd('blkv1')).length === 1, b1.out);
  b1 = stop('bv1', tp1);
  t('...the finish hook runs twice at one finish (PreToolUse, then Stop): still ONE blocked entry', logLines().filter((e) => e.gate === 'blocked' && e.handin_dir === hd('blkv1')).length === 1);
  fs.writeFileSync(path.join(hd('blkv1'), 'STATUS.md'), goodStatus.replace('Nothing skipped; did not verify the live editor.', blkLine + '\nBLOCKED: second thing | tried: another attempt at it | blocked by: another exact error message here | needs: a tool'));
  stop('bv1', tp1);
  t('...and a changed set of BLOCKED items is news again', logLines().filter((e) => e.gate === 'blocked' && e.handin_dir === hd('blkv1')).length === 2);
  // secret creation: never overwritten, exclusive create
  const lib = await import(pathToFileURL(path.join(src, 'tools/hooks/lib.mjs')).href);
  const sd = path.join(tmp, 'secret_state');
  const s1 = lib.ensureSecret(sd), s2 = lib.ensureSecret(sd);
  t('ensureSecret returns the same secret on every call and never overwrites it', s1 === s2 && lib.readSecret(sd) === s1 && /^[0-9a-f]{24}$/.test(s1));
  const kids = [0, 1, 2, 3, 4, 5].map(() => spawnSync(process.execPath, ['-e', `import(${JSON.stringify(pathToFileURL(path.join(src, 'tools/hooks/lib.mjs')).href)}).then((m) => console.log(m.ensureSecret(${JSON.stringify(path.join(tmp, 'secret_state2'))})))`], { encoding: 'utf8' }).stdout.trim());
  t('six processes asking for a secret in a fresh state dir all get the same one', new Set(kids).size === 1 && kids[0].length === 24, JSON.stringify(kids));
  const rec = lib.signRecord({ a: 1, b: 'x' }, sd);
  t('signRecord / verifyRecord: a signed record verifies, an edited one does not, a record without sig does not', lib.verifyRecord(rec, sd) && !lib.verifyRecord({ ...rec, a: 2 }, sd) && !lib.verifyRecord({ a: 1, b: 'x' }, sd) && !lib.verifyRecord(rec, path.join(tmp, 'other_state')));
}

console.log('gates3 hooks, part 3: a deleted builder record does not cancel the debt (auditgate audit fault 6)');
{
  const al = await import(pathToFileURL(path.join(src, 'tools/hooks/audit_lib.mjs')).href);
  const rt = path.join(tmp, 'debt'); fs.mkdirSync(path.join(rt, '.claude/state/builders'), { recursive: true });
  const sd = path.join(rt, '.claude/state');
  const folder = path.join(tmp, 'wtdebt', 'docs', 'progress', 'debt'); fs.mkdirSync(folder, { recursive: true });
  const B = Date.UTC(2026, 0, 1, 12, 0, 0);
  const rec = al.recordBuilder(folder, 'db1', 'general-purpose', sd, B).rec;
  fs.appendFileSync(path.join(sd, 'gate.log'), JSON.stringify({ ts: new Date(B).toISOString(), gate: 'audit', decision: 'owed', reason: 'audit owed: hand-in passed', handin_dir: folder, builder_id: 'db1', builder_type: 'general-purpose' }) + '\n');
  t('owed while the builder record exists', al.auditOwed(sd, rt).length === 1);
  fs.rmSync(path.join(sd, 'builders', al.builderKey(folder) + '.json'));
  const owed = al.auditOwed(sd, rt);
  t('the builder record DELETED: still owed (the "audit owed" log line stands in), builder id taken from the log', owed.length === 1 && owed[0].builder_id === 'db1', JSON.stringify(owed));
  const text = 'Auditor agent: da1\nVerdict: accept\nReferences opened: 1 of 1\n\n## Faults, ranked\n\nthe kerb line in render r3 misses the photo by one lane width at the junction\n';
  fs.writeFileSync(path.join(folder, 'AUDIT.md'), text); fs.utimesSync(path.join(folder, 'AUDIT.md'), new Date(B + 60000), new Date(B + 60000));
  t('a typed AUDIT.md does not clear it', al.auditOwed(sd, rt).length === 1);
  al.recordAudit(folder, { auditorId: 'da1', builderId: 'db1', verdict: 'accept', text }, sd, B + 120000);
  t('the signed audit record clears it', al.auditOwed(sd, rt).length === 0, JSON.stringify(al.auditOwed(sd, rt)));
  fs.rmSync(folder, { recursive: true, force: true });
  fs.rmSync(path.join(sd, 'audits', al.builderKey(folder) + '.json'), { force: true });
  t('a progress folder that no longer exists anywhere is not listed (nothing left to audit; documented limit)', al.auditOwed(sd, rt).length === 0);
}

console.log('gates3 hooks, part 4: HANDIN_DIR in the prompt, HANDIN: no-visual only in the BRIEF-FILE (this round\'s own hand-in was refused for it)');
{
  const round = 'nvfile';
  const briefText = mk(round, ['--no-visual']);   // holds HANDIN_DIR, HANDIN: no-visual (and the contract)
  const bfile = path.join(tmp, 'nvfile_brief.txt'); fs.writeFileSync(bfile, briefText);
  const hdir = hd(round); fs.mkdirSync(hdir, { recursive: true }); fs.writeFileSync(path.join(hdir, 'STATUS.md'), goodStatus);
  const prompt = `Read the brief file and follow it.\nBRIEF-FILE: ${bfile}\nHANDIN_DIR: ${hdir}\n`;
  const nv = stop('nv1', transcript('nvfile', prompt));
  t('prompt with HANDIN_DIR + BRIEF-FILE that says "HANDIN: no-visual": the engine-only check applies (no COMPARE.md demanded), stop allowed', nv.code === 0 && nv.out === '' && lastLog().reason === 'hand-in check passed', nv.out);
  fs.rmSync(path.join(hdir, 'STATUS.md'));
  const nv2 = stop('nv2', transcript('nvfile2', prompt));
  t('...and the check still runs: a missing STATUS.md blocks, without a COMPARE.md complaint', J(nv2.out).decision === 'block' && /STATUS\.md is missing/.test(J(nv2.out).reason) && !/COMPARE\.md is missing/.test(J(nv2.out).reason), nv2.out);
}

console.log(`\n${pass} passed, ${fail} failed (temp dir ${tmp})`);
if (failedNames.length) console.log('FAILED TESTS:\n - ' + failedNames.join('\n - '));
if (!process.env.KEEP_HOOKTEST) fs.rmSync(tmp, { recursive: true, force: true });
process.exit(fail ? 1 : 0);
