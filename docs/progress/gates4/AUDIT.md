Auditor: fresh agent, gates4 audit, 23 Oct 2026

# Audit of gates4 (branch agent/gates4, tip 7099226, no-visual hand-in)

## Verdict: SEND BACK (do not deploy the hooks live yet)

The suites are green on repeated runs (test_gates 462, test_hooks 399, test_worktree_tools 18). But the two new guards, run against the payloads real agents send today, produce false blocks on work the owner and CLAUDE.md require, and the delete guard has holes that let the incident repeat. The tests pass because they do not cover these payloads.

Method: I built the fault list from the diff, the code and my own runs of the hooks (scratch scripts in roadworld-scratch\gates4-audit: run.mjs, t1.mjs, t2.mjs, with a scratch worktree holding a real junction) before reading STATUS. No destructive command was executed anywhere; the hooks only judge text and read the disk. This is a no-visual round, so the first-look step of contract item 18 had no picture; the most obvious fault is the first line below.

## Fault list (false blocks first)

1. FALSE BLOCK, most obvious: state_guard refuses a subagent's use of `.claude/state/unreal_lane.lock`, which the owner's allow rules exist for. Refused: `touch`, `rm -f`, `test -f ... && cat`, PowerShell `New-Item` and `Remove-Item`, Write, and even Read of the lock. Cause: `STATE_RE` matches any path under `.claude/state`; only `gate.log` is exempt (and only for Read or a single cat/tail command). Repro: feed `{"tool_name":"Bash","agent_id":"x","tool_input":{"command":"touch C:/Users/bgbg0/Documents/Programming/roadworld/.claude/state/unreal_lane.lock"}}` to `tools/hooks/state_guard.mjs`: exit 2. Every Unreal builder would stop at the lane lock.
2. FALSE BLOCK: delete_guard refuses the backup mirror that CLAUDE.md line 65 prescribes: `robocopy C:\Users\bgbg0\Documents\Programming\roadworld\docs\reference C:\Users\bgbg0\roadworld-reference-backup /MIR /R:1` exits 2 (any robocopy /MIR or /PURGE with `docs/reference` anywhere in the command). The task required main -> backup ALLOWED and backup -> docs\reference refused. Judge the destination (last path argument) only.
3. FALSE BLOCK: state_guard refuses harmless commands that only name a word: `git commit -m "docs: ref_seed and gate_ack notes [skip ci]"` and `grep -rn gate_ack docs tools` both exit 2. Agents editing the gate tooling or docs will hit this constantly. Only a path under the state folder or a call of those scripts should count, not a word anywhere in the command line.
4. FALSE BLOCK: the Grep tool on `.claude/state/gate.log` is refused although the refusal text says reading gate.log is allowed; piped reads (`cat gate.log | tail`) are refused by the `single` test.
5. DELETE GUARD HOLE, worst: `ROOT` is the hook file's own location (`lib.mjs` line 9), so when the hook runs from a worktree "the main checkout root or its docs folder" means the WORKTREE's. From a worktree session `rm -rf C:\Users\bgbg0\Documents\Programming\roadworld` and `rm -rf C:\Users\bgbg0\Documents\Programming\roadworld\docs` (where the real photos live) are ALLOWED (verified). Only a path that spells docs/reference is caught. Find the real main checkout through the git common dir.
6. DELETE GUARD HOLE: PowerShell control-flow wrappers are not parsed. In the scratch worktree with a junction these were all allowed: `if (Test-Path docs) { Remove-Item docs -Recurse -Force }`, `try { Remove-Item -Recurse -Force docs } catch {}`, `foreach ($d in "docs") { Remove-Item $d -Recurse }`, `(Remove-Item docs -Recurse -Force)`, `$null = Remove-Item docs -Recurse -Force`. The first is a very common agent idiom.
7. DELETE GUARD HOLE (same scratch worktree): `Remove-Item docs -Recurse:$true -Force`, `Remove-Item build,docs -Recurse -Force` (comma list), `env rm -rf docs`, `echo docs | xargs rm -rf`, `find docs -exec rm -rf {} +`, `rm -rf docs/ref""erence`, and `cd docs && rm -rf reference` (cd is not tracked; the relative target resolves against the original cwd) were all allowed.
8. DELETE GUARD HOLE: `git -C <worktree> clean -fdx` was allowed (the git-clean branch is only taken when `git` is the first word and no `-C` path sits between), and `del /s /q <worktree>\*` was allowed. Both seen in t1.mjs against the real gates4 path.
9. Not shown live: STATUS admits every hook ran only on synthetic stdin. Unverified and decisive: whether CLAUDE_PROJECT_DIR for a subagent is the main checkout or its worktree. Hooks use ROOT = their own location, so records and the state guard's folder differ per worktree if it is the worktree, and the guard's wording "main checkout's .claude/state" would be false. Add a canary that runs the exact command line from settings.json against a fake payload before merge.
10. STATUS reader: a BLOCKED line with a plausible but never produced quoted error is accepted (`blocked by: "C:/x/y.cpp(3): error C2065: foo undeclared identifier"`, `needs: nothing`), and "needs: nothing" is not a block. An irrelevant quoted error beside "needs: more time" was refused only through the needs wording; with a neutral needs line it passes. The quoted error is checked against nothing.
11. ref_seed not run: reference provenance prints FAIL-OPEN and never blocks (as designed), so the "photo copied from the web registered by hash" and the "hand-drawn render plus matching flat mask (16+ colours)" evasions stay open; STATUS Residual 1 and 2 concede them. The owner should be told these remain policy only.
12. test_hooks.mjs adds 845 lines but its delete-guard and state-guard blocks have no case for the payloads in faults 1 to 8; the lane lock is not tested at all.

