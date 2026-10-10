# City Painter: UK road marking rules (draft, from sections 1 to 5)

10 Oct 2026

First extraction of the UK road marking rules, for turning into data files. Source: Department for Transport, Traffic Signs Manual Chapter 5: Road Markings (2019 edition, © Crown copyright), sections 1 to 5. Diagram numbers and TSRGD references are from that text.

**Not yet extracted:** roundabouts, grade-separated junctions, yellow box junctions, arrows and lane destinations, traffic calming (Chapter 5 sections 6 to 10); give way and stop lines, cycle and bus markings, parking bays (Chapter 3); pedestrian crossings and signals (Chapter 6). No further extraction is planned: this is a reference, and the person tunes exact numbers by eye.

**Licence caution:** the Manual's reproduction terms cover non-commercial research and internal circulation, with acknowledgement. The diagram dimensions are prescribed in the Traffic Signs Regulations and General Directions 2016 on legislation.gov.uk. For anything shipped in the game, take the numbers from the regulations and check their licence.

## Line patterns by speed limit

- **Warning line (diagram 1004 / 1004.1):** 4 m mark and 2 m gap at 40 mph or less, 6 m mark and 3 m gap above 40 mph.
- **Centre line (1008 / 1008.1):** the inverse of the warning line, so 2 m mark and 4 m gap at 40 mph or less, 3 m mark and 6 m gap above 40 mph. Inferred from the Manual's statement that warning lines are the inverse of centre lines.
- **Lane line (1005 / 1005.1):** 6 m module at 40 mph or less, 9 m module above 40 mph. Inferred from stud spacing, so confirm mark and gap against the regulations.
- **Double white lines (1013.1):** continuous line plus either another continuous line or a broken line (1 m marks, 5 m gaps). Lines normally 175 mm apart (minimum 90 mm), each at least 100 mm wide with 150 mm normal, overall width at most 900 mm (version B up to 1200 mm).

## Widths and stud spacing

- **Centre lines:** 100 mm on two-lane roads at least 5.5 m wide. 150 mm on two-lane roads 10 m or more wide, on three lanes marked two one way and one the other, and on four or more lanes. Studs every 12 m (100 mm) or 6 m (150 mm) at 40 mph or less, and every 18 m (100 mm) or 9 m (150 mm) above 40 mph.
- **Lane lines:** 100 mm, or 150 mm where more visibility helps. Always 150 mm on 70 mph dual carriageways and motorways. Studs every 12 m at 40 mph or less, 18 m above.
- **Warning lines:** 100 mm on two-lane roads under 10 m wide, 150 mm on roads 10 m or wider and on four or six lanes. Studs every 6 m (4/2 module) or 9 m (6/3 module).
- **Edge lines (diagram 1012.1, continuous):** all-purpose roads with no hard shoulder or hard strip 100 mm (150 mm at 70 mph), studs not normally used. With a hard shoulder or strip, 150 mm (200 mm at 70 mph), studs every 18 m. Motorways 200 mm, studs every 18 m.
- **Broken edge line at junction mouths, lay-bys and splays (diagram 1010):** 100 mm at 40 mph or less, 150 mm at 50 or 60 mph, 200 mm at 70 mph, studs every 8 m.
- Edge lines sit about 225 mm in from the carriageway edge. They never replace a give way line at a priority junction, and are not carried across side road mouths.
- On motorways, raised-rib edge lines (diagram 1012.2, 200 mm) are used, with ribs at 500 mm spacing, or 250 mm on slip roads.
- **Studs:** white for lanes and centre lines, red on the left edge, amber on the right edge beside a central reservation and on the offside of one-way roads, green at lay-bys and acceleration or deceleration lanes. Spacing is normally 18 m on dual carriageways, reduced to 9 m on bends under 450 m radius.

## When each marking is used

