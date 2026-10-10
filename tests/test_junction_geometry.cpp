// Junction geometry (part B): corner fillets, carriageway/pavement polygons, crossings.
#include <algorithm>
#include <cmath>
#include <memory>

#include <cstdio>
#include <string>
#include <vector>

#include "doctest/doctest.h"
#include "portable_rng.h"
#include "cp/junction.h"
#include "cp/road_commands.h"

using namespace cp;

namespace {

constexpr double kDeg = kPi / 180;

std::shared_ptr<const RegionPack> uk() {
    static const std::shared_ptr<const RegionPack> pack = [] {
        auto p = load_region_pack_by_id(CP_DATA_DIR, "uk");
        REQUIRE_MESSAGE(p, p.error);
        return std::make_shared<const RegionPack>(*p);
    }();
    return pack;
}

struct Net {
    RoadGraph g;
    CommandStack<RoadGraph> st{g};
    NodeId centre;
    Net() { g.set_region(uk()); }
    void polar(double deg, double len, const char* type = "residential") {
        DrawRoadSpec s;
        s.road_type = type;
        RoadEnd c, far;
        far.pos = {len * std::cos(deg * kDeg), len * std::sin(deg * kDeg), 0};
        c.pos = {0, 0, 0};
        c.existing_node = centre;
        s.start = c;
        s.end = far;
        REQUIRE(st.execute(make_draw_road(s)));
        if (!centre.valid()) centre = g.edges().rbegin()->second.a;
    }
    Junction build(const JunctionOverrides& ov = {}, const RegionPack* region = nullptr) const {
        const JunctionSet set = build_junctions(g, region ? *region : *uk(), ov);
        REQUIRE(set.junctions.size() == 1);
        return set.junctions.begin()->second;
    }
};

double area(const std::vector<Vec2>& r) {
    double a = 0;
    for (std::size_t i = 0; i < r.size(); ++i) a += r[i].cross(r[(i + 1) % r.size()]);
    return a / 2;
}

bool inside(const std::vector<Vec2>& r, Vec2 p) {
    bool in = false;
    for (std::size_t i = 0, j = r.size() - 1; i < r.size(); j = i++)
        if ((r[i].y > p.y) != (r[j].y > p.y) &&
            p.x < (r[j].x - r[i].x) * (p.y - r[i].y) / (r[j].y - r[i].y) + r[i].x)
            in = !in;
    return in;
}

bool segs_cross(Vec2 a, Vec2 b, Vec2 c, Vec2 d) {
    // Proper crossing deeper than 0.1 mm on both segments (touching within rounding is not one).
    const double l1 = (b - a).length(), l2 = (d - c).length();
    if (l1 < 1e-9 || l2 < 1e-9) return false;
    const double d1 = (b - a).cross(c - a) / l1, d2 = (b - a).cross(d - a) / l1;
    const double d3 = (d - c).cross(a - c) / l2, d4 = (d - c).cross(b - c) / l2;
    return d1 * d2 < 0 && std::fabs(d1) > 1e-4 && std::fabs(d2) > 1e-4 && d3 * d4 < 0 && std::fabs(d3) > 1e-4 && std::fabs(d4) > 1e-4;
}

bool self_intersects(const std::vector<Vec2>& r) {
    const std::size_t n = r.size();
    for (std::size_t i = 0; i < n; ++i)
        for (std::size_t k = i + 2; k < n; ++k) {
            if (i == 0 && k == n - 1) continue;
            if (segs_cross(r[i], r[(i + 1) % n], r[k], r[(k + 1) % n])) return true;
        }
    return false;
}

// Validity common to every junction: one closed CCW simple polygon containing every arm's
// trim-line centre point (sampled just inside the trim line).
void check_valid(const Junction& j) {
    REQUIRE(j.carriageway.size() == 1);
    const auto& ring = j.carriageway[0];
    CHECK(ring.size() >= 3);
    CHECK(area(ring) > 0);
    CHECK_FALSE(self_intersects(ring));
    CHECK(inside(ring, j.pos.xy()));
    REQUIRE(j.trims.size() == j.arms.size());
    for (std::size_t i = 0; i < j.arms.size(); ++i) {
        CHECK(j.trims[i] > 0);
        CHECK(inside(ring, arm_point(j.arms[i], j.trims[i] - 0.05, 0).pos.xy()));
    }
    for (const auto& p : j.pavements) {
        CHECK(area(p) > 0);
        CHECK_FALSE(self_intersects(p));
    }
    CHECK(j.crossings.size() == j.arms.size());
    CHECK(j.corners.size() == j.arms.size());
}

double along(const JunctionArm& a, Vec3 p) { return std::cos(a.heading_out) * p.x + std::sin(a.heading_out) * p.y; }

}  // namespace

