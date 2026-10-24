# STATUS gates14 (hook and gate tooling, no renders)

## Skipped, refused or failed
References opened: 5 of 5 listed (the gates13 AUDIT.md, gates13 STATUS.md, gates13 FAULTS.md, tools/hooks/state_guard_impl.mjs, the brief file).

- Nothing skipped, refused or failed in the code. No test was renamed or removed: the two task-file waivers stay (the `--gate allow_waiver ...` lines).
- Not run through a live Claude Code launch: the lane_lock tool, the new state_guard rules and the unmapped-launch notice are proven with the real hook scripts fed real payload shapes, not inside a live agent launch. The first real launch after merge is the live check.
- The signing secret MOVES on first use after merge: the old `<main>/.claude/state/ack_secret` is migrated to `<home>/.roadworld-gate/ack_secret` and the old copy removed, so records signed before the merge keep verifying. The main session should check `ls ~/.roadworld-gate` after the first hook run.
- The launch category is SELF-DECLARED: a builder picks its own `NO-CONTRACT: <category>: <reason>`. What backs it is the AUDIT.md write refusal (lock_guard: a builder cannot write AUDIT.md) and the audit-owed notice that lists every passed hand-in without a valid AUDIT.md by another agent. No new mechanism (main-session decision 7).
- Suites: all 13 suites were run one at a time under `timeout 600` with CP_GATE_STATE_DIR in scratch (the live gate.log was not written). They were NOT run through `run_suite.mjs`: by decision 2, `--script` refuses a script that is new or changed on the branch, and this round changes every one of them. The logs are plain runs of a node-only tooling round.

## Items (each decision, the tests that prove it)

