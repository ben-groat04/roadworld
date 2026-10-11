# World cover round 2 previews (11 Oct 2026)

Branch `agent/world-cover-r2`. Reference measurements and the photos opened: `REFERENCE_NOTES.md` (9 aerials). Previews are made by
`tests/test_world.cpp` case "previews" and recompressed (same commands as `docs/progress/2026-10-11-test-world/INDEX.md`):

    CP_WORLD_PREVIEW_DIR=docs/progress/2026-10-11-world-cover-r2 build-gcc/tests/sim_core_tests -tc="test_world: previews*"
    node tests/tools/png_recompress.mjs docs/progress/2026-10-11-world-cover-r2

Seeds 1, 2, 3; 2048 m square, 2 m per pixel, north up; uk region pack, per-seed varied by `make_test_world`. Compare with the
round 1 set in `docs/progress/2026-10-11-test-world/` (rectilinear grid, rectangular woods).

| File | What | Compare with aerial |
|---|---|---|
| `seedN_cover.png` | Land class over hillshade (dark green wood, light green pasture, tan arable, olive rough/wet), hedges as 1 px dark lines (drawn thin: real widths 3-8 m), tree dots | 21 (field sizes and shapes, hedge share), 14 (field mix, strips and wedges), 19 (woods as ribbons and copses) |
| `seedN_cover_close.png` | 640 x 960 m portrait crop at 1 m per pixel beside the scarp, hedges at their true width, trees as discs | **21_sixhills_lane_hedged_fields**: also portrait; its mid-ground fields (250-350 x 100-200 m) are 250-350 px wide here. Not the same perspective: the photo is oblique, this is plan view |
| `seedN_hillshade_contours.png`, `seedN_slope.png` | Terrain: contours 5 m, slope classes | 19 and 22 (rolling ground, valley sides) |
| `seedN_oblique_*.png` | Ray-marched flat-coloured ground (no tree meshes): a terrain/cover-class check only | 13 (valley and wood against fields at distance) |

## Checklist after 2 look-and-fix passes (honest)
- Fields irregular, not a grid: **yes**. Cells are 4-6 sided with sizes 1-16 ha (mean 3.6-6.5 ha by seed, p10 1.0-1.9, p90 7-11), median aspect
  about 1.7, orientation shared inside 450 m blocks and different between blocks; only 16-21 % of hedge length lies within 8 degrees of north/east
  (a random orientation gives about 18 %; the old grid gave ~100 %).
- Chessboard gone: **yes** by the numbers and by eye. Still visible: cells are convex Voronoi polygons (no L-shapes, no long thin strips
  beside lanes, few triangles against roads); in 21 and 13 the fields beside a road/valley are longer and straighter than these.
- Woods off the grid: **yes**: 10.5-11.4 % wood, patches fill 0.58 of their bounding box (a rectangle would be 1.0). Ribbons, belts (20-40 m,
  straight, short: read as bars), copses and one or two large lobed woods. Weak: valley-side ribbons are still near-uniform strips in places
  (seed 1 diagonal), and the scarp hanger is a very wide dark band.
- Hedges varied: **partly**: widths 3-8 m, gaps, 30 % of boundaries with trees at 32 m, 70 % of boundaries hedged. At overview scale the hedge
  share looks right; boundaries without a hedge are simply an edge between two ground colours (no fence geometry exists yet).
- Terrain: valley direction now varies by seed (sin of the angle -0.61..0.62 over 6 seeds); the scarp foot wanders about 13 m (weak spurs, visible in
  the seed 2 contours); plateau has 1-3 m rolling detail, but the 5 m contours show it as blobs only.
- Not done: boundaries are not aligned to contours except through block orientation at block centres (450 m resolution); no streams.
