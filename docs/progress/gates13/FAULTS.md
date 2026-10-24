# FAULTS gates13 (own fault passes, written before any audit)

## Pass 1

1. The first H (regex) tests passed vacuously: `reEsc` had been written with a no-op escape (`'\$&'`). Found by reading the helper's output on `[`; fixed, tests re-run, and a direct reEsc/safeRe unit row added. Fixed.
2. test_guards13 first reported a handin_gate FAIL-OPEN that was not a crash (no readable agent transcript). The crash pattern now matches only "hook implementation could not load or crashed", and the hook gets a real transcript. Fixed.
3. The wired-hook walker skipped the `args` arrays of settings.json, so some hooks were not fed the crash shape. Fixed; all 10 scripts are fed.
4. The "outside script" test failed for the wrong reason (run_suite rejected the script name). Renamed so only the location is wrong. Fixed.
5. The filtered-run test matched an unfiltered record by content hash. Different output for the filtered script; fixed.

## Pass 2

1. fail_first for guards13 hit missing exports and the missing run_suite on the gates12 code and crashed the file; the assertions are now tolerant, so the file lists each FAIL line (61 ok / 44 FAIL). Fixed.
2. fail_first_test_hooks first ran against an archive without docs/AGENT_CONTRACT.md and showed a contract_inject FAIL-OPEN; docs added to the archive, rerun. Fixed.
3. `disguisedJob` and its RE_ constants were dead code; removed with their guards11 test. Stale comments (phase_lib header, time_guard_impl T2) rewritten. Fixed.
4. A suite log copied into the hand-in folder did not match a record by path; records now also match by log sha. Fixed.

## Pass 3

1. `node -p`, `--eval` and `bash -c echo` printed logs (audit item 2 variants) have no record, the same condition as the `node -e` row; rows added to test_guards13 C. Fixed.
2. The suites were re-run after the last code commit so every log is newer than it. Done.

## Residual

- The launch allow-list and run_suite flow were not exercised in a live agent launch (disclosed first in STATUS). Not fixable from inside a builder run.
- The signed records are per machine (state dir under the checkout of the main session); a hand-in checked on another machine has none. By design.
- A malicious agent with a non-agent write path to the state dir could forge a record; state_guard blocks agent tool writes, and the main session is deliberately unrestricted.
