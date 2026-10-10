// Road markings (sim_core/src/markings.cpp, data/regions/uk/marking_rules.json).
#include <algorithm>
#include <cmath>
#include <map>
#include <memory>
#include <set>
#include <string>

#include "doctest/doctest.h"
#include "cp/markings.h"
#include "cp/road_commands.h"
#include "portable_rng.h"
#include "scene_fixture.h"

using namespace cp;

namespace {

constexpr double kDeg = kPi / 180;

std::shared_ptr<const RegionPack> pack(DrivingSide side) {
    auto p = load_region_pack_by_id(CP_DATA_DIR, "uk");
    REQUIRE_MESSAGE(p, p.error);
    p.value->driving_side = side;
    return std::make_shared<const RegionPack>(*p);
}

const MarkingRules& uk_rules() {
    static const MarkingRules rules = [] {
        auto r = load_marking_rules_by_id(CP_DATA_DIR, "uk");
        REQUIRE_MESSAGE(r, r.error);
        return *r;
    }();
    return rules;
}

struct Net {
    std::shared_ptr<const RegionPack> region;
    RoadGraph g;
    CommandStack<RoadGraph> st{g};
    NodeId centre;
    std::vector<EdgeId> edges;

    explicit Net(DrivingSide side = DrivingSide::Left) : region(pack(side)) { g.set_region(region); }

    // Draws a road between the centre (created at the origin on first use) and `to`.
    bool spoke(Vec3 to, const char* type = "residential", bool one_way = false, bool inbound = false,
               std::vector<Vec3> via = {}) {
        DrawRoadSpec s;
        s.road_type = type;
        s.one_way = one_way;
        RoadEnd c, far;
        far.pos = to;
        c.pos = {0, 0, 0};
        c.existing_node = centre;
        s.start = inbound ? far : c;
        s.end = inbound ? c : far;
        s.interior = std::move(via);
        if (!st.execute(make_draw_road(s))) return false;
        const Edge& e = g.edges().rbegin()->second;
        edges.push_back(e.id);
        if (!centre.valid()) centre = inbound ? e.b : e.a;
        return true;
    }
    bool polar(double deg, double len, const char* type = "residential", bool one_way = false, bool inbound = false,
               double z = 0) {
        return spoke({len * std::cos(deg * kDeg), len * std::sin(deg * kDeg), z}, type, one_way, inbound);
    }
    JunctionSet junctions() const { return build_junctions(g, *region, {}); }
};

MarkingSet all_marks(const Net& n, const JunctionSet& js, const MarkingParams& p = {}) {
    return generate_markings(n.g, *n.region, uk_rules(), js, p).merged();
}

std::vector<const Marking*> of(const MarkingSet& s, MarkingCategory c) {
    std::vector<const Marking*> out;
    for (const Marking& m : s.marks)
        if (m.category == c) out.push_back(&m);
    return out;
}

Vec2 centroid(const MarkingSet& s, const Marking& m) {
    Vec2 c{0, 0};
    for (const Vec3& p : s.points(m)) c = c + p.xy();
    return c * (1.0 / m.count);
}

double mean_z(const MarkingSet& s, const Marking& m) {
    double z = 0;
    for (const Vec3& p : s.points(m)) z += p.z;
    return z / m.count;
}

double area(const MarkingSet& s, const Marking& m) {
    const auto p = s.points(m);
    double a = 0;
    for (std::size_t i = 0; i < p.size(); ++i) a += p[i].xy().cross(p[(i + 1) % p.size()].xy());
    return a / 2;
}

bool seg_cross(Vec2 a, Vec2 b, Vec2 c, Vec2 d) {
    const double eps = 1e-9;
    const double d1 = (b - a).cross(c - a), d2 = (b - a).cross(d - a);
    const double d3 = (d - c).cross(a - c), d4 = (d - c).cross(b - c);
    return d1 * d2 < -eps && d3 * d4 < -eps;
}

bool strictly_inside(Vec2 p, std::span<const Vec3> poly) {
    int wn = 0;
    double dmin = 1e18;
    for (std::size_t i = 0; i < poly.size(); ++i) {
        const Vec2 a = poly[i].xy(), b = poly[(i + 1) % poly.size()].xy();
        const Vec2 ab = b - a;
        const double t = std::clamp((p - a).dot(ab) / std::max(ab.dot(ab), 1e-18), 0.0, 1.0);
        dmin = std::min(dmin, (p - (a + ab * t)).length());
        if (a.y <= p.y) {
            if (b.y > p.y && ab.cross(p - a) > 0) ++wn;
        } else if (b.y <= p.y && ab.cross(p - a) < 0) --wn;
    }
    return wn != 0 && dmin > 1e-5;
}

bool marks_overlap(const MarkingSet& s, const Marking& a, const Marking& b) {
    const auto pa = s.points(a), pb = s.points(b);
    double ax0 = 1e18, ax1 = -1e18, ay0 = 1e18, ay1 = -1e18, bx0 = 1e18, bx1 = -1e18, by0 = 1e18, by1 = -1e18;
    for (const Vec3& p : pa) {
        ax0 = std::min(ax0, p.x), ax1 = std::max(ax1, p.x), ay0 = std::min(ay0, p.y), ay1 = std::max(ay1, p.y);
    }
    for (const Vec3& p : pb) {
        bx0 = std::min(bx0, p.x), bx1 = std::max(bx1, p.x), by0 = std::min(by0, p.y), by1 = std::max(by1, p.y);
    }
    if (ax1 < bx0 || bx1 < ax0 || ay1 < by0 || by1 < ay0) return false;
    if (std::fabs(mean_z(s, a) - mean_z(s, b)) > 0.5) return false;  // bridge over road
    if ((centroid(s, a) - centroid(s, b)).length() < 1e-6 && std::fabs(area(s, a) - area(s, b)) < 1e-6) return true;
    for (std::size_t i = 0; i < pa.size(); ++i)
        for (std::size_t k = 0; k < pb.size(); ++k)
            if (seg_cross(pa[i].xy(), pa[(i + 1) % pa.size()].xy(), pb[k].xy(), pb[(k + 1) % pb.size()].xy())) return true;
    for (const Vec3& p : pa)
        if (strictly_inside(p.xy(), pb)) return true;
    for (const Vec3& p : pb)
        if (strictly_inside(p.xy(), pa)) return true;
    return false;
}

// Number of overlapping pairs (arrow glyphs may overlap themselves: a fork is two overlapping parts).
int count_overlaps(const MarkingSet& s) {
    int n = 0;
    for (std::size_t i = 0; i < s.marks.size(); ++i)
        for (std::size_t k = i + 1; k < s.marks.size(); ++k) {
            if (s.marks[i].category == MarkingCategory::Arrow && s.marks[k].category == MarkingCategory::Arrow) continue;
            if (marks_overlap(s, s.marks[i], s.marks[k])) {
                if (++n <= 6) {
                    const Vec2 a = centroid(s, s.marks[i]), b = centroid(s, s.marks[k]);
                    MESSAGE("overlap: ", marking_category_name(s.marks[i].category), "/", s.style_id(s.marks[i]), " (", a.x, ",", a.y,
                            ") vs ", marking_category_name(s.marks[k].category), "/", s.style_id(s.marks[k]), " (", b.x, ",", b.y, ")");
                }
            }
        }
    return n;
}

void check_well_formed(const MarkingSet& s) {
    for (const Marking& m : s.marks) {
        REQUIRE(m.count >= 3);
        REQUIRE(m.first + m.count <= s.vertices.size());
        REQUIRE(m.style < s.styles.size());
        REQUIRE(m.colour < s.colours.size());
        for (const Vec3& p : s.points(m)) {
            REQUIRE(std::isfinite(p.x));
            REQUIRE(std::isfinite(p.y));
            REQUIRE(std::isfinite(p.z));
        }
        REQUIRE(area(s, m) > 1e-6);  // CCW and not degenerate
    }
}

// Position in an arm's outward frame (straight arms).
struct ArmFrame {
    Vec2 origin, dir;
    Vec2 rel(Vec3 p) const {
        const Vec2 d = p.xy() - origin;
        return {d.dot(dir), d.dot(Vec2{-dir.y, dir.x})};  // (s, lateral)
    }
};
ArmFrame frame_of(const Junction& j, int arm) {
    const double h = j.arms[static_cast<std::size_t>(arm)].heading_out;
    return {j.pos.xy(), {std::cos(h), std::sin(h)}};
}
int arm_of_edge(const Junction& j, EdgeId e) {
    for (std::size_t i = 0; i < j.arms.size(); ++i)
        if (j.arms[i].edge == e) return static_cast<int>(i);
    return -1;
}

}  // namespace

