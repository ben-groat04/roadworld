# Reference notes for world cover round 2 (11 Oct 2026)

Aerial photos opened with the Read tool this pass (9, all in `docs/reference/aerial/`, private, not copied):
`21_sixhills_lane_hedged_fields`, `19_a23_handcross_lane_green_countryside`, `11_a68_grade_separated_roundabout_fields`,
`10_a16_a158_partney_roundabout_sun`, `13_a15_m180_barnetby_wide_dusk`, `22_cross_tay_link_road_river_bridge`,
`20_beverley_westwood_road_trees`, `23_m48_severn_bridge_wide_haze`, `14_laceby_crossroads_village`.
INDEX text was not used as evidence. 23 is hazy and distant: used only for the "dark lines are hedges/tree lines, fields
100-300 m" impression. Scale in every case is by eye from the road: a UK lane about 3.5 m, an A-road about 7.3 m, a dual
carriageway about 28-30 m. The photos are oblique, so scale changes with depth; numbers below are for the middle ground and
carry about +-30 %. Nothing here is a measurement in the survey sense.

## Field size, shape, orientation
- 21 (road 5.5 m wide is about 14-20 px in the middle ground): foreground arable about 200-250 m across; mid fields about
  250-350 x 100-200 m; far fields 400-600 m long and 80-150 m wide (aspect 3-5); paddocks next to the farm 60-100 m.
  So roughly 3-8 ha typical, with a few over 15 ha and several under 1.5 ha.
- 19 (dual carriageway about 28 m is about 35-40 px): bright pastures 100-300 m, the central one about 300 x 100 m (aspect 3);
  many parcels 70-140 m; all 4-6 sided and bent, most edges follow a stream, a lane or a wood edge. Fields share
  an orientation within a patch of 1-2 km and change between patches.
- 11 (dual A68 about 14 m): centre fields about 350 x 200 m (7 ha) with 100-200 m triangles and wedges where a road cuts across;
  three families of edge direction (along the road, across it, and about 45 degrees off).
- 10: big fields 300-500 m, wedge and triangle shapes beside the diagonal A16; edges at 2-3 distinct angles.
- 13: wedge and parallelogram fields 300-800 m long, 100-250 m wide, long axis parallel to the motorway, the cross edges at
  60-90 degrees to it; one very large dark wood in the corner (several hundred m across).
- 14: foreground arable about 500 x 300 m (15 ha); east of the road tall strips about 600 x 150 m (aspect 4); small
  pastures 100-200 m with trees; two edge families about 20 degrees apart plus cross edges.
- 22: one large pasture about 400 x 150 m with straight sides parallel to the railway and river (river-bank tree line
  10-20 m wide).
- Chosen defaults: mean field area 5 ha; block size multiplier 0.55-1.6 on linear size (so cells span roughly 1.5 to 14 ha
  means, wider after Voronoi); aspect 1.0 to 2.6 per block (mean about 1.8); orientation shared inside a 450 m block,
  jittered +-27 degrees between blocks around a per-seed regional angle (any direction); 12 % of blocks are small-holding
  blocks of 0.4x linear size (about 0.8 ha cells). Edges are straight to gently curved with a 3-5 m wobble.

## Hedges, fences, trees on boundaries
- 21, 10, 14: most field edges carry a continuous dark hedge line; some edges are visibly just a pale line (fence or track)
  or nothing (arable running into pasture). By eye 65-75 % hedge, 15-25 % fence-only, about 10 % open. Default 70 % hedge.
- Width: field hedges look 2-4 m, hedges beside roads 5-8 m (hedge plus bank plus trees: 10 and 21). Default per-boundary
  width 3-8 m weighted to the thin end (mean about 4.4 m).
- Gaps: gateways about one per 200 m, 4-6 m wide (21, 10); occasional decayed stretches of 15-50 m (14). Hedges do not
  always reach a junction of boundaries: ends stop short.
- Hedgerow trees: 21 and 14 show single oaks at 25-45 m on roughly 30 % of boundaries (clumps at corners 21); none on the
  rest. Default share 0.3, spacing 32 m.
- Roadside trees (21 right side, 20) are near continuous: road-edge layer, not touched here.

## Woodland
- Ribbons: 19 has a dark ribbon about 700 m long and 50-60 m wide down a valley and along a stream; 22 a bank tree line
  10-20 m wide; 13 strips of 30-40 m beside the motorway and along a stream.
- Belts: 11 and 10 show 40-60 m belts along field edges and around farms, lengths 150-400 m.
- Copses: 21 and 14 show 60-120 m rounded copses (about 1-3 ha), often with a pond or corner.
- Larger woods: 21 (about 250 x 100 m, 2.5 ha), 14 (about 200 x 80 m), 11 (400 x 80 m plantation, straight edge), 13 (the big
  dark block, 30 ha+), 19 (the Weald is 40-50 % wood: lobed, very ragged). Edges natural and lobed (5-20 m), plantation
  edges straighter. No wood in any photo is a rectangle aligned with the field pattern.
- Wood share: 21 about 5 %, 14 about 8 %, 11 and 13 about 10-15 %, 19 about 45 %. Default overall 10-14 % for the uk pack.

## Scattered trees, colours
- Parkland (20) has trees 15-25 m apart, crowns 8-12 m (not generated here). Field trees elsewhere: singles and 2-4 clumps,
  about 0.5-1 per ha in pasture, almost none in arable (21, 14).
- Mix: arable (tan stubble or brown soil) 35-50 %, pasture 35-45 %, rough/scrub 5-10 %. Classes cluster: a 1-2 km patch
  is mostly arable or mostly pasture (21 all tan, 19 mostly green). Default: arable share modulated by a low-frequency
  noise of wavelength 1500 m (+-0.3).
