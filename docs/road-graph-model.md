# City Painter: road graph data model (proposal v1)

10 Oct 2026. Status: **agreed v1** (answers recorded in §7). Nothing in `sim_core` implements this yet.

## 1. Principles

- **Authored vs derived.** Commands mutate a small *authored* layer (what the player drew, plus overrides). Everything else (lane graph, junction geometry, curbs, markings, meshes, terrain grading) is *derived*: regenerated deterministically from the authored layer and never saved. This keeps saves small and undo simple, and means generator improvements apply to old saves.
- **Stable IDs** (`cp::Id<Tag>`, never reused). IDs can be *retired* (for example a split edge), but a retired ID never comes back.
- **3D from day one.** Positions are `double` metres, x east, y north, z up, in a right-handed frame. The Unreal plugin converts to its left-handed cm frame at the boundary.
- **Region-agnostic core.** Nothing UK-specific lives in code. Road types, lane types, marking rules, clearances, speed units (mph or km/h) and design-speed tables come from a **region pack** (`data/regions/<region>/`). UK is just the first pack, used for prototyping. A save names its region pack. Internally all speeds are m/s and all lengths are metres.
- **Side-agnostic.** Cross-sections and junction rules use **nearside / offside** relative to the direction of travel, never left/right. One per-save `DrivingSide` setting maps nearside to left (UK) or right.

## 2. Authored layer

```
World
 ├─ settings: DrivingSide
 ├─ Node        { id, pos: Vec3, kind: End | Junction | Joint }
 ├─ Edge        { id, a: NodeId, b: NodeId,
 │                horizontal: ControlPoint[] + fitted Primitive[],
 │                vertical:   VerticalProfile,
 │                structure:  Span[]  (ground | bridge | tunnel over s-ranges),
 │                type: RoadTypeId, direction: TwoWay | OneWayAtoB }
 ├─ JunctionGroup { id, nodes: NodeId[], kind: Plain | Roundabout }
 └─ Overrides   { connector overrides, lane overrides, corner radii } keyed by stable keys (§5)
```

### Node
A point where edges meet, or where a road ends. `Joint` is a 2-edge node with no junction behaviour (for example where a road changes type or structure). Height is the node's authored z.