// ---------------------------------------------------------------- dash arithmetic

TEST_CASE("markings: dash layout arithmetic") {
    SUBCASE("module counts over a known length, symmetric") {
        const auto d = layout_dashes(100, 2, 4, 0.3);
        REQUIRE(d.size() == 17);
        for (const DashSpan& s : d) CHECK(s.s1 - s.s0 == doctest::Approx(2.0));
        CHECK(d.front().s0 == doctest::Approx(1.0));  // leftover 2 m split between the ends
        CHECK(100 - d.back().s1 == doctest::Approx(d.front().s0));
        CHECK(d[1].s0 - d[0].s1 == doctest::Approx(4.0));
    }
    SUBCASE("exact fit starts at zero") {
        const auto d = layout_dashes(14, 2, 4, 0.3);
        REQUIRE(d.size() == 3);
        CHECK(d.front().s0 == doctest::Approx(0));
        CHECK(d.back().s1 == doctest::Approx(14));
    }
    SUBCASE("a long enough leftover becomes partial marks at both ends") {
        const auto d = layout_dashes(33, 4, 2, 0.3);  // 5 whole marks use 28 m; leftover 5 m = 2 * (2 m gap + 0.5 m mark)
        REQUIRE(d.size() == 7);
        CHECK(d.front().s0 == doctest::Approx(0));
        CHECK(d.front().s1 == doctest::Approx(0.5));
        CHECK(d.back().s1 == doctest::Approx(33));
    }
    SUBCASE("minimum mark rule: leftover below the minimum is a margin, not a sliver") {
        const auto d = layout_dashes(33, 4, 2, 0.6);
        REQUIRE(d.size() == 5);
        for (const DashSpan& s : d) CHECK(s.s1 - s.s0 == doctest::Approx(4.0));
        CHECK(d.front().s0 == doctest::Approx(2.5));
    }
    SUBCASE("short runs") {
        auto d = layout_dashes(1.0, 2, 4, 0.3);
        REQUIRE(d.size() == 1);
        CHECK(d[0].s1 - d[0].s0 == doctest::Approx(1.0));
        CHECK(layout_dashes(0.2, 2, 4, 0.3).empty());
        CHECK(layout_dashes(0, 2, 4, 0.3).empty());
    }
    SUBCASE("continuous") {
        const auto d = layout_dashes(37, 1, 0, 0.3);
        REQUIRE(d.size() == 1);
        CHECK(d[0].s0 == 0);
        CHECK(d[0].s1 == 37);
    }
    SUBCASE("fuzz: ordered, inside, never shorter than the minimum, symmetric") {
        PortableRng rng{7};
        for (int i = 0; i < 2000; ++i) {
            const double L = rng.uniform(0.0, 300.0), mark = rng.uniform(0.5, 9.0), gap = rng.uniform(0.2, 9.0);
            const double minm = rng.uniform(0.1, 0.5);
            const auto d = layout_dashes(L, mark, gap, minm);
            for (std::size_t k = 0; k < d.size(); ++k) {
                REQUIRE(d[k].s0 >= -1e-9);
                REQUIRE(d[k].s1 <= L + 1e-9);
                REQUIRE(d[k].s1 - d[k].s0 >= minm - 1e-9);
                if (k > 0) REQUIRE(d[k].s0 >= d[k - 1].s1 + gap - 1e-9);
            }
            if (!d.empty()) REQUIRE(d.front().s0 == doctest::Approx(L - d.back().s1).epsilon(1e-6).scale(1.0));
        }
    }
}

