# STATUS gates13 (hook and gate tooling, no renders)

## Skipped, refused or failed
References opened: 6 of 6 listed (the gates12 AUDIT.md, tools/hooks/state_guard_impl.mjs, tools/hooks/brief_gate_impl.mjs, tools/hooks/time_guard_impl.mjs, tools/phase_lib.mjs, the brief file; every hook for the regex scan).


- Nothing skipped, refused or failed in the code. No test_hooks.mjs or test_gates.mjs test was renamed or removed this round: no new WAIVER NEEDED (the two existing task-file waivers stay).
- Not run through a real Claude Code launch: the new NO-CONTRACT allow-list, `tools/run_suite.mjs` flow and the state_guard lane-lock rule are proven with the real hook scripts fed real payload shapes (stdin JSON, cwd = main checkout), not inside a live agent launch. The first real launch after merge is the live check; if the allow-list refuses a legitimate launch, gate.log names the category it expected.
- The PostToolUse time-limit hook (`time_guard_post`) is gone on purpose (decision 2); settings.json no longer wires it. The merge text below lists the settings change.
- Hook-crash policy unchanged: hooks still fail open (a `FAIL-OPEN` log line), but no hook builds a regex from input text any more (decision 8), so the 23 Oct 22:02 crash shape cannot recur.
- Suite logs: all 12 suites were run one at a time through `node tools/run_suite.mjs`, each under `--timeout 600`, with `CP_GATE_STATE_DIR` in scratch (the live gate.log was not written). The signed records live in that scratch state dir on this machine only; `check_handin --phase build` on another machine would find no record (as designed: a record is per machine).

## Items (each decision, the tests that prove it)

| # | Decision | What changed | Proving tests |
|---|---|---|---|
| 1 | Launch allow-list | brief_gate accepts only the contract, BRIEF-FILE, AUDIT-OF, FIRSTLOOK-OF or `NO-CONTRACT: <clerk\|merge\|research\|status\|measure>: <reason 20+ chars>`; free text refused with the categories named; review-word matching deleted (`disguisedJob` removed from phase_marks.mjs) | test_guards13 section B (allow rows, the 13 audit evasion phrasings as REFUSE, all 77 live descriptions through the table below, no `disguisedJob` import); test_guards10, 11, 12 updated |
| 2 | One runner, signed record at the end | `tools/run_suite.mjs` runs the suite, writes the log and an HMAC-signed record at the END; `findSuiteRun` accepts only a matching record (path or log sha, signature, worktree, binary inside worktree, hash, unfiltered, ended after the last code commit); PostToolUse recording and `time_guard_post` deleted; state dir not writable by agents | test_guards13 section C (typed log, `node -e` printed log, edited log, wrong worktree, filtered run, binary outside worktree, edited record, background not ended, hung suite killed at the limit with exit 124); test_guards12 B rewritten |
| 3 | Worktree from the command | time guard resolves the root per job from `cd`, `git -C`, absolute program or redirect path, or the run_suite argument; tests use cwd = main | test_guards13 section D (cwd = main with `cd <wt> &&`, `git -C`, absolute program path, REFUSE same worktree, ALLOW other worktree) |
| 4 | Prefix runners | `skipRunnerArgs` in shell_lex.mjs: taskset, nice, env, stdbuf, time, chrt, xargs, start, `cmd /c`, `powershell -c`, `bash -c`, timeout; judged by position | test_guards13 section E (`taskset -c 0 ninja` and the rest, REFUSE and ALLOW rows) |
| 5 | Hand-in form | this STATUS, fail_first files, suites green, merge text | check_handin run (see Suite counts) |
| 6 | Release writes a builder record | a hand-in released after 3 refusals writes `<state>/released/<key>.json` (agent id, folder, released:true); handle_gate and `check_handin --accept` read it; `--accept` still fails until the hand-in passes | test_hooks (6 new D6 cases: record present, no owed notice, auditor not blocked for "no record", v1-authored AUDIT.md refused with "never the same agent", different auditor passes, `--accept` on the released folder fails) |
| 7 | One lane-lock form | the lock is a directory made with mkdir plus owner.txt (agent id, round, time, purpose); state_guard allows exactly that write and only the owner may remove or rewrite it; file form refused; grep and read allowed | test_guards13 section F (REFUSE and ALLOW rows), test_hooks 45-case state guard test |
| 8 | No regex from command text | `reEsc` and `safeRe` in lib.mjs; state_guard, delete_guard, lock_guard use them; a C++ heredoc with unbalanced `[` and escaped quotes is fed to all 10 wired hooks | test_guards13 section H (every wired hook: no crash, a decision made; a scan of every hook for `new RegExp(` on input-derived text; reEsc/safeRe unit rows) |

