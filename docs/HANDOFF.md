# Handoff: start here

Written 10 Oct 2026 (evening) at the end of the first local session, as it neared its usage limit. Branch: `claude/practical-ride-rrf8rx`. Read this, then `CLAUDE.md`, then `docs/planning-log.md` (decisions, now up to Round 32).

**Revised 10 Oct 2026 (late evening) by the following session:** the build verification in §0 is done, the Unreal install picture (§0, §5a) was wrong and is corrected, and machine constraints for the Unreal build were added (§1). Sections not mentioned here are unchanged.

## 0. State at handoff (read first)

- **Six agents were told to land cleanly and stop** because the usage limit was close; all six reported (final states in §5a, which supersedes §5 where they differ). **Verified later on 10 Oct:** clean from-scratch builds in `build-verify-msvc` (MSVC 19.51, Release) and `build-verify-gcc` (gcc 16, Ninja, Release) both configure and build; **235/235 test cases pass on both** (1,882,333 assertions, 1 skipped test case, not investigated); `bench`, `export_mesh`, `export_scene` build on both (the `bench.cpp` TileKey worry is resolved). MSVC: no warnings. gcc: 2 warnings in `sim_core/src/structure_mesh.cpp` (line 1293 -Wconversion; line 1023 unused function `portal_stub_top`). Prototype `scene.mjs --check`: 21/21. A clean build takes about 1-2 minutes per compiler. **Still not done: CI has not run on any of this** (first job: one normal commit so CI runs).
- **Unreal Engine 5.8 is NOT installed yet** (at 19:38 on 10 Oct the install was still running; see §5a "Unreal install" for the real state and how to check). Do not build the plugin until it finishes. The plugin has **never been compiled**.
- Everything is pushed. WIP pushes carry `[skip ci]`; the last CI-verified commit is still `1ae80b0` (roundabouts). Push a normal commit only when the suite is green on MSVC **and** gcc (it was, locally, see above).

## 1. First 10 minutes

Tools are not on PATH; use full paths.

- CMake/Ninja: `C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\{CMake\bin,Ninja}`. MSVC 19.51 (VS 2026).
  `cmake -S . -B build-msvc; cmake --build build-msvc --config Release -j; .\build-msvc\tests\Release\sim_core_tests.exe`
