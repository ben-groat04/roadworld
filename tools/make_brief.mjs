#!/usr/bin/env node
// Prints a brief = docs/templates/BRIEF.md filled from a task file + docs/AGENT_CONTRACT.md pasted in full.
// Usage: node tools/make_brief.mjs <task-file> --round <name> [--worktree <path>] [--branch <name>] [--no-visual]
// Emits a line `HANDIN_DIR: <absolute path of the progress folder in the agent's worktree>` (read by the SubagentStop
// hook tools/hooks/handin_gate.mjs and the launch gate tools/hooks/brief_gate.mjs) and, with --no-visual, `HANDIN: no-visual`.
// Task file (markdown): first "# " line is the task; sections "## Owner's words", "## Cases",
// "## Faults", "## Launched", "## Waiting". Refuses to print if a mandatory section is empty.
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const args = process.argv.slice(2);
const opt = (n, d = '') => { const i = args.indexOf('--' + n); return i >= 0 ? args[i + 1] : d; };
const valueOpts = ['--round', '--worktree', '--branch'];
const taskFile = args.find((a, i) => !a.startsWith('--') && !(i > 0 && valueOpts.includes(args[i - 1])));
if (!taskFile || !opt('round')) {
  console.error('usage: node tools/make_brief.mjs <task-file> --round <name> [--worktree <path>] [--branch <name>] [--no-visual]');
  process.exit(2);
}
const text = fs.readFileSync(taskFile, 'utf8').replace(/\r\n/g, '\n');
const sections = {};
let cur = null;
let task = '';
for (const line of text.split('\n')) {
  const h = line.match(/^##\s+(.*?)\s*$/);
  if (h) { cur = h[1].toLowerCase(); sections[cur] = ''; continue; }
  const t = line.match(/^#\s+(.*)$/);
  if (t && !task) { task = t[1].trim(); continue; }
  if (cur) sections[cur] += line + '\n';
}
const get = (k) => (sections[k] ?? '').trim();
const missing = [];
if (!task) missing.push('first line "# <task>"');
if (!/["“]/.test(get("owner's words"))) missing.push('"## Owner\'s words" with the verbatim quote in quotation marks');
if (get('cases').length < 5) missing.push('"## Cases" (case | absolute photo paths, at least 3 each)');
if (!get('faults')) missing.push('"## Faults" (starting list; write "none known" if none)');
if (!get('launched')) missing.push('"## Launched" (independent items launched now)');
if (!get('waiting')) missing.push('"## Waiting" (items not launched and why; write "none" if none)');
if (/time-box|whole task/i.test(text)) missing.push('remove "time-box" / "whole task" wording: the finish line is the photo');
if (missing.length) {
  console.error('REFUSED. Fix the task file:\n - ' + missing.join('\n - '));
  process.exit(1);
}
const round = opt('round');
const worktree = opt('worktree', path.join(path.dirname(root), 'roadworld-wt-' + round)); // matches new_agent_worktree.ps1
const handinDir = path.join(path.resolve(worktree), 'docs', 'progress', round);
const handinLines = 'HANDIN_DIR: ' + handinDir + (args.includes('--no-visual') ? '\nHANDIN: no-visual' : '');
const fill = {
  TASK: task,
  ROUND: opt('round'),
  WORKTREE: worktree,
  BRANCH: opt('branch', 'agent/' + opt('round')),
  HANDIN_LINES: handinLines,
  OWNER_WORDS: get("owner's words"),
  CASES: get('cases'),
  FAULTS: get('faults'),
  LAUNCHED: get('launched'),
  WAITING: get('waiting'),
  CONTRACT: fs.readFileSync(path.join(root, 'docs/AGENT_CONTRACT.md'), 'utf8').trim(),
};
const tpl = fs.readFileSync(path.join(root, 'docs/templates/BRIEF.md'), 'utf8');
// single pass so placeholder-like text inside the pasted values is never re-expanded
process.stdout.write(tpl.replace(/\{\{(\w+)\}\}/g, (_, k) => fill[k] ?? ''));

// Canary (tools/hooks/notice_lib.mjs): hooks that never run cannot report themselves, so look at gate.log from this side.
// Only in the main checkout, which owns the live log. The brief above is still printed; the exit code is 3 if tripped.
let canaryExit = 0;
try {
  const nl = await import('./hooks/notice_lib.mjs');
  const lib = await import('./hooks/lib.mjs');
  if (nl.isMainCheckout(root)) {
    const rep = nl.canaryReport(lib.STATE_DIR);
    if (rep.text) console.error('\n' + rep.text + '\n');
    if (rep.problems.length) canaryExit = 3;
    lib.log({ gate: 'make_brief', round: opt('round'), handin_dir: handinDir });
  }
} catch (e) { console.error('make_brief: canary check could not run (' + (e && e.message) + ')'); }
process.exitCode = canaryExit;
