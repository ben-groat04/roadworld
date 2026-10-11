# Unreal M2 status: click-click road with undo/redo, drawn through the render-layer structure

Branch `agent/unreal-m2` (worktree `../roadworld-wt-m2`), from 269fa1e. Not pushed, not merged.

## Skipped, not built, not run, not opened (read first)

- COMPILED: yes. `Build-Editor.ps1 -MaxParallelActions 2` on the worktree: `Result: Succeeded`, zero errors, zero warnings (log `unreal/CityPainter/Saved/Logs/build-20261011-161400.log`; the first full build (log 151724) had 2 real errors, both fixed: a non-literal `FString::Printf` format and a missing deleted copy constructor on an exported class holding a `TMap` of unique pointers; a third build fixed the `Tools/` include path).
- NOT RUN: no editor, no `-game`, no automation test, no playtest was executed (told not to: 15 GB RAM, other editor running). Every behaviour claim below is from code reading plus sim_core and engine headers, none from a run. No M2 screenshots exist.
- References opened: 26 of 44 listed in `docs/progress/2026-10-11-prototype-ui/` (main checkout). Opened: 02_idle, 03_hover_free, 04_midstroke, 05_snap_end, 06_snap_road, 07_snap_extension_guide, 08_snap_parallel_offset_guide, 08b_snap_offset_guide_3x, 09_hover_free_second, 10_snap_angle_length, 11_shift_snap_off, 12_error_min_span, 13_error_too_tight, 14_advisory_radius, 15_advisory_grade, 16_advisory_bridge, 17_advisory_tunnel, 18_type3_dual_ghost, 19_type4_motorway_ghost, 20_type3_single_carriageway_O, 21_type1_oneway, 22_type2_b_road_ghost, 25_profile_hidden_P, 26_stroke_3pts_before_finish, 27_finished_road_idle, 28_error_crossing_too_close.
  NOT opened (18): 01_idle_help, 23_tuning_panel_open, 24_perf_panel_F (help, tuning and frame-stats panels: not M2 by the parity spec); 29_colour_curvature, 30_colour_gradient (colour modes, after M5); 31 to 36 (edit mode, M7); 37 to 42 (bridge, tunnel, cut/fill, their profiles: M5); 43, 44 (inspect scene, after M8). Reason for all: the milestone plan puts them outside M2; not opened for lack of time, not because of a refusal.
- The prototype screenshots show the road ghost only in the prototype's own look; the Unreal ghost, ring, guides, dots, HUD and the road meshes have never been seen rendered. Visual parity is unverified.
- Ghost ribbon: prototype = depth test off, 55% opacity. Here a depth-tested translucent mesh with the engine material `/Engine/EngineDebugMaterials/M_SimpleUnlitTranslucent` (verified from its .uasset name table: translucent, unlit, vector parameter `Color`); NOT verified that its alpha drives opacity. If opaque: needs a project material (editor).
- Overlay rings, dashes and dots use `ULineBatchComponent`, which draws through the debug PDI (`UE_ENABLE_DEBUG_DRAWING`): fine in PIE and development, will not draw in Shipping. Replace before M8. Ring is three concentric circles, not a filled annulus; dash gaps measured per polyline.
- Esc is bound to "cancel stroke" but in play-in-editor Esc may stop PIE. Backspace and right-click with under two points also cancel.
- Keys 1-4 map through `UCPSettings::RoadTypePalette` (residential, b_road, a_road_dual, motorway) because the region pack has no tool palette (gap below).
- World-cover and vegetation sim layers are registered and built once at world start (`UCPSettings::bRegisterVegetationLayers`, default true). First-build time not measured; the log line `Roads ready: ... first world-cache build N ms` reports it. Turn the setting off if slow.
- No Unreal unit tests of `CPToolHudFormat` or `FCPClickGesture` (pure; easy to add).

## Prototype screenshots: what each shows and where M2 produces it

