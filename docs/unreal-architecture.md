# Unreal architecture proposal and cold read

Written 10 Oct 2026, before the first compile. Marks: [U] = Unreal 5.8 fact not verified; nothing below depends on a [U] item. Priority: the Unreal side must not become a mess as systems are added.

## 1. Layering and module map

Rule of the whole design: **Unreal draws and hosts input; sim_core decides.** Dependencies point down only.

```
CityPainter (game module: GameMode, PlayerController, input, tools, UI)
   |            \
   |             CityPainterEditor (editor-only: Python/automation helpers, test-level builders)
   v
CityPainterRender  (layer registry, tile lifecycle, floating origin, material binding, per-layer renderers)
   v
CityPainterWorld   (UWorldSubsystem: owns the sim instance, command dispatch, dirty pump, build scheduler)
   v
CityPainterSim     (compiles sim_core + third_party; plain C++ facade; NO UObject, NO Unreal types)
```

| Module | May include | Must not include |
|---|---|---|
| CityPainterSim | sim_core, std, Core (for build only, prefer none) | Engine, CoreUObject, any UObject |
| CityPainterWorld | Sim, Core, CoreUObject, Engine (subsystem, tasks) | Render, ProceduralMeshComponent, materials |
| CityPainterRender | World, Sim *types in the facade only*, Engine, mesh-component plugin | sim_core headers directly, game/UI |
| CityPainter (game) | Render, World, InputCore, UMG | sim_core headers, mesh components |
| CityPainterEditor | anything above | shipped in no packaged build |

Split plugin: **split Sim from the rest now** (cheap today: two folders and two Build.cs; the Sim module is already UE-free in `CPRoadMeshBuild.*`). Keep World and Render as two modules inside one plugin `CityPainterCore`, split later only if build times or layering violations force it. Reason for doing it now: the Sim module's no-PCH, no-unity, no-exception flags must not spread to code that uses UE normally, and a module boundary is the only thing UBT enforces.

**Never on the Unreal side:** game state (the world graph, parcels, zoning, budgets), rules (junction, marking, grading, zoning logic), region values (road types, colours, clearances, driving side, units), undo history, save format, mesh generation (including winding and tangent rules beyond the one frame conversion), id allocation.

## 2. The sim-to-render contract

**Edit flow (one path, always):** input/tool (game module) builds a `FCPCommandRequest` -> `UCPWorldSubsystem::Submit` -> sim facade executes the `cp::Command` on `cp::CommandStack` (game thread, microseconds) -> facade returns a `FDirtySet` (dirty tiles per layer, plus a monotonically increasing `WorldVersion`) -> subsystem schedules one build per dirty (layer, tile) -> results come back as immutable packets -> `UCPRenderSubsystem` swaps them in. Undo/redo/load take the same path. Nothing calls a mesh builder directly.

**Dirty set belongs in sim_core.** Today the caller derives tiles from dirty nodes (`docs/sim-performance-baseline.md`, finding 1). Add one sim_core entry point, `cp::DerivedCache::apply(graph) -> DirtyTiles` per layer, that owns the junction/mesh/marking/light/ground caches and clears `RoadGraph::dirty_*`. Unreal never knows what a "dirty node" is.

**Threading.** Game thread: command execute, undo, swap-in. Worker threads (`UE::Tasks` [U: confirm API names]): per-(layer,tile) builds. sim_core builds need a stable graph: before dispatching builds, the facade takes an immutable snapshot (copy-on-write graph, or a version counter that cancels builds if the graph moves). Each task carries `{Layer, TileKey, WorldVersion}`. On completion the game thread discards the packet if `WorldVersion` is older than the tile's latest requested version; a newer edit to the same tile cancels or supersedes the running build. The old tile stays until the new packet arrives (no flicker, no destroy-then-create). Swap-in is time-budgeted (a few ms per frame, near-to-far).

**Tile identity.** `FCPTileId { Layer, int64 I, int64 J, uint8 Lod }` mirrors `cp::TileKey` plus layer; it is a plain struct, hashable, never a pointer. A tile's render object lives while the tile is inside the streaming radius or has content; lifetime is owned by the layer, not the actor.

