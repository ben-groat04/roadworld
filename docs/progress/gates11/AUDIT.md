Auditor agent: ac1408ae90c73201d
Verdict: send back
References opened: 10 of 10

(Opened: gates9 AUDIT.md and STATUS.md, make_brief.mjs, check_handin.mjs, hooks folder (time_guard*, brief_gate_impl, phase_marks, phase_lib read in full; others skimmed or run), .claude/settings.json (time_guard wiring), AGENT_CONTRACT.md, templates folder, CLAUDE.md sections, live gate.log (read only, descriptions swept). Skipped: nothing. Not run live: no real subagent launch.)

## Part 1: own fault list, written before STATUS.md and FAULTS.md

Suites, each under `timeout 600` with CP_GATE_STATE_DIR in my scratch folder, live gate.log never written: test_guards11 171/0, guards10 107/0, guards9 54/0, guards8 56/0, guards7 28/0, guards6 32/0, test_hooks 412/0, test_gates 514/0, test_worktree_tools 27/0, test_backup_reference 56/0 (the gates9 CP_GATE_STATE_DIR failure is fixed). The three Unreal .ps1 files parse with 0 errors.

Attacks on time_guard (62 command lines through `judge`, then the real hook process with stdin JSON): refuses/allows correctly for bare and piped runs, `cd x &&`, `( )`, `{ }`, if/for bodies, `env`, `nohup`, `exec`, `command`, quoted .exe path, `bash -c`, nested `timeout 900 bash -c`, `timeout -k`, `15m`/`1h`/`1000`, PowerShell `Start-Process`+`Wait-Process`, `Invoke-Expression`, `-File`, `timeout` typed in PowerShell; mentions (grep, git commit -m, cat, `ctest -N`) pass. Hook run time 0.15-0.45 s. Lookup timeout writes FAIL-OPEN to gate.log (checked). Gaps are only exotic: `xargs ninja`, `timeout 900 -- ninja` (the `--` makes it see no job: passes), `make`/Build.bat/UnrealBuildTool/RunUAT not covered.

1. (worst) False duplicate refusal when another worktree runs the same image. Real hook, own worktree's earlier run already finished, CP_TIME_GUARD_PROCS="sim_core_tests" (stands for another agent's run in another worktree): refused with "a copy ... started 0 s ago from this worktree ... is still running". The message is false (it names this worktree), and this machine runs tests in several worktrees at once, so any second filtered run (`-tc=a` then `-tc=b`) within the first run's limit window (up to 900 s) is refused while anyone else has a suite open. The builder lists this as "unfixable"; it is not (get the image's path or command line, e.g. `Get-CimInstance Win32_Process` / `wmic ... ExecutablePath,CommandLine`, within the same 300 ms bound with fail-open, or only count the run as live if the record's own child still exists). Refusals nobody can satisfy push agents to script files, which the guard cannot see.
2. Build-phase suite result can be forged. phase_lib only reads suite_build.log: a typed `[doctest] test cases: 5 | 5 passed | 0 failed` line plus a `Suite binary: x.mjs` line (script paths are exempt from the mtime test) passes. Decision/brief wording was "run by the check itself, not quoted"; the builder moved it to "auditor re-runs" because of the 270 s hook kill. The deviation is stated in code and STATUS prior-item 2 but the forged-log hole is not listed as a limit. Fix: have the check run a bounded re-run (backgrounded with timeout) or verify the log against the binary's own output (e.g. require the binary to print a build id/hash the log must contain).
3. Launch refusal covers only the exact shapes in the log. "Auditing gates9", "Second-opinion reviewer of gates11", "Opus look at grade-r12 images", "Independent verification of gates9" pass as NO-CONTRACT (checked with disguisedJob on 14 made-up descriptions; no false refusal on any clerk, merge or research one, 44 of 131 live descriptions refused, all real audits/first looks, matching the builder's sweep). Low risk because the audit stays owed, but the owner's "have you been sidestepping it" is exactly this hole.
4. make_brief --phase build/render run from a worktree builds the path `roadworld-wt\roadworld-wt\<round>` (line 132: dirname(root)+'roadworld-wt' when root already is inside roadworld-wt), so "MEASURE.md does not exist" is reported for a file that exists. Works from the main checkout only; the contract says briefs are made from main, so low.

## Faults in the builder's claims and tools, worst first

1. See Part 1 item 1: STATUS "Known limits" and FAULTS "Residual" call the cross-worktree false refusal unfixable, and the 300 ms lookup (tasklist, ~0.36 s measured end to end incl. node start-up) leaves room for a path lookup. Contract item 17: not an allowed open reason.
2. See Part 1 item 2 (forgeable suite log). Tests for the build check use hand-made logs, so they cannot catch it.
3. test_guards11 coverage of the dup rule uses CP_TIME_GUARD_PROCS which is global, so no test shows two worktrees; add the cross-worktree case as REFUSE-must-not (ALLOW) test once fixed.
4. STATUS "Items" table is empty (header only) and the section "Own fault list and residual" says "Three passes" without content; the Gate-runs table is real and matches what the suites exercise (I re-ran the suites, not each row by hand).
5. Part 3 of the brief (phase gates) and the launch hook were never exercised by a live launch; stated honestly in STATUS first lines. Accepted as stated; first real use should be watched.
6. Items 3 and 4 of Part 1.

Fail-first files: fail_first_test_guards11.txt claims 44 passed / 127 failed on round-10 tools; I did not rebuild that, but the 171/0 now and the structure of new cases fit it. gates9 faults: all eleven were checked against the suites above and the live behaviour I tried (delete_guard heredoc, patch --dry-run, LOCK paths, temp prefix, backup test) and held; none re-opened.

## For the next builder

Fix 1 and 2 (real fixes, no waiver), add tests for the cross-worktree case and the forged log, fix 3 (inflected "Auditing", reviewer, "look at ... images" forms) and 4, fill the empty STATUS Items table, handle `timeout N -- cmd` in time_guard.
