#!/usr/bin/env node
// gates7: real-payload tables for the 14 items of docs/progress/gates6/AUDIT.md ("For the next builder") plus item 15 (the git guard, from the incident of 23 Oct 2026).
// Where the expected values come from: the gates6 AUDIT.md (section 1 "Refused although legitimate" = must be ALLOWED, section 2 "Open, fixable" = must be REFUSED, each numbered item named in
// the case labels), the main session's item 15 text (brief of 23 Oct 2026), and the decisions already in test_guards6 (structural protection + backup; `rm -rf build` in a clean worktree is
// fine). Nothing here pins a fault. Payloads use backslash cwds, as the harness sends them on Windows. The fixture is a temp "main checkout" with a clean registered worktree and a
// registered worktree holding a REAL junction at docs\reference; the whole temp folder (junction unlinked first) is removed at the end. No test writes the live gate.log.
// Usage: node tools/hooks/test_guards7.mjs
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { spawnSync } from 'node:child_process';
import { fileURLToPath, pathToFileURL } from 'node:url';
import { isolateState, cleanupTmp } from '../test_isolation.mjs';

const src = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..', '..');
const tmp = fs.mkdtempSync(path.join(os.tmpdir(), 'guards7-'));
const isolation = isolateState(tmp, src);
let pass = 0, fail = 0; const failedNames = [];
const t = (name, cond, detail = '') => { if (cond) { pass++; console.log('  ok   ' + name); } else { fail++; failedNames.push(name); console.log('  FAIL ' + name + (detail ? '\n       ' + String(detail).replace(/\n/g, '\n       ') : '')); } };

const main = path.join(tmp, 'roadworld'); const M = main.replace(/\\/g, '/');
const mk = (p) => fs.mkdirSync(p, { recursive: true });
mk(path.join(main, '.git', 'worktrees', 'w1')); mk(path.join(main, '.git', 'worktrees', 'j1'));
mk(path.join(main, 'docs', 'reference', 'aerial')); mk(path.join(main, '.claude', 'state', 'handin', 'r1'));
for (const n of ['p1.jpg', 'p2.jpg']) fs.writeFileSync(path.join(main, 'docs', 'reference', 'aerial', n), 'photo');
fs.writeFileSync(path.join(main, '.claude', 'state', 'ack_secret'), 'secret'); fs.writeFileSync(path.join(main, '.claude', 'state', 'gate.log'), '{}\n'); fs.writeFileSync(path.join(main, '.claude', 'state', 'handin', 'r1', 'HANDIN_FAILED.txt'), 'x');
const wt = path.join(tmp, 'roadworld-wt', 'w1'); const wtJ = path.join(tmp, 'roadworld-wt', 'j1');
for (const [w, n] of [[wt, 'w1'], [wtJ, 'j1']]) {
  mk(path.join(w, 'build', 'sub')); mk(path.join(w, 'docs')); fs.writeFileSync(path.join(w, 'build', 'a.obj'), 'o');
  fs.writeFileSync(path.join(w, '.git'), 'gitdir: ' + path.join(main, '.git', 'worktrees', n) + '\n');
  fs.writeFileSync(path.join(main, '.git', 'worktrees', n, 'gitdir'), path.join(w, '.git') + '\n'); fs.writeFileSync(path.join(main, '.git', 'worktrees', n, 'commondir'), '../..\n');
}
const photos = path.join(tmp, 'photos'); mk(photos); fs.writeFileSync(path.join(photos, 'k.jpg'), 'k');
fs.symlinkSync(photos, path.join(wtJ, 'docs', 'reference'), 'junction');
mk(path.join(wtJ, '.hidden')); fs.symlinkSync(photos, path.join(wtJ, '.hidden', 'ln'), 'junction');   // a link inside a dotted folder: what `rm -rf .[a-z]*` would walk into
const elsewhere = path.join(tmp, 'elsewhere'); mk(elsewhere);
mk(path.join(tmp, 'tempdir')); process.env.TEMP = path.join(tmp, 'tempdir'); process.env.TMP = process.env.TEMP; delete process.env.TMPDIR;

