## Skipped, refused or failed

HANDIN: no-visual

- REAL FINDING for the owner: the live `docs/reference` of the main checkout holds 18 damaged photos outside the `_damaged` quarantine, found by the new validator (`node tools/backup_reference.mjs --verify-only`): `aerial/46` to `aerial/62` (17 `*_oxon_*.jpg`, JPEG without its EOI marker, truncated; written on 23 Oct) and `street/08_wall-flint-roadside-uphill.jpg` (same). The validated mirror therefore refuses to run until these are re-fetched or moved to `_damaged`. 348 files outside the quarantine, 330 valid.
- The backup mirror was NOT run (it would refuse, correctly) and `C:\Users\bgbg0\roadworld-reference-backup` was not created or touched. No file under the real `docs/reference` was written, moved or deleted by this round (the validator only reads).
- Unverified: nothing here ran under a live Claude Code session that used these hooks. `CLAUDE_PROJECT_DIR`: the hooks documentation (code.claude.com/docs/en/hooks) says it is "the project root where the session started" and "stays put" after Claude enters a worktree or runs cd, while the `cwd` field in the hook input follows Claude; the live `gate.log` of the main checkout agrees (events of subagents carry the main checkout as cwd). The page does not address subagents specially. I could not run a probe hook inside a subagent's own session (live hooks are the main session's), so the answer rests on that page plus the log; the hooks now resolve the main checkout themselves (`root.mjs`) so the answer no longer matters for correctness, and tests run the settings.json command lines from a worktree cwd.
- Unverified: the live `.claude/settings.json` of main has `timeout: 60` for the finish hook; the measured gates need up to 270 s. This branch's settings.json has 300. If main's file is not replaced at the merge, the finish hook is killed before the scene check ends and fails open (merge notes below).
- Scene check measured against the target: `scene_check` measured 20/21, below the target 21/21 (at the floor): the one failing check is `no_bare_steps`, the baseline of main (earthworks, outside this round's files).
- My own fault, fixed: my first test runs wrote three `handin_gates` FAIL-OPEN lines into the LIVE `gate.log` of the main checkout (the main session acknowledged them). Fixed by `CP_GATE_STATE_DIR` and `tools/test_isolation.mjs`; every suite now ends with a test that no line of its run reached the real log (FAULTS Pass 2 item 8).
- The disk filled up twice during the round (ENOSPC, not from this round: `C:` showed 0 bytes free for minutes); one scratch file came out empty and was rewritten; no repository file was lost (git status and the suites checked afterwards).
- BLOCKED items: none. Nothing pushed or merged (as briefed). No Unreal, no C++ build, no engine file changed.
- `ref_seed` is NOT run: the reference register on main is empty, so reference provenance prints FAIL-OPEN (logged) and does not refuse (test "check_handin v3 with no register ... names `node tools/ref_seed.mjs`"). It is a merge step (checklist below). The hand-drawn render plus matching mask and the made-up photo registered as a reference stay open: policy only, not enforced (FAULTS Residual 3).

References opened: 11 of 12 listed
not opened: C:\Users\bgbg0\Documents\Programming\roadworld\tools\hooks\test_hooks.mjs (the 12th entry is the folder `tools\hooks` of the main checkout: its 7 hook files were read whole; main's old 283-line `test_hooks.mjs` was not read, its 1200-line successor in this branch was read and run)

## Prior audit items

Previous audit: `docs/progress/gates4/AUDIT.md` ("For the next builder", 12 items). TH = `node tools/hooks/test_hooks.mjs`, TG = `node tools/test_gates.mjs`, TW = `node tools/test_worktree_tools.mjs`, TB = `node tools/test_backup_reference.mjs`. The payload shapes in TH are the harness's: common fields (`session_id`, `transcript_path`, `cwd`, `hook_event_name`, `tool_use_id`, `agent_id`, `agent_type`) plus the tool's own `tool_input` (Bash and PowerShell `command`, Grep `pattern/path/glob`, Read/Write/Edit `file_path`).

| Item | Status | Proof (test or run) or reason a/b with evidence |
|---|---|---|
| 1 lane lock exempt for every agent | fixed | TH "state guard: 44 real-payload cases": touch, rm -f, test -f && cat, PowerShell New-Item and Remove-Item, redirect into it, Write and Read tools on `unreal_lane.lock` allowed; allowed for the main session too. Expected value: main session decision 23 Oct (lane lock allowed for every agent). |
| 2 robocopy judged by destination | fixed | TH "delete guard: 68 cases": `robocopy <main>\docs\reference C:\Users\bgbg0\roadworld-reference-backup /MIR /R:1` allowed (also with /W:1 /XD _compare from a worktree cwd); `robocopy C:\x <main>\docs\reference /MIR`, `... <worktree> /PURGE`, `... <backup> /MIR` (source not main's docs\reference), `... <main>\docs /MIR` refused. Source: CLAUDE.md line 65 and the decision. |
| 3 words are not targets | fixed | TH state cases: `git commit -m "docs: ref_seed and gate_ack notes [skip ci]"`, `grep -rn gate_ack docs tools`, `rg -n "recordAudit|ack_secret" tools`, echo text, a heredoc body naming `.claude/state`, `node --check tools/ref_seed.mjs` and `cat tools/hooks/gate_ack.mjs` all allowed; `node tools/hooks/gate_ack.mjs ...`, `node tools/ref_seed.mjs`, inline `node -e` calling recordAudit refused. Lexer: `tools/hooks/shell_lex.mjs`. |
| 4 gate.log readable by any tool | fixed | TH: Read, Grep (path and glob forms), Glob, `cat gate.log \| tail`, `Get-Content gate.log -Tail 20 \| Select-String`, `tail; grep` allowed; redirect over it, `rm`, Write and Edit refused. |
| 5 main checkout from the git common dir | fixed | `tools/hooks/root.mjs` (`.git` file, `commondir`, `worktreeRoots`); TH "delete guard run FROM A WORKTREE copy": rm -rf of the main checkout and of main\docs refused, logged in main's state not the worktree's; "lib.ROOT of a hook file that sits in a worktree is the main checkout". |
| 6 PowerShell blocks parsed | fixed | TH: `if (Test-Path docs) { Remove-Item docs -Recurse -Force }`, `try { ... } catch {}`, `foreach ($d in "docs") {...}`, `(Remove-Item ...)`, `$null = Remove-Item ...` refused (lexer reduces blocks to statements, tracks `$x = "literal"` and foreach lists). |
| 7 -Recurse:$true, lists, env, xargs, find -exec, quotes, cd | fixed | TH: `-Recurse:$true`, `build,docs`, `env rm -rf docs`, `echo docs \| xargs rm -rf`, `find docs -exec rm -rf {} +` and `-delete`, `docs/ref""erence`, `cd docs && rm -rf reference`, `cd "<main>" && rm -rf docs` from elsewhere, `Set-Location ...; Remove-Item .\docs`, `bash -lc`, `pwsh -c`, `Get-Item ... \| Remove-Item -Recurse`, wildcards `d*` and `*` refused; unknown variables near a tree refused; unresolvable cd makes a relative target refused. Known residual evasions: FAULTS Residual 1 (accepted by decision). |
| 8 `git -C <path> clean -fdx`, `del /s` on a worktree | fixed | TH: `git -C <wt> clean -fdx`, `git --git-dir=... clean -fdx`, `git clean -xdf`, `git clean -fd` in a worktree refused, `git clean -n -fdx` and `git clean -n` allowed; `del /s /q <wt>\*` and `rmdir /s /q <wt>` refused; the test worktree holds a REAL junction and its target survives every case. Plus the structural change: `new_agent_worktree.ps1` creates no junction (TW), `unlink_reference_junctions.ps1` removes old ones link-only (TW, 9 tests), `backup_reference.mjs` (TB, 31 tests). |
| 9 settings.json canary, CLAUDE_PROJECT_DIR | fixed | TH "settings.json canary": all 8 hook command lines of the wiring run with `CLAUDE_PROJECT_DIR` set and cwd inside a worktree against a fake payload of their event, exit 0, no load error. CLAUDE_PROJECT_DIR answer and its limit: first section. |
| 10 BLOCKED: quoted error found in the log, needs names a thing | fixed | gates v3 (`GATES-VERSION: 3`, make_brief emits it; v2 briefs keep the old reader): TG "gates v3" block, 18 tests: no `see:`, missing file, the audit's made-up quote against an unrelated log, a one-line echo log, quote more than half the file, whitespace and case ignored, two quotes with one missing, code-only blocks, `needs:` nothing, a decision, help, time, tokens, a fix, input all refused; the 4-lines-one-cause cap stays; end to end through `check_handin --gates-version 3` (refused without see:, passes with it) and `--gates-version 2` (old form still passes). |
| 11 ref_seed stated, merge checklist, FAIL-OPEN message test | fixed | First section and merge checklist below; TG "check_handin v3 with no register ... FAIL-OPEN ... ref_seed". The evasions remain policy only (FAULTS Residual 3). |
| 12 real-payload tests for items 1 to 8, each naming its expected-value source | fixed | TH block "gates5: state guard and delete guard against the real payload shapes": the comment above the tables names the sources (gates4 audit repro, main session decisions, CLAUDE.md line 65); 112 table cases plus root, worktree-copy and canary tests. |
| gates3 item 2 (records out of reach of the agent) | fixed | Items 1 to 4 above: the guard now judges targets, so it can stay switched on; records are trusted by who writes them. |
| gates3 item 5 (BLOCKED constrained) | fixed | Item 10 above. |
| gates3 item 12 (contract wording) | fixed | Proposed text below (the main session applies it at merge; the owner approved item 6a's intent). |
| main session addition: tests must not write the real gate.log | fixed | `CP_GATE_STATE_DIR`, `tools/test_isolation.mjs`; TG and TH each end with "NO line mentioning this run's temp folder reached the real gate.log of the main checkout"; TG "lib.log under CP_GATE_STATE_DIR lands in the isolated folder". |

## Reference files opened

Case: Evidence of the failed round 9 and the audit gate faults | Opened: C:\Users\bgbg0\Documents\Programming\roadworld-wt-grade-r9\docs\progress\2026-10-11-structures-r9\AUDIT.md, C:\Users\bgbg0\Documents\Programming\roadworld-wt-grade-r9\docs\progress\2026-10-11-structures-r9\STATUS.md, C:\Users\bgbg0\Documents\Programming\roadworld-wt-grade-r9\docs\progress\2026-10-11-structures-r9\FAULTS.md, C:\Users\bgbg0\Documents\Programming\roadworld-wt-auditgate\docs\progress\2026-10-11-auditgate\AUDIT.md
Case: Gate code and rules | Opened: C:\Users\bgbg0\Documents\Programming\roadworld\tools\check_handin.mjs, C:\Users\bgbg0\Documents\Programming\roadworld\tools\make_brief.mjs, C:\Users\bgbg0\Documents\Programming\roadworld\tools\hooks, C:\Users\bgbg0\Documents\Programming\roadworld\.claude\settings.json, C:\Users\bgbg0\Documents\Programming\roadworld\docs\AGENT_CONTRACT.md, C:\Users\bgbg0\Documents\Programming\roadworld\CLAUDE.md
Case: Templates and the scene check | Opened: C:\Users\bgbg0\Documents\Programming\roadworld\docs\templates\BRIEF.md, C:\Users\bgbg0\Documents\Programming\roadworld\docs\templates\STATUS.md, C:\Users\bgbg0\Documents\Programming\roadworld\prototypes\road-draw\tools\scene.mjs

## Own fault list and residual

FAULTS.md: Pass 1 (6 items), Pass 2 (5), Pass 3 (4), Residual (4 items; 1 to 3 accepted by the main session decision "structural protection + validated backup", 4 reason (a)).

## Changes (file:function)

- `tools/hooks/root.mjs` (new): `mainRootFrom`, `resolveMainRoot`, `worktreeRoots`. `lib.mjs` (ROOT from it, `CP_GATE_STATE_DIR`), `guard.mjs` (FAIL-OPEN line in the main checkout's state, same override).
- `tools/hooks/shell_lex.mjs` (new): `parseCommand`, `stripHeredocs`, `verbOf`.
- `tools/hooks/state_guard_impl.mjs`: rewritten on real targets: `judgeCommand`, `judgePath`, `stateRemainder`, `judge`.
- `tools/hooks/delete_guard_impl.mjs`: rewritten on a small rule set: `context` (classify: R1 docs\reference, R2 main and above, R3 worktree roots, R4 backup, R5 links below), `resolveWord`, `analyse` (cd and variable tracking, xargs, find, robocopy destination, git clean, del /s).
- `tools/new_agent_worktree.ps1` (no junction), `tools/remove_agent_worktree.ps1` (comment), `tools/unlink_reference_junctions.ps1` (new), `tools/backup_reference.mjs` (new: `validatePng`, `validateJpeg`, `validateFile`, `validateTree`, `backup`).
- `tools/gates_lib.mjs`: `anchorKinds`, `blockedEvidence`, `needsNamesAThing`, `parseBlocked(text, ctx)`, `itemsProblems(.., ctx)`, `statusProblems(.., opts.blockedCtx)`. `tools/check_handin.mjs`: `gatesV3`, `blockedCtx`. `tools/make_brief.mjs`: emits `GATES-VERSION: 3`.
- `tools/test_isolation.mjs` (new), `tools/test_gates.mjs`, `tools/hooks/test_hooks.mjs`, `tools/test_worktree_tools.mjs`, `tools/test_backup_reference.mjs` (new).
- `docs/templates/STATUS.md` (BLOCKED form with `see:` and a named `needs:`). `.claude/settings.json` unchanged in this round (wiring and the 300 s timeouts as in gates4).

## Images that carry each claim

None: engine and tooling round (HANDIN: no-visual). The claims rest on the runs and tests named above.

## Suite and checks

- `node tools/test_gates.mjs`: 490 passed, 0 failed. `node tools/hooks/test_hooks.mjs`: 412 passed, 0 failed. `node tools/test_worktree_tools.mjs`: 27 passed, 0 failed. `node tools/test_backup_reference.mjs`: 31 passed, 0 failed. Run 3 + 3 + 1 + 1 at the same time (final) and earlier singly several times: every run green.
- Waiver-guard deletions against the gates4 base in this branch: `test_worktree_tools.mjs` changed one assertion text (`NEVER rm -rf` to `Never recursive-delete`, because the script text changed with the junction); `test_hooks.mjs` old delete-guard tests kept and still pass (the new table covers more); no test case was deleted or loosened.
- Real data: `node tools/backup_reference.mjs --verify-only --main <main>`: 348 files, 18 invalid (named above); before the quarantine rule 650 files, 302 invalid.

## Proof table

Gate lines: `scene_check >= 20/21 (baseline 20/21, target 21/21)`, run with `--gates-version 3` from this branch, raw output `..\roadworld-scratch\gates5\proof\*.out` and `*.err`, state redirected to `proof\state`.

(i) Real round 9 folder (roadworld-wt-grade-r9, docs/progress/2026-10-11-structures-r9): REFUSED, 63 items. Among them: scene check measured 17/21 against the floor 20/21 (`ground_under_road`, `median_flat`, `ground_hidden_by_walls`, `no_bare_steps`); 8-column COMPARE rule (rows with 5); the grading golden data files edited (waiver guard, 11 files); two different pictures behind three named references; the undone-item and usage-reason lines of its STATUS.

(ii) Other hand-ins:

| Hand-in | What the gates say now |
|---|---|
| roadtool3 (main, `--no-visual`) | REFUSED, 5 items: scene 20/21 below the target and STATUS does not name it; three STATUS lines read as undone or deferred; "References opened: 48 of 48" with 45 files named. |
| tjunction2 (worktree) | REFUSED, 28 items: COMPARE rows with 6 columns, waiver findings in the marking and junction tests, missing stats records, an undone-item line, reference count. |
| roundabout2 (worktree) | REFUSED, 32 items: COMPARE rows with 6 columns, scene 20/21 not named, waiver findings, missing stats records. |

(iii) A made-up good hand-in passes: TG "v2, scene 21/21 measured: passes and prints what was measured" and the v3 end-to-end tests ("with see: build.log ... passes"); this folder's own run is the last line of the report.

(iv) Old layouts and running agents: briefs without `GATES-VERSION` are judged by the old checks; `--gates-version 2` keeps the old BLOCKED reader (TG "v2 ... still passes", "check_handin --gates-version 2: the same STATUS passes") and the 8-column COMPARE rule stays versioned by the marker; auditors have no HANDIN_DIR and are not touched; a builder with the current layout (`pvetrees`, v2): the new guards refuse only recursive deletes near the protected places and commands that touch `.claude/state` (the table's allowed cases are `rm -rf build`, builds, git, check_handin, commit messages, the lane lock). Their BLOCKED lines, if any, are read by the v2 reader unless their brief says version 3.

## Proposed rule text and merge notes (not applied: rule files are the main session's)

Merge checklist, in this order:
1. Merge the branch (the hook stubs import `guard.mjs`, `lib.mjs`, `root.mjs`, `shell_lex.mjs` and each other: they land together). Replace main's `.claude/settings.json` with this branch's (adds `delete_guard` and `state_guard` on PreToolUse, `ref_log` on PostToolUse, and the 300 s timeouts for the finish hook; main still has 60 s).
2. `node tools/ref_seed.mjs` once in the main checkout (until then reference provenance prints FAIL-OPEN and does not refuse).
3. Fix or quarantine the 18 damaged photos named in the first section, then run `node tools/backup_reference.mjs` (validated mirror to `C:\Users\bgbg0\roadworld-reference-backup`; refuses on any invalid picture or more than 5 % loss) and keep it on the routine that CLAUDE.md line 65 prescribes.
4. When the worktrees are idle: `powershell -NoProfile -ExecutionPolicy Bypass -File tools\unlink_reference_junctions.ps1 -All` (add `-WhatIfOnly` first). New worktrees have none.
5. Put `ctest assertions >= N` (the base's count) in each engine task file's Gates section (as in gates4).

Contract and CLAUDE.md wording (proposed):
- Contract item 1: replace "(the main checkout; run `powershell ...new_agent_worktree.ps1 <name>` or link the folder, your worktree has none)" with "(the main checkout, by absolute path: worktrees have no `docs\reference` and you never create a link to it)".
- Contract item 3: "Side-by-side composites go in `<main checkout>\docs\reference\_compare\<topic>\` (gitignored)".
- Contract item 6a (after item 6): "An honest statement that you cannot do something is a legitimate way to finish. Put it in the first section of STATUS.md, one line per item: `BLOCKED: <item> | tried: <a command in backticks, a file or a test you ran> | blocked by: <the quoted error text, the refused command or the missing file path> | see: <the log or output file that holds that error; the quote must be in it> | needs: <a named thing that would unblock it: a file, a command, a decision with its subject>`. 'The complexity', 'difficult', 'the geometry', time, scope and 'a larger rewrite' are not blocks; 'needs: help', 'a decision', 'nothing' are refused. At most 3 BLOCKED lines unless each has a different concrete cause. A BLOCKED line never waives a measured gate floor; only the main session can, with an `allow_floor_miss` line in the task file."
- Contract item 8, replace the sentence on removing a worktree: "Remove a worktree only with `tools\remove_agent_worktree.ps1 <name>`. No worktree holds a link to the reference photos; the `delete_guard` hook is a best-effort net, the protection is that structure plus the validated backup."
- CLAUDE.md "Layout" and the line 65 rule: "Removing a worktree: `tools\remove_agent_worktree.ps1 <name>` only (it also unlinks any old junction). New worktrees have no `docs\reference`. Keep the validated mirror current with `node tools\backup_reference.mjs` (it refuses on a damaged picture or more than 5 % fewer files)."
- Contract item 7 (hand-in): add "If the brief lists ITEM lines, STATUS.md has a `## Items` row per item." (from gates4, unchanged).

## Next step

Merge when the auditor accepts, in the order of the checklist above.
