#!/usr/bin/env node
// gates8: real-payload tables for the items of the main session's scope decision of 23 Oct 2026 (brief gates8) and docs/progress/gates7/AUDIT.md.
//   A  six false blocks (gates7 audit "refused although legitimate"): git stash list|show, path-less git clean -fd in a clean worktree, robocopy /MIR into a worktree build dir, short concrete `needs:` lines,
//      git add -u . / -n ., the junction-walk cap (build output folders are not walked)
//   C  holes an honest agent could type: iex / Invoke-Expression "<command>", a name filter that can match docs / reference / a photo folder, del /s with a wildcard, powershell "cmd" without -Command,
//      cd /d, literal GIT_DIR= / GIT_WORK_TREE=, git add .\, and the G2 write list (branch -D, tag, gc, worktree add|remove, config, update-ref, apply, clean; fetch stays allowed)
//   E1 locked tests (lock_guard) as a process with real transcripts; E2 checkpoint hand-back through the real finish hook and check_handin --checkpoint, with real transcripts
// Where the expected values come from: the gates7 AUDIT.md (the numbered items named in the labels), the main session's scope decision (A, C, E texts in the brief) and, for the refusal text of the lock
// guard, the brief's exact wording. Nothing here pins a fault. Payloads use backslash cwds as the harness sends them on Windows. The fixture is a temp "main checkout" with registered worktrees (one
// holding a REAL junction at docs\reference); the whole temp folder (junctions unlinked first) is removed at the end. No test writes the live gate.log.
// Usage: node tools/hooks/test_guards8.mjs
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { spawnSync } from 'node:child_process';
import { fileURLToPath, pathToFileURL } from 'node:url';
import { isolateState, cleanupTmp } from '../test_isolation.mjs';

const src = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..', '..');
const tmp = fs.mkdtempSync(path.join(os.tmpdir(), 'guards8-'));
const isolation = isolateState(tmp, src);
let pass = 0, fail = 0; const failedNames = [];
const t = (name, cond, detail = '') => { if (cond) { pass++; console.log('  ok   ' + name); } else { fail++; failedNames.push(name); console.log('  FAIL ' + name + (detail ? '\n       ' + String(detail).replace(/\n/g, '\n       ') : '')); } };

const main = path.join(tmp, 'roadworld'); const M = main.replace(/\\/g, '/'); const MB = main;
const mk = (p) => fs.mkdirSync(p, { recursive: true });
mk(path.join(main, '.git', 'worktrees')); mk(path.join(main, 'docs', 'reference', 'aerial')); mk(path.join(main, '.claude', 'state'));
for (const n of ['p1.jpg', 'p2.jpg']) fs.writeFileSync(path.join(main, 'docs', 'reference', 'aerial', n), 'photo');
fs.writeFileSync(path.join(main, '.claude', 'state', 'ack_secret'), 'secret'); fs.writeFileSync(path.join(main, '.claude', 'state', 'gate.log'), '{}\n');
const photos = path.join(tmp, 'photos'); mk(photos); fs.writeFileSync(path.join(photos, 'k.jpg'), 'k');
const wtRoot = path.join(tmp, 'roadworld-wt');
const addWt = (n) => {
  const w = path.join(wtRoot, n); mk(path.join(main, '.git', 'worktrees', n)); mk(path.join(w, 'build', 'sub')); mk(path.join(w, 'docs')); mk(path.join(w, 'tests')); fs.writeFileSync(path.join(w, 'build', 'a.obj'), 'o');
  fs.writeFileSync(path.join(w, '.git'), 'gitdir: ' + path.join(main, '.git', 'worktrees', n) + '\n');
  fs.writeFileSync(path.join(main, '.git', 'worktrees', n, 'gitdir'), path.join(w, '.git') + '\n'); fs.writeFileSync(path.join(main, '.git', 'worktrees', n, 'commondir'), '../..\n');
  return w;
};
const wt = addWt('w1'), wtJ = addWt('j1'), wtU = addWt('u1');
fs.symlinkSync(photos, path.join(wtJ, 'docs', 'reference'), 'junction');
// an Unreal-style worktree: a big Intermediate folder, with a link buried in it (a build output folder is not walked: the cap is never hit by one)
mk(path.join(wtU, 'unreal', 'CityPainter', 'Intermediate', 'Build')); for (let i = 0; i < 400; i++) fs.writeFileSync(path.join(wtU, 'unreal', 'CityPainter', 'Intermediate', 'Build', 'f' + i + '.obj'), 'o');
const elsewhere = path.join(tmp, 'elsewhere'); mk(elsewhere);
mk(path.join(tmp, 'tempdir')); process.env.TEMP = path.join(tmp, 'tempdir'); process.env.TMP = process.env.TEMP; delete process.env.TMPDIR;

const dgm = await import(pathToFileURL(path.join(src, 'tools/hooks/delete_guard_impl.mjs')).href);
const sgm = await import(pathToFileURL(path.join(src, 'tools/hooks/state_guard_impl.mjs')).href);
const ggm = await import(pathToFileURL(path.join(src, 'tools/hooks/git_guard_impl.mjs')).href);
const lgm = await import(pathToFileURL(path.join(src, 'tools/hooks/lock_guard_impl.mjs')).href);
const gl = await import(pathToFileURL(path.join(src, 'tools/gates_lib.mjs')).href);
const cwdOf = { wt, wtJ, wtU, main, elsewhere };
const dg = (command, cwd) => dgm.analyse(command, cwd, 0, null, main);
const R = M + '/docs/reference';
function table(title, cases) {
  const bad = [];
  for (const [command, cw, want, label] of cases) {
    let got; try { got = dg(command, cwdOf[cw]).length ? 2 : 0; } catch (e) { got = 'THROW ' + e.message; }
    if (got !== want) bad.push(`${label}: ${command.slice(0, 120)} (cwd ${cw}): ${got}, wanted ${want}`);
  }
  t(`${title}: ${cases.length} cases`, bad.length === 0, bad.join('\n'));
}
const G = (command, cw, extra = {}) => ggm.analyse(command, cwdOf[cw] || cw, { mainRoot: main, ...extra }).length ? 2 : 0;
function gtable(title, cases) {
  const bad = [];
  for (const [command, cw, want, label, extra] of cases) { let got; try { got = G(command, cw, extra); } catch (e) { got = 'THROW ' + e.message; } if (got !== want) bad.push(`${label}: ${command.slice(0, 110)} (cwd ${cw}): ${got}, wanted ${want}`); }
  t(`${title}: ${cases.length} cases`, bad.length === 0, bad.join('\n'));
}
const sdir = M + '/.claude/state'; const secret = sdir + '/ack_secret';
const sg = (command, tool = 'Bash') => { try { return sgm.judge({ session_id: 's1', cwd: elsewhere, hook_event_name: 'PreToolUse', tool_name: tool, tool_input: { command }, agent_id: 'ag8', agent_type: 'general-purpose' }, main).length ? 2 : 0; } catch (e) { return 'THROW ' + e.message; } };
function stable(title, cases) { const bad = []; for (const [command, want, label, tool] of cases) { const got = sg(command, tool); if (got !== want) bad.push(`${label}: ${command.slice(0, 110)}: ${got}, wanted ${want}`); } t(`${title}: ${cases.length} cases`, bad.length === 0, bad.join('\n')); }
const HI = wt + '\\docs\\progress\\r8';

