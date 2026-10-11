# Round 3 structure study: true top-down aerials (Oxfordshire, 25 cm, 2 km squares)

Photos 46-59 in `docs/reference/aerial/` (private, not copied or committed). Source: Oxfordshire County Council public ArcGIS ImageServer
`https://mymaps2.oxfordshire.gov.uk/images/rest/services/Image/Aerial_Photos_Current_25cm_Img/ImageServer` (service metadata gives no licence text;
used as private reference only). Each is a 2000 x 2000 m square at 1 m per pixel: I know the scale because I requested the box in British National
Grid (EPSG:27700) coordinates, centred on the grid reference in the INDEX, and the service reports 0.25 m source pixels. All 14 were opened with the
Read tool. 53 (Bicester) and 55 (Faringdon) are mostly town and were used only for their edges.

## What each shows (structure, not statistics)
- **46 Stanford in the Vale (enclosed clay vale).** Right half: fields are rectangles and parallelograms 150-400 m long. Boundaries meet in T-junctions
  almost everywhere; a few long lines run through the whole field group (the NE-SW lane/hedge at the top right, the diagonal at the right edge) and the
  fields hang off them as strips that all share the through-line's direction. Far right: narrow strips about 40 x 250 m side by side (old strip enclosure).
  A thin sinuous stream tree line (top centre, bottom right) is 10-20 m wide and ignores the field pattern. Woods: a ragged copse belt along the stream.
- **47 Uffington (downland edge and scarp foot).** Arable strips and rectangles in the upper left share one grain (N-S). Woods are whole parcels: three
  rectangular conifer blocks (150-300 m) with edges exactly on field boundaries, 20-40 m shelter belts between fields (the vertical one at the centre,
  the L-shaped one top centre), and a wooded re-entrant coombe with a ragged edge along the contour (lower right). Scarp ground (lower left) is open
  grassland, unfenced: not every steep slope is wood.
- **48 Wychwood / Evenlode (woodland).** One huge wood (more than 150 ha) with a field-bounded edge on the west (straight lines, cut corners) and
  ragged, lobed edges along the valley sides to the north-east. Fields at the margin are bounded by the wood edge; a few cuts run into the wood as rides.
- **49 Otmoor (planned enclosure plus wetland).** Dead-straight boundaries in two perpendicular families, crossing at X and T junctions, long (500 m+)
  continuous lines; fields 100-250 m. A hedge line with regular trees every 20 m. Not typical of the rest; the regular type.
- **50 Watlington (Chiltern scarp edge).** Upper right: large arable fields with straight edges; bottom-left: huge dark arable fields with curved sides that
  follow the lane. The scarp (right edge) is a ragged hanger wood along the contour. Small parcels (50-100 m) of garden/orchard against the village.
- **51 Hook Norton ironstone hills.** Irregular ancient enclosure: 2-5 ha polygons, 4-6 sides, many boundaries bend (not straight), T-junctions
  dominate; a few three-way meetings where an old track forks. A field group at the top has a common NNE grain; the lower group a different grain
  set by the road. Woods: small field-shaped parcels (150 x 150 m, 1-2 ha) with straight edges, plus ribbon along the stream (lower left).
- **52 Dorchester (Thames terrace).** Large fields (300-700 m) with sides that follow the river bend and the road: one triangular 40 ha field bounded by
  road and river. Gravel pits and lakes. Tree lines follow the river and the old river channels (thin, sinuous, 10-30 m).
- **54 Marcham / Ock valley.** Mixed: big arable parcels at the top left with sinuous sides following a stream; small irregular paddocks bottom;
  many boundaries are streams or ditches with a thin tree line (15 m) that wriggles. Two small copses at field corners (centre left) and one
  woodland parcel (700 x 200 m) filling a whole narrow field.
- **55 Faringdon (vale pasture, town edge).** Top-left pasture group: 2-6 ha irregular polygons on a common NE grain, bounded by a through-line (the
  diagonal hedge). Woods as strips along the road and park woodland at the top right.
