# Own fault list (tooling round, no renders)

## Pass 1

Faults found by reading my own code as an attacker or a tired main session, not from the brief:
1. A stale AUDIT.md left in the folder would satisfy the finish gate for a new auditor. Fixed: AUDIT.md must be newer than the auditor's run start (test "auditor: an AUDIT.md older than the auditor's run").
2. The notice's AUDIT OWED section used the first problem of the first candidate only; when one candidate is missing and another is malformed it showed "missing". Fixed in `auditProblems` (prefers the non-missing problem list).
3. `faults` section check matched the first heading containing "fault", which is "Own fault list", so a 2-character ranked section passed. Fixed: prefer the heading containing "ranked" (test with faults text "ok").
4. A fresh builder after "send back" would inherit the old audit. Fixed: record replaced on a different builder id, AUDIT.md must be newer than the latest pass (tests).
5. `gate_ack` on an owed-only notice would have claimed to acknowledge; now it names nothing and is refused.
6. gate-notices audit faults 1, 2, 4, 5, 10 and COMPARE relevance: implemented with tests (see STATUS).

## Pass 2

1. The release after 3 refusals for an auditor could look like a released hand-in in the notice. It is logged as gate `audit`, so it stays out of the HAND-IN RELEASED list; the audit remains owed (test).
2. `check_handin --since` also applies to a hand-in whose agent edits an old folder with `touch`: not closed (mtime only). Residual.
3. Pasted-fault-list detection is a heuristic (a heading, or two verbatim long lines of FAULTS.md). A paraphrased list passes. Residual.
4. The launch gate cannot see whether an auditor brief is for a round that was ever built (AUDIT-OF a folder with no record). Harmless: the auditor still must write the file.
5. COMPARE statements are checked for presence, not truth. Residual; the auditor opens the composite.

## Residual

- Live behaviour of agent_id equality, YOUR AGENT ID delivery and transcript timestamps untested (needs the independent live test).
- `touch`-able mtimes; paraphrased fault lists; true/false of COMPARE statements; subagent that reads ack_secret on purpose; deleting gate.log; a builder released after 3 refusals, started with NO-CONTRACT or without HANDIN_DIR never gets a builder record, so no audit is owed for it.
- Audit faults 3, 6, 7, 8 untouched (reasons in STATUS.md).
