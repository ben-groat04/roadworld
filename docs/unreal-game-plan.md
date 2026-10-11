> Adopted on 2026-10-11 as the plan for the Unreal game work. Owner defaults: prototype orbit camera; play-in-editor through M7 and packaged exe at M8; plain dark panels UI; 2 km seeded test world; flat ribbon ghost; plugin renamed CityPainterCore and the RoadTest level retired once road layers exist.
> Text below is the planning agent's final report, verbatim.


# City Painter: "road builder as an Unreal game" plan

Read-only; nothing changed, built or run. The `unreal-mcp` server failed to connect (ECONNREFUSED), so the live editor was not inspected.

## Inventory (basis for the plan)

**1. Prototype (`prototypes/road-draw/src/`)**

| Interaction | JS location | sim_core today |
|---|---|---|
| Click-point clothoid stroke, G2 fit, vertical profile | `geom.js`, `earthworks.js rebuild` | Exists: `fit_clothoid_chain` (`clothoid_chain.h`), `VerticalProfile::build`, `RoadGraph::refit` |
| Commit, undo/redo | `input.js exec/undo/redo, AddRoads` | Exists: `make_draw_road`, `CommandStack` |
| Delete, move handle, height of a point | `DeleteRoad, EditPoint, EditPts, commitHandleEdit` | Exists: `make_delete_edge`, `make_move_node`, `make_move_interior_point` |
| Road type 1-4, one-way (O) | key handler | Types in the region pack; `make_change_road_type` |
| Terrain ray pick, road pick, pixel-scaled radii | `rayTerrain, pick, nearestOnRoad` | **Missing** |
| Snapping: road end, on-road, end-extension guide, parallel-offset guide, angle/length; Shift off | `computeSnap` | **Missing** (JS only) |
| Preview ghost, errors (min span, fold, cannot fit), amber advisories (radius, grade, bridge/tunnel, G1 fallback), HUD readouts | `buildPreview, ghostMesh, renderHud` | Pieces only: `find_folds`, `RoadType::min_radius/advisory_max_grade`, `required_structures` |
| Auto-finish when snapping to an end/road | `startOrAddPoint` | **Missing** |
| Plan-crossing rule (dz < 1 m joins, 1-5.3 m refused, above clears) with pinned points | `planHits, conformRoad` | Split-at-crossing exists only inside the anonymous namespace of `sim_core/src/layout_io.cpp` (`build_layout`); `make_draw_road` attaches ends only |
| Dual type = two offset one-way carriageways | `finishRoad` | **Missing** as a command (`interchange_dual_offset` only) |
| PgUp/PgDn height offset | `adjustHeight` | **Missing** (tool state) |
| Auto bridge/tunnel, walls, graded ground | `earthworks.js` | Exists: `grading.h`, `structure_mesh.h`; not WorldCache layers |
| Terrain generator | `terrain.js genTerrain` | **Missing** (`Heightfield` is a bare grid) |
| Orbit/pan/zoom camera, profile panel, tuning panel | `scene.js`, `ui.js` | Host-side by nature |

The prototype has no roundabout tool, no bridge/tunnel tool and no junction creation: junctions appear only when `inspect.js` loads a sim_core export. sim_core is ahead there (`make_place_roundabout`, `check_place_roundabout`, `make_set_structure_span`).

**2. sim_core gaps for a live tool**
- No non-mutating preview path.
- No picking, snapping or ray queries.
- `WorldCache` has no ground or structures layer and holds no `Heightfield`. Layers today: `junctions`, `road_mesh`, `markings`, `street_lights`, plus `veg_corridor`, `world_cover`, `vegetation` via `add_vegetation_layers`.
- Grading has no dirty path (2.9 s whole-network preparation, `docs/sim-performance-baseline.md`).
- `VegetationParams::ground_height` and `grading_footprint` are unwired hooks.
- Commands are `Command<RoadGraph>` only, so terrain or building edits have no home.
- `WorldCache` is single-threaded and bound to one graph. `RoadGraph` is copyable.
- `layout_io` is explicitly not the save format.

