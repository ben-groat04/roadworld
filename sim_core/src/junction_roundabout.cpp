// Roundabouts: group finding by explicit road type, the place preset, per-group layout (island,
// splitters, give-way) and the post-pass over the JunctionSet. See docs/junctions.md section 7.
#include "cp/junction_roundabout.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <set>

#include "cp/edge_sampling.h"
#include "cp/geom.h"

#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4702)  // unreachable code inside the vendored Clipper2 header
#endif
#include "clipper2/clipper.h"
#if defined(_MSC_VER)
#pragma warning(pop)
#endif

namespace cp {

const RoundaboutRules& roundabout_params(const RegionPack& region) { return region.roundabout; }

std::string default_ring_road_type(const RegionPack& region) {
    for (const auto& [id, t] : region.road_types)
        if (t.roundabout_ring) return id;
    return {};
}

namespace {

constexpr double kIslandShape = 0.6;  // splitter island plan: half width ~ ((s_nose - s) / length)^kIslandShape (teardrop; 1 = straight triangle)

using Ring = std::vector<Vec2>;

constexpr double kSampleStep = 0.25;  // m, scan step of the tool along crossing edges

// ---------------------------------------------------------------- small geometry helpers

Clipper2Lib::PathD to_path(const Ring& r) {
    Clipper2Lib::PathD p;
    p.reserve(r.size());
    for (Vec2 v : r) p.emplace_back(v.x, v.y);
    return p;
}

PolygonSet from_paths(const Clipper2Lib::PathsD& ps) {
    PolygonSet out;
    for (const auto& p : ps) {
        if (p.size() < 3) continue;
        Ring r;
        r.reserve(p.size());
        for (const auto& pt : p) r.push_back({pt.x, pt.y});
        if (std::fabs(signed_area(r)) < 1e-6) continue;
        out.push_back(std::move(r));
    }
    return out;
}

// Arc length of each interior control point (spans start at the points).
std::vector<double> interior_s(const Edge& e) {
    std::vector<double> s;
    double acc = 0;
    for (std::size_t i = 0; i + 1 < e.horizontal.spans.size(); ++i) {
        acc += e.horizontal.spans[i].length;
        s.push_back(acc);
    }
    return s;
}

// Kerb-line extent of a one-way type: distances (m) from the centreline to the kerb face on the
// left and right of a -> b.
struct KerbExtent {
    bool ok = false;
    double left = 0, right = 0;
};

KerbExtent kerb_extent(const RegionPack& region, const RoadType& t) {
    KerbExtent k;
    Edge e;
    e.one_way = true;
    const std::vector<Lane> lanes = instantiate_lanes(e, t, region.driving_side);
    double left = 0, right = 0;
    bool drivable = false;
    for (const Lane& l : lanes) {
        left = std::max(left, l.offset + l.width / 2);
        right = std::max(right, -(l.offset - l.width / 2));
        const LaneType* lt = region.lane_type(l.lane_type);
        if (lt && lt->connects_at_junctions && l.dir != TravelDir::None) drivable = true;
    }
    k.left = left;
    k.right = right;
    for (const Lane& l : lanes) {
        const LaneType* klt = region.lane_type(l.lane_type);
        if (!klt || klt->profile != "kerb") continue;  // by profile, not by the lane type id
        if (l.offset > 0) k.left = std::min(k.left, l.offset - l.width / 2);
        else k.right = std::min(k.right, -l.offset - l.width / 2);
    }
    k.ok = drivable;
    return k;
}

bool is_clockwise_for(const RegionPack& region) { return region.driving_side == DrivingSide::Left; }

bool ring_flagged(const RegionPack& region, const Edge& e) {
    const RoadType* t = region.road_type(e.road_type);
    return t && t->roundabout_ring;
}

// ---------------------------------------------------------------- the place preset

class PlaceRoundabout : public RoadCommand {
public:
    PlaceRoundabout(Vec3 centre, double radius, std::string type, const RegionPack& region)
        : centre_(centre), radius_(radius), type_(std::move(type)), clockwise_(is_clockwise_for(region)),
          prm_(region.roundabout) {
        const RoadType* t = region.road_type(type_);
        if (!t) type_error_ = "unknown road type '" + type_ + "'";
        else if (!t->roundabout_ring) type_error_ = "road type '" + type_ + "' is not a roundabout ring type";
        else kerb_ = kerb_extent(region, *t);
        for (const auto& [id, rt] : region.road_types)
            if (rt.roundabout_ring) ring_types_.insert(id);
    }

    bool apply(RoadGraph& g) override {
        if (!planned_) {
            if (!plan(g).empty()) return false;
            planned_ = true;
        }
        if (execute(g)) return true;
        planned_ = false;
        return false;
    }

    void revert(RoadGraph& g) override { undo(g); }

    std::string name() const override { return "place roundabout"; }