const dgm = await import(pathToFileURL(path.join(src, 'tools/hooks/delete_guard_impl.mjs')).href);
const sgm = await import(pathToFileURL(path.join(src, 'tools/hooks/state_guard_impl.mjs')).href);
const ggm = await import(pathToFileURL(path.join(src, 'tools/hooks/git_guard_impl.mjs')).href);
const lex = await import(pathToFileURL(path.join(src, 'tools/hooks/shell_lex.mjs')).href);
const dg = (command, cwd) => dgm.analyse(command, cwd, 0, null, main);
const cwdOf = { wt, wtJ, main, elsewhere };
function table(title, cases) {
  const bad = [];
  for (const [command, cw, want, label] of cases) {
    let got; try { got = dg(command, cwdOf[cw]).length ? 2 : 0; } catch (e) { got = 'THROW ' + e.message; }
    if (got !== want) bad.push(`${label}: ${command.slice(0, 120)} (cwd ${cw}): ${got}, wanted ${want}`);
  }
  t(`${title}: ${cases.length} cases judged as the audit says`, bad.length === 0, bad.join('\n'));
}
const R = M + '/docs/reference';

console.log('gates7 delete guard: false blocks (gates6 audit section 1, items 1-3) must be ALLOWED, the dangerous twin REFUSED');
table('item 1 one-liner deletes of a path literal', [
  ['python -c "import shutil; shutil.rmtree(\'build\')"', 'wt', 0, 'item 1 python rmtree build'],
  ['python3 -c "import shutil; shutil.rmtree(\'build\', ignore_errors=True)"', 'wt', 0, 'item 1 python3 rmtree ignore_errors'],
  ['node -e "require(\'fs\').rmSync(\'build\',{recursive:true,force:true})"', 'wt', 0, 'item 1 node rmSync build'],
  ['node -e "fs.rmSync(\\"build\\", {recursive: true})"', 'wt', 0, 'item 1 node rmSync escaped quotes'],
  ['pwsh -c "[System.IO.Directory]::Delete(\'build\', $true)"', 'wt', 0, 'item 1 pwsh Directory.Delete build'],
  ['python -c "import shutil; shutil.rmtree(\'docs\')"', 'main', 2, 'item 1 rmtree docs in main'],
  [`python -c "import shutil; shutil.rmtree('${M}/docs')"`, 'elsewhere', 2, 'item 1 rmtree <main>/docs'],
  [`python -c "import shutil; shutil.rmtree('${R}')"`, 'elsewhere', 2, 'item 1 rmtree the photos'],
  ['node -e "require(\'fs\').rmSync(\'docs\',{recursive:true})"', 'wtJ', 2, 'item 1 rmSync docs in the link tree'],
  ['python -c "import shutil,os; shutil.rmtree(os.environ[\'X\'])"', 'main', 2, 'item 1 rmtree of a variable keeps the old refusal'],
  ['pwsh -c "[System.IO.Directory]::Delete(\'docs\', $true)"', 'wtJ', 2, 'item 1 Directory.Delete docs in the link tree'],
]);
table('item 2 del -Recurse is Remove-Item', [
  ['del -Recurse -Force build', 'wt', 0, 'item 2 del -Recurse -Force build'], ['erase -Recurse -Force build', 'wt', 0, 'item 2 erase -Recurse'], ['del -Force -Recurse .\\build', 'wt', 0, 'item 2 del .\\build'],
  ['del -Recurse -Force docs', 'wtJ', 2, 'item 2 del -Recurse docs in the link tree'], [`del -Recurse -Force ${R.replace(/\//g, '\\')}`, 'elsewhere', 2, 'item 2 del -Recurse the photos'],
  ['del /s /q build', 'wt', 2, 'item 2 cmd del /s in a worktree stays refused (it walks into any junction)'],
]);
table('item 3 filtered listing piped into Remove-Item from the worktree root', [
  ['Get-ChildItem -Path . -Filter "build*" -Directory | Remove-Item -Recurse -Force', 'wt', 0, 'item 3 gci -Filter build* | Remove-Item'],
  ['gci . -Filter "*.obj" -Recurse | Remove-Item -Force', 'wt', 0, 'item 3 gci -Filter *.obj | Remove-Item'],
  ['Get-ChildItem -Path . -Filter "build*" -Directory | Remove-Item -Recurse -Force', 'wtJ', 2, 'item 3 same in the link tree (the walk finds the link)'],
  ['Get-ChildItem -Path . -Filter "build*" -Directory | Remove-Item -Recurse -Force', 'main', 0, 'item 3 a filter that cannot match a picture, from main'],
  ['Get-ChildItem -Path . -Filter "*.jpg" | Remove-Item -Recurse -Force', 'main', 2, 'item 3 a filter that matches pictures, from main'],
  ['Get-ChildItem -Path . | Remove-Item -Recurse -Force', 'wt', 2, 'item 3 unfiltered listing of the worktree root stays refused'],
]);

