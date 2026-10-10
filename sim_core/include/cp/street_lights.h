#pragma once
// Street light placement (docs/v0-brief.md: street lights are part of the road types). Derived data
// only: generated from the road graph, junction geometry and the region pack's per-road-type
// "street_lights" rules, never saved. The engine instances a column mesh (StreetLight::style) at each
// returned transform. Signs and other street furniture are not part of this.
//
// Rules, in short (all numbers come from RoadType::street_lights and StreetLightParams):
//  - A "run" is an edge plus the edges joined to it through 2-arm Joint nodes of the same road type and
//    one_way flag; the pattern is laid out over the whole run so the phase is continuous across joints.
//    Between the junction clearances at its two ends the run is divided into n equal cells (n chosen so
//    the cell is as close to the rule's spacing as a whole number allows) with a column at the middle of
//    each cell: symmetric about the run, no short remainder.
//  - Columns sit at the back of the footway where one exists, else in the verge at the setback from the
//    carriageway edge. single_side uses the nearside of the run direction, switching to the outside of
//    the bend where the curve is tight. Dual carriageway edges with a parallel opposite partner get
//    twin-arm columns in the median once per pair (the lower edge id emits), else verge-side columns.
//  - Each junction corner gets one column on an arm, clear of the corner kerb, the carriageway, crossings
//    and roundabout splitter islands. Roundabouts are lit round the outside of the ring (and optionally
//    on the central island). No column in a tunnel; on a bridge columns mount at the parapet line.
//  - Driving side enters only through the nearside helper of lanes.cpp: mirroring a scene and flipping
//    DrivingSide mirrors the result exactly. No randomness.
#include <map>
#include <string>
#include <vector>

#include "cp/junction.h"
#include "cp/junction_roundabout.h"
#include "cp/region_pack.h"
#include "cp/road_graph.h"

namespace cp {

// Left/Right are planar sides of the edge's a -> b direction; Median columns sit between two carriageways.
enum class LightSide { Left, Right, Median };

enum class LightOrigin {
    EdgePattern,      // regular spacing along an edge
    JunctionCorner,   // near a junction corner, on an arm
    RoundaboutRing,   // round the outside of a roundabout ring
    RoundaboutIsland, // on the central island
};

struct StreetLight {
    Vec3 position;               // column base on the surface (world doubles)
    double heading = 0;          // rad CCW from +x: the direction the bracket points (toward the carriageway)
    double mounting_height = 0;  // m, luminaire height above the base
    double outreach = 0;         // m, bracket length
    std::string style;           // renderer mesh id
    EdgeId edge;                 // road edge it belongs to (null for island columns)
    NodeId node;                 // junction node for JunctionCorner (null otherwise)
    double s = 0;                // arc length along `edge` (a -> b) of the column's foot
    LightSide side = LightSide::Left;
    LightOrigin origin = LightOrigin::EdgePattern;
    bool on_structure = false;   // mounted on a bridge (parapet line)
    bool twin_arm = false;       // median column with a second arm pointing the opposite way (heading + pi)
    EdgeId partner_edge;         // twin_arm: the other carriageway's edge
};

struct StreetLightParams {
    double spacing_tolerance = 0.2;   // target fraction the cell may differ from the rule's spacing

    // Junction handling.
    double junction_offset = 3.0;     // m beyond a junction trim line where the lit zone of an arm starts
    double carriageway_clearance = 0.5;  // m from any junction carriageway polygon
    double corner_clearance = 1.0;    // m from corner kerb fillets
    double crossing_half_width = 2.5; // m, half the width of a crossing zone along the arm
    double crossing_clearance = 1.0;  // m beyond the crossing zone and its kerb-to-kerb extent
    double island_clearance = 1.5;    // m from splitter islands
    double min_corner_angle = 0.8;    // rad, tighter corners (merge gores) get no corner column
    int max_shift_steps = 20;         // corner columns slide along the arm this many 1 m steps to find a clear spot

    // Geometry.
    double bend_outside_radius = 150.0;  // m, single_side columns go to the outside of bends tighter than this
    double parapet_inset = 0.3;          // m, bridge columns this far in from the outer edge of the deck
    double median_slack = 3.0;           // m, a carriageway pair may be this much wider than width + median gap
    double median_heading_tol = 0.25;    // rad, how parallel (antiparallel in travel) a partner must be
    double fold_guard = 0.9;             // skip columns whose lateral offset exceeds this fraction of the radius of curvature

