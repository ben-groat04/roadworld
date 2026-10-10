// Roundabouts: the place tool, recognition of hand-drawn rings, the layout (island, carriageway,
// splitters, connectors) and the post-pass over the JunctionSet. See docs/junctions.md section 7.
#include "cp/junction_roundabout.h"

#include <algorithm>
#include <cmath>
#include <set>

#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4702)  // unreachable code inside the vendored Clipper2 header
#endif
#include "clipper2/clipper.h"
#if defined(_MSC_VER)
#pragma warning(pop)
#endif

namespace cp {

// No roundabout fields in the region pack yet: the fallbacks in RoundaboutParams apply.
RoundaboutParams roundabout_params(const RegionPack&) { return RoundaboutParams{}; }

namespace {

using Ring = std::vector<Vec2>;

constexpr double kSampleStep = 0.25;  // m, scan step of the tool along crossing edges

// ---------------------------------------------------------------- small geometry helpers

double signed_area(const Ring& r) {
    double a = 0;
    for (std::size_t i = 0; i < r.size(); ++i) a += r[i].cross(r[(i + 1) % r.size()]);
    return a / 2;
}

Clipper2Lib::PathD to_path(const Ring& r) {
    Clipper2Lib::PathD p;
    p.reserve(r.size());
    for (Vec2 v : r) p.emplace_back(v.x, v.y);
    return p;
}

Clipper2Lib::PathsD to_paths(const PolygonSet& s) {
    Clipper2Lib::PathsD ps;
    for (const Ring& r : s) ps.push_back(to_path(r));
    return ps;
}

// Clipper2 orients outers CCW and holes CW.
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

double edge_heading(const Edge& e, double s) {
    double acc = 0;
    for (std::size_t i = 0; i < e.horizontal.spans.size(); ++i) {
        const Clothoid& c = e.horizontal.spans[i];
        if (s <= acc + c.length || i + 1 == e.horizontal.spans.size())
            return c.heading(std::clamp(s - acc, 0.0, c.length));
        acc += c.length;
    }
    return 0;
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

// Kerb-line half widths of a one-way edge of `road_type`, as the arms see them (m from the
// centreline, + = left of a -> b; both are distances, so positive).
struct KerbExtent {
    bool ok = false;
    double left = 0, right = 0;
};

KerbExtent kerb_extent(const RegionPack& region, const std::string& road_type) {
    KerbExtent k;
    const RoadType* t = region.road_type(road_type);
    if (!t) return k;
    Edge e;
    e.road_type = road_type;
    e.one_way = true;
    const std::vector<Lane> lanes = instantiate_lanes(e, *t, region.driving_side);
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
        if (l.lane_type != "kerb") continue;
        if (l.offset > 0) k.left = std::min(k.left, l.offset - l.width / 2);
        else k.right = std::min(k.right, -l.offset - l.width / 2);
    }
    k.ok = drivable;
    return k;
}

bool is_clockwise_for(const RegionPack& region) { return region.driving_side == DrivingSide::Left; }

// ---------------------------------------------------------------- the place tool

class PlaceRoundabout : public RoadCommand {
public:
    PlaceRoundabout(Vec3 centre, double radius, std::string type, bool clockwise, RoundaboutParams params,
                    KerbExtent kerb)
        : centre_(centre), radius_(radius), type_(std::move(type)), clockwise_(clockwise), prm_(params), kerb_(kerb) {}

    bool apply(RoadGraph& g) override {
        if (!planned_) {
            if (!plan(g)) return false;
            planned_ = true;
        }
        if (execute(g)) return true;
        planned_ = false;
        return false;
    }

    void revert(RoadGraph& g) override { undo(g); }

    std::string name() const override { return "place roundabout"; }

private:
    struct Stub {
        Edge e;
        int na = -1, nb = -1;  // index into added_nodes_ when that end is a new ring node, else the edge's own end
    };
    struct Interval {
        double lo, hi;
        bool lo_cut, hi_cut;
    };

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

