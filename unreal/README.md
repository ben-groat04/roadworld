# City Painter: Unreal project (UE 5.8)

Skeleton only. Written without Unreal installed, so the first compile is the real test (see "If it does not compile").

## Layout
- `CityPainter/CityPainter.uproject` – project, one game module `CityPainter` (empty), enables `CityPainterSim` and `ProceduralMeshComponent`.
- `CityPainter/Plugins/CityPainterSim/` – runtime plugin. Its module compiles `sim_core/src/*.cpp` straight from the repo (tiny wrapper files in `Private/SimCore/`; include path `sim_core/include`). `ACPRoadNetworkActor` owns a `cp::RoadGraph` + `cp::CommandStack`, draws debug lines and a ribbon mesh.
- `CPCoords.h` is the only place sim coordinates are converted. Sim: metres, x east, y north, z up, right-handed. Unreal: cm, left-handed. Mapping is an X/Y swap: `UE.X = sim.y*100` (north), `UE.Y = sim.x*100` (east), `UE.Z = sim.z*100`. Heading: `yaw = 90 - degrees(heading)`.
- If you add or move `sim_core` files, add a matching `Wrap_<name>.cpp` in `Private/SimCore/`.

## Steps on Windows
Prerequisites: `docs/windows-setup.md` (VS 2026 or 2022 17.14+ with the Game development workload, UE 5.8 installed, `git lfs pull`).

1. Right-click `unreal/CityPainter/CityPainter.uproject` -> **Generate Visual Studio project files**. (If the menu entry is missing, run `Engine\Build\BatchFiles\GenerateProjectFiles.bat -project="<full path>\CityPainter.uproject" -game` from the UE install.)
2. Open `CityPainter.sln`. Set configuration **Development Editor**, platform **Win64**, startup project **CityPainter**. Build (Ctrl+B). First build takes a few minutes.
3. Start the editor (F5, or double-click the .uproject). Accept "rebuild modules" if asked.
4. File -> New Level -> **"Open World"** (World Partition enabled). Save as `/Game/Maps/RoadTest`.
   The person chose World Partition from day one.
5. Content Drawer -> Add -> **Place Actors panel** or the Place Actors window, search `CPRoadNetworkActor`, drag it into the level at the origin. Add a Player Start above it, or just use Play -> Simulate.
6. Press Play. You should see: cyan debug lines along two two-way curved roads and an orange line along one one-way road, with a grey flat ribbon mesh (7 m wide) under them. The first road climbs 6 m over about 200 m. The Output Log shows `CPRoadNetwork: 3 edges, N verts`.
7. Undo/redo: in the actor's Blueprint or the level Blueprint, call `UndoLast` / `RedoLast` (BlueprintCallable). The last road disappears and returns. `RebuildVisuals` is also a button in the Details panel.
8. Optional: assign any material to `RoadMaterial` on the actor. If the ribbon is invisible from above, the winding is flipped (see the triangle order in `RebuildVisuals`); tell Claude.

## If it does not compile
Likely suspects, in order: (1) `BuildSettingsVersion.Latest` / `EngineIncludeOrderVersion.Latest` in the two `*.Target.cs` files (replace with the explicit values from your engine's `Engine/Source/Programs/UnrealBuildTool/Configuration/TargetRules.cs`); (2) sim_core warnings promoted to errors (extend the `#pragma warning(disable ...)` list in `Private/SimCore/Wrap_*.cpp`); (3) `CreateMeshSection_LinearColor` signature in `ProceduralMeshComponent.h`. Paste the first error to Claude.

## First look and performance test (v0 brief), all TODO
- [ ] Hand-placed road on sculpted terrain (Landscape or the new 5.8 mesh terrain) at 1440p60 on an RTX 3060 class GPU, with upscaling (DLSS/TSR). Record frame time budget (draw calls, shadows).
- [ ] Runtime Virtual Texturing: RVT volume + landscape/terrain RVT output, road and marking decals blended into terrain, wear.
- [ ] Geometry Script (`UDynamicMeshComponent`) replacing the ProceduralMeshComponent ribbon: road surface, kerbs, retaining walls, cuttings, bridge meshes. Check the packaged-build Geometry Script crash reported for 5.8.3 before relying on it.
- [ ] World Partition level for 20 km+ maps; confirm large world coordinates behave with doubles.
- [ ] Lumen/Nanite/shadow cost with the junction-heavy test network.
- [ ] Sim on its own fixed-timestep thread, renderer interpolates (currently everything runs on the game thread).

## Claude Code + Unreal MCP (local session)
Recommended: Epic's own **Unreal MCP** plugin, built into UE 5.8 (experimental). No Fab download and no copy into `Plugins/`. Community plugins (chongdashu/unreal-mcp, mirno-ehf/ue5-mcp) are only for UE 5.7 and earlier.

1. In the editor: Edit -> Plugins, enable **Unreal MCP** and **All Toolsets** (Toolset Registry comes in as a dependency; without All Toolsets the server has no tools). Restart.
2. Edit -> Editor Preferences -> General -> Model Context Protocol: tick **Auto Start Server** (default port 8000, path `/mcp`). Or run `ModelContextProtocol.StartServer` in the editor console each session.
3. In the editor console run `ModelContextProtocol.GenerateClientConfig ClaudeCode`. It writes `.mcp.json`; check the Output Log for where. Move or merge it into the repo root. Or add it by hand:
   `claude mcp add --transport http --scope project unreal-mcp http://127.0.0.1:8000/mcp`
   (equivalent `.mcp.json`: `{"mcpServers":{"unreal-mcp":{"type":"http","url":"http://127.0.0.1:8000/mcp"}}}`; the generated file's exact contents were not verified, so prefer the generated one.)
4. Run `claude` from `C:\...\roadworld` (the folder holding `.mcp.json`), with the editor open on `unreal/CityPainter`. Check `/mcp` shows `unreal-mcp`, then ask "what actors are in the level?".
5. Optional: Epic's Claude Code plugin (MIT), `/plugin install unreal-engine-skills-for-claude-code@claude-plugins-official`. Needs Git Bash or WSL for its hook.

Notes: loopback only, no authentication, tool calls run serially on the game thread. Commit before agent sessions. The server must be restarted along with the editor after adding new tools. Python, console command, build, screenshot and log tools: unverified, so use `list_toolsets` to check.
