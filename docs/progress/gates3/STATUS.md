## Skipped, refused or failed

BLOCKED: proof row for the roadtool3 hand-in with the final code | tried: ran check_handin on the roadtool3 hand-in twice (before and after the ctest and status fixes) and once more with the final code | blocked by: the worktree C:\Users\bgbg0\Documents\Programming\roadworld-wt-roadtool3 was removed between the runs (merged and cleaned up: the directory does not exist now), so the last run read an empty path; its scene gate had also no node_modules (FAIL-OPEN logged) | needs: a copy of the merged docs/progress/roadtool3 folder, or the worktree restored

- REFUSED by the live finish hook at hand-in: "COMPARE.md is missing". The live hook (main checkout, the older code) reads `HANDIN: no-visual` only from the prompt when the prompt itself holds HANDIN_DIR; this round's prompt held HANDIN_DIR and kept the no-visual line in the BRIEF-FILE. I did not add a placeholder COMPARE.md (the very thing this round forbids). The defect is fixed in this branch (`handin_gate_impl.mjs` reads the BRIEF-FILE whenever the prompt names one; test part 4); the live hook is not touched, so this hand-in may be released after its three refusals with HANDIN_FAILED.txt written: that release is this defect, not an unchecked hand-in (the check passes with `--no-visual`).
- Nothing pushed or merged (as briefed). No Unreal run, no C++ build; the C++ sources were not changed. The live hooks in the main checkout and `.claude/settings.json` were not changed (the hook timeout there, 300 s, is above the 270 s spawn limit the gates use; the scene gate now needs at most 190 s).
- Unverified: the hooks ran with synthetic stdin only (the shape the live harness sends was confirmed by the gates2 audit, items a-e of its live results); nothing here ran under a real Claude Code session.
- Unverified: mesh-view `render.mjs` was not run this round (its stats writer is the gates2 one); `scene.mjs` now writes the same record and was run for 15 views, its record read by `pictureProblems` (cross_slope 23.9 % road passes; overview 0.31 % road is refused as a non-diagnostic view).
- The picture thresholds (1 % road, 0.1 % debug colour) have no photo measurement behind them; the reason and the round 9 numbers they sit between are in the comment above `pictureProblems`.
- Limits kept on purpose, with the reason in FAULTS.md Residual: items 1 to 5 there (typed record with a matching varied PNG; a progress folder deleted everywhere ends the audit debt; a replaced secret; vacuous tests; phrase-list status reader).
- Contract wording is proposed below, not applied: rule files on main were not edited.
- Test timing: `node tools/test_gates.mjs` takes about two minutes (it runs the real scene check once and a git repo per group); `node tools/hooks/test_hooks.mjs` about 40 s.

