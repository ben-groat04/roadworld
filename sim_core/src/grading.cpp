// Ground grading: port of prototypes/road-draw/src/earthworks.js (see grading.h).
//
// Behaviour is reproduced, including these oddities of the prototype (all kept on purpose):
//  * gradeTile keeps per-road intermediate values in Float32 arrays and reads them back, so the
//    distance, formation height and wall weight used per texel are float-rounded (mirrored here
//    with float arrays).
//  * Wall type / weight of a texel come from ONE nearest segment of each road (hard flip at the
//    segment midpoint), not interpolated.
//  * slopeReach was cached per road version in the JS and not invalidated by median or terrain
//    changes; here it is recomputed from the current flags (the intended value).
//  * The prototype culls tiles with pair-of-segment boxes built from the end points only; here
//    the middle sample is included so a bend cannot poke out of its box (culling cannot change
//    results otherwise).
//  * In the JS the plane test position is rebuilt as (rx + A.x), not x0 + i * res: kept.
//  * A tunnel whose portal is the road's first or last station gets no portal plane (the JS looks
//    at the neighbouring decimated sample, which does not exist): kept.
//  * The JS classifies with hBase (triangle interpolation of the coarse grid) but grades against
//    hBaseFine (bicubic). Here one GroundFn serves classification and sample_natural_tile
//    serves grading; the caller decides whether they are the same surface.
//  * The old JS walled 24 m before every portal / abutment although its comment said it did
//    not. Both are gone: the approach is an ordinary slope plus a short wing wall (wing_max).
//  * The JS portal plane test covered a fixed 10 decimated segments, shorter than the road's
//    reach once reach grew; it now covers (reach + plane_margin) of path length.
//  * Imported (interchange) roads in the JS never got wall weights, so their wall ends were
//    abrupt; every road gets them now.
#include "cp/grading.h"

#include <algorithm>
#include <cmath>
#include <utility>

#include "cp/lanes.h"

namespace cp {

GradingParams GradingParams::from_rules(const GradingRules& r) {
    GradingParams p;
    p.formation_drop = r.formation_drop;
    p.batter = r.batter;
    p.verge = r.verge;
    p.wall_thickness = r.wall_thickness;
    p.wall_start = r.wall_start;
    p.wall_cap = r.wall_cap;
    p.wall_slack = r.wall_slack;
    p.tunnel.clear_height = r.tunnel_clear_height;
    p.wall_max_height = r.wall_max_height;
    p.bench_min_width = r.bench_min_width;
    p.wall_min_length = r.wall_min_length;
    p.toe_ditch_depth = r.toe_ditch_depth;
    p.toe_ditch_width = r.toe_ditch_width;
    p.end_taper_batter = r.end_taper_batter;
    p.wing_max = r.wing_max;
    p.wing_ratio = r.wing_ratio;
    p.wing_min_h = r.wing_min_height;
    p.round_daylight = r.round_daylight;
    p.round_hinge = r.round_hinge;
    return p;
}

namespace {

using Flags = std::vector<char>;

// Fill false runs shorter than max_gap that sit between true samples.
void close_gaps(Flags& a, int max_gap) {
    const int n = static_cast<int>(a.size());
    int i = 0;
    while (i < n) {
        if (a[static_cast<std::size_t>(i)]) { ++i; continue; }
        int j = i;
        while (j < n && !a[static_cast<std::size_t>(j)]) ++j;
        if (i > 0 && j < n && j - i < max_gap)
            for (int k = i; k < j; ++k) a[static_cast<std::size_t>(k)] = 1;
        i = j;
    }
}

void drop_short(Flags& a, int min_run) {
    const int n = static_cast<int>(a.size());
    int i = 0;
    while (i < n) {
        if (!a[static_cast<std::size_t>(i)]) { ++i; continue; }
        int j = i;
        while (j < n && a[static_cast<std::size_t>(j)]) ++j;
        if (j - i < min_run)
            for (int k = i; k < j; ++k) a[static_cast<std::size_t>(k)] = 0;
        i = j;
    }
}

// Pad true runs by k samples each way (stops at locked samples and array ends).
void dilate(Flags& a, int k, const Flags& lock) {
    const int n = static_cast<int>(a.size());
    Flags o = a;
    for (int i = 0; i < n; ++i) {
        if (!a[static_cast<std::size_t>(i)]) continue;
        for (const int dir : {-1, 1})
            for (int d = 1; d <= k; ++d) {
                const int j = i + dir * d;
                if (j < 0 || j >= n || lock[static_cast<std::size_t>(j)]) break;
                o[static_cast<std::size_t>(j)] = 1;
            }
    }
    a = o;
}

// Grow every true run shorter than min_run to min_run samples (both ways, never into locked
// samples). Runs only grow: dropping one would leave a slope that cannot daylight.
void grow_short(Flags& a, int min_run, const Flags& lock) {
    const int n = static_cast<int>(a.size());
    int i = 0;
    while (i < n) {
        if (!a[static_cast<std::size_t>(i)]) { ++i; continue; }
        int j = i;
        while (j < n && a[static_cast<std::size_t>(j)]) ++j;
        int need = min_run - (j - i), lo = i, hi = j;  // [lo, hi)
        while (need > 0) {
            const bool can_l = lo > 0 && !lock[static_cast<std::size_t>(lo - 1)];
            const bool can_r = hi < n && !lock[static_cast<std::size_t>(hi)];
            if (!can_l && !can_r) break;
            if (can_l && (!can_r || (hi - lo) % 2 == 0)) {
                --lo;
                a[static_cast<std::size_t>(lo)] = 1;
            } else {
                a[static_cast<std::size_t>(hi)] = 1;
                ++hi;
            }
            --need;
        }
        i = std::max(j, hi);
    }
}

double slope_v(double e, double verge) { return e > verge ? e - verge : 0.0; }

// Extra grading reach for the rolled-over daylight and the hinge shift.
double round_reach(const GradingParams& p) { return p.round_daylight + p.round_hinge / 2; }

// Reach cap: farthest lateral distance a wall-start slope can need.
double reach_cap(double hw, const GradingParams& p) {
    return hw + p.verge + (p.wall_start + p.wall_slack) * p.batter + p.reach_margin + round_reach(p) + p.toe_ditch_width;
}

// Same beyond a free road end, where the run-out batter is gentler.
double end_reach_cap(double hw, const GradingParams& p) {
    return hw + p.verge + (p.wall_start + p.wall_slack) * std::max(p.batter, p.end_taper_batter) + p.reach_margin +
           round_reach(p) + p.toe_ditch_width;
}

// Ground as seen by a road with its neighbours' plain slopes in place: terrain, overridden by a
// neighbour's batter slope (cut: lower, fill: higher). Two slopes therefore meet in a valley /
// ridge between roads. Where the walk meets a neighbour's FOOTPRINT instead, the slope only
// lands on it (fits) if it arrives on the right side of that road's formation: a cut slope at or
// above it, a fill slope at or below it. Otherwise the neighbour sits on a bench the slope would
// pass under (and carve the hillside beyond), so it does not fit and the wall stays.
struct NeighbourGround {
    struct Q {
        const SampledRoad* r;
        Box2 box;
    };
    std::vector<Q> near;
    const GroundFn* terrain = nullptr;
    const GradingParams* p = nullptr;

