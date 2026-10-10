// [C] Merge / diverge nodes (docs/junctions.md §4). A slip road is a one-way road joining a
// carriageway at a shallow angle. The two nearly parallel arms (the "pair": main P and slip Q)
// meet one odd arm O. Merge: P and Q flow into O. Diverge: O flows out into P and Q.
//
// Trims: the pair arms are trimmed at the gore (nose) point, where their facing kerb lines cross
// (a*cot(t) + b/sin(t), the corner formula's kerb-intersection term, finite for t > 0) plus a short
// gore length, so the gore triangle sits inside the junction. The odd arm O is trimmed at the taper
// length for the main carriageway design speed. All trims are clamped to
// min(kArmSampleLength, edge_length / 2), so a 75-150 m taper is only covered up to 40 m here.
#include <algorithm>
#include <cmath>
#include <tuple>

#include "clipper2/clipper.h"
#include "cp/junction.h"

namespace cp {

// Parametric taper table (m) by design speed. TODO: move to the region pack (data).
double merge_taper_length(double design_speed_ms) {
    const double kph = design_speed_ms * 3.6;
    if (kph <= 80.0) return 75.0;    // up to ~50 mph
    if (kph <= 100.0) return 110.0;  // dual carriageway
    return 150.0;                    // motorway
}

namespace {

constexpr double kGoreLength = 10.0;  // m of gore triangle beyond the nose point

using Ring = std::vector<Vec2>;

double area(const Ring& r) {
    double a = 0;
    for (std::size_t i = 0; i < r.size(); ++i) a += r[i].cross(r[(i + 1) % r.size()]);
    return a / 2;
}

Clipper2Lib::PathD to_path(const Ring& r) {
    Clipper2Lib::PathD p;
    for (Vec2 v : r) p.emplace_back(v.x, v.y);
    return p;
}

PolygonSet from_paths(const Clipper2Lib::PathsD& ps) {
    PolygonSet out;
    for (const auto& p : ps) {
        if (p.size() < 3) continue;
        Ring r;
        for (const auto& pt : p) r.push_back({pt.x, pt.y});
        if (std::fabs(area(r)) < 1e-6) continue;
        out.push_back(std::move(r));  // Clipper2 orients outers CCW and holes CW
    }
    return out;
}

Clipper2Lib::PathsD to_paths(const PolygonSet& s) {
    Clipper2Lib::PathsD ps;
    for (const Ring& r : s) ps.push_back(to_path(r));
    return ps;
}

// Kerb-to-kerb strip of an arm from s0 to s1.
Ring arm_strip(const JunctionArm& arm, double s0, double s1) {
    s1 = std::max(s1, s0 + 0.01);
    std::vector<double> ss;
    const int n = std::max(1, static_cast<int>(std::ceil((s1 - s0) / 4.0)));
    for (int i = 0; i <= n; ++i) ss.push_back(s0 + (s1 - s0) * i / n);
    Ring r;
    for (double s : ss) r.push_back(arm_point(arm, s, arm.kerb_left).pos.xy());
    for (auto it = ss.rbegin(); it != ss.rend(); ++it) r.push_back(arm_point(arm, *it, -arm.kerb_right).pos.xy());
    if (area(r) < 0) std::reverse(r.begin(), r.end());
    return r;
}

// Rectangle just beyond an arm's trim line (cuts the pavement offset at the trim line).
Ring end_cap(const JunctionArm& arm, double trim, double depth) {
    const double w = std::max(arm.half_width_left, arm.half_width_right) + depth + 1;
    Ring r{arm_point(arm, trim, w).pos.xy(), arm_point(arm, trim + depth + 1, w).pos.xy(),
           arm_point(arm, trim + depth + 1, -w).pos.xy(), arm_point(arm, trim, -w).pos.xy()};
    if (area(r) < 0) std::reverse(r.begin(), r.end());
    return r;
}

// Lane indices of `arm` travelling toward (inbound) or away from the node, offside first.
std::vector<int> ordered_lanes(const Junction& j, const JunctionArm& arm, bool inbound, const JunctionOverrides& ov) {
    std::vector<std::pair<double, int>> v;
    for (std::size_t i = 0; i < arm.lanes.size(); ++i) {
        const LaneRef& l = arm.lanes[i];
        if (l.inbound != inbound) continue;
        auto it = ov.lane_connects.find({j.node, l.edge, l.slot});
        if (it != ov.lane_connects.end() && !it->second) continue;
        v.push_back({inbound ? -l.lateral : l.lateral, static_cast<int>(i)});  // travel-left coordinate
    }
    // Left driving: nearside is travel-left, so offside first = ascending. Right driving: descending.
    std::sort(v.begin(), v.end());
    if (j.side != DrivingSide::Left) std::reverse(v.begin(), v.end());
    std::vector<int> out;
    for (auto& p : v) out.push_back(p.second);
    return out;
}

// Lane pairs (index into the pair arm's list, index into O's list) joining `a` pair-arm lanes to
// `b` odd-arm lanes, aligned at the offside or nearside end. Surplus pair lanes merge into the
// last aligned O lane; surplus O lanes are left for the orphan pass.
std::vector<std::pair<int, int>> align(int a, int b, bool nearside) {
    std::vector<std::pair<int, int>> out;
    if (a <= 0 || b <= 0) return out;
    auto ia = [&](int k) { return nearside ? a - 1 - k : k; };
    auto ib = [&](int k) { return nearside ? b - 1 - k : k; };
    for (int k = 0; k < std::min(a, b); ++k) out.push_back({ia(k), ib(k)});
    for (int k = b; k < a; ++k) out.push_back({ia(k), ib(b - 1)});
    return out;
}

bool seg_cross(Vec2 a, Vec2 b, Vec2 c, Vec2 d) {
    const double d1 = (b - a).cross(c - a), d2 = (b - a).cross(d - a);
    const double d3 = (d - c).cross(a - c), d4 = (d - c).cross(b - c);
    const double e = 1e-9;
    return ((d1 > e && d2 < -e) || (d1 < -e && d2 > e)) && ((d3 > e && d4 < -e) || (d3 < -e && d4 > e));
}

bool paths_cross(const std::vector<Vec3>& p, const std::vector<Vec3>& q, double dz) {
    for (std::size_t i = 0; i + 1 < p.size(); ++i)
        for (std::size_t k = 0; k + 1 < q.size(); ++k)
            if (seg_cross(p[i].xy(), p[i + 1].xy(), q[k].xy(), q[k + 1].xy()) &&
                std::fabs(0.5 * (p[i].z + p[i + 1].z) - 0.5 * (q[k].z + q[k + 1].z)) < dz)
                return true;
    return false;
}

}  // namespace

void generate_merge_diverge(Junction& junction, const RoadGraph& graph, const RegionPack& region,
                            const JunctionOverrides& overrides) {
    const std::size_t n = junction.arms.size();
    junction.trims.assign(n, 0.0);
    junction.connectors.clear();
    junction.carriageway.clear();
    junction.pavements.clear();
    junction.gore.clear();
    junction.crossings.clear();
    junction.corners.clear();
    if (n != 3) return;
    const bool merge = junction.cls == NodeClass::Merge;
    const auto& arms = junction.arms;
    auto A = [&](int i) -> const JunctionArm& { return arms[static_cast<std::size_t>(i)]; };

    // The odd arm: the only one whose lanes flow the other way from the pair.
    int oi = -1;
    for (int i = 0; i < 3; ++i)
        if (merge ? A(i).outbound_count() > 0 : A(i).inbound_count() > 0) oi = i;
    if (oi < 0) return;
    const int a1 = (oi + 1) % 3, a2 = (oi + 2) % 3;  // a2 is CCW of a1

    // Main carriageway P = the more major of the pair; the slip Q is the other.
    auto rank = [&](int i) {
        const auto& a = A(i);
        return std::tuple(a.road_class, a.half_width_left + a.half_width_right, a.lanes.size());
    };
    const int pi = rank(a1) >= rank(a2) ? a1 : a2;
    const int qi = pi == a1 ? a2 : a1;

    // ---- trims
    auto clampt = [&](int i, double t) {
        return std::clamp(t, 0.0, std::min(kArmSampleLength, A(i).edge_length / 2));
    };
    double theta = std::fabs(wrap_angle(A(a2).heading_out - A(a1).heading_out));
    theta = std::clamp(theta, 0.05, kPi - 0.05);
    const double ka = A(a1).kerb_left, kb = A(a2).kerb_right;
    const double d1 = ka / std::tan(theta) + kb / std::sin(theta);
    const double d2 = ka / std::sin(theta) + kb / std::tan(theta);
    junction.trims[static_cast<std::size_t>(a1)] = clampt(a1, d1 + kGoreLength);
    junction.trims[static_cast<std::size_t>(a2)] = clampt(a2, d2 + kGoreLength);
    double speed = 0;
    if (const Edge* e = graph.edge(A(pi).edge))
        if (const RoadType* t = region.road_type(e->road_type)) speed = t->design_speed;
    if (speed <= 0) speed = A(pi).road_class / 3.6;
    junction.trims[static_cast<std::size_t>(oi)] = clampt(oi, merge_taper_length(speed));
    const auto& tr = junction.trims;

    // ---- connectors
    const std::vector<int> lo = ordered_lanes(junction, A(oi), !merge, overrides);
    const std::vector<int> lp = ordered_lanes(junction, A(pi), merge, overrides);
    const std::vector<int> lq = ordered_lanes(junction, A(qi), merge, overrides);
    struct Pair { int pair_arm, pair_lane, o_lane; };
    std::vector<Pair> pairs;
    std::vector<bool> o_fed(lo.size(), false);
    for (auto [x, y] : align(static_cast<int>(lp.size()), static_cast<int>(lo.size()), false)) {
        pairs.push_back({pi, lp[static_cast<std::size_t>(x)], lo[static_cast<std::size_t>(y)]});
        o_fed[static_cast<std::size_t>(y)] = true;
    }
    for (auto [x, y] : align(static_cast<int>(lq.size()), static_cast<int>(lo.size()), true)) {
        pairs.push_back({qi, lq[static_cast<std::size_t>(x)], lo[static_cast<std::size_t>(y)]});
        o_fed[static_cast<std::size_t>(y)] = true;
    }
    for (std::size_t y = 0; y < lo.size(); ++y)  // orphan O lanes: take the main carriageway's nearest lane
        if (!o_fed[y] && !lp.empty()) pairs.push_back({pi, lp[std::min(y, lp.size() - 1)], lo[y]});

    struct Conn { int af, lf, at, lt; };
    std::vector<Conn> cs;
    for (const Pair& p : pairs) {
        if (merge) cs.push_back({p.pair_arm, p.pair_lane, oi, p.o_lane});
        else cs.push_back({oi, p.o_lane, p.pair_arm, p.pair_lane});
    }
    auto key = [](const Conn& c) { return std::tie(c.af, c.lf, c.at, c.lt); };
    std::sort(cs.begin(), cs.end(), [&](const Conn& x, const Conn& y) { return key(x) < key(y); });
    cs.erase(std::unique(cs.begin(), cs.end(), [&](const Conn& x, const Conn& y) { return key(x) == key(y); }),
             cs.end());
    for (const Conn& c : cs) {
        const JunctionArm& fa = A(c.af);
        const JunctionArm& ta = A(c.at);
        const LaneRef& fl = fa.lanes[static_cast<std::size_t>(c.lf)];
        const LaneRef& tl = ta.lanes[static_cast<std::size_t>(c.lt)];
        LaneConnector lc;
        lc.arm_from = c.af;
        lc.lane_from = c.lf;
        lc.arm_to = c.at;
        lc.lane_to = c.lt;
        lc.kind = classify_turn(wrap_angle(ta.heading_out - (fa.heading_out + kPi)), junction.side);
        const std::vector<Vec3>* via = nullptr;
        auto it = overrides.connectors.find({junction.node, fl.edge, fl.slot, tl.edge, tl.slot});
        if (it != overrides.connectors.end()) {
            lc.enabled = it->second.enabled;
            lc.overridden = true;
            if (it->second.via) via = &*it->second.via;
        }
        lc.path = make_connector_path(fa, fl, tr[static_cast<std::size_t>(c.af)], ta, tl,
                                      tr[static_cast<std::size_t>(c.at)], via);
        junction.connectors.push_back(std::move(lc));
    }
    auto& cn = junction.connectors;
    for (std::size_t i = 0; i < cn.size(); ++i) {
        if (!cn[i].enabled) continue;
        for (std::size_t k = i + 1; k < cn.size(); ++k) {
            if (!cn[k].enabled) continue;
            if (cn[i].arm_from == cn[k].arm_from && cn[i].lane_from == cn[k].lane_from) continue;  // diverging
            const bool same_to = cn[i].arm_to == cn[k].arm_to && cn[i].lane_to == cn[k].lane_to;
            if (same_to || paths_cross(cn[i].path, cn[k].path, region.plan_crossing_junction_dz_m)) {
                cn[i].conflicts.push_back(static_cast<int>(k));
                cn[k].conflicts.push_back(static_cast<int>(i));
            }
        }
    }
    for (auto& c : cn) std::sort(c.conflicts.begin(), c.conflicts.end());

    // ---- corners (no fillets: the gore replaces them)
    for (std::size_t k = 0; k < n; ++k) {
        CornerCurve cc;
        cc.arm_right = static_cast<int>(k);
        cc.arm_left = static_cast<int>((k + 1) % n);
        cc.edge_r = arms[k].edge;
        cc.edge_l = arms[(k + 1) % n].edge;
        double ang = arms[(k + 1) % n].heading_out - arms[k].heading_out;
        while (ang <= 0) ang += 2 * kPi;
        cc.angle = ang;
        junction.corners.push_back(std::move(cc));
    }

    // ---- polygons
    using namespace Clipper2Lib;
    PathsD strips;
    for (int i = 0; i < 3; ++i) strips.push_back(to_path(arm_strip(A(i), 0, tr[static_cast<std::size_t>(i)])));
    PolygonSet cw = from_paths(Union(strips, FillRule::NonZero, 3));
    if (cw.empty()) cw = from_paths(strips);
    junction.carriageway = cw;

    // Gore: triangle from the nose point to the inner kerbs at the trim lines, if the kerbs separate in range.
    const double t1 = tr[static_cast<std::size_t>(a1)], t2 = tr[static_cast<std::size_t>(a2)];
    if (d1 < t1 - 0.05 && d2 < t2 - 0.05) {
        Ring g{arm_point(A(a1), d1, A(a1).kerb_left).pos.xy(), arm_point(A(a1), t1, A(a1).kerb_left).pos.xy(),
               arm_point(A(a2), t2, -A(a2).kerb_right).pos.xy()};
        if (area(g) < 0) std::reverse(g.begin(), g.end());
        if (area(g) > 1e-6) junction.gore.push_back(std::move(g));
    }

    double delta = 0;
    for (const auto& a : arms)
        delta = std::max({delta, a.half_width_left - a.kerb_left, a.half_width_right - a.kerb_right});
    if (delta > 1e-3 && !cw.empty()) {
        PathsD inner = to_paths(cw);
        for (const Ring& g : junction.gore) inner.push_back(to_path(g));
        inner = Union(inner, FillRule::NonZero, 3);
        const PathsD outer = InflatePaths(inner, delta, JoinType::Miter, EndType::Polygon, 2.0, 3);
        PathsD caps;
        for (int i = 0; i < 3; ++i) caps.push_back(to_path(end_cap(A(i), tr[static_cast<std::size_t>(i)], delta)));
        const PathsD cut = Union(inner, caps, FillRule::NonZero, 3);
        junction.pavements = from_paths(Difference(outer, cut, FillRule::NonZero, 3));
    }
}

}  // namespace cp
