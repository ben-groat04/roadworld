# Index, round gates10 (tooling: no renders)

HANDIN: no-visual. The evidence of this round is runs and tests, not pictures.

| File | What it holds |
|---|---|
| STATUS.md | skipped or unverified, Prior audit items (11 gates9 faults plus the delete_guard false positive), references opened, what was built, gate runs on made-up good and bad hand-ins, suite counts, merge notes, paste-ready text (item 26, amended item 24, CLAUDE.md line) |
| FAULTS.md | own fault passes and Residual |
| COMPARE.md | no-visual note |
| fail_first_test_guards10.txt | `test_guards10.mjs` run against main 5fe652c's tools: 79 failed, 28 passed |
| INDEX.md | this list |

Tests (each run alone under `timeout 600`): test_gates 514, test_hooks 412, test_guards6 32, test_guards7 28, test_guards8 56, test_guards9 54, test_guards10 107 (new), test_worktree_tools 27, test_backup_reference 56.
New: `tools/hooks/test_guards10.mjs`, `tools/hooks/lock_extra.mjs`, `tools/hooks/phase_marks.mjs`, `tools/phase_lib.mjs`, `tools/image_lib.mjs`, `docs/templates/FIRSTLOOK_BRIEF.md`, `docs/templates/FIRSTLOOK.md`.
Changed: `tools/make_brief.mjs`, `tools/check_handin.mjs`, `tools/gates_lib.mjs`, `tools/hooks/brief_gate_impl.mjs`, `handin_gate_impl.mjs`, `lock_guard_impl.mjs`, `delete_guard_impl.mjs`, `tools/clean_test_temp.mjs`, `tools/test_isolation.mjs`, `tools/test_backup_reference.mjs`, `tools/hooks/test_guards8.mjs`, `tools/hooks/test_hooks.mjs` (fixture copy lists only).

Files opened beyond the three Case lines in STATUS.md: every file in `tools/hooks`, every file in `docs/templates`, `.claude/settings.json`, `docs/progress/gates9/{FAULTS,INDEX}.md`, the 23 Oct NO-CONTRACT launch lines of the main checkout's `.claude/state/gate.log` (read only).
