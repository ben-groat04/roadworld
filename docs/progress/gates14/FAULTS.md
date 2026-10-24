# FAULTS gates14 (own fault passes, written before any audit)

## Pass 1

1. test_hooks, guards10 and guards11 broke after the secret and lane-lock changes: the temp mains of test_hooks hold copies of the hooks, so the secret was signed in two places. All hook children and the test process now share one secret folder (CP_GATE_SECRET_DIR); the no-secret ack case and the signRecord unit case turn it off locally. Fixed.
2. `rm -r <lock folder>` by the owner was refused as "a recursive delete of a folder that holds .claude" (reachesState also matched paths inside the state). Split into `reachesAbove`; the owner's rm of the lock is allowed again (probe and test row). Fixed.
3. test_guards14 B could not verify its own run_suite records (the child signed with another secret dir). The test sets CP_GATE_SECRET_DIR for its process. Fixed.
4. The lock-folder rows of guards12/13 and test_hooks that expected `mkdir` and `owner.txt` writes to be allowed now expect REFUSE, with ALLOW rows for `node tools/lane_lock.mjs take|release --agent <own id>`. Fixed.
5. test_hooks' temp main lacked `guard_paths.mjs` and `lane_lock.mjs` (state_guard crashed in the copy, FAIL-OPEN). Added to the copy list. Fixed.

## Pass 2 (probes not in the auditor's list: 24 forms against judgeCommand)

1. `cat $(echo <main>/.claude/state/ack_secret)`, the backtick form, and `x=<path>; cat $x` were ALLOWED (the path is named but sits inside a substitution or an assignment). state_guard now refuses the secret's name or folder combined with `$(`, a backtick, a variable assignment, `$env:` or Set-Variable; three REFUSE rows in test_guards14 A. Echo of the path and a commit message that mentions it stay allowed. Fixed.
2. Forms that already refused and are now rows: `$HOME`/`$USERPROFILE` paths, `cd ~ && cat .roadworld-gate/ack_secret`, rsync, `cp -r .cl*`, `cp -r .` from main, `.CLAUDE` case, `//` and `/./` path noise, pushd, find -exec cat, a symlink to the state folder. No change needed.
3. `git -C <main> archive HEAD` is allowed: it archives committed files only and `.claude/state` is gitignored, so nothing secret is in it. Not a fault.

## Pass 3

1. The make_brief BUILD text did not tell builders that `--script` now needs a script already at the merge base, or that a node-only round needs suite_node.log. Text added. Fixed.
2. The state_guard header comment still described the old lock (mkdir plus owner.txt by anyone). Rewritten for the tool. Fixed.
3. All 13 suites re-run one at a time after the last code commit (see STATUS).

## Residual

- The launch category is self-declared (a builder chooses its own `NO-CONTRACT: <category>`); what backs it is the AUDIT.md write refusal and the audit-owed notice. No new mechanism (main-session decision 7).
- The main session (no agent id) is not limited by state_guard, as designed; if a harness omitted agent_id for a subagent the guard would protect nothing.
- An agent that runs as the same OS user could in principle reach the secret by a route state_guard does not parse (obfuscated string building inside node code that never names the file). The guard refuses naming; it cannot refuse computing. Disclosed, not fixable by pattern.
- Not exercised in a live launch: the lane_lock tool and the new guards are proven with the real hook scripts fed real payload shapes.
