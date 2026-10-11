# Round 7 status
DONE: smoothing pass (JS + C++, goldens), no-crease invariant (bound 0.35 slope change per texel; measured smoothed max 0.29, raw 2.3), tile-independence test, cost 122 ms vs 60 ms raw per 256x256 tile (gcc Release), viewer colours (rock, needs_structure), r7 renders s1-s8 + 2 profile views, ditch/daylight test adjustments (documented in test_grading.cpp).
NOT DONE: wall ends (blunt vertical ends in structure_mesh.cpp), adjacent_levels still waived (bare steps are at the ends of the lower road's tiered walls), reference photo download, longitudinal/run-out specific smoothing.