    // Splits the plan: which edges and nodes go, which nodes and edges come. Reads the graph only.
    bool plan(const RoadGraph& g) {
        removed_edges_.clear();
        removed_nodes_.clear();
        added_nodes_.clear();
        stubs_.clear();
        if (!(radius_ > 0) || !kerb_.ok) return false;
        const double in_ext = clockwise_ ? kerb_.right : kerb_.left;  // distance to the island-side kerb
        if (radius_ - in_ext < prm_.min_island_radius) return false;

        const Vec2 c = centre_.xy();
        std::vector<double> cut_u;  // circulation parameter of each crossing, parallel to added_nodes_

        for (const auto& [id, e] : g.edges()) {
            const double L = e.horizontal.length();
            const auto dist = [&](double s) { return (edge_point(e, s).xy() - c).length() - radius_; };
            std::vector<Interval> ivs;
            bool touched = false;
            bool inside = dist(0) < 0;
            double start = 0;
            bool start_cut = false;
            touched = inside;
            double prev_s = 0;
            const int steps = std::max(1, static_cast<int>(std::ceil(L / kSampleStep)));
            for (int i = 1; i <= steps; ++i) {
                const double s = std::min(L, i * kSampleStep);
                const bool in = dist(s) < 0;
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
            if (!inside) ivs.push_back({start, L, start_cut, false});
            removed_edges_.push_back(e);

            const std::vector<double> is = interior_s(e);
            for (const Interval& iv : ivs) {
                if (iv.hi - iv.lo < prm_.min_edge_length) return false;  // stub too short
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
                    added_nodes_.push_back(Node{{}, ring_point(cut_u.back())});
                    (end == 0 ? st.na : st.nb) = static_cast<int>(added_nodes_.size()) - 1;
                }
                stubs_.push_back(std::move(st));
            }
        }
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
            const double d = wrap_2pi(rn[(i + 1) % m].u - rn[i].u);
            arc[i] = (m == 1 || d < 1e-9) ? 2 * kPi : d;
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
        if (total > prm_.max_ring_edges) return false;
        for (std::size_t i = 0; i < m; ++i)
            if (radius_ * arc[i] / sub[i] < prm_.min_edge_length) return false;  // ring edge too short

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
        return true;
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
    RoundaboutParams prm_;
    KerbExtent kerb_;

    bool planned_ = false;
    std::vector<Edge> removed_edges_;
    std::vector<Node> removed_nodes_;
    std::vector<Node> added_nodes_;  // ids filled on first execute and kept for redo
    std::vector<Stub> stubs_;        // edges to insert (ids likewise)
};

// ---------------------------------------------------------------- recognition

struct Candidate {
    std::vector<NodeId> nodes;
    std::vector<EdgeId> edges;
    double length = 0;
};

// Simple directed cycles of one-way edges, each reported once from its smallest node.
struct CycleFinder {
    const RoadGraph& g;
    const RoundaboutParams& prm;
    std::map<NodeId, std::vector<const Edge*>> out;
    double max_len = 0;
    std::vector<Candidate> cands;
    std::vector<NodeId> path_nodes;
    std::vector<EdgeId> path_edges;
    std::set<NodeId> on_path;

