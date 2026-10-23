#!/usr/bin/env node
// gates9: real-payload tables for the send-back items of docs/progress/gates8/AUDIT.md (F1-F3 false blocks, H1-H7 holes) and the main session's decisions of 23 Oct 2026 (brief gates9).
//   F1-F3  lock_guard no longer refuses ordinary work on tests/ (no list, no tests/locked folder: it protects nothing; a glob or folder counts only when it really reaches a locked file; a move is judged
//          by its source, a destination only when it IS a locked file); read-only inline python / node passes; the refusal text names the file and says BLOCKED only for a real test edit
//   H1     LOCKED_TESTS.txt and the prior AUDIT.md are protected like the locked tests (Write, Edit, rm, mv, sed -i, git rm, Set-Content, rm -r / mv of the folder)
//   H2     a bad list (UTF-16 is decoded, globs are supported, an entry that matches no file / a list that locks nothing / a missing list the audit speaks of) is a LOCK-LIST-BAD line, never silent
//   H3     delete guard: a junction inside a build output folder is found (rm -rf build, rm -rf *, git clean -fdx, rd /s /q, robocopy /MIR, Remove-Item -Recurse); the walk is bounded and the bound is logged
//   H4     checkpoint image: inside the hand-in folder or docs/reference/_compare/<round>, newer than the run start, >= 10 KB, valid PNG / JPEG header
//   H5     a CHECKPOINT.md older than the latest commit of the worktree is ignored (logged) at the final hand-in
//   H6     checkpoint window 30 s, one checkpoint per ROUND (hand-in folder)
//   H7     in-place formatters (clang-format -i, prettier --write, dos2unix, awk -i inplace, perl -i, ruby -i) count as writes in the lock guard
// Where the expected values come from: the gates8 AUDIT.md items named in the labels and the main session's SEND-BACK NOTE of 23 Oct (brief gates9). Nothing pins a fault. Payloads use backslash cwds as the
// harness sends them on Windows. Fixtures live in a temp folder with REAL junctions; each is unlinked (cmd rmdir semantics, never followed) before the folder is removed. No test writes the live gate.log.
// Usage: node tools/hooks/test_guards9.mjs
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import zlib from 'node:zlib';
import crypto from 'node:crypto';
import { spawnSync } from 'node:child_process';
import { fileURLToPath, pathToFileURL } from 'node:url';
import { isolateState, cleanupTmp } from '../test_isolation.mjs';

const src = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..', '..');
const tmp = fs.mkdtempSync(path.join(os.tmpdir(), 'guards9-'));
const isolation = isolateState(tmp, src);
let pass = 0, fail = 0; const failedNames = [];
const t = (name, cond, detail = '') => { if (cond) { pass++; console.log('  ok   ' + name); } else { fail++; failedNames.push(name); console.log('  FAIL ' + name + (detail ? '\n       ' + String(detail).replace(/\n/g, '\n       ') : '')); } };
const mk = (p) => fs.mkdirSync(p, { recursive: true });
const wr = (p, s = 'x') => { mk(path.dirname(p)); fs.writeFileSync(p, s); };
const gateLog = () => { try { return fs.readFileSync(path.join(isolation.stateDir, 'gate.log'), 'utf8').trim().split('\n').filter(Boolean).map((l) => { try { return JSON.parse(l); } catch { return {}; } }); } catch { return []; } };

// ---------------------------------------------------------------- fixture: a temp "main checkout" with registered worktrees
const main = path.join(tmp, 'roadworld'); const M = main.replace(/\\/g, '/');
mk(path.join(main, '.git', 'worktrees')); mk(path.join(main, 'docs', 'reference', 'aerial')); mk(path.join(main, '.claude', 'state'));
wr(path.join(main, 'docs', 'reference', 'aerial', 'p1.jpg'), 'photo'); wr(path.join(main, '.claude', 'state', 'gate.log'), '{}\n');
const photos = path.join(tmp, 'photos'); wr(path.join(photos, 'k.jpg'), 'k');
const wtRoot = path.join(tmp, 'roadworld-wt');
const addWt = (n) => {
  const w = path.join(wtRoot, n); mk(path.join(main, '.git', 'worktrees', n)); mk(path.join(w, 'build', 'sub')); mk(path.join(w, 'docs')); mk(path.join(w, 'tests')); wr(path.join(w, 'build', 'a.obj'), 'o');
  fs.writeFileSync(path.join(w, '.git'), 'gitdir: ' + path.join(main, '.git', 'worktrees', n) + '\n');
  fs.writeFileSync(path.join(main, '.git', 'worktrees', n, 'gitdir'), path.join(w, '.git') + '\n'); fs.writeFileSync(path.join(main, '.git', 'worktrees', n, 'commondir'), '../..\n');
  return w;
};
const elsewhere = path.join(tmp, 'elsewhere'); mk(elsewhere);
mk(path.join(tmp, 'tempdir')); process.env.TEMP = path.join(tmp, 'tempdir'); process.env.TMP = process.env.TEMP; delete process.env.TMPDIR;
const dgm = await import(pathToFileURL(path.join(src, 'tools/hooks/delete_guard_impl.mjs')).href);
const dg = (command, cwd) => dgm.analyse(command, cwd, 0, null, main).length ? 2 : 0;

