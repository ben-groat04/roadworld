#pragma once
// Roundabouts: a JunctionGroup of kind Roundabout (docs/road-graph-model.md section 4). A closed
// ring of one-way edges whose circulation follows DrivingSide (clockwise for left-hand traffic).
// Two ways in, one group out:
//   - make_place_roundabout: the dedicated tool, emits the ring and splits the approach roads;
//   - recognise_roundabouts: promotes hand-drawn closed one-way loops.
// The group is always derived (recognised from the graph), never stored, so both routes yield
// the same object. build_roundabout turns a group into the junction output (island, annulus,
// splitter islands, entry/exit/ring connectors); the per-node Junctions of the ring nodes get
// kind = Roundabout through apply_roundabouts (called by build_junctions).
#include <vector>

#include "cp/junction.h"
#include "cp/road_commands.h"

namespace cp {

// Recognition limits (hand-drawn rings must be roughly round and small).
inline constexpr double kRoundaboutMinRadius = 5.0;      // m, mean ring radius
inline constexpr double kRoundaboutMaxRadius = 80.0;     // m
inline constexpr double kRoundaboutMinRoundness = 0.6;   // min/max distance of the ring from its centroid
inline constexpr int kRoundaboutMaxRingEdges = 16;
inline constexpr int kRoundaboutMinArms = 3;
// Forced kerb fillet radii at ring nodes (m): tight entries, easy exits.
inline constexpr double kRoundaboutEntryRadius = 8.0;
inline constexpr double kRoundaboutExitRadius = 12.0;

// An approach road attached to the ring.
struct RoundaboutArm {
    int ring_index = 0;   // index into JunctionGroup::nodes
    EdgeId edge;          // the approach edge (not a ring edge)
    bool entry = false;   // carries traffic onto the ring
    bool exit = false;    // carries traffic off the ring (both set on a two-way approach)
};

struct JunctionGroup {
    JunctionKind kind = JunctionKind::Roundabout;
    std::vector<NodeId> nodes;       // ring nodes in circulation order, nodes[0] has the smallest id
    std::vector<EdgeId> ring_edges;  // ring_edges[i] runs nodes[i] -> nodes[(i + 1) % n]
    std::vector<RoundaboutArm> arms; // ordered by ring index, then edge id
    Vec2 centre;                     // centroid of the ring
    double radius = 0;               // m, mean distance of the ring from the centre
    bool clockwise = true;
};

// Detects hand-drawn (or tool-made) rings: a simple closed loop of one-way edges whose
// circulation matches region.driving_side, roughly round, with at least kRoundaboutMinArms
// approach edges attached. Groups are node-disjoint; order is deterministic.
std::vector<JunctionGroup> recognise_roundabouts(const RoadGraph& graph, const RegionPack& region);

// The tool. Every edge crossing the circle is cut at the ring (the part inside the circle is
// removed, including nodes left inside); the ring of one-way edges (at least 4 nodes, arcs of
// at most about 90 degrees) is inserted with the circulation of region.driving_side. Fails (and
// leaves the graph unchanged) if the ring road type is unknown or too wide for the radius, or
// if a crossing leaves a stub or ring edge shorter than a few metres. One undo step; redo
// reproduces the same ids.
RoadCommandPtr make_place_roundabout(Vec3 centre, double radius, const std::string& ring_road_type,
                                     const RegionPack& region);

enum class RoundaboutConnectorRole {
    Entry,    // approach inbound lane -> ring lane (gives way to the ring)
    Exit,     // ring lane -> approach outbound lane
    Through,  // ring lane -> ring lane across a ring node (from the node's junction)
    Ring,     // along a ring edge between the trim lines of consecutive ring nodes
};

struct RoundaboutConnector {
    RoundaboutConnectorRole role = RoundaboutConnectorRole::Ring;
    int ring_index = 0;     // ring node where it happens (Ring: the node it starts at)
    EdgeId from_edge, to_edge;
    int from_slot = 0, to_slot = 0;
    std::vector<Vec3> path;
    bool give_way = false;  // entries only
    // Entries only: the circulating movements they must yield to (Through at the same node and
    // the Ring connector arriving on the same lane). Indices into RoundaboutLayout::connectors.
    std::vector<int> conflicts;
};

struct SplitterIsland {
    int arm = 0;                 // index into JunctionGroup::arms
    std::vector<Vec2> polygon;   // teardrop, CCW
};

struct RoundaboutLayout {
    JunctionGroup group;
    PolygonSet island;       // central island: one CCW ring
    PolygonSet carriageway;  // circulatory carriageway: outer CCW ring + island-shaped CW hole
    std::vector<SplitterIsland> splitters;
    std::vector<RoundaboutConnector> connectors;
};

// Junction output for one group. Builds the ring nodes' junctions internally.
RoundaboutLayout build_roundabout(const JunctionGroup& group, const RoadGraph& graph, const RegionPack& region,
                                  const JunctionOverrides& overrides);

}  // namespace cp
