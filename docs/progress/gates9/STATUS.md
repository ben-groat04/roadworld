## Skipped, refused or failed

HANDIN: no-visual

- Nothing skipped or refused. All 15 items of the gates8 AUDIT.md (F1-F3, H1-H7, C1-C5) are fixed; none is left open.
- Not verified (said plainly): none of the changed hooks has run in a live session. They take effect after the merge and a restart of the main session. Every case below ran as a process or function against real files in a temp folder, with real junctions, real PNGs, real git repos and real transcripts; nothing wrote the live `.claude/state/gate.log` (every suite sets `CP_GATE_STATE_DIR`).
- Two round-8 tests were rewritten, not loosened: in `test_guards8.mjs` A6 pinned "a link inside a build output folder is not walked" (that is exactly hole H3, so it now pins the opposite), and E2 used an 8-byte fake PNG and a per-agent record (H4 and H6 replace both). The new expectations come from the audit text, not from the code.
- Method note: every Bash call starts in the main checkout; git was only `git -C <this worktree>` with explicit paths. Nothing pushed or merged. Contract, CLAUDE.md and `.claude/settings.json` were not touched. No emojis.

References opened: 6 of 6 listed (gates8 AUDIT.md, STATUS.md, FAULTS.md; the BRIEF-FILE with its send-back note; `tools/hooks/lock_guard_impl.mjs` as of round 8; `docs/AGENT_CONTRACT.md` and `CLAUDE.md` for the C1 grep)

## Prior audit items

Source: `docs/progress/gates8/AUDIT.md`, "Fault list": F1-F3, H1-H7, C1-C5 (15 items). Proof is in `tools/hooks/test_guards9.mjs` (54 checks); `fail_first_round8_code.txt` shows the same suite failing 36 of 54 on the round-8 tools (the 18 that pass there are the "must stay allowed" and unchanged-behaviour controls).

| Item | Status | Proof (test) or reason with evidence |
|---|---|---|
| F1 whole `tests/` refused for every builder | fixed | `lockedListInfo` / `lockCtx`: protection covers only files named by an EXISTING, READABLE list (plus `tests/locked` files that exist on disk). mv/cp destinations are judged as a path, not as `whole`; a glob counts only if it matches an existing locked file. TG9 "F1" (21 allowed payloads with no list, 28 with a list and a `tests/locked` folder: `mv new_test.cpp tests/`, `rm tests/*.tmp`, `sed -i ... tests/*.cpp`, `find tests -name "*.o" -delete`, `cd tests && rm -f *.log`, `git clean -n tests` allowed; a real hit refused) |
| F2 inline code that only reads a locked test refused | fixed | `inlineWrites`: structured analysis of python and node one-liners; a locked path as the argument of a read, copy-from, print or `.replace` on a string passes, as the target of open(..,'w'), writeFileSync, shutil.copy dest, os.remove, unlink, rename and similar is refused. TG9 "F2" (15 payloads, the three from the audit among them) |
| F3 refusal text says BLOCKED for plain moves | fixed | split messages: a real locked test names the file ("locked test: <file>; make it pass, or write BLOCKED"); the list and the prior audit have their own text with no BLOCKED. A glob refusal names the locked file it reaches. TG9 "F3" (3 checks) |
| H1 lock list and prior folder unprotected | fixed | for builders, `LOCKED_TESTS.txt` and the prior AUDIT.md are protected like locked tests (Write, Edit, rm, mv, cp, echo >, sed -i, git rm, Set-Content, Remove-Item, folder delete or move). Auditors, clerks and the main session are not builders and stay free. TG9 "H1" (30 payloads, every form from the audit) |
| H2 bad list silently locks nothing | fixed | `decodeList` reads UTF-16LE with and without a BOM; entries may be globs (`globToRe`, matched against existing files). An entry or glob matching no file, an empty list, or a prior audit that speaks of LOCKED_TESTS.txt with the file missing logs `LOCK-LIST-BAD` with a FAIL-OPEN marker (GATE NOTICE for the main session), once per agent, and shows a visible `systemMessage` to the builder. TG9 "H2" (22 checks) |
| H3 junction inside a build folder hides | fixed | `linksBelow` now walks build output folders with their own bounds (6 levels, 1500 ms for all build folders together) and finds `build\ref`, `unreal\X\Intermediate\ref`; the delete guard refuses only when a link is found. When a bound is hit, a `delete_guard` "scan bound" line is logged (no refusal without a link). `rm -rf build` in a link-free worktree stays allowed (timing checked). TG9 "H3" (21 delete-guard cases plus 4 bound checks). Residual 5 of round 8 is withdrawn |
| H4 checkpoint image test cosmetic | fixed | `check_handin --checkpoint`: image must lie in the hand-in folder or `_compare/<this round>`, be written after the run started (`--since`), be at least 10 KB, and start with a real PNG (signature plus IHDR) or JPEG header. `C:\Windows\System32\@WLOGO_96x96.png`, a text file named .png, an 8-byte signature, noise, an old image, another round's `_compare`, a reference photo: all refused with where it must be. TG9 "H4" (8 refusals, plus the good cases) |
| H5 left-over CHECKPOINT.md turns the final stop into a checkpoint | fixed | a CHECKPOINT.md older than the latest work commit of the worktree (`latestWorkCommitMs`, hand-in folder excluded) is ignored and logged; the refusal message tells the builder to delete CHECKPOINT.md before the final hand-in. TG9 "H5" (3 checks; round-8 code lets it through) |
| H6 120 s window too long | fixed | `CHECKPOINT_WINDOW_MS = 30000`; the record is per round (hash of the hand-in folder), not per agent: a second agent, or the same agent after 30 s, gets the full check ("already used, once per round"). TG9 "H6" (3 checks) |
| H7 formatters not seen | fixed | `FORMATTERS` table: in-place forms of clang-format, prettier, dos2unix, unix2dos, awk and gawk inplace, black, autopep8, eslint --fix, gofmt -w, rustfmt, perl -i, sed -i and PowerShell replace-then-Set-Content. Read-only forms (`clang-format tests/locked/a.mjs`, `prettier --check`) stay allowed. TG9 "H7" (22 payloads) |
| C1 "replace the git sentence" | fixed | text below says "add to item 8"; the lost backslashes in item 8 and CLAUDE.md are named with the repair (`docs\reference`, `C:\Users\bgbg0\roadworld-reference-backup`); the repair itself is the main session's, it owns CLAUDE.md and the contract |
| C2 G2 list not "as it is" | fixed | text below lists the real set from `isWriteSub` |
| C3 checkpoint and lock text lack rules | fixed | text below adds: delete CHECKPOINT.md before the final hand-in; image written during the run, in the hand-in folder, 10 KB or more; the auditor lists locked tests one path or glob per line, UTF-8 or UTF-16, in `LOCKED_TESTS.txt` beside AUDIT.md |
| C4 checklist order and probes | fixed | merge checklist below: suites straight after the merge (step 2), the lock probe says it needs a builder launch with a HANDIN_DIR and an existing file under `tests/locked`, the LOCKED_TESTS.txt step names encoding and globs |
| C5 residuals | fixed | FAULTS.md: Residual 5 of round 8 closed (H3 built), Residual 6 wording corrected, Residual 4 closed by H6 |