// ---------------------------------------------------------------- rule loading

TEST_CASE("markings: UK rules load and validate") {
    const MarkingRules& r = uk_rules();
    CHECK(r.sets.count("urban_single"));
    CHECK(r.sets.count("rural_single"));
    CHECK(r.sets.count("dual"));
    CHECK(r.sets.count("motorway"));
    const LineStyle& c = r.styles.at("centre_low");
    REQUIRE(c.strands.size() == 1);
    CHECK(c.strands[0].mark == doctest::Approx(2.0));
    CHECK(c.strands[0].gap == doctest::Approx(4.0));
    CHECK(r.styles.at("warning_low").strands[0].mark == doctest::Approx(4.0));  // inverse of the centre line
    CHECK(r.styles.at("double_white").strands.size() == 2);
    CHECK(r.arrows.glyphs.at("fork").polygons.size() == 2);
    // Every road type's marking id has a rule set.
    const auto region = pack(DrivingSide::Left);
    for (const auto& [id, rt] : region->road_types)
        if (!rt.markings.empty()) CHECK_MESSAGE(r.sets.count(rt.markings), id);
}

TEST_CASE("markings: loader reports errors without throwing") {
    CHECK_FALSE(parse_marking_rules("not json"));
    CHECK_FALSE(parse_marking_rules("{}"));
    const char* base = R"({"colours":{"white":{"rgb":[1,1,1]}},"styles":{"a":{"colour":"white","strands":[{"width_m":0.1}]}},"sets":{)";
    auto ok = parse_marking_rules(std::string(base) + "}}");
    CHECK_MESSAGE(ok, ok.error);
    auto bad = parse_marking_rules(
        std::string(base) +
        R"("s":{"boundaries":[{"id":"x","category":"lane_line","style":"nope","source":"t"}]}}})");
    REQUIRE_FALSE(bad);
    CHECK(bad.error.find("nope") != std::string::npos);
    auto bad_colour = parse_marking_rules(
        R"({"colours":{},"styles":{"a":{"colour":"white","strands":[{"width_m":0.1}]}},"sets":{}})");
    CHECK_FALSE(bad_colour);
    auto bad_width = parse_marking_rules(
        R"({"colours":{"white":{"rgb":[1,1,1]}},"styles":{"a":{"colour":"white","strands":[{"width_m":-1}]}},"sets":{}})");
    CHECK_FALSE(bad_width);
    CHECK_FALSE(load_marking_rules("does/not/exist.json"));
}

// ---------------------------------------------------------------- single edges

TEST_CASE("markings: straight residential road") {
    Net n;
    REQUIRE(n.polar(0, 100));
    const JunctionSet js = n.junctions();
    const MarkingSet s = all_marks(n, js);
    check_well_formed(s);
    // A 6 m parked residential street has no centre line along its length (marking_rules.json choice).
    CHECK(of(s, MarkingCategory::CentreLine).empty());
    CHECK(of(s, MarkingCategory::LaneLine).empty());
    CHECK(of(s, MarkingCategory::EdgeLine).empty());  // kerbs, no hard shoulder
    // Parking: one bay line per side and 17 dividers per lane (16 bays of 6 m in 100 m).
    CHECK(of(s, MarkingCategory::ParkingBay).size() == 2 + 2 * 17);
    // Marks sit inside the carriageway (parking lanes included, kerb at 5.0 m).
    for (const Marking& m : s.marks)
        for (const Vec3& p : s.points(m)) CHECK(std::fabs(p.y) <= 5.0);
}

