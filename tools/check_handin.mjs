#!/usr/bin/env node
// Hand-in check. Usage: node tools/check_handin.mjs <progress-folder> [--main <main-checkout>] [--no-visual]
// Fails (exit 1) with a list unless: STATUS.md starts with the "Skipped, refused or failed" heading with a body;
// STATUS.md names >=3 existing reference files per "Case:" line; FAULTS.md has >=2 "Pass" sections and a Residual
// section; STATUS.md has "References opened: N of M" (N < M needs a "not opened" line with a file name in the Skipped section);
// --no-visual skips the FAULTS.md and COMPARE.md checks (engine-only work); COMPARE.md has a table whose files exist (composites under <main>/docs/reference/_compare/).
import fs from 'node:fs';
import path from 'node:path';
import { execSync } from 'node:child_process';

const args = process.argv.slice(2);
const noVisual = args.includes('--no-visual');
const mi = args.indexOf('--main');
const mainArg = mi >= 0 ? args[mi + 1] : null;
const folder = args.find((a, i) => !a.startsWith('--') && !(mi >= 0 && i === mi + 1));
if (!folder) {
  console.error('usage: node tools/check_handin.mjs <progress-folder> [--main <path>] [--no-visual]');
  process.exit(2);
}

function mainCheckout() {
  if (mainArg) return path.resolve(mainArg);
  const cwds = [fs.existsSync(folder) ? path.resolve(folder) : null, process.cwd()].filter(Boolean);
  for (const cwd of cwds) {
    try {
      const common = execSync("git rev-parse --path-format=absolute --git-common-dir", { cwd, stdio: ["ignore", "pipe", "ignore"] }).toString().trim();
      return path.dirname(common);
    } catch { /* try next */ }
  }
  return process.cwd();
}
const main = mainCheckout();
const refRoot = path.join(main, 'docs', 'reference');
// The hand-in's own worktree root (so prototype screenshots / spec docs under docs/progress/... resolve).
function worktreeRoot() {
  const abs = path.resolve(folder);
  const fwd = abs.split(path.sep).join('/');
  const k = fwd.toLowerCase().indexOf('/docs/progress/');
  if (k >= 0) return fwd.slice(0, k);
  try { return execSync("git rev-parse --show-toplevel", { cwd: fs.existsSync(abs) ? abs : process.cwd(), stdio: ["ignore", "pipe", "ignore"] }).toString().trim(); } catch { return main; }
}
const wtRoot = worktreeRoot();
// An opened reference resolves if it is (a) an absolute path that exists, (b) relative to the main checkout or the hand-in's worktree, or (c) a name under docs/reference.
const refBases = [refRoot, main, wtRoot];
const fails = [];
const read = (f) => {
  const p = path.join(folder, f);
  return fs.existsSync(p) ? fs.readFileSync(p, 'utf8').replace(/\r\n/g, '\n') : null;
};
const stripComments = (s) => s.replace(/<!--[\s\S]*?-->/g, '');

function findFile(name, bases) {
  name = name.trim().replace(/^`|`$/g, '');
  if (!name) return false;
  if (path.isAbsolute(name)) return fs.existsSync(name);
  return bases.some((b) => fs.existsSync(path.join(b, name)));
}