## Prior audit items (docs/progress/gates12/AUDIT.md)

| Audit item | Status | Where |
|---|---|---|
| P1.1 wrong worktree root for agent calls | fixed (decision 3) | test_guards13 D, all with cwd = main |
| P1.2 record forgeable by `node -e` printed output | fixed (decision 2): a record exists only for a run of run_suite; typed, printed (`node -e`, `node -p`, `--eval`, `bash -c echo`) and edited logs have none | test_guards13 C |
| P1.3 background runs recorded as ended at once | fixed: the end is written when the run ends; a background run that has not ended has no record; the brief and error text say to use run_suite | test_guards13 C |
| P1.4 launch vocabulary list | fixed (decision 1): allow-list, no vocabulary; the 13 phrasings are REFUSE rows | test_guards13 B |
| P1.5 `taskset` and unknown wrappers | fixed (decision 4) | test_guards13 E |
| Claim 1 (items a/b "fixed" rested on cwd = worktree tests) | tests now use cwd = main | test_record_lib.logRun, test_guards13 D |
| Claim 2 ("forgeable log: fixed" false) | the forgery tests now cover a printed log made under a command | test_guards13 C |
| Claim 3 ("by meaning" overstated) | claim withdrawn; vocabulary removed | decision 1 |
| Claim 4 (payload fields "NOT verified live") | wording dropped | this file |
| Claim 5 (77 descriptions, not 131) | confirmed; the table below holds the 77 | live_launch_categories.json |
| Claim 6 (waivers) | unchanged | task file |

## Live gate.log descriptions to launch form (77 rows, from tools/hooks/live_launch_categories.json)

Counts: AUDIT-OF 32, FIRSTLOOK-OF 9, clerk 21, research 8, merge 6, status 1. Test: test_guards13 B reads this json: every clerk, merge, research and status row is ALLOWED with its category and its real reason; every AUDIT-OF and FIRSTLOOK-OF row, launched as free-text NO-CONTRACT, is REFUSED with the categories named.

