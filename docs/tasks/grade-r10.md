# Earthworks round 10 (M5): plan-first grading rewrite, cut from main's round 8 grading, three cases in order

## Owner's words
11 Oct: "think about why round 9 earthworks failed, improve the system"
11 Oct: "remember (should be in docs already) that the photorealism must come from adding the road builder into this landscape, nothing else"
11 Oct: "it isn't even getting relevant ref images!!!"
11 Oct: agents "should anticipate what the cold read viewer might say and just get round to fixing that also before it actually tries to get its work audited"
11 Oct: "is one earthworks agent creating AND auditing? that's wrong. and elsewhere, that's just an expample"
11 Oct, in chat, answering the steep-hillside and threading questions (QUEUE "Decided by the owner" items 13 and 14): "agree on both". The two decisions as recorded in `docs/QUEUE.md` (this wording is the main session's record; only "agree on both" is the owner's):
- Decision 13, steep hillsides: where no slope or wall fits, the road tool proposes a bridge or tunnel automatically and refuses the placement (with a warning) only when no structure fits. This is the rule for case 2.
- Decision 14, WorldCache threading: the rule "no threading in WorldCache" stays until this plan-first rewrite has been measured; revisit at M4. Do not add threads.
Background (not an owner quote): the stacked terraces of round 4 were rejected by the owner and a cold reader as a "ziggurat" (`docs/planning-log.md` round 36), so at most ONE wall per side.

## Cases
Method first, then three cases in order. The method is the plan-first order of C:\Users\bgbg0\Documents\Programming\roadworld\docs\research\earthworks-method-review.md (sections A root causes, B recommended order, C round 10 plan, "What should be thrown away"): decide the section type per station and side first (Slope, WallSlope, RockFace, Structure, Refuse), regularise it along the road on plan values, resolve between roads in plan space, emit ONE section polyline per station per side stitched into a ribbon, and build the ground raster, the wall meshes, the rock flags and the structure spans all from those ribbons. No bound clip, no relaxation pass as plan repair, no per-texel mixing of nine weights, no tiers, no second grading path.
FIRST STEP, before writing any rewrite: confirm the review's root causes 1 and 2 against the code in `sim_core/src/grading.cpp` and `sim_core/include/cp/grading.h` on current main (root cause 1: no plan object, `WallPlan` re-derived per texel and again in `wall_runs`; root cause 2: the structure/wall decision is keyed on centre or wall-line depth against `wall_cap` in `auto_structures` and never tests whether a slope or wall can daylight, so a 40 degree side hill flags no structure). Write what you found, with line numbers, as the first section of STATUS.md; if either cause is wrong or only half true, say so and adjust the plan before building. The review itself says it ran nothing, did not read `grading_layer.cpp` and did not verify UK numbers; its numbers (12 m, 6 m, 3 m, 9 m bench) are unsourced judgement and photos outrank them.

