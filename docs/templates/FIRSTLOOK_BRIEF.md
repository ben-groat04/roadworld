# FIRST LOOK BRIEF: {{TASK}}

FIRSTLOOK-OF: {{PROGRESS}}
ROUND: {{ROUND}}   You are the Opus FIRST-LOOK agent of this round. This is a short image pass, not an audit and not a build: you open pictures, you write one file, you stop. Your agent id is at the top of your context ("YOUR AGENT ID: ..."); you cannot finish until `FIRSTLOOK_OPUS.md` passes the finish gate (tools/hooks/handin_gate.mjs).
The round's progress folder: {{PROGRESS}}

## Owner's words (verbatim, with date; never paraphrase)
{{OWNER_WORDS}}

## Cases and reference files
{{CASES}}
{{REFLIST}}

## Order of work (the order is the point)
1. Do NOT open `AUDIT.md`, `STATUS.md` or `FAULTS.md` of the progress folder, now or before step 3. They hold the builder's and auditor's claims; the point of a first look is to see the pictures without them. The finish gate reads your transcript: a read of those files before your first image is refused unless FIRSTLOOK_OPUS.md carries a line `Order: <what you read first and why>`.
2. Open the round's headline images (COMPARE.md composites and renders under the progress folder or `docs/reference/_compare/{{ROUND}}/`; list the folder, do not read the .md files) and at least one reference photo per case from the files named above. Look at them as the owner would: what is the first thing that is wrong?
3. Write `FIRSTLOOK_OPUS.md` in the progress folder (template docs/templates/FIRSTLOOK.md). Its lines:
   First-look agent: <your agent id>
   Opened: <image path>, <image path>, ... (at least 3 that exist: the round's pictures AND at least one reference photo)
   Top 3:
   1. <the worst thing you see, one sentence, with the image it is in>
   2. ...
   3. ...
   What here would never be built: <the thing a real engineer or council would never build, or "nothing" with what you checked>
4. Do not edit anything else, do not fix anything, do not commit. The main session reads your three lines before it reads the audit.

## Report
First lines: anything you could not open. Then the Top 3, short.

---
# AGENT CONTRACT (pasted unchanged)
{{CONTRACT}}