    void dfs(NodeId start, NodeId cur, double len) {
        const auto it = out.find(cur);
        if (it == out.end() || cands.size() > 20000) return;
        for (const Edge* e : it->second) {
            const double l2 = len + e->horizontal.length();
            if (l2 > max_len) continue;
            path_edges.push_back(e->id);
            if (e->b == start) {
                if (path_edges.size() >= 3) cands.push_back({path_nodes, path_edges, l2});
            } else if (e->b.value > start.value && !on_path.count(e->b) &&
                       static_cast<int>(path_edges.size()) < prm.max_ring_edges) {
                path_nodes.push_back(e->b);
                on_path.insert(e->b);
                dfs(start, e->b, l2);
                on_path.erase(e->b);
                path_nodes.pop_back();
            }
            path_edges.pop_back();
        }
    }
};

bool evaluate(const Candidate& c, const RoadGraph& g, const RegionPack& region, const RoundaboutParams& prm,
              JunctionGroup& group) {
    const std::size_t n = c.nodes.size();
    std::vector<Vec2> pts;
    for (EdgeId id : c.edges) {
        const Edge* e = g.edge(id);
        const double L = e->horizontal.length();
        const int k = std::max(4, static_cast<int>(std::ceil(L)));
        for (int i = 0; i < k; ++i) pts.push_back(edge_point(*e, L * i / k).xy());
    }
    Vec2 centre{};
    for (Vec2 p : pts) centre = centre + p;
    centre = centre * (1.0 / static_cast<double>(pts.size()));
    double sum = 0, dmin = 1e18, dmax = 0;
    for (Vec2 p : pts) {
        const double d = (p - centre).length();
        sum += d;
        dmin = std::min(dmin, d);
        dmax = std::max(dmax, d);
    }
    const double radius = sum / static_cast<double>(pts.size());
    if (radius < prm.min_radius || radius > prm.max_radius) return false;
    if (dmin < prm.min_roundness * dmax) return false;
    const double area = signed_area(pts);
    if (std::fabs(area) < 0.5 * kPi * radius * radius) return false;  // figure-of-eight and the like
    const bool clockwise = area < 0;
    if (clockwise != is_clockwise_for(region)) return false;

    group = JunctionGroup{};
    group.nodes = c.nodes;
    group.ring_edges = c.edges;
    group.centre = centre;
    group.radius = radius;
    group.clockwise = clockwise;
    const std::set<EdgeId> ring(c.edges.begin(), c.edges.end());
    for (std::size_t i = 0; i < n; ++i) {
        std::vector<RoundaboutArm> here;
        for (const JunctionArm& a : collect_arms(g, c.nodes[i], region)) {
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
    return static_cast<int>(group.arms.size()) >= prm.min_arms;
}

// ---------------------------------------------------------------- layout helpers

// Trim of the arm of `edge` at `node` (0 if the node has no junction, e.g. a joint).
double ring_trim(const std::map<NodeId, Junction>& js, NodeId node, EdgeId edge, bool at_a) {
    const auto it = js.find(node);
    if (it == js.end()) return 0;
    const Junction& j = it->second;
    for (std::size_t k = 0; k < j.arms.size() && k < j.trims.size(); ++k)
        if (j.arms[k].edge == edge && j.arms[k].at_a == at_a) return j.trims[k];
    return 0;
}

Vec3 polyline_at(const std::vector<EdgeSample>& samples, double s) {
    if (samples.empty()) return {};
    std::size_t i = 0;
    while (i + 2 < samples.size() && samples[i + 1].s < s) ++i;
    if (samples.size() < 2) return samples[0].pos;
    const double span = samples[i + 1].s - samples[i].s;
    const double t = span > 0 ? std::clamp((s - samples[i].s) / span, 0.0, 1.0) : 0.0;
    const Vec3 &p = samples[i].pos, &q = samples[i + 1].pos;
    return {p.x + (q.x - p.x) * t, p.y + (q.y - p.y) * t, p.z + (q.z - p.z) * t};
}

std::vector<Vec3> slice(const OffsetPolyline& pl, double s0, double s1) {
    std::vector<Vec3> out;
    if (pl.samples.size() < 2) return out;
    if (s1 <= s0) s0 = s1 = (s0 + s1) / 2;
    out.push_back(polyline_at(pl.samples, s0));
    for (const EdgeSample& s : pl.samples)
        if (s.s > s0 + 1e-6 && s.s < s1 - 1e-6) out.push_back(s.pos);
    out.push_back(polyline_at(pl.samples, s1));
    return out;
}

void append_path(std::vector<Vec3>& out, const std::vector<Vec3>& p) {
    for (std::size_t i = 0; i < p.size(); ++i) {
        if (i == 0 && !out.empty()) {
            const Vec3& b = out.back();
            if (std::hypot(b.x - p[0].x, b.y - p[0].y) < 0.05) continue;
        }
        out.push_back(p[i]);
    }
}

double path_length(const std::vector<Vec3>& p) {
    double l = 0;
    for (std::size_t i = 0; i + 1 < p.size(); ++i) l += std::hypot(p[i + 1].x - p[i].x, p[i + 1].y - p[i].y);
    return l;
}

// Point at fraction f (0..1) of the path's length.
Vec3 path_at(const std::vector<Vec3>& p, double f) {
    const double target = path_length(p) * f;
    double acc = 0;
    for (std::size_t i = 0; i + 1 < p.size(); ++i) {
        const double l = std::hypot(p[i + 1].x - p[i].x, p[i + 1].y - p[i].y);
        if (acc + l >= target || i + 2 == p.size()) {
            const double t = l > 0 ? std::clamp((target - acc) / l, 0.0, 1.0) : 0.0;
            return {p[i].x + (p[i + 1].x - p[i].x) * t, p[i].y + (p[i + 1].y - p[i].y) * t,
                    p[i].z + (p[i + 1].z - p[i].z) * t};
        }
        acc += l;
    }
    return p.empty() ? Vec3{} : p.back();
}

// A lane change: starts on `a`, ends on `b` (two lanes of the same ring edge), smoothstep between.
std::vector<Vec3> blend_paths(const std::vector<Vec3>& a, const std::vector<Vec3>& b) {
    if (a.size() < 2 || b.size() < 2) return a;
    const int n = std::clamp(static_cast<int>(path_length(a) / 1.5), 8, 64);
    std::vector<Vec3> out;
    for (int i = 0; i <= n; ++i) {
        const double f = static_cast<double>(i) / n, w = f * f * (3 - 2 * f);
        const Vec3 p = path_at(a, f), q = path_at(b, f);
        out.push_back({p.x + (q.x - p.x) * w, p.y + (q.y - p.y) * w, p.z + (q.z - p.z) * w});
    }
    return out;
}

bool segments_cross(Vec2 a, Vec2 b, Vec2 c, Vec2 d) {
    const auto o = [](Vec2 p, Vec2 q, Vec2 r) { return (q - p).cross(r - p); };
    const double d1 = o(a, b, c), d2 = o(a, b, d), d3 = o(c, d, a), d4 = o(c, d, b);
    return ((d1 > 1e-9 && d2 < -1e-9) || (d1 < -1e-9 && d2 > 1e-9)) &&
           ((d3 > 1e-9 && d4 < -1e-9) || (d3 < -1e-9 && d4 > 1e-9));
}

bool paths_cross(const std::vector<Vec3>& p, const std::vector<Vec3>& q, double dz) {
    for (std::size_t i = 0; i + 1 < p.size(); ++i)
        for (std::size_t k = 0; k + 1 < q.size(); ++k)
            if (segments_cross(p[i].xy(), p[i + 1].xy(), q[k].xy(), q[k + 1].xy()) &&
                std::fabs((p[i].z + p[i + 1].z) / 2 - (q[k].z + q[k + 1].z) / 2) < dz)
                return true;
    return false;
}

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

// Largest-area ring of a Clipper result as an offset of a CCW polygon.
Ring inflate_ring(const Ring& ccw, double delta) {
    const Clipper2Lib::PathsD res = Clipper2Lib::InflatePaths(Clipper2Lib::PathsD{to_path(ccw)}, delta,
                                                              Clipper2Lib::JoinType::Miter,
                                                              Clipper2Lib::EndType::Polygon, 2.0, 3);
    Ring best;
    double best_area = 0;
    for (const PolygonSet::value_type& r : from_paths(res))
        if (signed_area(r) > best_area) {
            best_area = signed_area(r);
            best = r;
        }
    return best;
}

}  // namespace

// ---------------------------------------------------------------- public: tool and recognition

RoadCommandPtr make_place_roundabout(Vec3 centre, double radius, const std::string& ring_road_type,
                                     const RegionPack& region) {
    return std::make_unique<PlaceRoundabout>(centre, radius, ring_road_type, is_clockwise_for(region),
                                             roundabout_params(region), kerb_extent(region, ring_road_type));
}

std::vector<JunctionGroup> recognise_roundabouts(const RoadGraph& graph, const RegionPack& region) {
    const RoundaboutParams prm = roundabout_params(region);
    CycleFinder f{graph, prm, {}, 2 * kPi * prm.max_radius * 1.3, {}, {}, {}, {}};
    for (const auto& [id, e] : graph.edges())
        if (e.one_way && e.a != e.b) f.out[e.a].push_back(&e);
    for (const auto& [start, edges] : f.out) {
        f.path_nodes = {start};
        f.path_edges.clear();
        f.on_path.clear();
        f.dfs(start, start, 0);
    }
    std::stable_sort(f.cands.begin(), f.cands.end(), [](const Candidate& a, const Candidate& b) {
        if (a.nodes[0] != b.nodes[0]) return a.nodes[0] < b.nodes[0];
        return a.length < b.length;
    });
    std::vector<JunctionGroup> groups;
    std::set<NodeId> used;
    for (const Candidate& c : f.cands) {
        if (std::any_of(c.nodes.begin(), c.nodes.end(), [&](NodeId n) { return used.count(n) > 0; })) continue;
        JunctionGroup grp;
        if (!evaluate(c, graph, region, prm, grp)) continue;
        used.insert(c.nodes.begin(), c.nodes.end());
        groups.push_back(std::move(grp));
    }
    return groups;
}

// ---------------------------------------------------------------- public: layout

RoundaboutLayout build_roundabout(const JunctionGroup& group, const RoadGraph& graph, const RegionPack& region,
                                  const JunctionOverrides& overrides) {
    using namespace Clipper2Lib;
    RoundaboutLayout lay;
    lay.group = group;
    const RoundaboutParams prm = roundabout_params(region);
    const std::size_t n = group.nodes.size();
    if (n < 3 || group.ring_edges.size() != n) return fail(lay, "ring is not a closed loop");
    const std::set<EdgeId> ring_set(group.ring_edges.begin(), group.ring_edges.end());
    const auto ring_in = [&](std::size_t i) { return group.ring_edges[(i + n - 1) % n]; };
    const auto ring_out = [&](std::size_t i) { return group.ring_edges[i]; };

    // ---- forced fillet radii at the ring nodes (the player's own corner overrides win)
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
            const EdgeId in = ring_in(i), out = ring_out(i);
            double radius = -1;
            if (r_ring != l_ring) {
                const EdgeId rng = r_ring ? r.edge : l.edge;
                radius = rng == out ? prm.entry_corner_radius : prm.exit_corner_radius;
            } else if (r_ring && l_ring && ((r.edge == in && l.edge == out) || (r.edge == out && l.edge == in))) {
                radius = prm.ring_corner_radius_factor * group.radius;
            }
            if (radius >= 0) ov.corner_radius[key] = radius;
        }
    }
    JunctionSet built = build_junctions(graph, region, ov, group.nodes);
    lay.ring_junctions = std::move(built.junctions);

