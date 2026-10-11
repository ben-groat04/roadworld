# City Painter: Planning Log

Living plan for a photorealistic, sandbox city builder with a "city painter" focus. Code is written with Claude Code, so this doc is for planning only. Companion docs: the road graph data model (docs/road-graph-model.md), the v0 kickoff brief (docs/v0-brief.md), v0 pre-research findings (docs/research/pre-research.md) and UK road marking rules (docs/road-marking-rules.md). Last updated: 10 Oct 2026.

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
- **Who decides what (10 Oct 2026):** road-engineering details are decided by Claude with sensible defaults held in region data and exposed as player toggles or overrides where practice varies. The person is asked about scope, product direction, tool feel and whether pictures look right.
- **Personal project for now:** free or cheap tools and assets first. Costs that only fall due at a release are acceptable. Installed engine version is Unreal 5.8.3.
- **Paying for Realtime Mesh Component Pro (10 Oct 2026):** "depends how much". Not ruled out. Price found: $49 one-time on Fab per TriAxis's own page (Studio tier $249.99 for studios over $100K revenue); the Fab listing itself could not be read. Decide only after the free paths (hardware-RT Lumen on runtime road tiles, runtime static mesh build) have been measured and found lacking. See `docs/research/ue58-feature-sweep.md` section 1A.
- **Top quality tier requiring hardware ray tracing (10 Oct 2026):** wanted if achievable. The baseline must still run on the RTX 3060-class target without it.
- **AI write access to editor assets (10 Oct 2026):** approved. Claude may write editor assets through the Unreal MCP; git is the rollback.
- **Research outcomes (10 Oct 2026):** Mesh Terrain verdict stays "avoid" because its modifiers are editor-only (`docs/research/mesh-terrain-research.md`). Buildings use our own sim_core assembler, borrowing the PCG shape-grammar string format; PCG later for dressing only, with a one-day PCG trial before the buildings milestone (`docs/research/pcg-city-research.md`). Epic's first-party Unreal MCP is the editor bridge, VibeUE optional later (`docs/research/ue58-feature-sweep.md`). Movie Render Graph is the reference-render tool from the first week (`docs/research/ue58-feature-sweep.md`).
- **Unreal structure reference (10 Oct 2026):** the Unreal-side layering (plugin split, sim host, Blueprint limits, content layout) is set out in `docs/unreal-architecture.md`.

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
- **Dual carriageways and motorways are separate one-way carriageways.** A parallel tool draws both from one stroke, with heights referenced to the centreline so the median stays level (on curves the inner carriageway is steeper). Each can also be drawn alone. After placement they are edited independently.

### Roads: types, lanes and markings

- **Four road types at launch:** residential street, B road / main road, A road / dual carriageway, and motorway with slip roads. Slip roads and grade separation mean the junction model needs merges, diverges and ramps early. (Updated 10 Oct 2026: the UK pack now has nine road types, the original four plus `roundabout_ring`, `main_street`, `a_road_rural`, `b_road_rural` and `country_lane`. Ids in the data are `residential`, `b_road`, `main_street`, `a_road_rural`, `b_road_rural`, `country_lane`, `roundabout_ring`, `a_road_dual`, `motorway`. The rural types are kerbless and unlit; `country_lane` is modelled as two 1.75 m half lanes because there is no shared-lane model. Dual carriageway and motorway have no kerbs, just hard strip or shoulder and verge. Camber is 2.5% (crown on two-way roads, a single fall on one-way and dual), kerb upstand 125 mm, footway fall 2%. Street lights are set per road type in the pack (spacing roughly 28 to 50 m, single side, staggered or central reservation, column heights 6 to 12 m; motorway unlit by default), see `data/regions/uk/road_types.json`.)
- **Lane widths are baked into the road type,** not player-editable. Road types are data-defined presets, which keeps junction generation predictable.
- **Road markings** are generated automatically from the lane graph and junction geometry, using the draft UK rules reference as a starting point. The goal is sensible, recognisable markings, and the person tunes the exact numbers by eye. Marking numbers are estimates; reference photos are a plausibility check, not rules, since real practice varies. Decided 10 Oct 2026: zebras and other crossings are never automatic, only where the player places them. Centre lines are per road type / marking set (none along narrow urban streets except on the approach to a junction; B roads and above keep them). The give-way triangle is optional per marking set (off for minor junctions, on where the major road is a higher class). Gore and nose markings are chevrons where traffic on both sides runs the same direction, and diagonal hatching where opposing flows are separated.
- **v1 road users:** on-street parking, pavements and crossings. Bus lanes, cycle lanes and their traffic come later. Parked cars come from the traffic sim. All of these, including the later bus and cycle lanes, are lane types in the graph, so lane types must be extensible and carry their own rules (who may use them, who may park, where they connect).
- **One-way roads are in v1.** Direction is per lane in the graph, and roundabouts depend on it.
- **Driving side is one setting per save.** No mixed-side maps, so junction rules never have to reconcile two conventions in one network. The lane graph and junction rules must be side-agnostic: priority, turning conflicts, roundabout direction and signal layouts mirror from the one setting.

