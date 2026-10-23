## Skipped, refused or failed

References opened: 10 of 10 listed (the gates11 AUDIT.md and STATUS.md, tools/make_brief.mjs, tools/phase_lib.mjs, tools/check_handin.mjs, every file in tools/hooks, .claude/settings.json, docs/AGENT_CONTRACT.md, CLAUDE.md, the gate.log read through a scratch copy).

- WAIVER NEEDED FROM THE MAIN SESSION: the finish gate's waiver guard refuses two renamed tests in tools/hooks/test_hooks.mjs. Decision 3 reversed their expectation (a read-only review of another agent's work is now REFUSED as NO-CONTRACT), so the tests were rewritten, not weakened, under new names. I cannot grant the waiver myself (only a task-file line can). Keys: `allow_waiver deleted_gate_test:test_hooks.mjs:a read-only "review of docs/progress": allowed (the wording ` and `allow_waiver deleted_gate_test:test_hooks.mjs:a review that names no progress folder: allowed`. The replacement cases are in test_hooks.mjs ("a read-only review of docs/progress by someone else ... REFUSED by meaning", "a review of the hook scripts ... REFUSED by meaning", plus one new ALLOW for read-only research and diagnosis).
- Not run live: no real subagent launch went through the new launch refusal, the build phase check or the post hook. The evidence is the suites (real hook scripts fed real-shaped JSON) and a sweep of the live NO-CONTRACT descriptions. First real use is the test.
- NOT verified live: the shape of the PostToolUse and PostToolUseFailure payloads (`tool_use_id`, `error: "Exit code N"`, `tool_response`). The post hook reads them as the shapes in `time_guard_post_impl.mjs` assume; if the real payload differs, no end line is written and a build phase check refuses ("not on record"). That is the safe side but would block an honest builder. The first live build phase shows it.
- Deviation from decision 1: the second stage of the second-copy check (the path lookup through PowerShell Get-CimInstance) is bounded to 4 s, not 300 ms. PowerShell cold start measured 0.6-1.3 s on this machine. Stage 1 (tasklist, 230-660 ms) keeps the 300 ms bound, and only commands that already match a running image reach stage 2. Both fail open with a gate.log line. wmic does not exist here.
- Disclosure: during development one test call with no state dir wrote ONE live deny line (command `make -C build`, cwd under a temp `guards12-...` folder) to the main gate.log. Fixed (`tg` defaults to a scratch state dir; the suite asserts that no line naming its temp folder reaches the real log). The line is test noise, not a real launch.
- The build phase now needs a RECORDED run: a log typed or edited by hand, or written by a run of another worktree, is refused. Suites that run node tests must be started under `timeout N` too (node `test_*.mjs` count as limited jobs).
- Audits of things that are not hand-ins ("Audit repo organisation, read-only", "Audit the audit-gate tooling", "Audit gradelayer WorldCache layers") are now refused as NO-CONTRACT too (decision 3): they must go through `make_brief --audit`. "Earthworks method review (read-only)" is still allowed because it names no thing reviewed.
- Not applicable: no C++ changed, no MSVC or gcc build, no Unreal.
- The untracked `docs/progress/gates10/HANDIN_FAILED.txt` and `AUDIT_FAILED.txt` were not made by me; left alone.
- The brief said 131 live NO-CONTRACT descriptions; the log held 78 rows (77 unique). A re-copy at the end of the round found no new descriptions.

## Items

Section letters are those of `tools/hooks/test_guards12.mjs` (103 cases; `fail_first_test_guards12.txt`: 44 passed, 59 failed on the gates11 tools).