console.log('gates8 A1 git stash list|show: reading the stash is not a write');
gtable('A1 git guard, builder with HANDIN_DIR outside main (G2) and any subagent (G1)', [
  ['git stash list', 'main', 0, 'A1 stash list (builder)', { handinDir: HI }], ['git stash show', 'main', 0, 'A1 stash show (builder)', { handinDir: HI }], ['git stash show -p stash@{0}', 'main', 0, 'A1 stash show -p', { handinDir: HI }],
  ['git stash list', 'main', 0, 'A1 stash list (anyone)', {}], ['git stash push -m x', 'main', 2, 'A1 stash push stays a write for a builder', { handinDir: HI }], ['git stash pop', 'main', 2, 'A1 stash pop stays a write for a builder', { handinDir: HI }],
  ['git stash drop', 'main', 2, 'A1 stash drop', { handinDir: HI }], ['git stash -u', 'main', 2, 'A1 stash -u in main stays refused for anyone', {}],
]);
table('A1 delete guard', [['git stash list', 'main', 0, 'A1 stash list'], ['git stash show -p', 'main', 0, 'A1 stash show -p'], ['git stash list | head', 'wtJ', 0, 'A1 stash list piped']]);

console.log('gates8 A2 path-less git clean in a worktree that holds no junction or symlink');
table('A2 git clean', [
  ['git clean -fd', 'wt', 0, 'A2 git clean -fd in a clean worktree'], ['git clean -fdx', 'wt', 0, 'A2 git clean -fdx in a clean worktree'], ['git clean -fd', 'wtU', 0, 'A2 git clean -fd in an Unreal-style worktree'],
  ['git clean -fd', 'wtJ', 2, 'A2 git clean -fd in a worktree with the docs\\reference junction'], ['git clean -fdx', 'wtJ', 2, 'A2 git clean -fdx in the link tree'], ['git clean -fd', 'main', 2, 'A2 git clean -fd in main'],
  ['git clean -fdx', 'main', 2, 'A2 git clean -fdx in main'], ['git clean -fd build', 'wt', 0, 'A2 git clean -fd build (named)'], ['git clean -n', 'main', 0, 'A2 dry run'], ['git clean -fd docs', 'main', 2, 'A2 git clean -fd docs in main'],
  [`git -C ${wt} clean -fd`, 'main', 0, 'A2 git -C <clean worktree> clean -fd'], [`git -C ${MB} clean -fd`, 'wt', 2, 'A2 git -C main clean -fd from a worktree'],
]);

console.log('gates8 A3 robocopy /MIR into a worktree build dir');
table('A3 robocopy', [
  [`robocopy ${tmp}\\out ${wt}\\build /MIR`, 'elsewhere', 0, 'A3 /MIR into <worktree>\\build'], ['robocopy ..\\out build /MIR', 'wt', 0, 'A3 /MIR into build (relative, cwd worktree)'], [`robocopy ${tmp}\\out ${wtU}\\unreal\\CityPainter\\Intermediate /MIR`, 'elsewhere', 0, 'A3 /MIR into Intermediate'],
  [`robocopy ${tmp}\\out ${wtJ}\\docs\\reference /MIR`, 'elsewhere', 2, 'A3 /MIR onto the junction'], [`robocopy ${tmp}\\out ${MB}\\docs /MIR`, 'elsewhere', 2, 'A3 /MIR into main docs'], [`robocopy ${tmp}\\out ${MB}\\docs\\reference /MIR`, 'elsewhere', 2, 'A3 /MIR into main docs\\reference'],
  [`robocopy ${tmp}\\out ${wtJ}\\docs /MIR`, 'elsewhere', 2, 'A3 /MIR into the docs folder of the link tree (the junction lives there) stays refused'],
  [`robocopy ${tmp}\\out ${wt}\\docs /MIR`, 'elsewhere', 0, 'A3 /MIR into the docs folder of a clean worktree (no link in it) is judged like rm -rf'], [`robocopy ${tmp}\\out ${MB} /MIR`, 'elsewhere', 2, 'A3 /MIR into the main root'],
]);

console.log('gates8 A4 short concrete needs lines (gates_lib.needsIsConcrete)');
{
  const yes = ['a screenshot of the junction', 'texture for the kerb', 'the roundabout photo', 'an editor session for the kerb', 'a build of the junction plugin', 'a render of the T-junction', 'capture of the kerb', 'sample of the verge'];
  const no = ['owner decision about the junction', 'help', 'the owner', 'owner input', 'a decision', 'more guidance', 'approval'];
  const bad = [...yes.filter((x) => !gl.needsIsConcrete(x)).map((x) => 'should be concrete: ' + x), ...no.filter((x) => gl.needsIsConcrete(x)).map((x) => 'should NOT be concrete: ' + x)];
  t(`A4 needsIsConcrete: ${yes.length} short concrete lines pass, ${no.length} vague ones do not`, bad.length === 0, bad.join('\n'));
}

console.log('gates8 A5 git add -u . and git add -n . in main');
gtable('A5 staging', [
  ['git add -u .', 'main', 0, 'A5 add -u .'], ['git add -u', 'main', 0, 'A5 add -u'], ['git add -n .', 'main', 0, 'A5 add -n .'], ['git add --dry-run -A', 'main', 0, 'A5 add --dry-run -A'], ['git add -nA', 'main', 0, 'A5 add -nA'],
  ['git add -A', 'main', 2, 'A5 add -A still refused'], ['git add .', 'main', 2, 'A5 add . still refused'], ['git add -uA', 'main', 2, 'A5 add -uA stages everything (A wins)'], ['git add -A -u', 'main', 2, 'A5 add -A -u (A wins)'],
]);

console.log('gates8 A6 the junction walk skips build output folders; real Unreal worktrees measured read-only');
{
  const lb = dgm.linksBelow;
  // a link buried in a build output folder is not walked; one at the top level of the worktree is
  mk(path.join(wtU, 'unreal', 'CityPainter', 'Intermediate', 'Build', 'deep')); fs.symlinkSync(photos, path.join(wtU, 'unreal', 'CityPainter', 'Intermediate', 'Build', 'deep', 'lnk'), 'junction');
  const t0 = Date.now(); const r = lb(wtU); const dt = Date.now() - t0;
  t('A6 linksBelow(Unreal-style worktree with 400 build files and a link inside Intermediate) does not walk Intermediate (no link found, not capped)', r.length === 0 && !r.capped, JSON.stringify({ n: r.length, capped: r.capped, seen: r.seen }));
  t('A6 ... and a link outside the build folders is found (docs\\reference in the link tree)', lb(wtJ).length === 1);
  t('A6 ... and a link NAMED like a build folder is still found as an entry', (() => { const w = addWt('b1'); fs.rmSync(path.join(w, 'build'), { recursive: true }); fs.symlinkSync(photos, path.join(w, 'build'), 'junction'); const x = lb(w); try { fs.rmdirSync(path.join(w, 'build')); } catch { /* left */ } return x.length === 1; })());
  t('A6 git clean -fd in the Unreal-style worktree (link buried in Intermediate) is allowed, the walk costs < 2 s', dg('git clean -fd', wtU).length === 0 && Date.now() - t0 < 2000);
  t('A6 the cap still refuses: a 1 ms budget on a walkable tree reports capped', (() => { const x = lb(wt, { ms: 0, limit: 1 }); return x.capped === true; })());
}

