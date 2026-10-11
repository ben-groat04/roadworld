# Reference notes (round 8). Opened with Read: street/04, 05, 06, 46, 47, aerial/31 (older) and street/52-57 (6 new, fetched this round). Private (docs/reference/), not copied here.

New photos: 52 canal-side concrete retaining wall (weak: vegetation), 53 stone-infill concrete wall under a netted slope (Taiwan), 54 Texas SH16 low coursed-stone fill-side wall, 55 and 56 Bhutan Chelela-Paro road (dry-stone wall on a steep side-long slope, best of the set), 57 dry-stone wall in a field. Sources in docs/reference/street/ATTRIBUTION.md. A clean close view of a highway retaining-wall END in a cutting was again NOT found (searched Commons "Retaining walls" and geograph tags); 55-57 and 46/04/05 are the best available.

## What walls on side-long ground do (46, 05, 06, 53, 55, 56)
- The wall top is a straight or gently curved RAKED LINE that follows the hill: 55 and 56 over about 30 m of wall show no step at all; 46 has one course step in about 20 m; 47 steps by whole courses with long flat treads. So: smooth line by default, steps only course-sized (a few hundred mm) and only if the line needs them. Built: raked top (no panel steps), panels only place the joints.
- Directly above the wall top the natural bank carries on: 05, 06, 46, 53, 55, 56 all show a strip of 0 to 1.5 m behind the coping and then the unregraded hillside rising at its own steepness. No photo shows a long ramp or a second slope cut back into the hill above a wall. Built: slope efficiency rule (below).
- Wall height is 1-2 courses of a 2 m gabion in 05 / 06 / 46 (about 2-3 m), 3-4 m in 55 / 56. Nobody retains 5+ m with one wall plus a slope: 46 stacks two courses, 47 shows two tiers with a flat berm.
## Wall ends (04, 46, 47, 55, 57)
- 46: left end butts vertically against rubble masonry, right end runs into the bank getting lower (the course tops follow the bank), the bank wraps round it. 04: the wall end is a vertical concrete face against the netted rock. 55: right end vertical into the rock bank; left end dies by the top raking down into the ground. 57: wall height dies away to nothing, buried.
- None shows a staircase of blocks, a gap of bare bank between two runs of one wall, or an end standing proud of the bank. Built: gap bridging (a gap up to wall_close_gap_m between two runs of the same side / tier is a minimum-height wall), raked top, blunt end (wall_end_taper_m 0), the ground does the run-out.
## Defaults chosen from this (region data, grading.slope_* and structures.wall_*)
- slope_max_ratio 3.0: a slope's run to daylight may be at most 3x the flat-ground run for the height it starts above / below the ground, plus slope_run_slack_m 2. On a 0.4 hillside a 1:2 slope has ratio 5 and is refused (chasing); a 1:2 slope on 0.3 ground is ratio 2.5 and allowed.
- slope_max_run_m 15: a slope beyond a wall back face never runs more than 15 m. wall_slope_extra_m (15) is now only a box / reach margin.
- wall_raked_top true, wall_top_smooth_m 3 (half window, never below the ground it holds), wall_close_gap_m 30, wall_end_taper_m 0.
## Not covered by any photo
A highway retaining wall end in a soil cutting seen close, and the run-out where the earthwork meets the end of another road at a different level. The s7 end ramp (4:1, end_taper_batter) is the existing end batter reused.
## s6 embankment toe, judged against 42, 43, 44, 45
s6_embankment_toe_profile / _runout_close (new cameras, low raking sun): the toe melts into the field in a concave curve over roughly a third of the bank length, the crest is rounded, no ditch, no step: it matches 42, 44 and 45 (toe run-out 15-30 %). Residual: diagonal moire hatching on the bank in the render is shadow-map aliasing, not geometry.
