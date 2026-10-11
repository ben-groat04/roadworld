# FAULTS (own audit, engine round: no renders; the "picture" is each prototype screenshot and the prototype source)

Method: every one of the 45 screenshots opened one by one; for each, the question "what in this picture would a driver, an engineer or the owner say is missing, wrong or would never be built here?", answered against the sim_core output (HUD lines, previews, tests), then against `prototypes/road-draw/src/input.js`, `ui.js`, `roadmesh.js`, `config.js`, `shell.html`, `inspect.js` (opened in full for the parts named in COMPARE.md). Faults below are ones NOT in the brief's starting list unless marked (brief).

## Pass 1

1. Dragging a free road end onto another road made no junction (the prototype's conform joined it). FIXED: a free end dropped within 1 m of another road or node at the same level joins it (T), one undo step; test "Edit: a free end dropped on another road joins it".
2. After a junction-making edit the edited edge is replaced, so the selection is lost, and the moved handle can lie ON the road it joined, which makes "select the road at that point" ambiguous. FIXED: `EditResult::anchor` is a point on the edited road clear of every other road, `EditSession::select_at(anchor)` re-selects it; both junction tests select it back and check which road got selected.
3. My first `fit_clothoid_chain` bound (spans at most 12 times the chord) rejected the existing "G1 fallback" case (a 957 m spiral for a 62 m chord: legal before). FIXED: bound 20 times per span (the old total bound was 20 times the polyline). Noted: that G1 case is itself a poor road; it stays because an earlier test pins it as "advisory, not error".
4. My first fix fell back to the G1 headings when the G2 solve ran away, which turned the doubling-back stroke into a loop that "fits". WRONG for the tool (the old test, and the prototype's refusal, want an error). FIXED: a solve that builds a runaway chain refuses; the G1 fallback is only for a solve that failed to converge on a sane bend.
5. The first dual-ghost test compared the ghost with the exact offset of the CIRCLE through the clicked points and reported 2.6 m "error" at R 400: wrong metric (the fitted centreline is not a circle). FIXED: compare with the offset of the FITTED centreline; worst vertex distance 4 cm at R 400 and at R 60 (limit 15 cm), and a very tight R 25 case is in the test too.
6. Bearing north can come out as 359.999999 or 0: the compass test would fail on a wrap. FIXED: compared modulo 360.
7. My first `edit_hud` printed an invented readout line during a drag (not in the prototype). REMOVED: the prototype prints only the red error of a refused edit.
8. The tuning panel's "Wall start" default in the prototype is 12, the end of its range (2..12); `EarthworkParams::wall_start` in C++ is 6. Checked the region pack: 12 (so the screenshot value 12 is the data value). Pinned in the tuning test.
9. The help table has 14 rows (the parity doc says 13). The test pins 14 and the doc note is in H.
10. `finish_pick_px` default 10 made a click within 10 px of the last point a finish, where the prototype adds a point (screenshot 26). CHANGED to off by default (brief). Existing tests that use the cue now set it; this is a behaviour change a host that already calls `click` will see (see Residual 9).
11. Heat classes in the prototype use `ty.minR` and `ty.maxGrade` in whole percent; sim_core stores `advisory_max_grade` as a fraction. The test checks the 8% (0.08) boundaries both sides (0.119 amber, 0.121 red, 0.041 caution, 0.039 ok).
12. The prototype's profile label for zero is "0 m", and `ceil(lo / step) * step` can be -0 in C++: the label function and test cover it (`profile_z_label`).

## Pass 2 (second read of all 45 screenshots against COMPARE.md, then the source again)

13. Screenshot 18, 19 and 20 read `A road / dual c'way`; the region data name is `A road / dual carriageway`. The HUD would have printed the long name and the radius advisory too. FIXED with region data: optional `short_name` per road type, `RoadType::hud_name()` used by the HUD and the advisories; test "Road tool rules parse from region data".
14. A road that is one road in the prototype but several edges here must select as one: my first chain rule also chained two roads that were only drawn end to end (a road continued from another's end), which the prototype keeps as two roads. FIXED: chain only through a junction node (three or more edges); test "two roads meeting end to end are two roads". (A road whose crossing road was deleted afterwards is now two roads: Residual 3.)
15. In the prototype the PgUp / PgDn offset is added to a snapped height too; here a snapped end / node / road keeps its own height (a junction at the wrong height is worse). DECLARED (parity doc H.3), not changed.
16. `EditHover` still names the node / interior point / road for click logic, but the prototype lights only the road. FIXED: `hover_chain` names the road to light for every hover kind; test "Edit hover reports nodes and interior points but names the road to light up".
17. In edit mode the HUD header in the prototype shows the draw tool's type even when another road is selected (rough edge E.9). Here it shows the selected road's own type; declared.
18. An edit's refusal reason came back only as `EditResult::error` text; a host would print it plain. FIXED: `error_hud_line` / `warning_hud_line` give the same red / amber glyph lines as the previews.
19. `tuned_region` scales the BASE pack's K values, so repeated slider moves do not compound; checked by applying the default again (test).
20. Region neutrality: all values the prototype hard-coded are region data now (snap angle / length, min span, water level, K multiplier, chain turn limit, heat-map factors and colours); the UI colours and sizes are interface constants in `ui_theme()` (not region data) and are declared as such.

## Pass 3

21. Clean builds, full suite on both compilers (numbers in STATUS.md). Re-ran the tool-UI tests after each fix (95 cases in the road / edit / UI filter all green before the full run).
22. Last cold read of the diff for things the owner has said more than once: opened every screenshot (45 of 45), no row of COMPARE.md rests on the written spec alone; rows 24, 25, 37 to 40 and 43/44's J key are marked as host or other-agent work, not claimed.

## Residual (not fixed; why)

1. Selected road number `#N` is the lowest edge id of the chain, not creation order (screenshots 32, 41, 42 show #1 and #2). Needs a road / stroke id on `Edge`, a change to the graph model outside this round's files. Cosmetic.
2. Water: the tuning value reaches the profile series, and the host must give it to `HeightfieldGround`; the water plane, wet tint and the "bridge over water" look are terrain / host work (GAP-8 partly open).
3. Chain rule is geometric. A road drawn as one stroke whose crossing road was later deleted leaves a degree-2 node: its two edges select as two roads (the prototype: one). A stroke id on `Edge` would fix both this and item 1.
4. Dragging a node shared by several roads (a junction) moves them all and only ADVISES a same-level crossing; no junction is made for that case.
5. Screenshots 37 to 40 (bridge, tunnel portal, retaining walls) are the structure and grading code of other agents; this round's rows cover only the tool's advisory, profile zone and tuning fields for them. No test of mine pins their meshes.
6. Screenshots 24 (frame stats) and 25 (profile hidden) and the J key of 43/44 are host features; not tested here.
7. Prototype hover over a bridge deck picks the road by the 3D ray; here the host passes `surface_hit` and the pick uses the ground point under it: if the host does not pass it, a deck is not hoverable.
8. The `R at cursor` readout is infinity on every natural end (as in the screenshots); it is finite only after an End snap pins the end heading.
9. `finish_pick_px` default flip: a host written against the old default (click on the last point finishes) now needs `params().finish_pick_px = 10` or the Enter / right-click gestures. The Unreal M2 run (holds the lane) was not touched; its branch must be checked at merge.
10. No Unreal build and no render were made (lane taken; brief says engine work with screenshots as the spec).