### Junctions

- A junction appears where roads meet. Its default lane connectors and curb geometry are **generated from the lane graph the player drew** (lane graph first, geometry second). (Round 31 relaxes this: the generator may add a turn pocket lane the player did not draw; see below.)
- Junctions are first-class and editable: the player can override any connector, lane or corner radius. Goal: avoid CS1's janky connectors, and avoid CS:Ur's bad UI.
- **Roundabouts are an explicit one-way road type** (Round 29). The ring is ordinary, individually editable one-way edges of a roundabout-ring road type, with a normal junction at every ring node; "roundabout" is a property layered on top, found by road type and never recognised by shape. The dedicated tool is only a preset that draws the ring (radius limit per region pack, UK 8 to 100 m); hand-drawing the ring type gives the same result. Circulation direction follows the driving side setting (clockwise for left-hand traffic). A roundabout that cannot be built shows a visible warning rather than silently falling back. Full-circle U-turns are allowed by default. Splitter islands exist on two-way arms only. Lanes never narrow beside a splitter: the outer (nearside) kerb lines flare outwards so every entry and exit lane keeps full width to the ring. The central island is grass with a kerb by default and splitters are paved, both from region data. Roundabout traffic policies are parked for later (v0 has no cars).
- **Corner radii and priority** (10 Oct 2026). Corner radii default by the pair of road classes meeting (data table in the region pack); player overrides win. At a junction where all arms are the same class, the straightest pair of arms has priority and the others give way (data default, per-junction override).
- **Turn pockets are allowed** (Round 31, superseding the earlier rule that junctions connect only drawn lanes and add no turn pockets). A data rule in the region pack plus a per-junction override decide where the generator widens a major arm with an offside turn lane. Design in docs/junctions.md section 7b. The code exists but is disabled (`junction.turn_pockets.enabled = false` in the UK pack), with no override command and no tests yet.
- **Slip roads are hand-drawn like any road,** with no templates. The junction generator must produce sensible merge and diverge geometry and connectors from the lanes the player draws.
- **Junction control types in v1:** priority / give way, traffic lights, zebra and signal crossings, and roundabouts. All are properties of the junction object. Zebra crossings give pedestrians priority, which the sim must model.

### Elevation and terrain

