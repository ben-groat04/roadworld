Auditor: independent agent, not the builder
Verdict: send back
References opened: 9 of 9 chosen. Photos: aerial/09_m181_single_lane_roundabout_overcast.jpg, aerial/15_stockiemuir_road_t_junction_residential.jpg, aerial/35_a160_a180_habrough_junction_drone_uk.jpg, aerial/60_oxon_ortho_wolvercote_roundabout_12cm.jpg, aerial/63_oxon_ortho_cutteslowe_south_arm_splitter_12cm.png, street/26_junction-t-minor-double-dash-give-way.jpg, street/28_junction-roundabout-splitter-island-elevated.jpg, street/30_junction-roundabout-approach-zigzag.jpg, street/33_dual-slip-road-ladder-chevron.jpg. Renders opened: after/A B C D E F G K L, before/K, before/F.

Skipped or not opened (read this first):
- The 12 gsv screenshots (A40 Brynich, Watton, M11 J6) were NOT opened as images. Where "gsv" appears below it is from their INDEX.md text only.
- aerial/61, 62 and the RHT photos (40, 41, 42, 45) were not opened. No photos downloaded (every case had 3 or more usable photos, though only one true top-down T-junction and no close slip-merge photo exist).
- Renders H, I, J do not exist in after/ (only A-G, K, L). The builder says renders are not framed per reference photo; confirmed.
- I did not run the test suite; I read the test source only.
Composites (gitignored): docs/reference/_compare/junction-markings-audit/ 1_island_B_vs_street28, 2_entry_K_vs_aerial63, 3_tee_D_vs_aerial15, 4_gore_F_vs_street33, 5_ring_A_vs_aerial09, 6_tee_low_E_vs_street26.

## Short verdict
Real progress: before/after movement is genuine. The splitter island now exists as a visible grass teardrop with ladder bars (before/K: only four hairlines, no island), the slip gore is much longer and hatched (before/F: a short stub), and gore paving is gone. But the picture is still not what the photos show, and the layout (not the look) is wrong in ways the builder's own tests cannot catch. Send back for one more round on the items in the last section.

Layout/markings is scored apart from look. Look (flat colours, no wear, terrain) is out of scope here and not scored.

## Ranked faults, per case

