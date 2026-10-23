# AUDIT gates8 (fresh auditor, independent of the builder)

## First look (written after the brief, the hook sources and the suite runs, BEFORE reading STATUS.md or FAULTS.md)

The 3 biggest ways these hooks could still fail the owner after merge, ranked:

1. **Lock guard false blocks stall a builder in ordinary work.** `lock_guard_impl.mjs` adds `<worktree>/tests/locked` to its "folders above locked files" list unconditionally (`lockedAbs.push(...)`), and `judgeTarget` treats any glob or any move/delete argument as "the whole folder". So `mv new_test.cpp tests/`, `rm tests/*.tmp`, `find tests -name "*.o" -delete`, `find . -name "*.tmp" -delete`, `cd tests && rm -f *.log` are refused for EVERY builder, even when no `tests/locked` exists and no `LOCKED_TESTS.txt` was written. The refusal text says "make it pass, or write BLOCKED", which is wrong for a plain file move.
2. **The lock can be removed by the builder it is meant to bind.** `LOCKED_TESTS.txt` and the prior round's folder are not protected: `rm`, `mv`, `Set-Content`, `sed -i`, `git rm`, Write and Edit on the list all pass, after which every listed test is freely editable.
3. **The checkpoint is a free pass for the first stop, and its image test is cosmetic.** Any existing image path (anywhere on disk) plus a first line satisfies `--checkpoint`; a builder that leaves `CHECKPOINT.md` in place when it finishes for real has its one and only stop logged as `checkpoint`, with no builder record, so the real hand-in is never checked and the auditor later has no record of who built the folder. A stop within 120 s of a checkpoint, file untouched, is also waved through as "the same finish".

(The photo-wipe risk, ranked 4th and checked below: the junction walk skips build-output folders, and a junction inside one does hide; see H3.)

## Verdict: SEND BACK (one small fix round)

Fix, in order (details below): F1 and F3 (false blocks that hit every builder), H1 and H2 (the lock can be removed or is silently empty), H3 (junction inside a build folder), H5 and H4 (checkpoint), C1 to C4 (merge checklist and rule text). Everything else in A, B and C of the scope decision is fixed and green. D is not re-listed.

Method: seven suites run one at a time; about 290 real payloads fed as JSON to the real `lock_guard.mjs` process with real transcript files (scratch in %TEMP%, removed with links unlinked first, nothing executed); about 70 payloads to `delete_guard_impl.analyse` and `git_guard_impl.analyse` on a fixture with REAL junctions that I made to my own scratch photos folder (commands were strings only, never run); `check_handin.mjs --checkpoint` on files I wrote. No git in the main checkout. In the worktree: reads, one read-only `git merge-tree --write-tree` (clean, no conflicts), and the commit of this file.

## Fault list

### False blocks (a builder or auditor is stopped doing legitimate work)