- **Full terrain sculpting.** Terrain is editable at runtime. Roads, parcels and buildings must react to edits, and every sculpt is a command (undo, saves). On 20 km+ maps this means tiled terrain data that saves as edits rather than a full copy.
- **Terrain locks under roads, free elsewhere.** Sculpting never deforms existing roads. Roads and buildings own the ground beneath them, so the sim needs a locked mask derived from road and parcel footprints, and drawing a road grades the terrain once. Sculpting is also blocked near bridges and tunnels.
- **Constant gradient per segment:** from one clicked point to the next, like the "straight slope" mode in Cities: Skylines. The road does not follow the terrain; the terrain conforms to the road.
- **Cuttings and embankments** are generated in the terrain to meet the road. This is the same "road grades the terrain once" rule as above.
- **Roads own their earthwork geometry.** Cutting and embankment slopes, retaining walls, portals and abutments are generated as meshes from the road's cross-sections, not carved into the terrain heightfield (a height grid cannot show crisp slopes or walls). The terrain is hidden under the road corridor (Unreal: landscape holes or Runtime Virtual Texturing), and the seam must never be visible: earthwork meshes use the terrain's own material and overlap the natural ground past the daylight line. Found in the 3D prototype.
- **Smooth gradient changes** between segments, including where roads meet (vertical curves), so junctions and joins never show a kink in gradient. The road graph needs a vertical profile per segment, not just a node height.
- **Every point's height is the terrain height at that click,** adjustable with PgUp and PgDn. A road that starts from another road inherits that road's height.
- **Deep cuttings and high embankments keep going,** with retaining walls up to a hard height cap. Above the cap, the road is forced into tunnel or bridge mode. Bridges and tunnels come from a dedicated tool, and use constant gradient like other roads.
- **Earthwork numbers (10 Oct 2026, UK pack `grading` and `structures` blocks, tuned by eye).** 1.5 m flat graded verge, then 1:2 slopes. Slopes are used up to about 12.75 m (12 m wall cap plus 0.75 m slack) before a wall is required; walls only where a slope cannot fit, including between neighbouring roads, with a minimum wall run and tapered free ends. Approaches to tunnel portals and bridge abutments are slopes with a short wing wall (at most 10 m), replacing the earlier 24 m walled approach. Slope tops and toes are rounded (about 2 m at daylight, 1 m at the verge hinge). Retaining wall tops step in level 4 m panels (maximum step 0.5 m) with a thin coping (0.15 m, 4 cm overhang). Bridge parapet 1.0 m; a flagged span shorter than 20 m is a solid block unless it crosses another road or water; maximum pier spacing 30 m, and piers never stand in a carriageway or hard shoulder. Tunnels are real: terrain is discarded inside the bore, with a lined interior and a headwall with coping (the flat black mouth panel was a stopgap and is gone).

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
- **Unreal look work goes beyond v0 (decided 10 Oct 2026).** Vegetation (grass and ground cover on verges and earthwork slopes, hedges, trees) and road-attached hard features such as traffic islands (kerbed islands, splitters, refuges) go into the Unreal look test and the Unreal build from the start, because they make earthworks read as real. Full street furniture such as signs stays out for now, and houses, vehicles and people remain out. The owner supplies private real-world reference photos (kept locally in docs/reference/gsv/, never committed) as a capability bar for reviews, not as something to copy. The realism bar is "almost bang on the photos", judged by cold-read agents using like-for-like recreations of photographed junctions with matched cameras (`data/layouts/ref/`). Private reference photo folders under docs/reference/ are never committed. References need not be UK-only, except for markings, signs, kerb styles and driving side.
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

- **In v0:** every road and terrain feature planned for v1 except junction control: clothoid road drawing with snapping and guidelines, all road types (nine in the UK pack), one-way roads, roundabouts, slip roads, junction geometry, pavements, crossings, parking lanes, road markings, street lights, terrain sculpting, cuttings and embankments, retaining walls, bridges and tunnels, water, undo and redo, and debug overlays (lane graph, junction connectors, curvature and gradient, cut and fill).
- **Not in v0:** cars, pedestrians and other agents (including parked cars), junction control (signals and priority rules, which come with cars), vegetation (comes with buildings), saves and loading (later), parcels, zoning, buildings and the systems that depend on them. Vegetation and traffic islands are nonetheless built in the Unreal look test and Unreal build from the start (see Scenery and props).