// ---------------------------------------------------------------- lock_guard as a process with real transcripts
const LOCK_MSG = 'locked test: written by the auditor; make it pass, or write BLOCKED';
const trDir = path.join(tmp, 'tr9'); mk(path.join(trDir, 'subagents')); const trMain = trDir + '.jsonl'; fs.writeFileSync(trMain, '');
const brief = (txt) => JSON.stringify({ type: 'user', timestamp: new Date().toISOString(), message: { content: txt } }) + '\n';
const aw = (id, txt) => fs.writeFileSync(path.join(trDir, 'subagents', 'agent-' + id + '.jsonl'), brief(txt));
// a lock worktree: tests/, the prior round's folder with AUDIT.md and (optionally) LOCKED_TESTS.txt, and a builder agent with HANDIN_DIR + PRIOR-AUDIT
function lockWt(name, { list = null, audit = '# audit\n', lockedDir = true, priorAudit = true } = {}) {
  const w = addWt(name); const prior = path.join(w, 'docs', 'progress', 'prev'); mk(prior); fs.writeFileSync(path.join(prior, 'AUDIT.md'), audit);
  if (lockedDir) wr(path.join(w, 'tests', 'locked', 'a.mjs'), 'a');
  for (const f of ['listed_test.mjs', 'other.mjs', 'new_test.cpp', 'x.tmp', 'y.log', 'test_g1.mjs', 'test_g2.mjs', 'helper.cpp']) wr(path.join(w, 'tests', f), 'f');
  if (list !== null) fs.writeFileSync(path.join(prior, 'LOCKED_TESTS.txt'), list);
  const hand = path.join(w, 'docs', 'progress', 'r9'); mk(hand);
  const id = 'b_' + name;
  aw(id, `AGENT-CONTRACT-MARKER: v1\nHANDIN_DIR: ${hand}\n${priorAudit ? 'PRIOR-AUDIT: ' + path.join(prior, 'AUDIT.md') + '\n' : ''}## Owner's words\n"x"`);
  return { w, prior, hand, id };
}
const lk = (id, tool, ti, cwd) => {
  const input = { session_id: 's1', transcript_path: trMain, cwd, hook_event_name: 'PreToolUse', tool_name: tool, tool_input: ti, tool_use_id: 'toolu_01', ...(id ? { agent_id: id, agent_type: 'general-purpose' } : {}) };
  return spawnSync(process.execPath, [path.join(src, 'tools', 'hooks', 'lock_guard.mjs')], { input: JSON.stringify(input), cwd: elsewhere, encoding: 'utf8', timeout: 60000, env: { ...process.env, CP_MAIN_ROOT: main } });
};
const W = (p) => ['Write', { file_path: p, content: 'x' }], E = (p) => ['Edit', { file_path: p, old_string: 'a', new_string: 'b' }], B = (c) => ['Bash', { command: c }], P = (c) => ['PowerShell', { command: c }];
const refused = (r) => r.status === 2 && /REFUSED by the lock guard/.test(r.stderr);
function run(title, cases) {
  const bad = [];
  for (const [id, tool, ti, want, label, cwd] of cases) {
    const r = lk(id, tool, ti, cwd);
    const got = want === 2 ? (refused(r) ? 2 : `status ${r.status} ${r.stderr.slice(0, 100)}`) : (r.status === 0 && r.stderr === '' ? 0 : `status ${r.status} ${r.stderr.slice(0, 200)}`);
    if (got !== want) bad.push(`${label}: ${String(ti.command || ti.file_path).slice(0, 100)}: ${got}, wanted ${want}`);
  }
  t(`${title}: ${cases.length} real payloads`, bad.length === 0, bad.join('\n'));
}

// ================================================================ F1 / F2 / F3
console.log('gates9 F1: ordinary work on tests/ is not refused (no list, no tests/locked folder; and with a list)');
const N = lockWt('noLock', { list: null, lockedDir: false, priorAudit: false });          // builder with nothing locked
const L = lockWt('lockA', { list: '# the auditor wrote these\ntests/listed_test.mjs\n' });  // builder with a list and a tests/locked folder
const f1 = [
  'mv new_test.cpp tests/', 'mv new_test.cpp tests', 'mv a.cpp b.cpp tests/', 'rm tests/*.tmp', 'sed -i s/foo/bar/ tests/*.cpp', 'find tests -name "*.o" -delete', 'find . -name "*.tmp" -delete', 'cd tests && rm -f *.log',
  'git clean -n tests', 'rm tests/x.tmp', 'rm -rf tests/y.log', 'cp new_test.cpp tests/', 'echo x > tests/helper.cpp', 'find tests -name "*.log" -delete',
];
run('F1 builder with NO list and NO tests/locked: everything below is allowed', [
  ...f1.map((c) => [N.id, ...B(c), 0, 'F1 nolock: ' + c, N.w]),
  [N.id, ...P('Move-Item new_test.cpp tests\\'), 0, 'F1 nolock: Move-Item new_test.cpp tests\\', N.w], [N.id, ...P('Remove-Item tests\\*.obj'), 0, 'F1 nolock: Remove-Item tests\\*.obj', N.w],
  [N.id, ...B('rm -rf tests'), 0, 'F1 nolock: rm -rf tests (holds nothing locked)', N.w], [N.id, ...B('mv tests tests_old'), 0, 'F1 nolock: mv tests tests_old', N.w], [N.id, ...B('rm -rf *'), 0, 'F1 nolock: rm -rf * (lock guard only)', N.w],
  [N.id, ...W(path.join(N.w, 'tests', 'new.cpp')), 0, 'F1 nolock: Write a test', N.w], [N.id, ...W(path.join(N.w, 'tests', 'listed_test.mjs')), 0, 'F1 nolock: Write a file a list would have named (there is no list)', N.w],
]);
run('F1 builder WITH a list and a tests/locked folder: ordinary work still passes, a real hit does not', [
  ...f1.map((c) => [L.id, ...B(c), 0, 'F1 locked-wt: ' + c, L.w]),
  [L.id, ...P('Move-Item new_test.cpp tests\\'), 0, 'F1 locked-wt: Move-Item new_test.cpp tests\\', L.w], [L.id, ...P('Remove-Item tests\\*.obj'), 0, 'F1 locked-wt: Remove-Item tests\\*.obj', L.w],
  [L.id, ...B('rm tests/*.mjs'), 2, 'F1 a glob that matches the listed file stays refused', L.w], [L.id, ...B('rm tests/*'), 2, 'F1 rm tests/* reaches tests/locked and the listed file', L.w], [L.id, ...B('rm -rf tests'), 2, 'F1 rm -rf tests holds locked files', L.w],
  [L.id, ...B('mv tests tests_old'), 2, 'F1 mv tests tests_old holds locked files', L.w], [L.id, ...B('find tests -name "*.mjs" -delete'), 2, 'F1 find -name *.mjs -delete would delete the listed file', L.w],
  [L.id, ...B('find tests -delete'), 2, 'F1 find tests -delete (no name filter)', L.w], [L.id, ...B('mv /tmp/x tests/listed_test.mjs'), 2, 'F1 mv ONTO a locked file is a write', L.w], [L.id, ...B('mv other.mjs tests/'), 0, 'F1 mv an unlocked file into tests/ (destination is a folder)', L.w],
  [L.id, ...B('mv tests/listed_test.mjs /tmp/y'), 2, 'F1 mv a locked file away (source)', L.w], [L.id, ...B('cd tests && rm -f *.mjs'), 2, 'F1 cd tests && rm -f *.mjs', L.w], [L.id, ...B('git clean -fd tests'), 2, 'F1 git clean -fd tests (holds locked files)', L.w],
  [L.id, ...B('rm tests/listed_test.mjs'), 2, 'F1 rm the listed file', L.w],
]);