TEST_CASE("markings: dual carriageway and motorway carriageways") {
    for (const char* type : {"a_road_dual", "motorway"}) {
        Net n;
        REQUIRE(n.polar(0, 200, type, true));
        const JunctionSet js = n.junctions();
        const MarkingSet s = all_marks(n, js);
        check_well_formed(s);
        const auto lanes = instantiate_lanes(n.g.edges().begin()->second, *n.region->road_type(type), DrivingSide::Left);
        int driving = 0;
        for (const Lane& l : lanes) driving += l.dir != TravelDir::None;
        const std::size_t per_line = layout_dashes(200, 2, 7, 0.3).size();
        const auto lane_lines = of(s, MarkingCategory::LaneLine);
        CHECK(lane_lines.size() == static_cast<std::size_t>(driving - 1) * per_line);
        for (const Marking* m : lane_lines) CHECK(s.style_id(*m) == "lane_high_150");
        const auto edge = of(s, MarkingCategory::EdgeLine);
        REQUIRE(edge.size() == 2);  // continuous: offside strip side and nearside shoulder / strip side
        for (const Marking* m : edge) {
            CHECK(s.style_id(*m) == "edge_200");
            // Inside a driving lane, 0.225 m in from the carriageway edge.
            const double y = s.points(*m)[0].y;
            bool inside = false;
            for (const Lane& l : lanes)
                if (l.dir != TravelDir::None && std::fabs(y - l.offset) < l.width / 2) inside = true;
            CHECK(inside);
        }
        CHECK(of(s, MarkingCategory::CentreLine).empty());
    }
}

TEST_CASE("markings: lane-count change gives each edge its own lines") {
    Net n;
    REQUIRE(n.polar(0, 80, "b_road", true));
    // Second edge continues to the east as a different type.
    DrawRoadSpec spec;
    spec.road_type = "b_road";
    RoadEnd a, b;
    a.pos = {80, 0, 0};
    a.existing_node = n.g.edges().begin()->second.b;
    b.pos = {160, 0, 0};
    spec.start = a;
    spec.end = b;
    REQUIRE(n.st.execute(make_draw_road(spec)));
    const JunctionSet js = n.junctions();
    const MarkingSet s = all_marks(n, js);
    check_well_formed(s);
    CHECK(!of(s, MarkingCategory::CentreLine).empty());  // the two-way part
    CHECK(!of(s, MarkingCategory::LaneLine).empty());    // the two-lane one-way part
}

TEST_CASE("markings: unknown marking set is a warning, not a failure") {
    Net n;
    REQUIRE(n.polar(0, 50));
    MarkingRules rules = uk_rules();
    rules.sets.clear();
    const JunctionSet js = n.junctions();
    const MarkingSet s = generate_edge_markings(n.g, *n.region, rules, js, n.edges[0]);
    CHECK(s.empty());
    CHECK(!s.warnings.empty());
}

// ---------------------------------------------------------------- junctions

namespace {

struct TJunction {
    Net n;
    EdgeId major_e, major_w, minor;
    explicit TJunction(DrivingSide side = DrivingSide::Left, double mirror = 1.0) : n(side) {
        REQUIRE(n.polar(0, 100, "b_road"));
        major_e = n.edges.back();
        REQUIRE(n.polar(180, 100, "b_road"));
        major_w = n.edges.back();
        REQUIRE(n.polar(-90 * mirror, 90, "residential"));
        minor = n.edges.back();
    }
};

}  // namespace