console.log('gates8 C1 iex / Invoke-Expression "<command>" is a command');
table('C1 delete guard', [
  ['iex "rm -rf docs"', 'main', 2, 'C1 iex "rm -rf docs"'], ["Invoke-Expression 'Remove-Item docs -Recurse -Force'", 'wtJ', 2, 'C1 Invoke-Expression Remove-Item docs'], ['iex "Remove-Item docs -Recurse -Force"', 'wtJ', 2, 'C1 iex Remove-Item'],
  [`iex "rm -rf ${R}"`, 'elsewhere', 2, 'C1 iex rm -rf the photos'], ['iex "rm -rf build"', 'wt', 0, 'C1 iex rm -rf build in a clean worktree'], ['Invoke-Expression "git clean -fdx"', 'main', 2, 'C1 iex git clean -fdx in main'],
  ['iex "echo hello world"', 'main', 0, 'C1 iex echo is text'], ['iex ".\\build.ps1"', 'wt', 0, 'C1 iex of a script path'], ['Invoke-Expression "Get-ChildItem docs"', 'main', 0, 'C1 a listing'],
]);
gtable('C1 git guard', [
  ['iex "git add -A"', 'main', 2, 'C1 iex "git add -A"'], ["Invoke-Expression 'git add .'", 'main', 2, 'C1 Invoke-Expression git add .'], ['iex "git add docs/QUEUE.md"', 'main', 0, 'C1 iex git add <file>'],
  ['iex "git commit -am x"', 'main', 2, 'C1 iex git commit -am'], ['iex "git merge x"', 'main', 2, 'C1 iex git merge as a builder', { handinDir: HI }], ['iex "git status"', 'main', 0, 'C1 iex git status as a builder', { handinDir: HI }],
]);
stable('C1 state guard', [
  [`iex "cat ${secret}"`, 2, 'C1 iex cat the secret', 'PowerShell'], [`iex "Get-Content ${sdir}/gate.log"`, 0, 'C1 iex Get-Content gate.log', 'PowerShell'], [`Invoke-Expression 'Get-Content ${secret}'`, 2, 'C1 Invoke-Expression Get-Content the secret', 'PowerShell'],
  [`iex "rm ${sdir}/gate.log"`, 2, 'C1 iex rm gate.log', 'PowerShell'],
]);

console.log('gates8 C2 a name filter that can match docs, reference or a photo folder name, from main');
table('C2 filters', [
  ['Get-ChildItem -Path . -Filter "doc*" -Directory | Remove-Item -Recurse -Force', 'main', 2, 'C2 -Filter doc*'], ['Get-ChildItem -Path . -Filter "ref*" -Directory | Remove-Item -Recurse -Force', 'main', 2, 'C2 -Filter ref*'],
  ['Get-ChildItem -Path . -Filter "aerial" | Remove-Item -Recurse -Force', 'main', 2, 'C2 -Filter aerial'], ['gci . -Filter "street*" -Recurse | Remove-Item -Force', 'main', 2, 'C2 -Filter street*'], ['gci . -Filter "gsv" | Remove-Item -Recurse', 'main', 2, 'C2 -Filter gsv'],
  ['find . -name "doc*" | xargs rm -rf', 'main', 2, 'C2 find -name doc* | xargs rm'], ['find . -name "reference" -exec rm -rf {} \\;', 'main', 2, 'C2 find -name reference -exec rm'], ['find . -iname "aerial*" | xargs rm -rf', 'main', 2, 'C2 find -iname aerial*'],
  ['Get-ChildItem -Path . -Filter "*.log" | Remove-Item -Force', 'main', 0, 'C2 *.log cannot match a picture or a photo folder'], ['Get-ChildItem -Path . -Filter "build*" -Directory | Remove-Item -Recurse -Force', 'main', 0, 'C2 build* from main (unchanged)'],
  ['Get-ChildItem -Path . -Filter "doc*" -Directory | Remove-Item -Recurse -Force', 'wt', 0, 'C2 doc* in a clean worktree (no photos below it)'], ['Get-ChildItem -Path . -Filter "doc*" -Directory | Remove-Item -Recurse -Force', 'wtJ', 2, 'C2 doc* in the link tree'],
]);

console.log('gates8 C3 del /s with a wildcard');
table('C3 del /s', [
  ['del /s /q *.jpg', 'main', 2, 'C3 del /s /q *.jpg in main'], ['del /s /q *', 'main', 2, 'C3 del /s /q * in main'], ['del /s /q *.*', 'main', 2, 'C3 del /s /q *.* in main'], ['del /f /s /q *.jpg', 'elsewhere', 0, 'C3 outside every tree: nothing to protect'],
  [`del /s /q ${R.replace(/\//g, '\\')}\\*.jpg`, 'elsewhere', 2, 'C3 del /s the photos by absolute wildcard'], ['del /s /q docs\\reference\\*', 'main', 2, 'C3 del /s docs\\reference\\*'], ['del /s /q *.obj', 'wtJ', 2, 'C3 del /s *.obj in the link tree'],
  ['del /s /q build\\*.obj', 'wt', 2, 'C3 del /s build\\*.obj in a worktree stays refused (gates7 item 2: cmd del /s walks into any junction; use Remove-Item -Recurse)'], ['del /s /q *.obj', 'wt', 2, 'C3 del /s *.obj from the worktree root walks every folder: stays refused (gates7 item 2)'],
  ['del /q build\\a.obj', 'wt', 0, 'C3 del a file, no /s'],
]);

console.log('gates8 C4 powershell "cmd" without -Command');
table('C4 delete guard', [
  ['powershell "Remove-Item docs -Recurse -Force"', 'wtJ', 2, 'C4 powershell "Remove-Item docs"'], ['pwsh "Remove-Item docs -Recurse -Force"', 'wtJ', 2, 'C4 pwsh "Remove-Item docs"'], ['powershell -NoProfile "Remove-Item docs -Recurse -Force"', 'wtJ', 2, 'C4 powershell -NoProfile "..."'],
  ['powershell -NoProfile -NonInteractive "rm -rf docs"', 'main', 2, 'C4 powershell -NoProfile -NonInteractive "rm -rf docs"'], ['powershell "Remove-Item build -Recurse -Force"', 'wt', 0, 'C4 the same on build in a clean worktree'],
  ['powershell -File x.ps1', 'main', 0, 'C4 -File is a script'], ['powershell "Get-Date"', 'main', 0, 'C4 text'],
]);
gtable('C4 git guard', [['powershell "git add -A"', 'main', 2, 'C4 powershell "git add -A"'], ['pwsh -NoProfile "git commit -am x"', 'main', 2, 'C4 pwsh -NoProfile "git commit -am x"'], ['powershell "git add docs/QUEUE.md"', 'main', 0, 'C4 explicit path']]);