TEST_CASE("geometry: X junction r=6 has 4 fillets starting 11 m out") {
    Net n;
    for (double d : {0.0, 90.0, 180.0, 270.0}) n.polar(d, 100);
    const Junction j = n.build();
    check_valid(j);
    REQUIRE(j.corners.size() == 4);
    for (std::size_t k = 0; k < 4; ++k) {
        const CornerCurve& c = j.corners[k];
        CHECK(c.radius == doctest::Approx(6.0));
        CHECK_FALSE(c.overridden);
        CHECK(c.angle == doctest::Approx(kPi / 2));
        REQUIRE(c.kerb.size() >= 3);
        CHECK(along(j.arms[k], c.kerb.front()) == doctest::Approx(11.0).epsilon(1e-6));
        CHECK(along(j.arms[(k + 1) % 4], c.kerb.back()) == doctest::Approx(11.0).epsilon(1e-6));
    }
    // Two 22 x 10 strips (340 m2) plus four corners of 6x6 minus a quarter disc.
    const double expect = 340.0 + 4 * (36.0 - kPi * 9.0);
    CHECK(area(j.carriageway[0]) == doctest::Approx(expect).epsilon(0.005));
}

TEST_CASE("geometry: r=0 gives sharp corners") {
    Net n;
    for (double d : {0.0, 90.0, 180.0, 270.0}) n.polar(d, 100);
    JunctionOverrides ov;
    const auto arms = collect_arms(n.g, n.centre, *uk());
    for (std::size_t k = 0; k < 4; ++k) ov.corner_radius[{n.centre, arms[(k + 1) % 4].edge, arms[k].edge}] = 0;
    const Junction j = n.build(ov);
    check_valid(j);
    for (const auto& c : j.corners) {
        CHECK(c.radius == 0);
        CHECK(c.overridden);
        CHECK(c.kerb.size() <= 2);
    }
    CHECK(j.trims[0] == doctest::Approx(5.0));
    CHECK(area(j.carriageway[0]) == doctest::Approx(100.0).epsilon(0.001));
}

TEST_CASE("geometry: T junction has no corner on the straight side") {
    Net n;
    n.polar(0, 100);
    n.polar(180, 100);
    n.polar(-90, 80);
    const Junction j = n.build();
    check_valid(j);
    int with_kerb = 0;
    for (const auto& c : j.corners) with_kerb += !c.kerb.empty();
    CHECK(with_kerb == 2);
    // Corner E -> W (arms[1] -> arms[2], 180 degrees) is the straight kerb.
    CHECK(j.corners[1].kerb.empty());
    CHECK(j.corners[1].angle == doctest::Approx(kPi));
    const double expect = 22.0 * 10.0 + 6.0 * 10.0 + 2 * (36.0 - kPi * 9.0);
    CHECK(area(j.carriageway[0]) == doctest::Approx(expect).epsilon(0.005));
}

TEST_CASE("geometry: Y junction at 120 degrees") {
    Net n;
    for (double d : {90.0, 210.0, 330.0}) n.polar(d, 100);
    const Junction j = n.build();
    check_valid(j);
    const double trim = (5 * std::cos(120 * kDeg) + 5) / std::sin(120 * kDeg) + 6 / std::tan(60 * kDeg);
    for (std::size_t i = 0; i < 3; ++i) {
        CHECK(j.trims[i] == doctest::Approx(trim));
        CHECK(j.corners[i].kerb.size() >= 3);
        CHECK(j.corners[i].angle == doctest::Approx(120 * kDeg));
    }
}

