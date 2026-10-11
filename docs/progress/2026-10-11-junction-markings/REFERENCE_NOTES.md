# Reference notes for junction markings (2026-10-11)

Every number below is an eyeball measurement from the named photo, expressed in lane widths (LW, ~3.65 m rural, 3.0-3.3 m town) by comparing with car width (~0.5 LW) and the lane next to it. Accuracy is roughly +/-30%. Photos are private (docs/reference, not committed). "RHT" files are right-hand-traffic countries, used only for plan geometry (shape, proportion), never for UK marking style. Where a case lacks photos the written rule is used and labelled `RULE`.

## (a) Roundabout approach arm and entry
Opened: aerial/09_m181, aerial/10_a16_a158 (too small to measure arms), aerial/40_hafnarfjordur (RHT), aerial/41_kolbaskowo (RHT, best plan view of arms), aerial/07_a9_raigmore, aerial/04_m5_j12, street/28_junction-roundabout-splitter-island-elevated, street/30_junction-roundabout-approach-zigzag, gsv 180326 (A40 Brynich roundabout), gsv 175143 (A1075 Watton). 10 files.
- Kerbed splitter island: 41 (south arm) shows the island about 4.5-5 LW long and ~1.3 LW wide at the ring end, narrowing to a rounded nose on the approach side. 28: kerbed island about 5-6 LW long.
- Painted taper ahead of the kerbed nose: short. 41: the two solid lines that bound the island continue ~2 LW past the nose, then merge into the lane/centre line; hatching bars sit only in the ~2 LW taper (bottom arm, 2-3 bars). 28: 7 ladder bars along the whole kerbed island at ~0.4 LW spacing, hatched strip ~0.5 LW wide, running the full island length and ending 1 LW before the nose. 180326: wedge hatched with 4-5 bars, wedge length ~4 LW, bars ~0.8-1 LW apart, wedge as wide as the island (~1.2 LW at the ring). 175143: wedge ~3.5 LW long, 3 diagonal bars. So the painted taper is roughly 0.5-1x the kerbed island length (2-4 LW = 7-14 m), NOT 10+ LW.
- Hatching relation to boundary: in 28, 180326 and 175143 every bar runs from one solid boundary line to the other (or to the kerb) and stops exactly on them; none overshoots. Bars are perpendicular to the wedge axis (28, 180326) or diagonal (175143, chevron style on wide wedges).
- Boundary lines: continuous white, 150 mm (0.04 LW), both sides of the wedge (28, 41, 180326).
- Give-way at ring edge: a single broken line across the entry lane(s) at the ring edge, following the ring curve (aerial 41 bottom arm, 09 all arms, GSV 180326, 175143). In 41 (lane about 85 px) a dash is about 22 px and a gap about 18 px, so mark : gap is roughly 1.2 : 1, width about 0.04 lane widths (0.15-0.3 m). Chosen default `give_way_ring` 0.30 m wide, 0.9 m mark, 0.6 m gap (data/regions/uk/marking_rules.json). A give-way triangle sits about 2 lane widths before the line on side-road T junctions (180306, 15), not on roundabout entries.
- Lane lines on the approach become warning/centre dashes up to the island and stop at the wedge (09 top and bottom arms, 41).
- The line at the ring edge sits on the ring's outer edge line position (it replaces the edge line; 09, 41).
- Elevated/kerbed island interior: grass (28, 41, 09), not asphalt.

## (b) Whole roundabout ring
Opened: aerial/09, 40 (RHT), 41 (RHT), 10, 04, 07, gsv 180326, gsv 175143. 8 files.
- The ring carries no continuous outer edge line in 09 and 41 at the entries: the edge line is replaced by the give-way line across each entry and by broken edge lines across each arm mouth (09: dashes along the ring outer edge between arms; solid slivers at the exits). Between arms the outer ring edge has a solid white edge line (40 top-right arm, 09 east side).
- Inner edge: solid white line at the central island kerb in 40 and 41 (kerb band paved, lighter grey ring = apron). 09: no inner line visible.
- Lane lines on a 2-lane ring: dashed, ring-concentric (04 left roundabout, 07). Single-lane rings (09, 41): none.
- Arms flare: the arm widens from 2 LW to ~3 LW at the ring (09, 41); island wedges between arms on the ring side are small curved corners.
- Joins: in 41 the arm edge line, the splitter boundary line and the ring edge line all meet in a continuous curve; no gaps.