console.log('gates8 C5 cd /d, literal GIT_DIR= / GIT_WORK_TREE=, git add .\\');
gtable('C5 git guard', [
  [`cd /d ${MB} && git add -A`, 'elsewhere', 2, 'C5 cd /d main && git add -A'], [`cd /d ${wt} && git add -A`, 'main', 0, 'C5 cd /d <worktree> && git add -A'], [`cd /d ${MB}\\docs && git add .`, 'elsewhere', 2, 'C5 cd /d main\\docs && git add .'],
  [`GIT_DIR=${M}/.git git add -A`, 'elsewhere', 2, 'C5 GIT_DIR=<main>/.git git add -A'], [`GIT_WORK_TREE=${M} git add -A`, 'elsewhere', 2, 'C5 GIT_WORK_TREE=<main> git add -A'],
  [`$env:GIT_DIR='${M}/.git'; git add -A`, 'elsewhere', 2, 'C5 $env:GIT_DIR=...; git add -A'], [`export GIT_WORK_TREE=${M}; git add .`, 'elsewhere', 2, 'C5 export GIT_WORK_TREE; git add .'],
  [`set GIT_DIR=${MB}\\.git && git add -A`, 'elsewhere', 2, 'C5 set GIT_DIR=... && git add -A'], [`GIT_DIR=${M}/.git/worktrees/w1 git add -A`, 'elsewhere', 0, 'C5 GIT_DIR of a worktree is not main'],
  ['git add .\\', 'main', 2, 'C5 git add .\\'], ['git add ./', 'main', 2, 'C5 git add ./'], ['git add ./.', 'main', 2, 'C5 git add ./.'], ['git add .\\*', 'main', 2, 'C5 git add .\\*'], ['git add .\\docs\\QUEUE.md', 'main', 0, 'C5 git add .\\<file>'],
  ['git add .gitignore', 'main', 0, 'C5 git add .gitignore'], ['git add ..\\x', 'main', 0, 'C5 git add ..\\x is a path'],
]);
stable('C5 state guard cd /d', [[`cd /d ${sdir.replace(/\//g, '\\')} && type ack_secret`, 2, 'C5 cd /d state && type ack_secret'], [`cd /d ${sdir.replace(/\//g, '\\')} && type gate.log`, 0, 'C5 cd /d state && type gate.log']]);
table('C5 delete guard cd /d', [[`cd /d ${wt} && rmdir /s /q build`, 'elsewhere', 0, 'C5 cd /d <worktree> && rmdir /s /q build'], [`cd /d ${MB} && rmdir /s /q docs`, 'elsewhere', 2, 'C5 cd /d main && rmdir /s /q docs']]);

console.log('gates8 C6 the G2 write list');
gtable('C6 G2: a builder with HANDIN_DIR outside main writes nothing to main; reading stays allowed', [
  ['git branch -D x', 'main', 2, 'branch -D', { handinDir: HI }], ['git branch newone', 'main', 2, 'branch <name>', { handinDir: HI }], ['git tag v1', 'main', 2, 'tag <name>', { handinDir: HI }], ['git tag -d v1', 'main', 2, 'tag -d', { handinDir: HI }],
  ['git gc', 'main', 2, 'gc', { handinDir: HI }], ['git worktree add ..\\x', 'main', 2, 'worktree add', { handinDir: HI }], ['git worktree remove ..\\x', 'main', 2, 'worktree remove', { handinDir: HI }],
  ['git config user.name x', 'main', 2, 'config k v', { handinDir: HI }], ['git config --unset user.name', 'main', 2, 'config --unset', { handinDir: HI }], ['git update-ref -d refs/heads/x', 'main', 2, 'update-ref', { handinDir: HI }],
  ['git apply p.diff', 'main', 2, 'apply', { handinDir: HI }], ['git clean -fd', 'main', 2, 'clean -fd', { handinDir: HI }], ['git checkout main', 'main', 2, 'checkout', { handinDir: HI }], ['git restore x', 'main', 2, 'restore', { handinDir: HI }],
  ['git fetch', 'main', 0, 'fetch stays allowed', { handinDir: HI }], ['git fetch origin', 'main', 0, 'fetch origin', { handinDir: HI }], ['git branch', 'main', 0, 'branch listing', { handinDir: HI }], ['git branch -a', 'main', 0, 'branch -a', { handinDir: HI }],
  ['git branch --show-current', 'main', 0, 'branch --show-current', { handinDir: HI }], ['git tag -l', 'main', 0, 'tag -l', { handinDir: HI }], ['git tag', 'main', 0, 'tag (listing)', { handinDir: HI }], ['git config --get user.name', 'main', 0, 'config --get', { handinDir: HI }],
  ['git config --list', 'main', 0, 'config --list', { handinDir: HI }], ['git worktree list', 'main', 0, 'worktree list', { handinDir: HI }], ['git apply --check p.diff', 'main', 0, 'apply --check', { handinDir: HI }], ['git clean -n', 'main', 0, 'clean -n', { handinDir: HI }],
  ['git rev-parse HEAD', 'main', 0, 'rev-parse', { handinDir: HI }], ['git show HEAD', 'main', 0, 'show', { handinDir: HI }], ['git blame x', 'main', 0, 'blame', { handinDir: HI }],
  ['git branch -D x', 'wt', 0, 'the same branch -D inside the builder worktree', { handinDir: HI }], ['git gc', 'wt', 0, 'gc in the worktree', { handinDir: HI }], [`git -C ${wt} tag v1`, 'main', 0, 'git -C <worktree> tag', { handinDir: HI }],
  ['git branch -D x', 'main', 0, 'a merger (no HANDIN_DIR) may delete a branch in main', {}], ['git tag v1', 'main', 0, 'a merger may tag', {}],
]);

