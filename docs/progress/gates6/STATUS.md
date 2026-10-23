## Skipped, refused or failed

HANDIN: no-visual

- Nothing skipped or refused in the brief's scope (A to F of the send-back note). Open items: none under reason (a) or (b); the forms the main session accepted as residual are listed in FAULTS.md "Residual" with the decision cited.
- Item 23 SETTLED from the Claude Code hooks documentation (code.claude.com/docs/en/hooks, Configuration section), no probe hook needed: "When a subagent calls a tool, tool events such as PreToolUse and PostToolUse fire the same configured hooks as in the main conversation, and the input carries the agent_id and agent_type common input fields that identify the subagent." The guards treat a missing agent_id as the main session. What the state guard can and cannot protect is in the header of `tools/hooks/state_guard_impl.mjs`; the design stays "records are written by the hooks, the guard closes the agent's route to them".
- Which gates ran for this hand-in: `node tools/check_handin.mjs docs/progress/gates6 --no-visual --gates-version 3 --task docs/tasks/gates6.md` PASSES with "waiver guard: 5 finding(s) against merge base fdab9569, 5 allowed by the task file" (the five findings are the gates2/gates3 changes of `scene.mjs` and four renamed `test_hooks.mjs` ack tests that the merge base still counts as new; each allow line and its reason is in `docs/tasks/gates6.md`). Without `--task` the same run lists exactly those five (gates5 audit item 29). Reference provenance prints FAIL-OPEN (no `ref_seed` run yet, a merge step).
- Real finding on the way: `node tools/backup_reference.mjs --verify-only` on the real main (374 files) found one photo with a trailing restart marker that my first decoder wrongly refused (fixed, tested against that file), and at one point a zero-length `_compare/tjunction4/c78_b.png` (another round's composite, gone at the second run; it would have made the mirror refuse, correctly).
- The suites were run one at a time as asked. Nothing pushed or merged. No Unreal, no engine file touched.

References opened: 6 of 6 listed (the gates5 AUDIT.md whole; the gates5 STATUS.md, FAULTS.md, INDEX.md; `tools/hooks/delete_guard_impl.mjs`, `state_guard_impl.mjs`, `shell_lex.mjs`, `root.mjs`, `lib.mjs` whole; `tools/backup_reference.mjs`, `tools/gates_lib.mjs` (BLOCKED part), `tools/check_handin.mjs` (flags and ctx); `.claude/settings.json`; `tools/hooks/test_hooks.mjs` (gates5 block and header); the Claude Code hooks page for item 23). Not read in full: the rest of `check_handin.mjs`, `make_brief.mjs`, `handin_gate_impl.mjs` (not changed).

## Prior audit items

Previous audit: `docs/progress/gates5/AUDIT.md` (32 numbered items). TG6 = `node tools/hooks/test_guards6.mjs` (the real-payload tables, one named case per item, source: the audit text and the main session decisions of 23 Oct), TH = `node tools/hooks/test_hooks.mjs`, TGt = `node tools/test_gates.mjs`, TB = `node tools/test_backup_reference.mjs`.

| Item | Status | Proof or reason |
|---|---|---|
| 1 temp and build variables ($TEMP, $TMP, ${TMPDIR:-/tmp}, $BUILD, $(pwd), ${PWD}, mktemp, "$PWD\build", $($PWD.Path), $PSScriptRoot, Join-Path) | fixed | TG6 "item 1": 16 cases allowed. Every environment variable and the working-directory forms are resolved; a variable that is only unknown refuses only in main, in a tree with a docs\reference link, or when the command names the photos |
| 2 pipeline Remove-Item on a listing | fixed | TG6 "item 2": `Get-ChildItem build -Recurse -Filter *.obj \| Remove-Item`, `... \| Remove-Item -Recurse -Force`, `$env:TEMP` form, `ls build \| xargs rm -rf` allowed (the listing path in front of the pipe is judged) |
| 3 find -delete with a name filter from the worktree root | fixed | TG6 "item 3": allowed in a clean worktree and, with a filter that cannot match a picture, in main; refused in a junction tree and with `-name "*.jpg"` in main |
| 4 `ls .claude/state` for subagents | fixed | TG6 state "item 4": ls, ls -la, `cd state && ls`, dir, Get-ChildItem, find names, test -f allowed (names only) |
| 5 git clean with a path argument | fixed | TG6 "item 5": `git clean -fd build` and `git clean -fdx unreal/CityPainter/Intermediate` allowed; `git clean -fd` without a path and a path that holds the link refused |
| 6 words in shell comments | fixed | TG6 "item 6": `rm -rf build # docs/reference` and a PowerShell comment allowed (the lexer drops comments) |
| 7 own composites under `_compare/<topic>` | fixed | TG6 "item 7": allowed for a named topic folder and files below it; `_compare` itself, `_compare/*` and the photos refused; contract item 3 now says how |
| 8 linksBelow cost and silent cap | fixed | TG6 "item 8": 24,000 files walked in tens of ms (directory entries only, no realpath: a junction shows as a symlink entry, checked); a time cap (5 s) or entry cap REFUSES with the numbers in the message, never passes |
| 9 `\rm`, timeout N, busybox, command, builtin, exec, nohup, sudo, nice, time, env | fixed | TG6 "item 9": 12 refused |
| 10 eval, echo \| bash, bash <<<, bash -c $'..', cmd //c, cmd /c rd /s, heredoc into bash, printf \| sh | fixed | TG6 "item 10": 12 cases (cmd //c on build and echo into a file stay allowed) |
| 11 bash if/while/for/read control flow | fixed | TG6 "item 11" and "comments": `if ...; then rm -rf docs; fi`, while, for over a list and over the photos, `while read` in main refused; `if [ -d build ]; then rm -rf build; fi` allowed |
| 12 brace, class, trailing dot, backslash escape | fixed | TG6 "item 12": `refer[e]nce`, `{reference,x}`, `[d]ocs`, `{docs,build}`, `reference.`, `ref\erence`, `\\?\` prefix refused (wildcards are expanded against the disk); a glob matching nothing deletes nothing and is allowed |
| 13 assigned command substitutions | fixed | TG6 "item 13": `d=$(echo docs)`, backticks, `d=$(pwd)/docs`, `[System.IO.Path]::GetFullPath`, `(Resolve-Path)` refused |
| 14 Invoke-Expression, iex, -EncodedCommand, Start-Process, Push-Location, @(), nested parentheses, .Delete($true), foreach over Get-ChildItem | fixed | TG6 "item 14": 15 cases (the parenthesised-path forms are read as text for Remove-Item -Recurse because the lexer splits at the parentheses) |
| 15 `# remove_agent_worktree` substring | fixed | TG6 "item 15": every `git worktree remove\|prune` is refused whatever else the command says; only the script invocation (no git word) passes |
| 16 non-recursive mass deletes, moves, truncation | fixed | TG6 "item 16": 28 cases: rm -f *.jpg, Remove-Item *.jpg, cd && rm -f *.jpg, del aerial\*, gci \| ForEach Remove-Item/.Delete()/[IO.File]::Delete, for loop rm, mv/Move-Item/Rename-Item of docs\reference, docs, a subfolder, a worktree root, the backup, rmdir docs\reference, `>` onto a photo, truncate, Set-Content, Clear-Content, cp/Copy-Item onto a photo refused; one named photo, a batch into `_damaged`, a download, `>>` to the index, own composites allowed |
| 17 robocopy /MOV, /MOVE | fixed | TG6 "item 17": refused from docs\reference/main (also with the backup as destination); the prescribed backup and /MIR to an unguarded folder allowed |
| 18 rsync --delete, python/node/perl one-liners | fixed | TG6 "item 18": rsync --delete into the photos, `python -c os.system`, `node -e execSync`, `perl system` refused. xcopy and `cp --remove-destination` over files are not seen (FAULTS Residual) |
| 19 git stash | finding, no fault | nothing to do |
| 20 recursive reads above the state folder | fixed | TG6 state "item 20": grep -r on .claude or the main checkout, `rg --hidden/-uu`, `find -exec cat`, `Get-ChildItem -Recurse \| Select-String`, `find \| xargs cat` refused; allowed with `--exclude-dir=state` or a name/include filter that cannot match ack_secret, and for folders that do not contain the state |
| 21 subagents may read `.claude/state/handin` | fixed | TG6 "item 21": ls, cat, Read, wildcard on handin allowed; writes into it refused; the refusal text names who reads other records |
| 22 gate_ack wildcard / copy-then-run; Write/Edit of main settings and hooks | fixed | TG6 "item 22": `gate_a*.mjs`, `npx node gate_ack.mjs`, `cp gate_ack.mjs /tmp/g.mjs && node /tmp/g.mjs`, cp of audit_lib refused; Write/Edit/sed -i/redirect onto the MAIN checkout's `.claude/settings*.json` and `tools/hooks/*` refused for subagents, allowed in a worktree copy and for the main session. `node tools/hooks/test_hooks.mjs` and `test_guards6.mjs` may run |
| 23 does a subagent's PreToolUse carry agent_id | fixed (settled) | Claude Code hooks documentation quoted in the first section |
| 24 BLOCKED evidence: planted quote, `needs:` | fixed | TGt "BLOCKED evidence v4": the audit's junk.txt repro outside the own folder refused; a file older than the round start refused; a tracked file refused; `needs: owner decision about the roads` refused, "whether X or Y", a file or a command accepted. check_handin passes `roundStart` (--since, CP_ROUND_START, else the worktree's .git creation time), `ownRoots` (worktree, `roadworld-scratch\<name>*`) and `repoRoot` for gates v3 only; older callers keep the v3 rules |
| 25 | finding | no action |
| 26 ref_seed: a modified photo registered as new | fixed | TGt "an overwritten reference photo is not a reference": a picture whose path already had another sha1, changed during a SUBAGENT tool call, is logged with `replaced: true` and its hash is never a known reference; the original stays known; a re-fetch by the main session registers normally |
| 27a JPEG scan data | fixed | TB: baseline JPEGs are decoded through their entropy-coded scans (exact MCU count, restart markers, ends at a marker); 100 zero bytes, 40 flipped bytes and 60 cut bytes in the scan refused; the manifest keeps size, mtime and sha1, unchanged files are not decoded again and a same-size same-mtime change is refused as silent corruption. Progressive and arithmetic JPEGs only get a zero-run check (Residual). Real data: 374 real photos validate |
| 27b --accept-loss by any agent | fixed | TB: honoured only with `<state>/accept_loss` (the main session creates it; the state guard refuses any subagent write there), not older than an hour, consumed on use |
| 27c good photo moved into `_damaged` | fixed | TB: a file of the last manifest that is gone and sits validly in `_damaged` refuses the mirror (zero tolerance, named); a really damaged one may be quarantined and the backup keeps its good copy |
| 28 stale statement about the 18 damaged photos | fixed | First section and merge checklist below: main validates 374/374 now |
| 29 gated check only passes without gates | fixed | `docs/tasks/gates6.md` carries the five allow_waiver lines with their reason; the gated run is quoted in the first section |
| 30 tests did not cover lists 1 to 3 | fixed | TG6: 26 table tests (about 300 payloads) plus 5 process-level runs of the two hooks with the harness payload shape; the expected-value sources are in its header |
| 31 merge checklist wording | fixed | below |
| 32 worktree scripts | finding, no fault | nothing to do |

