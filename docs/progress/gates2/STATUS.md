## Skipped, refused or failed

- Proof (ii) used my own gate lines (`scene_check >= 20/21 ...`, `ctest failed_assertions == 2`) because the roadtool3, tjunction and roundabout briefs carried no Gates section; the table says "would have said", not "did say". For roadtool3 the ctest line is not meaningful (measured 0 against `== 2`: a round that fixes both known failures is refused by `==`; use `<=` in real task files, see rule text below).
- roadtool3 scene check: FAIL-OPEN (no `prototypes/road-draw/node_modules` in that worktree), so no scene number for it. tjunction and roundabout did measure 20/21.
- roundabout: no ctest result in its worktree (no `build*/Testing/Temporary/LastTest.log`), so the suite gate refuses with "build and run ctest".
- Side effect: running the scene gate against the old tjunction and roundabout worktrees ran `node build.mjs`, which regenerates the tracked `prototypes/road-draw/index.html` there (shows as modified in `git status` of both). Not reverted: I cannot tell my regeneration from the agents' own. Generated file only.
- Round 9 pixel stats: round 9 handed in no `.stats.json`. I re-rendered round 9's s4 and s6 views with this branch's `render.mjs` from the original mesh exports (`%TEMP%\structures-r9-export`) into `..\roadworld-scratch\gates2\r9`; the numbers come from a re-render with the current viewer, not from the hand-in's own PNGs. The 727680 debug-pixel figure for s4 aerial equals the earlier test constant.
- Nothing pushed or merged (as briefed). No Unreal run. `.claude/settings.json` in this worktree unchanged (hook timeout 300 s already exceeds the 270 s gate timeout).
- Contract item 8 usage wording: already deleted on main (b408633), nothing to propose. `AGENT_CONTRACT.md` item 16 (audit rule) is main's, merged.
- Partly read only: tools/hooks/test_hooks.mjs, audit_lib.mjs, notice_lib.mjs, gate_ack.mjs, gate_notice.mjs, contract_inject.mjs and scene.mjs (heads, greps and running them: 264 hook tests pass); the auditgate AUDIT.md (first 3000 characters).

References opened: 12 of 12 listed (items on the hook folder, scene.mjs and the auditgate AUDIT.md read partly, as listed above)

## Reference files opened

Case: Gate design evidence (round 9 hand-in and audit gate audit) | Opened: C:\Users\bgbg0\Documents\Programming\roadworld-wt-grade-r9\docs\progress\2026-10-11-structures-r9\AUDIT.md, C:\Users\bgbg0\Documents\Programming\roadworld-wt-grade-r9\docs\progress\2026-10-11-structures-r9\STATUS.md, C:\Users\bgbg0\Documents\Programming\roadworld-wt-grade-r9\docs\progress\2026-10-11-structures-r9\FAULTS.md, C:\Users\bgbg0\Documents\Programming\roadworld-wt-auditgate\docs\progress\2026-10-11-auditgate\AUDIT.md
Case: Gate code and rules | Opened: C:\Users\bgbg0\Documents\Programming\roadworld\tools\check_handin.mjs, C:\Users\bgbg0\Documents\Programming\roadworld\tools\make_brief.mjs, C:\Users\bgbg0\Documents\Programming\roadworld\docs\AGENT_CONTRACT.md, C:\Users\bgbg0\Documents\Programming\roadworld\docs\templates\BRIEF.md, C:\Users\bgbg0\Documents\Programming\roadworld\docs\templates\STATUS.md, C:\Users\bgbg0\Documents\Programming\roadworld\CLAUDE.md

## Diagnosis check (main session's six points against the files)

All six hold. Corrections and additions: (1) round 9's COMPARE.md also used the 5-column layout and the hand-in passed without a single stats file, so "no picture measurement exists at all" is the fuller statement; (3) the round 9 auditor says s1, s2 and s8 renders were not judged by anyone; (4) the builder's admissions (unattempted items, a usage reason) sat in the first lines of STATUS and were visible to a reader but to no gate; (6) true, but gate E only acts on numbers a task file lists under "## Rounds", so a topic with no recorded numbers is not caught.

