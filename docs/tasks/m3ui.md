# Unreal milestone 3: the prototype's road tool UI in the game (profile panel, readouts, warnings, full-width ghost)

## Owner's words
11 Oct: "remember a lot of these features were in the inital html, that ui was sort of what i want, that was the point of it"
11 Oct: "Screenshots. It opened only 3 of the prototype screenshots I listed and took the rest from the written spec. disappointing"
11 Oct: "there's so much waiting"
11 Oct: "i will never open it, this takes too long. just mess around with it yourself to test it (ideally in an agent)"

## Cases
The browser prototype is the UI spec. Every screenshot below is a case: open ALL of them (M = 45 screenshots + 6 documents = 51), and produce the same state in the game with real mouse and keys, shot at the same framing:

- C:\Users\bgbg0\Documents\Programming\roadworld\docs\progress\2026-10-11-prototype-ui\01_idle_help.png
- C:\Users\bgbg0\Documents\Programming\roadworld\docs\progress\2026-10-11-prototype-ui\02_idle.png
- C:\Users\bgbg0\Documents\Programming\roadworld\docs\progress\2026-10-11-prototype-ui\03_hover_free.png
- C:\Users\bgbg0\Documents\Programming\roadworld\docs\progress\2026-10-11-prototype-ui\04_midstroke.png
- C:\Users\bgbg0\Documents\Programming\roadworld\docs\progress\2026-10-11-prototype-ui\05_snap_end.png
- C:\Users\bgbg0\Documents\Programming\roadworld\docs\progress\2026-10-11-prototype-ui\06_snap_road.png
- C:\Users\bgbg0\Documents\Programming\roadworld\docs\progress\2026-10-11-prototype-ui\07_snap_extension_guide.png
- C:\Users\bgbg0\Documents\Programming\roadworld\docs\progress\2026-10-11-prototype-ui\08_snap_parallel_offset_guide.png
- C:\Users\bgbg0\Documents\Programming\roadworld\docs\progress\2026-10-11-prototype-ui\08b_snap_offset_guide_3x.png
- C:\Users\bgbg0\Documents\Programming\roadworld\docs\progress\2026-10-11-prototype-ui\09_hover_free_second.png
- C:\Users\bgbg0\Documents\Programming\roadworld\docs\progress\2026-10-11-prototype-ui\10_snap_angle_length.png
- C:\Users\bgbg0\Documents\Programming\roadworld\docs\progress\2026-10-11-prototype-ui\11_shift_snap_off.png
- C:\Users\bgbg0\Documents\Programming\roadworld\docs\progress\2026-10-11-prototype-ui\12_error_min_span.png
- C:\Users\bgbg0\Documents\Programming\roadworld\docs\progress\2026-10-11-prototype-ui\13_error_too_tight.png
- C:\Users\bgbg0\Documents\Programming\roadworld\docs\progress\2026-10-11-prototype-ui\14_advisory_radius.png
- C:\Users\bgbg0\Documents\Programming\roadworld\docs\progress\2026-10-11-prototype-ui\15_advisory_grade.png
- C:\Users\bgbg0\Documents\Programming\roadworld\docs\progress\2026-10-11-prototype-ui\16_advisory_bridge.png
- C:\Users\bgbg0\Documents\Programming\roadworld\docs\progress\2026-10-11-prototype-ui\17_advisory_tunnel.png
- C:\Users\bgbg0\Documents\Programming\roadworld\docs\progress\2026-10-11-prototype-ui\18_type3_dual_ghost.png
- C:\Users\bgbg0\Documents\Programming\roadworld\docs\progress\2026-10-11-prototype-ui\19_type4_motorway_ghost.png
- C:\Users\bgbg0\Documents\Programming\roadworld\docs\progress\2026-10-11-prototype-ui\20_type3_single_carriageway_O.png
- C:\Users\bgbg0\Documents\Programming\roadworld\docs\progress\2026-10-11-prototype-ui\21_type1_oneway.png
- C:\Users\bgbg0\Documents\Programming\roadworld\docs\progress\2026-10-11-prototype-ui\22_type2_b_road_ghost.png
- C:\Users\bgbg0\Documents\Programming\roadworld\docs\progress\2026-10-11-prototype-ui\23_tuning_panel_open.png
- C:\Users\bgbg0\Documents\Programming\roadworld\docs\progress\2026-10-11-prototype-ui\24_perf_panel_F.png
- C:\Users\bgbg0\Documents\Programming\roadworld\docs\progress\2026-10-11-prototype-ui\25_profile_hidden_P.png
- C:\Users\bgbg0\Documents\Programming\roadworld\docs\progress\2026-10-11-prototype-ui\26_stroke_3pts_before_finish.png
- C:\Users\bgbg0\Documents\Programming\roadworld\docs\progress\2026-10-11-prototype-ui\27_finished_road_idle.png
- C:\Users\bgbg0\Documents\Programming\roadworld\docs\progress\2026-10-11-prototype-ui\28_error_crossing_too_close.png
- C:\Users\bgbg0\Documents\Programming\roadworld\docs\progress\2026-10-11-prototype-ui\29_colour_curvature.png
- C:\Users\bgbg0\Documents\Programming\roadworld\docs\progress\2026-10-11-prototype-ui\30_colour_gradient.png
- C:\Users\bgbg0\Documents\Programming\roadworld\docs\progress\2026-10-11-prototype-ui\31_edit_hover_road.png
- C:\Users\bgbg0\Documents\Programming\roadworld\docs\progress\2026-10-11-prototype-ui\32_edit_selected_handles.png
- C:\Users\bgbg0\Documents\Programming\roadworld\docs\progress\2026-10-11-prototype-ui\33_edit_handle_hover.png
- C:\Users\bgbg0\Documents\Programming\roadworld\docs\progress\2026-10-11-prototype-ui\34_edit_handle_dragging.png
- C:\Users\bgbg0\Documents\Programming\roadworld\docs\progress\2026-10-11-prototype-ui\35_edit_after_drag.png
- C:\Users\bgbg0\Documents\Programming\roadworld\docs\progress\2026-10-11-prototype-ui\36_edit_after_delete.png
- C:\Users\bgbg0\Documents\Programming\roadworld\docs\progress\2026-10-11-prototype-ui\37_finished_bridge_viaduct.png
- C:\Users\bgbg0\Documents\Programming\roadworld\docs\progress\2026-10-11-prototype-ui\38_finished_tunnel_portal.png
- C:\Users\bgbg0\Documents\Programming\roadworld\docs\progress\2026-10-11-prototype-ui\39_finished_tunnel_portal_side.png
- C:\Users\bgbg0\Documents\Programming\roadworld\docs\progress\2026-10-11-prototype-ui\40_finished_cut_fill_walls.png
- C:\Users\bgbg0\Documents\Programming\roadworld\docs\progress\2026-10-11-prototype-ui\41_edit_select_viaduct_profile.png
- C:\Users\bgbg0\Documents\Programming\roadworld\docs\progress\2026-10-11-prototype-ui\42_edit_select_tunnel_profile.png
- C:\Users\bgbg0\Documents\Programming\roadworld\docs\progress\2026-10-11-prototype-ui\43_inspect_scene_loaded_connectors.png
- C:\Users\bgbg0\Documents\Programming\roadworld\docs\progress\2026-10-11-prototype-ui\44_inspect_connectors_off.png

