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
| Ours | Photo | Why |
|---|---|---|
| `seed1_match_2km.png` | `58_oxon_vertical_brill_hills_woodland_edge` | sinuous diagonal through-line, fields in rows, small district top-left, large district right |
| `seed2_match_2km.png` | `59_oxon_vertical_charlbury_cotswold_dip` | wood parcels with stepped edges, belts, corner plantations |
| `seed3_match_2km.png` | `57_oxon_vertical_wroxton_north_oxon_uplands` | mixed sizes, tan / dark / bright fields, tree lines |
| `seed3_close_1m.png` (1 km, 1 m per pixel) | detail of 57 / 58 | hedge weights, notches, tramlines |

Colours: dark green wood, mid green permanent pasture, bright green ley, olive rough, brown ploughed, tan stubble, mid-dark green cereal with light tramlines,
very dark green other crop; field boundaries without a hedge show as a faint line.

## Checklist (honest; all four images opened, next to the photos named above; 2 look iterations used)
| Question | Seed 1 | Seed 2 | Seed 3 |
|---|---|---|---|
| Only T-junctions? (measured, 8 km box: T / Y / X) | 3138 / 2 / 3 | 4235 / 0 / 0 | 3985 / 1 / 3 |
| Wedges gone? (min corner, max aspect, triangles) | 60.3 deg, 4.00, none | 62.4, 4.00, none | 64.5, 3.99, none |
| Through-lines sinuous? | yes, the diagonal lane and valley line cross the map | yes | yes |
| Districts of size? (area-weighted p10 / p50 / p90 ha) | 1.6 / 7.5 / 22 | 1.5 / 3.4 / 21 | 1.3 / 6.2 / 23 |
| Three boundary weights visible? | yes: thin hedge, thick tree-line stretches, faint fence lines | yes | yes |
| Woods on parcels with notches? | yes, stepped; one long scarp wood still reads as a ribbon (about 50-60 m) | yes, large stepped masses | yes |
| Per-field variety like the photo? | good: 7 uses, tint spread; tramline hatching reads as aerial | good | good |

**Would a person still say "Voronoi with random woods"?** The Voronoi part: no. No wedges, no star vertices, boundaries are curved lines with fields hanging
off them as in 58. "Random woods": partly. Woods now sit on parcels with notches, but the large masses are blocky (stepped on 20-60 m) where 48 and 47 show
ragged organic lobes, and the stream/scarp wood is still the most artificial element. Remaining gaps against the photos: no lanes, tracks, farmsteads or ponds (owner
decision pending), fields are almost all quads with gently curved sides (51, 59 have more 5-6 sided, bent fields), rounded corners absent, and the grain of
cropped fields is always along the long axis (56 also shows tramlines following a curved edge). My own score on the same scale as the round-3 review:
fields 7, boundaries 6, woodland 6, overall 6 to 6.5 (a self-score; a cold read is still needed).