console.log('gates7 delete guard: holes (gates6 audit section 2, items 6-8, 10) must be REFUSED');
table('item 6 PowerShell blocks with the brace glued to the verb', [
  ['try {Remove-Item docs -Recurse -Force} catch {}', 'wtJ', 2, 'item 6 try {Remove-Item'], ['if (1) {Remove-Item docs -Recurse -Force}', 'wtJ', 2, 'item 6 if (1) {Remove-Item'],
  ['foreach ($x in 1) {Remove-Item docs -Recurse -Force}', 'wtJ', 2, 'item 6 foreach {Remove-Item'], ['1..2 | ForEach-Object {Remove-Item docs -Recurse -Force}', 'wtJ', 2, 'item 6 ForEach-Object {Remove-Item'],
  ['& {Remove-Item docs -Recurse -Force}', 'wtJ', 2, 'item 6 & {Remove-Item'], ['&{Remove-Item docs -Recurse -Force}', 'wtJ', 2, 'item 6 &{Remove-Item'],
  ['powershell -c "& {Remove-Item docs -Recurse -Force}"', 'wtJ', 2, 'item 6 powershell -c "& {Remove-Item"'], ['powershell -Command {Remove-Item docs -Recurse -Force}', 'wtJ', 2, 'item 6 powershell -Command {Remove-Item'],
  ['if (1){Remove-Item docs -Recurse -Force}', 'wtJ', 2, 'item 6 if (1){Remove-Item'], ['try{Remove-Item docs -Recurse -Force}catch{}', 'wtJ', 2, 'item 6 try{...}catch{}'],
  ['if ($true) { Remove-Item docs -Recurse -Force}', 'wtJ', 2, 'item 6 spaced opener, glued closer'],
  ['try {Remove-Item build -Recurse -Force} catch {}', 'wt', 0, 'item 6 the same block on build in a clean worktree is allowed'], ['if (Test-Path build){Remove-Item build -Recurse -Force}', 'wt', 0, 'item 6 if (Test-Path build){...}'],
  ['rm -rf {docs,build}', 'main', 2, 'item 6 brace expansion still read as a word'], ['find . -name "*.obj" -exec rm {} \\;', 'wt', 0, 'item 6 find -exec rm {} in a clean worktree'],
  ['find . -name "*.obj" -exec rm {} \\;', 'wtJ', 2, 'item 6 find -exec rm {} in the link tree'], ['echo ${HOME}', 'main', 0, 'item 6 ${HOME} is a word'],
]);
table('item 7 xargs rm and find -exec truncate over the photos', [
  [`find ${R} -type f | xargs rm`, 'elsewhere', 2, 'item 7 find | xargs rm (no flags)'], [`find ${R} -exec truncate -s0 {} +`, 'elsewhere', 2, 'item 7 find -exec truncate'],
  [`find ${R} -type f -exec shred -u {} \\;`, 'elsewhere', 2, 'item 7 find -exec shred'], [`find ${R} -type f | xargs truncate -s 0`, 'elsewhere', 2, 'item 7 | xargs truncate'],
  [`ls ${R}/aerial | xargs rm`, 'elsewhere', 2, 'item 7 ls | xargs rm'], [`find ${R} -type f | xargs -0 rm -f`, 'elsewhere', 2, 'item 7 xargs -0 rm -f stays refused'],
  ['find build -type f | xargs rm', 'wt', 0, 'item 7 find build | xargs rm in a clean worktree'], ['find . -name "*.obj" | xargs rm', 'wt', 0, 'item 7 find . -name | xargs rm in a clean worktree'],
  ['find build -exec truncate -s0 {} +', 'wt', 0, 'item 7 find build -exec truncate'],
]);
table('own fault list, pass 1 (found while attacking the fixes above)', [
  ['cmake -E rm -rf docs', 'main', 2, 'F4 cmake -E rm -rf docs'], ['cmake -E remove_directory docs', 'main', 2, 'F4 cmake -E remove_directory docs'], ['cmake -E rm -rf build', 'wt', 0, 'F4 cmake -E rm -rf build in a clean worktree'],
  ['{rm,-rf,docs}', 'main', 2, 'F1 a brace list as the whole command line is brace-expanded by bash into rm -rf docs'], ['{rm,-rf,build}', 'wt', 0, 'F1 the same on build in a clean worktree'], ['echo {a,b}', 'main', 0, 'F1 echo {a,b} is text'],
  [`find ${M}/docs -type f | xargs rm`, 'elsewhere', 2, 'F2 xargs rm over a find of the main docs folder (the photos live below it)'], [`find ${M} -name "*.jpg" | xargs rm`, 'elsewhere', 2, 'F2 find main -name *.jpg | xargs rm'], [`find ${M} -name "*.log" | xargs rm`, 'elsewhere', 0, 'F2 a name filter that cannot match a picture'],
  ['find . -type f | xargs rm', 'main', 2, 'F2 find . -type f | xargs rm in main'], ['find . -type f | xargs rm', 'wt', 0, 'F2 the same in a clean worktree'],
  ['python -c "import shutil; shutil.rmtree(\'build\'); shutil.rmtree(\'docs\')"', 'main', 2, 'F3 two literals, the second is docs'], ['python -c "import shutil; shutil.rmtree(\'bu\' + \'ild\')"', 'wt', 2, 'F3 a computed path is not a literal: refused near a tree'],
]);
table('item 8 abbreviated PowerShell parameters, dotted globs, git clean -e', [
  ['gci -r | rm -fo', 'wtJ', 2, 'item 8 gci -r | rm -fo'], ['Get-ChildItem -Recurse | Remove-Item -Force', 'wtJ', 2, 'item 8 the spelled-out form stays refused'], ['gci build -r | rm -fo', 'wt', 0, 'item 8 gci build -r | rm -fo in a clean worktree'],
  ['Remove-Item -Rec -Pa docs -Fo', 'wtJ', 2, 'item 8 Remove-Item -Rec -Pa docs -Fo'], ['Remove-Item -Rec -Pa build -Fo', 'wt', 0, 'item 8 -Rec -Pa build'], ['Remove-Item -Recurse -Fo -Lit docs', 'wtJ', 2, 'item 8 -Lit docs'],
  ['rm -rf .[a-z]*', 'wtJ', 2, 'item 8 rm -rf .[a-z]* where a dotted folder holds a link'], ['rm -rf .[a-z]*', 'wt', 0, 'item 8 rm -rf .[a-z]* in a clean worktree (a .git file only)'],
  ['git clean -xfd -e foo', 'wtJ', 2, 'item 8 git clean -xfd -e foo, link tree'], ['git clean -xfd -e foo', 'main', 2, 'item 8 git clean -xfd -e foo, main'], ['git clean -fdx --exclude foo', 'main', 2, 'item 8 --exclude foo'],
  ['git clean -xfd -e foo build', 'wt', 0, 'item 8 git clean -xfd -e foo build, clean worktree'], ['git clean -n -xfd -e foo', 'wtJ', 0, 'item 8 dry run'],
]);
table('item 10 generic wrapper look-through', [
  ['unbuffer rm -rf docs', 'main', 2, 'item 10 unbuffer'], ['watch rm -rf docs', 'main', 2, 'item 10 watch'], ['parallel rm -rf ::: docs', 'main', 2, 'item 10 parallel'], ['flock /tmp/l rm -rf docs', 'main', 2, 'item 10 flock'],
  ['taskset 1 rm -rf docs', 'main', 2, 'item 10 taskset'], ['strace rm -rf docs', 'main', 2, 'item 10 strace'], ['script -c "rm -rf docs" /dev/null', 'main', 2, 'item 10 script -c'],
  ['foo --bar -- rm -rf docs', 'main', 2, 'item 10 <cmd> <args> -- rm'], ['foo --bar -- Remove-Item docs -Recurse -Force', 'main', 2, 'item 10 <cmd> -- Remove-Item'], ['foo --bar -- git clean -fdx', 'main', 2, 'item 10 <cmd> -- git clean'],
  ['unbuffer rm -rf build', 'wt', 0, 'item 10 unbuffer on build'], ['parallel rm -rf ::: build', 'wt', 0, 'item 10 parallel on build'], ['flock /tmp/l rm -rf build', 'wt', 0, 'item 10 flock on build'],
  ['echo rm -rf docs', 'main', 0, 'item 10 echo is text'], ['curl -d "rm -rf docs" http://example.test/x', 'main', 0, 'item 10 curl body is text'], ['git commit -m "rm -rf docs"', 'main', 0, 'item 10 commit message is text'],
  ['cmake --build build -- -j4', 'main', 0, 'item 10 cmake -- -j4'], ['npm run build -- --watch', 'main', 0, 'item 10 npm run build -- --watch'], ['grep -rn "rm -rf" tools', 'main', 0, 'item 10 grep for rm'],
]);

