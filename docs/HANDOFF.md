# Handoff: start here (for a new local Claude Code session with Unreal)

Written 10 Oct 2026 at the end of the first cloud session. Branch: `claude/practical-ride-rrf8rx`. Read this, then `CLAUDE.md` (working rules), then `docs/planning-log.md` (source of truth for decisions).

## 1. First 10 minutes

1. `git pull` on `claude/practical-ride-rrf8rx`. The last commits may be **WIP snapshots** of agents that were still running when the cloud session ended (see §5), so verify before building on them:
   - `cmake -S . -B build && cmake --build build -j && ctest --test-dir build --output-on-failure` (sim_core; must be green, zero warnings)
   - `cd prototypes/road-draw && node build.mjs && node tools/scene.mjs --check` (prototype invariants; was 12/12 before the last fix round)
2. GPU: the person's PC is an **RTX 4060**. The browser previously ran on "Microsoft Basic Render Driver" (no GPU driver); after fixing it the prototype runs ~60 fps. If Unreal or the prototype is slow, check `chrome://gpu` / Device Manager first.
3. Unreal: **UE 5.8**, VS 2026/2022 installed per `docs/windows-setup.md`. Set up MCP per `unreal/README.md` → "Claude Code + Unreal MCP": Epic's built-in **Unreal MCP** plugin (experimental in 5.8). Enable it plus All Toolsets, run `ModelContextProtocol.GenerateClientConfig ClaudeCode` (writes `.mcp.json`), and start `claude` from the repo root. First check whether it exposes screenshot, console, build and log tools (unverified).

## 2. Working rules (from the person, all in CLAUDE.md)

- The main session (Opus) **plans, asks questions, coordinates**. All coding goes to **Sonnet subagents**, with Haiku only for trivial mechanical edits. The main session **never audits screenshots or code itself**: reviews go to cold-read Sonnet agents.
- **Parallel agents by default**, split by file/function with tight briefs (exact functions, acceptance checks, screenshot budgets). Continue an existing agent via message rather than spawning a new one for follow-ups. **Give every agent a stop condition and cancel it the moment it's unnecessary.** Agent baseline cost is ~50k tokens, so avoid tiny agents.
- Verify with **text checks** (`ctest`, `tools/scene.mjs --check`), not screenshot loops. Visual judgement: one cold-read Sonnet review per round against **real-world** roads. Send the person progress pictures (reviewed ones).
- **Commit and push at the end of every turn.** Ask the person often on design and UX, and keep reversible choices moving.
- **UK is only the first region pack.** The core stays region-agnostic (`data/regions/<region>/`).

## 3. What exists

**sim_core (C++20, headless, permanent):** `sim_core/`, tests in `tests/` (~90 doctest cases).
- Commands/undo, stable IDs; clothoid G1 fit + G2 chain fitter; vertical profiles (constant grade + parabolic curves).
- Road graph (nodes/edges, draw/split/delete/move, **structure spans** bridge/tunnel with undo); region pack loader (exception-free, nlohmann/json); lane instantiation (side-agnostic); edge/offset sampling with fold detection.
- Heightfield + earthwork cross-sections (slope/wall/bridge/tunnel), slope mesh, corridor footprint.
- **Junction generator** (`junction.h`, `docs/junctions.md`): arms, node classification, trims, lane connectors (A), kerb/carriageway/pavement/crossing geometry (B, Clipper2), merges/diverges with full-length tapers and gores (C). Overrides keyed by stable IDs.
- Clipper2 vendored (`third_party/clipper2`). sim_core builds with `-fno-exceptions`.

**Unreal skeleton:** `unreal/CityPainter/` (uproject, game module, plugin `CityPainterSim` compiling sim_core via one `Wrap_<file>.cpp` per sim_core source; CI checks for missing wrappers). `ACPRoadNetworkActor` draws a few hard-coded roads (debug lines + ProceduralMesh ribbon) with Undo/Redo. **Never compiled**: the known risks are listed in `unreal/README.md`. Coordinates: north = +X, east = +Y, cm (`CPCoords.h`).