## Reference files opened

Case: gates5 audit and its hand-in | Opened: docs/progress/gates5/AUDIT.md, docs/progress/gates5/STATUS.md, docs/progress/gates5/FAULTS.md, docs/progress/gates5/INDEX.md
Case: guards | Opened: tools/hooks/delete_guard_impl.mjs, tools/hooks/state_guard_impl.mjs, tools/hooks/shell_lex.mjs, tools/hooks/root.mjs, tools/hooks/lib.mjs, tools/hooks/test_hooks.mjs, .claude/settings.json
Case: backup and BLOCKED | Opened: tools/backup_reference.mjs, tools/test_backup_reference.mjs, tools/gates_lib.mjs, tools/check_handin.mjs

## Changes (file:function)

- `tools/hooks/shell_lex.mjs`: `parseCommand` (comments, `then/do/else/if/while` keywords dropped, `seg.piped`, `<<<` here-string, `$'..'`, bash backtick substitution, Join-Path and `::` assignments), `verbOf` (`\` prefix, timeout/busybox/stdbuf/setsid/ionice/doas wrappers).
- `tools/hooks/delete_guard_impl.mjs`: rewritten `analyse` (see its header for the rule list), `linksBelow` (entries only, time and entry caps reported), `expandGlob`, `braceExpand`, `context.classify`.
- `tools/hooks/state_guard_impl.mjs`: rewritten `judgeCommand`, `judge(input, mainRoot)`, `protectedMain`, `reachesState`, `excludesState`; item 23 documented in the header.
- `tools/backup_reference.mjs`: `validateJpegScan`, `validateTree(root, files, man)`, `acceptLossAllowed`, `backup` (manifest `files`, `_damaged` logic).
- `tools/gates_lib.mjs`: `blockedEvidence` (own roots, round start, tracked file), `needsIsConcrete`; `tools/check_handin.mjs`: `blockedCtx` (`roundStart`, `ownRoots`, `repoRoot`).
- `docs/AGENT_CONTRACT.md` item 3 (own composites), `docs/tasks/gates6.md`, tests: `tools/hooks/test_guards6.mjs` (new), `test_backup_reference.mjs`, `test_gates.mjs`, `test_hooks.mjs` (one `needs:` wording).
- `.claude/settings.json` unchanged (the hook wiring and timeouts are as in gates5).

## Images that carry each claim

None: engine and tooling round (HANDIN: no-visual). The claims rest on the test runs named in the tables.

## Suite and checks

Run one at a time, each green: `node tools/test_gates.mjs` (506), `node tools/hooks/test_hooks.mjs` (412), `node tools/hooks/test_guards6.mjs` (31), `node tools/test_worktree_tools.mjs` (27), `node tools/test_backup_reference.mjs` (45). No test wrote the live gate.log (each suite ends with the isolation test). Real data: `node tools/backup_reference.mjs --verify-only` on the main checkout: 374 files, all valid, 23 s on the first run (later runs skip unchanged files through the manifest).

## Proof table

Unchanged from gates5 for the measured gates (round 9 refused with 63 items; roadtool3, tjunction2, roundabout2 refused; a good hand-in passes), see `docs/progress/gates5/STATUS.md`. New in this round: (i) the gates5 audit's own repro payloads are the TG6 tables; (ii) the gated hand-in of this folder passes with the five task-file waivers; (iii) old layouts: briefs without `GATES-VERSION: 3` keep the v2 BLOCKED reader, `blockedCtx.roundStart/ownRoots/repoRoot` are set only for version 3, so the running builder `pvetrees` (v2) and auditors (no HANDIN_DIR) are unaffected.

## Merge checklist (updated, in this order)

1. Merge the branch. A plain merge brings `.claude/settings.json` (tracked, unchanged on main). Sessions started BEFORE the merge keep the old wiring (finish hook 60 s, no delete or state guard) until they are restarted: tell the main session to restart, and for the agents running now (their hooks are the main session's, read at its start) nothing changes until then.
2. `node tools/ref_seed.mjs` once in the main checkout (until then reference provenance prints FAIL-OPEN and does not refuse).
3. The 18 damaged photos are already re-fetched (main `docs\reference` validates, 374 files now). Run `node tools/backup_reference.mjs` (first run decodes every JPEG and writes the manifest with hashes; later runs are fast). To accept a deliberate loss the MAIN SESSION first creates `.claude\state\accept_loss` and then passes `--accept-loss "<reason>"`.
4. When the worktrees are idle: `powershell -NoProfile -ExecutionPolicy Bypass -File tools\unlink_reference_junctions.ps1 -All` (add `-WhatIfOnly` first). Until then a tree with a junction is treated strictly by the delete guard.
5. Apply the rule text of gates5 STATUS (contract items 1, 3, 6a, 8, CLAUDE.md line 65); item 3 of the contract is already worded in this branch.

## Next step

Fresh auditor for gates6, then merge.