console.log('gates9 F2: inline python / node that only READS a locked test is allowed; a write is not');
run('F2 inline code', [
  [L.id, ...B(`python -c "import shutil; shutil.copy('tests/locked/a.mjs','/tmp/a')"`), 0, 'F2 shutil.copy FROM the locked file', L.w], [L.id, ...B(`python -c "print(open('tests/locked/a.mjs').read().replace('a','b'))"`), 0, 'F2 open().read().replace()', L.w],
  [L.id, ...B(`node -e "fs.writeFileSync('out.txt', fs.readFileSync('tests/locked/a.mjs','utf8'))"`), 0, 'F2 writeFileSync(out.txt, readFileSync(locked))', L.w], [L.id, ...B(`node -e "console.log(require('fs').readFileSync('tests/locked/a.mjs','utf8').replace(/a/g,'b'))"`), 0, 'F2 node read + replace', L.w],
  [L.id, ...B(`python -c "import shutil; shutil.move('x.txt','y.txt'); print(open('tests/locked/a.mjs').read())"`), 0, 'F2 move of another file, read of the locked one', L.w],
  [L.id, ...B(`python -c "import shutil; shutil.copy('/tmp/a','tests/locked/a.mjs')"`), 2, 'F2 shutil.copy ONTO the locked file', L.w], [L.id, ...B(`python -c "import shutil; shutil.move('tests/locked/a.mjs','/tmp/x')"`), 2, 'F2 shutil.move away', L.w],
  [L.id, ...B(`node -e "fs.renameSync('tests/locked/a.mjs','x')"`), 2, 'F2 renameSync', L.w], [L.id, ...B(`node -e "fs.copyFileSync('x','tests/locked/a.mjs')"`), 2, 'F2 copyFileSync onto', L.w],
  [L.id, ...B(`python -c "from pathlib import Path; Path('tests/locked/a.mjs').write_text('x')"`), 2, 'F2 Path.write_text', L.w], [L.id, ...B(`python -c "import os; os.replace('x','tests/locked/a.mjs')"`), 2, 'F2 os.replace onto', L.w],
  [L.id, ...B(`node -e "fs.writeFileSync('tests/listed_test.mjs','x')"`), 2, 'F2 writeFileSync of a listed file', L.w], [L.id, ...B(`python -c "open('tests/locked/a.mjs','w').write('x')"`), 2, 'F2 open(...,w)', L.w],
  [L.id, ...B(`node -e "fs.unlinkSync('tests/locked/a.mjs')"`), 2, 'F2 unlinkSync', L.w], [L.id, ...B(`python -c "import shutil; shutil.rmtree('tests/locked')"`), 2, 'F2 rmtree of the folder', L.w],
]);

console.log('gates9 F3: the refusal names the exact file; BLOCKED only for a real test edit');
{
  let r = lk(L.id, ...B('rm tests/*.mjs'), L.w);
  t('F3 a glob refusal names the locked file it reaches (tests/listed_test.mjs or tests/locked/a.mjs)', refused(r) && /listed_test\.mjs|locked[\\/]a\.mjs/.test(r.stderr), r.stderr);
  r = lk(L.id, ...W(path.join(L.w, 'tests', 'listed_test.mjs')), L.w);
  t('F3 a real edit of a locked test: the text says make it pass or write BLOCKED, and names the file', refused(r) && r.stderr.includes(LOCK_MSG) && /listed_test\.mjs/.test(r.stderr), r.stderr);
  r = lk(L.id, ...W(path.join(L.prior, 'LOCKED_TESTS.txt')), L.w);
  t('F3 an edit of the lock list: refused, names the list, and does NOT tell the builder to write BLOCKED', refused(r) && /LOCKED_TESTS\.txt/.test(r.stderr) && !/BLOCKED/.test(r.stderr), r.stderr);
  r = lk(L.id, ...B('rm -rf tests'), L.w);
  t('F3 rm -rf tests: the reason names a locked file it holds, not just "the folder"', refused(r) && /listed_test\.mjs|locked[\\/]a\.mjs/.test(r.stderr), r.stderr);
}

// ================================================================ H1
console.log('gates9 H1: the lock list and the prior AUDIT.md are protected like the locked tests (builders only)');
const lst = path.join(L.prior, 'LOCKED_TESTS.txt'), aud = path.join(L.prior, 'AUDIT.md');
aw('aud9', 'NO-CONTRACT: audit the hand-in of the last round and write the tests that pin its faults\nAUDIT-OF: ' + L.hand); aw('clerk9', 'NO-CONTRACT: merge the branch and update the queue file by hand');
run('H1 attacks on the list and the audit (every form from the audit is refused)', [
  [L.id, ...W(lst), 2, 'H1 Write the list', L.w], [L.id, ...E(lst), 2, 'H1 Edit the list', L.w], [L.id, ...W(aud), 2, 'H1 Write the prior AUDIT.md', L.w], [L.id, ...E(aud), 2, 'H1 Edit the prior AUDIT.md', L.w],
  [L.id, ...B('rm docs/progress/prev/LOCKED_TESTS.txt'), 2, 'H1 rm the list', L.w], [L.id, ...B('mv docs/progress/prev/LOCKED_TESTS.txt /tmp/x'), 2, 'H1 mv the list away', L.w], [L.id, ...B('cp /dev/null docs/progress/prev/LOCKED_TESTS.txt'), 2, 'H1 cp /dev/null onto the list', L.w],
  [L.id, ...B('echo > docs/progress/prev/LOCKED_TESTS.txt'), 2, 'H1 echo > the list', L.w], [L.id, ...B('sed -i /listed/d docs/progress/prev/LOCKED_TESTS.txt'), 2, 'H1 sed -i /listed/d the list', L.w], [L.id, ...B('git rm docs/progress/prev/LOCKED_TESTS.txt'), 2, 'H1 git rm the list', L.w],
  [L.id, ...P('Set-Content docs\\progress\\prev\\LOCKED_TESTS.txt ""'), 2, 'H1 Set-Content the list', L.w], [L.id, ...P('Remove-Item docs\\progress\\prev\\LOCKED_TESTS.txt'), 2, 'H1 Remove-Item the list', L.w],
  [L.id, ...B('rm -r docs/progress/prev'), 2, 'H1 rm -r the prior folder', L.w], [L.id, ...B('mv docs/progress/prev docs/progress/prev2'), 2, 'H1 mv the prior folder', L.w], [L.id, ...P('Remove-Item -Recurse -Force docs\\progress\\prev'), 2, 'H1 Remove-Item -Recurse the prior folder', L.w],
  [L.id, ...B('rm docs/progress/prev/AUDIT.md'), 2, 'H1 rm the prior AUDIT.md', L.w], [L.id, ...B('echo x >> docs/progress/prev/AUDIT.md'), 2, 'H1 append to the prior AUDIT.md', L.w], [L.id, ...B('git rm -r docs/progress/prev'), 2, 'H1 git rm -r the prior folder', L.w],
  [L.id, ...B('cd docs/progress/prev && rm LOCKED_TESTS.txt'), 2, 'H1 cd prev && rm', L.w], [L.id, ...B('rm -rf docs/progress'), 2, 'H1 rm -rf docs/progress (holds the prior folder)', L.w],
  [L.id, ...B(`node -e "fs.writeFileSync('docs/progress/prev/LOCKED_TESTS.txt','')"`), 2, 'H1 node writeFileSync the list', L.w],
  // what must stay free for the builder
  [L.id, ...B('cat docs/progress/prev/LOCKED_TESTS.txt'), 0, 'H1 cat the list (read)', L.w], [L.id, ...W(path.join(L.hand, 'STATUS.md')), 0, 'H1 Write its own STATUS.md', L.w], [L.id, ...B('rm -rf docs/progress/r9'), 0, 'H1 rm -rf its own hand-in folder', L.w],
  [L.id, ...B('cp docs/progress/prev/AUDIT.md /tmp/a'), 0, 'H1 cp FROM the prior audit', L.w],
  ['aud9', ...W(lst), 0, 'H1 AUDITOR (AUDIT-OF) may write the list', L.w], ['aud9', ...W(aud), 0, 'H1 auditor may write AUDIT.md', L.w], ['clerk9', ...B('rm docs/progress/prev/LOCKED_TESTS.txt'), 0, 'H1 clerk is free', L.w],
  [null, ...W(lst), 0, 'H1 main session (no agent_id) is free', L.w], [null, ...B('rm -r docs/progress/prev'), 0, 'H1 main session rm -r the prior folder', L.w],
]);

