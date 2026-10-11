> HISTORICAL (11 Oct 2026): kept for the record; not current state. Current state: docs/QUEUE.md and docs/HANDOFF.md.

# City Painter: v0 kickoff brief for Claude Code

10 Oct 2026

v0 is a Windows Unreal Engine (C++) road painter on sculptable terrain: clothoid roads, junctions, bridges, tunnels, water and automatic road markings, with no traffic, parcels or buildings. Decisions come from the planning log (docs/planning-log.md), which stays the source of truth.

## Scope

- **In v0:** clothoid road drawing with snapping and guidelines, all four road types, one-way roads, roundabouts, slip roads, junction geometry, pavements, crossings, parking lanes, road markings, street lights, terrain sculpting, cuttings and embankments, retaining walls, bridges and tunnels, water, undo and redo, and debug overlays.
- **Not in v0:** cars, pedestrians and other agents (including parked cars), junction control (signals, priority rules), vegetation, saves and loading, parcels, zoning, buildings.
- **v0 passes** when a junction-heavy network on hilly terrain runs at the target frame rate and drawing and editing roads feels good. The person will give the final criteria.

## Technical ground rules

- Windows PC only. Target an RTX 3060 class GPU at 1440p, 60 fps, with upscaling. Maps are 20 km and larger, so plan for World Partition and large-world coordinates. Treat LOD (road meshes, markings, terrain, shadows) as core, not polish. The camera is free orbit only.
- The sim core is an engine-agnostic, headless C++ library with a test runner. A thin Unreal plugin renders it.
- Every player edit is a command (this gives undo and redo). The sim is deterministic: fixed timestep, own thread, renderer interpolates.
- The road graph is 3D from day one, with stable IDs.
- Use proven, permissively licensed geometry libraries: Clipper2 (Boost licence) for clipping, offsetting and triangulation, and Boost.Geometry if needed. Do not use CGAL: most of its algorithm packages are GPL, which conflicts with Unreal's rule against combining engine code with copyleft code. Fuzz-test on messy layouts.
- Road types, lane types and marking rules live in data files.
- Verify your own work with headless unit and golden tests, screenshot tests for geometry, and debug overlays: lane graph, junction connectors, curvature and gradient, cut and fill.
- For UX questions, build standalone desktop prototypes controlled by mouse and give them to the person for feedback. For geometry tests, also import real road networks and terrain as fixtures (OpenStreetMap, Ordnance Survey open data, open LiDAR), checking each dataset's licence. This needs a heightmap import path.

## Roads and junctions

- Roads are drawn by clicking points, and the tool fits clothoid curves through them. Snapping uses angle and length increments plus road guidelines (extension of a road, tangent continuation, parallel offset). There is no grid.
- Curvature and gradient show live while drawing and are advisory. Placement is blocked only when the result breaks graphically.
- Four road types: residential street, B road / main road, A road / dual carriageway, motorway with slip roads. Lane widths are baked into the type. Lane types must be extensible (parking and pavements now, bus and cycle lanes later).
- One-way roads are in. Roundabouts are available both as a tool and hand-drawn, and both produce the same junction object. Circulation direction follows the driving side setting.
- Driving side is one setting per save. The lane graph and junction rules must be side-agnostic.
- Junctions appear where roads meet. Default lane connectors and curb geometry are generated from the lane graph. The player can override any connector, lane or corner radius. There are no junction prefabs. Slip roads are hand-drawn, so the generator must handle merges and diverges. Pavements and crossings belong in the junction model.

## Elevation and terrain

- Terrain is fully sculptable, stored as tiled data with edits as commands or deltas.
- Each segment has a constant gradient between its clicked points, like the "straight slope" mode in Cities: Skylines. Each point's height is the terrain height at the click, adjustable with PgUp and PgDn. A road starting from another road inherits that road's height. Gradient changes smoothly between segments (vertical curves), so the graph needs a vertical profile per segment.
- The terrain conforms to the road with cuttings and embankments, using retaining walls up to a hard height cap. Above the cap, the road is forced into tunnel or bridge mode. A dedicated bridge and tunnel tool exists, and bridges and tunnels also use constant gradient.
- Terrain under roads is locked. Sculpting is blocked near bridges and tunnels.
- Water is static in v0: painted levels and shorelines. Store a level per water body so dynamic water can be added later. Roads cross water by bridge.

## Road markings

- Markings are generated automatically from the lane graph and junction geometry, not placed by hand.
- The goal is sensible, recognisably UK markings, not exact regulation numbers: the person tunes them by eye. Use docs/road-marking-rules.md as the starting reference, and research further only where a case is unclear. Sources already identified: Traffic Signs Manual Chapter 5 (free PDF at https://assets.publishing.service.gov.uk/media/5c4ace6ded915d38a0611abc/traffic-signs-manual-chapter-05.pdf), Traffic Signs Regulations and General Directions 2016 Schedule 9 Part 6 on legislation.gov.uk, and Manual Chapter 3 for stop, give way, cycle and bus markings. Verify the current versions. See docs/research/pre-research.md for the licence and data checks. Cover junction markings in particular (for example give-way and stop lines, hatching, box junctions, roundabout markings, crossings, lane arrows, edge lines, parking bays).
- Keep the marking rules in data files, and note where the numbers came from.
- Street lights are part of the road types.

## Suggested build order

This is a suggestion. Propose changes where you see a better order.

1. Project skeleton: headless sim library, tests, thin Unreal plugin, plus an early look and performance test (hand-placed road on terrain at target frame rate). Include Runtime Virtual Texturing in the test, for blending roads, markings and wear into the terrain, and Geometry Script for generated road, wall, cutting and bridge meshes.
2. Road graph data model (3D, stable IDs, vertical profiles, lane types). Propose it and ask the person questions before building.
3. Terrain and clothoid road drawing together, with snapping and live curvature and gradient.
4. Junction generator, connector overrides, roundabouts and slip roads.
5. Cuttings, embankments, retaining walls, bridge and tunnel tool, water.
6. Road markings and street lights.
7. Debug overlays and undo and redo throughout.

## To settle by prototyping

- The height cap value for retaining walls.
- The steepness at which cutting and embankment sides switch from sloped earth to retaining walls.
- Which validity rules count as "breaks graphically" (self-overlap, degenerate corners, minimum segment length).
- Final v0 pass criteria, from the person.
- Markings standard for right-hand traffic: mirror the UK rules, or use another national standard.