// STATUS.md
const status = read('STATUS.md');
if (status === null) {
  fails.push('STATUS.md is missing');
} else {
  const s = stripComments(status);
  const heads = [...s.matchAll(/^##\s+(.*)$/gm)];
  if (!heads.length || !/^skipped, refused or failed/i.test(heads[0][1].trim())) {
    fails.push('STATUS.md: first "##" heading must be "Skipped, refused or failed"');
  } else {
    const start = heads[0].index + heads[0][0].length;
    const end = heads[1] ? heads[1].index : s.length;
    const skipBody = s.slice(start, end);
    if (skipBody.trim().length < 10) fails.push('STATUS.md: "Skipped, refused or failed" section is empty');
    const ro = s.match(/References opened:\s*(\d+)\s+of\s+(\d+)/i);
    if (!ro) fails.push('STATUS.md: no "References opened: N of M" line (integers)');
    else if (+ro[1] < +ro[2]) {
      const lines = skipBody.split('\n').filter((l) => /not opened/i.test(l) && /[\w-]+\.\w{2,5}\b/.test(l));
      if (!lines.length) fails.push(`STATUS.md: References opened ${ro[1]} of ${ro[2]}: the Skipped section needs a "not opened" line naming each unopened file with a reason`);
    }
  }
  const cases = [...s.matchAll(/^\s*Case:\s*(.*?)\s*\|\s*Opened:\s*(.*)$/gim)];
  if (!cases.length) fails.push('STATUS.md: no "Case: <name> | Opened: <file>, <file>, <file>" lines');
  for (const c of cases) {
    const files = [...new Set(c[2].split(',').map((x) => x.trim()).filter(Boolean))];
    const ok = files.filter((f) => findFile(f, refBases));
    const bad = files.filter((f) => !ok.includes(f));
    if (ok.length < 3) {
      fails.push(`STATUS.md: case "${c[1]}" names ${ok.length} existing reference files, need at least 3` +
        (bad.length ? ` (not found as absolute path, under ${wtRoot}, ${main} or ${refRoot}: ${bad.join(', ')})` : ''));
    } else if (bad.length) {
      fails.push(`STATUS.md: case "${c[1]}": files not found (absolute, under ${wtRoot}, ${main} or ${refRoot}): ${bad.join(', ')}`);
    }
  }
}

// FAULTS.md
const faults = noVisual ? '' : read('FAULTS.md');
if (noVisual) { /* skipped */ } else if (faults === null) {
  fails.push('FAULTS.md is missing');
} else {
  const passes = [...faults.matchAll(/^##\s+Pass\s+\d+/gim)].length;
  if (passes < 2) fails.push(`FAULTS.md: ${passes} "## Pass N" section(s), need at least 2`);
  const idx = faults.search(/^##\s+Residual/im);
  if (idx < 0) {
    fails.push('FAULTS.md: no "## Residual" section');
  } else {
    const rest = faults.slice(idx).split('\n').slice(1);
    const next = rest.findIndex((l) => /^##\s/.test(l));
    const body = (next < 0 ? rest : rest.slice(0, next)).join('\n').trim();
    if (body.length < 5) fails.push('FAULTS.md: "## Residual" is empty (list what remains, or "none" and why)');
  }
}

// COMPARE.md
const compare = noVisual ? '' : read('COMPARE.md');
if (noVisual) { /* skipped */ } else if (compare === null) {
  fails.push('COMPARE.md is missing');
} else {
  const rows = compare.split('\n')
    .filter((l) => l.trim().startsWith('|') && !/^\s*\|[\s:|-]+\|\s*$/.test(l))
    .slice(1);
  if (!rows.length) fails.push('COMPARE.md: no table rows (case | render | reference | composite | before-same-camera)');
  rows.forEach((r, i) => {
    const cells = r.split('|').slice(1, -1).map((c) => c.trim());
    if (cells.length < 5) { fails.push(`COMPARE.md row ${i + 1}: needs 5 columns`); return; }
    const [kase, render, ref, comp, before] = cells;
    const bases = [folder, path.join(refRoot, '_compare'), refRoot, main];
    if (!findFile(render, bases)) fails.push(`COMPARE.md row "${kase}": render not found: ${render}`);
    if (!findFile(ref, refBases)) fails.push(`COMPARE.md row "${kase}": reference not found: ${ref}`);
    if (!findFile(comp, bases)) fails.push(`COMPARE.md row "${kase}": composite not found (expected under docs/reference/_compare/<topic>/): ${comp}`);
    if (!findFile(before, bases)) fails.push(`COMPARE.md row "${kase}": before image not found: ${before}`);
  });
}

if (fails.length) {
  console.error(`HAND-IN CHECK FAILED (${fails.length}) for ${folder}:\n - ` + fails.join('\n - '));
  console.error('Send this list back to the agent unread; do not read the report.');
  process.exit(1);
}
console.log(`HAND-IN CHECK PASSED for ${folder}`);