    // Empty string: the plan is valid for `g`. Reads the graph only.
    std::string plan(const RoadGraph& g) {
        removed_edges_.clear();
        removed_nodes_.clear();
        added_nodes_.clear();
        stubs_.clear();
        if (!type_error_.empty()) return type_error_;
        if (!std::isfinite(centre_.x) || !std::isfinite(centre_.y) || !std::isfinite(centre_.z))
            return "centre is not finite";
        if (!std::isfinite(radius_)) return "radius is not finite";
        if (radius_ < prm_.min_tool_radius) return "radius " + fmt(radius_) + " m is below the minimum of " + fmt(prm_.min_tool_radius) + " m";
        if (radius_ > prm_.max_tool_radius) return "radius " + fmt(radius_) + " m is above the maximum of " + fmt(prm_.max_tool_radius) + " m";
        if (!kerb_.ok) return "ring road type has no driving lanes";
        const double in_ext = clockwise_ ? kerb_.right : kerb_.left;  // distance to the island-side kerb
        if (radius_ - in_ext < prm_.min_island_radius)
            return "radius " + fmt(radius_) + " m leaves an island of " + fmt(radius_ - in_ext) + " m, below the minimum of " +
                   fmt(prm_.min_island_radius) + " m";

        const Vec2 c = centre_.xy();
        std::vector<double> cut_u;  // circulation parameter of each crossing, parallel to the cut nodes
        std::vector<Node> cut_nodes;

        for (const auto& [id, e] : g.edges()) {
            const double L = e.horizontal.length();
            const auto dist = [&](double s) { return (edge_point(e, s).xy() - c).length() - radius_; };
            // Cheap reject: the whole edge is farther from the centre than the circle plus its sagitta slack.
            const Vec2 pa = edge_point(e, 0).xy(), pb = edge_point(e, L).xy();
            const double nearest = ((pa + pb) * 0.5 - c).length();
            if (nearest - radius_ > 1.5 * L + 1.0) continue;

            std::vector<Interval> ivs;
            bool inside = dist(0) < 0;
            bool touched = inside;
            bool structure_inside = false;
            double start = 0;
            bool start_cut = false;
            double prev_s = 0;
            const int steps = std::max(1, static_cast<int>(std::ceil(L / kSampleStep)));
            const auto in_structure = [&](double s) {
                for (const StructureSpan& sp : e.structure)
                    if (sp.kind != StructureKind::Ground && s >= sp.s0 && s <= sp.s1) return true;
                return false;
            };
            if (inside && in_structure(0)) structure_inside = true;
            for (int i = 1; i <= steps; ++i) {
                const double s = std::min(L, i * kSampleStep);
                const bool in = dist(s) < 0;
                if (in && in_structure(s)) structure_inside = true;
                if (in != inside) {
                    double lo = prev_s, hi = s;
                    for (int k = 0; k < 50; ++k) {
                        const double mid = (lo + hi) / 2;
                        if ((dist(mid) < 0) == inside) lo = mid;
                        else hi = mid;
                    }
                    const double root = (lo + hi) / 2;
                    touched = true;
                    if (inside) {
                        start = root;
                        start_cut = true;
                    } else {
                        ivs.push_back({start, root, start_cut, true});
                    }
                    inside = in;
                }
                prev_s = s;
            }
            if (!touched) continue;
            if (ring_types_.count(e.road_type)) return "the circle overlaps an existing roundabout ring (edge " + std::to_string(id.value) + ")";
            if (structure_inside) return "a bridge or tunnel (edge " + std::to_string(id.value) + ") passes through the circle";
            if (!inside) ivs.push_back({start, L, start_cut, false});
            if (ivs.empty()) return "road " + std::to_string(id.value) + " lies wholly inside the circle (it would be a road in the island)";
            removed_edges_.push_back(e);

            const std::vector<double> is = interior_s(e);
            for (const Interval& iv : ivs) {
                if (iv.hi - iv.lo < prm_.min_edge_length)
                    return "road " + std::to_string(id.value) + " would leave a stub of " + fmt(iv.hi - iv.lo) +
                           " m, below the minimum of " + fmt(prm_.min_edge_length) + " m";
                Stub st;
                st.e = e;
                st.e.id = {};
                st.e.interior.clear();
                st.e.structure.clear();
                for (std::size_t k = 0; k < is.size() && k < e.interior.size(); ++k)
                    if (is[k] > iv.lo + 0.5 && is[k] < iv.hi - 0.5) st.e.interior.push_back(e.interior[k]);
                for (const StructureSpan& sp : e.structure) {
                    const double s0 = std::max(sp.s0, iv.lo), s1 = std::min(sp.s1, iv.hi);
                    if (s1 > s0) st.e.structure.push_back({s0 - iv.lo, s1 - iv.lo, sp.kind});
                }
                st.e.heading_a = iv.lo_cut ? std::optional<double>(edge_heading(e, iv.lo)) : e.heading_a;
                st.e.heading_b = iv.hi_cut ? std::optional<double>(edge_heading(e, iv.hi)) : e.heading_b;
                for (int end = 0; end < 2; ++end) {
                    const bool cut = end == 0 ? iv.lo_cut : iv.hi_cut;
                    if (!cut) continue;
                    const Vec2 p = edge_point(e, end == 0 ? iv.lo : iv.hi).xy();
                    const double geo = std::atan2(p.y - c.y, p.x - c.x);
                    cut_u.push_back(wrap_2pi(clockwise_ ? -geo : geo));
                    cut_nodes.push_back(Node{{}, ring_point(cut_u.back())});
                    (end == 0 ? st.na : st.nb) = static_cast<int>(cut_nodes.size()) - 1;
                }
                stubs_.push_back(std::move(st));
            }
        }
        added_nodes_ = cut_nodes;
        for (const auto& [nid, n] : g.nodes())
            if ((n.pos.xy() - c).length() < radius_) removed_nodes_.push_back(n);

        // ---- the ring: crossings sorted along the circulation, then filler nodes
        struct RN {
            double u;
            int node;
        };
        std::vector<RN> rn;
        for (std::size_t k = 0; k < cut_u.size(); ++k) rn.push_back({cut_u[k], static_cast<int>(k)});
        if (rn.empty())
            for (int k = 0; k < 4; ++k) {
                added_nodes_.push_back(Node{{}, ring_point(k * kPi / 2)});
                rn.push_back({k * kPi / 2, static_cast<int>(added_nodes_.size()) - 1});
            }
        std::stable_sort(rn.begin(), rn.end(), [](const RN& a, const RN& b) { return a.u < b.u; });
        const std::size_t m = rn.size();
        std::vector<double> arc(m);
        std::vector<int> sub(m, 1);
        int total = 0;
        for (std::size_t i = 0; i < m; ++i) {
            arc[i] = m == 1 ? 2 * kPi : wrap_2pi(rn[(i + 1) % m].u - rn[i].u);
            if (radius_ * arc[i] < prm_.min_edge_length)
                return "two roads meet the circle only " + fmt(radius_ * arc[i]) + " m apart, below the minimum ring edge of " +
                       fmt(prm_.min_edge_length) + " m";
            sub[i] = std::max(1, static_cast<int>(std::ceil(arc[i] / prm_.max_arc - 1e-9)));
            total += sub[i];
        }
        while (total < 4) {
            std::size_t best = 0;
            for (std::size_t i = 1; i < m; ++i)
                if (arc[i] / sub[i] > arc[best] / sub[best]) best = i;
            ++sub[best];
            ++total;
        }
        if (total > prm_.max_ring_edges)
            return "the ring would need " + std::to_string(total) + " edges, above the maximum of " + std::to_string(prm_.max_ring_edges);
        for (std::size_t i = 0; i < m; ++i)
            if (radius_ * arc[i] / sub[i] < prm_.min_edge_length) return "a ring edge would be shorter than the minimum of " + fmt(prm_.min_edge_length) + " m";

        struct SeqNode {
            double u;
            int node;
        };
        std::vector<SeqNode> seq;
        for (std::size_t i = 0; i < m; ++i) {
            seq.push_back({rn[i].u, rn[i].node});
            for (int f = 1; f < sub[i]; ++f) {
                const double u = rn[i].u + arc[i] * f / sub[i];
                added_nodes_.push_back(Node{{}, ring_point(u)});
                seq.push_back({u, static_cast<int>(added_nodes_.size()) - 1});
            }
        }
        for (std::size_t k = 0; k < seq.size(); ++k) {
            const SeqNode& a = seq[k];
            const SeqNode& b = seq[(k + 1) % seq.size()];
            double d = b.u - a.u;
            while (d <= 1e-9) d += 2 * kPi;
            Stub st;
            st.e.road_type = type_;
            st.e.one_way = true;
            st.e.interior = {ring_point(a.u + d / 3), ring_point(a.u + 2 * d / 3)};
            st.e.heading_a = ring_heading(a.u);
            st.e.heading_b = ring_heading(a.u + d);
            st.na = a.node;
            st.nb = b.node;
            stubs_.push_back(std::move(st));
        }
        return {};
    }

private:
    struct Stub {
        Edge e;
        int na = -1, nb = -1;  // index into added_nodes_ when that end is a new ring node, else the edge's own end
    };
    struct Interval {
        double lo, hi;
        bool lo_cut, hi_cut;
    };