    // Surface shape for the base height; same defaults as RenderMeshParams (the road type overrides them).
    double default_crossfall = 0.025;
    double default_kerb_upstand = 0.125;
    double default_footway_crossfall = 0.02;
    double default_verge_crossfall = 0.05;
};

// Equal-cell division of a lit length: `count` cells of `cell` m, columns at the cell midpoints.
struct LightPattern {
    int count = 0;
    double cell = 0;
    bool within_tolerance = false;  // |cell / spacing - 1| <= tolerance
};
// The count whose cell is nearest the spacing (ties to more), within tolerance when a whole number
// allows; 0 when even one cell would be under half the spacing. Pure (tests).
LightPattern plan_light_pattern(double available_length, double spacing, double tolerance = 0.2);

// ---------------------------------------------------------------- generation

// Pattern columns of one edge (the part of its run that falls on this edge). Edges in an ok roundabout
// layout's ring are lit by generate_roundabout_street_lights instead and give nothing here.
std::vector<StreetLight> generate_edge_street_lights(const RoadGraph& graph, const RegionPack& region,
                                                     const JunctionSet& junctions, EdgeId edge,
                                                     const std::vector<RoundaboutLayout>* roundabouts = nullptr,
                                                     const StreetLightParams& params = {});

// Corner columns of one junction node (none for End/Joint nodes, nodes without a junction or ring arms).
std::vector<StreetLight> generate_junction_street_lights(const RoadGraph& graph, const RegionPack& region,
                                                         const JunctionSet& junctions, NodeId node,
                                                         const std::vector<RoundaboutLayout>* roundabouts = nullptr,
                                                         const StreetLightParams& params = {});

// Ring and central island columns of one roundabout group (empty when the layout is not ok).
std::vector<StreetLight> generate_roundabout_street_lights(const RoadGraph& graph, const RegionPack& region,
                                                           const JunctionSet& junctions, const RoundaboutLayout& layout,
                                                           const std::vector<RoundaboutLayout>* roundabouts = nullptr,
                                                           const StreetLightParams& params = {});

struct StreetLightResult {
    std::map<EdgeId, std::vector<StreetLight>> edges;
    std::map<NodeId, std::vector<StreetLight>> junctions;    // by junction node
    std::map<NodeId, std::vector<StreetLight>> roundabouts;  // by the group's first ring node
    // Everything, edges then junctions then roundabouts, in key order (deterministic).
    std::vector<StreetLight> merged() const;
};

// Builds lights for the given edges / junction nodes; both empty = the whole graph, every junction and
// every roundabout layout. With a non-empty selection roundabouts are rebuilt when their ring touches it.
StreetLightResult generate_street_lights(const RoadGraph& graph, const RegionPack& region,
                                         const JunctionSet& junctions,
                                         const std::vector<RoundaboutLayout>* roundabouts = nullptr,
                                         const StreetLightParams& params = {},
                                         const std::vector<EdgeId>& only_edges = {},
                                         const std::vector<NodeId>& only_nodes = {});

// Dirty rebuild into an existing result. The dirty nodes/edges are first widened by one hop (the neighbouring
// nodes of dirty nodes and of the ends of dirty edges), then it regenerates the dirty edges, the edges at those
// nodes, runs that
// share a joint with them, median partners of dirty dual edges and the junction / roundabout entries of the
// dirty nodes (and of the end nodes of dirty edges); drops entries for edges, junctions and groups that no
// longer exist (pass removed ids: RoadGraph::dirty_nodes()/dirty_edges() include them). The result equals a full
// rebuild for planar networks; lights also depend on junction polygons spatially near an edge, which the hop
// widening does not cover.
void rebuild_dirty_street_lights(StreetLightResult& cache, const RoadGraph& graph, const RegionPack& region,
                                 const JunctionSet& junctions, const std::vector<RoundaboutLayout>* roundabouts,
                                 const std::vector<NodeId>& dirty_nodes, const std::vector<EdgeId>& dirty_edges,
                                 const StreetLightParams& params = {});

}  // namespace cp