TEST_CASE("geometry: skewed X (60 and 120 degree corners)") {
    Net n;
    for (double d : {0.0, 60.0, 180.0, 240.0}) n.polar(d, 100);
    const Junction j = n.build();
    check_valid(j);
    int acute = 0;
    for (const auto& c : j.corners) {
        CHECK_FALSE(c.kerb.empty());
        acute += std::fabs(c.angle - 60 * kDeg) < 1e-6;
    }
    CHECK(acute == 2);
    // Acute corners need longer trims than obtuse ones: arms 0 and 1 border a 60 degree corner.
    CHECK(j.trims[0] > 11.0);
}

TEST_CASE("geometry: mixed widths (residential into a B road)") {
    Net n;
    n.polar(0, 100, "b_road");
    n.polar(180, 100, "b_road");
    n.polar(-90, 80, "residential");
    const Junction j = n.build();
    check_valid(j);
    // Region radius is the larger of the two road types' (B road 10 m).
    CHECK(j.corners[0].radius == doctest::Approx(10.0));
    CHECK(j.corners[1].kerb.empty());
    // Both sides of a B road are 3.4 m kerb offsets, the residential's are 5.0: the polygon
    // still closes around all three trim lines.
    CHECK(inside(j.carriageway[0], arm_point(j.arms[0], j.trims[0] - 0.05, 4.9).pos.xy()));
    CHECK_FALSE(inside(j.carriageway[0], arm_point(j.arms[0], j.trims[0] - 0.05, 5.6).pos.xy()));
}

TEST_CASE("geometry: corner radius override changes only that corner") {
    Net n;
    for (double d : {0.0, 90.0, 180.0, 270.0}) n.polar(d, 100);
    const auto arms = collect_arms(n.g, n.centre, *uk());
    JunctionOverrides ov;
    ov.corner_radius[{n.centre, arms[1].edge, arms[0].edge}] = 10.0;
    const Junction base = n.build();
    const Junction j = n.build(ov);
    check_valid(j);
    CHECK(j.corners[0].radius == doctest::Approx(10.0));
    CHECK(j.corners[0].overridden);
    CHECK(along(j.arms[0], j.corners[0].kerb.front()) == doctest::Approx(15.0).epsilon(1e-6));
    for (std::size_t k = 1; k < 4; ++k) {
        CHECK(j.corners[k].radius == doctest::Approx(6.0));
        CHECK_FALSE(j.corners[k].overridden);
    }
    CHECK(j.trims[0] == doctest::Approx(15.0));
    CHECK(j.trims[1] == doctest::Approx(15.0));
    CHECK(j.trims[2] == doctest::Approx(11.0));
    CHECK(area(j.carriageway[0]) != doctest::Approx(area(base.carriageway[0])));
}

TEST_CASE("geometry: pavement band width and crossings") {
    Net n;
    for (double d : {0.0, 90.0, 180.0, 270.0}) n.polar(d, 100);
    const Junction j = n.build();
    REQUIRE(j.pavements.size() == 4);
    const double w = 7.15 - 5.0;  // kerb face to the outer edge of the cross-section
    for (const auto& p : j.pavements) {
        // Annular sector of 90 degrees between radius 6 - w and 6 (the pavement is on the centre's side).
        const double sector = 0.5 * (kPi / 2) * (36 - (6 - w) * (6 - w));
        CHECK(area(p) == doctest::Approx(sector).epsilon(0.01));
    }
    // Straight kerb of a T: a 22 m long band exactly w wide.
    Net t;
    t.polar(0, 100);
    t.polar(180, 100);
    t.polar(-90, 80);
    const Junction jt = t.build();
    REQUIRE(jt.pavements.size() == 3);
    CHECK(area(jt.pavements[1]) == doctest::Approx(22.0 * w).epsilon(1e-6));
    // Crossings: one per arm, beyond the trim line, across the carriageway.
    for (std::size_t i = 0; i < 4; ++i) {
        const CrossingSpot& c = j.crossings[i];
        CHECK(c.arm == static_cast<int>(i));
        CHECK(c.s > j.trims[i]);
        CHECK(c.s - j.trims[i] < 4.0);
        CHECK(c.length == doctest::Approx(10.0));
        CHECK(std::fabs(wrap_angle(c.heading - j.arms[i].heading_out)) == doctest::Approx(kPi / 2));
        CHECK(along(j.arms[i], c.centre) == doctest::Approx(c.s));
    }
}

