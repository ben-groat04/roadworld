# T-junction round 2 (M7): a rural priority T that matches the Oxfordshire photos at any angle, mouth geometry first

## Owner's words
11 Oct: "why is the workflow so bad at doing realistic roundabouts? its like its stuck to shitty elements of it, is it scared to make changes"
11 Oct: "it isn't even getting relevant ref images!!!"
11 Oct: agents "should anticipate what the cold read viewer might say and just get round to fixing that also before it actually tries to get its work audited"
11 Oct: "remember (should be in docs already) that the photorealism must come from adding the road builder into this landscape, nothing else"

## Cases
Three cases only. Each is finished when the top-down render, at the photo's metres-per-pixel and with the main road at the photo's angle, matches the photo item by item, measured in lane widths and metres.
Case 1, plain rural T, skewed (every real photo is a skew; the junction must be right at 90, 60 and 45 degrees and on both driving sides):
- C:\Users\bgbg0\Documents\Programming\roadworld\docs\reference\aerial\77_oxon_ortho_tjunction_rural_t15_12cm.png
- C:\Users\bgbg0\Documents\Programming\roadworld\docs\reference\aerial\81_oxon_ortho_tjunction_rural_t7_12cm.png
- C:\Users\bgbg0\Documents\Programming\roadworld\docs\reference\aerial\80_oxon_ortho_tjunction_rural_hatched_edge_t8_12cm.png
Case 2, T with a triangle or wider mouth:
- C:\Users\bgbg0\Documents\Programming\roadworld\docs\reference\aerial\78_oxon_ortho_tjunction_triangle_t16_12cm.png
- C:\Users\bgbg0\Documents\Programming\roadworld\docs\reference\aerial\79_oxon_ortho_tjunction_triangle_t36_12cm.png
- only 2 photos here: find a third matching one in `docs\reference\MATCHED.md` or fetch one (pre-approved); say which.
Case 3, single-track lane joining a two-lane road (round 1 drew a 29 m give-way line across a 3.5 m lane): find at least 3 matching photos via `docs\reference\MATCHED.md` and the aerial INDEX, or fetch them; never pair a near miss.
Also open, all of them:
- C:\Users\bgbg0\Documents\Programming\roadworld\docs\reference\MATCHED.md
- C:\Users\bgbg0\Documents\Programming\roadworld-wt-tjunction\docs\progress\tjunction\AUDIT.md (audit of round 1, verdict send back: its "For the next builder" list is part of this brief)
- C:\Users\bgbg0\Documents\Programming\roadworld-wt-tjunction\docs\progress\tjunction\STATUS.md and FAULTS.md
- C:\Users\bgbg0\Documents\Programming\roadworld-wt-tjunction\docs\progress\tjunction\after\B1_top_down.png (round 1's headline render: your "before")
- C:\Users\bgbg0\Documents\Programming\roadworld-scratch\audit-tjunction\r_skew60L\top.png, r_skew60R\top.png, r_skew45L\top.png (the auditor's skew renders showing the breakage)
- C:\Users\bgbg0\Documents\Programming\roadworld\docs\road-marking-rules.md
Driver's-eye views are NOT a case in this round: round 1's showed tarmac and horizon and matched nothing. Layout from above first.

Start: worktree C:\Users\bgbg0\Documents\Programming\roadworld-wt\tjunction2, branch agent/tjunction2, cut from current main; then merge branch `agent/tjunction` (tip 9e30467: round 1 plus its audit) and continue. You own the priority-junction (T and side-road) paths of `sim_core/src/junction_geometry.cpp`, the give-way and stub code in `markings.cpp`, the `give_way` and side-road keys of `data/regions/uk/region.json`, the T-junction tests, fixtures and views. Roundabout round 2 is running in `junction_roundabout.cpp` and the roundabout block of region.json and may make small listed edits in `junction_geometry.cpp` and `markings.cpp`: stay out of roundabout code and list every edit you make outside T-junction paths. No Unreal. Do not push or merge. Suite runs: one compiler at a time.

## Gates
scene_check >= 20/21 (baseline 20/21)
ctest failed_assertions <= 4 (2 known pier assertions + 2 from the roundabout strip test marked may_fail in tjunction6, owned by the roundabout round)
No new test waiver, skip or should_fail. Tests are parameterised over angle (90, 60, 45 degrees, both hands) and driving side. Every test's expected value names the photo measurement or the rule in `docs/road-marking-rules.md` it comes from.

## Faults
From the audit and the main session's own look at B1 and the skew render against aerial 77 (the photos decide, not this list). You are expected to make big changes: rebuild the mouth from measured numbers rather than tuning round 1's shapes; photos outrank tests, audits and this list (contract item 15).
1. Mouth geometry did not come from a photo. Round 1's is a rectangle of tarmac with two 20 m fillets: a slab 2.4 m by 33 m sticks out beyond the FAR side of the main road, square blocks sit at the near corners, and a 1.8 m ledge each side where the fillet meets the minor road. In aerial 77 and 81 the main road's edges run straight through untouched, the far side has nothing at all, and the minor road joins with two small corner radii (measure them: a few metres) that differ on the acute and obtuse side of a skew. Build the mouth as: main carriageway unchanged, plus the minor road's own outline, plus two measured corner fillets tangent to both edges. No fill outside those outlines; a test that the junction surface never extends beyond the far main-road edge and has no step where it meets either road.
2. Skew: the give-way line is laid perpendicular to the minor road's axis. It must run along the main road's near edge line across the whole mouth between the two corner tangent points, at any angle. At 60 degrees round 1 gives a diagonal stub inside the main carriageway (driving left) or a 17 m bar across both main lanes (driving right); at 45 degrees it falls back 25 m down the minor road.
3. Main road markings through the junction: the far edge line must carry on unbroken past the mouth (round 1 drops it); the near edge line is replaced across the mouth by the give-way marking only; the centre line continues (check the photos for whether it changes to a warning pattern near the junction, and put the rule in region data).
4. Give-way marking: double dashed transverse line and triangle, dimensions from `docs/road-marking-rules.md` checked against the photos; width = the entering lane(s) per the photos (measure whether it spans the whole mouth or only the entering half on 77, 80, 81 and say which); minor-road centre line stops at it. "Full lane width" is asserted by a test that fails when the line is half a lane short; the existing `> 1.8*lane_w` test passes a 16 m line and proves nothing.
5. Single-track lane: line length follows the lane, not a default.
6. Parking-bay ticks on the urban fixture have no rule or photo behind them: remove them from this round's fixture or find a matching photo; the test must check what it claims.
7. Render cameras: top-down, same metres-per-pixel and orientation as each photo.
Not in this round: ghost-island right-turn lanes (aerial 82-85), driver's-eye look, kerbed urban junctions.

## Launched
This job. Also running: roundabout round 2 (sim_core, neighbouring junction files), measuring hand-in gates (tools), built-in trees evaluation (Unreal lane), road tool round 3 merger, auditor for the grading layers.

## Waiting
Slip road round: waits for this branch and roundabout round 2 (same junction files). Earthworks round 10: waits for the gradelayer verdict. Milestone 3 UI: waits for the road tool merge and the Unreal lane.