**v0 passes** when a junction-heavy network on hilly terrain runs at the target frame rate and drawing and editing roads feels good. The final criteria will come from the person. **v0 build order:** terrain and roads together from the start, not roads on flat ground first.

## 4. Open questions

### Roads, terrain and v0

- Height cap for retaining walls: resolved 10 Oct 2026 as 12 m for road walls (UK pack `wall_cap_m`). The cap for building foundations is still to be set by prototyping the steep-land plots.
- Slope to wall switch: resolved 10 Oct 2026. Slopes (1:2) are used up to about 12.75 m, walls only beyond that or where a slope cannot fit.
- What counts as "breaks graphically": which validity rules block road placement (self-overlap, degenerate corners, minimum segment length)?
- Final v0 pass criteria: to be set by the person, on top of the working definition above.
- Road markings for right-hand traffic: mirror the UK rules, or use another national standard?

### Project

- Save format, and whether to commit to modding later (data-only mods like road types and styles, or custom meshes and code)?

## 5. Parked for later

- Economy, demand, services, utilities and citizens
- Public transit
- Roundabout traffic policies (how vehicles choose lanes, priority behaviour); v0 only emits which entry connectors yield to the ring
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
- Round 27: Sonnet agents now do the coding, with Haiku for trivial edits and the main session planning and reviewing. The 2D prototype is replaced by a 3D one. Unreal: ProceduralMeshComponent for now, Geometry Script evaluated in the look test; coordinates north = +X, east = +Y; World Partition from day one. Dual carriageways and motorways: a parallel tool places both carriageways with the height reference on the centreline (level median, inner carriageway steeper on curves, as intended), and one-way mode draws a single carriageway.
- Round 28: the 3D prototype showed that grading the terrain grid produces jagged cuttings and portals. Decision: roads own their earthwork meshes, with an invisible corridor mask on the terrain. The prototype was split into src/ modules so parallel agents can each work on one file.

