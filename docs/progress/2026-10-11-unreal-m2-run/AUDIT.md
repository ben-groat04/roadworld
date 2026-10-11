Auditor: independent agent, not the builder
Verdict: accept for merge (tool mechanics only; the look of a finished road is NOT accepted, see fault 1; correct the builder's "matches" claims listed below before anyone quotes them)
References opened: 89 of 114 images listed, plus the sources named below.
- Prototype screenshots: 45 of 45 (all of `docs/progress/2026-10-11-prototype-ui/`, 01..44 and 08b), opened before reading any builder document.
- Composites `compare/`: 25 of 25.
- `renders/`: 19 of 44 opened as single files (all 15 `10_m2_*`, `20_st_s27b`, `s27c`, `s30`, `s31`). The other 25 `20_st_*` renders were seen only as the left half of their composite (same picture).
- NOT OPENED: `before/` (23 images; not needed for the verdict), `results_full.json`, `results_m2states.json` (I used my own two runs instead).
- Also opened: `hud_text.txt`, STATUS.md, FAULTS.md, COMPARE.md (after the images, as ordered), the five Playtest source files, `CPRoadDrawTool.cpp`, `CPToolManager.cpp`, `CPToolHudFormat.cpp` (grep), `sim_core/src/road_tool.cpp` (click, finish, build_preview), `CPSimWorld.cpp` (BuildTerrainTile), `unreal/tools/README.md`, `Run-Playtest.ps1`. My own run pictures: `AppData\Local\Temp\claude\audit_run\r1`, `r2` (not committed).
- NOT DONE: the five extra probes (step 4) could not be run. The harness has no scenario-file flag, only `-CPPlaytestM2Only` and `-CPPlaytestStates`; a new scenario needs C++ and a rebuild, which the brief forbids. I did them as code reading instead (section "Extra probes").

# Run result (I ran it, twice, Unreal lane free, closed afterwards)

- Run 1 (`-CPPlaytestStates`, 239 s): 51 checks, 14 failed. All 14 are in the milestone 1 camera section (rotate, pitch, pan, the 5 picks, HUD click-through, HUD refresh): the known "all synthetic mouse input lost" flake. The 14 milestone 2 / state checks all passed in that same run, so mouse input came back later (it was lost for about the first third). Run 1 pictures of s27b, s28, s30 look the same as the builder's.
- Run 2 (same command, 119 s): 51 checks, 0 failed. So the builder's "51/51" is real, and the flake rate is real (1 of my 2 runs, 2 of 9 theirs). A single run is not trustworthy either way; the harness should say "mouse input lost" loudly instead of 14 separate failures.

# 1. What differs, game picture vs prototype picture, per state (done before reading the builder's text)

Common to every pair: game HUD is top-left, same layout and wording as the prototype (header "DRAW . type", "cursor z", "snap:", span / bearing / R / min R, grade / max / total, advisory lines). Differences present in every pair: no "G2 ok" tick; amber and red lines start with ASCII "!" and "X" instead of the prototype's warning and cross glyphs; the game adds a bottom-left coordinate/frame-time block and a bottom key-cue bar that the prototype does not have; no profile panel at the bottom; no Tuning button, no scene bar; ground is dull, tiled-noise grey-green with a visible grid-like dither, not the bright terrain; ghost is carriageway width, prototype's is wider (total width). Game camera is higher and the roads are thin lines in the picture.

| Pair | Same state, comparable view? | What is different |
|---|---|---|
| s02 idle | Partly. Prototype is the whole-map start view with the pointer off the map (no cursor line); game is an oblique ground view with the pointer on the ground ("cursor z -4.4 m"). | Only the header is comparable. Game shows an extra cursor line, coordinates and cue bar; no map edge, water, Tuning panel, scene bar. |
| s03 hover free | Yes. | White ring small in both. Game ring is a little smaller. Ground differs. |
| s04 midstroke | Same kind of state, different stroke: prototype 4 points, 224 m, over a hill with profile; game 3 points, 120 m, flat, beside the existing road R0. | Game ghost is narrower, pale grey; prototype ghost is wider. Gold dots, dashed centre line and ring present in both. Game has 4 readout lines but no G2 tick; no profile panel. Game max grade "15.7%" next to "grade -1.1%" on terrain that reads 0.7% slope (see fault 9). |
| s05 snap end | Yes. | Green ring and "snap: end" match. Game road has only grey kerb outlines, asphalt tapers to nothing at the end; no brown cut/fill banks like the prototype. |
| s06 snap road | Yes. | Green ring on the road, "snap: road": match. Same road look as s05. |
| s07 extension guide | Yes. | Dashed cyan line from the road end to a cyan ring: matches in kind. Game guide has a slight S bend. |
| s08 / s08b offset guide | Yes in kind; geometry unverifiable. | Prototype shows a smooth dashed arc parallel to the arched road. Game shows a dashed line that runs flat then bends toward the cursor and straightens again (kink); the builder's own FAULTS item 5 says the same. 08b (prototype at 3x distance) vs game second spot: same kind. |
| s09 hover free | Yes. | Match (white ring beside a road). |
| s10 angle and length | Yes (both a 2-point stroke with angle snap). | Game ghost is narrower; prototype has a profile panel and a wider ghost. |
| s11 Shift | Yes. | Amber "snap off" in the header in both; match. |
| s12 min span | Yes (game zoomed in on purpose). | Red text and no ghost match. |
| s13 too tight | Yes. | Red ghost in both; prototype is a fan 10 m span, game a smaller blob (4.4 m radius vs 6.4 m). Red + amber lines match. |
| s14 radius | Yes. | Match in format; game also shows a grade line "max 23.6%" on near-flat ground. |
| s15 grade | DIFFERENT situation. Prototype: a natural 33% hillside next to a lake. Game: a 40 m stroke on flat ground raised 10 m with PgUp. | Text style matches; the picture proves nothing about real slopes. |
| s16 bridge | DIFFERENT. Prototype: a 232 m stroke across a lake with the white ghost drawn over the water. Game: a 90 m stroke on flat ground raised 25 m. | Text matches ("fill over 12 m wall cap: becomes a bridge"). No picture of the ghost over water or a hill, so the "ghost visible through terrain" claim is untested (builder admits it). |
| s17 tunnel | DIFFERENT. Prototype: natural hill, 144 m, -15.4%. Game: flat ground, cursor lowered 25 m, "grade -60.1%". | Same remark as s16. |
| s18 dual | Yes. | Two ribbons with a gap in both; game ribbons are narrower. Game says max grade 35.9% on flat ground (fault 9). |
| s19 motorway | Yes. | Same. Wider ribbons than s18 in both. |
| s20 single carriageway (O) | Yes. | One ribbon, header says "single carriageway": match. |
| s21 one-way | Yes. | "one-way" in header: match. Game shows a grade advisory, prototype none (different terrain). |
| s22 B road | Yes. | Match in header; both show advisories. |
| s26 three points before finish | Same state, but not the same content. | Prototype: pointer on last point shows 4 readout lines, "snap: angle", profile panel, ring on last point. Game: ghost is a 3-point curve, gold ring on last point, but the HUD has only "cursor z / snap: free" and the bottom cue "Click again here to finish": all span / grade / length readouts are missing while the pointer sits on the last point (hud_text.txt confirms). The builder says "matches"; it does not. |
| s27 finished idle | Yes. | Prototype: black asphalt with white edge lines, dashed centre line, brown earthwork bank. Game: two kerb strips and white marks; asphalt dark but visibly absent over the right-hand 20 m, a dark shadow wedge beside the left end. |
| s28 crossing refused | DIFFERENT. Prototype: a red ghost drawn across the road at the same height (grade advisory, red tint). Game: a text refusal "crossing too close vertically (1.4 m)" for a road raised 1 m; the only things drawn are the start marker and a yellow ring, the crossing ghost is not shown at all, and the road it should cross is off to the side. | The red ghost for a refused crossing is not shown. |

Prototype states with NO game picture at all: 01 help overlay (H), 23 Tuning panel, 24 perf panel (F), 25 profile hidden (P), 29 colour curvature, 30 colour gradient, 31-36 edit mode (hover, select, handle hover, drag, after drag, delete), 37 viaduct, 38-39 tunnel portal, 40 cut/fill walls, 41-42 select viaduct / tunnel profile, 43-44 scene inspect and connectors. (All belong to later milestones; the builder lists them as gaps.) Game pictures with no prototype counterpart: all `10_m2_*`, s27b, s27c, s30, s31.

# 2. What looks broken to a player, ranked by how visible it is

1. Finished roads float or sink. Evidence: s27b (asphalt tapers to a point at the east end, kerbs continue), s27c (the far road is clearly a ribbon hanging above the ground with a dark shadow gap under it, the near road sinks into the hill), s30 (a 120 m road on a hill is two thin grey strips and one floating scrap), `10_m2_05` (right half of the S-curve is kerbs only). The builder reports only the "sinking" half; the road also FLOATS (shadow wedges beside the left end of road A in every picture, s27c far road). Cause confirmed in code, section 5.
2. The headline outcome of the milestone, "I drew a road and it is there", is not visible on any hill: a player will think the click failed.
3. Terrain is a dull, grey-green tiled pattern with a visible grid dither; looks unlike the prototype (world look item, not M2).
4. s28: no red ghost for a refused crossing (the player sees only a line of text).
5. s26: readouts disappear when the pointer is on the last placed point.
6. With one point placed the cue says "Click again here to finish the road" (`10_m2_01`); clicking does refuse ("a road needs at least two points") so the text is misleading.
7. Offset guide bends instead of staying a parallel arc (s08, s08b).
8. Warning and error lines use "!" and "X", not the prototype's glyphs; no G2 tick.
9. Unexplained numbers: every curved 3-point stroke on near-flat ground shows "max grade 15.7% / 23.6% / 35.9%" next to an end grade of -1% to -3% (s04, s14, s18-s22; hud_text.txt). The prototype's max is close to the span grade. Could be real steep ground under the first span (the terrain at (-80,0) has 42% slope per `10_m2_01`) or a vertical-curve artefact; I could not tell. The advisory text that results ("grade 35.9% above advisory 6.0%") would be wrong if it is an artefact. Needs a number check by the next builder.
10. Tiny dash left near the old S-curve end after the undo in the builder's `10_m2_09/10`; not reproduced in my run 2 (`r2/10_m2_10_undo3.png` is clean). Probably a JPEG or timing artefact; low.

# 3. Checks: do they assert what their name says?

51 checks = 37 milestone 1 (camera, picking, HUD) + 14 milestone 2.

Milestone 1 (37): real measurements with tolerances (azimuth, polar, pan pixels and metres, zoom factor, picks against the sim, reprojection). Weak ones: "cursor: shown, not locked" (reads two flags), "frame time p95 < 120 ms" (guard only), "HUD updates every frame" (needs 9 of 11 changes; run 1 got 8 of 11 when mouse input was lost, so it is also flaky).

Milestone 2 (14), all of them count nodes and edges only (`Stats()` = `GraphStats()`):
- A straight (2/1), S-curve (4/2): pass if any 2-point or 4-point road was added anywhere, any height, any type, with or without a mesh. They would have passed with the asphalt missing, which is the bug in the pictures. Weak.
- T junction (6/4): a real split signature (A cut in two plus T), but it does not check where the junction is.
- Undo 1, 2, 3 and redo 1, 2: counts only; they do not check that the mesh was removed or restored, that the redo stack is cleared by a new stroke, or undo while a stroke is open. Weak.
- Right-click finishes (6/3): real input, counts change; fine. Right-drag rotates and does not finish (6/3 and azimuth moved more than 0.05 rad): good.
- "Esc cancels the open stroke" (6/3): WEAK, would pass if Esc did nothing: the count is 6/3 whether or not the stroke is cancelled, and the stroke point count is never read. Needs `PointCount() == 0` or the HUD cue back to "Click to start a road".
- States: "R0 drawn" (2/1), "second road finished" (4/2) fine as counts. "Everything undone" presses Ctrl+Z 8 times and expects 0/0: it passes even if s28's refused road was actually created, because extra undos on an empty stack are harmless. It does not prove s28 was refused.
- None of the 25 state scenarios asserts anything. The HUD text is logged (`LogHud`) and screenshots are taken, but no check says "snap: end", "snap: guide", "segment shorter than 4 m", the advisory lines, ghost visible, guide present, ring colour. Whether a state is right is decided by a human reading a picture.
- No check uses the position of anything drawn (a road at the wrong place or height passes), the road mesh, the ghost, the overlay, or the type chosen by 1-4 / O (only visible in the HUD header, not asserted).
- Input path is real (Slate events through the viewport, Enhanced Input, tool manager) for clicks, keys, Shift, right-click: good. Camera set-up uses `Pawn->SetView` directly (setup, fine). `ScreenOf` projects with `GroundZCm`, so click points land on the ground, not on the road; the "T onto a road" click works because snapping is by XY.

# 4. Extra probes (code reading only; not run, see top)

1. Road across a hill: builder's s30/s31 and my run 1 `s30` show the asphalt almost gone. Verified by picture.
2. Very short click-click: `build_preview` and `finish_with` refuse spans under `min_span` (4 m) with "segment shorter than 4 m"; `UCPRoadDrawTool::OnClick` shows it as a refusal. At overview zoom a second click within the finish ring counts as "finish this stroke" (pixel radius), so a quick double click ends up as the "needs two points" refusal.
3. Finish on the first point: Enter with one point and a click on the first point give the refusal "a road needs at least two points" (kept in the stroke); right-click with one point cancels silently (`OnSecondaryClick`). Cue text wrongly offers to finish (fault 6).
4. Undo during a stroke: `UCPToolManager::DoUndo` calls `World->Undo()` and leaves the open stroke alone, so Ctrl+Z removes the previous finished road while the stroke whose first point snapped onto that road's end stays open with a stale reference. The prototype does the same (`input.js` line 4 / 402), so it is parity, but it is a real hazard in a game (finishing then connects to a node that no longer exists, or is refused). Untested.
5. Drawing over an existing road at a shallow angle: only a 90 degree crossing with a 1 m height difference was drawn (s28). The shallow case goes through `check_stroke` with `refuse_band` in sim_core; no picture, no Unreal-side check. Untested.

# 5. Builder's report vs what I found

Holds: 51/51 (my run 2); the 11 + 3 milestone 2 / state checks as counts; the HUD wording (hud_text.txt matches the prototype's words); the white ghost with gold dots, dashed centre and rings; types 1-4 and one-way in the header and ribbon count; red ghost on a fold; "snap off" in amber; before/after fixes (ghost pale instead of black, HUDs no longer overlapping); the honest first lines of STATUS (asphalt, PIE Esc, ghost through hills, Shipping overlay, missing panels, flaky harness).

Does not hold or is overstated:
- COMPARE "matches" for s26 (readouts missing), s16/s17 (not the same situation as the prototype), s28 (no red ghost; different refusal), s08/s08b ("matches", but the guide kinks), s27 ("layout matches" with the asphalt gone).
- "Asphalt dips under the terrain": only half the story; roads also float (shadow wedges, s27c). STATUS shows it only as sinking.
- "Esc cancels" is listed as verified by a check that cannot fail.
- Missed entirely: the HUD cue offering "finish" with one point; the unexplained max-grade numbers; the missing red ghost on a refused crossing; undo during an open stroke untested; no state is asserted by the harness.

Cause of the sinking and floating, confirmed from code, not from the builder's text:
- The road centreline height comes from the draw session: each click point is `ground_z + PgUp/PgDn offset` (`DrawSession::build_preview`), and the finished edge is a smooth vertical-curve profile fitted through those few points (`fit_edge`). Between control points the road height has nothing to do with the ground.
- The terrain mesh is built once from the original heightfield (`CPGroundLayer.cpp` -> `FCPSimWorld::BuildTerrainTile` -> `CPTerrain::BuildTile`); nothing regrades it after a road is committed. `sim_core/src/earthworks.cpp` is compiled into the module (`Wrap_earthworks.cpp`) but nothing calls it (grep: no caller outside the wrapper; the playtest comment says "Earthworks are not wired yet").
- Measured example from `10_m2_01/02/06`: road A runs from ground 7.13 m at x=-80 to -2.16 m at x=80, so its straight profile is +2.5 m at x=0 where the ground is -1.52 m: 4 m floating above ground in the middle (the T-snap picture reads "cursor z 2.5 m" there, `10_m2_06`), and buried where the ground bulges the other way. The cross-section is flat while the ground is not, so a sloped site buries one kerb and floats the other.
- Anything cheap and not earthworks? I recommend NOT papering over it in M2: a depth bias or lifting the road would hide it only where the gap is small and would float roads on hills. The only cheap, honest changes: (a) let the HUD print the vertical gap between the road profile and the ground under the cursor ("road 4.1 m above ground") so the player knows why; (b) record in `docs/ACCEPTANCE.md` that "finished road look" is not accepted until earthworks. Everything else (calling `earthworks.cpp` after each commit/undo and rebuilding the affected ground tiles) is the earthworks item.

# For the next Unreal builder

## Belongs to milestone 2 (fix before calling the road tool done)
1. `UCPToolHudFormat::BuildLines`: when the pointer is on the last placed point (`bWouldFinish`), still print the span / bearing / R / grade / total readouts of the held ribbons (s26). Add "G2" only after the sim_core merge.
2. Cue text: when `PointCount() < 2` and `bWouldFinish`, do not print "Click again here to finish the road"; print "Click to add the next point" (`CPToolHudFormat.cpp` line ~108).
3. Refused crossing: show the red ghost ribbon for the stroke the refusal came from (s28) and keep the text; today only text and a ring are visible. Decide where: `UCPRoadDrawTool::OnClick` / `Feedback.Preview`.
4. Open-stroke undo: decide and implement (cancel the stroke on Ctrl+Z, or undo the last point first, or keep parity with the prototype and say so in `docs/`); add a playtest check.
5. Replace the "!" and "X" prefixes with the prototype's glyphs if the font has them; otherwise log that the font lacks them.
6. Investigate the max-grade readout on flat ground (s04, s14, s18-s22): print `Edge` grade samples for the s18 stroke and compare with a hand computation; fix or explain.
7. Strengthen the harness (`CPPlaytestRoads.cpp`, `CPPlaytestRoadStates.cpp`, `CPPlaytestRoadSteps.h`):
   - add `AssertHud(Label, Contains...)` and assert, per state, the snap word ("snap: end", "snap: road", "snap: guide", "snap: angle", "snap: free"), the refusal texts ("segment shorter than 4 m", "too tight", "crossing too close"), the advisory words, and "single carriageway" / "one-way" in the header;
   - "Esc cancels": assert `PointCount() == 0` (add an accessor on `UCPPlayerController` / tool) instead of unchanged counts;
   - "A drawn" etc.: also assert a road surface mesh exists (count of vertices or sections from `UCPRenderSubsystem`) and that the first and last control points are at the clicked XY within 0.5 m;
   - "everything undone": assert the exact number of commands undone (the undo stack depth), not 8 blind presses;
   - add scenarios: undo during an open stroke; Enter and click on a one-point stroke; shallow-angle crossing; road over a slope; ghost over a hill (check the ghost material has depth test off and take a picture over the terrain edge).
8. Make the playtest print one line "MOUSE INPUT LOST: rerun" when the early M1 checks all fail together, and auto-rerun once; today it prints 14 separate failures.
9. Ghost through hills picture (s16/s17 on real terrain, as in the prototype 16/17) and a refusal picture for a real over-water bridge advisory.

## Belongs to milestone 3 (prototype UI parity)
1. Profile panel (bottom strip: blue fill, orange cut, red over the wall cap, yellow vertical curves, grade labels), hidden with P; needs the sim_core merge for the profile series.
2. G2 tick, total ribbon width for the ghost (GAP-9), Tuning panel (snap angle, snap length, wall cap, wall start, batter, vertical-curve K, min segment, water level), scene bar and J connectors, perf panel (F), help overlay (H), colour modes (C: plain / curvature / gradient), edit mode (Tab: hover, select, handles, drag, delete, PgUp/PgDn on a handle).
3. Prototype states 01, 23-25, 29-36, 43-44 have no game picture; build a game picture for each at the prototype's framing.
4. Offset-guide shape: smooth parallel arc (check the sim guide's drape step and the Unreal line drawing against prototype 08 and 08b).
5. Replace the debug line-batch overlay (does not draw in Shipping) with a real overlay.

## Belongs to earthworks / ground
1. Call `sim_core/src/earthworks.cpp` after every committed, undone or redone road and rebuild the touched ground tiles (`CPGroundLayer`, `BuildTerrainTile`); the prototype's rule set is `prototypes/road-draw/src/earthworks.js` (batter 2 H per V, wall start 12 m, wall cap 12 m, bridges for fill over the cap, tunnels for cut over the cap).
2. Decide the road profile against the ground: today the profile through the click points ignores the terrain between points (road A floats 4 m in the middle). Either add profile control points at intervals (so the vertical curve follows the ground) or let earthworks regrade to the profile; the prototype regrades.
3. Bridges, viaducts, tunnel portals and cut/fill walls (prototype 37-42).
4. Terrain and sky look (prototype terrain colours, edge, water), lighting so a floating or buried road is visible in review pictures.
5. Add a check that reads the vertical gap between road surface and ground along each committed road (max and min, in metres) so "asphalt under terrain" becomes a number, not a picture.
