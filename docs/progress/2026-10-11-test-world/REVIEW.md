# Test world cold-read review (11 Oct 2026)

## 1. Files opened
All 18 PNGs in this folder (seed1-3: hillshade_contours, slope, cover, oblique_overview, oblique_scarp, oblique_valley) plus INDEX.md (file list only).
Aerial references opened (9): `21_sixhills_lane_hedged_fields`, `19_a23_handcross_lane_green_countryside`, `11_a68_grade_separated_roundabout_fields`, `10_a16_a158_partney_roundabout_sun`, `13_a15_m180_barnetby_wide_dusk`, `22_cross_tay_link_road_river_bridge`, `20_beverley_westwood_road_trees`, `26_a9_gelly_overbridge_earthworks_scotland`, `23_m48_severn_bridge_wide_haze`.

## 2. Scores (/10)
- Terrain shape: **6**
- Field pattern: **2**
- Hedges: **3**
- Woodland: **3**
- Reads as the countryside in the photos: **3**

## 3. Faults and fixes
**F1. Fields are a rectilinear chessboard** (all seed*_cover.png; oblique_overview of every seed). Lines run strictly N-S and E-W, with only a slight wobble, and cells are near-equal in size. Target: 21 (fields of very different size, 4-6 sides, edges bent to follow the lane and the ground), 19 (small irregular pastures of 60-150 m beside big arable), 13 (long wedge fields set at oblique angles).
Fix: replace the grid with a jittered-Voronoi or recursive-split cell layout. Mean area about 4-6 ha, with sizes spread over a lognormal range of 1 to 25 ha. Aspect ratio 1:1 to 1:3. Rotate the whole pattern by a regional angle of 15-40 degrees off north, plus a per-patch rotation of +/-15 degrees. Snap field edges to contour or valley direction on slopes over 6 %. Measurement: the cover maps are 2 m/px, and I read cells at about 250-400 m x 110-260 m. That is larger than the "150x110 m" in the brief, so check the generator's real cell size. The pattern is still too regular either way.
**F2. Scale inferred from reference 21.** A 5.5 m lane measures about 20 px, which puts the large foreground field at about 220 m wide and the mid fields at about 220 x 80 m. Photo 11 has one field over 400 m across (the road is about 7 m wide, so rough scale only). Target distribution: 60 % of fields 80-250 m on the long side, 25 % 250-450 m, 15 % under 80 m (paddocks).

**F3. Hedges are a uniform thin dark line on every field edge** (cover maps), continuous with no gaps. In 21, 11 and 10 the hedges are thick, vary in width (3-8 m), and have gaps at gates. Hedgerow oaks stand at 20-40 m spacing on about 30 % of boundaries, and roadside trees are close to continuous along lanes (21, right side; 20). Some boundaries (13, 22) are fence only.
Fix: hedge on 70 % of edges, fence-only on 20 %, open on 10 %. Add a 4-8 m gate gap about every 120 m. Hedgerow trees every 25-45 m on 30 % of hedges, and clumps at field corners.

**F4. Woodland is rectangular blocks with ragged edges** (seed1 cover top and right, seed2 cover left, seed3 cover right). They sit on the field grid, so they read as dark tiles. The scarp hanger is the only believable one: a curved ribbon on the steep side (seed1_oblique_scarp, seed3_oblique_valley). Target: 19 (woods follow streams, steep ground and field edges; irregular lobes), 10 (narrow shelter belts 20-40 m wide), 21 (small copses of about 1 ha), 13 (large wood in the corner).
Fix: build woods from the Voronoi cell union plus noise-eroded lobes. Apply 4 shapes: valley-side ribbon, 20-40 m belts of 150-400 m, 1-3 ha copses, and one 15-40 ha wood per 4 km². Keep rectangles under 20 % of wood area. Wood area should be about 10-15 % overall; seed2 is nearer 15 % and the others less.

**F5. Terrain is smooth, but it is one valley plus a flat plateau.** The valley floor (flat strip about 250 m wide, pale in slope maps) is believable. The scarp is a single 30 %+ slab, 600-800 m long, with straight sides (seed1 slope, seed3 slope). Elsewhere the relief is almost flat (under 3 % on most of the plateau, with low blobby contours). In 19 and 22 the ground rolls everywhere. Contours at 5 m show wobbly blobs with no fall direction.
Fix: add a 20-60 m wavelength rolling component at 1-3 m amplitude, plus gentle regional tilt of 2-4 %. Break the scarp into 2-3 spurs with re-entrants. Add a side dry-valley or gully 100-200 m wide. Check whether the valley floor sits on the grid axis: it runs almost exactly N-S in all seeds (rotate by 10-30 degrees).

**F6. Valley floor.** A wet/rough strip follows the valley line (olive in the oblique views, seed2_oblique_valley). It has a smooth uniform width and reads as a road. Target: 22, where the river has a bank-side tree line 10-20 m wide. Fix: vary the width 30-120 m, add carr/alder trees on one bank, and add a stream line.

**F7. Seed variety is low.** All three have a N-S valley, a scarp on one side and the same field grid. Vary the valley angle (0-45 degrees), grade strength, and field size mean per seed.

**F8. Road-crossing risks.** Every grid line is a hedge, so a drawn road will cross a hedge every 100-250 m in one axis. A road along a grid axis will lie exactly on a field edge. The 2 m resolution contours are very low-lying, so earthworks will look tiny. Cover is a hard-edged tile, so a road cutting through shows square-edged tile joints.

## 4. Keep
- The valley-plus-scarp hanger structure and the curved wooded scarp ribbon.
- Smooth, artefact-free heights (no visible octave grid or stair-stepping).
- Pasture/arable/rough colour split, tree dots, the track following the valley.

## 5. Ranked top 5
1. Replace the rectilinear field grid with irregular rotated cells (F1, F2).
2. Rebuild woods as lobes, belts and copses, off-grid (F4).
3. Thicken and vary hedges, with gaps and hedgerow trees (F3).
4. Add rolling relief and rotate the valley (F5).
5. Vary seeds and the valley strip width (F6, F7).

## 6. Confidence
Medium. The grid problem is unambiguous. Field sizes in metres are inferred from lane widths in only 2-3 photos, with perspective error up to about 30 %. I could not judge colour, texture, trees as meshes or lighting (flat preview renders), and could not check the true cover-map scale or the generator code.
