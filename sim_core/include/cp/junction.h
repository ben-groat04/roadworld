#pragma once
// Junction generator: shared interface (docs/junctions.md). Derived data only: everything
// here is rebuilt from the road graph + JunctionOverrides and never saved.
//
// Three parts fill these structs in parallel; the contracts between them are in the
// comments below and in docs/junctions.md:
//   [A] lane connectors   (junction_connectors.cpp)
//   [B] junction geometry (junction_geometry.cpp): trims, corners, polygons, crossings
//   [C] merges/diverges   (junction_merge.cpp): owns Merge/Diverge nodes completely
// Orchestration, collect_arms, classify_node live in junction.cpp.
//
// Conventions
//  - Metres, radians (CCW from +x), x east, y north, z up. Driving-side agnostic: "nearside"
//    is the side traffic keeps to (left for DrivingSide::Left). Geometry that is purely
//    planar (headings, lateral offsets) uses left/right; anything about traffic uses
//    nearside/offside via the `side` argument.
//  - Everything about an arm is expressed in the arm's OUTWARD frame: s = 0 at the node,
//    heading_out points away from the node, lateral + = left of heading_out.
//  - Arms are sorted CCW by heading_out (ascending in (-pi, pi], ties by edge id).
//  - Corner k lies between arms[k] and arms[(k+1) % n] (arms[k+1] is CCW of arms[k], i.e. on
//    its left looking outward). Its override key is (node, edge_l = arms[k+1].edge,
//    edge_r = arms[k].edge).
#include <compare>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "cp/edge_sampling.h"
#include "cp/lanes.h"
#include "cp/region_pack.h"
#include "cp/road_graph.h"
#include "cp/vec.h"

namespace cp {

// ---------------------------------------------------------------- classification

enum class NodeClass {
    End,       // 1 arm: dead end
    Joint,     // 2 arms: no junction behaviour (type/structure change, plain bend)
    Junction,  // 3+ arms: general at-grade junction (T, X, Y, star); owned by A + B
    Merge,     // 3 arms, one-way: two arms flow in, one flows out, the two nearly parallel; owned by C
    Diverge,   // 3 arms, one-way: one arm flows in, two flow out, the two nearly parallel; owned by C
};

// Merge/Diverge test (classify_arms): all three arms one-way, the two same-direction arms
// separated by less than kMergeMaxAngle, the odd arm at least kMergeMinOppositeAngle from both.
inline constexpr double kMergeMaxAngle = 0.7854;         // 45 degrees
inline constexpr double kMergeMinOppositeAngle = 2.3562;  // 135 degrees

// ---------------------------------------------------------------- arms and lanes

// A lane of an arm's edge that takes part in connectors (lane type connects_at_junctions AND
// a travel direction, so pavements are excluded; they feed crossings instead).
struct LaneRef {
    EdgeId edge;
    int slot = 0;             // Lane::slot on `edge` (stable, used in override keys)
    bool inbound = false;     // true: travels toward the node, false: away from it
    double lateral = 0;       // m, lane centre in the arm's outward frame (+ = left of heading_out)
    double width = 0;         // m
    std::string lane_type;
};

struct JunctionArm {
    EdgeId edge;
    bool at_a = true;           // true: this arm is edge.a's end at the node, else edge.b's
    double heading_out = 0;     // rad, direction of the centreline leaving the node
    // Connecting lanes (see LaneRef), ordered by lateral, left to right looking outward.
    std::vector<LaneRef> lanes;
    // Full cross-section half widths from the centreline, looking outward (include kerb/pavement).
    double half_width_left = 0, half_width_right = 0;
    // Carriageway half widths: lateral position of the kerb line (inner face of the outermost
    // kerb lane) per side; equals the half width when that side has no kerb lane.
    double kerb_left = 0, kerb_right = 0;
    // Proxy for road class until the region pack gets an explicit field: design speed in km/h
    // (rounded). Higher = more major. Only compared between arms.
    int road_class = 0;
    double corner_radius = 0;   // m, region-pack default fillet radius of this arm's road type (0 = n/a)
    double edge_length = 0;     // m, bounds the usable trim
    // Centreline from the node outward (s measured from the node, heading = heading_out(s)),
    // sampled for at most kArmSampleLength. Trim lines and connector ends are read from it
    // via arm_point().
    std::vector<EdgeSample> centreline;