| File | Shows | Unreal M2 |
|---|---|---|
| 02_idle | HUD header only, profile hint, tuning button | Header line: `UCPToolHudFormat::BuildLines` (no `colour: plain`: not M2). Profile, tuning, scene bar: not in M2 (GAP-1, later) |
| 03_hover_free | small white ring, `cursor z .. snap: free` | `UCPOverlayComponent::DrawMarks` (7 px ring, white); HUD line 2 |
| 04_midstroke | pale ghost ribbon, gold dots, faint dashed angle line, ring, 4 HUD lines, profile | ribbon `DrawRibbon`; dots, faint guide, ring `DrawMarks`; HUD lines 1-4 (`G2 ✓` omitted, GAP-6); profile not M2 |
| 05_snap_end | green ring, `snap: end`, finished road with markings | ring colour `RingColour`; road + markings: `FCPRoadSurfaceLayer`, `FCPMarkingsLayer`; earthwork scar not M2 |
| 06_snap_road | green ring on centreline, `snap: road` | same |
| 07_snap_extension_guide | cyan dashed extension line, cyan ring, `snap: guide` | `DrawMarks` guide `Line` non-faint |
| 08, 08b | cyan dashed parallel-offset curve | guide `Offset` (same drawing) |
| 09_hover_free_second | free ring beside a road | ring |
| 10_snap_angle_length | rectangular ghost, white ring, faint dashed centre line | ribbon + faint guide; straight, from the sim's `ribbons` |
| 11_shift_snap_off | amber `snap off` in the header, `snap: free` | header: amber when Shift held (`FCPToolFeedback::bSnapOff`) |
| 12_error_min_span | red `segment shorter than 4 m`, no ghost, dot + ring only | red HUD error line; ghost absent only if the sim returns no ribbon for MinSpan (UNVERIFIED) |
| 13_error_too_tight | red ghost, red and amber text | `Preview.HasErrors()` picks the red material; text lines |
| 14, 15 | amber advisories, ghost stays white | advisory lines amber; `HasErrors` ignores advisories |
| 16, 17 | `(+25 m)` / `(-22 m)` offset, bridge/tunnel advisories | `cursor z .. (+N m)` from `HeightOffsetM`; PgUp/PgDn in `UCPRoadDrawTool::OnKey` |
| 18, 19 | dual and motorway: two ribbons | one ribbon per sim `Ribbon`; width is the carriageway, not `total` (GAP-9) |
| 20 | `single carriageway`, one ribbon | header text; `O` toggles one-way |
| 21, 22 | one-way, B road headers and ghosts | header text; type from keys 1-4 |
| 25_profile_hidden_P | HUD with no profile | profile panel is not in M2 |
| 26_stroke_3pts_before_finish | 3 clicked points, ghost to cursor, ring on the last point | ribbon through points; `bWouldFinish` gold ring |
| 27_finished_road_idle | finished road, asphalt, kerbs, markings, green end ring | road/marking layers after `Submit` |
| 28_error_crossing_too_close | red ghost on a refused finish | refusal text kept 5 s and the ghost stays red (`bRefused`); prototype lost the text, we keep it |

## My own fault list: what the owner would say differs from the pictures

Fixed in M2: HUD sits top-left like the prototype (moved below the M1 readout, was top-right); a refused finish keeps the red ghost and a red text line; the finish cue is a bar at the bottom ("Click the last point or press Enter to finish", prototype had none, E.1); the ghost is not lost when the cursor is on the last point (ribbons held from the previous hover).
Open, belongs to later milestones or needs the editor/sim:
1. No profile panel (GAP-1), no tuning panel, help overlay, frame stats, colour modes.
2. Ghost width is the carriageway (`Ribbon::width`), the prototype draws `total` width, so the Unreal ghost will look narrower (GAP-9).
3. Ghost may look more transparent or opaque than the prototype's pale 55% grey (material unverified); depth test is on, the prototype's is off (ghost hides behind a hill crest).
4. `✕`, `⚠`, `∞`, `✓` glyphs replaced by `X`, `!`, `inf`, nothing (font coverage not checked).
5. Ring is three thin concentric circles, not a solid annulus; dashes are line segments (no thickness control).
6. `G2 ✓/G1` missing (GAP-6). Bridge/tunnel advisories show text only (structures are M5).
7. Camera pan/zoom of the M1 pawn is untouched; hover re-runs when the camera moves (signature includes camera), as the prototype.

## Build and structure

