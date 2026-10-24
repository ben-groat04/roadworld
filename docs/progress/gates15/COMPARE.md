# COMPARE gates15

No-visual round: no renders, no reference photos. The comparison is before and after on the same tests: `fail_first_test_guards15.txt` is `test_guards15.mjs` (162 rows) run against an export of commit 9945974 (the gates14 tools): 88 FAIL, 74 ok (the ok rows are the ALLOW rows, controls and fixtures). After the round: 162 passed, 0 failed.

Where the expected values came from: the probes of `docs/progress/gates14/AUDIT.md`, the main-session ruling of 24 Oct 2026 and the coordinator's glob-crash note (the word `[..*Linking`). No test pins a fault; the c4 owner.txt forms moved from REFUSE to harmless by the ruling and are tested as such.