**3. Unreal today**
- One plugin, one module (`CityPainterSim`).
- `ACPRoadMeshActor` owns the sim; `CPRoadMeshBuild.*` orchestrates without `WorldCache`.
- Builds are synchronous, PMC is in the public header, collision is off.
- No terrain (40 km floor plane), no game mode, pawn, input assets or UI.
- The game module is one line; the map is `/Game/Maps/RoadTest`.
- Of `docs/unreal-architecture.md` §6, only the wrapper guard in `Build.cs` is done.

## A. Target structure

Plugin `CityPainterCore` replaces `CityPainterSim`. Dependencies point down only:
`CityPainter` (game) → `CityPainterRender` → `CityPainterWorld` → `CityPainterSim` → sim_core. `CityPainterEditor` sits on top.

| Module | Class | Responsibility |
|---|---|---|
| Sim (no UObject) | `FCPSimWorld` | Owns `cp::World`, `CommandStack`, `WorldCache`; the only code that includes `cp/` headers |
| Sim | `FCPToolSession` | Wraps `cp::DrawSession`; rays in, preview structs out |
| World | `UCPWorldSubsystem` (`UTickableWorldSubsystem`) | `Submit/Undo/Redo`, tool-session access, pumps results, broadcasts `OnTilesChanged` |
| World | `FCPSimWorker` | Serial `UE::Tasks::FPipe`; all graph and cache mutation |
| World | `UCPSettings` (`UDeveloperSettings`) | Data dir, region, seed, budgets |
| Render | `UCPRenderSubsystem` | Layer registry, budgeted swap queue, tile lifetime |
| Render | `ICPRenderLayer`, `ICPTileMesh` / `FCPProcTileMesh` | Contract; PMC confined to one .cpp |
| Render | `FCPGroundLayer`, `FCPRoadSurfaceLayer`, `FCPMarkingsLayer`, `FCPStructuresLayer` | Mesh layers, one sim layer each |
| Render | `FCPInstanceLayer` | ISM pools keyed (tile, type): lights, `world_cover`, `vegetation` |
| Render | `UCPOverlayComponent` | Ghost ribbon, guides, handles; untiled, rebuilt per hover |
| Game | `ACPGameMode`, `ACPPlayerController`, `ACPCameraPawn` | Wiring; cursor ray and Enhanced Input; orbit/pan/zoom rig |
| Game | `UCPToolManager`, `UCPTool` | Active tool, input routing; base with `OnHover/OnClick/OnCancel/OnKey` |
| Game | `UCPRoadDrawTool`, `UCPRoadEditTool`, `UCPRoundaboutTool`, `UCPBulldozeTool` | Each forwards to one sim session; no rules |
| Game | `UCPInputConfig` | `UInputAction`s and `UInputMappingContext` built in C++ (`MapKey`), so no binary assets |
| Game | `UCPHudWidget` (`UUserWidget`) | Readouts, type picker, errors |

- **HUD:** UMG with C++ base classes. Until a look is designed, build the tree in C++ (`UWidgetTree::ConstructWidget`); this stays diffable and outside the editor lane.
- **Map:** `/Game/CityPainter/Maps/L_Sandbox` holds sky and lights only; the world is spawned by the subsystems.
- **A future system (buildings)** adds a `cp::WorldLayer`, its `cp::Command`s, one `ICPRenderLayer` registration and one `UCPTool`. No road file changes.
- **Traffic** needs a second, per-frame contract (`ICPDynamicLayer::Tick(alpha)`). Reserve it now; do not build it.

## B. Data flow

**Mouse move (game thread only):**
1. `ACPPlayerController::Tick` calls `DeprojectMousePositionToWorld`.
2. `UCPRoadDrawTool::OnHover(ray, pixelScale, modifiers)` calls `FCPToolSession::Hover`, which calls `cp::DrawSession::hover`.
3. `hover` reads the latest immutable `shared_ptr<const cp::WorldView>` (graph copy, spatial index, heightfield). It does the ray-ground pick, snap, stroke fit and validation, and returns a `cp::DrawPreview` (ribbon mesh, guide, snap kind, errors, warnings, readouts).
4. `UCPOverlayComponent` redraws; `UCPHudWidget` reads the readouts.

No graph mutation, no `WorldCache`, no Unreal collision. Cost is that of the prototype's per-move fit.

