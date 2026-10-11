# STATUS (junction markings branch agent/junction-markings)

FIRST LINES (skipped, refused, not done, not verified):
- Reference photos: fetched 4 Oxfordshire 12.5 cm orthophotos (aerial/60-63) and kept them; rejected 4 Commons high-altitude interchange photos (46-49) plus 1 empty orthophoto request for resolution, all deleted and un-logged. NOT fetched: T-junction with ghost island, slip gore close-ups, kerbside parking, signalised crossroads, mini-roundabouts. Orthophoto measurements in REFERENCE_NOTES.md are eyeballed (+/-20 %), no crop/measure tool was available; island length defaults were NOT re-tuned to them.
- Item 5 (ring edge/lane lines continuous across seams): NOT done. Item 3 parking-strip taper-out: NOT done. Item 4 dashed line across the slip mouth: NOT done. Dark slot (item 6): fix applied, not confirmed in Unreal. Junction triangle counts before/after not measured.
- Unreal plugin not touched or built; the new region.json `gore_length` table and `JunctionOverrides::gore_length` are not wired in unreal/.
- Renders are not framed per reference photo (only cameras A-L).
Suite: gcc 337/337 pass (2 expected should_fail checks). MSVC: see the final report.
Region fields added outside my files: region_pack.h/.cpp `junction.merge.gore_length` (speed table) + `MergeRules::gore_length_for`; uk region.json merge.gore_length table and roundabout.splitter_surface "grass".

## Self-audit (one pass only, done after the last render; nothing below was fixed in a second pass)
Reference files opened for the audit: street/28, street/33, gsv 180326, 175143, 180306, 175655, aerial/09, 15, 41, 60, 61, 62, 63. Before/after pairs from identical cameras: before/ and after/ (A-L).
What a driver, road engineer or reviewer would say is still wrong:
Roundabout arm
1. Island is 4 m x 26 m; real ones on these roads are 10-20 m long and wider relative to the lane (photos 28, 41, 61, 63). Region data (17/26/40 m by speed) not re-tuned.
2. A 4.4 m wide dead gap between the two lane edge lines runs about 27 m (flare held to the corner trim); the island fills 3 m of it, the rest is a 0.5 m painted flank strip. Real arms have lane lines directly beside the island. Fixing it needs the flare/trim geometry, not markings.
3. The painted wedge past the nose is an empty outline, not hatched; photo 180326/175143 show a filled wedge of 3-5 bars across the full width.
4. Lines past the nose converge over 14 m and end as two parallel hairlines 0.4 m apart at the centre line with no join to the arm's centre line (gap not measured).
5. No ring edge lines, no entry arrows, no exit-side edge line, no hazard-line (centre line) change before the island; the island outline closes across its ring end and reads as an extra transverse line at the ring edge.
6. One give-way row, 3 m wide, on the inbound lane only; photo 41/61 entries often show the line carried round the whole entry width including flare.
T junction
7. Centre line across the mouth uses the warning style 4/2 m; real majors show the long centre dash through a mouth in some photos (180306) and the warning dash in others; the dash phase relative to the edge's last dash was only bounded (gap <= style gap + 0.35 m), not matched.
8. Bay lines stop 10 m short but the parking strip and its kerb build-out are still full length to the corner; no taper marking; bay dividers/lines not compared with a UK parking photo (none available).
9. E camera frames sky and grass; not a usable comparison.
Slip road
10. Chevrons (35 m) are plain V chevrons; street/33 and 175655 show perpendicular ladder bars between two solid lines. The owner chose chevrons, so left, but it is the first thing a road engineer will question.
11. No dashed line across the slip mouth, no slip lane line; the two bare-ground notches at the nose remain (verge) and the nose point has no painted triangle outline.
12. Lane lines through the junction only for the main pair; the odd arm's own warning dashes and these may double near the trim (no overlap test failure, but not inspected beyond F/G).
Seams
13. Edge lines do not join across arm/ring (item 5 not attempted); the junction mesh now uses 2 m triangles but no before/after triangle counts or crack check beyond the existing seam test.