TEST_CASE("geometry: identical for either driving side") {
    Net n;
    n.polar(0, 100);
    n.polar(180, 100);
    n.polar(-90, 80);
    RegionPack right = *uk();
    right.driving_side = DrivingSide::Right;
    const Junction l = n.build();
    const Junction r = n.build({}, &right);
    check_valid(r);
    CHECK(area(r.carriageway[0]) == doctest::Approx(area(l.carriageway[0])));
    CHECK(r.pavements.size() == l.pavements.size());
}

// ---------------------------------------------------------------- curved-arm fuzz

namespace {

// Distance from p to the kerb curve (arm, lateral) over s in [0, smax]; also returns the s found.
double kerb_distance(const JunctionArm& arm, double lateral, Vec3 p, double smax, double& s_out) {
    double best = 1e18, bs = 0;
    const auto probe = [&](double s) {
        const double d = (arm_point(arm, s, lateral).pos.xy() - p.xy()).length();
        if (d < best) best = d, bs = s;
    };
    for (double s = 0; s <= smax + 1e-9; s += 0.02) probe(s);
    const double c = bs;
    for (double s = std::max(0.0, c - 0.02); s <= std::min(smax, c + 0.02); s += 0.0005) probe(s);
    s_out = bs;
    return best;
}

double ring_distance(const std::vector<Vec2>& ring, Vec2 p) {
    double best = 1e18;
    for (std::size_t i = 0; i < ring.size(); ++i) {
        const Vec2 a = ring[i], b = ring[(i + 1) % ring.size()], ab = b - a;
        const double l2 = ab.x * ab.x + ab.y * ab.y;
        const double t = l2 > 0 ? std::clamp((p - a).x * ab.x / l2 + (p - a).y * ab.y / l2, 0.0, 1.0) : 0.0;
        best = std::min(best, (a + ab * t - p).length());
    }
    return best;
}

}  // namespace