| Item | Status | Proving test |
|---|---|---|
| (a) cross-worktree false duplicate refusal | fixed | Section A: "ALLOW: the same image is running, but from ANOTHER worktree"; "REFUSE: both worktrees run it; only this worktree's path is named"; "ALLOW: the earlier run of this worktree has an end record"; "REFUSE: a BACKGROUND run counts until its process is gone". The check is per worktree: an open run record (start line, no end line) of this worktree, a running image, and that process's executable path or command line under this worktree's root |
| (b) forgeable suite_build.log | fixed | Section B: "a log TYPED BY HAND with a valid doctest summary and a Suite binary line FAILS"; "edited AFTER its run FAILS with a hash mismatch"; "written by a recorded run of ANOTHER worktree FAILS"; "end never recorded FAILS"; "ended BEFORE the last code commit FAILS"; exit code checks; the GOOD hand-in passes. Logs are made by `test_record_lib.logRun`, which drives the real Pre and Post hooks, never a hand-made record line |
| (c) launch refusal only for exact wordings | fixed | Section C: the four named descriptions refused; with AUDIT-OF or FIRSTLOOK-OF carriers allowed; sweep over `live_launches_23oct.json` (77 descriptions): 41 refused, every one a real audit or first look, no clerk, merger or research job refused. Four layers in `phase_marks.mjs:disguisedJob` (description head, review word plus a named thing, a reason that opens with the review job, a prompt line ordering a review of a progress folder) |
| (d) make_brief --phase from a worktree doubles roadworld-wt | fixed | Section D: from a worktree cwd, --phase build builds `<roadworld-wt>/<round>` and finds MEASURE.md |
| (e) `timeout N -- cmd`, `xargs ninja`, make, UBT | fixed | Section E: REFUSE for each without a limit, ALLOW with `timeout 900 -- ...`; the refusal names the way to add the limit; `timeout 0 --` is no limit |
| (f) empty STATUS Items table | fixed | This table |
| (g) cross-worktree ALLOW test and forged-log test | fixed | Section A (ALLOW) and Section B (forged log) above |

## Prior audit items

Gates11 AUDIT.md "Part 1" items 1-4 and "Faults in the builder's claims" items 1-6.

| Item | Status | Proof |
|---|---|---|
| P1.1 false duplicate refusal | fixed | Item (a) |
| P1.2 forgeable suite log | fixed | Item (b) |
| P1.3 launch refusal exact shapes | fixed | Item (c) |
| P1.4 make_brief doubled path | fixed | Item (d) |
| P1.gaps `--`, xargs, make, UBT | fixed | Item (e) |
| C1 "unfixable" limit stated wrongly | fixed | The limit is gone from FAULTS Residual; (a) is fixed by the path lookup |
| C2 build check tests used hand-made logs | fixed | test_guards10 and test_guards11 now make every log through `logRun` |
| C3 no two-worktree test | fixed | Item (g) |
| C4 empty Items table, "Three passes" without content | fixed | This STATUS and FAULTS.md with content |
| C5 phase gates and launch hook never run live | open, reason a | Cannot be run from inside a builder (no launch tool in a gates round); stated in the first lines; first real use is the test |
| C6 = P1.3 and P1.4 | fixed | Items (c) and (d) |

## Reference files opened

Case: gates11 audit and round | Opened: C:\Users\bgbg0\Documents\Programming\roadworld-wt\gates10\docs\progress\gates11\AUDIT.md, C:\Users\bgbg0\Documents\Programming\roadworld-wt\gates10\docs\progress\gates11\STATUS.md, C:\Users\bgbg0\Documents\Programming\roadworld-wt\gates10\docs\progress\gates11\FAULTS.md
Case: tools and hooks | Opened: C:\Users\bgbg0\Documents\Programming\roadworld-wt\gates10\tools\make_brief.mjs, C:\Users\bgbg0\Documents\Programming\roadworld-wt\gates10\tools\phase_lib.mjs, C:\Users\bgbg0\Documents\Programming\roadworld-wt\gates10\tools\hooks\time_guard_impl.mjs, C:\Users\bgbg0\Documents\Programming\roadworld-wt\gates10\.claude\settings.json
Case: contract and rules | Opened: C:\Users\bgbg0\Documents\Programming\roadworld-wt\gates10\docs\AGENT_CONTRACT.md, C:\Users\bgbg0\Documents\Programming\roadworld-wt\gates10\CLAUDE.md, C:\Users\bgbg0\Documents\Programming\roadworld-wt\gates10\tools\check_handin.mjs

## Own fault list and residual

Three passes in FAULTS.md; Residual lists only what code cannot fix.

## Gate runs on made-up good and bad hand-ins

Each row is a made-up fixture built inside `test_guards12.mjs` (temp dir, own git repo, hooks run as processes); no live hand-in was touched.