- gcc 16 (stricter than CI's gcc about missing includes; CI moves to Ubuntu 26 on 19 Oct): add `C:\msys64\mingw64\bin` to PATH, `cmake -S . -B build-gcc -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=g++`.
- Node 24: `C:\Program Files\nodejs`. Prototype: `cd prototypes/road-draw; node build.mjs; node tools/scene.mjs --check` (21 checks, ~6 s; `npm install` already done; drives installed Edge on the RTX 4060).
- Many `build-*` dirs exist (gitignored); delete freely.
- Unreal (only once the install has finished, §5a): `powershell -NoProfile -ExecutionPolicy Bypass -File unreal\tools\First-Run.ps1 -EngineRoot D:\UE_5.8` (engine detection works without launcher registration; find engine, build, environment report, import textures, set up look-test level, screenshots; resumable; see `unreal/tools/README.md`). Likely first-compile errors and their exact fixes: `unreal/tools/KNOWN_RISKS.md`. Human-only steps: accept a prerequisites prompt, wait out the first shader compile, enable **Unreal MCP** + **All Toolsets** plugins and Auto Start Server (then a new Claude session is needed to see the MCP server; `.mcp.json` is at the repo root).

## 2. Working rules

All in `CLAUDE.md`, plus these from the person today (also in Claude's local memory):

- **Decide road-engineering details yourself**: pick a sensible default, keep it in region-pack data, make it a player toggle/override where practice varies. Do not ask about dimensions, markings or conventions. Still ask about scope, product direction, tool feel, and whether pictures look right.
- **Background slow commands** (pushes, builds, CI watches). **`[skip ci]` on WIP pushes**: failed runs email the person.
- **Free or cheap first** (personal project for now); costs that only fall due at a release are fine.
- **Not UK-only** for anything region-neutral (shapes, structures, surfaces, light). UK matters only for markings, signs, kerb styles, driving side.
- Main session coordinates; Sonnet agents code; cold-read Sonnet agents review code and pictures; send the person progress pictures.
- Agents share one checkout: give each its own build dir, file ownership, `-j 4`, and "do not commit".

Machine constraints for the Unreal build (10 Oct): C: (internal NVMe, where the repo is) has only ~34 GB free; 16 GB RAM; Ryzen 9 7940HS; RTX 4060 plus integrated Radeon 780M (the editor may pick the integrated GPU); VS 2026 only (MSVC 14.51), not yet confirmed accepted by UE 5.8's UBT. D: is a slow USB disk (§5a). Expect a cold editor launch of 5-15 min from D: before shaders compile, and roughly 10-25 GB of C: used by the first build plus shader compile (estimate, unmeasured). A review of `unreal/tools` against this machine found: project-file generation with `-engine` would crawl on the USB disk; no DDC path was set; First-Run dropped stderr and showed no live output; timeouts were tight for a cold HDD; `KNOWN_RISKS.md` wrongly said `-nullrhi` warms the shader cache. Another agent was fixing `unreal/tools/**`, `DefaultEngine.ini`, `KNOWN_RISKS.md` and `unreal/tools/README.md` at the time of writing (new parameters such as `-LocalDdcPath` default `C:\UEDDC`, `-MaxParallelActions` default 4, `-GraphicsAdapter`): check `unreal/tools/README.md` for what actually landed. The `unreal-mcp` MCP server fails to connect until the editor is running with the plugin enabled (expected).

## 3. What exists

**sim_core** (headless C++20, exception-free; 235 doctest cases):
- Core from before: commands/undo, ids, clothoids, vertical profiles, road graph with structure spans, region pack loader, lanes, edge sampling, heightfield, earthwork cross-sections.
- **Junctions** (`junction*.cpp`, `docs/junctions.md`): damped fillet solver on curved kerbs, `degraded` + `degraded_reason`, `requested_radius` vs `radius`, merges/diverges.
- **Roundabouts** (`junction_roundabout.*`): explicit one-way road type flag `roundabout_ring`; ring = ordinary editable edges, a Junction at every ring node (2-arm Joint entries possible), `RoundaboutLayout` holds island, splitters, give-way; place tool 8–100 m. `build_junctions` applies roundabouts.
- **Render meshes** (`render_mesh.*`): cambered carriageways, 3D kerbs, pavements, junction surfaces welded to edges, islands, per-tile chunks (256 m), float-relative-to-double origin, UVs in metres, LOD by chord tolerance.
- **Markings** (`markings.*`, `data/regions/uk/marking_rules.json`): lines with warning variants, give-way, stop, roundabout entry, chevrons vs diagonal hatching, parking bays, arrows, zebras only for placed crossings.
- **Street lights** (`street_lights.*`), **grading** (`grading.*`: exact port of the prototype's `gradeTile`, wall runs, tunnel cut-out table; goldens in `tests/data/grading/` from `prototypes/road-draw/tools/grade_golden.mjs`), **structures** (`structure_mesh.*`: bridges, tunnels, retaining walls), **layout files** (`layout_io.*`, `data/layouts/`), **test layouts** (`test_layouts.*`), tools `export_scene`, `export_mesh`, `bench`.

**Prototype** (`prototypes/road-draw/`): grading, real tunnels (terrain discarded inside the bore in the shader), stepped walls, 21 invariants. Cold-read score 6/10 overall, tunnels 7/10. `prototypes/mesh-view/` renders sim_core OBJ exports headlessly and makes photo-vs-ours composites.

**Unreal** (`unreal/`): `CPRoadMeshActor` + UE-free `CPRoadMeshBuild.*` (interchange with real meshes, markings, materials; standalone-compiled and run, never compiled in UE), wrappers for every sim_core source, config for Lumen/VSM/Nanite/TSR, tooling in `unreal/tools/` (PowerShell + editor Python).

**Assets**: 10 Poly Haven CC0 2K texture sets (`unreal/CityPainter/SourceArt/Textures/PolyHaven/`, LFS, manifest + import script); Blender-scripted kit of 67 Nanite-ready pieces (`tools/blender_kit/`, FBX in `SourceArt/Meshes/Kit/`, headless `blender.exe --background`; kerbs, gullies, lamp columns matching the street-light style ids, barriers, parapets).

**References** (private, gitignored, never commit): `docs/reference/gsv/` (the person's 11 Street View shots + `INDEX.md` + `compare/` composites), `aerial/` (45, CC), `street/` (37, CC). Each has an INDEX of what makes it read as real.

**Docs added today**: `performance-plan.md`, `vegetation-and-surface-realism.md`, `sim-performance-baseline.md` (may be partial).

## 4. Decisions today (beyond the planning log's text)

- Roundabouts: explicit road type, per-node junctions, tool is a preset; 8–100 m; unbuildable → visible warning; U-turns allowed; splitters on two-way arms only; **lanes never narrow beside a splitter: the outer kerbs flare**.
- **Turn pockets are allowed** (Round 31): data rule + per-junction override. The old "junctions connect only drawn lanes" rule is gone.
- Zebras never automatic. Centre lines per road type (none on narrow urban streets except near junctions). Give-way triangle optional per set. Equal-class junction: straightest pair has priority.
- Earthworks: 1.5 m verge, 1:2 slopes up to ~12.75 m, walls only where a slope cannot fit; approaches to portals/abutments should be slopes with short wing walls (round 3, in progress); rounded slope tops/toes; walls step in 4 m panels (max step 0.5 m, thin coping); bridge parapet 1.0 m; spans < 20 m are a solid block unless they cross a road or water; pier spacing max 30 m.
- Unreal look work is not limited to v0 (Round 30): vegetation and traffic islands in; signs out; houses, vehicles, people out.
- Bar for realism: "almost bang on the photos, judged by cold-read agents", using like-for-like recreations (`data/layouts/ref/`) with matched cameras. First renders were called "very clean and sterile": wear/dirt masks, material variation, vegetation and soft edges are explicit goals.

## 5. In flight / known broken (verify each)

- **Render mesh round 2**: no kerbs on dual/motorway; grass roundabout island from data; wear/dirt vertex masks; junction UV stretch (cause: `blend()` blends v between arms); kerb tangents; merge/diverge pavement gap (fix in `junction_merge.cpp`); LOD not applied to junctions/islands; triangulator hole cases; island kerbs hovering; 2-arm joint filler; ~17 sliver triangles at the south dual carriageway's outer edge; optional ≤2-sections-per-tile packing with surface ids; **shared `cp/tile_key.h`** (render_mesh.h and grading.h both define `cp::TileKey`, so they cannot be included together); **public surface-height functions** for markings.
- **Markings round 3**: 12 edge-line marks at slip junction ends triangulate to nothing (a test fails); chevrons fragmented/outside carriageway; marks ignore camber/kerb height (float +125 mm, buried up to −198 mm): must call the mesh's surface function; triangle overlaps lane line on multi-lane arms; dash phase across joints; region leaks (`"mph"`, `surface == "asphalt"`). Round 4 (not started): the rest of the automatic markings (ghost islands, studs, zig-zags, SLOW, etc.); player-placed ones need a UX proposal.
- **Grading round 3**: sloped approaches, rounded tops/toes, no giant walls between adjacent roads, junction footprints (C++), workspace reuse. JS and C++ must stay in lockstep via the goldens.
- **Roundabouts/junctions round 3**: kerbless rural road types, flared/deflected entries, long shaped splitters, class-pair corner radii, lane-width rule, turn pockets (last).
- **Structures round 2**: first visual check, real wall runs from grading, region-pack fields, export + Unreal-side hookup.
- **Benchmark**: city-scale timings, edit loop, scaling, thread-safety list.
- Not started: blocks/parcels (the next milestone after roads); Unreal ground rendering (height-texture clipmap from `grading`); instancing kit meshes in Unreal; vegetation; reviewing the kit and the structures against photos; Poly Haven model download (offered, not confirmed); Infinigen trial (needs WSL2).

## 5a. Final agent states (supersedes §5 where they differ)

- **Render mesh round 2: done.** Dual-carriageway kerbs were caused by `junction_merge.cpp` sizing pavement by the widest arm (now per arm; also fixes the merge pavement gap). Shared `cp/tile_key.h` and `cp/triangulate.h`; `make_surface_profile`, `edge_surface_z`, `JunctionSurface`, `junction_surface_z`; vertex colours R wear, G edge dirt, B variation, A reserved, UV1 lane-relative; planar junction UVs; LOD inside junctions (interchange 14,414 / 5,596 / 2,798 / 2,314 triangles at 2 cm / 10 cm / 50 cm / 2 m); `build_joint_mesh`; `ChunkParams::merge_surfaces`. Left: sliver triangles are dropped by threshold, cause untraced; joint filler tested on synthetic roads only; junction kerb line on kerbless rural types still falls at the verge edge.
- **Markings round 3: done** except splitter markings (`generate_splitter_markings` built, never exercised). Marks follow the mesh surface functions; triangle and centre-line options in data; `mouth_edge`; `Marking::wear`; dash phase through joints. The 12 untriangulable edge lines could not be reproduced (test passes). Left: slip edge lines can leave `Junction::carriageway` near the nose; the roundabout test no longer asserts ring lane lines.
- **Grading round 3: items 1, 2, 3, 6 done in JS and C++** (10 goldens within 0.05 mm): approaches are slopes with a wing wall `min(10 m, 1.0 x H)`; `round_daylight` 2 m, `round_hinge` 1 m; neighbour-aware walls; `GradeWorkspace` (256x256 tile, 10 overlapping roads: ~24 ms). Invariants loosened: `ground_under_road` 5 -> 3 cm, `no_bare_steps` 1.0 -> 1.02 m. **Not done:** junction footprints in grading (item 5); `wing_*`/`round_*` not yet region-pack fields; no renders of the new rules, no visual review.
- **Roundabouts/junctions round 3:** road types `main_street`, `a_road_rural`, `b_road_rural`, `country_lane` (two 1.75 m half lanes: no shared-lane model); splitters by ring size and approach speed; island surface fields + apron; class-pair corner radii with skew factor; `ArmFlare` with lanes keeping width; dual-carriageway splitter. **No dedicated tests** for lane clear width, splitter shape, apron, class-pair radii, skewed T. **Turn pockets: code present, disabled** (`junction.turn_pockets.enabled = false`), no override command, no tests; design note in `docs/junctions.md` §7b. (The planning log now has Round 31 for turn pockets and Round 32 for the 10 Oct decisions that were only in this handoff: commits `fc3b03d`, `3c103b5`.)
- **Structures round 2: partial.** Done: tunnel ledge, pier spacing 30 m with avoidance of roads/junctions/other piers, `StructureRules` in the region pack, structures in `export_mesh`. Not started: any render or visual check, real wall runs from grading, unifying copied structs with grading's, Unreal hookup, thinner wing-wall tops. Finding: the interchange's central reservation is only 0.2 m wide, so the 40 m bridge is flagged `span_too_long` with no pier: widen the dual type's median or the fixture.
- **Benchmark: partial** (`docs/sim-performance-baseline.md`, `sim_core/tools/bench.cpp`). Large grid, one run, busy laptop: 20,252 edges, 10,440 junctions: junctions 5.9 s, markings 2.5 s, mesh 63 / 19 / 9.8 / 7.4 s at 0.02 / 0.1 / 0.5 / 2.0. Junction meshing is 87% of mesh time; connectors 62% of the junction build. Edit loop (small nets): dirty 20–150 ms vs full 260–850 ms. Bugs found: `erase_node` drops removed nodes from the dirty set (stale edges); street lights need the dirty set widened one hop; grading has no dirty path; there is no change-road-type command. Degraded junctions: grid < 0.3%, organic 1–4.5%, motorway 25% when junctions are ~47 m apart. `bench.cpp` had a `TileKey` workaround removed by another agent: check it still builds.
- **Unreal install: slow, not stalled, and NOT finished.** (The earlier note said it was stuck at "Installing 83%" with files present; that was wrong.) D: is a **USB spinning hard disk** (WD My Passport, 1.8 TB), the root cause of the slowness. The ~29 GB download had completed; the Epic install helper was in "backup and stage relocation", moving 250,504 files out of `D:\UE_5.8\.egstore\bps` into place at ~100 staged chunks/s and slowing. The launcher's "Installing 83%" bar does not reflect this phase. `Engine\Source` and `Engine\Shaders` did not exist yet; ~163,000 chunks were staged, and at 19:38 about 110,000 still were. **Leave it running.** The earlier advice (pause/resume, restart the launcher, run as administrator) was harmful: each restart cancels the relocation and it has to resume.
  - Check progress: the real log is `%LOCALAPPDATA%\Epic Games\Epic Online Services\InstallHelper\Logs\UE-*.log` (process `EpicOnlineServicesInstallHelper.exe`), not the launcher log. Remaining work = file count of `D:\UE_5.8\.egstore\bps\f`. **Done** = `D:\UE_5.8\Engine\Source` exists and `C:\ProgramData\Epic\UnrealEngineLauncher\LauncherInstalled.dat` lists UE_5.8 (still an empty list at 19:38).
  - The person added a Windows Defender exclusion for `D:\UE_5.8` (about 15% faster moves). `Find-Unreal.ps1 -EngineRoot D:\UE_5.8` works without the launcher record.

## 6. Next steps

1. §1 verification is done locally (§0); remaining: finish §5 rounds (optionally clear the 2 gcc warnings), then one normal commit (no `[skip ci]`) so CI runs.
2. Wait for the Unreal install to finish (§5a; do not restart it), then Unreal first run with `-EngineRoot D:\UE_5.8`; fix compile errors from `KNOWN_RISKS.md`; first screenshots; then `docs/performance-plan.md` day-one measurements (hardware RT Lumen vs Lumen Lite on runtime tiles first).
3. Road material with wear masks; ground from `grading`; structures and kit instancing; vegetation probe.
4. Re-run the photo comparisons each round; cold-read review; send pictures.
