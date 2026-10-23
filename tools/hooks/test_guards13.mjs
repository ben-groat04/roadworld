#!/usr/bin/env node
// gates13: real-payload tables for the send-back of round gates12 (docs/progress/gates12/AUDIT.md and decisions 1-8 of the main session).
//   H  decision 8: no hook builds a regular expression from text it did not write. The command shape that crashed state_guard (FAIL-OPEN, 23 Oct 22:02): an agent's heredoc of C++ source with
//      unbalanced [ and { and escaped quotes, plus glob words naming the state folder. Every wired hook gets it; the decision is MADE (no FAIL-OPEN, no crash line in the isolated gate.log).
// Where the expected values come from: the main-session decisions in the gates13 brief and the live gate.log line of 23 Oct 22:02:37 (SyntaxError: Invalid regular expression ... Unterminated character
// class). Nothing pins a fault. No test writes the live gate.log (CP_GATE_STATE_DIR; isolation check at the end).
// Usage: node tools/hooks/test_guards13.mjs
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { spawnSync, spawn } from 'node:child_process';
import { fileURLToPath } from 'node:url';
import { isolateState, cleanupTmp } from '../test_isolation.mjs';

const src = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..', '..');
const tmp = fs.mkdtempSync(path.join(os.tmpdir(), 'guards13-'));
const isolation = isolateState(tmp, src);
let pass = 0, fail = 0; const failedNames = [];
const t = (name, cond, detail = '') => { if (cond) { pass++; console.log('  ok   ' + name); } else { fail++; failedNames.push(name); console.log('  FAIL ' + name + (detail ? '\n       ' + String(detail).slice(0, 600) : '')); } };
const mk = (p) => fs.mkdirSync(p, { recursive: true });
const gateLog = () => { try { return fs.readFileSync(path.join(isolation.stateDir, 'gate.log'), 'utf8'); } catch { return ''; } };

// ---------------------------------------------------------------- H: the crash shape against every wired hook
const settings = JSON.parse(fs.readFileSync(path.join(src, '.claude', 'settings.json'), 'utf8'));
const wired = new Set(); (function walk(o) { if (o && typeof o === 'object') { if (Array.isArray(o.args)) { for (const a of o.args) { const m = String(a).match(/tools[\\/]hooks[\\/]([a-z_]+\.mjs)/); if (m) wired.add(m[1]); } } Object.values(o).forEach(walk); } })(settings.hooks || {});
t('settings.json wires at least the ten known hooks', wired.size >= 10, [...wired].join(' '));

const CPP = [
  'cat > src/thing.cpp <<\'EOF\'',
  '#include <vector>',
  'static const char* kNames[] = { "a\\"b", "c[0" };',
  'int f(int x) { return kNames[x][ 0 ] == \'[\' ? x[ : 0; }',
  'auto g = [&](int a) { return a < 3 ? "\\"quoted\\" [" : "}{"; };',
  'EOF',
  'ls .claude/state/*[a ; cat .claude/state/x[ ; rm .claude/state/[z-a].txt ; cat .claude/state/ack_secr[et ; cp .claude/state/gate.l?g[ /tmp/x',
].join('\n');
const shapes = [
  ['Bash heredoc of C++ with [ { \\" and state globs', 'Bash', { command: CPP }],
  ['Bash: a state-folder word with a lone [', 'Bash', { command: 'cat .claude/state/foo[bar && echo done' }],
  ['Bash: a delete glob with a reversed range', 'Bash', { command: 'rm -f build/[z-a]*.o ../x/[' }],
  ['Write of C++ source', 'Write', { file_path: path.join(tmp, 'x', 'thing.cpp'), content: CPP }],
  ['Edit with C++ in old/new text', 'Edit', { file_path: path.join(tmp, 'x', 'thing.cpp'), old_string: 'a[', new_string: CPP }],
  ['Agent launch whose prompt is the C++ heredoc', 'Agent', { description: 'Builder round x', subagent_type: 'general-purpose', prompt: CPP, run_in_background: true }],
  ['Grep of a pattern with [', 'Grep', { pattern: 'a[', path: path.join(tmp, '.claude', 'state', '[x') }],
];
const events = ['PreToolUse', 'PostToolUse'];
fs.writeFileSync(path.join(tmp, 'none.jsonl'), JSON.stringify({ type: 'user', message: { role: 'user', content: 'Builder text only' } }) + '\n');
for (const hook of ['brief_gate.mjs', 'contract_inject.mjs', 'delete_guard.mjs', 'gate_notice.mjs', 'git_guard.mjs', 'handin_gate.mjs', 'lock_guard.mjs', 'ref_log.mjs', 'state_guard.mjs', 'time_guard.mjs']) {
  mk(path.join(tmp, 'x'));
  let crashed = []; let runs = 0;
  for (const [label, tool, ti] of shapes) for (const ev of events) for (const agent of [true, false]) {
    const before = gateLog().length;
    const payload = { hook_event_name: ev, tool_name: tool, tool_input: ti, tool_response: { output: '' }, cwd: tmp, session_id: 's13', transcript_path: path.join(tmp, 'none.jsonl'), ...(agent ? { agent_id: 'h13a', agent_type: 'general-purpose' } : {}) };
    const r = spawnSync(process.execPath, [path.join(src, 'tools', 'hooks', hook)], { input: JSON.stringify(payload), encoding: 'utf8', env: { ...process.env, CP_GATE_STATE_DIR: isolation.stateDir }, timeout: 30000 });
    runs++;
    const added = gateLog().slice(before);
    const bad = /FAIL-OPEN: hook implementation could not load or crashed|"error":"[^"]*(?:Error|Unterminated)|SyntaxError|Invalid regular expression|Unterminated|TypeError|ReferenceError/.test(added) || /SyntaxError|Invalid regular expression|Unterminated/.test(r.stderr || '') || r.status === null || (r.status !== 0 && r.status !== 2);
    if (bad) crashed.push(`${label} / ${ev} / ${agent ? 'agent' : 'main'}: exit ${r.status} ${(added.match(/FAIL-OPEN[^"]*|SyntaxError[^"]*/) || [''])[0].slice(0, 120)} ${String(r.stderr || '').slice(0, 100)}`);
  }
  t(`H ${hook}: all ${runs} runs of the crash shapes decide (no crash, no FAIL-OPEN, no regex error)`, crashed.length === 0, crashed.slice(0, 4).join('\n'));
}