## What was built (this branch, tools/ plus the viewer stats)

- A measured gates: tools/gates_lib.mjs (parseGateLine, runMeasuredGates, measureSceneCheck = `node build.mjs` + `scene.mjs --check`, measureCtest = newest build*/Testing/Temporary/LastTest.log, refused when older than the sources or a subset run). make_brief.mjs reads "## Gates", emits `GATES-VERSION: 2` and `GATE:` lines; check_handin.mjs (flags `--gates-version 2 --gate "<line>"` or `--task <file>`) measures. Below floor is refused; below target but at or above floor passes only if the STATUS first section names the metric.
- B waiver guard: waiverFindings and collectWaiverDiff diff tests/, scene.mjs, test-geom.mjs against the merge base: should_fail, skip, may_fail, WILL_FAIL, WARN, growth of the is_rough() list, more GRADE_CHECK(is_rough), loosened tolerance constants or thresholds, fewer CHECK lines, `#if 0`, deleted or renamed test cases and scene checks, a test source dropped from CMake, hard-coded `pass: true`. Allowed only by `allow_waiver <key>` in the task file. It cannot see: a waiver in a file outside those paths (sim_core), a test made vacuous without removing a line, a widened input range, an expected value edited in a data file, behaviour waived by editing the code under test.
- C picture sanity: mesh-view.js statsPass (flat-colour pass) and render.mjs write `<view>.stats.json` (road, marking, ground and debug pixels, width, height, png_sha1, diagnostic). pictureProblems refuses: no road (under 1 %), debug colour (over 0.1 %), a missing record for COMPARE.md renders and for every `*_aerial.png` / `*_street.png`, a record that does not match the PNG hash, a diagnostic view listed in COMPARE.md, more than 40 % diagnostic views.
- D statusProblems: wording that says an item was not even tried, fewer than 3 non-empty passes without a "Residual only unfixable: ..." line, empty Pass headings, usage or time-limit reasons.
- E method rule: make_brief.mjs demands "## Rounds" when "Previous round:" is named; three rounds with no improvement print METHOD REVIEW FIRST (exit 1) unless `--override "<20+ chars>"` (logged to gate.log).
- F audit-gate faults (agent/auditgate merged and fixed; tools/hooks/audit_lib.mjs, handin_gate.mjs, notice_lib.mjs): the auditor id comes from the harness and is recorded by the finish gate (a made-up id in the file does not clear the owed audit); `--accept` requires verdict accept; a resumed builder's later passing hand-in renews passed_at so the old audit no longer counts; no builder record falls back to the gate log, else refuses. The 8-column COMPARE rule is versioned by `GATES-VERSION: 2` in the brief; briefs without it keep the 5-column rule.
- G scope: make_brief.mjs refuses more than 3 cases without a "## Scope" section (20+ chars) and the BRIEF template says so. Cases saying "no renders" without `--no-visual` are refused.
- Fail open: gates catch internal errors, print and log `FAIL-OPEN`; `--accept` is the one place where a gate that could not run is a refusal.
- This round: narrowed the regex for limit-type reasons (no false hits on an ordinary time limit or quota), added a deferral phrase, four tests.

## Proof table

(i) Real round 9 folder (roadworld-wt-grade-r9, docs/progress/2026-10-11-structures-r9), gates `scene_check >= 20/21 (baseline 20/21, target 21/21)` and `ctest failed_assertions == 2`: REFUSED, 39 items, among them:
- scene check measured 17/21 on the wrong side of >= 20/21, failing ground_under_road, median_flat, ground_hidden_by_walls, no_bare_steps (the number typed in STATUS is not read).
- waiver guard: `is_rough() waiver list gained "over_height_cliff"` and `"stress_gradient_085"`, more GRADE_CHECK(is_rough) wrappers (tests/test_grading.cpp).
- STATUS: undone-item lines (4, 6, 33), a two-pass self-audit with a limit given as the reason (line 7), the same reason in FAULTS.md line 43.
- 5-column COMPARE.md refused; 24 views without a stats record.
- Pixel stats from the re-render: s4_retaining_wall_run_aerial 50.53 % debug colour (needs 397719 px, rock 329961 px); s4 street 23.51 %; s4 end 42.61 %; s6_embankment_street road 0.00 %, s6_embankment_side_close 0.00 %, s6_embankment_along 0.35 % ("the subject is not in the picture"). s6_embankment_aerial passes (2.0 % road).

