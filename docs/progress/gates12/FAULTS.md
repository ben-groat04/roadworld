# FAULTS gates12

No-visual round. Method: real hook scripts fed real-shaped JSON, temp git repos, a sweep of the live NO-CONTRACT descriptions, the full suites, fail-first against the gates11 tools.

## Pass 1 (while building)

1. Backslashes halved in shell heredocs and `node -e` edits broke regexes; fixed by using the edit tools only.
2. An apostrophe in a single-quoted string (make_brief PHASE_TEXT) was a syntax error; text reworded.
3. make_brief imported root.mjs unguarded and failed in the broken-tools fixture (test_gates); wrapped, falls back to root.
4. test_hooks: two review-launch tests expected the old behaviour; flipped by decision 3. The post hook was missing from its file copy list; added.
5. A test with no state dir wrote one live deny line to gate.log; fixed and asserted (see STATUS).

## Pass 2 (cold read as an auditor)

1. The path lookup through PowerShell cannot meet 300 ms (0.6-1.3 s cold start); two stages, stage 2 bounded to 4 s, stated in STATUS.
2. An open run record with no end line would count forever if its post hook never fired; a run is open only inside its time limit, so a dead record expires.
3. fail_first on the old tools crashed at a missing record file and at a missing module instead of failing cases; helpers made safe, the run now gives 59 individual failures.
4. Exit code unknown, or non-zero with a clean log, looked like a pass; both refused in `buildProblems`.

## Pass 3 (what would the owner notice)

1. Would a normal builder be refused? No false refusal in the sweep of 77 real NO-CONTRACT descriptions (41 refused, all real audits or first looks); every guard has ALLOW tests.
2. Would an honest build phase be blocked by the record? Only if the live Post payload differs from the assumed shape; stated first in STATUS.
3. Does any test write the live gate.log? One line did during development (disclosed); now asserted not to.

## Residual

Only what code cannot fix: the Post payload shape is unverified live; paths built from variables or `$(...)` are invisible to lock_guard; an agent can open a first-look image without looking at it (the main session's own look stays); the launch refusal is by meaning with regexes, so a new unusual wording can still pass and is caught only by the audit gate that lists every passed hand-in without an AUDIT.md.
