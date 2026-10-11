# Structures round 8 (11 Oct 2026): slope efficiency rule, raked walls, free road ends, s7 pit
All scenarios s1-s8 re-rendered with the r7 cameras (`regen.sh`, gcc Release build of the committed state) except the two s6 profile cameras (new, see STATUS). Before = ../2026-10-11-structures-r7/<same file name>.

| Render (after) | Before | Compare with (opened) | Verdict from the picture |
|---|---|---|---|
| s4b_side_hill_realistic_aerial / _alongA / _alongB / _end / _street | r7 same names | street/05, 06, 46, 47, 55, 56 | wall runs are continuous, tops are smooth raked lines (alongB), the natural hillside rises directly behind the coping (no long ramp); ends are blunt wedges; a second tier appears on the cut side where the cut is deeper (it starts and ends as a wedge); west end is a rock cut (grey / tan face) because a cut deeper than two 3 m tiers on a 0.4 hill escalates to rock. |
| s7_adjacent_levels_end / _end_west / _aerial / _alongA / _alongB / _between / _street | r7 same names | street/50 | the 2.9 m cliff / flat pit at the lower road's end is gone: it ends in a 4:1 run-out into the upper road's fill; the long serrated striped trench east of the road ends is gone; a soft dark bowl remains at the lower end; walls continuous, raked. |
| s5_cutting_*, s6_embankment_* | r7 same names | street/38, 39, 42, 44, 45 | unchanged shapes (smoothing round untouched). s6_embankment_toe_profile and _runout_close now show the toe and crest (before: grass only). |
| s4_retaining_wall_run_*, s1-s3, s8 | r7 same names | street/04, 15-21 | s4 (0.85 hill): large rock cut and a needs_structure fill stretch, now ending at 15 m instead of running 45 m; s1-s3, s8 unchanged. |