// ---------------------------------------------------------------- E1 locked tests, as a process with real transcripts
console.log('gates8 E1 locked tests (lock_guard.mjs as a process, real transcripts)');
const LOCK_MSG = 'locked test: written by the auditor; make it pass, or write BLOCKED';
{
  const prior = path.join(wt, 'docs', 'progress', 'prev'); mk(prior); fs.writeFileSync(path.join(prior, 'AUDIT.md'), '# audit\n');
  mk(path.join(wt, 'tests', 'locked')); fs.writeFileSync(path.join(wt, 'tests', 'locked', 'a.mjs'), 'a');
  fs.writeFileSync(path.join(wt, 'tests', 'listed_test.mjs'), 'l'); fs.writeFileSync(path.join(wt, 'tests', 'other.mjs'), 'o');
  fs.writeFileSync(path.join(prior, 'LOCKED_TESTS.txt'), '# the auditor wrote these\ntests/listed_test.mjs\n' + path.join(tmp, 'abs_locked', 'z.mjs').replace(/\\/g, '/') + '\n');
  mk(path.join(tmp, 'abs_locked')); fs.writeFileSync(path.join(tmp, 'abs_locked', 'z.mjs'), 'z');
  const trDir = path.join(tmp, 'tr8'); mk(path.join(trDir, 'subagents')); const trMain = trDir + '.jsonl'; fs.writeFileSync(trMain, '');
  const brief = (txt) => JSON.stringify({ type: 'user', timestamp: new Date().toISOString(), message: { content: txt } }) + '\n';
  const aw = (id, txt) => fs.writeFileSync(path.join(trDir, 'subagents', 'agent-' + id + '.jsonl'), brief(txt));
  aw('bld8', `AGENT-CONTRACT-MARKER: v1\nHANDIN_DIR: ${HI}\nPRIOR-AUDIT: ${path.join(prior, 'AUDIT.md')}\n## Owner's words\n"x"`);
  aw('aud8', 'NO-CONTRACT: audit the hand-in of the last round and write the tests that pin its faults\nAUDIT-OF: ' + HI);
  aw('clerk8', 'NO-CONTRACT: merge the branch and update the queue file by hand');
  aw('bnp8', `AGENT-CONTRACT-MARKER: v1\nHANDIN_DIR: ${HI}\n## Owner's words\n"x"`);   // a builder with no PRIOR-AUDIT: only tests/locked is locked for it
  const bf = path.join(tmp, 'brief8.txt'); fs.writeFileSync(bf, `HANDIN_DIR: ${HI}\nPRIOR-AUDIT: ${path.join(prior, 'AUDIT.md')}\n`); aw('bf8', `BRIEF-FILE: ${bf}\nread it`);
  const lk = (id, tool, ti, cwd = wt) => { const input = { session_id: 's1', transcript_path: trMain, cwd, hook_event_name: 'PreToolUse', tool_name: tool, tool_input: ti, tool_use_id: 'toolu_01', ...(id ? { agent_id: id, agent_type: 'general-purpose' } : {}) };
    const r = spawnSync(process.execPath, [path.join(src, 'tools', 'hooks', 'lock_guard.mjs')], { input: JSON.stringify(input), cwd: elsewhere, encoding: 'utf8', timeout: 60000, env: { ...process.env, CP_MAIN_ROOT: main } }); return r; };
  const refused = (r) => r.status === 2 && r.stderr.includes(LOCK_MSG);
  const W = (p) => ['Write', { file_path: p, content: 'x' }], E = (p) => ['Edit', { file_path: p, old_string: 'a', new_string: 'b' }], B = (c) => ['Bash', { command: c }], P = (c) => ['PowerShell', { command: c }];
  const lockedA = path.join(wt, 'tests', 'locked', 'a.mjs'), listed = path.join(wt, 'tests', 'listed_test.mjs'), other = path.join(wt, 'tests', 'other.mjs'), absL = path.join(tmp, 'abs_locked', 'z.mjs');
  const cases = [
    ['bld8', ...W(lockedA), 2, 'Write tests/locked/a.mjs'], ['bld8', ...E(lockedA), 2, 'Edit tests/locked/a.mjs'], ['bld8', ...W(path.join(wt, 'tests', 'locked', 'new.mjs')), 2, 'Write a NEW file under tests/locked'],
    ['bld8', ...E(path.join(tmp, 'elsewhere_repo', 'tests', 'locked', 'q.mjs')), 2, 'Edit under tests/locked of any other repo'], ['bld8', ...W(listed), 2, 'Write a file listed in LOCKED_TESTS.txt (relative entry)'], ['bld8', ...E(absL), 2, 'Edit a file listed by absolute path'],
    ['bld8', ...W(other), 0, 'Write an unlisted test'], ['bld8', ...W(path.join(wt, 'tools', 'x.mjs')), 0, 'Write a tool'], ['bld8', 'NotebookEdit', { notebook_path: lockedA, new_source: 'x' }, 2, 'NotebookEdit a locked file'],
    ['bld8', ...B('echo x > tests/locked/a.mjs'), 2, 'redirect >'], ['bld8', ...B('echo x >> tests\\locked\\a.mjs'), 2, 'redirect >> with backslashes'], ['bld8', ...B('sed -i s/a/b/ tests/locked/a.mjs'), 2, 'sed -i'],
    ['bld8', ...B('rm tests/locked/a.mjs'), 2, 'rm'], ['bld8', ...B('rm -rf tests/locked'), 2, 'rm -rf tests/locked'], ['bld8', ...B('rm -rf tests'), 2, 'rm -rf tests (holds tests/locked)'], ['bld8', ...B('mv tests/locked/a.mjs /tmp/x'), 2, 'mv away'],
    ['bld8', ...B('mv /tmp/x tests/locked/a.mjs'), 2, 'mv onto'], ['bld8', ...B('mv tests tests_old'), 2, 'mv tests tests_old'], ['bld8', ...B('cp /tmp/y tests/locked/a.mjs'), 2, 'cp onto'], ['bld8', ...B('git rm tests/locked/a.mjs'), 2, 'git rm'],
    ['bld8', ...B('git checkout -- tests/locked/a.mjs'), 2, 'git checkout -- file'], ['bld8', ...B('git restore tests/locked/a.mjs'), 2, 'git restore file'], ['bld8', ...B('truncate -s0 tests/locked/a.mjs'), 2, 'truncate'],
    ['bld8', ...B('echo x | tee tests/locked/a.mjs'), 2, 'tee'], ['bld8', ...B('touch tests/locked/a.mjs'), 2, 'touch'], ['bld8', ...B('cd tests/locked && echo x > a.mjs'), 2, 'cd then redirect'],
    ['bld8', ...B('bash -c "echo x > tests/locked/a.mjs"'), 2, 'bash -c'], ['bld8', ...B('python -c "open(\'tests/locked/a.mjs\',\'w\').write(\'x\')"'), 2, 'python -c write'], ['bld8', ...B('node -e "fs.unlinkSync(\'tests/locked/a.mjs\')"'), 2, 'node -e unlink'],
    ['bld8', ...B('echo x > tests/listed_test.mjs'), 2, 'redirect onto a listed file'], ['bld8', ...B('rm tests/listed_test.mjs'), 2, 'rm a listed file'], ['bld8', ...B(`rm ${absL}`), 2, 'rm a listed file by absolute path'],
    ['bld8', ...P('Set-Content tests\\locked\\a.mjs "x"'), 2, 'Set-Content'], ['bld8', ...P('"x" | Out-File tests\\locked\\a.mjs'), 2, 'Out-File'], ['bld8', ...P('Remove-Item tests\\locked\\a.mjs'), 2, 'Remove-Item'], ['bld8', ...P('Add-Content tests\\listed_test.mjs "x"'), 2, 'Add-Content a listed file'],
    ['bld8', ...P('Move-Item tests\\locked\\a.mjs ..\\x.mjs'), 2, 'Move-Item'], ['bld8', ...P('Rename-Item tests\\locked\\a.mjs b.mjs'), 2, 'Rename-Item'], ['bld8', ...P('Copy-Item x.mjs tests\\locked\\a.mjs'), 2, 'Copy-Item onto'],
    ['bld8', ...P('iex "rm tests/locked/a.mjs"'), 2, 'iex "rm ..."'],
    ['bld8', ...B('robocopy C:\\x tests\\locked /MIR'), 2, 'self-audit 1: robocopy /MIR onto the folder'], ['bld8', ...B('xcopy /y C:\\x\\a.mjs tests\\locked\\'), 2, 'self-audit 1: xcopy onto the folder'], ['bld8', ...B('rsync -a x/ tests/locked/'), 2, 'self-audit 1: rsync onto the folder'],
    ['bld8', ...B('ln -sf /tmp/x tests/locked/a.mjs'), 2, 'self-audit 1: ln -sf over the file'], ['bld8', ...B('find tests/locked -delete'), 2, 'self-audit 1: find <locked> -delete'], ['bld8', ...B('find tests -type f -exec rm {} +'), 2, 'self-audit 1: find tests -exec rm (the folder holds tests/locked)'],
    ['bld8', ...P('Get-ChildItem tests\\locked | Remove-Item'), 2, 'self-audit 1: a listing of the locked folder piped into Remove-Item'], ['bld8', ...P('gci tests\\locked | Remove-Item -Force'), 2, 'self-audit 1: gci | Remove-Item -Force'],
    ['bld8', ...B('git clean -fdx tests/locked'), 2, 'self-audit 1: git clean <locked path>'], ['bld8', ...P('[IO.File]::WriteAllText("tests\\locked\\a.mjs","x")'), 2, 'self-audit 1: [IO.File]::WriteAllText'], ['bld8', ...P('[System.IO.File]::Delete("tests\\listed_test.mjs")'), 2, 'self-audit 1: [System.IO.File]::Delete of a listed file'],
    ['bld8', ...B('robocopy C:\\x tests\\other_dir /MIR'), 0, 'self-audit 1: robocopy into an unlocked folder'], ['bld8', ...B('rsync -a x/ build/'), 0, 'self-audit 1: rsync into build'], ['bld8', ...B('find tests/locked -name "*.mjs"'), 0, 'self-audit 1: find in the locked folder without -delete'],
    ['bld8', ...P('Get-ChildItem tests\\locked | Select-Object Name'), 0, 'self-audit 1: a listing of the locked folder'], ['bld8', ...P('Get-ChildItem build | Remove-Item -Recurse -Force'), 0, 'self-audit 1: a listing of build piped into Remove-Item'], ['bld8', ...P('[IO.File]::ReadAllText("tests\\locked\\a.mjs")'), 0, 'self-audit 1: [IO.File]::ReadAllText'],
    ['bld8', ...B('cat tests/locked/a.mjs'), 0, 'cat (read)'], ['bld8', ...B('node tests/locked/a.mjs'), 0, 'run the locked test'], ['bld8', ...B('cp tests/locked/a.mjs /tmp/y'), 0, 'cp FROM the locked test'], ['bld8', ...B('git status'), 0, 'git status'],
    ['bld8', ...B('git checkout -- .'), 0, 'git checkout -- . (cannot be named a locked path)'], ['bld8', ...B('rm tests/other.mjs'), 0, 'rm an unlisted test'], ['bld8', ...B('echo x > tests/other.mjs'), 0, 'redirect to an unlisted test'],
    ['bld8', ...B('grep -rn foo tests/locked'), 0, 'grep in the locked folder'], ['bld8', ...B('ctest --test-dir build'), 0, 'ctest'], ['bld8', ...P('Get-Content tests\\locked\\a.mjs'), 0, 'Get-Content'], ['bld8', ...B('echo "tests/locked is read-only"'), 0, 'text that names the folder'],
    ['aud8', ...W(lockedA), 0, 'AUDITOR (NO-CONTRACT, AUDIT-OF): Write a locked test'], ['aud8', ...B('echo x > tests/locked/a.mjs'), 0, 'AUDITOR: redirect'], ['clerk8', ...W(lockedA), 0, 'CLERK (NO-CONTRACT, no HANDIN_DIR): Write'],
    [null, ...W(lockedA), 0, 'the MAIN SESSION (no agent_id): Write'], [null, ...B('rm tests/locked/a.mjs'), 0, 'the main session: rm'],
    ['ghost8', ...W(lockedA), 0, 'an agent whose transcript cannot be read is not judged a builder'],
    ['bnp8', ...W(lockedA), 2, 'a builder with no PRIOR-AUDIT: tests/locked is still locked'], ['bnp8', ...W(listed), 0, 'a builder with no PRIOR-AUDIT: an unlisted file is free'],
    ['bf8', ...W(listed), 2, 'builder whose HANDIN_DIR and PRIOR-AUDIT are in the BRIEF-FILE it names'],
  ];
  const bad = [];
  for (const [id, tool, ti, want, label] of cases) {
    const r = lk(id, tool, ti);
    const got = want === 2 ? (refused(r) ? 2 : `status ${r.status} ${r.stderr.slice(0, 120)}`) : (r.status === 0 && r.stderr === '' ? 0 : `status ${r.status} ${r.stderr.slice(0, 160)}`);
    if (got !== want) bad.push(`${label}: ${String(ti.command || ti.file_path || ti.notebook_path).slice(0, 90)}: ${got}, wanted ${want}`);
  }
  t(`E1 lock_guard process: ${cases.length} real payloads (refusal text exact: "${LOCK_MSG}")`, bad.length === 0, bad.join('\n'));
  let r = lk('bld8', 'Read', { file_path: lockedA }); t('E1 a Read of a locked file: not this hook\'s business (exit 0)', r.status === 0);
  r = spawnSync(process.execPath, [path.join(src, 'tools', 'hooks', 'lock_guard.mjs')], { input: 'not json', cwd: elsewhere, encoding: 'utf8', env: { ...process.env, CP_MAIN_ROOT: main } });
  const lg = fs.readFileSync(path.join(isolation.stateDir, 'gate.log'), 'utf8');
  t('E1 bad stdin: exit 0 and a FAIL-OPEN line for lock_guard in the (isolated) gate.log', r.status === 0 && /"gate":"lock_guard"[^\n]*FAIL-OPEN/.test(lg), lg.slice(-300));
  r = lk('bld8', ...W(lockedA)); t('E1 a refusal is logged as deny with the agent id', /"gate":"lock_guard","decision":"deny"[^\n]*"agent_id":"bld8"/.test(fs.readFileSync(path.join(isolation.stateDir, 'gate.log'), 'utf8')));
  t('E1 the locked files are untouched after every case (nothing here ran a command)', fs.readFileSync(lockedA, 'utf8') === 'a' && fs.readFileSync(listed, 'utf8') === 'l' && fs.readFileSync(absL, 'utf8') === 'z');
  const settings = JSON.parse(fs.readFileSync(path.join(src, '.claude', 'settings.json'), 'utf8'));
  const blk = settings.hooks.PreToolUse.find((b) => b.hooks.some((h) => /lock_guard\.mjs$/.test(h.args[0])));
  t('E1 settings.json wires lock_guard.mjs on Write|Edit|NotebookEdit|Bash|PowerShell (timeout 15)', !!blk && blk.matcher === 'Write|Edit|NotebookEdit|Bash|PowerShell' && blk.hooks.find((h) => /lock_guard\.mjs$/.test(h.args[0])).timeout === 15, JSON.stringify(blk));
}

