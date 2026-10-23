# Index, round gates7 (engine and tooling: no renders, no COMPARE.md)

HANDIN: no-visual. The evidence of this round is runs and tests, not pictures.

| File | What it holds |
|---|---|
| STATUS.md | first section (what was skipped or unverified, gates that ran), Prior audit items (the 14 items of docs/progress/gates6/AUDIT.md plus item 15 of the send-back note), references opened, changes, suite counts (two runs of every suite), merge checklist with the new line 14 |
| FAULTS.md | Pass 1 to 3 (own attack on the fixes, real data, what a builder would say) and Residual |
| INDEX.md | this list |

Tests (each run twice, one at a time): `node tools/test_gates.mjs`, `node tools/hooks/test_hooks.mjs`, `node tools/hooks/test_guards6.mjs`, `node tools/hooks/test_guards7.mjs` (new: one named real-payload case per gates6 audit item, plus the git guard), `node tools/test_worktree_tools.mjs`, `node tools/test_backup_reference.mjs`.
New files: `tools/hooks/git_guard.mjs`, `tools/hooks/git_guard_impl.mjs`, `tools/hooks/test_guards7.mjs`.
Changed: `tools/hooks/shell_lex.mjs`, `delete_guard_impl.mjs`, `state_guard_impl.mjs`, `lib.mjs` (transcript helper), `handin_gate_impl.mjs` (uses it), `tools/backup_reference.mjs`, `tools/gates_lib.mjs` (`needsIsConcrete`), `tools/test_isolation.mjs` (`cleanupTmp`), `tools/test_gates.mjs`, `tools/hooks/test_hooks.mjs`, `tools/hooks/test_guards6.mjs`, `tools/test_backup_reference.mjs`, `.claude/settings.json` (git_guard wired beside delete_guard).
