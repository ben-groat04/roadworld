## Skipped, refused or failed

References opened: 10 of 10 listed (gates9 AUDIT.md, gates9 STATUS.md, tools/make_brief.mjs, tools/check_handin.mjs, every file in tools/hooks, .claude/settings.json, docs/AGENT_CONTRACT.md, every file in docs/templates, CLAUDE.md, the gate.log read through a scratch copy; the gates10 AUDIT.md, STATUS.md and FAULTS.md were read as well).

- Round 10's own hand-in was NOT accepted cleanly: it was released after 3 refusals of the finish gate (status-gate wording on STATUS lines 42 and 81, and only 2 self-audit passes in FAULTS.md), which left `docs/progress/gates10/HANDIN_FAILED.txt`. Its STATUS still said "Nothing skipped, refused or failed". That was wrong and is stated here (audit item 12). The untracked `HANDIN_FAILED.txt` and `AUDIT_FAILED.txt` in docs/progress/gates10 were not made by me and I left them alone.
- Not run live: no real subagent launch went through the new launch refusal, the phase checks or the first-look check. The evidence is the test suites, 34 scratch cases for the time guard and a sweep of the live NO-CONTRACT descriptions. First real use is the test.
- Not applicable: no C++ changed, so no MSVC or gcc build was run. No Unreal work (the three Unreal PowerShell scripts got only a `-TimeoutSec` parameter and were not executed).
- Known limits (not faults I could fix by code):
  - the duplicate-run check matches by image name, not path, so a test running in another worktree can refuse a start for up to that run's time limit;
  - the process lookup is bounded to 300 ms and fails open with a gate.log line;
  - paths built from variables or `$(...)` cannot be seen by lock_guard (stated in its header);
  - an agent can open a first-look image without looking at it; the main session's own look stays.
- No write was denied to me during the round.

## Items

| Item | Status | Proof |
|---|---|---|

## Prior audit items

Twelve faults of docs/progress/gates10/AUDIT.md, all fixed. "Section X" means the section letter in `tools/hooks/test_guards11.mjs`; the fail-first file is `fail_first_test_guards11.txt` (44 passed, 127 failed on the round-10 tools, 171/0 now).

| Item | Status | Proof |
|---|---|---|
| 1 launch hook refuses a real clerk ("Write audit first-look rule into docs") | fixed | Refusal now by description regex `^(auditor\|audit(ing)? (of\|for)\|opus first look\|first look)\b` or a prompt naming a progress folder to audit; AUDIT-OF and FIRSTLOOK-OF carriers pass. Section B; live sweep of all 76 NO-CONTRACT descriptions in the gate.log: 38 refused, all real audits or first looks, no false refusal |
| 2 build phase runs the suite inside a hook that is killed at 270 s | fixed | No suite runs in any hook. Agent runs it under `timeout 900` into `suite_build.log`; phase_lib checks log newer than the last code commit, a real doctest or ctest summary, failures only in the brief's ALLOW-FAIL list, binary newer than the last code commit. Audit brief template tells the auditor to re-run (docs/templates/AUDIT_BRIEF.md). Section C |
| 3 build with no code commit passes | fixed | Refused unless a commit touches code or tests outside the hand-in folder. Section C |
| 4 fail_first not checked for failure | fixed | Each fail_first file must show a failing case in the runner format (doctest, ctest, repo node runners, `N passed, M failed`). Section C |
| 5 measure check never checks photos | fixed | Photo files must exist, M must equal the brief's REFS count, one table per case (brief emits REFS and CASE). Section C and the make_brief emission test |
| 6 first-look checks shallow | fixed | Placeholders and repeated words fail; a reference photo under docs/reference (not _compare) and a round picture are both required; quoted paths with spaces work. Section D |
| 7 `[unreal]` unverifiable | fixed | Frame must be in the round's _compare or hand-in folder, cited in STATUS, with a playtest log newer than the brief that names the frame (`unrealFrameProblems` in tools/gates_lib.mjs). Section E |
| 8 lock_guard evasions | fixed | Chained `require('fs').rm/rmSync/unlinkSync/renameSync/copyFileSync/truncateSync`, `import('fs').then(...)`, git stash (push, save, bare, pop, apply, branch), reset --hard, checkout --, revert covered. Section F |
| 9 `patch --dry-run` refused | fixed | Allowed (also --check, -C; a diff piped from cat is read). Section F ALLOW cases |
| 10 contract item number clash | fixed | New rule is item 27; item 26 stays the time-limits rule. See merge text below |
| 11 delete_guard heredoc | fixed | A heredoc body feeding an interpreter, or a written file run later in the same command, is judged as code; plain text is allowed. Section G (ALLOW and REFUSE) |
| 12 round hygiene, STATUS honesty | fixed | Plain statement under the first heading above |

