VERDICT: accept (with the faults below sent to the next builder; none blocks the merge, one needs a merge-time edit in the Unreal M2 code)

Auditor: a fresh agent, not the builder. Read-only on code, tests, data and builder documents; this file is the only change.

## Skipped, refused or failed (mine)

- SUITE NUMBERS NOT COMPLETE AT THE TIME OF WRITING. Clean builds in my own dirs (`build-audit-gcc`, `build-audit-msvc`) compiled with no error on both compilers. My first runs (gcc without optimisation, MSVC Debug) were so slow on the saturated machine (9 sim_core_tests processes from other audits at the same time) that I stopped them after about 90 minutes and rebuilt Release. The Release runs were still going when this was written; see "Suite numbers" for what finished. I do NOT claim 433/433 myself; the builder's figure is unconfirmed by me except for the 33 tool-UI cases (below).
- What I did run to completion: the 33 `test_tool_ui` cases (gcc, `-sf="*test_tool_ui*"`): 33 of 33 passed, 2496 assertions, 0 failed.
- No Unreal build, no editor, no 3D render (brief).
- `probe3` (fold threshold scan) finished for residential and b_road only (4 of 6 combinations); `a_road_dual` was cut by my own 300 s timeout.
- My first MSVC attempt wrote to `..\audit-msvc.log`, a file another auditor was also writing; it was clobbered. I re-ran with my own log names. No other agent's file was touched.

## Reference files opened: 51 of 52 listed (45 screenshots + gaps task + 5 prototype sources; `docs/unreal-ui-parity.md` NOT read in full: not opened, for lack of time)

