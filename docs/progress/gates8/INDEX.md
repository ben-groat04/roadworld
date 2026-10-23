# Index, round gates8 (engine and tooling: no renders)

HANDIN: no-visual. The evidence of this round is runs and tests, not pictures.

| File | What it holds |
|---|---|
| STATUS.md | first section (what was skipped or unverified), Prior audit items (the 21 items of docs/progress/gates7/AUDIT.md, with the five decision-D items marked), references opened (12 of 12), changes, locked tests and checkpoint hand-back as built, suite counts, the updated merge checklist, proposed contract and CLAUDE.md text |
| FAULTS.md | Pass 1 to 3 (own attack on the lock guard, on the delete and git fixes, what a builder or the owner would say), Residual, and "Residual (accepted by main session, 23 Oct)" |
| COMPARE.md | no-visual note |
| INDEX.md | this list |

Tests (each run one at a time): `node tools/test_gates.mjs` 514, `node tools/hooks/test_hooks.mjs` 412, `node tools/hooks/test_guards6.mjs` 32, `node tools/hooks/test_guards7.mjs` 28, `node tools/hooks/test_guards8.mjs` 56 (new), `node tools/test_worktree_tools.mjs` 27, `node tools/test_backup_reference.mjs` 56.
New files: `tools/hooks/lock_guard.mjs`, `tools/hooks/lock_guard_impl.mjs`, `tools/hooks/test_guards8.mjs`, `tools/clean_test_temp.mjs`.
Changed: `tools/hooks/shell_lex.mjs`, `delete_guard_impl.mjs`, `git_guard_impl.mjs`, `state_guard_impl.mjs`, `lib.mjs`, `handin_gate_impl.mjs`, `tools/check_handin.mjs`, `tools/gates_lib.mjs`, `tools/hooks/test_hooks.mjs`, `.claude/settings.json` (lock_guard wired).
