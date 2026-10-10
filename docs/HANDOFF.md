# Handoff: start here

Branch: `claude/practical-ride-rrf8rx`. Read this, then `CLAUDE.md`, then `docs/planning-log.md` (decisions, up to Round 34). Index of all docs: `docs/README.md`.

**Revised 10 Oct 2026 (late evening)**: full rewrite for the start of the Unreal phase in a NEW chat. Superseded in-flight lists, the agent final-state log and the old install story were removed; everything below is current as of this revision. Three code agents were still running when it was written (see §5): check `git log` for what landed.

## 0. State and priority

- **Priority #1 for the Unreal phase (the person's words): "build in such a way that when we do the rest of the city builder it doesn't become a mess".** `docs/unreal-architecture.md` is the structure reference; its six decisions were adopted (Round 34); its two-week plan (§6 there) is the order of work; **scaffolding before features**.
- Everything committed is pushed. Every push carries `[skip ci]` until the person asks for a CI run. The last CI-verified commit is still `1ae80b0` (roundabouts); CI has not run on anything since.
- sim_core: 257 test cases pass on MSVC (19.51) and gcc 16 (per the agent's report in commit `734522b`; clean builds take 1-2 minutes each). Prototype `scene.mjs --check` was 21/21 earlier today.
- **The Unreal plugin has STILL never been compiled by Unreal.** Today's pre-compile fixes (TileKey `i/j` as int64, Build.cs guard that every sim_core source has a wrapper, old `CPRoadNetworkActor`/`CPCoords.h` removed) were verified with MSVC only.

## 1. First steps in the new chat, in order

Tools are not on PATH; use full paths.

1. **Verify the engine on C:.** Unreal 5.8.3 was installed by the launcher to `D:\UE_5.8` (a USB spinning disk, slow). The person is **copying it to `C:\UE_5.8`** (internal NVMe; ~135 GB was free before the copy). **IN PROGRESS, unverified.** Done = `C:\UE_5.8\Engine\Source` and `C:\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe` exist and file counts match D:. Keep the D: copy until C: has built the project. The launcher only knows the D: location. A Windows Defender exclusion is needed for `C:\UE_5.8` (the D: one does not carry over). Do not restart the launcher or Epic install helper (each restart cancels its relocation work; `Find-Unreal.ps1 -EngineRoot` works without the launcher record).
2. Rerun `powershell -NoProfile -ExecutionPolicy Bypass -File unreal\tools\tests\Test-FindUnreal.ps1` (edited today, not rerun).
3. `powershell -NoProfile -ExecutionPolicy Bypass -File unreal\tools\First-Run.ps1 -EngineRoot C:\UE_5.8` (resumable; forwards `-LocalDdcPath` default `C:\UEDDC`, `-MaxParallelActions` default 4, `-GraphicsAdapter <n>` to pick the RTX 4060 over the Radeon 780M; parameters in `unreal/tools/README.md`).
4. Fix compile errors per `unreal/tools/KNOWN_RISKS.md` (predicted from sources, never compiled).
5. Human steps: accept a prerequisites prompt; wait out the first shader compile (10-30+ min); **the person enables the Unreal MCP and All Toolsets plugins** (Epic's own, ship with 5.8, experimental; Auto Start Server is off by default). `.mcp.json` at the repo root already points at `http://127.0.0.1:8000/mcp`. A **new Claude session is needed** before the MCP connects (it fails with ECONNREFUSED until the editor runs with the plugin; expected).
6. Then the architecture note's **week-one scaffolding BEFORE feature work**: `UCPWorldSubsystem` (sim instance in a world subsystem, not an actor), the render-layer registry (`ICPRenderLayer`, `ICPTileMesh` hiding ProceduralMeshComponent), the sim_core-owned derived-data cache and dirty set (`cp::DerivedCache`, new sim_core header + tests), and the plugin split (Sim module separate from World+Render, content under `/Game/CityPainter/`). Day 3-4: first compile and first pictures through the new path.
7. Use Movie Render Graph for reference renders from week one. Measure hardware-RT Lumen on runtime road tiles (and `docs/performance-plan.md` day-one measurements) **before** considering Realtime Mesh Component Pro ($49, not approved: "depends how much"; decide after measuring). Free Realtime Mesh Core gets a trial in week two behind `ICPTileMesh`.

Build and test, sim_core and prototype:
- CMake/Ninja: `C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\{CMake\bin,Ninja}`. MSVC 19.51 (VS 2026): `cmake -S . -B build-msvc; cmake --build build-msvc --config Release -j; .\build-msvc\tests\Release\sim_core_tests.exe`
- gcc 16 (stricter about missing includes than CI's gcc; CI moves to Ubuntu 26 on 19 Oct): add `C:\msys64\mingw64\bin` to PATH, `cmake -S . -B build-gcc -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=g++`.
- Node 24: `C:\Program Files\nodejs`. Prototype: `cd prototypes/road-draw; node build.mjs; node tools/scene.mjs --check` (~8 s; drives installed Edge on the RTX 4060).
- Many `build-*` dirs exist (gitignored); delete freely.

Machine: Ryzen 9 7940HS, 16 GB RAM, RTX 4060 plus integrated Radeon 780M, VS 2026 only (MSVC 14.51, not yet confirmed accepted by UE 5.8's UBT). Expect a slow cold editor launch and roughly 10-25 GB of C: for the first build plus shaders (estimate, unmeasured).

## 2. Working rules

All in `CLAUDE.md`, plus these from the person (also in Claude's local memory):

- **Decide road-engineering details yourself**: pick a default, keep it in region-pack data, make it a player toggle where practice varies. Still ask about scope, product direction, tool feel, and whether pictures look right.
- **Every push carries `[skip ci]`** until the person asks for a CI run (failed runs email them). There is no "one normal commit so CI runs" step.
- **Agents are always backgrounded**; so are pushes, builds and CI watches (the person was blocked by foreground waits).
- **Research Unreal, plugin and tool facts with agents; never answer them from memory.** Mark unverified engine facts `[U]`.
- **Free or cheap first** (personal project for now); flag any cost.
- **Not UK-only** for anything region-neutral; UK matters only for markings, signs, kerb styles, driving side.
- Main session coordinates; Sonnet agents code; cold-read Sonnet agents review code and pictures; send the person progress pictures.
- **Shared checkout: always do CLEAN builds in a per-agent build dir** (stale objects caused a false crash today), file ownership per agent, `-j 4`, "do not commit". For the big Unreal restructure consider per-agent git worktrees.

## 3. What exists

**sim_core** (headless C++20, exception-free; 257 doctest cases): commands/undo, ids, clothoids, vertical profiles, road graph with structure spans, region pack loader, lanes, edge sampling, heightfield, earthwork sections, junctions (`junction*.cpp`, `docs/junctions.md`: fillet solver, degraded reporting, merges/diverges, turn pockets written but disabled), roundabouts (`junction_roundabout.*`), render meshes (`render_mesh.*`: tile chunks, wear/dirt/variation vertex colours, surface-height functions, LOD), markings (`markings.*`, UK rules in `data/regions/uk/marking_rules.json`), street lights, grading (`grading.*`, goldens in `tests/data/grading/` from `prototypes/road-draw/tools/grade_golden.mjs`, JS and C++ in lockstep), structures (`structure_mesh.*`), layout files and test layouts, tools `export_scene`, `export_mesh`, `bench` (in `sim_core/tools/`). Added today: 12 junction round-3 tests; edit-loop fixes (`dirty_edges()`, `make_change_road_type`, `erase_node` dirty set, street-light dirty set widened one hop; known limit: spatially near but unconnected roads); shared `cp/geom.h` and `json_util.h` replacing copied helpers (`edge_heading` public); `cp/tile_key.h`, `cp/triangulate.h`.

**Prototype** (`prototypes/road-draw/`): grading, real tunnels, stepped walls, 21 invariants. `prototypes/mesh-view/` renders sim_core OBJ exports headlessly and makes photo-vs-ours composites.

**Unreal** (`unreal/`): plugin `CityPainterSim` with `CPRoadMeshActor` + UE-free `CPRoadMeshBuild.*` (standalone-compiled and run, never compiled in UE), a wrapper per sim_core source, config for Lumen/VSM/Nanite/TSR, tooling in `unreal/tools/` (PowerShell + editor Python; First-Run, Build-Editor, Run-EditorPython, log parser, script tests). `docs/unreal-architecture.md` §5 lists 11 cold-read findings against this code (data dir found by walking up directories, synchronous build, PMC leaking into public headers, hard-coded surface slots); findings 1, 2 and 9 are fixed.

**Assets**: 10 Poly Haven CC0 2K texture sets (`unreal/CityPainter/SourceArt/Textures/PolyHaven/`, LFS); Blender-scripted kit of 67 Nanite-ready pieces (`tools/blender_kit/`, FBX in `SourceArt/Meshes/Kit/`).

**References** (private, gitignored, never commit): `docs/reference/gsv/` (12 Street View shots + `compare/`), `aerial/` (45, CC), `street/` (37, CC), each with an INDEX.

## 4. Decisions (see the planning log; one line each)

- **Round 31**: turn pockets allowed (data rule + per-junction override; generator disabled, no override command or tests yet).
- **Round 32**: catch-up of 10 Oct decisions: nine UK road types, roundabout splitter and flare rules, class-pair corner radii, marking rules, earthwork and structure numbers, realism bar and reference-photo rules, free-first tooling.
- **Round 33**: Mesh Terrain avoided (editor-only modifiers); buildings by our own sim_core assembler borrowing the PCG shape-grammar string format, PCG for dressing only, one-day PCG trial before the buildings milestone; hardware-RT top quality tier wanted, baseline must still run on a 3060-class GPU without it; MCP write access to editor assets approved; Epic's Unreal MCP is the editor bridge, VibeUE optional later; Movie Render Graph for reference renders; RMC Pro undecided.
- **Round 34**: six Unreal-structure decisions adopted (plugin split, world subsystem, sim_core-owned derived cache, PMC behind one interface, Blueprint for UI/assets only, content under `/Game/CityPainter/`).
- Also standing: Unreal look work is not limited to v0 (Round 30: vegetation and traffic islands in; signs, houses, vehicles, people out). Realism bar: "almost bang on the photos", judged by cold-read agents on like-for-like recreations (`data/layouts/ref/`). First renders were "very clean and sterile": wear/dirt masks, material variation, vegetation and soft edges are goals.

## 5. In flight and known gaps

**Three code agents were running at the time of writing; their results are not known here.** Verify with `git log` before assuming any of this is done or open:
- **Grading rule changes**: wall height cap 3 m with benching, tapered earthwork ends, cutting toe ditch, `wing_*`/`round_*` parameters moved into region data. (JS and C++ must stay in lockstep via the goldens.)
- **Structure mesh fixes**: thin coping, visible 4 m panels, solid short-span block, abutments; re-renders go to `docs/progress/2026-10-10-structures-r2/`.
- **sim_core tool fixes**: `export_mesh` wrote invalid JSON; one skipped debug test.

Context: the first structure and earthwork renders (`docs/progress/2026-10-10-structures/`, INDEX and REVIEW) scored **4.5/10** with a cold reader; earthworks and walls were weakest (walls 8-10 m tall, slab tops, no visible panel steps, giant stacked walls between adjacent roads, embankment view empty).

Known and not done:
- The interchange test layout hard-codes its two carriageways 5.5 m off centre, giving a 0.2 m central reservation (should be 6.65 m for the 2.5 m median in data), so the 40 m bridge has no pier. Fixing it shifts expected numbers in about six test files.
- `docs/junctions.md` does not say that splitter length has a floor, that a dual carriageway is two one-way edges, or that a per-junction override can force a turn pocket while the feature is disabled.
- Grading has no junction footprints (item 5 of grading round 3); markings round 4 (ghost islands, studs, zig-zags, SLOW) not started; splitter markings never exercised; no tests for lane clear width, splitter shape, apron, class-pair radii, skewed T.
- Benchmark is partial (`docs/sim-performance-baseline.md`): junction meshing is 87% of mesh time at city scale; grading has no dirty path.
- Not started: blocks/parcels (next sim milestone after roads); Unreal ground (height-texture clipmap from `grading`); kit instancing and vegetation in Unreal; Poly Haven model download (offered, not confirmed); Infinigen trial (needs WSL2).

**Repo health** (`docs/repo-health-review.md`, rated "fair"). Done today: shared geom/json helpers, wrapper guard. Remaining structural items for later: split `markings.cpp`, `structure_mesh.cpp`, `render_mesh.cpp`; unify the wall/tunnel structs duplicated between `structure_mesh.h` and `grading.h`; move tunables into region data; make `RoadGraph` mutators non-public so edits cannot bypass `cp::Command`; add a second synthetic region pack to prove region-neutrality; tag tests fast/slow.

## 6. Next steps

1. Land or review the three in-flight agents (§5); commit with `[skip ci]`.
2. §1 steps 1-7: verify C: engine, tools test, First-Run, compile fixes, MCP, then week-one scaffolding per `docs/unreal-architecture.md` §6.
3. Week two per the same plan (ground layer stub from grading output, instance layer for light poles, golden screenshots, packaged smoke test, Movie Render Graph shots, PMC vs Realtime Mesh Core).
4. Road material with wear masks; structures and kit instancing; vegetation probe; one-day PCG trial before buildings.
5. Re-run photo comparisons each round with cold-read review; send the person pictures.