    // lateral distance beyond the neighbour's footprint edge and its formation level there
    static bool nearest(const Q& q, double x, double y, double& e, double& bz_out, const GradingParams& p, double* dir = nullptr) {
        if (x < q.box.x0 || x > q.box.x1 || y < q.box.y0 || y > q.box.y1) return false;
        const std::vector<RoadSample>& S = q.r->samples;
        double bd = 1e18, bz = 0, ux = 1, uy = 0;
        for (std::size_t i = 0; i + 1 < S.size(); ++i) {
            const RoadSample &A = S[i], &B = S[i + 1];
            if (A.br || A.tu || B.br || B.tu) continue;
            const double dx = B.x - A.x, dy = B.y - A.y, L2 = dx * dx + dy * dy, px = x - A.x, py = y - A.y;
            double u = L2 > 1e-12 ? (px * dx + py * dy) / L2 : 0.0;
            u = u < 0 ? 0 : u > 1 ? 1 : u;
            const double ex = px - u * dx, ey = py - u * dy, d2 = ex * ex + ey * ey;
            if (d2 < bd) {
                bd = d2;
                bz = A.z + u * (B.z - A.z) - p.formation_drop;
                const double Ln = std::sqrt(L2);
                ux = dx / (Ln != 0 ? Ln : 1.0);
                uy = dy / (Ln != 0 ? Ln : 1.0);
            }
        }
        if (bd > 1e17) return false;
        e = std::sqrt(bd) - q.r->half_width;
        bz_out = bz;
        if (dir) { dir[0] = ux; dir[1] = uy; }
        return true;
    }
    double operator()(double x, double y) const {
        const double t = (*terrain)(x, y);
        double g = t;
        for (const Q& q : near) {
            double e = 0, bz = 0;
            if (!nearest(q, x, y, e, bz, *p)) continue;
            if (e <= 0) { g = bz; continue; }
            if (t > bz) {
                const double v = bz + slope_v(e, p->verge) / p->batter;
                if (v < g) g = v;
            } else {
                const double v = bz - slope_v(e, p->verge) / p->batter;
                if (v > g) g = v;
            }
        }
        return g;
    }
    // unit direction of the neighbour whose footprint contains (x, y)
    bool contact_dir(double x, double y, double* dir) const {
        for (const Q& q : near) {
            double e = 0, bz = 0;
            if (nearest(q, x, y, e, bz, *p, dir) && e <= 0) return true;
        }
        return false;
    }
    // formation level of the neighbour whose footprint contains (x, y)
    bool contact(double x, double y, double& bz) const {
        for (const Q& q : near) {
            double e = 0;
            if (nearest(q, x, y, e, bz, *p) && e <= 0) return true;
        }
        return false;
    }
};

// ---- wall plans (port of earthworks.js slopeFits / solveTiers / ownerTiers / planSide) ----

using Probe = std::function<double(double)>;           // ground height at lateral e beyond the footprint edge
using Contact = std::function<bool(double, double&)>;  // neighbour formation level at lateral e, false when none

// Does the 1:batter slope from level L (starting at lateral s0) daylight within E, or land on a neighbour's
// footprint the right way?
bool slope_fits(bool cut, double L, double s0, double E, double bt, const Probe& grd, const Contact* cn, double from) {
    for (double e = from; e < E + 0.5; e += 0.5) {
        const double ee = std::min(e, E), run = ee > s0 ? ee - s0 : 0.0;
        const double zs = cut ? L + run / bt : L - run / bt;
        double bz = 0;
        if (cn && (*cn)(ee, bz)) return cut ? zs >= bz : zs <= bz;  // landed on a neighbour's footprint
        if (cut ? zs >= grd(ee) : zs <= grd(ee)) return true;
        if (ee >= E) break;
    }
    return false;
}

struct TierResult {
    enum Kind { None, Tiers, Fail } kind = None;
    TierSet t;
};

// Tiers for one side. minTiers forces at least that many tiers (zero height ones pad).
TierResult solve_tiers(bool cut, double z0, double E, double bt, const Probe& grd, const Contact* cn, int min_tiers,
                       const GradingParams& p, double cap) {
    TierResult r;
    TierSet& t = r.t;
    t.cut = cut;
    const double T = p.wall_thickness;
    double L = z0;
    bool nat = false;
    for (;;) {
        const double s0 = t.n ? t.a[t.n - 1] + T : p.verge;
        if (t.n >= min_tiers && slope_fits(cut, L, s0, E, bt, grd, cn, t.n ? s0 : 0.5)) {
            r.kind = t.n ? TierResult::Tiers : TierResult::None;
            t.nat = false;
            return r;
        }
        if (t.n >= kMaxWallTiers) { r.kind = TierResult::Fail; return r; }
        const double amin = t.n ? t.a[t.n - 1] + T + p.bench_min_width : 0.0;
        if (amin + T > E) { r.kind = TierResult::Fail; return r; }
        const double rise = cut ? grd(amin + T) - L : L - grd(amin + T);
        if (rise <= cap + 1e-9) {  // this wall reaches the natural ground at its back face / foot
            const double h = rise > 0.05 ? rise : 0.0;
            L = cut ? L + h : L - h;
            t.a[t.n] = amin;
            t.z[t.n] = L - z0;
            ++t.n;
            nat = true;
            if (t.n == 1 && t.z[0] == 0 && min_tiers == 0) { r.kind = TierResult::None; return r; }
            t.nat = nat;
            r.kind = TierResult::Tiers;
            return r;
        }
        L = cut ? L + cap : L - cap;
        t.a[t.n] = amin;
        t.z[t.n] = L - z0;
        ++t.n;
    }
}

// One structure between two roads at different levels, built by the lower one.
bool owner_tiers(double Dz, double ec, const GradingParams& p, WallPlan& out) {
    const double T = p.wall_thickness;
    const int n_req = std::max(1, static_cast<int>(std::ceil(Dz / p.wall_max_height - 1e-9)));
    int n_fit = 0;
    for (int n = 1; n <= kMaxWallTiers; ++n)
        if ((n - 1) * (T + p.bench_min_width) + T <= ec + 1e-9) n_fit = n;
    if (n_fit < 1) return false;
    const int n = std::min(n_req, n_fit);
    const double h = Dz / n;
    out = WallPlan{};
    out.present = true;
    out.cut = true;
    out.t.cut = true;
    out.t.plat = true;
    out.t.n = n;
    for (int k = 0; k < n; ++k) {
        out.t.a[k] = k * (T + p.bench_min_width);
        out.t.z[k] = (k + 1) * h;
    }
    out.over = n_req > n_fit;
    return true;
}

// The plan for side sg of sample s; false for a plain slope. cut_set: -1 decide from the ground, 0 fill, 1 cut
// (padded samples); min_tiers > 0 forces tiers there.
bool plan_side(const RoadSample& s, double hw, int sg, const GroundFn& ground, const NeighbourGround* ng, int cut_set,
               int min_tiers, const GradingParams& p, WallPlan& out) {
    const double nx = -std::sin(s.h) * sg, ny = std::cos(s.h) * sg, bt = p.batter, z0 = s.z - p.formation_drop;
    const double E = p.verge + (p.wall_start + p.wall_slack) * bt;
    const Probe gB = [&](double e) { return ground(s.x + nx * (hw + e), s.y + ny * (hw + e)); };
    bool cut;
    if (cut_set < 0) {
        const double g0 = gB(0) - z0;
        if (std::abs(g0) < 1e-3) return false;
        cut = g0 > 0;
    } else {
        cut = cut_set == 1;
    }
    const TierResult r0 = solve_tiers(cut, z0, E, bt, gB, nullptr, min_tiers, p, p.wall_max_height);
    const bool has_near = ng && !ng->near.empty();
    if (r0.kind == TierResult::None && !(has_near && cut_set < 0)) return false;
    TierResult r = r0;
    bool over = false;
    // the cap is never escalated: when it cannot be kept inside the corridor the single legacy wall is used and flagged
    auto solve_at = [&](const Probe& g, const Contact* c) {
        for (const double m : {1.0}) {
            TierResult q = solve_tiers(cut, z0, E, bt, g, c, min_tiers, p, p.wall_max_height * m);
            if (q.kind != TierResult::Fail) { over = m > 1; return q; }
        }
        over = true;
        TierResult f;
        f.kind = TierResult::Fail;
        return f;
    };
    if (r.kind == TierResult::Fail) r = solve_at(gB, nullptr);
    bool have_owner = false;
    double owner_ec = 0, owner_bz = 0, owner_nat = 0;
    if (has_near) {  // a wall is only kept if the slope cannot land on the neighbours' earthworks either; a neighbour at another level can also ask for one (shared structure below)
        const Probe gE = [&](double e) { return (*ng)(s.x + nx * (hw + e), s.y + ny * (hw + e)); };
        const Contact cn = [&](double e, double& bz) { return ng->contact(s.x + nx * (hw + e), s.y + ny * (hw + e), bz); };
        if (cut_set < 0)
            for (double e = 0.5; e < E + 0.5; e += 0.5) {
                const double ee = std::min(e, E);
                double bz = 0;
                if (cn(ee, bz)) {  // only a roughly parallel neighbour shares a structure (a crossing road is a junction)
                    double cd[2] = {0, 0};
                    if (ng->contact_dir(s.x + nx * (hw + ee), s.y + ny * (hw + ee), cd) &&
                        std::abs(cd[0] * std::cos(s.h) + cd[1] * std::sin(s.h)) >= 0.9) {
                        have_owner = true;
                        owner_ec = ee;
                        owner_bz = bz;
                        owner_nat = gB(ee);
                    }
                    break;
                }
                if (ee >= E) break;
            }
        if (r0.kind == TierResult::None && (!have_owner || std::abs(owner_bz - z0) < 0.25)) return false;  // bare ground needs no wall: only a neighbour at another level (a real height difference) can ask for one
        over = false;
        r = solve_at(gE, &cn);
        if (r.kind == TierResult::None) return false;
    }
    if (have_owner) {  // ONE structure for the pair, built by the lower road; the upper road leaves its side as a plain slope
        const double Dz = owner_bz - z0;
        // the lower road builds it when it is itself cut into the ground on this side; when it stands on fill / level ground
        // it needs no wall of its own and the upper road retains its embankment as usual
        if (std::abs(Dz) >= 0.25) {
            if (Dz > 0) return owner_tiers(Dz, owner_ec, p, out);
            if (owner_nat > owner_bz + 0.25) return false;
        }
    }
    out = WallPlan{};
    out.present = true;
    out.cut = cut;
    if (r.kind == TierResult::Fail) {
        out.full = true;
        out.over = true;
        return true;
    }
    out.t = r.t;
    out.t.cut = cut;
    out.t.plat = false;
    out.over = over;
    return true;
}

// Drop true runs whose arc length (plus one nominal step) is under min_len metres.
void drop_short_len(Flags& a, const std::vector<RoadSample>& S, double min_len, double step) {
    const int n = static_cast<int>(a.size());
    int i = 0;
    while (i < n) {
        if (!a[static_cast<std::size_t>(i)]) { ++i; continue; }
        int j = i;
        while (j < n && a[static_cast<std::size_t>(j)]) ++j;
        if (S[static_cast<std::size_t>(j - 1)].s - S[static_cast<std::size_t>(i)].s + step < min_len - 1e-9)
            for (int k = i; k < j; ++k) a[static_cast<std::size_t>(k)] = 0;
        i = j;
    }
}

// Auto structure rule: br / tu per sample from cut / fill depth at the centre and both wall lines.
void auto_structures(const SampledRoad& r, const GroundFn& ground, const GradingParams& p, Flags& br, Flags& tu) {
    const std::size_t n = r.samples.size();
    const double hw = r.half_width;
    br.assign(n, 0);
    tu.assign(n, 0);
    Flags mb(n, 0), mt(n, 0);
    for (std::size_t i = 0; i < n; ++i) {
        const RoadSample& s = r.samples[i];
        const double nx = -std::sin(s.h), ny = std::cos(s.h);
        const double dz = s.z - ground(s.x, s.y), form = s.z - p.formation_drop;
        const double eL = ground(s.x + nx * hw, s.y + ny * hw), eR = ground(s.x - nx * hw, s.y - ny * hw);
        const bool b = dz > p.wall_cap, t = dz < -p.wall_cap;
        // a wall may never exceed wall_cap: judge both wall lines, not just the centreline
        mb[i] = b || form - eL > p.wall_cap || form - eR > p.wall_cap;
        mt[i] = t || eL - form > p.wall_cap || eR - form > p.wall_cap;
        br[i] = mb[i];
        tu[i] = mt[i] && !mb[i];
    }
    for (Flags* a : {&br, &tu}) {
        close_gaps(*a, p.structure_gap);
        drop_short(*a, p.structure_min_run);
    }
    for (std::size_t i = 0; i < n; ++i) {  // smoothing may extend, never shrink below the cap rule
        if (mb[i]) br[i] = 1;
        else if (mt[i]) tu[i] = 1;
    }
    for (std::size_t i = 0; i < n; ++i)
        if (br[i] && tu[i]) tu[i] = 0;
}

std::vector<StructureRange> runs_of(const Flags& a, StructureKind kind) {
    std::vector<StructureRange> out;
    const int n = static_cast<int>(a.size());
    int i = 0;
    while (i < n) {
        if (!a[static_cast<std::size_t>(i)]) { ++i; continue; }
        int j = i;
        while (j + 1 < n && a[static_cast<std::size_t>(j + 1)]) ++j;
        out.push_back({i, j, kind});
        i = j + 1;
    }
    return out;
}

}  // namespace

std::vector<StructureRange> required_structures(const SampledRoad& road, const GroundFn& ground,
                                                const GradingParams& p) {
    Flags br, tu;
    auto_structures(road, ground, p, br, tu);
    std::vector<StructureRange> out = runs_of(br, StructureKind::Bridge);
    for (const StructureRange& r : runs_of(tu, StructureKind::Tunnel)) out.push_back(r);
    std::sort(out.begin(), out.end(), [](const StructureRange& a, const StructureRange& b) { return a.first < b.first; });
    return out;
}

void classify_structures(SampledRoad& road, const GroundFn& ground, const GradingParams& p, StructureMode mode) {
    std::vector<RoadSample>& S = road.samples;
    const std::size_t n = S.size();
    road.portals.clear();
    if (n == 0) return;
    Flags br, tu;
    if (mode == StructureMode::Auto) {
        auto_structures(road, ground, p, br, tu);
    } else {
        br.assign(n, 0);
        tu.assign(n, 0);
        for (std::size_t i = 0; i < n; ++i) {
            br[i] = S[i].br;
            tu[i] = S[i].tu && !S[i].br;
        }
    }
    for (std::size_t i = 0; i < n; ++i) {
        RoadSample& s = S[i];
        s.br = br[i];
        s.tu = tu[i];
        s.portal = tu[i] && (i == 0 || !tu[i - 1] || i == n - 1 || !tu[i + 1]);
        if (s.portal) road.portals.push_back(static_cast<int>(i));
    }
}

void classify_walls(SampledRoad& road, const GroundFn& ground, const GradingParams& p,
                    const std::vector<const SampledRoad*>& near_roads) {
    std::vector<RoadSample>& S = road.samples;
    const std::size_t n = S.size();
    if (n == 0) return;
    const double hw = road.half_width;
    NeighbourGround ng;
    ng.terrain = &ground;
    ng.p = &p;
    for (const SampledRoad* o : near_roads) ng.near.push_back({o, road_box(*o, p)});
    // Walls only where the batter slope (after the verge) cannot daylight within wall_start of
    // height. Per side: wall type per sample (1 cut, 2 fill), gaps bridged, runs grown, never
    // into bridge / tunnel samples, so no short stubs. Plans: see plan_side.
    Flags lock(n, 0);
    for (std::size_t i = 0; i < n; ++i) lock[i] = S[i].br || S[i].tu;
    std::vector<int> typ[2] = {std::vector<int>(n, 0), std::vector<int>(n, 0)};
    std::vector<WallPlan> plans[2] = {std::vector<WallPlan>(n), std::vector<WallPlan>(n)};
    const int ni = static_cast<int>(n);
    auto idx = [](int i) { return static_cast<std::size_t>(i); };
    for (int k = 0; k < 2; ++k) {
        const int sg = k == 0 ? 1 : -1;
        std::vector<int>& t = typ[k];
        std::vector<WallPlan>& pl = plans[k];
        Flags w(n, 0), wing(n, 0);
        for (std::size_t i = 0; i < n; ++i)
            if (!lock[i]) {
                WallPlan wp;
                if (plan_side(S[i], hw, sg, ground, &ng, -1, 0, p, wp)) pl[i] = wp;
                t[i] = pl[i].present ? (pl[i].cut ? 1 : 2) : 0;
                w[i] = t[i] > 0;
            }
        close_gaps(w, p.wall_gap);
        for (std::size_t i = 0; i < n; ++i)
            if (lock[i]) w[i] = 0;
        dilate(w, p.wall_pad, lock);
        grow_short(w, p.wall_min_run, lock);
        // wing walls beside every portal / abutment: the first two samples always, then up to the
        // wing length (the approach itself stays an ordinary slope)
        for (int i = 0; i < ni; ++i) {
            if (!lock[idx(i)]) continue;
            for (const int dir : {-1, 1}) {
                const int q0 = i + dir;
                if (q0 < 0 || q0 >= ni || lock[idx(q0)]) continue;
                const RoadSample& s0 = S[idx(q0)];
                const double nx = -std::sin(s0.h) * sg, ny = std::cos(s0.h) * sg;
                const double H = std::abs(ground(s0.x + nx * (hw + p.wall_thickness), s0.y + ny * (hw + p.wall_thickness)) -
                                          (s0.z - p.formation_drop));
                const double len = H < p.wing_min_h ? 0.0 : std::min(p.wing_max, p.wing_ratio * H);
                if (len <= 0) continue;
                double d = 0;
                w[idx(q0)] = 1;
                wing[idx(q0)] = 1;
                int cnt = 1;
                for (int q = q0 + dir; q >= 0 && q < ni && !lock[idx(q)]; q += dir, ++cnt) {
                    const RoadSample &a = S[idx(q)], &b = S[idx(q - dir)];
                    d += std::hypot(a.x - b.x, a.y - b.y);
                    if (d > len && cnt >= 2) break;
                    w[idx(q)] = 1;
                    wing[idx(q)] = 1;
                }
            }
        }
        close_gaps(w, p.wall_gap);
        for (std::size_t i = 0; i < n; ++i)
            if (lock[i]) w[i] = 0;
        drop_short_len(w, S, p.wall_min_length, p.sample_step);  // a wall run shorter than one panel is a slope
        for (std::size_t i = 0; i < n; ++i) {
            if (!w[i]) { t[i] = 0; pl[i] = WallPlan{}; continue; }
            if (t[i] == 0) {  // bridged / grown sample: type from the ground at the wall's outer face
                const RoadSample& s = S[i];
                const double nx = -std::sin(s.h) * sg, ny = std::cos(s.h) * sg;
                t[i] = ground(s.x + nx * (hw + p.wall_thickness), s.y + ny * (hw + p.wall_thickness)) >
                               s.z - p.formation_drop
                           ? 1
                           : 2;
            }
        }
        // plans of the padded samples: every sample of a run (same type) gets as many tiers as the run needs anywhere
        for (int i = 0; i < ni;) {
            if (!w[idx(i)]) { ++i; continue; }
            int j = i;
            while (j < ni && w[idx(j)] && t[idx(j)] == t[idx(i)]) ++j;
            for (int q = i; q < j; ++q) {
                if (wing[idx(q)]) continue;
                WallPlan& cur = pl[idx(q)];
                if (cur.present) continue;
                WallPlan wp;
                if (plan_side(S[idx(q)], hw, sg, ground, &ng, t[idx(q)] == 1 ? 1 : 0, 1, p, wp)) {
                    cur = wp;
                } else {
                    cur = WallPlan{};
                    cur.present = true;
                    cur.cut = t[idx(q)] == 1;
                    cur.full = true;
                }
            }
            for (int q = i; q < j; ++q)
                if (!wing[idx(q)]) t[idx(q)] = pl[idx(q)].cut ? 1 : 2;
            i = j;
        }
        // wing walls: legacy full wall blended in from the earthworks beside it by the funnel weight
        for (int i = 0; i < ni;) {
            if (!(w[idx(i)] && wing[idx(i)])) { ++i; continue; }
            int j = i;
            while (j < ni && w[idx(j)] && wing[idx(j)] && t[idx(j)] == t[idx(i)]) ++j;
            const bool from_start = i > 0 && lock[idx(i - 1)], from_end = j < ni && lock[idx(j)];
            std::vector<double> cum{0.0};
            for (int q = i + 1; q < j; ++q)
                cum.push_back(cum[idx(q - i - 1)] + std::hypot(S[idx(q)].x - S[idx(q - 1)].x, S[idx(q)].y - S[idx(q - 1)].y));
            const double tot = cum.back(), ramp = tot < 1e-6 ? p.wall_ramp : std::min(p.wall_ramp, tot);
            const int far_idx = from_start && !from_end ? j : i - 1;
            bool has_base = false;
            TierSet base;
            if (far_idx >= 0 && far_idx < ni && w[idx(far_idx)] && !wing[idx(far_idx)] && t[idx(far_idx)] == t[idx(i)] &&
                pl[idx(far_idx)].present && !pl[idx(far_idx)].full) {
                has_base = true;
                base = pl[idx(far_idx)].t;
            }
            for (int q = i; q < j; ++q) {
                double fw = 1;
                if (!(from_start && from_end)) {
                    const double d = (from_start ? tot - cum[idx(q - i)] : cum[idx(q - i)]) + 1e-9;
                    const double u = std::min(1.0, d / ramp);
                    fw = u * u * (3 - 2 * u);
                }
                WallPlan wp;
                wp.present = true;
                wp.cut = t[idx(q)] == 1;
                wp.full = true;
                wp.wing = true;
                wp.fw = fw;
                wp.has_base = has_base;
                wp.base = base;
                pl[idx(q)] = wp;
            }
            i = j;
        }
    }
    road.over_height = false;
    for (std::size_t i = 0; i < n; ++i) {
        S[i].wL = S[i].wL0 = typ[0][i];
        S[i].wR = S[i].wR0 = typ[1][i];
        S[i].pL = plans[0][i];
        S[i].pR = plans[1][i];
        if (S[i].pL.over || S[i].pR.over) road.over_height = true;
    }
}

void classify_road(SampledRoad& road, const GroundFn& ground, const GradingParams& p, StructureMode mode) {
    classify_structures(road, ground, p, mode);
    classify_walls(road, ground, p, {});
}

Box2 road_box(const SampledRoad& road, const GradingParams& p) {
    const double m = reach_cap(road.half_width, p) + p.box_margin, me = end_reach_cap(road.half_width, p) + p.box_margin;
    Box2 b{1e9, 1e9, -1e9, -1e9};
    for (const RoadSample& s : road.samples) {
        b.x0 = std::min(b.x0, s.x);
        b.x1 = std::max(b.x1, s.x);
        b.y0 = std::min(b.y0, s.y);
        b.y1 = std::max(b.y1, s.y);
    }
    Box2 r{b.x0 - m, b.y0 - m, b.x1 + m, b.y1 + m};
    if (!road.samples.empty())
        for (const RoadSample* s : {&road.samples.front(), &road.samples.back()}) {  // the run-out beyond the road's ends
            r.x0 = std::min(r.x0, s->x - me);
            r.y0 = std::min(r.y0, s->y - me);
            r.x1 = std::max(r.x1, s->x + me);
            r.y1 = std::max(r.y1, s->y + me);
        }
    return r;
}

void mark_medians(std::vector<SampledRoad>& roads, const GradingParams& p) {
    std::vector<SampledRoad*> rs;
    for (SampledRoad& r : roads)
        if (r.samples.size() > 1) rs.push_back(&r);
    for (SampledRoad* r : rs)
        for (RoadSample& s : r->samples) {
            s.mL = s.mR = false;
            s.wL = s.wL0;
            s.wR = s.wR0;
        }
    for (SampledRoad* r : rs) {
        if (!r->dual) continue;
        const double lim = r->median_gap + 2 * r->half_width + 2;
        const Box2 bb = road_box(*r, p);
        for (SampledRoad* r2 : rs) {
            if (r2 == r || r2->type_id != r->type_id) continue;
            const Box2 b2 = road_box(*r2, p);
            if (b2.x1 < bb.x0 || b2.x0 > bb.x1 || b2.y1 < bb.y0 || b2.y0 > bb.y1) continue;
            for (std::size_t i = 0; i < r->samples.size(); i += 2) {
                RoadSample& s = r->samples[i];
                const double nx = -std::sin(s.h), ny = std::cos(s.h);
                for (std::size_t k = 0; k < r2->samples.size(); k += 2) {
                    const RoadSample& t = r2->samples[k];
                    const double dx = t.x - s.x, dy = t.y - s.y;
                    if (std::abs(dx) > lim || std::abs(dy) > lim) continue;
                    const double d = std::hypot(dx, dy);
                    if (d > lim) continue;
                    const double lat = dx * nx + dy * ny;
                    if (std::abs(d - std::abs(lat)) < 2 && std::cos(t.h - s.h) < -0.7) {
                        if (lat > 0) s.mL = true;
                        else s.mR = true;
                    }
                }
            }
        }
    }
    for (SampledRoad* r : rs) {
        std::vector<RoadSample>& S = r->samples;
        for (std::size_t i = 1; i < S.size(); i += 2) {
            RoadSample& s = S[i];
            const RoadSample& a = S[i - 1];
            const RoadSample& b = S[std::min(i + 1, S.size() - 1)];
            s.mL = a.mL || b.mL || s.mL;
            s.mR = a.mR || b.mR || s.mR;
        }
    }
    for (SampledRoad* r : rs)
        for (RoadSample& s : r->samples) {
            if (s.mL) s.wL = 0;
            if (s.mR) s.wR = 0;
        }
}

void compute_wall_weights(SampledRoad& road, const GradingParams& p) {
    std::vector<RoadSample>& S = road.samples;
    const int n = static_cast<int>(S.size());
    for (int side = 0; side < 2; ++side) {
        auto flag = [&](int i) { return side == 0 ? S[static_cast<std::size_t>(i)].wL : S[static_cast<std::size_t>(i)].wR; };
        auto set_ww = [&](int i, double v) {
            (side == 0 ? S[static_cast<std::size_t>(i)].wwL : S[static_cast<std::size_t>(i)].wwR) = v;
        };
        int i = 0;
        while (i < n) {
            if (!flag(i)) { set_ww(i, 0); ++i; continue; }
            int j = i;
            while (j < n && flag(j)) ++j;
            std::vector<double> cum{0.0};
            for (int k = i + 1; k < j; ++k) {
                const RoadSample& a = S[static_cast<std::size_t>(k)];
                const RoadSample& b = S[static_cast<std::size_t>(k - 1)];
                cum.push_back(cum[static_cast<std::size_t>(k - i - 1)] + std::hypot(a.x - b.x, a.y - b.y));
            }
            const double tot = cum.back();
            // only free run ends (incl. the road's own ends) taper: structures (abutment, portal) hide the wall end
            const bool free0 = i == 0 || (!S[static_cast<std::size_t>(i - 1)].br && !S[static_cast<std::size_t>(i - 1)].tu);
            const bool free1 = j == n || (!S[static_cast<std::size_t>(j)].br && !S[static_cast<std::size_t>(j)].tu);
            const double ramp = (free0 && free1) || tot < 1e-6 ? p.wall_ramp : std::min(p.wall_ramp, tot);
            for (int k = i; k < j; ++k) {
                const double c = cum[static_cast<std::size_t>(k - i)];
                const double d = std::min(free0 ? c : 1e9, free1 ? tot - c : 1e9) + 1e-9;
                const double u = std::min(1.0, d / ramp);
                set_ww(k, u * u * (3 - 2 * u));  // run end -> 0 (smoothstep)
            }
            i = j;
        }
    }
}

double slope_reach(const SampledRoad& road, const GroundFn& ground, const GradingParams& p) {
    const std::vector<RoadSample>& S = road.samples;
    const double hw = road.half_width, cap = reach_cap(hw, p), cap_e = end_reach_cap(hw, p), bt = p.batter;
    double mx = 0;
    bool tiers = false;
    const std::size_t n = S.size();
    auto struct_at = [&](std::size_t i) { return S[i].br || S[i].tu; };
    for (std::size_t i = 0; i < n; i += 2) {
        const RoadSample& s = S[i];
        if (s.br || s.tu) continue;
        const double zf = s.z - p.formation_drop;
        // road end or next to a bridge / tunnel: the embankment also falls away along the road (cone around the end)
        const bool road_end = i == 0 || i + 2 >= n - 1;
        const bool endish = road_end || (i >= 2 && struct_at(i - 2)) || (i + 2 < n && struct_at(i + 2));
        const int sgs[4] = {1, -1, 2, 3};
        for (int q = 0; q < (endish ? 4 : 2); ++q) {
            const int sg = sgs[q];
            if (sg == 1 ? s.wL : sg == -1 ? s.wR : 0) {
                const WallPlan& pp = sg == 1 ? s.pL : s.pR;
                if (pp.present && !pp.full) tiers = true;
                continue;
            }
            const double nx = sg > 1 ? std::cos(s.h) * (sg == 2 ? 1 : -1) : -std::sin(s.h) * sg;
            const double ny = sg > 1 ? std::sin(s.h) * (sg == 2 ? 1 : -1) : std::cos(s.h) * sg;
            const double bd = sg > 1 && !road_end ? bt : sg > 1 ? std::max(bt, p.end_taper_batter) : bt;  // run-out batter only beyond a free road end
            const double cp = sg > 1 ? cap_e : cap;
            for (double e = 0.5; hw + e <= cp; e += 1) {
                const double b = ground(s.x + nx * (hw + e), s.y + ny * (hw + e));
                const double v = b > zf ? zf + slope_v(e, p.verge) / bd : zf - slope_v(e, p.verge) / bd;
                if (b > zf ? v >= b : v <= b) {
                    if (e > mx) mx = e;
                    break;
                }
                if (hw + e + 1 > cp && e > mx) mx = e;
            }
        }
    }
    bool any = false;
    for (const RoadSample& s : S) any = any || s.br || s.tu;
    // embankment ends at bridges fall away along the road too: full reach
    const double meas = std::min(cap_e, hw + mx + p.reach_pad + round_reach(p) + p.toe_ditch_width);
    return any || tiers ? std::max(cap, meas) : meas;
}

std::vector<GradingRoad> prepare_grading_roads(std::vector<SampledRoad>& roads, const GroundFn& ground,
                                               const GradingParams& p) {
    {  // walls with the neighbouring roads' earthworks in place (structure flags are final here)
        std::vector<SampledRoad*> rs;
        std::vector<Box2> boxes;
        for (SampledRoad& r : roads)
            if (r.samples.size() > 1) {
                rs.push_back(&r);
                boxes.push_back(road_box(r, p));
            }
        for (std::size_t a = 0; a < rs.size(); ++a) {
            std::vector<const SampledRoad*> near_roads;
            for (std::size_t b = 0; b < rs.size(); ++b) {
                if (a == b) continue;
                const Box2 &x = boxes[a], &y = boxes[b];
                if (y.x1 < x.x0 || y.x0 > x.x1 || y.y1 < x.y0 || y.y0 > x.y1) continue;
                near_roads.push_back(rs[b]);
            }
            classify_walls(*rs[a], ground, p, near_roads);
        }
    }
    mark_medians(roads, p);
    for (SampledRoad& r : roads)
        if (r.samples.size() > 1) compute_wall_weights(r, p);
    std::vector<GradingRoad> out;
    for (const SampledRoad& r : roads) {
        if (r.samples.size() < 2) continue;
        const std::vector<RoadSample>& full = r.samples;
        GradingRoad g;
        g.half_width = r.half_width;
        g.reach = slope_reach(r, ground, p);
        g.bbox = road_box(r, p);
        g.core = {1e9, 1e9, -1e9, -1e9};
        for (const RoadSample& q : full) {
            g.core.x0 = std::min(g.core.x0, q.x - r.half_width);
            g.core.x1 = std::max(g.core.x1, q.x + r.half_width);
            g.core.y0 = std::min(g.core.y0, q.y - r.half_width);
            g.core.y1 = std::max(g.core.y1, q.y + r.half_width);
        }
        const std::size_t n = full.size();
        std::vector<double> dec_h;
        // every 2nd sample, plus the last graded / first tunnel sample at each portal (the portal plane stays exact)
        for (std::size_t i = 0; i < n; ++i) {
            const RoadSample& a = full[i];
            if (!(i % 2 == 0 || i == n - 1 || full[i - 1].tu != a.tu || full[i + 1].tu != a.tu)) continue;
            GradingSample d;
            d.x = a.x; d.y = a.y; d.z = a.z;
            d.wwL = a.wwL; d.wwR = a.wwR;
            d.wL = static_cast<std::int8_t>(a.wL); d.wR = static_cast<std::int8_t>(a.wR);
            d.pL = a.pL; d.pR = a.pR;
            d.fwL = a.pL.wing ? a.pL.fw : 0.0; d.fwR = a.pR.wing ? a.pR.fw : 0.0;
            d.br = a.br; d.tu = a.tu;
            g.samples.push_back(d);
            dec_h.push_back(a.h);
        }
        const std::vector<GradingSample>& dec = g.samples;
        const int nd = static_cast<int>(dec.size());
        for (int i = 0; i + 1 < nd; i += 2) {  // coarse reach boxes for tile culling (middle sample included)
            const GradingSample &a = dec[static_cast<std::size_t>(i)], &m = dec[static_cast<std::size_t>(i + 1)],
                                &b = dec[static_cast<std::size_t>(std::min(i + 2, nd - 1))];
            g.seg_boxes.push_back({std::min({a.x, m.x, b.x}) - g.reach, std::min({a.y, m.y, b.y}) - g.reach,
                                   std::max({a.x, m.x, b.x}) + g.reach, std::max({a.y, m.y, b.y}) + g.reach});
        }
        // portal planes: decimated-segment range they cut, plane point, unit normal pointing into the tunnel
        auto path_len = [&](int from, int to) {
            double d = 0;
            for (int q = from; q < to; ++q)
                d += std::hypot(dec[static_cast<std::size_t>(q + 1)].x - dec[static_cast<std::size_t>(q)].x,
                                dec[static_cast<std::size_t>(q + 1)].y - dec[static_cast<std::size_t>(q)].y);
            return d;
        };
        const double span = g.reach + p.plane_margin;
        for (int k = 0; k < nd; ++k) {
            const GradingSample& a = dec[static_cast<std::size_t>(k)];
            if (!a.tu) continue;
            const double h = dec_h[static_cast<std::size_t>(k)];
            if (k > 0 && !dec[static_cast<std::size_t>(k - 1)].tu) {
                int k0 = k;
                while (k0 > 0 && path_len(k0, k) < span) --k0;
                g.planes.push_back({k0, k, a.x, a.y, std::cos(h), std::sin(h)});
            }
            if (k + 1 < nd && !dec[static_cast<std::size_t>(k + 1)].tu) {
                int k1 = k;
                while (k1 < nd - 1 && path_len(k, k1) < span) ++k1;
                g.planes.push_back({k, k1, a.x, a.y, -std::cos(h), -std::sin(h)});
            }
        }
        out.push_back(std::move(g));
    }
    for (std::size_t a = 0; a < out.size(); ++a)  // roads whose reach boxes overlap: a slope may not reach across them
        for (std::size_t b = 0; b < out.size(); ++b) {
            if (a == b) continue;
            const Box2 &x = out[a].bbox, &y = out[b].bbox;
            if (!(y.x1 < x.x0 || y.x0 > x.x1 || y.y1 < x.y0 || y.y0 > x.y1)) out[a].nbr.push_back(static_cast<int>(b));
        }
    return out;
}

// ---------------------------------------------------------------------------------------------
// Tiles
// ---------------------------------------------------------------------------------------------

std::vector<float> sample_natural_tile(const GroundFn& ground, const TileSpec& t) {
    std::vector<float> out(static_cast<std::size_t>(t.nx) * static_cast<std::size_t>(t.ny));
    for (int j = 0; j < t.ny; ++j)
        for (int i = 0; i < t.nx; ++i)
            out[static_cast<std::size_t>(j) * static_cast<std::size_t>(t.nx) + static_cast<std::size_t>(i)] =
                static_cast<float>(ground(t.x0 + i * t.res, t.y0 + j * t.res));
    return out;
}

std::vector<float> sample_natural_tile(const Heightfield& hf, const TileSpec& t) {
    std::vector<float> out(static_cast<std::size_t>(t.nx) * static_cast<std::size_t>(t.ny));
    for (int j = 0; j < t.ny; ++j)
        for (int i = 0; i < t.nx; ++i)
            out[static_cast<std::size_t>(j) * static_cast<std::size_t>(t.nx) + static_cast<std::size_t>(i)] =
                static_cast<float>(hf.height(t.x0 + i * t.res, t.y0 + j * t.res));
    return out;
}

namespace {

// Squared distance between segments PQ and AB (0 if they cross).
double seg_seg_dist2(double px, double py, double qx, double qy, double ax, double ay, double bx, double by) {
    const double d1x = qx - px, d1y = qy - py, d2x = bx - ax, d2y = by - ay, den = d1x * d2y - d1y * d2x;
    if (std::abs(den) > 1e-12) {
        const double t = ((ax - px) * d2y - (ay - py) * d2x) / den, u = ((ax - px) * d1y - (ay - py) * d1x) / den;
        if (t >= 0 && t <= 1 && u >= 0 && u <= 1) return 0;
    }
    auto pt = [](double x, double y, double x0, double y0, double x1, double y1) {
        const double dx = x1 - x0, dy = y1 - y0, L2 = dx * dx + dy * dy;
        double u = L2 > 1e-12 ? ((x - x0) * dx + (y - y0) * dy) / L2 : 0.0;
        u = u < 0 ? 0 : u > 1 ? 1 : u;
        const double ex = x - x0 - u * dx, ey = y - y0 - u * dy;
        return ex * ex + ey * ey;
    };
    return std::min(std::min(pt(px, py, ax, ay, bx, by), pt(qx, qy, ax, ay, bx, by)),
                    std::min(pt(ax, ay, px, py, qx, qy), pt(bx, by, px, py, qx, qy)));
}

struct Eval {
    int kind;  // 0 nothing (natural ground), 1 inside a solid (v: road-facing value), 2 cut surface, 3 fill surface
    double v;
};

// Ground value of one wall plan at lateral e (beyond the footprint edge). Tier k occupies e in [w*a[k], w*a[k] + wallT]
// and levels are zf + w*z[k]; w is the run weight.
Eval tier_eval(const TierSet& pl, double w, double e, double zf, double b, double res, double wall_t, double bt) {
    const int nT = pl.n;
    const bool cut_p = pl.cut;
    int kk = -1;
    for (int q = nT - 1; q >= 0; --q)
        if (e >= w * pl.a[q]) { kk = q; break; }
    if (kk < 0) return {cut_p ? 2 : 3, zf};
    const double tk = w * pl.a[kk], Lp = kk > 0 ? zf + w * pl.z[kk - 1] : zf, Lk = zf + w * pl.z[kk];
    if (e <= tk + wall_t - res) return {1, Lp};
    if (e <= tk + wall_t) return cut_p ? Eval{1, Lk} : Eval{3, Lk};
    const double sgn = cut_p ? 1.0 : -1.0;
    if (kk < nT - 1) return {cut_p ? 2 : 3, Lk};
    if (pl.plat) return {b > Lk ? 2 : 3, Lk + (b > Lk ? 1.0 : -1.0) * (1 - w) * (e - (tk + wall_t)) / bt};  // flat up to the neighbour; the slope comes back as the run weight drops (run ends)
    if (pl.nat) {  // the slope beyond the last wall starts at the local natural level (rolls into the ground); lower while the run weight is below 1
        const double Lf = zf + pl.z[nT - 1];
        const double Ls = cut_p ? Lk + w * (std::max(Lf, b - 0.1) - Lf) : Lk + w * (std::min(Lf, b) - Lf);
        return {cut_p ? 2 : 3, Ls + sgn * (e - (tk + wall_t)) / bt};
    }
    return {cut_p ? 2 : 3, Lk + sgn * (e - (tk + wall_t)) / bt};
}

// The legacy full wall at weight 1 (retains the natural ground at the texel, whatever its height).
Eval full_eval(bool cut_p, double e, double zf, double b, double res, double wall_t, double verge, double bt) {
    if (cut_p) {
        const double zfw = std::max(zf, b - 0.1);
        return e <= wall_t ? Eval{1, e <= wall_t - res ? zf : zfw} : Eval{2, zfw + (e - verge) / bt};
    }
    if (e <= wall_t - res) return {1, zf};
    return {3, e <= wall_t ? b : b - (e - verge) / bt};
}

}  // namespace

void grade_tile_into(const std::vector<GradingRoad>& roads, const GradingParams& p, const TileSpec& t,
                     const float* natural, float* out, std::uint8_t* footprint) {
    GradeWorkspace ws;
    grade_tile_into(roads, p, t, natural, out, footprint, ws);
}

void grade_tile_into(const std::vector<GradingRoad>& roads, const GradingParams& p, const TileSpec& t,
                     const float* base, float* out, std::uint8_t* footprint, GradeWorkspace& ws) {
    const std::size_t N = static_cast<std::size_t>(t.nx) * static_cast<std::size_t>(t.ny);
    if (N == 0) return;
    const int nx = t.nx, ny = t.ny;
    const double res = t.res, x0 = t.x0, y0 = t.y0, bt = p.batter, fd = p.formation_drop, verge = p.verge;
    const double d_d = p.toe_ditch_depth, d_w = p.toe_ditch_width, e_b = p.end_taper_batter > 0 ? p.end_taper_batter : bt;
    const double hb = p.round_hinge, rk = p.round_daylight / (2 * bt);  // hinge run; vertical half-width of the daylight roll-over
    // verge -> slope hinge: parabola over hb, then the straight slope shifted out by hb / 2
    auto sv = [&](double e) {
        return e <= verge ? 0.0 : hb > 0 ? (e < verge + hb ? (e - verge) * (e - verge) / (2 * hb) : e - verge - hb / 2) : e - verge;
    };
    // smooth max(d, 0) over +-rk: rolls a slope over into natural ground
    auto smax = [&](double d) { return d >= rk ? d : d <= -rk ? 0.0 : (d + rk) * (d + rk) / (4 * rk); };
    // wall band: texels with e <= wall_thickness - res take the inside (formation) value, so the
    // one-texel ramp to natural lies inside the solid
    const double wall_t = p.wall_thickness, half = wall_t - res;
    const double tx1 = x0 + (nx - 1) * res, ty1 = y0 + (ny - 1) * res;
    ws.fp_d.assign(N, 9.0f);
    ws.fp_z.assign(N, 1e9f);
    ws.cut_m.assign(N, 1e9f);
    ws.fill_m.assign(N, -1e9f);
    ws.rq_d.assign(N, 1e9f);  // per road: nearest-station formation (reset after each road); across roads: lowest wins
    for (std::vector<float>* v : {&ws.rq_ww, &ws.rq_z, &ws.cut_e, &ws.fill_e, &ws.rq_ex, &ws.rq_ey, &ws.rq_bt, &ws.rq_fw})
        if (v->size() < N) v->resize(N);
    if (ws.rq_w.size() < N) ws.rq_w.resize(N);
    if (ws.rq_p.size() < N) ws.rq_p.resize(N);
    std::vector<float> &fp_d = ws.fp_d, &fp_z = ws.fp_z, &cut_m = ws.cut_m, &fill_m = ws.fill_m, &rq_ww = ws.rq_ww,
                       &rq_d = ws.rq_d, &rq_z = ws.rq_z, &cut_e = ws.cut_e, &fill_e = ws.fill_e, &rq_ex = ws.rq_ex,
                       &rq_ey = ws.rq_ey, &rq_bt = ws.rq_bt, &rq_fw = ws.rq_fw;
    std::vector<std::uint8_t>& rq_w = ws.rq_w;
    std::vector<const WallPlan*>& rq_p = ws.rq_p;
    std::vector<const PortalPlane*> ap;
    int ri0 = 0, ri1 = -1, rj0 = 0, rj1 = -1;  // texel rectangle the current road touched
    for (const GradingRoad& rd : roads) {
        const Box2& bb = rd.bbox;
        if (bb.x1 < x0 || bb.x0 > tx1 || bb.y1 < y0 || bb.y0 > ty1) continue;
        bool near_any = rd.seg_boxes.empty();
        for (const Box2& b : rd.seg_boxes)
            if (b.x1 >= x0 && b.x0 <= tx1 + res && b.y1 >= y0 && b.y0 <= ty1 + res) { near_any = true; break; }
        if (!near_any) continue;
        const std::vector<GradingSample>& S = rd.samples;
        const double hw = rd.half_width, R = rd.reach, R2 = R * R;
        // a slope never reaches across another road: it is dropped where the straight way from its
        // edge to the texel crosses a neighbour's footprint (so a road that lands on a neighbour's
        // earthworks cannot carve the ground behind it)
        auto blocked = [&](std::size_t o, int i, int j, double dd) {
            if (rd.nbr.empty() || hw + 0.5 >= dd) return false;
            const double tx = x0 + i * res, ty = y0 + j * res, ex = rq_ex[o], ey = rq_ey[o], f = (hw + 0.5) / dd;
            const double qx = tx - ex + ex * f, qy = ty - ey + ey * f;
            for (const int ni : rd.nbr) {
                const GradingRoad& nb = roads[static_cast<std::size_t>(ni)];
                const Box2& bx = nb.core;
                if (std::max(qx, tx) < bx.x0 || std::min(qx, tx) > bx.x1 || std::max(qy, ty) < bx.y0 || std::min(qy, ty) > bx.y1) continue;
                const double l2 = (nb.half_width - 0.25) * (nb.half_width - 0.25);
                for (std::size_t k = 0; k + 1 < nb.samples.size(); ++k) {
                    const GradingSample &a = nb.samples[k], &b = nb.samples[k + 1];
                    if ((a.br || a.tu) && (b.br || b.tu)) continue;
                    const double m = nb.half_width;
                    if (std::max(a.x, b.x) + m < std::min(qx, tx) || std::min(a.x, b.x) - m > std::max(qx, tx) ||
                        std::max(a.y, b.y) + m < std::min(qy, ty) || std::min(a.y, b.y) - m > std::max(qy, ty))
                        continue;
                    if (seg_seg_dist2(qx, qy, tx, ty, a.x, a.y, b.x, b.y) < l2) return true;
                }
            }
            return false;
        };
        ri0 = nx; ri1 = -1; rj0 = ny; rj1 = -1;
        for (std::size_t k = 0; k + 1 < S.size(); ++k) {
            const GradingSample &A = S[k], &B = S[k + 1];
            if ((A.br || A.tu) && (B.br || B.tu)) continue;  // bridge / tunnel stations are not graded; a segment with one structure end is graded up to the abutment / portal
            const double dx = B.x - A.x, dy = B.y - A.y, L2 = dx * dx + dy * dy;
            if (L2 < 1e-12) continue;
            const int i0 = std::max(0, static_cast<int>(std::floor((std::min(A.x, B.x) - R - x0) / res)));
            const int i1 = std::min(nx - 1, static_cast<int>(std::ceil((std::max(A.x, B.x) + R - x0) / res)));
            const int j0 = std::max(0, static_cast<int>(std::floor((std::min(A.y, B.y) - R - y0) / res)));
            const int j1 = std::min(ny - 1, static_cast<int>(std::ceil((std::max(A.y, B.y) + R - y0) / res)));
            ri0 = std::min(ri0, i0); ri1 = std::max(ri1, i1); rj0 = std::min(rj0, j0); rj1 = std::max(rj1, j1);
            const double dz = B.z - A.z;
            ap.clear();
            const int ki = static_cast<int>(k);
            for (const PortalPlane& pl : rd.planes)
                if (ki >= pl.k0 && ki < pl.k1) ap.push_back(&pl);
            const double sq = std::sqrt(L2);
            const double t_hi = 1 + p.plane_over / sq, t_lo = -p.plane_over / sq;
            const bool a_str = A.br || A.tu, b_str = B.br || B.tu;
            for (int j = j0; j <= j1; ++j) {
                const double ry = y0 + j * res - A.y;
                for (int i = i0; i <= i1; ++i) {
                    const double rx = x0 + i * res - A.x;
                    if (!ap.empty()) {  // past a tunnel portal plane: natural ground (the headwall hides the one-texel step)
                        const double px = rx + A.x, py = ry + A.y;
                        bool beyond = false;
                        for (const PortalPlane* pl : ap)
                            if ((px - pl->x) * pl->nx + (py - pl->y) * pl->ny > p.plane_over) { beyond = true; break; }
                        if (beyond) continue;
                    }
                    double tt = (rx * dx + ry * dy) / L2;
                    // tunnel portal: grading stops exactly at the portal plane (the first tunnel station); bridges keep the rounded embankment end
                    if ((B.tu && !A.tu && tt > t_hi) || (A.tu && !B.tu && tt < t_lo)) continue;
                    const double t_raw = tt;
                    tt = tt < 0 ? 0 : tt > 1 ? 1 : tt;
                    const double ex = rx - tt * dx, ey = ry - tt * dy, d2 = ex * ex + ey * ey;
                    if (d2 > R2) continue;
                    const double dd = std::sqrt(d2);
                    const std::size_t o = static_cast<std::size_t>(j) * static_cast<std::size_t>(nx) + static_cast<std::size_t>(i);
                    if (dd >= rq_d[o]) continue;  // nearest station of this road decides (one zf, one wall flag per texel)
                    rq_d[o] = static_cast<float>(dd);
                    rq_ex[o] = static_cast<float>(ex);
                    rq_ey[o] = static_cast<float>(ey);
                    rq_z[o] = static_cast<float>(A.z + tt * dz - fd);
                    {  // free road end: the run-out beyond it eases off with the angle from the side direction
                        double bte = bt;
                        if (e_b > bt && dd > 0 &&
                            ((k == 0 && t_raw < 0 && !A.br && !A.tu) || (k == S.size() - 2 && t_raw > 1 && !B.br && !B.tu))) {
                            const double along = t_raw < 0 ? -(rx * dx + ry * dy) / sq : (rx * dx + ry * dy) / sq - sq;
                            bte = bt + (e_b - bt) * std::min(1.0, along / dd);
                        }
                        rq_bt[o] = static_cast<float>(bte);
                    }
                    const GradingSample& E = a_str ? B : b_str ? A : tt < 0.5 ? A : B;
                    const bool lf = (dx * ry - dy * rx) > 0;  // cross > 0: texel on the left
                    rq_w[o] = static_cast<std::uint8_t>(lf ? E.wL : E.wR);
                    rq_p[o] = lf ? &E.pL : &E.pR;
                    {
                        const double fa = lf ? A.fwL : A.fwR, fb = lf ? B.fwL : B.fwR;
                        rq_fw[o] = static_cast<float>(a_str ? fb : b_str ? fa : fa + (fb - fa) * tt);
                    }
                    const double wa = lf ? A.wwL : A.wwR, wb = lf ? B.wwL : B.wwR;
                    rq_ww[o] = static_cast<float>(a_str ? wb : b_str ? wa : wa + (wb - wa) * tt);
                }
            }
        }
        for (int jj = rj0; jj <= rj1; ++jj)
        for (int ii = ri0; ii <= ri1; ++ii) {
            const std::size_t o = static_cast<std::size_t>(jj) * static_cast<std::size_t>(nx) + static_cast<std::size_t>(ii);
            const double dd = rq_d[o];
            if (dd > 1e8) continue;
            const double zf = rq_z[o];
            if (dd <= hw) {  // union of footprints: lowest formation wins (never pokes through any road)
                fp_d[o] = 0;
                if (zf < fp_z[o]) fp_z[o] = static_cast<float>(zf);
                continue;
            }
            const double e = dd - hw, b = base[o];
            const bool cut_t = b > zf;
            auto blk = [&]() {  // evaluated only when a slope would be recorded
                return blocked(o, static_cast<int>(o % static_cast<std::size_t>(nx)), static_cast<int>(o / static_cast<std::size_t>(nx)), dd);
            };
            int wf = rq_w[o];
            double ww = wf ? static_cast<double>(rq_ww[o]) : 0.0;
            const WallPlan* pl = wf ? rq_p[o] : nullptr;
            const double bte = rq_bt[o];
            if (wf && !(pl && pl->t.plat) && (wf == 1) != cut_t) { wf = 0; ww = 0; pl = nullptr; }  // flag says cut but this texel is fill (or vice versa): plain slope
            if (wf && pl && (!pl->full || pl->wing)) {  // stacked walls (plan_side), or a wing wall blended in from the earthworks beside it
                Eval ev;
                if (!pl->full) {
                    ev = tier_eval(pl->t, ww, e, zf, b, res, wall_t, bt);
                } else {  // wing: (1 - fw) * ordinary earthworks (adjacent run's tiers, else the plain slope) + fw * full wall
                    Eval eb = pl->has_base ? tier_eval(pl->base, ww, e, zf, b, res, wall_t, bt)
                                           : Eval{pl->cut ? 2 : 3, pl->cut ? zf + sv(e) / bt : zf - sv(e) / bt};
                    const Eval ef = full_eval(pl->cut, e, zf, b, res, wall_t, verge, bt);
                    if (eb.kind == 0) { eb.kind = ef.kind; eb.v = pl->cut ? b + rk : b - rk; }  // natural ground carries on beside the adjacent wall
                    ev.kind = eb.kind == 1 || ef.kind == 1 ? 1 : ef.kind;
                    ev.v = eb.v + static_cast<double>(rq_fw[o]) * (ef.v - eb.v);
                }
                if (ev.kind == 0) continue;
                const double v = ev.v;
                if (ev.kind == 1) {
                    fp_d[o] = 0;
                    const double zw = pl->cut ? std::min(v, b) : v;
                    if (zw < fp_z[o]) fp_z[o] = static_cast<float>(zw);
                    continue;
                }
                if (ev.kind == 2) {
                    if (v < b + rk && v < cut_m[o] && !blk()) { cut_m[o] = static_cast<float>(v); cut_e[o] = static_cast<float>(e); }
                } else if (v > b - rk && v > fill_m[o] && !blk()) {
                    fill_m[o] = static_cast<float>(v);
                    fill_e[o] = static_cast<float>(e);
                }
                continue;
            }
            // Legacy full wall (ww = weight 0..1 along the run; verge == wall thickness so ww = 0 is exactly the plain verge + slope).
            // cut wall: the wall raises the formation level toward natural by ww (hidden behind the road-facing wall face), the slope starts above it.
            // fill wall: the wall's foot sits ww of the way from formation down to natural; the slope continues below the foot.
            if (wf == 1) {
                const double zfw = zf + ww * (std::max(zf, b - 0.1) - zf);
                if (e <= wall_t) {
                    fp_d[o] = 0;
                    const double zw = e <= half ? std::min(zf, b) : std::min(zfw, b);
                    if (zw < fp_z[o]) fp_z[o] = static_cast<float>(zw);
                    continue;
                }
                const double v = zfw + (e - verge) / bt;
                if (v < b + rk && v < cut_m[o] && !blk()) { cut_m[o] = static_cast<float>(v); cut_e[o] = static_cast<float>(e); }
                continue;
            }
            if (wf == 2) {
                if (e <= half) {
                    fp_d[o] = 0;
                    if (zf < fp_z[o]) fp_z[o] = static_cast<float>(zf);
                    continue;
                }
                const double zff = zf - ww * (zf - b);
                const double v = e <= wall_t ? zff : zff - (e - verge) / bt;
                if (v > b - rk && v > fill_m[o] && !blk()) { fill_m[o] = static_cast<float>(v); fill_e[o] = static_cast<float>(e); }
                continue;
            }
            if (cut_t) {  // toe ditch between verge and slope (fades out in shallow cuts), then the slope
                double v = zf + sv(e - d_w) / bt;
                if (d_d > 0 && d_w > 0 && e > verge && e < verge + d_w) {
                    const double u = std::sin(3.14159265358979323846 * (e - verge) / d_w);
                    v -= d_d * std::min(1.0, b - zf) * u * u;
                }
                if (v < b + rk && v < cut_m[o] && !blk()) { cut_m[o] = static_cast<float>(v); cut_e[o] = static_cast<float>(e); }
            } else {
                const double v = zf - sv(e) / bte;
                if (v > b - rk && v > fill_m[o] && !blk()) { fill_m[o] = static_cast<float>(v); fill_e[o] = static_cast<float>(e); }
            }
        }
        for (int jj = rj0; jj <= rj1; ++jj)  // reset the touched rectangle for the next road
            std::fill_n(rq_d.begin() + static_cast<std::ptrdiff_t>(jj) * nx + ri0, ri1 - ri0 + 1, 1e9f);
    }
    for (std::size_t o = 0; o < N; ++o) {
        double g;
        if (fp_d[o] < 9) {
            g = fp_z[o];
        } else {
            g = base[o];
            // dip below natural (cut) / rise above it (fill), rounded near daylight
            const double dc = cut_m[o] < 1e8f ? (rk > 0 ? smax(g - cut_m[o]) : std::max(0.0, g - cut_m[o])) : 0.0;
            const double df = fill_m[o] > -1e8f ? (rk > 0 ? smax(fill_m[o] - g) : std::max(0.0, fill_m[o] - g)) : 0.0;
            // never cross the base to the wrong side: take the nearer of a cut and a fill slope
            if (dc > 0 && df > 0) g = cut_e[o] <= fill_e[o] ? g - dc : g + df;
            else if (dc > 0) g -= dc;
            else if (df > 0) g += df;
        }
        out[o] = static_cast<float>(g);
        if (footprint) footprint[o] = fp_d[o] < 9 ? 1 : 0;
    }
}

GradedTile grade_tile(const std::vector<GradingRoad>& roads, const GradingParams& p, const TileSpec& t,
                      const std::vector<float>& natural) {
    GradedTile g;
    const std::size_t N = static_cast<std::size_t>(t.nx) * static_cast<std::size_t>(t.ny);
    g.height.assign(N, 0.0f);
    g.footprint.assign(N, 0);
    if (natural.size() < N) return g;
    grade_tile_into(roads, p, t, natural.data(), g.height.data(), g.footprint.data());
    return g;
}

// ---------------------------------------------------------------------------------------------
// Wall runs
// ---------------------------------------------------------------------------------------------

std::vector<WallRun> wall_runs(const SampledRoad& road, const GroundFn& ground, const GradingParams& p) {
    const std::vector<RoadSample>& S = road.samples;
    const int n = static_cast<int>(S.size());
    const double hw = road.half_width;
    std::vector<WallRun> runs;
    for (const char side : {'L', 'R'}) {
        const int sg = side == 'L' ? 1 : -1;
        auto w_flag = [&](int i) {
            if (i < 0 || i >= n) return false;
            const RoadSample& q = S[static_cast<std::size_t>(i)];
            return (side == 'L' ? q.wL : q.wR) != 0 && !q.br && !q.tu;
        };
        auto plan_of = [&](const RoadSample& q) -> const WallPlan& { return side == 'L' ? q.pL : q.pR; };
        enum Key { Ww, Fw };
        auto val = [&](const RoadSample& q, Key key) {
            if (key == Ww) return side == 'L' ? q.wwL : q.wwR;
            const WallPlan& pp = plan_of(q);
            return pp.present && pp.wing ? pp.fw : 0.0;
        };
        // the grading interpolates weights linearly between its decimated stations (every 2nd sample): do the same,
        // so wall heights meet the graded ground
        auto chord = [&](int i, int ref, Key key) {
            const RoadSample& r = S[static_cast<std::size_t>(ref)];
            if (i % 2 == 0 || i == n - 1 || ref != i) return val(r, key);
            const int a = i - 1, b = std::min(i + 1, n - 1);
            return w_flag(a) && w_flag(b) ? (val(S[static_cast<std::size_t>(a)], key) + val(S[static_cast<std::size_t>(b)], key)) / 2
                                          : val(r, key);
        };
        for (int k = 0; k < kMaxWallTiers; ++k) {
            int cur = -1;
            for (int i = 0; i < n; ++i) {
                const RoadSample& s = S[static_cast<std::size_t>(i)];
                int ref = -1;
                if (w_flag(i)) ref = i;
                else if (s.tu && s.portal && w_flag(i - 1)) ref = i - 1;  // a portal station joins the wall run beside it
                else if (s.tu && s.portal && w_flag(i + 1)) ref = i + 1;
                if (ref < 0) { cur = -1; continue; }
                const RoadSample& rs = S[static_cast<std::size_t>(ref)];
                // the grading takes the plan of the decimated station at or after an odd sample (its t >= 0.5 rule)
                const RoadSample& pref = (ref == i && i % 2 == 1 && i + 1 < n && w_flag(i + 1)) ? S[static_cast<std::size_t>(i + 1)] : rs;
                const WallPlan& pp = plan_of(pref);
                const double w = chord(i, ref, Ww);
                const double nx = -std::sin(s.h) * sg, ny = std::cos(s.h) * sg, zf = s.z - p.formation_drop;
                const bool wing = pp.present && pp.wing;
                const TierSet* tp = wing ? (pp.has_base ? &pp.base : nullptr) : (pp.present && !pp.full ? &pp.t : nullptr);
                const double fw = wing ? chord(i, ref, Fw) : 0.0;
                const double out = ground(s.x + nx * (hw + p.wall_thickness), s.y + ny * (hw + p.wall_thickness));
                const bool cut = pp.present ? pp.cut : out > zf;
                const double sgn = cut ? 1.0 : -1.0;
                double off = 0, zTop, zBot, top;
                if (pp.present && !pp.full) {
                    const TierSet& t = pp.t;
                    if (k >= t.n || std::abs(t.z[k] - (k ? t.z[k - 1] : 0.0)) < 1e-9) { cur = -1; continue; }
                    off = w * t.a[k];
                    const double Lp = zf + w * (k ? t.z[k - 1] : 0.0), Lk = zf + w * t.z[k];
                    if (cut) { zTop = Lk; zBot = Lp; top = Lk + 0.4 + std::max(0.0, 0.5 - (Lk - Lp)); }
                    else { zTop = Lp; zBot = Lk; top = k == 0 ? zf + 0.3 + w * (s.z + 1.0 - zf - 0.3) : Lp + 0.4; }
                } else {
                    if (k > 0 && !(wing && tp && k < tp->n && std::abs(tp->z[k] - tp->z[k - 1]) > 1e-9)) { cur = -1; continue; }
                    const double H = cut ? std::max(out - zf, 0.0) : std::max(zf - out, 0.0), wf = wing ? fw : w;  // legacy full wall; a wing blends (1 - fw) of the adjacent tiers with fw of the full wall
                    const double H0 = wing ? (1 - fw) * (tp ? w * std::abs(tp->z[0]) : 0.0) + fw * H : w * H, L0 = zf + sgn * H0;
                    if (k == 0) {
                        if (cut) { zTop = L0; zBot = zf; top = L0 + 0.4 + std::max(0.0, 0.5 - (L0 - zf)); }
                        else { zTop = zf; zBot = L0; top = zf + 0.3 + wf * (s.z + 1.0 - zf - 0.3); }
                    } else {
                        off = w * tp->a[k];
                        const double Lp = L0 + (1 - fw) * w * (tp->z[k - 1] - tp->z[0]), Lk = Lp + (1 - fw) * w * (tp->z[k] - tp->z[k - 1]);
                        if (cut) { zTop = Lk; zBot = Lp; top = Lk + 0.4 + std::max(0.0, 0.5 - (Lk - Lp)); }
                        else { zTop = Lp; zBot = Lk; top = Lp + 0.4; }
                    }
                }
                const bool nat = pp.present && (pp.full ? !pp.wing : pp.t.nat && k == pp.t.n - 1);
                if (cur < 0 || runs[static_cast<std::size_t>(cur)].cut != cut) {
                    WallRun r;
                    r.side = side;
                    r.cut = cut;
                    r.lo = 0;
                    r.hi = p.wall_thickness;
                    r.tier = k;
                    runs.push_back(std::move(r));
                    cur = static_cast<int>(runs.size()) - 1;
                }
                if (pp.present && pp.over) runs[static_cast<std::size_t>(cur)].over = true;
                WallPoint wp;
                wp.x = s.x + nx * (hw + off);
                wp.y = s.y + ny * (hw + off);
                wp.zTop = zTop;
                wp.zBot = zBot;
                wp.top = top;
                wp.h = s.h;
                wp.nat = nat;
                runs[static_cast<std::size_t>(cur)].pts.push_back(wp);
            }
        }
    }
    return runs;
}

// ---------------------------------------------------------------------------------------------
// Tunnels
// ---------------------------------------------------------------------------------------------

TunnelProfile tunnel_profile(double half_width, const GradingParams& p) {
    TunnelProfile t;
    t.W = half_width;
    t.rise = p.tunnel.rise_k * half_width;
    t.Hs = p.tunnel.clear_height;
    t.R = (half_width * half_width + t.rise * t.rise) / (2 * t.rise);
    t.Hc = t.Hs + t.rise;
    t.zc = t.Hs + t.rise - t.R;
    return t;
}

double tunnel_roof(const TunnelProfile& prof, double q, double off) {
    return prof.zc + std::sqrt(std::max((prof.R + off) * (prof.R + off) - q * q, 0.0));
}

std::vector<StructureRange> tunnel_runs(const SampledRoad& road) {
    const std::vector<RoadSample>& S = road.samples;
    const int n = static_cast<int>(S.size());
    std::vector<StructureRange> out;
    int i = 0;
    while (i < n) {
        if (!S[static_cast<std::size_t>(i)].tu) { ++i; continue; }
        int j = i;
        while (j + 1 < n && S[static_cast<std::size_t>(j + 1)].tu) ++j;
        if (j > i) out.push_back({i, j, StructureKind::Tunnel});
        i = j + 1;
    }
    return out;
}

std::vector<TunnelVolume> tunnel_volumes(const std::vector<SampledRoad>& roads, const GradingParams& p) {
    std::vector<TunnelVolume> out;
    for (std::size_t ri = 0; ri < roads.size(); ++ri) {
        const SampledRoad& r = roads[ri];
        if (r.samples.size() < 2) continue;
        const TunnelProfile prof = tunnel_profile(r.half_width, p);
        for (const StructureRange& run : tunnel_runs(r)) {
            TunnelVolume v;
            v.road = static_cast<int>(ri);
            v.a = run.first;
            v.b = run.last;
            v.prof = prof;
            for (int i = run.first; i <= run.last; ++i) {
                const RoadSample& s = r.samples[static_cast<std::size_t>(i)];
                v.st.push_back({s.x, s.y, s.z, s.h, s.s});
            }
            out.push_back(std::move(v));
        }
    }
    return out;
}

int tunnel_table_width(const GradingParams& p) { return 2 + 2 * (p.tunnel.chunk + 1); }

int pack_tunnels(std::vector<float>& data, int max_rows, const std::vector<TunnelVolume>& vols,
                 const GradingParams& p, Vec2 origin) {
    const int W = tunnel_table_width(p);
    data.assign(static_cast<std::size_t>(W) * static_cast<std::size_t>(std::max(0, max_rows)) * 4, 0.0f);
    const TunnelParams& tp = p.tunnel;
    int row = 0;
    for (const TunnelVolume& v : vols) {
        const int m = static_cast<int>(v.st.size());
        const double off = tp.lining + tp.margin;
        const TunnelProfile& pr = v.prof;
        for (int s0 = 0; s0 + 1 < m && row < max_rows; s0 += tp.chunk, ++row) {
            const int s1 = std::min(m - 1, s0 + tp.chunk), n_seg = s1 - s0;
            const std::size_t base = static_cast<std::size_t>(row) * static_cast<std::size_t>(W) * 4;
            double x0 = 1e18, y0 = 1e18, z0 = 1e18, x1 = -1e18, y1 = -1e18, z1 = -1e18;
            for (int j = 0; j <= n_seg; ++j) {
                const TunnelStation& st = v.st[static_cast<std::size_t>(s0 + j)];
                const std::size_t o = base + static_cast<std::size_t>(2 + 2 * j) * 4;
                const double wd = pr.W + off, sx = st.x - origin.x, sy = st.y - origin.y;
                data[o] = static_cast<float>(sx);
                data[o + 1] = static_cast<float>(sy);
                data[o + 2] = static_cast<float>(st.z);
                data[o + 3] = static_cast<float>(wd);
                data[o + 4] = static_cast<float>(pr.zc);
                data[o + 5] = static_cast<float>(pr.R + off);
                data[o + 6] = (s0 + j == 0 || s0 + j == m - 1) ? 1.0f : 0.0f;
                data[o + 7] = 0.0f;
                x0 = std::min(x0, sx - wd - 1);
                x1 = std::max(x1, sx + wd + 1);
                y0 = std::min(y0, sy - wd - 1);
                y1 = std::max(y1, sy + wd + 1);
                z0 = std::min(z0, st.z - tp.floor_depth - 0.1);
                z1 = std::max(z1, st.z + pr.Hc + off + 0.1);
            }
            data[base] = static_cast<float>(x0);
            data[base + 1] = static_cast<float>(y0);
            data[base + 2] = static_cast<float>(z0);
            data[base + 3] = static_cast<float>(n_seg);
            data[base + 4] = static_cast<float>(x1);
            data[base + 5] = static_cast<float>(y1);
            data[base + 6] = static_cast<float>(z1);
            data[base + 7] = 0.0f;
        }
    }
    return row;
}

bool in_bore(const float* data, int width_texels, int rows, double x, double y, double up, const GradingParams& p) {
    const std::size_t W = static_cast<std::size_t>(width_texels);
    for (int c = 0; c < rows; ++c) {
        const std::size_t b = static_cast<std::size_t>(c) * W * 4;
        if (x < data[b] || x > data[b + 4] || y < data[b + 1] || y > data[b + 5] || up < data[b + 2] || up > data[b + 6])
            continue;
        const int n = static_cast<int>(data[b + 3]);
        for (int i = 0; i < n; ++i) {
            const std::size_t a = b + static_cast<std::size_t>(2 + 2 * i) * 4, e = a + 8;
            const double ax = data[a], ay = data[a + 1], dx = data[e] - ax, dy = data[e + 1] - ay, L2 = dx * dx + dy * dy;
            if (L2 < 1e-9) continue;
            const double px = x - ax, py = y - ay, t = (px * dx + py * dy) / L2;
            if (t < (data[a + 6] > 0.5f ? 0.0 : -0.1) || t > (data[e + 6] > 0.5f ? 1.0 : 1.1)) continue;
            const double lat = (dx * py - dy * px) / std::sqrt(L2);
            const double tc = t < 0 ? 0 : t > 1 ? 1 : t;
            auto mx = [&](std::size_t k) { return static_cast<double>(data[a + k]) + (static_cast<double>(data[e + k]) - data[a + k]) * tc; };
            const double zr = mx(2), Wd = mx(3), zc = mx(4), R = mx(5), rel = up - zr, al = std::abs(lat);
            if (al <= Wd && rel >= -p.tunnel.floor_depth && rel <= zc + std::sqrt(std::max(R * R - al * al, 0.0))) return true;
        }
    }
    return false;
}

// ---------------------------------------------------------------------------------------------
// RoadGraph adapter
// ---------------------------------------------------------------------------------------------

GroundFn ground_fn(const Heightfield& hf) {
    return [&hf](double x, double y) { return hf.height(x, y); };
}

SampledRoad sample_edge_for_grading(const Edge& e, const std::string& type_id, double half_width, bool dual,
                                    double median_gap, const GradingParams& p) {
    SampledRoad r;
    r.type_id = type_id;
    r.half_width = half_width;
    r.dual = dual;
    r.median_gap = median_gap;
    const std::vector<Clothoid>& spans = e.horizontal.spans;
    if (spans.empty()) return r;
    // station arc lengths: per span at most sample_step apart and at most 0.05 rad of heading change per step
    std::vector<double> sv;
    std::vector<double> span_s0;
    double acc = 0;
    for (std::size_t i = 0; i < spans.size(); ++i) {
        const Clothoid& c = spans[i];
        span_s0.push_back(acc);
        const double turn = std::abs(c.k0 * c.length + 0.5 * c.dk * c.length * c.length);
        const int n = std::max({2, static_cast<int>(std::ceil(c.length / p.sample_step)), static_cast<int>(std::ceil(turn / 0.05))});
        for (int j = i == 0 ? 0 : 1; j <= n; ++j) sv.push_back(acc + c.length * j / n);
        acc += c.length;
    }
    const double total = acc, tol = 1e-6;
    // extra stations exactly at structure boundaries, so portal planes and abutments sit on the spans
    for (const StructureSpan& sp : e.structure)
        for (const double s : {sp.s0, sp.s1}) {
            if (s <= tol || s >= total - tol) continue;
            bool have = false;
            for (const double q : sv) have = have || std::abs(q - s) < tol;
            if (!have) sv.push_back(s);
        }
    std::sort(sv.begin(), sv.end());
    for (const double s : sv) {
        std::size_t si = 0;
        while (si + 1 < spans.size() && s > span_s0[si] + spans[si].length) ++si;
        const Clothoid& c = spans[si];
        const double u = std::clamp(s - span_s0[si], 0.0, c.length);
        const Vec2 pt = c.point(u);
        RoadSample rs;
        rs.s = s;
        rs.x = pt.x;
        rs.y = pt.y;
        rs.h = c.heading(u);
        rs.z = e.vertical.z(s);
        for (const StructureSpan& sp : e.structure) {
            if (s < sp.s0 - tol || s > sp.s1 + tol) continue;
            if (sp.kind == StructureKind::Bridge) rs.br = true;
            else if (sp.kind == StructureKind::Tunnel) rs.tu = true;
        }
        if (rs.br) rs.tu = false;
        r.samples.push_back(rs);
    }
    return r;
}

std::vector<SampledRoad> sample_graph_for_grading(const RoadGraph& g, const GradingParams& p) {
    std::vector<SampledRoad> out;
    const RegionPack* region = g.region();
    if (!region) return out;
    for (const auto& [id, e] : g.edges()) {
        const RoadType* rt = region->road_type(e.road_type);
        if (!rt) continue;
        const double hw = cross_section_width(instantiate_lanes(e, *rt, region->driving_side)) / 2;
        out.push_back(sample_edge_for_grading(e, rt->id, hw, rt->dual, rt->median_gap, p));
    }
    return out;
}

}  // namespace cp
