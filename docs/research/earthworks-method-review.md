# Skipped, refused or failed

- UK engineering numbers (DMRB slope ratios, when a wall replaces a slope, bench spacing) are NOT verified. Searches reached only CD 622's scope page text and scheme drawings. `leicestershire.gov.uk` returned 403 for the CD 622 PDF; the Transport Scotland drawing PDF was binary and the fetch tool could not read it (saved at `C:\Users\bgbg0\.claude\projects\C--Users-bgbg0-Documents-Programming-roadworld\3e6af4f9-b127-4b69-a11a-0705aa4391e1\tool-results\webfetch-1791747788537-l26rul.pdf`, not opened by me). Everything numeric in section "UK practice" below is either a search-result summary (marked) or my own judgement (marked). Do not treat it as a standard.
- I did not build or run anything. Scene-check and suite numbers are the auditor's and builder's, copied with source. I did not re-run `scene.mjs --check` or the C++ suite.
- I did not verify that the `agent/gradelayer` worktree's `grading.cpp` equals main's r8 `grading.cpp`. I read only `grading_layer.h` (top 120 lines) and the layer's wrapping contract; I did not read `grading_layer.cpp` body.
- I did not open the r9 disturbance sheets for s1/s2/s3/s5/s6/s8, the r9 composites, or s1/s2/s8 renders (the auditor did not either). Rounds r1 to r6 are reconstructed from `docs/progress/*/INDEX.md`, `REVIEW.md` in r1, `docs/planning-log.md` rounds 36/45, `docs/ACCEPTANCE.md` and commit messages; I did not read r2..r6 REVIEW/STATUS files in full (several do not exist as STATUS files; only INDEX.md). Round table scores for r2, r4, r6, r7 are therefore partly "not recorded".
- I did not trace why the beige slab in s4 is exactly 20 m wide on the uphill side (rock-cut benches vs the bound). I believe it is the flagged rock-cut face (footprint class 2) plus the 20.5 m bound, but I did not instrument it.
- No new reference photos were fetched (none needed for this review; two cases below need a new one, listed in C).

# Reference files opened

Photos actually opened as images (12):
- `C:\Users\bgbg0\Documents\Programming\roadworld\docs\reference\street\04_cutting-netted-rock-with-wall-end.jpg`
- `...\street\20_tunnel-portal-hindhead-approach.jpg`
- `...\street\38_cutting-chalk-benched-a27.jpg`
- `...\street\40_cutting-m5-naish-hill-bridge.jpg`
- `...\street\42_embankment-motorway-grass-field.jpg`
- `...\street\46_wall-gabion-retaining-end.jpg`
- `...\street\50_split-level-m5-gordano-valley.jpg`
- `...\street\51_split-level-a40-hillside.jpg`
- `...\street\53_wall-slope-netting-retaining.jpg`
- `...\street\55_wall-bhutan-chelela-paro-145.jpg`
- `C:\Users\bgbg0\Documents\Programming\roadworld\docs\reference\aerial\28_a3_hindhead_tunnel_portals_cutting_uk.jpg`
- `...\aerial\26_a9_gelly_overbridge_earthworks_scotland.jpg`

INDEX text read (not images): `street\INDEX.md` (entries 01-21, matched by grep), `aerial\INDEX.md` (entries 03, 04, 07, 09, 10, 22, 26-31, 33, 36-38 and the case table). Not opened as images: street 06 (the auditor says it is byte-identical to 46; not verified by me), 03, 39, 41, 44, 45, 47, 49, 56, 57.

