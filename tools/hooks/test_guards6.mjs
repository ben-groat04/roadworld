#!/usr/bin/env node
// gates6: real-payload tables for the delete guard and the state guard, one named case per item of the gates5 AUDIT.md (docs/progress/gates5/AUDIT.md).
// Where the expected values come from: the gates5 audit (items 1-23: "refused although legitimate" = must be allowed, "holes" = must be refused), the main session decisions of
// 23 Oct 2026 (structural protection + validated backup; remaining forms listed in FAULTS Residual are accepted), and CLAUDE.md line 65 (the prescribed backup is robocopy main
// docs\reference -> C:\Users\bgbg0\roadworld-reference-backup /MIR). Nothing here pins a fault.
// The guards are imported (analyse, judge); the fixture is a temp "main checkout" with a registered clean worktree, a registered worktree holding a REAL junction at docs\reference,
// and photos behind that junction. No test writes the live gate.log (CP_GATE_STATE_DIR, checked at the end). Usage: node tools/hooks/test_guards6.mjs
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { fileURLToPath, pathToFileURL } from 'node:url';
import { isolateState, cleanupTmp } from '../test_isolation.mjs';

const src = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..', '..');
const tmp = fs.mkdtempSync(path.join(os.tmpdir(), 'guards6-'));
const isolation = isolateState(tmp, src);
let pass = 0, fail = 0; const failedNames = [];
const t = (name, cond, detail = '') => { if (cond) { pass++; console.log('  ok   ' + name); } else { fail++; failedNames.push(name); console.log('  FAIL ' + name + (detail ? '\n       ' + String(detail).replace(/\n/g, '\n       ') : '')); } };

const main = path.join(tmp, 'roadworld'); const M = main.replace(/\\/g, '/');
const mk = (p) => fs.mkdirSync(p, { recursive: true });
mk(path.join(main, '.git', 'worktrees', 'w1')); mk(path.join(main, '.git', 'worktrees', 'j1'));
mk(path.join(main, 'docs', 'reference', 'aerial')); mk(path.join(main, 'docs', 'reference', '_compare', 'mytopic')); mk(path.join(main, 'docs', 'reference', '_damaged')); mk(path.join(main, '.claude', 'state', 'handin', 'r1'));
for (const n of ['p1.jpg', 'p2.jpg', 'p3.jpg']) fs.writeFileSync(path.join(main, 'docs', 'reference', 'aerial', n), 'photo');
fs.writeFileSync(path.join(main, 'docs', 'reference', '_compare', 'mytopic', 'x.png'), 'c');
fs.writeFileSync(path.join(main, '.claude', 'state', 'ack_secret'), 'secret'); fs.writeFileSync(path.join(main, '.claude', 'state', 'gate.log'), '{}\n'); fs.writeFileSync(path.join(main, '.claude', 'state', 'handin', 'r1', 'HANDIN_FAILED.txt'), 'x');
mk(path.join(main, 'tools', 'hooks')); fs.writeFileSync(path.join(main, 'tools', 'hooks', 'gate_ack.mjs'), '//'); fs.writeFileSync(path.join(main, '.claude', 'settings.json'), '{}');
const wt = path.join(tmp, 'roadworld-wt', 'w1'); const wtJ = path.join(tmp, 'roadworld-wt', 'j1');
for (const [w, n] of [[wt, 'w1'], [wtJ, 'j1']]) {
  mk(path.join(w, 'build', 'sub')); mk(path.join(w, 'docs')); mk(path.join(w, 'unreal', 'CityPainter', 'Intermediate')); fs.writeFileSync(path.join(w, 'build', 'a.obj'), 'o'); fs.writeFileSync(path.join(w, 'build', 'a.log'), 'o');
  fs.writeFileSync(path.join(w, '.git'), 'gitdir: ' + path.join(main, '.git', 'worktrees', n) + '\n');
  fs.writeFileSync(path.join(main, '.git', 'worktrees', n, 'gitdir'), path.join(w, '.git') + '\n'); fs.writeFileSync(path.join(main, '.git', 'worktrees', n, 'commondir'), '../..\n');
}
const photos = path.join(tmp, 'photos'); mk(photos); fs.writeFileSync(path.join(photos, 'k.jpg'), 'k');
fs.symlinkSync(photos, path.join(wtJ, 'docs', 'reference'), 'junction');
const elsewhere = path.join(tmp, 'elsewhere'); mk(elsewhere);
mk(path.join(tmp, 'tempdir')); process.env.TEMP = path.join(tmp, 'tempdir'); process.env.TMP = process.env.TEMP; delete process.env.TMPDIR;
const BK = 'C:\\Users\\bgbg0\\roadworld-reference-backup';

