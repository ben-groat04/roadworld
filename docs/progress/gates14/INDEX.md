# Index, round gates14 (tooling: no renders)

HANDIN: no-visual. The evidence of this round is runs and tests, not pictures.

| File | What it holds |
|---|---|
| STATUS.md | skipped or unverified, items table, prior audit items (the 7 faults of the gates13 AUDIT.md), live-description table (81 rows), good/bad table per new gate, suite counts, merge text (contract, CLAUDE.md, lane lock tool, secret migration, self-declared category) |
| FAULTS.md | own fault passes and Residual |
| COMPARE.md | no-visual note |
| fail_first_test_guards14.txt | `test_guards14.mjs` against the gates13 tools: the failing cases |
| suite_*.log | one log per suite (13 suites), run one at a time under `timeout 600` with CP_GATE_STATE_DIR in scratch |
| INDEX.md | this list |
