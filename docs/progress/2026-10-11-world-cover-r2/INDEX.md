# World cover round 3 previews (11 Oct 2026)

Branch `agent/world-cover-r2`. Round 3 replaced the Voronoi field model by blocks with a grain, recursive cuts (T-junctions, through-lines) and
woodland assigned to parcels, boundaries, corners and steep ground. Structure study of the 14 fetched vertical aerials (46-59, private):
`REFERENCE_NOTES_R3.md`; sizes from the earlier 9 oblique photos: `REFERENCE_NOTES.md`. The round 2 (Voronoi) index is kept as `INDEX_r2_voronoi.md`.
Previews come from `tests/test_world.cpp` case "previews":

    CP_WORLD_PREVIEW_DIR=docs/progress/2026-10-11-world-cover-r2 build-gcc/tests/sim_core_tests -tc="test_world: previews*"
    node tests/tools/png_recompress.mjs docs/progress/2026-10-11-world-cover-r2

Seeds 1, 2, 3, uk region pack, per-seed varied by `make_test_world`.

| File | What |
|---|---|
| `seedN_match_2km.png` | **Same ground width as photos 46-59**: 2000 m square, north up, 2 m per pixel (photos are 1 m per pixel; view both at 1000 px). Dark green wood, light green pasture, tan arable, olive rough/wet; hedges at true width (3-8 m: 1-4 px); trees as dots |
| `seedN_cover.png`, `seedN_cover_close.png` | Whole-world cover (2 m per pixel) and a 640 x 960 m crop (1 m per pixel) |
| `seedN_hillshade_contours.png`, `seedN_slope.png`, `seedN_oblique_*.png` | Terrain (unchanged in round 3) and flat-coloured oblique views |

## Side by side (both opened)
| Our map | Photo | Why this pair |
|---|---|---|
| `seed1_match_2km.png` | `59_oxon_vertical_charlbury_cotswold_dip` | small irregular fields, two grains separated by a through-line, stream tree line |
| `seed2_match_2km.png` | `57_oxon_vertical_wroxton_north_oxon_uplands` | two grains, whole-field wood parcels, shelter belts, corner copses |
| `seed3_match_2km.png` | `51_oxon_vertical_hook_norton_ironstone_hills`, `58_oxon_vertical_brill_hills_woodland_edge` | irregular enclosure with small woods; parcels and belts |

## Checklist (honest; 2 look iterations used)
| Item | Photos | Round 3 output | Verdict |
|---|---|---|---|
| Junction type | T-junctions almost everywhere; three-way points rare | cuts end on an existing line (T); three-way points only where block edges meet (every 600-900 m) | matches |
| Shared through-lines | long lanes and hedges with fields hanging off both sides | block edges are through-lines, every cut spans a whole sub-block; seed 1 shows fields hanging off a diagonal line | matches, but through-lines are straight (photos: sinuous lanes) |
| Grain | one direction per block from a lane, stream or contour; neighbours differ 15-60 degrees | longest bounding line (70 %), terrain hint on slopes and near the valley, else regional; blocks differ visibly | matches |
| Field size spread | 1-4 ha ancient, 3-12 ha uplands, 30+ ha downs | over 8 x 8 km: mean 3.9-4.4 ha, p10 0.8, p90 about 10, max 30 | mean slightly small, spread right |
| Corners and shape | mostly rectangles and trapezia, a few curved sides | convex quads and pentagons; acute wedges where a cut meets a block edge at a shallow angle | **weaker**: more wedges than the photos; no curved sides or rounded corners |
| Wood on parcels | whole fields with field-straight edges | yes (seed 2 left, seed 3 centre); wooded districts by clustering | matches |
| Wood on boundaries | 20-40 m straight belts | yes, 12 % of boundaries, gated 150-400 m stretches | matches |
| Stream tree line | thin, sinuous, 10-30 m | yes, 8-32 m wide, in stretches; wide where it meets the scarp hanger | partly: no field boundaries follow the stream |
| Corner copses | 40-60 m | yes | matches |
| Isolated trees | rare, parkland only | 0.7 per ha in pasture, 0.14 in arable | slightly many |
| Wood share | 5-15 % | default source 5-11 % per seed, plus the terrain's scarp hanger | ok |
| Giveaways left | | straight through-lines; sliver wedges; all hedges one dark line; no lanes or farms; hanger wood is one wide band | |

Would a person still say "Voronoi with random woods"? No for the topology (T-junctions, grain blocks, parcel woods). They would still notice the lack
of roads and sinuous lanes, and wedge-shaped parcels.

Not verified: the oblique renders and terrain previews were not re-judged this round; no 3D view with meshes; the photos show hedge texture and tractor
lines that a plan-view class map cannot be compared with.