## Changes (file:function)

- `tools/hooks/lock_guard_impl.mjs` (rewritten): `globToRe`, `decodeList`, `lockedListInfo`, `lockedList`, `lockCtx` (protection only for files that exist), `inlineWrites`, `judgeCommand`, `judgeDest`, `FORMATTERS`, `reportBadList`, `LOCK_MESSAGE` and `PROTECTED_WHY` (split texts).
- `tools/hooks/delete_guard_impl.mjs`: `BUILD_OUTPUT_DIRS`, `linksBelow` (build folders walked with `buildMs` and `buildDepth`, `buildBound` and `boundWhy` on the result, a logged line when bounded).
- `tools/check_handin.mjs`: `imageDims`, the checkpoint image block (location, `--since`, 10 KB, header).
- `tools/hooks/handin_gate_impl.mjs`: `CHECKPOINT_WINDOW_MS`, `latestWorkCommitMs`, stale-checkpoint ignore, per-round `checkpoint_<hash>.json` record, same-agent same-finish rule.
- `tools/hooks/test_guards9.mjs` (new, 54 checks), `tools/hooks/test_guards8.mjs` (A6 and E2 updated as said above, real PNG helper).

## Suites (each run alone on this worktree, 23 Oct)

`test_gates` 514, `test_hooks` 412, `test_guards6` 32, `test_guards7` 28, `test_guards8` 56, `test_guards9` 54 (new), `test_worktree_tools` 27, `test_backup_reference` 56: all 0 failed. Fail-first: `fail_first_round8_code.txt` (36 failed, 18 passed on the round-8 tools, commit 24bb64c).

## Merge checklist (for the main session), in this order

