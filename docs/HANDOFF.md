# Handoff: start here

Written 10 Oct 2026 (evening) at the end of the first local session, as it neared its usage limit. Branch: `claude/practical-ride-rrf8rx`. Read this, then `CLAUDE.md`, then `docs/planning-log.md` (decisions, now up to Round 31).

## 0. State at handoff (read first)

- **Six agents were told to land cleanly and stop** because the usage limit was close: render mesh round 2, markings round 3, grading round 3, roundabouts/junctions round 3, structures round 2, sim benchmark. Their last reports may not have arrived. **The tree is NOT verified green.** First job: build and run the whole suite (§1), then finish or back out whatever is half-done (§5).
- **Unreal Engine 5.8** was in the last stage of installing to `D:\UE_5.8` (files present, launcher not yet listing it in `C:\ProgramData\Epic\UnrealEngineLauncher\LauncherInstalled.dat`). The plugin has **never been compiled**.
- Everything is pushed. WIP pushes carry `[skip ci]`; the last CI-verified commit is `1ae80b0` (roundabouts). Push a normal commit only when the suite is green on MSVC **and** gcc.

## 1. First 10 minutes

Tools are not on PATH; use full paths.

- CMake/Ninja: `C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\{CMake\bin,Ninja}`. MSVC 19.51 (VS 2026).
  `cmake -S . -B build-msvc; cmake --build build-msvc --config Release -j; .\build-msvc\tests\Release\sim_core_tests.exe`
- gcc 16 (stricter than CI's gcc about missing includes; CI moves to Ubuntu 26 on 19 Oct): add `C:\msys64\mingw64\bin` to PATH, `cmake -S . -B build-gcc -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=g++`.
- Node 24: `C:\Program Files\nodejs`. Prototype: `cd prototypes/road-draw; node build.mjs; node tools/scene.mjs --check` (21 checks, ~6 s; `npm install` already done; drives installed Edge on the RTX 4060).
- Many `build-*` dirs exist (gitignored); delete freely.
- Unreal: `powershell -NoProfile -ExecutionPolicy Bypass -File unreal\tools\First-Run.ps1` (find engine, build, environment report, import textures, set up look-test level, screenshots; resumable; see `unreal/tools/README.md`). Likely first-compile errors and their exact fixes: `unreal/tools/KNOWN_RISKS.md`. Human-only steps: accept a prerequisites prompt, wait out the first shader compile, enable **Unreal MCP** + **All Toolsets** plugins and Auto Start Server (then a new Claude session is needed to see the MCP server; `.mcp.json` is at the repo root).

## 2. Working rules

All in `CLAUDE.md`, plus these from the person today (also in Claude's local memory):

- **Decide road-engineering details yourself**: pick a sensible default, keep it in region-pack data, make it a player toggle/override where practice varies. Do not ask about dimensions, markings or conventions. Still ask about scope, product direction, tool feel, and whether pictures look right.
- **Background slow commands** (pushes, builds, CI watches). **`[skip ci]` on WIP pushes**: failed runs email the person.
- **Free or cheap first** (personal project for now); costs that only fall due at a release are fine.
- **Not UK-only** for anything region-neutral (shapes, structures, surfaces, light). UK matters only for markings, signs, kerb styles, driving side.
- Main session coordinates; Sonnet agents code; cold-read Sonnet agents review code and pictures; send the person progress pictures.
- Agents share one checkout: give each its own build dir, file ownership, `-j 4`, and "do not commit".

## 3. What exists

**sim_core** (headless C++20, exception-free; ~190 doctest cases):
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

## 6. Next steps

1. §1 verification; finish §5 rounds; one green commit (MSVC + gcc), let CI run.
2. Unreal first run; fix compile errors from `KNOWN_RISKS.md`; first screenshots; then `docs/performance-plan.md` day-one measurements (hardware RT Lumen vs Lumen Lite on runtime tiles first).
3. Road material with wear masks; ground from `grading`; structures and kit instancing; vegetation probe.
4. Re-run the photo comparisons each round; cold-read review; send pictures.
