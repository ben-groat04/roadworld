// [A] Lane connectors (docs/junctions.md section 4): lane distribution by turn kind,
// clothoid paths, conflicts, overrides.
#include <algorithm>
#include <cassert>
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

bool paths_cross(const std::vector<Vec3>& p, const std::vector<Vec3>& q, double dz) {
    for (std::size_t i = 0; i + 1 < p.size(); ++i) {
        for (std::size_t j = 0; j + 1 < q.size(); ++j) {
            double t, u;
            if (!seg_cross(p[i].xy(), p[i + 1].xy(), q[j].xy(), q[j + 1].xy(), t, u)) continue;
            const double zp = p[i].z + (p[i + 1].z - p[i].z) * t;
            const double zq = q[j].z + (q[j + 1].z - q[j].z) * u;
            if (std::fabs(zp - zq) < dz) return true;
        }
    }
    return false;
}

}  // namespace

namespace {

// One end of a connector. On a flared arm the lane runs along its shifted line from the trim line in to where
// the spread is full ("inner"); the connector curve proper starts there. `run` holds the points from the trim
// line (first) up to, not including, the inner point.
struct ConnEnd {
    ArmPoint inner;
    std::vector<Vec2> run;
};

ConnEnd conn_end(const JunctionArm& arm, const LaneRef& lane, double trim) {
    ConnEnd e;
    e.inner = arm_point(arm, trim, lane.lateral);
    const double spread = flare_spread(arm, 0.0, lane.lateral > 0);
    if (arm.flare.active() && spread > 0 && lane.lateral != 0 && trim > arm.flare.full + 1e-6) {
        const double s_in = arm.flare.full;
        e.inner = arm_point(arm, s_in, flared_lateral(arm, s_in, lane.lateral));
        for (double s = trim; s > s_in + 0.5; s -= 1.0)
            e.run.push_back(arm_point(arm, s, flared_lateral(arm, s, lane.lateral)).pos.xy());
        e.run.front() = arm_point(arm, trim, lane.lateral).pos.xy();
    }
    return e;
}

}  // namespace

std::vector<Vec3> make_connector_path(const JunctionArm& from_arm, const LaneRef& from_lane, double trim_from,
                                      const JunctionArm& to_arm, const LaneRef& to_lane, double trim_to,
                                      const std::vector<Vec3>* via) {
    const ArmPoint a = arm_point(from_arm, trim_from, from_lane.lateral);
    const ArmPoint b = arm_point(to_arm, trim_to, to_lane.lateral);
    const ConnEnd fe = conn_end(from_arm, from_lane, trim_from), te = conn_end(to_arm, to_lane, trim_to);
    const double h0 = fe.inner.heading_out + kPi, h1 = te.inner.heading_out;
    std::vector<Vec2> pts{fe.inner.pos.xy()};
    std::vector<double> hs{h0};
    if (via) {
        for (std::size_t i = 0; i < via->size(); ++i) pts.push_back((*via)[i].xy());
    }
    pts.push_back(te.inner.pos.xy());
    hs.resize(pts.size());
    hs.back() = h1;
    for (std::size_t i = 1; i + 1 < pts.size(); ++i) {
        const Vec2 d = pts[i + 1] - pts[i - 1];
        hs[i] = std::atan2(d.y, d.x);
    }
    std::vector<Vec2> xy = fe.run;
    if (!xy.empty()) xy.push_back(pts[0]);
    for (std::size_t i = 0; i + 1 < pts.size(); ++i) append_segment(xy, pts[i], hs[i], pts[i + 1], hs[i + 1]);
    for (auto it = te.run.rbegin(); it != te.run.rend(); ++it) xy.push_back(*it);
    std::vector<double> cum(xy.size(), 0.0);
    for (std::size_t i = 1; i < xy.size(); ++i) cum[i] = cum[i - 1] + (xy[i] - xy[i - 1]).length();
    const double total = cum.back() > 0 ? cum.back() : 1.0;
    std::vector<Vec3> out;
    out.reserve(xy.size());
    for (std::size_t i = 0; i < xy.size(); ++i)
        out.push_back({xy[i].x, xy[i].y, a.pos.z + (b.pos.z - a.pos.z) * cum[i] / total});
    return out;
}

