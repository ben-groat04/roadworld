# Cold-read review, round 3 (10 Oct 2026)

Compared r3 against r1 and r2 images and (by INDEX text only, the images were not opened at the time; street/01 and street/03 were checked against the images on 11 Oct 2026 and the figures below hold: wall about 3.5-4 m at the tall end with stone coping and a curved return; wall end stepping up at roughly 40-45 degrees) reference street/01 (wall 2.5-3.5 m, coping, end) and street/03
(wall end stepping diagonally into the bank at 35-40 deg). Geometry and engineering plausibility only.

## Direct answers
- **3 m cap + benches: terraces or noise?** Neither yet, because they barely appear. No wall run in any scenario has `tier > 0`; s4 is still
  one 9-10 m wall (`s4_retaining_wall_run_street.png`, left half; `_alongA/B.png`), flagged `over`. Where the cap did bite (s7 lower road,
  2.5 m) the result reads as a believable low wall with a graded bank behind (`s7_adjacent_levels_between.png`, left wall). Benching was
  never exercised, so no judgement of terraces is possible. The `over` fallback is the problem.
- **Wall ends taper cleanly?** No. s4 east end (`s4_..._aerial.png`, right-centre): stair-stepped diagonal sliver of wall beside the road end,
  then a vertical cut face with striping beyond; essentially identical to r2. s7 east end (`s7_adjacent_levels_end.png`): the lower wall tapers
  as a coarse sawtooth that sits proud of the slope (lower right, looks floating); the middle wall stops with a vertical end followed by a
  ragged pit/gouge in the bank (centre right) and trench-like steps further right. The diagonal is in the spirit of street/03 but too coarse.
- **s7 shared structure: one wall or two?** Between the roads it is ONE thin wall (brick one side, stepped concrete the other;
  `s7_..._between.png` centre strip): coherent. But the upper road still gets its own separate 5.5 m `over` wall on its uphill side, so the
  section reads as two or three parallel retaining lines (`s7_..._aerial.png`), not a terraced slope.
- **Cutting ditch visible and sensible?** Not visible. `s5_cutting_street.png`: smooth batters run straight into a flat verge, no ditch
  profile at the toe. `s5_cutting_aerial.png` shows nothing (camera over flat ground). Cannot confirm the ditch exists; scenario camera
  fault plus possibly grading.

## Per scenario (r1 -> r3)
- **s1 bridge over road 6 -> 6.** Same as r2; piers, deck, broad approaches legible. New: a small notch/channel left of the abutment with a
  thin white sliver (`s1_..._street.png`, left of the pier, about x 560-620): grading end run-out meeting the structure. Minor.
- **s2 short gap 4 -> 5.** Block reads better (parapet-topped concrete spanning the ditch), ditch banks run out smoothly (street left).
  Still two separate parapet strips with a gap; near one has a tapered underside. structure_mesh.
- **s3 tunnel portal 6 -> 6.** Arched portal, wing walls, cutting notch fine (street). Aerial: right wing wall is a thin diagonal slab that
  looks pasted on; no exit portal on the far side (test scenario). Unchanged.
- **s4 retaining wall 3 -> 3.** 9-10 m wall, unchanged from r2 (`over=1`: the cap cannot be met and it falls back to the old behaviour).
  Panels, joints and coping read well and step cleanly in the along-views; the scale is absurd against street/01. Grading-rule fault
  (fallback); the 0.85 test slope is extreme but must still produce benches.
- **s5 cutting 6 -> 5.** Clean but featureless; no ditch, no berm; aerial empty. Marked down for the promised ditch not showing.
- **s6 embankment 2 -> 2.** Street view is an empty field (camera in the field, test scenario). Aerial: the bank is barely distinguishable
  from flat ground and the road simply stops at the left end (`s6_..._aerial.png`); run-out cannot be judged.
- **s7 adjacent levels 3 -> 4.** Better: lower walls 2.5 m, one shared wall, 5.5 m max. Still broken: upper wall over-height, ragged ends,
  gouge at east end.
- **s8 interchange 5 -> 5.** Roundabout, slips, overbridge on a small embankment readable (`s8_..._aerial.png`); ragged wedge edges at both
  slip merges remain (lower left; right of overbridge).

## Newly broken (none severe)
1. s7 east end gouge/pit and trench steps: `s7_adjacent_levels_end.png`, right half. Grading end run-out vs tier layout.
2. s7 lower-wall sawtooth taper floating over the slope: same image, lower right. Wall-mesh vs heightfield mismatch.
3. s1 notch beside the abutment: `s1_bridge_over_road_street.png`. Grading/structure junction.

## Overall: 4.5/10 (r1 4.5): no net change. Per-scenario mean: r1 4.4, r3 4.5.

## Top-6 fixes
1. Over-height fallback: when a wall exceeds 3 m, build the bench tiers anyway (or lay the slope back) instead of one 9-10 m wall. (grading)
2. Wall ends: taper in fine steps following the slope line, finish with a return/end pier; no vertical cut face at the end and no pit beyond. (grading, structure_mesh)
3. Make the wall mesh sample the final heightfield for bottom and top so sawtooth ends sit on the ground. (structure_mesh)
4. s7: one tiered section; no separate upper-road wall once tiers exist. (grading)
5. Cutting ditch: make it visible (1 m+ wide, 0.3-0.5 m deep) and re-aim the s5/s6 cameras (low oblique at the toe; embankment from the side). (test scenario, grading)
6. Terrain step 0.5 m on slope faces to remove stair artefacts in s4/s7 aerials; forward `tier`/`over` for display. (export tool, viewer)

## Recommendation: KEEP the new grading and iterate.
Reverting would not help: s4 is identical to r2 because the cap falls back, s7 is better with the new rules, the rest is unchanged or
marginally better. The failing part is the over-height fallback and the wall ends, not the cap itself.

## Best / worst
- Biggest improvement: `s7_adjacent_levels_between.png` (vs `../2026-10-10-structures-r2/s7_adjacent_levels_aerial.png`).
- Worst new breakage: `s7_adjacent_levels_end.png`. Worst overall: `s4_retaining_wall_run_street.png`.
