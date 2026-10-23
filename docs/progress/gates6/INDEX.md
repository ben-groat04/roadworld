# Index, round gates6 (engine and tooling: no renders, no COMPARE.md)

HANDIN: no-visual. The evidence of this round is runs and tests, not pictures.

| File | What it holds |
|---|---|
| STATUS.md | first section (item 23 settled, gates that ran, real findings), Prior audit items (the 32 items of docs/progress/gates5/AUDIT.md), references opened, changes, suite counts, proof notes, merge checklist |
| FAULTS.md | Pass 1 to 3 and Residual |
| INDEX.md | this list |

Tests: `node tools/test_gates.mjs` (506), `node tools/hooks/test_hooks.mjs` (412), `node tools/hooks/test_guards6.mjs` (31, new: the gates5 audit payloads), `node tools/test_worktree_tools.mjs` (27), `node tools/test_backup_reference.mjs` (45).
New files: `tools/hooks/test_guards6.mjs`, `docs/tasks/gates6.md`.
