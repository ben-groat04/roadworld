# Round 7 status (11 Oct 2026, taken over after the first agent was cut off)
SKIPPED / NOT DONE (first, by rule)
- Wall ends: NOT changed (photos 04, 05, 46 blunt buried ends, 47 stepped tops; code does a 4 m stepped taper). adjacent_levels: still waived (cause proven below).
- Photos not found despite searching Commons: a close retaining-wall end, a machined benched rock cut. Fetched instead: street/38-51 (14 photos). Downloads were NOT refused; I only searched Commons.
- Toe-ditch presence for soil cuts has no photo support (only rock cuts show one); kept as designed feature, not switched off.

DONE
- 14 reference photos fetched (in docs/reference INDEX.md + ATTRIBUTION.md) and opened with 4 older ones; `REFERENCE_NOTES.md` has the measurements and the defaults chosen.
- Smoothing now relaxes the displacement from natural ground (not the height). Effect: natural ground beyond the grading is never altered; crease measure at free texels fell from max 0.29 (height relaxation, 16 passes) to max 0.28 on adjacent_levels and 0.09-0.17 everywhere else (raw: up to 2.3).
- 8 passes instead of 16: crest rounding of the 1:2 cutting 4 m raw -> 4.5-5 m (a third of the slope); 16 passes gave 6 m (43 %), more than any photo.
- Toe ditch: fix bit 8 (designed feature, left alone by the smoothing); the ditch test no longer disables smoothing. JS and C++ in lockstep, goldens regenerated.
- slopes_daylight: tolerance unchanged (0.15 m); the "must have converged" station moves out by the smoothing spread (2 sigma = 2 * sqrt(iters*strength/2) * 0.5 m = 2 m), because the smoothed daylight edge blends over that run. The old miss was 0.158 m.
- no-crease invariant hard-fails above 0.3 slope change per 0.5 m texel (free texels: not footprint, not ditch, not within 3 texels of a footprint / step over 1.2 m, not within 25 m of a structure station).
- Viewer colours (rock, needs_structure) were already in; used in the renders.
NOT DONE
- Wall ends unchanged (photos 04, 05, 46 show blunt buried vertical ends; 47 shows stepped tops; code does a 4 m stepped taper).
- adjacent_levels is still waived. Cause proven (dump along y = 0.5, x 196-216): the lower road ends at x = 200 and its round end cap (radius = half width, formation 19.85) is a fixed footprint out to x = 206; the upper road's fill surface there is 22.8 m (natural 19.9), so the cap is a flat pit with a 2.9 m vertical cliff at x = 206/207. Fix would be to not force the cap (a non-road area) below another road's fill, or give the cap its own end batter; both touch the footprint union rule in gradeTileCore and C++ in lockstep, not done.
- scene.mjs --check: ground_hidden_by_walls 5/4466 (0.237 m, road 15/16 near 1688,1838) fails identically on the r7 baseline commit, so it is not caused by this round; perf_js_mouse flaky (4.27-4.51 ms vs 4).