**Floating origin.** Sim stays in double metres. `FCPRenderOrigin` (in Render) holds a double sim-space anchor snapped to a 1 km grid; the only function converting sim to UE is `CPFrame::SimToLocal(dvec3, origin)` (one header, replaces both `CPCoords.h` and the copy in `CPRoadMeshBuild.cpp`). Tile components carry a double offset from the anchor; vertices are float, tile-relative (already done). When the camera leaves the anchor cell, the origin shifts and all components re-offset in one frame (use UE's origin rebasing if World Partition/Large World Coordinates make it unnecessary [U]; the facade hides the choice). Because UE5 is double-precision, a first version may skip the shift and only use double component transforms; the interface keeps the door open.

**Plug-in layers.** A new system adds one file and one registration; no existing layer changes.

```cpp
// Render module. Plain C++.
struct FCPTilePacket {                    // immutable, built off-thread, shared_ptr
  FCPTileId Id; uint64 WorldVersion;
  virtual ~FCPTilePacket() = default;
};
class ICPRenderLayer {                     // one per system: Roads, Markings, Ground, Structures,
 public:                                   // KitInstances, Vegetation, Buildings, Water, Vehicles
  virtual FName LayerName() const = 0;
  virtual uint32 Priority() const = 0;     // swap-in and shadow policy
  virtual TSharedPtr<FCPTilePacket> Build(const FCPSimSnapshot&, FCPTileId) = 0;  // worker thread
  virtual void Apply(TSharedPtr<FCPTilePacket>, FCPRenderContext&) = 0;           // game thread
  virtual void Remove(FCPTileId, FCPRenderContext&) = 0;
  virtual void OnOriginShift(const FCPRenderOrigin&) = 0;
};
class UCPRenderSubsystem : public UWorldSubsystem {
  void RegisterLayer(TUniquePtr<ICPRenderLayer>);   // called from each layer's module startup
  void OnDirty(const FCPDirtySet&);                  // fans out to layers that listed those tiles
};
class ICPTileMesh {                        // the PMC/RealtimeMesh wrapper (performance-plan.md item 1)
 public: virtual void Set(const FCPMeshSections&, FVector Offset) = 0; virtual void Clear() = 0;
};
```

Instanced layers (kit, vegetation, buildings, vehicles) implement the same `ICPRenderLayer` but their `Apply` updates an ISM/HISM pool keyed by `(tile, module-id)`; the packet is an instance list in sim metres. Sim emits instance lists and ids only; the module-id -> mesh table is a content registry in Render.

## 3. Conventions

- **Prefixes.** Modules `CityPainter*`; UObject classes `UCP*`/`ACP*`; plain C++ `FCP*`, interfaces `ICP*`; sim_core namespace `cp::` stays untouched. Files = type name. Folder layout per module: `Public/` (API headers only), `Private/<Area>/`, areas = `Roads`, `Ground`, `Instancing`, `Debug`.
- **UObject vs plain.** UObjects only where the engine demands them: subsystem, components, actors for placed things, data assets. Everything else (tile packets, ids, settings, facade) is plain C++ and unit-testable without the engine. No UObject pointers cross into the Sim module.
- **Ids.** `cp::Id<Tag>` crosses the boundary as a plain `FCPId { uint8 Kind; uint64 Value }` struct, never as `int32`/`FName`/`FString`. Components keep an id only for picking (hit -> id), through a lookup table in the layer, not by subclassing components. Ids are never saved by Unreal.
- **Region pack data.** One loader in the Sim facade reads `data/regions/<region>/`; Render gets only a `FCPSurfaceTable` (surface class -> material slot -> parameter set) and a marking-colour table, both emitted by the facade from the region pack plus a data file `data/render/surface_slots.json` (new; replaces `SlotForSurface`). No region name or road-type id string appears in Unreal code.
- **Materials.** One master material per surface family (road, ground, markings, foliage, glass), instances per surface class created in the editor, never at runtime per tile. Per-tile variation through vertex channels already defined in `render_mesh.h` (wear R, dirt G, variation B) and a `surface_id` attribute; global state (wetness, time) through one Material Parameter Collection. Runtime MIDs only for rare debug use.
- **Content.** `/Game/CityPainter/<Family>/<Name>`: `Materials/M_<Family>`, `Materials/MI_<Class>`, `Textures/T_<name>_<D|N|ORM>`, `Kit/SM_<module>`, `Maps/L_<name>`. Region-specific overrides in `/Game/Regions/<region>/...`; shared families stay region-free. Today's `/Game/Materials/Surfaces/...` (from `import_textures.py`) moves under this root once, by script.
- **Config/cvars.** Settings are a `UDeveloperSettings` class `UCPSettings` (data dir, default region, tile size, budgets), not actor properties. Cvars `cp.<area>.<name>` (e.g. `cp.render.TileBudgetMs`, `cp.sim.Threads`).
- **Logging.** One category per module: `LogCPSim`, `LogCPWorld`, `LogCPRender`, `LogCPGame`; never `LogTemp`.
- **Blueprint policy.** C++ owns everything stateful. Blueprint allowed for: UI widgets (UMG) calling a handful of `BlueprintCallable` subsystem functions, material/instance assets, level layout, tuning data assets. Not allowed: tools logic, command construction, tile handling, anything that touches ids or the sim.
- **Save/load.** sim_core owns the format (layout file today; save with undo, ids, terrain, overrides later). Unreal only calls `Facade::Save(path)`/`Load(path)` and then triggers a full dirty set. Unreal-side settings (camera, graphics) are `USaveGame`/ini and never contain world state.

## 4. Testing and verification

1. **Headless sim_core (stays the main net):** doctest, including golden mesh hashes; plus a new "render contract" test emitting per-tile packet summaries (vertex counts, bounds, hashes) so a drift between sim_core and Unreal expectations fails without Unreal. The frame/winding conversion is a pure function in the Render module's plain-C++ part; test it in doctest against known triangles.
2. **UE automation tests (few, valuable):** (a) module-load and facade smoke (`Interchange` loads, N tiles, no `Messages`); (b) command -> dirty -> apply round trip updates exactly the expected tile ids; (c) cancellation: two edits to one tile yields one swap with the newest version; (d) origin shift keeps world-space vertex positions within 1 mm [U: automation test API names]. Run with `-ExecCmds="Automation RunTests CityPainter; Quit"` [U].
3. **Golden screenshots:** fixed cameras (`screenshot_views.py` views) rendered through Movie Render Graph (Production Ready in 5.8 per `research/ue58-feature-sweep.md` F), deterministic settings (fixed TSR seed, no auto exposure), compared by SSIM/perceptual diff with a loose threshold and a human-approval step to update goldens. Used for regressions of whole-scene look, not geometry.
4. **Packaged-build smoke:** cook + stage `data/`, run `-nullrhi`-free short scenario that loads a layout, applies one scripted edit, undoes, saves, exits 0 and writes a stats JSON. Catches missing staged data (see finding 7).
5. **Perf capture:** `snapshothitches` and a scripted camera fly-through that writes `stat unit` / Insights summary to JSON; thresholds in git. Edit-latency probe: time from `Submit` to swap-in for 1, 10, 100 dirty tiles.
6. **Fit with `unreal/tools`:** `Build-Editor.ps1` + `BuildLogParser.ps1` = compile gate; `Run-EditorPython.ps1` stays the editor-side runner for the screenshot and environment scripts; add `Run-Automation.ps1` and `Package-Smoke.ps1` in the same style. Python is for level setup and assets only, never for gameplay or sim.

## 5. Cold read of the current code

| # | Finding | When |
|---|---|---|
| 1 | `CPRoadMeshBuild.cpp:132-148` uses `cp::TileKey::x/.y` and `:351` `Chunk.tile.x`; `sim_core/include/cp/tile_key.h` now has `i, j` (int64). `FTile::TileX/TileY` in `CPRoadMeshBuild.h` are int32 and the tile map key is int32 pairs: wrong at 20 km+ and a compile error today. Also `MeshChunk` gained `surface_ids`/`merge_surfaces`, unused. | fix before first compile |
| 2 | The same facade-vs-sim_core drift will recur: `Build.cs` needs a wrapper per sim_core `.cpp` (24 today, all present) with no guard. Add a check in `CityPainterSim.Build.cs` that fails with a clear message if a `sim_core/src/*.cpp` has no `Wrap_*.cpp`. | fix before first compile |
| 3 | `CPRoadMeshBuild.cpp:305-365` orchestrates the pipeline (junctions -> roundabouts -> chunks -> markings, tile assignment of marks by centroid at :395, winding fix at :421-428, UV/tangent/colour constants at :412-416, `MarkingParams` defaults at :364) in the wrapper. That is sim_core logic and it ignores street lights, structures, grading. Move into a `cp::DerivedCache` (see section 2). `W.Overrides` is never persisted. | first week |
| 4 | Sim world is owned by an `AActor` (`CPRoadMeshActor.cpp:21-35`, `CPRoadNetworkActor.cpp:17-21`), rebuilt in `OnConstruction` and by float-hash change detection (:153-197). Game state in a level actor dies with the level, runs in the editor world, and `RebuildAll` discards undo history (header comment). Needs `UCPWorldSubsystem`. | first week |
| 5 | Build is synchronous on the game thread (`CPRoadMeshActor.cpp:316`); refresh destroys and recreates every component (:321, :335, :387-401); `UndoLast` rebuilds everything (:547). At the baseline 66 ms/tile that is a hitch per edit. | first week (flow in section 2) |
| 6 | PMC leaks: `ProceduralMeshComponent` in public `Build.cs` deps (`:38`), `.uproject` and `.uplugin` plugin lists, `UProceduralMeshComponent` in the public actor header (`CPRoadMeshActor.h:164`) and `CreateMeshSection` inline. Violates `performance-plan.md` item 1. Hide behind `ICPTileMesh`; PMC in one .cpp. | first week |
| 7 | Data dir located by walking up six/four levels from the plugin (`Build.cs`, `CPRoadMeshActor.cpp:225-232`) and `TODO(packaging)` at header :89. Works only from the repo checkout; packaged builds fail. Use `UCPSettings` plus staged `data/` (non-UFS dir) and a startup check. | first week |
| 8 | Hard-coded content/region knowledge in code: `SlotForSurface` (`CPRoadMeshActor.cpp:54-62`: "asphalt_new", "kerb_concrete", island -> grass); marking material paths `MI_marking_<id>`; `BasicShapeMaterial` "Color" fallback (:460-470, runtime MIDs); `RegionId = "uk"` default and `ECPTestLayout` in a shipped UCLASS (`.h`). `CPRoadNetworkActor.cpp:~95` hard-codes road type `"residential"`. Move to data (`surface_slots.json`) and `UCPSettings`. | first week |
| 9 | Two sim hosts: `ACPRoadNetworkActor` (own graph and stack, ribbon mesh, `LogTemp`) duplicates `ACPRoadMeshActor` and a second copy of the coordinate map (`CPCoords.h` vs `CPRoadMeshBuild.cpp:117-128`, whose header comment calls itself "single place"). Delete the old actor; keep one `CPFrame` header. | fix before first compile (delete) / first week |
| 10 | `DrawDebug` calls `FlushPersistentDebugLines` on the whole world (`CPRoadMeshActor.cpp:515`); debug collection lives in the wrapper (`:217-288`). Use a debug layer implementing `ICPRenderLayer`; debug lines from sim_core. | later |
| 11 | `UFUNCTION(BlueprintCallable)` Undo/Redo/Rebuild on the actor bypass a dispatcher; hash-of-floats change detection (:161-173) is fragile. | later |

## 6. First two weeks (small agent tasks, file ownership)

Pictures first, but only on top of this minimum scaffolding (days 1-3):
1. **A1 (Sim module)** owns `Plugins/CityPainterCore/Source/CityPainterSim/`: move wrappers and `CPRoadMeshBuild.*`, fix finding 1, add the wrapper guard (2). No UObject.
2. **A2 (sim_core)** owns `sim_core/include/cp/derived_cache.h`, `src/derived_cache.cpp`, `tests/test_derived_cache.cpp`: pipeline + dirty tiles + render-contract test (3). Coordinate with the agents editing sim_core now.
3. **A3 (World)** owns `CityPainterWorld/`: `UCPWorldSubsystem` (own sim instance, `Submit`, undo/redo, version counter), `UCPSettings`, `LogCP*`.
4. **A4 (Render)** owns `CityPainterRender/`: `CPFrame.h`, `ICPRenderLayer`, `UCPRenderSubsystem`, `ICPTileMesh` with PMC implementation, `RoadsLayer` ported from the actor with swap-in (no destroy), data-driven `surface_slots.json` (new `data/render/`), material binding.
5. **A5 (game/editor)** owns `Source/CityPainter/` and `CityPainterEditor/`: a debug spawner replacing the actors, retarget `setup_look_test_level.py`/`screenshot_views.py` (owns `unreal/tools/py/`), delete old actors.
6. **First compile and first pictures (day 3-4):** `First-Run.ps1` runs end to end; same Interchange images as before through the new path; check against the old screenshots.
7. **Week 1 end:** async build with version cancel; hitch probe; Markings layer split from Roads; automation tests (a)-(c).
8. **Week 2:** `GroundLayer` stub fed from the grading output (corridor tiles), `InstanceLayer` with ISM pool for light poles from sim (proves non-mesh layer), golden screenshot job, packaged smoke test, Movie Render Graph look-test shots, PMC vs Realtime Mesh Core trial behind `ICPTileMesh`.

Haiku-sized: renames, folder moves, `surface_slots.json`, script wrappers. Sonnet: everything else.

## 7. Decisions for the person

1. **Split the plugin now (Sim separate, World+Render together)?** Recommend yes. Reversal cost: low (merging modules later is a day). Not splitting costs more later.
2. **Sim lives in a world subsystem, not an actor?** Recommend yes. Reversal: medium (actor-placed worlds would need to be rebuilt); but placing the world in a level actor blocks saves and menus.
3. **sim_core owns the derived-data cache and dirty set (new header), with Unreal only consuming?** Recommend yes. Reversal: high once other systems depend on it, so decide now.
4. **Keep PMC for the look test, behind `ICPTileMesh`, and trial Realtime Mesh Core in week 2?** Recommend yes. Reversal: low if the interface holds. (Pro version is paid: not adopted without your say.)
5. **Blueprint policy: UMG and assets only, no gameplay/tool logic in Blueprint?** Recommend yes. Reversal: low but gets more expensive with every Blueprint written.
6. **Content root `/Game/CityPainter/...` with one-time move of existing imported textures/materials?** Recommend yes now, while the project has little content. Reversal: medium (asset redirectors).