// ================================================================ H7
console.log('gates9 H7: in-place formatters are writes');
run('H7 formatters over a locked test', [
  [L.id, ...B('clang-format -i tests/listed_test.mjs'), 2, 'H7 clang-format -i', L.w], [L.id, ...B('clang-format -i -style=file tests/*.mjs'), 2, 'H7 clang-format -i tests/*.mjs', L.w], [L.id, ...B('npx prettier --write tests/'), 2, 'H7 npx prettier --write tests/', L.w],
  [L.id, ...B('prettier -w tests/listed_test.mjs'), 2, 'H7 prettier -w', L.w], [L.id, ...B('prettier --write tests/locked/a.mjs'), 2, 'H7 prettier --write locked file', L.w], [L.id, ...B('dos2unix tests/listed_test.mjs'), 2, 'H7 dos2unix', L.w],
  [L.id, ...B("awk -i inplace '{print}' tests/listed_test.mjs"), 2, 'H7 awk -i inplace', L.w], [L.id, ...B("gawk -i inplace '{print}' tests/locked/a.mjs"), 2, 'H7 gawk -i inplace', L.w], [L.id, ...B("perl -i -pe 's/a/b/' tests/listed_test.mjs"), 2, 'H7 perl -i -pe', L.w],
  [L.id, ...B("perl -i.bak -pe 's/a/b/' tests/listed_test.mjs"), 2, 'H7 perl -i.bak', L.w], [L.id, ...B("ruby -i -pe 'gsub(/a/,\"b\")' tests/listed_test.mjs"), 2, 'H7 ruby -i', L.w], [L.id, ...B('prettier --write .'), 2, 'H7 prettier --write . from the worktree root', L.w],
  [L.id, ...B('unix2dos tests/locked/a.mjs'), 2, 'H7 unix2dos', L.w], [L.id, ...B('npx clang-format -i tests/listed_test.mjs'), 2, 'H7 npx clang-format -i', L.w],
  [L.id, ...B('clang-format -i tests/helper.cpp'), 0, 'H7 clang-format -i on an unlocked test', L.w], [L.id, ...B('clang-format tests/listed_test.mjs'), 0, 'H7 clang-format without -i prints to stdout', L.w], [L.id, ...B('prettier --check tests/'), 0, 'H7 prettier --check', L.w],
  [L.id, ...B('prettier --write sim_core'), 0, 'H7 prettier --write on another folder', L.w], [L.id, ...B("awk '{print}' tests/listed_test.mjs"), 0, 'H7 awk without -i', L.w], [L.id, ...B('dos2unix tests/helper.cpp'), 0, 'H7 dos2unix on an unlocked file', L.w],
  [L.id, ...B("perl -ne 'print' tests/listed_test.mjs"), 0, 'H7 perl -ne (read)', L.w], [L.id, ...B("ruby -ne 'print' tests/listed_test.mjs"), 0, 'H7 ruby -ne (read)', L.w],
]);

