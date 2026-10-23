# BRIEF: {{TASK}}

ROUND: {{ROUND}}   WORKTREE: {{WORKTREE}}   BRANCH: {{BRANCH}}   BASE: current main (run `powershell -NoProfile -ExecutionPolicy Bypass -File tools\new_agent_worktree.ps1 <name>`, do not start from a stale branch)
Progress folder for hand-in: docs/progress/{{ROUND}}/
{{HANDIN_LINES}}

## Owner's words (verbatim, with date; never paraphrase)
{{OWNER_WORDS}}

## Finish line
Your output matches the reference photos in the cases below. The fault list further down is a starting list, not the task. You are done when the self-audit residual list holds only things you cannot fix (and you name why), or after 3 self-audit passes. There is no time limit in hours. Never mark a listed item "not done" without having attempted it and saying what blocked it.

## Cases (at most 3 per round unless the task file says why more) and photos (at least 3 per case, absolute paths in the main checkout)
Every file listed here must be opened; M = the number of files listed in this brief.
{{CASES}}
Downloads of reference photos are pre-approved: see the Agent contract below, item 2, for the owner's words.

## Known faults (starting list)
{{FAULTS}}

If this is a send-back round: every item in the prior AUDIT.md and your own FAULTS.md must be fixed (contract item 17); fill the STATUS.md table "Prior audit items"; an item stays open only if too pedantic for the owner to notice or the audit's reading is doubtful, with your own evidence.

## Self-audit (required)
Per the Agent contract item 4: render each case at the framing of its best photo, make composites (render | photo | before, same camera) into `docs/reference/_compare/{{ROUND}}/`, write FAULTS.md Pass 1 with faults that are NOT in the list above, fix, re-render, Pass 2, Pass 3, then Residual.

## Hand in (files in docs/progress/{{ROUND}}/)
STATUS.md (use docs/templates/STATUS.md; first heading "Skipped, refused or failed"), FAULTS.md (Pass 1..n and Residual), COMPARE.md (table: case | render | reference | composite | before-same-camera | subject/type match | scale match | viewpoint match; the last three state in words that reference and render match, the check fails without them), INDEX.md (render list), suite counts. Run `node tools/check_handin.mjs docs/progress/{{ROUND}}` before reporting, adding `--gates-version 2` and one `--gate "<text>"` for each `GATE:` line near the top of this brief (the finish hook runs the same checks). Gates v2 measure the work themselves: they run the scene check and read the suite result, diff your tests for any new waiver, skip, should_fail, loosened tolerance or deleted test, count the road and debug-colour pixels of each view in its `<view>.mask.png` (written by the render tool beside `<view>.png` and `<view>.stats.json`; a number you type is never what decides), require render, reference, composite and before to be four different files and every reference photo to be a registered file of docs/reference, and read STATUS.md for undone items. If the brief lists `ITEM:` lines, STATUS.md needs a `## Items` section with one row per item (`| <id> | done | <the test, run or image that proves it> |`, or a BLOCKED line); an item with no row is refused. A BLOCKED line never waives a measured gate floor (only the main session can, with an `allow_floor_miss` line in the task file). Your self-audit needs 3 passes in FAULTS.md unless the residual list holds only unfixable items (then a line "Residual only unfixable: <item numbers, and why each cannot be fixed>"). If you really cannot do something, say so in the first section of STATUS.md in the BLOCKED form, one line per item, instead of filling a placeholder table or giving a reason such as time, scope or a later round: `BLOCKED: <item> | tried: <what you attempted> | blocked by: <the exact error, refusal or missing thing> | needs: <what would unblock it>`. "tried" names a command in backticks, a file or a test that you ran; "blocked by" quotes the error text, the refused command or the missing file path ("the complexity", "difficult", "the geometry" and "a larger rewrite" are not blocks); at most 3 BLOCKED lines unless each names a different concrete cause. A STATUS whose open items are all in that form passes the finish gate, and the main session is told. A round that adds a `sim_core/src/*.cpp` also adds its `unreal/.../Private/SimCore/Wrap_<name>.cpp` (`node tools/check_wrappers.mjs` checks it).

## Parallelism note (main session fills; one line per independent item)
Launched now: {{LAUNCHED}}
Waiting, and why (Unreal lane, shared file): {{WAITING}}

## Report
First lines: skipped, refused, failed, unverified. Then what changed, with the image that carries each claim.

---
# AGENT CONTRACT (pasted unchanged)
{{CONTRACT}}
