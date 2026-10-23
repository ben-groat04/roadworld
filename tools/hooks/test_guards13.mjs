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
import { spawnSync } from 'node:child_process';
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

// ---------------------------------------------------------------- end
t('no line of this run reached the real gate.log', isolation.leaks().length === 0, isolation.leaks().join('\n'));
console.log(`\n${pass} passed, ${fail} failed`);
if (fail) console.log(failedNames.map((n) => ' - ' + n).join('\n'));
cleanupTmp(tmp);
process.exit(fail ? 1 : 0);