    // ---- connectors: entries, exits and throughs from the node junctions, then the ring lanes
    for (std::size_t i = 0; i < n; ++i) {
        const auto jt = lay.ring_junctions.find(group.nodes[i]);
        if (jt == lay.ring_junctions.end()) continue;
        const Junction& j = jt->second;
        for (const LaneConnector& c : j.connectors) {
            if (!c.enabled || c.path.size() < 2) continue;
            const JunctionArm& fa = j.arms[static_cast<std::size_t>(c.arm_from)];
            const JunctionArm& ta = j.arms[static_cast<std::size_t>(c.arm_to)];
            const bool f_ring = ring_set.count(fa.edge) > 0, t_ring = ring_set.count(ta.edge) > 0;
            RoundaboutConnector rc;
            rc.ring_index = static_cast<int>(i);
            if (!f_ring && t_ring && ta.edge == ring_out(i)) rc.role = RoundaboutConnectorRole::Entry;
            else if (f_ring && fa.edge == ring_in(i) && !t_ring) rc.role = RoundaboutConnectorRole::Exit;
            else if (f_ring && t_ring && fa.edge == ring_in(i) && ta.edge == ring_out(i))
                rc.role = RoundaboutConnectorRole::Through;
            else continue;  // approach to approach at a ring node: not a roundabout movement
            rc.from_edge = fa.edge;
            rc.to_edge = ta.edge;
            rc.from_slot = fa.lanes[static_cast<std::size_t>(c.lane_from)].slot;
            rc.to_slot = ta.lanes[static_cast<std::size_t>(c.lane_to)].slot;
            rc.path = c.path;
            rc.give_way = rc.role == RoundaboutConnectorRole::Entry;
            lay.connectors.push_back(std::move(rc));
        }
    }
    const std::size_t first_ring = lay.connectors.size();
    for (std::size_t i = 0; i < n; ++i) {
        const Edge* e = graph.edge(ring_out(i));
        const RoadType* t = e ? region.road_type(e->road_type) : nullptr;
        if (!t) return fail(lay, "ring edge has an unknown road type");
        const double t0 = ring_trim(lay.ring_junctions, group.nodes[i], e->id, true);
        const double t1 = ring_trim(lay.ring_junctions, group.nodes[(i + 1) % n], e->id, false);
        const double L = e->horizontal.length();
        for (const Lane& l : instantiate_lanes(*e, *t, region.driving_side)) {
            const LaneType* lt = region.lane_type(l.lane_type);
            if (!lt || !lt->connects_at_junctions || l.dir != TravelDir::Forward) continue;
            RoundaboutConnector rc;
            rc.role = RoundaboutConnectorRole::Ring;
            rc.ring_index = static_cast<int>(i);
            rc.from_edge = rc.to_edge = e->id;
            rc.from_slot = rc.to_slot = l.slot;
            rc.path = slice(sample_offset(*e, l.offset), t0, L - t1);
            lay.connectors.push_back(std::move(rc));
        }
    }
    // Entries yield to the circulating movements: Through at the same node and the Ring lane
    // arriving on the same slot (all arriving Ring lanes if no slot matches).
    for (std::size_t ci = 0; ci < first_ring; ++ci) {
        RoundaboutConnector& c = lay.connectors[ci];
        if (c.role != RoundaboutConnectorRole::Entry) continue;
        const int arriving = static_cast<int>((static_cast<std::size_t>(c.ring_index) + n - 1) % n);
        std::vector<int> ring_match, ring_any;
        for (std::size_t k = 0; k < lay.connectors.size(); ++k) {
            const RoundaboutConnector& o = lay.connectors[k];
            if (o.role == RoundaboutConnectorRole::Through && o.ring_index == c.ring_index && o.to_slot == c.to_slot)
                c.conflicts.push_back(static_cast<int>(k));
            if (o.role == RoundaboutConnectorRole::Ring && o.ring_index == arriving) {
                ring_any.push_back(static_cast<int>(k));
                if (o.from_slot == c.to_slot) ring_match.push_back(static_cast<int>(k));
            }
        }
        const std::vector<int>& add = ring_match.empty() ? ring_any : ring_match;
        c.conflicts.insert(c.conflicts.end(), add.begin(), add.end());
        std::sort(c.conflicts.begin(), c.conflicts.end());
    }

