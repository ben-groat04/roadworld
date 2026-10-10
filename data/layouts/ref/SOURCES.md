# Reference layout sources

Road centrelines: (c) OpenStreetMap contributors, ODbL (https://www.openstreetmap.org/copyright). Internal test fixtures only (approved in docs/planning-log.md). No imagery or Google data is stored here; the photos are the owner's private Street View screenshots in the gitignored docs/reference/gsv/.

Queries (Overpass API, JSON, converted with an equirectangular projection about the ring centre):

- Brynich roundabout: `way["highway"](around:700,51.9410,-3.3590);out tags geom;`
  Ways used: ring 4792662; A40 SW arm 87349824, 4792665, 617086556, 617086559; A40 SE arm 617086562, 617086565, 4792671; A470 NE arm 1313269102, 87940697, 4888801; B4601 NW arm 987368139, 27255493, 42765380. Ring centre (7.7, 41.5) in the query's local frame, mean radius about 44 m. Arms are simplified to straight approaches.
- Watton (Thetford Road, A1075): `way["highway"~"^(trunk|primary|secondary|tertiary|unclassified|residential|primary_link|secondary_link|service)$"](around:450,52.5625,0.8292);out tags geom;`
  Ways used: ring 444337276 (centre about (3.2, 5.3), radius about 16 m); A1075 north 4020066, 917453273/4; SE 1343564128, 917453270/5/6; Watton Road 13959927, 917453271/2; NE minor road 982290396/7/8.
- A40 Brynich T-junction (a40_brynich_tjunction.json): NOT identified in OSM. Several wide searches around the Brynich roundabout and the A40 to its west found no matching side road, so this layout is a photo-only approximation (both photos, 180248 and 180306, share it).

Identification of the two roundabouts rests on the catalogue notes, the photo minimaps and the road names/geometry; the Watton match is likely, the Brynich match is likely but the SW approach and splitter shapes differ from the real ones.