    int inbound_count() const;   // number of inbound lanes
    int outbound_count() const;
    bool one_way() const { return inbound_count() == 0 || outbound_count() == 0; }
};

inline constexpr double kArmSampleLength = 40.0;  // m of centreline stored per arm; max useful trim

// Point on the arm at arc length s from the node and lateral offset (+ left of heading_out),
// plus the local outward heading. s is clamped to the stored centreline.
struct ArmPoint {
    Vec3 pos;
    double heading_out = 0;
};
ArmPoint arm_point(const JunctionArm& arm, double s, double lateral);

// ---------------------------------------------------------------- connectors [A]

// Nearside/offside are relative to DrivingSide; use turn_is_left() to get planar left/right.
enum class TurnKind { Straight, Nearside, Offside, UTurn };

// Classifies the heading change from the incoming travel direction to the outgoing one.
// |turn| < 30 deg: Straight; > 150 deg: UTurn; otherwise left turns are Nearside for
// DrivingSide::Left and Offside for Right (mirrored). `turn` = outgoing travel heading minus
// incoming travel heading, wrapped to (-pi, pi]; + = turning left.
TurnKind classify_turn(double turn, DrivingSide side);
// For a Nearside/Offside turn: whether it turns planar-left under `side`.
bool turn_is_left(TurnKind kind, DrivingSide side);

struct LaneConnector {
    int arm_from = 0, lane_from = 0;  // indices into Junction::arms and arms[arm_from].lanes
    int arm_to = 0, lane_to = 0;      // (lane_* indexes the arm's `lanes`, not the slot)
    TurnKind kind = TurnKind::Straight;
    // Centre-line path from the trim line of arm_from to the trim line of arm_to: first point
    // = arm_point(arm_from, trims[arm_from], lanes[lane_from].lateral), last point likewise
    // for the 'to' side; tangent-continuous with both lanes. z interpolated between ends.
    std::vector<Vec3> path;
    // Indices into Junction::connectors of connectors whose paths cross this one in plan
    // (|dz| < region plan_crossing_junction_dz_m), or that end in the same to-lane (merge).
    // Symmetric, sorted, never contains itself. Diverging (same from-lane) is not a conflict.
    std::vector<int> conflicts;
    bool enabled = true;      // false: disabled by override; kept so the UI can offer re-enabling
    bool overridden = false;  // an override entry affects this connector (enabled flag or custom path)
};

// ---------------------------------------------------------------- geometry [B]

// Kerb fillet between two adjacent arms. Polyline runs from the tangent point on
// arms[arm_right]'s kerb to the tangent point on arms[arm_left]'s kerb (z from the arms).
struct CornerCurve {
    int arm_right = 0, arm_left = 0;   // arm_left = (arm_right + 1) % n
    EdgeId edge_r, edge_l;             // override key parts
    double angle = 0;                  // rad, CCW angle from arm_right to arm_left, (0, 2pi)
    double radius = 0;                 // m, as used (override or default); 0 = sharp corner
    bool overridden = false;
    std::vector<Vec3> kerb;            // empty if there is no corner (angle >= pi: straight kerb)
};

// Where a pedestrian crossing sits on an arm (v0: positions only, no signals/priority).
struct CrossingSpot {
    int arm = 0;
    double s = 0;          // m from the node along the arm (just outside the trim line)
    Vec3 centre;           // on the arm centreline
    double heading = 0;    // rad, direction of travel across the road (perpendicular to the arm)
    double length = 0;     // m, kerb to kerb
};

// Polygons are rings of Vec2 (implicit close), outer rings CCW, holes CW. Clipper2 is the
// intended implementation (third_party/clipper2) but is not part of this header.
using PolygonSet = std::vector<std::vector<Vec2>>;

// ---------------------------------------------------------------- the junction

enum class JunctionKind { Plain, Roundabout };  // Roundabout reserved; see docs/junctions.md

struct Junction {
    NodeId node;
    Vec3 pos;
    DrivingSide side = DrivingSide::Left;
    NodeClass cls = NodeClass::Junction;
    JunctionKind kind = JunctionKind::Plain;
    std::vector<NodeId> group_nodes;  // reserved: ring nodes of a roundabout group

    std::vector<JunctionArm> arms;    // CCW order, see top of file
    std::vector<int> major_arms;      // indices of arms with the highest (road_class, width); >= 1 entry
                                      // when arms is non-empty. Used later for give-way markings.

    // [B] fills, [C] for Merge/Diverge:
    std::vector<double> corner_radii;  // per corner k (size n), resolved incl. overrides
    std::vector<double> trims;         // per arm, m from the node to the trim line (the junction boundary)
    std::vector<CornerCurve> corners;  // per corner k (size n)
    PolygonSet carriageway;            // asphalt inside the trim lines and corners
    PolygonSet pavements;              // pavement/kerb polygons around the corners and between arms
    std::vector<CrossingSpot> crossings;

    // [A] fills (C for Merge/Diverge):
    std::vector<LaneConnector> connectors;

