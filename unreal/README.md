# City Painter: Unreal project (UE 5.8)

Skeleton only. Written without Unreal installed, so the first compile is the real test (see "If it does not compile").

## Layout
- `CityPainter/CityPainter.uproject` – project, one game module `CityPainter` (empty), enables `CityPainterSim` and `ProceduralMeshComponent`.
- `CityPainter/Plugins/CityPainterSim/` – runtime plugin. Its module compiles `sim_core/src/*.cpp` straight from the repo (tiny wrapper files in `Private/SimCore/`; include path `sim_core/include`). `ACPRoadMeshActor` builds the sim_core `interchange` test layout and shows real cambered carriageways, kerbs, pavements, junction surfaces, roundabout islands and road markings (one ProceduralMesh per 256 m tile, one section per surface class; markings in their own components). It builds in the editor viewport (`OnConstruction`), has `RebuildAll` / `UndoLast` / `RedoLast` buttons and a `LogCPRoadMesh` category; its sim-facing logic is UE-free in `Private/CPRoadMeshBuild.{h,cpp}`. Materials: `SurfaceMaterials` / `MarkingMaterials` maps, else `/Game/Materials/Surfaces/<slot>/MI_<slot>` (asphalt->asphalt_new, paving->pavement, kerb->kerb_concrete, grass and island->grass), else the engine default.
- `tools/` – PowerShell build and headless-editor scripts (`tools/README.md`).
- `Private/CPRoadMeshBuild.h` (frame rule) and `.cpp` (`SimToUE`) are the only place sim coordinates are converted. Sim: metres, x east, y north, z up, right-handed. Unreal: cm, left-handed. Mapping is an X/Y swap: `UE.X = sim.y*100` (north), `UE.Y = sim.x*100` (east), `UE.Z = sim.z*100`. Heading: `yaw = 90 - degrees(heading)`.
- If you add or move `sim_core` files, add a matching `Wrap_<name>.cpp` in `Private/SimCore/`.

## First run (scripted, from "engine just finished installing")
**One command, engine installed to first pictures (re-runnable, resumes where it stopped):**
`powershell -NoProfile -ExecutionPolicy Bypass -File unreal\tools\First-Run.ps1`
(`-DryRun` previews, `-From <step>` / `-Only <step>` / `-Force` override; steps: find-engine, build, environment, textures, level, screenshots; state in `unreal\CityPainter\Saved\first-run-state.json`. On failure it prints the log path and the exact command to re-run that step.)

Only these still need a human:
1. Accept the prerequisites prompt if one is shown.
2. Wait out the first shader compile (10-30+ minutes).
3. Enable the Unreal MCP + All Toolsets plugins and tick Auto Start Server.

Prerequisites: `docs/windows-setup.md` (VS 2026 / 2022 17.14+, Game development workload), UE 5.8 installed, `git lfs pull`. Tools are documented in `tools/README.md`; first-build failures map to fixes in `tools/KNOWN_RISKS.md`. Run from the repo root:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File unreal\tools\Find-Unreal.ps1          # 1. engine found and complete?
powershell -NoProfile -ExecutionPolicy Bypass -File unreal\tools\Build-Editor.ps1         # 2. build (generates project files on first run)
powershell -NoProfile -ExecutionPolicy Bypass -File unreal\tools\Run-EditorPython.ps1 -Script py\report_environment.py -TimeoutMinutes 90   # 3. first launch: compiles shaders, proves module + actor load
powershell -NoProfile -ExecutionPolicy Bypass -File unreal\tools\Run-EditorPython.ps1 -Script py\setup_look_test_level.py                  # 4. create /Game/Maps/RoadTest
powershell -NoProfile -ExecutionPolicy Bypass -File unreal\tools\Run-EditorPython.ps1 -Script py\screenshot_views.py -Render -ScriptArgs pie # 5. screenshots -> unreal\CityPainter\Saved\Screenshots\look\
```
Step 4 places `CPRoadMeshActor` and step 5 shows six interchange views (aerial, roundabout oblique and approach, T-junction, slip merge, kerb close-up); neither needs `pie` because the actor builds in `OnConstruction`. (The `old` / `both` script args were removed with `CPRoadNetworkActor` and now fail with a message.) Run `import_textures.py` before step 4 if you want the Poly Haven materials assigned.

**Still needs a human:**
- Accept the prerequisites installer (VS components, UE prerequisites, .NET) the first time the engine or the project asks.
- Enable the MCP plugins by hand (names unverified, so not in the .uproject): Edit > Plugins > **Unreal MCP** and **All Toolsets**, restart; then Editor Preferences > General > Model Context Protocol > **Auto Start Server**. The repo-root `.mcp.json` already points Claude Code at `http://127.0.0.1:8000/mcp`.
- First editor launch compiles thousands of shaders (10-30+ minutes with the DX12 SM6 + Lumen + ray-tracing baseline). Step 3 absorbs this; use a large `-TimeoutMinutes`.
- Opening the project interactively (double-click the .uproject) the first time may ask to rebuild modules; after step 2 it should not.
- Look-test downloads that cannot be scripted: the **NVIDIA DLSS plugin for UE 5.8** (NVIDIA developer site, Super Resolution; Ray Reconstruction separately), the **AMD FSR plugin** if comparing, any Fab trees / house kit assets for look-test items 11-12, and a recent NVIDIA driver. The 12-item test list is `docs/unreal-photorealism.md` s3.
- Judging the screenshots (realism) is a human or cold-read-agent task; the scripts only produce files.

Manual fallback for the road: open the map; `CPRoadMeshActor` builds the interchange in the editor viewport (press `Rebuild All` in the Details panel if nothing shows). The Output Log (`LogCPRoadMesh`) prints the build statistics. Undo/redo: `UndoLast` / `RedoLast`. If the roads are invisible from above the winding is flipped (see `unreal/tools/KNOWN_RISKS.md` H14).

## If it does not compile
See `tools/KNOWN_RISKS.md` first (error message -> exact edit). Original notes:
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
