#!/usr/bin/env node
// Prints a brief = docs/templates/BRIEF.md filled from a task file + docs/AGENT_CONTRACT.md pasted in full.
// Usage: node tools/make_brief.mjs <task-file> --round <name> [--worktree <path>] [--branch <name>] [--no-visual]
// Emits a line `HANDIN_DIR: <absolute path of the progress folder in the agent's worktree>` (read by the SubagentStop
// hook tools/hooks/handin_gate.mjs and the launch gate tools/hooks/brief_gate.mjs) and, with --no-visual, `HANDIN: no-visual`.
// With --audit <progress-folder> (round name optional) it prints the AUDITOR brief for that round instead (docs/templates/AUDIT_BRIEF.md):
// marker line `AUDIT-OF: <abs folder>`, the task's owner words and cases/reference files, NOT the builder's fault list; the launch gate
// and the finish gate (tools/hooks/audit_lib.mjs) then require AUDIT.md by an agent that is not the builder.
// Task file (markdown): first "# " line is the task; sections "## Owner's words", "## Cases",
// "## Faults", "## Launched", "## Waiting". Refuses to print if a mandatory section is empty.
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { parseGateLine, parseRounds, methodChangeProblem } from './gates_lib.mjs';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const args = process.argv.slice(2);
const opt = (n, d = '') => { const i = args.indexOf('--' + n); return i >= 0 ? args[i + 1] : d; };
const valueOpts = ['--round', '--worktree', '--branch', '--audit', '--override'];
const taskFile = args.find((a, i) => !a.startsWith('--') && !(i > 0 && valueOpts.includes(args[i - 1])));
if (!taskFile || (!opt('round') && !opt('audit'))) {
  console.error('usage: node tools/make_brief.mjs <task-file> (--round <name> | --audit <progress-folder>) [--worktree <path>] [--branch <name>] [--no-visual]');
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
const auditMode = args.includes('--audit');
if (!task) missing.push('first line "# <task>"');
if (!/["“]/.test(get("owner's words"))) missing.push('"## Owner\'s words" with the verbatim quote in quotation marks');
if (get('cases').length < 5) missing.push('"## Cases" (case | absolute photo paths, at least 3 each)');
if (!auditMode && !get('faults')) missing.push('"## Faults" (starting list; write "none known" if none)');
if (!auditMode && !get('launched')) missing.push('"## Launched" (independent items launched now)');
if (!auditMode && !get('waiting')) missing.push('"## Waiting" (items not launched and why; write "none" if none)');
if (/time-box|whole task/i.test(text.replace(get("owner's words"), ''))) missing.push('remove "time-box" / "whole task" wording: the finish line is the photo');
// ---- gates v2 (tools/gates_lib.mjs). Builder briefs only; `GATES-VERSION: 2` switches the measured checks on in check_handin.
const gateLines = [];
if (!auditMode) {
  const noVisualFlag = args.includes('--no-visual');
  // Engine-only briefs without HANDIN: no-visual made two builders write placeholder COMPARE tables to get through.
  if (!noVisualFlag && /\bno renders?\b|\bno-visual\b|engine[- ]only|no pictures?\b/i.test(get('cases'))) {
    missing.push('"## Cases" says there are no renders / no pictures but the brief is not built with --no-visual (without it the hand-in demands COMPARE.md with composites, and builders fabricate placeholders)');
  }
  // Scope (G): at most 3 cases per round unless the task file says why more.
  const nCases = (get('cases').match(/^\s*(?:[-*]\s*)?(?:Case|Scenario|View)\s*[\w.-]*\s*[:,|(-]/gim) || []).length;
  if (nCases > 3 && get('scope').length < 20) missing.push('"## Cases" names ' + nCases + ' cases; a round takes at most 3 unless the task file has a "## Scope" section (20+ characters) saying why more. One round with eight scenarios is how earthworks round 9 left several untouched');
  // Gates
  for (const raw of get('gates').split('\n')) {
    const g = parseGateLine(raw);
    if (!g) continue;
    if (g.kind === 'bad') {
      // prose lines in a Gates section are allowed (e.g. "No new test waiver"); only lines that look like gate lines are checked
      if (/(>=|<=|==|^allow_waiver|^picture_)/i.test(g.raw)) missing.push('"## Gates": ' + g.raw + ' -> ' + g.why);
      continue;
    }
    gateLines.push(g.raw);
  }
  // Method-change rule (E)
  const prev = text.match(/^Previous round:[ \t]*(\S.*)$/mi);
  const rounds = parseRounds(get('rounds'));
  if (prev && !rounds.length) missing.push('the task file names a previous round ("' + prev[1].slice(0, 60) + '") but has no "## Rounds" section with its baseline numbers, one line per round, oldest first: "round8: scene_check=20, ctest_failed_assertions=2"');
  if (rounds.length && rounds.some((r) => !Object.keys(r.values).length)) missing.push('"## Rounds": a line has no numbers (' + rounds.find((r) => !Object.keys(r.values).length).raw.slice(0, 60) + ')');
  const mc = methodChangeProblem(rounds);
  const ov = opt('override');
  if (mc && !(ov && ov.length >= 20)) missing.push('METHOD REVIEW FIRST: ' + mc);
  else if (mc) console.error('make_brief: method-change rule overridden: ' + ov);
}
if (missing.length) {
  console.error('REFUSED. Fix the task file:\n - ' + missing.join('\n - '));
  process.exit(1);
}
if (auditMode) {
  const folder = path.resolve(opt('audit'));
  const round = opt('round') || path.basename(folder);
  // reference files = path-like tokens with a file extension in the Cases section (counted so the auditor can write "N of M")
  const refRe = /[A-Za-z]:[\\/][^\s|,;()"'<>]+\.\w{2,5}|[\w./\\-]+\.(?:jpe?g|png|webp|gif|md|mjs|js|json|cpp|h|hpp|txt|pdf|uasset|py)\b/gi;
  const refs = [...new Set(get('cases').match(refRe) || [])];
  const tplA = fs.readFileSync(path.join(root, 'docs/templates/AUDIT_BRIEF.md'), 'utf8');
  const fillA = {
    TASK: task, ROUND: round, PROGRESS: folder, OWNER_WORDS: get("owner's words"), CASES: get('cases'),
    REF_COUNT: String(refs.length),
    REFLIST: refs.length ? 'Files named in the cases (' + refs.length + '):\n' + refs.map((r) => '- ' + r).join('\n')
      : '(no file paths could be read from the cases text: count the files it names yourself and say M in AUDIT.md)',
    CONTRACT: fs.readFileSync(path.join(root, 'docs/AGENT_CONTRACT.md'), 'utf8').trim(),
  };
  process.stdout.write(tplA.replace(/\{\{(\w+)\}\}/g, (_, k) => fillA[k] ?? ''));
  let canaryA = 0;
  try {
    const nl = await import('./hooks/notice_lib.mjs');
    const lib = await import('./hooks/lib.mjs');
    if (nl.isMainCheckout(root)) {
      const rep2 = nl.canaryReport(lib.STATE_DIR);
      if (rep2.text) console.error('\n' + rep2.text + '\n');
      if (rep2.problems.length) canaryA = 3;
      lib.log({ gate: 'make_brief', round: 'audit:' + round, audit_of: folder });
    }
  } catch (e) { console.error('make_brief: canary check could not run (' + (e && e.message) + ')'); }
  process.exit(canaryA);
}
const round = opt('round');
const worktree = opt('worktree', path.join(path.dirname(root), 'roadworld-wt', round)); // matches new_agent_worktree.ps1 (new worktrees live under ..\roadworld-wt\<name>)
const handinDir = path.join(path.resolve(worktree), 'docs', 'progress', round);
const handinLines = 'HANDIN_DIR: ' + handinDir + (args.includes('--no-visual') ? '\nHANDIN: no-visual' : '') + '\nGATES-VERSION: 2' + gateLines.map((g) => '\nGATE: ' + g).join('');
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
