# Faults found (gates6), in plain words

Method: after the fixes for the gates5 audit I attacked my own guards with payloads shaped like the ones real agents send (Bash, PowerShell, Read, Write, Edit tool inputs with the harness's common fields; tables in `tools/hooks/test_guards6.mjs`), ran the guards against ordinary builder idioms in this very worktree (rm -rf build, cmake, git clean -fd build, find -delete, comment lines, if/then/fi), and ran the changed backup tool against the real photo folder. No-visual round: the evidence is the audit text and the runs.

## Pass 1 (attack on my own fixes)

1. `Remove-Item -Recurse -Force @("docs")` was allowed: the lone `@` was read as a relative path `@`. Fixed (a bare `@` or `$` target is unknown; refused in main or a link tree). Test "item 14 @()".
2. `timeout -k 2 10 rm -rf docs` was not looked through: `-k` takes a value. Fixed in `verbOf`. Test "item 9 timeout -k".
3. `Remove-Item (Join-Path $PWD "docs") -Recurse -Force` and `Remove-Item (Get-Item docs) -Recurse -Force` were allowed: the lexer cuts at the parentheses so the flags land in another segment. Fixed with a whole-statement text check for Remove-Item with -Recurse and a parenthesised path (refused in main, in a link tree, or when the command names the photos).
4. Bash backticks (``d=`echo docs`; rm -rf $d``) were read as PowerShell escapes: the value became the text "echo". Fixed: a backtick pair is a command substitution (value unknown, inside judged).
5. My first fixture put the test worktrees under the system temp folder, so `$env:TEMP` was a folder ABOVE them and `Get-ChildItem $env:TEMP -Filter ... | Remove-Item` was refused by the test, not by a bug: the test now gives itself its own TEMP.

## Pass 2 (real data and real runs)

6. The real photo folder: `--verify-only` refused `street/02_wall-sandstone-footway-low-sun.jpg`: the scan decodes completely and then a restart marker (RST6) stands before EOI. Legal; my decoder wrongly refused it. Fixed, and `test_backup_reference.mjs` decodes that real file when it exists.
7. The silent-corruption rule first allowed 2 s of mtime difference, so a legitimate re-download with the same size right after the mirror looked like corruption. Fixed: exact mtime (1 ms).
8. A regex written through the shell came out with a backspace character (`\b` turned into byte 8) in `gates_lib.mjs`; the new `needs:` tests caught it (two tests failed). Fixed and scanned all of `tools/` for byte 8: none left.
9. The first full validation of 374 real photos took 49 s (a per-byte zero-run loop on every file). Fixed with a native search first: 23 s, and later runs skip unchanged files through the manifest hashes.
10. `grep -r foo .` run from the main checkout root is refused by the audit's rule (a recursive read above the state folder); the refusal says to add `--exclude-dir=state` or `--include=*.md`. Builders work in worktrees (no state folder there), so it only meets the main session and auditors working in main. Kept, by the audit's rule.

## Pass 3 (what the owner or a builder would say)

11. "Why does `git clean -fd` stay refused?" Decision of 23 Oct (gates4 audit item 8): without a path it removes untracked folders, an old docs\reference junction among them. `git clean -fd build` is allowed now (item 5). Message names the way out.
12. "Will a time cap refusal block my build cleanup?" Walking 24,000 files takes tens of milliseconds; a real build folder of 100,000+ entries is far inside the 5 s cap; if a slow disk ever hits the cap the message says so with the entry count and the command is refused (never passed silently), as the send-back note says.
13. "Can an agent forge the main session's accept_loss file?" Not with Write/Edit/redirect/cp/rm: every path under `.claude/state` other than the lane lock, gate.log and handin is refused for a subagent. A subagent's run of the backup without the file is told so in the output.
14. The STATUS merge checklist now says sessions started before the merge keep the old hooks until restarted.

## Residual

1. (accepted by the main session decision of 23 Oct 2026, "structural protection + validated backup"; the guard is a net, the protection is no junction in new worktrees plus the validated mirror) Not seen by the delete guard: a delete done inside a script file the command only runs, a path assembled over several steps or read from a file, command substitution RESULTS (`rm -rf "$(cat list)"` is refused only near a tree), aliases and functions defined elsewhere, loops whose list or body is computed elsewhere (`while read f; do rm "$f"; done < list.txt` is refused in main or a link tree, allowed in a clean worktree), variable indirection, nested brace expansion, 8.3 short names, `xcopy /E /Y` and `cp --remove-destination` over files, tools other than Bash and PowerShell.
2. (same decision) Not seen by the state guard: a script file that touches the state, a path built from pieces, a state path with a space, `find ... | while read f; do cat "$f"; done`, a settings or hook change made by a program the guard does not parse; the whole guard depends on the harness sending `agent_id` for subagent tool events (documented, see STATUS item 23).
3. Progressive and arithmetic-coded JPEGs are not decoded by the backup validator (only structure and a 32-byte zero run inside entropy data): honest limit; the manifest sha1 still catches later in-place changes.
4. BLOCKED evidence: a hand-written log with real-looking lines inside the own scratch folder, written after the round started, still passes (the rules stop the foreign, old and tracked file, not forgery). Reason (b)-style limit: nothing can tell a hand-typed log from a captured one.