- **56 Blewbury downs (open arable).** Few, very large fields (300-800 m); boundaries are long smooth curves following the lane and the old scarp;
  corners rounded; the one tree line follows the curve of the ground (a dark curved belt about 60 m wide and 700 m long). Tractor lines show the
  grain of each field: they run parallel to the field's longest side or follow the curved edge.
- **57 Wroxton (north Oxfordshire uplands).** Fields 3-12 ha with straight to slightly curved edges; two grain families: NNE-SSW one block, NNW the
  other. A quarry. Woods are whole fields (a 400 x 250 m wood parcel with straight edges lower left), 30-40 m shelter belts along the lane and
  field edges (top left, a belt along the whole NW boundary of one field), and copses of 40-60 m in field corners (right centre).
- **58 Brill hills / Wotton (woodland edge).** Dense wood mosaic: wood fills whole fields (rectangular and L-shaped parcels, straight edges that follow
  field cuts), long straight shelter belts 25-40 m wide on field edges (top left diagonal), a stepped wood edge where the wood occupies the
  fields behind the boundary. Open parkland bottom left: single trees 80-120 m apart. Field colours alternate: crop, pasture, crop.
- **59 Charlbury (Cotswold dip slope, ancient enclosure).** Very irregular small fields 1-4 ha with mostly straight edges and many bends; two
  field groups with different grains (diagonal NE-SW at the top and WNW-ESE lower down) separated by a sinuous through-line (the lane with trees);
  boundaries meet in T-junctions; hedge/tree lines continuous but variable, with trees on the lane. Tree line along a stream (thin, sinuous).

## Structural findings (hypothesis of the owner confirmed, with caveats)
1. **Hierarchy confirmed.** First-order lines (roads/lanes, streams, the foot of a scarp) bound blocks of 30-150 ha. Inside a block, fields are made
   by subdividing: long cuts along and across a grain that comes from the block's bounding line or the slope. The result is a through-line with fields
   hanging off it (46, 57, 59, 55), not a Voronoi mosaic.
2. **Junctions are T-junctions** (a cut ends on an existing line). Three-way points at 120 degrees are rare (forks of old tracks). X junctions occur
   only in the planned enclosure (49).
3. **Grain**: one direction per block, set by the block's longest bounding line (lane, stream) or the contour; adjacent blocks differ by
   15-60 degrees. Field aspect 1:1 to 1:4, longer sides along the grain; ancient enclosure is less rectilinear (bends), planned is rectilinear.
4. **Boundaries**: straight to gently curved; sinuous only where they follow a stream or lane; corners are sharp except in big arable (rounded).
5. **Woodland sits as**: (a) whole parcels (field-bounded, straight edges; 47, 51, 57, 58); (b) shelter belts 20-40 m on one boundary
   (47, 57, 58); (c) thin sinuous tree lines on streams (46, 52, 54, 59); (d) ragged hangers along steep ground contours (47, 50, 48); (e)
   copses in field corners (57, 54); (f) one very large wood (48). Rectangles do not float in fields; ellipses of woodland do not occur.
6. **In-field trees**: rare; parkland only (58). Hedgerow trees at 20-40 m intervals on a minority of hedges (49).
7. **Land use**: adjacent fields differ (crop vs grass vs fallow) at almost every boundary; not clustered by large districts as strongly as
   I assumed in round 2, though the downs (56) are all arable and the vale pastures (55, 59) mostly grass.

## What a person would spot as fake in the round 2 output
- Three-way Voronoi vertices at every junction (no T-junctions, no through-lines).
- Every boundary straight and a convex polygon; no field group shares a grain with a lane or stream.
- Woods as blobs and ribbons laid over the fields: they cross hedges and field edges instead of filling parcels.
- Valley ribbon as two near-parallel uniform bands; belts as short bars that do not sit on a boundary.
- Ellipse-shaped copses in mid-field instead of at corners.