TEST_CASE("geometry: curved-arm fuzz (3-5 arms)") {
    static const char* types[] = {"residential", "b_road", "a_road_dual"};
    PortableRng rng{20260101};
    int built = 0, degraded = 0, unconverged = 0, fillets = 0, shrunk = 0, needless = 0;
    for (int iter = 0; iter < 400; ++iter) {
        Net n;
        const int arms = 3 + static_cast<int>(rng.uniform(0, 3));
        std::vector<double> degs;
        while (static_cast<int>(degs.size()) < arms) {
            const double d = rng.uniform(0, 360);
            bool ok = true;
            for (double e : degs)
                if (std::fabs(std::remainder(d - e, 360.0)) < (iter < 300 ? 30.0 : 15.0)) ok = false;
            if (ok) degs.push_back(d);
        }
        for (double d : degs) {
            DrawRoadSpec s;
            s.road_type = types[static_cast<int>(rng.uniform(0, 3))];
            const double len = rng.uniform(90, 140), off = iter < 300 ? rng.uniform(-12, 12) : rng.uniform(-35, 35);
            const double ca = std::cos(d * kDeg), sa = std::sin(d * kDeg);
            RoadEnd c, far;
            far.pos = {len * ca, len * sa, 0};
            c.pos = {0, 0, 0};
            c.existing_node = n.centre;
            s.start = c;
            s.end = far;
            s.interior.push_back({0.5 * len * ca - off * sa, 0.5 * len * sa + off * ca, 0});
            REQUIRE(n.st.execute(make_draw_road(s)));
            if (!n.centre.valid()) n.centre = n.g.edges().rbegin()->second.a;
        }
        const JunctionSet set = build_junctions(n.g, *uk(), {});
        const auto it = set.junctions.find(n.centre);
        REQUIRE(it != set.junctions.end());
        const Junction& j = it->second;
        ++built;
        const std::size_t na = j.arms.size();
        CAPTURE(iter);
        CAPTURE(j.degraded_reason);
        if (j.degraded) {
            ++degraded;
            CHECK_FALSE(j.degraded_reason.empty());
            if (j.degraded_reason.find("converge") != std::string::npos) ++unconverged;
        }
        // Polygon convention: a set starts with an outer (CCW) ring; holes are CW and follow it.
        for (const PolygonSet* ps : {&j.carriageway, &j.pavements}) {
            if (!ps->empty()) CHECK(area((*ps)[0]) > 0);
            for (const auto& r : *ps)
                if (area(r) > 0) CHECK_FALSE(self_intersects(r));
        }
        if (j.carriageway.empty()) {
            CHECK(j.degraded);
            continue;
        }
        // Degraded junctions (flagged, with a reason) keep valid polygons but promise no more.
        if (j.degraded) continue;
        CHECK(j.carriageway.size() == 1);
        check_valid(j);
        // The outline meets every trim line (both kerb points) within 1 mm.
        for (std::size_t i = 0; i < na; ++i) {
            const JunctionArm& a = j.arms[i];
            CHECK(ring_distance(j.carriageway[0], arm_point(a, j.trims[i], a.kerb_left).pos.xy()) < 1e-3);
            CHECK(ring_distance(j.carriageway[0], arm_point(a, j.trims[i], -a.kerb_right).pos.xy()) < 1e-3);
        }
        for (std::size_t k = 0; k < na; ++k) {
            const std::size_t jj = (k + 1) % na;
            const CornerCurve& cc = j.corners[k];
            const JunctionArm &ak = j.arms[k], &aj = j.arms[jj];
            if (cc.angle >= kPi - 0.01) continue;
            ++fillets;
            const double requested = j.corner_radii[k];
            CHECK(cc.requested_radius == doctest::Approx(requested));
            CHECK(cc.radius >= 0);
            CHECK(cc.radius <= requested + 1e-9);
            double sp = 0, sq = 0;
            // Tangent points lie on the kerbs, inside the trims (sharp corners: the kerb meeting).
            const Vec3 first = cc.kerb.empty() ? Vec3{} : cc.kerb.front();
            const Vec3 last = cc.kerb.empty() ? Vec3{} : cc.kerb.back();
            if (cc.kerb.size() >= 2) {
                CHECK(kerb_distance(ak, ak.kerb_left, first, j.trims[k] + 0.5, sp) < 2e-3);
                CHECK(kerb_distance(aj, -aj.kerb_right, last, j.trims[jj] + 0.5, sq) < 2e-3);
                CHECK(sp <= j.trims[k] + 2e-3);
                CHECK(sq <= j.trims[jj] + 2e-3);
                // Tangent: the arc leaves/enters along the kerb headings (chord is within half a step).
                const Vec2 d0 = cc.kerb[1].xy() - first.xy();
                const Vec2 d1 = last.xy() - cc.kerb[cc.kerb.size() - 2].xy();
                CHECK(std::fabs(wrap_angle(std::atan2(d0.y, d0.x) - arm_point(ak, sp, 0).heading_out - kPi)) < 0.08);
                CHECK(std::fabs(wrap_angle(std::atan2(d1.y, d1.x) - arm_point(aj, sq, 0).heading_out)) < 0.08);
            }
            // A radius below the request is only legitimate when the fillet is pushed against a trim
            // line (the largest radius that fits); otherwise it would be a needless shrink. A sharp
            // result has no arc to measure: it needs the kerb meeting itself on a trim line.
            if (cc.radius < requested - 1e-9) {
                ++shrunk;
                if (cc.kerb.size() < 2) {
                    CHECK(cc.radius == 0);
                    continue;
                }
                const bool limited = sp > j.trims[k] - 5e-3 || sq > j.trims[jj] - 5e-3;
                if (!limited) ++needless;
                CHECK(limited);
            }
        }
    }
    std::printf("curved fuzz: built %d degraded %d (unconverged %d) fillets %d shrunk %d needless %d\n", built,
                degraded, unconverged, fillets, shrunk, needless);
    CHECK(built == 400);
    CHECK(needless == 0);
    CHECK(unconverged <= 8);  // solver stalls are rare, and always flagged
    CHECK(degraded < 120);
}