TEST_CASE("markings: T junction give-way line, warning lines, trims") {
    TJunction t;
    const JunctionSet js = t.n.junctions();
    const Junction* j = js.find(t.n.centre);
    REQUIRE(j);
    const MarkingSet s = all_marks(t.n, js);
    check_well_formed(s);
    const int minor_arm = arm_of_edge(*j, t.minor);
    REQUIRE(minor_arm >= 0);
    const JunctionArm& arm = j->arms[static_cast<std::size_t>(minor_arm)];
    const ArmFrame fr = frame_of(*j, minor_arm);
    const double trim = j->trims[static_cast<std::size_t>(minor_arm)];

    SUBCASE("give way line on the minor arm only, spanning exactly the inbound lanes, set back from the trim") {
        const auto gw = of(s, MarkingCategory::GiveWayLine);
        REQUIRE(!gw.empty());
        double s_min = 1e9, s_max = -1e9, l_min = 1e9, l_max = -1e9;
        for (const Marking* m : gw) {
            CHECK(m->edge == t.minor);
            CHECK(m->node == t.n.centre);
            CHECK(s.style_id(*m) == "give_way_low");
            for (const Vec3& p : s.points(*m)) {
                const Vec2 r = fr.rel(p);
                s_min = std::min(s_min, r.x), s_max = std::max(s_max, r.x);
                l_min = std::min(l_min, r.y), l_max = std::max(l_max, r.y);
            }
        }
        double lo = 1e9, hi = -1e9;
        for (const LaneRef& l : arm.lanes)
            if (l.inbound) lo = std::min(lo, l.lateral - l.width / 2), hi = std::max(hi, l.lateral + l.width / 2);
        // Dashes are laid symmetrically inside the span: they never leave it and nearly fill it.
        CHECK(l_min >= lo - 1e-6);
        CHECK(l_max <= hi + 1e-6);
        CHECK(l_min - lo <= 0.45);
        CHECK(hi - l_max <= 0.45);
        CHECK(s_min == doctest::Approx(trim + 0.5).epsilon(1e-6));
        CHECK(s_max - s_min == doctest::Approx(0.2).epsilon(1e-6));
        // The triangle sits behind the line, apex away from the junction.
        const auto tri = of(s, MarkingCategory::GiveWayTriangle);
        CHECK(tri.size() == 3);
        for (const Marking* m : tri)
            for (const Vec3& p : s.points(*m)) CHECK(fr.rel(p).x > s_max);
    }

    SUBCASE("no give way lines on the major arms; stop lines only when asked") {
        for (const Marking* m : of(s, MarkingCategory::GiveWayLine)) CHECK(m->edge != t.major_e);
        CHECK(of(s, MarkingCategory::StopLine).empty());
        MarkingParams p;
        p.stop_arms.insert({t.n.centre, t.minor});
        const MarkingSet s2 = all_marks(t.n, js, p);
        CHECK(of(s2, MarkingCategory::StopLine).size() == 1);
        CHECK(of(s2, MarkingCategory::GiveWayLine).empty());
    }

    SUBCASE("centre line switches to the warning style on approach") {
        // Residential (30 mph): 5 marks of a 6 m module = 30 m. B road (40 mph): 7 marks = 42 m.
        auto check = [&](EdgeId e, double zone, bool expect_plain) {
            const int arm_i = arm_of_edge(*j, e);
            const ArmFrame f = frame_of(*j, arm_i);
            int warning = 0, centre = 0;
            double first = 1e9;  // the zone is measured from where the lines start (after the arm markings)
            for (const Marking* m : of(s, MarkingCategory::CentreLine))
                if (m->edge == e) first = std::min(first, f.rel(s.points(*m)[0]).x);
            for (const Marking* m : of(s, MarkingCategory::CentreLine)) {
                if (m->edge != e) continue;
                const double d = f.rel(s.points(*m)[0]).x - first;
                const bool is_warning = s.style_id(*m).rfind("warning", 0) == 0;
                (is_warning ? warning : centre) += 1;
                if (d < zone - 1.0) CHECK(is_warning);
                if (d > zone + 8.0) CHECK_FALSE(is_warning);
            }
            CHECK(warning >= 4);
            if (expect_plain) CHECK(centre >= 3);
            else CHECK(centre == 0);
        };
        check(t.minor, 30.0, false);
        check(t.major_e, 42.0, true);
    }

    SUBCASE("marks never cross a junction trim, overlap, or leave the carriageway") {
        for (const Marking& m : s.marks) {
            if (!m.edge.valid() || m.category == MarkingCategory::Hatching) continue;
            const int ai = arm_of_edge(*j, m.edge);
            REQUIRE(ai >= 0);
            const ArmFrame f = frame_of(*j, ai);
            const JunctionArm& a = j->arms[static_cast<std::size_t>(ai)];
            for (const Vec3& p : s.points(m)) {
                const Vec2 r = f.rel(p);
                CHECK(r.x >= j->trims[static_cast<std::size_t>(ai)] - 1e-6);
                CHECK(r.y <= a.kerb_left + 1e-6);
                CHECK(r.y >= -a.kerb_right - 1e-6);
            }
        }
        CHECK(count_overlaps(s) == 0);
    }

    SUBCASE("zebras are never automatic; a placed crossing marks only its own arm") {
        CHECK(of(s, MarkingCategory::Zebra).empty());
        MarkingParams p;
        p.crossing_arms.insert({t.n.centre, t.minor});
        const MarkingSet s2 = all_marks(t.n, js, p);
        check_well_formed(s2);
        const auto z = of(s2, MarkingCategory::Zebra);
        CHECK(z.size() > 10);
        double zmin = 1e9;
        for (const Marking* m : z) {
            CHECK(m->edge == t.minor);
            for (const Vec3& v : s2.points(*m)) zmin = std::min(zmin, fr.rel(v).x);
        }
        double gw_max = -1e9;
        for (const Marking* m : of(s2, MarkingCategory::GiveWayLine))
            for (const Vec3& v : s2.points(*m)) gw_max = std::max(gw_max, fr.rel(v).x);
        CHECK(zmin >= gw_max + 0.99);
        CHECK(count_overlaps(s2) == 0);
    }
}

