// Junction orchestration and the small shared pieces: arm collection, classification,
// major-arm derivation, override resolution. See docs/junctions.md.
#include "cp/junction.h"

#include <algorithm>
#include <cmath>

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
        left = std::max(left, lat + l.width / 2);
        right = std::max(right, -(lat - l.width / 2));
        if (l.lane_type == "kerb") kerbs.push_back({lat, l.width});
        const LaneType* lt = region.lane_type(l.lane_type);
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
                                         const JunctionOverrides& overrides) {
    std::vector<double> r(arms.size(), kDefaultCornerRadius);
    for (std::size_t k = 0; k < arms.size(); ++k) {
        // Region default: the larger of the two arms' road-type radii (0 = n/a), else the fallback.
        const double pack = std::max(arms[k].corner_radius, arms[(k + 1) % arms.size()].corner_radius);
        if (pack > 0) r[k] = pack;
        const CornerKey key{node, arms[(k + 1) % arms.size()].edge, arms[k].edge};
        const auto it = overrides.corner_radius.find(key);
        if (it != overrides.corner_radius.end()) r[k] = it->second;
    }
    return r;
}

JunctionSet build_junctions(const RoadGraph& graph, const RegionPack& region, const JunctionOverrides& overrides,
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
        j.corner_radii = resolve_corner_radii(id, j.arms, overrides);
        if (j.cls == NodeClass::Merge || j.cls == NodeClass::Diverge) {
            generate_merge_diverge(j, graph, region, overrides);
        } else {
            j.trims = compute_trims(j.arms, j.corner_radii);
            generate_connectors(j, j.side, overrides);
            generate_geometry(j, graph, region, overrides);
        }
        set.junctions.emplace(id, std::move(j));
    }
    return set;
}

}  // namespace cp