const dgm = await import(pathToFileURL(path.join(src, 'tools/hooks/delete_guard_impl.mjs')).href);
const sgm = await import(pathToFileURL(path.join(src, 'tools/hooks/state_guard_impl.mjs')).href);
const dg = (command, cwd) => dgm.analyse(command, cwd, 0, null, main);

const cwdOf = { wt, wtJ, main, elsewhere };
function table(title, cases) {
  const bad = [];
  for (const [command, cw, want, label] of cases) {
    let got; try { got = dg(command, cwdOf[cw]).length ? 2 : 0; } catch (e) { got = 'THROW ' + e.message; }
    if (got !== want) bad.push(`${label}: ${command.slice(0, 110)} (cwd ${cw}): ${got}, wanted ${want}`);
  }
  t(`${title}: ${cases.length} cases judged as the audit says`, bad.length === 0, bad.join('\n'));
}

console.log('gates6 delete guard: false blocks (gates5 audit items 1-8) must be ALLOWED');
const TEMPV = process.env.TEMP || os.tmpdir();
table('item 1 variables for temp and build folders', [
  ['rm -rf "$TEMP/x"', 'wt', 0, 'item 1 $TEMP'], ['rm -rf $TMP/gatetest-*', 'wt', 0, 'item 1 $TMP glob'], ['rm -rf ${TMPDIR:-/tmp}/x', 'wt', 0, 'item 1 ${TMPDIR:-/tmp}'],
  ['rm -rf "$BUILD"', 'wt', 0, 'item 1 unknown $BUILD in a clean worktree'], ['rm -rf $(pwd)/build', 'wt', 0, 'item 1 $(pwd)'], ['rm -rf ${PWD}/build', 'wt', 0, 'item 1 ${PWD}'],
  ['rm -rf "$(mktemp -d)"', 'wt', 0, 'item 1 mktemp'], ['rm -rf $PWD/build', 'wt', 0, 'item 1 $PWD'], ['BUILD=build; rm -rf "$BUILD"', 'wt', 0, 'item 1 assigned variable'],
  ['Remove-Item -Recurse -Force "$PWD\\build"', 'wt', 0, 'item 1 "$PWD\\build"'], ['Remove-Item -Recurse -Force "$($PWD.Path)\\build"', 'wt', 0, 'item 1 $($PWD.Path)'],
  ['Remove-Item -Recurse -Force "$PSScriptRoot\\build"', 'wt', 0, 'item 1 $PSScriptRoot'], ['$tmp = Join-Path $env:TEMP "x"; Remove-Item -Recurse -Force $tmp', 'wt', 0, 'item 1 Join-Path $env:TEMP'],
  ['$b = Join-Path $PWD "build"; Remove-Item -Recurse -Force $b', 'wt', 0, 'item 1 Join-Path $PWD'], ['Remove-Item -Recurse -Force "$env:TEMP\\gatetest-1"', 'wt', 0, 'item 1 $env:TEMP'],
  ['rm -rf build', 'wtJ', 0, 'item 1 build in a tree with a link elsewhere (the walk finds the link only below the target)'],
]);
table('item 2 pipeline Remove-Item on a build listing', [
  ['Get-ChildItem build -Recurse -Filter *.obj | Remove-Item', 'wt', 0, 'item 2 gci build -Recurse | Remove-Item'], ['Get-ChildItem build | Remove-Item -Recurse -Force', 'wt', 0, 'item 2 gci build | Remove-Item -Recurse'],
  ['Get-ChildItem $env:TEMP -Filter "gatetest-*" | Remove-Item -Recurse -Force', 'wt', 0, 'item 2 gci $env:TEMP'], ['ls build | xargs rm -rf', 'wt', 0, 'item 2 ls | xargs rm -rf'],
]);
table('item 3 find -delete with a name filter from the worktree root', [
  ['find . -name "*.log" -delete', 'wt', 0, 'item 3 *.log'], ['find . -name "*.obj" -delete', 'wt', 0, 'item 3 *.obj'], ['find build -name "*.o" -delete', 'wt', 0, 'item 3 build'],
  ['find . -name "*.log" -delete', 'main', 0, 'item 3 filter that cannot match a picture, in main'],
]);
table('item 5 git clean with a path argument', [
  ['git clean -fd build', 'wt', 0, 'item 5 git clean -fd build'], ['git clean -fdx unreal/CityPainter/Intermediate', 'wt', 0, 'item 5 git clean -fdx unreal/...Intermediate'],
]);
table('item 6 words in shell comments', [
  ['rm -rf build # docs/reference', 'wt', 0, 'item 6 comment names the folder'], ['rm -rf build   # never touch docs/reference or roadworld-reference-backup', 'wt', 0, 'item 6 long comment'],
  ['Remove-Item -Recurse -Force build # docs\\reference', 'wt', 0, 'item 6 PowerShell comment'], ['echo hi # rm -rf docs', 'main', 0, 'item 6 comment holds a whole delete'],
  ['if [ -d build ]; then rm -rf build; fi', 'wt', 0, 'item 11 the common if/then idiom'], ['for d in build; do rm -rf "$d"; done', 'wt', 0, 'item 11 for loop over build'],
]);
table('item 7 own composites under docs/reference/_compare/<topic>', [
  [`rm -rf ${M}/docs/reference/_compare/mytopic`, 'main', 0, 'item 7 absolute'], ['rm -rf docs/reference/_compare/mytopic', 'main', 0, 'item 7 relative'],
  ['Remove-Item -Recurse -Force docs\\reference\\_compare\\mytopic', 'main', 0, 'item 7 PowerShell'], [`rm -rf ${M}/docs/reference/_compare/mytopic/x.png`, 'main', 0, 'item 7 file below the topic'],
  [`rm -rf ${M}/docs/reference/_compare`, 'main', 2, 'item 7 the _compare folder itself stays refused'], [`rm -rf ${M}/docs/reference/_compare/*`, 'main', 2, 'item 7 wildcard under _compare (other agents\' topics)'],
  [`rm -rf ${M}/docs/reference/aerial`, 'main', 2, 'item 7 the photos themselves stay refused'],
]);
{ // item 8: the walk
  const big = path.join(wt, 'bigbuild'); mk(big);
  for (let d = 0; d < 24; d++) { const dd = path.join(big, 'd' + d); mk(dd); for (let f = 0; f < 1000; f++) fs.writeFileSync(path.join(dd, 'f' + f + '.o'), ''); }
  const t0 = Date.now(); const r = dg('rm -rf bigbuild', wt); const ms = Date.now() - t0;
  t(`item 8: 24,000 files are walked in ${ms} ms and the delete is allowed`, r.length === 0 && ms < 4000, r.join('; '));
  const lb = dgm.linksBelow(big, { ms: -1 });
  t('item 8: when the time cap ends the walk it says so (capped, entries seen), never passes silently', lb.capped === true && lb.seen > 0, JSON.stringify({ c: lb.capped, s: lb.seen }));
  const ctx = dgm.context(main); const orig = dgm.linksBelow;
  void orig;
  const cl = ctx.classify(path.join(wt, 'bigbuild'));
  t('item 8: the classifier on a fully walked folder with no link returns allowed', cl === null, String(cl));
  const lb2 = dgm.linksBelow(path.join(wtJ, 'docs'));
  t('item 8: a junction is found from the directory entries alone (no realpath per directory)', lb2.length === 1 && /reference$/.test(lb2[0]), JSON.stringify(lb2));
}

