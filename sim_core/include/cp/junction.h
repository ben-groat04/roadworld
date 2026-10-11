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
// Topology thresholds of the classifier (what counts as "nearly parallel" / "opposite"), not design values: left in code.
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

// Approach shaping inside the junction (roundabout splitter flares, turn-pocket widening). Derived, never
// authored: the kerb line on each side lies `spread` metres further out than kerb_left / kerb_right and the
// lanes on that side lie the same distance further out, so no lane gets narrower beside an island or a pocket.
// The spread is constant up to s = full (so the corner fillets, which lie inside it, see ordinary parallel
// kerbs) and falls smoothly (smoothstep) to 0 at s = length, which is the arm's trim line: connectors
// start there on the unshifted lanes. All zero = an ordinary arm.
struct ArmFlare {
    double spread_left = 0, spread_right = 0;  // m, outward shift of the left (+lateral) / right kerb and lanes
    double full = 0;                           // m from the node: the spread holds up to here
    double length = 0;                         // m from the node: the spread is 0 here and beyond (== the trim)
    bool active() const { return length > 0 && (spread_left > 0 || spread_right > 0); }
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
    // Full edge centreline from the node outward (s measured from the node, heading =
    // heading_out(s)), 0..edge_length. Trim lines, connector ends and merge tapers are read
    // from it via arm_point().
    std::vector<EdgeSample> centreline;
    ArmFlare flare;             // see ArmFlare; set by the shape requests in JunctionOverrides

    int inbound_count() const;   // number of inbound lanes
    int outbound_count() const;
    bool one_way() const { return inbound_count() == 0 || outbound_count() == 0; }
};

// Numerical bound, not a design value: arm centrelines are sampled this far from the node and a normal junction trim
// never exceeds it (merge/diverge trims may reach edge_length / 2). Left in code.
inline constexpr double kArmSampleLength = 40.0;  // m

// Point on the arm at arc length s from the node and lateral offset (+ left of heading_out),
// plus the local outward heading. s is clamped to the arm centreline (0..edge_length).
struct ArmPoint {
    Vec3 pos;
    double heading_out = 0;
};
ArmPoint arm_point(const JunctionArm& arm, double s, double lateral);

// Outward spread (m) of the arm's left (+lateral) or right kerb at s: the ArmFlare profile (0 for an ordinary arm).
double flare_spread(const JunctionArm& arm, double s, bool left);
// `lateral` (a kerb line or lane centre, + = left of heading_out) once the flare at s is applied: moved away
// from the centreline by the spread of its side. Identity for an ordinary arm.
double flared_lateral(const JunctionArm& arm, double s, double lateral);

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
    bool via_pocket = false;  // offside turn that leaves the through lane for the turn pocket (Junction::pockets)
};

// ---------------------------------------------------------------- geometry [B]

// Kerb fillet between two adjacent arms. Polyline runs from the tangent point on
// arms[arm_right]'s kerb to the tangent point on arms[arm_left]'s kerb (z from the arms).
struct CornerCurve {
    int arm_right = 0, arm_left = 0;   // arm_left = (arm_right + 1) % n
    EdgeId edge_r, edge_l;             // override key parts
    double angle = 0;                  // rad, CCW angle from arm_right to arm_left, (0, 2pi)
    double radius = 0;                 // m, as used; 0 = sharp corner or no fillet. Never above requested_radius
    double requested_radius = 0;       // m, override or default. radius < requested_radius means the trims
                                       // (edge length / 40 m cap) left no room: an ordinary clamp, NOT degraded
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
    bool refuge = false;   // dual carriageway: this is one half of a crossing split at a median refuge
};

// Polygons are rings of Vec2 (implicit close), outer rings CCW, holes CW. Clipper2 is the
// intended implementation (third_party/clipper2) but is not part of this header.
using PolygonSet = std::vector<std::vector<Vec2>>;

// ---------------------------------------------------------------- the junction

enum class JunctionKind { Plain, Roundabout };  // Roundabout reserved; see docs/junctions.md

