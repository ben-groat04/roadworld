# Structures round 7 (11 Oct 2026): smoothing pass
All scenarios s1-s8 rendered with the r5/r6 cameras (`regen.sh`, gcc build, smoothed ground), plus two new close profile views: `s5_cutting_crest_profile.png` (crest of a cutting) and `s6_embankment_toe_profile.png` (toe of an embankment).
Smoothing: constrained relaxation of the graded ground (16 passes, strength 0.8, margin 8 m, max move 1 m) on free texels; footprint, wall faces (steps over 1.2 m), rock faces and structure surroundings stay fixed. Rock-cut texels are grey-brown, needs_structure stretches orange (see s4 aerial).
Pair each render with a photo in docs/reference (private, not copied here):
| Render | Compare with |
|---|---|
| s5_cutting_*, s5_cutting_crest_profile | street/11 (chalk cutting), street/12 |
| s6_embankment_*, s6_embankment_toe_profile | street/13, street/14 |
| s4b_side_hill_realistic_* | street/05 (gabion wall under a bank), street/04 |
| s4_retaining_wall_run_* | street/04, aerial/29 |
| s7_adjacent_levels_* | none covers two roads at different levels |
| s1-s3, s8 | street/15-21 (bridges, tunnels), aerial interchanges |
Verdict: crests, toes and slope transitions are now rounded (second difference of ground height per 0.5 m texel: raw up to 2.3, smoothed at most 0.29). The angular look remains where walls end (staircase tops, vertical faces, the s7 east/west ends: pit and striped faces still visible); those were not changed.
