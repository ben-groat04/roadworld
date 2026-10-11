# Cover review R3 (cold read)

## 1. Files opened
Generated: seed1_match_2km, seed2_match_2km, seed3_match_2km, INDEX.md (read after forming a first view; the cover/close PNGs were not opened, see section 6).
Photos (12, all in docs/reference/aerial/): 46 stanford_vale, 47 uffington, 48 wychwood, 49 otmoor, 50 watlington, 51 hook_norton, 52 dorchester, 55 faringdon, 56 blewbury, 57 wroxton, 58 brill, 59 charlbury. Skipped 53 (town), 54.

## 2. Scores
- Field pattern: 6/10
- Boundaries/hedges: 4/10
- Woodland: 5/10
- Overall "reads as the photos at the same scale": 5/10
- Would the owner still say "Voronoi with random woods"? **Partly yes** (about 60 percent likely). The topology has improved (T-junctions, grain bands), but seed 2 in particular still shows radial, convex, Voronoi-like polygons, and the woods still read as pasted shapes.

## 3. Five-second tells, ranked
1. **Wedges, kites and triangles, and star-shaped three-way vertices.** Seed 2: the olive triangle at centre-top (x 340-610, y 90-330), the dark wood triangle at (130-330, 540-640), and the olive triangle at bottom-right (700-960, 810-990). Seed 3: the olive triangle at right (590-840, 250-490). Seed 2 also has many corners where three lines meet at about 120 degrees (e.g. 440,320 and 130,565). Photos: 51 (pasture patchwork), 58 and 55 are almost all quads or pentagons with 80-100 degree corners, and a triangle only appears as one rare filled corner (51 near x 1100, y 1500). Photos 46 and 56 have a few trapezia, never a field narrowing to a point.
2. **Woods have no relationship to the land, only to the polygons.** Seed 2: wood is a perfect triangle at left and a rectangle at bottom (x 430-690, y 790-930) beside a free-form wide band. Seed 3: a smooth, uniformly wide (about 80 m) river-shaped ribbon (x 0-430, y 200-690), plus a perfect upright rectangle at (405-525, 140-420). Real woods (59, 47, 48) are blobs with notched, stepped edges following field corners (59 at x 700-900, y 640-960), with small rectangular plantations tucked into field corners (47 at 720-880, y 180-420). Wood texture in the generated images is salt-and-pepper noise; fine for a flat render, but the wide uniform bands read as rivers of forest.
3. **Every boundary is a hairline of identical weight and colour, dead straight.** All three seeds. Photos show hedges 6-15 m wide with shadow, irregular, with trees at intervals (51, 55, 58), plus stone-wall/fence lines that are thin (52, 57). The generated map has one thin class, no gaps, no hedgerow trees along lines. In 58 and 55 the hedge pattern is more visible than the fields.
4. **No lanes, tracks, farms, ponds or streams-as-features.** Entire map is one landcover layer with a single pale tan diagonal (the stream) per seed. Every photo has a road or lane (46, 51, 52, 55, 56, 57, 58, 59), usually curved, and fields are arranged around it; a farmstead appears in about 10 of 12 photos (47, 57, 58, 59). Damage: lanes and farmsteads, high (the most obvious missing structure); streams/ponds, medium (49, 52 and 56 show them as major features, but the player will see only sparse ones in the upland photos); villages, low for the first test, as the player builds those.
5. **Field sizes feel uniformly medium, no clear small-ancient vs large-open contrast within a seed.** Seed 1 has the biggest spread (a very large field at bottom-left, 400 m wide, beside 60 m parcels); seeds 2 and 3 are mostly 150-300 m fields with similar area. Photos have strong contrasts: 58 and 51 at 1-4 ha everywhere, 56 and 46 at 30+ ha with straight rows.
6. **Straight long edges ending in a block edge with no kink.** Seed 1 long through-lines from (0, 440) to (460, 640), and the diagonal at upper left. Photos have gentle S-curves on lanes (58, 56, 55) and kinks about every 100-200 m on field edges (51).
7. **Tree dots.** Isolated dots look like pixel dirt rather than parkland trees; real field trees appear mainly in pasture and parkland (59, 57), as large crowns about 15-25 m wide, not 2-4 px dots.

## 4. What is now right and must be kept
- T-junctions dominate in seed 1 (upper-left cluster), as in 51 and 58.
- Grain bands: seed 1 shows two clear field directions meeting at a through-line (like 58 and 46, and 59 upper-right).
- Whole-field woodland with straight edges (seed 3 centre rectangle, seed 2 left) matches 59 and 47.
- Narrow tree lines along the stream, in stretches (all seeds), matches the streamside lines in 52 and 57.
- Whole-field pasture/arable/rough mix as colour blocks looks plausible and matches the mosaic in 55 and 51.
- Seeds differ visibly from each other (seed 1 busy, seed 2 big and angular, seed 3 stripy); keep that variety.

## 5. Top 6 changes (ranked)
1. **Kill wedges.** After subdivision, reject or merge any field with an interior angle below 55 degrees or aspect (long side over short side) above 4:1, and any triangle outright. Photos: nearly all fields are quads or pentagons with 75-105 degree corners.
2. **Replace block-edge three-way vertices.** Only allow T junctions (a cut ending on a line at 70-110 degrees). Where block edges meet at three-way points, snap one edge so it terminates on the other (51, 58).
3. **Add sinuous lanes and farms.** One lane per 600-900 m block edge (8-10 m wide including verges), with S-curves of 30-60 m offset per 300 m, and about 1 farmstead (60 x 80 m cluster, with a track) per 2-3 km^2. Photos 58, 55, 57, 51.
4. **Hedge vocabulary.** Three classes: hedge 6-10 m (about 70 percent), grass-bank or fence/wall 2-3 m (about 20 percent), and tree-line hedge 12-20 m with crown dots every 25-40 m (about 10 percent); gaps of 10-30 m in about 5 percent of lengths (51, 55, 58, 52).
5. **Wood shapes from fields, not ribbons.** Cap generated ribbon width at 40-60 m; make wood edges follow field edges with 20-60 m notches; add small plantations 60-120 m on a side tucked in field corners (47, 59). Break the near-perfect rectangle (seed 3, x 405-525) by one step of 30-50 m.
6. **Widen the size spread and jitter edges.** Per seed: at least one district with 1-2 ha fields (100 x 150 m) and one with 20-40 ha (500 x 600 m), as in 51 versus 56. Add a 5-15 m wobble every 100-150 m on long boundaries; make 1 in 6 boundary corners rounded with radius 20-40 m (56, 55).

## 6. Confidence and limits
Confidence medium-high on pattern judgements (three match images against 12 photos at the same scale); medium on measurements (read off photo pixel positions by eye, so plus or minus 20 percent). Not judged: colour, texture, 3D look, terrain consistency (seedN_cover, cover_close, hillshade and oblique images not opened), whether hedges in the engine are drawn thinner than the 1-4 px stated in the INDEX, and the real 1 m per pixel versus 2 m per pixel loss of detail (match images viewed at 1000 px, photos at roughly 1930 px). Photo 54 was not opened, so only 12 were; photo 53 skipped as instructed. The photos were viewed through a rescaled display, so the pixel coordinates I quote for photos are approximate on a 1932-pixel view (1 px about 1.03 m).