All 45 screenshots in `C:\Users\bgbg0\Documents\Programming\roadworld\docs\progress\2026-10-11-prototype-ui\`, each opened one by one: 01_idle_help, 02_idle, 03_hover_free, 04_midstroke, 05_snap_end, 06_snap_road, 07_snap_extension_guide, 08_snap_parallel_offset_guide, 08b_snap_offset_guide_3x, 09_hover_free_second, 10_snap_angle_length, 11_shift_snap_off, 12_error_min_span, 13_error_too_tight, 14_advisory_radius, 15_advisory_grade, 16_advisory_bridge, 17_advisory_tunnel, 18_type3_dual_ghost, 19_type4_motorway_ghost, 20_type3_single_carriageway_O, 21_type1_oneway, 22_type2_b_road_ghost, 23_tuning_panel_open, 24_perf_panel_F, 25_profile_hidden_P, 26_stroke_3pts_before_finish, 27_finished_road_idle, 28_error_crossing_too_close, 29_colour_curvature, 30_colour_gradient, 31_edit_hover_road, 32_edit_selected_handles, 33_edit_handle_hover, 34_edit_handle_dragging, 35_edit_after_drag, 36_edit_after_delete, 37_finished_bridge_viaduct, 38_finished_tunnel_portal, 39_finished_tunnel_portal_side, 40_finished_cut_fill_walls, 41_edit_select_viaduct_profile, 42_edit_select_tunnel_profile, 43_inspect_scene_loaded_connectors, 44_inspect_connectors_off (.png).
Plus: `docs/tasks/roadtool3-gaps.md` (main checkout); in the worktree `docs/unreal-ui-parity.md` was NOT read in full (I read only what the diff touched; listed as not opened); prototype `prototypes/road-draw/src/input.js` (lines 60-425), `ui.js` (whole), `config.js`, `shell.html`, `roadmesh.js` (heat functions, grep), `inspect.js` (grep of the status string). Not opened: `docs/unreal-ui-parity.md` in full (time), `inspect.js` beyond the status line. That is 51 of 52 if the parity doc counts; named here rather than hidden.
No photo references apply (engine work, UI screenshots are the spec).

## 45-row table (written before I read FAULTS.md / STATUS.md)

Key: M = matches (engine call and a test pin the same words / thresholds); M* = matches with a stated difference; H = host or other agents' code, no tool-engine equivalent; scenario = the builder's renders/NN.txt uses a synthetic scenario (flat ground), so words and format match, digits differ from the screenshot unless said.

| # | Screenshot shows | sim_core call and test | Verdict |
|---|---|---|---|
| 01 | Help overlay, 14 rows, title, closing paragraph | `help_rows`, `help_title`, `help_paragraph`; "Help overlay" case. Text identical to shell.html (checked word by word) | M |
| 02 | Idle: header only, "Profile: draw a road..." text, inspect idle text | `draw_hud(no pick)`, `profile_empty_text`, `inspect_idle_text`; tested | M |
| 03 | Hover free: "cursor z 75.9 m . snap: free", small white ring | `draw_hud`, `ring_style(Free)` 7 px white; tested | M |
| 04 | 4-point stroke, span/bearing/R/min R/grade/max/G2/total, profile with +9.5/+7.0/+0.6 labels | `draw_hud`, `profile_title`, `ProfileSeries::grades`; "HUD (04,13)" and "Profile series" cases | M (scenario; test only checks 3 grade labels exist, sign and amber flag, not the values) |
| 05 | Snap end, green ring | `SnapKind::End`, `ring_style`; tested | M |
| 06 | Snap road, green ring | `SnapKind::Road`; tested | M |
| 07 | Extension guide, cyan dashed from the end to cursor | `SnapGuide::Line` start exactly at the end (x 200), not faint; tested | M |
| 08 | Offset guide 1x, no stroke started (HUD has no span line) | `SnapGuide::Offset`; test always starts a stroke first. I probed without a stroke: snap=Guide, type Offset, works | M (no test for the screenshot's own situation) |
| 08b | Offset guide 3x | same, k = 1..3 both sides, 4x refused; tested | M |
| 09 | Free hover after finishing | tested (ring white 7 px) | M |
| 10 | Angle+length snap, span 48.0, bearing 105, grade -2.5% | reproduced line for line with crafted ground | M (exact digits) |
| 11 | Shift: "snap off" amber, span 45.9, bearing 101 | reproduced line for line; Run `snap_off` tone Warning | M (exact digits) |
| 12 | Min span: red "segment shorter than 4 m", no ghost, no profile | `IssueCode::MinSpan`, ribbons empty, profile invalid, click refused, follows tuning value; tested | M |
| 13 | Fold error then radius advisory, exact words | `Fold`, `RadiusBelowMinimum`; order and words tested; ghost red | M (scenario: R 6.8 vs 6.4 m). My scan of 1017 cursor placements per type found fold fired only when min R < half-width (residential, b_road, one-way and two-way): no mismatch |
| 14 | Radius advisory for B road, amber, ghost not red | tested | M |
| 15 | Grade advisory "above advisory 10%" (whole number) | `trim1`; tested for 10, 8, 4, 6 | M |
| 16 | Bridge advisory, "(+25 m)" in cursor line | `BridgeNeeded`, `HudContext::height_offset`; tested | M |
| 17 | Tunnel advisory, "(-22 m)" | `TunnelNeeded`; tested | M |
| 18 | Dual A road ghost, two ribbons, advisories with "A road / dual c'way" | `ribbons.size()==2`, width 10.8 each, `short_name` from region data; tested. Ghost is two fitted roads, not the offset of one centreline (declared; pinned at 4 cm R 400, 0.5 m limit R 25) | M* |
| 19 | Motorway ghost, 17.45 each | tested | M* (same refit) |
| 20 | "single carriageway" bold, one ribbon | tested incl. bold run | M |
| 21 | one-way header | tested | M |
| 22 | B road ghost 10.8 | tested | M |
| 23 | Tuning panel: 8 labelled sliders, ranges, values 15 8 12 12 2 1 4 14 | `tuning_fields`, `default_tuning`, `clamped`, `apply_tuning`, `tuned_region`; tested. Effect on grading of wall cap/start/batter is not tested (only K and water reach a test) | M |
| 24 | Frame stats panel (F) | none | H |
| 25 | Profile hidden (P) | none (panel toggle) | H |
| 26 | 3 points placed, cursor on the last point, Enter finishes | default `finish_pick_px=0`: a click near the last point adds a point or the min-span error, `finish()` ends it; tested. The "finish:" line of renders/26 is a literal string, not computed | M |
| 27 | Finished road, end ring green | tested | M |
| 28 | Red ghost over a crossing, "crossing too close vertically" | `Crossing` error text identical to prototype; shown on hover and refuses the click (the prototype shows it only at finish: declared) | M* |
| 29 | Curvature colours | `heat_class_curvature` thresholds 0.5 / 1 / 2 x type radius, colours equal to roadmesh.js; tested | M |
| 30 | Gradient colours | `heat_class_gradient` 1.5 / 1 / 0.5 x limit; tested | M |
| 31 | Edit hover: header only, profile of the hovered road | `EditPreview.hover`, `profile` of hovered road, `hover_chain`; tested | M |
| 32 | Selected road, "#1 . 200 m", 3 handles | `EditSelection`, `handles()`; tested. `#N` is the lowest edge id of the chain, not creation order | M* |
| 33 | Handle hover, gold 10 px | `EditHoverKind::Handle`, `ui_theme` 10 px; tested | M |
| 34 | Dragging: live ghost and profile | `drag_to`: ghosts, ribbons, profile longer than 200 m, graph untouched; tested | M |
| 35 | After drag: HUD 206 m | tested (length > 200); terrain regrade is host | M |
| 36 | After delete: stale profile in the prototype | engine shows none (declared decision), undo brings it back; tested | M* |
| 37 | Bridge viaduct | tool gives `ProfileStructure`, advisories; the mesh is other agents' code | H |
| 38 | Tunnel portal | same | H |
| 39 | Portal side | same | H |
| 40 | Cut/fill walls | same | H |
| 41 | Select viaduct road, whole-road profile 450 m | `road_chain_of`, `join_profiles`, handles once at the junction; "Road chain" cases | M* (chain rule geometric; `#N`) |
| 42 | Select tunnel road #2 | same | M* |
| 43 | Inspect bar with loaded scene, "J connectors on" | `inspect_status_text`: the numbers are passed in by the caller, the test passes constants; no scene counting. The prototype also appends " . <warn>" when a scene has a warning: not in the engine | M* |
| 44 | Connectors off | same string with "off" | M* |

