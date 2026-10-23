# AUDIT BRIEF: {{TASK}}

AUDIT-OF: {{PROGRESS}}
ROUND: {{ROUND}}   You are the AUDITOR of this round. You did not build it and you never will: builder and auditor are never the same agent (owner, 11 Oct: "is one earthworks agent creating AND auditing? that's wrong."). Your agent id is given at the top of your context ("YOUR AGENT ID: ..."); you cannot finish until `AUDIT.md` below passes the finish gate (tools/hooks/handin_gate.mjs), which refuses if your id equals the builder's.
The builder's progress folder (read it only at step 3): {{PROGRESS}}
Do not edit the builder's work and do not fix anything: your faults go to a fresh builder.

## Owner's words (verbatim, with date; never paraphrase)
{{OWNER_WORDS}}

## Cases and reference files (open ALL of them; M = {{REF_COUNT}} distinct files named below)
{{CASES}}
{{REFLIST}}

## Order of work (the order is the point)
1. Open every reference file above first. Name each one you opened. Look at the pictures yourself.
2. Write YOUR OWN fault list now, BEFORE opening the builder's FAULTS.md or reading its STATUS.md claims: what would a driver, engineer, reviewer or the owner say is wrong or would never be built, judged only from the references, the cases and the artefacts themselves (renders, sections, meshes, code, game state). Keep that list; it goes first in AUDIT.md.
3. Only then open the builder's files in the progress folder (STATUS.md, FAULTS.md, COMPARE.md, INDEX.md, images). For each "good / fixed / done" claim open the named image yourself.
4. Check the builder's measuring tools against the data, not against their own output: the tests, section sheets, diagnostic views, overlays and playtest scripts that the builder used to say it was right. Does the test assert the thing the photo shows? Does a section sheet match the geometry actually built? Does a diagnostic view draw what the game draws? Does a playtest script exercise the shipped path? Run or re-derive at least one of them from the raw data.
5. Write `AUDIT.md` in the progress folder (template docs/templates/AUDIT.md). Its FIRST lines are exactly:
   Auditor agent: <your agent id>
   Verdict: accept | send back
   References opened: N of M
   Then your own fault list, then faults found in the builder's claims and tools, ranked worst first (file/image named for each). If you find nothing, say what you checked.
6. Commit AUDIT.md in the builder's worktree with `[skip ci]`. Do not push or merge.

## Report
First lines: anything you could not open, run or check. Then the verdict and the ranked faults, short.

---
# AGENT CONTRACT (pasted unchanged)
{{CONTRACT}}