| # | live gate.log description | launch form |
|---|---|---|
| 1 | Hook live test: exempt launch | status |
| 2 | Audit gate-notice hooks independently | AUDIT-OF |
| 3 | Audit junction markings independently | AUDIT-OF |
| 4 | Fetch matched junction reference photos | research |
| 5 | Audit vegetation kit independently | AUDIT-OF |
| 6 | Audit Unreal M2 independently | AUDIT-OF |
| 7 | Find best free vegetation assets | research |
| 8 | Find best free road and world assets | research |
| 9 | Audit repo organisation, read-only | AUDIT-OF |
| 10 | Bring queue and acceptance log current | clerk |
| 11 | Audit the audit-gate tooling | AUDIT-OF |
| 12 | Reorganise repo docs for handover | clerk |
| 13 | Merger: verify playtest, merge integ to main | merge |
| 14 | Audit earthworks round 9 | AUDIT-OF |
| 15 | Audit gradelayer WorldCache layers | AUDIT-OF |
| 16 | Audit road tool round 3 | AUDIT-OF |
| 17 | Audit T-junction round | AUDIT-OF |
| 18 | Audit roundabout round | AUDIT-OF |
| 19 | Earthworks method review (read-only) | research |
| 20 | Clerk: queue, index, commit | clerk |
| 21 | Merger: roadtool3 into main | merge |
| 22 | Clerk: roundabout verdict, queue, commit | clerk |
| 23 | Clerk: photos-outrank-tests rule, commit review | clerk |
| 24 | Clerk: T-junction verdict, queue check | clerk |
| 25 | Clerk: correct made-up times in records | clerk |
| 26 | Merge roadtool3 into main | merge |
| 27 | Write round-10 and M2/M3 task files | clerk |
| 28 | Queue update and worktree cleanup clerk | clerk |
| 29 | Task files and queue clerk | clerk |
| 30 | Add every-audit-item-fixed rule to docs | clerk |
| 31 | Auditor for gates2 hand-in | AUDIT-OF |
| 32 | two owner notes, add to docs | clerk |
| 33 | Finish roadtool3 merge (Unreal steps) | merge |
| 34 | Auditor for tjunction2 hand-in | AUDIT-OF |
| 35 | Fix roadtool3 wrappers, build, merge | merge |
| 36 | Queue and acceptance log update | clerk |
| 37 | Auditor for roundabout2 hand-in | AUDIT-OF |
| 38 | Write audit first-look rule into docs | clerk |
| 39 | Auditor for gates3 hand-in | AUDIT-OF |
| 40 | Fetch case 2 and technical reference images | research |
| 41 | Queue note: approved BLOCKED wording | clerk |
| 42 | Recover the private reference photos | research |
| 43 | Write junction-safe deletion rule | clerk |
| 44 | Auditor for m2fix hand-in | AUDIT-OF |
| 45 | Recovery round 2: validate and re-fetch photos | research |
| 46 | Auditor for gates4 hand-in | AUDIT-OF |
| 47 | Opus first look: tjunction3 images | FIRSTLOOK-OF |
| 48 | Auditor for tjunction3 hand-in | AUDIT-OF |
| 49 | Opus first look: grade-r10 images | FIRSTLOOK-OF |
| 50 | Auditor for grade-r10 hand-in | AUDIT-OF |
| 51 | Queue catch-up for 22-23 Oct events | clerk |
| 52 | Opus first look: m2fix2 images | FIRSTLOOK-OF |
| 53 | Auditor for m2fix2 hand-in | AUDIT-OF |
| 54 | Re-fetch 18 truncated photos and back up | research |
| 55 | Auditor for gates5 hand-in | AUDIT-OF |
| 56 | Opus first look: roundabout3 images | FIRSTLOOK-OF |
| 57 | Auditor for roundabout3 hand-in | AUDIT-OF |
| 58 | Auditor for gates6 hand-in | AUDIT-OF |
| 59 | Opus first look: tjunction4 images | FIRSTLOOK-OF |
| 60 | Auditor for tjunction4 hand-in | AUDIT-OF |
| 61 | Opus first look: grade-r11 images | FIRSTLOOK-OF |
| 62 | Auditor for grade-r11 hand-in | AUDIT-OF |
| 63 | Auditor for m2fix4a hand-in | AUDIT-OF |
| 64 | Auditor for gates7 hand-in | AUDIT-OF |
| 65 | Clerk: queue, acceptance, workflow rules | clerk |
| 66 | Opus first look: roundabout4 images | FIRSTLOOK-OF |
| 67 | Auditor for roundabout4 hand-in | AUDIT-OF |
| 68 | Opus first look: tjunction5 images | FIRSTLOOK-OF |
| 69 | Auditor for tjunction5 hand-in | AUDIT-OF |
| 70 | Auditor for gates8 hand-in | AUDIT-OF |
| 71 | Auditor for m2fix5a hand-in | AUDIT-OF |
| 72 | Auditor for gates9 hand-in | AUDIT-OF |
| 73 | Merge gates7 then gates9 into main | merge |
| 74 | Opus first look on grade-r12 images | FIRSTLOOK-OF |
| 75 | Clerk: contract speed rules, queue, acceptance | clerk |
| 76 | Clerk: contract item 26 time limits | clerk |
| 77 | Clerk: QUEUE and ACCEPTANCE update | clerk |

## Good and bad hand-in per new gate

| Gate | Good input (passes) | Bad input (refused) | Test |
|---|---|---|---|
| Launch allow-list (brief_gate) | `NO-CONTRACT: clerk: <20+ chars>`; BRIEF-FILE; AUDIT-OF; FIRSTLOOK-OF | free text, unknown category, reason under 20 characters, a categorised launch that asks for AUDIT.md | guards13 B |
| Suite record (check_handin --phase build) | log made by `node tools/run_suite.mjs`, unedited, same worktree, binary inside it, unfiltered, ended after the last code commit | typed log, `node -e` printed log, edited log, other worktree, filtered run, binary outside the worktree, edited record, run not ended | guards13 C |
| Time guard (worktree from the command) | `cd <wt> && timeout 900 <suite>` from cwd = main; run in another worktree; run_suite with `--timeout` up to 900 | second copy in the same worktree, no limit after a cd, `taskset -c 0 ninja`, run_suite over 900 | guards13 D, E |
| Released-builder record | released hand-in: auditor finishes, `--accept` names "never the same agent" for a v1-authored AUDIT.md | `--accept` on the released folder before the hand-in passes | test_hooks D6 |
| Lane lock (state_guard) | `mkdir <state>/unreal.lock` and write `owner.txt` naming the writer; the owner removes it; grep and read | file-form lock, owner.txt naming another agent, release or rewrite by a non-owner | guards13 F |
| Regex safety (all hooks) | C++ heredoc with unbalanced `[` and escaped quotes: no crash, a decision made | (a crash = FAIL-OPEN "hook implementation could not load or crashed") | guards13 H |

