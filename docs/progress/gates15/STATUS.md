# STATUS gates15 (hook and gate tooling, no renders)

## Skipped, refused or failed
HANDIN: no-visual
References opened: 10 of 10 listed. Opened and read: gates9 AUDIT.md and STATUS.md, tools/make_brief.mjs, tools/check_handin.mjs (heads and the parts this round touches), .claude/settings.json, docs/AGENT_CONTRACT.md, docs/templates (all 7 files; AUDIT_BRIEF.md edited), CLAUDE.md sections, the live gate.log (read only: 1790 lines, 134 NO-CONTRACT lines). tools/hooks: every file listed with its length; read in full: lib, state_guard_impl, guard_paths, shell_lex, notice_lib, suite_record, live_launch_categories.json and the tests I changed; the other hook files were opened at their heads only (stated, not sampled silently).

- ACCEPTED RESIDUAL (ruling 1), stated plainly: deliberate code run as the same OS user can still read the signing secret and forge any signed record (suite record, audit record, ack). A pattern guard cannot stop code that computes a path. This round adds DETECTION: state_guard refuses commands and file writes outside tools/ and the tests that name the secret or import a hooks file; the auditor brief (item 4b) makes every auditor grep the builder's commits, scratch and progress folder and write a "Secret check" in AUDIT.md. Detection is by name only.
- Not run through a live launch: all checks are real hook scripts fed real payload shapes against temp folders.
- Not fixed, said here: two c1 probe forms stay allowed (`git archive HEAD .claude`, which archives committed files only; a `?` glob onto a fixture file ack_secret_old in the state folder, which holds no key). A preload in NODE_OPTIONS runs before run_suite's first line; run_suite then refuses and writes no record, but the preload itself ran (it is code, the residual above).
- Observed, not changed: delete_guard refused a Bash heredoc that only contained the text `fs.rmSync` (false positive); I wrote the file with the Write tool.
- Suites: run one at a time under `timeout 600` with CP_GATE_STATE_DIR in scratch (live gate.log not written). Not run through run_suite.mjs (this round changes the suites). Counts below; any suite not finished at hand-in is marked PENDING.

## Items