void generate_connectors(Junction& junction, DrivingSide side, const JunctionOverrides& overrides,
                         const RegionPack& region) {
    junction.connectors.clear();
    const auto& arms = junction.arms;
    const int n = static_cast<int>(arms.size());
    // Contract: generate_connectors is called with trims set (one per arm).
    assert(junction.trims.size() == arms.size());
    auto has_trim = [&](int i) { return static_cast<std::size_t>(i) < junction.trims.size(); };
    auto trim_of = [&](int i) { return junction.trims[static_cast<std::size_t>(i)]; };
    auto lane_on = [&](int arm, int lane) {
        const auto it = overrides.lane_connects.find(
            {junction.node, arms[static_cast<std::size_t>(arm)].edge,
             arms[static_cast<std::size_t>(arm)].lanes[static_cast<std::size_t>(lane)].slot});
        return it == overrides.lane_connects.end() || it->second;
    };
    // All lanes per arm in travel order (travel-left first): inbound lanes run against
    // heading_out, so their travel-left-to-right order is the reverse of the arm's list.
    // lane_connects=false lanes stay in the distribution (so the other lanes keep their
    // connectors) and their connectors are emitted disabled.
    std::vector<LaneIdx> in(static_cast<std::size_t>(n)), out(static_cast<std::size_t>(n));
    for (int a = 0; a < n; ++a) {
        const auto& lanes = arms[static_cast<std::size_t>(a)].lanes;
        for (int l = static_cast<int>(lanes.size()) - 1; l >= 0; --l)
            if (lanes[static_cast<std::size_t>(l)].inbound) in[static_cast<std::size_t>(a)].push_back(l);
        for (int l = 0; l < static_cast<int>(lanes.size()); ++l)
            if (!lanes[static_cast<std::size_t>(l)].inbound) out[static_cast<std::size_t>(a)].push_back(l);
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

        // Acute forks: a lane the distribution left without a connector (its only exits were
        // classified as U-turns) falls back to the nearest non-self exit.
        int best = -1;
        double best_turn = 1e9;
        for (int t = 0; t < n; ++t) {
            if (t == f || out[static_cast<std::size_t>(t)].empty()) continue;
            const double turn = std::fabs(wrap_angle(arms[static_cast<std::size_t>(t)].heading_out -
                                                     (arms[static_cast<std::size_t>(f)].heading_out + kPi)));
            if (turn < best_turn) {
                best_turn = turn;
                best = t;
            }
        }
        if (best < 0) continue;
        const LaneIdx& outl = out[static_cast<std::size_t>(best)];
        for (int i = 0; i < L; ++i) {
            const int lane = inl[static_cast<std::size_t>(i)];
            const bool has = std::any_of(cands.begin(), cands.end(),
                                         [&](const Cand& c) { return c.af == f && c.lf == lane; });
            if (!has)
                cands.insert({f, lane, best, outl[static_cast<std::size_t>(i * static_cast<int>(outl.size()) / L)]});
        }
    }
    // Likewise an outbound lane nothing reaches is fed from the nearest other arm's inbound lanes.
    for (int t = 0; t < n; ++t) {
        const LaneIdx& outl = out[static_cast<std::size_t>(t)];
        const int M = static_cast<int>(outl.size());
        for (int j = 0; j < M; ++j) {
            const int lane = outl[static_cast<std::size_t>(j)];
            if (std::any_of(cands.begin(), cands.end(), [&](const Cand& c) { return c.at == t && c.lt == lane; }))
                continue;
            int best = -1;
            double best_turn = 1e9;
            for (int f = 0; f < n; ++f) {
                if (f == t || in[static_cast<std::size_t>(f)].empty()) continue;
                const double turn = std::fabs(wrap_angle(arms[static_cast<std::size_t>(t)].heading_out -
                                                         (arms[static_cast<std::size_t>(f)].heading_out + kPi)));
                if (turn < best_turn) {
                    best_turn = turn;
                    best = f;
                }
            }
            if (best < 0) continue;
            const LaneIdx& inl = in[static_cast<std::size_t>(best)];
            cands.insert({best, inl[static_cast<std::size_t>(j * static_cast<int>(inl.size()) / M)], t, lane});
        }
    }

    // Enabling overrides can add connectors the distribution did not make (incl. U-turns).
    // A loop edge (a == b) has two arms with one edge id: the key's at_a flag tells them apart.
    // For other edges the flag is ignored.
    auto is_loop = [&](EdgeId e) {
        return std::count_if(arms.begin(), arms.end(), [&](const JunctionArm& a) { return a.edge == e; }) > 1;
    };
    auto find_lane = [&](EdgeId e, bool at_a, int slot, bool inbound, int& arm, int& lane) {
        const bool loop = is_loop(e);
        for (int a = 0; a < n; ++a) {
            if (!(arms[static_cast<std::size_t>(a)].edge == e)) continue;
            if (loop && arms[static_cast<std::size_t>(a)].at_a != at_a) continue;
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
        if (find_lane(key.from_edge, key.from_at_a, key.from_slot, true, af, lf) &&
            find_lane(key.to_edge, key.to_at_a, key.to_slot, false, at, lt))
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
        if (!has_trim(c.af) || !has_trim(c.at)) continue;  // contract violation (asserted above)
        const bool from_loop = is_loop(fa.edge), to_loop = is_loop(ta.edge);
        const auto it = overrides.connectors.find({junction.node, fa.edge, fl.slot, ta.edge, tl.slot,
                                                   from_loop ? fa.at_a : true, to_loop ? ta.at_a : true});
        const std::vector<Vec3>* via = nullptr;
        if (it != overrides.connectors.end()) {
            lc.overridden = true;
            lc.enabled = it->second.enabled;
            if (it->second.via) via = &*it->second.via;
        }
        if (!lane_on(c.af, c.lf) || !lane_on(c.at, c.lt)) {
            lc.overridden = true;
            lc.enabled = false;
        }
        lc.path = make_connector_path(fa, fl, trim_of(c.af), ta, tl, trim_of(c.at), via);
        if (!via && lc.kind == TurnKind::Offside) {
            // Offside turn out of a turn pocket: the vehicle keeps its lane to the start of the pocket,
            // changes into the pocket over the first part of it (smoothstep, tangent at both ends), then turns.
            for (const TurnPocket& p : junction.pockets) {
                if (p.arm != c.af || p.to_arm != c.at || !fa.flare.active()) continue;
                const double side = fa.flare.spread_left > fa.flare.spread_right ? 1.0 : -1.0;
                const double lat_p = side * p.lane_width / 2;
                const double s0 = p.storage_length, lc_len = std::min(12.0, s0 * 0.6), s_p = s0 - lc_len;
                if (lc_len < 1.0 || trim_of(c.af) <= s0) break;
                std::vector<Vec3> path;
                for (double s = trim_of(c.af); s > s0; s -= 1.0)
                    path.push_back(arm_point(fa, s, flared_lateral(fa, s, fl.lateral)).pos);
                const int steps = std::max(4, static_cast<int>(std::ceil(lc_len / 1.0)));
                for (int i = 0; i < steps; ++i) {
                    const double t = static_cast<double>(i) / steps, w = t * t * (3 - 2 * t);
                    const double s = s0 - lc_len * t;
                    path.push_back(arm_point(fa, s, fl.lateral + (lat_p - fl.lateral) * w).pos);
                }
                LaneRef pocket_lane = fl;
                pocket_lane.lateral = lat_p;
                const std::vector<Vec3> rest = make_connector_path(fa, pocket_lane, s_p, ta, tl, trim_of(c.at), nullptr);
                if (rest.size() < 2) break;
                path.front() = lc.path.front();
                path.insert(path.end(), rest.begin(), rest.end());
                lc.path = std::move(path);
                lc.via_pocket = true;
                break;
            }
        }
        junction.connectors.push_back(std::move(lc));
    }

    auto& cs = junction.connectors;
    for (std::size_t i = 0; i < cs.size(); ++i) {
        if (!cs[i].enabled) continue;
        for (std::size_t j = i + 1; j < cs.size(); ++j) {
            if (!cs[j].enabled) continue;
            if (cs[i].arm_from == cs[j].arm_from && cs[i].lane_from == cs[j].lane_from) continue;
            const bool merge = cs[i].arm_to == cs[j].arm_to && cs[i].lane_to == cs[j].lane_to;
            if (merge || paths_cross(cs[i].path, cs[j].path, region.plan_crossing_junction_dz_m)) {
                cs[i].conflicts.push_back(static_cast<int>(j));
                cs[j].conflicts.push_back(static_cast<int>(i));
            }
        }
    }
    for (auto& c : cs) std::sort(c.conflicts.begin(), c.conflicts.end());
}

}  // namespace cp
