## Skipped, refused or failed

References opened: 26 of 26 listed (all 26 prototype screenshots the brief lists, opened this run; the 18 other prototype screenshots are not in this brief's list).

- FAILED, not fixable here (needs the earthworks queue item): on a sloped or undulating site a finished road's asphalt dips under the terrain mesh in places (no earthworks yet), so only kerbs and markings show and the asphalt appears to taper to a point at a free end. Pictures: renders/10_m2_06_t_snap_to_road.jpg (S-curve with asphalt missing), renders/20_st_s27b_finished_topdown.jpg, s27c_finished_low_oblique.jpg, s30_road_on_slope.jpg. The brief asked for a picture of this; it is not built here.
- NOT VERIFIED: Esc inside play-in-editor (the editor may bind Esc to stop PIE). Only -game was run; Esc cancels the stroke there (check "Esc cancels the open stroke" passes). The editor was not started (unreal-mcp did not connect; one Unreal process at a time).
- NOT VERIFIED: ghost depth test off over hills or water. M_Ghost sets disable_depth_test, but every ghost picture is over flat ground; no picture shows the ghost through a hill.
- NOT VERIFIED in Shipping: overlay rings, dots and guides use ULineBatchComponent (debug drawing). They draw in -game (every state picture) and will not in Shipping: note for M8.
- NOT DONE (outside M2, stay gaps): profile panel, tuning panel, scene/inspect bar, colour modes, G2 tick, total ribbon width (ghost is carriageway width, GAP-9), edit mode, bridge/tunnel structures. When the sim_core merge brings total width, G2 flag and profile series, the Unreal side uses them in UCPToolHudFormat::BuildLines (G2), UCPOverlayComponent::DrawRibbon (width) and a new profile widget.
- FLAKY HARNESS (known): 2 of 9 playtest runs lost all synthetic mouse input (one during milestone 1, one for everything). Reran; the final runs are clean.
- The ground in these pictures is the dull 2 km sandbox ground, not the prototype's bright terrain: world look is outside this milestone.
- sim_core logs "surface 'kerb': mixed tangent handedness in one section; using the majority" on curved roads at every commit (sim_core, not touched).

## Reference files opened

Case: ghost, snapping, guides and rings | Opened: C:/Users/bgbg0/Documents/Programming/roadworld/docs/progress/2026-10-11-prototype-ui/03_hover_free.png, C:/Users/bgbg0/Documents/Programming/roadworld/docs/progress/2026-10-11-prototype-ui/04_midstroke.png, C:/Users/bgbg0/Documents/Programming/roadworld/docs/progress/2026-10-11-prototype-ui/05_snap_end.png, C:/Users/bgbg0/Documents/Programming/roadworld/docs/progress/2026-10-11-prototype-ui/06_snap_road.png, C:/Users/bgbg0/Documents/Programming/roadworld/docs/progress/2026-10-11-prototype-ui/07_snap_extension_guide.png, C:/Users/bgbg0/Documents/Programming/roadworld/docs/progress/2026-10-11-prototype-ui/08_snap_parallel_offset_guide.png, C:/Users/bgbg0/Documents/Programming/roadworld/docs/progress/2026-10-11-prototype-ui/08b_snap_offset_guide_3x.png, C:/Users/bgbg0/Documents/Programming/roadworld/docs/progress/2026-10-11-prototype-ui/09_hover_free_second.png, C:/Users/bgbg0/Documents/Programming/roadworld/docs/progress/2026-10-11-prototype-ui/10_snap_angle_length.png, C:/Users/bgbg0/Documents/Programming/roadworld/docs/progress/2026-10-11-prototype-ui/11_shift_snap_off.png
Case: errors and advisories | Opened: C:/Users/bgbg0/Documents/Programming/roadworld/docs/progress/2026-10-11-prototype-ui/12_error_min_span.png, C:/Users/bgbg0/Documents/Programming/roadworld/docs/progress/2026-10-11-prototype-ui/13_error_too_tight.png, C:/Users/bgbg0/Documents/Programming/roadworld/docs/progress/2026-10-11-prototype-ui/14_advisory_radius.png, C:/Users/bgbg0/Documents/Programming/roadworld/docs/progress/2026-10-11-prototype-ui/15_advisory_grade.png, C:/Users/bgbg0/Documents/Programming/roadworld/docs/progress/2026-10-11-prototype-ui/16_advisory_bridge.png, C:/Users/bgbg0/Documents/Programming/roadworld/docs/progress/2026-10-11-prototype-ui/17_advisory_tunnel.png, C:/Users/bgbg0/Documents/Programming/roadworld/docs/progress/2026-10-11-prototype-ui/28_error_crossing_too_close.png
Case: road types, dual, one-way | Opened: C:/Users/bgbg0/Documents/Programming/roadworld/docs/progress/2026-10-11-prototype-ui/18_type3_dual_ghost.png, C:/Users/bgbg0/Documents/Programming/roadworld/docs/progress/2026-10-11-prototype-ui/19_type4_motorway_ghost.png, C:/Users/bgbg0/Documents/Programming/roadworld/docs/progress/2026-10-11-prototype-ui/20_type3_single_carriageway_O.png, C:/Users/bgbg0/Documents/Programming/roadworld/docs/progress/2026-10-11-prototype-ui/21_type1_oneway.png, C:/Users/bgbg0/Documents/Programming/roadworld/docs/progress/2026-10-11-prototype-ui/22_type2_b_road_ghost.png
Case: finishing and finished road | Opened: C:/Users/bgbg0/Documents/Programming/roadworld/docs/progress/2026-10-11-prototype-ui/02_idle.png, C:/Users/bgbg0/Documents/Programming/roadworld/docs/progress/2026-10-11-prototype-ui/25_profile_hidden_P.png, C:/Users/bgbg0/Documents/Programming/roadworld/docs/progress/2026-10-11-prototype-ui/26_stroke_3pts_before_finish.png, C:/Users/bgbg0/Documents/Programming/roadworld/docs/progress/2026-10-11-prototype-ui/27_finished_road_idle.png

## Own fault list and residual

Full passes in FAULTS.md. Fixed this run: ghost was near-black (now pale, red on error), two overlapping HUD panels, oversized HUD text, ghost lost after a click, infinity printed as "inf". Residual: asphalt under terrain (earthworks), PIE Esc, ghost through hills, Shipping overlay, the listed gaps.

## Changes (file:function)

- Plugins/CityPainterCore/Source/CityPainterRender/Public/CPRenderSettings.h: GhostMaterial default now /Game/CityPainter/Materials/M_Ghost.
- unreal/tools/py/make_ghost_material.py (new) and Content/CityPainter/Materials/M_Ghost.uasset (new): unlit translucent, depth test off, emissive = Color.rgb x Brightness 20000 (fixed EV100 14 makes emissive 1 black), opacity = Color.a.
- Plugins/.../CityPainterSim/Private/CPToolSession.cpp: FCPToolSession::Click keeps the held ribbons after an added point.
- Source/CityPainter/CPHudWidget.cpp: M1 readout moved bottom-left, font 12. Tools/CPToolHudWidget.cpp: tool HUD at top-left (16,16), font 14, cue font 13. Tools/CPToolHudFormat.cpp: FormatRadius prints the infinity sign.
- CPPlayerController.{h,cpp}: GetToolHudText. Playtest: Playtest/CPPlaytestRoadSteps.h (shared builder, new), CPPlaytestRoadStates.cpp (every state, new), CPPlaytestRoads.cpp (uses the shared builder), CPPlaytestScript.cpp (flags -CPPlaytestM2Only, -CPPlaytestStates). unreal/tools/Compose-M2States.ps1 (new): render | prototype composites.
- Merged main three times; no conflicts.

## Images that carry each claim

| Claim | Image file (opened by me) |
|---|---|
| Ghost is pale, red on error | compare/s13_error_too_tight_vs_proto.jpg, compare/s18_type3_dual_ghost_vs_proto.jpg; before: before/20_st_s13_error_too_tight.jpg |
| Snap kinds, guides, rings | compare/s06, s07, s08 composites |
| Min-span error, no ghost | compare/s12_error_min_span_vs_proto.jpg |
| Advisory and error text matches the prototype | compare/s04, s10, s17 composites, hud_text.txt |
| Whole-stroke ghost + finish ring | compare/s26_stroke_3pts_before_finish_vs_proto.jpg |
| Crossing refusal | compare/s28_error_crossing_too_close_vs_proto.jpg |
| T onto a road, undo | renders/10_m2_06_t_snap_to_road.jpg, 10_m2_09_undo2.jpg |
| Asphalt under terrain (residual) | renders/20_st_s27b_finished_topdown.jpg, s27c_finished_low_oblique.jpg, s30_road_on_slope.jpg |

## Suite and checks

- Build-Editor: succeeded (zero errors).
- Automation CityPainter.World: RoadEdit Success, Terrain Success.
- Full playtest (results_full.json; 1600x900, 1000x600 after the resize step): 51 checks, 0 failed = milestone 1 37/37 + milestone 2 scenarios 11/11 + 3 state checks. M2 states alone at 1600x900 (results_m2states.json): 15 checks, 0 failed.
- Graph counts, measured, equal the derived ones: A 2 nodes/1 edge, S 4/2, T 6/4 (A split), undo 4/2, 2/1, 0/0, redo 2/1, 4/2, right-click finish 6/3, right-drag rotates and does not finish, Esc cancels.
- Measured: first world-cache build 560 ms (vegetation layers on, no start-up stall; left on). Commit latency (submit, undo, redo) 19 to 72 ms over 15 edits, seen as one slow frame (async is milestone 4). HUD text for every state: hud_text.txt.

## Next step

Earthworks and structures as game-world layers (asphalt under terrain), then the merge that brings total ribbon width, G2 flag and profile series; profile panel; replace the ULineBatch overlay before M8.
