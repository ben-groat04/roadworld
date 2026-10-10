// [A] Lane connectors (docs/junctions.md section 4): lane distribution by turn kind,
// clothoid paths, conflicts, overrides.
#include <algorithm>
#include <cmath>
#include <set>
#include <tuple>

#include "cp/clothoid.h"
#include "cp/junction.h"

namespace cp {

TurnKind classify_turn(double turn, DrivingSide side) {
    const double t = wrap_angle(turn);
    const double a = std::fabs(t);
    if (a < kPi / 6) return TurnKind::Straight;
    if (a > kPi * 5 / 6) return TurnKind::UTurn;
    const bool left = t > 0;
    const bool nearside = left == (side == DrivingSide::Left);
    return nearside ? TurnKind::Nearside : TurnKind::Offside;
}

bool turn_is_left(TurnKind kind, DrivingSide side) {
    return (kind == TurnKind::Nearside) == (side == DrivingSide::Left);
}

namespace {

// region.plan_crossing_junction_dz_m is not reachable from generate_connectors' signature;
// this mirrors the pack value (1.0 m).
constexpr double kPlanCrossingDz = 1.0;

using LaneIdx = std::vector<int>;

// Clothoid G1 segment sampled finely at both ends (so the polyline tangent matches the lane
// tangent to well under a degree) and every ~0.5 m between.
void append_segment(std::vector<Vec2>& out, Vec2 p0, double h0, Vec2 p1, double h1) {
    const auto c = fit_clothoid_g1(p0, h0, p1, h1);
    if (!c || c->length < 1e-6) {
        if (out.empty()) out.push_back(p0);
        out.push_back(p1);
        return;
    }
    const double len = c->length;
    std::vector<double> ss{0.0};
    if (len > 0.4) {
        ss.push_back(0.05);
        const double a = 0.05, b = len - 0.05;
        const int n = std::max(1, static_cast<int>(std::ceil((b - a) / 0.5)));
        for (int i = 1; i < n; ++i) ss.push_back(a + (b - a) * i / n);
        ss.push_back(b);
    }
    ss.push_back(len);
    if (out.empty()) out.push_back(p0);
    for (std::size_t i = 1; i < ss.size(); ++i) out.push_back(c->point(ss[i]));
    out.back() = p1;
}

// Pairs of (inbound index, outbound index) into the two ordered lane lists; monotone, covers every lane.
std::vector<std::pair<int, int>> pair_lanes(int a, int b) {
    std::set<std::pair<int, int>> s;
    for (int i = 0; i < a; ++i) s.insert({i, i * b / a});
    for (int j = 0; j < b; ++j) s.insert({j * a / b, j});
    return {s.begin(), s.end()};
}

// Lane sub-range [lo, hi] of [0, r) for exit i of k (contiguous when r >= k, shared otherwise).
std::pair<int, int> split_range(int r, int k, int i) {
    const int lo = i * r / k;
    const int hi = std::max(lo, (i + 1) * r / k + (((i + 1) * r) % k ? 1 : 0) - 1);
    return {lo, std::min(hi, r - 1)};
}

struct Cand {
    int af, lf, at, lt;
    bool operator<(const Cand& o) const {
        return std::tie(af, lf, at, lt) < std::tie(o.af, o.lf, o.at, o.lt);
    }
};

bool seg_cross(Vec2 a, Vec2 b, Vec2 c, Vec2 d, double& t, double& u) {
    const Vec2 r = b - a, q = d - c;
    const double den = r.cross(q);
    if (std::fabs(den) < 1e-12) return false;
    t = (c - a).cross(q) / den;
    u = (c - a).cross(r) / den;
    return t >= 0 && t <= 1 && u >= 0 && u <= 1;
}

bool paths_cross(const std::vector<Vec3>& p, const std::vector<Vec3>& q) {
    for (std::size_t i = 0; i + 1 < p.size(); ++i) {
        for (std::size_t j = 0; j + 1 < q.size(); ++j) {
            double t, u;
            if (!seg_cross(p[i].xy(), p[i + 1].xy(), q[j].xy(), q[j + 1].xy(), t, u)) continue;
            const double zp = p[i].z + (p[i + 1].z - p[i].z) * t;
            const double zq = q[j].z + (q[j + 1].z - q[j].z) * u;
            if (std::fabs(zp - zq) < kPlanCrossingDz) return true;
        }
    }
    return false;
}

}  // namespace

std::vector<Vec3> make_connector_path(const JunctionArm& from_arm, const LaneRef& from_lane, double trim_from,
                                      const JunctionArm& to_arm, const LaneRef& to_lane, double trim_to,
                                      const std::vector<Vec3>* via) {
    const ArmPoint a = arm_point(from_arm, trim_from, from_lane.lateral);
    const ArmPoint b = arm_point(to_arm, trim_to, to_lane.lateral);
    const double h0 = a.heading_out + kPi, h1 = b.heading_out;
    std::vector<Vec2> pts{a.pos.xy()};
    std::vector<double> hs{h0};
    if (via) {
        for (std::size_t i = 0; i < via->size(); ++i) pts.push_back((*via)[i].xy());
    }
    pts.push_back(b.pos.xy());
    hs.resize(pts.size());
    hs.back() = h1;
    for (std::size_t i = 1; i + 1 < pts.size(); ++i) {
        const Vec2 d = pts[i + 1] - pts[i - 1];
        hs[i] = std::atan2(d.y, d.x);
    }
    std::vector<Vec2> xy;
    for (std::size_t i = 0; i + 1 < pts.size(); ++i) append_segment(xy, pts[i], hs[i], pts[i + 1], hs[i + 1]);
    std::vector<double> cum(xy.size(), 0.0);
    for (std::size_t i = 1; i < xy.size(); ++i) cum[i] = cum[i - 1] + (xy[i] - xy[i - 1]).length();
    const double total = cum.back() > 0 ? cum.back() : 1.0;
    std::vector<Vec3> out;
    out.reserve(xy.size());
    for (std::size_t i = 0; i < xy.size(); ++i)
        out.push_back({xy[i].x, xy[i].y, a.pos.z + (b.pos.z - a.pos.z) * cum[i] / total});
    return out;
}

void generate_connectors(Junction& junction, DrivingSide side, const JunctionOverrides& overrides) {
    junction.connectors.clear();
    const auto& arms = junction.arms;
    const int n = static_cast<int>(arms.size());
    auto trim_of = [&](int i) {
        return static_cast<std::size_t>(i) < junction.trims.size() ? junction.trims[static_cast<std::size_t>(i)] : 0.0;
    };
    auto lane_on = [&](int arm, int lane) {
        const auto it = overrides.lane_connects.find(
            {junction.node, arms[static_cast<std::size_t>(arm)].edge,
             arms[static_cast<std::size_t>(arm)].lanes[static_cast<std::size_t>(lane)].slot});
        return it == overrides.lane_connects.end() || it->second;
    };
    // Usable lanes per arm in travel order (travel-left first): inbound lanes run against
    // heading_out, so their travel-left-to-right order is the reverse of the arm's list.
    std::vector<LaneIdx> in(static_cast<std::size_t>(n)), out(static_cast<std::size_t>(n));
    for (int a = 0; a < n; ++a) {
        const auto& lanes = arms[static_cast<std::size_t>(a)].lanes;
        for (int l = static_cast<int>(lanes.size()) - 1; l >= 0; --l)
            if (lanes[static_cast<std::size_t>(l)].inbound && lane_on(a, l)) in[static_cast<std::size_t>(a)].push_back(l);
        for (int l = 0; l < static_cast<int>(lanes.size()); ++l)
            if (!lanes[static_cast<std::size_t>(l)].inbound && lane_on(a, l)) out[static_cast<std::size_t>(a)].push_back(l);
    }

    std::set<Cand> cands;
    for (int f = 0; f < n; ++f) {
        const LaneIdx& inl = in[static_cast<std::size_t>(f)];
        const int L = static_cast<int>(inl.size());
        if (L == 0) continue;
        struct Exit { int arm; double turn; };
        std::vector<Exit> left, straight, right;
        for (int t = 0; t < n; ++t) {
            if (t == f || out[static_cast<std::size_t>(t)].empty()) continue;
            const double turn = wrap_angle(arms[static_cast<std::size_t>(t)].heading_out -
                                           (arms[static_cast<std::size_t>(f)].heading_out + kPi));
            const TurnKind k = classify_turn(turn, side);
            if (k == TurnKind::UTurn) continue;
            (k == TurnKind::Straight ? straight : turn > 0 ? left : right).push_back({t, turn});
        }
        auto by_turn = [](const Exit& x, const Exit& y) { return x.turn > y.turn; };
        std::sort(left.begin(), left.end(), by_turn);
        std::sort(straight.begin(), straight.end(), by_turn);
        std::sort(right.begin(), right.end(), by_turn);
        const bool hl = !left.empty(), hs = !straight.empty(), hr = !right.empty();
        std::pair<int, int> lr{0, L - 1}, sr{0, L - 1}, rr{0, L - 1};
        if (L > 1) {
            if (hs) {
                lr = {0, 0};
                rr = {L - 1, L - 1};
            } else if (hl && hr) {
                const int nl = (L + 1) / 2;
                lr = {0, nl - 1};
                rr = {nl, L - 1};
            }
        }
        auto emit = [&](const std::vector<Exit>& g, std::pair<int, int> range) {
            const int k = static_cast<int>(g.size());
            const int r = range.second - range.first + 1;
            for (int i = 0; i < k; ++i) {
                const auto sub = split_range(r, k, i);
                const int t = g[static_cast<std::size_t>(i)].arm;
                const LaneIdx& outl = out[static_cast<std::size_t>(t)];
                const int na = sub.second - sub.first + 1;
                for (auto [ia, ob] : pair_lanes(na, static_cast<int>(outl.size())))
                    cands.insert({f, inl[static_cast<std::size_t>(range.first + sub.first + ia)], t,
                                  outl[static_cast<std::size_t>(ob)]});
            }
        };
        emit(left, lr);
        emit(straight, sr);
        emit(right, rr);
    }

    // Enabling overrides can add connectors the distribution did not make (incl. U-turns).
    auto find_lane = [&](EdgeId e, int slot, bool inbound, int& arm, int& lane) {
        for (int a = 0; a < n; ++a) {
            if (!(arms[static_cast<std::size_t>(a)].edge == e)) continue;
            const auto& lanes = arms[static_cast<std::size_t>(a)].lanes;
            for (int l = 0; l < static_cast<int>(lanes.size()); ++l)
                if (lanes[static_cast<std::size_t>(l)].slot == slot && lanes[static_cast<std::size_t>(l)].inbound == inbound) {
                    arm = a;
                    lane = l;
                    return true;
                }
        }
        return false;
    };
    for (const auto& [key, ov] : overrides.connectors) {
        if (!(key.node == junction.node) || !ov.enabled) continue;
        int af, lf, at, lt;
        if (find_lane(key.from_edge, key.from_slot, true, af, lf) && find_lane(key.to_edge, key.to_slot, false, at, lt))
            cands.insert({af, lf, at, lt});
    }

    for (const Cand& c : cands) {
        const JunctionArm& fa = arms[static_cast<std::size_t>(c.af)];
        const JunctionArm& ta = arms[static_cast<std::size_t>(c.at)];
        const LaneRef& fl = fa.lanes[static_cast<std::size_t>(c.lf)];
        const LaneRef& tl = ta.lanes[static_cast<std::size_t>(c.lt)];
        LaneConnector lc;
        lc.arm_from = c.af;
        lc.lane_from = c.lf;
        lc.arm_to = c.at;
        lc.lane_to = c.lt;
        lc.kind = classify_turn(wrap_angle(ta.heading_out - (fa.heading_out + kPi)), side);
        const auto it = overrides.connectors.find({junction.node, fa.edge, fl.slot, ta.edge, tl.slot});
        const std::vector<Vec3>* via = nullptr;
        if (it != overrides.connectors.end()) {
            lc.overridden = true;
            lc.enabled = it->second.enabled;
            if (it->second.via) via = &*it->second.via;
        }
        lc.path = make_connector_path(fa, fl, trim_of(c.af), ta, tl, trim_of(c.at), via);
        junction.connectors.push_back(std::move(lc));
    }

    auto& cs = junction.connectors;
    for (std::size_t i = 0; i < cs.size(); ++i) {
        if (!cs[i].enabled) continue;
        for (std::size_t j = i + 1; j < cs.size(); ++j) {
            if (!cs[j].enabled) continue;
            if (cs[i].arm_from == cs[j].arm_from && cs[i].lane_from == cs[j].lane_from) continue;
            const bool merge = cs[i].arm_to == cs[j].arm_to && cs[i].lane_to == cs[j].lane_to;
            if (merge || paths_cross(cs[i].path, cs[j].path)) {
                cs[i].conflicts.push_back(static_cast<int>(j));
                cs[j].conflicts.push_back(static_cast<int>(i));
            }
        }
    }
    for (auto& c : cs) std::sort(c.conflicts.begin(), c.conflicts.end());
}

}  // namespace cp
