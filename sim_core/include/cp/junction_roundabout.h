#pragma once
// Roundabouts: a JunctionGroup of kind Roundabout (docs/road-graph-model.md section 4). A closed
// ring of one-way edges whose circulation follows DrivingSide (clockwise for left-hand traffic).
// Two ways in, one group out:
//   - make_place_roundabout: the dedicated tool, emits the ring and cuts the approach roads;
//   - recognise_roundabouts: promotes hand-drawn closed one-way loops.
// The group is always derived (recognised from the graph), never stored, so both routes yield
// the same object. build_roundabout turns a group into the layout (island, carriageway, splitter
// islands, entry/exit/through/ring connectors); apply_roundabouts is the post-pass over the
// JunctionSet from build_junctions that replaces the ring nodes' plain junctions by one
// Junction of kind Roundabout (see docs/junctions.md section 7).
#include <string>
#include <vector>

#include "cp/junction.h"
#include "cp/road_commands.h"

namespace cp {

// Tunables. Everything here is region-agnostic geometry or a placeholder for a region-pack
// field: roundabout_params() is the one place that will read the pack (it has no roundabout
// fields yet, so it returns these fallbacks).
struct RoundaboutParams {
    // Recognition limits (hand-drawn rings must be roughly round and small).
    double min_radius = 5.0;      // m, mean ring radius
    double max_radius = 80.0;     // m
    double min_roundness = 0.6;   // min/max distance of the ring from its centroid
    int max_ring_edges = 16;
    int min_arms = 3;             // approach edges attached to the ring
    // Forced kerb fillet radii at ring nodes (m): tight entries, easy exits; ring_corner_radius_factor
    // times the ring radius on the island side.
    double entry_corner_radius = 8.0;
    double exit_corner_radius = 12.0;
    double ring_corner_radius_factor = 1.0;
    // The tool.
    double max_arc = 1.62;           // rad, longest ring edge (about 93 degrees)
    double min_island_radius = 3.0;  // m, smallest central island the tool accepts
    double min_edge_length = 4.0;    // m, shortest ring edge or approach stub the tool accepts
    // Splitter islands on two-way approaches.
    double splitter_length = 12.0;   // m beyond the trim line
    double splitter_width = 1.8;     // m at the ring end
};
RoundaboutParams roundabout_params(const RegionPack& region);

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
// circulation matches region.driving_side, roughly round, with at least min_arms approach
// edges attached. Ring nodes with only the two ring edges (joints) are allowed. Groups are
// node-disjoint; order is deterministic.
std::vector<JunctionGroup> recognise_roundabouts(const RoadGraph& graph, const RegionPack& region);

// The tool. Every edge crossing the circle is cut at the ring (the part inside the circle is
// removed, including nodes left inside); the ring of one-way edges (at least 4 nodes, arcs of
// at most max_arc) is inserted with the circulation of region.driving_side. Ring nodes take the
// height of `centre`. Fails (and leaves the graph unchanged) if the ring road type is unknown
// or too wide for the radius, or if a crossing leaves a stub or ring edge shorter than
// min_edge_length. One undo step; redo reproduces the same ids.
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
    bool ok = false;         // false: nothing below is usable, `error` says why
    std::string error;
    PolygonSet island;       // central island: one CCW ring
    PolygonSet carriageway;  // circulatory carriageway and approach flares: outer CCW ring(s) + island-shaped CW hole
    std::vector<SplitterIsland> splitters;
    std::vector<RoundaboutConnector> connectors;
    // The ring nodes' own junctions (forced fillet radii applied); empty for joint nodes.
    std::map<NodeId, Junction> ring_junctions;
};

// Layout for one group. Builds the ring nodes' junctions internally with the forced entry/exit
// corner radii (player corner overrides in `overrides` win).
RoundaboutLayout build_roundabout(const JunctionGroup& group, const RoadGraph& graph, const RegionPack& region,
                                  const JunctionOverrides& overrides);

// Post-pass over the JunctionSet from build_junctions. For every recognised roundabout with at
// least one ring node in `set`, erases the ring nodes' plain junctions and inserts ONE Junction
// at group.nodes[0]: kind = Roundabout, group_nodes = the ring nodes, arms = the approach arms
// of all ring nodes (CCW about the ring centre, trims from the ring node junctions), carriageway
// = ring + flares with the island as a hole, one connector per (inbound lane, outbound lane of
// another arm) routed round the ring. Groups whose layout fails are left as plain junctions.
void apply_roundabouts(JunctionSet& set, const RoadGraph& graph, const RegionPack& region,
                       const JunctionOverrides& overrides);

}  // namespace cp