**Prototype (three.js, the reference for drawing feel and earthworks):** `prototypes/road-draw/`. Edit `src/*.js`, run `node build.mjs`, open `index.html` (needs internet for three.js). The controls are listed in its README (H in-app). `tools/scene.mjs` gives a canonical scene, 9+ fixed views, and `--check` (~13 invariants, ~50 s).
- Architecture (decided after several failed approaches): **one graded ground surface for the whole network** on a 0.5 m grid (`earthworks.js` `gradeTile`, pure and tile-based: port this to sim_core), rendered as a GPU clipmap with per-pixel normals (`terrain.js`). **Meshes only for structures** (walls 1.5 m solid hiding the grid step, bridges, portals) in `roadmesh.js`. Every ground step must be hidden inside a structure.

**Docs:** `planning-log.md` (decisions, rounds 1–28+), `road-graph-model.md`, `junctions.md`, `pre-research.md` (incl. Unreal MCP and runtime terrain), `unreal-photorealism.md` (UE 5.8 feature verdicts plus a 12-item look-test checklist), `road-marking-rules.md`, `windows-setup.md`.

## 4. Key decisions (beyond the planning log's earlier rounds)

- Edges only (no Road layer). Dual carriageways and motorways: one stroke places two independent one-way carriageways; heights are referenced to the centreline (level median, inner carriageway steeper on curves). One-way mode draws a single carriageway.
- Junctions connect only drawn lanes (no auto turn pockets). Road class and corner radius live in the region pack. U-turns only via override.
- Unreal: ProceduralMeshComponent for now (Geometry Script is evaluated in the look test). World Partition from day one. **Landscape can't be edited at runtime in shipped games**, so the plan is our own height-texture-driven ground mesh (as in the prototype), with Virtual Heightfield Mesh as the fallback; terrain is stored as tiles in sim_core (coarse everywhere, 0.5 m where roads touch).
- Photorealism verdicts: adopt Lumen (test on runtime meshes; compare with Lumen Lite), VSM, TSR/DLSS, RVT for road–terrain blending, geometry markings, a custom terrain material and water. Avoid depending on Mesh Terrain, the Water plugin, stock HLOD for generated content, and Mass.

## 5. In flight when the cloud session ended (verify; may be partial in WIP commits)

- Prototype realism round (from a cold-read review scoring 3/10):
  - **earthworks.js:** sloped tunnel approach; tight-curve high-fill curtains (the person's own scene); walls only where a slope can't fit, with a 15 m minimum; a flat graded verge; the `no_bare_steps` check; a `tight_curve_fill` scenario.
  - **roadmesh.js (bridges/portals):** an opaque portal bore; parapets/abutments/pier caps; no short accidental bridges.
  - **roadmesh.js (walls/edges):** smooth wall tops; no grass verge meshes (the verge is graded ground); no folding walls on tight curves.
- **Junction fixes** from a cold-read code review (12 issues): geometry robustness + `Junction::degraded` + fuzz (junction_geometry.cpp); connector fallback/overrides + fuzz (junction_connectors.cpp); slip side from geometry, overrides on merges + fuzz (junction_merge.cpp).
- **Roundabouts** (`junction_roundabout.cpp`): a place tool plus recognition of hand-drawn rings, with entries, exits, islands and splitters.
- **The person's test layout, "interchange"**: an E–W dual carriageway at grade, with an N–S minor road bridging over it; four diamond slips; a **roundabout** at the north terminal; a **crossroads** at the south; and a **T-junction** side road. It's built by `sim_core/tools/export_scene.cpp` into `prototypes/road-draw/fixtures/interchange.json`, and viewed in the prototype's new **Inspect mode** (`src/inspect.js`, J toggles the connector overlay).

## 6. Next steps (suggested order)

1. Verify §1, and finish or redo anything from §5 that's incomplete (re-run checks, then one cold-read review).
2. Unreal **compile test**: build the skeleton, fix compile issues, place `ACPRoadNetworkActor`, and confirm roads render. Then the **look test** from `docs/unreal-photorealism.md` (Lumen on runtime meshes, VSM, RVT, TSR/DLSS at 1440p/60).
3. Port grading (`gradeTile`) to sim_core; build Unreal ground rendering (a height-texture clipmap) and structure meshes from sim_core cross-sections/junction geometry.
4. Junction markings (give-way lines, centre/lane/edge lines, hatching) from `road-marking-rules.md` as region-pack data.
5. Open questions in the planning log §4 (wall cap value, the slope-to-wall threshold, "breaks graphically" rules, v0 pass criteria, RHT markings).
