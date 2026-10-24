// Reader of the suite-run record (gates13, main-session decision 2; replaces the gates12 hook-based record). tools/run_suite.mjs writes one signed line to <state>/suite_records.jsonl when a
// run ENDS: worktree, binary path and sha256, exe mtime, arguments, start, end, exit code, log path and sha256. `check_handin --phase build` calls findSuiteRun() so that a suite log counts only when
// a run of the runner produced exactly these bytes, in this worktree, with a binary inside it, ending after the last code commit. No hook records anything any more (the PostToolUse recording is gone:
// a background run's hook fired at once and a typed command could be dressed up as a suite).
import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import { STATE_DIR, verifyRecord } from './lib.mjs';

export const normP = (p) => String(p).replace(/\\/g, '/').replace(/^\/([a-zA-Z])\//, '$1:/').replace(/\/+/g, '/').toLowerCase().replace(/\/+$/, '');
export const sha256Of = (file) => { try { return crypto.createHash('sha256').update(fs.readFileSync(file)).digest('hex'); } catch { return null; } };

// { ok: true, run } | { ok: false, why }
export function findSuiteRun(logFile, wt, { after = 0, stateDir = STATE_DIR } = {}) {
  const file = path.join(stateDir, 'suite_records.jsonl');
  let lines;
  try { lines = fs.readFileSync(file, 'utf8').split('\n').filter(Boolean); } catch { return { ok: false, why: `no run record file (${file}): tools/run_suite.mjs has not recorded any suite run on this machine; run \`node tools/run_suite.mjs <worktree>\`` }; }
  const sha = sha256Of(logFile); if (!sha) return { ok: false, why: `${logFile} cannot be read` };
  const lf = normP(path.resolve(logFile)); const root = normP(wt);
  const recs = [];
  for (const l of lines) { let o; try { o = JSON.parse(l); } catch { continue; } if (o && o.v === 1 && (normP(o.log || '') === lf || o.logSha === sha)) recs.push(o); }
  const how = 'make the log with `node tools/run_suite.mjs <worktree>` (foreground or background); a log made by any other command, or typed, has no record';
  if (!recs.length) return { ok: false, why: `no suite run record for ${path.basename(logFile)}: ${how}` };
  const signed = recs.filter((r) => verifyRecord(r, stateDir));
  if (!signed.length) return { ok: false, why: `the record of ${path.basename(logFile)} does not verify (not written by run_suite.mjs)` };
  const mine = signed.filter((r) => normP(r.worktree) === root);
  if (!mine.length) return { ok: false, why: `the suite record of ${path.basename(logFile)} belongs to another worktree (${signed[signed.length - 1].worktree}), not ${wt}` };
  const inBin = mine.filter((r) => normP(r.binary).startsWith(root + '/'));
  if (!inBin.length) return { ok: false, why: `the recorded suite binary (${mine[mine.length - 1].binary}) is not inside the worktree ${wt}: run the suite of this worktree` };
  const exact = inBin.filter((r) => r.logSha === sha);
  if (!exact.length) return { ok: false, why: `${path.basename(logFile)} differs from what the recorded run wrote (hash mismatch): the log was edited after the run; run the suite again` };
  const unfiltered = exact.filter((r) => !r.filter);
  if (!unfiltered.length) return { ok: false, why: `${path.basename(logFile)} is the log of a filtered run (--filter ${exact[exact.length - 1].filter}); a filtered run never counts as the suite: run it without --filter` };
  const fresh = unfiltered.filter((r) => r.endMs > after);
  if (!fresh.length) return { ok: false, why: `the recorded run that wrote ${path.basename(logFile)} ended ${unfiltered[unfiltered.length - 1].end}, before the last code commit (${new Date(after).toISOString()}): run the suite again on the committed code` };
  const run = fresh[fresh.length - 1];
  const nowSha = sha256Of(run.binary);
  if (nowSha && run.binarySha && nowSha !== run.binarySha) return { ok: false, why: `the suite binary ${run.binary} changed after the recorded run (rebuilt or edited): run the suite again on the current build` };
  return { ok: true, run: { ...run, ts: run.endMs } };
}
