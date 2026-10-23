# Index, round gates4 (engine and tooling: no renders, no COMPARE.md)

HANDIN: no-visual. The evidence of this round is runs and tests, not pictures.

| File | What it holds |
|---|---|
| STATUS.md | first section (unverified items, limits), Prior audit items (14 items of docs/progress/gates3/AUDIT.md), the 9 re-opened gates2 items, Items table (A to G, S1, S3a to c), references opened, picture threshold numbers, changes, proof table (round 9, roadtool3, tjunction2, roundabout2, fake hand-in, old layouts), proposed rule text and merge notes |
| FAULTS.md | Pass 1 to 3 and Residual (6 items) |
| INDEX.md | this list |

Run outputs behind the proof table (outside the repo, in `..\roadworld-scratch\gates4`): `proof\{r9,tjunction2,roundabout2,roadtool3}.out/.err`, `shots\` (a real `scene.mjs` run with masks), `runs2\` (concurrent test runs), `realpic.mjs`, `cal.mjs`/`cal2.mjs` (colour classifier calibration), `attack2.mjs` (24 fresh wordings).

Tests: `node tools/test_gates.mjs`, `node tools/hooks/test_hooks.mjs`, `node tools/test_worktree_tools.mjs`.
New files: `tools/remove_agent_worktree.ps1`, `tools/ref_seed.mjs`, `tools/test_worktree_tools.mjs`, `tools/hooks/{delete_guard,state_guard,ref_log}.mjs` with `_impl.mjs`, `tools/hooks/ref_lib.mjs`.
