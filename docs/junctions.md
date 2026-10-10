# City Painter: junction generator (interface v1)

10 Oct 2026. Interface: `sim_core/include/cp/junction.h`. Tests: `tests/test_junction_interface.cpp`. Decisions come from `planning-log.md` (Junctions) and `road-graph-model.md` (§3, §5).

## 1. Data flow
```
RoadGraph + RegionPack + JunctionOverrides
  -> collect_arms        arms CCW by heading_out: lanes, widths, kerb offsets, centreline   [junction.cpp, real]
  -> classify_arms       End | Joint | Junction | Merge | Diverge                                [real]
  -> major_arm_indices, resolve_corner_radii                                                     [real]
  Junction:          compute_trims (B) -> generate_connectors (A) -> generate_geometry (B)
  Merge / Diverge:   generate_merge_diverge (C) does trims, connectors and geometry for the node
  -> JunctionSet { NodeId -> Junction }   (nodes with 3+ arms only)
```
Lane graph first, geometry second: A reads only `arms`, `trims`, overrides (never B's polygons), and B never reads connectors. The one coupling is `trims`, via `compute_trims`. `build_junctions(..., only)` rebuilds just the dirty nodes. Everything is derived and never saved; the saved state is `JunctionOverrides`.

## 2. Frames and ordering (shared conventions)
- Each arm is in its own outward frame: s = 0 at the node, `heading_out` away from it, lateral + = left of `heading_out`. `arm_point(arm, s, lateral)` reads the arm's full edge centreline (0..edge_length); only normal-junction trims are capped at 40 m (`kArmSampleLength`), merge/diverge trims at edge_length / 2.
- Arms sort CCW by `heading_out`. Corner k is between arms[k] and arms[k+1], keyed `(node, edge_l = arms[k+1].edge, edge_r = arms[k].edge)`.
- `LaneRef` lists only connecting lanes (lane type `connects_at_junctions` with a travel direction), so pavements, parking and shoulders are excluded. `lane_from`/`lane_to` index `arm.lanes`; override keys use the stable `slot`.
- Polygons are `std::vector<std::vector<Vec2>>`: outer rings CCW, holes CW.

## 3. Trim contract (B -> A)
`compute_trims(arms, radii)` (real, in `junction_geometry.cpp`) returns, per arm, the distance from the node to the junction boundary. For corner angle theta (CCW arm k to arm k+1) and kerb offsets a = arms[k].kerb_left, b = arms[k+1].kerb_right, the kerb lines meet at `a*cot(theta) + b/sin(theta)` on arm k and `a/sin(theta) + b*cot(theta)` on arm k+1. The fillet tangent points lie `r/tan(theta/2)` beyond. An arm's trim is the max over its two corners (theta >= 180 deg gives no corner), clamped to [0, min(edge_length/2, 40 m)].
A's connector paths **start and end exactly on the trim lines**: first point `arm_point(from, trims[from], lane.lateral)`, last point `arm_point(to, trims[to], lane.lateral)`, tangent to the lanes there. B's carriageway polygon is bounded by the same lines, so lanes and asphalt meet without a gap. Edge meshes are cut at `trims[i]`.

## 4. Parts
**A, connectors** (`junction_connectors.cpp`): for every inbound lane x outbound lane on a different arm, one `LaneConnector`. No auto turn pockets, no U-turns by default (only an enabling override creates one). `kind` from `classify_turn` (real; thresholds 30/150 deg; left is Nearside under `DrivingSide::Left`, mirrored otherwise). `path` comes from `make_connector_path` (also used by C). `conflicts` = indices of connectors crossing in plan (|dz| < region `plan_crossing_junction_dz_m`) or ending in the same lane. Disabled connectors stay in the list with `enabled=false`, so the UI can re-enable them. Order is deterministic (arm_from, lane_from, arm_to, lane_to).
**B, geometry** (`junction_geometry.cpp`): fills `corners` (fillet kerb polylines, empty where the kerb runs straight), `carriageway`, `pavements`, `crossings` (one per arm, just outside the trim line, perpendicular, kerb to kerb). Use Clipper2 (`third_party/clipper2`, link target `clipper2`, already a dependency of sim_core) for union/offset: carriageway = union of arm stubs and corner wedges; pavements = offset of the carriageway minus the carriageway. Clipper2 builds with `-fno-exceptions`: it checks `__cpp_exceptions` and turns errors into no-ops, so an empty result must be treated as failure. It uses Int64 internally; use `PathsD` with precision 3.
**C, merge/diverge** (`junction_merge.cpp`): classification is already real (all three arms one-way, two same-direction arms within 45 deg, the third >= 135 deg from both). C owns the node: sets `trims` itself (the gore point; the corner formula blows up as theta -> 0), maps lanes across (lane add/drop, taper) into connectors via `make_connector_path`, and fills the same polygons. Lane counts need not match.

## 5. Overrides survive regeneration
`JunctionOverrides` holds `connectors[(node, from_edge, from_slot, to_edge, to_slot)] -> {enabled, via}`, `corner_radius[(node, edge_l, edge_r)]` and `lane_connects[(node, edge, slot)]`. All keys are stable IDs, so a rebuild looks them up and re-applies them. A custom curve stores only interior control points (`via`); the end points are re-snapped to the current trim lines, so changing a radius or a neighbour does not invalidate it. On an edge split the *commands* remap keys to the new edge touching the node (§5 of road-graph-model) and record any dropped ones for undo; the generator never mutates overrides. Unmatched keys are ignored.

## 6. Side-agnostic rules
Only `DrivingSide` decides nearside; nothing branches on left/right except `classify_turn`/`turn_is_left`. Lane `inbound` is derived from `Lane::dir`, which is already mirrored by `instantiate_lanes`. Tests run both sides and expect nearside/offside counts to be identical. Major arm: highest `road_class` (design speed in km/h, a proxy until the pack has an explicit class), then widest cross-section; ties keep all tied arms. Signals and priority are out of v0, but `major_arms` is the input for give-way markings.

## 7. Roundabouts (`junction_roundabout.cpp`)
A roundabout is a ring of one-way edges (circulation clockwise for left-hand traffic, mirrored from `DrivingSide`) whose junction nodes are ordinary 3-arm nodes (ring in, ring out, spoke); 2-arm ring nodes (joints) are allowed. It is always *recognised* from the graph (`recognise_roundabouts`: simple directed cycle of one-way edges, circulation matches the side, mean radius 5-80 m, min/max radius ratio >= 0.6, >= 3 approach edges, groups node-disjoint) and never stored, so the tool and a hand-drawn ring give the same `JunctionGroup`.
- **Tool:** `make_place_roundabout(centre, radius, ring_road_type, region)` (one undo step, redo keeps ids). Cuts every edge crossing the circle (inside parts and nodes removed, stubs keep structure spans and pin the tangent), adds ring nodes at the crossings plus joints so arcs are <= ~93 deg and the ring has >= 4 nodes, then ring edges with tangent headings. Ring nodes take the centre's z. Fails, graph untouched, on an unknown/too wide type (island < 3 m) or a stub/ring edge < 4 m.
- **Post-pass:** `apply_roundabouts(set, graph, region, overrides)` after `build_junctions` (callers must invoke it; `junction.cpp` does not yet). It rebuilds the ring nodes with forced corner radii (spoke/ring-out 8 m, ring-in/spoke 12 m, island-side corner = ring radius; player corner overrides win), erases their plain junctions and inserts ONE `Junction` at `group.nodes[0]`: `kind = Roundabout`, `group_nodes`, `arms` = the approach arms (CCW about the ring centre, trims from the ring node junctions), `carriageway` = ring + flares with the island as a CW hole, pavements/crossings/gore merged, and one connector per (inbound lane, outbound lane of another arm) routed round the ring (shortest; a lane change is blended along the last ring edge).
- **Layout:** `build_roundabout` gives the `RoundaboutLayout`: island (inner kerb offset of the ring, a Clipper2 inflate), splitter teardrops on two-way approaches, and Entry/Exit/Through/Ring connectors. Priority: only Entry connectors have `give_way` and `conflicts` (the Through at the same node on the target lane, and the Ring lane arriving on that slot).
- **Export:** the JSON `islands` array holds the island and splitters (rebuilt from the graph; `Junction` has no `islands` field).
- **Params:** `RoundaboutParams` fallbacks until the region pack has roundabout fields (`roundabout_params(region)` is the one place to read them).

## 8. Test strategy and golden numbers
Residential = 2 x 3.0 m driving, kerb inner face at 5.0 m, half width 7.15 m. Existing, passing: arms (T: 3 arms, 2 connecting lanes each, kerb 5.0), X/Y ordering and 120 deg gaps, classification incl. merge/diverge/one-way-120 deg, major arms (b_road over residential), `compute_trims` (**r = 6 m, 90 deg: 11 m; r = 0: 5 m; r = 10 on one corner: 15 m on its two arms**; 120 deg: `(a cos + b)/sin + r/tan(60)`), `classify_turn` both sides, corner-radius override, `build_junctions`.
Skipped placeholders to enable:
- **A:** T of two residentials -> **6 connectors**: 2 Straight, 2 Nearside, 2 Offside (same for both sides); X -> 12 (4/4/4). Every path starts/ends on a trim line within 1 mm and tangent within 1 deg. Disabling one connector keeps 6 listed, 5 enabled. In the T, the two straight connectors cross the two side-road turns (conflicts non-empty), and the two connectors into the side arm's one outbound lane conflict (merge).
- **B:** X, r = 6: 4 corners, each kerb polyline starts/ends 11 m from the node, carriageway one polygon with area ~ 4 stubs + corner wedges; r = 0 gives sharp corners (empty or 2-point kerb); T has no corner on the straight side; polygons are valid (no self-intersection, CCW). Radius override changes only that corner.
- **C:** one-way 2-lane carriageway + 1-lane slip at 10 deg diverge: lane-count mismatch yields connectors for every outbound lane with no orphan, trims > 0, no connector crosses another.
