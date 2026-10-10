// Junction orchestration and the small shared pieces: arm collection, classification,
// major-arm derivation, override resolution. See docs/junctions.md.
#include "cp/junction.h"
#include "cp/geom.h"
#include "cp/junction_roundabout.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <string>

namespace cp {

int JunctionArm::inbound_count() const {
    return static_cast<int>(std::count_if(lanes.begin(), lanes.end(), [](const LaneRef& l) { return l.inbound; }));
}

int JunctionArm::outbound_count() const { return static_cast<int>(lanes.size()) - inbound_count(); }

ArmPoint arm_point(const JunctionArm& arm, double s, double lateral) {
    const auto& c = arm.centreline;
    if (c.empty()) return {};
    std::size_t i = 0;
    while (i + 1 < c.size() && c[i + 1].s < s) ++i;
    ArmPoint out;
    Vec3 p = c[i].pos;
    double h = c[i].heading;
    if (i + 1 < c.size() && s > c[i].s) {
        const double span = c[i + 1].s - c[i].s;
        const double t = span > 0 ? std::clamp((s - c[i].s) / span, 0.0, 1.0) : 0.0;
        const Vec3& q = c[i + 1].pos;
        p = {p.x + (q.x - p.x) * t, p.y + (q.y - p.y) * t, p.z + (q.z - p.z) * t};
        h += wrap_angle(c[i + 1].heading - c[i].heading) * t;
    }
    out.heading_out = h;
    out.pos = {p.x - std::sin(h) * lateral, p.y + std::cos(h) * lateral, p.z};
    return out;
}

int Junction::find_connector(int arm_from, int lane_from, int arm_to, int lane_to) const {
    for (std::size_t i = 0; i < connectors.size(); ++i) {
        const LaneConnector& c = connectors[i];
        if (c.arm_from == arm_from && c.lane_from == lane_from && c.arm_to == arm_to && c.lane_to == lane_to)
            return static_cast<int>(i);
    }
    return -1;
}

const Junction* JunctionSet::find(NodeId n) const {
    const auto it = junctions.find(n);
    return it == junctions.end() ? nullptr : &it->second;
}

namespace {

JunctionArm make_arm(const RoadGraph& graph, const Edge& e, bool at_a, const RegionPack& region) {
    JunctionArm arm;
    arm.edge = e.id;
    arm.at_a = at_a;
    const double length = e.horizontal.length();
    arm.edge_length = length;

    // Centreline from the node outward.
    for (const EdgeSample& s : sample_centreline(e)) {
        EdgeSample o = s;
        if (!at_a) {
            o.s = length - s.s;
            o.heading = wrap_angle(s.heading + kPi);
        }
        arm.centreline.push_back(o);
    }
    if (!at_a) std::reverse(arm.centreline.begin(), arm.centreline.end());
    if (!arm.centreline.empty()) arm.heading_out = arm.centreline.front().heading;

    const RoadType* type = region.road_type(e.road_type);
    if (!type) return arm;
    arm.road_class = type->road_class > 0 ? type->road_class : static_cast<int>(std::lround(type->design_speed * 3.6));
    arm.corner_radius = type->corner_radius;

    std::vector<Lane> all = instantiate_lanes(e, *type, region.driving_side);
    double left = 0, right = 0;
    std::vector<std::pair<double, double>> kerbs;  // (lateral, width)
    for (const Lane& l : all) {
        const double lat = at_a ? l.offset : -l.offset;
        const LaneType* lt = region.lane_type(l.lane_type);
        left = std::max(left, lat + l.width / 2);
        right = std::max(right, -(lat - l.width / 2));
        if (l.lane_type == "kerb") kerbs.push_back({lat, l.width});
        if (!lt || !lt->connects_at_junctions || l.dir == TravelDir::None) continue;
        LaneRef r;
        r.edge = e.id;
        r.slot = l.slot;
        r.lateral = lat;
        r.width = l.width;
        r.lane_type = l.lane_type;
        // dir is relative to a -> b; an arm at a looks along a -> b, so Backward travels toward the node.
        r.inbound = at_a ? l.dir == TravelDir::Backward : l.dir == TravelDir::Forward;
        arm.lanes.push_back(r);
    }
    std::stable_sort(arm.lanes.begin(), arm.lanes.end(),
                     [](const LaneRef& x, const LaneRef& y) { return x.lateral > y.lateral; });
    arm.half_width_left = left;
    arm.half_width_right = right;
    arm.kerb_left = left;
    arm.kerb_right = right;
    for (auto [lat, w] : kerbs) {
        if (lat > 0) arm.kerb_left = std::min(arm.kerb_left, lat - w / 2);
        else arm.kerb_right = std::min(arm.kerb_right, -lat - w / 2);
    }
    (void)graph;
    return arm;
}

}  // namespace

std::vector<JunctionArm> collect_arms(const RoadGraph& graph, NodeId node, const RegionPack& region) {
    std::vector<JunctionArm> arms;
    for (EdgeId id : graph.edges_at(node)) {
        const Edge* e = graph.edge(id);
        if (!e) continue;
        if (e->a == node) arms.push_back(make_arm(graph, *e, true, region));
        if (e->b == node) arms.push_back(make_arm(graph, *e, false, region));
    }
    std::stable_sort(arms.begin(), arms.end(), [](const JunctionArm& x, const JunctionArm& y) {
        if (x.heading_out != y.heading_out) return x.heading_out < y.heading_out;
        if (x.edge != y.edge) return x.edge < y.edge;
        return x.at_a && !y.at_a;
    });
    return arms;
}

NodeClass classify_arms(const std::vector<JunctionArm>& arms) {
    const std::size_t n = arms.size();
    if (n <= 1) return NodeClass::End;
    if (n == 2) return NodeClass::Joint;
    if (n > 3) return NodeClass::Junction;
    std::vector<int> in, out;
    for (std::size_t i = 0; i < 3; ++i) {
        const bool flows_in = arms[i].inbound_count() > 0 && arms[i].outbound_count() == 0;
        const bool flows_out = arms[i].outbound_count() > 0 && arms[i].inbound_count() == 0;
        if (flows_in) in.push_back(static_cast<int>(i));
        else if (flows_out) out.push_back(static_cast<int>(i));
        else return NodeClass::Junction;
    }
    const bool merge = in.size() == 2 && out.size() == 1;
    const bool diverge = in.size() == 1 && out.size() == 2;
    if (!merge && !diverge) return NodeClass::Junction;
    const std::vector<int>& pair = merge ? in : out;
    const int odd = merge ? out[0] : in[0];
    const auto sep = [&](int x, int y) {
        return std::fabs(wrap_angle(arms[static_cast<std::size_t>(x)].heading_out -
                                    arms[static_cast<std::size_t>(y)].heading_out));
    };
    if (sep(pair[0], pair[1]) < kMergeMaxAngle && sep(pair[0], odd) >= kMergeMinOppositeAngle &&
        sep(pair[1], odd) >= kMergeMinOppositeAngle)
        return merge ? NodeClass::Merge : NodeClass::Diverge;
    return NodeClass::Junction;
}

NodeClass classify_node(const RoadGraph& graph, NodeId node, const RegionPack& region) {
    return classify_arms(collect_arms(graph, node, region));
}

std::vector<int> major_arm_indices(const std::vector<JunctionArm>& arms) {
    std::vector<int> best;
    const auto width = [](const JunctionArm& a) { return a.half_width_left + a.half_width_right; };
    for (std::size_t i = 0; i < arms.size(); ++i) {
        if (!best.empty()) {
            const JunctionArm& b = arms[static_cast<std::size_t>(best[0])];
            const bool better = arms[i].road_class > b.road_class ||
                                (arms[i].road_class == b.road_class && width(arms[i]) > width(b) + 1e-6);
            const bool tie = arms[i].road_class == b.road_class && std::fabs(width(arms[i]) - width(b)) <= 1e-6;
            if (better) best.clear();
            else if (!tie) continue;
        }
        best.push_back(static_cast<int>(i));
    }
    return best;
}

std::vector<double> resolve_corner_radii(NodeId node, const std::vector<JunctionArm>& arms,
                                         const JunctionOverrides& overrides, const RegionPack* region) {
    std::vector<double> r(arms.size(), kDefaultCornerRadius);
    for (std::size_t k = 0; k < arms.size(); ++k) {
        const std::size_t j = (k + 1) % arms.size();
        // Region default: the larger of the two arms' road-type radii (0 = n/a), else the fallback. With a
        // region the pack's table for the pair of road classes wins over that, scaled by the corner skew.
        double base = std::max(arms[k].corner_radius, arms[j].corner_radius);
        if (region) {
            const double table = region->junction.corner_radius_for(arms[k].road_class, arms[j].road_class);
            if (table > 0) base = table;
            double theta = arms[j].heading_out - arms[k].heading_out;
            if (theta <= 0) theta += 2 * kPi;
            if (base > 0 && theta < kPi - 0.01) base *= region->junction.skew_factor(theta);
        }
        if (base > 0) r[k] = base;
        const CornerKey key{node, arms[j].edge, arms[k].edge};
        const auto it = overrides.corner_radius.find(key);
        if (it != overrides.corner_radius.end()) r[k] = it->second;
    }
    return r;
}

double flare_spread(const JunctionArm& arm, double s, bool left) {
    const ArmFlare& f = arm.flare;
    const double w = left ? f.spread_left : f.spread_right;
    if (!(f.length > 0) || w <= 0 || s >= f.length) return 0;
    if (s <= f.full) return w;
    const double span = f.length - f.full;
    if (span < 1e-9) return 0;
    const double t = (s - f.full) / span;
    return w * (1 - t * t * (3 - 2 * t));
}

double flared_lateral(const JunctionArm& arm, double s, double lateral) {
    if (lateral > 0) return lateral + flare_spread(arm, s, true);
    if (lateral < 0) return lateral - flare_spread(arm, s, false);
    return lateral;
}

namespace {

struct PocketPlan {
    int arm = -1, to_arm = -1;
    bool forced = false, left = true;  // left: the offside (outbound lane) side is the +lateral side
};

// Where a turn pocket goes at this node: a two-way major arm whose offside has a lesser road leaving at
// roughly a right angle, with a two-way through arm opposite. The region rule decides by default; a
// per-junction override forces a pocket on (class and angle limits relaxed) or off.
std::vector<PocketPlan> plan_pockets(const Junction& j, const RegionPack& region, const JunctionOverrides& ov) {
    std::vector<PocketPlan> out;
    const TurnPocketRules& R = region.junction.turn_pockets;
    const int n = static_cast<int>(j.arms.size());
    if (j.cls != NodeClass::Junction || n < 3 || n > 4) return out;
    for (int ai = 0; ai < n; ++ai) {
        const JunctionArm& A = j.arms[static_cast<std::size_t>(ai)];
        if (A.inbound_count() == 0 || A.outbound_count() == 0) continue;
        const auto it = ov.turn_pockets.find({j.node, A.edge});
        if (it != ov.turn_pockets.end() && !it->second) continue;
        const bool forced = it != ov.turn_pockets.end();
        if (!forced && (!R.enabled || A.road_class < R.major_class_min)) continue;
        int bi = -1;
        double bend = 1e9;
        for (int c = 0; c < n; ++c) {
            if (c == ai) continue;
            const double d = std::fabs(wrap_angle(j.arms[static_cast<std::size_t>(c)].heading_out - (A.heading_out + kPi)));
            if (d < bend) bend = d, bi = c;
        }
        if (bi < 0 || bend > (forced ? 0.5 : R.max_bend)) continue;
        const JunctionArm& B = j.arms[static_cast<std::size_t>(bi)];
        if (B.inbound_count() == 0 || B.outbound_count() == 0) continue;
        if (!forced && B.road_class < R.major_class_min) continue;
        double lat = 0;
        int cnt = 0;
        for (const LaneRef& l : A.lanes)
            if (!l.inbound) lat += l.lateral, ++cnt;
        const bool left = cnt > 0 && lat / cnt > 0;
        for (int c = 0; c < n; ++c) {
            if (c == ai || c == bi) continue;
            const JunctionArm& M = j.arms[static_cast<std::size_t>(c)];
            const double phi = wrap_angle(M.heading_out - A.heading_out);
            if ((phi > 0) != left) continue;
            const double lo = forced ? 0.5 : R.min_minor_angle, hi = forced ? kPi - 0.5 : R.max_minor_angle;
            if (std::fabs(phi) < lo || std::fabs(phi) > hi) continue;
            if (M.outbound_count() == 0) continue;
            if (!forced && (M.road_class > R.minor_class_max || M.road_class >= A.road_class)) continue;
            out.push_back({ai, c, forced, left});
            break;
        }
    }
    return out;
}

double area2(const std::vector<Vec2>& r) { return 2 * signed_area(r); }  // exact: halving then doubling a double

// Pocket lane and hatched ghost island polygons, from the finished flare of the pocket's arm.
void build_pocket_polygons(const Junction& j, TurnPocket& p) {
    const JunctionArm& A = j.arms[static_cast<std::size_t>(p.arm)];
    const bool left = A.flare.spread_left > A.flare.spread_right;
    const double sign = left ? 1.0 : -1.0;
    const double w = p.lane_width;
    const auto at = [&](double s, double lat) { return arm_point(A, s, lat).pos; };
    const double s0 = p.storage_length, s1 = A.flare.length;
    // Pocket lane: the strip [0, w] beside the centre line from the node to the end of the full spread.
    std::vector<Vec2> lane, ghost;
    for (double s = 0; s < s0; s += 1.0) lane.push_back(at(s, 0).xy());
    lane.push_back(at(s0, 0).xy());
    lane.push_back(at(s0, sign * w).xy());
    for (double s = s0 - 1.0; s > 0; s -= 1.0) lane.push_back(at(s, sign * w).xy());
    lane.push_back(at(0, sign * w).xy());
    // Ghost island: from the end of the lane to the nose; the outer edge follows the shifted lane edge.
    for (double s = s0; s < s1; s += 1.0) ghost.push_back(at(s, 0).xy());
    ghost.push_back(at(s1, 0).xy());
    for (double s = s1 - 1.0; s > s0; s -= 1.0) ghost.push_back(at(s, sign * flare_spread(A, s, left)).xy());
    ghost.push_back(at(s0, sign * w).xy());
    p.centreline.clear();
    for (double s = s0; s > 0; s -= 1.0) p.centreline.push_back(at(s, sign * w / 2));
    p.centreline.push_back(at(0, sign * w / 2));
    for (std::vector<Vec2>* poly : {&lane, &ghost}) {
        std::vector<Vec2> clean;
        for (const Vec2& v : *poly)
            if (clean.empty() || (clean.back() - v).length() > 1e-6) clean.push_back(v);
        while (clean.size() > 1 && (clean.front() - clean.back()).length() <= 1e-6) clean.pop_back();
        if (clean.size() < 3 || std::fabs(area2(clean)) < 1e-3) continue;
        if (area2(clean) < 0) std::reverse(clean.begin(), clean.end());
        (poly == &lane ? p.lane : p.ghost).push_back(std::move(clean));
    }
}

}  // namespace

JunctionSet build_plain_junctions(const RoadGraph& graph, const RegionPack& region, const JunctionOverrides& overrides,
                                  const std::vector<NodeId>& only) {
    JunctionSet set;
    std::vector<NodeId> nodes = only;
    if (nodes.empty())
        for (const auto& [id, n] : graph.nodes()) nodes.push_back(id);
    for (NodeId id : nodes) {
        const Node* n = graph.node(id);
        if (!n) continue;
        Junction j;
        j.node = id;
        j.pos = n->pos;
        j.side = region.driving_side;
        j.arms = collect_arms(graph, id, region);
        j.cls = classify_arms(j.arms);
        if (j.arms.size() < 3) continue;
        j.major_arms = major_arm_indices(j.arms);
        j.corner_radii = resolve_corner_radii(id, j.arms, overrides, &region);
        if (j.cls == NodeClass::Merge || j.cls == NodeClass::Diverge) {
            generate_merge_diverge(j, graph, region, overrides);
        } else {
            // ---- approach shaping: roundabout flares (requested through the overrides) and turn pockets
            std::map<std::size_t, ArmShape> shapes;  // arm index -> wanted shape
            for (std::size_t a = 0; a < j.arms.size(); ++a) {
                const auto it = overrides.arm_shapes.find({id, j.arms[a].edge, j.arms[a].at_a});
                if (it != overrides.arm_shapes.end()) shapes[a] = it->second;
            }
            if (region.junction.turn_pockets.enabled || !overrides.turn_pockets.empty()) {
                const TurnPocketRules& R = region.junction.turn_pockets;
                for (const PocketPlan& pp : plan_pockets(j, region, overrides)) {
                    if (shapes.count(static_cast<std::size_t>(pp.arm))) continue;  // already shaped (roundabout)
                    const Edge* e = graph.edge(j.arms[static_cast<std::size_t>(pp.arm)].edge);
                    const RoadType* t = e ? region.road_type(e->road_type) : nullptr;
                    const double speed = t ? t->speed_limit : 0;
                    ArmShape sh;
                    (pp.left ? sh.spread_left : sh.spread_right) = R.lane_width;
                    sh.hold = R.lookup(R.storage, speed, 20.0);
                    sh.taper = R.lookup(R.taper_ratio, speed, 15.0) * R.lane_width;
                    sh.min_taper = R.min_taper_ratio * R.lane_width;
                    sh.what = "turn pocket";
                    shapes[static_cast<std::size_t>(pp.arm)] = sh;
                    TurnPocket tp;
                    tp.arm = pp.arm;
                    tp.to_arm = pp.to_arm;
                    tp.forced = pp.forced;
                    tp.lane_width = R.lane_width;
                    j.pockets.push_back(tp);
                }
            }
            for (const auto& [a, sh] : shapes) {
                j.arms[a].flare.spread_left = sh.spread_left;
                j.arms[a].flare.spread_right = sh.spread_right;
            }
            j.trims = compute_trims(j.arms, j.corner_radii);
            std::string dropped;
            for (const auto& [a, sh] : shapes) {
                JunctionArm& arm = j.arms[a];
                const double full = std::max(sh.hold, j.trims[a] + 3.0);
                // Half the edge (the far end may be a junction with its own trim); all of it but 2 m when the far end has none.
                double cap = arm.edge_length / 2;
                if (const Edge* ge = graph.edge(arm.edge)) {
                    const NodeId far = arm.at_a ? ge->b : ge->a;
                    if (far != id && graph.edges_at(far).size() < 3) cap = arm.edge_length - 2.0;
                }
                const double avail = cap - full;
                const double wanted = sh.length > 0 ? std::max(sh.length - full, sh.min_taper) : sh.taper;
                if (avail < sh.min_taper - 1e-9 || avail < 1.0) {
                    arm.flare = ArmFlare{};
                    if (dropped.empty()) dropped = sh.what + " on edge " + std::to_string(arm.edge.value) + " does not fit";
                    j.pockets.erase(std::remove_if(j.pockets.begin(), j.pockets.end(),
                                                   [&](const TurnPocket& p) { return p.arm == static_cast<int>(a); }),
                                    j.pockets.end());
                } else {
                    arm.flare.full = full;
                    arm.flare.length = full + std::min(wanted, avail);
                }
            }
            if (!dropped.empty()) j.trims = compute_trims(j.arms, j.corner_radii);
            for (const auto& [a, sh] : shapes)
                if (j.arms[a].flare.active()) j.trims[a] = j.arms[a].flare.length;
            for (TurnPocket& p : j.pockets) {
                const ArmFlare& f = j.arms[static_cast<std::size_t>(p.arm)].flare;
                p.storage_length = f.full;
                p.taper_length = f.length - f.full;
            }
            generate_connectors(j, j.side, overrides, region);
            generate_geometry(j, graph, region, overrides);
            for (TurnPocket& p : j.pockets) build_pocket_polygons(j, p);
            if (!dropped.empty() && !j.degraded) {
                j.degraded = true;
                j.degraded_reason = dropped;
            }
        }
        set.junctions.emplace(id, std::move(j));
    }
    return set;
}

JunctionSet build_junctions(const RoadGraph& graph, const RegionPack& region, const JunctionOverrides& overrides,
                            const std::vector<NodeId>& only) {
    JunctionSet set = build_plain_junctions(graph, region, overrides, only);
    apply_roundabouts(set, graph, region, overrides, only);  // marks roundabout ring nodes (cheap when there are none)
    return set;
}

}  // namespace cp