Decision 13 (run time-limit guard, contract item 26 made mechanical): new `tools/hooks/time_guard.mjs` plus `time_guard_impl.mjs`, wired in `.claude/settings.json` for Bash and PowerShell. Refuses sim_core_tests, ctest, cmake --build, ninja, msbuild, Run-Playtest.ps1, Build-Editor.ps1, Run-EditorPython.ps1, scene.mjs, render.mjs, `*_tests.exe`, `t_full*.exe` without GNU `timeout N` (N <= 900), the script's own `-TimeoutSec` (caps: Build-Editor 5400, Run-Playtest 1800, Run-EditorPython 1800) or Start-Process plus Wait-Process -Timeout. `timeout` typed in the PowerShell tool does not count. Mentions via grep, cat, git log and quick forms (--help, --list-test-cases, ctest -N) are allowed. A second copy of a running test is refused; the process lookup is bounded to 300 ms and fails open with a log line. Tests: section A.

## Reference files opened

Case: gates10 audit and round | Opened: C:\Users\bgbg0\Documents\Programming\roadworld-wt\gates10\docs\progress\gates10\AUDIT.md, C:\Users\bgbg0\Documents\Programming\roadworld-wt\gates10\docs\progress\gates9\AUDIT.md, C:\Users\bgbg0\Documents\Programming\roadworld-wt\gates10\docs\progress\gates9\STATUS.md
Case: tools and hooks | Opened: C:\Users\bgbg0\Documents\Programming\roadworld-wt\gates10\tools\make_brief.mjs, C:\Users\bgbg0\Documents\Programming\roadworld-wt\gates10\tools\check_handin.mjs, C:\Users\bgbg0\Documents\Programming\roadworld-wt\gates10\tools\hooks\lock_extra.mjs, C:\Users\bgbg0\Documents\Programming\roadworld-wt\gates10\.claude\settings.json
Case: contract and rules | Opened: C:\Users\bgbg0\Documents\Programming\roadworld-wt\gates10\docs\AGENT_CONTRACT.md, C:\Users\bgbg0\Documents\Programming\roadworld-wt\gates10\CLAUDE.md, C:\Users\bgbg0\Documents\Programming\roadworld-wt\gates10\docs\templates\STATUS.md

## Own fault list and residual

Three passes in FAULTS.md; Residual lists only what code cannot fix.

## Gate runs on made-up good and bad hand-ins

Each row is a made-up fixture built inside `test_guards11.mjs` (temp dir, own git repo); no live hand-in was touched.

| Gate | Made-up bad hand-in | Result | Made-up good hand-in | Result |
|---|---|---|---|---|
| measure | MEASURE.md naming photos that do not exist, M differing from the brief, one table for two cases | refused | real photo files, M equals REFS, one table per case | passes |
| build, no code commit | BUILD.md with only the BUILD.md commit | refused | one commit touching a source file | passes |
| build, suite log | log older than the code commit; no summary; failure outside ALLOW-FAIL; binary older | each refused | fresh log with a passed summary and a fresh binary | passes |
| fail_first | text "a passing run, nothing failed" | refused | runner-format `9 passed, 2 failed` | passes |
| first look | repeated-word placeholder; image not under docs/reference; no round image | refused | named reference photo plus a round picture, real top 3 | passes |
| unreal frame | frame outside _compare, not cited, playtest log older than the brief | refused | frame in _compare, cited, log newer and naming it | passes |
| launch | description "Auditor for round x", "Opus first look ..." | refused | "Write audit first-look rule into docs" and AUDIT-OF carriers | allowed |
| time guard | sim_core_tests with no limit, a second copy of a running test | refused | `timeout 600` runs, `git log -- sim_core_tests`, Start-Process plus Wait-Process -Timeout | allowed |
| lock_guard | chained fs.rmSync, git stash, reset --hard on a locked test | refused | patch --dry-run, unlocked edits | allowed |
| delete_guard | heredoc feeding node with fs.rmSync(var) | refused | heredoc prose, plain file write | allowed |

