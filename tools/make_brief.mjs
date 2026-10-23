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
// gates_lib loads dynamically: if it is broken the brief is still printed (old layout, no GATES-VERSION line) and a FAIL-OPEN line is logged.
let GL = null;
try { GL = await import('./gates_lib.mjs'); } catch (e) {
  console.error('FAIL-OPEN: gates_lib.mjs could not be loaded (' + (e && e.message) + '); the brief is printed WITHOUT the measured gates');
  try { const lib = await import('./hooks/lib.mjs'); lib.log({ gate: 'make_brief', decision: 'allow', reason: 'FAIL-OPEN: gates_lib could not be loaded', error: String((e && e.message) || e).slice(0, 300) }); } catch { /* the console line above is all that is left */ }
}

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const args = process.argv.slice(2);
const opt = (n, d = '') => { const i = args.indexOf('--' + n); return i >= 0 ? args[i + 1] : d; };
const valueOpts = ['--round', '--worktree', '--branch', '--audit', '--override', '--phase', '--first-look', '--prior-audit'];
const taskFile = args.find((a, i) => !a.startsWith('--') && !(i > 0 && valueOpts.includes(args[i - 1])));
if (!taskFile || (!opt('round') && !opt('audit') && !opt('first-look'))) {
  console.error('usage: node tools/make_brief.mjs <task-file> (--round <name> [--phase measure|build|render] | --audit <progress-folder> | --first-look <progress-folder>) [--worktree <path>] [--branch <name>] [--no-visual]');
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
const auditMode = args.includes('--audit') || args.includes('--first-look');   // a first look is briefed like an audit: owner's words and cases, no builder sections
const lookMode = args.includes('--first-look');
const phaseOpt = opt('phase');
if (phaseOpt && !['measure', 'build', 'render'].includes(phaseOpt)) { console.error('REFUSED: --phase must be measure, build or render (got "' + phaseOpt + '")'); process.exit(1); }
if (phaseOpt && !opt('round')) { console.error('REFUSED: --phase needs --round <name>'); process.exit(1); }
if (!task) missing.push('first line "# <task>"');
if (!/["“]/.test(get("owner's words"))) missing.push('"## Owner\'s words" with the verbatim quote in quotation marks');
if (get('cases').length < 5) missing.push('"## Cases" (case | absolute photo paths, at least 3 each)');
if (!auditMode && !get('faults')) missing.push('"## Faults" (starting list; write "none known" if none)');
if (!auditMode && !get('launched')) missing.push('"## Launched" (independent items launched now)');
if (!auditMode && !get('waiting')) missing.push('"## Waiting" (items not launched and why; write "none" if none)');
if (/time-box|whole task/i.test(text.replace(get("owner's words"), ''))) missing.push('remove "time-box" / "whole task" wording: the finish line is the photo');
// ---- gates v2 (tools/gates_lib.mjs). Builder briefs only; `GATES-VERSION: 3` (2 = the earlier layout, kept for agents already running) switches the measured checks on in check_handin.
const gateLines = [];
let overrideLog = null;
if (!auditMode && GL) {
  const { parseGateLine, parseRounds, methodChangeProblem, FAILURE_METRICS } = GL;
  const noVisualFlag = args.includes('--no-visual');
  // Engine-only briefs without HANDIN: no-visual made two builders write placeholder COMPARE tables to get through.
  if (!noVisualFlag && /\bno[- ]renders?\b|\bno-visual\b|engine[- ]only|no[- ]pictures?\b|\bheadless\b|no[- ]screenshots?\b|\bno (?:views?|images?|visuals?)\b|nothing (?:is )?rendered/i.test(get('cases'))) {
    missing.push('"## Cases" says there are no renders / pictures / screenshots (engine-only, headless) but the brief is not built with --no-visual (without it the hand-in demands COMPARE.md with composites, and builders fabricate placeholders)');
  }
  // Scope (G): at most 3 cases per round unless the task file says why more. Cases are counted by what the section lists: "Case|Scenario|View|Item|Test N:" lines,
  // "s1:" / "1." / "2)" / "A)" lines, and an explicit "Cases: N" / "N scenarios" statement; the largest count counts. Lines that start with a drive path are file lists.
  const caseLine = /^\s*(?:[-*]\s*)?(?:\*\*)?(?:(?:Case|Scenario|View|Item|Test)\s*[\w.-]*\s*[:.)|-]|[A-Za-z]{1,2}\d+\s*[:.)|-]|\d+\s*[.)]|[A-Z]\s*\))/i;
  const caseLines = get('cases').split('\n').filter((l) => caseLine.test(l) && !/^\s*(?:[-*]\s*)?[A-Za-z]:[\/]/.test(l));
  const stated = Math.max(0, ...[...get('cases').matchAll(/\bcases?\s*[:=]\s*(\d+)|\b(\d+)\s+(?:cases|scenarios|views)\b/gi)].map((m) => Number(m[1] || m[2])));
  const nCases = Math.max(caseLines.length, stated);
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
    if (g.kind === 'metric' && FAILURE_METRICS.includes(g.metric) && g.op === '==') { missing.push('"## Gates": ' + g.raw + ' -> "==" on a failure count would refuse an improvement (a fix that brings it to 0); write "' + g.metric + ' <= ' + g.floor + '"'); continue; }
    gateLines.push(g.raw);
  }
  // A ctest failed-assertions gate is paired with failed_tests: a crashed test shows 0 failed assertions. Failed tests cannot exceed failed assertions
  // unless a test crashed, so the assertion floor is a valid upper bound for the pair.
  const fa = gateLines.map(parseGateLine).find((g) => g && g.kind === 'metric' && g.metric === 'ctest failed_assertions');
  if (fa && !gateLines.some((l) => /^ctest failed_tests\b/i.test(l))) gateLines.push('ctest failed_tests <= ' + fa.floor);
  // Method-change rule (E)
  const prev = text.match(/^Previous round:[ \t]*(\S.*)$/mi);
  const rounds = parseRounds(get('rounds'));
  if (prev && !rounds.length) missing.push('the task file names a previous round ("' + prev[1].slice(0, 60) + '") but has no "## Rounds" section with its baseline numbers, one line per round, oldest first: "round8: scene_check=20, ctest_failed_assertions=2, bare_steps=3 (lower)"');
  for (const r of rounds) if (r.undirected && r.undirected.length) missing.push('"## Rounds": ' + r.round + ' has metric(s) with no stated direction: ' + r.undirected.join(', ') + ' (write e.g. "bare_steps=3 (lower)" or "(higher)"; the direction is never guessed from the name)');
  if (rounds.length && rounds.some((r) => !Object.keys(r.values).length)) missing.push('"## Rounds": a line has no numbers (' + rounds.find((r) => !Object.keys(r.values).length).raw.slice(0, 60) + ')');
  const mc = methodChangeProblem(rounds);
  const ov = opt('override');
  if (mc && !(ov && ov.length >= 20)) missing.push('METHOD REVIEW FIRST: ' + mc);
  else if (mc) { console.error('make_brief: method-change rule overridden: ' + ov); overrideLog = ov; }
}
if (missing.length) {
  console.error('REFUSED. Fix the task file:\n - ' + missing.join('\n - '));
  process.exit(1);
}
if (auditMode) {
  const folder = path.resolve(opt(lookMode ? 'first-look' : 'audit'));
  const round = opt('round') || path.basename(folder);
  // reference files = path-like tokens with a file extension in the Cases section (counted so the auditor can write "N of M")
  const refRe = /[A-Za-z]:[\\/][^\s|,;()"'<>]+\.\w{2,5}|[\w./\\-]+\.(?:jpe?g|png|webp|gif|md|mjs|js|json|cpp|h|hpp|txt|pdf|uasset|py)\b/gi;
  const refs = [...new Set(get('cases').match(refRe) || [])];
  const tplA = fs.readFileSync(path.join(root, lookMode ? 'docs/templates/FIRSTLOOK_BRIEF.md' : 'docs/templates/AUDIT_BRIEF.md'), 'utf8');
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
      lib.log({ gate: 'make_brief', round: (lookMode ? 'firstlook:' : 'audit:') + round, [lookMode ? 'firstlook_of' : 'audit_of']: folder });
    }
  } catch (e) { console.error('make_brief: canary check could not run (' + (e && e.message) + ')'); }
  process.exit(canaryA);
}
const round = opt('round');
const worktree = opt('worktree', path.join(path.dirname(root), 'roadworld-wt', round)); // matches new_agent_worktree.ps1 (new worktrees live under ..\roadworld-wt\<name>)
const handinDir = path.join(path.resolve(worktree), 'docs', 'progress', round);
const priorAudit = opt('prior-audit') ? path.resolve(opt('prior-audit')) : '';   // send-back round: the previous AUDIT.md; the hand-in gate then demands the "Prior audit items" table
// "## Items": the work items the brief lists, one per line ("A. text", "1) text", "- s3: text"). Each becomes an `ITEM: <id> | <text>` line; the hand-in gate demands a row per item
// in STATUS.md "## Items" (done with proof, or a valid BLOCKED line), so an item left out is caught without reading any wording.
// With no "## Items" section the numbered or lettered lines of "## Faults" are the items (every listed fault is fixed or BLOCKED, contract item 17); a brief with neither has no ITEM lines.
const itemSource = get('items') || get('faults');
if (!get('items') && /^\s*(?:[-*]\s*)?\**(?:\d{1,3}|[A-Za-z]|[A-Za-z]{1,2}\d{1,3})\**\s*[.):]\s+\S/m.test(get('faults'))) console.error('make_brief: no "## Items" section: the numbered lines of "## Faults" are used as the items the hand-in must answer');
const itemLines = itemSource.split('\n').map((l) => l.match(/^\s*(?:[-*]\s*)?\**(\d{1,3}|[A-Za-z]|[A-Za-z]{1,2}\d{1,3})\**\s*[.):]\s+(\S.*)$/)).filter(Boolean).map((m) => 'ITEM: ' + m[1] + ' | ' + m[2].replace(/\s+/g, ' ').slice(0, 200));
// ---- phase mode (gates10): a round split into short fresh agents. build and render start only when the earlier phase's file exists AND passes its own check (run here, not quoted).
const suiteLines = get('suite').split('\n').map((l) => l.replace(/^\s*[-*]\s*/, '').replace(/^\x60|\x60$/g, '').trim()).filter(Boolean);
// gates11: what the phase checks compare with. REFS = the reference files the cases name (the same count the auditor brief gives), CASE = one label per case line, ALLOW-FAIL = the tests the build
// suite may fail ("## Allowed failures" of the task file, one test-name text per line).
const refRe2 = /[A-Za-z]:[\\/][^\s|,;()"'<>]+\.\w{2,5}|[\w./\\-]+\.(?:jpe?g|png|webp|gif|md|mjs|js|json|cpp|h|hpp|txt|pdf|uasset|py)\b/gi;
const refCount = [...new Set(get('cases').match(refRe2) || [])].length;
const caseLabels = get('cases').split('\n').filter((l) => /^\s*(?:[-*]\s*)?(?:\*\*)?(?:(?:Case|Scenario|View|Item|Test)\s*[\w.-]*\s*[:.)|-]|[A-Za-z]{1,2}\d+\s*[:.)|-]|\d+\s*[.)]|[A-Z]\s*\))/i.test(l) && !/^\s*(?:[-*]\s*)?[A-Za-z]:[\\/]/.test(l)).map((l) => l.replace(/^\s*(?:[-*]\s*)?(?:\*\*)?/, '').replace(/\*\*/g, '').split('|')[0].trim()).filter(Boolean);
const allowFailLines = get('allowed failures').split('\n').map((l) => l.replace(/^\s*[-*]\s*/, '').replace(/^`|`$/g, '').trim()).filter(Boolean);
if (phaseOpt === 'build' || phaseOpt === 'render') {
  const { spawnSync } = await import('node:child_process');
  const need = phaseOpt === 'build' ? ['measure'] : ['measure', 'build'];
  const bad = [];
  for (const ph of need) {
    const f = path.join(handinDir, ph === 'measure' ? 'MEASURE.md' : 'BUILD.md');
    if (!fs.existsSync(f)) { bad.push(f + ' does not exist: the ' + ph + ' phase has not handed over'); continue; }
    const cargs = [path.join(root, 'tools', 'check_handin.mjs'), handinDir, '--phase', ph, '--main', root, '--worktree', path.resolve(worktree), ...(refCount ? ['--refs', String(refCount)] : []), ...(ph === 'measure' ? caseLabels.flatMap((c) => ['--case', c]) : []), ...(ph === 'build' ? allowFailLines.flatMap((c) => ['--allow-fail', c]) : [])];
    const r = spawnSync(process.execPath, cargs, { encoding: 'utf8', timeout: 5 * 60 * 1000 });
    if (r.status !== 0) bad.push('the ' + ph + ' phase check fails:\n   ' + String(r.stderr || r.stdout || r.error || '').trim().split('\n').slice(0, 12).join('\n   '));
  }
  if (bad.length) { console.error('REFUSED: the ' + phaseOpt + ' phase cannot be briefed yet:\n - ' + bad.join('\n - ')); process.exit(1); }
}
const handinLines = 'HANDIN_DIR: ' + handinDir + (phaseOpt ? '\nPHASE: ' + phaseOpt : '') + (phaseOpt === 'build' ? suiteLines.map((c) => '\nSUITE: ' + c).join('') + allowFailLines.map((c) => '\nALLOW-FAIL: ' + c).join('') : '') + (phaseOpt === 'measure' || phaseOpt === 'build' ? (refCount ? '\nREFS: ' + refCount : '') + caseLabels.map((c) => '\nCASE: ' + c).join('') : '') + (args.includes('--no-visual') ? '\nHANDIN: no-visual' : '') + (GL ? '\nGATES-VERSION: 3' + gateLines.map((g) => '\nGATE: ' + g).join('') + itemLines.map((l) => '\n' + l).join('') : '') + (priorAudit ? '\nPRIOR-AUDIT: ' + priorAudit : '');
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
const PHASE_TEXT = {
  measure: 'MEASURE phase (first of up to three short agents; the next one starts from your file). You open EVERY reference, measure in lane widths or metres, and write MEASURE.md in the progress folder, nothing else: no code. Format: a line "References opened: N of M listed" (M is the REFS: count of this brief; N < M needs a "not opened" line with the file and why); one "## Case <name>" section per CASE: line of this brief, each with a table "| photo file | what was measured | value (m, cm or lane widths) |" naming at least 3 different photos that exist under docs/reference of the main checkout, and a trace or overlay image of your own for that case (a PNG/JPEG of at least 32 x 32 px you draw over the photo or the render, written during this run, named in the section, not shared with another case). You may not finish until "node tools/check_handin.mjs <HANDIN_DIR> --phase measure" passes (your own 3-refusal count; the CHECKPOINT allowance does not apply). Commit MEASURE.md and the images with [skip ci].',
  build: 'BUILD phase (second of up to three short agents). Read MEASURE.md and the decisions in this brief; write the code and its tests; commit after each verified step with [skip ci]; at least one commit must touch code or tests outside the progress folder. Write BUILD.md in the progress folder with a "Commits:" line listing every commit you made since MEASURE.md was written and a line "Suite binary: <path of the test executable or script the suite ran>". Run the SUITE: line(s) of this brief yourself on the committed code, under a time limit, with the output in the progress folder: "timeout 900 <suite command> > <HANDIN_DIR>/suite_build.log 2>&1" (the check READS that log and never runs the suite; the log must be newer than your last code commit and come from a binary built after it; failures are allowed only for the ALLOW-FAIL: tests of this brief). For every NEW test (a new test file, or tests added to one) keep a fail_first_<name>.txt in the progress folder with the real failing output of the new tests on the code before your change, in the runner format (FAIL lines, doctest ERROR blocks, ctest ***Failed). No new waiver, skip or loosened tolerance. git status must be empty when you finish. You may not finish until "node tools/check_handin.mjs <HANDIN_DIR> --phase build" passes (your own 3-refusal count; no CHECKPOINT allowance). The round auditor re-runs the suite itself.',
  render: 'RENDER phase (last of up to three short agents). MEASURE.md and BUILD.md passed their checks. You make the renders, composites and the self-audit and write the full hand-in below (STATUS.md, FAULTS.md, COMPARE.md, INDEX.md); the ordinary hand-in check applies unchanged, and the round is audited after it.',
};
process.stdout.write(tpl.replace(/\{\{(\w+)\}\}/g, (_, k) => fill[k] ?? '').replace(/^(## Finish line)/m, () => (phaseOpt ? '## Phase\n' + PHASE_TEXT[phaseOpt] + '\n\n' : '') + '## Finish line'));

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
    lib.log({ gate: 'make_brief', round: opt('round'), handin_dir: handinDir, ...(overrideLog ? { method_override: overrideLog } : {}) });
  }
} catch (e) { console.error('make_brief: canary check could not run (' + (e && e.message) + ')'); }
process.exitCode = canaryExit;
