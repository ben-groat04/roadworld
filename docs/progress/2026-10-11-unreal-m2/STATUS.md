# Unreal M2 status: click-click road with undo/redo, drawn through the render-layer structure

Branch `agent/unreal-m2` (worktree `../roadworld-wt-m2`), from 269fa1e. Not pushed, not merged.

## Skipped / not done / not verified (read first)

- NOTHING WAS RUN. The editor was not launched, no automation test and no playtest was executed (told not to). Build result is in "Build" below. Every behaviour claim here is from code reading, sim_core headers and engine headers, not from a run.
- Not rendered / not judged visually: roads, markings, ghost, snap ring, guides, HUD. No screenshots exist for M2.
- Ghost ribbon: the prototype draws it with depth test off at 55% opacity. Here it is a depth-tested translucent mesh (engine material `/Engine/EngineDebugMaterials/M_SimpleUnlitTranslucent`, vector parameter `Color`). I verified from the .uasset name table that the material is translucent/unlit and has a `Color` parameter; I did NOT verify that the parameter's alpha drives opacity. If the ghost is opaque, the fix is a project material (needs the editor).
- Overlay lines/rings/dots use `ULineBatchComponent` (engine debug-draw class): they do not draw in Shipping builds (`UE_ENABLE_DEBUG_DRAWING`, `LineBatchComponent.cpp` GetDynamicMeshElements). Fine for PIE (plan: PIE through M7); replace before M8.
- Esc is bound as "cancel stroke" but in play-in-editor Esc normally stops PIE. Backspace (remove last point) and right-click (with fewer than 2 points: cancel) also cancel, so nothing is lost; check what Esc does in PIE.
- Key 1-4 -> road type uses `UCPSettings::RoadTypePalette` (default residential, b_road, a_road_dual, motorway) because the region pack has no tool palette (see sim_core gaps).
- No Unreal-side unit test of `CPToolHudFormat` or `FCPClickGesture` (they are pure functions, easy to add later).
- HUD omits the prototype's `G2 ✓` / `G1` flag (GAP-6), colour mode, profile panel, help panel, type picker (all later milestones).
- World-cover/vegetation sim layers are registered (as asked) and built once at world start (`UCPSettings::bRegisterVegetationLayers`, default true). I did not measure how long that first build takes on the 2 km world; the log line `Roads ready: ... first world-cache build N ms` reports it. If it is slow, turn the setting off.

## Build

(filled in at the end)

## Classes added

(filled in at the end)