Counts: 29 M, 10 M*, 6 H (24, 25, 37-40). Rows with no test of their own situation: 24, 25, 37-40 (6), and 08 (works, untested). 0 rows where the engine contradicts a screenshot without it being declared.

## Measuring tools: do the renders tell the truth?

- Regenerated all 39 engine renders with `CP_TOOLUI_DUMP` on my own gcc build and compared byte for byte: 39 of 39 identical (the 6 missing ones are 24, 25, 37-40, which the builder says are not engine work). They are deterministic output of real engine calls, not hand-written.
- But two kinds of line inside them ARE literals written in the test, not engine output: 05/06/07/09 "ring: green, 11 px", 07 "guide: cyan dashed line from the road end (x 200)...", 26 "finish: Enter / right-click...", 27 "ring: green...", 08/08b the guide sentence, 14 "ghost colour: normal". Each is next to a CHECK that pins the real value, so nothing is false, but the render file is not evidence on those lines.
- Scenarios are synthetic (flat or planar ground, cursor placed by hand); only 10 and 11 reproduce the screenshot's digits. COMPARE.md says so (23 in FAULTS.md). 08/08b render shows a span line the screenshot does not have (a stroke is started first).
- The 43/44 "render" is `inspect_status_text` fed the screenshot's own numbers: a format test only.

## Fault list, most serious first