TEST_CASE("markings: both driving sides mirror exactly") {
    // Left network, and the same network reflected in the x axis under right-hand traffic.
    auto build = [](DrivingSide side, double m) {
        auto n = std::make_unique<Net>(side);
        REQUIRE(n->polar(0, 100, "b_road"));
        REQUIRE(n->polar(180, 100, "b_road"));
        REQUIRE(n->polar(-90 * m, 90, "residential"));
        REQUIRE(n->polar(90 * m, 120, "b_road", true, true));  // one-way inbound, two lanes: arrows
        return n;
    };
    auto left = build(DrivingSide::Left, 1.0);
    auto right = build(DrivingSide::Right, -1.0);
    // The right-hand network is the reflection (y -> -y): draw the mirrored positions.
    const JunctionSet jl = left->junctions(), jr = right->junctions();
    // `right` was built with mirrored bearings for the 3rd and 4th spokes only; the first two are on the axis.
    const MarkingSet sl = all_marks(*left, jl), sr = all_marks(*right, jr);
    check_well_formed(sl);
    check_well_formed(sr);
    std::map<std::string, int> cl, cr;
    for (const Marking& m : sl.marks) ++cl[std::string(marking_category_name(m.category)) + "/" + sl.style_id(m)];
    for (const Marking& m : sr.marks) ++cr[std::string(marking_category_name(m.category)) + "/" + sr.style_id(m)];
    CHECK(cl == cr);
    CHECK(sl.marks.size() == sr.marks.size());
    // Every left mark has a mirror image (y -> -y) among the right marks.
    std::vector<Vec2> pr;
    for (const Marking& m : sr.marks) pr.push_back(centroid(sr, m));
    int missing = 0;
    for (const Marking& m : sl.marks) {
        const Vec2 c = centroid(sl, m);
        bool found = false;
        for (const Vec2& q : pr)
            if (std::fabs(q.x - c.x) < 1e-5 && std::fabs(q.y + c.y) < 1e-5) {
                found = true;
                break;
            }
        if (!found) ++missing;
    }
    CHECK(missing == 0);
    CHECK(!of(sl, MarkingCategory::Arrow).empty());
}

TEST_CASE("markings: determinism") {
    TJunction t;
    const JunctionSet js = t.n.junctions();
    const MarkingSet a = all_marks(t.n, js), b = all_marks(t.n, js);
    REQUIRE(a.vertices.size() == b.vertices.size());
    CHECK(a.marks.size() == b.marks.size());
    for (std::size_t i = 0; i < a.vertices.size(); ++i) {
        REQUIRE(a.vertices[i].x == b.vertices[i].x);
        REQUIRE(a.vertices[i].y == b.vertices[i].y);
        REQUIRE(a.vertices[i].z == b.vertices[i].z);
    }
}

TEST_CASE("markings: marks follow a curved, sloping road surface within 1 mm") {
    Net n;
    REQUIRE(n.spoke({120, 40, 6}, "b_road", false, false, {{40, 5, 1}, {80, 35, 4}}));
    const JunctionSet js = n.junctions();
    MarkingParams p;
    p.chord_tolerance = 0.005;
    const MarkingSet s = all_marks(n, js, p);
    check_well_formed(s);
    REQUIRE(!s.marks.empty());
    const Edge& e = n.g.edges().begin()->second;
    const double L = e.horizontal.length();
    // Nearest centreline point by a coarse scan plus refinement.
    auto surface_z = [&](Vec3 q) {
        double best = 1e18, bs = 0;
        for (double sv = 0; sv <= L; sv += 0.25) {
            const double d = (edge_point(e, sv).xy() - q.xy()).length();
            if (d < best) best = d, bs = sv;
        }
        double lo = std::max(0.0, bs - 0.25), hi = std::min(L, bs + 0.25);
        for (int i = 0; i < 40; ++i) {
            const double m1 = lo + (hi - lo) / 3, m2 = hi - (hi - lo) / 3;
            if ((edge_point(e, m1).xy() - q.xy()).length() < (edge_point(e, m2).xy() - q.xy()).length()) hi = m2;
            else lo = m1;
        }
        return edge_point(e, (lo + hi) / 2).z;
    };
    for (const Marking& m : s.marks)
        for (const Vec3& q : s.points(m)) REQUIRE(std::fabs(q.z - surface_z(q)) < 1e-3);
    // Curve tessellation follows the LOD parameter.
    MarkingParams coarse;
    coarse.chord_tolerance = 0.2;
    CHECK(all_marks(n, js, coarse).vertices.size() < s.vertices.size());
}

TEST_CASE("markings: bend switches the centre line to a warning line") {
    Net n;
    // A tight bend (radius well under 200 m) on a 40 mph road.
    REQUIRE(n.spoke({150, 150, 0}, "b_road", false, false, {{100, 0, 0}, {140, 60, 0}}));
    const JunctionSet js = n.junctions();
    const MarkingSet s = all_marks(n, js);
    int warning = 0, centre = 0;
    for (const Marking* m : of(s, MarkingCategory::CentreLine)) (s.style_id(*m).rfind("warning", 0) == 0 ? warning : centre) += 1;
    CHECK(warning > 0);
    CHECK(centre > 0);
}

TEST_CASE("markings: dirty rebuild matches a full rebuild") {
    TJunction t;
    const JunctionSet js = t.n.junctions();
    MarkingResult cache = generate_markings(t.n.g, *t.n.region, uk_rules(), js);
    const std::size_t full = cache.merged().marks.size();
    cache.edges.erase(t.minor);
    cache.junctions.clear();
    rebuild_dirty_markings(cache, t.n.g, *t.n.region, uk_rules(), js, {t.n.centre}, {});
    CHECK(cache.merged().marks.size() == full);
}

// ---------------------------------------------------------------- roundabout, slips, interchange

