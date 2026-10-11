#!/usr/bin/env node
// Unit tests for the three hooks with synthetic stdin JSON. Usage: node tools/hooks/test_hooks.mjs
// Copies the hook files, check_handin.mjs, make_brief.mjs, the contract and templates into a temp "main checkout" and
// runs every script as a child process from a different cwd, as the harness would. Exit 1 if any test fails.
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { spawnSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';

const src = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..', '..');
const tmp = fs.mkdtempSync(path.join(os.tmpdir(), 'hooktest-'));
const root = path.join(tmp, 'main');
const wt = path.join(tmp, 'wt');
const cp = (rel) => { const d = path.join(root, rel); fs.mkdirSync(path.dirname(d), { recursive: true }); fs.copyFileSync(path.join(src, rel), d); };
['tools/hooks/lib.mjs', 'tools/hooks/brief_gate.mjs', 'tools/hooks/handin_gate.mjs', 'tools/hooks/contract_inject.mjs',
  'tools/check_handin.mjs', 'tools/make_brief.mjs', 'docs/AGENT_CONTRACT.md', 'docs/templates/BRIEF.md'].forEach(cp);
// three reference files in the temp main checkout
for (const f of ['aerial/a.jpg', 'street/b.jpg', 'gsv/c.jpg']) { const p = path.join(root, 'docs/reference', f); fs.mkdirSync(path.dirname(p), { recursive: true }); fs.writeFileSync(p, 'x'); }
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

console.log(`\n${pass} passed, ${fail} failed (temp dir ${tmp})`);
if (!process.env.KEEP_HOOKTEST) fs.rmSync(tmp, { recursive: true, force: true });
process.exit(fail ? 1 : 0);