    // Index of the connector for (arm_from, lane_from, arm_to, lane_to), or -1.
    int find_connector(int arm_from, int lane_from, int arm_to, int lane_to) const;
};

struct JunctionSet {
    std::map<NodeId, Junction> junctions;  // only nodes with 3+ arms; End/Joint nodes are absent
    const Junction* find(NodeId n) const;
};

// ---------------------------------------------------------------- overrides (road-graph-model.md §5)

struct ConnectorKey {
    NodeId node;
    EdgeId from_edge;
    int from_slot = 0;
    EdgeId to_edge;
    int to_slot = 0;
    friend auto operator<=>(const ConnectorKey&, const ConnectorKey&) = default;
};

struct ConnectorOverride {
    bool enabled = true;
    // Player-edited curve: interior control points only, in world coordinates. A re-adds the
    // end points on the current trim lines and fits the tangent-continuous path through `via`,
    // so the override survives trim changes. Empty/nullopt = default path.
    std::optional<std::vector<Vec3>> via;
};

struct CornerKey {
    NodeId node;
    EdgeId edge_l;
    EdgeId edge_r;
    friend auto operator<=>(const CornerKey&, const CornerKey&) = default;
};

// A lane's participation can be switched off (e.g. a lane the player wants to dead-end).
struct LaneKey {
    NodeId node;
    EdgeId edge;
    int slot = 0;
    friend auto operator<=>(const LaneKey&, const LaneKey&) = default;
};

struct JunctionOverrides {
    std::map<ConnectorKey, ConnectorOverride> connectors;
    std::map<CornerKey, double> corner_radius;  // m; 0 = sharp
    std::map<LaneKey, bool> lane_connects;      // false: lane takes no part in connectors at this node
};

// ---------------------------------------------------------------- functions

inline constexpr double kDefaultCornerRadius = 6.0;  // m; fallback when the region pack gives no corner_radius_m

// Real (junction.cpp). Arms of `node` in CCW order, with lanes from instantiate_lanes using
// region.driving_side. A loop edge (a == b) yields two arms with the same edge id.
std::vector<JunctionArm> collect_arms(const RoadGraph& graph, NodeId node, const RegionPack& region);

// Real (junction.cpp).
NodeClass classify_arms(const std::vector<JunctionArm>& arms);
NodeClass classify_node(const RoadGraph& graph, NodeId node, const RegionPack& region);

// Real (junction.cpp). Indices of the most major arms: highest road_class, then widest
// (half_width_left + half_width_right). Several arms can tie.
std::vector<int> major_arm_indices(const std::vector<JunctionArm>& arms);

// Real (junction.cpp). Radius per corner k: override if present, else kDefaultCornerRadius.
std::vector<double> resolve_corner_radii(NodeId node, const std::vector<JunctionArm>& arms,
                                         const JunctionOverrides& overrides);

// [B] Real (junction_geometry.cpp), pure. Trim distance per arm (m from the node along the
// centreline) so that a kerb fillet of radius radii[k] fits tangent to both kerb lines of
// corner k. Corner angle theta (CCW arm k -> arm k+1), kerb offsets a = arms[k].kerb_left,
// b = arms[k+1].kerb_right:
//   kerb-line intersection is at  a*cot(theta) + b/sin(theta)  along arm k and
//   a/sin(theta) + b*cot(theta)  along arm k+1; the fillet tangent points are a further
//   r / tan(theta/2) beyond that. Trim of an arm = max over its two corners (corners with
//   theta >= pi - 0.01 contribute 0), clamped to [0, min(edge_length / 2, kArmSampleLength)].
// radii.size() must equal arms.size() (else returns {}). Example: two 10 m wide kerb-to-kerb
// arms at 90 degrees, r = 6: 5 + 6 = 11 m.
// Merge/Diverge nodes (theta -> 0) are NOT handled here; C sets their trims itself.
std::vector<double> compute_trims(const std::vector<JunctionArm>& arms, const std::vector<double>& radii);

// [A] Fills junction.connectors from the arms' lanes and the trims: every inbound connecting
// lane to every outbound connecting lane of every OTHER arm (no auto turn pockets, no
// U-turns unless an override enables one), with kind, path, conflicts. Reads junction.trims
// (must be set), `overrides` (connector enable/disable/via, lane_connects). Order is
// deterministic: by arm_from, lane_from, arm_to, lane_to. Does not touch other fields.
void generate_connectors(Junction& junction, DrivingSide side, const JunctionOverrides& overrides);

// [A] Pure helper, also used by C: path for one connector between two lanes given the trims.
// `via` null = default curve. First/last points as documented at LaneConnector::path.
std::vector<Vec3> make_connector_path(const JunctionArm& from_arm, const LaneRef& from_lane, double trim_from,
                                      const JunctionArm& to_arm, const LaneRef& to_lane, double trim_to,
                                      const std::vector<Vec3>* via);

// [B] Fills corners, carriageway, pavements, crossings from junction.arms/trims/corner_radii.
// `graph` gives access to edge geometry for kerb offsets. Does not touch connectors.
void generate_geometry(Junction& junction, const RoadGraph& graph, const RegionPack& region,
                       const JunctionOverrides& overrides);

// [C] For cls Merge/Diverge only: sets trims, connectors (lane add/drop and taper), corners,
// carriageway, pavements, crossings, replacing both A and B for that node.
void generate_merge_diverge(Junction& junction, const RoadGraph& graph, const RegionPack& region,
                            const JunctionOverrides& overrides);

// Real (junction.cpp). Orchestrates, per node with 3+ arms:
//   collect_arms -> classify -> major_arms -> resolve_corner_radii
//   Junction: compute_trims -> generate_connectors (A) -> generate_geometry (B)
//   Merge/Diverge: generate_merge_diverge (C)
// If `only` is non-empty, just those nodes are built (dirty-node regeneration).
JunctionSet build_junctions(const RoadGraph& graph, const RegionPack& region, const JunctionOverrides& overrides,
                            const std::vector<NodeId>& only = {});

}  // namespace cp