    // ---- island and carriageway
    const Ring poly = ring_polyline(group, graph);
    if (poly.size() < 3) return fail(lay, "ring has no geometry");
    double out_ext = 0, in_ext = 0;
    for (EdgeId id : group.ring_edges) {
        const Edge* e = graph.edge(id);
        const KerbExtent k = kerb_extent(region, e->road_type);
        out_ext = std::max(out_ext, group.clockwise ? k.left : k.right);
        in_ext = std::max(in_ext, group.clockwise ? k.right : k.left);
    }
    const Ring island = inflate_ring(poly, -in_ext);
    const Ring disc = inflate_ring(poly, out_ext);
    if (island.size() < 3 || disc.size() < 3) return fail(lay, "island or ring outline is empty");
    lay.island.push_back(island);

    PathsD solid{to_path(disc)};
    for (const auto& [nid, j] : lay.ring_junctions)
        for (const Ring& r : j.carriageway) solid.push_back(to_path(r));
    const PathsD merged = Union(solid, FillRule::NonZero, 3);
    lay.carriageway = from_paths(Difference(merged, PathsD{to_path(island)}, FillRule::NonZero, 3));
    if (merged.empty() || lay.carriageway.empty()) return fail(lay, "carriageway is empty");

    // ---- splitter islands on two-way approaches
    for (std::size_t gi = 0; gi < group.arms.size(); ++gi) {
        const RoundaboutArm& arm = group.arms[gi];
        if (!(arm.entry && arm.exit)) continue;
        const auto jt = lay.ring_junctions.find(group.nodes[static_cast<std::size_t>(arm.ring_index)]);
        if (jt == lay.ring_junctions.end()) continue;
        const Junction& j = jt->second;
        for (std::size_t k = 0; k < j.arms.size() && k < j.trims.size(); ++k) {
            const JunctionArm& ja = j.arms[k];
            if (ja.edge != arm.edge) continue;
            const double s0 = j.trims[k];
            const double len = std::min(prm.splitter_length, ja.edge_length - s0 - 1.0);
            if (len < 4.0) break;
            constexpr int kSteps = 6;
            const double hw = prm.splitter_width / 2;
            SplitterIsland sp;
            sp.arm = static_cast<int>(gi);
            for (int q = 0; q <= kSteps; ++q) {
                const double t = static_cast<double>(q) / kSteps;
                sp.polygon.push_back(arm_point(ja, s0 + len * t, hw * (1 - t * t)).pos.xy());
            }
            for (int q = kSteps - 1; q >= 0; --q) {
                const double t = static_cast<double>(q) / kSteps;
                sp.polygon.push_back(arm_point(ja, s0 + len * t, -hw * (1 - t * t)).pos.xy());
            }
            if (signed_area(sp.polygon) < 0) std::reverse(sp.polygon.begin(), sp.polygon.end());
            lay.splitters.push_back(std::move(sp));
            break;
        }
    }
    lay.ok = true;
    return lay;
}

// ---------------------------------------------------------------- public: the post-pass

void apply_roundabouts(JunctionSet& set, const RoadGraph& graph, const RegionPack& region,
                       const JunctionOverrides& overrides) {
    using namespace Clipper2Lib;
    for (const JunctionGroup& group : recognise_roundabouts(graph, region)) {
        if (std::none_of(group.nodes.begin(), group.nodes.end(),
                         [&](NodeId nd) { return set.junctions.count(nd) > 0; }))
            continue;
        RoundaboutLayout lay = build_roundabout(group, graph, region, overrides);
        if (!lay.ok) continue;
        const std::size_t n = group.nodes.size();
        const std::set<EdgeId> ring_set(group.ring_edges.begin(), group.ring_edges.end());

        Junction rep;
        rep.node = group.nodes[0];
        rep.side = region.driving_side;
        rep.cls = NodeClass::Junction;
        rep.kind = JunctionKind::Roundabout;
        rep.group_nodes = group.nodes;
        double z = 0;
        for (NodeId nd : group.nodes) z += graph.node(nd) ? graph.node(nd)->pos.z : 0;
        rep.pos = {group.centre.x, group.centre.y, z / static_cast<double>(n)};

        // Approach arms of all ring nodes, CCW about the ring centre.
        struct Src {
            std::size_t ring, k;
            double angle;
        };
        std::vector<Src> srcs;
        for (std::size_t i = 0; i < n; ++i) {
            const auto jt = lay.ring_junctions.find(group.nodes[i]);
            if (jt == lay.ring_junctions.end()) continue;
            const Vec2 p = jt->second.pos.xy() - group.centre;
            for (std::size_t k = 0; k < jt->second.arms.size(); ++k)
                if (!ring_set.count(jt->second.arms[k].edge)) srcs.push_back({i, k, std::atan2(p.y, p.x)});
        }
        std::stable_sort(srcs.begin(), srcs.end(), [](const Src& a, const Src& b) { return a.angle < b.angle; });
        std::map<std::pair<std::size_t, std::size_t>, int> arm_index;  // (ring, k) -> rep arm
        std::vector<std::size_t> arm_ring;
        for (const Src& s : srcs) {
            const Junction& j = lay.ring_junctions.at(group.nodes[s.ring]);
            arm_index[{s.ring, s.k}] = static_cast<int>(rep.arms.size());
            rep.arms.push_back(j.arms[s.k]);
            rep.trims.push_back(s.k < j.trims.size() ? j.trims[s.k] : 0.0);
            arm_ring.push_back(s.ring);
        }
        rep.major_arms = major_arm_indices(rep.arms);

        for (std::size_t i = 0; i < n; ++i) {
            const auto jt = lay.ring_junctions.find(group.nodes[i]);
            if (jt == lay.ring_junctions.end()) continue;
            const Junction& j = jt->second;
            if (j.degraded && !rep.degraded) {
                rep.degraded = true;
                rep.degraded_reason = "ring node " + std::to_string(group.nodes[i].value) + ": " + j.degraded_reason;
            }
            for (const CrossingSpot& c : j.crossings) {
                const auto it = arm_index.find({i, static_cast<std::size_t>(c.arm)});
                if (it == arm_index.end()) continue;
                CrossingSpot r = c;
                r.arm = it->second;
                rep.crossings.push_back(r);
            }
            rep.gore.insert(rep.gore.end(), j.gore.begin(), j.gore.end());
        }
        rep.carriageway = lay.carriageway;
        {
            PathsD pav;
            for (const auto& [nd, j] : lay.ring_junctions)
                for (const Ring& r : j.pavements) pav.push_back(to_path(r));
            if (!pav.empty()) {
                PathsD occupied = to_paths(lay.carriageway);
                for (const Ring& r : lay.island) occupied.push_back(to_path(r));
                rep.pavements = from_paths(Difference(Union(pav, FillRule::NonZero, 3),
                                                      Union(occupied, FillRule::NonZero, 3), FillRule::NonZero, 3));
            }
        }

        // ---- one connector per (inbound lane, outbound lane of another arm), routed round the ring
        const auto find_conn = [&](RoundaboutConnectorRole role, int ring, EdgeId from_edge, int from_slot,
                                   EdgeId to_edge, int to_slot) -> const RoundaboutConnector* {
            for (const RoundaboutConnector& c : lay.connectors)
                if (c.role == role && c.ring_index == ring && (!from_edge || c.from_edge == from_edge) &&
                    c.from_slot == from_slot && (!to_edge || c.to_edge == to_edge) &&
                    (to_slot < 0 || c.to_slot == to_slot))
                    return &c;
            return nullptr;
        };
        for (std::size_t a = 0; a < rep.arms.size(); ++a)
            for (std::size_t la = 0; la < rep.arms[a].lanes.size(); ++la) {
                const LaneRef& lin = rep.arms[a].lanes[la];
                if (!lin.inbound) continue;
                for (std::size_t b = 0; b < rep.arms.size(); ++b) {
                    if (b == a || arm_ring[b] == arm_ring[a]) continue;
                    for (std::size_t lb = 0; lb < rep.arms[b].lanes.size(); ++lb) {
                        const LaneRef& lout = rep.arms[b].lanes[lb];
                        if (lout.inbound) continue;
                        std::vector<Vec3> best;
                        double best_len = 0;
                        for (const RoundaboutConnector& entry : lay.connectors) {
                            if (entry.role != RoundaboutConnectorRole::Entry ||
                                entry.ring_index != static_cast<int>(arm_ring[a]) ||
                                entry.from_edge != rep.arms[a].edge || entry.from_slot != lin.slot)
                                continue;
                            const int k = entry.to_slot;
                            std::vector<Vec3> path = entry.path;
                            std::size_t m = arm_ring[a];
                            for (std::size_t step = 0; step < n; ++step) {
                                const RoundaboutConnector* rg = find_conn(RoundaboutConnectorRole::Ring,
                                                                          static_cast<int>(m), {}, k, {}, k);
                                if (!rg) break;
                                if ((m + 1) % n == arm_ring[b]) {
                                    // Last ring edge: leave on any ring lane that can exit, changing lane
                                    // along this edge when it is not the one we entered on.
                                    for (const RoundaboutConnector& ex : lay.connectors) {
                                        if (ex.role != RoundaboutConnectorRole::Exit ||
                                            ex.ring_index != static_cast<int>(arm_ring[b]) ||
                                            ex.to_edge != rep.arms[b].edge || ex.to_slot != lout.slot)
                                            continue;
                                        const RoundaboutConnector* rg2 = find_conn(
                                            RoundaboutConnectorRole::Ring, static_cast<int>(m), {}, ex.from_slot, {},
                                            ex.from_slot);
                                        if (!rg2) continue;
                                        std::vector<Vec3> cand = path;
                                        append_path(cand, rg2 == rg ? rg->path : blend_paths(rg->path, rg2->path));
                                        append_path(cand, ex.path);
                                        const double len = path_length(cand);
                                        if (cand.size() >= 2 && (best.empty() || len < best_len - 1e-9)) {
                                            best = std::move(cand);
                                            best_len = len;
                                        }
                                    }
                                    break;
                                }
                                append_path(path, rg->path);
                                m = (m + 1) % n;
                                const RoundaboutConnector* th = find_conn(RoundaboutConnectorRole::Through,
                                                                          static_cast<int>(m), {}, k, {}, k);
                                if (th) append_path(path, th->path);
                                else if (lay.ring_junctions.count(group.nodes[m])) break;
                            }
                        }
                        if (best.empty()) continue;
                        LaneConnector lc;
                        lc.arm_from = static_cast<int>(a);
                        lc.lane_from = static_cast<int>(la);
                        lc.arm_to = static_cast<int>(b);
                        lc.lane_to = static_cast<int>(lb);
                        const double turn = wrap_angle(rep.arms[b].heading_out - wrap_angle(rep.arms[a].heading_out + kPi));
                        lc.kind = classify_turn(turn, region.driving_side);
                        lc.path = std::move(best);
                        rep.connectors.push_back(std::move(lc));
                    }
                }
            }
        for (std::size_t x = 0; x < rep.connectors.size(); ++x)
            for (std::size_t y = 0; y < rep.connectors.size(); ++y) {
                if (x == y) continue;
                const LaneConnector &cx = rep.connectors[x], &cy = rep.connectors[y];
                if ((cx.arm_to == cy.arm_to && cx.lane_to == cy.lane_to) ||
                    paths_cross(cx.path, cy.path, region.plan_crossing_junction_dz_m))
                    rep.connectors[x].conflicts.push_back(static_cast<int>(y));
            }

        for (NodeId nd : group.nodes) set.junctions.erase(nd);
        set.junctions.emplace(rep.node, std::move(rep));
    }
}

}  // namespace cp