- Round 29: roundabouts are an explicit one-way road type (a region-pack ring type); the ring is ordinary editable edges with a junction at every ring node, found by road type and never recognised by shape; the tool is only a preset that draws it (radius limit from the region pack, 100 m in the UK pack); an unbuildable roundabout shows a visible warning; full-circle U-turns allowed; roundabout traffic policies parked for later. Decided 10 Oct 2026.
- Round 30: Unreal photoreal work is not limited to v0. Vegetation (verge and slope grass, hedges, trees) and road-attached features such as traffic islands come into the Unreal look test and build from the start; signs and other full street furniture stay out for now. The person supplies private real-world reference photos (docs/reference/gsv/, not committed) as the capability bar for reviews.
- Round 31: turn pockets are allowed, reversing the earlier rule that junctions connect only drawn lanes and generate no turn pockets. Controlled by a region-pack data rule plus a per-junction override. The generator is written but disabled (`junction.turn_pockets.enabled = false`), with no override command and no tests yet; design note in docs/junctions.md section 7b. Decided 10 Oct 2026.
- Round 32: catch-up pass for decisions of 10 Oct 2026 previously recorded only in the handoff: nine UK road types (rural kerbless and unlit, country_lane as two half lanes, no kerbs on dual and motorway, camber, street lights per type); roundabout splitters on two-way arms only, flared kerbs so lanes keep width, grass island and paved splitters (unverified against references: gsv 175143 and 180326 show tarmac-surfaced kerbed splitters, street/28 a grassed one; no photo shows paving slabs); class-pair corner radii and equal-class priority; marking rules (no automatic crossings, centre lines per type, optional give-way triangle, chevrons versus hatching, numbers are estimates); earthwork and structure numbers (12 m wall cap with 0.75 m slack, 1:2 slopes, short wing walls, rounded tops and toes, 4 m wall panels, parapet, spans, piers, real tunnels), resolving the two open wall questions; realism bar and reference-photo rules; division of decisions; free-first tooling; Unreal 5.8.3 installed.
- Round 33: answers to the UE 5.8 sweep questions, 10 Oct 2026. Realtime Mesh Component Pro: "depends how much", not ruled out, decided only after hardware-RT Lumen on runtime tiles and runtime static mesh build are measured and found lacking (price found: $49 one-time per TriAxis's site; Fab listing unread). A hardware-RT-only top quality tier is wanted if achievable, with the baseline still running on the RTX 3060-class target. AI write access to editor assets via the Unreal MCP approved. Research outcomes recorded: Mesh Terrain stays "avoid" (editor-only modifiers, `docs/research/mesh-terrain-research.md`); buildings use our own sim_core assembler with the PCG shape-grammar string format, PCG later for dressing only, one-day PCG trial before the buildings milestone (`docs/research/pcg-city-research.md`); Epic's Unreal MCP is the editor bridge, VibeUE optional later; Movie Render Graph is the reference-render tool from week one (`docs/research/ue58-feature-sweep.md`).
- Round 34: the person delegated six Unreal-structure decisions ("do whatever you think is best") and all six recommended defaults in `docs/unreal-architecture.md` were adopted, 10 Oct 2026. (1) The Unreal plugin is split now: the sim wrapper module (compiles sim_core, no Unreal types) is separate from the rest. (2) The sim instance is hosted in a world subsystem, not an actor. (3) sim_core owns the derived-data cache and the dirty set; Unreal only consumes them. This is the costly one to reverse. (4) ProceduralMeshComponent is kept for the look test, hidden behind one tile-mesh interface; the free Realtime Mesh Component Core gets a trial in week two; the paid Pro tier ($49) is not adopted without the person's say. (5) Blueprint is limited to UI (UMG) and assets; no tool or gameplay logic in Blueprint. (6) All content moves under `/Game/CityPainter/` once, now. The person's priority #1 for the Unreal phase is a structure that does not become a mess as the rest of the city builder is added; `docs/unreal-architecture.md` is the reference for the layering.
- Round 35: product framing, 11 Oct 2026. The person: "this is a game, we will just be placing the road in the open world and I expect it to then with only that be what you'll be comparing to the images. So all the stuff has to come from the existing world and the road edge scatter." Decided consequences: nothing is hand-dressed in look-test levels; vegetation and ground come only from a `world_cover` layer (seeded default, replaceable by a future world generator, pattern measured from the aerial references) and road-generated content (surface, markings, kerbs, earthworks, verge and edge scatter, lighting columns); placing a road clears world cover in its corridor. The flat grass plane in the Unreal level is a stand-in; ground from terrain plus grading is the next critical Unreal piece; a small world (a few km) is fine for prototyping. Reviewers compare against all reference photos and ignore buildings, fences and walls not generated by the road, signs, vehicles, people and other street furniture (lists in `docs/research/vegetation-plan.md`), but lighting columns are generated and judged (this overrides the lighting-column entry on that ignore list).
- Round 36: earthworks tier rule, 11 Oct 2026. The round-4 stacked terraces (eight walls of about 3.8 m) were rejected by the person and a cold reader as a "ziggurat" (`docs/progress/2026-10-10-structures-r4/REVIEW.md`; the brief had said "as many tiers as needed" with no bound). New rule: at most two tiers with a 4.5 m berm; beyond that a rock-cut face on the cut side and one wall up to 6 m on the fill side; if that cannot reach the formation, flag `needs_structure` (bridge, viaduct or tunnel) instead of stacking. Numbers are the assistant's judgement from 11 opened photos (shapes supported, figures not); values live in region data. A realistic side-hill scenario (gradient 0.3-0.5) was added next to the 0.85 stress case. Reversible by editing the region data tunables.
- Round 37: vegetation approach, 11 Oct 2026 (`docs/research/vegetation-plan.md`, from 36 opened references). Two layers only: `world_cover` (exists before the road) and road-edge scatter (created by the road), both as sim_core WorldCache layers; cut-edge truncation where the road corridor clears cover. Free route first: Poly Haven CC0 models (11 plants fetched 11 Oct to `unreal/CityPainter/SourceArt/Meshes/PolyHaven/`; two giant conifers deliberately not fetched), a Blender vegetation kit (`tools/blender_kit/vegetation/`) for hedges, grass, scrub and placeholder trees, and the Unreal Procedural Vegetation Editor (experimental, GUI-driven, packaged-build test required) for real trees. Infinigen was evaluated read-only (`docs/research/infinigen-preflight.md`) and dropped by the person ("a bit much for what it can do"). Realtime Mesh Component Pro ($49) is still not approved; decided after the Lumen measurement.
- Round 38: junction and splitter decisions made by the assistant, 11 Oct 2026 (road detail, within its remit; reversible via region data and `1e48099`). Roundabout splitter islands are short triangles sized by approach speed (about 17-26 m long, 4 m wide at the ring) with a rounded nose, ladder bars and lane-edge lines on the painted gap, not 51 m constant-width strips; the splitter surface is asphalt or grass, not paving slabs (gsv 175143 and 180326 show tarmac, street/28 a grassed one); give-way line at the ring edge of every arm; slip diverge/merge gores are paved, bordered and chevroned; kerb slivers removed; default marking wear lowered (5-15% on main roads, 30-50% only for old minor roads); bridge pier clearance default to drop to 0.2 m as region data (decided, not yet done). Renders: `docs/progress/2026-10-11-junction-fixes/`.
- Round 39: reference-claims rule, 11 Oct 2026. A review written after opening 2 of about 90 photos produced six wrong claims (brown asphalt, lighter wheel tracks, heavy wear, dense patches, oil stains, cream kerbs) that reached builders (`docs/research/reference-claims-audit.md`). Rule: any statement about what the reference photos show must name the files actually opened; review briefs set a minimum of three opened photos per view; the main session checks the count before acting; scores based on fewer than two photos say so; briefs avoid unbounded absolutes and require agents to report what they did not verify. Working practices learned the same night (single Unreal lane, worktree per code agent, at most 3-4 concurrent agents after the usage limit hit at 02:40, `[skip ci]` on every push) are in `docs/HANDOFF.md` section 10 and `CLAUDE.md`.

- Round 40: the game is built in Unreal, 11 Oct 2026 (`docs/unreal-game-plan.md`, milestones M1-M8). The six owner defaults were adopted: prototype orbit camera; play-in-editor through M7 and a packaged exe at M8; plain dark panels copied from the prototype; 2 km seeded test world; flat ribbon ghost; plugin renamed `CityPainterCore` and the RoadTest level retired once road layers exist. sim_core work (DrawSession, ground pick, RoadIndex, stroke resolver, World aggregate) runs in parallel worktrees; Unreal is the single lane.
- Round 41: Unreal module structure, 11 Oct 2026 (implements Round 34). Plugin `CityPainterCore` with modules Sim (facade, no UObjects, the only code that includes `cp/` headers), World (`UCPWorldSubsystem`), Render (`UCPRenderSubsystem`, `ICPRenderLayer`, `ICPTileMesh`, ground layers) and Legacy (the old `ACPRoadMeshActor`, kept until road layers exist, with CoreRedirects); game module `CityPainter` for game mode, controller, camera pawn, input config (Enhanced Input built in C++), HUD. Dependencies point down only. A new system adds a sim layer, its commands, one render-layer registration and one tool.
- Round 42: reference photos first, 11 Oct 2026. The owner: "All the time you should be using reference images, this is at the end of the day what I want it to look like." Builders open at least three photos per case before designing, measure in lane widths, name the files, render at the same framing and compare item by item; layout/markings are scored separately from look; "joins up" properties become tests. Reason: markings built from the rules doc scored "7/10" for two rounds while the owner found five layout faults in a minute. Reference-photo downloads are pre-approved (in chat: "If you ever need more reference images go get them"); reference images only. "Nothing fails silently": skipped, refused or failed parts go in the first lines of the report and the STATUS file. The main session opens headline images itself before relaying a "fixed/good" claim and asks what would never be built.
- Round 43: the prototype's UI is the spec, 11 Oct 2026. The owner: "that ui was sort of what i want, that was the point of it". The Unreal game ports the browser prototype's elements, inputs and feedback (`docs/unreal-ui-parity.md`: 15 elements, 31 inputs, 18 feedback items, tuning sliders, parity checklist); third-party products are a source of extras only.
- Round 44: agents playtest, 11 Oct 2026. The owner: "i will never open it, this takes too long. just mess around with it yourself to test it (ideally in an agent)". Agents launch and drive the editor or a `-game` run (Epic MCP over HTTP on port 8000, scripted input, automation tests), take screenshots, judge and close it. Milestones are not done until an agent exercised the interaction. MCP plugins enabled and auto-start on port 8000. Only one editor/-game process at a time on the 15 GB machine; code may be compiled in another worktree meanwhile.
- Round 45: earthworks rule changes after round 5, 11 Oct 2026 (assistant's road-engineering judgement, values in region data). Round 6: crossfade of cut/fill displacement where two roads' earthworks overlap; rounding capped at a third of the slope length. Round 7: smoothing relaxes the displacement from natural ground (not the height; 8 passes, strength 0.8, margin 8 m, max move 1 m) so natural ground is never altered; toe ditch fixed; no-crease bound 0.3 per 0.5 m; the `adjacent_levels` pit cause proven (a road's end cap under another road's fill). Round 8 (in progress): a slope above or below a wall may run only `slope_max_ratio` 3.0 times the flat-ground run plus 2 m slack and never more than 15 m past the wall; otherwise the wall takes the height (stops cut slopes chasing the hillside, which the owner spotted); walls get a raked top, short gaps between wall runs (under 30 m) are bridged, free ends are blunt and buried (photos street/04, 05, 46) rather than stepped.
- Round 46: world cover model, 11 Oct 2026. The Voronoi field model (round 2, rated "Voronoi with random woods") was replaced in round 3 by blocks with a grain, then in round 4 by through-line rows with brick-bond cross lines (T-junctions only), quality-gated cuts (corners at least 55 degrees, aspect at most 4), three boundary weights (hedge, fence/bank, tree line), stepped wood edges and belts, and per-field attributes (`field_id`, `LandUse`, tint, row direction) so visuals can colour and texture each field. Built from 14 vertical 25 cm Oxfordshire orthophotos (aerial/46-59). Lanes, tracks, farmsteads and ponds are not generated; whether the starting world should contain man-made features is an open owner question (assistant's recommendation: no real roads at start; tracks and farmsteads later as cover).
- Round 47: Road Creator Pro and Civil Engine research verdict, 11 Oct 2026 (`docs/research/civil-engine-research.md`, redone from the Fab listing and the 89-page manual). Do not buy: it is an editor-time mesh-kit tool (the construction script does not run in a packaged game), with pre-made junctions and no cut/fill, wall or runtime terrain logic. Ideas worth borrowing are noted in the research file; the prototype already had its own versions of the checks and guidance, so the prototype stays the UI spec. A props pack (signs, barriers, lampposts) is optional and the owner's call.
- Round 48: working rules added 11 Oct 2026, recorded in `CLAUDE.md` and `docs/HANDOFF.md` section 10. Videos in the browser pane are muted. Unreal code may be compiled in a separate worktree while another agent runs the editor, but nobody kills compiler processes that are not theirs. Fresh agent per round from a STATUS file; change method when tuning stalls after one round. The 5-hour usage window cut all agents off at about 12:40 on 11 Oct; work resumed cleanly from STATUS files and branches.
