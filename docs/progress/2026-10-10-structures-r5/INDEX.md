# Structures round 5: slopes, one wall or two tiers, rock cut (10-11 Oct 2026)
gcc 16 build, `regen.sh [scenario ...]` (delete `build-*/export_graded.exe` first when sim_core changed: regen only relinks it when its own source is newer).
Rule: slope if it daylights; else one wall <= 3 m with slope; else at most two tiers (berm >= 4.5 m with a sloped strip); beyond that CUT = engineered rock cut (face 0.5:1, 3 m catch berm every 9 m, rock-trap ditch), FILL = one wall up to 6 m, beyond that `needs_structure` + steepest stable fill slope (1:1.5).
The rock face is flagged (`rock_cut` per sample, `surface_runs()`, texel class 2 in the tile footprint) but the viewer still paints it grass: berms and steep face are visible only as shading.

| Scenario | What it shows | Images |
|---|---|---|
| **s4b realistic side hill (gradient 0.4)** best picture of the result | cut side: walls <= 3 m under a slope; fill side: one 3 m wall + slope; wall ends taper into the bank | `s4b_side_hill_realistic_{street,aerial,alongA,alongB,end}.png` |
| s4 stress, 0.85 gradient (40 deg) | uphill rock cut with berms, downhill needs_structure (steep fill slope, no wall); no stacked walls | `s4_retaining_wall_run_{street,aerial,alongA,alongB,end}.png` |
| s5 cutting | toe ditch (0.4 m deep, 1.5 m wide) | `s5_cutting_{street,aerial,along,end,toe,toe_section}.png` (ditch only readable numerically: ground 0.27 m below formation at the toe) |
| s6 embankment | bank, run-out | `s6_embankment_{street,aerial,along,end,side_close,runout_close}.png` |
| s7 adjacent levels | one shared 5.5 m wall (over: 3.4 m gap) | `s7_adjacent_levels_*.png`; the east/west ends still show a pit and striped cut faces (NOT fixed) |