Classes added (one responsibility each):
- Sim (no UObject; only this module includes `cp/`): `FCPToolSession` (wraps `cp::DrawSession`: hover/click/finish/cancel/undo-point/type/one-way/height, plain structs out, keeps the last ribbon visible), `FCPCommandHandle` (opaque move-only edit), `CPToolTypes.h` (pointer, preview, click and finish structs), `CPTileData.h` (`FCPLayerTile`, `FCPChangeSet`), `CPTileConvert` (cache tile snapshot -> plain tile; one table row per exported layer: road_mesh, markings; conventions copied from the legacy `ConvertBuffers`/marking code: UV1 and mask colour for surfaces, wear in colour R for markings), `CPSimCore.h` (single guarded include of cp headers). `FCPSimWorld` gained the graph, `CommandStack`, `WorldCache` (layers junctions, road_mesh, markings, street_lights built in; veg_corridor, world_cover, vegetation registered), `Submit/Undo/Redo/SnapshotAll/GraphStats/RoadTypes`; the synchronous cache update is the single private `RunCacheUpdate` (the M4 seam).
- World: `UCPWorldSubsystem` gained `Submit/Undo/Redo`, `GetToolSession`, `OnTilesChanged` (broadcasts the change set, logs each edit with counts and ms); `CPToolBridge` (the one place pointer and preview cross sim frame <-> UE frame); `UCPSettings` gained `RoadTypePalette`, `bRegisterVegetationLayers`; automation test `CityPainter.World.RoadEdit`.
- Render: `ICPRenderLayer::OnTilesChanged` (additive, default no-op), `ICPTileMesh::ClearSection` (additive), `FCPTileSectionLayer` (tile bookkeeping), `FCPRoadSurfaceLayer`, `FCPMarkingsLayer` (registered in the module startup), `FCPMaterialTable` (the ONE sim-key -> asset mapping: asphalt MI_RoadAsphalt, paving MI_Footway, kerb MI_Kerb, grass/island MI_Ground, markings MI_Marking_<id>), `UCPOverlayComponent` (ghost, ring, guides, dots, finish ring), `UCPRenderSubsystem` forwards edits and owns the overlay (catch-up by `SnapshotAll`). `FCPProcTileMesh` now passes UV1 and vertex colour (still the only file with PMC).
- Game (`Source/CityPainter/Tools/`): `UCPTool` (base: OnHover/OnClick/OnSecondaryClick/OnCancel/OnKey), `UCPRoadDrawTool`, `UCPToolManager` (routing, undo/redo, signature-gated hover, overlay and HUD push), `UCPToolActions` (Enhanced Input actions made in C++, `bConsumeInput=false` so the camera's right-mouse rotate still fires), `FCPClickGesture` (click vs drag, 4 px: left click places, right CLICK finishes, right DRAG is the camera's), `CPToolHudFormat` (spec text), `UCPToolHudWidget`. Playtest: `Playtest/CPPlaytestRoads.cpp`.

Dependency graph: Game (`CityPainter`) -> Render -> World -> Sim -> sim_core. Game includes Sim public headers (plain structs) but never `cp/`; PMC only in `CPProcTileMesh.cpp`; a new rendered system = one `CPTileConvert` table row + one `ICPRenderLayer` subclass + one registration line, no road file edited.

Engine headers verified (C:\UE_5.8\Engine): `Source/Runtime/Engine/Classes/Components/LineBatchComponent.h` and `Private/Components/LineBatchComponent.cpp` (DrawLine/DrawPoint/DrawCircle/Flush signatures, DebugPDI, lifetime 0 = persistent); `Plugins/Runtime/ProceduralMeshComponent/.../ProceduralMeshComponent.h` (10-argument `CreateMeshSection` with UV1 and FColor, `ClearMeshSection`, `GetNumSections`); `Plugins/EnhancedInput/.../EnhancedInputComponent.h` (`BindAction` with payload, `bConsumeInput` in `InputAction.h`); `Source/Runtime/InputCore/Classes/InputCoreTypes.h` (EKeys names); `Source/Runtime/Engine/Classes/GameFramework/PlayerController.h` (`IsInputKeyDown`, `GetMousePosition`, `DeprojectScreenPositionToWorld`); `Engine/Classes/Components/ActorComponent.h` (`OnComponentDestroyed`); `Engine/Public/Materials/MaterialInstanceDynamic.h`; `Engine/Public/Subsystems/SubsystemCollection.h` (`InitializeDependency` returns the typed pointer); `UMG/Public/Components/CanvasPanelSlot.h`, `VerticalBox.h`; `Core/Public/Math/Color.h` (`FromSRGBColor`). Engine content `Engine/Content/EngineDebugMaterials/M_SimpleUnlitTranslucent.uasset` (name table read).

