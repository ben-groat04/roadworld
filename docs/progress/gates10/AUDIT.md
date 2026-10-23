Auditor agent: abdb0fb590da174a0
Verdict: send back
References opened: 10 of 10

# AUDIT gates10 (fresh auditor, did not build it)

Part 1 (own attacks) was written BEFORE reading the builder's FAULTS.md; STATUS.md was read after the first attack batch. All runs used CP_GATE_STATE_DIR in my own temp dir, one suite at a time under `timeout 600`.

Opened: gates9 AUDIT.md, gates9 STATUS.md, tools/make_brief.mjs, tools/check_handin.mjs, tools/hooks (brief_gate_impl, phase_marks, handin_gate_impl read; lock_guard/lock_extra and delete_guard attacked through payloads), .claude/settings.json, docs/AGENT_CONTRACT.md (as pasted in the brief), docs/templates, CLAUDE.md (as in the brief), main gate.log (all 76 distinct NO-CONTRACT descriptions, read only).
Not run live: no real subagent launch (as the builder also says).

## Suites (mine)
test_gates 514/0 (59 s), test_hooks 412/0 (67 s), guards6 32/0, guards7 28/0, guards8 56/0, guards9 54/0, guards10 107/0 (104 s), test_worktree_tools 27/0, test_backup_reference 56/0 with the variable set, test_isolation rc 0. All green. Live gate.log not written by my runs. Hook speed: lock_guard 80-110 ms per payload over 107 payloads (max 307 ms), nothing slower than gates9. No ordinary builder command was refused in my ALLOW payloads except `patch --dry-run` (fault 9).

## Own fault list (worst first)

1. **The launch hook refuses a real clerk launch from the log.** Gate.log description "Write audit first-look rule into docs" (reason "clerk job adding an owner audit-method rule to the rule docs on main (docs only...)") is REFUSED as "first look": `disguisedJob` matches the words first-look anywhere in the description or reason. Reproduced through brief_gate.mjs (exit 2). Also refused: a clerk whose reason says "update ACCEPTANCE.md with the main session first look notes". The main session writes first-look notes into ACCEPTANCE.md every round, so this will bite. The builder's ALLOW table did not include this real description. Fix: match the job (description starting "Opus first look" or "first look on/of <round>", reason saying the agent IS a first look), not the phrase. Misses in the other direction (wording evasions, accepted as limits of a wording rule): "an auditor of the gates10 builder output", "independent reviewer ... verdict accept or send back", "view the headline images and say the three most obvious faults" all pass as NO-CONTRACT. The 23 Oct real descriptions ("Auditor for X hand-in", "Opus first look on X images", "Audit X independently") are all refused correctly (checked against all 76 logged descriptions).
2. **Build phase check runs the whole suite inside a hook that kills it at 270 s, which fails open.** `phase_lib.buildProblems` allows 20 min per suite; handin_gate_impl GATE_TIMEOUT_MS is 270000 and settings.json gives the SubagentHandback hook 300 s. A cmake build plus ctest on sim_core (never tried by the builder: STATUS says only `node -e` suites were exercised) will exceed that; spawnSync returns an error and the code logs FAIL-OPEN and ALLOWS the stop. The build gate then does not run exactly when the suite is real. It also contradicts item 26 (15 min at most). Found by code read, not run to the limit. Fix: have the check read a suite result the agent produced under its own `timeout` (log written during the run, exit code and ctest summary parsed, newer than the last code commit), or raise both timeouts together with the item 26 bound and test with a real slow suite; a timeout must refuse, not allow.
3. **Phase build gate passes a build that changed nothing.** BUILD.md with `Commits: none` and no code commit passes (my noCommits case): the only commit is the BUILD.md one, which is exempt. Fix: require at least one commit touching a file outside the hand-in folder.
4. **fail_first file is not checked for failure.** A `fail_first_new.txt` saying "a passing run, nothing failed" plus padding passes (40 characters and the test name is all it needs). New tests in files outside test_*/tests//_test patterns (`tools/hooks/check_new.mjs`) are not detected as tests at all. Fix: require a failure marker and detect tests with the waiver guard's patterns.
5. **Measure check never checks the photos exist.** MEASURE.md naming a.jpg to f.jpg (files that exist nowhere) with 6 of 6 opened PASSES. M is not compared with the brief's, and the brief's case count is not enforced (the `cases` option exists but check_handin never passes it). A unit inside the photo file name (`a_3m.jpg`) satisfies the value test. One trace image can serve every case; a 1x1 PNG counts as a trace. Correctly refused: no file, no opened line, 0 opened, N<M without "not opened", truncated and junk traces, missing trace, 2 photos, no unit, `3.2 mph`, `# Case` heading, no table, trace that is a reference photo. Fix: resolve each photo under main docs/reference, compare M and the case count with the brief, take the value only from non-photo cells, require distinct images per case and a minimum size.
6. **First-look file checks are shallow.** Placeholders pass: Top 3 lines of 24 repeated letters and "never built: nothing nothing nothing nothing" are accepted. Any decodable image anywhere on disk (not under docs/reference) counts as the reference photo. A quoted path with a space is refused as "does not exist". Fix: require the photo under docs/reference, reject repeated-character and repeated-word lines, accept quoted paths. Correct: same image under 3 spellings counts once; fewer than 3 images refused; CRLF, backslashes and space-separated paths accepted.
7. **`[unreal]` is an unverifiable exemption.** Any browser render tagged `[unreal]` skips stats.json and mask.png and the road-coverage and debug-fraction check; only integrity is checked. My payloads: 12 KB-truncated PNG, 69-byte PNG, zero-padded tiny PNG, junk after IEND, fake JPEG, flat 1280x720 (2.7 KB), stale file: all refused. A 200x200 noise PNG tagged unreal passes. Nothing ties the frame to Unreal (screenshot folder, editor log line). Fix: require the frame under the Unreal Saved/Screenshots folder or a playtest log line, and list the exemption in STATUS Not verified.
8. **lock_guard evasions remaining (locked file tests/listed_test.mjs).** The chained form `node -e "require('fs').rm('tests/listed_test.mjs',()=>{})"` is ALLOWED, as are `require('fs').rmSync`, `.unlinkSync`, `.renameSync`, `.copyFileSync`, `.truncateSync`; the bare `fs.rm(...)`, `const fs=require('fs');fs.rmSync(...)` and `const {rmSync}=require('fs');rmSync(...)` are refused. The fs.rm fault was fixed for the spelling the test uses only. Also allowed: `chmod 000` on a locked file, `git am`, `git revert`, `git stash`, `git reset --hard`, `git checkout HEAD~1 -- tests` (these can revert a locked test, while `git checkout -- file` and `git restore file` ARE refused, so the line is inconsistent), and piped diffs (`cat p.diff | patch -p1`, `| git apply -`; the header says piped diffs are not seen: accepted). Refused correctly: about 70 other forms (write, tee, sed -i all orders, perl, awk -i, cp, mv, rm with brace and glob, ln, install, dd, truncate, touch, Set-Content, Remove-Item, robocopy filter and /MOV, xcopy, cd-then-write, bash -c, cmd /c, powershell -Command, python write forms, git apply and patch with a file argument, bulk formatters via find/xargs/git ls-files/$(ls)); Write and Edit on locked files, AUDIT.md and LOCKED_TESTS.txt.
9. **False refusal:** `patch --dry-run -p1 < p.diff` on a diff naming a locked file is refused though `git apply --check` and `--stat` are allowed (read-only).
10. **Contract text number clash.** STATUS proposes the new rule as "item 26". Main already has item 26 (time limits, a1cf243, pasted in this brief). The merger would create two item 26s. Renumber to 27 and say item 26 stays; the CLAUDE.md "7." line goes after the real item 6 anchor. Also item text says the main session splits every look round, as asked, good.
11. **delete_guard heredoc form still refused.** A bash command that writes a Node script by heredoc whose body contains `fs.rmSync(<variable>)` (text being written to a file, not run) is refused ("script one-liner ... not a string literal"). The fix covered prose and non-interpreter heredocs only. Hit once; worked around with the Write tool.
12. **Round hygiene.** An untracked `docs/progress/gates10/HANDIN_FAILED.txt` is in the worktree: the builder's own hand-in was released after 3 refusals (status-gate wording on STATUS lines 42 and 81, and only 2 self-audit passes in FAULTS.md). The committed state now passes `check_handin --no-visual` (I ran it), but STATUS's first heading still says "Nothing skipped, refused or failed" although the real hook refused the hand-in 3 times. That belongs in the first lines of STATUS. FAULTS.md has 2 passes.