// A ghost-island turn pocket on a major arm of a priority junction (docs/junctions.md section 9): derived,
// never authored. The arm's offside kerb and outbound lane are shifted out by `lane_width` (ArmFlare) so the
// inbound through lane keeps its width; the pocket lane takes the freed strip next to the centre line. Along
// the arm (s from the node): pocket lane for s in [0, storage_length], hatched ghost island (paint only, no
// kerb) on the approach taper for s in [storage_length, storage_length + taper_length], nose at the far end.
// The offside-turn connector into `to_arm` (LaneConnector::via_pocket) is the only user of the pocket lane.
struct TurnPocket {
    int arm = -1;                // index into Junction::arms: the major arm whose inbound traffic turns offside
    int to_arm = -1;             // index of the minor arm it leads to
    bool forced = false;         // on because of a per-junction override rather than the default rule
    double lane_width = 0;       // m
    double storage_length = 0;   // m, pocket lane length from the node (includes the corner region)
    double taper_length = 0;     // m, length of the hatched approach taper beyond it
    std::vector<Vec3> centreline;  // pocket lane centre line from the nose end to the node, on the shifted lane
    PolygonSet lane;             // the pocket lane surface
    PolygonSet ghost;            // the hatched ghost island on the taper (no kerb)
};

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
    PolygonSet gore;                   // [C] Merge/Diverge only: nose/gore area between the two nearly parallel arms (hatching)
    std::vector<CrossingSpot> crossings;
    std::vector<TurnPocket> pockets;   // ghost-island turn pockets (priority junctions); see TurnPocket
    // [B] true when the geometry could not be built cleanly: the fillet solver did not converge,
    // a kerb intersection lies beyond a trim line even at radius 0 (the corner becomes a chord
    // between the trim-line kerb points), the carriageway union came back empty, duplicate
    // headings, or a polygon that failed validation; degraded_reason says why (first cause).
    // A fillet merely smaller than asked because the trims leave no room is NOT degraded: compare
    // CornerCurve::radius with CornerCurve::requested_radius.
    bool degraded = false;
    std::string degraded_reason;

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
    // Arm identity, only meaningful for a loop edge (a == b, two arms with one edge id): which
    // end of the edge the lane is on. Set it to JunctionArm::at_a for loop edges and leave true
    // for every other edge.
    bool from_at_a = true;
    bool to_at_a = true;
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

// Identifies one arm of a node (at_a only matters for a loop edge, which has two arms with one edge id).
struct ArmKey {
    NodeId node;
    EdgeId edge;
    bool at_a = true;
    friend auto operator<=>(const ArmKey&, const ArmKey&) = default;
};

// Wanted approach shaping for one arm. DERIVED data: roundabout splitter flares and turn pockets fill it in
// before building the junction; the player does not author it. The generator turns it into JunctionArm::flare:
// the spread holds for max(hold, corner trim + a margin) metres, then falls to 0 over `taper` metres; both
// are cut back so the whole shape fits inside half the edge (the arm's trim becomes its end). When even
// `min_taper` does not fit the shape is dropped and the junction is flagged degraded.
struct ArmShape {
    double spread_left = 0, spread_right = 0;  // m
    double hold = 0;                           // m, least length over which the full spread holds
    double taper = 0;                          // m, wanted length over which it falls to 0 (after the hold)
    double length = 0;                         // m, or: wanted distance from the node to where it reaches 0 (overrides taper)
    double min_taper = 0;                      // m, shortest taper accepted when space is short
    std::string what = "approach shape";       // names the shape in the degraded reason
};

struct PocketKey {
    NodeId node;
    EdgeId edge;  // the major arm's edge (a node has at most one pocket per major arm)
    friend auto operator<=>(const PocketKey&, const PocketKey&) = default;
};

struct JunctionOverrides {
    std::map<ConnectorKey, ConnectorOverride> connectors;
    std::map<CornerKey, double> corner_radius;  // m; 0 = sharp
    // Merge / diverge nodes: gore (chevron area) length in metres for that node, replacing the region's speed table (negative = ignored).
    std::map<NodeId, double> gore_length;
    std::map<LaneKey, bool> lane_connects;      // false: lane takes no part in connectors at this node
    // Turn pockets: true forces a pocket on that major arm (when the geometry allows one), false suppresses
    // the default one. Absent = the region rule decides. Edited through make_set_turn_pocket_override().
    std::map<PocketKey, bool> turn_pockets;
    // Derived, not authored (see ArmShape): filled by apply_roundabouts() for the ring nodes.
    std::map<ArmKey, ArmShape> arm_shapes;
};