**Commit:**
1. `DrawSession::finish()` returns a `cp::CommandPtr`.
2. `UCPWorldSubsystem::Submit` enqueues it on `FCPSimWorker`.
3. Pipe task: execute all queued commands (microseconds each), publish a new `WorldView`, run `WorldCache::update()` once (the ~190 ms step), and post `{ChangeList, shared_ptr tile snapshots}` to the game thread.
4. `UCPRenderSubsystem` queues one swap per (layer, tile), nearest first, at `cp.render.TileBudgetMs` (3 ms) per frame. The old tile stays until its replacement lands; stale versions are dropped.
5. The ghost stays as "pending" until the first tile of that world version arrives.

Undo, redo and load take the same path. Rapid edits coalesce into one update.

## C. Milestones
> Scope only. State (what is done, running, owed) is in docs/QUEUE.md.


sim_core work runs in parallel worktrees; Unreal work is the single lane.

| # | Runnable result | sim_core (parallel) | Unreal (lane) | Out | Verified by | Size |
|---|---|---|---|---|---|---|
| M1 | Orbit camera over seeded hills; HUD shows cursor world position | `cp::World`; port `genTerrain`; `raycast_ground` | Plugin split, both subsystems, registry, `ICPTileMesh`, `FCPGroundLayer`, game mode, pawn, input, map; delete the actor | Roads | doctest; screenshot; automation test "N tiles, no errors" | L |
| M2 | Click-click straight road, Ctrl+Z/Y | `DrawSession` v0 (free points, ribbon preview) | Road and marking layers on `WorldCache`; draw tool; overlay; synchronous update | Snapping, grading | doctest replay of click scripts; automation "command changes exactly these tiles" | M |
| M3 | Prototype-parity drawing: curves, all snaps, junctions on attach or cross, red/amber, PgUp/PgDn, types, one-way, dual | Full `DrawSession`; extract the crossing resolver from `layout_io.cpp`; `RoadIndex` | Guides, HUD readouts, type picker | Edit mode | Same click scripts run through JS and C++ | M (sim L) |
| M4 | No hitch on a 12x12 grid | Per-object mesh reuse | `FCPSimWorker`, budgeted swap, pending ghost | — | Latency probe JSON; version-cancel automation test | M |
| M5 | Cuttings, embankments, walls, auto bridge/tunnel | `ground` and `structures` layers with dirty tiles | Graded `FCPGroundLayer`, `FCPStructuresLayer` | Tunnel bore discard (spike) | Grading goldens; cold-read renders | L |
| M6 | Lights, world cover, verge scatter; a road clears vegetation | Wire `ground_height` and `grading_footprint` | `FCPInstanceLayer`, kit import | Real trees | Instance-count test; renders | M |
| M7 | Edit mode, bulldoze, roundabout and bridge tools | `EditSession`; roundabout preview | Three tools | — | doctest scripts | M |
| M8 | Packaged exe; save/load | Save format v0 | Stage `data/`; `Package-Smoke.ps1` | — | Smoke exit 0 | M |

## D. sim_core gaps, ranked

1. `cp/road_tool.h`: `DrawSession`/`DrawPreview`. Ports `computeSnap`, `buildPreview`, `startOrAddPoint`, `conformRoad`/`planHits`, the dual-offset part of `finishRoad`, and `adjustHeight`. Tunables (`snapAngle`, `snapLen`, `minSpan`, `HEADROOM`) move to region or tool data.
2. A shared stroke resolver (`make_draw_stroke`) lifted out of `layout_io.cpp`, so tool and layouts split crossings identically.
3. `cp::World` aggregate and `Command<World>`; immutable `WorldView` snapshot.
4. Queries: `RoadIndex` (nearest edge, node, end), `raycast_ground`.
5. Terrain generator or loader.
6. Grading and structures as `WorldLayer`s with a dirty path.
7. Incremental cost (mesh reuse, `apply_roundabouts` scan).
8. Save format.

## E. Risks and cheapest answers