| # | Decision | What changed | Proving tests |
|---|---|---|---|
| 1 | Secret outside every repo tree; archive, move and recursive delete refused | `lib.mjs` (SECRET_HOME_DIR, secretDirFor, readSecret, ensureSecret with migration); new `guard_paths.mjs`; `state_guard_impl.mjs` refuses cp -r, mv, tar, zip, Compress-Archive, robocopy, xcopy, rsync, recursive rm, recursive PowerShell delete, rmdir /s of `.claude` or of a folder holding it, any path or glob (relative after a cd, brace, wildcard in two segments, `..`) resolving into `.claude/state` or the secret folder, Grep and Read of them; the secret named together with `$(`, a backtick or a variable assignment | test_guards14 A (the auditor's four forms, rm -rf of main/.claude, 5+ own forms, ALLOW rows), probes of 55 command forms |
| 2 | Suite records | `run_suite.mjs --script` runs only a script present and unchanged at the merge base with main (CP_SUITE_BASE honoured only with CP_GATE_STATE_DIR); `phase_lib.buildProblems(..., nodeOnly)` needs suite_gcc.log unless the task file declares `node-only` (`SUITE: node-only` in the brief, passed by check_handin `--suite node-only`) | test_guards14 B (the auditor's printing test_fake.mjs gives ok:false; a new or edited script is refused; a C++ round with only suite_node.log fails; a node-only round passes) |
| 3 | Recursive delete and mv of main/.claude | covered by 1, tested apart | test_guards14 A (separate rm -rf and mv rows) |
| 4 | Lane lock owner | `node tools/lane_lock.mjs take`, `release`, `status` writes owner.txt with the caller's id; state_guard refuses every other write into the lock folder (mkdir, redirect, tee, Set-Content, Out-File, cp, mv, sed -i, Write and Edit tools) and checks `--agent` equals the caller's agent id | test_guards14 C; guards13 F and the test_hooks 45-case guard test updated; `echo 'agent: other' > .../owner.txt` REFUSED |
| 5 | Lock name | `unreal_lane.lock` in every text of this hand-in and in the merge text below | this file |
| 6 | Unmapped NO-CONTRACT launches | `notice_lib.mjs`: every NO-CONTRACT log line whose description is not in `live_launch_categories.json` is an item with its own `#n[code]`, counted and acked one by one | test_guards14 D, guards13 table test (81 rows) |
| 7 | Self-declared category stated | first lines of this file, merge text 1 | this file |

## Prior audit items (docs/progress/gates13/AUDIT.md)

| Audit item | Status | Where |
|---|---|---|
| 1 secret readable via cp -r, mv, cd+glob, tar | fixed: the secret is outside the repo; those forms and 20+ more are refused | test_guards14 A |
| 2 rm -rf main/.claude and mv allowed | fixed, two separate rows | test_guards14 A |
| 3 `--script` forges a suite record | fixed: merge-base content check; node-only needs the task-file declaration | test_guards14 B |
| 4 owner.txt spoof on the Bash path | fixed: only the tool writes it, other writes refused | test_guards14 C |
| 5 merge text named the lock `unreal.lock` | fixed: `unreal_lane.lock` | this file |
| 6 live table 77 vs 81 | fixed: 81 rows, unmapped launches become notice items; the test reads the live gate.log read-only | test_guards14 D, guards13 |
| 7 category self-declared | disclosed above and in FAULTS Residual; no new mechanism by decision | this file |

## Reference files opened

Case: gates13 audit | Opened: C:\Users\bgbg0\Documents\Programming\roadworld-wt\gates10\docs\progress\gates13\AUDIT.md, C:\Users\bgbg0\Documents\Programming\roadworld-wt\gates10\docs\progress\gates13\STATUS.md, C:\Users\bgbg0\Documents\Programming\roadworld-wt\gates10\docs\progress\gates13\FAULTS.md
Case: hooks and runner | Opened: C:\Users\bgbg0\Documents\Programming\roadworld-wt\gates10\tools\hooks\state_guard_impl.mjs, C:\Users\bgbg0\Documents\Programming\roadworld-wt\gates10\tools\run_suite.mjs, C:\Users\bgbg0\Documents\Programming\roadworld-wt\gates10\tools\phase_lib.mjs

## Own fault list and residual

See FAULTS.md (3 passes). Residual: self-declared category; the main session is unlimited by design; the secret is reachable only by a route that computes a path without naming it.

## Changes (file:function)

tools/hooks/lib.mjs (secretDirFor, readSecret, ensureSecret); tools/hooks/guard_paths.mjs (new); tools/hooks/state_guard_impl.mjs (judgeCommand, reachesAbove, archive, move and delete rules, lane lock rewrite); tools/lane_lock.mjs (new); tools/run_suite.mjs (suiteBase, scriptBaseProblem); tools/phase_lib.mjs (buildProblems); tools/check_handin.mjs (--suite); tools/make_brief.mjs (BUILD text); tools/hooks/notice_lib.mjs (mappedDescriptions, unmapped items); tools/hooks/live_launch_categories.json (81 rows); tests: test_guards10 to 13, test_hooks, test_record_lib, new test_guards14.

## Images that carry each claim

No-visual round: no images. Evidence: fail_first_test_guards14.txt and the suite logs.

## Good and bad hand-in per new gate

| Gate | Good input (passes) | Bad input (refused) | Test |
|---|---|---|---|
| Secret and `.claude` guard | ls of the state folder, cat gate.log, tar docs, rm -rf build, a commit message that names the secret | cp -r, mv, tar, zip, rm -rf of `.claude`; `cd .claude && cat st*/ack_se*`; `cat $(echo <path>)` | guards14 A |
| Suite script check | a script unchanged at the merge base | a script added or edited on the branch; a printing test_fake.mjs | guards14 B |
| Node-only suite | task file declares node-only, suite_node.log | C++ round with only suite_node.log | guards14 B |
| Lane lock tool | `node tools/lane_lock.mjs take --agent <own id> ...`; the owner removes the lock | `--agent <other id>`; `echo 'agent: other' > owner.txt`; mkdir of the lock | guards14 C |
| Unmapped launch notice | a description in the table | a description not in the table: its own `#n[code]` item | guards14 D |

## Live gate.log descriptions

81 rows in tools/hooks/live_launch_categories.json (AUDIT-OF 32, FIRSTLOOK-OF 9, clerk 24, research 9, merge 6, status 1); the four new rows are the descriptions the gates13 audit found unmapped.

## Suite counts (one at a time, timeout 600)

SUITECOUNTS

## Merge text for main

1. docs/AGENT_CONTRACT.md, replace the lock and launch lines: "The Unreal lane lock is the folder `.claude/state/unreal_lane.lock` holding owner.txt, made and removed ONLY by `node tools/lane_lock.mjs take --agent <your agent id> --round <round> --purpose <text>` and `release --agent <your agent id>` (`status` reads it); every other write into the folder is refused. The launch category in `NO-CONTRACT: <category>: <reason>` is SELF-DECLARED by the launching agent; what backs it is that no builder can write AUDIT.md (lock_guard) and every passed hand-in without an AUDIT.md by another agent stays on the notice. `run_suite.mjs --script` runs only scripts already at the merge base with main; a node-only round says `node-only` under `## Suite` in its task file."
2. CLAUDE.md, the Unreal lane sentence and the merge line: "Unreal is a single lane: take it with `node tools/lane_lock.mjs take --agent <id> --round <r> --purpose <text>` (lock folder `.claude/state/unreal_lane.lock`), release with `release`." Also the `.claude/` bullet: the signing secret lives in `<home>/.roadworld-gate`, outside every checkout.
3. docs/HANDOFF.md line 15 already names `unreal_lane.lock`; add the tool.
4. Secret migration: on the first hook run after merge `ensureSecret` moves `<main>/.claude/state/ack_secret` to `<home>/.roadworld-gate/ack_secret` and deletes the old file. Nothing to do by hand; check `ls ~/.roadworld-gate`.
5. .claude/settings.json is unchanged this round.