### Roundabout (A, B, C, K, L vs aerial/09, 63, 60, street/28, street/30)
1. R1 (layout, worst). Only 2 of the 4 arms have a splitter island (A, L: north and south have one; east and west have none, the west arm has just a 4-dash stub across it). aerial/09 and 63 show every arm with a splitter island and hatched approach. Composite 5.
2. R2. Ring lane markings are incomplete. In A and B the ring lane-line dashes appear only round the northern half; none round the south and west. aerial/09 and 60 show dashes continuous round the ring, broken only at entries. No ring edge lines on either kerb (builder's own item 5). Composite 5.
3. R3. Island proportion and end shape. The island is a slot with a square-cut ring end and a closed white outline round the grass (K). street/28 and aerial/63 show the white line running beside a kerbed island, never closed round it. The island is about 7 lane widths long in B against 4 to 6 in photos (builder admits, item 1, not retuned). Composites 1 and 2.
4. R4. Hatching is two narrow ladders on the flanks; the wedge past the nose is an empty outline with two bar stubs at its end (C, bottom, y about 750). street/28 shows ladder bars along the whole island between the white line and the kerb, the line then bending back to the centre line. aerial/63 shows 3 to 5 bars across the wedge. Builder item 3.
5. R5. Give-way line. K shows two dashes spanning the inbound lane, mark about 53 px, gap about 92 px: the gap is nearly twice the mark. The data says 0.9 m mark / 0.6 m gap (gap shorter than mark). It is straight, not curved to follow the ring (aerial/09 shows the dashed line following the ring curve). In A the west arm line is a 4-dash vertical stub about half the arm.
6. R6. Arms flare far more than photos: A and B show bellmouths about double the arm width, and a long dead strip of tarmac beside the island with no lane lines in it (builder item 2, the "4.4 m dead gap"). aerial/09 and 63: edge lines run straight beside the island.
7. R7 (look, but a viewer will read it as a defect). In B and K the south arm ends in a stone slab or step structure across the carriageway with markings running over it; thin black contour lines on the terrain round the ring (A, B).
8. R8. No exit-side edge line, no entry arrows, no hazard-line change before the island (builder item 5; confirmed in B, C). street/30 shows arrows and zig-zag on the approach.

### T-junction mouth (D, E vs aerial/15, street/26)
1. T1 (layout, worst). The give-way line is a 3-dash stub covering about 0.6 of the inbound lane (D, composite 3), not the full inbound lane to the major-road edge; street/26 shows a double row of long dashes across the full minor-road width at the major-road edge. In D the triangle sits almost against the stub (under 1 lane width); aerial/15 and the builder's own notes say about 2 lane widths before the line. An unexplained extra short solid line runs right of the triangle.
2. T2. The minor arm carries two rows of dashes plus two edge lines with small perpendicular tick marks at regular intervals along both edges (D, bottom right). These look like segment joins rendered as cross-bars; no photo shows them and every minor road would have them.
3. T3. Major-road centre line is the same dash right through the mouth, no warning-line (short gap) change on the approach, no edge-line break across the mouth. aerial/15 shows the minor road with its own centre stub ending at the triangle and the major edge line interrupted at the mouth. Builder item 7.
4. T4. Camera E is unusable (sky and grass, markings under 3 px), so the street-level T-junction is effectively unrendered. Composite 6. street/26 is the target framing.
5. T5. The builder's checklist says give-way line and triangle "YES (unchanged)": true for presence, false for geometry (T1).

### Slip gore (F, G vs street/33, aerial/35)
1. G1 (layout, worst). The gore is V chevrons (about 20 nested) in a wedge with no solid boundary lines on its two sides; street/33 and aerial/35 show a solid line each side, bars between, and a clear triangular nose. The builder knowingly left chevrons "as chosen by the owner" (item 10). That is a product choice I do not overrule, but the missing boundary lines and the nose are not owner-chosen. Composite 4.
2. G2. The nose is wrong: two grass notches (dark triangles) cut into the carriageway at the start of each gore (F, near (950-1010, 350-370) and (1050-1140, 670-690) px); the wedge begins with a blunt cut, not a point.
3. G3. Past the gore the slip lane is not visible as a separate lane in F: the chevron wedge ends in a hard diagonal edge against grass at top right. No dashed line across the slip mouth, no slip lane line (builder admits).
4. G4. G (low) shows the gore at the horizon as tiny speckle; it proves nothing about marks. Needs a camera on the slip road at the nose like street/33.
5. G5. Central reservation is a bare grass strip with no barrier (look only).

## Builder's measuring tools (tests on agent/junction-markings, tip d611346; tests/test_markings.cpp mainly)
New TEST_CASEs: hatching bar endpoints lie on boundary lines; painted taper short; major centre line has no gap beyond its own across a side mouth; parking bays stop short of a junction; entry give-way sits on the ring outer radius; no paving in a slip gore (both driving sides); every splitter arm of the interchange fixture has island, converging paint, bars and an entry line.
They do test photo properties for: bars end on lines (street/28, 33), island narrows away from the ring, no paving in gore, short taper, centre line continuity.
Weak: the entry test only checks each dash centroid is within about 1.6 m of the ring outer radius, plus count >= 3 per marking and >= 4 entries. It does not check span. The island test asserts `islands >= 2`, which passes with islandless arms (R1).
Properties a photo demands that no test checks:
- Give-way line spans the whole inbound lane and is absent from the exit lane; dash:gap ratio; line follows the ring curve.
- T-junction give-way line spans the inbound minor lane at the major-road edge; triangle about 2 lane widths before it.
- Every arm of a roundabout has an island (the test accepts 2).
- Ring lane lines and edge lines continuous round the ring, broken only at entries.
- Island size in lane widths (1.2-1.5 wide at the ring, 4-6 long) and no closed outline round the kerb.
- Gore: solid boundary line each side, pointed nose, no ground notch at the nose, slip lane visible after the nose.
- No stray perpendicular geometry along edge lines (cross-ticks).
- Edge-line break across a side-road mouth; warning line on the approach; minor-road centre stub ending at the triangle.
- Camera coverage: no test or render is tied to a photo framing.

## Builder's STATUS.md and REFERENCE_NOTES.md: claims vs what I found
Holds:
- Island visible, widest at the ring, nose pointing away (B, K vs before/K).
- No paving in gore, long speed-derived gore (F vs before/F).
- The self-audit items 1-13 are honest and largely match mine (island proportions, empty wedge, dead gap, no ring edge lines, chevrons vs ladders, nose notches, camera E).
Does not hold or missed:
- Checklist "Give-way line + triangle: YES (unchanged)": geometry wrong (T1), triangle jammed against the stub.
- Checklist "Give-way dashes across inbound lane: YES": gap in the render is longer than the mark (K), not the 0.9/0.6 data or the roughly 1.2:1 mark:gap in REFERENCE_NOTES; and straight, not following the ring.
- "Ring lane dashes exist": only on half the ring in A and B, not listed.
- Missed entirely by the builder: only 2 of 4 arms have islands (R1); cross-ticks on the minor road (T2); gore ends in a blunt cut with no visible slip lane (G3); stone slab over the south arm (R7).
- REFERENCE_NOTES says street/26 was "via INDEX only, not re-opened"; the T case rests on 3 files and is thin. One more top-down UK T-junction and one slip merge photo would help. Fetching is pre-approved by the owner (standing, in the agent contract); the builder's reason for not fetching ("approval only relayed") was wrong, although it did fetch 4 orthophotos.
- Orthophoto measurements are admittedly eyeballed (+/-20-30 %). Island length defaults (17/26/40 m) were not retuned though its own figures say they are 1.3-2x too long.
Same-camera before/after movement: real for K and F (island and gore appear). For A, B and D the arm and T geometry the photos care about most changed little.

## For the next builder's brief (exact items)
Roundabout
1. Island on EVERY entry arm (A west and east have none). Test: island count equals arm count for arms with a kerbed median or 2+ lanes, not `>= 2`.
2. Island 1.2-1.5 LW wide at the ring, 4-6 LW long, nose away from the ring; no closed white outline round the kerb; white line beside the kerb at about 0.4 LW with ladder bars between along the full island (street/28), then bending back to the centre line; wedge past the nose hatched 3-5 bars (aerial/63).
3. Give-way: dashed, mark at least equal to gap (data 0.9/0.6 m), spans the whole inbound lane, follows the ring edge curve, none on the exit side. Tests: span >= lane width, ratio, none on exit half.
4. Ring lane dashes and edge lines continuous all round (A and B show half). Test: marked fraction of ring circumference outside entries >= 90 %.
5. Remove the 4.4 m dead gap: lane edge lines run directly beside the island (flare/trim geometry).
6. Remove stone slab over the south arm markings and black terrain contours, or frame them out.
7. Approach: hazard-line change before the island, entry arrows (street/30), exit edge line.
T-junction
8. Give-way line spans the full inbound minor lane at the major-road edge (double-dash style per street/26 if kept in data); triangle 1.5-2.5 LW before the line; remove the stray stub. Tests on span and triangle distance.
9. Remove the perpendicular cross-ticks along the minor-road edges. Test: no marking quad with long axis perpendicular to the road tangent unless it is a declared bar, rung or dash.
10. Warning-line dashes on the approach; edge line broken across the mouth; minor-road centre stub ending at the triangle (aerial/15, street/26).
11. New camera E at 1.6 m eye height on the minor road about 8 m from the mouth (street/26 framing); add a top-down camera matched to aerial/15.
Slip gore
12. Pointed nose, no ground notches; solid boundary line each side; bars between (chevrons only if the owner still wants them, but with the solid lines); slip lane visible after the nose; dashed line across the slip mouth. Tests: nose is a point, no ground triangle inside the carriageway outline, each side has a solid line.
13. New camera G standing on the slip road at the nose (street/33 framing); top-down matched to aerial/35.
Process
14. Frame every render at the best photo's framing, composite render | photo | before, list photo files in COMPARE.md. Write each new test from a property in this list and name the photo that demands it.