1. Unreal M2 code written against the old `finish_pick_px` (see "Merge notes" 4a): wrong cue text, dead code, the HUD there is a second implementation that differs from `cp::draw_hud`. Files: `unreal/CityPainter/Source/CityPainter/Tools/CPToolHudFormat.cpp:108-116`, `unreal/.../CityPainterRender/Private/CPOverlayComponent.cpp:258-263`, `unreal/.../CityPainterSim/Private/CPToolSession.cpp:146,156`.
2. Selection is lost on undo of a junction-making edit and nothing tests it: after the edit the host does `select_at(anchor)` (id 5 in my probe); undo restores edge 2, `selected()` is invalid; redo restores edge 5 (ids are stable, I checked nodes and edges after undo/redo) but only because `sel_` still holds 5. A host that stores the selection must re-select by `anchor` on undo as well; `EditResult` gives no anchor for the undo direction. `sim_core/src/edit_tool.cpp:632-703` (`commit_move`), test `tests/test_tool_ui.cpp:1168-1207` selects after execute and after undo by explicit `es.select(b)` only.
3. Chain rule is geometric, so the engine's "road" is not the user's stroke: two roads drawn separately that meet in a crossing and are straight within 25 degrees select, profile and DELETE as one road (prototype: two roads). Delete of the whole chain is one undo step but removes a road the user did not select. `sim_core/src/edit_tool.cpp:171-195` (`continuation`). Builder lists the reverse case (item 3 of Residual), not this one.
4. `fit_clothoid_chain` guard: accepts a non-finite coordinate. `{(0,0),(inf,0)}` returns a chain of length inf (NaN is refused, `inf <= 20*inf` is true). `sim_core/src/clothoid_chain.cpp` (`spans_sane`, `kMaxSpanRatio`). Also the 20x bound is loose: a sweep of 768 pinned-heading pairs (16 x 16 headings, chords 2, 10, 100 m) accepted all of them and the worst span was 14.9 times its chord; a bound near 8 would still hold the 1.7 to 2.4 seen for real turns. Not reachable from the tool through a ground pick (14 km limit) but reachable from layout loading.
5. Inspect bar text drops the prototype's trailing warning (" . ${i.warn}") and name fallback ("scene"); `sim_core/src/tool_hud.cpp` `inspect_status_text`. Low.
6. Region-neutrality, minor: `ToolTuning` (`sim_core/include/cp/tool_tuning.h`, wall_cap 12, wall_start 12, batter 2, water 14), `RoadToolRules` and `HeatMapRules` defaults in `region_pack.h`, and `default_tuning(nullptr)` hold the prototype's UK numbers as C++ fallbacks. They are overridden by `data/regions/uk/region.json`; the fallback is declared and tested (`default_tuning(nullptr) == t0`). `water_level` is a world setting stored in region data.
7. Tuning is applied by `RoadGraph::set_region` (refits every edge, outside the CommandStack). I probed `make_move_interior_point`: undo after a K change refits with the new K (correct). `SetOneWay` and change-type commands keep a copy of the whole edge (`edit_tool.cpp` `SetOneWay::revert`), so undoing them after a K change would put back an old-K edge. Not tested; low.
8. `EditSelection::number` is `int` from a 64-bit id (`edit_tool.cpp` `selection()`): wraps after 2^31 edges. Cosmetic.
9. Test text pins use `kFree`/synthetic ground for 04, so the profile grade label VALUES (+9.5 etc.) are not pinned, only count, sign and amber flag (`tests/test_tool_ui.cpp:1025-1044`).
10. `docs/unreal-ui-parity.md` says no warnings in this round's files; my MSVC build shows C4189 in `sim_core/src/grading.cpp(1776)` (another round's file). Not the builder's.

## Declared behaviour changes: what I found