// ================================================================ H2
console.log('gates9 H2: a bad list is loud (LOCK-LIST-BAD), a UTF-16 list is decoded, globs are supported');
{
  const u16 = Buffer.concat([Buffer.from([0xff, 0xfe]), Buffer.from('tests/listed_test.mjs\r\n', 'utf16le')]);
  const U = lockWt('lockU16', { list: u16, lockedDir: false });
  run('H2 a UTF-16LE list (BOM) works', [[U.id, ...W(path.join(U.w, 'tests', 'listed_test.mjs')), 2, 'H2 UTF-16 list: Write the listed file', U.w], [U.id, ...W(path.join(U.w, 'tests', 'other.mjs')), 0, 'H2 UTF-16 list: an unlisted file is free', U.w]]);
  const u16nb = Buffer.from('tests/listed_test.mjs\r\n', 'utf16le');
  const U2 = lockWt('lockU16b', { list: u16nb, lockedDir: false });
  run('H2 a UTF-16LE list without a BOM works', [[U2.id, ...W(path.join(U2.w, 'tests', 'listed_test.mjs')), 2, 'H2 UTF-16 (no BOM): Write the listed file', U2.w]]);
  const G = lockWt('lockGlob', { list: 'tests/test_g*.mjs\r\n', lockedDir: false });
  run('H2 a glob entry is supported (what it matches is locked)', [
    [G.id, ...W(path.join(G.w, 'tests', 'test_g1.mjs')), 2, 'H2 glob: Write test_g1.mjs', G.w], [G.id, ...B('rm tests/test_g2.mjs'), 2, 'H2 glob: rm test_g2.mjs', G.w], [G.id, ...B('sed -i s/a/b/ tests/test_g1.mjs'), 2, 'H2 glob: sed -i', G.w],
    [G.id, ...W(path.join(G.w, 'tests', 'other.mjs')), 0, 'H2 glob: an unmatched file is free', G.w], [G.id, ...B('rm tests/*.tmp'), 0, 'H2 glob: rm tests/*.tmp', G.w], [G.id, ...B('rm tests/*.mjs'), 2, 'H2 glob: rm tests/*.mjs reaches the matched files', G.w],
  ]);
  const lines0 = gateLog().length;
  const NM = lockWt('lockNoMatch', { list: 'tests/missing_test.mjs\n', lockedDir: false });
  let r = lk(NM.id, ...B('echo hi'), NM.w);
  const bad1 = gateLog().filter((e) => e.gate === 'lock_guard' && /LOCK-LIST-BAD/.test(e.reason || '') && e.agent_id === NM.id);
  t('H2 an entry that matches no file: a LOCK-LIST-BAD line (with FAIL-OPEN so the main session gets a GATE NOTICE) in gate.log', bad1.length === 1 && /FAIL-OPEN/.test(bad1[0].reason) && /missing_test\.mjs/.test(JSON.stringify(bad1[0])), JSON.stringify(gateLog().slice(lines0)));
  t('H2 ... and a visible warning for the builder (systemMessage on stdout), exit 0', r.status === 0 && /LOCK-LIST-BAD/.test(r.stdout), r.stdout + r.stderr);
  r = lk(NM.id, ...B('echo hi again'), NM.w);
  t('H2 ... the same problem is logged once per agent, not on every command', gateLog().filter((e) => e.gate === 'lock_guard' && /LOCK-LIST-BAD/.test(e.reason || '') && e.agent_id === NM.id).length === 1);
  const MS = lockWt('lockMissing', { list: null, audit: '# audit\n\nLocked tests: see LOCKED_TESTS.txt beside this file.\n', lockedDir: false });
  lk(MS.id, ...B('echo hi'), MS.w);
  t('H2 PRIOR-AUDIT given, the audit speaks of LOCKED_TESTS.txt but the file is missing: LOCK-LIST-BAD', gateLog().some((e) => e.gate === 'lock_guard' && /LOCK-LIST-BAD/.test(e.reason || '') && e.agent_id === MS.id));
  const MQ = lockWt('lockQuiet', { list: null, audit: '# audit\nNo locked tests this round.\n', lockedDir: false });
  r = lk(MQ.id, ...B('echo hi'), MQ.w);
  t('H2 PRIOR-AUDIT given, no list and the audit does not speak of one: silent (nothing is locked, nothing is wrong)', !gateLog().some((e) => e.agent_id === MQ.id && /LOCK-LIST-BAD/.test(e.reason || '')) && r.stdout === '' && r.status === 0);
  const EM = lockWt('lockEmpty', { list: '# only a comment\n\n', lockedDir: false }); lk(EM.id, ...B('echo hi'), EM.w);
  t('H2 a list with no entry (locks nothing): LOCK-LIST-BAD', gateLog().some((e) => e.gate === 'lock_guard' && /LOCK-LIST-BAD/.test(e.reason || '') && e.agent_id === EM.id));
  const GL = lockWt('lockGlobNone', { list: 'tests/nothing_*.mjs\n', lockedDir: false }); lk(GL.id, ...B('echo hi'), GL.w);
  t('H2 a glob that matches no file: LOCK-LIST-BAD', gateLog().some((e) => e.gate === 'lock_guard' && /LOCK-LIST-BAD/.test(e.reason || '') && e.agent_id === GL.id));
  const OK = lockWt('lockGood', { list: 'tests/listed_test.mjs\n', lockedDir: false }); r = lk(OK.id, ...B('echo hi'), OK.w);
  t('H2 a good list: no LOCK-LIST-BAD line, no warning', !gateLog().some((e) => e.agent_id === OK.id && /LOCK-LIST-BAD/.test(e.reason || '')) && r.stdout === '');
}

// ================================================================ H3
console.log('gates9 H3: a junction inside a build output folder is found by the delete guard (real junctions)');
{
  const wB = addWt('jbuild'); fs.symlinkSync(photos, path.join(wB, 'build', 'ref'), 'junction');
  const wI = addWt('jintermediate'); mk(path.join(wI, 'unreal', 'CityPainter', 'Intermediate', 'Build')); fs.symlinkSync(photos, path.join(wI, 'unreal', 'CityPainter', 'Intermediate', 'ref'), 'junction');
  const wD = addWt('jdeep'); mk(path.join(wD, 'build', 'a', 'b', 'c')); fs.symlinkSync(photos, path.join(wD, 'build', 'a', 'b', 'c', 'ref'), 'junction');
  const wC = addWt('jclean'); for (let i = 0; i < 1200; i++) wr(path.join(wC, 'build', 'sub', 'f' + i + '.obj'), 'o'); mk(path.join(wC, 'unreal', 'CityPainter', 'Intermediate')); wr(path.join(wC, 'unreal', 'CityPainter', 'Intermediate', 'x.obj'), 'o');
  const empty = path.join(tmp, 'empty'); mk(empty);
  const cw = { wB, wI, wD, wC };
  const cases = [
    ['rm -rf build', 'wB', 2, 'rm -rf build'], ['rm -rf build/', 'wB', 2, 'rm -rf build/'], ['rm -rf *', 'wB', 2, 'rm -rf *'], ['git clean -fdx', 'wB', 2, 'git clean -fdx'], ['git clean -fdx build', 'wB', 2, 'git clean -fdx build'],
    ['cmd /c rd /s /q build', 'wB', 2, 'cmd /c rd /s /q build'], ['rmdir /s /q build', 'wB', 2, 'rmdir /s /q build'], [`robocopy ${empty} build /MIR`, 'wB', 2, 'robocopy empty build /MIR'],
    ['Remove-Item -Recurse -Force build', 'wB', 2, 'Remove-Item -Recurse -Force build'], ['rm -rf build/*', 'wB', 2, 'rm -rf build/*'],
    ['rm -rf unreal', 'wI', 2, 'rm -rf unreal (link in Intermediate)'], ['rm -rf unreal/CityPainter/Intermediate', 'wI', 2, 'rm -rf Intermediate'], ['git clean -fdx', 'wI', 2, 'git clean -fdx (link in Intermediate)'],
    ['Remove-Item -Recurse -Force unreal', 'wI', 2, 'Remove-Item -Recurse unreal'], ['rm -rf build', 'wD', 2, 'rm -rf build (link 4 levels down)'],
    ['rm -rf build', 'wC', 0, 'a plain clean build of a link-free folder passes'], ['git clean -fdx', 'wC', 0, 'git clean -fdx in a link-free worktree passes'], ['rm -rf unreal', 'wC', 0, 'rm -rf unreal (no link) passes'],
    ['cmd /c rd /s /q build', 'wC', 0, 'rd /s /q build (no link) passes'], [`robocopy ${empty} build /MIR`, 'wC', 0, 'robocopy /MIR into a link-free build passes'], ['Remove-Item -Recurse -Force build', 'wC', 0, 'Remove-Item -Recurse build (no link) passes'],
  ];
  const bad = []; const t0 = Date.now();
  for (const [c, w, want, label] of cases) { let got; try { got = dg(c, cw[w]); } catch (e) { got = 'THROW ' + e.message; } if (got !== want) bad.push(`${label}: ${c} (cwd ${w}): ${got}, wanted ${want}`); }
  t(`H3 ${cases.length} delete-guard cases on real junctions inside build folders (refused) and link-free folders (allowed)`, bad.length === 0, bad.join('\n'));
  const tClean = (() => { const a = Date.now(); dg('rm -rf build', wC); dg('git clean -fdx', wC); return Date.now() - a; })();
  t(`H3 the clean-build path stays fast (two checks on a 1200-file build folder: ${tClean} ms, limit 1500)`, tClean < 1500);
  // the bound: depth and time; hitting it is logged and does not refuse
  const lb = dgm.linksBelow;
  const before = gateLog().length;
  const r1 = lb(wC, { buildMs: 0 });
  t('H3 a zero time budget for build folders reports buildBound, finds no link, does not refuse', r1.buildBound === true && r1.length === 0 && !r1.capped, JSON.stringify({ n: r1.length, b: r1.buildBound, c: r1.capped }));
  t('H3 ... and the bound is logged (a gate.log line that says the scan was bounded)', gateLog().slice(before).some((e) => /scan bound/i.test(e.reason || '') && /delete_guard/.test(e.gate || '')), JSON.stringify(gateLog().slice(before)));
  // gates10 (gates9 audit fault 1): there is no depth bound any more; the link 4 levels down is found with any options (the old test pinned the hole)
  const r2 = lb(wD, { buildDepth: 2 });
  t('H3 a link below the old depth bound IS found (no depth bound since gates10)', r2.length === 1, JSON.stringify({ n: r2.length, b: r2.buildBound }));
  const r3 = lb(wD);
  t('H3 within the default depth the same link IS found', r3.length === 1, JSON.stringify(r3));
}