- C:\Users\bgbg0\Documents\Programming\roadworld-wt-m2\docs\progress\2026-10-11-unreal-m2-run\STATUS.md (what milestone 2 did and its list of gaps left for this milestone)
- C:\Users\bgbg0\Documents\Programming\roadworld-wt-m2\docs\progress\2026-10-11-unreal-m2-run\COMPARE.md (open its composites in compare\ too)
- C:\Users\bgbg0\Documents\Programming\roadworld\docs\unreal-game-plan.md (milestone 3 scope)
- C:\Users\bgbg0\Documents\Programming\roadworld\docs\unreal-ui-parity.md
- C:\Users\bgbg0\Documents\Programming\roadworld\prototypes\road-draw\src (the prototype source: open the files that draw each panel you port and name them)
- C:\Users\bgbg0\Documents\Programming\roadworld-wt-roadtool3\docs\progress\roadtool3\AUDIT.md (the road tool follow-up audit, verdict accept; its section "For the Unreal M3 UI builder" has 9 items, copied into Faults below; open the whole file, and read its engine API notes: `cp::draw_hud`, `cp::edit_hud`, `ui_theme()`, `ProfileSeries`, `EditSession`, `tuning_fields()`)
Start (updated 22 Oct): worktree C:\Users\bgbg0\Documents\Programming\roadworld-wt\m3ui, branch agent/m3ui, cut from current main AFTER the roadtool3 merge (`merge/roadtool3`, it brings the engine side of the road tool: ProfileSeries, CrossingReport, EdgeAnalysis, RoadWidths, EditSession, draw_hud, edit_hud, ui_theme) and, if it has merged by then, after the M2 fix job (`docs/tasks/m2fix.md`, same tool, HUD and playtest files). The old worktree `..\roadworld-wt-m3ui` (62368f3, no commits of its own) is stale: remove it. Another Unreal builder (sandbox look) owns ground, sky, light and materials: you own the game module's tool, overlay, HUD and widget files and the playtest state script. Do not change ground/sky materials or render layers, and no sim_core junction/grading code. Read the milestone 2 auditor's file C:\Users\bgbg0\Documents\Programming\roadworld-wt-m2\docs\progress\2026-10-11-unreal-m2-run\AUDIT.md, section "For the next Unreal builder": its 9 items "Belongs to milestone 2" go to the m2fix job, not to you; you take its "Belongs to milestone 3" items 1 to 5 (they are the screenshot cases below). If m2fix has not merged when you start, say so in STATUS.md and do not edit the same lines it edits.
UNREAL LANE (one Unreal build, editor or -game process at a time on this machine, several agents share it): before any UBT build, editor or game launch, take the lock by creating the directory C:\Users\bgbg0\Documents\Programming\roadworld\.claude\state\unreal_lane.lock (mkdir fails if it exists: then wait and retry every 60 s; also wait while any UnrealEditor, UnrealBuildTool or dotnet UBT process that you did not start is running), write a file owner.txt inside with your branch name and the time, and remove the directory as soon as your build or run ends, every time, including on failure. Never hold it while you are only editing or thinking. If it is older than 60 minutes and no Unreal process runs, say so in STATUS.md, then take it. Build and run commands: unreal\tools\README.md.

