// [B] Junction geometry: trims (the contract with A), kerb fillets, carriageway and pavement
// polygons, crossings. See docs/junctions.md.
#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#include "clipper2/clipper.h"
#include "cp/junction.h"

namespace cp {

std::vector<double> compute_trims(const std::vector<JunctionArm>& arms, const std::vector<double>& radii) {
    const std::size_t n = arms.size();
    if (radii.size() != n) return {};
    std::vector<double> trims(n, 0.0);
    for (std::size_t k = 0; k < n; ++k) {
        const std::size_t j = (k + 1) % n;
        double theta = arms[j].heading_out - arms[k].heading_out;
        if (theta <= 0) theta += 2 * kPi;
        if (theta >= kPi - 0.01) continue;  // straight or reflex: no corner
        const double a = arms[k].kerb_left, b = arms[j].kerb_right;
        const double sn = std::sin(theta), cot = std::cos(theta) / sn;
        const double fillet = radii[k] / std::tan(theta / 2);
        trims[k] = std::max(trims[k], a * cot + b / sn + fillet);
        trims[j] = std::max(trims[j], a / sn + b * cot + fillet);
    }
    for (std::size_t i = 0; i < n; ++i)
        trims[i] = std::clamp(trims[i], 0.0, std::min(arms[i].edge_length / 2, kArmSampleLength));
    return trims;
}

namespace {

constexpr double kFilletStep = 0.1;       // rad, max angle per arc segment
constexpr double kNoCornerAngle = kPi - 0.01;
constexpr double kCrossingGap = 2.0;      // m from the trim line to the crossing centre
constexpr double kSolveTol = 1e-5;        // m, fillet centre mismatch accepted by the solver
constexpr double kMinArea = 1e-3;         // m^2, smaller polygons (and holes) are dropped

Vec2 left_normal(double h) { return {-std::sin(h), std::cos(h)}; }

// Points of the arm at lateral offset `lat` from s0 to s1 (either direction), following the
// stored centreline so curved arms are traced, not chorded.
void run(const JunctionArm& arm, double s0, double s1, double lat, std::vector<Vec3>& out) {
    out.push_back(arm_point(arm, s0, lat).pos);
    const bool up = s1 >= s0;
    if (up) {
        for (const EdgeSample& c : arm.centreline)
            if (c.s > s0 + 1e-6 && c.s < s1 - 1e-6) out.push_back(arm_point(arm, c.s, lat).pos);
    } else {
        for (auto it = arm.centreline.rbegin(); it != arm.centreline.rend(); ++it)
            if (it->s < s0 - 1e-6 && it->s > s1 + 1e-6) out.push_back(arm_point(arm, it->s, lat).pos);
    }
    out.push_back(arm_point(arm, s1, lat).pos);
}

struct Corner {
    bool fillet = false;
    double angle = 0, radius = 0;
    double sp = 0, sq = 0;     // s of the tangent points on arm k (left kerb) and arm j (right kerb)
    Vec3 p, q;                 // those points
    Vec2 centre;
    double a0 = 0, sweep = 0;  // arc start angle and signed sweep about `centre`
    std::vector<Vec3> kerb;    // fillet polyline P..Q (empty when sharp or no corner)
};

Vec3 arc_point(const Corner& c, double r, int i, int n) {
    const double t = static_cast<double>(i) / n;
    const double a = c.a0 + c.sweep * t;
    return {c.centre.x + r * std::cos(a), c.centre.y + r * std::sin(a), c.p.z + (c.q.z - c.p.z) * t};
}

enum class Newton { Diverged, Converged, Stalled };

// One damped-Newton run from (sp, sq) on F(sp, sq) = c1(sp) - c2(sq), where c1 / c2 are the circle
// centres offset r from the left kerb of ak (lateral a) and the right kerb of aj (lateral -b).
// arm_point is piecewise linear, so the Jacobian is a central difference over a step that
// straddles at most one kink, and every step is backtracked until the residual shrinks.
// Stalled: stopped within 1 mm of a root without reaching kSolveTol.
Newton newton_fillet(const JunctionArm& ak, const JunctionArm& aj, double a, double b, double r, double& sp,
                     double& sq) {
    const auto f = [&](double s, double t) {
        const ArmPoint p = arm_point(ak, s, a), q = arm_point(aj, t, -b);
        const Vec2 c1 = p.pos.xy() + left_normal(p.heading_out) * r;
        const Vec2 c2 = q.pos.xy() - left_normal(q.heading_out) * r;
        return c1 - c2;
    };
    constexpr double h = 0.02;
    Vec2 f0 = f(sp, sq);
    double n0 = f0.length();
    const auto verdict = [&] { return n0 < 1e-3 ? Newton::Stalled : Newton::Diverged; };
    for (int it = 0; it < 60; ++it) {
        if (n0 < kSolveTol) return Newton::Converged;
        const double s_lo = std::max(0.0, sp - h), t_lo = std::max(0.0, sq - h);
        const Vec2 ds = (f(sp + h, sq) - f(s_lo, sq)) * (1.0 / (sp + h - s_lo));
        const Vec2 dt = (f(sp, sq + h) - f(sp, t_lo)) * (1.0 / (sq + h - t_lo));
        const double det = ds.cross(dt);
        if (std::fabs(det) < 1e-12) return verdict();
        // Solve [ds dt] [x y]^T = -f0.
        const Vec2 rhs{-f0.x, -f0.y};
        const double x = std::clamp(rhs.cross(dt) / det, -20.0, 20.0);
        const double y = std::clamp(ds.cross(rhs) / det, -20.0, 20.0);
        double lam = 1.0;
        bool moved = false;
        for (int bt = 0; bt < 14; ++bt, lam *= 0.5) {
            const double ns = std::max(0.0, sp + lam * x), nt = std::max(0.0, sq + lam * y);
            const Vec2 fn = f(ns, nt);
            const double nn = fn.length();
            if (nn < n0 * (1.0 - 1e-4 * lam) || nn < kSolveTol) {
                sp = ns, sq = nt, f0 = fn, n0 = nn;
                moved = true;
                break;
            }
        }
        if (!moved) return verdict();
    }
    return n0 < kSolveTol ? Newton::Converged : verdict();
}

enum class Solve { Found, Outside, Unsolved };

// Fillet of radius r between the left kerb of `ak` (offset a) and the right kerb of `aj` (offset b),
// found on the real (possibly curved) kerb lines: the arm parameters sp, sq where the circle
// centres coincide. r = 0 gives the kerb intersection. (sp, sq) is the straight-kerb estimate on
// entry. Found: a root with sp <= smax_k and sq <= smax_j (the one nearest the node when several).
// Outside: roots exist only beyond the trims, or the kerbs never meet there (acute fork, arms
// curving apart). Unsolved: the iteration stalled next to a root it could not reach.
Solve solve_fillet(const JunctionArm& ak, const JunctionArm& aj, double a, double b, double r, double smax_k,
                   double smax_j, double& sp, double& sq) {
    constexpr int kGrid = 6;
    const double est_s = sp, est_t = sq;
    bool stalled = false, outside = false;
    double best = 1e18, bs = 0, bt = 0;
    const auto run_from = [&](double s0, double t0) {
        double s = s0, t = t0;
        const Newton v = newton_fillet(ak, aj, a, b, r, s, t);
        if (v == Newton::Stalled) stalled = true;
        if (v != Newton::Converged) return;
        if (s <= smax_k + 1e-6 && t <= smax_j + 1e-6) {
            if (s + t < best) best = s + t, bs = s, bt = t;
        } else {
            outside = true;
        }
    };
    run_from(est_s, est_t);
    if (best < 1e17) {  // the straight-kerb estimate converges to a root inside the trims
        sp = bs, sq = bt;
        return Solve::Found;
    }
    for (int i = 1; i <= kGrid; ++i)
        for (int k = 1; k <= kGrid; ++k) run_from(smax_k * i / (kGrid + 1), smax_j * k / (kGrid + 1));
    if (best < 1e17) {
        sp = bs, sq = bt;
        return Solve::Found;
    }
    return outside || !stalled ? Solve::Outside : Solve::Unsolved;
}

int arc_segments(double sweep) { return std::max(2, static_cast<int>(std::ceil(std::fabs(sweep) / kFilletStep))); }

double signed_area(const std::vector<Vec2>& r) {
    double a = 0;
    for (std::size_t i = 0; i < r.size(); ++i) a += r[i].cross(r[(i + 1) % r.size()]);
    return a / 2;
}

int sgn(double v) { return v > 1e-9 ? 1 : (v < -1e-9 ? -1 : 0); }

bool within_box(Vec2 a, Vec2 b, Vec2 p) {
    return p.x >= std::min(a.x, b.x) - 1e-9 && p.x <= std::max(a.x, b.x) + 1e-9 &&
           p.y >= std::min(a.y, b.y) - 1e-9 && p.y <= std::max(a.y, b.y) + 1e-9;
}

// Closed segments ab and cd share a point (proper crossing, touching, or collinear overlap).
bool segments_meet(Vec2 a, Vec2 b, Vec2 c, Vec2 d) {
    const int o1 = sgn((b - a).cross(c - a)), o2 = sgn((b - a).cross(d - a));
    const int o3 = sgn((d - c).cross(a - c)), o4 = sgn((d - c).cross(b - c));
    if (o1 * o2 < 0 && o3 * o4 < 0) return true;
    return (o1 == 0 && within_box(a, b, c)) || (o2 == 0 && within_box(a, b, d)) ||
           (o3 == 0 && within_box(c, d, a)) || (o4 == 0 && within_box(c, d, b));
}

// True when two non-adjacent edges share any point (crossing, pinch or overlap): such a ring is
// not a simple polygon and goes through the Clipper union instead of being used as drawn.
bool ring_self_intersects(const std::vector<Vec2>& r) {
    const std::size_t n = r.size();
    for (std::size_t i = 0; i < n; ++i)
        for (std::size_t k = i + 2; k < n; ++k) {
            if (i == 0 && k == n - 1) continue;
            if (segments_meet(r[i], r[(i + 1) % n], r[k], r[(k + 1) % n])) return true;
        }
    return false;
}

bool ring_contains(const std::vector<Vec2>& r, Vec2 p) {
    bool in = false;
    for (std::size_t i = 0, j = r.size() - 1; i < r.size(); j = i++)
        if ((r[i].y > p.y) != (r[j].y > p.y) && p.x < (r[j].x - r[i].x) * (p.y - r[i].y) / (r[j].y - r[i].y) + r[i].x)
            in = !in;
    return in;
}

// Clipper output to the PolygonSet convention: each outer ring (CCW) is followed by its holes
// (CW). Rings and holes under kMinArea are dropped; a hole outside every outer ring is dropped.
void append_polygons(PolygonSet& out, const Clipper2Lib::PathsD& paths) {
    std::vector<std::vector<Vec2>> outers, holes;
    for (const Clipper2Lib::PathD& m : paths) {
        std::vector<Vec2> ring;
        for (const auto& pt : m) ring.push_back({pt.x, pt.y});
        if (ring.size() < 3) continue;
        const double a = signed_area(ring);
        if (std::fabs(a) <= kMinArea) continue;
        (a > 0 ? outers : holes).push_back(std::move(ring));
    }
    std::vector<std::vector<std::vector<Vec2>>> mine(outers.size());
    for (auto& h : holes) {
        Vec2 c{0, 0};
        for (const Vec2& p : h) c = c + p * (1.0 / static_cast<double>(h.size()));
        const Vec2 probe = h[0] + (c - h[0]) * 0.05;
        int best = -1;
        for (std::size_t o = 0; o < outers.size(); ++o)
            if (ring_contains(outers[o], probe) && (best < 0 || signed_area(outers[o]) < signed_area(outers[static_cast<std::size_t>(best)])))
                best = static_cast<int>(o);
        if (best >= 0) mine[static_cast<std::size_t>(best)].push_back(std::move(h));
    }
    for (std::size_t o = 0; o < outers.size(); ++o) {
        out.push_back(std::move(outers[o]));
        for (auto& h : mine[o]) out.push_back(std::move(h));
    }
}

void flag_degraded(Junction& j, const std::string& why) {
    if (!j.degraded) j.degraded_reason = why;
    j.degraded = true;
}

}  // namespace

void generate_geometry(Junction& junction, const RoadGraph&, const RegionPack&, const JunctionOverrides& overrides) {
    junction.corners.clear();
    junction.carriageway.clear();
    junction.pavements.clear();
    junction.crossings.clear();
    const std::vector<JunctionArm>& arms = junction.arms;
    const std::size_t n = arms.size();
    if (n < 2) return;
    if (junction.corner_radii.size() != n) junction.corner_radii = resolve_corner_radii(junction.node, arms, overrides);
    if (junction.trims.size() != n) junction.trims = compute_trims(arms, junction.corner_radii);
    const std::vector<double>& trims = junction.trims;

    // ---- corners
    std::vector<Corner> corners(n);
    for (std::size_t k = 0; k < n; ++k) {
        const std::size_t j = (k + 1) % n;
        const JunctionArm &ak = arms[k], &aj = arms[j];
        Corner& c = corners[k];
        double theta = aj.heading_out - ak.heading_out;
        if (theta <= 0) theta += 2 * kPi;
        c.angle = theta;
        const double a = ak.kerb_left, b = aj.kerb_right;
        c.fillet = theta < kNoCornerAngle;
        const double requested = std::max(0.0, junction.corner_radii[k]);
        if (c.fillet) {
            const double sn = std::sin(theta), cot = std::cos(theta) / sn, th = std::tan(theta / 2);
            const double base_k = a * cot + b / sn, base_j = a / sn + b * cot;
            enum class Fit { Unsolved, Outside, Ok };
            // Solve for radius r on the real kerb lines; Outside = a tangent point lies beyond a trim.
            const auto attempt = [&](double r, double& sp, double& sq) {
                sp = base_k + r / th;
                sq = base_j + r / th;
                switch (solve_fillet(ak, aj, a, b, r, trims[k], trims[j], sp, sq)) {
                    case Solve::Found: return Fit::Ok;
                    case Solve::Outside: return Fit::Outside;
                    case Solve::Unsolved: break;
                }
                return Fit::Unsolved;
            };
            // The requested radius is used whenever it fits. When it does not (trims clamp to half
            // the edge / 40 m) the largest radius that does is found by bisection: an ordinary
            // clamp, visible as radius < requested_radius, not a degraded junction.
            double sp = 0, sq = 0;
            bool unsolved = false;
            Fit fit = attempt(requested, sp, sq);
            c.radius = requested;
            if (fit != Fit::Ok) {
                unsolved = fit == Fit::Unsolved;
                double lo_sp = 0, lo_sq = 0;
                const Fit zero = attempt(0.0, lo_sp, lo_sq);
                if (zero != Fit::Ok) {
                    // Even the sharp kerb intersection is not between the trim lines (acute angle or
                    // strongly curving arms): chord between the trim-line kerb points instead.
                    c.radius = 0;
                    sp = std::min(zero == Fit::Unsolved ? base_k : lo_sp, trims[k]);
                    sq = std::min(zero == Fit::Unsolved ? base_j : lo_sq, trims[j]);
                    flag_degraded(junction, zero == Fit::Unsolved
                                                ? "corner " + std::to_string(k) + ": fillet solver did not converge"
                                                : "corner " + std::to_string(k) + ": kerb intersection lies beyond the trim line");
                } else {
                    double lo = 0, hi = requested;
                    sp = lo_sp;
                    sq = lo_sq;
                    for (int i = 0; i < 30 && hi - lo > 1e-6; ++i) {
                        const double mid = 0.5 * (lo + hi);
                        double ms = 0, mq = 0;
                        const Fit f = attempt(mid, ms, mq);
                        if (f == Fit::Ok) lo = mid, sp = ms, sq = mq;
                        else {
                            hi = mid;
                            unsolved = unsolved || f == Fit::Unsolved;
                        }
                    }
                    c.radius = lo;
                    if (unsolved)
                        flag_degraded(junction, "corner " + std::to_string(k) +
                                                    ": fillet solver did not converge, radius may be smaller than needed");
                }
            }
            c.sp = sp;
            c.sq = sq;
        } else {
            c.radius = 0;
        }
        c.p = arm_point(ak, c.sp, a).pos;
        c.q = arm_point(aj, c.sq, -b).pos;
        if (c.fillet) {
            const double hp = arm_point(ak, c.sp, a).heading_out, hq = arm_point(aj, c.sq, -b).heading_out;
            const Vec2 c1 = c.p.xy() + left_normal(hp) * c.radius;
            const Vec2 c2 = c.q.xy() - left_normal(hq) * c.radius;
            c.centre = (c1 + c2) * 0.5;
            c.a0 = hp - kPi / 2;
            c.sweep = wrap_angle(hq + kPi / 2 - c.a0);
            if (c.radius > 1e-9) {
                const int segs = arc_segments(c.sweep);
                for (int i = 0; i <= segs; ++i) c.kerb.push_back(arc_point(c, c.radius, i, segs));
                c.kerb.front() = c.p;
                c.kerb.back() = c.q;
            }
        }
        CornerCurve cc;
        cc.arm_right = static_cast<int>(k);
        cc.arm_left = static_cast<int>(j);
        cc.edge_r = ak.edge;
        cc.edge_l = aj.edge;
        cc.angle = theta;
        cc.radius = c.radius;
        cc.requested_radius = requested;
        cc.overridden = overrides.corner_radius.count(CornerKey{junction.node, aj.edge, ak.edge}) > 0;
        cc.kerb = c.kerb;
        junction.corners.push_back(std::move(cc));
    }

    // ---- carriageway: the outline kerb to kerb, then a Clipper union to normalise it
    std::vector<Vec3> outline;
    for (std::size_t i = 0; i < n; ++i) {
        const JunctionArm& arm = arms[i];
        const Corner& prev = corners[(i + n - 1) % n];
        const Corner& next = corners[i];
        const double t = trims[i];
        run(arm, std::min(prev.sq, t), t, -arm.kerb_right, outline);
        run(arm, t, std::min(next.sp, t), arm.kerb_left, outline);
        if (next.fillet && !next.kerb.empty()) outline.insert(outline.end(), next.kerb.begin(), next.kerb.end());
        else {
            outline.push_back(next.p);
            outline.push_back(next.q);
        }
    }
    Clipper2Lib::PathD path;
    for (const Vec3& p : outline) path.emplace_back(p.x, p.y);
    const Clipper2Lib::PathsD merged = Clipper2Lib::Union(Clipper2Lib::PathsD{path}, Clipper2Lib::FillRule::NonZero, 3);
    append_polygons(junction.carriageway, merged);
    if (junction.carriageway.empty()) {  // failure: leave the junction without geometry
        junction.corners.clear();
        flag_degraded(junction, "carriageway outline produced no polygon");
        return;
    }

    // ---- pavements: a band outside the kerb of each corner (also across straight kerbs)
    for (std::size_t k = 0; k < n; ++k) {
        const std::size_t j = (k + 1) % n;
        const JunctionArm &ak = arms[k], &aj = arms[j];
        const double wk = ak.half_width_left - ak.kerb_left, wj = aj.half_width_right - aj.kerb_right;
        const double w = std::max(wk, wj);
        if (w < 1e-3) continue;
        const Corner& c = corners[k];
        std::vector<Vec3> band;
        const double s_end_k = std::min(c.sp, trims[k]), s_end_j = std::min(c.sq, trims[j]);
        run(ak, trims[k], s_end_k, ak.kerb_left, band);
        if (c.fillet && !c.kerb.empty()) band.insert(band.end(), c.kerb.begin(), c.kerb.end());
        else {
            band.push_back(c.p);
            band.push_back(c.q);
        }
        run(aj, s_end_j, trims[j], -aj.kerb_right, band);
        // Outer edge, back from arm j to arm k. The pavement lies on the fillet centre's side of
        // the kerb arc, so its outer arc has radius r - w; when that vanishes the offset lines meet
        // in a mitre.
        double sk_out = s_end_k, sj_out = s_end_j;
        std::vector<Vec3> mid;
        if (c.fillet && c.radius > w + 1e-6) {
            const int segs = arc_segments(c.sweep);
            for (int i = segs; i >= 0; --i) mid.push_back(arc_point(c, c.radius - w, i, segs));
        } else if (c.fillet) {
            const ArmPoint o1 = arm_point(ak, c.sp, ak.kerb_left + w), o2 = arm_point(aj, c.sq, -(aj.kerb_right + w));
            const Vec2 hk{std::cos(o1.heading_out), std::sin(o1.heading_out)};
            const Vec2 hj{std::cos(o2.heading_out), std::sin(o2.heading_out)};
            const double den = hk.cross(hj);
            if (std::fabs(den) > 1e-9) {
                const Vec2 d = o2.pos.xy() - o1.pos.xy();
                const double t = d.cross(hj) / den, u = d.cross(hk) / den;
                sk_out = std::min(c.sp + t, trims[k]);
                sj_out = std::min(c.sq + u, trims[j]);
                mid.push_back({o1.pos.x + hk.x * t, o1.pos.y + hk.y * t, o1.pos.z});
            }
        } else {
            mid.push_back(arm_point(aj, 0, -(aj.kerb_right + w)).pos);
            mid.push_back(arm_point(ak, 0, ak.kerb_left + w).pos);
        }
        run(aj, trims[j], sj_out, -(aj.kerb_right + w), band);
        band.insert(band.end(), mid.begin(), mid.end());
        run(ak, sk_out, trims[k], ak.kerb_left + w, band);
        // Straight bands are kept as drawn; one that crosses itself (curved arms, tight fillets)
        // goes through a Clipper union to normalise it.
        std::vector<Vec2> raw;
        for (const Vec3& p : band)
            if (raw.empty() || (raw.back() - p.xy()).length() > 1e-9) raw.push_back(p.xy());
        while (raw.size() > 1 && (raw.front() - raw.back()).length() <= 1e-9) raw.pop_back();
        if (raw.size() < 3) continue;
        if (!ring_self_intersects(raw)) {
            const double ar = signed_area(raw);
            if (std::fabs(ar) <= kMinArea) continue;
            if (ar < 0) std::reverse(raw.begin(), raw.end());
            junction.pavements.push_back(std::move(raw));
            continue;
        }
        Clipper2Lib::PathD bp;
        for (const Vec2& p : raw) bp.emplace_back(p.x, p.y);
        append_polygons(junction.pavements, Clipper2Lib::Union(Clipper2Lib::PathsD{bp}, Clipper2Lib::FillRule::NonZero, 3));
    }

    // ---- crossings: one per arm, just outside the trim line
    for (std::size_t i = 0; i < n; ++i) {
        const JunctionArm& arm = arms[i];
        const double s = trims[i] + kCrossingGap;
        const ArmPoint ap = arm_point(arm, s, 0);
        CrossingSpot cs;
        cs.arm = static_cast<int>(i);
        cs.s = s;
        cs.centre = ap.pos;
        cs.heading = wrap_angle(ap.heading_out + kPi / 2);
        cs.length = arm.kerb_left + arm.kerb_right;
        junction.crossings.push_back(cs);
    }
}

}  // namespace cp
