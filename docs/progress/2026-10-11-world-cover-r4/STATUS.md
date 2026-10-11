# Status, world cover round 4 (final for this session)

## SKIPPED / NOT DONE / NOT VERIFIED (read first)
- Rounded field corners: NOT implemented as geometry (only the S-curves of the row lines and the warp). A fillet must move the land-use boundary too, or the hedge cuts a wedge off the field.
- NO lanes, tracks, farmsteads, ponds, houses (owner decision); the photos have them in nearly every view. This is the largest remaining tell.
- No additional aerials fetched; 11 of the 14 opened (46, 47, 48, 50, 51, 52, 54, 56, 57, 58, 59). Not reopened: 49, 55; 53 (town) skipped.
- Look passes: 3 done (see below). No cold-read agent was run; every judgement here is my own.
- Terrain peak slope done (0.42 at seed 1 on the 4 m grid, 0.38-0.45 along the steep stretch; bound back to 0.50, steep-stretch bound 0.47); relief 52 m.
- Not verified: Unreal side (does not read the new CoverGround fields yet); performance of ground_at on very large worlds; thread-safety under heavy parallel load (mutex-guarded block cache only).
- Side-by-side images with the private photos are in `C:\Users\bgbg0\cp_scratch\pairs\` (not in the repo: they contain the photos).

## Own fault list, "shown both unlabelled, how would a reviewer tell which is ours?" (final pass)
Fixed over 3 passes: (1) palette far too saturated and fields jumping in hue -> muted, tints narrowed, per-pixel mottling; (2) hedges drawn as heavy cartoon outlines, then too faint -> 0.42 x width, darker green; (3) strips: too many long thin fields (aspect 3-4) -> max_aspect 3.2, target aspect 1-1.9; (4) wood: too many big dark blocks and wide scarp band -> woodland share 4.5 %, steep keep 0.35, notches 15-45 m and 30 % of boundary; (5) tramline hatching on everything -> only cropped fields, 27 m spacing.
Residual tells (not removed):
1. No lanes/farms/houses/ponds (see above). Any reviewer sees it at once.
2. Woods are flat dark green with stair-stepped (rectilinear) edges; the photos show textured canopy, soft lobed edges on large woods (48) and straight-edged conifer rectangles (59). Our big wood in seed 1/3 along the scarp still reads as a contour band 60-100 m wide.
3. Hedges are one even dark line; photos show shadow, gaps with trees, varying width, hedge trees as individual crowns. Tree dots in the preview are tiny squares.
4. Fields are almost all convex quads with gently curved sides; the pasture photos (51, 58, 59) have more 5-6 sided, bent fields and some round-cornered ones (46, 56).
5. Field sizes in a district are more uniform than the photos'; the real districts also share one colour family (all pasture or all crop), ours mix every use field by field.
6. Rows run in one family of direction across the whole 2 km (frame fixed by the valley), so far-apart blocks look parallel; the photos show two to three grains.
7. Fence/bank lines are only a faint line in the preview; no rendering of them exists yet.
8. Flat shading: no hedge shadows, no terrain-lit tone, no soil mottling beyond the noise added in the preview.

## Done
- Rows + brick-bond blocks + quality-gated cut tree (vegetation.cpp default source); per-field id / LandUse / tint / row direction on CoverGround; CoverHints::stream_offset; default_cover_structure_stats.
- uk and testland `vegetation.world_cover` data; test_world wrapper (stream ribbon, field-gated steep wood, lines minus wood); previews by land use.
- Suite: gcc 367/367 (clean build) and MSVC Release 367/367 (from-scratch build in C:\Users\bgbg0\cp_scratch\build-msvc-r4); the golden cover test (field ids, uses, tints, row directions, hedge length) is identical on both.