- **Centre line:** single carriageways only. Omit it where the carriageway is under 5.5 m wide. It should never be narrower than a lane line.
- **Warning line replaces the centre line** where forward visibility is below the warning visibility distance W, and near hazards such as a side road. At a priority junction, a warning line on the major road is normally justified when side road traffic exceeds about 100 vehicles an hour, or visibility of the junction is poor.
- **Lane lines become warning lines** on the approach to give way and stop lines, signals and roundabouts.
- **Minimum number of warning-line marks per approach:** central warning line on a two-lane road, 5 at 30 mph, 7 at 40 mph and 7 above; on four or six lanes, 7 at 30 mph, 10 at 40 mph and 10 above; lane line on approaches to roundabouts or signals and on minor roads at priority junctions, 5 at 30 mph, 7 at 40 mph and 7 above.
- **Visibility distance** (based on the 85th percentile speed, not the limit; values are not interpolated within a band): desirable minimum V and warning distance W are 75 m and 115 m up to 30 mph, 95 and 160 for 31 to 40, 120 and 195 for 41 to 50, 150 and 240 for 51 to 60, and 175 and 275 above 60.
- **Double white lines:** used where visibility falls below V, on climbing lanes, and on three-lane overtaking sections. They are not normally used on carriageways under 6.1 m wide or in built-up areas. White bi-directional studs sit between the lines at 3 to 4.5 m spacing, with 4.5 m recommended. Double lines mean no stopping on single-lane-each-way roads.

## Arrows and tapers

- **Deflection arrows (diagram 1014)** come before continuous lines and hatched markings: arrow length and distance from the arrow tip to the start of the taper or line are 4.5 m long at 13.75, 43.75 and 79.75 m for 30 mph; 4.5 m at 19.75, 55.75 and 109.75 m for 40 mph; 6 m at 21, 66 and 138 m for 50 mph; 6 m at 30, 84 and 165 m for 60 mph. Two arrows normally, a third where visibility is limited.
- **Hatched marking tapers (diagram 1040):** 1 in 40 up to 40 mph, 1 in 45 for 41 to 50, 1 in 50 for 51 to 60, 1 in 55 for 61 to 70.
- **Tapers at refuges and islands:** preferred minimum 1 in 40 up to 40 mph, 1 in 45 for 41 to 50, 1 in 50 for 51 to 60. Absolute minimum 1 in 20 up to 30 mph, 1 in 30 for 31 to 40, 1 in 40 for 41 to 50, 1 in 50 for 51 to 60. Lines stop about 300 mm from the island edge (not under 150 mm).

## Priority junctions

- **Simple T-junction or crossroads:** a give way line (diagram 1003A) on the minor road, and warning lines on both roads where justified. Details of the line itself are in Chapter 3.
- **Ghost island junction** (right-turn lane protected by hatching): through lanes 3.0 to 3.65 m wide, turning lane 3.5 m desirable (3 to 5 m, and no narrower than 2.5 m except on narrow urban roads). The turning length is 10 m measured from the minor road centre line, plus a deceleration length and direct taper that depend on speed. Use two deflection arrows when the 85th percentile speed is 40 mph or more.
- **Ghost island arrows:** a bifurcation arrow (diagram 1039) at the start of the taper, 8 m long with the 4/2 warning module and 16 m with the 6/3 module (32 m at 70 mph). Single-headed lane arrows (diagram 1038) at least two per lane, 4 m long with the 4/2 module, 6 m with the 6/3 module, and 9 m at 70 mph.
- **Staggered junctions:** a right-then-left stagger is preferred, with at least 50 m between the minor road centre lines.
- **Dual carriageway junction:** the right-turn lane is separated from the through lanes by diagram 1010 at 100 mm (40 mph or less), 150 mm (50 or 60 mph) or 200 mm (70 mph). Lane lines through the junction are warning lines, with at least 7 marks at 40 mph or above and 5 below.

## What the generator will need from the lane graph

This is a working inference, not from the Manual.

- Per road segment: speed limit, road class, lane count and direction, carriageway width, hard shoulder or strip presence, and one-way status.
- Per junction: topology (T, crossroads, staggered, slip), which road has priority, and the turning movements.
- Computed values: curvature and crest visibility distances from the clothoid and vertical profile, to decide between centre lines, warning lines and double white lines.