// ---------------------------------------------------------------- E2 checkpoint hand-back, through the real finish hook and check_handin --checkpoint
console.log('gates8 E2 checkpoint hand-back (handin_gate.mjs and check_handin.mjs --checkpoint, real transcripts)');
{
  // the hook finds check_handin.mjs under the main checkout it resolves (CP_MAIN_ROOT): a temp main with copies of the tools
  const root = path.join(tmp, 'main2'); mk(path.join(root, '.git')); mk(path.join(root, 'tools', 'hooks')); mk(path.join(root, 'docs', 'templates')); mk(path.join(root, 'docs', 'reference', 'aerial'));
  for (const f of fs.readdirSync(path.join(src, 'tools'))) if (/\.mjs$/.test(f)) fs.copyFileSync(path.join(src, 'tools', f), path.join(root, 'tools', f));
  for (const f of fs.readdirSync(path.join(src, 'tools', 'hooks'))) if (/\.mjs$/.test(f)) fs.copyFileSync(path.join(src, 'tools', 'hooks', f), path.join(root, 'tools', 'hooks', f));
  fs.copyFileSync(path.join(src, 'docs', 'AGENT_CONTRACT.md'), path.join(root, 'docs', 'AGENT_CONTRACT.md'));
  const state = path.join(tmp, 'state-e2'); mk(state);
  const env = { ...process.env, CP_GATE_STATE_DIR: state }; delete env.CP_MAIN_ROOT;
  const wtE = path.join(tmp, 'wt-e2'); const hd = path.join(wtE, 'docs', 'progress', 'cp'); mk(hd);
  const logOf = () => { try { return fs.readFileSync(path.join(state, 'gate.log'), 'utf8').trim().split('\n').filter(Boolean).map((l) => JSON.parse(l)); } catch { return []; } };
  const lastHandin = () => logOf().filter((l) => l.gate === 'handin').pop() || {};
  const mkTr = (name, handinDir, startAgoMs = 600000) => {
    const p = path.join(tmp, name + '.jsonl');
    const brief = `AGENT-CONTRACT-MARKER: v1\nHANDIN_DIR: ${handinDir}\nHANDIN: no-visual\n## Owner's words\n"The photo is the target." (11 Oct)\n`;
    fs.writeFileSync(p, JSON.stringify({ type: 'user', isSidechain: true, timestamp: new Date(Date.now() - startAgoMs).toISOString(), message: { role: 'user', content: brief } }) + '\n' + JSON.stringify({ type: 'assistant', message: { role: 'assistant', content: [{ type: 'text', text: 'working' }] } }) + '\n');
    return p;
  };
  const stopHook = (id, tp, event = 'SubagentStop') => {
    fs.appendFileSync(path.join(state, 'gate.log'), JSON.stringify({ ts: new Date().toISOString(), gate: 'inject', agent_type: 'general-purpose', agent_id: id, decision: 'inject', chars: 1 }) + '\n');
    const input = event === 'SubagentStop' ? { hook_event_name: 'SubagentStop', session_id: 's1', cwd: elsewhere, agent_id: id, agent_type: 'general-purpose', agent_transcript_path: tp, stop_hook_active: false, last_assistant_message: 'done' }
      : { hook_event_name: 'PreToolUse', tool_name: 'SubagentHandback', session_id: 's1', cwd: elsewhere, agent_id: id, agent_type: 'general-purpose', agent_transcript_path: tp, transcript_path: tp, tool_input: { message: 'report' } };
    const r = spawnSync(process.execPath, [path.join(root, 'tools', 'hooks', 'handin_gate.mjs')], { input: JSON.stringify(input), cwd: elsewhere, encoding: 'utf8', timeout: 120000, env });
    return { code: r.status, out: r.stdout, err: r.stderr };
  };
  const J = (s) => { try { return JSON.parse(s); } catch { return {}; } };
  const img = path.join(hd, 'shot.png'); fs.writeFileSync(img, Buffer.from('89504e470d0a1a0a', 'hex'));
  const cpFile = path.join(hd, 'CHECKPOINT.md');
  const check = (...a) => spawnSync(process.execPath, [path.join(root, 'tools', 'check_handin.mjs'), hd, '--main', root, ...a], { cwd: elsewhere, encoding: 'utf8', env });

  // check_handin --checkpoint
  let c = check('--checkpoint'); t('E2 --checkpoint with no CHECKPOINT.md: fails and says so', c.status === 1 && /no CHECKPOINT\.md/.test(c.stderr), c.stderr);
  fs.writeFileSync(cpFile, 'Just a note without the header line\n'); c = check('--checkpoint'); t('E2 --checkpoint first line not in the form: fails with the form', c.status === 1 && /first line must be: CHECKPOINT:/.test(c.stderr), c.stderr);
  fs.writeFileSync(cpFile, 'CHECKPOINT: lane widths measured on four photos | images: \n'); c = check('--checkpoint'); t('E2 --checkpoint with no image path: fails', c.status === 1, c.stderr);
  fs.writeFileSync(cpFile, 'CHECKPOINT: lane widths measured on four photos | images: notes.txt\n'); c = check('--checkpoint'); t('E2 --checkpoint naming a non-image: fails', c.status === 1 && /names no image path/.test(c.stderr), c.stderr);
  fs.writeFileSync(cpFile, `CHECKPOINT: lane widths measured on four photos | images: ${path.join(hd, 'missing.png')}\n`); c = check('--checkpoint'); t('E2 --checkpoint naming an image that does not exist: fails and names it', c.status === 1 && /missing\.png/.test(c.stderr) && /does not exist|none of the named images/.test(c.stderr), c.stderr);
  fs.writeFileSync(cpFile, `CHECKPOINT: x | images: ${img}\n`); c = check('--checkpoint'); t('E2 --checkpoint with a placeholder "what is done": fails', c.status === 1 && /placeholder|empty/.test(c.stderr), c.stderr);
  fs.writeFileSync(cpFile, `CHECKPOINT: lane widths measured on four photos | images: ${img}\n\nmore text\n`); c = check('--checkpoint'); t('E2 --checkpoint, valid file with an existing image (absolute path): passes and says it is not a hand-in', c.status === 0 && /CHECKPOINT PASSED/.test(c.stdout) && /not a hand-in/.test(c.stdout), c.stdout + c.stderr);
  fs.writeFileSync(cpFile, 'CHECKPOINT: lane widths measured on four photos | images: shot.png\n'); c = check('--checkpoint'); t('E2 --checkpoint, image named relative to the hand-in folder: passes', c.status === 0, c.stderr);
  fs.writeFileSync(cpFile, `CHECKPOINT: lane widths measured on four photos | images: ${path.join(hd, 'nope.png')}, ${img}\n`); c = check('--checkpoint'); t('E2 --checkpoint, one of two images exists: passes (at least one)', c.status === 0, c.stderr);
  c = check(); t('E2 the FULL check of the same folder still fails (a checkpoint is not a hand-in)', c.status === 1 && /STATUS\.md is missing/.test(c.stderr), c.stderr);

  // the finish hook, SubagentStop and PreToolUse(SubagentHandback), the order of one real finish
  fs.rmSync(cpFile, { force: true });
  const tpA = mkTr('tpA', hd);
  let r = stopHook('cpA', tpA, 'PreToolUse'); t('E2 hook: no CHECKPOINT.md, no hand-in: refused as before (exit 2, STATUS.md missing)', r.code === 2 && /STATUS\.md is missing/.test(r.err), r.err + r.out);
  fs.writeFileSync(cpFile, 'CHECKPOINT: lane widths measured | images: ' + img + '\n');
  r = stopHook('cpB', mkTr('tpB', hd), 'PreToolUse'); t('E2 hook, first call of a finish (PreToolUse SubagentHandback) with a valid CHECKPOINT.md: allowed (exit 0)', r.code === 0 && r.err === '', r.err + r.out);
  t('E2 ... logged gate=handin decision=checkpoint', lastHandin().decision === 'checkpoint', JSON.stringify(lastHandin()));
  t('E2 ... not counted as a hand-in: no builder record, no refusal counter, no HANDIN_FAILED.txt', !fs.existsSync(path.join(state, 'builders')) || fs.readdirSync(path.join(state, 'builders')).length === 0, '') ;
  t('E2 ... no refusal counter file for the agent', !fs.existsSync(path.join(state, 'handin', 'cpB.json')) && !fs.existsSync(path.join(hd, 'HANDIN_FAILED.txt')));
  r = stopHook('cpB', mkTr('tpB', hd), 'SubagentStop'); t('E2 hook, second call of the SAME finish (SubagentStop, within the window, same file): allowed again, logged as the same finish', r.code === 0 && r.out === '' && /second hook call of the same finish/.test(lastHandin().reason || ''), r.out + r.err + JSON.stringify(lastHandin()));
  // once per agent: a later stop, after the window or with a newer file, is a hand-in and gets the full check
  const recFile = path.join(state, 'handin', 'cpB.checkpoint.json'); const rec = J(fs.readFileSync(recFile, 'utf8'));
  fs.writeFileSync(recFile, JSON.stringify({ ...rec, at: Date.now() - 10 * 60000 }));
  r = stopHook('cpB', mkTr('tpB', hd), 'SubagentStop'); t('E2 hook, a LATER stop of the same agent (outside the window): the checkpoint is used up, the full check runs: blocked, and the message says why', J(r.out).decision === 'block' && /already used|once per agent/.test(J(r.out).reason) && /STATUS\.md is missing/.test(J(r.out).reason), r.out + r.err);
  // a different agent with an INVALID checkpoint: not let through, and told why
  fs.writeFileSync(cpFile, 'CHECKPOINT: lane widths measured | images: ' + path.join(hd, 'ghost.png') + '\n');
  r = stopHook('cpC', mkTr('tpC', hd), 'SubagentStop'); t('E2 hook, invalid CHECKPOINT.md (the image does not exist): not let through, the reason names the checkpoint', J(r.out).decision === 'block' && /CHECKPOINT\.md did not count/.test(J(r.out).reason) && /ghost\.png/.test(J(r.out).reason), r.out + r.err);
  // a stale CHECKPOINT.md from an earlier agent run (written before this agent started) does not count
  fs.writeFileSync(cpFile, 'CHECKPOINT: lane widths measured | images: ' + img + '\n'); const old = new Date(Date.now() - 3600000); fs.utimesSync(cpFile, old, old);
  r = stopHook('cpD', mkTr('tpD', hd, 60000), 'SubagentStop'); t('E2 hook, a stale CHECKPOINT.md (older than this agent\'s run): does not count', J(r.out).decision === 'block' && /older than this agent/.test(J(r.out).reason), r.out + r.err);
  // the final hand-in is still checked in full: a passing hand-in in the same folder is allowed and recorded as a hand-in
  fs.writeFileSync(path.join(hd, 'STATUS.md'), '## Skipped, refused or failed\n\nNothing skipped; did not verify the live editor.\n\nReferences opened: 3 of 3 listed\n\n## Reference files opened\n\nCase: A | Opened: aerial/a.jpg, street/b.jpg, gsv/c.jpg\n');
  for (const f of ['aerial/a.jpg', 'street/b.jpg', 'gsv/c.jpg']) { const p = path.join(root, 'docs', 'reference', f); mk(path.dirname(p)); fs.writeFileSync(p, 'photo ' + f); }
  fs.rmSync(cpFile, { force: true });
  r = stopHook('cpE', mkTr('tpE', hd), 'SubagentStop'); t('E2 hook, the final hand-in (no CHECKPOINT.md) with a passing STATUS.md: allowed as a hand-in ("hand-in check passed")', r.code === 0 && /hand-in check passed/.test(lastHandin().reason || ''), r.out + r.err + JSON.stringify(lastHandin()));
  // a checkpoint file does not shortcut a hand-in that is complete either: the first stop of a new agent with BOTH is a checkpoint (logged so), not a hand-in
  fs.writeFileSync(cpFile, 'CHECKPOINT: lane widths measured | images: ' + img + '\n');
  r = stopHook('cpF', mkTr('tpF', hd), 'SubagentStop'); t('E2 hook, CHECKPOINT.md present and a complete STATUS.md: the stop is logged as a checkpoint, not as a passed hand-in', r.code === 0 && lastHandin().decision === 'checkpoint', JSON.stringify(lastHandin()));
  // fail-open stays: check_handin.mjs gone mid-checkpoint
  const wtF = path.join(tmp, 'wt-e2f'); const hdF = path.join(wtF, 'docs', 'progress', 'cpf'); mk(hdF); fs.writeFileSync(path.join(hdF, 'CHECKPOINT.md'), 'CHECKPOINT: lane widths measured | images: ' + img + '\n');
  fs.renameSync(path.join(root, 'tools', 'check_handin.mjs'), path.join(root, 'tools', 'check_handin.bak'));
  r = stopHook('cpG', mkTr('tpG', hdF), 'SubagentStop'); fs.renameSync(path.join(root, 'tools', 'check_handin.bak'), path.join(root, 'tools', 'check_handin.mjs'));
  t('E2 hook, check_handin.mjs missing: a checkpoint is not granted (fails open on the full check, logged), never a crash', r.code === 0 && /FAIL-OPEN/.test(lastHandin().reason || ''), r.out + r.err + JSON.stringify(lastHandin()));
}