- **Terrain representation.** `performance-plan.md` wants a height-texture clipmap; the plan starts with mesh tiles behind `ICPTileMesh` (64 m tiles at 0.5 m in corridors, coarse elsewhere). Spike in M5: time one 129x129 PMC section update. The tunnel discard needs a material spike.
- **Collision and traces.** Avoided: picking is analytic in sim_core. Revisit only when physics actors exist.
- **PMC and Lumen.** `LUMEN_FINDINGS.md` records that PMC has no surface cache. Accepted for now.
- **Vegetation instancing.** Spike in M6: add and remove 50k ISM instances per tile swap and time it.
- **PIE versus packaged.** The data dir is found by walking up from the plugin. Fix in M1 with `UCPSettings`; prove staging in M8 (`DirectoriesToAlwaysStageAsNonUFS`).
- **Graph copy per edit** is O(n). Measure at 5k edges before designing copy-on-write.
- **The 190 ms figure** is not recorded in the repo; `tests/test_world_cache.cpp` prints it. Re-measure it in M4.

## F. Decisions for the person

1. **Camera:** prototype orbit rig (right-drag rotate, middle-drag pan, wheel, WASD, Q/E). Default yes.
2. **First playable:** editor PIE through M7, packaged exe at M8. Default yes.
3. **UI look:** plain dark panels copied from the prototype until parity. Default yes.
4. **World size:** 2 km seeded test world, matching the prototype. Default yes.
5. **Ghost:** flat ribbon as in the prototype, not a full junction preview. Default ribbon.
6. **Rename the plugin to `CityPainterCore` and retire `RoadTest`.** Default yes.

## Unreal names verified in `C:\UE_5.8\Engine`

- `Source/Runtime/Engine/Public/Subsystems/WorldSubsystem.h`: `UWorldSubsystem`, `UTickableWorldSubsystem`
- `Source/Runtime/DeveloperSettings/Public/Engine/DeveloperSettings.h`: `UDeveloperSettings`
- `Source/Runtime/Core/Public/Tasks/Pipe.h`, `Task.h`: `UE::Tasks::FPipe::Launch`, `UE::Tasks::Launch`
- `Plugins/EnhancedInput/Source/EnhancedInput/Public/`: `InputAction.h`, `InputMappingContext.h` (`MapKey`), `EnhancedInputComponent.h` (`BindAction`), `EnhancedInputSubsystems.h` (`UEnhancedInputLocalPlayerSubsystem::AddMappingContext`)
- `Source/Runtime/Engine/Classes/GameFramework/PlayerController.h`: `DeprojectMousePositionToWorld`, `bShowMouseCursor`, `FInputModeGameAndUI`
- `Source/Runtime/Engine/Classes/GameFramework/`: `GameModeBase.h`, `SpringArmComponent.h`; `Classes/Camera/CameraComponent.h`
- `Source/Runtime/UMG/Public/Blueprint/`: `UserWidget.h`, `WidgetTree.h` (`ConstructWidget`)
- `Plugins/Runtime/ProceduralMeshComponent/.../Public/ProceduralMeshComponent.h`: `CreateMeshSection`, `UpdateMeshSection`, `bUseAsyncCooking`
- `Source/Runtime/Engine/Classes/Components/InstancedStaticMeshComponent.h`: `AddInstances`, `RemoveInstances`, `BatchUpdateInstancesTransforms`
- `Source/Runtime/Engine/Classes/Components/LineBatchComponent.h`
- `Source/Runtime/Core/Public/Misc/AutomationTest.h`
- `Source/Developer/DeveloperToolSettings/Classes/Settings/ProjectPackagingSettings.h`: `DirectoriesToAlwaysStageAsNonUFS`

## Not determined

- Current test count, and whether grading round 5 landed.
- Whether the tree builds.
- The body of `CPRoadMeshActor.cpp` beyond greps.
- Details of the `layout_io.cpp` crossing resolver.
- PMC update timings, ISM churn cost and graph-copy cost at scale.

### Critical Files for Implementation
- C:\Users\bgbg0\Documents\Programming\roadworld\prototypes\road-draw\src\input.js
- C:\Users\bgbg0\Documents\Programming\roadworld\sim_core\include\cp\world_cache.h
- C:\Users\bgbg0\Documents\Programming\roadworld\sim_core\src\layout_io.cpp
- C:\Users\bgbg0\Documents\Programming\roadworld\sim_core\include\cp\road_commands.h
- C:\Users\bgbg0\Documents\Programming\roadworld\unreal\CityPainter\Plugins\CityPainterSim\Source\CityPainterSim\CityPainterSim.Build.cs