## Prior items (gates3 AUDIT, 14 items), STATUS table checked
Claimed tests for items 1, 3, 4, 6, 7, 8, 9, 10, 11, 13, 14 exist and pass in my runs; I found no counter-example in the part I exercised. Item 2 (records out of the agent's reach): not truly fixed, state_guard both over-blocks (faults 1, 3, 4) and depends on a word list; re-opened. Item 5 (BLOCKED constrained): re-opened for fault 10. Item 12 (contract wording): marked fixed but "not applied"; the main session applies it at merge, so it is open until then. I did not retry the gates3 fake hand-in variations end to end (hand-drawn render plus matching mask is conceded by the builder).

## Tests: source of expected values
- test_gates picture and mask tests: expected values come from real mask counts of scene.mjs runs and the round 9 numbers in STATUS (the tool's own output); the 0.05 % movement floor and the 16-colour floor have no photo behind them (builder says so).
- test_hooks delete and state guard tests: the written rule (CLAUDE.md item 8) plus the builder's reading, no real agent payloads. None pins faults 1 to 8 as wanted behaviour; they simply do not cover them.
- test_worktree_tools: scratch repo with two junctions, targets survive; expected value is the written rule; sound.

## For the next builder (every item, none optional)
1. Exempt `.claude/state/unreal_lane.lock` (only it) from state_guard for Read, Write, Bash and PowerShell; tests for touch, rm, New-Item, Remove-Item, test -f.
2. Judge robocopy by destination: main docs\reference -> roadworld-reference-backup with /MIR allowed; destination under docs\reference or a worktree with /MIR or /PURGE refused; test both directions.
3. state_guard: stop refusing mentions of gate_ack, ref_seed, recordAudit in commit messages, grep patterns and docs; refuse only paths under .claude/state, ack_secret reads and actual runs of those scripts.
4. Allow Grep, Glob and piped reads of gate.log by subagents.
5. Find the main checkout via the git common dir; protect it and its docs in delete_guard from any worktree cwd; test from a worktree.
6. Parse PowerShell blocks (braces, parentheses, assignments, try) so Remove-Item -Recurse inside them is judged.
7. Handle `-Recurse:$true`, comma target lists, `env`, xargs, find -exec rm, quote concatenation and cd-tracking in chained commands; where a target cannot be known near a worktree, refuse.
8. Fix `git -C <path> clean -fdx` and `del /s <wt>\*`; test both with a real junction.
9. Add the settings.json canary; settle CLAUDE_PROJECT_DIR for subagents and make the hooks read the main checkout's state either way.
10. Refuse empty or "nothing" needs; require the quoted error to appear in output the hooks saw, or at least a path that exists.
11. State plainly in STATUS that ref_seed is not run and the hand-drawn render evasion remains; put ref_seed in the merge checklist with a FAIL-OPEN message test.
12. Add real-payload tests for items 1 to 8 to test_hooks.mjs, each naming where its expected value came from.

References opened: 4 of 6 listed. Opened: CLAUDE.md, the gates4 STATUS.md (after my own fault list), tools/hooks/delete_guard_impl.mjs and state_guard_impl.mjs, the picture and status sections of tools/gates_lib.mjs. Not opened: docs/tasks/gates2.md and docs/progress/gates3/AUDIT.md (the prior-items verdict rests on the STATUS table and the code, not the gates3 audit text).