(ii) and (iii):

| Hand-in | What gates v2 would say |
|---|---|
| roadtool3 (no-visual) | FAIL-OPEN on the scene check (no node_modules); waiver guard: test case "EditSession: a drag across another road at the same level is allowed and advised: an edit makes no junction" deleted; STATUS line 6 undone item without a tried/blocked-by statement; my `== 2` ctest line does not apply (0 measured) |
| tjunction | scene 20/21 below target 21/21 and STATUS does not name it; waiver guard: 1 fewer CHECK line in tests/test_markings.cpp; 7 views without stats; 8-column COMPARE missing; the STATUS B3 line (an item with no attempt) |
| roundabout | scene 20/21 not named; no ctest result in the worktree; waiver guard: threshold loosened `far_bar <= island * 1.6 + 1.5` to `2.0 + 1.5`, test case "markings geometry: every hatching bar endpoint lies on a boundary line (2 cm)" deleted; 5 views without stats; 8-column COMPARE missing; an undone-item line |
| Made-up good hand-in (tools/test_gates.mjs end-to-end tests) | PASSES: scene 21/21 measured, clean waiver diff, road in frame, no debug colour, 3 passes, no unattempted items |
| Old-layout brief (no GATES-VERSION; the running agents) | old checks only; 5-column COMPARE still passes; 3/21 on disk does not block it (hook-level tests) |
| Auditor brief with no HANDIN_DIR | allowed (hook tests) |

## Own fault list and residual

FAULTS.md: Pass 1, Pass 2, Pass 3 and Residual.

## Changes (file:function)

tools/gates_lib.mjs (new): parseGateLine, runMeasuredGates, measureSceneCheck, measureCtest, waiverFindings, collectWaiverDiff, pictureProblems, statusProblems, methodChangeProblem, parseRounds. tools/check_handin.mjs: v2 section and --accept. tools/make_brief.mjs: Gates, Rounds, Scope, no-renders refusal, --override, --audit. tools/hooks: audit_lib (new), handin_gate, brief_gate, notice_lib, gate_ack. prototypes/mesh-view: mesh-view.js statsPass, render.mjs stats record. docs/templates: BRIEF.md, AUDIT.md, AUDIT_BRIEF.md.

## Proposed rule-text changes (not made on main)

- CLAUDE.md "Before accepting a report": add "run `check_handin.mjs <folder> --accept` (it needs a recorded builder and an accept verdict)".
- Task files for rounds with a known failing suite use `ctest failed_assertions <= 2`, not `== 2`.
- Task files list "## Rounds" with numbers so gate E has something to compare.

## Images that carry each claim

| Claim | Image file (opened by me) |
|---|---|
| s4 aerial is half debug colour | C:\Users\bgbg0\Documents\Programming\roadworld-scratch\gates2\r9\s4_retaining_wall_run_aerial.png (numbers from its .stats.json; the picture itself not opened by me) |
| s6 street has no road in frame | C:\Users\bgbg0\Documents\Programming\roadworld-scratch\gates2\r9\s6_embankment_street.png (numbers only, not opened) |

## Suite and checks

node tools/test_gates.mjs: 148 passed, 0 failed. node tools/hooks/test_hooks.mjs: 264 passed, 0 failed (one early run showed 263 passed, 1 failed; it did not repeat in 11 further runs and the failing line was not captured, so the flake is unidentified). No C++ build (engine untouched).

## Next step

Main session: a fresh auditor writes AUDIT.md, then merge agent/gates2 and put "## Gates" lines (with baselines) and "## Rounds" into the next earthworks task file.