(a) `finish_pick_px` default 0. Behaviour verified: probe: a click 1 m from the last point gives no point, no finish, one error (min span); a click 6 m away adds a point. Matches the prototype (screenshot 26 / 12). See merge notes.
(b) Edge id changes after a junction-making edit: verified by probe. First execution: edges 1,2 become 3,4,5 and nodes 5,6; undo restores 1,2 and nodes 3,4 exactly; redo restores 3,4,5 and 5,6 again; a different draw after an undo gets fresh ids (edge 2 and nodes 3,4 in my probe after undoing edge 1), none reused. Who holds an edge id across an edit: only `EditSession::sel_` in sim_core (see fault 2); the Unreal M2 code holds no edge id and has no edit tool yet (`grep EdgeId|EditSession|select_at` over `unreal/CityPainter/Source` and `Plugins` finds nothing outside the bridge types).
(c) `fit_clothoid_chain` runaway refusal: tested myself, 23 cases plus two sweeps. Refused: coincident points, a single point, exact back-track `(0,0),(100,0),(50,0)`, near back-track `(100,0)->(50,1)`, NaN. Accepted and sane (ratio length/polyline): 90 degree turns at 3 and 4 m chords (1.22), 180 degree hairpins at 3 and 6 m (1.19-1.23), closed loops (1.09), pinned U-turns at 2, 4, 10 m chords (1.7, scale independent), a 4-point 15 m radius ring (1.09), 1e7 m coordinates, a 1e-4 m kink, 1e-6 m first span. Problems: `inf` accepted (fault 4), loose bound (fault 4). `spans_sane` also returns false if `spans.size()+1 != pts.size()`; I found no input where that fires for a legitimate stroke.

## Merge notes (item 4)

A clean three-way merge exists: `git merge-tree` of HEAD and `claude/practical-ride-rrf8rx` (8e8a100) reports no conflicting file. The Unreal M2 code on main:

