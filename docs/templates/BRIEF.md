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

## Self-audit (required)
Per the Agent contract item 4: render each case at the framing of its best photo, make composites (render | photo | before, same camera) into `docs/reference/_compare/{{ROUND}}/`, write FAULTS.md Pass 1 with faults that are NOT in the list above, fix, re-render, Pass 2, Pass 3, then Residual.

## Hand in (files in docs/progress/{{ROUND}}/)
STATUS.md (use docs/templates/STATUS.md; first heading "Skipped, refused or failed"), FAULTS.md (Pass 1..n and Residual), COMPARE.md (table: case | render | reference | composite | before-same-camera | subject/type match | scale match | viewpoint match; the last three state in words that reference and render match, the check fails without them), INDEX.md (render list), suite counts. Run `node tools/check_handin.mjs docs/progress/{{ROUND}}` before reporting, adding `--gates-version 2` and one `--gate "<text>"` for each `GATE:` line near the top of this brief (the finish hook runs the same checks). Gates v2 measure the work themselves: they run the scene check and read the suite result, diff your tests for any new waiver, skip, should_fail, loosened tolerance or deleted test, read the per-view stats files next to your renders (road in frame, no debug colours), and read STATUS.md for "not attempted" items. The number you type is never read. Your self-audit needs 3 passes in FAULTS.md unless the residual list holds only unfixable items (then a line "Residual only unfixable: <why each cannot be fixed>").

## Parallelism note (main session fills; one line per independent item)
Launched now: {{LAUNCHED}}
Waiting, and why (Unreal lane, shared file): {{WAITING}}

## Report
First lines: skipped, refused, failed, unverified. Then what changed, with the image that carries each claim.

---
# AGENT CONTRACT (pasted unchanged)
{{CONTRACT}}