### Edge: horizontal geometry
- **ControlPoints** are the clicks: position (x, y, z) plus optional pinned tangent (from snapping, guidelines, or continuation from a neighbour at a node).
- **Primitives** are the fitted result: a G2-continuous chain of `Line | Arc | Clothoid` pieces, each `{s0, length, x0, y0, heading0, k0, k1}` (a clothoid's curvature varies linearly from k0 to k1). Lines and arcs are degenerate clothoids, so one evaluator handles all three.
- **Proposal: store both.** Control points are authoritative for editing. Primitives are stored so geometry stays bit-identical even if the fitter changes later; they are refit only when the player edits that edge.

### Edge: vertical profile
- Each control point carries a height. Between control points the gradient is **constant** (the straight-slope rule).
- At each grade break (including at nodes, across edges), a **parabolic vertical curve** of length L is centred on the break, which gives smooth gradient changes. Default L = K × |Δgrade %|, with K taken from the road type's design speed (crest and sag K differ). **Q4**
- Stored: heights per control point plus an optional L override per break. Sampled z(s) is derived.

### Edge: structure spans
A list of `{s0, s1, kind}` along the edge. The bridge and tunnel tool writes spans explicitly. The retaining wall cap forces spans when the cut or fill exceeds it. The terrain lock mask and the mesh generator both read spans.

### Road type and cross-section (region pack, `data/regions/uk/road_types.json`)
A road type defines the cross-section for **one direction of travel, from the centre outwards to the nearside**. Two-way roads mirror it for the other direction. A median is a centre slot.

```
residential_street: two-way, half: [driving 3.0, parking 2.0, kerb, pavement 2.0]
a_road_dual:        carriageway (one-way): [driving 3.65 ×2, hard_strip 1.0, verge 2.0], median_gap 2.5
motorway:           carriageway (one-way): [driving 3.65 ×3, hard_shoulder 3.3, verge], median_gap 3.0
one-way variant:    explicit cross-section, all driving lanes forward
```

**Dual carriageways and motorways (agreed):** drawn as **one stroke**, but placing it creates **two independent one-way edges** offset either side of the stroke by `median_gap/2 + carriageway half-width`, travelling in opposite directions per `DrivingSide`. After placement each carriageway is a normal one-way edge: dragged, split, bridged or deleted on its own, so carriageways can diverge freely at grade-separated junctions. The median and central reservation surface is *derived*: generated between nearby parallel, opposite-direction one-way edges of a dual type, not stored as a link.

Plus: design speed, speed limit, street light spacing and style, marking class, default vertical-curve K values, and max cut/fill before forced structure.

### Lane types (shared core set plus region additions, `data/lane_types.json`)
Extensible records, not an enum: `{id, allowed_users: [car, ped, bus, bike, …], may_park, connects_at_junctions: bool, edge_marking_rules, surface}`. Bus and cycle lanes added later are new records.

## 3. Derived layer (regenerated, never saved)

Regeneration is triggered by dirty flags set by commands: an edit dirties its edges and their end nodes, and the regenerator rebuilds only the junctions and edges touching dirty nodes.

1. **Lanes per edge:** instantiate the cross-section into lane records `{edge, slot, offset, width, type, travel dir}`. Lane centrelines are offset curves of the primitive chain, sampled adaptively (clothoid offsets are not clothoids).
2. **Junctions:** per node or JunctionGroup, trim edge ends back to a junction boundary, then generate default **connectors** (lane → lane, with turn type and conflict sets), curb and corner polygons, crossings, and pavement continuity.
3. **Markings:** from lanes, connectors and computed visibility (curvature and crest), per the region pack's `marking_rules.json`.
4. **Terrain grading:** cut/fill/retaining-wall footprint plus the locked mask. *Note:* grading is applied to the terrain once, by the command that created or edited the road (it records the terrain delta for undo). It is not continuously re-derived, so sculpting elsewhere never moves it.
5. **Meshes** (Unreal side): road surface, kerbs, walls, bridges and markings from the above.

## 4. Topology rules

- **Connecting into the middle of an edge splits it**: the old edge ID is retired and two new edges are created with a new Junction node between them. There is no player-level Road object above edges (agreed): whole-road selection, if wanted later, is a derived walk along connected edges of the same type.
- **Plan-view crossings:** two edges that cross in plan form a junction only if |Δz| at the crossing is < 1.0 m. Otherwise they must clear by at least the region pack's headroom (UK: about 5.3 m), or one must be a tunnel. Anything in between is invalid ("breaks graphically"). The numbers are tunable.
- **Starting a road from an existing road** snaps to it and inherits its z there (planning log rule).
- **Roundabouts:** a JunctionGroup of kind Roundabout whose ring is a closed loop of one-way edges (circulation from `DrivingSide`) with entry and exit nodes. The roundabout tool emits exactly that. Hand-drawn rings are *recognised* (closed, one-way, every node on the ring is a junction) and promoted to the same group.

## 5. Override keys (survive regeneration)

- **Connector override:** `(node, from_edge, from_slot, to_edge, to_slot) → {enabled | disabled, custom curve?}`.
- **Corner radius:** `(node, edge_left, edge_right) → radius`.
- When a split retires an edge, overrides on the retired edge are **remapped** to whichever new edge now touches that node. Overrides that can no longer apply are dropped, and the command records them so undo restores them.

## 6. Commands (first set)

`DrawRoad` (composite: create nodes and edges, split edges, apply terrain grading), `MoveControlPoint`, `DeleteEdge`, `ChangeRoadType` (upgrade tool), `SetDirection`/`ReverseEdge`, `SetStructureSpan`, `PlaceRoundabout`, `SetConnectorOverride`, `SetCornerRadius`. Each records exactly what it needs to revert.

## 7. Decisions (answered 10 Oct 2026)

- **Q1. Road vs edge:** edges only. No Road layer above the graph.
- **Q2. Dual carriageways:** drawn as one stroke, then placed as two individually editable one-way edges (see §2).
- **Q3. Editing:** clicked points stay as draggable handles, and the clothoid refits live.
- **Q4. Vertical curves:** length is automatic from design speed (K-values). A per-break override is possible later.
- **Q5. One-way:** per-edge toggle that swaps in the type's one-way cross-section, plus a reverse-direction tool.
