# FAULTS gates11

No-visual round. Method: real hook scripts fed real-shaped JSON in temp checkouts, scratch cases, a sweep of the live NO-CONTRACT descriptions, the full suites. Fail-first against the round-10 tools.

## Pass 1 (own fault list while building)

1. Backslashes lost in shell heredocs (SyntaxErrors in generated scripts). Fixed: edit tools only.
2. `porcelainPath` sliced three characters and let `git stash` pass; fixed with a regex.
3. `git stash -- <path>` skipped because paths after `--` counted as positions; fixed.
4. `import('fs').then(m=>m.rmSync(...))` allowed; added `ANY_RECV`.
5. Clerk prompt "audit of docs/progress/roundx was accepted" refused; prompt regexes anchored to line start.
6. The "never built" placeholder was not caught; regex fixed.
7. fail_first `9 passed, 2 failed` not recognised; fixed.
8. Write-then-run heredoc allowed; added `runsAnyOf`.

## Pass 2 (cold read as an auditor)

1. Test fixtures depended on the machine: the Start-Process allow case was refused as a duplicate whenever a real suite was running. The fixture now defaults the process list empty.
2. The `git revert` expectation was wrong; added a commit touching only other.mjs.
3. The make_brief emission test needed the `## Faults` and `Case A:` lines; CASE appears only in the measure brief.
4. `timeout` typed inside the PowerShell tool would have counted as a limit though PowerShell has no such command; it no longer does.
5. Quick forms (--help, ctest -N) were treated as runs; now exempt.

## Pass 3 (what would the owner notice)

1. Would a normal builder be refused? Live sweep of 76 real NO-CONTRACT descriptions: no false refusal; the allowed forms of every guard have ALLOW tests.
2. Would the time guard slow every command? The process lookup is bounded to 300 ms, runs only for commands already matching a long-job name, and fails open with a log line.
3. Does any test write the live gate.log? Checked: all suites run with CP_GATE_STATE_DIR set.

## Residual

Residual only unfixable: the duplicate check matches by image name, not path, so another worktree's run can refuse for up to its limit (the process list gives no cheap path); variables and `$(...)` paths are invisible to lock_guard; an agent can open a first-look image without looking, and no live launch has run through the new gates yet (first use is the test).
