#!/usr/bin/env node
// Merge checklist step 6 (gates8): delete the leftover temp folders of earlier test runs. Links (junctions, symlinks) inside a folder are unlinked FIRST and never followed (cleanupTmp in
// tools/test_isolation.mjs), and only a direct child of the OS temp folder whose name starts with a known test prefix is touched. Never a plain recursive delete.
// Usage: node tools/clean_test_temp.mjs [--dry-run] [--dir <temp folder>]
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { pathToFileURL } from 'node:url';
import { cleanupTmp } from './test_isolation.mjs';

export const PREFIXES = ['gatetest-', 'hooktest-', 'guards6-', 'guards7-', 'guards8-', 'guards9-', 'guards10-', 'guards11-', 'guards12-', 'scenegate-', 'bkref-', 'wttools-'];   // every mkdtemp prefix of the suites (gates10: guards9- and scenegate- were missing)
export function findLeftovers(dir) {
  let ents = []; try { ents = fs.readdirSync(dir, { withFileTypes: true }); } catch { return []; }
  return ents.filter((e) => e.isDirectory() && !e.isSymbolicLink() && PREFIXES.some((p) => e.name.startsWith(p))).map((e) => path.join(dir, e.name));
}

if (process.argv[1] && import.meta.url === pathToFileURL(path.resolve(process.argv[1])).href) {
  const a = process.argv.slice(2);
  const dry = a.includes('--dry-run');
  const di = a.indexOf('--dir');
  const dir = di >= 0 && a[di + 1] ? path.resolve(a[di + 1]) : os.tmpdir();
  const list = findLeftovers(dir);
  let gone = 0, kept = 0;
  for (const d of list) {
    if (dry) { console.log('would remove ' + d); continue; }
    // cleanupTmp only accepts a direct child of the OS temp folder; for --dir elsewhere (tests) point TEMP/TMP at it
    if (cleanupTmp(d)) gone++; else { kept++; console.log('kept (not removed): ' + d); }
  }
  console.log(`${list.length} leftover test folder(s) found in ${dir}${dry ? ' (dry run)' : `: ${gone} removed, ${kept} kept`}`);
  process.exit(kept ? 1 : 0);
}
