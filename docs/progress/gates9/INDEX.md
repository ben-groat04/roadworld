# Index, round gates9 (tooling: no renders)

HANDIN: no-visual. The evidence of this round is runs and tests, not pictures.

| File | What it holds |
|---|---|
| STATUS.md | skipped or unverified, Prior audit items (F1-F3, H1-H7, C1-C5, all fixed), references opened, changes, suite counts, merge checklist, proposed rule text |
| FAULTS.md | own fault passes and Residual |
| COMPARE.md | no-visual note |
| fail_first_round8_code.txt | `test_guards9.mjs` run against the round-8 tools: 36 failed, 18 passed |
| INDEX.md | this list |

Tests (each run alone): test_gates 514, test_hooks 412, test_guards6 32, test_guards7 28, test_guards8 56, test_guards9 54 (new), test_worktree_tools 27, test_backup_reference 56.
New: `tools/hooks/test_guards9.mjs`. Changed: `tools/hooks/lock_guard_impl.mjs`, `delete_guard_impl.mjs`, `handin_gate_impl.mjs`, `tools/check_handin.mjs`, `tools/hooks/test_guards8.mjs`.