## Prior audit items (gates9 AUDIT, 11 faults): ruling on the builder's table

| # | Builder says | My ruling |
|---|---|---|
| 1 H3 depth bound | fixed | FIXED by code read and the guards9/10 junction tests (green); not live-attacked with a 7-deep junction by me. |
| 2 H7 bulk formatters | fixed | FIXED: find -exec, xargs, git ls-files, $(ls), astyle, sort -o, ed, sed file-before-script refused; ALLOW forms on unlocked files pass. |
| 3 patch, git apply, fs.rm, robocopy | fixed | PARTLY: patch and git apply with a file argument, robocopy filter and /MOV fixed. fs.rm only in bare form: RE-OPENED (fault 8). `patch --dry-run` false refusal (fault 9). |
| 4 item 8 text | in main | FIXED (d763801). |
| 5 backslash text | in main | FIXED. |
| 6 numbering | in main | FIXED, but the new text clashes with item 26: fault 10. |
| 7 A6 limit | fixed | FIXED (limit 2000, time in the message; 56/0). |
| 8 truncated PNG | fixed | FIXED (my image payloads above); reference-copy floor documented. |
| 9 warning channel | fixed | FIXED by code read (both channels emitted); not shown live that the agent sees it. |
| 10 temp prefix | fixed | FIXED by read; no stray folders left by my suite runs. |
| 11 backup_reference env | fixed | FIXED: 56/0 with CP_GATE_STATE_DIR set. |
| F heredoc and commit text | fixed | FIXED for prose and commit messages; real one-liners still refused. Edge: fault 11. |

## Test-truth table
| Group | Expected value from | Pins a fault? |
|---|---|---|
| guards10 launch ALLOW/REFUSE | real gate.log descriptions, but the ALLOW set omits "Write audit first-look rule into docs" | No; blind to fault 1 |
| guards10 measure/build good and bad | the brief's written rules | No; blind to faults 3, 4, 5 |
| guards10 first look | the brief's written rules | No; blind to fault 6 |
| guards10 C tables | gates9 audit payloads | No; fs.rm only in the bare spelling (fault 8) |
Fail-first file: not re-derived by me; the builder says 79 FAIL on main's tools.

## Verdict
Send back. Faults 1 and 2 are defects in the running system (a real clerk launch is refused; the build gate fails open on a real suite). Everything else in the brief is delivered, and all suites are green.