console.log('gates7 state guard (gates6 audit items 4 and 9)');
const sdir = (M + '/.claude/state'); const secret = sdir + '/ack_secret'; const sdirW = sdir.replace(/\//g, '\\');
const common = (tool, ti, extra = {}) => ({ session_id: 's1', cwd: elsewhere, hook_event_name: 'PreToolUse', tool_name: tool, tool_input: ti, agent_id: 'ag7', agent_type: 'general-purpose', ...extra });
const sg = (tool, ti, extra) => { try { return sgm.judge(common(tool, ti, extra), main).length ? 2 : 0; } catch (e) { return 'THROW ' + e.message; } };
const B = (command) => ['Bash', { command }], P = (command) => ['PowerShell', { command }];
{
  const scratchOut = (path.join(tmp, 'tempdir', 'g.log')).replace(/\\/g, '/');
  const cases = [
    [...B(`grep deny ${sdir}/*.log`), 0, 'item 4 grep deny state/*.log'], [...B(`grep -n deny ${sdir}/gate.log`), 0, 'item 4 grep gate.log'], [...B(`cat ${sdir}/*.log`), 0, 'item 4 cat state/*.log'],
    [...B(`cp ${sdir}/gate.log ${scratchOut}`), 0, 'item 4 cp gate.log to a scratch file'], [...B(`cp ${sdir}/gate.log /tmp/g.log`), 0, 'item 4 cp gate.log /tmp/g.log'],
    [...P(`Copy-Item ${sdirW}\\gate.log $env:TEMP\\g.log`), 0, 'item 4 Copy-Item gate.log $env:TEMP'], [...B(`cp ${sdir}/handin/r1/HANDIN_FAILED.txt /tmp/x.txt`), 0, 'item 4 cp HANDIN_FAILED.txt out'],
    [...B(`node -e "console.log(require('fs').readFileSync('${sdir}/gate.log','utf8').split('\\n').length)"`), 0, 'item 4 node -e reads gate.log'],
    [...B(`python -c "print(len(open('${sdir}/gate.log').readlines()))"`), 0, 'item 4 python -c reads gate.log'], [...B(`node -e "require('fs').readdirSync('${sdir}/handin')"`), 0, 'item 4 node -e lists handin'],
    [...B(`grep x ${sdir}/*`), 2, 'item 4 grep x state/* (can match the secret)'], [...B(`cat ${sdir}/ack_s*`), 2, 'item 4 cat state/ack_s*'], [...B(`cat ${sdir}/?ck_secret`), 2, 'item 4 cat state/?ck_secret'],
    [...B(`cp ${secret} /tmp/s`), 2, 'item 4 cp the secret out'], [...B(`cp /tmp/x ${sdir}/gate.log`), 2, 'item 4 cp onto gate.log (the destination is a write)'], [...B(`cp ${sdir}/gate.log ${sdir}/handin/x.log`), 2, 'item 4 cp into handin (a write)'],
    [...B(`mv ${sdir}/gate.log /tmp/x`), 2, 'item 4 mv gate.log away'], [...B(`rm ${sdir}/*.log`), 2, 'item 4 rm state/*.log (a write through a wildcard)'],
    [...B(`node -e "require('fs').writeFileSync('${sdir}/gate.log','')"`), 2, 'item 4 node -e writes gate.log'], [...B(`node -e "console.log(require('fs').readFileSync('${secret}','utf8'))"`), 2, 'item 4 node -e reads the secret'],
    [...B(`python -c "open('${sdir}/gate.log','w').write('')"`), 2, 'item 4 python -c truncates gate.log'], [...B(`python -c "print(open('${secret}').read())"`), 2, 'item 4 python -c reads the secret'],
    [...P(`Select-String -Path ${sdirW}\\* -Pattern .`), 2, 'item 9 Select-String -Path state\\*'], [...P(`Select-String -Pattern . -Path ${sdirW}\\ack_secret`), 2, 'item 9 Select-String -Path ack_secret'],
    [...P(`Select-String -Path ${sdirW}\\*.log -Pattern deny`), 0, 'item 9 Select-String -Path state\\*.log'], [...P(`sls deny ${sdirW}\\gate.log`), 0, 'item 9 sls pattern then gate.log'],
    [...P(`Get-ChildItem ${sdirW} | ForEach-Object { Get-Content $_.FullName }`), 2, 'item 9 gci state | ForEach-Object { Get-Content }'], [...P(`gci ${sdirW} | % { cat $_.FullName }`), 2, 'item 9 gci state | % { cat }'],
    [...P(`gci ${sdirW} | ForEach-Object {Get-Content $_.FullName}`), 2, 'item 9 glued braces'], [...P(`Get-ChildItem ${sdirW} -Filter *.log | ForEach-Object { Get-Content $_.FullName }`), 0, 'item 9 -Filter *.log cannot match the secret'],
    [...P(`gci ${sdirW} | ForEach-Object { $_.Name }`), 0, 'item 9 names only'], [...P(`Get-Content ${sdirW}\\gate.log -Tail 5`), 0, 'item 9 Get-Content gate.log'],
  ];
  const bad = [];
  for (const [tool, ti, want, label] of cases) { const got = sg(tool, ti); if (got !== want) bad.push(`${label}: ${(ti.command || ti.file_path || '').slice(0, 110)}: ${got}, wanted ${want}`); }
  t(`state guard: ${cases.length} cases (gates6 audit items 4 and 9) judged as the audit says`, bad.length === 0, bad.join('\n'));
}

console.log('gates7 lexer: glued braces become their own tokens, ${x} and {a,b} do not');
{
  const words = (c) => lex.parseCommand(c).segments.map((s) => s.words.map((w) => w.v));
  const flat = (c) => words(c).flat();
  t('try {Remove-Item docs -Recurse -Force} catch {}: no word carries a brace; Remove-Item is a statement of its own', !flat('try {Remove-Item docs -Recurse -Force} catch {}').some((w) => /[{}]/.test(w) && w !== '{}') && words('try {Remove-Item docs -Recurse -Force} catch {}').some((s) => s[0] === 'Remove-Item' && s.length === 4));
  t('${HOME}/x and {a,b} stay single words', flat('rm ${HOME}/x {a,b}c').join('|') === 'rm|${HOME}/x|{a,b}c', flat('rm ${HOME}/x {a,b}c').join('|'));
  t('find -exec rm {} \\; keeps the {} placeholder', flat('find . -exec rm {} \\;').includes('{}'));
  t('the body of a piped ForEach-Object is marked piped and fe', lex.parseCommand('gci x | ForEach-Object {Get-Content $_.FullName}').segments.some((s) => s.words[0].v === 'Get-Content' && s.piped && s.fe));
}

console.log('gates7 git guard (item 15, in-process, backslash cwds)');
const G = (command, cw, extra = {}) => ggm.analyse(command, cwdOf[cw] || cw, { mainRoot: main, ...extra }).length ? 2 : 0;
const MB = M.replace(/\//g, '\\'); const WB = wt.replace(/\\/g, '/'); const HI = wt + '\\docs\\progress\\r1';
function gtable(title, cases) {
  const bad = [];
  for (const [command, cw, want, label, extra] of cases) { let got; try { got = G(command, cw, extra); } catch (e) { got = 'THROW ' + e.message; } if (got !== want) bad.push(`${label}: ${command.slice(0, 110)} (cwd ${cw}): ${got}, wanted ${want}`); }
  t(`${title}: ${cases.length} cases`, bad.length === 0, bad.join('\n'));
}
gtable('G1 blanket staging in the main checkout, any subagent', [
  ['git add -A', 'main', 2, 'git add -A'], ['git add .', 'main', 2, 'git add .'], ['git add --all', 'main', 2, 'git add --all'], ['git add -A; git commit -m x', 'main', 2, 'git add -A; git commit'], ['git commit -am "x"', 'main', 2, 'git commit -am'],
  ['git commit -a -m x', 'main', 2, 'git commit -a -m'], ['git commit --all -m x', 'main', 2, 'git commit --all'], ['git stash -u', 'main', 2, 'git stash -u'], ['git stash push -a', 'main', 2, 'git stash push -a'], ['git stash --include-untracked', 'main', 2, 'git stash --include-untracked'],
  [`cd ${MB} && git add -A`, 'elsewhere', 2, 'cd main && git add -A'], [`git -C ${MB} add -A`, 'elsewhere', 2, 'git -C main add -A'], [`git -C ${M}/docs add .`, 'elsewhere', 2, 'git -C main/docs add .'], [`Set-Location ${MB}; git add -A`, 'elsewhere', 2, 'Set-Location main; git add -A'],
  ['bash -c "git add -A"', 'main', 2, 'bash -c "git add -A"'], ['git add -- .', 'main', 2, 'git add -- .'], ['git add *', 'main', 2, 'git add *'], ['git -c user.name=x add -A', 'main', 2, 'git -c k=v add -A'],
  ['git add -A', 'wt', 0, 'git add -A in a worktree'], ['git commit -am x', 'wtJ', 0, 'git commit -am in a worktree'], [`git -C ${wt} add -A`, 'main', 0, 'git -C <worktree> add -A from a main cwd'], [`cd ${wt} && git add -A`, 'main', 0, 'cd <worktree> && git add -A from main'],
  [`git -C ${MB} add -A`, 'wt', 2, 'git -C main add -A from a worktree'], ['git -C $wt add -A', 'main', 0, 'a variable path is unknown, not main (documented gap)'],
  ['git add docs/QUEUE.md', 'main', 0, 'explicit path'], ['git add -A docs/QUEUE.md', 'main', 0, '-A with an explicit path'], ['git commit -m "x" docs/QUEUE.md', 'main', 0, 'commit with explicit path'], ['git commit -m "fix a -a thing"', 'main', 0, '-a inside the message'],
  ['git commit --amend --no-edit', 'main', 0, 'amend'], ['git status', 'main', 0, 'status'], ['git stash', 'main', 0, 'plain stash'], ['git stash pop', 'main', 0, 'stash pop'], ['git stash push -m "wip"', 'main', 0, 'stash push -m'], ['git log --all', 'main', 0, 'log --all'],
  ['git add -u', 'main', 0, 'add -u (tracked only)'], ['git diff --stat', 'main', 0, 'diff'], ['git commit -m "x" -- docs/QUEUE.md', 'main', 0, 'commit -- path'],
]);
gtable('G2 a builder with a HANDIN_DIR outside main writes nothing to main', [
  ['git add docs/QUEUE.md', 'main', 2, 'add with a path', { handinDir: HI }], ['git commit -m x docs/QUEUE.md', 'main', 2, 'commit', { handinDir: HI }], ['git merge agent/x', 'main', 2, 'merge', { handinDir: HI }],
  ['git reset --hard', 'main', 2, 'reset', { handinDir: HI }], ['git rebase main', 'main', 2, 'rebase', { handinDir: HI }], ['git cherry-pick abc123', 'main', 2, 'cherry-pick', { handinDir: HI }], ['git push', 'main', 2, 'push', { handinDir: HI }],
  [`git -C ${MB} commit -m x`, 'wt', 2, 'git -C main commit from a worktree', { handinDir: HI }], [`cd ${MB}; git merge x`, 'wt', 2, 'cd main; git merge', { handinDir: HI }],
  ['git status', 'main', 0, 'status', { handinDir: HI }], ['git log --oneline', 'main', 0, 'log', { handinDir: HI }], ['git diff', 'main', 0, 'diff', { handinDir: HI }], ['git worktree list', 'main', 0, 'worktree list', { handinDir: HI }],
  [`git -C ${wt} commit -m x`, 'main', 0, 'git -C <worktree> commit from a main cwd', { handinDir: HI }], ['git commit -m x', 'wt', 0, 'commit in the worktree', { handinDir: HI }], ['git push', 'wt', 0, 'push from the worktree', { handinDir: HI }],
  ['git add docs/QUEUE.md', 'main', 0, 'a NO-CONTRACT clerk (no HANDIN_DIR) adds a named file in main', {}], ['git commit -m x docs/QUEUE.md', 'main', 0, 'a clerk commits a named file in main', {}], ['git push', 'main', 0, 'a merger pushes from main', {}],
  ['git merge agent/x', 'main', 0, 'a merger merges in main', {}], ['git commit -m x docs/QUEUE.md', 'main', 0, 'a builder whose HANDIN_DIR is INSIDE main may commit there', { handinDir: M + '/docs/progress/r1' }],
]);
console.log('gates7 git guard as a process: the harness payload, transcripts, FAIL-OPEN');
{
  const trDir = path.join(tmp, 'tr'); mk(path.join(trDir, 'subagents')); const trMain = trDir + '.jsonl'; fs.writeFileSync(trMain, '');
  const brief = (txt) => JSON.stringify({ type: 'user', timestamp: new Date().toISOString(), message: { content: txt } }) + '\n';
  fs.writeFileSync(path.join(trDir, 'subagents', 'agent-bld7.jsonl'), brief(`AGENT-CONTRACT-MARKER: v1\nHANDIN_DIR: ${HI}\n## Owner's words\n"x"`));
  fs.writeFileSync(path.join(trDir, 'subagents', 'agent-clerk7.jsonl'), brief('NO-CONTRACT: merge the branch and update the queue file by hand'));
  const bf = path.join(tmp, 'brief.txt'); fs.writeFileSync(bf, `HANDIN_DIR: ${HI}\n`);
  fs.writeFileSync(path.join(trDir, 'subagents', 'agent-bf7.jsonl'), brief(`BRIEF-FILE: ${bf}\nread it`));
  const runHook = (input, raw) => spawnSync(process.execPath, [path.join(src, 'tools', 'hooks', 'git_guard.mjs')], { input: raw !== undefined ? raw : JSON.stringify(input), cwd: elsewhere, encoding: 'utf8', timeout: 60000, env: { ...process.env, CP_MAIN_ROOT: main } });
  const pay = (id, command, cwd) => ({ session_id: 's1', transcript_path: trMain, cwd, hook_event_name: 'PreToolUse', tool_name: 'Bash', tool_input: { command }, tool_use_id: 'toolu_01', ...(id ? { agent_id: id, agent_type: 'general-purpose' } : {}) });
  let r = runHook(pay(null, 'git add -A', main));
  t('process: the main session (no agent_id) may run git add -A in main (exit 0)', r.status === 0 && r.stderr === '', r.stderr);
  r = runHook(pay('clerk7', 'git add -A', main));
  t('process: a NO-CONTRACT clerk running git add -A in main: refused (exit 2, message names the incident fix)', r.status === 2 && /git add -A/.test(r.stderr) && /Name the files/.test(r.stderr), r.stderr);
  r = runHook(pay('clerk7', 'git add docs/QUEUE.md && git commit -m "queue [skip ci]" docs/QUEUE.md', main));
  t('process: the same clerk with explicit paths: allowed (exit 0)', r.status === 0, r.stderr);
  r = runHook(pay('bld7', 'git commit -m "x [skip ci]" docs/QUEUE.md', main));
  t('process: a builder (HANDIN_DIR in its own worktree, read from its transcript) committing a named file in main: refused (exit 2), message says HANDIN_DIR and git -C', r.status === 2 && /HANDIN_DIR/.test(r.stderr) && /git -C/.test(r.stderr), r.stderr);
  r = runHook(pay('bld7', `git -C ${wt} commit -m "x [skip ci]"`, main));
  t('process: the same builder using git -C <its worktree>: allowed (exit 0)', r.status === 0, r.stderr);
  r = runHook(pay('bf7', 'git merge agent/x', main));
  t('process: a builder whose HANDIN_DIR is in the BRIEF-FILE it names is read too: refused (exit 2)', r.status === 2, r.stderr);
  r = runHook(pay('ghost7', 'git commit -m x docs/QUEUE.md', main));
  t('process: an agent whose transcript cannot be read: G2 does not apply, explicit paths pass (exit 0)', r.status === 0, r.stderr);
  r = runHook(pay('ghost7', 'git add -A', main));
  t('process: ... but G1 still refuses git add -A (exit 2)', r.status === 2, r.stderr);
  r = runHook(null, 'not json');
  const lg = fs.readFileSync(path.join(isolation.stateDir, 'gate.log'), 'utf8');
  t('process: bad stdin: exit 0 and a FAIL-OPEN line in the (isolated) gate.log', r.status === 0 && /"gate":"git_guard"[^\n]*FAIL-OPEN/.test(lg), lg.slice(-300));
  const settings = JSON.parse(fs.readFileSync(path.join(src, '.claude', 'settings.json'), 'utf8'));
  const bashBlock = settings.hooks.PreToolUse.find((b) => b.matcher === 'Bash|PowerShell');
  t('settings.json wires git_guard.mjs beside delete_guard.mjs on Bash|PowerShell (timeout 15)', !!bashBlock && bashBlock.hooks.some((h) => /git_guard\.mjs$/.test(h.args[0]) && h.timeout === 15) && bashBlock.hooks.some((h) => /delete_guard\.mjs$/.test(h.args[0])));
}

console.log('test isolation and cleanup');
t('no line mentioning this run\'s temp folder reached the real gate.log', isolation.leaks().length === 0, isolation.leaks().slice(0, 3).join('\n'));
t('the photos behind the junction and in the fixture main survived every case', fs.readFileSync(path.join(photos, 'k.jpg'), 'utf8') === 'k' && fs.readFileSync(path.join(main, 'docs', 'reference', 'aerial', 'p1.jpg'), 'utf8') === 'photo');
const gone = cleanupTmp(tmp);
t(`the temp folder (junction unlinked first) is removed${process.env.KEEP_HOOKTEST ? ' [KEEP_HOOKTEST set: kept]' : ''}`, gone || !!process.env.KEEP_HOOKTEST, tmp);
console.log(`\n${pass} passed, ${fail} failed`);
if (failedNames.length) console.log('FAILED TESTS:\n - ' + failedNames.join('\n - '));
process.exit(fail ? 1 : 0);