TEST_CASE("markings: roundabout entry lines on approaches only") {
    Net n;
    for (double d : {0.0, 90.0, 180.0, 270.0}) REQUIRE(n.polar(d, 100, "b_road"));
    REQUIRE(n.st.execute(make_place_roundabout({0, 0, 0}, 25, "roundabout_ring", *n.region)));
    const JunctionSet js = n.junctions();
    const MarkingSet s = all_marks(n, js);
    check_well_formed(s);
    std::set<EdgeId> entry_edges;
    for (const Marking* m : of(s, MarkingCategory::RoundaboutEntry)) {
        entry_edges.insert(m->edge);
        const Edge* e = n.g.edge(m->edge);
        REQUIRE(e);
        CHECK_FALSE(n.region->road_type(e->road_type)->roundabout_ring);
    }
    CHECK(entry_edges.size() == 4);
    CHECK(of(s, MarkingCategory::GiveWayLine).empty());  // ring nodes use entry lines, not priority give way
    // The ring carries lane lines between its two same-direction lanes.
    int ring_lane_lines = 0;
    for (const Marking* m : of(s, MarkingCategory::LaneLine)) {
        const Edge* e = n.g.edge(m->edge);
        if (e && n.region->road_type(e->road_type)->roundabout_ring) ++ring_lane_lines;
    }
    CHECK(ring_lane_lines > 0);
    CHECK(count_overlaps(s) == 0);
}

TEST_CASE("markings: interchange fixture generates cleanly") {
    std::string err;
    auto sc = cp::tools::build_scene("interchange", CP_DATA_DIR, err);
    REQUIRE_MESSAGE(sc, err);
    const JunctionSet js = cp::tools::scene_junctions(*sc);
    const MarkingResult r = generate_markings(sc->graph, *sc->region, uk_rules(), js);
    const MarkingSet s = r.merged();
    check_well_formed(s);
    std::map<std::string, int> counts;
    for (const Marking& m : s.marks) ++counts[marking_category_name(m.category)];
    std::string line;
    for (const auto& [k, v] : counts) line += k + "=" + std::to_string(v) + " ";
    MESSAGE("interchange marks: ", s.marks.size(), " total; ", line);
    for (const std::string& w : s.warnings) MESSAGE("warning: ", w);
    CHECK(s.marks.size() > 100);
    CHECK(counts["centre_line"] > 0);
    CHECK(counts["lane_line"] > 0);
    CHECK(counts["edge_line"] > 0);
    CHECK(counts["roundabout_entry"] > 0);
    CHECK(counts["zebra"] == 0);
    CHECK(counts["chevron"] > 0);
    CHECK(counts["hatching"] == 0);  // the interchange has only same-direction noses
    CHECK(counts["give_way_line"] > 0);
    CHECK(count_overlaps(s) == 0);
    int slip_edges = 0;
    for (const Marking& m : s.marks) slip_edges += m.category == MarkingCategory::EdgeLine && m.node.valid();
    CHECK(slip_edges > 0);
    // Triangulation covers every mark.
    for (const Marking& m : s.marks) REQUIRE(!triangulate_marking(s, m).empty());
}

TEST_CASE("markings: fuzz over random junctions") {
    PortableRng rng{20261010};
    const char* types[] = {"residential", "b_road", "a_road_dual", "motorway", "residential", "b_road"};
    int built = 0;
    for (int iter = 0; iter < 60; ++iter) {
        Net n(iter % 2 ? DrivingSide::Right : DrivingSide::Left);
        const int arms = 3 + static_cast<int>(rng.next() % 3);
        const double base = rng.uniform(0, 360);
        int made = 0;
        for (int a = 0; a < arms; ++a) {
            const double deg = base + a * 360.0 / arms + rng.uniform(-18, 18);
            const char* type = types[rng.next() % 6];
            const bool dual = std::string(type) == "a_road_dual" || std::string(type) == "motorway";
            const bool one_way = dual || (rng.next() % 4 == 0);
            const bool inbound = rng.next() % 2 == 0;
            if (n.polar(deg, rng.uniform(70, 160), type, one_way, inbound)) ++made;
        }
        if (made < 3) continue;
        ++built;
        const JunctionSet js = n.junctions();
        const MarkingSet s = all_marks(n, js);
        for (const Marking& m : s.marks) {
            REQUIRE(m.count >= 3);
            REQUIRE(area(s, m) > 1e-6);
            for (const Vec3& p : s.points(m)) {
                REQUIRE(std::isfinite(p.x));
                REQUIRE(std::isfinite(p.y));
                REQUIRE(std::isfinite(p.z));
            }
        }
    }
    CHECK(built > 20);
}

TEST_CASE("markings: B road keeps its centre line") {
    Net n;
    REQUIRE(n.polar(0, 100, "b_road"));
    const JunctionSet js = n.junctions();
    const MarkingSet s = all_marks(n, js);
    check_well_formed(s);
    const auto centre = of(s, MarkingCategory::CentreLine);
    CHECK(centre.size() == 17);  // 100 m, 2 m mark / 4 m gap (40 mph)
    for (const Marking* m : centre) {
        CHECK(s.style_id(*m) == "centre_low");
        for (const Vec3& p : s.points(*m)) CHECK(std::fabs(p.y) <= 0.05 + 1e-9);
    }
}

// ---------------------------------------------------------------- gore patterns

