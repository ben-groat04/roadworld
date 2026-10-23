# AUDIT gates9 (fresh auditor, did not build it)

Written in two passes. Part 1 was written BEFORE STATUS.md, FAULTS.md or COMPARE.md were opened.

## Part 1: first look and own attacks (before reading the builder's claims)

Suites, run by me with CP_GATE_STATE_DIR set to my own temp dir: test_gates 514/0, test_hooks 412/0, test_guards6 32/0, test_guards7 28/0, test_guards8 56/0, test_guards9 54/0, test_worktree_tools 27/0, test_backup_reference 55/1. The one failure is an artefact of my isolated state dir (the test expects the main-session `accept_loss` file under the state dir it was not given); rerun with the env var unset it is 56/0. No line of my runs reached the live gate.log (line count unchanged apart from other live sessions' hooks).

Hook speed: every PreToolUse hook process (lock_guard) took 57-170 ms over ~300 payloads (node start-up dominates); delete_guard analyse() 1-14 ms, including `rm -rf build` over a 3000-file link-free build folder (11 ms). Fine.

Fail-first: `fail_first_round8_code.txt` is real. I exported commit 24bb64c's `tools/`, `docs/AGENT_CONTRACT.md`, `.claude/settings.json` into a scratch folder, put this round's test_guards9.mjs beside them and ran it: 36 FAIL / 18 ok (the 18 are controls and the unchanged behaviour: isolation checks, fixtures, the "control" cases). That matches what the file claims.

My own payloads (not the builder's), 300+ commands through the real lock_guard process with a real builder transcript, a real LOCKED_TESTS.txt, a real prior AUDIT.md:

- F1 false blocks: 69 ordinary builder commands all ALLOWED (mv x tests/, rm tests/*.tmp, sed -i on unlocked tests, find tests -name '*.o' -delete, cd tests && rm -f *.log, git clean -n tests, git rm / git mv / git checkout of unlocked tests, touch, cp, tee, clang-format -i on unlocked files, commit messages and grep patterns that merely mention `rm tests/locked_a.cpp`, reading the locked file in every way, writing STATUS.md, rm -rf its own hand-in folder, rm -rf build). Zero false blocks. This item is fixed.
- Locked files refused in about 100 spellings (rm, mv, cp, redirects, tee, sed -i in 8 spellings, truncate, dd, install, rsync, ln -sf, git rm/mv/checkout/restore, cd-then-relative, pushd, find -delete, python/perl/node one-liners, bash -c, eval, PowerShell Remove-Item/del/ri/Set-Content/Out-File/Move-Item/Clear-Content/.NET calls/`>`, Write and Edit tool with ./, .., case changes, backslashes). H1: Write, Edit, rm, mv, `>`, sed -i, git rm, Rename-Item, `rm -rf docs`, `mv docs docs2`, `mv docs/progress ...`, `cd docs/progress/prev && rm *`, `rm docs/progress/prev/*`, `rm docs/progress/*/AUDIT.md` on LOCKED_TESTS.txt and the prior AUDIT.md are all refused; auditor, clerk and main session stay free. Fixed.
- H2: an entry that matches no file, a non-path text line: LOCK-LIST-BAD in gate.log with FAIL-OPEN, plus a systemMessage on stdout, exit 0. List variants all work: CRLF, backslash entries, absolute entries, leading/trailing blanks, `**` globs, `./` prefix, trailing `# comment`, other-case path, UTF-8 BOM.
- H3, real junctions (cmd mklink /J into a fixture docs\reference), all refused: link in build\, link in Intermediate\Build\Win64\foo\, a link at the worktree root; for `rm -rf build`, `rm -rf build/*`, `rm -rf *`, `git clean -fdx / -xfd / -fd / -fdX`, `rd /s /q`, `rmdir /s /q`, `robocopy empty build /MIR`, `Remove-Item -Recurse -Force build`, `ri -r -fo`, `find build -delete`, `find . -delete`. A link-free build of 3000 files: ALLOW in 4-14 ms for rm -rf build, rm -rf *, git clean -fdx, rd /s /q, Remove-Item -Recurse. Photos intact afterwards; every junction unlinked with cmd rmdir before cleanup.
- H4: valid PNG in the hand-in folder (also in a subfolder) passes; reference photo, `_compare/<other round>`, outside folder, `..` traversal, nonexistent, 20 KB of zeros, 20 KB of text, 268-byte PNG, stale image (5 h old), IHDR with zero width, JPEG SOI + zeros: all refused with a message that says why. Stale CHECKPOINT.md vs `--since`: refused.

Faults found by my own payloads: depth-7 junction under build (fault 1), bulk formatter pipelines (fault 2), robocopy, patch, git apply, fs.rm (fault 3), truncated PNG (fault 8). Details in the fault list below.

Not attacked live by me: H5 and H6 through the real finish hook. For those I read `handin_gate_impl.mjs` and the builder's tests and ran them (fail-first included); H4 I attacked with my own images (see above).

Merge check: `git merge-tree --write-tree claude/practical-ride-rrf8rx agent/gates9` (run in the gates9 worktree, merge base 56b7c59) is clean: tree b6a2c9ea, no conflict. The branch only touches tools/ and docs/progress/gates9.

## Part 2: after reading STATUS, FAULTS, COMPARE

### Verdict: SEND BACK (11 faults)

The F/H core is sound and should not be redone. Faults 1, 4 and 5 must not reach main as they stand.

### Fault list for the next builder (every item is to be fixed; none is too small for the owner)

1. **H3 is not closed: a junction deeper than 6 levels below a build folder lets `rm -rf build` through.** Real junction at `build\q0\q1\q2\q3\q4\q5\q6\ref` into the photos: `rm -rf build` and `Remove-Item -Recurse -Force .\build` are ALLOWED. The only trace is an `allow` line "scan bound" in gate.log (no FAIL-OPEN, so no GATE NOTICE for the main session). The same holds for a link found only after the 1.5 s build budget. The builder's Residual 1 ("would have to be put there on purpose") is doubtful: contract item 1 tells agents to "link the folder" without saying where, and a links-only walk is cheap (I walked every build, Intermediate, Binaries and Saved folder of every worktree under roadworld-wt: all under 130 ms). Fix: drop the depth bound (keep a time bound), and when any bound is hit log it with the FAIL-OPEN marker so the main session is told. Add real-junction tests for depth 7 and for a time-bound hit.
2. **H7 is only half done: bulk and pipeline formatter forms pass.** ALLOWED on a locked file: `find tests -name '*.cpp' -exec clang-format -i {} +`, `find tests -name '*.cpp' | xargs clang-format -i`, `git ls-files tests | xargs clang-format -i`, `clang-format -i $(ls tests/*.cpp)`, `astyle tests/locked_a.cpp`, `uncrustify --replace`, `clang-tidy --fix`, `sort -o f f`, `ex -sc '%s/a/b/|wq' f`, `vim -es -c wq f`, `ed -s f`, and `sed -i tests/locked_a.cpp -e s/a/b/` (file before the script). The first three are what an honest builder types to reformat a tree, not obfuscation. Refused correctly: every direct form of clang-format, prettier, dos2unix, unix2dos, awk -i, gawk -i, perl -i and -pi, ruby -pi, black, autopep8, gofmt, eslint --fix, with globs, with `cd tests &&`, and `prettier --write .`.
3. **Other write routes to a locked test are not seen:** `robocopy x tests locked_a.cpp` and `robocopy tests x /MOV`; `patch -p1 < p.diff` and `git apply p.diff` (the diff names the locked file; the guard can read the `+++` and `---` lines when the argument is a readable path); `node -e "fs.rm('tests/locked_a.cpp',()=>{})"` (fs.rmSync is caught, fs.rm is not); `ruby -e "File.delete(...)"`; brace expansion `rm tests/{locked_a.cpp,free.cpp}`. FAULTS Residual 4 accepts patch and git apply "by nature"; I do not: `patch` is the ordinary way to edit a test. Variable, `$(...)` and `$PWD` forms I do not ask for (they cannot be seen statically), but say so in the guard's header. Residual 2 (`>|`, `cp -t`) I accept as agreed in round 8.
4. **Proposed contract item 8 text is wrong about scope.** It says "Write operations are refused for subagents in the main checkout". The `git_guard_impl.mjs` header says G1 (`add -A`, `add .`, `commit -a`, `stash -u`) applies to every subagent, but G2 (all other writes) only to a builder whose launch carried a HANDIN_DIR outside main; NO-CONTRACT clerks and mergers may still `git add docs/QUEUE.md` and commit with explicit paths in main. Also "every `git worktree` form except `list`" is wrong: only add, remove, prune, move, lock, unlock and repair are writes. A contract line should not cite the code identifier `isWriteSub`. Rewrite the sentence from the code.
5. **C1 was not delivered as text.** The brief said to restore the lost backslashes "as proposed text". STATUS gives two names (`docs\reference`, `C:\Users\bgbg0\roadworld-reference-backup`) and "repair". The merger would have to reconstruct item 8 and CLAUDE.md line 65 itself. Both are visibly corrupted today: `docseference`, `<worktree>docseference`, `<main>docseference`, `C:Usersgbg0oadworld-reference-backup`, and the robocopy command. Give the full corrected item 8 and the full corrected CLAUDE.md worktree bullet as paste-ready text.
6. **Proposed rule text has no item numbers or placement.** The two new contract items (locked tests, checkpoint) are given without numbers (the contract ends at 22, so 23 and 24) and the CLAUDE.md change is "add ..." without the line it follows. Give numbers and before/after anchors.
7. **Goalpost move in a rewritten test (contract item 22).** In `test_guards8.mjs` A6 the walk-time limit was raised from `< 2000` to `< 3000` ms while the test was turned around. STATUS says the expectations came from the audit text; it does not say why the limit moved or what the walk costs. Show the measured time in the test message; if it is under 2 s put the limit back; if not, say what it is and why.
8. **H4 checks the header only.** A PNG truncated to 12 KB (valid IHDR, no valid end) and a reference photo copied into the hand-in folder both PASS. The first is cheap to close (a PNG must end in IEND, a JPEG in FFD9). For the second, say plainly in the contract text that it is a floor the main session's own look must cover.
9. **"Visible warning for the builder" is unverified.** LOCK-LIST-BAD writes `{"systemMessage": ...}` on stdout with exit 0. The builder's tests only check that the string is on stdout. STATUS does not say whether Claude Code shows a PreToolUse systemMessage to the agent (it may reach only the user's UI), and its "Not verified" paragraph does not list this. Check the hook output contract in the Claude Code docs, or use the channel the agent is known to see, and say which in STATUS.
10. **`tools/clean_test_temp.mjs` PREFIXES stop at `guards8-`.** A test run that is killed or crashes leaves its `guards9-*` folder with real junctions to the fixture photos. My own first fail-first attempt (it crashed on a missing contract file) left exactly that: `jbuild\build\ref`, `jdeep\build\a\b\c\ref`, `jintermediate\...\Intermediate\ref`; I unlinked them with cmd rmdir before deleting. Add `guards9-`, and make the suite register an exit and SIGINT cleanup (unlink first). A stray `guards8-aR3agW` with two junctions (not mine) is in %TEMP% now; it shows the same gap in round 8.
11. **`test_backup_reference.mjs` fails (55/1) when `CP_GATE_STATE_DIR` is already set** (the brief's own instruction to auditors): the accept_loss test looks for the main-session file in the state dir it is handed. Pre-existing, but it makes the eight-suite run order- and environment-dependent. Have the suite use its own fixture folder or unset the variable for that case.

### Prior audit items (gates8 AUDIT: F1-F3, H1-H7, C1-C5), ruling on the builder's table

| Item | Builder says | My ruling |
|---|---|---|
| F1 | fixed | FIXED. 69 own ordinary commands allowed, about 100 locked spellings refused (Part 1). |
| F2 | fixed | FIXED for python and node read-only, and for the write forms I tried; the `fs.rm` gap goes under fault 3. |
| F3 | fixed | FIXED. The refusal text names the file; list and audit refusals carry no BLOCKED (checked on live stderr). |
| H1 | fixed | FIXED. Every attack form in Part 1 refused; when the prior folder is also the hand-in folder the builder can still write STATUS and FAULTS and is refused AUDIT.md. |
| H2 | fixed | FIXED in the log; the "visible warning" is unverified: fault 9. |
| H3 | fixed | RE-OPENED: fault 1 (depth and time bound hole, quiet log line). Everything shallower is fixed, with real junctions, in all spellings. |
| H4 | fixed | FIXED, with fault 8 (header only). |
| H5 | fixed | FIXED by reading code and the builder's tests plus fail-first (not live-attacked by me). |
| H6 | fixed | FIXED by reading code and tests, same note. |
| H7 | fixed | RE-OPENED: fault 2. |
| C1 | fixed | OPEN: fault 5 (no paste-ready text). |
| C2 | fixed | OPEN: fault 4 (text misstates the scope and the worktree forms). |
| C3 | fixed | FIXED in content; numbering is fault 6. |
| C4 | fixed | FIXED. Step order right (suites right after the merge); the probe says it needs a HANDIN_DIR builder; `-WhatIfOnly`, `-All`, `--verify-only --full` and `ref_seed.mjs` all exist. |
| C5 | fixed | FIXED (FAULTS.md residuals now match the code). |

### Test-truth table (where each expected value in test_guards9 came from)

| Group | Expected value from | Pins a fault? |
|---|---|---|
| F1 (49 allow and refuse payloads) | gates8 AUDIT refused-command list and the main session's decisions of 23 Oct (judge the source, protect only listed files) | No. |
| F2 (15 inline payloads) | gates8 audit F2 payloads plus the builder's mirror cases; read versus write is a stated rule | No. |
| F3 (3) | audit F3 text | No. |
| H1 (30) | audit attack list; the builder, auditor, clerk split is the main session's rule | No. |
| H2 (22) | audit H2 plus the main session's note (log and warn) | No. |
| H3 (21 + 4) | audit H3 and the main session's bound rule (link-free build passes fast) | The depth bound is the written rule, so the tests do not see fault 1; no test has a link past the bound. |
| H4 (8 + good cases) | main session's H4 rule (inside folder, newer, 10 KB, header) | No; header-only by decision (fault 8). |
| H5, H6 | main session's H5 (older than the latest commit, ignored and logged) and H6 (30 s, one per round) | No. |
| H7 (22) | main session's list | Direct forms only, so fault 2 is invisible to them. |
| Isolation, fixture-survival checks | gates6 and gates7 rules (no live gate.log writes, photos survive) | No. |

Rewritten round-8 tests. **A6 (test_guards8): accepted.** It pinned "a link inside a build output folder is not walked", which is hole H3 itself, so turning it round is right (expected value from the audit, not from the code); the 2 s to 3 s limit change is fault 7. **E2: accepted.** It used an 8-byte "PNG" (hole H4) and a per-agent record (hole H6); a real PNG helper and a per-round record replace them, again from the audit text. Neither was loosened.

### Top 5
1. H3 depth and time bound lets a deep junction through `rm -rf build`, quietly (fault 1).
2. H7 misses bulk formatter pipelines (`find -exec clang-format -i`, `xargs`) and other in-place tools (fault 2).
3. Proposed contract item 8 text misstates which agents are limited and which worktree forms are writes (fault 4).
4. C1 backslash repair not delivered as paste-ready text; item numbers missing (faults 5, 6).
5. The LOCK-LIST-BAD "visible warning" is unverified; patch, git apply and robocopy still write locked tests (faults 9, 3).

References opened: gates9 brief and send-back note, gates8 AUDIT item list (as quoted in the brief and STATUS), STATUS.md, FAULTS.md, COMPARE.md, INDEX.md (listing), fail_first_round8_code.txt (head), lock_guard_impl.mjs, delete_guard_impl.mjs diff, handin_gate_impl.mjs diff, check_handin.mjs diff, git_guard_impl.mjs, test_guards9.mjs, test_guards8.mjs diff, AGENT_CONTRACT.md item 8 and CLAUDE.md line 65 (corruption). No photos: no-visual round.