F1. **Lock guard refuses ordinary work on the whole `tests/` folder, for EVERY builder, even with no `tests/locked` folder and no `LOCKED_TESTS.txt`.** Cause: `lockCtx` always adds `<worktree>/tests/locked` to `lockedAbs`; `judgeTarget(.., whole=true)` is applied to every positional argument of mv / Move-Item (so the DESTINATION `tests/` counts as a folder being moved away); any glob is judged by `above(base)`. Verified with no list and no locked folder on disk, cwd = worktree, builder with HANDIN_DIR:
   - `mv new_test.cpp tests/`, `mv new_test.cpp tests`, `mv a.cpp b.cpp tests/`, `Move-Item new_test.cpp tests\` -> expected allowed, got REFUSED
   - `rm tests/*.tmp`, `Remove-Item tests\*.obj`, `sed -i s/foo/bar/ tests/*.cpp` -> expected allowed, got REFUSED
   - `find tests -name "*.o" -delete`, `find . -name "*.tmp" -delete`, `cd tests && rm -f *.log`, `git clean -n tests` -> expected allowed (cannot reach a locked file, or a dry run), got REFUSED
   Expected rule: refuse only when the target can really hit a locked file: a glob that matches an existing locked file, a folder delete or move whose tree really contains an existing locked file, a mv/cp destination that is a locked path. `lockedAbs.push(tests/locked)` must be conditional on the folder existing or on list entries, and the destination of mv must not be judged as `whole`.
F2. Inline interpreter code that only READS a locked test is refused when the code holds a write-like word: `python -c "import shutil; shutil.copy('tests/locked/a.mjs','/tmp/a')"`, `python -c "print(open('tests/locked/a.mjs').read().replace('a','b'))"`, `node -e "fs.writeFileSync('out.txt', fs.readFileSync('tests/locked/a.mjs','utf8'))"` -> expected allowed, got REFUSED (`replace` and `move` are in the write regex). Reading the locked test through node or python is a normal way to inspect it. Expected: judge whether the locked path is the argument of the write call, or at least drop `replace` and `move`.
F3. The refusal text for F1 and F2 says "locked test: written by the auditor; make it pass, or write BLOCKED", so a builder is told to write BLOCKED for a plain file move. Expected: the reasons name the exact locked file or list entry that was matched (not "the folder holds tests/locked").

### Holes (an honest agent could type it, or the control does not hold)

H1. **The lock list and the prior folder are not protected.** For a builder, all ALLOWED, expected REFUSED: Write and Edit of `docs/progress/prev/LOCKED_TESTS.txt`; `rm`, `mv ... /tmp`, `cp /dev/null`, `echo >`, `sed -i /listed/d`, `git rm` of it; `Set-Content ... ""`; `Remove-Item`; Write of the prior `AUDIT.md`; `rm -r docs/progress/prev`; `mv docs/progress/prev docs/progress/prev2` (the folder is not found any more, so the list reads empty). After any of these every listed test is free to edit. Expected: `LOCKED_TESTS.txt`, the prior AUDIT.md and a delete or move of the folder holding them are refused with the same message.
H2. **A bad `LOCKED_TESTS.txt` silently locks nothing.** `lockedList` reads utf8: a UTF-16LE file (what Windows PowerShell can write) gives garbage entries (`\x00t\x00e\x00s...`); a glob entry (`tests/test_*.cpp`) never matches (`direct()` returns null); BOM plus CRLF is handled fine. No log line in either case. Expected: decode UTF-16 (BOM FF FE), support globs or reject them loudly, and log a line (gate=lock_guard, "FAIL-OPEN: entry matches no file" style) when an entry matches no file or the list cannot be decoded, so the main session gets a GATE NOTICE.
H3. **A junction to the photos inside a build-output folder hides from the delete guard.** The A6 fix skips `build`, `Intermediate`, `Binaries`, `Saved`, ... in the junction walk. With a real junction `build\ref -> photos` in a fixture worktree (same for `unreal\X\Intermediate\ref`) these are ALLOWED: `rm -rf build`, `rm -rf *`, `git clean -fdx`, `rm -rf unreal`, `cmd /c rd /s /q build`, `rmdir /s /q build`, `robocopy empty build /MIR`. Control with the junction at `docs\ref2`: `rm -rf *` and `git clean -fdx` are refused as they should be. `rm -rf build` is the routine CLEAN build the contract demands, and contract item 1 tells agents to "link the folder" without saying where. The 22 Oct wipe was exactly a delete following a link. FAULTS Residual 5 says nothing in the project puts a junction there: that is intent, not what an agent can type. Expected: refuse a delete or clean whose target tree holds a link within depth 3 of a build-output folder root (a links-only walk of three levels costs milliseconds even on an Unreal tree), or refuse when the deleted target IS a build-output folder that holds a link at any depth found by a links-only walk under a time budget.
H4. **The checkpoint image test is cosmetic.** `check_handin --checkpoint` PASSES with `images: C:\Windows\System32\@WLOGO_96x96.png`, with `images: fake.png` (a 10-byte text file), and for an image from an earlier round: only existence and an image extension are checked. Expected: the image lies in the hand-in folder or the worktree, was written after the agent started (the `--since` the file check already uses), is non-empty and starts with a PNG or JPEG signature.
H5. **A builder that leaves CHECKPOINT.md and then finishes for real has its final stop logged as a checkpoint** (the builder's test cpF pins this as intended). Result: no builder record, no audit started, and the later auditor fails with "no record of who built this folder". The proposed contract text never tells a builder to delete CHECKPOINT.md before its final hand-in. Expected: run the full check first; if it passes, it is a hand-in (record the builder); only if it fails is a valid checkpoint let through. Add to the contract text: "delete CHECKPOINT.md before your final hand-in".
H6. Minor: the 120 s second-call window lets a resumed builder stop again within two minutes, file untouched, with no check. The two hook calls of one finish are seconds apart: use 15 to 20 s.
H7. Formatters that rewrite a locked test in place are not seen: `clang-format -i tests/locked/a.mjs`, `npx prettier --write`, `dos2unix`, `awk -i inplace`. An honest agent running a formatter over `tests/` is the realistic route. Expected: add these to the in-place set. Obfuscated or rare forms I rule D and do not list as faults: `cp -t`, `>|`, `curl -o`, `wget -O`, `tar -C`, `unzip -d`, `(Get-Item x).Delete()`, a trailing-dot folder name `locked.`.

### Checklist and rule text

C1. The proposed contract item 8 text says "replace the git sentence with: ...", but neither the branch's nor main's `docs/AGENT_CONTRACT.md` or `CLAUDE.md` holds a git sentence (grep for `git -C` and `git add -A`: zero hits in both). It must say "add to item 8". While editing, item 8 (and CLAUDE.md) have lost backslashes (`docseference`, `C:Usersbgbg0oadworld-reference-backup`): repair to `docs\reference` and the real path.
C2. The proposed G2 list is not "as it is": `isWriteSub` also refuses `worktree move|lock|unlock|repair`, `branch` with -c/-C/-f/-u/--track and `tag` with -a/-f/-s/-u; the text names only some. List them, or say "worktree (any form except list)".
C3. The proposed checkpoint item lacks the two rules a builder needs: delete CHECKPOINT.md before the final hand-in (H5) and the image must be written during the run (H4). The proposed lock item does not say that the auditor lists tests as one path per line, no globs, UTF-8, in `LOCKED_TESTS.txt` beside AUDIT.md (STATUS has this, the contract text does not).
C4. Merge checklist: runnable, and the backup order is safe (verified: `--verify-only [--full]` exists; the mirror run validates every picture first and refuses on any bad one or a loss above 5 percent; step 5 needs step 4's exit 0 plus a second verify; `unlink_reference_junctions.ps1` has `-All` and `-WhatIfOnly`; `ref_seed.mjs` and `clean_test_temp.mjs` exist; `clean_test_temp.mjs --dry-run` ran: 0 found; the branch merges into main without conflicts). Faults: (a) step 10 (the seven suites on main) comes after the junction unlink of step 5 and the probe of step 8; put it straight after step 1 so a bad merge is found before anything is unlinked. (b) Step 8's lock probe needs a builder launch with a HANDIN_DIR and an existing file under `tests/locked`; say so. (c) Step 9 should say the auditor keeps `LOCKED_TESTS.txt` UTF-8 without globs (H2).
C5. FAULTS Residual 5 is rejected (H3). Residual 6 is overstated (`a photo please` IS refused by `needsIsConcrete`, verified), harmless. Residual 1 to 4 accepted (4: shorten the window, H6). Residual 3 (`git add --pathspec-from-file`) accepted as pedantic.

## Prior items (gates7 AUDIT.md "For the next builder", 21 items), checked with my own payloads, not only the builder's table

| Item | My verdict | Evidence |
|---|---|---|
| 1 iex / Invoke-Expression | fixed | `iex "rm -rf docs"`, `Invoke-Expression "Remove-Item -Recurse docs/reference"`, `iex "git add -A"` refused |
| 2 name filter matching docs / reference / photo folder | fixed | `rm -rf do*`, `Get-ChildItem -Filter "aer*" \| Remove-Item -Recurse` refused (`rm -rf ref*` from the main root allowed: nothing there matches, correct) |
| 3 del /s with wildcard | fixed | `del /s /q docs\*`, `del /s /q *.jpg` refused |
| 4 powershell / pwsh "cmd" without -Command | fixed | `powershell "rm -rf docs"`, `pwsh "Remove-Item -Recurse docs"`, `powershell "git add -A"` refused |
| 5 to 8 | open by decision D | accepted, not re-listed |
| 9 cd /d, GIT_DIR, git add .\ (look-through is D) | fixed | `cd /d <main> && git add -A`, `GIT_DIR=<main>/.git git add -A`, `git add .\` refused |
| 10 G2 write list | fixed | branch -D, tag v1, gc, worktree add, config set, update-ref, apply, clean -fd refused; fetch, status, log, `branch --list`, `config --get` allowed |
| F1 stash list / show | fixed | allowed for a builder with HANDIN_DIR |
| F2 path-less git clean -fd, clean worktree | fixed | allowed in a link-free worktree, refused in main for a builder |
| F3 robocopy /MIR into build | fixed | allowed in a worktree (but see H3) |
| F4 short concrete needs lines | fixed | "a screenshot of the junction", "the Unreal editor session log", "a recording of the viewer" accepted; "help", "more time", "something to look at", "a photo please" refused |
| F5 git add -u . / -n . | fixed | allowed; `git add -A` refused |
| F6 5 s walk cap | cap fixed; RE-OPENED as H3 | real junction in `build\ref` is not seen |
| C1 to C5 checklist items | fixed, but new wording and order faults C1 to C4 above | |

## E1 attack results (real lock_guard.mjs process, real transcripts)

Refused as wanted (about 95): Write and Edit through every spelling of the locked path (relative, `..` from a subfolder cwd, case variants, double slashes, `..` inside, ADS suffix), NotebookEdit; shell: redirects (`>`, `>>`, `1>`, bare `>`, no-space), `tee`, `touch`, `truncate`, `dd of=`, `sed -i`, `sed --in-place`, `sed -ni`, `perl -pi`, `rm` (plain, glob, `-rf`, `./`, after `cd`, after `;`, `&&`, `||`, in `( )`, `{ }`, `if`, `for`, `env`, `command`, `\rm`, `/bin/rm`, `nohup`, `time`), `rmdir`, `mv` (file, folder, within), `cp` onto, `install`, `git rm`, `git mv`, `git -C ... rm`, `git checkout [ref] -- file`, `git checkout ref file`, PowerShell Set-Content (-Path, -LiteralPath), Out-File, Add-Content, Clear-Content, Remove-Item (glob), Move-Item, Rename-Item (folder), Copy-Item -Destination, scriptblock, iex, `[IO.File]::WriteAllText`, `cmd /c`, `bash -c`, `powershell "cmd"`, `pwsh -c`, python and node inline writes, a new file under a listed folder.
Auditor (NO-CONTRACT with AUDIT-OF): free. Clerk (NO-CONTRACT, no HANDIN_DIR): free. Main session (no agent_id): free, including the list. Locked files untouched after the run; no FAIL-OPEN lines. Missed: H1, H2, H7 (and D forms).

## E2 attack results

- Two checkpoints by one agent: the second (after the window, or with a rewritten file) gets the full check, message "already used ... once per agent" (builder's tests re-read, code matches).
- Missing image: refused and named. Stale CHECKPOINT.md (older than the agent): refused, full check. A final stop after a checkpoint: full check. Gaps: H4, H5, H6.

## Suites (run one at a time on the worktree)

| Suite | Result |
|---|---|
| tools/test_gates.mjs | 514 passed, 0 failed |
| tools/hooks/test_hooks.mjs | 412 passed, 0 failed |
| tools/hooks/test_guards6.mjs | 32 passed, 0 failed |
| tools/hooks/test_guards7.mjs | 28 passed, 0 failed |
| tools/hooks/test_guards8.mjs | 56 passed, 0 failed |
| tools/test_worktree_tools.mjs | 27 passed, 0 failed |
| tools/test_backup_reference.mjs | 56 passed, 0 failed |

Live gate.log (`C:\Users\bgbg0\Documents\Programming\roadworld\.claude\state\gate.log`): 972 lines before the suites, 973 after the first three, 985 at the end. The new lines are other live sessions' hook lines (inject, handin, notice, ack, make_brief with other agent ids); a grep of them for the suites' and my own temp-folder names (`aud8`, `guards8-`, `hooktest-`, `gatetest-`, `guards6-`, `guards7-`, `bkref-`, `wttools-`) finds 0. The suites do not write the live log. `%TEMP%` held no leftover test folder; my `aud8*` scratch folders are removed (links unlinked first, none touched photos).

Where expected values come from, and tests that pin a fault: `test_guards8` pins `rm -rf tests` and `mv tests tests_old` as refused, from the builder's reading of the brief; the brief says nothing about globs or an mv destination under `tests/`, so F1 is untested, not pinned by a written rule. `test_guards8` case cpF pins H5 as "intended" with no written rule behind it beyond the builder's reading of "once per agent": rewrite it with the fix. Builder residual 5 (junction inside build) is a written decision with no test either way (H3).
