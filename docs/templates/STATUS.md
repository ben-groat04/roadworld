## Skipped, refused or failed

References opened: N of M listed   <!-- e.g. `References opened: 44 of 44 listed`; M = files the brief lists as reference/spec. If N < M, list each unopened file below as "not opened: <file> (reason)". -->

<!-- FIRST. Everything you skipped, were refused, could not do, failed, or did not verify, one line each, with what blocked it.
     Write "Nothing skipped, refused or failed." only if true, then say what you did NOT verify. Never leave this empty.
     Something you truly cannot do goes in ONE line of this form (the finish gate accepts it and tells the main session; "time", "scope", "later round" are not blocks):
     BLOCKED: <item> | tried: <command in backticks, file or test you ran> | blocked by: <quoted error text, refused command or missing file path> | see: <path of the log or output file that holds that error; the quote must be in it> | needs: <a named thing that would unblock it: a file, a command, a decision with its subject>
     "the complexity", "difficult", "the geometry", "a larger rewrite" are not blocks; the quoted error is checked against the see: file (a quote not found there refuses the line); "needs: help", "needs: a decision", "needs: nothing" are refused; at most 3 BLOCKED lines unless each has a different concrete cause; a BLOCKED line never waives a measured gate floor. -->

BLOCKED lines, if any, go here (delete this line if there are none).

## Items

<!-- Only when the brief lists `ITEM:` lines: one row per item, "done" with the test, run or image that proves it, or the BLOCKED form in the row. An item with no row is refused. -->

| Item | Status | Proof |
|---|---|---|

## Prior audit items

<!-- Every item from the prior AUDIT.md "For the next builder" and your own FAULTS.md. Only two reasons keep an item open: (a) too pedantic for the owner to notice; (b) the audit's measurement or reading is doubtful, shown by your own check. "Out of scope", "time", "bigger change", "later round" are not reasons. Write "No prior audit." in a first round. -->

| Item | Status | Proof (image or test) or reason a/b with evidence |
|---|---|---|

## Reference files opened

<!-- One line per case. Paths relative to docs/reference/ (or absolute in the main checkout). At least 3 existing files per case.
     If no photo covers the case, write "No photo covers: <what>" and name the fetch you tried. -->
Case: <name> | Opened: <aerial/a.jpg>, <street/b.jpg>, <gsv/c.jpg>

## Own fault list and residual

<!-- Short summary; the full passes are in FAULTS.md (Pass 1, Pass 2, ..., Residual). -->

## Changes (file:function)

## Images that carry each claim

| Claim | Image file (opened by me) |
|---|---|

## Suite and checks

## Next step