References opened: 10 of 12 listed
not opened: C:\Users\bgbg0\Documents\Programming\roadworld\tools\hooks (test_hooks.mjs was read in parts with long lines cut at 200-330 characters; the other eight files were read whole)
not opened: C:\Users\bgbg0\Documents\Programming\roadworld\prototypes\road-draw\tools\scene.mjs (lines 335-667, the 21 invariants, were read with lines cut at 120-170 characters; every check's logic was read, the tails of its longest lines were not)

## Prior audit items

The previous AUDIT.md is docs/progress/gates2/AUDIT.md ("For the next builder", 16 items). Every row names the test or run that proves it; test names are quoted from `node tools/test_gates.mjs` (TG) or `node tools/hooks/test_hooks.mjs` (TH).

| Item | Status | Proof (test or run) or reason a/b with evidence |
|---|---|---|
| 1 --accept escapes | fixed | TH: "--accept --builder whoever does NOT remove the check", "--accept --builder o1, NO builder record and a typed AUDIT.md with a fake id: REFUSED", "--accept with the builder recovered from gate.log and a typed fake-id AUDIT.md ... REFUSED", "--legacy with a short reason: refused", "--legacy ... lets a pre-audit-gate folder through ... and says so" and the log line test. CLI run on a scratch main: `--accept --builder whoever` with `Auditor agent: zzfake` prints "no audit record". |
| 2 picture gate | fixed | TG block "picture gate: layouts, forged records, diagnostic flags": after/ and before/ layout passes, record missing beside a render refused, typed hash without PNG refused, counts above the frame refused, width/height vs the PNG header, diagnostic on `*_aerial` refused, diagnostic explained in STATUS accepted; "picture plausibility" block (size, flat PNG); thresholds documented in the source with the round 9 numbers; scene.mjs writes the record (`writeStats`, run on 15 views). Run: round 9 re-render: s4 aerial 50.53 % debug colour and s6 street 0.00 % road refused (proof table). |
| 3 ctest gate | fixed | TG block "ctest gate": total from CTestTestfile.cmake (no ctest binary), subset refused, unknown total refused, filtered doctest run refused, `== 2` with 0 measured passes, crash counted as failed test and caught by the paired `failed_tests` gate, make_brief refuses `==` and pairs the gates. Multi-config case from the real roadtool3 build. Real log of merge-roadtool3 measured: 2 failed assertions, 0 failed tests, 433 cases. |
| 4 fail open only for missing infrastructure | fixed | TG block "scene gate": crash without summary, hang, failing build and hanging build are refusals (`fail`), missing node_modules stays `error`; build 40 s + check 150 s < 270 s. |
| 5 no write into the inspected worktree | fixed | TG: the check runs on a temp copy; test "the REAL scene check on a clean copy of road-draw ... `git status` is still clean"; the proof runs on the r9, tjunction and roundabout worktrees left them as they were: r9 `git status` is clean; the `index.html` modification shown in tjunction and roundabout has the mtime 20:22-20:23, before the final proof runs (21:57-21:59), so it is the earlier gates2 run, not this round. |
| 6 waiver guard evasions | fixed | TG block "waiver guard: the cheap evasions": 31 tests, one per evasion in the audit list plus signs, expressions, hunks with extra lines, loop bounds, CMake `#`, LABELS, other test locations, effective count from real file contents. |
| 7 STATUS reading | fixed | TG block "STATUS reading": 30 excuse phrases refused, 10 honest texts accepted, "not done by design" and "time constraint of 5 ms" accepted, empty `blocked by:` refused, the self-contradicting "not done" test replaced by "ordinary STATUS text is not mistaken for an excuse". |
| 8 NO-CONTRACT cannot carry an auditor | fixed | TH block "gates3 hooks": NO-CONTRACT audit launch denied (docs/progress path with audit/auditor/review wording), proper AUDIT-OF launch unaffected, research launches allowed, the refusal is a notice item. |
| 9 libraries load inside try/catch | fixed | TH: syntax error in lib.mjs or audit_lib.mjs for each of the four hooks gives exit 0 and one FAIL-OPEN line; broken implementation; missing implementation; `guard.mjs` imports only node built-ins. TG: syntax error in gates_lib.mjs, make_brief still prints the brief and says FAIL-OPEN. check_handin loads both libraries dynamically. |
| 10 make_brief cases, no-render words, directions | fixed | TG block "make_brief: case counting ...": `s1:`, `1.`, `A)`, `Item N:`, `Cases: 6` counted; drive-path file lists not counted; five no-render phrasings refused; metric direction required and not guessed. |
| 11 signed records | fixed | TH: "signRecord / verifyRecord", "an audit record without its signature is ignored", "edited after signing", "a hand-typed (unsigned) builder record is ignored", six-process secret test. Header of `lib.mjs` states the limit (an agent that reads `ack_secret` or writes gate.log has gone out of its way). |
| 12 --since on FAULTS.md; residual line; pass floor | fixed | TG: "v2 engine-only: a FAULTS.md from before the agent started is refused as stale", "Residual only unfixable: that names no item: refused", "a Pass body of filler ... is not a pass". |
| 13 placeholder COMPARE cells | fixed | TG block "BLOCKED form and placeholder refusal": gates2's own row ("evidence row only, no match claimed" x3), "placeholder" and "n/a" cells, one sentence copied into three cells, empty table, missing file: all refused with the BLOCKED form in the message. This round has no COMPARE.md (engine-only brief). |
| 14 STATUS honesty | fixed | This file: partial reads counted as not opened (10 of 12, two "not opened" lines); the headline claims below name the run that carries them; the residual reasons in FAULTS.md are limits with a stated cause, none is "unavoidable" or "by design"; gate `statusProblems` now refuses the "all opened but partly read" combination (TG "12 of 12 opened next to read partly"). |
| 15 clock-free tests, failing names, 20 runs | fixed | TH audit section uses explicit times (B, mtimes, record times); both runners print "FAILED TESTS:" with names; 20 concurrent-ish runs (4 batches of 5) of test_hooks: all 20 "0 failed" (numbers below). |
| 16 proof table re-run | fixed | Proof table below; typed-fake `--accept` row included. |

Further findings of the gates2 audit's own fault list not in the 16 items: the ranked faults 1-14 map to items 1-14 above; fault 15 (flake) is item 15; the "Exclusions: none" line needs nothing.

## Main session additions of 22 Oct and the coordinator message (own table; same form)

| Item | Status | Proof |
|---|---|---|
| Gate-notice noise from helper agents | fixed | TH: "empty agent_type, no transcript, never launched: a separate non-alarm log entry", "a real agent type with no transcript: still FAIL-OPEN", "an empty agent_type whose id the launch hook saw ... FAIL-OPEN", "twelve helper agents in a row raise no notice item". |
| NO-CONTRACT notices grouped | fixed | TH: five launches give ONE summary line with the count and distinct reasons, one `#n[code]`, acked as one item, a later launch shows alone. |
| Wrapper gate | fixed | `tools/check_wrappers.mjs`; run by check_handin when the branch adds or renames a `sim_core/src/*.cpp`; TG block "wrapper gate"; CLI on the roadtool3 merge state 69302ae (before the fix commit) refuses road_heat, tool_hud, tool_tuning; on 1b614a1 and on the main checkout passes. The missing wrappers were not created. |
| BLOCKED form | fixed | TG block "BLOCKED form and placeholder refusal, end to end" and the BLOCKED/STATUS blocks; TH: the hook logs a `blocked` entry, the notice shows it, an ack clears it, excuses with no tried/blocked-by still refuse, old-layout briefs get the news too. |

## Gate tests removed against main (waiver guard on this round's own diff)

Run on this hand-in the waiver guard reports four gate tests of tools/hooks/test_hooks.mjs as removed or renamed against main: "ack without text: refused, exit 1", "ack with 20+ characters: accepted and counted", "ack logged with text, timestamp, through_line", "acking with nothing pending is allowed". They are the blanket-ack tests that the gates2 branch (before this round) replaced with the per-item ack tests ("ack with items but no text: refused", "ack of one item: accepted, names it ...", "ack logged with the item number, text, timestamp", "acking with nothing pending: refused ..."): the per-item form is stricter, not weaker. An allow line for them would be `allow_waiver deleted_gate_test:test_hooks.mjs`; no gate line exists for this round (its brief carried none), so the guard output is listed here instead of being allowed.

## Proof table

Gate lines used: `scene_check >= 20/21 (baseline 20/21, target 21/21)`, `ctest failed_assertions <= 2`, `ctest failed_tests <= 2`, plus the waiver guard, picture, status and wrapper checks. Raw outputs: `..\roadworld-scratch\proof_final\*.txt` and `proof_r9.txt`, `proof_roadtool3.txt`.

(i) Real round 9 folder (roadworld-wt-grade-r9, docs/progress/2026-10-11-structures-r9): REFUSED, 46 items. Among them:
- scene check measured 17/21 against the floor 20/21 (ground_under_road, median_flat, ground_hidden_by_walls, no_bare_steps named with their worst values).
- waiver guard: `is_rough()` gained "over_height_cliff" and "stress_gradient_085"; more checks wrapped in `GRADE_CHECK(is_rough(...))`.
- STATUS.md: the undone-item lines (4, 6, 33), the two-pass self-audit given with a usage reason (line 7), the all-opened-but-partly-read combination; FAULTS.md line 43 gives the same usage reason.
- 5-column COMPARE.md refused (6 rows); 23 picture items: no stats record beside the aerial/street renders.
- Pixel numbers (the round 9 views re-rendered with the stats-writing viewer, `..\roadworld-scratch\gates2\r9`): s4_retaining_wall_run_aerial 50.53 % debug colour (needs 397719 px, rock 329961 px), alongA 24.86 %, alongB 26.96 %, end 42.61 %, street 23.51 %; s6_embankment_street 0.00 % road, side_close 0.00 %, along 0.35 %, toe_profile 0.70 % (a legitimate profile view: would need the diagnostic declaration). Run: `pictureProblems` on the folder with those records overlaid.

(ii) The three hand-ins in their worktrees (what each gate says now):

| Hand-in | What the gates say |
|---|---|
| roadtool3 (no-visual) | first run: ctest "covers 1 of 5 registered tests" (the multi-config false positive, fixed: Pass 1 item 1); after the fix the ctest gate measures it; STATUS lines "Not done, with the reason: Unreal build ..." and "was not touched" refused as undone items (the second wording is no longer matched); scene gate FAIL-OPEN (no node_modules). Final-code row: see the BLOCKED line (worktree removed). |
| tjunction | 28 items: 8-column COMPARE missing (5 columns), scene 20/21 below target not named in STATUS, 4 findings in tests/test_markings.cpp (a check under an `if`, Approx epsilon 1e-6 to 0.02, an expected value 0.2 to 0.6, one check fewer) and 2 in tests/test_tjunction_mouth.cpp, 8 views with no stats record, STATUS "B3 ... NOT attempted" and other undone lines. |
| roundabout | 28 items: 8-column COMPARE missing (7 rows), no ctest result in the worktree, scene 20/21 not named, threshold loosened on four `sp.length` checks and `far_bar` bound (1.6 to 2.0), hatching-bar test case deleted, 6 views with no stats record, undone-item lines, "12 of 12 opened" next to partly-read text. |

(iii) A made-up good hand-in: passes (TG "v2, scene 21/21 measured: passes and prints what was measured", with a clean waiver diff, road in frame, three passes, no excuses; TG "BLOCKED ... an item that only the STATUS reader sees ... passes at 21/21 and is reported"). Dogfood: see "Pass 3 results".

(iv) Typed fake audit: `check_handin <dir> --accept --builder whoever` on a scratch main with `Auditor agent: zzfake` and no record: "AUDIT.md: no audit record: this AUDIT.md was not written by an auditor that passed the finish gate (a typed file does not count)". Also TH tests for the gate.log recovery path and a hand-typed builder record.

(v) Old-layout agents: TH "a brief WITHOUT GATES-VERSION (agent started before gates v2) is not measured: 3/21 on disk does not block it"; auditors with no HANDIN_DIR are not affected (the AUDIT-OF tests); the 5-column COMPARE.md of a v1 brief still passes (TG "v1: the old 5-column COMPARE.md still passes"); a v1 STATUS with a BLOCKED line passes and its news is logged (TH part 2). The wrapper gate runs for every brief version: a running agent that adds a `sim_core/src` source without its wrapper is refused, correctly (the Unreal build would fail).

## Pass 3 results

The finished gates were run on this very hand-in (`node tools/check_handin.mjs docs/progress/gates3 --no-visual --gates-version 2 --prior-audit docs/progress/gates2/AUDIT.md --gate "allow_waiver deleted_gate_test:test_hooks.mjs"`). First run: 47 items. What they were, and what was changed:
1. The waiver guard's line analysis fired about 40 times on tools/test_gates.mjs and tools/hooks/test_hooks.mjs: those files hold every waiver word as test data. Changed: the gates' own tests are watched by test NAME (a removed or renamed test is `deleted_gate_test:<file>:<name>`); 8 new tests in "the gates' own tests are watched by test NAME". Four findings remain (the blanket-ack tests replaced by per-item ack tests before this round, see the section above); with the allow line the run passes.
2. The status reader refused four of my own lines: a quotation of round 9's own wording, a scope statement that used the word for leaving a directory unchanged, and two excuse phrases in the proposed contract text. Rephrased; that word stays an excuse word on purpose (the gates2 audit lists it), so scope statements say "not affected" or "not modified".
3. The Prior audit items check counted the header row of my second table as a row. Moved the second table under its own heading.
4. No ctest gate was given (the brief has no Gates section and this worktree has no build): the measured gates are covered by the proof table above.
Final run: HAND-IN CHECK PASSED, with the waiver line "4 finding(s) ... 4 allowed", the BLOCKED item printed as BLOCKED-ITEM.

Load: `node tools/hooks/test_hooks.mjs` 20 times, 5 at a time: 20 of 20 "328 passed, 0 failed". `node tools/test_gates.mjs` 3 at once: 3 of 3 "327 passed, 0 failed" (before the 8 name-watch tests were added); the last single run is 335 passed, 0 failed.

5. After the hand-in the live finish hook refused this folder for "COMPARE.md is missing" (see the first section): fixed in the branch, 2 new tests ("gates3 hooks, part 4").

Final proof table runs (21:57-21:59, final gate code): round 9 refused with 46 items, tjunction 28, roundabout 28 (raw files in `..\roadworld-scratch\proof_final2`).

## Reference files opened

Case: Gate design evidence (round 9 hand-in, audit gate audit, gates2 audit) | Opened: C:\Users\bgbg0\Documents\Programming\roadworld-wt-grade-r9\docs\progress\2026-10-11-structures-r9\AUDIT.md, C:\Users\bgbg0\Documents\Programming\roadworld-wt-grade-r9\docs\progress\2026-10-11-structures-r9\STATUS.md, C:\Users\bgbg0\Documents\Programming\roadworld-wt-grade-r9\docs\progress\2026-10-11-structures-r9\FAULTS.md, C:\Users\bgbg0\Documents\Programming\roadworld-wt-auditgate\docs\progress\2026-10-11-auditgate\AUDIT.md
Case: Gate code and rules | Opened: C:\Users\bgbg0\Documents\Programming\roadworld\tools\check_handin.mjs, C:\Users\bgbg0\Documents\Programming\roadworld\tools\make_brief.mjs, C:\Users\bgbg0\Documents\Programming\roadworld\.claude\settings.json, C:\Users\bgbg0\Documents\Programming\roadworld\docs\AGENT_CONTRACT.md
Case: Templates and the scene check | Opened: C:\Users\bgbg0\Documents\Programming\roadworld\docs\templates\BRIEF.md, C:\Users\bgbg0\Documents\Programming\roadworld\docs\templates\STATUS.md, C:\Users\bgbg0\Documents\Programming\roadworld\CLAUDE.md

## Proposed contract wording (not applied)

Add to `docs/AGENT_CONTRACT.md` after item 6 (and keep the numbering):

"6a. **An honest statement that you cannot do something is a legitimate way to finish.** Put it in the first section of STATUS.md, one line per item: `BLOCKED: <item> | tried: <what you attempted> | blocked by: <the exact error, refusal or missing thing> | needs: <what would unblock it>`. A STATUS whose open items are all in this form (with real tried and blocked-by text) passes the finish gate and the main session is told. Time, usage and scope are not blocks, and neither is saying that the work needs a larger rewrite or belongs to another round. Do not fill a placeholder table or file to get through the gate: a placeholder is refused, a BLOCKED line is not."

Add one clause to item 7: "send a send-back round's previous AUDIT.md path with `make_brief.mjs --prior-audit <file>`: the finish gate then also demands the Prior audit items table".

## Own fault list and residual

FAULTS.md: Pass 1, Pass 2, Pass 3 and Residual (5 items).

## Changes (file:function)

- `tools/gates_lib.mjs`: `measureSceneCheck` (temp copy, refusals), `countRegisteredTests`, `measureCtest`, `runMeasuredGates` (BLOCKED-aware), `waiverFindings` (+ `comparisons`, `looserCmp`, `stripCode`), `collectWaiverDiff` (contents, more paths), `pictureProblems` (+ `pngColourCount`), `statusProblems` (+ `parseBlocked`, excuse lists), `priorAuditProblems`, `methodChangeProblem`/`parseRounds` (stated directions), `PLACEHOLDER_RE`, `BLOCKED_HINT`.
- `tools/check_handin.mjs`: dynamic library loading, `--legacy`, `--prior-audit`, accept path, wrapper gate, placeholder COMPARE cells, `BLOCKED-ITEM` output, `--since` on FAULTS.md under v2.
- `tools/check_wrappers.mjs` (new). `tools/make_brief.mjs`: dynamic gates_lib, case counting, no-render words, `==` refusal and ctest pairing, `--prior-audit`.
- `tools/hooks/guard.mjs` (new) and the four hooks split into stubs plus `*_impl.mjs`; `lib.mjs` (secret, `signRecord`, `verifyRecord`); `audit_lib.mjs` (signed records, record required on every path, `owedInLog`); `handin_gate_impl.mjs` (helper-agent entry, BLOCKED news, PRIOR-AUDIT); `brief_gate_impl.mjs` (NO-CONTRACT audit refusal); `notice_lib.mjs` (grouped NO-CONTRACT, BLOCKED, refused audit launches); `gate_ack.mjs` (grouped ack).
- `prototypes/road-draw/tools/scene.mjs` (`--page`, `writeStats`), `build.mjs` (`--out`). `docs/templates/BRIEF.md`, `STATUS.md` (BLOCKED form, wrapper check).
- Tests: `tools/test_gates.mjs`, `tools/hooks/test_hooks.mjs`.

## Images that carry each claim

None: engine-only round, no renders (HANDIN: no-visual). The claims rest on the runs named above.

## Suite and checks

- `node tools/test_gates.mjs`: 335 passed, 0 failed (the run of record; three earlier concurrent runs 327 passed, 0 failed each, before the last 8 tests).
- `node tools/hooks/test_hooks.mjs`: 330 passed, 0 failed (20 runs, 5 at a time, all 328/0 before the two part-4 tests were added).
- `node tools/check_wrappers.mjs` on the main checkout: PASSED, 35 sources. On the roadtool3 merge state 69302ae (scratch archive): FAILED, 3 of 35.
- `node tools/check_handin.mjs docs/progress/gates3 ...`: see "Pass 3 results".
- No C++ build, no ctest, no Unreal: no engine file was changed.

## Next step

Merge when the auditor accepts; the main session then needs: the three hook stubs and `guard.mjs` land together (the stubs import `guard.mjs`); `ack_secret` is created by the first signed record; tell the running agents nothing (old layouts are unaffected, see the proof table).