// ================================================================ checkpoint: H4, H5, H6 through the real finish hook and check_handin --checkpoint
console.log('gates9 H4-H6 checkpoint hand-back (handin_gate.mjs and check_handin.mjs --checkpoint, real transcripts, real git repos)');
{
  const root = path.join(tmp, 'main2'); mk(path.join(root, '.git')); mk(path.join(root, 'tools', 'hooks')); mk(path.join(root, 'docs', 'templates')); mk(path.join(root, 'docs', 'reference', 'aerial'));
  for (const f of fs.readdirSync(path.join(src, 'tools'))) if (/\.mjs$/.test(f)) fs.copyFileSync(path.join(src, 'tools', f), path.join(root, 'tools', f));
  for (const f of fs.readdirSync(path.join(src, 'tools', 'hooks'))) if (/\.mjs$/.test(f)) fs.copyFileSync(path.join(src, 'tools', 'hooks', f), path.join(root, 'tools', 'hooks', f));
  fs.copyFileSync(path.join(src, 'docs', 'AGENT_CONTRACT.md'), path.join(root, 'docs', 'AGENT_CONTRACT.md'));
  const state = path.join(tmp, 'state-e9'); mk(state);
  const env = { ...process.env, CP_GATE_STATE_DIR: state }; delete env.CP_MAIN_ROOT;
  const logOf = () => { try { return fs.readFileSync(path.join(state, 'gate.log'), 'utf8').trim().split('\n').filter(Boolean).map((l) => JSON.parse(l)); } catch { return []; } };
  const lastHandin = () => logOf().filter((l) => l.gate === 'handin').pop() || {};
  const J = (s) => { try { return JSON.parse(s); } catch { return {}; } };
  const crcT = (() => { const tb = []; for (let n = 0; n < 256; n++) { let c = n; for (let k = 0; k < 8; k++) c = c & 1 ? 0xedb88320 ^ (c >>> 1) : c >>> 1; tb[n] = c >>> 0; } return tb; })();
  const crc32 = (buf) => { let c = 0xffffffff; for (const b of buf) c = crcT[(c ^ b) & 255] ^ (c >>> 8); return (c ^ 0xffffffff) >>> 0; };
  const chunk = (type, data) => { const len = Buffer.alloc(4); len.writeUInt32BE(data.length); const td = Buffer.concat([Buffer.from(type), data]); const crc = Buffer.alloc(4); crc.writeUInt32BE(crc32(td)); return Buffer.concat([len, td, crc]); };
  const makePng = (w = 90, h = 90) => { const rows = []; for (let y = 0; y < h; y++) rows.push(Buffer.concat([Buffer.from([0]), crypto.randomBytes(w * 3)])); const ihdr = Buffer.alloc(13); ihdr.writeUInt32BE(w, 0); ihdr.writeUInt32BE(h, 4); ihdr[8] = 8; ihdr[9] = 2;
    return Buffer.concat([Buffer.from('89504e470d0a1a0a', 'hex'), chunk('IHDR', ihdr), chunk('IDAT', zlib.deflateSync(Buffer.concat(rows), { level: 0 })), chunk('IEND', Buffer.alloc(0))]); };
  const png = makePng();
  const git = (w, args, envx = {}) => spawnSync('git', ['-C', w, '-c', 'user.name=t', '-c', 'user.email=t@t', '-c', 'commit.gpgsign=false', ...args], { encoding: 'utf8', env: { ...process.env, ...envx } });
  // one round = one worktree (a real git repo) with its own hand-in folder; commitAgoMs: date of the latest commit
  const mkRound = (name, { commitAgoMs = 3600000 } = {}) => {
    const w = path.join(tmp, 'wt-' + name); const hd = path.join(w, 'docs', 'progress', name); mk(hd);
    git(w, ['init', '-q']); wr(path.join(w, 'f.txt'), 'f'); git(w, ['add', 'f.txt']);
    const d = new Date(Date.now() - commitAgoMs).toISOString(); const c = git(w, ['commit', '-q', '-m', 'x'], { GIT_COMMITTER_DATE: d, GIT_AUTHOR_DATE: d });
    return { w, hd, cp: path.join(hd, 'CHECKPOINT.md'), name, ok: c.status === 0 };
  };
  const mkTr = (name, handinDir, startAgoMs = 600000) => {
    const p = path.join(tmp, name + '.jsonl');
    const b = `AGENT-CONTRACT-MARKER: v1\nHANDIN_DIR: ${handinDir}\nHANDIN: no-visual\n## Owner's words\n"The photo is the target." (11 Oct)\n`;
    fs.writeFileSync(p, JSON.stringify({ type: 'user', isSidechain: true, timestamp: new Date(Date.now() - startAgoMs).toISOString(), message: { role: 'user', content: b } }) + '\n' + JSON.stringify({ type: 'assistant', message: { role: 'assistant', content: [{ type: 'text', text: 'working' }] } }) + '\n');
    return p;
  };
  const stopHook = (id, tp, event = 'SubagentStop') => {
    fs.appendFileSync(path.join(state, 'gate.log'), JSON.stringify({ ts: new Date().toISOString(), gate: 'inject', agent_type: 'general-purpose', agent_id: id, decision: 'inject', chars: 1 }) + '\n');
    const input = event === 'SubagentStop' ? { hook_event_name: 'SubagentStop', session_id: 's1', cwd: elsewhere, agent_id: id, agent_type: 'general-purpose', agent_transcript_path: tp, stop_hook_active: false, last_assistant_message: 'done' }
      : { hook_event_name: 'PreToolUse', tool_name: 'SubagentHandback', session_id: 's1', cwd: elsewhere, agent_id: id, agent_type: 'general-purpose', agent_transcript_path: tp, transcript_path: tp, tool_input: { message: 'report' } };
    const r = spawnSync(process.execPath, [path.join(root, 'tools', 'hooks', 'handin_gate.mjs')], { input: JSON.stringify(input), cwd: elsewhere, encoding: 'utf8', timeout: 120000, env });
    return { code: r.status, out: r.stdout, err: r.stderr };
  };
  const check = (hd, ...a) => spawnSync(process.execPath, [path.join(root, 'tools', 'check_handin.mjs'), hd, '--main', root, ...a], { cwd: elsewhere, encoding: 'utf8', env });
  const setCp = (R, imgs, what = 'lane widths measured on four photos') => fs.writeFileSync(R.cp, `CHECKPOINT: ${what} | images: ${imgs}\n`);
  const ageRecords = (atMs) => { const dir = path.join(state, 'handin'); try { for (const f of fs.readdirSync(dir)) if (/checkpoint.*\.json$/.test(f)) { const p = path.join(dir, f); const o = J(fs.readFileSync(p, 'utf8')); fs.writeFileSync(p, JSON.stringify({ ...o, at: atMs })); } } catch { /* none */ } };
  const since = String(Date.now() - 300000);

  // ---- H4 the image
  const R4 = mkRound('h4'); t('H4 fixture: the temp git repo was created', R4.ok);
  const inFolder = path.join(R4.hd, 'shot.png'); fs.writeFileSync(inFolder, png);
  setCp(R4, inFolder); let c = check(R4.hd, '--checkpoint', '--since', since);
  t('H4 a real PNG (>= 10 KB, valid header) inside the hand-in folder, written during the run: passes', c.status === 0 && /CHECKPOINT PASSED/.test(c.stdout), c.stdout + c.stderr);
  setCp(R4, 'shot.png'); c = check(R4.hd, '--checkpoint', '--since', since); t('H4 ... named relative to the hand-in folder: passes', c.status === 0, c.stderr);
  const sys = path.join(tmp, 'sysimg', 'logo.png'); wr(path.join(tmp, 'sysimg', 'x'), 'x'); fs.writeFileSync(sys, png);
  setCp(R4, sys); c = check(R4.hd, '--checkpoint', '--since', since); t('H4 a good PNG OUTSIDE the hand-in folder and _compare/<round> (like C:\\Windows\\System32\\@WLOGO_96x96.png): refused, says where it must be', c.status === 1 && /hand-in folder|_compare/.test(c.stderr), c.stderr);
  fs.writeFileSync(path.join(R4.hd, 'fake.png'), 'not an image'); setCp(R4, path.join(R4.hd, 'fake.png')); c = check(R4.hd, '--checkpoint', '--since', since); t('H4 images: fake.png that is a text file: refused', c.status === 1 && /10 KB|header|too small/.test(c.stderr), c.stderr);
  fs.writeFileSync(path.join(R4.hd, 'sig.png'), Buffer.from('89504e470d0a1a0a', 'hex')); setCp(R4, path.join(R4.hd, 'sig.png')); c = check(R4.hd, '--checkpoint', '--since', since); t('H4 an 8-byte file that is only the PNG signature: refused (too small)', c.status === 1 && /10 KB|too small/.test(c.stderr), c.stderr);
  fs.writeFileSync(path.join(R4.hd, 'noise.png'), crypto.randomBytes(20000)); setCp(R4, path.join(R4.hd, 'noise.png')); c = check(R4.hd, '--checkpoint', '--since', since); t('H4 20 KB of noise named .png: refused (no valid header)', c.status === 1 && /header/.test(c.stderr), c.stderr);
  const bigText = path.join(R4.hd, 'big.png'); fs.writeFileSync(bigText, Buffer.concat([Buffer.from('89504e470d0a1a0a', 'hex'), crypto.randomBytes(20000)])); setCp(R4, bigText); c = check(R4.hd, '--checkpoint', '--since', since); t('H4 PNG signature followed by noise (no IHDR): refused', c.status === 1 && /header|IHDR/.test(c.stderr), c.stderr);
  const jpg = path.join(R4.hd, 'a.jpg'); const jb = Buffer.concat([Buffer.from([0xff, 0xd8, 0xff, 0xc0, 0x00, 0x11, 0x08, 0x00, 0x40, 0x00, 0x40, 0x03, 0x01, 0x22, 0x00, 0x02, 0x11, 0x01, 0x03, 0x11, 0x01]), crypto.randomBytes(12000), Buffer.from([0xff, 0xd9])]);
  fs.writeFileSync(jpg, jb); setCp(R4, jpg); c = check(R4.hd, '--checkpoint', '--since', since); t('H4 a JPEG with a valid SOI and a frame header, >= 10 KB: passes', c.status === 0, c.stderr);
  const old = path.join(R4.hd, 'old.png'); fs.writeFileSync(old, png); const hour = new Date(Date.now() - 3600000); fs.utimesSync(old, hour, hour); setCp(R4, old); c = check(R4.hd, '--checkpoint', '--since', since); t('H4 an image from before this run (an earlier round): refused', c.status === 1 && /older|before/.test(c.stderr), c.stderr);
  const cmpDir = path.join(root, 'docs', 'reference', '_compare', 'h4'); mk(cmpDir); const cmpImg = path.join(cmpDir, 'composite.png'); fs.writeFileSync(cmpImg, png); setCp(R4, cmpImg); c = check(R4.hd, '--checkpoint', '--since', since); t('H4 an image in docs/reference/_compare/<round> (the round = the hand-in folder name): passes', c.status === 0, c.stderr);
  const cmpOther = path.join(root, 'docs', 'reference', '_compare', 'someotherround'); mk(cmpOther); fs.writeFileSync(path.join(cmpOther, 'c.png'), png); setCp(R4, path.join(cmpOther, 'c.png')); c = check(R4.hd, '--checkpoint', '--since', since); t('H4 an image in _compare/<ANOTHER round>: refused', c.status === 1, c.stderr);
  const photo = path.join(root, 'docs', 'reference', 'aerial', 'p.png'); fs.writeFileSync(photo, png); setCp(R4, photo); c = check(R4.hd, '--checkpoint', '--since', since); t('H4 a reference photo (docs/reference/aerial) is not a checkpoint image: refused', c.status === 1, c.stderr);

  // ---- H5 a stale CHECKPOINT.md at the final hand-in
  const mkValid = (R, minusMs = 30000) => { const im = path.join(R.hd, 'shot.png'); fs.writeFileSync(im, png); setCp(R, im); const tm = new Date(Date.now() - minusMs); fs.utimesSync(R.cp, tm, tm); fs.utimesSync(im, tm, tm); };
  const R5a = mkRound('h5a', { commitAgoMs: 120000 }); mkValid(R5a, 30000);
  let r = stopHook('a5', mkTr('t5a', R5a.hd), 'PreToolUse');
  t('H5 control: CHECKPOINT.md written AFTER the latest commit counts as a checkpoint', r.code === 0 && lastHandin().decision === 'checkpoint', r.err + JSON.stringify(lastHandin()));
  const R5b = mkRound('h5b', { commitAgoMs: 1000 }); mkValid(R5b, 30000);
  r = stopHook('b5', mkTr('t5b', R5b.hd), 'PreToolUse'); const r5b = r;
  t('H5 CHECKPOINT.md OLDER than the latest commit is ignored: the full check runs (refused, STATUS.md missing)', r.code === 2 && /STATUS\.md is missing/.test(r.err), r.err + r.out);
  t('H5 ... the ignored checkpoint is logged (a gate.log line that says CHECKPOINT.md was ignored, older than the latest commit)', logOf().some((l) => /^(?:handin|checkpoint)$/.test(l.gate) && /checkpoint/i.test(l.reason || '') && /ignored/i.test(l.reason || '') && /commit/i.test(l.reason || '')), JSON.stringify(logOf().slice(-4)));
  // a commit that only adds the CHECKPOINT.md itself does not void it (the builder commits after every step)
  const R5c = mkRound('h5c', { commitAgoMs: 120000 }); mkValid(R5c, 30000); git(R5c.w, ['add', 'docs']); git(R5c.w, ['commit', '-q', '-m', 'checkpoint']);
  r = stopHook('c5', mkTr('t5c', R5c.hd), 'PreToolUse');
  t('H5 a commit that only adds CHECKPOINT.md and its picture does not void the checkpoint', r.code === 0 && lastHandin().decision === 'checkpoint', r.err + JSON.stringify(lastHandin()));
  t('H5 ... and the refusal message tells the builder to delete CHECKPOINT.md', /delete CHECKPOINT\.md/i.test(r5b.err), r5b.err);

  // ---- H6 window and once per round
  const R6 = mkRound('h6', { commitAgoMs: 3600000 }); mkValid(R6, 60000);
  r = stopHook('a6', mkTr('t6a', R6.hd), 'PreToolUse'); t('H6 first checkpoint of the round: allowed', r.code === 0 && lastHandin().decision === 'checkpoint', r.err + JSON.stringify(lastHandin()));
  r = stopHook('a6', mkTr('t6a', R6.hd), 'SubagentStop'); t('H6 second hook call of the same finish (seconds later, same file): allowed', r.code === 0 && r.out === '', r.out + r.err);
  ageRecords(Date.now() - 40000);
  r = stopHook('a6', mkTr('t6a', R6.hd), 'SubagentStop'); t('H6 the same agent stopping again 40 s later (outside the 30 s window), file untouched: the full check runs (blocked, "already used")', J(r.out).decision === 'block' && /already used|once per round/.test(J(r.out).reason), r.out + r.err);
  ageRecords(Date.now() - 600000);
  mkValid(R6, 1000);
  r = stopHook('b6', mkTr('t6b', R6.hd), 'SubagentStop'); t('H6 ANOTHER agent of the same round with a fresh valid CHECKPOINT.md: one checkpoint per round, the full check runs', J(r.out).decision === 'block' && /once per round/.test(J(r.out).reason), r.out + r.err);
  const R6b = mkRound('h6b', { commitAgoMs: 3600000 }); mkValid(R6b, 30000);
  r = stopHook('c6', mkTr('t6c', R6b.hd), 'PreToolUse'); t('H6 a different round is a different allowance: allowed', r.code === 0 && lastHandin().decision === 'checkpoint', r.err + JSON.stringify(lastHandin()));
  // the contract text and the hook comment name the 30 s window
  const hs = fs.readFileSync(path.join(src, 'tools', 'hooks', 'handin_gate_impl.mjs'), 'utf8');
  t('H6 CHECKPOINT_WINDOW_MS is 30000', /CHECKPOINT_WINDOW_MS\s*=\s*30000\b/.test(hs));
}

