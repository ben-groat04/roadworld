# Faults found in the gates3 work (self-audit, three passes)

The "photo" for this round is the evidence of what went wrong: the real round 9 hand-in, the roadtool3, tjunction and roundabout hand-ins, the real `LastTest.log` of merge-roadtool3, the real output of `scene.mjs`, and the gates2 AUDIT.md. Each pass below ran the gates against that real data or attacked them the way an auditor would. None of the faults below is in the 16-item list of the gates2 audit; those are in the STATUS.md table.

## Pass 1: the gates run against the real hand-ins and logs

1. ctest total counted per configuration. Run against the roadtool3 hand-in, `measureCtest` said "covers 1 of 5 registered tests" because a multi-config MSVC build repeats `add_test(sim_core_tests ...)` once per configuration. A correct whole-suite log was refused. Fixed: distinct test names are counted (`countRegisteredTests`); test "a multi-config build registers ONE test under three configurations".
2. STATUS reader false positives on real text. "400 cases, 400 passed, 1 skipped" (a doctest count) and "the Unreal M2 branch was not touched" (a plain scope statement) were read as excuses. Fixed: a number before "skipped" is a count; "touched" is no longer in the verb lists. Test: honest-text list in "STATUS reading: excuses, honest blocks, false positives".
3. Pictures in `before/` were judged as if they were the round's work (tjunction layout: `before/t_aerial` with no road in frame refused the whole hand-in). Fixed: records under `before/` are history. Test "the real tjunction layout ... passes; before/ history is not judged".
4. The new `expected_value` finding fired on a TIGHTENED assertion (`< 0.3` to `< 0.1`), which is a better test. Fixed: a stricter comparison on the same left side is skipped. Test in "self-audit hardening".
5. The real `scene.mjs` "overview" view has 0.31 % road in frame and round 9's `s6_embankment_toe_profile` has 0.70 %: both are legitimate non-headline views and are refused until declared `diagnostic` with a reason in STATUS.md. That is the intended workflow, shown in the picture-gate tests; not changed.

## Pass 2: attacking the gates the way the gates2 auditor did

6. A typed stats record with a matching stub PNG (a 40x20 or one-colour file) passed the picture gate. Fixed: frames under 320x180 are refused, and a decodable PNG with fewer than 16 distinct colours is refused (`pngColourCount`: an inflate and unfilter of 8-bit RGB/RGBA PNGs; other formats skip the colour check, never fail it). Tests in "picture plausibility". Limit: a faker who writes a varied PNG and a matching record still passes; the record is not signed because the render tool runs in the agent's worktree where no secret exists.
7. More ways to say "not done": "could not finish", "unable to implement", "gave up", "unfinished", "unresolved", "partly done", "half implemented", "not done yet". Added with false-positive tests ("the engine cannot model a tunnel", "failed to find node_modules and I installed it", "incomplete data in the fixture").
8. Two hook processes starting together could each create `ack_secret`; the loser's signed record would then never verify (an audit silently not owed). Fixed: exclusive create, the loser reads the winner's secret. Test with six processes.
9. The BLOCKED news would be logged twice per finish (the finish hook runs on PreToolUse and on Stop), and old-layout briefs (no measured gates, so no reader) logged nothing. Fixed: the hook scans STATUS.md itself, writes one `blocked` entry per changed item set. Tests "old-layout brief ... BLOCKED news is logged anyway" and "still ONE blocked entry".
10. Deleting a builder record cancelled the audit debt (auditgate AUDIT fault 6, never fixed). Fixed: the "audit owed" log line stands in for a missing record; only a signed audit record clears it (`owedInLog`). Test part 3. The auditgate AUDIT fault 8 (a folder deleted everywhere drops the debt) stays by decision: nothing is left to audit, and the test says so.
11. The wrapper gate looked only at added files (`--diff-filter=A`); a renamed source would be missed. Fixed: `AR`.
12. The waiver guard's list of blind spots did not mention a check moved into a helper that is never called. Added to the "WHAT IT CANNOT SEE" comment in `gates_lib.mjs`.

## Pass 3: dogfood and load

13. The finished gates were run on this hand-in with `--gates-version 2 --prior-audit docs/progress/gates2/AUDIT.md`. Findings are listed under "Pass 3 results" in STATUS.md (what the gates said about their own round, and what was changed).
16. The live finish hook refused this very hand-in ("COMPARE.md is missing"): with HANDIN_DIR in the prompt it never read the BRIEF-FILE, so `HANDIN: no-visual` and the GATE lines in the file were lost. Fixed in `handin_gate_impl.mjs`, tests in "gates3 hooks, part 4". (Same family as the engine-only briefs that made two builders write placeholder COMPARE tables.)
14. test_hooks.mjs and test_gates.mjs were run 20 times, 5 at a time; the numbers are in STATUS.md.
15. The proof table was re-run with the final code on the real round 9, tjunction and roundabout folders (the roadtool3 worktree was removed during the round: see the BLOCKED line in STATUS.md).

## Residual

1. Typed stats record plus a varied matching PNG (fault 6): not signable without a secret in the agent's worktree. A cold viewer opening the headline images is still the real check (the gate records the images' road and debug numbers, it does not see the picture).
2. Auditgate fault 8 (a progress folder deleted everywhere ends the debt): kept; nothing is left to audit.
3. A signed record becomes unverifiable if `.claude/state/ack_secret` is replaced: the builder reads as unrecorded and the log line stands in for it, so the audit stays owed (fails toward more audit, not less).
4. The waiver guard cannot see a vacuous test that changes no assertion line (listed in `gates_lib.mjs`); `expected_value` findings need an `allow_waiver` line from the main session even for a justified expectation change (a deliberate cost: an expectation change needs a photo measurement or written rule).
5. The status reader is a phrase list: new disguises will appear. The BLOCKED form is the legitimate way out, so an honest agent has no reason to find one.
