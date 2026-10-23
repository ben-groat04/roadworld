# Faults found (gates8), in plain words

No-visual round. Method: payloads fed to the hooks' functions (and, for lock_guard and the finish hook, to the real scripts as processes with fake harness JSON and real transcript files), never executed. Scratch under %TEMP%, removed with links unlinked first.

## Pass 1 (attack on my own lock guard, written before the first test of it)

I wrote 43 payloads against `lock_guard_impl.mjs` that the first draft's tests did not contain. Allowed although they write a locked file:
1. `robocopy C:\x tests\locked /MIR` and `robocopy C:\x tests\locked a.mjs`: robocopy, xcopy and rsync were not in the copy set.
2. `xcopy /y C:\x\a.mjs tests\locked\` and `rsync -a x/ tests/locked/`.
3. `ln -sf /tmp/x tests/locked/a.mjs`: a link replaces the file.
4. `find tests/locked -delete` and `find tests/locked -type f -exec rm {} +`: the start folder of a find is what is deleted from.
5. `Get-ChildItem tests\locked | Remove-Item`: Remove-Item with no path of its own acts on what the pipe feeds it.
6. `git clean -fdx tests/locked`: a clean of a named path.
7. `[IO.File]::WriteAllText("tests\locked\a.mjs","x")` (PowerShell's own .NET calls).
All seven forms (10 payloads) are fixed in `lock_guard_impl.mjs` (copy-like programs, `find` with -delete / -exec rm, `prevPaths` for piped Remove-Item and friends, `git clean <path>`, a .NET file-call check) and pinned in `test_guards8.mjs` (E1, labels "self-audit 1"), together with the matching "must stay allowed" cases (robocopy into an unlocked folder, rsync into build, `find tests/locked -name`, a listing piped into Select-Object, a listing of build piped into Remove-Item, `[IO.File]::ReadAllText`).
8. A wrong expectation of mine, found by the first suite run, not a guard fault: robocopy /MIR into a clean worktree's docs folder is judged like `rm -rf` and is allowed; `del /s` with a wildcard inside a worktree stays refused (pinned by gates7 item 2). I changed the test, not the guard, and said so in the labels.
9. Wiring: `test_hooks.mjs` copies the hook files to a temp main and starts every settings.json hook line; the first run failed because the new lock_guard files were not in that copy list. Fixed (the canary now starts 11 lines).

## Pass 2 (the delete and git guard fixes attacked with 36 payloads that are not in my tables)

Delete guard (22): name filters `*cs`, `[d]ocs`, `-iname DOCS`, `do?s`, `r*`, `Get-ChildItem -Filter "*cs"`, `"DOCS"`, `"re*" -Recurse`; `del /s /q "docs\reference\*"`, `docs\*`, `.\*`, `..\roadworld\*.jpg`; `iex ("rm -rf " + "docs")`, `& "rm -rf docs"`, `iex "iex 'rm -rf docs'"`, `iex "rm -rf docs; echo done"`, `cmd /c "rd /s /q docs"`, `powershell -NoProfile "iex 'rm -rf docs'"`, `pwsh "Get-Date; Remove-Item docs -Recurse"`, `robocopy ... docs\reference /mir` (lower case), `robocopy ... "<main>\docs" /MIR`: all refused as they should be. `powershell -File x.ps1 "rm -rf docs"` is allowed (the text is an argument to a script, not a command: correct).
Git guard (14): `cmd /c "cd /d <main> && git add -A"`, `git -C <main> add .\`, `GIT_DIR=<main>/.git git -C <worktree> add -A` (refused conservatively: GIT_DIR wins over -C), `--git-dir= --work-tree=`, `iex "git -C <main> add -A"`, `& git add -A`, `git.exe add -A`, `"git" add -A`, `git add :/`, `git commit -a --amend -m x`: refused. `git -C <worktree> add -A` from main, `git add -A -- docs` (explicit path), `git add -A; git status` in a worktree: allowed, correct.
One real gap found: `git add --pathspec-from-file=x` stages whatever the file lists, including `.`. See Residual 3.
The A6 measurement on real worktrees (read-only listing; nothing deleted) is in STATUS.md.

## Pass 3 (what a builder, the auditor or the owner would say)

1. "My test under tests/locked has a typo the auditor made; I cannot even fix it." Answer built in: the refusal text says to make it pass or write BLOCKED with `needs:`; a locked test with a real defect is a BLOCKED item the main session or a new auditor resolves, not a builder edit. Not a fault of the guard.
2. "I created a NEW test under tests/locked." Refused too (the folder is the auditor's). A builder writes its own tests under `tests/` (any other folder).
3. "I am a resumed builder (SendMessage): is my checkpoint used up?" Yes, once per agent id, including after a resume; the full check then applies. Said in STATUS.md and in the refusal note ("a checkpoint hand-back was already used by this agent").
4. "The checkpoint is a loophole to stop without handing in." A checkpoint needs a valid first line, a real existing image and a file written after the agent started, and it is not a hand-in: no audit starts, nothing is recorded as passed, the final hand-in is still checked in full. The honest cost: the main session sees `decision=checkpoint` in gate.log and must look at the named image itself; that is the point of it.
5. "A path-less `git clean -fd` in my worktree is refused." Only when a junction or symlink exists on disk below it (outside the build output folders); the message tells the builder to name a folder.
6. Owner view: nothing here changes how a round looks; the rules are three lines in the contract (locked tests, checkpoint, git list) which are proposed in STATUS.md.

## Residual

1. lock_guard cannot see a path built from variables or pieces, a script file that edits the test (`node x.mjs`), a build tool or editor that rewrites it, or anything outside Bash, PowerShell, Write, Edit and NotebookEdit. It reads text only.
2. `patch -p1 < x.diff` and `git apply x.diff` change a locked file when the diff names it: the diff content is not read. `git stash` / `git reset --hard` / `git checkout .` can restore an older version of a locked test: they only undo the builder's own uncommitted edits; a committed weakening by the builder is caught by the auditor's re-check and by `git diff` of the locked paths in the next audit (the auditor should run `git diff <audited tip> -- tests/locked` as a first step; I could not enforce it).
3. `git add --pathspec-from-file=<file>` (and `--pathspec-file-nul`) in main stages what the file lists; the file is not read. Pedantic: nobody types it by accident.
4. Checkpoint: the second-call window (120 s) means a second stop within two minutes of the first, for the unchanged file, is also allowed. A builder that stops, is resumed, edits the file and stops again within the window is let through a second time only if the file was not rewritten (the file's time must be older than the first grant); a rewritten file counts as a later stop and gets the full check. The image is only checked for existing, not for content (a blank PNG passes): the main session looks at it.
5. F6: the cap itself (5 s, 5 000 000 entries) was never reached by real data on this machine; build output folders are skipped instead. A junction buried inside a build output folder of a worktree (for example under `Intermediate`) is therefore not found when a worktree-wide delete or `git clean` is judged; a link NAMED like a build folder is found. Reason: a built Unreal tree has hundreds of thousands of entries there and the audit's own measurement says the old cap would have refused it; nothing in this project puts a junction there.
6. `needsIsConcrete` is a word heuristic: a vague line that happens to contain an object word and a second word ("a photo please") is accepted. The audit's 14 vague examples and my 7 stay refused.

## Residual (accepted by main session, 23 Oct)

Decision D of the scope message: not built; each is a spelling an honest agent does not type, or a look-through for the git guard.
1. Hole 5: loops over the photos with a per-file delete (`foreach ($f in gci docs\reference -r) { rm $f.FullName }`, `Remove-Item (Get-ChildItem docs\reference -Recurse)`).
2. Hole 6: one-liners other than rmtree / rmSync that reach the photos (`os.remove` in a comprehension, `pathlib ... unlink`, `fs.unlinkSync` in a loop, `subprocess.run(['rm','-rf',...])`, `fs.renameSync`, `shutil.move`, `os.rename`).
3. Hole 7: archivers and sync that delete their source (`7z a x.7z <photos> -sdel`, `rar a -df`, `tar --remove-files`, `rsync --remove-source-files`).
4. Hole 8: mass moves, truncations and long named lists (`Get-ChildItem docs\reference -Recurse -File | Move-Item`, `find ... | xargs mv -t`, `| Clear-Content`, a `for` loop with `: > "$f"`, three named photos in one `rm`).
5. The look-through for the git guard over `xargs git add -A`, `find . -exec git add -A \;`, `parallel git add -A`, `Start-Process git -ArgumentList 'add','-A'` and `script -c "git add -A"`.
