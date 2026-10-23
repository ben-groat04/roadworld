// Reader of the suite-run record (gates12, main-session decision 2). The time guard (PreToolUse) and time_guard_post (PostToolUse / PostToolUseFailure) write it to
// <state>/runs.jsonl: a "start" line per suite or test run the guard let through (command, worktree root, files the command redirects its output to) and an "end" line (exit code, end time,
// sha256 of each output file at that moment). `check_handin --phase build` calls findSuiteRun() so that suite_build.log counts only when a run the hooks saw produced exactly these bytes.
import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import { STATE_DIR } from './lib.mjs';

const normP = (p) => String(p).replace(/\\/g, '/').replace(/^\/([a-zA-Z])\//, '$1:/').replace(/\/+/g, '/').toLowerCase().replace(/\/+$/, '');
export const sha256Of = (file) => { try { return crypto.createHash('sha256').update(fs.readFileSync(file)).digest('hex'); } catch { return null; } };

// { ok: true, run } | { ok: false, why } ; stateDir defaults to the hooks' own state folder
export function findSuiteRun(logFile, wt, { after = 0, stateDir = STATE_DIR } = {}) {
  let lines;
  try { lines = fs.readFileSync(path.join(stateDir, 'runs.jsonl'), 'utf8').split('\n').filter(Boolean); } catch { return { ok: false, why: `no run record file (${path.join(stateDir, 'runs.jsonl')}): the time-limit hook has not recorded any suite run on this machine` }; }
  const starts = new Map(); const ends = [];
  for (const l of lines) { let o; try { o = JSON.parse(l); } catch { continue; } if (o.ev === 'start') starts.set(o.id, o); else if (o.ev === 'end') ends.push(o); }
  const sha = sha256Of(logFile); if (!sha) return { ok: false, why: `${logFile} cannot be read` };
  const lf = normP(path.resolve(logFile)); const root = normP(wt);
  const mine = ends.filter((e) => (e.files || []).some((f) => normP(f.path) === lf) && starts.has(e.id) && normP(starts.get(e.id).root) === root);
  if (!mine.length) return { ok: false, why: `no suite run of this worktree that wrote ${path.basename(logFile)} is on record: the log has to be the redirected output of a suite run the time-limit hook saw (a Bash or PowerShell call such as \`timeout 900 <suite> > ${logFile} 2>&1\`); a log typed by hand has no record` };
  const exact = mine.filter((e) => (e.files || []).some((f) => normP(f.path) === lf && f.sha256 === sha));
  if (!exact.length) return { ok: false, why: `${path.basename(logFile)} differs from what the recorded suite run wrote (hash mismatch): the file was edited after the run; run the suite again` };
  const fresh = exact.filter((e) => e.ts > after);
  if (!fresh.length) return { ok: false, why: `the recorded suite run that wrote ${path.basename(logFile)} ended ${new Date(exact[exact.length - 1].ts).toISOString()}, before the last code commit (${new Date(after).toISOString()}): run the suite again on the committed code` };
  const run = fresh[fresh.length - 1];
  return { ok: true, run: { ...run, command: starts.get(run.id).command || '' } };
}