## Files the other agent is editing that I touched

| File | Where | Why |
|---|---|---|
| `Source/CityPainter/CPPlayerController.h` | `class UCPToolManager;` forward decl, member `Tools` (blocks marked "M2 tools hook") | holds the tool manager |
| `Source/CityPainter/CPPlayerController.cpp` | include; end of `SetupInputComponent` (`Tools = UCPToolManager::Create(this, EIC)`); `BeginPlay` before `MaybeStart` (`Tools->Begin()`); end of `PlayerTick` (`Tools->Tick`) | four 1-3 line hooks |
| `Source/CityPainter/Playtest/CPPlaytestScript.cpp` | include `CPPlaytestRoads.h`; last line of `BuildCPPlaytestScript` calls `AppendCPPlaytestRoadScenarios(Out)` | M2 scenario lives in its own file |
| `Source/CityPainter/CityPainter.Build.cs` | `PrivateIncludePaths.Add(ModuleDirectory)` | files in `Tools/` include `Tools/...` |
| `ACPCameraPawn`, `UCPHudWidget`, `CPInputConfig` | NOT touched | the tool HUD is its own widget, tool input its own actions |

Also changed outside the game module (not known to be edited by the other agent): Render/World/Sim files listed above; `CPSettings.h`, `CPRenderSettings.h` (new properties).

## After merge: run and look for

1. `Build-Editor.ps1`, then `UnrealEditor-Cmd.exe CityPainter.uproject -ExecCmds="Automation RunTests CityPainter.World; Quit" -nullrhi -unattended`: expect `CityPainter.World.Terrain` and the new `CityPainter.World.RoadEdit` to pass (draw -> only tiles x,y in -1..0; road B far away touches none of A's tiles; undo removes exactly what the draw added; redo restores; new edit clears redo).
2. `unreal\tools\Run-Playtest.ps1`: new steps `10_m2_*.png` and checks "A: straight road..." through "Esc cancels the open stroke". Expected counts are derived, not measured: A 2 nodes/1 edge, S 4/2, T 6/4 (A split by the junction), undo to 4/2, 2/1, 0/0, redo 2/1, 4/2, right-click road 6/3. A failing count is a lead, read the log lines `CPPlaytest M2:` and `Edit '...'`.
3. Log: `Roads ready: .. first world-cache build N ms` (vegetation cost), `Edit 'submit ...': world vN, .. tiles +a ~b -c .. ms` (update latency; 190 ms was the plan's figure), `Material '...'` warnings (missing assets), `Ghost material ... not found`.
4. By eye: ghost opacity and colour (white, red on error only), ring size constant while zooming, dashed guides, finish ring on the last point, HUD text and bottom cue, roads and markings lit like the legacy RoadTest look (compare with the existing look-test screenshots), Z-fighting of markings at distance (3 mm lift), right click vs right drag, Esc in PIE.

## sim_core gaps hit (not edited)

- Region pack has no tool palette / hotkey list (keys 1-4 are a project setting here).
- `DrawPreview` returns no ribbon when `would_finish` (GAP-4); worked around in the session wrapper by holding the previous ribbons.
- `Ribbon::width` is carriageway width, prototype ghost width is `total` (GAP-9).
- `Readouts` has no G2/G1 flag (GAP-6); no profile series (GAP-1); `Crossing` only arrives from `finish()` (GAP-3), kept visible by the host for 5 s.
- Unknown: whether `DrawSession::hover` returns no ribbon on a MinSpan error (the prototype removes the ghost); if it does return one, the Unreal ghost will show red where the prototype shows none.
- `WorldCache` vegetation layers: time to build on the 2 km world unmeasured.
