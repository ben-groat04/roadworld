# Structures round 7 (11 Oct 2026): smoothing of the displacement, toe ditch fixed, photos fetched
All scenarios s1-s8 rendered with the r5/r6 cameras (`regen.sh`, gcc Release build, smoothed ground), plus the two profile views `s5_cutting_crest_profile.png` and `s6_embankment_toe_profile.png`. Rock-cut texels are grey-brown, needs_structure stretches orange (s4 aerial; the ragged colour edge is the 0.5 m cell colouring, not geometry).
Smoothing: constrained relaxation of the DISPLACEMENT from natural ground (8 passes, strength 0.8, margin 8 m, max move 1 m), so natural ground is never altered and the daylight edge blends into it. Footprint, wall faces (steps over 1.2 m), rock faces, structure surroundings and the toe ditch stay fixed.
Photos (docs/reference, private, not copied): old street/04, 05, 10, 11, 12, 13, 14 and new street/38-49. Measurements in `REFERENCE_NOTES.md`.

| Render | Compare with | Checklist (honest) |
|---|---|---|
| s5_cutting_aerial / along / end / street | street/38, 39, 11 | cut face and crest read as one soft landform: yes. Verge and toe: concave, no ditch visible from here (ditch 0.4 m deep is tiny at this distance). |
| s5_cutting_crest_profile | street/38, 39 | crest rounded, no crease visible; rounding about a third of the slope, a little more than photo 38 (10-15 %) for steep cuts. |
| s5_cutting_toe, s5_cutting_toe_section | street/38, 10 | toe curve and verge plausible; the ditch is small. |
| s6_embankment_* , s6_embankment_toe_profile | street/13, 14, 42, 43 | crest and toe both rounded (about a third of the bank); toe run-out in the field shorter than photos 42/44/45 (15-30 %) at the profile camera. No pit. |
| s4b_side_hill_realistic_* | street/05, 46, 47 | wall steps down at its end in course-sized steps like 47; the wall end is a stepped diagonal and not a blunt buried end like 04/05/46 (not changed this round). Bank above/below smooth. |
| s4_retaining_wall_run_* | street/04, 47, aerial/29, 31 | bank smooth; grey rock-cut region has a ragged edge (colouring only). Orange needs_structure stretch is by design. |
| s7_adjacent_levels_* | none (no photo of two roads at different levels) | between the roads: clean terraces, no pit visible in `_between`. East end (`_end`, `_alongA/B`): FAULT remains: a ground cliff (about 3 m) where the lower road's end cap meets the upper road's fill, and striped vertical faces. |
| s1-s3, s8 | street/15-21, aerial/22-24 | unchanged by this round except for smoother surrounding ground; no new faults seen. |
