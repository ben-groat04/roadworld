# Hand-in gates that measure the work, not the paperwork (support: fixes why earthworks round 9 got through), plus the four audit-gate faults

## Owner's words
11 Oct, after earthworks round 9 was audited and sent back: "think about why round 9 earthworks failed, improve the system"
11 Oct: "is one earthworks agent creating AND auditing? that's wrong. and elsewhere, that's just an expample"
11 Oct: agents "should anticipate what the cold read viewer might say and just get round to fixing that also before it actually tries to get its work audited"
11 Oct: "this just all seems super disorganised you have no clear plan and are super reactive not considering the full development"
11 Oct: "as opus, why are you doing so much work. you are the orchestrator"

## Cases
Engine and tooling work, no renders: HANDIN: no-visual. The "photo" here is the evidence of what went wrong; every gate you add must be shown to REFUSE the real round 9 hand-in and to PASS a good one.
Files to open, all of them (M = 12):
- C:\Users\bgbg0\Documents\Programming\roadworld-wt-grade-r9\docs\progress\2026-10-11-structures-r9\AUDIT.md (the audit: what was wrong)
- C:\Users\bgbg0\Documents\Programming\roadworld-wt-grade-r9\docs\progress\2026-10-11-structures-r9\STATUS.md (what the builder admitted in its own first lines and still handed in)
- C:\Users\bgbg0\Documents\Programming\roadworld-wt-grade-r9\docs\progress\2026-10-11-structures-r9\FAULTS.md
- C:\Users\bgbg0\Documents\Programming\roadworld-wt-auditgate\docs\progress\2026-10-11-auditgate\AUDIT.md (four faults in the unmerged audit gate, branch agent/auditgate ff920a9)
- C:\Users\bgbg0\Documents\Programming\roadworld\tools\check_handin.mjs
- C:\Users\bgbg0\Documents\Programming\roadworld\tools\make_brief.mjs
- C:\Users\bgbg0\Documents\Programming\roadworld\tools\hooks (every file in the folder)
- C:\Users\bgbg0\Documents\Programming\roadworld\.claude\settings.json
- C:\Users\bgbg0\Documents\Programming\roadworld\docs\AGENT_CONTRACT.md
- C:\Users\bgbg0\Documents\Programming\roadworld\docs\templates\BRIEF.md and STATUS.md
- C:\Users\bgbg0\Documents\Programming\roadworld\prototypes\road-draw\tools\scene.mjs (the 21-invariant scene check)
- C:\Users\bgbg0\Documents\Programming\roadworld\CLAUDE.md (section "Working rules")

Why round 9 got through (main session's diagnosis; check it against the files and correct it if wrong):
1. The brief asked for scene check 21/21 and the baseline was 20/21; the builder handed in 17/21 and the hand-in gate passed, because the gate checks that files exist, not what any number is.
2. The builder made the C++ tests pass by adding waivers (two new, one kept), and described them in STATUS as narrower than they are. Nothing detects a new waiver, skip or should_fail.
3. Headline renders were plainly broken (no road in the frame; debug "needs structure" colour and rock-colour slabs instead of a wall) and four of six composites were never opened. Nothing checks that the subject is in the picture or that debug colours are absent.
4. STATUS listed "not attempted" items and "2 passes, not 3 (usage window)". The gate reads none of that; the contract (item 8) itself mentions usage warnings, against the owner's rule that agents are not told about usage limits.
5. One round carried eight scenarios; several were not touched.
6. It was the ninth round on one method and it regressed: nothing forces a change of method when the numbers stop moving.

What to build (each as a mechanical check with tests; reversible details are yours):
A. Measured gates. `make_brief.mjs` accepts a "## Gates" section in a task file: lines such as `scene_check >= 20/21 (baseline 20/21, target 21/21)` or `ctest failed_assertions == 2`. `check_handin.mjs` RUNS the named measurement itself in the agent's worktree (scene check: build + `scene.mjs --check`; suite: read a fresh ctest result or run it when a build dir exists) and refuses a hand-in below baseline. Below target but at or above baseline passes only with the shortfall in STATUS first lines. The builder's own number is never trusted. Keep the total added time small and print what was measured.
B. Waiver guard. `check_handin.mjs` diffs the branch against its merge base and refuses when tests gained a waiver, skip, `should_fail`, a loosened tolerance constant or a deleted test case, unless the task file's Gates section allows that exact name. Find how waivers are really written in `tests/` (grading tests) and in `scene.mjs` and match those forms; list what the guard cannot see.
C. Picture sanity for visible rounds. Give the render tool (`scene.mjs` and the views json the earthworks and junction rounds use) a per-view record written next to each render: fraction of pixels showing road surface, and count of pixels in debug colours (the "needs structure" orange and any other diagnostic colour). `check_handin.mjs` refuses a headline view with no road in frame or with debug colour present, unless the view is declared diagnostic. Show it refuses round 9's s6 street view and s4 aerial.
D. STATUS reading. Refuse a hand-in whose STATUS has "not attempted" items or fewer self-audit passes than 3 without a residual-only reason, or names a usage limit as a reason. Propose (as a diff in your report, do not apply to CLAUDE.md) the wording change that removes usage talk from contract item 8 and from the CLAUDE.md usage-limit paragraph.
E. Method-change rule. When a task file names a previous round, `make_brief.mjs` requires its baseline numbers; if the last two rounds on the same topic did not improve the gate numbers, it prints a refusal telling the main session to commission a method review instead of another round. Keep it simple and overridable with a written reason.
F. The four audit-gate faults (from the auditgate AUDIT.md): a made-up auditor id clears the owed audit; `--accept` ignores the verdict; a resumed builder keeps its old audit; no builder record means no check. Bring the useful parts of `agent/auditgate` into your branch (cherry-pick or re-implement) and fix them. Its 8-column COMPARE rule must not refuse agents already running with the old layout: version it by a marker in the brief.
G. Scope line in the brief template: a round names at most 3 cases unless the task file says why more.
Prove it: tests for every gate; a table in STATUS of each gate run against (i) the real round 9 folder: must refuse, with the message, (ii) the roadtool3, tjunction and roundabout hand-ins in their worktrees: say what each gate would have said, (iii) a made-up good hand-in: must pass. The gates must fail OPEN with a logged `FAIL-OPEN` line on an internal error, like the existing hooks, and must not block the five agents now running (auditors with no HANDIN_DIR; one builder `pvetrees` briefed with the current layout).

Start: worktree C:\Users\bgbg0\Documents\Programming\roadworld-wt\gates2, branch agent/gates2, cut from current main. You own `tools/` (scripts and hooks), `docs/templates/`, `docs/AGENT_CONTRACT.md`, the gate parts of `prototypes/road-draw/tools/scene.mjs`, and `.claude/settings.json` in your worktree only. Do not touch sim_core, tests of the engine, Unreal, or the live hooks in the main checkout. No Unreal. Do not push or merge.

## Faults
Known: the six diagnosis points above and the four audit-gate faults. Also known from earlier today: engine-only briefs without the line `HANDIN: no-visual` made two builders write placeholder COMPARE tables to get through; make `make_brief.mjs` refuse a task file whose Cases say "no renders" without `--no-visual`.

## Launched
This job. Also launched now: a read-only method review of the earthworks approach (why nine rounds did not converge). Running: auditors for gradelayer, road tool round 3, T-junction and roundabout; the built-in trees evaluation (Unreal lane).

## Waiting
Earthworks round 10: waits for the method review and the gradelayer audit verdict (same grading files). Milestone 3 UI and milestone 2 follow-up: wait for the Unreal lane. Slip road: waits for the roundabout merge.