// ---------------------------------------------------------------- merge checklist step 6 script
console.log('gates8 step 6: tools/clean_test_temp.mjs');
{
  const fake = path.join(tmp, 'faketemp'); mk(fake);
  const dirs = ['gatetest-aaa', 'hooktest-bbb', 'guards6-ccc', 'guards7-ddd', 'guards8-eee', 'bkref-fff', 'wttools-ggg'].map((d) => path.join(fake, d)); const keep = path.join(fake, 'someone-elses-folder');
  for (const d of [...dirs, keep]) { mk(path.join(d, 'sub')); fs.writeFileSync(path.join(d, 'sub', 'f.txt'), 'x'); }
  const ph = path.join(tmp, 'photos2'); mk(ph); fs.writeFileSync(path.join(ph, 'precious.jpg'), 'precious');
  fs.symlinkSync(ph, path.join(dirs[1], 'sub', 'ln'), 'junction');   // a junction inside a leftover folder: it must be unlinked, never followed
  const run = (...a) => spawnSync(process.execPath, [path.join(src, 'tools', 'clean_test_temp.mjs'), ...a], { encoding: 'utf8', env: { ...process.env, TEMP: fake, TMP: fake } });
  let r = run('--dry-run', '--dir', fake); t('step 6 --dry-run lists 7 folders and removes none', r.status === 0 && /7 leftover/.test(r.stdout) && dirs.every((d) => fs.existsSync(d)), r.stdout + r.stderr);
  r = run('--dir', fake); t('step 6 removes the 7 test folders, leaves the other folder, exit 0', r.status === 0 && dirs.every((d) => !fs.existsSync(d)) && fs.existsSync(keep), r.stdout + r.stderr);
  t('step 6 the photos behind the junction survived (the link was unlinked, not followed)', fs.readFileSync(path.join(ph, 'precious.jpg'), 'utf8') === 'precious');
}

console.log('test isolation and cleanup');
t('no line mentioning this run\'s temp folder reached the real gate.log', isolation.leaks().length === 0, isolation.leaks().slice(0, 3).join('\n'));
t('the photos behind the junctions and in the fixture main survived every case', fs.readFileSync(path.join(photos, 'k.jpg'), 'utf8') === 'k' && fs.readFileSync(path.join(main, 'docs', 'reference', 'aerial', 'p1.jpg'), 'utf8') === 'photo');
const gone = cleanupTmp(tmp);
t(`the temp folder (junctions unlinked first) is removed${process.env.KEEP_HOOKTEST ? ' [KEEP_HOOKTEST set: kept]' : ''}`, gone || !!process.env.KEEP_HOOKTEST, tmp);
console.log(`\n${pass} passed, ${fail} failed`);
if (failedNames.length) console.log('FAILED TESTS:\n - ' + failedNames.join('\n - '));
process.exit(fail ? 1 : 0);