// no hook builds a RegExp from input-derived text by hand: every new RegExp( in a hook either has a literal/constant source or goes through reEsc / safeRe
const hookFiles = fs.readdirSync(path.join(src, 'tools', 'hooks')).filter((f) => f.endsWith('.mjs') && !f.startsWith('test_'));
const offenders = [];
for (const f of hookFiles) fs.readFileSync(path.join(src, 'tools', 'hooks', f), 'utf8').split('\n').forEach((line, i) => {
  if (!/new RegExp\(/.test(line) || /^\s*(\/\/|const RE_|const [A-Z_]+ = new RegExp)/.test(line)) return;
  if (/reEsc\(|safeRe\(/.test(line)) return;
  if (/NO_CONTRACT_CATEGORIES|String\.raw|\$\{file\}|'\^' \+ key|LEAD \+|WORD \+|ORDER \+/.test(line)) return;   // constant sources
  offenders.push(`${f}:${i + 1}`);
});
t('H no hook builds a RegExp from other text without reEsc/safeRe', offenders.length === 0, offenders.join(' '));
const lib = await import(new URL('./lib.mjs', import.meta.url).href);
t('H reEsc makes any text match itself literally; safeRe never throws', ['a[', ']', 'x{1', '\\"q\\"', '*?', '[z-a]', '(', '-'].every((s) => new RegExp('^' + lib.reEsc(s) + '$').test(s)) && lib.safeRe('a[').test('zzz') === false, '');

// ================================================================ shared helpers for the sections below
const readJson = (f) => JSON.parse(fs.readFileSync(f, 'utf8'));
const mkdirs = (p) => fs.mkdirSync(p, { recursive: true });
const normS = (p) => String(p).replace(/\\/g, '/').toLowerCase();
const envFor = (stateDir, extra = {}) => { const e = { ...process.env, CP_GATE_STATE_DIR: stateDir, CP_TIME_GUARD_PROCS: '' }; delete e.CP_MAIN_ROOT; delete e.CP_TIME_GUARD_PROC_CMD; delete e.CP_TIME_GUARD_PATHS; delete e.CP_TIME_GUARD_PATH_CMD; for (const [k, v] of Object.entries(extra)) { if (v === null) delete e[k]; else e[k] = v; } return e; };
const hookRun = (hook, payload, stateDir = isolation.stateDir, extra = {}) => spawnSync(process.execPath, [path.join(src, 'tools', 'hooks', hook)], { input: JSON.stringify(payload), encoding: 'utf8', env: envFor(stateDir, extra), timeout: 60000 });
const stateLines = (dir) => { try { return fs.readFileSync(path.join(dir, 'gate.log'), 'utf8').trim().split('\n').filter(Boolean).map((l) => JSON.parse(l)); } catch { return []; } };
const gitq = (cwd, ...a) => spawnSync('git', a, { cwd, encoding: 'utf8', env: { ...process.env, GIT_AUTHOR_NAME: 't', GIT_AUTHOR_EMAIL: 't@t', GIT_COMMITTER_NAME: 't', GIT_COMMITTER_EMAIL: 't@t' } });

// ================================================================ B  decision 1: launch allow-list
console.log('gates13 B: launches by allow-list (decision 1): contract / BRIEF-FILE / AUDIT-OF / FIRSTLOOK-OF / NO-CONTRACT <category>: <reason>; free text refused whatever its wording');
{
  const elsewhere = path.join(tmp, 'elsewhere'); mkdirs(elsewhere);
  const ds = path.join(tmp, 'state-b'); mkdirs(ds);
  const launch = (description, prompt, type = 'general-purpose') => hookRun('brief_gate.mjs', { hook_event_name: 'PreToolUse', tool_name: 'Agent', session_id: 's13', cwd: elsewhere, tool_input: { description, prompt, subagent_type: type } }, ds);
  const catTable = readJson(path.join(src, 'tools', 'hooks', 'live_launch_categories.json')).rows;
  t('B the live table holds all 77 real descriptions, each with a launch form', catTable.length === 77 && catTable.every(([d, c, w]) => d && c && w), String(catTable.length));
  const allowCats = ['clerk', 'merge', 'research', 'status', 'measure'];
  const honest = catTable.filter(([, c]) => allowCats.includes(c));
  const badAllow = honest.filter(([d, c, why]) => { const r = launch(d, `NO-CONTRACT: ${c}: ${why}\nDo the job.`); return r.status !== 0; }).map(([d]) => d);
  t(`B every live clerk / merge / research / status description (${honest.length}) is ALLOWED with its category and its real reason`, badAllow.length === 0 && honest.length >= 35, badAllow.join(' | '));
  const reviews = catTable.filter(([, c]) => c === 'AUDIT-OF' || c === 'FIRSTLOOK-OF');
  const badRefuse = reviews.filter(([d, , why]) => { const r = launch(d, `NO-CONTRACT: ${why}\nDo the job.`); return !(r.status === 2 && /free-text NO-CONTRACT form is not accepted/.test(r.stderr) && /clerk, merge, research, status, measure/.test(r.stderr) && /AUDIT-OF/.test(r.stderr) && /FIRSTLOOK-OF/.test(r.stderr)); }).map(([d]) => d);
  t(`B every live audit and first-look description (${reviews.length}), launched as free-text NO-CONTRACT, is REFUSED and the text names the categories and AUDIT-OF / FIRSTLOOK-OF`, badRefuse.length === 0 && reviews.length === 41, badRefuse.join(' | '));
  // the auditor's 13 evasions of gates12 (AUDIT.md item 4) plus the earlier phrasings: refused whatever the words, because free text is refused
  const evasions = ['Check gates12 hand-in quality', 'Does the gates11 round hold up', 'Sanity-check the grade-r12 composites', 'Inspect grade-r12 renders', 'Critique the junction frames', 'Evaluate gates12 work', 'Opus eyes on grade-r12 images', 'Proof-read gates9 hand-in', 'QA the gates12 round', 'Cross-check builder claims in gates9', 'Judge grade-r12 renders vs photos', 'Vet the gates12 hand-in', 'Assess round gates12', 'Auditing gates9', 'Second-opinion reviewer of gates11'];
  const evaded = evasions.filter((d) => { const r = launch(d, 'NO-CONTRACT: a quick read for the main session, nothing else about it\nDo the job.'); return r.status !== 2; });
  t(`B the audit's ${evasions.length} evasion descriptions as free-text NO-CONTRACT are all REFUSED`, evaded.length === 0, evaded.join(' | '));
  const r1 = launch('Docs', 'NO-CONTRACT: clerk: short');
  t('B a category reason under 20 characters is refused (names the 20-character rule)', r1.status === 2 && /under 20 characters/.test(r1.stderr), r1.stderr.slice(0, 200));
  for (const bad of ['audit', 'review', 'first-look', 'builder', 'CLERK JOB', 'clerks']) { const r = launch('Job', `NO-CONTRACT: ${bad}: independent look at the finished round of another agent\nGo.`); t(`B category "${bad}" is not on the list: refused as free text`, r.status === 2 && /clerk, merge, research, status, measure/.test(r.stderr), r.stderr.slice(0, 160)); }
  const r2 = launch('Docs', 'NO-CONTRACT: Clerk: update docs/QUEUE.md and ACCEPTANCE.md round states, no code at all\nGo.');
  t('B the category is case-insensitive ("Clerk:") and a good reason is allowed', r2.status === 0, r2.stderr.slice(0, 200));
  t('B the allow log line carries the category', stateLines(ds).some((l) => l.no_contract_category === 'clerk' && l.reason === 'NO-CONTRACT'), JSON.stringify(stateLines(ds).slice(-2)).slice(0, 300));
  const r3 = launch('Job', 'NO-CONTRACT: clerk: write the AUDIT.md of the gates12 hand-in for me, please\nGo.');
  t('B a categorised launch is still refused when it asks for AUDIT.md to be written', r3.status === 2, r3.stderr.slice(0, 200));
  const hdA = path.join(tmp, 'fl_b', 'docs', 'progress', 'fl'); mkdirs(hdA);
  const r4 = launch('Check gates12 hand-in quality', `AUDIT-OF: ${hdA}\nAGENT-CONTRACT-MARKER: v1\n## Owner's words\n"x" (1 Oct)\n`);
  t('B ... while the same vague description with AUDIT-OF is allowed (the real audit launch)', r4.status === 0, r4.stderr.slice(0, 200));
  const r5 = launch('Opus eyes on grade-r12 images', `FIRSTLOOK-OF: ${hdA}\nAGENT-CONTRACT-MARKER: v1\n## Owner's words\n"x" (1 Oct)\n`);
  t('B ... and with FIRSTLOOK-OF', r5.status === 0, r5.stderr.slice(0, 200));
  const r6 = launch('Just a job', 'Do the thing.');
  t('B no form at all: refused as before', r6.status === 2, r6.stderr.slice(0, 200));
  // the code no longer reads review words from a launch
  const bg = fs.readFileSync(path.join(src, 'tools', 'hooks', 'brief_gate_impl.mjs'), 'utf8');
  t('B brief_gate_impl.mjs no longer imports or calls disguisedJob (no vocabulary list)', !/disguisedJob/.test(bg.replace(/\/\/.*$/gm, '')));
}

// ================================================================ C  decision 2: the suite record is written by tools/run_suite.mjs at the run END
console.log('gates13 C: tools/run_suite.mjs and the build-phase record (decision 2)');
{
  const rs = path.join(src, 'tools', 'run_suite.mjs');
  const ds = path.join(tmp, 'state-c'); mkdirs(ds);
  const sr = await import(new URL('./suite_record.mjs', import.meta.url).href);
  const mkWt = (name) => { const w = path.join(tmp, name); mkdirs(path.join(w, '.git')); mkdirs(path.join(w, 'tests')); return w; };
  const wt = mkWt('c_wt');
  const fake = (w, body, name = 'test_fake_suite.mjs') => { const f = path.join(w, 'tests', name); fs.writeFileSync(f, body); return f; };
  const run = (w, args, e = {}) => spawnSync(process.execPath, [rs, w, ...args], { encoding: 'utf8', env: envFor(ds, e), timeout: 120000 });
  const okScript = fake(wt, "process.stdout.write('[doctest] test cases: 4 | 4 passed | 0 failed | 0 skipped\\n');");
  let r = run(wt, ['--script', okScript, '--round', 'rc']);
  const logP = path.join(wt, 'docs', 'progress', 'rc', 'suite_node.log');
  t('C an honest FOREGROUND run: exit 0, the log is written where the round says', r.status === 0 && fs.existsSync(logP) && /4 passed/.test(fs.readFileSync(logP, 'utf8')), r.stdout + r.stderr);
  const recFile = path.join(ds, 'suite_records.jsonl');
  const last = () => { const ls = fs.readFileSync(recFile, 'utf8').trim().split('\n'); return JSON.parse(ls[ls.length - 1]); };
  t('C the record is written at the END of the run (end time at or after start, exit, log hash, worktree, binary)', (() => { const o = last(); return o.exit === 0 && Date.parse(o.end) >= Date.parse(o.start) && /^[0-9a-f]{64}$/.test(o.logSha) && normS(o.worktree) === normS(wt) && normS(o.binary) === normS(okScript); })(), fs.readFileSync(recFile, 'utf8').slice(-300));
  let f1 = sr.findSuiteRun(logP, wt, { after: 0, stateDir: ds });
  t('C findSuiteRun accepts the honest foreground run', f1.ok === true, JSON.stringify(f1));
  // background: the same record, written by the runner when it ends, not by a hook when the call returns
  const slow = fake(wt, "setTimeout(() => { process.stdout.write('[doctest] test cases: 2 | 2 passed | 0 failed | 0 skipped\\n'); }, 1500);", 'test_slow_suite.mjs');
  const child = spawn(process.execPath, [rs, wt, '--script', slow, '--round', 'rbg'], { env: envFor(ds), stdio: 'ignore', detached: true }); child.unref();
  const bgLog = path.join(wt, 'docs', 'progress', 'rbg', 'suite_node.log');
  await new Promise((res) => setTimeout(res, 300));
  const early = sr.findSuiteRun(bgLog, wt, { after: 0, stateDir: ds });
  t('C a BACKGROUND run that has not ended has no record yet (the log is not accepted half-written)', early.ok === false, JSON.stringify(early));
  for (let i = 0; i < 60 && !(sr.findSuiteRun(bgLog, wt, { after: 0, stateDir: ds }).ok); i++) await new Promise((res) => setTimeout(res, 250));
  const bgOk = sr.findSuiteRun(bgLog, wt, { after: 0, stateDir: ds });
  t('C ... and once it ends the record matches the finished log (a background run works)', bgOk.ok === true && /2 passed/.test(fs.readFileSync(bgLog, 'utf8')), JSON.stringify(bgOk));
  // forgeries
  const typed = path.join(wt, 'docs', 'progress', 'rt', 'suite_node.log'); mkdirs(path.dirname(typed));
  fs.writeFileSync(typed, '[doctest] test cases: 9 | 9 passed | 0 failed | 0 skipped\n');
  t('C a log TYPED by hand has no record: refused', sr.findSuiteRun(typed, wt, { after: 0, stateDir: ds }).ok === false);
  const eNode = spawnSync(process.execPath, ['-e', "console.log('[doctest] test cases: 9 | 9 passed | 0 failed | 0 skipped')"], { encoding: 'utf8' });
  const printed = path.join(wt, 'docs', 'progress', 'rp', 'suite_node.log'); mkdirs(path.dirname(printed)); fs.writeFileSync(printed, eNode.stdout);
  t('C a log printed by `node -e` has no record: refused (the 23 Oct forgery)', sr.findSuiteRun(printed, wt, { after: 0, stateDir: ds }).ok === false);
  const edited = path.join(wt, 'docs', 'progress', 'rc', 'suite_node.log'); fs.appendFileSync(edited, '[doctest] test cases: 9 | 9 passed | 0 failed\n');
  const fe = sr.findSuiteRun(edited, wt, { after: 0, stateDir: ds });
  t('C an EDITED log is refused (hash mismatch)', fe.ok === false && /hash mismatch/.test(fe.why), JSON.stringify(fe));
  const other = mkWt('c_other');
  const fo = sr.findSuiteRun(bgLog, other, { after: 0, stateDir: ds });
  t('C a record of ANOTHER worktree is refused', fo.ok === false && /another worktree/.test(fo.why), JSON.stringify(fo));
  // binary outside the worktree
  const outside = path.join(tmp, 'test_outside_suite.mjs'); fs.writeFileSync(outside, "console.log('x')");
  const ro = run(wt, ['--script', outside, '--round', 'ro']);
  t('C run_suite refuses a script outside the worktree (no run, no record)', ro.status !== 0 && /not inside the worktree|does not exist/.test(ro.stderr), ro.stderr);
  const recs = fs.readFileSync(recFile, 'utf8').trim().split('\n').map(JSON.parse);
  const ds2 = path.join(tmp, 'state-c2'); mkdirs(ds2);
  const lib = await import(new URL('./lib.mjs', import.meta.url).href);
  const forged = { ...recs[0] }; delete forged.sig; forged.binary = outside; forged.log = logP;
  fs.writeFileSync(path.join(ds2, 'suite_records.jsonl'), JSON.stringify(lib.signRecord(forged, ds2)) + '\n');
  const fb = sr.findSuiteRun(logP, wt, { after: 0, stateDir: ds2 });
  t('C a record naming a binary OUTSIDE the worktree is refused', fb.ok === false && /not inside the worktree|differs|hash/.test(fb.why), JSON.stringify(fb));
  const forged2 = { ...recs[0] }; forged2.binary = outside;   // edited record, old signature
  fs.writeFileSync(path.join(ds2, 'suite_records.jsonl'), JSON.stringify(forged2) + '\n');
  const fsig = sr.findSuiteRun(logP, wt, { after: 0, stateDir: ds2 });
  t('C a record edited after signing does not verify', fsig.ok === false, JSON.stringify(fsig));
  const stale = sr.findSuiteRun(bgLog, wt, { after: Date.now() + 60000, stateDir: ds });
  t('C a run that ended BEFORE the last code commit is refused', stale.ok === false && /before the last code commit/.test(stale.why), JSON.stringify(stale));
  // a filtered run never counts as the suite
  const okScript2 = fake(wt, "process.stdout.write('[doctest] test cases: 1 | 1 passed | 0 failed | 0 skipped\\n');", 'test_fake_suite2.mjs');
  const rf = run(wt, ['--script', okScript2, '--filter', 'x', '--round', 'rf']);
  const fl = sr.findSuiteRun(path.join(wt, 'docs', 'progress', 'rf', 'suite_node.filter.log'), wt, { after: 0, stateDir: ds });
  t('C a FILTERED run is logged as suite_<x>.filter.log and never counts as the suite', rf.status === 0 && fl.ok === false && /filtered run/.test(fl.why), JSON.stringify(fl));
  // limits
  const rt = run(wt, ['--script', okScript, '--timeout', '901', '--round', 'rx']);
  t('C --timeout over 900 is refused (contract item 26), nothing runs', rt.status === 2 && /900/.test(rt.stderr) && !fs.existsSync(path.join(wt, 'docs', 'progress', 'rx')), rt.stderr);
  const hang = fake(wt, 'setTimeout(() => {}, 60000);', 'test_hang_suite.mjs');
  const t0 = Date.now(); const rh = run(wt, ['--script', hang, '--timeout', '2', '--round', 'rh']);
  t('C a hung suite is killed at its limit: exit 124 and the record says 124', rh.status === 124 && Date.now() - t0 < 30000 && last().exit === 124, `${rh.status} ${rh.stderr}`);
  const hl = path.join(wt, 'docs', 'progress', 'rh', 'suite_node.log');
  t('C ... and the log of a timed-out run is never accepted as a passed suite (it says exit=124)', /exit=124/.test(fs.readFileSync(hl, 'utf8')));
  // an agent cannot write the record file (state_guard) and the post hooks are gone
  const sgP = (command, agent = 'agX') => hookRun('state_guard.mjs', { hook_event_name: 'PreToolUse', tool_name: 'Bash', tool_input: { command }, cwd: tmp, agent_id: agent, agent_type: 'general-purpose', session_id: 's13' }, ds);
  const stateRel = '.claude/state/suite_records.jsonl';
  for (const [label, cmd] of [['redirect', `echo '{"v":1}' >> ${stateRel}`], ['tee', `echo x | tee -a ${stateRel}`], ['cp onto it', `cp fake.jsonl ${stateRel}`], ['sed -i', `sed -i s/a/b/ ${stateRel}`], ['rm', `rm ${stateRel}`], ['absolute path', `echo x >> ${path.join(tmp, '.claude', 'state', 'suite_records.jsonl').replace(/\\/g, '/')}`]]) {
    const rr = sgP(cmd); t(`C REFUSE: an agent writing suite_records.jsonl (${label})`, rr.status === 2, `status ${rr.status} ${rr.stderr.slice(0, 160)}`);
  }
  const wr1 = hookRun('state_guard.mjs', { hook_event_name: 'PreToolUse', tool_name: 'Write', tool_input: { file_path: path.join(tmp, '.claude', 'state', 'suite_records.jsonl'), content: '{}' }, cwd: tmp, agent_id: 'agX', agent_type: 'general-purpose', session_id: 's13' }, ds);
  t('C REFUSE: the Write tool on suite_records.jsonl', wr1.status === 2, `status ${wr1.status} ${wr1.stderr.slice(0, 160)}`);
  const sg2 = hookRun('state_guard.mjs', { hook_event_name: 'PreToolUse', tool_name: 'Bash', tool_input: { command: `cat ${stateRel}` }, cwd: tmp, agent_id: 'agX', agent_type: 'general-purpose', session_id: 's13' }, ds);
  t('C REFUSE: even reading the record file by name is not offered to an agent (only the lock and gate.log are readable)', sg2.status === 2, `status ${sg2.status}`);
  const sg3 = hookRun('state_guard.mjs', { hook_event_name: 'PreToolUse', tool_name: 'Bash', tool_input: { command: `echo x >> ${stateRel}` }, cwd: tmp, session_id: 's13' }, ds);
  t('C ALLOW: the main session (no agent id) is not limited by state_guard', sg3.status === 0, `status ${sg3.status}`);
  t('C the PostToolUse recording is gone: no time_guard_post files, no PostToolUse time guard in settings.json', !fs.existsSync(path.join(src, 'tools', 'hooks', 'time_guard_post.mjs')) && !fs.existsSync(path.join(src, 'tools', 'hooks', 'time_guard_post_impl.mjs')) && !/time_guard_post/.test(fs.readFileSync(path.join(src, '.claude', 'settings.json'), 'utf8')));
  t('C make_brief build-phase text names tools/run_suite.mjs and no longer asks for a "Suite binary:" line', (() => { const m = fs.readFileSync(path.join(src, 'tools', 'make_brief.mjs'), 'utf8'); return /node tools\/run_suite\.mjs/.test(m) && !/Suite binary: <path/.test(m); })());
}

// ================================================================ D  decision 3: the worktree comes from the command, not the cwd
console.log('gates13 D: the time guard takes each job\'s worktree from the command (cd, absolute program path, run_suite argument); cwd = the main checkout');
{
  const mainD = path.join(tmp, 'd_main'); mkdirs(path.join(mainD, '.git'));
  const wtA = path.join(tmp, 'd_wtA'); mkdirs(path.join(wtA, '.git', '')); const wtB = path.join(tmp, 'd_wtB'); mkdirs(path.join(wtB, '.git'));
  const exeA = path.join(wtA, 'build', 'tests', 'sim_core_tests.exe'), exeB = path.join(wtB, 'build', 'tests', 'sim_core_tests.exe');
  const tgd = (command, { cwd = mainD, stateDir, id = 'i' + Math.random().toString(36).slice(2), e = {}, tool = 'Bash' } = {}) => hookRun('time_guard.mjs', { hook_event_name: 'PreToolUse', tool_name: tool, session_id: 's13', cwd, agent_id: 'a1', tool_use_id: id, tool_input: { command } }, stateDir, e);
  const mkState = (n) => { const d = path.join(tmp, 'state-d-' + n); mkdirs(d); return d; };
  const runsRoots = (d) => { try { return fs.readFileSync(path.join(d, 'runs.jsonl'), 'utf8').split('\n').filter(Boolean).map(JSON.parse).filter((o) => o.ev === 'start').map((o) => normS(o.root)); } catch { return []; } };
  const refused = (r) => r.status === 2 && /REFUSED by the time-limit guard/.test(r.stderr);
  const cwdF = (s) => s.replace(/\\/g, '/');
  let d1 = mkState('cd');
  let r = tgd(`cd "${cwdF(wtA)}" && timeout 900 build/tests/sim_core_tests.exe > s.log 2>&1`, { stateDir: d1 });
  t('D `cd <worktree> && timeout 900 <suite>` from cwd = main: allowed, recorded under the WORKTREE root', r.status === 0 && runsRoots(d1).includes(normS(wtA)) && !runsRoots(d1).includes(normS(mainD)), `${r.status} ${r.stderr} ${runsRoots(d1)}`);
  let d2 = mkState('abs');
  r = tgd(`timeout 900 "${cwdF(exeA)}" > s.log 2>&1`, { stateDir: d2 });
  t('D an absolute program path inside a worktree sets that worktree as the root (cwd = main)', r.status === 0 && runsRoots(d2).includes(normS(wtA)) && !runsRoots(d2).includes(normS(mainD)), `${r.status} ${r.stderr} ${runsRoots(d2)}`);
  let d3 = mkState('rs');
  r = tgd(`node tools/run_suite.mjs "${cwdF(wtA)}"`, { stateDir: d3 });
  t('D `node tools/run_suite.mjs <worktree>` takes the worktree from its argument', r.status === 0 && runsRoots(d3).includes(normS(wtA)), `${r.status} ${r.stderr} ${runsRoots(d3)}`);
  let d4 = mkState('cdgit');
  r = tgd(`cd "${cwdF(wtA)}" && git -C "${cwdF(wtB)}" status && timeout 900 build/tests/sim_core_tests.exe`, { stateDir: d4 });
  t('D `cd A && git -C B status && timeout 900 suite`: the suite job keeps A (the later git -C does not move it)', r.status === 0 && runsRoots(d4).includes(normS(wtA)) && !runsRoots(d4).includes(normS(wtB)), `${r.status} ${r.stderr} ${runsRoots(d4)}`);
  // second copy decided per worktree even though every call starts in main
  let d5 = mkState('dup');
  tgd(`cd "${cwdF(wtA)}" && timeout 900 build/tests/sim_core_tests.exe > s.log 2>&1`, { stateDir: d5, id: 'first' });
  const running = { CP_TIME_GUARD_PROCS: 'sim_core_tests', CP_TIME_GUARD_PATHS: exeA };
  r = tgd(`cd "${cwdF(wtA)}" && timeout 900 build/tests/sim_core_tests.exe > s2.log 2>&1`, { stateDir: d5, id: 'second', e: running });
  t('D REFUSE: a second copy in the same worktree, both calls started from the main checkout', refused(r) && /second copy/.test(r.stderr) && r.stderr.includes(path.resolve(exeA)), `${r.status} ${r.stderr.slice(0, 300)}`);
  r = tgd(`cd "${cwdF(wtB)}" && timeout 900 build/tests/sim_core_tests.exe > s2.log 2>&1`, { stateDir: d5, id: 'third', e: running });
  t('D ALLOW: the same image runs in worktree A, the new run is in worktree B', r.status === 0, `${r.status} ${r.stderr.slice(0, 300)}`);
  r = tgd(`timeout 900 build/tests/sim_core_tests.exe > s2.log 2>&1`, { stateDir: d5, id: 'fourth', e: running });
  t('D ALLOW: a run with no cd and no absolute path belongs to the cwd (main), not to worktree A', r.status === 0, `${r.status} ${r.stderr.slice(0, 300)}`);
  r = tgd(`cd "${cwdF(wtA)}" && build/tests/sim_core_tests.exe > s2.log 2>&1`, { stateDir: d5, id: 'fifth' });
  t('D REFUSE: no limit is still refused after a cd', refused(r) && /timeout 900/.test(r.stderr), `${r.status} ${r.stderr.slice(0, 200)}`);
  const prior = hookRun('time_guard.mjs', { hook_event_name: 'PreToolUse', tool_name: 'Bash', session_id: 's13', cwd: wtA, agent_id: 'a1', tool_use_id: 'cwd-form', tool_input: { command: 'timeout 900 build/tests/sim_core_tests.exe > s.log 2>&1' } }, mkState('cwdform'));
  t('D the old form (cwd inside the worktree, no cd) still works', prior.status === 0, prior.stderr);
}

// ================================================================ E  decision 4: the time guard by position
console.log('gates13 E: the time guard knows any prefix runner by position (taskset, nice, env, stdbuf, time, chrt, start, cmd /c, powershell -c, bash -c)');
{
  const ds = path.join(tmp, 'state-e'); mkdirs(ds);
  const tge = (command, tool = 'Bash') => hookRun('time_guard.mjs', { hook_event_name: 'PreToolUse', tool_name: tool, session_id: 's13', cwd: tmp, agent_id: 'a1', tool_use_id: 'e' + Math.random().toString(36).slice(2), tool_input: { command } }, ds);
  const refuse = ['taskset -c 0 ninja', 'taskset 0x3 ninja -C build', 'nice -n 5 ctest', 'nice ninja', 'env X=1 ninja', 'env -u A ninja', 'stdbuf -oL ninja', 'stdbuf -o0 -e0 ctest', 'time ninja', 'chrt 10 ninja', 'chrt -f 10 ninja', 'start /b ninja', 'cmd /c ninja', 'powershell -c ninja', 'bash -c "ninja"', 'ionice -c3 ninja', 'nohup ninja', 'taskset -c 0 nice -n 5 ninja', 'env X=1 taskset -c 0 ctest', 'cd build && taskset -c 0 ninja'];
  for (const c of refuse) { const r = tge(c); t(`E REFUSE: ${c}`, r.status === 2 && /REFUSED by the time-limit guard/.test(r.stderr), `status ${r.status} ${r.stderr.slice(0, 120)}`); }
  const allow = ['timeout 900 taskset -c 0 ninja', 'taskset -c 0 timeout 900 ninja', 'nice -n 5 timeout 900 ctest', 'env -u A timeout 900 ninja', 'stdbuf -oL timeout 900 ninja', 'env X=1 taskset -c 0 timeout 900 ctest', 'taskset -c 0 echo hi', 'taskset -c 0 ls build', 'time ls', 'nice -n 5 git status', 'env X=1 echo ok', 'stdbuf -oL cat build.ninja'];
  for (const c of allow) { const r = tge(c); t(`E ALLOW: ${c}`, r.status === 0 && r.stderr === '', `status ${r.status} ${r.stderr.slice(0, 120)}`); }
  const rr = tge('taskset -c 0 ninja'); t('E the refusal for a wrapped command names the way to add the limit', /timeout 900/.test(rr.stderr), rr.stderr.slice(0, 300));
  const rn = tge('node tools/run_suite.mjs /x/y --timeout 5000'); t('E run_suite with --timeout over 900 is refused by the guard too', rn.status === 2, rn.stderr.slice(0, 200));
  const ok = tge('node tools/run_suite.mjs /x/y --timeout 600'); t('E run_suite with its own limit is allowed (it enforces it)', ok.status === 0, ok.stderr.slice(0, 200));
}

// ================================================================ F  decision 7: the Unreal lane lock, one form
console.log('gates13 F: the Unreal lane lock is a directory made with mkdir holding owner.txt; only the owner releases it');
{
  const ds = path.join(tmp, 'state-f'); mkdirs(ds);
  const proj = path.join(tmp, 'f_proj'); const stateF = path.join(proj, '.claude', 'state'); mkdirs(stateF);
  const lockDir = path.join(stateF, 'unreal_lane.lock'); const lockRel = '.claude/state/unreal_lane.lock';
  const sgF = (payload, agent = 'ag5') => hookRun('state_guard.mjs', { hook_event_name: 'PreToolUse', cwd: proj, session_id: 's13', agent_id: agent, agent_type: 'general-purpose', ...payload }, ds);
  const bash = (command, agent) => sgF({ tool_name: 'Bash', tool_input: { command } }, agent);
  const wtool = (file_path, content, agent) => sgF({ tool_name: 'Write', tool_input: { file_path, content } }, agent);
  const refuseF = (label, r) => t(`F REFUSE: ${label}`, r.status === 2 && /state_guard|REFUSED/i.test(r.stderr), `status ${r.status} ${r.stderr.slice(0, 200)}`);
  const allowF = (label, r) => t(`F ALLOW: ${label}`, r.status === 0, `status ${r.status} ${r.stderr.slice(0, 200)}`);
  const ownerText = (id) => `agent: ${id}\nround: m3ui-r3\ntime: 2026-10-23T22:40:00Z\npurpose: playtest of the road tool\n`;
  // no lock yet
  allowF('mkdir of the lock folder (it fails by itself while held)', bash(`mkdir ${lockRel}`));
  allowF('mkdir -p with the absolute path', bash(`mkdir -p "${lockDir.replace(/\\/g, '/')}"`));
  allowF('New-Item -ItemType Directory (PowerShell form of the same lock)', sgF({ tool_name: 'PowerShell', tool_input: { command: `New-Item -ItemType Directory -Path ${lockRel}` } }));
  refuseF('the lock as a FILE: touch', bash(`touch ${lockRel}`));
  refuseF('the lock as a FILE: redirect', bash(`echo ag5 > ${lockRel}`));
  refuseF('the lock as a FILE: the Write tool on the lock path itself', wtool(lockDir, 'ag5'));
  refuseF('the lock as a FILE: New-Item -ItemType File', sgF({ tool_name: 'PowerShell', tool_input: { command: `New-Item -ItemType File ${lockRel}` } }));
  allowF('the owner file inside the lock: the Write tool with the writer\'s own id (a free lock)', wtool(path.join(lockDir, 'owner.txt'), ownerText('ag5')));
  refuseF('owner.txt content naming ANOTHER agent (agent: ag9 written by ag5)', wtool(path.join(lockDir, 'owner.txt'), ownerText('ag9')));
  refuseF('owner.txt content with no agent line at all', wtool(path.join(lockDir, 'owner.txt'), 'round: x\n'));
  refuseF('another file inside the lock (only owner.txt may live there)', bash(`echo x > ${lockRel}/notes.txt`));
  allowF('echo "agent: ag5 ..." > owner.txt (redirect on a free lock)', bash(`echo "agent: ag5 round: m3ui-r3 time: now purpose: playtest" > ${lockRel}/owner.txt`));
  allowF('reading owner.txt', bash(`cat ${lockRel}/owner.txt`, 'ag7'));
  allowF('listing the lock', bash(`ls ${lockRel}`, 'ag7'));
  allowF('Grep on the lock', sgF({ tool_name: 'Grep', tool_input: { pattern: 'agent', path: lockDir } }, 'ag7'));
  // the lock is now held by ag5 (a real owner file)
  fs.mkdirSync(lockDir, { recursive: true }); fs.writeFileSync(path.join(lockDir, 'owner.txt'), ownerText('ag5'));
  const absLock = lockDir.replace(/\\/g, '/');
  refuseF('a second agent rewriting owner.txt of a held lock (Write tool)', wtool(path.join(lockDir, 'owner.txt'), ownerText('ag7'), 'ag7'));
  refuseF('a second agent rewriting owner.txt (redirect)', bash(`echo "agent: ag7" > ${absLock}/owner.txt`, 'ag7'));
  refuseF('a second agent removing the lock (rm -r)', bash(`rm -r ${absLock}`, 'ag7'));
  refuseF('a second agent removing the lock (rmdir after rm owner.txt)', bash(`rm ${absLock}/owner.txt && rmdir ${absLock}`, 'ag7'));
  refuseF('a second agent removing the lock (Remove-Item)', sgF({ tool_name: 'PowerShell', tool_input: { command: `Remove-Item -Recurse ${absLock}` } }, 'ag7'));
  allowF('the owner removing the lock (rm -r)', bash(`rm -r ${absLock}`, 'ag5'));
  allowF('the owner rewriting owner.txt (Write tool with its own id)', wtool(path.join(lockDir, 'owner.txt'), ownerText('ag5'), 'ag5'));
  allowF('the main session (no agent id) is not limited: it can release any lock', hookRun('state_guard.mjs', { hook_event_name: 'PreToolUse', tool_name: 'Bash', tool_input: { command: `rm -r ${absLock}` }, cwd: proj, session_id: 's13' }, ds));
  // a lock with no readable owner (made by hand, or the legacy file form) can be cleared by anyone, so a crashed agent never blocks the lane for good
  const orphan = path.join(tmp, 'f_orphan'); const oState = path.join(orphan, '.claude', 'state'); mkdirs(path.join(oState, 'unreal_lane.lock'));
  allowF('removing a lock with no owner.txt (a crashed agent\'s leftover)', hookRun('state_guard.mjs', { hook_event_name: 'PreToolUse', tool_name: 'Bash', tool_input: { command: `rm -r ${path.join(oState, 'unreal_lane.lock').replace(/\\/g, '/')}` }, cwd: orphan, session_id: 's13', agent_id: 'ag7', agent_type: 'general-purpose' }, ds));
  // everything else in the state folder stays closed
  refuseF('gate.log is still not writable by an agent', bash(`echo x >> .claude/state/gate.log`));
  refuseF('the ack secret is still not readable', bash(`cat .claude/state/ack_secret`));
  refuseF('another state file named next to the lock (unreal_lane.lock.bak)', bash(`touch ${lockRel}.bak`));
  const sgSrc = fs.readFileSync(path.join(src, 'tools', 'hooks', 'state_guard_impl.mjs'), 'utf8');
  t('F state_guard_impl.mjs documents the one lock form (mkdir + owner.txt) and the owner-only release', /ONE lock form/.test(sgSrc) && /owner\.txt/.test(sgSrc) && /only by the agent named in owner\.txt/.test(sgSrc));
}


// ---------------------------------------------------------------- end
t('no line of this run reached the real gate.log', isolation.leaks().length === 0, isolation.leaks().join('\n'));
console.log(`\n${pass} passed, ${fail} failed`);
if (fail) console.log(failedNames.map((n) => ' - ' + n).join('\n'));
cleanupTmp(tmp);
process.exit(fail ? 1 : 0);