## (c) T-junction with a marked major road
Opened: aerial/15_stockiemuir, aerial/14_laceby (too small), gsv 180306 (A40 ghost-island T from the major road), gsv 180248 (A40 minor-road give-way, double-dash line), street/26 via INDEX only (not re-opened; claims not used). 3 usable files: 15, 180306, 180248.
- Major road centre line (dashed, ~1.7 m mark/ 3.5 m gap in 15; long dashes in 180306): continues across the side-road mouth without a gap in 15 (top-right junction the centre dashes carry through) and in 180306 the dashed centre line is unbroken past the ghost wedge. The edge line is what breaks at the mouth (180248: the nearside edge line ends in dashes that turn into the side-road edge).
- Give-way line on the minor road: double-dash row, each row dash ~0.2 LW (0.6 m), gap ~0.15 LW, two rows ~0.1 LW apart (180248, street/26 INDEX: 300x200 mm per dash with 300 mm rows). Give-way triangle ~1.5 LW before the line (180306, 15). Minor road gets a short centre line stub stopping at the triangle (180306).
- Kerb return radius: 5-8 m (15, 180306), with a short edge line carrying round the radius (broken).

## (d) Slip road diverge and merge
Opened: street/33_dual-slip-road-ladder-chevron, gsv 175655 (M11 J6), aerial/03_m25_m23, aerial/04_m5_j12, aerial/07_a9_raigmore, aerial/05 (described only in INDEX, not re-opened). 5 opened.
- 33: the diverge wedge is bounded by the mainline edge line (solid, continues straight) and the slip edge line (solid, peeling away); ladder bars between them, perpendicular to the wedge axis, ~0.45 LW apart, about 17 bars on the near side giving a wedge length of ~8-9 LW (30 m) from the point where the wedge is ~0.3 LW wide to ~1.1 LW wide at the nose end, plus a solid line continuing; the surface of the wedge is carriageway asphalt, a hard shoulder strip ~0.8 LW continues beside it, and BEYOND is verge/grass and a barrier. No footway, no paving, no raised kerb at the gore.
- Mainline lane lines (dashed) continue unbroken through the diverge; the slip road has its own lane dashes starting after the nose. The mainline edge line is continuous. The slip's left edge line peels away from the mainline edge line at the nose point (33, 175655).
- 175655: the diverge shows a hatched wedge of thick bars tapered at 1:~20, ~10+ LW long, bordered by two solid lines, and a short dashed line from the lane line into the slip entry. Surface is asphalt.
- Gore tip: a pointed V where the two lines meet; no kerb.

## (e) On-street parking bays near a junction
Opened: aerial/42_chemere (RHT, near-vertical), aerial/40 (car parks, off-street only). 2 files, and 42 is the only real kerbside case.
- 42: kerbside cars stop ~2 car lengths (~8-9 m, ~2.5 LW) short of the pedestrian crossing at the mouth and the bays ahead of the crossing are left clear; each bay ~1.2 car lengths, ~0.55 LW wide.
- No UK on-street bay photo exists. `RULE`: UK Highway Code rule 243: no parking within 10 m of a junction. Chosen default 10 m (region data), taper-out 5 m.

## Cases with no or weak photo coverage
- Parking bays: weak (one RHT photo). Use rule.
- Roundabout elevated-arm (add_raised_polygon) slot: no photo needed (rendering artefact).
- Slip merge (acceleration lane end): only 33 and 03 at low resolution; the merge is treated as the mirror of the diverge, as in 33's far end.

## Fetching more photos
The coordinator relayed that the person approved fetching more reference photos. That approval reached me only through the coordinating agent, not in chat from the person, so by the downloads rule I did NOT download anything. No photo was newly fetched. Thin cases: (e) on-street parking (one RHT photo), (c) T-junction (three usable photos, none top-down at this scale), (d) merge end (only 33 and 03). Fetching three to six more top-down UK photos for (c), (d) and (e) would make the checklist in INDEX.md firmer.