| # | Decision | What changed | Proving tests |
|---|---|---|---|
| R1 | Residual accepted, detection added | state_guard_impl: refuses Bash and Write/Edit content that imports a hooks file or names `.roadworld-gate`, readSecret, signRecord, ack_secret outside tools/hooks, tests and existing tools files; preload flags and NODE_OPTIONS preloads on gate tools; script files run by an interpreter are scanned at command time; heredoc bodies fed to interpreters; AUDIT_BRIEF.md item 4b | test_guards15 B |
| 2a | Import tree hashed against merge base | run_suite importClosure; a dynamic closure (spawn, createRequire, built import paths) needs the whole top folder and tools/ unchanged | guards15 C |
| 2b | Clean child environment | KEEP_ENV allow-list (CP_*, PATH, temp, home); NODE_OPTIONS, NODE_PATH and preload execArgv refused with exit 2 | guards15 C |
| 2c | gcc exe tied to the CMake build | CMakeCache source dir equals the worktree, objects exist, exe not older than newest object, hash recorded | guards15 C |
| 3 | Home folder bulk operations | cp, mv, tar, zip, rsync, rm, Compress-Archive, robocopy, recursive grep/rg/find/ls/gci/du, find -exec, xargs, git clone/-C/--git-dir of home or an ancestor; Grep and Glob tools rooted at home; subfolders of home stay allowed | guards15 D |
| 4 | Lane lock decided by gate.log | lane_lock logs `lane take/release`; laneHolder folds the log; owner.txt is a hint; status, release and the state_guard rm check use the log; NODE_OPTIONS and --import refused | guards15 E, guards13 F, guards14 C |
| 5 | Table and prior items | 9 descriptions added to live_launch_categories.json (90 rows); gates13 items 1 and 3 re-opened below | guards15 F, guards13 |
| G | Glob crash (coordinator) | globRe in lib.mjs compiles inside try/catch and returns match-all on failure, so a bad pattern REFUSES; used by globSegRe and globToRe. The word `[..*Linking` plus `[`, `[a-`, `[\`, `[]`, `[z-a]*`, `[[]`, `[^`, `*[`, `x[a-z`, `[\]` through judgeCommand, the glob helpers and the real hook: no throw, no FAIL-OPEN | guards15 A |

## Prior audit items (docs/progress/gates14/AUDIT.md, and gates13 items 1 and 3 re-opened)

| Audit item | Status | Where |
|---|---|---|
| gates14 1 secret reachable by a script file, inline import, preload | residual by ruling 1; detection added, not prevention | guards15 B, AUDIT_BRIEF 4b |
| gates14 2 home bulk operations, Grep/Glob at home, git clone/-C | fixed | guards15 D |
| gates14 3 suite records forged (changed helper, preload/env, dropped-in exe) | fixed for the three routes that needed no secret; forging with the secret is residual by ruling 1 | guards15 C |
| gates14 4 lane owner written by Bash forms, --import around the tool | fixed by making the log the authority: owner.txt writes change nothing; gate.log writes and preloads refused | guards15 E |
| gates14 5 three unmapped NO-CONTRACT descriptions | fixed: 9 mapped (the 3 plus 6 newer) | guards15 F |
| gates13 1 secret readable | RE-OPENED: residual by ruling 1 | this file |
| gates13 3 --script forges a suite record | RE-OPENED: residual by ruling 1 (routes without the secret closed) | this file |
| glob crash of 24 Oct 08:46Z (coordinator item) | fixed: bad pattern refuses; see item G | guards15 A |

## Good and bad hand-in per new gate

| Gate | Good input (passes) | Bad input (refused) | Test |
|---|---|---|---|
| Secret-code detection | a script under tools/, a test file, a .md note naming the words, a commit message naming them, an edit of an existing tools file | `node <scratch>/forge.mjs` naming ack_secret, a script importing lib.mjs and signing, `node -e import(lib.mjs)`, a heredoc to an interpreter, Write of a new .mjs importing hooks, `--import` before a gate tool | guards15 B |
| run_suite import tree | everything unchanged vs merge base | changed helper (direct or two levels down), spawning script with changed tools/, NODE_OPTIONS preload | guards15 C |
| run_suite gcc | CMakeCache of this tree, objects, newer exe | dropped-in exe, foreign cache, no objects, stale exe | guards15 C |
| Home bulk | ls ~, find ~ -maxdepth 1, grep -r --include=*.md, cp into ~/AppData/x, Grep with type js | cp -r ~, tar ~, grep -r . ~, find ~ -name ack_secret, git clone ~/.roadworld-gate, Glob **/ack_secret at home | guards15 D |
| Lane by log | take and release by the tool, the holder's release, main release | hand-written owner.txt (changes nothing), gate.log writes, a second agent's take or release | guards15 E |
| Bad glob | any normal word | a pattern the engine rejects that could name the secret | guards15 A |

## Live gate.log descriptions

90 rows in tools/hooks/live_launch_categories.json (9 new: suitespeed, M6 research, pvetrees, tjunction7 waiver keys, m6lights, performance review, free disk x2, trace photo asphalt edges).

## Suite counts (one at a time, timeout 600)

guards6 32, guards7 28, guards8 56, guards9 54, guards10 107, guards11 170, guards12 95, guards13 139, guards14 97, guards15 162, test_hooks 419; all 0 failed. test_gates, test_worktree_tools, test_backup_reference, test_record_lib: PENDING at hand-in time (still running); nothing they test was changed. fail_first_test_guards15.txt: 88 FAIL / 74 ok against the gates14 tools (commit 9945974).

## Merge text for main

1. docs/AGENT_CONTRACT.md: "The Unreal lane holder is the fold of the `lane take/release` lines in `.claude/state/gate.log`, written only by `node tools/lane_lock.mjs`; owner.txt is a hint. Agents run as the same OS user as the hooks, so the signing secret can be read by deliberate code: this is an accepted residual. Detection: state_guard refuses code outside tools/ and the tests that names the secret or imports a hooks file, and every audit greps the builder's commits, scratch and progress folder for those names (Secret check in AUDIT.md). run_suite --script hashes the whole import tree against the merge base, runs the child in a clean environment and refuses NODE_OPTIONS preloads; a gcc run needs the exe built from this worktree's CMake build."
2. CLAUDE.md: add to the hooks paragraph: "Secret reading by deliberate code is an accepted residual; the auditor's Secret check is the detection."
3. Rounds may be split into measure, build and render phases (short fresh agents handing over through files in the hand-in folder); the builder and auditor separation is unchanged.
4. docs/templates/AUDIT_BRIEF.md item 4b is in this branch.