console.log('test isolation and cleanup');
t('no line mentioning this run\'s temp folder reached the real gate.log', isolation.leaks().length === 0, isolation.leaks().slice(0, 3).join('\n'));
t('the photos behind the junctions survived every case', fs.readFileSync(path.join(photos, 'k.jpg'), 'utf8') === 'k' && fs.readFileSync(path.join(main, 'docs', 'reference', 'aerial', 'p1.jpg'), 'utf8') === 'photo');
t('the locked test files are untouched after every case (nothing here ran a command)', fs.readFileSync(path.join(L.w, 'tests', 'listed_test.mjs'), 'utf8') === 'f' && fs.readFileSync(path.join(L.w, 'tests', 'locked', 'a.mjs'), 'utf8') === 'a' && fs.readFileSync(lst, 'utf8').includes('listed_test'));
const gone = cleanupTmp(tmp);
t(`the temp folder (junctions unlinked first) is removed${process.env.KEEP_HOOKTEST ? ' [KEEP_HOOKTEST set: kept]' : ''}`, gone || !!process.env.KEEP_HOOKTEST, tmp);
console.log(`\n${pass} passed, ${fail} failed`);
if (failedNames.length) console.log('FAILED TESTS:\n - ' + failedNames.join('\n - '));
process.exit(fail ? 1 : 0);