Renders and sheets opened (round 9, `C:\Users\bgbg0\Documents\Programming\roadworld-wt-grade-r9\docs\progress\2026-10-11-structures-r9\`): `s4_retaining_wall_run_aerial.png`, `s4b_side_hill_realistic_aerial.png`, `s4b_side_hill_realistic_end.png`, `s7_adjacent_levels_end.png`, `s6_embankment_aerial.png`, `s5_cutting_aerial.png`, `s3_tunnel_portal_aerial.png`, `views_disturb\s4_retaining_wall_run_after_sections.png`, `views_disturb\s7_adjacent_levels_after_sections.png`. Text: `AUDIT.md`, `STATUS.md` (first 6 KB), `FAULTS.md` (first 5 KB).

What the photos show (lane about 3.5 m; my estimates):
- 04: netted rock face about 8 m high at about 70 deg, rising straight from the verge; no berm, no ditch; a plain vertical concrete wall block ends the rock on the left; the face continues above it.
- 46: two-course gabion wall about 2 m; the bank above is the plain natural slope at about 35 deg starting right at the wall top (no bench, no regrade); the wall ends by stepping down and butting into old masonry, ground wraps round it.
- 55: dry-stone wall about 3 m, level coping, 0.5 to 1 m grass strip, then unregraded hill; wall end is vertical into the bank.
- 53: concrete panel wall about 3 m with piers; netting slope at 35-40 deg directly above the wall top; ground above is untouched.
- 50: a continuous dark wall about 6 to 8 m high between two carriageways of a split-level motorway, curving with the road, barrier on the upper edge, a drainage channel at the foot; hillside above the upper carriageway shows no large regrade.
- 51: distant view of the same sort of split level: two thin ledges in a wooded hillside; no scar. Detail: none.
- 38: chalk cutting, both sides about 12-15 m at about 1:1.5 to 1:2, narrow irregular ledges (not regular benches), 3-4 m vegetated strip at the toe, rounded crest.
- 40: M5 cutting with a wooded slope about 1:2 on the left, an overbridge; no walls.
- 42: embankment seen from a field, about 6 m, wooded, fence at toe, soft concave toe.
- 20: Hindhead portals set into a symmetrical regraded grass bank of about 12 m with a smooth crest and a flat shelf above; low stepped wall beside the approach.
- 28 (aerial): the portals sit at the end of a cutting whose two side slopes run continuously into the portal faces; wing walls are low and plain; the slope above the portals is untouched forest.
- 26 (aerial): fresh abutment embankments with rounded slopes about 1:2; the surrounding works zone is regraded, not cliffs.

# A. Root causes

Size of what is being tuned: `sim_core/src/grading.cpp` 2446 lines, `prototypes/road-draw/src/earthworks.js` 954 lines, `tests/test_grading.cpp` 1495 lines, 38 tunables in `data/regions/uk/region.json` "grading".

## Round history (what was tried, what failed, what came back)

| Round | What it tried | Check result / waivers | Reviewer said |
|---|---|---|---|
| port (commits 96056e1, 572e1b4) | JS prototype grading, 12 m slopes, wall cap 12 m | JS scene check 16/16 then 21/21 (572e1b4) | not rated |
| r1 (10 Oct) | first structure renders from ported C++ | none recorded | 4.5/10: coping slab too wide, vertical striping on steep hill, wall-end stair-stepped, s8 sawtooth (`progress/2026-10-10-structures/REVIEW.md`) |
| r2 | wall mesh only (coping, panels); grading untouched | none | not rated |
| r3 (e66edb7) | 3 m wall cap + tiers/benches, shared structure, tapered ends, toe ditch | not recorded | 4.5/10, "no net change; wall cap falls back on steep ground, ends do not taper cleanly" (commit a6f8b92) |
| r4 (fb4b107) | terraced walls at fixed pitch; diagonal ends | "7 of 9 invariant sweeps hard again" (2 waived) | terraces rejected as a "ziggurat" (8 walls on s4); s7 east end dark pit/gouge (commit 3a441d3; r4 INDEX) |
| r5 (acb90f3) | slope, then 1 wall, then 2 tiers, then rock cut (cut) / 6 m wall / `needs_structure` (fill) | s7 end pit still open | 5/10, ziggurat gone; s7 end pit and wall ends open (commit 67e9f88) |
| r6 (ac3cfc8) | cut/fill displacement crossfade between roads; rounding capped at 1/3 slope | adjacent_levels waived (pit "halved, not gone") | not rated |
| r7 | smoothing of the displacement, 8 passes; toe ditch; crease bound 0.3 | adjacent_levels + short_run_between_bridges waived | not rated; wall ends NOT changed |
| r8 (a95dbf9, cfd0776) | slope efficiency rule (`slope_max_ratio` 3.0, 15 m cap), raked wall tops, wall gap bridging, end caps | JS scene 20/21: `no_bare_steps` 749/2207 worst 6.9 m; adjacent_levels still waived | owner spotted slopes chasing hillside; wall ends "not changed" (STATUS) |
| r9 (de26275) | hard bound (20.5 m), free-end run-out 12 m, wall_start 12 to 7 | JS scene 17/21 (ground_under_road, median_flat, ground_hidden_by_walls, no_bare_steps 1374/5215 worst 8.66 m); new waivers over_height_cliff and stress_gradient_085; adjacent_levels waiver kept; MSVC C4189 | AUDIT: send back. s4 has no wall (debug slabs), s4b loose wall strips, s7 pit and gully, s6 views empty |

## Faults that come back, round after round

| Fault | Seen in rounds | Evidence |
|---|---|---|
| Pit / gouge / gully where two roads (or one road) end on a hillside (s7) | r4, r5, r6 (halved), r7 (cause "proven"), r8 (lower road fixed, upper end face not), r9 (new crescent pit) | `s7_adjacent_levels_end.png`; sections `s7_..._after_sections.png` |
| Wall ends do not butt into the bank (stepped, blunt, floating, S-curl) | r1, r3, r5, r7 ("NOT changed"), r8 ("grading side NOT changed"), r9 ("not attempted") | `s4b_..._end.png`; photos 04, 46, 55 |
| Steep hill (s4, 0.85 = 40 deg) gets no honest answer: pierced 10 m wall, 8 terraces, rock + needs_structure, debug slabs | r1 (10 m walls), r3 (cap falls back), r4 (ziggurat), r5/r8/r9 (slab) | `s4_..._aerial.png` |
| Rock-cut / plan run ends as a cliff or ragged stair edge (`no_bare_steps`) | r8 (6.9 m), r9 (8.66 m; 4201/4227 texels on over_height_cliff) | audit fault 1, 2 |
| Fan / bowl of changed ground past road ends and long creases on the hill | r7, r9 Step A faults 1 to 7 in FAULTS.md | `FAULTS.md` Step A |
| Headline cameras that show nothing (s6) | r4 ("views remain weak"), r9 | `s6_embankment_aerial.png`: flat field, road line |
| Waivers that hide real faults | r4 onward; r9 adds two and widens scope | `tests/test_grading.cpp:37` `is_rough` |
| Passing numbers that do not mean "looks right" | every round | s4b loose ribbons pass every invariant |

Scene-check trend (JS): 21/21 (port) -> 20/21 (r8) -> 17/21 (r9). Reviewer scores 4.5 -> 4.5 -> 5 over r1 to r5; no later score recorded. Nine rounds, one reviewer-visible movement (stacked terraces removed in r5).

## Root causes, most important first

### 1. There is no plan object. The "plan" is a heightfield plus 14 per-station side arrays, and every consumer re-derives it.
In `grading.cpp` the wall/slope decision is stored as `WallPlan` per sample per side (grading.h lines 125-154: `present, sup, cut, full, wing, over, fw, t, has_base, base, base_stairs, base_h, base_mt, base_kind, stairs, mt, kind, own, rock, needs, h, n_stairs, reach_e, along_max, has_alt, alt, sw`). Then `grade_tile_into` (line 1327 onward) re-evaluates a 1-D lateral profile PER TEXEL from the nearest station and mixes up to nine interpolated weights (`rq_ww`, `rq_fw`, `rq_sv`, `rq_sp`, `rq_endw`, `rq_al`, `rq_bt`, `rq_fs`, `rq_fl`). Then `wall_runs` (line 1982) rebuilds the WALL MESHES by re-running the same blending (`g_at`, lines ~2040-2060; comment at ~2000: "the grading interpolates weights linearly between its decimated stations (every 2nd sample): do the same, so wall heights meet the graded ground"). So the wall mesh and the ground are two separate reconstructions of one idea, kept aligned by copied logic. Anything that touches the ground after the plan (bound, smoothing, run-out fades) breaks the match, and nothing in the system can say "this wall holds nothing". This is why `s4b_side_hill_realistic_aerial.png` shows wall ribbons lying on the slope: the upper-tier ribbon is emitted by `wall_runs` per tier k wherever `stair_profile` has a k-th tier at that station (`cur = -1` on any break), but the ground there is whatever the blends and the relaxation left. No invariant checks "ground behind the wall top is at wall top height" (see 5).

### 2. The structure / wall / slope decision is keyed on the wrong quantity and made per side per station, then patched with morphology.
- Bridge/tunnel decision: `auto_structures` (line 564) flags a bridge only if the centre or a wall line is more than `wall_cap` = 12 m (region.json) above/below ground. It never looks at how far the slope would have to run. A road laid along a 40 deg hill has centre dz about 0 and wall lines about +-5 m (half-width 7 x 0.85), so NO bridge and NO tunnel is ever flagged (`s4`). A road with 13 m of fill on flat ground is flagged. The rule measures depth below the road, not "can a slope or one wall daylight within the corridor".
- Side decision: `plan_side` (line 458) probes the ground along the normal out to `E = verge + (wall_start + wall_slack) * batter` (about 17 m), calls `solve_tiers` (line 275) and, if one wall plus slope fails, `stairs_plan` (line 411): two 3 m tiers; else for cut a rock face (`kind` 1, `own` 3), for fill one 6 m wall or `needs` steep fill (`kind` 2). `wall_runs` then emits NO walls for kind 1/2 ("rock cut / steep fill: no walls", line ~2035). So on a 40 deg hill the code deliberately builds neither wall nor structure: the cut side becomes a flagged rock surface (viewer colour beige), the fill side a 1:1.5 slope (34 deg) that can never meet a 40 deg ground and is cut off by `apply_bound` at about 20.5 m (orange slab, ragged edge). `s4_retaining_wall_run_aerial.png` is exactly that. `s4_..._after_sections.png`: the graded line is parallel to the ground, offset, in a +-20 m orange "changed" band at every station; no magenta wall anywhere.
- Station flags are then cleaned by five morphological operations on the boolean `w` (`close_gaps`, `dilate`, `grow_short`, `drop_short_len`, run_treatment "one treatment per run if ANY station needs it", lines 681-768). On a bumpy hillside these decide where a wall run starts and stops, independent of whether a wall is geometrically needed there. That is the mechanism for walls starting/stopping mid-slope.

### 3. Constraints are applied after the plan and clip it, instead of being decided in the plan.
`grade_tile_smooth_into` (line 1968-ish): grade -> `apply_bound` -> `relax_tile` (8 passes) -> `apply_bound` again (lines 1894-1976). The bound (`B = verge + side_bound_run 15 + round_daylight + hinge/2 + ditch` = 20.5 m, shrunk toward `end_run_out` 12 m by `bw`) is a hard clip on lateral extent. If the plan has not daylighted by then (any hillside steeper than the batter, any 1:1.5 fill on a 40 deg slope), the clip creates a cliff: `no_bare_steps` 4201/4227 texels on over_height_cliff, 43/630 on stress_gradient_085. The right answer for those cases is a different plan (structure or refusal), which the pipeline cannot produce because feasibility is never tested before rasterising. Same pattern in r8 (`slope_max_ratio` rule escalated side-long cuts to rock, creating cliffs where a rock run ends, `no_bare_steps` 749/2207) and r9 (end-pad fade vs median).

### 4. Road ends are handled by six overlapping mechanisms, so ends produce pits and fans.
For one idea ("what happens when this road stops") `grade_tile_into` and `grade_tile_smooth_into` use: the round end cap as a zero-width cut/fill candidate with weight `cw` (lines ~1584-1592); the terrace carry-on `pro` branch with `along_max`/`end_ext` (1492-1515, 1574-1582); `rq_by == 2` run-out weight `rfs *= 1 - u*u*(3-2u)` (1616-1620); `end_ceil` / `cap_f` ceilings (1569-1573, 1740-1743); `end_w`/`bw` shrinking the bound (1894-1909); wall weights `ww` and `wall_ramp` plus wing blends. Each is smooth by itself; they add or take min/max with each other ("the two weighted displacements ADD ... capped by the largest full one", lines 1346-1347, 1730-1735). Where two roads at different levels end next to each other (`s7`), these interact and leave the crescent pit and gully in `s7_adjacent_levels_end.png` (r9 FAULTS Step A items 1-5 list the cone, pad, sliver, finger and creases, each a separate patch). It has been "fixed" in r4, r5, r6, r7, r8 and r9; the pit moves.

### 5. The measuring tools check smoothness and containment, not "does it look like earthworks in the photos".
The 21 JS invariants and 18 C++ cases check: ground not above the road, ground hidden by walls, no bare steps over 1 m, crease below 0.3, median flat, bound. All of `s4b_side_hill_realistic_aerial.png`'s floating ribbons pass them. There is no check that (a) a wall has retained ground at its back face within about 0.3 m of its top and free air or road in front, (b) a wall's ends reach height 0 or buried into ground, (c) every station's section type is one of the intended ones (no `needs`, no flagged debug class in a headline render), (d) the section plan is continuous along the road (wall height rate of change). So each round "improves" what the invariants see and a reviewer finds the next thing.
The waiver mechanism makes it worse: `is_rough` (test_grading.cpp:37) wraps SEVEN checks (`GRADE_CHECK(is_rough(name), ...)` at lines 741-743, 848-850, 1341) for four named cases; STATUS claimed two. It already hid 99% bare texels (over_height_cliff) and a real pit (adjacent_levels, 117/1345 bare, 2.4 m wall hide).

### 6. Why fixing one scenario breaks another (20/21 to 17/21).
Because the same global weights serve every scenario. Example from STATUS r9: the end-pad fade (added for s7's pit) removes the end run-out of the motorway median at (900.0, 1798.2): `median_flat` 14/30600, 1.007 m step, "with the fade off it passes". `slope_max_ratio` added in r8 for s4b's "chasing" slopes created the rock-run-end cliffs in the JS world. Each round adds tunables (`wall_start` 12 to 7, `end_run_out` 12, `side_bound_run` 15, `slope_max_ratio` 3.0, ...) and a new weight field, tuned on the scenes the auditor looked at last. With 38 tunables, nine weights and a flag-morphology stage, there is no local reasoning: a change in `plan_side` shifts run membership, which shifts `ww`, `sw`, `fw`, which shifts blended ground everywhere in reach. Also builders do not finish: r9 self-audit 2 of 3 passes, 4 of 6 composites unopened.

### 7. Heightfield plus bound: what it can and cannot express.
- Can: cuts and fills with slopes up to about 1:1 at 0.5 m texels, crest/toe rounding, ditches.
- Cannot: vertical faces (a wall is a step; the code hides it with a 1.5 m "wall band" and the `kFaceStep` 1.2 m exemption in the invariants, grading.cpp:1767), an overhanging or undercut face, a viaduct (ground continuing under the road at its natural height) and a bore (already handled separately: `tunnel_volumes`, `in_bore`).
- The bound is not an expression of anything; it is a limit that conflicts with the plan whenever a plan does not daylight. So the heightfield approach CAN express what a game needs (slopes, rounded, with separate wall meshes), but only if (i) the plan is a daylight-checked section per station and (ii) the wall mesh is generated from the very same section, not re-derived. Today neither holds.

### 8. The JS and C++ twins.
They share the algorithm line by line (names mirror: `slopeFits`/`slope_fits`, `stairProfile`, `applyBound`, `edtSq`) and are held to each other by goldens. Computationally they agree on the 18 analytic cases. They do NOT agree in coverage: the C++ suite runs 18 synthetic cases (passes with waivers; 365 cases, 2 known failures), the JS scene check runs 21 invariants on the prototype's world and fails 4. So "C++ passes" says nothing about the JS world, and "JS 17/21" has not been reproduced in C++ (D). The twin costs every round double work and golden regeneration (r7, r8 STATUS list "goldens regenerated" on every commit), and the Unreal game uses only C++. I recommend ending the JS grading twin (keep the prototype road UI); see C.

### 9. Process causes that matter for method.
- "Tuned by eye" values (planning-log round 32/36/45: "numbers are the assistant's judgement from 11 opened photos") became load-bearing constants (wall cap 12 vs hard max 6 vs wall_start 7, rock bench 9 m).
- No physical vertical-profile feedback: `sample_edge_for_grading` takes `rs.z = e.vertical.z(s)` (grading.cpp:2421) as given. On a 40 deg side hill the only inputs left are structure or refusal; the code has neither path for the side-hill case.
- Reviewers see headline pictures; builders' camera choices (s6) and the instrument names (`*_profile.png` are 3D renders) make the evidence weaker than it looks (audit measuring-tool findings).

# B. Is the decision order wrong?

## Current order, from the code

1. Sample road (`sample_edge_for_grading`): station z is authored input (`e.vertical.z(s)`).
2. `classify_structures` -> `auto_structures`: bridge/tunnel only from centre / wall-line depth > `wall_cap` 12 m, with `close_gaps` and `drop_short`.
3. `classify_walls` per road: for each station and side, `plan_side` -> one slope / one wall / terraced stairs plan; neighbour roads enter via `NeighbourGround` (contact levels, owner structure `owner_tiers`).
4. Flag morphology on the wall booleans: `close_gaps`, `dilate`, `grow_short`, wings at portals, `drop_short_len`; padded stations re-planned with `min_tiers`; run treatment (one rock/steep/hard/2-tier treatment per run); blend ramps (`has_alt`/`sw`) beyond run ends; wing blends (`fw`).
5. `prepare_grading_roads`: `mark_medians`, `compute_wall_weights` (`ww`), `slope_reach`, neighbour lists.
6. Per tile and texel (`grade_tile_into`): nearest station per road -> 1-D lateral profile evaluated on the spot (`tier_eval`, `stair_eval`, plain slope with ditch, end cap, run-out), recorded as cut / fill candidates per texel across roads, combined with crossfade and end ceilings.
7. `apply_bound` -> `relax_tile` (8 passes) -> `apply_bound`.
8. `wall_runs`: wall meshes re-evaluated from the plan with copied blending.
9. Invariants and viewers judge the result.

The plan exists only in steps 3-4 as per-station flags; steps 6-8 reinterpret it and step 7 changes the geometry afterwards.

## Recommended order (decide section type first, smooth along the road, then build everything from one plan)

1. Sample road (unchanged); read the ground along the normal at every station and side as a 1-D profile `N_side(e)` out to the corridor limit (about 25 m).
2. **Section decision per station and side, from `N(e)` and limits only.** Choose the cheapest feasible type: `Slope` (daylights within `corridor` at batter 1:2 to 1:3), `WallSlope` (one wall of height h in [h_min, h_max] with the slope above or below it, whichever daylights), `RockFace` (cut only, steep batter with ledges, up to a max height), `Structure` (bridge/viaduct on the fill side, tunnel or cut-and-cover on the cut side), `Refuse` (nothing feasible: the tool shows "this road needs a structure here / change the line"). The result is a plain record: `{type, formation_z, wall_h, wall_setback, batter, ledge_count, daylight_e, feasible}`. Structure and wall decisions are made together because they are the same question ("how far apart in height is the road from the ground at the daylight distance").
3. **Regularise along the road, per side, on the plan parameters, not on boolean flags.** Hysteresis between types, minimum run length per type, a limit on how fast `wall_h` may change per metre (the wall top follows a ramp, which is what "raked top" and photos 46, 04, 03 show), and wall ends defined as `wall_h -> 0` over a distance (the wall dies into the bank). Convert `Structure` runs into the existing authored `StructureSpan`s (the repository already has `StructureMode::Authored` and bridge/tunnel meshes), so `needs_structure` never reaches the ground.
4. **Resolve between roads in plan space** (before any geometry): where footprints of two roads are closer than the two sections need, one structure for the pair, built by the lower road (what `owner_tiers` and `NeighbourGround` try to do today), and the upper road's side gets type `Supported`. Medians likewise. Done on the plan arrays, not by crossfading raster displacements.
5. **Emit one section per station per side** (a polyline: verge, optional ditch, wall base and top, slope or rock face, roll-over into natural ground at `daylight_e`) and stitch consecutive stations into a ribbon (strip) per side. At a free end the ribbon ends with a designed cap: the formation tapers into the ground by the same section logic with `wall_h` ramped to 0; no separate end mechanisms.
6. **Build everything from the ribbons:** (a) raster the ribbons onto the tile heightfield with cut = min(natural, ribbon), fill = max(natural, ribbon), blending only in the roll-over band; ground outside `daylight_e` is bit-identical to natural, so there is no bound and no relaxation pass; (b) wall meshes are the vertical segments of the same section polylines, with ends from the same ramp; (c) the rock-face / structure flags come from the section type; (d) tests and invariants read the ribbons first and the raster second.
7. Invariants (new, plan-level) before pictures: every station has a feasible type or a Structure; wall retained-ground check; wall-height rate; every wall run ends at height <= 0.35 m or buried; no texel step above 1 m outside a wall face; changed-ground extent equals the plan's `daylight_e` plus roll-over (replaces the bound).

## Data structures needed
- `SectionPlan` (per station per side) as listed in step 2, plus `status` (feasible / structure / refused) and `owner` (this road, neighbour, shared).
- `PlanTrack`: arrays of `SectionPlan` per road per side, with the regularisation passes as pure functions over it (unit-testable without any terrain raster).
- `EarthRibbon`: per station a short section polyline (about 8-12 points: `e`, `z`, `kind`), with a free-end cap record.
- `GradingState`-compatible outputs: the existing `GradedGround` height and footprint classes (0 free, 1 solid, 2 rock) and `WallRun` for `structure_mesh`, so the Unreal host and `structure_mesh` do not change.

## What of the existing code survives
- Survives: `sample_edge_for_grading`, `ground_fn`, tunnel code (`tunnel_profile`, `tunnel_volumes`, `in_bore`), bridge/tunnel `StructureSpan` flow and `structure_mesh`, `edt_sq`/`window_count` utilities if needed, `slope_fits`' idea (daylight check) as the basis of step 2, `owner_tiers`' idea for shared structures, the golden/analytic terrain harness in `tests/test_grading.cpp` (the `Terrain` evaluators, invariant functions, `kCases`), `export_graded --dump` and the section / disturbance sheet tools (the auditor found them faithful to the mesh, 0.000 m, with the caveat that they read the same `GradedGround`), the WorldCache layer wrapper (`grading_layer.cpp`) and its dirty-path test pattern.
- Survives as an idea: crest/toe rounding (`round_daylight`, `round_hinge`), toe ditch for rock, raked wall tops, tier cap of 3 m only as a wall height limit (no tiers).
- Layer contract to keep (from `grading_layer.h`): the layer only wraps `sample_edge_for_grading`, `classify_road`, `prepare_grading_roads`, `grade_tile_smooth_into`, `tunnel_volumes`; edits re-prepare the road CLUSTER (whole roads), then recompute blocks whose halo overlaps an influence box; `update()` must stay bit-identical to `rebuild_all()` and no texel outside the dirty set may change. The new method fits that contract if the plan stage is per road/cluster (it is: whole-road windows) and the raster stage is local to reach + roll-over; `grading_halo_m` shrinks because the relaxation halo disappears (verify by running the layer tests; I did not).

## Recommendation about merging (judgement, flagged)
Do not merge r9's grading algorithm changes (hard bound, end run-out, `wall_start` 7): the audit shows it regressed. Keep r9's tooling (`export_graded --dump`, sections and disturbance sheets). Merge `agent/gradelayer` on r8 grading (which main already holds), because round 10 replaces the internals behind the same entry points. I did not verify that gradelayer's grading.cpp equals main's.

# C. Round 10 plan (3 cases, in order)

General rules for the round: no new tunables without deleting two; waivers removed, not extended; the plan-level invariants are written and passing on a flat/synthetic case BEFORE any picture is made; builder, a fresh auditor, then main session as usual.

## Case 1: one road on a realistic side hill (the s4b scene): slope or ONE wall per side, wall ends butted into the bank
- Matching photos: 46 (gabion, end butting masonry, bank above at 35 deg), 55 (dry-stone wall, unregraded hill behind), 53 (panel wall, netted slope directly above), 04 (wall end meets rock face), plus text-only 03 (stepped diagonal end) and 05. All opened except 03/05 (INDEX text). The photos agree: one wall 1.5-4 m, hill above is untouched or a plain slope, nothing is terraced.
- Measurable gates (new C++ invariants over `SectionPlan`/ribbons, then confirmed on the raster):
  - `wall_retains`: along every wall run, ground at the back face within 0.3 m of the wall top (or natural ground where the wall is buried); gate: 0 violations.
  - `wall_ends`: every wall run's end retained height <= 0.35 m or buried in ground; gate: 0 floating ends; wall_h rate of change <= 1 m per 8 m of road (value to be settled by the builder from photos 46 and 04, not assumed).
  - Minimum wall height 1.0 m except in end ramps; no second-tier ribbon (tiers deleted).
  - `no_bare_steps` 0, `ground_hidden_by_walls` 0, `ground_under_road` 0, crease <= 0.3 (existing numbers) with NO waiver for `side_hill_realistic`, `wall_steep_side`, `bench_side_hill`, `cutting`, `embankment`.
  - Changed-ground extent beyond the formation edge == plan `daylight_e` + roll-over (replaces the 20.5 m bound).
  - Same-camera before/after must show real change: `s4b_side_hill_realistic_end.png` and `_aerial.png` from the r9 cameras: no floating ribbons, wall ends dying into the bank. If the composite looks the same as r9 the method has not changed; stop.
- Must not regress: the 14 other C++ cases keep their r8 invariant counts (waived cases are measured as before until they come up); `straight_crossfall`, `cutting_toe`, `embankment_end` hard checks stay green; layer tests (bit-identical update vs rebuild) stay green; Release suite on gcc and MSVC (365 cases, only the 2 known failures); the MSVC C4189 warning fixed or deleted with the code.

## Case 2: the over-height hillside (s4 at 0.85 and over_height_cliff 1.6): structure or refusal, never a debug slab
- Matching photos: partly 53 and 04 (a 6-8 m netted rock/retaining combination above a road); no matching photo for "road on a 40 deg hillside carried on a viaduct or half-viaduct with a rock cut above": fetch one (UK or Alpine side-hill viaduct and half-tunnel; reference images only, log source in the INDEX). For a rock cut of about 10-20 m with ledges: 38 (chalk, benches), plus fetch a UK rock cutting photo with netting.
- Measurable gates:
  - Every station's plan type is one of {Slope, WallSlope, RockFace, Structure}; count of `needs` / `Refuse` stations with no Structure span or tool refusal: 0.
  - In `stress_gradient_085` and `over_height_cliff`: fill-side stations beyond the wall limit become authored bridge/viaduct spans (counted), cut-side stations become `RockFace` with ledges at <= 9 m (value from region data) or tunnel; no flagged debug colours reach the viewer (viewer colour for rock-cut becomes a rock material or grey, needs_structure colour removed).
  - Waivers `over_height_cliff` and `stress_gradient_085` deleted: `no_bare_steps` 0 (was 4201/4227 and 43/630), `ground_hidden_by_walls` 0, crease <= 0.3.
  - `s4_retaining_wall_run_aerial.png` from the same camera shows a viaduct or wall on the fill side and a ledged rock face above, no beige or orange slab.
- Must not regress: Case 1 gates; `bridge_valley` and `tunnel_curved_grade` (existing structure cases) keep their numbers; the 2 known `should_fail` pier assertions unchanged.
- Open decision for the owner: at what steepness the road tool refuses to place the road rather than proposing a structure. I set no number.

## Case 3: two carriageways at different levels and road ends on a hill (s7 adjacent_levels), plus the median end-fade conflict
- Matching photos: 50 (split-level M5: one tall continuous wall between the carriageways, nothing scooped at the ends, drainage channel and barrier), 51 (distant, no detail), 55 (wall end), plus 28 aerial (approach and wing walls at the end of a cutting). Road ends on open ground: no photo shows a road simply stopping (roads end at junctions); no matching photo, fetch one if the builder needs a target for temporary free ends (they are a tool artefact).
- Measurable gates:
  - `adjacent_levels` waiver deleted: `ground_under_road` 0 (was 8/4444, 1.64 m), `ground_hidden_by_walls` 0 (95/4444, 2.4 m), `no_bare_steps` 0 (117/1345, 4.25 m at (-0.25, 15)).
  - Free-end invariant: for every free road end, the maximum height step per 0.5 m texel past the end <= 0.74 m (the r8 number) and no texel more than 0.3 m below both natural ground and formation within 12 m of an end plane ("no pit": a new check, cut and fill volume past the end plane reported).
  - `median_flat` 0 on the dual-carriageway end (the r9 regression at (900.0, 1798.2) must not come back; the rule is "ends are handled by the section cap, not by a global fade").
  - Shared structure in plan space: the lower road owns one wall, the upper road's side `Supported`; verify with the plan table in the hand-in.
  - Same-camera `s7_adjacent_levels_end.png`: no crescent pit, no gully, wall ends butted into the hillside.
- Must not regress: Cases 1 and 2 gates; all others as above.

## Round 11 (not in this round): s6 embankment with proper headline cameras (photo 42, 44/45 text-only), s5 cutting with ledges and toe strip (photos 38, 04, 40), s3 tunnel portal in a regraded bank with symmetric batters round the arch (photos 20, 28 aerial). Re-shoot s6 aerial and street so road and slope are in frame (audit fault 7).

## What should be thrown away
- Terraced stairs: `stair_profile`, `stairs_plan`, `StairTier`, `StairResult`, `bench_z`, `kFadeK`, `max_tiers`, `bench_min_width`, `berm_slope`, `kMaxWallTiers` and tier runs in `wall_runs` (the "ziggurat" the owner rejected; at most one wall).
- Per-station kind 1/2 plans as ground without wall or structure: `needs`/`kind == 2`/`fill_steep_batter` as an output (`surf_z` kind 2) and the viewer's needs-structure colour.
- `run_treatment`, `has_alt`/`alt`/`sw`, wing blends (`fw`, `base_*`), `wall_ramp` blends, `close_gaps`/`dilate`/`grow_short`/`drop_short_len` on wall flags (replaced by regularisation of plan values).
- `rq_*` per-texel nearest-station mixing of nine weights, the `pro` terrace carry-on, `end_ceil`/`cap_f`, `end_w`/`bw`, `reach_fade`, `along_max`, `end_ext` (replaced by the ribbon and its end cap).
- `apply_bound`, the tail fade (`kTailFrom`), `side_bound_run`, `end_run_out` as a fade, the second bound pass, and `relax_tile` as a plan-repair step (keep only a local crest/toe rounding inside the section polyline).
- `is_rough` and all `GRADE_CHECK(is_rough(...))` calls; the dead `short_run_between_bridges` entry.
- The JS grading twin `earthworks.js` (grading functions) and the golden lockstep; keep the prototype road UI. Port the 21 scene invariants (`ground_under_road`, `ground_hidden_by_walls`, `no_bare_steps`, `median_flat`, ...) to read the C++ dump so they run on one implementation.
- About 20 of the 38 region.json tunables; keep: `formation_drop`, `batter`, `verge`, wall height min/max, wall setback, rock batter and ledge height/width, `round_daylight`, `toe_ditch_*`, `corridor_max`, `wall_cap`/structure limits, `wing_*` if portal wings stay.

## UK practice (research notes; not verified standards)
- CD 622 "Managing geotechnical risk" (formerly HD 22/08, BD 10/97, HA 120/08): defines technical approval and certification for earthworks, retaining structures and ground treatment; it is a process standard and does not set slope ratios (search-result summary, https://www.leicestershire.gov.uk/sites/default/files/field/pdf/2021/3/18/TG5-DMRB-CD-622-managing-geotechnical-risk.pdf, fetch failed 403; also https://www.infrastructure-ni.gov.uk/articles/dfi-roads-geotechnical-certification).
- Search-result summary of a Transport Scotland scheme drawing: cut slopes 1V:3H and embankment slopes 1V:2H as a project choice, and topsoil 150 mm maximum on slopes of 1V:2H or steeper (https://www.transport.gov.scot/media/4khlbed0/a83aab-awj-eac-mts_gen-dr-vt-000008.pdf; PDF binary, not read by me). Another scheme drawing lists cut 1 in 2 and fill 1 in 1.5 (https://www.contractsfinder.service.gov.uk/Notice/Attachment/33d23b80-63a6-43f3-9480-ce3d4b59e8c2; not opened). These are scheme choices.
- Historic UK source, not read: TRL report 488, "The design of slopes for highway cuttings and embankments" (https://www-old.bridgeforum.org/dir/ha/report/SSR-TRL-488.html).
- US guidance with concrete rock-cut numbers, NOT UK: Iowa DOT normal rock cut 0.5H:1V, bench at most every 35 ft (10.7 m), ditch at least 10 ft (3 m) (https://iowadot.gov/design-manual/chapter-3-cross-sections/3j-1-berms-and-benches); Minnesota DOT planning assumption rock slope 2V:1H with catchment (https://mndot.org/materials/geologydocs/rockslopes.pdf). These happen to match the region pack's `rock_cut_batter` 0.5, `rock_bench_height` 9 and berm 3.
- Network Rail's earthworks page (https://www.networkrail.co.uk/running-the-railway/looking-after-the-railway/earthworks-cutting-slopes-and-embankments) was in results; not read.
- My judgement, flagged: the photos show real UK roads with one wall (1.5 to 4 m) under a plain slope, ledges and a toe strip in chalk cuts (38), rock faces at about 70 deg with netting (04) and no terraces. Where ground is too steep for a soil slope the practice in the photos is a wall, a rock cut, or a structure. I found no source for "when a wall replaces a slope" in DMRB; the 12 m / 6 m / 3 m numbers in the code remain unsourced judgement. How two carriageways at different levels are separated: photo 50 shows one tall wall plus barriers and a drainage channel; I found no DMRB text on it.

# D. What I could not determine, and what would settle it

1. Whether C++ and JS fail on the same scenes. Settle by exporting the scene-check world's road graph and running the C++ invariants on `export_graded --dump`; if the numbers differ from JS 17/21, the twins disagree in a way the goldens do not cover.
2. Exactly why the s4 uphill band is 20 m wide (rock-cut ledges vs bound). Settle by dumping per-texel `cut_k`/`footprint` class and `bw` for s4 and plotting against the section lines; it does not change the recommendation.
3. Whether the new method fits the layer's halo and dirty-path invariants and its budget. Settle by writing the ribbon raster as a function of (road cluster, texel) and running the existing layer tests (bit-identical `update` vs `rebuild_all`, no-change tests) plus a timing run on one 256 m tile; not done.
4. Real UK numbers: when a wall replaces a slope, maximum unsupported soil cut/fill height, benches, rock-cut height before a tunnel. Settle by fetching the current DMRB documents from standardsforhighways.co.uk (CD 622 and the earthworks / retaining wall standards it references; MCHW Series 600) by hand or by a tool that can read PDFs, or by asking the owner for the numbers; until then every number stays region data with a "judgement" label.
5. Where the gate numbers for wall rate of change and minimum wall height should sit. Settle by measuring photos 46, 04, 03 (end taper 35-40 deg per INDEX) in lane widths as the builder's first step.
6. What the road tool should do for infeasible hillsides (refuse, propose a viaduct, or let the player lower the line). This is a product decision for the owner; only the plan record `status = refused` is proposed here.
7. Whether gradelayer's `grading.cpp` is the same as main's r8 (I did not diff).
8. Whether the 2 s7 numbers in the waiver comments (117/1345, 95/4444, 8/4444) are still current; copied from AUDIT and STATUS, not reproduced by me.