1. Merge `agent/gates9` (it carries gates8; the hook stubs and `_impl` files land together; no settings change in this round).
2. Straight away, before anything is unlinked: run the eight suites on main, one at a time: `node tools/test_gates.mjs`, `node tools/hooks/test_hooks.mjs`, `node tools/hooks/test_guards6.mjs`, `node tools/hooks/test_guards7.mjs`, `node tools/hooks/test_guards8.mjs`, `node tools/hooks/test_guards9.mjs`, `node tools/test_worktree_tools.mjs`, `node tools/test_backup_reference.mjs`. Trust only a clean run of the merged state.
3. `node tools/ref_seed.mjs` once in the main checkout.
4. `node tools/backup_reference.mjs --verify-only --full` (changes nothing). Non-zero or ALARM: STOP and fix the photos.
5. Only if step 4 exited 0: `node tools/backup_reference.mjs` for real (never `--accept-loss` on the first run).
6. Only if step 5 exited 0 AND a following `--verify-only` exits 0, with worktrees idle: `powershell -NoProfile -ExecutionPolicy Bypass -File tools\unlink_reference_junctions.ps1 -WhatIfOnly`, then `-All`.
7. Put `ctest assertions >= N` in each engine task file's Gates section.
8. Optional: `node tools/clean_test_temp.mjs --dry-run`, then for real. It removes `gatetest-*`, `hooktest-*`, `guards6-*` to `guards8-*`, `bkref-*`, `wttools-*`; the `guards9-*` folders are cleaned by the suite itself (links unlinked first), so expect none.
9. After a restart of the main session, probe: (a) git guard: a NO-CONTRACT read-only agent that runs `git add -A` in main must be refused, and `gate.log` holds `"gate":"git_guard","decision":"deny"` with a non-empty `agent_id`. (b) lock guard: the probe needs a BUILDER launch (a contract brief with `HANDIN_DIR:`) in a worktree that has an existing file under `tests/locked`; it must be refused when it edits that file and allowed when it moves a new test into `tests/`.
10. Start of the first round that uses locked tests: the auditor puts tests under `tests/locked/` or lists them in `LOCKED_TESTS.txt` beside its AUDIT.md (UTF-8 or UTF-16, one path or glob per line); the next brief carries `PRIOR-AUDIT: <path to that AUDIT.md>`. A bad list shows a GATE NOTICE (`LOCK-LIST-BAD`).

## Proposed rule text (not applied: the contract and CLAUDE.md are the main session's)

- Contract item 8, ADD (there is no git sentence to replace): "Every tool call of an agent starts in the MAIN checkout (the cwd is reset between calls). Run git as `git -C <absolute path of your worktree> ...`. For every SUBAGENT (the main session is not limited), `git add -A`, `git add .`, `git commit -a` and `git stash -u` in the main checkout are refused (git_guard hook). Write operations are refused for subagents in the main checkout: `git branch` with a name, -d, -D, -m, -M, -c, -C, -f, -u or --track; `git tag` with a name, -a, -f, -s, -u or -d; `git gc`; every `git worktree` form except `list`; and the other write subcommands in `isWriteSub`. Read-only forms (status, log, diff, show, stash list, branch --list, tag -l) stay allowed." Also repair the lost backslashes in item 8 and CLAUDE.md: `docs\reference` and `C:\Users\bgbg0\roadworld-reference-backup`.
- Contract, new item (locked tests): "A test the auditor wrote is locked: any file under `tests/locked/`, or named in `LOCKED_TESTS.txt` in the prior round's hand-in folder (the folder of the brief's `PRIOR-AUDIT:` file). The auditor writes that list as UTF-8 or UTF-16, one path or glob per line, beside its AUDIT.md. A builder may not edit, delete, rename or reformat a locked test, nor edit `LOCKED_TESTS.txt` or the prior AUDIT.md (the lock_guard hook refuses Write, Edit, shell writes, in-place formatters and inline python or node writes; reading is free). Make the test pass, or write `BLOCKED: <item> | tried: | blocked by:` in STATUS.md."
- Contract, new item (checkpoint): "You may stop early once per round with `<HANDIN_DIR>/CHECKPOINT.md`, first line `CHECKPOINT: <what is done> | images: <path>[, <path>...]`, naming at least one image that you wrote during this run, in the hand-in folder or its `_compare/<round>` folder, a real PNG or JPEG of 10 KB or more; `node tools/check_handin.mjs <folder> --checkpoint` checks it. The finish hook lets it through once per round (30 s window for the two hook calls of one finish); it is NOT a hand-in and starts no audit. DELETE CHECKPOINT.md before your final hand-in: a CHECKPOINT.md older than your latest commit is ignored, and a second checkpoint in the same round gets the full check."
- CLAUDE.md "Hooks enforce part of both gates": add "lock_guard refuses a builder's edit of a locked test, of `LOCKED_TESTS.txt` or of the prior AUDIT.md" and "a valid `CHECKPOINT.md` lets a builder stop once per round, without starting an audit".

## Next step

Fresh auditor first (it starts as soon as `check_handin.mjs` passes), then the merge in the order above.

## Reference files opened

Case: gates8 audit and round files | Opened: C:\Users\bgbg0\Documents\Programming\roadworld-wt\gates9\docs\progress\gates8\AUDIT.md, C:\Users\bgbg0\Documents\Programming\roadworld-wt\gates9\docs\progress\gates8\STATUS.md, C:\Users\bgbg0\Documents\Programming\roadworld-wt\gates9\docs\progress\gates8\FAULTS.md
Case: contract and rules | Opened: C:\Users\bgbg0\Documents\Programming\roadworld-wt\gates9\docs\AGENT_CONTRACT.md, C:\Users\bgbg0\Documents\Programming\roadworld-wt\gates9\CLAUDE.md, C:\Users\bgbg0\AppData\Local\Temp\claude\C--Users-bgbg0-Documents-Programming-roadworld\dadbe0ba-a9fc-4f68-9e75-a11e00cf49e8\scratchpad\gates9.brief.txt
