# Index, round gates3 (engine-only: no renders, no COMPARE.md)

HANDIN: no-visual. The evidence of this round is runs and tests, not pictures.

| File | What it holds |
|---|---|
| STATUS.md | first section (one BLOCKED line), Prior audit items table (16 items of docs/progress/gates2/AUDIT.md, one row each with the proving test or run), the main session additions, proof table (round 9, roadtool3, tjunction, roundabout, typed fake audit, old-layout agents), Pass 3 results, proposed contract wording, changes, suite counts |
| FAULTS.md | Pass 1 to 3 and Residual (5 items) |
| INDEX.md | this list |

Run outputs behind the proof table (outside the repo, in `..\roadworld-scratch`): `proof_final2\{r9,tjunction,roundabout}.txt` (final code), `proof_final\*.txt` and `proof_r9.txt`, `proof_roadtool3.txt` (earlier code, the roadtool3 worktree was removed afterwards), `g3proof\pic9.mjs` (round 9 pixel numbers from the re-rendered views), `g3proof\wrap_bad` and `wrap_good` (wrapper gate on the roadtool3 merge state 69302ae and the fixed state 1b614a1).

Tests: `node tools/test_gates.mjs`, `node tools/hooks/test_hooks.mjs`.
New files: `tools/check_wrappers.mjs`, `tools/hooks/guard.mjs`, `tools/hooks/{brief_gate,handin_gate,gate_notice,contract_inject}_impl.mjs`.
