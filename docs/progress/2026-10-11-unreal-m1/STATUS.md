# Unreal M1 status (kept current; last update: after step 1 and first code pass)

## Step 1 (sim_core suite, clean builds, commit 2c4abe3)
- MSVC 19.51 (Release, build-m1-msvc, outside %TEMP%): 361 cases, 361 passed, 0 failed, 1 skipped; assertions 2125128, 2125126 passed, 2 failed (the known should_fail interchange-bridge case).
- gcc 16 / Ninja (Release, build-m1-gcc): identical counts (361 / 361 passed / 1 skipped; 2 failed assertions).
- Both measured on 2c4abe3 (after the grading-r6 merge). A first attempt began before that merge and was discarded and rerun.

## Done (code written, NOT yet compiled successfully as of this line)
- docs/unreal-game-plan.md (step 0; extracted from the planning agent's SubagentHandback in its subagents jsonl, because the named task .output file was empty).
- Plugin renamed CityPainterSim -> CityPainterCore (git mv); modules CityPainterSim (facade, no UObjects, Core only), CityPainterWorld, CityPainterRender, CityPainterLegacy (old ACPRoadMeshActor + CoreRedirects in DefaultEngine.ini).
- Game module: CPGameMode, CPPlayerController, CPCameraPawn, CPInputConfig, CPHudWidget.
- Tools: look_env.py (extracted shared look constants), setup_sandbox_level.py, screenshot_views.py `sandbox` mode.

## Next
Fix compile errors (first UBT run predates the CPSimApi.h/UHT include fixes), run setup_sandbox_level.py, automation test, captures into this folder, INDEX.md.
- Plugin and game module BUILD (Build-Editor.ps1 succeeded).
- L_Sandbox created by setup_sandbox_level.py (6 environment actors, game mode CPGameMode on the map).
- Automation test CityPainter.World.Terrain passes (64 tiles, 524288 triangles, all CCW from above, borders coincide, picks agree).

## Update: M1 complete
Captures, INDEX.md, headless -game pick log and automation test done. Open: ground material grey, void beyond world edge, PIE mouse interactions not tested by a human.