console.log('gates6 delete guard: holes (gates5 audit items 9-18) must be REFUSED');
const R = `${M}/docs/reference`;
table('item 9 prefixes', [
  [`\\rm -rf ${M}/docs/reference`, 'elsewhere', 2, 'item 9 \\rm'], ['timeout 5 rm -rf docs', 'main', 2, 'item 9 timeout N'], ['busybox rm -rf docs', 'main', 2, 'item 9 busybox'], ['command rm -rf docs', 'main', 2, 'item 9 command'],
  ['builtin rm -rf docs', 'main', 2, 'item 9 builtin'], ['exec rm -rf docs', 'main', 2, 'item 9 exec'], ['nohup rm -rf docs', 'main', 2, 'item 9 nohup'], ['sudo rm -rf docs', 'main', 2, 'item 9 sudo'], ['nice -n 5 rm -rf docs', 'main', 2, 'item 9 nice -n'],
  ['time rm -rf docs', 'main', 2, 'item 9 time'], ['env FOO=1 rm -rf docs', 'main', 2, 'item 9 env'], ['timeout -k 2 10 rm -rf docs', 'main', 2, 'item 9 timeout -k'],
]);
table('item 10 nested commands', [
  [`eval "rm -rf ${M}/docs"`, 'elsewhere', 2, 'item 10 eval'], [`echo "rm -rf ${M}/docs" | bash`, 'elsewhere', 2, 'item 10 echo | bash'], [`bash <<< "rm -rf ${M}/docs"`, 'elsewhere', 2, 'item 10 bash <<<'],
  [`bash -c $'rm -rf ${M}/docs'`, 'elsewhere', 2, "item 10 bash -c $'...'"], [`cmd //c "rmdir /s /q ${M.replace(/\//g, '\\')}\\docs"`, 'elsewhere', 2, 'item 10 cmd //c rmdir /s'], [`cmd //c rmdir //s //q ${M}/docs`, 'elsewhere', 2, 'item 10 cmd //c rmdir //s //q'],
  [`cmd /c rd /s /q ${M.replace(/\//g, '\\')}\\docs\\reference`, 'elsewhere', 2, 'item 10 cmd /c rd /s'], [`cat <<EOF | bash\nrm -rf ${M}/docs\nEOF`, 'elsewhere', 2, 'item 10 heredoc into bash'],
  [`bash <<EOF\nrm -rf ${M}/docs\nEOF`, 'elsewhere', 2, 'item 10 bash heredoc'], ['cmd //c "rmdir /s /q build"', 'wt', 0, 'item 10 cmd //c on build stays allowed'],
  [`printf 'rm -rf ${M}/docs' | sh`, 'elsewhere', 2, 'item 10 printf | sh'], ['echo "rm -rf docs" > notes.txt', 'main', 0, 'item 10 echo into a file is text'],
]);
table('item 11 bash control flow', [
  ['if true; then rm -rf docs; fi', 'main', 2, 'item 11 if'], ['while true; do rm -rf docs; break; done', 'main', 2, 'item 11 while'], ['for d in docs; do rm -rf $d; done', 'main', 2, 'item 11 for with list'],
  [`for f in ${M}/docs/reference/*; do rm -rf "$f"; done`, 'elsewhere', 2, 'item 11 for over the photos (recursive)'], ['while read f; do rm -rf $f; done < list.txt', 'main', 2, 'item 11 while read in main'],
  [`for f in ${R}/aerial/*; do rm "$f"; done`, 'elsewhere', 2, 'item 16 for ... rm "$f" over the photos'], ['if [ -d build ]; then rm -rf build; else echo no; fi', 'wt', 0, 'item 11 if/else/fi in a clean worktree'],
]);
table('item 12 brace, class, trailing dot, escape', [
  [`rm -rf ${M}/docs/refer[e]nce`, 'elsewhere', 2, 'item 12 [e]'], [`rm -rf ${M}/docs/{reference,x}`, 'elsewhere', 2, 'item 12 {reference,x}'], ['rm -rf [d]ocs', 'main', 2, 'item 12 [d]ocs'], ['rm -rf {docs,build}', 'main', 2, 'item 12 {docs,build}'],
  [`rm -rf ${M}/docs/reference.`, 'elsewhere', 2, 'item 12 trailing dot'], [`rm -rf ${M}/docs/ref\\erence`, 'elsewhere', 2, 'item 12 backslash escape'], ['rm -rf docs.', 'main', 2, 'item 12 docs.'],
  [`rm -rf '\\\\?\\${M.replace(/\//g, '\\')}\\docs'`, 'elsewhere', 2, 'item 12 \\\\?\\ prefix'], ['rm -rf ref*', 'main', 0, 'item 12 a glob that matches nothing here deletes nothing'],
]);
table('item 13 assigned substitutions', [
  ['d=$(echo docs); rm -rf "$d"', 'main', 2, 'item 13 d=$(echo docs)'], ['d=`echo docs`; rm -rf $d', 'main', 2, 'item 13 backticks'], ['d=$(pwd)/docs; rm -rf "$d"', 'main', 2, 'item 13 $(pwd)/docs'],
  ['$d = [System.IO.Path]::GetFullPath("docs"); Remove-Item $d -Recurse -Force', 'main', 2, 'item 13 PowerShell GetFullPath'], ['$d = (Resolve-Path docs); Remove-Item $d -Recurse -Force', 'main', 2, 'item 13 (Resolve-Path)'],
  ['d=$(echo docs); rm -rf "$d"', 'wtJ', 2, 'item 13 same in a tree with a junction'],
]);
table('item 14 PowerShell wrappers', [
  ['Invoke-Expression "Remove-Item docs -Recurse -Force"', 'main', 2, 'item 14 Invoke-Expression'], ['iex "Remove-Item docs -Recurse -Force"', 'main', 2, 'item 14 iex'],
  [`powershell -NoProfile -EncodedCommand ${Buffer.from('Remove-Item docs -Recurse -Force', 'utf16le').toString('base64')}`, 'main', 2, 'item 14 -EncodedCommand'],
  [`Start-Process cmd -ArgumentList "/c rd /s /q ${M.replace(/\//g, '\\')}\\docs\\reference"`, 'elsewhere', 2, 'item 14 Start-Process cmd'], ['Start-Process powershell -ArgumentList "-c Remove-Item docs -Recurse -Force"', 'main', 2, 'item 14 Start-Process powershell'],
  ['Push-Location docs; Remove-Item reference -Recurse -Force; Pop-Location', 'main', 2, 'item 14 Push-Location'], ['Remove-Item -Recurse -Force -Path (Resolve-Path docs)', 'main', 2, 'item 14 -Path (Resolve-Path)'],
  ['Remove-Item -Recurse -Force @("docs")', 'main', 2, 'item 14 @()'], ['Remove-Item (Join-Path $PWD "docs") -Recurse -Force', 'main', 2, 'item 14 (Join-Path)'], ['Remove-Item (Get-Item docs) -Recurse -Force', 'main', 2, 'item 14 (Get-Item)'],
  ['[IO.Directory]::Delete((Resolve-Path docs), $true)', 'main', 2, 'item 14 [IO.Directory]::Delete((...))'], ['(Get-Item docs).Delete($true)', 'main', 2, 'item 14 .Delete($true)'],
  ['foreach ($d in Get-ChildItem) { Remove-Item $d.FullName -Recurse -Force }', 'main', 2, 'item 14 foreach over Get-ChildItem'], ['Remove-Item -Recurse -Force -Path (Resolve-Path docs)', 'wtJ', 2, 'item 14 same, tree with a junction'],
  ['Remove-Item -Recurse -Force -Path .\\build', 'wt', 0, 'item 14 -Path .\\build stays allowed'],
]);
table('item 15 the worktree script is judged by the actual invocation', [
  [`git worktree remove ${wt} # remove_agent_worktree`, 'main', 2, 'item 15 comment substring'], [`echo remove_agent_worktree; git worktree remove --force ${wt}`, 'main', 2, 'item 15 echo then remove'],
  ['powershell -NoProfile -ExecutionPolicy Bypass -File tools\\remove_agent_worktree.ps1 foo', 'main', 0, 'item 15 the real script invocation'], ['git worktree list', 'main', 0, 'item 15 worktree list'],
  ['git worktree add ../roadworld-wt/x -b agent/x', 'main', 0, 'item 15 worktree add'],
]);
table('item 16 non-recursive mass deletes, moves and truncation of the photos', [
  [`rm -f ${R}/aerial/*.jpg`, 'elsewhere', 2, 'item 16 rm -f *.jpg'], [`Remove-Item ${R.replace(/\//g, '\\')}\\aerial\\*.jpg -Force`, 'elsewhere', 2, 'item 16 Remove-Item *.jpg'],
  [`cd ${R}/aerial && rm -f *.jpg`, 'elsewhere', 2, 'item 16 cd && rm -f *.jpg'], [`del ${R.replace(/\//g, '\\')}\\aerial\\*`, 'elsewhere', 2, 'item 16 del aerial\\*'],
  [`Get-ChildItem ${R.replace(/\//g, '\\')} -Recurse -File | ForEach-Object { Remove-Item $_.FullName -Force }`, 'elsewhere', 2, 'item 16 gci | ForEach Remove-Item'],
  [`Get-ChildItem ${R.replace(/\//g, '\\')} -Recurse -File | ForEach-Object { $_.Delete() }`, 'elsewhere', 2, 'item 16 gci | .Delete()'], [`Get-ChildItem ${R.replace(/\//g, '\\')} -Recurse -File | ForEach-Object { [IO.File]::Delete($_.FullName) }`, 'elsewhere', 2, 'item 16 gci | [IO.File]::Delete'],
  [`mv ${R} /tmp/x`, 'elsewhere', 2, 'item 16 mv docs/reference'], [`mv ${M}/docs ${M}/docs_old`, 'elsewhere', 2, 'item 16 mv docs'], [`Move-Item ${R.replace(/\//g, '\\')} ${tmp}\\x`, 'elsewhere', 2, 'item 16 Move-Item'],
  [`Rename-Item ${R.replace(/\//g, '\\')} ref_old`, 'elsewhere', 2, 'item 16 Rename-Item'], [`mv ${R}/aerial /tmp/x`, 'elsewhere', 2, 'item 16 mv a photo subfolder'], [`mv ${wt} ${wt}_old`, 'elsewhere', 2, 'item 16 mv a worktree root'],
  [`mv ${BK.replace(/\\/g, '/')} /tmp/x`, 'elsewhere', 2, 'item 16 mv the backup'], [`rmdir ${R}`, 'elsewhere', 2, 'item 16 rmdir docs/reference'],
  [`> ${R}/aerial/p1.jpg`, 'elsewhere', 2, 'item 16 truncating redirect'], [`truncate -s 0 ${R}/aerial/p1.jpg`, 'elsewhere', 2, 'item 16 truncate'], [`Set-Content ${R.replace(/\//g, '\\')}\\aerial\\p1.jpg ""`, 'elsewhere', 2, 'item 16 Set-Content'],
  [`Clear-Content ${R.replace(/\//g, '\\')}\\aerial\\p1.jpg`, 'elsewhere', 2, 'item 16 Clear-Content'], [`cp /tmp/empty ${R}/aerial/p1.jpg`, 'elsewhere', 2, 'item 16 cp onto a photo'], [`Copy-Item C:\\x\\empty ${R.replace(/\//g, '\\')}\\aerial\\p1.jpg -Force`, 'elsewhere', 2, 'item 16 Copy-Item -Force'],
  [`mv ${R}/aerial/p2.jpg ${R}/_damaged/`, 'elsewhere', 0, 'item 16 one damaged photo to _damaged stays allowed'], [`mv ${R}/aerial/p*.jpg ${R}/_damaged/`, 'elsewhere', 0, 'item 16 a batch into _damaged stays allowed'],
  [`rm ${R}/aerial/p3.jpg`, 'elsewhere', 0, 'item 16 one named photo may be removed'], [`curl -o ${R}/aerial/new.jpg http://x/y.jpg`, 'elsewhere', 0, 'item 16 download of a new photo'], [`echo note >> ${R}/INDEX.md`, 'elsewhere', 0, 'item 16 appending to the index'],
  [`mkdir -p ${R}/_damaged && mv ${R}/aerial/p1.jpg ${R}/_damaged/`, 'elsewhere', 0, 'item 16 mkdir + mv into _damaged'], [`mv ${R}/_compare/mytopic ${tmp}/x`, 'elsewhere', 0, 'item 16 moving own composites'],
]);
table('item 17 robocopy', [
  [`robocopy ${M.replace(/\//g, '\\')}\\docs\\reference ${BK} /MIR /MOV`, 'elsewhere', 2, 'item 17 /MOV'], [`robocopy ${M.replace(/\//g, '\\')}\\docs\\reference ${BK} /MIR /MOVE`, 'elsewhere', 2, 'item 17 /MOVE'],
  [`robocopy ${M.replace(/\//g, '\\')}\\docs\\reference C:\\other /E /MOVE`, 'elsewhere', 2, 'item 17 /MOVE to anywhere'], [`robocopy ${M.replace(/\//g, '\\')}\\docs\\reference ${BK} /MIR /R:1 /W:1 /XD _compare /NFL`, 'elsewhere', 0, 'item 17 the prescribed backup stays allowed'],
  [`robocopy ${M.replace(/\//g, '\\')}\\docs\\reference C:\\Users\\bgbg0\\other /MIR`, 'elsewhere', 0, 'item 17 /MIR to an unguarded folder (harmless for the photos)'],
]);
table('item 18 other tools', [
  [`rsync -a --delete /tmp/empty/ ${R}/`, 'elsewhere', 2, 'item 18 rsync --delete'], [`python -c "import os; os.system('rm -rf ${M}/docs')"`, 'elsewhere', 2, 'item 18 python os.system'],
  [`node -e "require('child_process').execSync('rm -rf ${M}/docs')"`, 'elsewhere', 2, 'item 18 node execSync'], [`perl -e 'system("rm -rf ${M}/docs")'`, 'elsewhere', 2, 'item 18 perl system'],
  ['python -c "print(1)"', 'main', 0, 'item 18 harmless python'],
]);
table('refused in the junction worktree and allowed in the clean one (item 1 policy)', [
  ['rm -rf $x', 'wtJ', 2, 'unknown variable in a tree with a docs\\reference link'], ['rm -rf $x', 'main', 2, 'unknown variable in main'], ['rm -rf $x', 'wt', 0, 'unknown variable in a clean worktree'],
  ['rm -rf *', 'main', 2, 'rm -rf * in main'], ['rm -rf docs', 'wtJ', 2, 'docs of the junction worktree'], ['find . -name "*.log" -delete', 'wtJ', 2, 'item 3 find in a tree with a junction'],
  ['find . -name "*.jpg" -delete', 'main', 2, 'item 3 a filter that matches pictures, in main'], ['git clean -fd', 'wtJ', 2, 'git clean without a path in a junction tree'], ['git clean -fd docs', 'wtJ', 2, 'item 5 path argument that holds the link'],
  ['rm -rf docs/reference', 'wtJ', 2, 'the link itself'],
]);

