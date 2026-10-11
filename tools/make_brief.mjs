#!/usr/bin/env node
// Prints a brief = docs/templates/BRIEF.md filled from a task file + docs/AGENT_CONTRACT.md pasted in full.
// Usage: node tools/make_brief.mjs <task-file> --round <name> [--worktree <path>] [--branch <name>]
// Task file (markdown): first "# " line is the task; sections "## Owner's words", "## Cases",
// "## Faults", "## Launched", "## Waiting". Refuses to print if a mandatory section is empty.
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const args = process.argv.slice(2);
const opt = (n, d = '') => { const i = args.indexOf('--' + n); return i >= 0 ? args[i + 1] : d; };
const taskFile = args.find((a, i) => !a.startsWith('--') && !(i > 0 && args[i - 1].startsWith('--')));
if (!taskFile || !opt('round')) {
  console.error('usage: node tools/make_brief.mjs <task-file> --round <name> [--worktree <path>] [--branch <name>]');
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
const fill = {
  TASK: task,
  ROUND: opt('round'),
  WORKTREE: opt('worktree', '<set by new_agent_worktree.ps1>'),
  BRANCH: opt('branch', 'agent/' + opt('round')),
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