1. `CPToolHudFormat.cpp:108-110` ("Click again here to finish the road") and `:77`, `:116` ("Click the last point or press Enter to finish"): with `finish_pick_px = 0` `would_finish` is never true, so line 108 is dead and line 116 now tells the player something false (clicking the last point adds a point or shows the min-span error). Replace by "Press Enter or right-click to finish".
2. `CPOverlayComponent.cpp:258-263`: the gold finish ring around the last point is never drawn (needs `would_finish`). Dead code until the option is on.
3. `CPToolSession.cpp:146,156`: `bWouldFinish` is mapped, harmless; the `would_finish && !LastRibbons.empty()` branch is dead.
4. `CPPlaytestRoadStates.cpp:77` comment (zoom so the 2.5 m offset is outside the finish ring) is obsolete; the scenario (s12) still produces the min-span error. s26 finishes with Enter, so it still passes. No playtest step clicks the last point to finish (`CPPlaytestRoads.cpp` uses Enter, right-click and a snap onto a road).
5. `CPRoadDrawTool.cpp:94-103`: `!R.bAdded && !R.bFinished` with errors refuses with the first error: a click near the last point now refuses with the min-span red text. That is the prototype's behaviour.
6. Bigger: `CPToolHudFormat.cpp:48-95` is a second HUD formatter and differs from the new `cp::draw_hud`: no "colour: ..." in the header, "X" and "!" instead of the cross and warning glyphs, "grade %+.1f%%" (adds a plus the prototype does not print), no "G2 ✓ / G1", no snap-off bold/amber runs. At merge it should be deleted in favour of `draw_hud` (the builder's header comment says the same).
7. `ToCode(cp::IssueCode)` in `CPToolSession.cpp:42-57` has no case for `CrossingAtGrade`; it falls to `CannotFit`. Needed only when the edit tool lands.
8. New `DrawPreview::road_type_name`, `one_way`, `dual_type` make `Out.RoadTypeName` unnecessary on the Unreal side; not breaking.

## Rule review of the diff (item 3)

- Commands: every edit the new code can return is a `cp::Command` executed on the stack: `make_move_node`, `make_move_interior_point`, `make_delete_edge`, `make_change_road_type`, `SetOneWay`, composites, and "edit road: new junction" (`make_delete_edge` + `make_draw_stroke`, dry-run applied on a graph copy first so a refusal leaves the real graph untouched). Undo/redo ids verified by probe (above). Tool sessions never mutate the graph. Exception: tuning goes through `RoadGraph::set_region` (declared "not undoable").
- Ids: stable, never reused (probe). `EditSelection::number` is an `int` cast.
- Doubles: yes for positions, lengths, heights in the new code.
- Driving side: nothing new depends on it; the dual offset test uses the region's side.
- Region values: new numbers are in `data/regions/uk/region.json` (`road_tool`) and `road_types.json` (`short_name`), parsed with range checks. Fallbacks in C++: fault 6. Help and HUD English strings live in core (UI words, not region data).

## Suite numbers

- gcc 16.1 `-sf="*test_tool_ui*"` (unoptimised, my build): 33 cases passed, 2496 assertions, 0 failed.
- Full suite on gcc and MSVC: see the line appended below if it finished; otherwise NOT CONFIRMED BY ME (builder: 433 of 433 passed, 3490418 assertions, 2 failed = the known `should_fail` pier case, on both compilers).

## For the next builder

1. Add a stroke id (or road id) to `Edge` so `#N`, selection, profile and delete follow what the user drew; keep the geometric rule only as a fallback for loaded layouts (faults 3, builder residual 1 and 3).
2. Make undo of a junction-making edit restorable by the host: return an anchor for the undo direction, or let `EditSession` remember `anchor` and re-select by itself after `invalidate()`; add a test that executes, undoes, redoes and checks `selected()` each time.
3. Tighten `fit_clothoid_chain`: reject non-finite input first; pick the span bound from a measured distribution (real turns 1.0 to 2.4) instead of 20; add tests for `inf`, 1e-9 spans and the 16 x 16 pinned-heading sweep.
4. Add the missing tests: offset guide with no stroke started (08), a CommandStack test for `SetOneWay` / change-type undo after a K change, grade label VALUES in 04, wall cap / start / batter reaching the graded result through `tuned_region`.
5. Replace the literal lines in the dump (ring, guide, finish, ghost colour) by values read from the preview, so the render files are evidence.
6. Add the inspect warning suffix and the "scene" name fallback to `inspect_status_text`.
7. Move the prototype's number fallbacks in `ToolTuning` / `RoadToolRules` out of C++ (load them from the region pack only and fail loudly with no region).

## For the Unreal M3 UI builder

1. Use `cp::draw_hud` / `cp::edit_hud` for every HUD line; delete `CPToolHudFormat::BuildLines` in the M2 code. Draw runs (bold, amber, red) from `HudRun::bold` and `HudTone`.
2. Use `ui_theme()` for ring colours and sizes (free 7 px white, others 11 px; end, node, road green; guide cyan), ghost colour (`ghost_colour`), dash lengths, handle sizes (7 px, hover 10 px gold), edit line colours.
3. Drop the finish ring and the "click the last point" cue (or set `params().finish_pick_px = 10` deliberately and keep both). Finish by Enter, right-click (no drag) or double-click; Backspace `undo_point`; Esc `cancel`; PgUp/PgDn `adjust_height` (1 m, 5 m with Shift; in edit mode `adjust_point_height` on the hovered handle).
4. Edit mode: highlight `hover_chain` only (not the picked node/point); selection `selection()`, handles `handles()`; on release `end_drag`, execute `command`, then `invalidate()` and, when `made_junction`, `select_at(anchor)`; after undo re-select the same way (fault 2); drag threshold 4 px is the host's job.
5. Profile panel: draw from `ProfileSeries` with `profile_axes` ticks, `profile_band` colours, `grades[].label` and `over_limit` amber, `curves` as yellow bands, `structures` as the over-cap region; empty text from `profile_empty_text`; show the hovered road's profile in edit mode, none after delete.
6. Tuning panel: `tuning_fields()`; tool-side values through `apply_tuning`, region-side through `tuned_region` + `RoadGraph::set_region` + a regrade after 120 ms (150 ms for water).
7. Heat-map road colouring: `heat_vertices` per edge; the host draws the mesh colours.
8. Not provided by the engine, build in the host: frame stats (24), panel toggle (25), connector overlay and the inspect scene loader counts (43, 44), bridge/tunnel/wall meshes (37-40).
9. Pass `surface_hit` for bridge decks, and `metres_per_pixel` for every ray; without them pick radii in pixels are meaningless.