console.log('gates6 state guard (gates5 audit items 4, 20-22)');
const sdir = (M + '/.claude/state'); const secret = sdir + '/ack_secret';
const common = (tool, ti, extra = {}) => ({ session_id: 's1', cwd: elsewhere, hook_event_name: 'PreToolUse', tool_name: tool, tool_input: ti, agent_id: 'ag6', agent_type: 'general-purpose', ...extra });
const sg = (tool, ti, extra) => { try { return sgm.judge(common(tool, ti, extra), main).length ? 2 : 0; } catch (e) { return 'THROW ' + e.message; } };
const B = (command) => ['Bash', { command }], P = (command) => ['PowerShell', { command }];
{
  const cases = [
    [...B(`ls ${sdir}`), 0, 'item 4 ls the state folder'], [...B(`ls -la ${sdir}/`), 0, 'item 4 ls -la'], [...B(`cd ${sdir} && ls`), 0, 'item 4 cd state && ls'], [...B('ls .claude/state/'), 0, 'item 4 ls relative'],
    [...P(`Get-ChildItem ${sdir.replace(/\//g, '\\')}`), 0, 'item 4 Get-ChildItem'], [...B(`dir ${sdir}`), 0, 'item 4 dir'], [...B(`find ${sdir} -maxdepth 1 -name "*.lock"`), 0, 'item 4 find names'],
    [...B(`test -f ${sdir}/unreal_lane.lock && echo yes`), 0, 'item 4 test -f lane lock'], [...B(`ls ${sdir}/handin`), 0, 'item 21 ls handin'], [...B(`cat ${sdir}/handin/r1/HANDIN_FAILED.txt`), 0, 'item 21 cat HANDIN_FAILED.txt'],
    ['Read', { file_path: sdir + '/handin/r1/HANDIN_FAILED.txt' }, 0, 'item 21 Read HANDIN_FAILED.txt'], [...B(`cat ${sdir}/handin/*.txt`), 0, 'item 21 handin wildcard'],
    [...B(`cat ${secret}`), 2, 'cat the secret still refused'], [...B(`cd ${sdir} && cat ack_secret`), 2, 'cd state && cat ack_secret'], [...B(`cd ${sdir} && cat *`), 2, 'cd state && cat *'],
    [...B(`rm ${sdir}/handin/r1/HANDIN_FAILED.txt`), 2, 'item 21 writes into handin stay refused'], [...B(`echo x > ${sdir}/handin/r1/HANDIN_FAILED.txt`), 2, 'item 21 redirect into handin refused'],
    [...B(`ls ${sdir} | xargs cat`), 2, 'ls state | xargs cat'], [...P(`Get-ChildItem ${sdir.replace(/\//g, '\\')} | Get-Content`), 2, 'Get-ChildItem state | Get-Content'],
    [...B(`grep -r secret ${M}/.claude`), 2, 'item 20 grep -r secret .claude'], [...B(`grep -r . ${M}`), 2, 'item 20 grep -r . main'], [...B(`grep -rn secret ${M}/.claude --exclude-dir=state`), 0, 'item 20 grep -r with --exclude-dir=state'],
    [...B(`grep -r "x" ${M}/docs`), 0, 'item 20 grep -r on docs only'], [...B(`rg --hidden --no-ignore . ${M}`), 2, 'item 20 rg --hidden --no-ignore'], [...B(`rg -uu secret ${M}/.claude`), 2, 'item 20 rg -uu'], [...B(`rg -n foo ${M}/tools`), 0, 'item 20 plain rg on tools'],
    [...B(`find ${M}/.claude -type f -exec cat {} +`), 2, 'item 20 find -exec cat'], [...B(`find ${M}/.claude -type f -name "*.json" -exec cat {} +`), 0, 'item 20 find -exec cat limited to *.json'],
    [...P(`Get-ChildItem -Recurse ${M.replace(/\//g, '\\')}\\.claude | Select-String secret`), 2, 'item 20 gci -Recurse | Select-String'], [...P(`Get-ChildItem -Recurse ${M.replace(/\//g, '\\')}\\.claude -Filter *.json | Select-String secret`), 0, 'item 20 gci -Recurse -Filter *.json | sls'],
    [...B(`find ${M} -name "*.md" | xargs grep foo`), 0, 'item 20 find *.md | xargs grep'], [...B(`find ${M} -type f | xargs cat`), 2, 'item 20 find . | xargs cat'],
    [...B(`cd ${M} && grep -r secret .`), 2, 'item 20 grep -r . from main'], [...B('node tools/hooks/gate_a*.mjs'), 2, 'item 22 wildcard gate_a*'], [...B('npx node tools/hooks/gate_ack.mjs "3:ab" "x"'), 2, 'item 22 npx node gate_ack'],
    [...B('cp tools/hooks/gate_ack.mjs /tmp/g.mjs && node /tmp/g.mjs'), 2, 'item 22 copy then run'], [...B('cp tools/hooks/audit_lib.mjs /tmp/a.mjs'), 2, 'item 22 copy audit_lib'], [...B('node tools/hooks/test_hooks.mjs'), 0, 'item 22 test_hooks may run'],
    [...B('node tools/hooks/test_guards6.mjs'), 0, 'item 22 test_guards6 may run'], [...B('cat tools/hooks/gate_ack.mjs'), 0, 'reading the source of gate_ack'],
    ['Write', { file_path: M + '/.claude/settings.json', content: '{}' }, 2, 'item 22 Write main settings.json'], ['Edit', { file_path: M + '/.claude/settings.local.json', old_string: 'a', new_string: 'b' }, 2, 'item 22 Edit main settings.local.json'],
    ['Write', { file_path: M + '/tools/hooks/delete_guard_impl.mjs', content: '' }, 2, 'item 22 Write main tools/hooks'], ['Edit', { file_path: M + '/tools/hooks/state_guard_impl.mjs', old_string: 'a', new_string: 'b' }, 2, 'item 22 Edit main tools/hooks'],
    ['Write', { file_path: wt + '/tools/hooks/delete_guard_impl.mjs', content: '' }, 0, 'item 22 Write the same file in a worktree stays allowed'], ['Write', { file_path: wt + '/.claude/settings.json', content: '{}' }, 0, 'item 22 worktree settings.json allowed'],
    ['Write', { file_path: M + '/docs/QUEUE.md', content: 'x' }, 0, 'item 22 other files in main'], [...B(`sed -i s/a/b/ ${M}/tools/hooks/state_guard_impl.mjs`), 2, 'item 22 sed -i main hook'], [...B(`echo x >> ${M}/.claude/settings.json`), 2, 'item 22 redirect into main settings'],
    ['Read', { file_path: M + '/.claude/settings.json' }, 0, 'item 22 reading settings is fine'],
  ];
  const bad = [];
  for (const [tool, ti, want, label] of cases) { const got = sg(tool, ti, tool === 'Bash' && ti.command.startsWith('cd ' + M) ? { cwd: main } : {}); if (got !== want) bad.push(`${label}: ${(ti.command || ti.file_path || '').slice(0, 100)}: ${got}, wanted ${want}`); }
  t(`state guard: ${cases.length} cases (gates5 audit items 4, 20, 21, 22) judged as the audit says`, bad.length === 0, bad.join('\n'));
  t('state guard: the main session (no agent_id) may write main settings.json and tools/hooks and read the secret', sgm.judge(common('Write', { file_path: M + '/.claude/settings.json', content: '{}' }, { agent_id: undefined })).length === 0 && sgm.judge(common('Bash', { command: `cat ${secret}` }, { agent_id: undefined }), main).length === 0);
  t('state guard: a subagent whose cwd IS the state folder cannot cat ack_secret', sgm.judge(common('Bash', { command: 'cat ack_secret' }, { cwd: path.join(main, '.claude', 'state') }), main).length > 0);
}