## Changes (file:function)

- tools/hooks/time_guard.mjs, time_guard_impl.mjs: new (`judge`, `jobsIn`, `processRunning`, `recentRuns`).
- .claude/settings.json: time_guard in the Bash and PowerShell hooks.
- unreal/tools/Build-Editor.ps1, Run-Playtest.ps1, Run-EditorPython.ps1: `-TimeoutSec`.
- tools/hooks/phase_marks.mjs: `disguisedJob`, `placeholderWhy`, `openedPaths`, `checkFirstLookFile`.
- tools/hooks/brief_gate_impl.mjs, handin_gate_impl.mjs: matched to the above, no suite inside a hook.
- tools/phase_lib.mjs: measure and build checks. tools/gates_lib.mjs: `unrealOriginProblems`, `unrealFrameProblems`. tools/check_handin.mjs: --refs, --case, --allow-fail.
- tools/make_brief.mjs: emits REFS, CASE, ALLOW-FAIL; reads the task file section "## Allowed failures".
- docs/templates/AUDIT_BRIEF.md: re-run the suite.
- tools/hooks/lock_guard_impl.mjs: `normalizeFsRefs`, `ANY_RECV`, `judgeGitHistory`, `porcelainPath`. lock_extra.mjs: patch dry run.
- tools/hooks/delete_guard_impl.mjs: `heredocInterpreter`, `writtenTargets`, `runsAnyOf`.
- tools/clean_test_temp.mjs: prefix `guards11-`.
- Tests: tools/hooks/test_guards11.mjs (new), test_guards10.mjs (build tests adapted), test_hooks.mjs (file list).

## Images that carry each claim

No images in this round.

| Claim | Image file (opened by me) |
|---|---|
| none (tooling) | evidence is the runs in "Suite and checks" |

## Suite and checks

Each run alone under `timeout 600`, `CP_GATE_STATE_DIR` set to a scratch folder; no test wrote the live gate.log.

test_hooks 412/0, guards6 32/0, guards7 28/0, guards8 56/0, guards9 54/0, guards10 107/0, guards11 171/0. Fail-first: `fail_first_test_guards11.txt` (44 passed, 127 failed against the round-10 tools); `fail_first_test_guards10_build.txt` (104 passed, 3 failed: the adapted build tests).

## Merge text (for the merger; main not edited by me)

Contract numbering: the new rule is item **27**, after the existing item 26 (time limits). Text:

> 27. **Phases.** A builder round may be split into short fresh agents run in sequence: `measure` (opens every reference, writes MEASURE.md), `build` (code and tests, runs the suite under `timeout 900` into `suite_build.log`, writes BUILD.md), `render` (images and composites). Each hands over through a file in the hand-in folder and is checked with `check_handin.mjs --phase measure|build`. The main session splits every look round this way; an auditor re-runs the suite itself.

CLAUDE.md line (after the real "6." anchor in "Two gates"): `7. Look rounds run as measure / build / render phases (contract item 27); no suite runs inside a hook, agents run it under a time limit (item 26, enforced by tools/hooks/time_guard.mjs).`

Also merge: the .claude/settings.json time_guard entry (Bash and PowerShell), the three `-TimeoutSec` parameters, `guards11-` in clean_test_temp.

## Next step

Auditor re-runs the suites and attacks the time guard (other-worktree false refusals, forms the lexer misses) and the new launch regex against fresh gate.log descriptions.
