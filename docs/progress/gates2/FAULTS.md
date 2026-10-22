# Faults (gates2)

## Pass 1
Written before fixing, from probing the gates with adversarial inputs and from the proof runs.
1. statusProblems false positives: "the time limit of the scene check is 240 s" and "the quota of lane widths per arm" were refused as usage reasons (the regex matched bare "time limit" and "quota"). An honest STATUS would be bounced.
2. statusProblems misses "will do next round" (evades the not-attempted patterns).
3. A task-file gate `ctest failed_assertions == 2` refuses a round that fixes the two known failures (measured 0). Fixing the cause should not be refused.
4. The picture check refuses legitimate ground close-ups (round 9 s6 toe profile, 0.7 % road) unless declared diagnostic; the message names the fix.
5. The scene gate runs `node build.mjs` in whichever worktree it is pointed at, regenerating the tracked index.html there (seen in the old tjunction and roundabout worktrees).
6. Round 9 shipped no stats at all; proof for the s6 street and s4 aerial needed a re-render with the current viewer.
7. One hook test run failed once (263 passed, 1 failed) and the failing line was not captured.

## Pass 2
Fixes and re-checks. 1 and 2 fixed in tools/gates_lib.mjs (quota and time limit only after hit/reached/due to/because of/ran into; not-attempted patterns gained "will do next round|later"), four tests added to tools/test_gates.mjs (148 pass). 3 documented as `<=` advice in STATUS (the grammar already supports it). 6 done: re-rendered round 9 s4 (5 views) and s6 (7 views); s4 aerial 50.53 % debug, s4 street 23.51 %, s6 street 0.00 % road, all refused by pictureProblems. 7: re-ran test_hooks.mjs 11 times, all 264 passed, 0 failed.
New in Pass 2: the template STATUS.md must pass statusProblems (checked: no findings). The roadtool3 proof used a ctest line that does not apply to it (noted in STATUS).

## Pass 3
Re-ran the proof after the regex change: round 9 is still refused with all findings (scene 17/21, three is_rough waiver findings, not-attempted lines, usage reasons, missing stats); tjunction and roundabout results unchanged. Looked for evasion of the waiver guard: a waiver added in sim_core, a vacuous test edit and an expected value in a data file are not seen (listed in STATUS under B). Checked fail-open: a missing node_modules gives FAIL-OPEN (logged), not a refusal, while `--accept` refuses instead. Checked old-layout briefs: the 5-column COMPARE and 3/21 on disk do not block them (hook tests). No new fault beyond the limits listed.

Found in Pass 3: this round's own STATUS tripped the status reading when it merely described the gate (quoting the trigger phrases); reworded. The reading is a phrase heuristic and flags discussion of the phrases too.

## Residual
- 8: the status reading flags text that only quotes the trigger phrases; a round about the gates must paraphrase. Exempting quotes would be an evasion route, so left.
- 5: build.mjs side effect in the worktree under test; cannot be avoided without a second copy of the prototype, and for a builder's own worktree it is the normal generated file.
- Waiver guard blind spots (sim_core edits, vacuous tests, data-file expected values): a diff of test files cannot show them; the fresh auditor is the check.
- 4: by design, the view is declared diagnostic in the views json.
- 7: flake not reproduced, line not captured.