## Suite counts (one at a time through run_suite, timeout 600)

test_guards6 32, guards7 28, guards8 56, guards9 54, guards10 107, guards11 170, guards12 95, guards13 136, test_hooks 419, test_gates 514, test_worktree_tools 27, test_backup_reference 56; 0 failed in all. Logs: `suite_*.log` here. fail_first: `fail_first_test_guards13.txt` 61 ok / 44 FAIL against the gates12 tools; `fail_first_test_hooks.txt` 6 FAIL (the D6 cases, the 45-case state guard test, the settings canary).

## Merge text for main

1. docs/AGENT_CONTRACT.md, new item 27: "A launch carries the contract, a BRIEF-FILE line, AUDIT-OF, FIRSTLOOK-OF, or `NO-CONTRACT: <category>: <reason 20+ chars>` with category one of clerk, merge, research, status, measure. Free text is refused. Suites run only through `node tools/run_suite.mjs <worktree> [--script <file>] --timeout <=900`; the hand-in build log is that run's log (a signed record on this machine). The Unreal lane lock is a directory made with mkdir plus owner.txt (agent id, round, time, purpose); only the owner releases it."
2. CLAUDE.md, one line under the hook paragraph: "Launch form `NO-CONTRACT: <category>: <reason>` (clerk, merge, research, status, measure); run suites with `node tools/run_suite.mjs`; the lane lock is a directory plus owner.txt."
3. .claude/settings.json: the `time_guard_post` PostToolUse entries are removed (the files are deleted); nothing else is rewired. The 10 wired hook scripts are checked by test_guards13 H.
4. The new NO-CONTRACT form: CLAUDE.md line 29 (`NO-CONTRACT: <reason, 20+ chars>`) and docs/AGENT_CONTRACT.md item 8 still describe the free-text form; replace it with `NO-CONTRACT: <category>: <reason, 20+ chars>` (clerk, merge, research, status, measure). Not edited in this branch (contract and CLAUDE.md are main files); make_brief output names run_suite and `--compiler msvc`.
5. Lane lock line: "Unreal lane lock = `mkdir <state>/unreal.lock` + `owner.txt` (agent id, round, time, purpose); state_guard allows exactly that write; release only by the owner (the main session may clear an ownerless or legacy lock)."
6. Released builders: a hand-in let go after 3 refusals writes `<state>/released/<key>.json`; `--accept` still fails until the hand-in passes.

Case: gates12 audit | Opened: C:\Users\bgbg0\Documents\Programming\roadworld-wt\gates10\docs\progress\gates12\AUDIT.md, C:\Users\bgbg0\Documents\Programming\roadworld-wt\gates10\docs\progress\gates12\STATUS.md, C:\Users\bgbg0\Documents\Programming\roadworld-wt\gates10\docs\progress\gates12\FAULTS.md
Case: hooks and runner | Opened: C:\Users\bgbg0\Documents\Programming\roadworld-wt\gates10\tools\hooks\state_guard_impl.mjs, C:\Users\bgbg0\Documents\Programming\roadworld-wt\gates10\tools\hooks\brief_gate_impl.mjs, C:\Users\bgbg0\Documents\Programming\roadworld-wt\gates10\tools\run_suite.mjs
Case: contract and rules | Opened: C:\Users\bgbg0\Documents\Programming\roadworld-wt\gates10\docs\AGENT_CONTRACT.md, C:\Users\bgbg0\Documents\Programming\roadworld-wt\gates10\CLAUDE.md, C:\Users\bgbg0\Documents\Programming\roadworld-wt\gates10\tools\check_handin.mjs
