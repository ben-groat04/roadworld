# World cover round 4 (11 Oct 2026)

**Not done / not verified is in `STATUS.md`, first section. Read that first.**

Branch `agent/world-cover-r2`. The Voronoi block level of round 3 is gone. New method, from the vertical aerials (private, in the main checkout's
`docs/reference/aerial/`; opened this round: 46 stanford_vale, 47 uffington, 48 wychwood, 50 watlington, 51 hook_norton, 52 dorchester, 54 marcham,
56 blewbury, 57 wroxton, 58 brill, 59 charlbury; 53 skipped as a town, 49 and 55 not reopened).

## What the photos show, and what was built from it
| Seen (file) | Built |
|---|---|
| Long sinuous lanes / tree lines cross the whole 2 km view and the fields hang off them in rows of parallelograms (58 diagonal lane; 46 NE-SW lane; 56 wavy road with fields N and S) | **Rows**: through-lines every ~700 m (0.7-1.3x), smooth S-curves between knots every ~1 km, in a frame set by the terrain (valley / contour direction at the world centre), plus a smooth warp (70 m over 1.4 km, 10 m over 130 m) so nothing stays straight |
| Every boundary meeting is a T; forks are rare (51, 58, 59) | **Brick bond**: cross lines run from one row line to the next and END ON them; neighbouring rows have different cross-line positions. Cuts inside a block end on its edges. Y and X cannot arise except by coincidence |
| Fields are quads/pentagons with 75-105 degree corners, aspect up to 3-4 (58, 51, 57) | A cut is accepted only if both pieces keep every corner >= 55 deg, aspect <= 4, >= 4 corners and 20 m between a cut end and any real corner; 10 candidate cuts per node (hashed, then snapped perpendicular to the two edges hit); else the piece stays whole |
| 1-3 ha ancient enclosure in 51/58/59; 20-40 ha open arable in 56/50/52, in districts | Target field area follows a smooth district noise (1500 m wavelength), 2.2 to 26 ha target, plus 12 % small-paddock blocks |
| Three boundary weights: dark hedges with shadow, thin fence lines (57, 47), tree lines with crowns every ~30 m (46 right edge, 58, 50) | hedge 6-10 m (70 %), fence/bank (20 %, emits nothing, still separates fields), tree line 12-20 m with crowns every 25-40 m (10 %); 10-30 m gateway gaps, 2 per km |
| Woods fill whole fields with stepped edges, belts 25-40 m, square plantations in corners (59, 47, 57, 58); ragged hanger on the scarp (47, 50) | Wood parcels (clustered), edge steps of 20-60 m over 40-110 m runs (on either side of the boundary), belts 20-40 m, plantations 60-120 m in a field corner; steep fields wooded with probability 0.5 (the rest rough grazing): no wide band; the stream is a field boundary (fields never straddle it) |
| Every field differs: stubble tan, ploughed brown, cereal with tramlines, dark other crop, bright ley, olive rough (47, 50, 56) | Per field: id, `LandUse` (7 + woodland), tint, row direction (long axis of the field, +-3.5 deg) in `CoverGround` |

## Pictures (2 km, north up, 2 m per pixel; photos are 1 m per pixel, view both at 1000 px)
| Ours | Photo (opened) | Why |
|---|---|---|
| seed1_match_2km.png | 58_oxon_vertical_brill_hills_woodland_edge | sinuous lane with fields in rows, small and large districts |
| seed2_match_2km.png | 51_oxon_vertical_hook_norton_ironstone_hills (59 for the woods) | small irregular enclosure, wood parcels |
| seed3_match_2km.png | 46_oxon_vertical_stanford_vale_enclosure (57 for sizes) | mixed sizes, crop colours |

Side-by-side composites contain the private photos, so they are kept out of the repo: `C:/Users/bgbg0/cp_scratch/pairs/` (`final_pair*`, and the earlier passes `p2_`, `p3_`, `p4_` for before/after at the same seed).

## Measured (uk pack, test-world hints, 8 km box per seed; seeds 1 / 2 / 3)
Fields 1851 / 2464 / 2266; count p10 0.8, p50 1.7, p90 5-8 ha; area-weighted p10 1.1-1.4, p50 3.0-6.1, p90 17.5-20.3 ha; max 30-38 ha. Min corner 60.3 / 60.7 / 64.3 deg,
max aspect 3.2, no triangles. Junctions T 3626 / 4917 / 4582, Y 1 / 1 / 1, X 8 / 0 / 1; T stem angle 60.3-90.6 (p5 76). Boundary weights by length: hedge 70 %, fence 20 %,
tree line 10 %; gateway gaps 4.3 %; hedge removed inside wood 7-10 %. Area share by use (seed 1): wood 5, permanent pasture 24, ley 13, ploughed 5, cereal 24, stubble 8, other crop 13, rough 7 %.

## Checklist (own judgement after 3 passes; the residual list is in STATUS.md)
| Question | Verdict |
|---|---|
| T-junctions only? | Yes, measured (Y + X under 0.3 % of T) |
| Wedges gone? | Yes (min corner 60 deg, aspect 3.2, no triangles) |
| Through-lines sinuous? | Yes, but one grain family across the map |
| Districts of size? | Yes (area-weighted 1-20 ha); count median 1.7 ha is a little small |
| Three boundary weights visible? | Yes in close crops; hedge shadow and varied width missing |
| Woods on parcels with notches? | Yes, but flat colour and stair-stepped; scarp wood still band-like |
| Per-field variety? | Yes; the photos cluster uses by district more |

**Still "Voronoi with random woods"?** Not Voronoi. The woods are the weakest part. Own score: fields 7, boundaries 6, woodland 5.5, overall 6 (self-score, no cold read yet).
