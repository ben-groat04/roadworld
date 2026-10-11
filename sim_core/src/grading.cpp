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
    p.max_tiers = r.max_tiers;
    p.wall_slope_extra = r.wall_slope_extra;
    p.slope_max_ratio = r.slope_max_ratio;
    p.slope_run_slack = r.slope_run_slack;
    p.slope_max_run = r.slope_max_run;
    p.berm_slope = r.berm_slope;
    p.wall_hard_max = r.wall_hard_max;
    p.rock_cut_batter = r.rock_cut_batter;
    p.rock_berm_width = r.rock_berm_width;
    p.rock_bench_height = r.rock_bench_height;
    p.fill_steep_batter = r.fill_steep_batter;
    p.corridor_max = r.corridor_max;
    p.wall_vis_min = r.wall_vis_min;
    p.reach_fade = r.reach_fade;
    p.relax_iters = r.relax_iterations;
    p.relax_strength = r.relax_strength;
    p.relax_margin = r.relax_margin;
    p.relax_max_delta = r.relax_max_delta;
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
bool slope_fits(bool cut, double L, double s0, double E, double bt, const Probe& grd, const Contact* cn, double from,
                const GradingParams& p) {
    for (double e = from; e < E + 0.5; e += 0.5) {
        const double ee = std::min(e, E), run = ee > s0 ? ee - s0 : 0.0;
        const double zs = cut ? L + run / bt : L - run / bt;
        double bz = 0;
        if (cn && (*cn)(ee, bz)) return cut ? zs >= bz : zs <= bz;  // landed on a neighbour's footprint
        if (cut ? zs >= grd(ee) : zs <= grd(ee)) {  // daylights, but only efficiently (else it chases the hillside and the wall takes the height)
            const double H0 = std::max(0.0, cut ? grd(s0) - L : L - grd(s0));
            return ee - s0 <= p.slope_max_ratio * H0 * bt + p.slope_run_slack;
        }
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
        if (t.n >= min_tiers && slope_fits(cut, L, s0, t.n ? std::min(E + p.wall_slope_extra, s0 + p.slope_max_run) : E, bt, grd, cn, t.n ? s0 : 0.5, p)) {
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

// ---- terraced sections (port of earthworks.js stairProfile / stairHeight / stairsPlanH) ----
struct StairTier {
    double a, f, u, eps;  // lateral start, front level, top level, ground still unretained above the top (0: reaches the ground)
};
struct StairResult {
    std::vector<StairTier> tiers;
    bool ok = true;
};

double tier_pitch(const GradingParams& p) { return p.wall_thickness + p.bench_min_width; }

constexpr double kFadeK = 6.0;       // the strip behind a full tier carves at most kFadeK * (height still to retain) below natural ground (internal)
constexpr double kSteepReach = 45.0; // reach limit of a steep fill that never meets the ground (internal)

// mt: tiers allowed (1 or 2); ok = the last tier reaches the ground.
StairResult stair_profile(bool cut, double zf, double h, const Probe& N, int mt, const GradingParams& p) {
    const double sg = cut ? 1.0 : -1.0, T = p.wall_thickness, P = tier_pitch(p);
    StairResult r;
    double F = zf;
    for (int k = 0; k < mt; ++k) {
        const double a = k * P, eb = a + T;
        const double rise = sg * (N(eb) - F);
        if (rise <= 0) { r.ok = true; return r; }  // natural ground at the back face is at or below the front level: nothing to retain here
        if (rise <= h) { r.tiers.push_back({a, F, F + sg * rise, 0.0}); r.ok = true; return r; }
        const double u = F + sg * h, eps = rise - h;
        r.tiers.push_back({a, F, u, eps});
        if (k == mt - 1) { r.ok = false; return r; }
        const double e1 = (k + 1) * P, n1 = N(e1), strip = u + sg * p.berm_slope * (e1 - eb);
        F = cut ? std::min(n1, std::max(strip, n1 - kFadeK * eps)) : std::max(n1, std::min(strip, n1 + kFadeK * eps));
    }
    r.ok = false;
    return r;
}

double sv_top(double e, const GradingParams& p) {
    return e <= p.verge ? 0.0 : p.round_hinge > 0 ? (e < p.verge + p.round_hinge ? (e - p.verge) * (e - p.verge) / (2 * p.round_hinge) : e - p.verge - p.round_hinge / 2) : e - p.verge;
}

// Engineered rock-cut face (kind 1): verge, rock-trap ditch, then a face at rock_cut_batter (h per v) rising rock_bench_height, a rock_berm_width catch berm, and so on.
// Steep fill (kind 2): a plain slope at fill_steep_batter. Natural ground clips both.
double surf_z(int kind, double e, double zf, const GradingParams& p) {
    if (kind == 1) {
        if (e <= p.verge) return zf;
        const double e0 = p.verge + p.toe_ditch_width;
        if (e < e0) {
            const double u = std::sin(3.14159265358979323846 * (e - p.verge) / p.toe_ditch_width);
            return zf - 1.5 * p.toe_ditch_depth * u * u;
        }
        const double d = e - e0, fh = p.rock_bench_height * p.rock_cut_batter, per = fh + p.rock_berm_width;
        const double n = std::floor(d / per), r = d - n * per;
        return zf + n * p.rock_bench_height + (r <= fh ? r / p.rock_cut_batter : p.rock_bench_height);
    }
    return zf - sv_top(e, p) / p.fill_steep_batter;
}

struct RunTreat {
    int kind = 0;  // 0 terrace, 1 rock, 2 steep fill
    double h = 0;
    int mt = 0;
};

// A plan evaluated on the spot. own: what this station alone needs (0 two capped tiers, 1 one tall wall, 2 steep fill, 3 rock cut).
WallPlan profile_plan(bool cut, double z0, const Probe& N, const RunTreat& run, int own, const GradingParams& p) {
    const double sg = cut ? 1.0 : -1.0;
    double reach_e = 0;
    int n = 0;
    if (run.kind == 0) {
        const StairResult pr = stair_profile(cut, z0, run.h, N, run.mt, p);
        n = static_cast<int>(pr.tiers.size());
        reach_e = n ? pr.tiers.back().a + p.wall_thickness : 0.0;
    } else {
        const double lim = run.kind == 2 ? std::min(p.corridor_max, kSteepReach) : p.corridor_max;
        for (double e = 0.5; e <= lim; e += 1) {
            reach_e = e;
            const double z = surf_z(run.kind, e, z0, p);
            if (cut ? z >= N(e) : z <= N(e)) break;
        }
        reach_e += 2;
    }
    WallPlan o;
    o.present = true;
    o.cut = cut;
    o.t.cut = cut;
    o.stairs = true;
    o.kind = run.kind;
    o.own = own;
    o.h = run.h;
    o.mt = run.mt;
    o.rock = run.kind == 1;
    o.needs = own == 2;
    o.n_stairs = n;
    o.over = run.kind == 0 && run.h > p.wall_max_height + 1e-9;
    o.reach_e = reach_e;
    o.along_max = std::max(0.0, sg * (N(p.wall_thickness) - z0)) * std::max(p.end_taper_batter, 1.0);
    return o;
}

// The candidate plan of one station when one wall and a slope do not do (preference: two tiers <= cap; fill: one wall <= wall_hard_max; else rock cut / steep fill).
WallPlan stairs_plan(bool cut, double z0, const Probe& N, const GradingParams& p) {
    const RunTreat t2{0, p.wall_max_height, p.max_tiers};
    if (stair_profile(cut, z0, t2.h, N, t2.mt, p).ok) return profile_plan(cut, z0, N, t2, 0, p);
    if (!cut) {
        const RunTreat t1{0, p.wall_hard_max, 1};
        if (stair_profile(false, z0, t1.h, N, 1, p).ok) return profile_plan(false, z0, N, t1, 1, p);
        return profile_plan(false, z0, N, RunTreat{2, 0, 0}, 2, p);
    }
    return profile_plan(true, z0, N, RunTreat{1, 0, 0}, 3, p);
}

// A run (contiguous stations, same side and cut) keeps ONE treatment: rock cut / steep fill if any station needs it, else one tall wall if any needs it, else two capped tiers.
RunTreat run_treatment(const std::vector<int>& owns, const GradingParams& p) {
    bool rock = false, sf = false, hard = false;
    for (const int o : owns) { rock = rock || o == 3; sf = sf || o == 2; hard = hard || o == 1; }
    if (rock) return {1, 0, 0};
    if (sf) return {2, 0, 0};
    if (hard) return {0, p.wall_hard_max, 1};
    return {0, p.wall_max_height, p.max_tiers};
}

// One structure between two roads at different levels, built by the lower one.
bool owner_tiers(double Dz, double ec, const GradingParams& p, WallPlan& out) {
    const double T = p.wall_thickness;
    const int n_req = std::max(1, static_cast<int>(std::ceil(Dz / p.wall_max_height - 1e-9)));
    int n_fit = 0;
    for (int n = 1; n <= std::min(kMaxWallTiers, p.max_tiers); ++n)
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
            {
                WallPlan tmp;
                if (owner_tiers(-Dz, owner_ec, p, tmp)) {  // the lower road builds the structure up to this road: nothing to do on this side
                    out = WallPlan{};
                    out.sup = true;
                    return true;
                }
            }
            if (owner_nat > owner_bz + 0.25) return false;
        }
    }
    out = WallPlan{};
    out.present = true;
    out.cut = cut;
    if (r.kind == TierResult::Fail || (r.kind == TierResult::Tiers && r.t.n >= 2)) {  // one wall and a slope cannot keep the cap: terraces
        out = stairs_plan(cut, z0, gB, p);
        return true;
    }
    out.t = r.t;
    out.t.cut = cut;
    out.t.plat = false;
    out.over = over;
    return true;
}

// Drop true runs whose arc length (plus one nominal step) is under min_len metres.
// ... unless a station of the run really needs a wall (keep): a plain slope could not daylight there.
void drop_short_len(Flags& a, const std::vector<RoadSample>& S, double min_len, double step, const Flags& keep) {
    const int n = static_cast<int>(a.size());
    int i = 0;
    while (i < n) {
        if (!a[static_cast<std::size_t>(i)]) { ++i; continue; }
        int j = i;
        while (j < n && a[static_cast<std::size_t>(j)]) ++j;
        bool need = false;
        for (int k = i; k < j; ++k) need = need || keep[static_cast<std::size_t>(k)];
        if (!need && S[static_cast<std::size_t>(j - 1)].s - S[static_cast<std::size_t>(i)].s + step < min_len - 1e-9)
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
    std::vector<int> sup[2] = {std::vector<int>(n, 0), std::vector<int>(n, 0)};
    const int ni = static_cast<int>(n);
    auto idx = [](int i) { return static_cast<std::size_t>(i); };
    for (int k = 0; k < 2; ++k) {
        const int sg = k == 0 ? 1 : -1;
        std::vector<int>& t = typ[k];
        std::vector<WallPlan>& pl = plans[k];
        Flags w(n, 0), wing(n, 0), need_raw(n, 0);
        for (std::size_t i = 0; i < n; ++i)
            if (!lock[i]) {
                WallPlan wp;
                if (plan_side(S[i], hw, sg, ground, &ng, -1, 0, p, wp)) {
                    if (wp.sup) sup[k][i] = 1;
                    else pl[i] = wp;
                }
                t[i] = pl[i].present ? (pl[i].cut ? 1 : 2) : 0;
                w[i] = t[i] > 0;
                need_raw[i] = w[i];
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
        drop_short_len(w, S, p.wall_min_length, p.sample_step, need_raw);  // a wall run shorter than one panel is a slope (unless the ground needs a wall there)
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
        // one tier height per terraced run (a continuous run keeps one set of benches); the hard-limit flag is shared too
        for (int i = 0; i < ni;) {
            if (!(w[idx(i)] && pl[idx(i)].stairs)) { ++i; continue; }
            const bool cut0 = pl[idx(i)].cut;
            int j = i;
            std::vector<int> owns;
            while (j < ni && w[idx(j)] && pl[idx(j)].stairs && pl[idx(j)].cut == cut0) {
                owns.push_back(pl[idx(j)].own);
                ++j;
            }
            const RunTreat run = run_treatment(owns, p);
            for (int q = i; q < j; ++q) {
                const RoadSample& s = S[idx(q)];
                const double nx = -std::sin(s.h) * sg, ny = std::cos(s.h) * sg, z0 = s.z - p.formation_drop;
                const Probe N = [&](double e) { return ground(s.x + nx * (hw + e), s.y + ny * (hw + e)); };
                pl[idx(q)] = profile_plan(cut0, z0, N, run, pl[idx(q)].own, p);
            }
            i = j;
        }
        // beyond either end of a terraced run the neighbouring stations (one wall and a slope, or a single wall to the ground) blend into the terraces over wall_ramp: ground = alt + sw * (terraces - alt)
        {
            std::vector<double> nearv(n, 1e9);
            for (int pass = 0; pass < 2; ++pass)
                for (int q = pass ? ni - 1 : 0; pass ? q >= 0 : q < ni; q += pass ? -1 : 1) {
                    if (w[idx(q)] && !wing[idx(q)] && pl[idx(q)].stairs) { nearv[idx(q)] = 0; continue; }
                    const int pq = q - (pass ? -1 : 1);
                    if (pq >= 0 && pq < ni && nearv[idx(pq)] < 1e8)
                        nearv[idx(q)] = std::min(nearv[idx(q)], nearv[idx(pq)] + std::abs(S[idx(q)].s - S[idx(pq)].s));
                }
            for (int q = 0; q < ni; ++q) {
                const WallPlan alt = pl[idx(q)];
                if (!(w[idx(q)] && !wing[idx(q)] && alt.present && !alt.stairs && !alt.full && !alt.t.plat && nearv[idx(q)] > 0 &&
                      nearv[idx(q)] < p.wall_ramp))
                    continue;
                int src = -1;
                double bd = 1e9;
                for (int j = 0; j < ni; ++j)
                    if (w[idx(j)] && !wing[idx(j)] && pl[idx(j)].stairs && pl[idx(j)].cut == alt.cut &&
                        std::abs(S[idx(j)].s - S[idx(q)].s) < bd) {
                        bd = std::abs(S[idx(j)].s - S[idx(q)].s);
                        src = j;
                    }
                if (src < 0) continue;
                const double u = 1 - nearv[idx(q)] / p.wall_ramp;
                const RoadSample& s = S[idx(q)];
                const double nx = -std::sin(s.h) * sg, ny = std::cos(s.h) * sg;
                const Probe N = [&](double e) { return ground(s.x + nx * (hw + e), s.y + ny * (hw + e)); };
                WallPlan np = profile_plan(alt.cut, s.z - p.formation_drop, N, RunTreat{pl[idx(src)].kind, pl[idx(src)].h, pl[idx(src)].mt}, pl[idx(src)].own, p);
                np.has_alt = true;
                np.alt = alt.t;
                np.sw = u * u * (3 - 2 * u);
                pl[idx(q)] = np;
            }
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
            bool has_base = false, base_stairs = false;
            int base_mt = 0, base_kind = 0;
            double base_h = 0;
            TierSet base;
            if (far_idx >= 0 && far_idx < ni && w[idx(far_idx)] && !wing[idx(far_idx)] && t[idx(far_idx)] == t[idx(i)] &&
                pl[idx(far_idx)].present && !pl[idx(far_idx)].full) {
                has_base = true;
                base = pl[idx(far_idx)].t;
                base_stairs = pl[idx(far_idx)].stairs;
                base_h = pl[idx(far_idx)].h;
                base_mt = pl[idx(far_idx)].mt;
                base_kind = pl[idx(far_idx)].kind;
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
                wp.base_stairs = base_stairs;
                wp.base_h = base_h;
                wp.base_mt = base_mt;
                wp.base_kind = base_kind;
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
        S[i].rockL = plans[0][i].present && plans[0][i].rock;
        S[i].rockR = plans[1][i].present && plans[1][i].rock;
        S[i].needsL = plans[0][i].present && plans[0][i].needs;
        S[i].needsR = plans[1][i].present && plans[1][i].needs;
        S[i].sL = sup[0][i] && !S[i].wL;
        S[i].sR = sup[1][i] && !S[i].wR;
        if (S[i].pL.over || S[i].pR.over) road.over_height = true;
    }
}

void classify_road(SampledRoad& road, const GroundFn& ground, const GradingParams& p, StructureMode mode) {
    classify_structures(road, ground, p, mode);
    classify_walls(road, ground, p, {});
}

// [lateral reach beyond the footprint edge, run-out beyond a free end] of the terraced sides
static void stair_extent(const SampledRoad& r, double& e, double& al) {
    e = 0;
    al = 0;
    const std::vector<RoadSample>& S = r.samples;
    for (std::size_t i = 0; i < S.size(); ++i)
        for (int side = 0; side < 2; ++side) {
            const WallPlan& pp = side == 0 ? S[i].pL : S[i].pR;
            if (!(side == 0 ? S[i].wL : S[i].wR) || !pp.present || !pp.stairs) continue;
            e = std::max(e, pp.reach_e);
            if (i == 0 || i + 1 == S.size()) al = std::max(al, pp.along_max);
        }
}

Box2 road_box(const SampledRoad& road, const GradingParams& p) {
    double s_e = 0, s_a = 0;
    stair_extent(road, s_e, s_a);
    const double hw0 = road.half_width;
    const double m = std::max(reach_cap(hw0, p) + p.wall_slope_extra, hw0 + s_e + 4) + p.box_margin,
                 me = std::max({end_reach_cap(hw0, p) + p.wall_slope_extra, hw0 + s_e + 4, s_a + hw0 + 4}) + p.box_margin;
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
            // a terraced run keeps full weight up to the road's own end (its walls end diagonally beyond it)
            const WallPlan& p0 = side == 0 ? S[static_cast<std::size_t>(i)].pL : S[static_cast<std::size_t>(i)].pR;
            const bool stairs_run = p0.present && p0.stairs;
            const bool free0 = (i == 0 && !stairs_run) || (i > 0 && !S[static_cast<std::size_t>(i - 1)].br && !S[static_cast<std::size_t>(i - 1)].tu);
            const bool free1 = (j == n && !stairs_run) || (j < n && !S[static_cast<std::size_t>(j)].br && !S[static_cast<std::size_t>(j)].tu);
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
    double mx = 0, stair_e = 0;
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
                if (pp.present && pp.stairs) stair_e = std::max(stair_e, pp.reach_e);
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
    double reach = any || tiers ? std::max(cap + (tiers ? p.wall_slope_extra : 0.0), meas) : meas;
    if (stair_e > 0) reach = std::max(reach, hw + stair_e + 4);  // terraced sides reach as far as their last tier
    return reach;
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
    {  // the upper road of a shared structure builds nothing on that side while the lower road's structure is at full weight (its own earthworks come back as the structure tapers at the ends)
        std::vector<SampledRoad*> rs;
        std::vector<Box2> boxes;
        for (SampledRoad& r : roads)
            if (r.samples.size() > 1) { rs.push_back(&r); boxes.push_back(road_box(r, p)); }
        for (std::size_t a = 0; a < rs.size(); ++a)
            for (RoadSample& s : rs[a]->samples) {
                s.supL = s.supR = 0;
                if (!(s.sL || s.sR)) continue;
                double best = 1e18, w = 0;
                for (std::size_t b = 0; b < rs.size(); ++b) {
                    if (a == b) continue;
                    const Box2 &x = boxes[a], &y = boxes[b];
                    if (y.x1 < x.x0 || y.x0 > x.x1 || y.y1 < x.y0 || y.y0 > x.y1) continue;
                    for (const RoadSample& q : rs[b]->samples) {
                        const int pl = q.wL && q.pL.present && q.pL.t.plat ? 1 : q.wR && q.pR.present && q.pR.t.plat ? 2 : 0;
                        if (!pl) continue;
                        const double d = (q.x - s.x) * (q.x - s.x) + (q.y - s.y) * (q.y - s.y);
                        if (d < best) { best = d; w = pl == 1 ? q.wwL : q.wwR; }
                    }
                }
                if (best < 1e17) { if (s.sL) s.supL = w; if (s.sR) s.supR = w; }
            }
    }
    std::vector<GradingRoad> out;
    for (const SampledRoad& r : roads) {
        if (r.samples.size() < 2) continue;
        const std::vector<RoadSample>& full = r.samples;
        GradingRoad g;
        g.half_width = r.half_width;
        g.reach = slope_reach(r, ground, p);
        g.bbox = road_box(r, p);
        g.ground = ground;
        {
            const RoadSample* ends[2] = {&full.front(), &full.back()};
            for (const RoadSample* q : ends)
                for (const WallPlan* pp : {q->wL ? &q->pL : nullptr, q->wR ? &q->pR : nullptr})
                    if (pp && pp->present && pp->stairs) g.end_ext = std::max(g.end_ext, pp->along_max + r.half_width + 2);
        }
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
            if (!(i % 2 == 0 || i == n - 1 || full[i - 1].tu != a.tu || full[i + 1].tu != a.tu || full[i - 1].br != a.br || full[i + 1].br != a.br)) continue;
            GradingSample d;
            d.x = a.x; d.y = a.y; d.z = a.z;
            d.wwL = a.wwL; d.wwR = a.wwR;
            d.wL = static_cast<std::int8_t>(a.wL); d.wR = static_cast<std::int8_t>(a.wR);
            d.supL = a.supL; d.supR = a.supR;
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

// Does the straight way (qx, qy) -> (tx, ty) enter the footprint of a neighbouring road (strip of half-width sqrt(l2) beside its centreline)? The round end cap of a FREE end does
// not count: it is only a temporary finish (the player will extend the road), so a slope of another road may run over it. Sampled every 0.5 m.
bool path_hits_footprint(double qx, double qy, double tx, double ty, const std::vector<GradingSample>& Sb, double l2) {
    const bool cap_first = !Sb.front().br && !Sb.front().tu, cap_last = !Sb.back().br && !Sb.back().tu;
    const double L = std::hypot(tx - qx, ty - qy);
    const int n = std::max(1, static_cast<int>(std::ceil(L / 0.5)));
    for (int i = 0; i <= n; ++i) {
        const double x = qx + (tx - qx) * i / n, y = qy + (ty - qy) * i / n;
        double bd = 1e30, bu = 0;  // the nearest point of the centreline decides: beyond a free end it is the end vertex itself (the cap)
        std::size_t bk = 0;
        for (std::size_t k = 0; k + 1 < Sb.size(); ++k) {
            const GradingSample &A = Sb[k], &B = Sb[k + 1];
            if ((A.br || A.tu) && (B.br || B.tu)) continue;
            const double dx = B.x - A.x, dy = B.y - A.y, L2 = dx * dx + dy * dy;
            double u = L2 > 1e-12 ? ((x - A.x) * dx + (y - A.y) * dy) / L2 : 0.0;
            const double ur = u;
            u = u < 0 ? 0 : u > 1 ? 1 : u;
            const double ex = x - A.x - u * dx, ey = y - A.y - u * dy, d2 = ex * ex + ey * ey;
            if (d2 < bd) { bd = d2; bk = k; bu = ur; }
        }
        if (bd < l2 && !((bu < 0 && cap_first && bk == 0) || (bu > 1 && cap_last && bk + 2 == Sb.size()))) return true;
    }
    return false;
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

// Ground value of a terraced section at lateral e: same convention as tier_eval. zf: formation level (lifted beyond a free end); N(e): natural ground along the normal.
// The sloped strip behind a full tier, never carved more than kFadeK * eps below natural ground.
double bench_z(bool cut, const StairTier& t, double e, double n, const GradingParams& p) {
    const double strip = t.u + (cut ? 1.0 : -1.0) * p.berm_slope * (e - (t.a + p.wall_thickness));
    return cut ? std::max(strip, n - kFadeK * t.eps) : std::min(strip, n + kFadeK * t.eps);
}

Eval stair_eval(bool cut, int kind, double h, int mt, double e, double zf, double b, double res, double wall_t, const Probe& N,
                const GradingParams& p) {
    if (kind != 0) return {cut ? 2 : 3, surf_z(kind, e, zf, p)};  // rock cut / steep fill: a plain surface, natural ground clips it
    const StairResult pr = stair_profile(cut, zf, h, N, mt, p);
    const int k = static_cast<int>(std::floor(e / tier_pitch(p) + 1e-12));
    if (k >= static_cast<int>(pr.tiers.size())) return {0, 0};
    const StairTier& t = pr.tiers[static_cast<std::size_t>(k)];
    if (e <= t.a + wall_t - res) return {1, t.f};
    if (e <= t.a + wall_t) return cut ? Eval{1, t.u} : Eval{3, t.u};
    if (t.eps <= 0) return {0, 0};
    return {cut ? 2 : 3, bench_z(cut, t, e, b, p)};
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
    // per-texel rounding: the hinge and the daylight roll-over never take more than a third of the slope's length each (short banks are not all rounding)
    double hbc = hb, rkc = rk;
    // verge -> slope hinge: parabola over hbc, then the straight slope shifted out by hbc / 2
    auto sv = [&](double e) {
        return e <= verge ? 0.0 : hbc > 0 ? (e < verge + hbc ? (e - verge) * (e - verge) / (2 * hbc) : e - verge - hbc / 2) : e - verge;
    };
    // smooth max(d, 0) over +-rk: rolls a slope over into natural ground
    auto smax = [&](double d, double k) { return d >= k ? d : d <= -k ? 0.0 : (d + k) * (d + k) / (4 * k); };
    // displacement a recorded cut / fill surface causes at base b (weight r): across roads the winner moves the ground most, not the one that is highest
    auto dC = [&](double v, double b, double r, double k) { return v >= 1e8 ? 0.0 : r * (k > 0 ? smax(b - v, k) : std::max(0.0, b - v)); };
    auto dF = [&](double v, double b, double r, double k) { return v <= -1e8 ? 0.0 : r * (k > 0 ? smax(v - b, k) : std::max(0.0, v - b)); };
    // best candidate (cut_m / e / r) + the second-best weighted displacement (d2) + the largest unweighted one (u): where a structure tapers
    // out and a neighbour's own slope fades in, the two weighted displacements ADD (a crossfade), capped by the largest full one
    auto recC = [&](std::size_t o, double v, double e, double b, double r, double dit = 0.0) {
        const double d = dC(v, b, r, rkc), u = dC(v, b, 1, rkc), c = dC(ws.cut_m[o], b, ws.cut_r[o], ws.cut_k2[o]);
        if (u > ws.cut_u[o]) ws.cut_u[o] = static_cast<float>(u);
        if (d > c) {
            if (c > ws.cut_d2[o]) ws.cut_d2[o] = static_cast<float>(c);
            ws.cut_m[o] = static_cast<float>(v); ws.cut_e[o] = static_cast<float>(e); ws.cut_r[o] = static_cast<float>(r); ws.cut_k2[o] = static_cast<float>(rkc);
            ws.cut_dit[o] = static_cast<float>(dit);
            return true;
        }
        if (d > ws.cut_d2[o]) ws.cut_d2[o] = static_cast<float>(d);
        return false;
    };
    auto recF = [&](std::size_t o, double v, double e, double b, double r) {
        const double d = dF(v, b, r, rkc), u = dF(v, b, 1, rkc), c = dF(ws.fill_m[o], b, ws.fill_r[o], ws.fill_k2[o]);
        if (u > ws.fill_u[o]) ws.fill_u[o] = static_cast<float>(u);
        if (d > c) {
            if (c > ws.fill_d2[o]) ws.fill_d2[o] = static_cast<float>(c);
            ws.fill_m[o] = static_cast<float>(v); ws.fill_e[o] = static_cast<float>(e); ws.fill_r[o] = static_cast<float>(r); ws.fill_k2[o] = static_cast<float>(rkc);
            return true;
        }
        if (d > ws.fill_d2[o]) ws.fill_d2[o] = static_cast<float>(d);
        return false;
    };
    // wall band: texels with e <= wall_thickness - res take the inside (formation) value, so the
    // one-texel ramp to natural lies inside the solid
    const double wall_t = p.wall_thickness, half = wall_t - res;
    const double tx1 = x0 + (nx - 1) * res, ty1 = y0 + (ny - 1) * res;
    ws.fp_d.assign(N, 9.0f);
    ws.fp_z.assign(N, 1e9f);
    ws.end_ceil.assign(N, 1e9f);
    ws.cap_f.assign(N, 0);
    ws.cut_m.assign(N, 1e9f);
    ws.fill_m.assign(N, -1e9f);
    ws.cut_r.assign(N, 1.0f);
    ws.cut_k.assign(N, 0);
    ws.fix.assign(N, 0);
    if (ws.rq_st.size() < N) ws.rq_st.resize(N);
    ws.fill_r.assign(N, 1.0f);
    ws.cut_d2.assign(N, 0.0f);
    ws.cut_k2.assign(N, 0.0f);
    ws.cut_dit.assign(N, 0.0f);
    ws.fill_k2.assign(N, 0.0f);
    ws.fill_d2.assign(N, 0.0f);
    ws.cut_u.assign(N, 0.0f);
    ws.fill_u.assign(N, 0.0f);
    ws.rq_d.assign(N, 1e9f);  // per road: nearest-station formation (reset after each road); across roads: lowest wins
    for (std::vector<float>* v : {&ws.rq_ww, &ws.rq_z, &ws.cut_e, &ws.fill_e, &ws.rq_ex, &ws.rq_ey, &ws.rq_bt, &ws.rq_fw, &ws.rq_al, &ws.rq_fs, &ws.rq_sp, &ws.rq_sv})
        if (v->size() < N) v->resize(N);
    if (ws.rq_w.size() < N) ws.rq_w.resize(N);
    if (ws.rq_fl.size() < N) ws.rq_fl.resize(N);
    if (ws.rq_by.size() < N) ws.rq_by.resize(N);
    if (ws.rq_p.size() < N) ws.rq_p.resize(N);
    std::vector<float> &fp_d = ws.fp_d, &fp_z = ws.fp_z, &cut_m = ws.cut_m, &fill_m = ws.fill_m, &rq_ww = ws.rq_ww,
                       &rq_d = ws.rq_d, &rq_z = ws.rq_z, &cut_e = ws.cut_e, &fill_e = ws.fill_e, &rq_ex = ws.rq_ex,
                       &rq_ey = ws.rq_ey, &rq_bt = ws.rq_bt, &rq_fw = ws.rq_fw, &rq_al = ws.rq_al, &rq_fs = ws.rq_fs, &rq_sp = ws.rq_sp, &rq_sv = ws.rq_sv, &cut_r = ws.cut_r, &fill_r = ws.fill_r, &cut_d2 = ws.cut_d2, &fill_d2 = ws.fill_d2, &cut_u = ws.cut_u, &fill_u = ws.fill_u;
    std::vector<std::uint8_t>& cut_k = ws.cut_k;
    std::vector<std::uint8_t>& rq_w = ws.rq_w;
    std::vector<std::uint8_t>& rq_fl = ws.rq_fl;
    std::vector<std::uint8_t>& rq_by = ws.rq_by;
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
                if ((!nb.samples.front().br && !nb.samples.front().tu) || (!nb.samples.back().br && !nb.samples.back().tu)) {  // a road with a free end: the cap does not block
                    if (path_hits_footprint(qx, qy, tx, ty, nb.samples, l2)) return true;
                    continue;
                }
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
            const double Rs = (k == 0 || k == S.size() - 2) && rd.end_ext > R ? rd.end_ext : R;  // beyond a free end the terraces carry on straight (diagonal wall ends)
            const int i0 = std::max(0, static_cast<int>(std::floor((std::min(A.x, B.x) - Rs - x0) / res)));
            const int i1 = std::min(nx - 1, static_cast<int>(std::ceil((std::max(A.x, B.x) + Rs - x0) / res)));
            const int j0 = std::max(0, static_cast<int>(std::floor((std::min(A.y, B.y) - Rs - y0) / res)));
            const int j1 = std::min(ny - 1, static_cast<int>(std::ceil((std::max(A.y, B.y) + Rs - y0) / res)));
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
                        if (beyond) { ws.fix[static_cast<std::size_t>(j) * static_cast<std::size_t>(nx) + static_cast<std::size_t>(i)] |= 4; continue; }
                    }
                    double tt = (rx * dx + ry * dy) / L2;
                    // tunnel portal: grading stops exactly at the portal plane (the first tunnel station); bridges keep the rounded embankment end
                    if ((B.tu && !A.tu && tt > t_hi) || (A.tu && !B.tu && tt < t_lo)) continue;
                    const double t_raw = tt;
                    tt = tt < 0 ? 0 : tt > 1 ? 1 : tt;
                    double ex = rx - tt * dx, ey = ry - tt * dy, d2 = ex * ex + ey * ey;
                    int pro = 0;
                    double along_v = 0, fs = 0;
                    if ((k == 0 && t_raw < 0 && !A.br && !A.tu) || (k == S.size() - 2 && t_raw > 1 && !B.br && !B.tu)) {
                        // beyond a free end: a terraced side carries on straight, its levels rising (cut) / falling (fill) at the run-out batter, so the walls end diagonally into the ground
                        const GradingSample& Eq = k == 0 ? A : B;
                        const bool sdL = Eq.wL && Eq.pL.present && Eq.pL.stairs, sdR = Eq.wR && Eq.pR.present && Eq.pR.stairs;
                        if (sdL || sdR) {
                            const double crs = dx * ry - dy * rx, perp = crs / sq,
                                         along = t_raw < 0 ? -(rx * dx + ry * dy) / sq : (rx * dx + ry * dy) / sq - sq;
                            const double aL = sdL ? Eq.pL.along_max : 0.0, aR = sdR ? Eq.pR.along_max : 0.0;
                            if (std::abs(perp) <= R && along <= (perp > 0 ? aL : aR) + hw) {
                                pro = 1;
                                along_v = along;
                                ex = rx - t_raw * dx;
                                ey = ry - t_raw * dy;
                                d2 = perp * perp;
                                if (std::abs(perp) < hw) {
                                    pro = 2;
                                    const double sL = sdL ? (Eq.pL.cut ? 1.0 : -1.0) : 0.0, sR = sdR ? (Eq.pR.cut ? 1.0 : -1.0) : 0.0,
                                                 u0 = (perp + hw) / (2 * hw), u1 = std::min(1.0, std::max(0.0, (u0 - 0.35) / 0.3)),
                                                 u = u1 * u1 * (3 - 2 * u1);
                                    fs = sR + (sL - sR) * u;
                                }
                            }
                        }
                    }
                    if (!pro && d2 > R2) continue;
                    const double dd = std::sqrt(d2);
                    const std::size_t o = static_cast<std::size_t>(j) * static_cast<std::size_t>(nx) + static_cast<std::size_t>(i);
                    if (dd >= rq_d[o]) continue;  // nearest station of this road decides (one zf, one wall flag per texel)
                    rq_al[o] = static_cast<float>(along_v);
                    rq_fl[o] = pro == 2 ? 1 : 0;
                    rq_fs[o] = static_cast<float>(fs);
                    rq_by[o] = !pro && (t_raw < 0 || t_raw > 1)
                                   ? (((k == 0 && t_raw < 0 && !A.br && !A.tu) || (k == S.size() - 2 && t_raw > 1 && !B.br && !B.tu)) ? 2 : 1)
                                   : 0;
                    ws.rq_st[o] = (A.br || A.tu || B.br || B.tu) ? 1 : 0;
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
                    rq_sp[o] = static_cast<float>(lf ? E.supL : E.supR);
                    {  // the terraces' share beside a terraced run, interpolated between the stations like the other weights
                        const WallPlan &pa = lf ? A.pL : A.pR, &pb = lf ? B.pL : B.pR;
                        const double ya = pa.present && pa.stairs ? (pa.has_alt ? pa.sw : 1.0) : 0.0, yb = pb.present && pb.stairs ? (pb.has_alt ? pb.sw : 1.0) : 0.0;
                        rq_sv[o] = static_cast<float>(a_str ? yb : b_str ? ya : ya + (yb - ya) * tt);
                    }
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
            if (ws.rq_st[o]) ws.fix[o] |= 4;
            const double zf = rq_z[o];
            if (rq_by[o] == 2 && !rq_fl[o]) {  // beyond a free end the road's own run-out ceiling (rising at the end batter from the end station): where another road's fill stands there, it is cut down to it, so the road does not end against a cliff
                const float c = static_cast<float>(zf + dd / e_b);
                if (c < ws.end_ceil[o]) ws.end_ceil[o] = c;
                if (dd <= hw) ws.cap_f[o] = 1;
            }
            if (rq_fl[o]) {  // ahead of a free end of a terraced road: the floor rises / falls with the run-out batter (cut / fill side), across the width it is a crossfall between the two
                const double lift = rq_al[o] / e_b, fs = rq_fs[o], zl = zf + fs * lift, b0 = base[o];
                double zz = zf;
                if (fs > 0) zz = std::min(b0, zl);
                else if (fs < 0) zz = std::max(b0, zl);
                fp_d[o] = 0;
                if (zz < fp_z[o]) fp_z[o] = static_cast<float>(zz);
                continue;
            }
            if (dd <= hw) {  // union of footprints: lowest formation wins (never pokes through any road)
                if (rq_by[o] == 2) {  // the round end cap of a FREE end is only a temporary finish (the player will extend the road): a zero-width cut / fill surface at formation level in the ordinary displacement machinery, so it never overrides another road's earthworks and meets them without a step
                    const double b0 = base[o], h3c = std::abs(b0 - zf) / 3;
                    hbc = hb > 0 ? std::max(0.05, std::min(hb, h3c * bt)) : 0.0;
                    rkc = rk > 0 ? std::max(0.01, std::min(rk, h3c)) : 0.0;
                    if (b0 > zf) { if (dC(zf, b0, 1, rkc) > cut_d2[o]) recC(o, zf, 0, b0, 1); }
                    else if (dF(zf, b0, 1, rkc) > fill_d2[o]) recF(o, zf, 0, b0, 1);
                    continue;
                }
                fp_d[o] = 0;
                if (zf < fp_z[o]) fp_z[o] = static_cast<float>(zf);
                continue;
            }
            if (rq_sp[o] >= 1) continue;  // the neighbour's structure reaches this road: no earthworks of its own on this side (they fade back in as the structure tapers)
            const double e = dd - hw, b = base[o];
            { const double h3 = std::abs(b - zf) / 3; hbc = hb > 0 ? std::max(0.05, std::min(hb, h3 * bt)) : 0.0; rkc = rk > 0 ? std::max(0.01, std::min(rk, h3)) : 0.0; }  // rounding bound: each end at most a third of the slope (height H: length H * batter)
            const bool cut_t = b > zf;
            auto blk = [&]() {  // evaluated only when a slope would be recorded
                return blocked(o, static_cast<int>(o % static_cast<std::size_t>(nx)), static_cast<int>(o / static_cast<std::size_t>(nx)), dd);
            };
            int wf = rq_w[o];
            double ww = wf ? static_cast<double>(rq_ww[o]) : 0.0;
            const WallPlan* pl = wf ? rq_p[o] : nullptr;
            const double bte = rq_bt[o];
            // a plain slope that has not met the ground by the end of the road's reach fades into it (no cut face / pit at the reach limit)
            double rf = 1.0;
            if (p.reach_fade > 0 && dd > R - p.reach_fade) {
                const double u = (R - dd) / p.reach_fade;
                rf = u <= 0 ? 0.0 : u * u * (3 - 2 * u);
            }
            const double rfs = rf * (1.0 - static_cast<double>(rq_sp[o]));
            if (wf && pl && pl->stairs && rq_by[o] == 2) continue;  // beyond the run-out of a terraced free end: natural ground (no cone)
            if (wf && pl && pl->stairs && rq_by[o]) { wf = 0; ww = 0; pl = nullptr; }
            if (wf && rq_by[o] == 2 && !(pl && pl->stairs)) { wf = 0; ww = 0; pl = nullptr; }  // beyond a free end the wall band does not wrap around the road end as a ring of formation-level ground: the plain run-out slope  // terraces do not wrap around a bridge abutment / portal (the wing wall does that): plain run-out slope
            if (wf && !(pl && pl->t.plat) && (wf == 1) != cut_t) { wf = 0; ww = 0; pl = nullptr; }  // flag says cut but this texel is fill (or vice versa): plain slope
            if (wf && pl && (!pl->full || pl->wing)) {  // stacked walls (plan_side), or a wing wall blended in from the earthworks beside it
                Eval ev;
                const double tx = x0 + (o % static_cast<std::size_t>(nx)) * res, ty = y0 + static_cast<double>(o / static_cast<std::size_t>(nx)) * res;
                const double fxs = tx - rq_ex[o], fys = ty - rq_ey[o], uxs = rq_ex[o] / dd, uys = rq_ey[o] / dd;
                const Probe Ns = [&](double q) { return rd.ground(fxs + uxs * (hw + q), fys + uys * (hw + q)); };
                if (pl->stairs) {
                    ev = stair_eval(pl->cut, pl->kind, pl->h, pl->mt, e, zf + (pl->cut ? 1.0 : -1.0) * rq_al[o] / e_b, b, res, wall_t, Ns, p);
                    auto eff = [&](const Eval& q) { return q.kind == 1 ? q.v : q.kind == 0 ? b : pl->cut ? std::min(b, q.v) : std::max(b, q.v); };
                    if (pl->has_alt && rq_sv[o] < 1) {  // beside a terraced run: blend with the station's own plan (one wall and a slope, or a wall to the ground)
                        TierSet alt = pl->alt;
                        const Eval ea = tier_eval(alt, ww, e, zf, b, res, wall_t, bt);
                        const double ga = eff(ea), gs2 = eff(ev);
                        ev.kind = pl->cut ? 2 : 3;
                        ev.v = ga + static_cast<double>(rq_sv[o]) * (gs2 - ga);
                    }
                    if (ww < 1) {  // run ends: the terraced ground and the plain slope blend by the run weight
                        const double gs = eff(ev), gp = pl->cut ? std::min(b, zf + sv(e) / bt) : std::max(b, zf - sv(e) / bt);
                        ev.kind = pl->cut ? 2 : 3;
                        ev.v = gp + ww * (gs - gp);
                    }
                } else if (!pl->full) {
                    ev = tier_eval(pl->t, ww, e, zf, b, res, wall_t, bt);
                } else {  // wing: (1 - fw) * ordinary earthworks (adjacent run's tiers, else the plain slope) + fw * full wall
                    Eval eb;
                    if (pl->has_base && !(pl->base_stairs && rq_by[o]))
                        eb = pl->base_stairs ? stair_eval(pl->cut, pl->base_kind, pl->base_h, pl->base_mt, e, zf, b, res, wall_t, Ns, p)
                                             : tier_eval(pl->base, ww, e, zf, b, res, wall_t, bt);
                    else
                        eb = Eval{pl->cut ? 2 : 3, pl->cut ? zf + sv(e) / bt : zf - sv(e) / bt};
                    const Eval ef = full_eval(pl->cut, e, zf, b, res, wall_t, verge, bt);
                    if (eb.kind == 0) { eb.kind = ef.kind; eb.v = pl->cut ? b + rk : b - rk; }  // natural ground carries on beside the adjacent wall
                    ev.kind = eb.kind == 1 || ef.kind == 1 ? 1 : ef.kind;
                    // a solid of the adjacent terraces beside the wing: where the full wall has only a slope, the ground there is natural, not the slope level
                    const double vfe = eb.kind == 1 && ef.kind != 1 ? (pl->cut ? std::min(ef.v, b) : std::max(ef.v, b)) : ef.v;
                    ev.v = eb.v + static_cast<double>(rq_fw[o]) * (vfe - eb.v);
                }
                if (ev.kind == 0) continue;
                const double v = ev.v;
                if (ev.kind == 1) {
                    fp_d[o] = 0;
                    const double zw = pl->cut && !pl->t.plat ? std::min(v, b) : pl->stairs && !pl->cut ? std::max(v, b) : v;  // a shared structure (plat) builds the ground up to the neighbour whatever the natural ground does
                    if (zw < fp_z[o]) fp_z[o] = static_cast<float>(zw);
                    continue;
                }
                double rr = 1.0;  // rock cut / steep fill fade into the ground at the end of the reach like plain slopes, and out along the run-out beyond a free end
                if (pl->stairs && pl->kind != 0) {
                    const double u = rq_al[o] / (pl->along_max + 1e-9);
                    rr = rfs * (u <= 0 ? 1.0 : u >= 1 ? 0.0 : 1 - u * u * (3 - 2 * u));
                }
                if (ev.kind == 2) {
                    if (rr > 0 && v < b + rk && dC(v, b, rr, rkc) > cut_d2[o] && !blk()) { if (recC(o, v, e, b, rr)) cut_k[o] = pl->stairs && pl->kind == 1; }
                } else if (rr > 0 && v > b - rk && dF(v, b, rr, rkc) > fill_d2[o] && !blk()) {
                    recF(o, v, e, b, rr);
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
                if (rfs > 0 && v < b + rk && dC(v, b, rfs, rkc) > cut_d2[o] && !blk()) recC(o, v, e, b, rfs);
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
                if (rfs > 0 && v > b - rk && dF(v, b, rfs, rkc) > fill_d2[o] && !blk()) recF(o, v, e, b, rfs);
                continue;
            }
            if (cut_t) {  // toe ditch between verge and slope (fades out in shallow cuts), then the slope
                double v = zf + sv(e) / bt, dit = 0.0;
                if (d_d > 0 && d_w > 0 && e > verge && e < verge + d_w) {
                    const double u = std::sin(3.14159265358979323846 * (e - verge) / d_w);
                    dit = d_d * std::min(1.0, b - zf) * u * u;
                    v -= dit;
                }
                if (rfs > 0 && v < b + rk && dC(v, b, rfs, rkc) > cut_d2[o] && !blk()) recC(o, v, e, b, rfs, dit);
            } else {
                const double v = zf - sv(e) / bte;
                if (rfs > 0 && v > b - rk && dF(v, b, rfs, rkc) > fill_d2[o] && !blk()) recF(o, v, e, b, rfs);
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
            const double dc = cut_m[o] < 1e8f ? std::min(dC(cut_m[o], g, cut_r[o], ws.cut_k2[o]) + cut_d2[o], std::max(static_cast<double>(cut_u[o]), dC(cut_m[o], g, cut_r[o], ws.cut_k2[o]))) : 0.0;
            const double df = fill_m[o] > -1e8f ? std::min(dF(fill_m[o], g, fill_r[o], ws.fill_k2[o]) + fill_d2[o], std::max(static_cast<double>(fill_u[o]), dF(fill_m[o], g, fill_r[o], ws.fill_k2[o]))) : 0.0;
            // a cut slope and a fill slope of two roads meet: blend their displacements, each weighted by its size over
            // its distance to its own road (one continuous surface; a nearer-wins switch left a pit)
            if (dc > 0 && df > 0) {
                const double wc = dc / (static_cast<double>(cut_e[o]) + 1), wf = df / (static_cast<double>(fill_e[o]) + 1);
                g = g + (df * wf - dc * wc) / (wc + wf);
            } else if (dc > 0) g -= dc;
            else if (df > 0) g += df;

            if (ws.end_ceil[o] < 1e8f && (dc > 0 || df > 0)) {
                if (df > 0) g = std::min(g, std::max(static_cast<double>(base[o]), static_cast<double>(ws.end_ceil[o])));
                if (dc > 0 && ws.cap_f[o]) g = std::min(g, static_cast<double>(ws.end_ceil[o]));
            }
        }
        out[o] = static_cast<float>(g);
        if (fp_d[o] >= 9 && cut_m[o] < 1e8f && static_cast<double>(ws.cut_dit[o]) * cut_r[o] > 0.01) ws.fix[o] |= 8;  // the toe ditch is a designed feature: the smoothing pass leaves it alone
        if (footprint) footprint[o] = fp_d[o] < 9 ? 1 : (cut_k[o] && g < base[o] - 0.05 ? 2 : 0);  // 2: engineered rock-cut face (renderer: rock material)
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
// Smoothing: a constrained relaxation of the graded ground (port of relaxTile / gradeTile in earthworks.js)
// ---------------------------------------------------------------------------------------------

namespace {
constexpr double kFaceStep = 1.2;  // m between adjacent texels (a 1:2.4 face at 0.5 m texels is 1.0 m per texel): above this the step is a wall face / abutment, not ground
// Count of flagged texels in the (2r+1)^2 window around each texel (integral image: exact integers).
std::vector<std::int32_t> window_count(const std::vector<std::uint8_t>& flag, int nx, int ny, int r) {
    const int W = nx + 1;
    std::vector<std::int32_t> I(static_cast<std::size_t>(nx + 1) * static_cast<std::size_t>(ny + 1), 0), out(static_cast<std::size_t>(nx) * static_cast<std::size_t>(ny), 0);
    for (int j = 0; j < ny; ++j) {
        std::int32_t row = 0;
        for (int i = 0; i < nx; ++i) {
            row += flag[static_cast<std::size_t>(j) * nx + i];
            I[static_cast<std::size_t>(j + 1) * W + i + 1] = I[static_cast<std::size_t>(j) * W + i + 1] + row;
        }
    }
    for (int j = 0; j < ny; ++j)
        for (int i = 0; i < nx; ++i) {
            const int i0 = std::max(0, i - r), i1 = std::min(nx - 1, i + r) + 1, j0 = std::max(0, j - r), j1 = std::min(ny - 1, j + r) + 1;
            out[static_cast<std::size_t>(j) * nx + i] = I[static_cast<std::size_t>(j1) * W + i1] - I[static_cast<std::size_t>(j0) * W + i1] - I[static_cast<std::size_t>(j1) * W + i0] + I[static_cast<std::size_t>(j0) * W + i0];
        }
    return out;
}

// FREE texels: within m_tex of any texel the grading changed (or fixed) and not within 2 texels of a fixed one. Jacobi passes
// g' = (1 - s) g + s * mean of the 4 neighbours (outside the window a texel is its own neighbour), clamped to [lo, hi]: a cut texel
// never rises above natural ground, a fill texel never sinks below it, no texel moves more than max_d from its graded value.
std::vector<double> relax_tile(const float* g0, const float* nat, const std::vector<std::uint8_t>& fix, int nx, int ny, int iters, double s, int m_tex, double max_d,
                               std::vector<std::uint8_t>* free_out = nullptr) {
    const std::size_t N = static_cast<std::size_t>(nx) * static_cast<std::size_t>(ny);
    std::vector<std::uint8_t> fixed(N), changed(N);
    for (std::size_t o = 0; o < N; ++o) {
        fixed[o] = fix[o] ? 1 : 0;
        changed[o] = fix[o] || std::abs(static_cast<double>(g0[o]) - nat[o]) > 1e-4 ? 1 : 0;
    }
    // a texel on either side of a step of more than kFaceStep between neighbours is part of a face (wall face, abutment, portal step): fixed too
    for (int j = 0; j < ny; ++j)
        for (int i = 0; i < nx; ++i) {
            const std::size_t o = static_cast<std::size_t>(j) * nx + i;
            if (i + 1 < nx && std::abs(static_cast<double>(g0[o]) - g0[o + 1]) > kFaceStep) { fixed[o] = 1; fixed[o + 1] = 1; changed[o] = 1; changed[o + 1] = 1; }
            if (j + 1 < ny && std::abs(static_cast<double>(g0[o]) - g0[o + nx]) > kFaceStep) { fixed[o] = 1; fixed[o + nx] = 1; changed[o] = 1; changed[o + nx] = 1; }
        }
    const auto ring = window_count(fixed, nx, ny, 2), near = window_count(changed, nx, ny, m_tex);
    std::vector<std::uint8_t> free_(N);
    std::vector<double> lo(N), hi(N), a(N), b(N);
    for (std::size_t o = 0; o < N; ++o) {
        free_[o] = near[o] > 0 && ring[o] == 0 ? 1 : 0;
        const double d = static_cast<double>(g0[o]) - nat[o], g = g0[o];
        lo[o] = d > 1e-4 ? std::max(0.0, d - max_d) : d - max_d;
        hi[o] = d < -1e-4 ? std::min(0.0, d + max_d) : d + max_d;
        a[o] = d;  // the relaxation works on the displacement from natural ground (see relaxTile in earthworks.js)
    }
    for (int it = 0; it < iters; ++it) {
        for (int j = 0; j < ny; ++j)
            for (int i = 0; i < nx; ++i) {
                const std::size_t o = static_cast<std::size_t>(j) * nx + i;
                if (!free_[o]) { b[o] = a[o]; continue; }
                const double c = a[o], l = i > 0 ? a[o - 1] : c, r = i < nx - 1 ? a[o + 1] : c, d = j > 0 ? a[o - nx] : c, u = j < ny - 1 ? a[o + nx] : c;
                double v = (1 - s) * c + s * ((l + r + d + u) / 4);
                if (v < lo[o]) v = lo[o]; else if (v > hi[o]) v = hi[o];
                b[o] = v;
            }
        std::swap(a, b);
    }
    if (free_out) *free_out = free_;
    for (std::size_t o = 0; o < N; ++o) a[o] += nat[o];
    return a;
}
}  // namespace

void grade_tile_smooth_into(const std::vector<GradingRoad>& roads, const GradingParams& p, const TileSpec& t, const GroundFn& natural,
                            float* out, std::uint8_t* footprint, GradeWorkspace& ws) {
    const std::size_t N = static_cast<std::size_t>(t.nx) * static_cast<std::size_t>(t.ny);
    if (N == 0) return;
    if (p.relax_iters <= 0) {
        const std::vector<float> nat = sample_natural_tile(natural, t);
        grade_tile_into(roads, p, t, nat.data(), out, footprint, ws);
        return;
    }
    const int m_tex = static_cast<int>(std::ceil(p.relax_margin / t.res)), H = p.relax_iters + m_tex + 2;
    TileSpec e = t;
    e.x0 = t.x0 - H * t.res;
    e.y0 = t.y0 - H * t.res;
    e.nx = t.nx + 2 * H;
    e.ny = t.ny + 2 * H;
    const std::size_t NE = static_cast<std::size_t>(e.nx) * static_cast<std::size_t>(e.ny);
    const std::vector<float> nat = sample_natural_tile(natural, e);
    std::vector<float> g0(NE);
    std::vector<std::uint8_t> fp(NE);
    grade_tile_into(roads, p, e, nat.data(), g0.data(), fp.data(), ws);
    std::vector<std::uint8_t> fix(NE);
    for (std::size_t o = 0; o < NE; ++o) fix[o] = fp[o] || (ws.fix[o] & (4 | 8)) ? 1 : 0;
    const std::vector<double> r = relax_tile(g0.data(), nat.data(), fix, e.nx, e.ny, p.relax_iters, p.relax_strength, m_tex, p.relax_max_delta);
    for (int j = 0; j < t.ny; ++j)
        for (int i = 0; i < t.nx; ++i) {
            const std::size_t o = static_cast<std::size_t>(j) * t.nx + i, oe = static_cast<std::size_t>(j + H) * e.nx + i + H;
            out[o] = static_cast<float>(r[oe]);
            if (footprint) footprint[o] = fp[oe];
        }
}

GradedTile grade_tile_smooth(const std::vector<GradingRoad>& roads, const GradingParams& p, const TileSpec& t, const GroundFn& natural) {
    GradedTile g;
    const std::size_t N = static_cast<std::size_t>(t.nx) * static_cast<std::size_t>(t.ny);
    g.height.assign(N, 0.0f);
    g.footprint.assign(N, 0);
    GradeWorkspace ws;
    grade_tile_smooth_into(roads, p, t, natural, g.height.data(), g.footprint.data(), ws);
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
        const int k_max = std::max(kMaxWallTiers, p.max_tiers);
        for (int k = 0; k < k_max; ++k) {
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
                if (pp.present && pp.stairs && pp.kind != 0 && !(pp.has_alt && k == 0)) { cur = -1; continue; }  // rock cut / steep fill: no walls (only the neighbouring station's own wall fading out)
                if (pp.present && pp.stairs) {  // terraced section: the walls come from the same profile the grading evaluates
                    const auto sw_of = [&](const RoadSample& q) {
                        const WallPlan& pq = plan_of(q);
                        return pq.present && pq.stairs ? (pq.has_alt ? pq.sw : 1.0) : 0.0;
                    };
                    const double wwo = w, sgT = pp.cut ? 1.0 : -1.0;
                    const double sw = (i % 2 == 1 && i + 1 < n && w_flag(i - 1) && w_flag(i + 1))
                                          ? (sw_of(S[static_cast<std::size_t>(i - 1)]) + sw_of(S[static_cast<std::size_t>(i + 1)])) / 2
                                          : (pp.has_alt ? pp.sw : 1.0);
                    const auto tier_pt = [&](double ox, double oy, double zfq, WallPoint& o) {
                        const Probe Nq = [&](double e) { return ground(ox + nx * (hw + e), oy + ny * (hw + e)); };
                        // the ground the grading makes at the wall's front / back face: terraces, blended beside a terraced run with the station's own plan (sw) and with the plain slope at run ends (ww)
                        const auto g_at = [&](double e, double gs) {
                            double g = gs;
                            const double nb = Nq(e);
                            if (sw < 1) {
                                double ga = nb;
                                if (pp.has_alt) {
                                    const Eval ea = tier_eval(pp.alt, wwo, e, zfq, nb, 0.5, p.wall_thickness, p.batter);
                                    ga = ea.kind == 1 ? ea.v : ea.kind == 0 ? nb : pp.cut ? std::min(nb, ea.v) : std::max(nb, ea.v);
                                }
                                g = ga + sw * (g - ga);
                            }
                            if (wwo < 1) {
                                const double sv_e = e <= p.verge ? 0.0 : p.round_hinge > 0 ? (e < p.verge + p.round_hinge ? (e - p.verge) * (e - p.verge) / (2 * p.round_hinge) : e - p.verge - p.round_hinge / 2) : e - p.verge;
                                const double gp = pp.cut ? std::min(nb, zfq + sv_e / p.batter) : std::max(nb, zfq - sv_e / p.batter);
                                g = gp + wwo * (g - gp);
                            }
                            return g;
                        };
                        const StairResult pr = stair_profile(pp.cut, zfq, pp.h, Nq, pp.mt, p);
                        const bool has_alt = pp.has_alt && k == 0 && pp.alt.n > 0;
                        StairTier t{0, zfq, zfq, 0};
                        if (k < static_cast<int>(pr.tiers.size())) t = pr.tiers[static_cast<std::size_t>(k)];
                        else if (!has_alt) return false;
                        const double T = p.wall_thickness;
                        const double gB = g_at(t.a + T, t.u), gF = g_at(t.a, t.f);
                        if (std::abs(gB - gF) < p.wall_vis_min) return false;
                        {
                            const double hS = std::abs(t.u - t.f);
                            const double h_struct = has_alt ? (1 - sw) * wwo * std::abs(pp.alt.z[0]) + sw * wwo * hS : sw * wwo * hS;
                            if (h_struct < p.wall_vis_min) return false;
                        }
                        // the ground just behind the back face (ground the wall must still hide: natural where it carries on, else the bench)
                        const auto beyond = [&](double d) {
                            const double eBe = t.a + T + d, nBe = Nq(eBe);
                            return g_at(eBe, t.eps <= 0 ? nBe : bench_z(pp.cut, t, eBe, nBe, p));
                        };
                        const double g_beyond = std::max(beyond(0.5), beyond(0.25) + 0.3);
                        double g_front = gF;  // fill: the bench of the tier above, just in front of the wall
                        if (!pp.cut && k > 0 && k - 1 < static_cast<int>(pr.tiers.size())) {
                            const StairTier& tp0 = pr.tiers[static_cast<std::size_t>(k - 1)];
                            g_front = std::max(g_at(t.a - 0.1, tp0.u), tp0.u);
                        }
                        o.x = ox + nx * (hw + t.a);
                        o.y = oy + ny * (hw + t.a);
                        o.zTop = pp.cut ? gB : gF;
                        o.zBot = pp.cut ? gF : gB;
                        o.top = pp.cut ? std::max(gB + 0.35, g_beyond + 0.02) : (k == 0 ? s.z + 1.0 : std::max(gF + 0.4, g_front + 0.32));
                        o.h = s.h;
                        o.nat = t.eps <= 0;
                        return true;
                    };
                    WallPoint main_pt;
                    std::vector<WallPoint> ext;  // beyond a free road end the wall carries on straight while it still retains something
                    const bool has_main = tier_pt(s.x, s.y, zf, main_pt);
                    if ((i == 0 || i == n - 1) && has_main)
                        for (double a = p.sample_step; a <= pp.along_max + hw + 2; a += p.sample_step) {
                            const double dirx = std::cos(s.h) * (i == 0 ? -1 : 1), diry = std::sin(s.h) * (i == 0 ? -1 : 1);
                            WallPoint e1;
                            if (!tier_pt(s.x + dirx * a, s.y + diry * a, zf + sgT * a / p.end_taper_batter, e1)) break;
                            ext.push_back(e1);
                        }
                    if (!has_main) { cur = -1; continue; }
                    if (cur < 0 || runs[static_cast<std::size_t>(cur)].cut != pp.cut) {
                        WallRun r;
                        r.side = side;
                        r.cut = pp.cut;
                        r.lo = 0;
                        r.hi = p.wall_thickness;
                        r.tier = k;
                        runs.push_back(std::move(r));
                        cur = static_cast<int>(runs.size()) - 1;
                    }
                    WallRun& rr = runs[static_cast<std::size_t>(cur)];
                    if (pp.over) rr.over = true;
                    if (i == 0)
                        for (std::size_t q = ext.size(); q-- > 0;) rr.pts.push_back(ext[q]);
                    rr.pts.push_back(main_pt);
                    if (i != 0)
                        for (const WallPoint& e1 : ext) rr.pts.push_back(e1);
                    continue;
                }
                const bool wing = pp.present && pp.wing;
                struct TierView {
                    int n = 0;
                    std::vector<double> a, z;
                } tv;
                bool has_tp = false;
                if (wing && pp.has_base) {  // the adjacent run's tiers (old tiers, or terraces at this station)
                    has_tp = true;
                    if (pp.base_stairs && pp.base_kind != 0) {
                        // rock cut / steep fill beside the wing: no tiers
                    } else if (pp.base_stairs) {
                        const Probe Nq = [&](double e) { return ground(s.x + nx * (hw + e), s.y + ny * (hw + e)); };
                        const StairResult pr = stair_profile(pp.cut, zf, pp.base_h, Nq, pp.base_mt, p);
                        for (const StairTier& t : pr.tiers) { tv.a.push_back(t.a); tv.z.push_back(t.u - zf); }
                    } else {
                        for (int q = 0; q < pp.base.n; ++q) { tv.a.push_back(pp.base.a[q]); tv.z.push_back(pp.base.z[q]); }
                    }
                    tv.n = static_cast<int>(tv.a.size());
                }
                const TierView* tp = has_tp ? &tv : nullptr;
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
                } else if (k > 0 && wing && pp.has_base && pp.base_stairs && pp.base_kind == 0) {
                    // wing beside terraces: tiers k >= 1 are (1 - fw) of the terraces and fw of the natural ground (what the grading makes outside the full wall)
                    const Probe Nn = [&](double e) { return ground(s.x + nx * (hw + e), s.y + ny * (hw + e)); };
                    const StairResult pr = stair_profile(pp.cut, zf, pp.base_h, Nn, pp.base_mt, p);
                    if (k >= static_cast<int>(pr.tiers.size())) { cur = -1; continue; }
                    const StairTier& t = pr.tiers[static_cast<std::size_t>(k)];
                    const double T = p.wall_thickness;
                    const double gB = (1 - fw) * t.u + fw * Nn(t.a + T), gF = (1 - fw) * t.f + fw * Nn(t.a);
                    if (std::abs(gB - gF) < p.wall_vis_min || (1 - fw) * std::abs(t.u - t.f) < p.wall_vis_min) { cur = -1; continue; }
                    off = t.a;
                    zTop = cut ? gB : gF;
                    zBot = cut ? gF : gB;
                    const StairTier& tq = pr.tiers[static_cast<std::size_t>(k - 1)];
                    const double gFr = (1 - fw) * tq.u + fw * Nn(t.a - 0.1);
                    const double eBe = t.a + T + 0.5, nBe = Nn(eBe);
                    const double gBe = (1 - fw) * (t.eps <= 0 ? nBe : bench_z(cut, t, eBe, nBe, p)) + fw * nBe;
                    top = cut ? std::max(gB + 0.35, gBe + 0.02) : std::max(gF + 0.4, gFr + 0.32);
                } else {
                    if (k > 0 && !(wing && tp && k < tp->n && std::abs(tp->z[k] - tp->z[k - 1]) > 1e-9)) { cur = -1; continue; }
                    const double H = cut ? std::max(out - zf, 0.0) : std::max(zf - out, 0.0), wf = wing ? fw : w;  // legacy full wall; a wing blends (1 - fw) of the adjacent tiers with fw of the full wall
                    const double H0 = wing ? (1 - fw) * (tp && tp->n > 0 ? w * std::abs(tp->z[0]) : 0.0) + fw * H : w * H, L0 = zf + sgn * H0;
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

std::vector<SurfaceRun> surface_runs(const SampledRoad& road) {
    std::vector<SurfaceRun> out;
    const std::vector<RoadSample>& S = road.samples;
    const int n = static_cast<int>(S.size());
    for (const char side : {'L', 'R'}) {
        int i = 0;
        while (i < n) {
            const RoadSample& s = S[static_cast<std::size_t>(i)];
            const WallPlan& pp = side == 'L' ? s.pL : s.pR;
            if (!((side == 'L' ? s.wL : s.wR) && pp.present && pp.stairs && pp.kind != 0)) { ++i; continue; }
            SurfaceRun r;
            r.side = side;
            r.cut = pp.cut;
            r.first = i;
            int j = i;
            while (j + 1 < n) {
                const RoadSample& q = S[static_cast<std::size_t>(j + 1)];
                const WallPlan& pq = side == 'L' ? q.pL : q.pR;
                if (!((side == 'L' ? q.wL : q.wR) && pq.present && pq.stairs && pq.kind == pp.kind && pq.cut == pp.cut)) break;
                ++j;
            }
            r.last = j;
            r.rock_cut = pp.kind == 1;
            for (int k = i; k <= j; ++k) {
                const WallPlan& pk = side == 'L' ? S[static_cast<std::size_t>(k)].pL : S[static_cast<std::size_t>(k)].pR;
                r.needs_structure = r.needs_structure || pk.needs;
            }
            out.push_back(r);
            i = j + 1;
        }
    }
    return out;
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