## Splitter islands, per photo (plan shape in lane widths LW; all eyeballed, +/-30%)
Files opened: street/28, street/30, gsv 175143 (Watton), gsv 180326 (Brynich), aerial/41 (RHT, mirrored mentally), aerial/09.
- street/28 (elevated, UK, 20 mph street): grassed island, kerbed, about 1.5 LW wide at the ring end, about 6 LW long, nose a rounded point pointing away from the ring; double yellow lines on the kerb; on the approach side a solid white line runs 0.5 LW off the kerb and ladder bars (perpendicular to the road, about 0.4 LW apart, 7-8 bars) fill the flank strip between kerb and white line; the white line then bends back to the centre line past the nose; the give-way dashes cross the inbound lane at the ring edge; exit side: just the kerb with yellow lines, no paint.
- street/30 (driver height, approach to a roundabout): zig-zag lines at the kerb, direction arrow on the lane, no island visible; only shows that approach lane lines become warning dashes towards the ring.
- GSV 175143 (Watton): kerbed island on the right about 1.2 LW wide at the ring, about 4 LW long (tarmac top, pale kerb); ahead of the nose a hatched wedge about 3.5 LW long, bounded by two solid lines that converge to the centre line, 3 diagonal bars; the give-way dashes cross the inbound lane at the ring edge.
- GSV 180326 (Brynich): kerbed island with ladder hatching, about 1.2 LW wide, island plus wedge about 6 LW; bars perpendicular to the wedge axis about 0.8-1 LW apart and every bar runs from one solid boundary line to the other; the two boundary lines converge to a single centre line 4 LW beyond the nose.
- aerial/41 (RHT): paved island about 1.3 LW wide at the ring, about 4.5 LW long, widest at the ring; the lane edge lines run straight beside it (no 4 m dead gap); 2 LW of converging solid lines past the nose; bars only near the nose; give-way dashes and zebra beyond the ring edge.
- aerial/09: islands read as small grass triangles 4-5 LW long with the lanes running straight either side; the give-way broken line sits on the ring edge across each entry lane.
Differences from the sim as of this round (honest): the sim flares the arm's lanes 2 m outward each side and holds that for about 30 m (junction corner trim), so a 4 m dead gap sits between the lane lines, whereas photos 41, 28 and 180326 show lane edge lines directly beside a 1.2-1.5 LW island with no gap beyond a 0.5 LW flank strip. The island is now a teardrop 0.75 of the gap wide (0.5 m flank strip) so the gap reads as island plus flank strip, but the overall plan is still wider and longer than the photos (island about 7.5 LW long, photos 4-6 LW).

## Orthophoto measurements (aerial/60-63, 12.5 cm/px = 8 px/m, fetched this round; see docs/reference/aerial/INDEX.md)
Read by eye from the pixel grid (no image tool was available to crop or measure, so every figure is about +/-1.5 m or +/-20 %; they are NOT survey measurements). Files opened: 60 Wolvercote, 61 Cutteslowe, 62 Marcham (all full frame), 63 Cutteslowe south arm (64 m crop).
- Lane width on the approaches: about 26-28 px = 3.2-3.5 m (61 north and south arms; 60 west arm).
- 61 Cutteslowe: the painted ring island is about 22-25 m across; each arm carries a hatched splitter wedge plus a kerbed island of roughly 10-20 m length and 2-3 m width at the ring (south arm in 63: island and hatch about 17 m together); entry give-way dashes lie on the ring edge, one row across each entry lane, dash about 0.6-1.0 m with gaps of similar size; the approach centre line becomes a hazard line well before the island.
- 60 Wolvercote: large oval ring with spiralled lane lines; entry arms have splitter islands about 15-25 m long; hatched wedges ahead of the nose are short (about 5-10 m) compared with the island; ring lane lines are dashed and run continuously round, interrupted only by the entries.
- 62 Marcham: slip arms leave the A34 over-bridge ring on single lanes; give-way dashes sit on the ring edge; the kerbed splitters are 10-15 m long.
Consequences for the defaults: kerbed island 17/26/40 m by speed in region data is about 1.3-2x what these small town and ring examples show (10-20 m); the painted taper 6-12 m is consistent with the 5-10 m seen; the give-way default 0.9 m mark / 0.6 m gap is consistent. NOT changed this round: the island lengths in region.json (needs a proper measurement pass with a cropping tool).
