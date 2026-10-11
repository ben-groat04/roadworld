# Unreal milestone 3: the prototype's road tool UI in the game (profile panel, readouts, warnings, full-width ghost)

## Owner's words
11 Oct: "remember a lot of these features were in the inital html, that ui was sort of what i want, that was the point of it"
11 Oct: "Screenshots. It opened only 3 of the prototype screenshots I listed and took the rest from the written spec. disappointing"
11 Oct: "there's so much waiting"
11 Oct: "i will never open it, this takes too long. just mess around with it yourself to test it (ideally in an agent)"

## Cases
The browser prototype is the UI spec. Every screenshot below is a case: open ALL of them (M = 45 screenshots + 5 documents = 50), and produce the same state in the game with real mouse and keys, shot at the same framing:

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
Start: worktree C:\Users\bgbg0\Documents\Programming\roadworld-wt-m3ui, branch agent/m3ui, cut from agent/unreal-m2 (milestone 2, played; sim_core there already holds the road tool data: ProfileSeries, CrossingReport, EdgeAnalysis, RoadWidths, EditSession). Another Unreal builder (sandbox look) owns ground, sky, light and materials: you own the game module's tool, overlay, HUD and widget files and the playtest state script. Do not change ground/sky materials or render layers, and no sim_core junction/grading code. If the milestone 2 auditor's file C:\Users\bgbg0\Documents\Programming\roadworld-wt-m2\docs\progress\2026-10-11-unreal-m2-run\AUDIT.md exists, read its section "For the next Unreal builder" and take the milestone 2 and 3 items.
UNREAL LANE (one Unreal build, editor or -game process at a time on this machine, several agents share it): before any UBT build, editor or game launch, take the lock by creating the directory C:\Users\bgbg0\Documents\Programming\roadworld\.claude\state\unreal_lane.lock (mkdir fails if it exists: then wait and retry every 60 s; also wait while any UnrealEditor, UnrealBuildTool or dotnet UBT process that you did not start is running), write a file owner.txt inside with your branch name and the time, and remove the directory as soon as your build or run ends, every time, including on failure. Never hold it while you are only editing or thinking. If it is older than 60 minutes and no Unreal process runs, say so in STATUS.md, then take it. Build and run commands: unreal\tools\README.md.

## Faults
Gaps after milestone 2 (the screenshots decide, not this list):
1. No profile panel: the height profile along the stroke (ground against road, fill and cut colours, wall cap, vertical curves, gradient labels, the highlighted span under the cursor), as the bottom panel in the screenshots.
2. Ghost shows carriageway width, not the total width with verges; dual carriageway ghost, G2 tick, colour modes.
3. Warning and error glyphs print as "!" and "X": use the real glyphs with a font that has them; text, order and colours of every HUD line as the screenshots.
4. Tuning panel, help overlay, inspect bar: as the screenshots, wired to the sim_core fields that exist; list fields that do not exist yet instead of inventing them.
5. Edit tool (select, drag points, delete, per-road HUD line) through EditSession: build the unbuilt wrapper Wrap_edit_tool.cpp and the UCPTool for it.
6. Overlay rings, dots and guides use ULineBatchComponent, which will not draw in Shipping: note only, do not fix now.
Playtest: extend the scripted states so every screenshot has a game picture from real input; composites (game | prototype) for all of them; milestone 1 and 2 checks must still pass (about 2 in 9 runs lose all mouse input: rerun once before believing a failure). Research every Unreal fact from C:\UE_5.8 source or docs, never from memory.

## Launched
This and the sandbox look builder (both need the Unreal lane: share it by the lock). Also running: roundabout, T-junction and slip road rounds, grading world layer, road tool follow-up (sim_core side of the remaining UI gaps), earthworks round 9, integration, audit gate tooling, matched reference set, two free asset searches, milestone 2 auditor.

## Waiting
Heat-map road colouring and whole-road selection in Unreal: wait for the road tool follow-up to land in sim_core.