## Faults
Gaps after milestone 2 (the screenshots decide, not this list):
1. No profile panel: the height profile along the stroke (ground against road, fill and cut colours, wall cap, vertical curves, gradient labels, the highlighted span under the cursor), as the bottom panel in the screenshots.
2. Ghost shows carriageway width, not the total width with verges; dual carriageway ghost, G2 tick, colour modes.
3. Warning and error glyphs print as "!" and "X": use the real glyphs with a font that has them; text, order and colours of every HUD line as the screenshots.
4. Tuning panel, help overlay, inspect bar: as the screenshots, wired to the sim_core fields that exist; list fields that do not exist yet instead of inventing them.
5. Edit tool (select, drag points, delete, per-road HUD line) through EditSession: build the unbuilt wrapper Wrap_edit_tool.cpp and the UCPTool for it.
6. Overlay rings, dots and guides use ULineBatchComponent, which will not draw in Shipping: note only, do not fix now.
7. From the road tool follow-up audit (roadtool3 AUDIT.md, "For the Unreal M3 UI builder", all 9 items, verbatim from the auditor):
   1. Use `cp::draw_hud` / `cp::edit_hud` for every HUD line; delete `CPToolHudFormat::BuildLines` in the M2 code. Draw runs (bold, amber, red) from `HudRun::bold` and `HudTone`.
   2. Use `ui_theme()` for ring colours and sizes (free 7 px white, others 11 px; end, node, road green; guide cyan), ghost colour (`ghost_colour`), dash lengths, handle sizes (7 px, hover 10 px gold), edit line colours.
   3. Drop the finish ring and the "click the last point" cue (or set `params().finish_pick_px = 10` deliberately and keep both). Finish by Enter, right-click (no drag) or double-click; Backspace `undo_point`; Esc `cancel`; PgUp/PgDn `adjust_height` (1 m, 5 m with Shift; in edit mode `adjust_point_height` on the hovered handle).
   4. Edit mode: highlight `hover_chain` only (not the picked node/point); selection `selection()`, handles `handles()`; on release `end_drag`, execute `command`, then `invalidate()` and, when `made_junction`, `select_at(anchor)`; after undo re-select the same way (fault 2 of that audit); drag threshold 4 px is the host's job.
   5. Profile panel: draw from `ProfileSeries` with `profile_axes` ticks, `profile_band` colours, `grades[].label` and `over_limit` amber, `curves` as yellow bands, `structures` as the over-cap region; empty text from `profile_empty_text`; show the hovered road's profile in edit mode, none after delete.
   6. Tuning panel: `tuning_fields()`; tool-side values through `apply_tuning`, region-side through `tuned_region` + `RoadGraph::set_region` + a regrade after 120 ms (150 ms for water).
   7. Heat-map road colouring: `heat_vertices` per edge; the host draws the mesh colours.
   8. Not provided by the engine, build in the host: frame stats (24), panel toggle (25), connector overlay and the inspect scene loader counts (43, 44), bridge/tunnel/wall meshes (37-40).
   9. Pass `surface_hit` for bridge decks, and `metres_per_pixel` for every ray; without them pick radii in pixels are meaningless.
