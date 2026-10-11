# Status, world cover round 4 (in progress)

## SKIPPED / NOT DONE / NOT VERIFIED (kept current; read this first)
- Rounded field corners: NOT implemented as geometry (only the smooth S-curves of the row lines and the warp). Reason: a fillet on a hedge must move the land-use boundary too, otherwise the hedge cuts a wedge off the field.
- No additional aerials fetched so far; 11 of the 14 opened (46, 47, 48, 50, 51, 52, 54, 56, 57, 58, 59). The coordinator confirmed the fetch permission is the owner's; fetch only if a landscape character turns out to be missing.
- Terrain peak slope: not yet changed (see below when done).
- MSVC from-scratch build and both-compiler suite counts: not yet run.

## Done so far
- Voronoi block level replaced by rows + brick-bond cross lines + cut tree with quality gates (vegetation.cpp, default source).
- Per-field attributes (field_id, LandUse, tint, row_dir) on CoverGround; CoverHints::stream_offset; default_cover_structure_stats.
- Region data (uk, testland) updated; test_world wrapper updated; preview colours by land use with tramline hatching.