namespace {

// One-way b_road in from the west splitting into two outbound roads at +-15 degrees (a diverge), or the
// same with the flows reversed (a merge).
struct SlipNode {
    Net n;
    explicit SlipNode(DrivingSide side, bool diverge) : n(side) {
        REQUIRE(n.polar(180, 100, "b_road", true, diverge));
        REQUIRE(n.polar(15, 100, "b_road", true, !diverge));
        REQUIRE(n.polar(-15, 100, "b_road", true, !diverge));
    }
};

// Wings of every chevron trail behind its apex, relative to the travel direction through the gore.
void check_chevron_direction(const MarkingSet& s, const Junction& j) {
    REQUIRE(j.gore.size() == 1);
    Vec2 c{0, 0};
    for (const Vec2& p : j.gore[0]) c = c + p;
    c = c * (1.0 / static_cast<double>(j.gore[0].size()));
    // Travel direction: out along the pair arms of a diverge, in along them for a merge.
    Vec2 f{0, 0};
    for (const JunctionArm& a : j.arms) {
        const bool odd_in = a.inbound_count() > 0;
        const Vec2 out{std::cos(a.heading_out), std::sin(a.heading_out)};
        if (j.cls == NodeClass::Diverge && !odd_in) f = f + out;
        if (j.cls == NodeClass::Merge && odd_in) f = f + out * -1.0;
    }
    f = f * (1.0 / f.length());
    const Vec2 side{-f.y, f.x};
    int checked = 0;
    for (const Marking* m : of(s, MarkingCategory::Chevron)) {
        double u_axis = 0, u_wing = 0, lat_max = 0;
        int n_axis = 0;
        for (const Vec3& p : s.points(*m)) lat_max = std::max(lat_max, std::fabs(side.dot(p.xy() - c)));
        if (lat_max < 0.6) continue;  // small pieces are clipped by the border and the polygon
        for (const Vec3& p : s.points(*m)) {
            const double lat = std::fabs(side.dot(p.xy() - c)), u = f.dot(p.xy() - c);
            if (lat < 1e-6) u_axis += u, ++n_axis;
            if (lat > lat_max - 1e-6) u_wing = u;
        }
        if (n_axis == 0) continue;
        CHECK(u_wing < u_axis / n_axis);
        ++checked;
    }
    CHECK(checked > 3);
}

}  // namespace

TEST_CASE("markings: same-direction gore gets chevrons pointing with the traffic, bordered") {
    for (DrivingSide side : {DrivingSide::Left, DrivingSide::Right})
        for (bool diverge : {true, false}) {
            SlipNode t(side, diverge);
            const JunctionSet js = t.n.junctions();
            const Junction* j = js.find(t.n.centre);
            REQUIRE(j);
            REQUIRE((j->cls == NodeClass::Diverge || j->cls == NodeClass::Merge));
            const MarkingSet s = generate_junction_markings(t.n.g, *t.n.region, uk_rules(), *j);
            check_well_formed(s);
            CHECK(of(s, MarkingCategory::Hatching).empty());
            check_chevron_direction(s, *j);
            int border = 0;
            for (const Marking* m : of(s, MarkingCategory::EdgeLine)) border += s.style_id(*m).rfind("edge_", 0) == 0;
            CHECK(border >= 3);
            CHECK(count_overlaps(s) == 0);
        }
}

TEST_CASE("markings: diverge under both driving sides mirrors exactly") {
    SlipNode l(DrivingSide::Left, true), r(DrivingSide::Right, true);
    const JunctionSet jl = l.n.junctions(), jr = r.n.junctions();
    const MarkingSet sl = all_marks(l.n, jl), sr = all_marks(r.n, jr);
    REQUIRE(sl.marks.size() == sr.marks.size());
    for (const Marking& m : sl.marks) {
        const Vec2 c = centroid(sl, m);
        bool found = false;
        for (const Marking& q : sr.marks) {
            const Vec2 d = centroid(sr, q);
            if (q.category == m.category && std::fabs(d.x - c.x) < 1e-5 && std::fabs(d.y + c.y) < 1e-5) {
                found = true;
                break;
            }
        }
        REQUIRE(found);
    }
}

TEST_CASE("markings: opposing flows either side of a gore get diagonal hatching") {
    SlipNode t(DrivingSide::Left, true);
    const JunctionSet js = t.n.junctions();
    Junction j = *js.find(t.n.centre);
    REQUIRE(j.cls == NodeClass::Diverge);
    // Reverse the flow of one arm next to the gore: the nose now separates opposing traffic.
    for (JunctionArm& a : j.arms)
        if (a.inbound_count() == 0) {
            for (LaneRef& l : a.lanes) l.inbound = true;
            break;
        }
    const MarkingSet s = generate_junction_markings(t.n.g, *t.n.region, uk_rules(), j);
    CHECK(of(s, MarkingCategory::Chevron).empty());
    CHECK(of(s, MarkingCategory::Hatching).size() > 3);
    // The choice is data: with the opposing pattern set to none only the border remains.
    MarkingRules rules = uk_rules();
    rules.gore.opposing = "none";
    const MarkingSet s2 = generate_junction_markings(t.n.g, *t.n.region, rules, j);
    CHECK(of(s2, MarkingCategory::Hatching).empty());
    CHECK(of(s2, MarkingCategory::Chevron).empty());
}
