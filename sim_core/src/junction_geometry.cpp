// [B] Junction geometry: trims (the contract with A), kerb fillets, carriageway and pavement
// polygons, crossings. See docs/junctions.md.
#include <algorithm>
#include <cmath>

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

int arc_segments(double sweep) { return std::max(2, static_cast<int>(std::ceil(std::fabs(sweep) / kFilletStep))); }

double signed_area(const std::vector<Vec2>& r) {
    double a = 0;
    for (std::size_t i = 0; i < r.size(); ++i) a += r[i].cross(r[(i + 1) % r.size()]);
    return a / 2;
}

std::vector<Vec2> to_ring(const std::vector<Vec3>& pts) {
    std::vector<Vec2> r;
    r.reserve(pts.size());
    for (const Vec3& p : pts) r.push_back(p.xy());
    if (signed_area(r) < 0) std::reverse(r.begin(), r.end());
    return r;
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
        if (c.fillet) {
            const double sn = std::sin(theta), cot = std::cos(theta) / sn, th = std::tan(theta / 2);
            const double base_k = a * cot + b / sn, base_j = a / sn + b * cot;
            const double fit = std::min(trims[k] - base_k, trims[j] - base_j) * th;
            c.radius = std::clamp(junction.corner_radii[k], 0.0, std::max(0.0, fit));
            c.sp = base_k + c.radius / th;
            c.sq = base_j + c.radius / th;
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
    if (merged.empty()) {  // failure: leave the junction without geometry
        junction.corners.clear();
        return;
    }
    for (const Clipper2Lib::PathD& m : merged) {
        std::vector<Vec2> ring;
        for (const auto& pt : m) ring.push_back({pt.x, pt.y});
        if (ring.size() >= 3) junction.carriageway.push_back(std::move(ring));
    }
    if (junction.carriageway.empty()) {
        junction.corners.clear();
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
        std::vector<Vec2> ring = to_ring(band);
        if (ring.size() >= 3 && std::fabs(signed_area(ring)) > 1e-6) junction.pavements.push_back(std::move(ring));
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