    static std::string fmt(double v) {
        char b[32];
        std::snprintf(b, sizeof b, "%.1f", v);
        return b;
    }

    // Circulation parameter u (increasing along the circulation) -> geometric angle and back.
    double angle_of(double u) const { return clockwise_ ? -u : u; }
    Vec3 ring_point(double u) const {
        const double a = angle_of(u);
        return {centre_.x + radius_ * std::cos(a), centre_.y + radius_ * std::sin(a), centre_.z};
    }
    double ring_heading(double u) const {
        const double a = angle_of(u);
        return clockwise_ ? a - kPi / 2 : a + kPi / 2;
    }
    static double wrap_2pi(double a) {
        a = std::fmod(a, 2 * kPi);
        return a < 0 ? a + 2 * kPi : a;
    }

    bool execute(RoadGraph& g) {
        for (const Edge& e : removed_edges_) g.erase_edge(e.id);
        for (const Node& n : removed_nodes_) g.erase_node(n.id);
        for (Node& n : added_nodes_) n.id = g.insert_node(n);
        bool ok = true;
        for (Stub& st : stubs_) {
            Edge e = st.e;
            if (st.na >= 0) e.a = added_nodes_[static_cast<std::size_t>(st.na)].id;
            if (st.nb >= 0) e.b = added_nodes_[static_cast<std::size_t>(st.nb)].id;
            st.e.id = g.insert_edge(e);
            if (!st.e.id) {
                ok = false;
                break;
            }
        }
        if (ok) return true;
        undo(g);
        for (Node& n : added_nodes_) n.id = {};
        for (Stub& st : stubs_) st.e.id = {};
        return false;
    }

    void undo(RoadGraph& g) {
        for (auto it = stubs_.rbegin(); it != stubs_.rend(); ++it)
            if (it->e.id) g.erase_edge(it->e.id);
        for (auto it = added_nodes_.rbegin(); it != added_nodes_.rend(); ++it)
            if (it->id && g.node(it->id)) g.erase_node(it->id);
        for (const Node& n : removed_nodes_) g.insert_node(n);
        for (const Edge& e : removed_edges_) g.insert_edge(e);
    }

    Vec3 centre_;
    double radius_;
    std::string type_;
    bool clockwise_;
    RoundaboutRules prm_;
    KerbExtent kerb_;
    std::string type_error_;
    std::set<std::string> ring_types_;