console.log('gates6: the two hooks as processes, with the harness payload shape (common fields + agent_id), exit codes and messages');
{
  const { spawnSync } = await import('node:child_process');
  const runHook = (script, input, cwd) => spawnSync(process.execPath, [path.join(src, 'tools', 'hooks', script)], { input: JSON.stringify(input), cwd, encoding: 'utf8', timeout: 60000, env: { ...process.env, CP_MAIN_ROOT: main } });
  const pay = (tool, ti, cwd) => ({ session_id: 's1', transcript_path: path.join(tmp, 'tr.jsonl'), cwd, hook_event_name: 'PreToolUse', tool_name: tool, tool_input: ti, tool_use_id: 'toolu_01', agent_id: 'ag6', agent_type: 'general-purpose' });
  let r = runHook('delete_guard.mjs', pay('Bash', { command: 'rm -rf build # docs/reference' }, wt), wt);
  t('process: delete_guard allows `rm -rf build # docs/reference` in a clean worktree (exit 0, no message)', r.status === 0 && r.stderr === '', r.stderr);
  r = runHook('delete_guard.mjs', pay('Bash', { command: `rm -rf ${M}/docs/reference` }, wt), wt);
  t('process: delete_guard refuses the photos (exit 2, message names the photos folder and the script)', r.status === 2 && /photos/.test(r.stderr) && /remove_agent_worktree/.test(r.stderr), r.stderr);
  r = runHook('state_guard.mjs', pay('Bash', { command: `ls ${sdir}` }, wt), wt);
  t('process: state_guard allows `ls .claude/state` by a subagent (exit 0)', r.status === 0, r.stderr);
  r = runHook('state_guard.mjs', pay('Bash', { command: `grep -r secret ${M}/.claude` }, wt), wt);
  t('process: state_guard refuses a recursive grep that reaches ack_secret (exit 2)', r.status === 2 && /ack|state/.test(r.stderr), r.stderr);
  r = runHook('state_guard.mjs', pay('Write', { file_path: M + '/.claude/settings.json', content: '{}' }, wt), wt);
  t('process: state_guard refuses a subagent Write to the main settings.json (exit 2); the message says the main session changes it', r.status === 2 && /main session/.test(r.stderr), r.stderr);
}

console.log('test isolation');
t('no line mentioning this run\'s temp folder reached the real gate.log', isolation.leaks().length === 0, isolation.leaks().slice(0, 3).join('\n'));
t('the photos behind the junction and in the fixture main survived every case', fs.readFileSync(path.join(photos, 'k.jpg'), 'utf8') === 'k' && fs.readFileSync(path.join(main, 'docs', 'reference', 'aerial', 'p1.jpg'), 'utf8') === 'photo');
const gone = cleanupTmp(tmp);   // gates7 item 13: links unlinked first, then the whole temp folder (state-isolated and tempdir included) is removed
t('the temp folder is removed at the end (junction unlinked first, nothing left in %TEMP%)', gone || !!process.env.KEEP_HOOKTEST, tmp);
console.log(`\n${pass} passed, ${fail} failed`);
if (failedNames.length) console.log('FAILED TESTS:\n - ' + failedNames.join('\n - '));
process.exit(fail ? 1 : 0);
