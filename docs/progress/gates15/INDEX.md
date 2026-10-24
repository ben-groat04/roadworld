# Index, round gates15 (tooling: no renders)

HANDIN: no-visual. The evidence of this round is runs and tests, not pictures.

| File | What it holds |
|---|---|
| STATUS.md | skipped or unverified, items table, prior audit items (the 5 faults of the gates14 AUDIT.md, gates13 items 1 and 3 re-opened), glob-crash item, live-description table (90 rows), good/bad table per new gate, suite counts, accepted residual, merge text |
| FAULTS.md | own fault passes and Residual |
| COMPARE.md | no-visual note |
| fail_first_test_guards15.txt | `test_guards15.mjs` against the gates14 tools: 88 FAIL / 74 ok |
| suite_*.log | one log per suite, run one at a time under `timeout 600` with CP_GATE_STATE_DIR in scratch |
| INDEX.md | this list |