    bool planned_ = false;
    std::vector<Edge> removed_edges_;
    std::vector<Node> removed_nodes_;
    std::vector<Node> added_nodes_;  // ids filled on first execute and kept for redo
    std::vector<Stub> stubs_;        // edges to insert (ids likewise)
};

// ---------------------------------------------------------------- group finding

// Ring order and geometry of a clean component; returns an error text on failure.
std::string finish_group(const RoadGraph& graph, const RegionPack& region, const std::vector<NodeId>& node_order,
                         const std::vector<EdgeId>& edge_order, JunctionGroup& group) {
    std::vector<Vec2> pts;
    for (EdgeId id : edge_order) {
        const Edge* e = graph.edge(id);
        const double L = e->horizontal.length();
        const int k = std::max(4, static_cast<int>(std::ceil(L)));
        for (int i = 0; i < k; ++i) pts.push_back(edge_point(*e, L * i / k).xy());
    }
    Vec2 centre{};
    for (Vec2 p : pts) centre = centre + p;
    centre = centre * (1.0 / static_cast<double>(pts.size()));
    double sum = 0;
    for (Vec2 p : pts) sum += (p - centre).length();
    const double area = signed_area(pts);
    const bool clockwise = area < 0;
    if (std::fabs(area) < 1e-6) return "the ring encloses no area";
    if (clockwise != is_clockwise_for(region)) return "the ring circulates against the driving side";
    group = JunctionGroup{};
    group.nodes = node_order;
    group.ring_edges = edge_order;
    group.centre = centre;
    group.radius = sum / static_cast<double>(pts.size());
    group.clockwise = clockwise;
    const std::set<EdgeId> ring(edge_order.begin(), edge_order.end());
    for (std::size_t i = 0; i < node_order.size(); ++i) {
        std::vector<RoundaboutArm> here;
        for (const JunctionArm& a : collect_arms(graph, node_order[i], region)) {
            if (ring.count(a.edge)) continue;
            RoundaboutArm arm;
            arm.ring_index = static_cast<int>(i);
            arm.edge = a.edge;
            arm.entry = a.inbound_count() > 0;
            arm.exit = a.outbound_count() > 0;
            here.push_back(arm);
        }
        std::stable_sort(here.begin(), here.end(),
                         [](const RoundaboutArm& x, const RoundaboutArm& y) { return x.edge < y.edge; });
        group.arms.insert(group.arms.end(), here.begin(), here.end());
    }
    return {};
}

// ---------------------------------------------------------------- layout helpers

RoundaboutLayout fail(RoundaboutLayout l, std::string why) {
    l.ok = false;
    l.error = std::move(why);
    return l;
}

Ring ring_polyline(const JunctionGroup& group, const RoadGraph& graph) {
    Ring r;
    for (EdgeId id : group.ring_edges) {
        const Edge* e = graph.edge(id);
        if (!e) continue;
        const std::vector<EdgeSample> s = sample_centreline(*e);
        for (std::size_t i = 0; i + 1 < s.size(); ++i) r.push_back(s[i].pos.xy());
    }
    if (signed_area(r) < 0) std::reverse(r.begin(), r.end());  // Clipper offsets expect CCW
    return r;
}

// Largest-area ring of the offset of a CCW polygon (empty if it vanishes).
Ring inflate_ring(const Ring& ccw, double delta) {
    const Clipper2Lib::PathsD res = Clipper2Lib::InflatePaths(Clipper2Lib::PathsD{to_path(ccw)}, delta,
                                                              Clipper2Lib::JoinType::Miter,
                                                              Clipper2Lib::EndType::Polygon, 2.0, 3);
    Ring best;
    double best_area = 0;
    for (const Ring& r : from_paths(res))
        if (signed_area(r) > best_area) {
            best_area = signed_area(r);
            best = r;
        }
    return best;
}

const JunctionArm* find_arm(const Junction& j, EdgeId edge, bool at_a, int* index) {
    for (std::size_t k = 0; k < j.arms.size(); ++k)
        if (j.arms[k].edge == edge && j.arms[k].at_a == at_a) {
            if (index) *index = static_cast<int>(k);
            return &j.arms[k];
        }
    return nullptr;
}

// A Joint-class Junction for a ring node with only the two ring arms: the through connectors
// (trim 0, so they are just the lane continuations) and nothing else.
Junction make_joint_junction(const RoadGraph& graph, const RegionPack& region, const JunctionOverrides& overrides,
                             NodeId node) {
    Junction j;
    j.node = node;
    j.pos = graph.node(node) ? graph.node(node)->pos : Vec3{};
    j.side = region.driving_side;
    j.cls = NodeClass::Joint;
    j.arms = collect_arms(graph, node, region);
    j.major_arms = major_arm_indices(j.arms);
    j.corner_radii = resolve_corner_radii(node, j.arms, overrides);
    j.trims.assign(j.arms.size(), 0.0);
    generate_connectors(j, region.driving_side, overrides, region);
    return j;
}

}  // namespace

// ---------------------------------------------------------------- public: tool

std::string check_place_roundabout(const RoadGraph& graph, Vec3 centre, double radius, const std::string& ring_road_type,
                                   const RegionPack& region) {
    PlaceRoundabout p(centre, radius, ring_road_type, region);
    return p.plan(graph);
}

RoadCommandPtr make_place_roundabout(Vec3 centre, double radius, const std::string& ring_road_type,
                                     const RegionPack& region) {
    return std::make_unique<PlaceRoundabout>(centre, radius, ring_road_type, region);
}

// ---------------------------------------------------------------- public: group finding

RoundaboutScan find_roundabouts(const RoadGraph& graph, const RegionPack& region, const std::vector<NodeId>& only) {
    RoundaboutScan out;
    bool any_type = false;
    for (const auto& [id, t] : region.road_types) any_type = any_type || t.roundabout_ring;
    if (!any_type) return out;

    std::vector<EdgeId> seeds;
    if (only.empty()) {
        for (const auto& [id, e] : graph.edges())
            if (ring_flagged(region, e)) seeds.push_back(id);
    } else {
        for (NodeId n : only)
            for (EdgeId id : graph.edges_at(n)) {
                const Edge* e = graph.edge(id);
                if (e && ring_flagged(region, *e)) seeds.push_back(id);
            }
    }

    std::set<EdgeId> seen;
    for (EdgeId seed : seeds) {
        if (seen.count(seed)) continue;
        // Connected component of flagged edges (undirected).
        std::set<EdgeId> edges;
        std::set<NodeId> nodes;
        std::vector<EdgeId> stack{seed};
        edges.insert(seed);
        while (!stack.empty()) {
            const Edge* e = graph.edge(stack.back());
            stack.pop_back();
            for (NodeId n : {e->a, e->b}) {
                if (!nodes.insert(n).second) continue;
                for (EdgeId id : graph.edges_at(n)) {
                    const Edge* o = graph.edge(id);
                    if (o && ring_flagged(region, *o) && edges.insert(id).second) stack.push_back(id);
                }
            }
        }
        seen.insert(edges.begin(), edges.end());

        RoundaboutProblem prob;
        prob.nodes.assign(nodes.begin(), nodes.end());
        prob.edges.assign(edges.begin(), edges.end());
        std::map<NodeId, int> in, outc;
        std::map<NodeId, EdgeId> out_edge;
        std::string why;
        for (EdgeId id : edges) {
            const Edge* e = graph.edge(id);
            if (!e->one_way) why = "ring edge " + std::to_string(id.value) + " is two-way";
            else if (e->a == e->b) why = "ring edge " + std::to_string(id.value) + " is a loop on one node";
            ++outc[e->a];
            ++in[e->b];
            out_edge[e->a] = id;
        }
        if (why.empty()) {
            for (NodeId n : nodes) {
                if (in[n] == 1 && outc[n] == 1) continue;
                if (in[n] + outc[n] == 1)
                    why = "the ring is not closed: its edges stop at node " + std::to_string(n.value) +
                          " (a road of another type continues there)";
                else
                    why = "node " + std::to_string(n.value) + " has " + std::to_string(in[n]) + " ring edge(s) in and " +
                          std::to_string(outc[n]) + " out; a ring needs exactly one of each";
                break;
            }
        }
        if (why.empty() && nodes.size() < 3) why = "a ring needs at least 3 nodes";
        std::vector<NodeId> node_order;
        std::vector<EdgeId> edge_order;
        if (why.empty()) {
            const NodeId start = *nodes.begin();
            NodeId cur = start;
            do {
                node_order.push_back(cur);
                const EdgeId id = out_edge[cur];
                edge_order.push_back(id);
                cur = graph.edge(id)->b;
            } while (cur != start && node_order.size() <= nodes.size());
            if (cur != start || node_order.size() != nodes.size()) why = "the ring edges do not form one loop";
        }
        JunctionGroup group;
        if (why.empty()) why = finish_group(graph, region, node_order, edge_order, group);
        if (why.empty()) out.groups.push_back(std::move(group));
        else {
            prob.what = why;
            out.problems.push_back(std::move(prob));
        }
    }
    std::sort(out.groups.begin(), out.groups.end(),
              [](const JunctionGroup& a, const JunctionGroup& b) { return a.nodes[0] < b.nodes[0]; });
    return out;
}

// ---------------------------------------------------------------- public: layout

RoundaboutLayout build_roundabout(const JunctionGroup& group, const RoadGraph& graph, const RegionPack& region,
                                  const JunctionOverrides& overrides) {
    using namespace Clipper2Lib;
    RoundaboutLayout lay;
    lay.group = group;
    const RoundaboutRules& prm = roundabout_params(region);
    const std::size_t n = group.nodes.size();
    if (n < 3 || group.ring_edges.size() != n) return fail(lay, "ring is not a closed loop");
    const std::set<EdgeId> ring_set(group.ring_edges.begin(), group.ring_edges.end());
    const auto ring_in = [&](std::size_t i) { return group.ring_edges[(i + n - 1) % n]; };
    const auto ring_out = [&](std::size_t i) { return group.ring_edges[i]; };

    // ---- ring size class, ring extents and forced fillet radii at the ring nodes (the player's own corner overrides win)
    const RoundaboutSizeClass cls = prm.size_class(group.radius);
    double in_ext = 0, out_ext = 0;
    bool bad_type = false;
    for (EdgeId id : group.ring_edges) {
        const Edge* e = graph.edge(id);
        const RoadType* t = e ? region.road_type(e->road_type) : nullptr;
        if (!t) {
            bad_type = true;
            break;
        }
        const KerbExtent k = kerb_extent(region, *t);
        in_ext = std::max(in_ext, group.clockwise ? k.right : k.left);
        out_ext = std::max(out_ext, group.clockwise ? k.left : k.right);
    }
    JunctionOverrides ov = overrides;
    for (std::size_t i = 0; i < n; ++i) {
        const std::vector<JunctionArm> arms = collect_arms(graph, group.nodes[i], region);
        if (arms.size() < 3) continue;
        for (std::size_t k = 0; k < arms.size(); ++k) {
            const JunctionArm& r = arms[k];
            const JunctionArm& l = arms[(k + 1) % arms.size()];
            const CornerKey key{group.nodes[i], l.edge, r.edge};
            if (ov.corner_radius.count(key)) continue;
            const bool r_ring = ring_set.count(r.edge) > 0, l_ring = ring_set.count(l.edge) > 0;
            double radius = -1;
            if (r_ring != l_ring) {
                const EdgeId rng = r_ring ? r.edge : l.edge;
                radius = rng == ring_out(i) ? cls.entry_corner_radius : cls.exit_corner_radius;
            } else if (r_ring && l_ring && r.edge != l.edge) {
                radius = prm.ring_corner_radius_factor * group.radius;
            }
            if (radius >= 0) ov.corner_radius[key] = radius;
        }
    }
    // Two-way approaches get a splitter island: both kerbs spread out by half its width (the flare) so no lane
    // gets narrower, falling to nothing at the nose (docs/junctions.md section 7).
    if (!bad_type && cls.splitter_width > 0 && cls.splitter_length > 0)
        for (const RoundaboutArm& arm : group.arms) {
            if (!(arm.entry && arm.exit)) continue;
            const Edge* e = graph.edge(arm.edge);
            const RoadType* t = e ? region.road_type(e->road_type) : nullptr;
            if (!t || t->dual) continue;
            const NodeId nd = group.nodes[static_cast<std::size_t>(arm.ring_index)];
            ArmShape sh;
            sh.spread_left = sh.spread_right = cls.splitter_width / 2;
            sh.length = out_ext + cls.splitter_length * prm.splitter_speed_factor(t->speed_limit);
            sh.min_taper = std::max(prm.splitter_min_taper, 0.3 * (sh.length - out_ext));
            sh.what = "splitter island";
            ov.arm_shapes[{nd, arm.edge, e->a == nd}] = sh;
        }
    JunctionSet built = build_plain_junctions(graph, region, ov, group.nodes);
    lay.ring_junctions = std::move(built.junctions);

    // ---- give way: entries yield to the circulating (ring-in -> ring-out) connectors
    for (std::size_t i = 0; i < n; ++i) {
        const auto jt = lay.ring_junctions.find(group.nodes[i]);
        if (jt == lay.ring_junctions.end()) continue;
        const Junction& j = jt->second;
        int in_arm = -1, out_arm = -1;
        find_arm(j, ring_in(i), false, &in_arm);
        find_arm(j, ring_out(i), true, &out_arm);
        if (in_arm < 0 || out_arm < 0) continue;
        std::vector<int> through;
        for (std::size_t c = 0; c < j.connectors.size(); ++c)
            if (j.connectors[c].enabled && j.connectors[c].arm_from == in_arm && j.connectors[c].arm_to == out_arm)
                through.push_back(static_cast<int>(c));
        for (std::size_t c = 0; c < j.connectors.size(); ++c) {
            const LaneConnector& k = j.connectors[c];
            if (!k.enabled || k.arm_to != out_arm || ring_set.count(j.arms[static_cast<std::size_t>(k.arm_from)].edge)) continue;
            GiveWay gw;
            gw.node = group.nodes[i];
            gw.connector = static_cast<int>(c);
            for (int t : through)
                if (std::binary_search(k.conflicts.begin(), k.conflicts.end(), t)) gw.yields_to.push_back(t);
            if (gw.yields_to.empty())
                for (int t : through)
                    if (j.connectors[static_cast<std::size_t>(t)].lane_to == k.lane_to) gw.yields_to.push_back(t);
            if (gw.yields_to.empty()) gw.yields_to = through;
            lay.give_way.push_back(std::move(gw));
        }
    }

    // ---- island: the ring centreline offset to the inner kerb face
    if (bad_type) return fail(lay, "ring edge has an unknown road type");
    lay.island_surface = prm.central_island_surface;
    lay.splitter_surface = cls.splitter_surface.empty() ? prm.splitter_surface : cls.splitter_surface;
    lay.apron_surface = prm.apron_surface;
    lay.island_kerb_upstand = prm.island_kerb_upstand;
    if (group.radius - in_ext < prm.min_island_radius)
        return fail(lay, "ring too tight for an island: mean ring radius " + std::to_string(group.radius) +
                             " m leaves an island under the minimum of " + std::to_string(prm.min_island_radius) + " m");
    const Ring poly = ring_polyline(group, graph);
    if (poly.size() < 3) return fail(lay, "ring has no geometry");
    const Ring island = inflate_ring(poly, -in_ext);
    if (island.size() < 3) return fail(lay, "the island outline is empty (ring too tight or too irregular)");
    // Over-run apron: a low paved collar round a small island (the raised island shrinks inside it).
    lay.island.push_back(island);
    if (cls.apron_width > 0 && prm.apron_island_radius_max > 0) {
        const double island_r = std::sqrt(std::fabs(signed_area(island)) / kPi);
        if (island_r < prm.apron_island_radius_max) {
            const Ring inner = inflate_ring(island, -cls.apron_width);
            if (inner.size() >= 3 && std::fabs(signed_area(inner)) > 1.0) {
                Ring hole = inner;
                std::reverse(hole.begin(), hole.end());
                lay.apron = {island, hole};
                lay.island = {inner};
            }
        }
    }

    // ---- splitter islands
    // Two-way approach: a teardrop between the shifted kerbs of the flare (no lane is narrower than the road
    // type's lane beside it). Its ring-side edge follows the circle of the ring's outer kerb.
    const double ring_out_r = group.radius + out_ext;
    const auto project = [&](Vec2 p) {
        const Vec2 d = p - group.centre;
        const double len = d.length();
        return len > 1e-9 ? group.centre + d * (ring_out_r / len) : p;
    };
    const auto ring_junction_arm = [&](const RoundaboutArm& arm, const Junction** jout) -> const JunctionArm* {
        const auto jt = lay.ring_junctions.find(group.nodes[static_cast<std::size_t>(arm.ring_index)]);
        if (jt == lay.ring_junctions.end()) return nullptr;
        for (const JunctionArm& ja : jt->second.arms)
            if (ja.edge == arm.edge) {
                *jout = &jt->second;
                return &ja;
            }
        return nullptr;
    };
    std::vector<bool> paired(group.arms.size(), false);
    for (std::size_t gi = 0; gi < group.arms.size(); ++gi) {
        const RoundaboutArm& arm = group.arms[gi];
        if (!(arm.entry && arm.exit)) continue;
        const Junction* j = nullptr;
        const JunctionArm* ja = ring_junction_arm(arm, &j);
        if (!ja || !ja->flare.active()) continue;
        const double s0 = std::min(out_ext, ja->flare.length - 2.0), s1 = ja->flare.length;
        const double h0 = std::max(ja->flare.spread_left, ja->flare.spread_right);
        const Edge* ge = graph.edge(arm.edge);
        const RoadType* gt = ge ? region.road_type(ge->road_type) : nullptr;
        // The kerbed island is a short teardrop: its length comes from the data (class length x approach speed
        // factor), never from the flare. The flare gap beyond it (corner fillets need it) is paint.
        const double want = cls.splitter_length * prm.splitter_speed_factor(gt ? gt->speed_limit : 0.0);
        const double s_nose = std::max(std::min(s0 + want, s1 - 0.5), s0 + 3.0);
        SplitterIsland sp;
        sp.arm = static_cast<int>(gi);
        sp.length = s_nose - s0;
        sp.width = 2 * h0;  // the gap between the flared lanes (the island is 0.75 of it: base_half)
        sp.ring_s = s0;
        sp.nose_s = s_nose;
        const double h_edge = 0.75 * h0;  // the island leaves a painted flank strip (a quarter of the gap half width) beside each lane edge line
        sp.base_half = h_edge;
        const auto ring_end = [&](std::vector<Vec2>& poly, Vec2 r0, Vec2 l0) {
            poly.push_back(project(r0));
            for (int q = 1; q < 5; ++q) poly.push_back(project(r0 + (l0 - r0) * (q / 5.0)));
        };
        const auto fix_ccw = [&](std::vector<Vec2>& poly) {
            if (signed_area(poly) < 0) std::reverse(poly.begin(), poly.end());
        };
        // ---- the whole gap between the flared lanes (paint area): ring end to the end of the flare
        {
            std::vector<Vec2> left, right;
            for (double s = s0; s < s1; s += 1.0) {
                left.push_back(arm_point(*ja, s, flare_spread(*ja, s, true)).pos.xy());
                right.push_back(arm_point(*ja, s, -flare_spread(*ja, s, false)).pos.xy());
            }
            left.push_back(arm_point(*ja, s1, 0).pos.xy());
            sp.paint = left;
            for (std::size_t q = right.size(); q-- > 1;) sp.paint.push_back(right[q]);
            ring_end(sp.paint, right.front(), left.front());
            sp.paint.front() = project(sp.paint.front());
            fix_ccw(sp.paint);
        }
        // ---- the kerbed island: a triangle from the full gap width at the ring to a small rounded nose
        {
            const double r_nose = std::min(0.45, 0.5 * h0);
            sp.nose_r = r_nose;
            const double s_c = s_nose - r_nose;
            const auto half = [&](double s) {
                // A teardrop: widest at the ring, rounding to the nose (exponent < 1 keeps it full for most of its length)
                const double line = r_nose + (h_edge - r_nose) * std::pow(std::max(0.0, (s_c - s) / (s_c - s0)), kIslandShape);
                return std::max(0.05, std::min(line, flare_spread(*ja, s, true) - 0.25 * h0));  // never wider than the gap less its flank strip
            };
            std::vector<Vec2> left, right;
            for (double s = s0; s < s_c - 1e-6; s += 1.0) {
                const double h = half(s);
                left.push_back(arm_point(*ja, s, h).pos.xy());
                right.push_back(arm_point(*ja, s, -h).pos.xy());
            }
            const double hc = half(s_c);
            left.push_back(arm_point(*ja, s_c, hc).pos.xy());
            right.push_back(arm_point(*ja, s_c, -hc).pos.xy());
            sp.polygon = left;
            for (int q = 1; q < 6; ++q) {  // the rounded nose: half circle of radius hc about (s_c, 0)
                const double phi = kPi * q / 6.0;
                sp.polygon.push_back(arm_point(*ja, s_c + hc * std::sin(phi), hc * std::cos(phi)).pos.xy());
            }
            for (std::size_t q = right.size(); q-- > 0;) sp.polygon.push_back(right[q]);
            ring_end(sp.polygon, right.front(), left.front());
            sp.polygon.front() = project(sp.polygon.front());
            fix_ccw(sp.polygon);
        }
        lay.splitters.push_back(std::move(sp));
    }
    // Dual-carriageway approach: two one-way arms (one in, one out) on adjacent ring nodes, roughly parallel.
    // The splitter is the central reservation between them carried into the ring.
    for (std::size_t ga = 0; ga < group.arms.size(); ++ga) {
        const RoundaboutArm& A = group.arms[ga];
        if (paired[ga] || !(A.entry && !A.exit)) continue;
        const Edge* ea = graph.edge(A.edge);
        const RoadType* ta = ea ? region.road_type(ea->road_type) : nullptr;
        if (!ta || !ta->dual) continue;
        for (std::size_t gb = 0; gb < group.arms.size(); ++gb) {
            const RoundaboutArm& B = group.arms[gb];
            if (paired[gb] || !(B.exit && !B.entry)) continue;
            const Edge* eb = graph.edge(B.edge);
            const RoadType* tb = eb ? region.road_type(eb->road_type) : nullptr;
            if (!tb || tb != ta) continue;
            const int di = (A.ring_index - B.ring_index + static_cast<int>(n)) % static_cast<int>(n);
            if (di != 1 && di != static_cast<int>(n) - 1) continue;
            const Junction *ja_j = nullptr, *jb_j = nullptr;
            const JunctionArm *ja = ring_junction_arm(A, &ja_j), *jb = ring_junction_arm(B, &jb_j);
            if (!ja || !jb) continue;
            if (std::fabs(wrap_angle(ja->heading_out - jb->heading_out)) > 0.5) continue;
            // Inner (median) side of each carriageway: toward the other one.
            const Vec2 pa = arm_point(*ja, 5.0, 0).pos.xy(), pb = arm_point(*jb, 5.0, 0).pos.xy();
            const Vec2 na{-std::sin(ja->heading_out), std::cos(ja->heading_out)};
            const Vec2 nb{-std::sin(jb->heading_out), std::cos(jb->heading_out)};
            const double sa = (pb - pa).x * na.x + (pb - pa).y * na.y >= 0 ? 1.0 : -1.0;
            const double sb = -((pb - pa).x * nb.x + (pb - pa).y * nb.y) >= 0 ? 1.0 : -1.0;
            const double lat_a = sa > 0 ? ja->kerb_left : -ja->kerb_right;
            const double lat_b = sb > 0 ? jb->kerb_left : -jb->kerb_right;
            const double speed = ta->speed_limit;
            const double len = std::min({cls.splitter_length * prm.splitter_speed_factor(speed),
                                         ja->edge_length / 2 - 1.0, jb->edge_length / 2 - 1.0});
            if (len < 4.0) continue;
            const double s0 = std::min(out_ext, len - 2.0);
            SplitterIsland sp;
            sp.arm = static_cast<int>(ga);
            sp.arm_b = static_cast<int>(gb);
            sp.length = len - s0;
            // Side points inside the ring's outer circle belong to the carriageway: drop them, the ring end is the circle.
            const auto outside_ring = [&](Vec2 p) { return (p - group.centre).length() > ring_out_r + 0.02; };
            std::vector<Vec2> side_a, side_b;
            for (double s = s0; s < len; s += 2.0) {
                const Vec2 pa = arm_point(*ja, s, lat_a).pos.xy(), pb = arm_point(*jb, s, lat_b).pos.xy();
                if (outside_ring(pa) && outside_ring(pb)) {
                    side_a.push_back(pa);
                    side_b.push_back(pb);
                }
            }
            if (side_a.empty()) continue;
            const Vec2 b0 = side_b.front(), a0 = side_a.front();
            sp.width = (b0 - a0).length();
            sp.polygon = side_a;
            sp.polygon.push_back(arm_point(*ja, len, lat_a).pos.xy());
            sp.polygon.push_back(arm_point(*jb, len, lat_b).pos.xy());
            for (std::size_t q = side_b.size(); q-- > 0;) sp.polygon.push_back(side_b[q]);
            sp.polygon.push_back(project(b0));
            for (int q = 1; q < 5; ++q) sp.polygon.push_back(project(b0 + (a0 - b0) * (q / 5.0)));
            sp.polygon.push_back(project(a0));
            if (signed_area(sp.polygon) < 0) std::reverse(sp.polygon.begin(), sp.polygon.end());
            paired[ga] = paired[gb] = true;
            lay.splitters.push_back(std::move(sp));
            break;
        }
    }
    lay.ok = true;
    return lay;
}

// ---------------------------------------------------------------- public: the post-pass

RoundaboutResult apply_roundabouts(JunctionSet& set, const RoadGraph& graph, const RegionPack& region,
                                   const JunctionOverrides& overrides, const std::vector<NodeId>& only) {
    RoundaboutResult res;
    RoundaboutScan scan = find_roundabouts(graph, region, only);
    res.problems = std::move(scan.problems);
    for (const RoundaboutProblem& p : res.problems)
        for (NodeId nd : p.nodes) {
            const auto it = set.junctions.find(nd);
            if (it == set.junctions.end()) continue;
            it->second.degraded = true;
            it->second.degraded_reason = "malformed roundabout ring: " + p.what;
        }
    for (const JunctionGroup& group : scan.groups) {
        RoundaboutLayout lay = build_roundabout(group, graph, region, overrides);
        for (NodeId nd : group.nodes) {
            Junction j;
            const auto it = lay.ring_junctions.find(nd);
            if (it != lay.ring_junctions.end()) j = it->second;
            else j = make_joint_junction(graph, region, overrides, nd);
            j.kind = JunctionKind::Roundabout;
            j.group_nodes = group.nodes;
            if (!lay.ok && !j.degraded) {
                j.degraded = true;
                j.degraded_reason = "roundabout: " + lay.error;
            }
            set.junctions.insert_or_assign(nd, std::move(j));
        }
        res.layouts.push_back(std::move(lay));
    }
    return res;
}

}  // namespace cp
