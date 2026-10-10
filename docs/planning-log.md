# City Painter: Planning Log

Living plan for a photorealistic, sandbox city builder with a "city painter" focus. Code is written with Claude Code, so this doc is for planning only. Companion docs: the road graph data model (docs/road-graph-model.md), the v0 kickoff brief (docs/v0-brief.md), v0 pre-research findings (docs/pre-research.md) and UK road marking rules (docs/road-marking-rules.md). Last updated: 10 Oct 2026.

## 1. Vision

- A city builder leaning toward painting and creating rather than economy management.
- Photorealistic when zoomed in, statistical (but still visible) when zoomed out.
- Sandbox only for now: no demand, economy, services or utilities. These come later.
- UK visual theme to start. UK is only the first region pack, used for prototyping: the core stays region-agnostic, and all region-specific rules (road types, markings, clearances, units) live in swappable data packs.

## 2. Decisions made

### Engine and workflow

- **Unreal Engine with C++.** Free until $1M revenue, then a 5% royalty (confirmed on Epic's licence page on 10 Oct 2026: first $1M lifetime gross revenue free, Epic Games Store sales royalty-free). Chosen for visuals (Lumen, Nanite, vegetation, weather).
- **Sim core is an engine-agnostic, headless C++ library** with a test runner. A thin Unreal plugin renders it. This lets Claude Code build and verify without the editor.
- **Commands and undo:** every player edit is a command on the sim core (undo, saves and tests follow).
- **Determinism:** fixed timestep, sim on its own thread, renderer interpolates.
- **Geometry:** use a proven library (Clipper2 or Boost.Geometry; avoid CGAL, as most of its algorithm packages are GPL and Unreal's licence forbids combining engine code with copyleft code), fuzz-test on messy layouts.
- **LOD for everything:** buildings, trees, props, markings and shadows, not just cars. Plan for World Partition and large-world coordinates.
- **Claude Code workflow:** headless unit and golden tests, debug visualisations, screenshot tests for geometry.
- **UX test tools:** for UX questions such as road drawing, Claude Code builds a standalone test tool the person can try and give feedback on, rather than guessing the UX. The first one is road drawing (clothoids).
- **Pre-Unreal prototypes:** Claude Code builds the standalone prototypes itself, desktop and mouse-driven, before Unreal is set up. The first two are road drawing (clothoids) and allowable plot design on steep land.
- Blender MCP is used for the module kit, driven by bpy scripts (see Buildings).

### Targets and platform

- **Platform: Windows PC only.**
- **Target hardware: mid-range, RTX 3060 class.** Photorealism has to fit that budget on 20 km+ maps. Set frame-time budgets early, plan for scalability settings and upscaling, and treat LOD (buildings, vegetation, cars, pedestrians, shadows) as core, not polish. This raises the priority of the early look and performance test.
- **Performance target: 1440p at 60 fps with upscaling on an RTX 3060 class GPU.** Demanding for photoreal on huge maps. Per-frame budgets (draw calls, shadows, vegetation, traffic) get set at the first look test and tracked from then on.
- **Maps of 20 km and larger.** This forces streaming (World Partition), large-world coordinates, and sim data that can load and unload by region from the start. The CTM suits this because it is cheap per lane.
- **Camera: free orbit only** for v1. Street-level and photo-mode cameras come later.
- **Definitions live in data files:** road types, building styles, module lists and materials. This makes modding possible but does not deliver it. Modding would also need stable versioned formats, validation, and an import path for custom meshes, so the commitment is deferred.

### Roads: drawing and rules

- Roads are **hand-drawn splines**, Cities: Skylines style. No fixed junction prefabs.
- **Roads are drawn by clicking points,** and the tool fits clothoid curves through them.
- **Clothoid road curves.** Centrelines are clothoid (Euler spiral) transitions joined to arcs and straights, so curvature changes smoothly like real roads. This costs more geometry work than Beziers (lane offsets, curve intersections, junction corners), so milestone 1 should prototype it early.
- **Snapping:** no grid. Angle and length increments, plus snapping to road guidelines as in CS1: extension of a road, tangent continuation and parallel offset.
- **Feedback, not restrictions.** Curvature and grade show live while drawing, and the player is only blocked when the result breaks graphically. Gradient limits are advisory only, like curvature. Since sharp curves and steep grades are allowed, the traffic sim should derive free-flow speed from the geometry.
- **The road graph is 3D from day one** (bridges, tunnels, overpasses, multi-level junctions), with stable IDs.

### Roads: types, lanes and markings

- **Four road types at launch:** residential street, B road / main road, A road / dual carriageway, and motorway with slip roads. Slip roads and grade separation mean the junction model needs merges, diverges and ramps early.
- **Lane widths are baked into the road type,** not player-editable. Road types are data-defined presets, which keeps junction generation predictable.
- **Road markings** are generated automatically from the lane graph and junction geometry, using the draft UK rules reference as a starting point. The goal is sensible, recognisable markings, and the person tunes the exact numbers by eye.
- **v1 road users:** on-street parking, pavements and crossings. Bus lanes, cycle lanes and their traffic come later. Parked cars come from the traffic sim. All of these, including the later bus and cycle lanes, are lane types in the graph, so lane types must be extensible and carry their own rules (who may use them, who may park, where they connect).
- **One-way roads are in v1.** Direction is per lane in the graph, and roundabouts depend on it.
- **Driving side is one setting per save.** No mixed-side maps, so junction rules never have to reconcile two conventions in one network. The lane graph and junction rules must be side-agnostic: priority, turning conflicts, roundabout direction and signal layouts mirror from the one setting.

### Junctions

- A junction appears where roads meet. Its default lane connectors and curb geometry are **generated from the lane graph the player drew** (lane graph first, geometry second).
- Junctions are first-class and editable: the player can override any connector, lane or corner radius. Goal: avoid CS1's janky connectors, and avoid CS:Ur's bad UI.
- **Roundabouts: both a dedicated tool and hand-drawing.** Either route must produce the same junction object, recognised as a ring of one-way segments. Circulation direction follows the driving side setting (clockwise for left-hand traffic).
- **Slip roads are hand-drawn like any road,** with no templates. The junction generator must produce sensible merge and diverge geometry and connectors from the lanes the player draws.
- **Junction control types in v1:** priority / give way, traffic lights, zebra and signal crossings, and roundabouts. All are properties of the junction object. Zebra crossings give pedestrians priority, which the sim must model.

### Elevation and terrain

- **Full terrain sculpting.** Terrain is editable at runtime. Roads, parcels and buildings must react to edits, and every sculpt is a command (undo, saves). On 20 km+ maps this means tiled terrain data that saves as edits rather than a full copy.
- **Terrain locks under roads, free elsewhere.** Sculpting never deforms existing roads. Roads and buildings own the ground beneath them, so the sim needs a locked mask derived from road and parcel footprints, and drawing a road grades the terrain once. Sculpting is also blocked near bridges and tunnels.
- **Constant gradient per segment:** from one clicked point to the next, like the "straight slope" mode in Cities: Skylines. The road does not follow the terrain; the terrain conforms to the road.
- **Cuttings and embankments** are generated in the terrain to meet the road. This is the same "road grades the terrain once" rule as above.
- **Smooth gradient changes** between segments, including where roads meet (vertical curves), so junctions and joins never show a kink in gradient. The road graph needs a vertical profile per segment, not just a node height.
- **Every point's height is the terrain height at that click,** adjustable with PgUp and PgDn. A road that starts from another road inherits that road's height.
- **Deep cuttings and high embankments keep going,** with retaining walls up to a hard height cap. Above the cap, the road is forced into tunnel or bridge mode. Bridges and tunnels come from a dedicated tool, and use constant gradient like other roads.

### Water

- **Water in v1:** rivers, lakes and sea. Water is part of the terrain data, parcels exclude it, and roads cross it with bridges. It needs an early look test alongside the road and house scene, since water rendering and sculpting (shorelines, riverbeds) interact.
- **Water is static for now, dynamic later.** Painted levels and shorelines in v1. Store a level per water body so dynamic water can be added without reworking the data.

### Parcels, zoning and steep land

- Parcels, not cells. Road network defines blocks; blocks are subdivided into lots of varying size by frontage and depth.
- Each parcel has a frontage edge, setbacks and a shape. The house and garden fit whatever the parcel gives.
- **Zoning: the player draws a zone area and parcels fill it.** They choose land use, density, building type and style, with style set per zone. This replaces the earlier idea of painting a land use or style directly onto individual parcels.
- **Full parcel control: split, merge and resize.** The subdivision generator gives the defaults and player edits are stored as overrides, the same pattern as junction connector overrides. Edits are commands, persist in saves, and regenerate the building on that parcel. A parcel with a building on it cannot be edited; demolish first. When a road edit touches the building part of a parcel, that building is demolished and the parcel re-flows. Edits touching only the rest of the parcel trim it and keep the building. Affected buildings are highlighted for the player before a road edit is confirmed.
- **Steep land (direction to prototype in milestone 3):** each plot has a building platform, a sub-area under the foundations that is graded gently (not fully flat). The rest of the plot blends to the surrounding terrain, so the transition ends up slightly steeper. Cut and fill is capped so ground never looks bulldozed.
- **Extreme slopes are allowed,** using tall foundations or retaining walls rather than refusing the parcel. The building system needs foundation modules and a way to read the ground under the footprint. Where foundations would exceed the height cap, the building steps down in tiers instead. The cap value is set by prototyping.
- **Platform grading is automatic and invisible.** The player never edits it.
- **Land with a building on it is locked against terrain editing,** platform and blend alike, the same rule as roads. After demolition the ground stays graded and the lock lifts.

### Buildings

- Procedural from building blocks, no large hand-made asset library. UK typologies (terraced, semi, detached, flats over shops, Victorian, Georgian, 1930s) suit rule-based generation.
- **All building types at launch** (terraced, semi-detached, detached, flats, shops), using simple placeholder models. Breadth first, art polish later.
- The **game assembles buildings floor by floor** from a data-driven style definition (module lists, proportions, materials, roof types).
- **Blender kit:** about 50 to 100 fixed-size modules (windows, doors, bays, chimneys, porches), generated by bpy scripts and checked via Blender MCP screenshots. Conventions fixed early: unit grid, pivots, naming and sockets, UV and trim-sheet approach.
- **Generated at runtime in Unreal:** plain walls, fillers, roofs and floor slabs, since they stretch to arbitrary parcels.
- Variation sells realism: per-building brick, mortar, tile colour, weathering, plus small details (gutters, chimney pots, fences, hedges, bins, parked cars). PBR materials can come from Fab.
- Buildings are seeded from the parcel ID so they are deterministic.
- Wall-fitting algorithm (distributing slack into stretchable filler) is deferred to milestone 3 and will be found by prototyping.

### Scenery and props

- **v1 scenery: trees and hedges, grass and ground cover.** Street furniture and signs were not selected for v1 and can be added later. Street lights and road markings are part of the road types, and parked cars come from the traffic sim.
- **Planning note:** vegetation is likely to dominate frame cost on an RTX 3060 class GPU, so instancing and LOD for it belong in the early performance test.

### Traffic

- **Dual fidelity.** Near the camera: individual agents with car-following and lane changes. Zoomed out: a mesoscopic **cell transmission model (CTM)** per lane (cells of about 10 to 20 m, fundamental diagram per cell).
- **Vehicle traffic in v1 is cars only.** Buses, bikes and their lanes come later. Keep vehicle classes and lane types extensible so adding them does not touch the CTM core.
- **Pedestrians use the same dual fidelity as cars.** Flow-based and cosmetic at macro scale, visible agents near the camera. Crossings are conflict points with traffic, so they belong in the junction model from the start.
- Real signal phases, per-connector capacity, spillback and gridlock, conflicting movements sharing capacity.
- Cars are always visible: instanced, cosmetic, placed from cell densities, so queues visibly reach back from red lights.
- **Handoff** between macro and micro uses matching state at the boundary, a hysteresis band, and a short crossfade of behaviour differences.
- Trips come from a simple per-parcel trip generator (by type and size).
- Pathfinding is hierarchical (for example contraction hierarchies), with costs fed from the CTM so traffic reroutes. Lane-level routing only near the camera.

## 3. Roadmap

Vertical slice first: one small map with hand-drawn roads, a few junctions, parcels with houses and gardens, and cars at both zoom levels.

**Settle before building (expensive to change later):**

1. Road graph data model
2. Command and undo architecture
3. Project skeleton (headless sim library, test runner, thin Unreal plugin)
4. Early Unreal look test: one hand-placed road, a house and a car, at target frame rate

**Milestones:**

1. Spline roads, lane graph and junction generator, with debug rendering only
2. Block extraction and parcel subdivision on messy layouts
3. One parametric house plus garden fitting arbitrary parcels, including steep ground (riskiest visual question)
4. Micro traffic, then the CTM macro model, then the LOD handoff
5. Rendering, terrain and art polish

**v0: the full road painter test.** The first playable build, and the main test of the road, terrain and junction tools before anything lives on them.

- **In v0:** every road and terrain feature planned for v1 except junction control: clothoid road drawing with snapping and guidelines, all four road types, one-way roads, roundabouts, slip roads, junction geometry, pavements, crossings, parking lanes, road markings, street lights, terrain sculpting, cuttings and embankments, retaining walls, bridges and tunnels, water, undo and redo, and debug overlays (lane graph, junction connectors, curvature and gradient, cut and fill).
- **Not in v0:** cars, pedestrians and other agents (including parked cars), junction control (signals and priority rules, which come with cars), vegetation (comes with buildings), saves and loading (later), parcels, zoning, buildings and the systems that depend on them.

**v0 passes** when a junction-heavy network on hilly terrain runs at the target frame rate and drawing and editing roads feels good. The final criteria will come from the person. **v0 build order:** terrain and roads together from the start, not roads on flat ground first.

## 4. Open questions

### Roads, terrain and v0

- What is the height cap value for retaining walls and foundations? Set by prototyping the steep-land plots.
- At what steepness do cutting and embankment sides switch from sloped earth to retaining walls?
- What counts as "breaks graphically": which validity rules block road placement (self-overlap, degenerate corners, minimum segment length)?
- Final v0 pass criteria: to be set by the person, on top of the working definition above.
- Road markings for right-hand traffic: mirror the UK rules, or use another national standard?

### Project

- Save format, and whether to commit to modding later (data-only mods like road types and styles, or custom meshes and code)?

## 5. Parked for later

- Economy, demand, services, utilities and citizens
- Public transit
- Time of day and weather
- Photo mode (high relevance for a painter game)
- Modding support
- Additional building styles beyond the first UK set
- Infinigen (open-source, Blender-based procedural generator, BSD licence) as an offline factory for photoreal vegetation and ground materials, exported as static assets. Not used at runtime. Test a few trees on the RTX 3060 target when vegetation arrives after v0. Unreal 5.7's experimental Procedural Vegetation Editor may cover part of this, so compare both.

### Tools and ideas considered

Sorted by when they are needed. Licences and current feature status are unverified and must be checked before use.

**Adopt for v0:**

- **Real UK data as test fixtures:** OpenStreetMap road networks, Ordnance Survey open data and open LiDAR terrain, imported by Claude Code to fuzz-test junction generation and (later) block and parcel logic on real, messy layouts. Check each dataset's licence.
- **Runtime Virtual Texturing:** evaluate in the early look test, for blending roads, markings and wear into the terrain.
- **Geometry Script (or equivalent runtime mesh code):** for generated road, retaining wall, cutting and bridge meshes.

**Later, after v0:**

- **PCG framework:** rule-based scattering of vegetation and props, using Infinigen-made assets.
- **Traffic-driven visuals:** road wear, tyre marks and queue brake lights at night, driven by flow data.
- **Traffic-driven audio (MetaSounds):** soundscape generated from the flow model.

**Maybe:**

- **Starter terrain tools** (for example Gaea) for eroded base heightmaps. A heightmap import path is needed anyway for LiDAR fixtures.
- **Real-place maps** that players can start from. Needs licence and scale checks.

## 6. Change log

- 10 Oct 2026, start: log created from the planning discussion.
- Round 1: switchable driving side (later narrowed to one setting per save), 20 km+ maps, v1 road users.
- Round 2: full terrain sculpting, clothoid road curves, hybrid flow-plus-visible pedestrians.
- Round 3: terrain locked under roads, roundabouts by tool and by hand, water in v1.
- Round 4: driving side one setting per save, static water for now, RTX 3060 class target.
- Round 5: 1440p at 60 fps with upscaling, click-point road drawing, cars-only vehicle traffic. Bus and cycle lanes moved out of v1. UX test tools added to the workflow.
- Round 6: first UX test tool is road drawing, sculpting blocked near bridges and tunnels, free orbit camera.
- Round 7: lane widths baked into road types, four road types, angle, length and guideline snapping with no grid.
- Round 8: Windows PC only, hand-drawn slip roads, data-driven definitions (modding deferred).
- Round 9: junction control types, one-way roads, building platform plus blend for steep land.
- Round 10: automatic invisible platform grading, built-on land locked, extreme slopes allowed with foundations or walls.
- Round 11: zone areas with parcels filling them, full parcel editing, all building types with placeholder models.
- Round 12: prototyping moves to Claude Code on desktop (a throwaway chat prototype was made). Feedback: constant gradient per segment, terrain cuttings and embankments, smooth gradient changes between segments.
- Round 13: every point's height is terrain height (PgUp and PgDn adjust), dedicated bridge and tunnel tool, no parcel edits once built on, zoning chooses land use, density and building type, demolition lifts the ground lock.
- Round 14: style set per zone, road edits demolish buildings only when they touch the building part of a parcel, road guidelines (extension, tangent continuation, parallel offset).
- Round 15: road edits touching only a parcel's outer part trim it and keep the building, affected buildings highlighted before confirming.
- Round 16: gradient limits advisory, hard cap on wall and foundation height (value set by prototyping) that forces bridge or tunnel mode, buildings step down in tiers beyond it, v1 scenery is trees, hedges, grass and ground cover.
- Round 17: v0 defined as the full road painter test (all v1 road and terrain features, no cars or buildings). Road markings and street lights are in v1, parked cars come from the traffic sim, and bridges and tunnels use constant gradient.
- Round 18: v0 excludes junction control (comes with cars), vegetation (comes with buildings) and saves (undo and redo only for now).
- Round 19: v0 debug overlays are lane graph, junction connectors, curvature and gradient, and cut and fill. v0 passes on both frame rate and feel (final criteria to come), and is built terrain and roads together.
- Round 20: road markings are automatic, with Claude Code spawning a research agent for the official junction marking rules. The v0 kickoff brief for Claude Code was drafted as a separate doc.
- Round 21: Infinigen parked as an offline vegetation and material factory. Tools and ideas sorted into adopt for v0 (real UK test data, Runtime Virtual Texturing, Geometry Script), later (PCG, traffic-driven visuals and audio) and maybe (starter terrain, real-place maps). Max 5x upgrade blocked by Google Play billing; support escalated.
- Round 22: pre-research done and written up in the doc City Painter: v0 pre-research findings. CGAL ruled out (GPL), Clipper2 chosen, Unreal terms confirmed, UK data licences and road marking sources identified. Marking rules still to be extracted.
- Round 23: first extraction of UK road marking rules (Traffic Signs Manual Chapter 5, sections 1 to 5) written up as the doc City Painter: UK road marking rules (draft). Roundabouts, grade-separated junctions, yellow boxes, arrows and the Chapter 3 and 6 markings are still to extract.
- Round 24: road marking research stopped at the draft reference. Markings must be generated sensibly, with exact numbers tuned by eye by the person, and no further extraction is planned.
- Round 25: audit of all four docs. Section 2 of this log regrouped by topic (engine and workflow, targets and platform, roads, junctions, elevation and terrain, water, parcels and zoning, buildings, scenery, traffic). Duplicates merged and superseded statements reconciled, with no decisions removed. Companion doc paths added, and brief and pre-research cross-references aligned with the repo file names.
- Round 26: work started in the repo. Project skeleton built (headless sim_core, command and undo stack, stable IDs, doctest tests, CI). UK confirmed as the first region pack only, with the core region-agnostic. Road graph data model agreed: edges only (no Road layer above them), dual carriageways and motorways drawn as one stroke but placed as two independently editable one-way edges, draggable clicked-point handles, automatic vertical curve length, one-way as a per-edge toggle. Engine is Unreal 5.8. The road drawing UX prototype is a throwaway browser tool (2D plan plus height profile).
