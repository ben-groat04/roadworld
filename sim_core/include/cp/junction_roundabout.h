#pragma once
// Roundabouts (docs/junctions.md section 7, docs/road-graph-model.md section 4).
//
// A roundabout is NOT a merged junction. Its ring is ordinary, individually editable one-way
// edges whose road type carries RoadType::roundabout_ring, and every ring node keeps its own
// Junction. "Roundabout" is a property layered on top:
//   - find_roundabouts():   groups = closed directed loops of flagged edges circulating the way
//                           DrivingSide dictates. Only flagged edges are walked; anything else
//                           that is flagged but not a clean ring is reported, never guessed.
//   - apply_roundabouts():  post-pass over the JunctionSet from build_junctions: marks every ring
//                           node's Junction (kind = Roundabout, group_nodes), forces entry/exit
//                           corner radii, creates a Joint-class Junction for ring nodes that have
//                           only the two ring arms (so JunctionSet::find works for every ring
//                           node), flags unbuildable rings as degraded, and returns the group-level
//                           results (islands, splitters, give-way).
//   - make_place_roundabout(): a preset that draws the ring (and cuts the approach roads).
#include <string>
#include <vector>

#include "cp/junction.h"
#include "cp/road_commands.h"

namespace cp {

// The region-pack tunables (RegionPack::roundabout; fallbacks live in RoundaboutRules).
const RoundaboutRules& roundabout_params(const RegionPack& region);

// First (smallest id) road type flagged roundabout_ring, or "" if the pack has none.
std::string default_ring_road_type(const RegionPack& region);

struct RoundaboutArm {
    int ring_index = 0;   // index into JunctionGroup::nodes
    EdgeId edge;          // the approach edge (not a ring edge)
    bool entry = false;   // carries traffic onto the ring
    bool exit = false;    // carries traffic off the ring (both set on a two-way approach)
};

// A valid ring. It exists as soon as the loop closes: the island is built and the ring nodes
// are marked even with no approach roads (a ring with fewer than 3 arms in total is just an
// unattached loop; nothing below treats the arm count as a requirement).
struct JunctionGroup {
    JunctionKind kind = JunctionKind::Roundabout;
    std::vector<NodeId> nodes;       // ring nodes in circulation order, nodes[0] has the smallest id
    std::vector<EdgeId> ring_edges;  // ring_edges[i] runs nodes[i] -> nodes[(i + 1) % n]
    std::vector<RoundaboutArm> arms; // approach edges, ordered by ring index then edge id
    Vec2 centre;                     // centroid of the ring centreline
    double radius = 0;               // m, mean distance of the ring centreline from the centre
    bool clockwise = true;
};

// A flagged edge set that is not a clean ring. Reported to the caller and shown as degraded
// on the Junctions of its nodes; never turned into a group.
struct RoundaboutProblem {
    std::vector<NodeId> nodes;
    std::vector<EdgeId> edges;
    std::string what;
};

struct RoundaboutScan {
    std::vector<JunctionGroup> groups;
    std::vector<RoundaboutProblem> problems;
};

// Finds roundabout groups. Walks only edges of roundabout_ring road types. Malformed means: not
// closed, a node without exactly one flagged in-edge and one flagged out-edge (figure of eight,
// two rings sharing a node, a T on the ring), a flagged edge that is two-way or a self-loop, fewer
// than 3 ring nodes, or circulation against DrivingSide. A loop that mixes flagged and unflagged
// road types is not a ring: its flagged part is an open chain and is reported as such.
// `only` empty: whole graph. Otherwise just the rings touching those nodes (dirty rebuild): the
// cost is proportional to the rings found, not to the graph size.
RoundaboutScan find_roundabouts(const RoadGraph& graph, const RegionPack& region, const std::vector<NodeId>& only = {});

// Tool validation: empty string if make_place_roundabout would succeed, else the reason.
std::string check_place_roundabout(const RoadGraph& graph, Vec3 centre, double radius, const std::string& ring_road_type,
                                   const RegionPack& region);

// The preset. Every edge crossing the circle is cut at the ring (the part inside the circle is
// removed along with nodes left without edges inside it). It FAILS, graph untouched, when:
// the road type is not a roundabout_ring type; the radius is outside [min_tool_radius,
// max_tool_radius] or leaves an island under min_island_radius; an existing road lies wholly
// inside the circle (it would be a road in the island); a roundabout ring or a bridge/tunnel
// crosses the circle; a stub or ring edge would be shorter than min_edge_length. Nothing the tool
// did not create is ever deleted other than the inside parts cut by that rule. The ring has at
// least 4 nodes, arcs of at most max_arc, is flat at centre.z and circulates per DrivingSide.
// One undo step; redo reproduces the same ids.
RoadCommandPtr make_place_roundabout(Vec3 centre, double radius, const std::string& ring_road_type,
                                     const RegionPack& region);

// A splitter island. Two-way approach: a short kerbed teardrop (roughly a triangle with a rounded nose) on the
// arm's centreline, widest at the ring (the full width of the flare's gap between the two lanes) and tapering to
// the nose `length` metres from the ring's outer kerb. The flare's gap is longer than the island when the corner
// fillets need it (the lanes keep their width, the outer kerbs flare), so the rest is paint, not kerb: `paint` is
// the whole gap between the two flared lanes (ring end to the end of the flare), of which the island is the
// kerbed part; markings put the ladder hatching and the lane-edge lines on it. Dual-carriageway approach (two
// one-way arms at adjacent ring nodes): the central reservation between the two carriageways carried into the
// ring, `arm` and `arm_b` being the two arms (no paint area).
struct SplitterIsland {
    int arm = 0;                 // index into JunctionGroup::arms
    int arm_b = -1;              // dual-carriageway pair: the other arm
    double length = 0;           // m, from the ring edge to the nose
    double width = 0;            // m, at the ring end
    std::vector<Vec2> polygon;   // CCW: the kerbed island
    // Two-way approach only (empty / 0 for a dual-carriageway median):
    std::vector<Vec2> paint;     // CCW: the whole gap between the flared lanes, ring end to flare end (contains polygon)
    double ring_s = 0;           // m along the arm from the ring node: the ring's outer kerb, where the island starts
    double nose_s = 0;           // m along the arm: the island's nose
    double base_half = 0;        // m: the island's half width at ring_s
    double nose_r = 0;           // m: radius of the rounded nose (centre at nose_s - nose_r on the arm centreline)
};

// Which connectors of a ring node give way, and to what. Priority data only (no lane choice or
// traffic behaviour): v0 needs it for give-way markings. Wanted later as fields on LaneConnector
// (`bool give_way; std::vector<int> yields_to;`); until then it lives here, keyed by node and
// connector index into that node's Junction::connectors.
struct GiveWay {
    NodeId node;
    int connector = 0;              // approach arm -> ring-out arm connector (an entry)
    std::vector<int> yields_to;     // ring-in -> ring-out connectors it must give way to
};

// Group-level result: derived per group, not stored on any Junction.
struct RoundaboutLayout {
    JunctionGroup group;
    bool ok = false;         // false: the island and splitters are empty and `error` says why
    std::string error;
    PolygonSet island;       // central island: one CCW ring (the raised part: inside the apron when there is one)
    PolygonSet apron;        // over-run apron collar round the island (small rings only): outer CCW ring + CW hole, else empty
    // Render data from the region rules (surface classes are lane-type surface names).
    std::string island_surface, splitter_surface, apron_surface;
    double island_kerb_upstand = 0;  // m; 0 = the render default
    bool island_kerbed = true;       // the central island and splitters have a raised kerb (the apron does not)
    std::vector<SplitterIsland> splitters;
    std::vector<GiveWay> give_way;
    // The ring nodes' own junctions with the forced fillet radii (and player overrides) applied;
    // absent for ring nodes that have only the two ring arms.
    std::map<NodeId, Junction> ring_junctions;
};

// Layout for one group: builds the ring nodes' junctions (forced radii; a player corner override
// wins), then island, splitters and give-way.
RoundaboutLayout build_roundabout(const JunctionGroup& group, const RoadGraph& graph, const RegionPack& region,
                                  const JunctionOverrides& overrides);

struct RoundaboutResult {
    std::vector<RoundaboutLayout> layouts;
    std::vector<RoundaboutProblem> problems;
};

// Post-pass over the JunctionSet from build_junctions(graph, region, overrides[, only]). For
// every group with a ring node in `set` (or, when `only` is non-empty, touching `only`), replaces
// the ring nodes' junctions by the forced-radius ones with kind = Roundabout and group_nodes set.
// Failures (a layout that cannot be built, malformed rings) set Junction::degraded on the ring
// nodes' junctions and are returned. Use the same `only` as for build_junctions.
RoundaboutResult apply_roundabouts(JunctionSet& set, const RoadGraph& graph, const RegionPack& region,
                                   const JunctionOverrides& overrides, const std::vector<NodeId>& only = {});

}  // namespace cp