| Gate | Made-up bad hand-in | Result | Made-up good hand-in | Result |
|---|---|---|---|---|
| build, suite log | typed log with a valid summary; log edited after its run; log of another worktree's run; no end record; run before the last code commit; exit 1 with no failing test; unknown exit code | each refused | log written by a run recorded by the Pre and Post hooks, exit 0, summary present | passes |
| build, allowed failure | failing run (exit 1) with a failure outside the brief's ALLOW-FAIL list | refused | exit 1 with only the allowed failure | passes |
| launch | "Auditing gates9", "Second-opinion reviewer of gates11", "Opus look at grade-r12 images", "Independent verification of gates9" as NO-CONTRACT | refused | the same with AUDIT-OF or FIRSTLOOK-OF carriers; clerks, mergers, research | allowed |
| time guard | make, `timeout 900 -- ninja` without a limit, xargs ninja, UBT, a node suite with no limit; a second copy of this worktree's own running test | refused | the same with `timeout 900`; the same image running from another worktree | allowed |
| make_brief | n/a | n/a | --phase build run from a worktree cwd | finds MEASURE.md |

## Changes (file:function)

- tools/hooks/time_guard_impl.mjs: `walk` (xargs, `--`), `classify` (make family, UBT cap 5400, node suites), `normP`, `processPaths`, `underRoot`, `openRuns`, `recordRun`, `outputFiles`, `main` (per-worktree duplicate check, start line with id, root, files).
- tools/hooks/time_guard_post.mjs, time_guard_post_impl.mjs: new (`exitCodeOf`, `sha256File`; end line with exit and hashes).
- tools/hooks/suite_record.mjs: new (`findSuiteRun`).
- tools/phase_lib.mjs: `buildProblems` uses `findSuiteRun` and the exit-code checks.
- tools/hooks/phase_marks.mjs: `disguisedJob` by meaning.
- tools/make_brief.mjs: `wtParent`, `mainRoot`; build PHASE_TEXT.
- .claude/settings.json: PostToolUse and PostToolUseFailure `time_guard_post.mjs` (Bash and PowerShell).
- tools/clean_test_temp.mjs: prefix `guards12-`.
- Tests: tools/hooks/test_guards12.mjs, test_record_lib.mjs, live_launches_23oct.json (new); test_guards10, test_guards11, test_hooks adapted (build logs through `logRun`; three audit-of-non-hand-in cases and two review launches flipped to REFUSE).

## Images that carry each claim

No images in this round.

| Claim | Image file (opened by me) |
|---|---|
| none (tooling) | evidence is the runs in "Suite and checks" |

## Suite and checks

Each run alone under `timeout 600`, `CP_GATE_STATE_DIR` set to a scratch folder, final committed state.

test_hooks 413/0, guards6 32/0, guards7 28/0, guards8 56/0, guards9 54/0, guards10 107/0, guards11 171/0, guards12 103/0, test_gates 514/0, test_worktree_tools 27/0, test_backup_reference 56/0. Fail-first: `fail_first_test_guards12.txt` (44 passed, 59 failed on the gates11 tools).

## Merge text (for the merger; main not edited by me)

Contract item **27** (after item 26, time limits):

> 27. **Phases.** A builder round may be split into short fresh agents run in sequence: `measure` (opens every reference, writes MEASURE.md), `build` (code and tests, runs the suite under `timeout 900` into `suite_build.log`, writes BUILD.md), `render` (images and composites). Each hands over through a file in the hand-in folder and is checked with `check_handin.mjs --phase measure|build`. The build check accepts a suite log only if the hooks recorded the run that wrote it (same worktree, same file hash, ended after the last code commit); a typed or edited log is refused. The main session splits every look round this way; an auditor re-runs the suite itself.

CLAUDE.md line (after the "6." anchor in "Two gates"): `7. Look rounds run as measure / build / render phases (contract item 27); no suite runs inside a hook, agents run it under a time limit (item 26, enforced by tools/hooks/time_guard.mjs, which also records each run for the build phase check).`

Also merge: the `.claude/settings.json` PostToolUse and PostToolUseFailure entries for `time_guard_post.mjs`, and `guards12-` in clean_test_temp. Main was not edited by me.

## Next step

Auditor: re-run the suites, run one real build phase to see the live Post payload, and attack the path lookup (renamed worktree folders, junctions).