Case 1, one road on a realistic side hill (scene s4b, gradient about 0.3-0.5): slope or ONE wall per side, wall ends butted into the bank, no floating ribbons. Photos (open all):
- C:\Users\bgbg0\Documents\Programming\roadworld\docs\reference\street\46_wall-gabion-retaining-end.jpg
- C:\Users\bgbg0\Documents\Programming\roadworld\docs\reference\street\04_cutting-netted-rock-with-wall-end.jpg
- C:\Users\bgbg0\Documents\Programming\roadworld\docs\reference\street\55_wall-bhutan-chelela-paro-145.jpg
- C:\Users\bgbg0\Documents\Programming\roadworld\docs\reference\street\53_wall-slope-netting-retaining.jpg
- C:\Users\bgbg0\Documents\Programming\roadworld\docs\reference\street\03_wall-stone-weepholes-stepped-end.jpg
- C:\Users\bgbg0\Documents\Programming\roadworld\docs\reference\street\05_gabion-wall-bank-hazard-sign.jpg
- C:\Users\bgbg0\Documents\Programming\roadworld\docs\reference\street\06_gabion-wall-forest-road-stone-end.jpg
Note: 06 and 46 are byte-identical (round 9's auditor, md5 checked), so they count as ONE photo; 57_wall-in-dale.jpg is a 1 m field wall in Israel, not a road wall: do not use it. 03 and 05 were never opened as images by the review or the auditor. If fewer than 3 distinct matching photos remain after you open them all, fetch more (pre-approved) and log them in `docs\reference\street\INDEX.md`.
Before pictures for case 1 (round 9 cameras, open them):
- C:\Users\bgbg0\Documents\Programming\roadworld-wt-grade-r9\docs\progress\2026-10-11-structures-r9\s4b_side_hill_realistic_aerial.png
- C:\Users\bgbg0\Documents\Programming\roadworld-wt-grade-r9\docs\progress\2026-10-11-structures-r9\s4b_side_hill_realistic_end.png
- C:\Users\bgbg0\Documents\Programming\roadworld-wt-grade-r9\docs\progress\2026-10-11-structures-r9\s4b_side_hill_realistic_alongA.png
- C:\Users\bgbg0\Documents\Programming\roadworld-wt-grade-r9\docs\progress\2026-10-11-structures-r9\s4b_side_hill_realistic_street.png

Case 2, over-height hillside (scene s4 at gradient 0.85 and the C++ cases over_height_cliff and stress_gradient_085): structure or refusal, never a debug slab. NO matching photo exists for a road on a 40 degree hillside carried on a viaduct or half-viaduct with a rock cut above: you fetch at least 3 (reference images only; UK or Alpine side-hill viaduct, half-tunnel, rock cutting with netting; save into `docs\reference\street\` or `aerial\`, log source URL and content in the INDEX). Photos to open now:
- C:\Users\bgbg0\Documents\Programming\roadworld\docs\reference\street\04_cutting-netted-rock-with-wall-end.jpg
- C:\Users\bgbg0\Documents\Programming\roadworld\docs\reference\street\53_wall-slope-netting-retaining.jpg
- C:\Users\bgbg0\Documents\Programming\roadworld\docs\reference\street\38_cutting-chalk-benched-a27.jpg
- C:\Users\bgbg0\Documents\Programming\roadworld\docs\reference\aerial\29_gasteiner_strasse_tunnel_portals_rock_cutting_austria.jpg
- C:\Users\bgbg0\Documents\Programming\roadworld\docs\reference\aerial\28_a3_hindhead_tunnel_portals_cutting_uk.jpg
Before pictures for case 2 (beige rock-cut slab and orange needs_structure slab: these debug colours must not reach any headline picture):
- C:\Users\bgbg0\Documents\Programming\roadworld-wt-grade-r9\docs\progress\2026-10-11-structures-r9\s4_retaining_wall_run_aerial.png
- C:\Users\bgbg0\Documents\Programming\roadworld-wt-grade-r9\docs\progress\2026-10-11-structures-r9\s4_retaining_wall_run_street.png
- C:\Users\bgbg0\Documents\Programming\roadworld-wt-grade-r9\docs\progress\2026-10-11-structures-r9\s4_retaining_wall_run_end.png

Case 3, two carriageways at different levels and free road ends on a hill (scene s7 adjacent_levels), plus the median end-fade conflict at (900.0, 1798.2) that round 9 broke. Photos (open all):
- C:\Users\bgbg0\Documents\Programming\roadworld\docs\reference\street\50_split-level-m5-gordano-valley.jpg
- C:\Users\bgbg0\Documents\Programming\roadworld\docs\reference\street\51_split-level-a40-hillside.jpg
- C:\Users\bgbg0\Documents\Programming\roadworld\docs\reference\street\55_wall-bhutan-chelela-paro-145.jpg
- C:\Users\bgbg0\Documents\Programming\roadworld\docs\reference\aerial\28_a3_hindhead_tunnel_portals_cutting_uk.jpg
- C:\Users\bgbg0\Documents\Programming\roadworld\docs\reference\aerial\26_a9_gelly_overbridge_earthworks_scotland.jpg
No photo shows a road simply stopping on open ground (roads end at junctions): free road ends are a tool artefact; say so, and fetch one only if you need a target for the end cap shape. Before pictures for case 3:
- C:\Users\bgbg0\Documents\Programming\roadworld-wt-grade-r9\docs\progress\2026-10-11-structures-r9\s7_adjacent_levels_end.png
- C:\Users\bgbg0\Documents\Programming\roadworld-wt-grade-r9\docs\progress\2026-10-11-structures-r9\s7_adjacent_levels_end_west.png
- C:\Users\bgbg0\Documents\Programming\roadworld-wt-grade-r9\docs\progress\2026-10-11-structures-r9\s7_adjacent_levels_aerial.png
- C:\Users\bgbg0\Documents\Programming\roadworld-wt-grade-r9\docs\progress\2026-10-11-structures-r9\s7_adjacent_levels_alongA.png

Also open, all of them:
- C:\Users\bgbg0\Documents\Programming\roadworld\docs\research\earthworks-method-review.md
- C:\Users\bgbg0\Documents\Programming\roadworld-wt-grade-r9\docs\progress\2026-10-11-structures-r9\AUDIT.md (round 9 audit, verdict send back; its "For the next builder" has 10 items, all part of this brief, see Faults)
- C:\Users\bgbg0\Documents\Programming\roadworld-wt-grade-r9\docs\progress\2026-10-11-structures-r9\STATUS.md
- C:\Users\bgbg0\Documents\Programming\roadworld-wt-grade-r9\docs\progress\2026-10-11-structures-r9\FAULTS.md
- C:\Users\bgbg0\Documents\Programming\roadworld-wt-grade-r9\docs\progress\2026-10-11-structures-r9\COMPARE.md
- C:\Users\bgbg0\Documents\Programming\roadworld-wt-gradelayer\docs\progress\gradelayer\AUDIT.md (the layer audit: its fault 1 gives the new gate below; its item 8 and the merge-hazard section give the comparer updates this round owns)
- C:\Users\bgbg0\Documents\Programming\roadworld\docs\ACCEPTANCE.md (what the main session accepted and refused for grading before)
- C:\Users\bgbg0\Documents\Programming\roadworld\docs\planning-log.md, rounds 32, 36 and 45 (the grading rules and who set which number)
- C:\Users\bgbg0\Documents\Programming\roadworld\docs\reference\street\INDEX.md
- C:\Users\bgbg0\Documents\Programming\roadworld\docs\reference\aerial\INDEX.md
Reference photos are in the MAIN checkout (private, gitignored): never commit or copy them; composites go in `docs\reference\_compare\grade-r10\`.

Dimensioned sections are part of the hand-in (the main session checks them against technical drawings): for each case, at minimum 4 stations along the road (mid-run, run start, wall end or free end, worst slope) and both sides, a section sheet drawn from the C++ dump (`prototypes/mesh-view/tools/export_graded.cpp --dump`, ground.bin/ground.json) with numbers on it: formation width, verge, wall height and setback, batter ratio, ledge heights and widths, daylight distance `daylight_e`, roll-over band, ground level against road level in metres. Also a plan-view disturbance sheet per case. Sheets must read the plan ribbons as well as the raster the render reads (so the sheet shows the plan, not only its result); say in COMPARE.md which they read. Then check them against typical technical cross-sections: `docs\reference\technical\` does not exist yet; fetch 3 to 5 typical UK earthworks and retaining-wall cross-sections or standard drawings (reference images only, pre-approved; log source URL, content and the numbers read off them in a one-page `docs\reference\technical\NUMBERS.md`; sense check only, never a hard test) and name in COMPARE.md where your sections agree and differ.

## Faults
The photos decide, not this list. Starting list: round 9's 10 items, the method review, the layer audit.
Method and gates (new this round):
1. Plan-level invariants are written and PASSING on flat and synthetic terrain BEFORE any picture is made (method review C): every station has a feasible type or a Structure (count of unresolved `needs`/`Refuse` stations with no span and no tool refusal: 0); wall retained-ground check (ground at the back face within 0.3 m of the wall top, or buried); every wall run ends at retained height <= 0.35 m or buried (0 floating ends), with the wall height rate of change settled from photos 46 and 04 (the review's 1 m per 8 m is a suggestion); minimum wall height 1.0 m except in end ramps; no second-tier ribbon; changed ground beyond the formation edge equals the plan `daylight_e` plus roll-over (replaces the 20.5 m bound); no texel step above 1 m outside a wall face.
2. NEW GATE from the layer audit, fault 1: the ground is identical for any tile or block size. `grading.cpp` culls a road for a tile unless the tile overlaps one of the road's `seg_boxes`, so tiles near a free end returned natural ground while a one-call build returned embankment (up to 20.7 m on watton_thetford, 56.8 m on interchange). Add a test: for fixtures including a free road end on a slope, `interchange`, `watton_thetford_road_roundabout` and one dual carriageway end, grade the same area with tile/block sizes 32, 64, 128 and 256 m and with ONE call over the whole area; max texel difference must be 0.000 m (bit-identical float heights). Same for walls and structures: wall meshes built per tile equal the one-call build in triangle count and area. The comparison must not use any layer's own rebuild as its oracle.
3. The wall rule across roads: the layer audit found that per-road wall building and `export_graded`'s one-call `close_wall_gaps` joins differ (1.4 to 7 % of wall triangles in 4 of 8 scenes). Round 10 decides ONE rule in plan space (the lower road owns one shared wall, the upper road's side is `Supported`; no list-order joins across roads), makes `export_graded`, the prototype dump and any per-road path agree, and pins it with a test.
4. Comparer updates owned by this round: round 10 changes the grading structs (`GradingSample`, `WallPlan` and friends) that `agent/gradelayer`'s `grading_layer.cpp` compares (`same_grading_road`, `static_assert(sizeof(GradingSample) == 800)` and the three other struct-size asserts). The layer is NOT on main and that branch must never be merged (it carries private photo composites, commit e88751d). Deliver in the hand-in folder `LAYER_HANDOVER.md`: every struct field added, removed or changed with its meaning, the new `sizeof` values, which fields the comparer must compare, which inputs the layer's influence boxes and cluster rule depend on in the new method (plan stage per road or cluster, raster stage local to reach plus roll-over, `grading_halo_m` shrinking because the relaxation halo is gone: verify that claim in numbers), and a ready patch file `gradelayer_comparer.patch` made against a scratch checkout of `agent/gradelayer` (copy only `grading_layer.*` and its tests into a scratch folder; never commit that branch's e88751d; never add the branch as a remote). Gradelayer round 2 applies it.
Round 9 audit items (10), each a gate:
5. Scene check back to at least 20/21, then 21/21: median_flat (900.0, 1798.2) end-pad fade, ground_hidden_by_walls (1306.5, 637.4) road 16, ground_under_road (1316.8, 649.2), tight-curve bare step (1771.5, 834.8) 8.66 m. Do not accept an end fade that breaks a dual carriageway median ("ends are handled by the section cap, not by a global fade"). The JS grading twin (`prototypes/road-draw/src/earthworks.js`) stays on round 8 in this round; port the 21 scene invariants to read the C++ dump so they run on the one implementation (list them in STATUS.md). The main session decides separately whether to retire the JS twin: do not delete it, and list every place a golden or scene check still reads it.
6. Waivers: delete `is_rough` and every `GRADE_CHECK(is_rough(...))` (tests/test_grading.cpp:37; it covers seven checks for four named cases) and the dead `short_run_between_bridges` entry. No waiver, skip or `should_fail` is added. Waived counts to beat: over_height_cliff 4201/4227 bare, stress_gradient_085 43/630, adjacent_levels 117/1345 bare (4.25 m at (-0.25, 15)), ground_under_road 8/4444 (1.64 m), ground_hidden_by_walls 95/4444 (2.40 m). Each of those cases passes all seven checks outright.
7. Over-height hillsides (case 2): per decision 13 the plan proposes an authored bridge or viaduct span on the fill side and a ledged RockFace (ledge height from region data, 9 m in the current pack, to be checked against photos) or tunnel on the cut side, and refuses (plan type Refuse plus a warning string the road tool can show) ONLY when no structure fits; wiring the warning into the Unreal HUD is M3, not this round. Rock-cut beige and needs_structure orange viewer colours are removed from every headline render (rock material or grey).
8. s7 (case 3): no crescent pit, no gully, wall ends butted into the hillside; free-end invariant: max height step per 0.5 m texel past any free end <= 0.74 m (the r8 number) and no texel more than 0.3 m below both natural ground and formation within 12 m of an end plane; report cut and fill volume past each end plane.
9. Wall ends: butt into the bank with ground wrapping them (photos 04, 55, 53, 46); remove the S-curl and floating second-tier ribbons; minimum wall height.
10. References: a third distinct wall-end photo to replace the 06=46 duplicate (fetch and log), 53 opened, 57 replaced by a road retaining wall; real crest/toe height-profile sheets with numbers in place of the 3D renders named `*_profile.png`. The s6 re-shoot (embankment and road in frame; aerial and street) and the s3 portal batters (photo 20, Hindhead) are round 11, not this round.
11. No new compiler warning on gcc or MSVC (round 9 left C4189 at `grading.cpp(1810)`; that code is not on main).
12. Wall look is out of scope for materials (sandstone-pink panels, black posts), but if you touch wall meshes list whether the parts photo 50 shows are present: coping, barrier on the upper edge, drainage channel at the foot.
13. Process: three self-audit passes with every composite and disturbance image opened and named (round 9 left four composites and six image sets unopened); no tunable added without deleting two (count them in STATUS.md before and after; the region pack `grading` block has 38); same-camera before/after must show real movement, else stop and change method, not another tuning round. Throw away what the review lists under "What should be thrown away" as the code is replaced: tiers (`stair_profile`, `stairs_plan`, `StairTier`), the `needs`/kind-2 output, `run_treatment`, `has_alt`/`alt`/`sw`, wing blends, `rq_*` per-texel mixing, `apply_bound`, `relax_tile` as plan repair, `end_ceil`/`cap_f`/`end_w`/`bw`. Keep: `sample_edge_for_grading`, tunnel code, bridge/tunnel `StructureSpan` flow and `structure_mesh`, the analytic terrain harness in `tests/test_grading.cpp`, `export_graded --dump` and the section/disturbance tools (take those from `agent/grade-r9` by cherry-pick or file copy of the tool files ONLY; round 9's grading changes are dropped).
Gates (numbers, all must hold at hand-in):
- Start: worktree C:\Users\bgbg0\Documents\Programming\roadworld-wt\grade-r10, branch `agent/grade-r10`, cut from current main (round 8 grading). Do NOT merge `agent/grade-r9` or `agent/gradelayer`.
- ctest failed_assertions == 2 (the known `should_fail` pier case, unchanged), every other case passes, on gcc 16 AND MSVC 19.51 (clean builds, one compiler at a time; at most 2 full-suite runs on the machine at once; wait for your own background runs before the final report).
- C++ hard checks with no waiver: ground_under_road 0, ground_hidden_by_walls 0, no_bare_steps 0, crease <= 0.3, median_flat 0, slopes_daylight, wall_height_cap, on every scenario including s4, s4b, s7, over_height_cliff, stress_gradient_085, adjacent_levels.
- Scene check (ported invariants on the C++ dump) 21/21 at hand-in; never below the baseline 20/21.
- The tile-size gate of Fault 2 passes. The gradelayer branch's layer tests are NOT run (not on main).
- Existing cases keep their numbers: straight_crossfall, cutting_toe, embankment_end, bridge_valley, tunnel_curved_grade; the other r8 C++ cases keep their invariant counts.
- No Unreal build. Renders are required (same-camera before/after composites, section sheets), so the hand-in is visual.
Main session accepted these waiver rows of grade-r14 WAIVERS.md (24 Oct 2026: R1, R2, F1, G1-G6; R3 and D1 are NOT accepted):
allow_waiver guarded_check:test_grading.cpp:if (pl.type == SectionType::Ro
allow_waiver should_fail:// (prototypes/road-draw/tools/scene.mjs
allow_waiver assertion_retargeted:test_grading.cpp
allow_waiver check_false_swap:test_grading.cpp

## Launched
This job (grade-r10 builder, then a fresh auditor as soon as the hand-in passes `check_handin.mjs`, then gradelayer round 2). Independent items inside the job, one agent: (a) confirm root causes 1 and 2; (b) plan structs and plan-level invariants on synthetic terrain; (c) ribbons and raster; (d) wall meshes from ribbons; (e) tile-size gate; (f) reference fetch and technical drawings; (g) layer hand-over patch. Items (f) and (g) are research or patch writing and may be split off to a second Sonnet agent working in a separate scratch folder if a builder slot is free; they never touch `sim_core/src/grading.cpp`. It needs a builder slot (at most 3-4 live builders; roundabout2, tjunction2 and gates2 are running, pvetrees is not). It needs no Unreal lane.

## Waiting
Gradelayer round 2 (rebases the layer wrapper onto the new grading, takes the layer audit's cost faults 4 to 7 and the dense-district cost; threading stays closed by decision 14): waits for this round's audit. Round 11 (s6 embankment cameras, s5 cutting ledges and toe strip, s3 portal batters): waits for this round. Unreal side of the earthworks (M5 game wiring, refusal warning in the HUD): waits for M3 UI. Retiring the JS grading twin: main session decision after this round.