8. Dead M2 code to REMOVE (the engine now owns these behaviours; leaving them is a fault): the finish cue and the duplicate HUD formatter in `unreal/CityPainter/Source/CityPainter/Tools/CPToolHudFormat.cpp` (the whole `BuildLines` formatter once item 1 is done, including the "Click again here to finish the road" cue), the finish ring in `unreal/CityPainter/Plugins/CityPainterCore/Source/CityPainterRender/Private/CPOverlayComponent.cpp`, and the `would_finish` branch in `unreal/CityPainter/Plugins/CityPainterCore/Source/CityPainterSim/Private/CPToolSession.cpp`. After removal: `finish_pick_px` stays at the engine default 0 (finish = Enter, right-click without drag, double-click, or a snap onto a road or end), no dangling `bWouldFinish`/`would_finish` references anywhere (grep and show the grep), and the playtest checks that assumed "click the last point finishes" are rewritten to the new finishing keys with a note of which assertions changed. Check what m2fix changed in those same lines before you delete them.
Open design items from the roadtool3 merge notes (not owner questions, decide and record them): stroke id on Edge for exact road identity; whether the game enables finish-on-last-point (this brief says no); whether dragging a shared junction node makes junctions.
Playtest: extend the scripted states so every screenshot has a game picture from real input; composites (game | prototype) for all of them; milestone 1 and 2 checks must still pass (about 2 in 9 runs lose all mouse input: rerun once before believing a failure). Research every Unreal fact from C:\UE_5.8 source or docs, never from memory.

## Launched
This job (updated 22 Oct): not started; starts after the roadtool3 merge and the M2 fix job (`docs/tasks/m2fix.md`) because all three touch the same Unreal tool, HUD and playtest files and share the one Unreal lane. Running in other lanes meanwhile (22 Oct): roundabout2, tjunction2 and gates2 builders (engine, no Unreal), the roadtool3 merger; pvetrees has no agent running yet and also needs the Unreal lane.

## Waiting
Heat-map road colouring and whole-road selection in Unreal: the sim_core side (`heat_vertices`, `hover_chain`) is in the roadtool3 branch and arrives with its merge; it is part of this job (items 4 and 7 above) once merged.
