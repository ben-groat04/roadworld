# Index, round gates5 (engine and tooling: no renders, no COMPARE.md)

HANDIN: no-visual. The evidence of this round is runs and tests, not pictures.

| File | What it holds |
|---|---|
| STATUS.md | first section (real finding: 18 damaged photos in the live docs/reference; unverified items; CLAUDE_PROJECT_DIR answer), Prior audit items (12 items of docs/progress/gates4/AUDIT.md plus gates3 items 2, 5, 12 and the test-isolation item), references opened, changes, proof table (round 9, roadtool3, tjunction2, roundabout2, old layouts), merge checklist and proposed rule text |
| FAULTS.md | Pass 1 to 3 and Residual (4 items, evasions accepted by the main session decision) |
| INDEX.md | this list |

Run outputs behind the proof table (outside the repo, in `..\roadworld-scratch\gates5`): `proof\{r9,rt3,tj2,ra2}.out/.err`, `attack.mjs`, `dgt.mjs`, `sgt.mjs` (the attack tables later moved into test_hooks.mjs).

Tests: `node tools/test_gates.mjs` (490), `node tools/hooks/test_hooks.mjs` (412), `node tools/test_worktree_tools.mjs` (27), `node tools/test_backup_reference.mjs` (31).
New files: `tools/hooks/root.mjs`, `tools/hooks/shell_lex.mjs`, `tools/unlink_reference_junctions.ps1`, `tools/backup_reference.mjs`, `tools/test_isolation.mjs`, `tools/test_backup_reference.mjs`.