// ---------------------------------------------------------------- functions

// Fallback only for callers with no region pack (resolve_corner_radii with a null region); with a pack the radius
// is JunctionRules::default_corner_radius (region.json junction.default_corner_radius_m).
inline constexpr double kDefaultCornerRadius = 6.0;  // m

// Real (junction.cpp). Arms of `node` in CCW order, with lanes from instantiate_lanes using
// region.driving_side. A loop edge (a == b) yields two arms with the same edge id.
std::vector<JunctionArm> collect_arms(const RoadGraph& graph, NodeId node, const RegionPack& region);

// Real (junction.cpp).
NodeClass classify_arms(const std::vector<JunctionArm>& arms);
NodeClass classify_node(const RoadGraph& graph, NodeId node, const RegionPack& region);

// Real (junction.cpp). Indices of the most major arms: highest road_class, then widest
// (half_width_left + half_width_right). Several arms can tie.
std::vector<int> major_arm_indices(const std::vector<JunctionArm>& arms);

// Real (junction.cpp). Radius per corner k: override if present, else (with a region) the pack's default for
// the pair of road classes meeting, scaled by the skew factor of the corner angle (larger on the obtuse
// corner, smaller on the acute one), else the larger of the arms' road-type radii, else kDefaultCornerRadius.
// Without a region: the arms' road-type radii only, no class table and no skew.
std::vector<double> resolve_corner_radii(NodeId node, const std::vector<JunctionArm>& arms,
                                         const JunctionOverrides& overrides, const RegionPack* region = nullptr);

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
void generate_connectors(Junction& junction, DrivingSide side, const JunctionOverrides& overrides,
                         const RegionPack& region);

// [A] Pure helper, also used by C: path for one connector between two lanes given the trims.
// `via` null = default curve. First/last points as documented at LaneConnector::path.
std::vector<Vec3> make_connector_path(const JunctionArm& from_arm, const LaneRef& from_lane, double trim_from,
                                      const JunctionArm& to_arm, const LaneRef& to_lane, double trim_to,
                                      const std::vector<Vec3>* via);

// [B] Fills corners, carriageway, pavements, crossings from junction.arms/trims/corner_radii.
// `graph` gives access to edge geometry for kerb offsets. Does not touch connectors.
void generate_geometry(Junction& junction, const RoadGraph& graph, const RegionPack& region,
                       const JunctionOverrides& overrides);

// [C] Taper length (m) of a merge/diverge from the main carriageway design speed (m/s): the region pack's
// junction.merge.taper_length table. The odd arm's trim is the full taper,
// clamped only to half the edge length.
double merge_taper_length(const RegionPack& region, double design_speed_ms);

// [C] For cls Merge/Diverge only: sets trims, connectors (lane add/drop and taper), corners,
// carriageway, pavements, crossings, replacing both A and B for that node.
void generate_merge_diverge(Junction& junction, const RoadGraph& graph, const RegionPack& region,
                            const JunctionOverrides& overrides);

// Real (junction.cpp). Orchestrates, per node with 3+ arms:
//   collect_arms -> classify -> major_arms -> resolve_corner_radii
//   Junction: compute_trims -> generate_connectors (A) -> generate_geometry (B)
//   Merge/Diverge: generate_merge_diverge (C)
// If `only` is non-empty, just those nodes are built (dirty-node regeneration).
// It ends with apply_roundabouts() (junction_roundabout.h), so ring nodes of roundabouts come out as
// kind = Roundabout junctions; callers that also want the group-level result (islands, give-way)
// call build_plain_junctions + apply_roundabouts themselves.
JunctionSet build_junctions(const RoadGraph& graph, const RegionPack& region, const JunctionOverrides& overrides,
                            const std::vector<NodeId>& only = {});

// The same without the roundabout post-pass.
JunctionSet build_plain_junctions(const RoadGraph& graph, const RegionPack& region, const JunctionOverrides& overrides,
                                  const std::vector<NodeId>& only = {});

}  // namespace cp
