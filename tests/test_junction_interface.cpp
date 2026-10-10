// Interface tests for the junction generator (docs/junctions.md). collect_arms,
// classify_node, compute_trims and classify_turn are real; the tests tagged "[A golden]" etc.
// are skipped placeholders that record the golden numbers each part must hit.
#include <cmath>
#include <memory>

#include "doctest/doctest.h"
#include "cp/junction.h"
#include "cp/road_commands.h"

using namespace cp;

namespace {

std::shared_ptr<const RegionPack> uk() {
    static const std::shared_ptr<const RegionPack> pack = [] {
        auto p = load_region_pack_by_id(CP_DATA_DIR, "uk");
        REQUIRE_MESSAGE(p, p.error);
        return std::make_shared<const RegionPack>(*p);
    }();
    return pack;
}

constexpr double kDeg = kPi / 180;

struct Net {
    RoadGraph g;
    CommandStack<RoadGraph> st{g};
    NodeId centre;

    Net() { g.set_region(uk()); }

    // Draws a road from the centre (creating it at the origin on first use) to `to`.
    // Edges drawn toward the centre (inbound = true) run to -> centre.
    void spoke(Vec3 to, const char* type = "residential", bool one_way = false, bool inbound = false) {
        DrawRoadSpec s;
        s.road_type = type;
        s.one_way = one_way;
        RoadEnd c, far;
        far.pos = to;
        c.pos = {0, 0, 0};
        c.existing_node = centre;
        s.start = inbound ? far : c;
        s.end = inbound ? c : far;
        REQUIRE(st.execute(make_draw_road(s)));
        if (!centre.valid()) {
            const Edge& e = g.edges().rbegin()->second;
            centre = inbound ? e.b : e.a;
        }
    }
    void polar(double deg, double len, const char* type = "residential", bool one_way = false, bool inbound = false) {
        spoke({len * std::cos(deg * kDeg), len * std::sin(deg * kDeg), 0}, type, one_way, inbound);
    }
};

}  // namespace

TEST_CASE("T junction: three arms in CCW order with lanes and widths") {
    Net n;
    n.polar(0, 100);
    n.polar(180, 100);
    n.polar(-90, 80);
    const auto arms = collect_arms(n.g, n.centre, *uk());
    REQUIRE(arms.size() == 3);
    CHECK(arms[0].heading_out == doctest::Approx(-kPi / 2).epsilon(1e-6));  // side road
    CHECK(arms[1].heading_out == doctest::Approx(0).epsilon(1e-6));
    CHECK(std::fabs(arms[2].heading_out) == doctest::Approx(kPi).epsilon(1e-6));
    for (const auto& a : arms) {
        CHECK(a.lanes.size() == 2);  // two driving lanes; parking and pavement do not connect
        CHECK(a.inbound_count() == 1);
        CHECK(a.outbound_count() == 1);
        CHECK(a.half_width_left == doctest::Approx(7.15));
        CHECK(a.half_width_right == doctest::Approx(7.15));
        CHECK(a.kerb_left == doctest::Approx(5.0));
        CHECK(a.kerb_right == doctest::Approx(5.0));
    }
    // Lanes are ordered left to right; the two lanes differ in direction.
    CHECK(std::fabs(arms[1].lanes[0].lateral) == doctest::Approx(1.5));
    CHECK(arms[1].lanes[0].lateral > arms[1].lanes[1].lateral);
    CHECK(arms[1].lanes[0].inbound != arms[1].lanes[1].inbound);
    // Lane geometry at s: the east arm is along +x, so lateral 1.5 is y = +1.5.
    const ArmPoint p = arm_point(arms[1], 11.0, arms[1].lanes[0].lateral);
    CHECK(p.pos.x == doctest::Approx(11.0));
    CHECK(p.pos.y == doctest::Approx(arms[1].lanes[0].lateral));
    CHECK(classify_node(n.g, n.centre, *uk()) == NodeClass::Junction);
}

TEST_CASE("driving side flips which side the inbound lane is on") {
    Net n;
    n.polar(0, 100);
    n.polar(180, 100);
    n.polar(-90, 80);
    RegionPack right = *uk();
    right.driving_side = DrivingSide::Right;
    const auto l = collect_arms(n.g, n.centre, *uk());
    const auto r = collect_arms(n.g, n.centre, right);
    REQUIRE(l.size() == 3);
    REQUIRE(r.size() == 3);
    CHECK(l[1].lanes[0].inbound != r[1].lanes[0].inbound);
}

TEST_CASE("X junction and Y junction (120 degrees)") {
    {
        Net n;
        for (double d : {0.0, 90.0, 180.0, 270.0}) n.polar(d, 100);
        const auto arms = collect_arms(n.g, n.centre, *uk());
        CHECK(arms.size() == 4);
        CHECK(classify_node(n.g, n.centre, *uk()) == NodeClass::Junction);
        CHECK(major_arm_indices(arms).size() == 4);  // all equal: all tie
    }
    {
        Net n;
        for (double d : {90.0, 210.0, 330.0}) n.polar(d, 100);
        const auto arms = collect_arms(n.g, n.centre, *uk());
        REQUIRE(arms.size() == 3);
        for (std::size_t i = 0; i < 3; ++i) {
            const double gap = arms[(i + 1) % 3].heading_out - arms[i].heading_out;
            CHECK(std::fmod(gap + 2 * kPi, 2 * kPi) == doctest::Approx(120 * kDeg).epsilon(1e-6));
        }
        CHECK(classify_node(n.g, n.centre, *uk()) == NodeClass::Junction);
    }
}

TEST_CASE("major arms follow road class, then width") {
    Net n;
    n.polar(0, 100, "b_road");
    n.polar(180, 100, "b_road");
    n.polar(-90, 80, "residential");
    const auto arms = collect_arms(n.g, n.centre, *uk());
    const auto major = major_arm_indices(arms);
    REQUIRE(major.size() == 2);
    for (int i : major) CHECK(arms[static_cast<std::size_t>(i)].road_class == 70);
}

TEST_CASE("end, joint, merge and diverge classification") {
    {
        Net n;
        n.polar(0, 100);
        CHECK(classify_node(n.g, n.centre, *uk()) == NodeClass::End);
    }
    {
        Net n;
        n.polar(0, 100);
        n.polar(180, 100);
        CHECK(classify_node(n.g, n.centre, *uk()) == NodeClass::Joint);
    }
    {  // two one-way roads flow in at +-6 degrees, one flows out the opposite way
        Net n;
        n.polar(174, 100, "residential", true, true);
        n.polar(186, 100, "residential", true, true);
        n.polar(0, 100, "residential", true, false);
        CHECK(classify_node(n.g, n.centre, *uk()) == NodeClass::Merge);
    }
    {
        Net n;
        n.polar(174, 100, "residential", true, false);
        n.polar(186, 100, "residential", true, false);
        n.polar(0, 100, "residential", true, true);
        CHECK(classify_node(n.g, n.centre, *uk()) == NodeClass::Diverge);
    }
    {  // one-way but 120 degrees apart: a plain junction, not a merge
        Net n;
        n.polar(90, 100, "residential", true, true);
        n.polar(210, 100, "residential", true, true);
        n.polar(330, 100, "residential", true, false);
        CHECK(classify_node(n.g, n.centre, *uk()) == NodeClass::Junction);
    }
}

TEST_CASE("compute_trims: 90 degree corners, kerb 5 m, radius 6 m -> 11 m") {
    Net n;
    for (double d : {0.0, 90.0, 180.0, 270.0}) n.polar(d, 100);
    const auto arms = collect_arms(n.g, n.centre, *uk());
    const auto t = compute_trims(arms, std::vector<double>(4, 6.0));
    REQUIRE(t.size() == 4);
    for (double v : t) CHECK(v == doctest::Approx(11.0));
    for (double v : compute_trims(arms, std::vector<double>(4, 0.0))) CHECK(v == doctest::Approx(5.0));
    CHECK(compute_trims(arms, {1.0}).empty());
}

TEST_CASE("compute_trims: T junction has no corner on the straight side; Y trims longer") {
    {
        Net n;
        n.polar(0, 100);
        n.polar(180, 100);
        n.polar(-90, 80);
        const auto arms = collect_arms(n.g, n.centre, *uk());
        for (double v : compute_trims(arms, std::vector<double>(3, 6.0))) CHECK(v == doctest::Approx(11.0));
    }
    {  // 120 degrees: (a*cos + b)/sin + r/tan(theta/2), see docs/junctions.md
        Net n;
        for (double d : {90.0, 210.0, 330.0}) n.polar(d, 100);
        const auto arms = collect_arms(n.g, n.centre, *uk());
        const double th = 120 * kDeg;
        const double expect = 5.0 * std::cos(th) / std::sin(th) + 5.0 / std::sin(th) + 6.0 / std::tan(th / 2);
        for (double v : compute_trims(arms, std::vector<double>(3, 6.0))) CHECK(v == doctest::Approx(expect));
    }
}

TEST_CASE("classify_turn is side-agnostic") {
    CHECK(classify_turn(0.1, DrivingSide::Left) == TurnKind::Straight);
    CHECK(classify_turn(kPi / 2, DrivingSide::Left) == TurnKind::Nearside);   // left turn, keep-left
    CHECK(classify_turn(-kPi / 2, DrivingSide::Left) == TurnKind::Offside);
    CHECK(classify_turn(kPi / 2, DrivingSide::Right) == TurnKind::Offside);
    CHECK(classify_turn(-kPi / 2, DrivingSide::Right) == TurnKind::Nearside);
    CHECK(classify_turn(kPi * 0.95, DrivingSide::Left) == TurnKind::UTurn);
    CHECK(turn_is_left(TurnKind::Nearside, DrivingSide::Left));
    CHECK_FALSE(turn_is_left(TurnKind::Nearside, DrivingSide::Right));
}

TEST_CASE("override keys: corner radius changes the resolved radius and the trim") {
    Net n;
    for (double d : {0.0, 90.0, 180.0, 270.0}) n.polar(d, 100);
    const auto arms = collect_arms(n.g, n.centre, *uk());
    JunctionOverrides ov;
    ov.corner_radius[{n.centre, arms[1].edge, arms[0].edge}] = 10.0;  // corner 0: edge_l = arms[1], edge_r = arms[0]
    const auto r = resolve_corner_radii(n.centre, arms, ov);
    CHECK(r[0] == doctest::Approx(10.0));
    CHECK(r[1] == doctest::Approx(kDefaultCornerRadius));
    const auto t = compute_trims(arms, r);
    CHECK(t[0] == doctest::Approx(15.0));
    CHECK(t[1] == doctest::Approx(15.0));
    CHECK(t[2] == doctest::Approx(11.0));
}

TEST_CASE("build_junctions: only 3+ arm nodes appear, with arms, trims and major arms filled") {
    Net n;
    n.polar(0, 100);
    n.polar(180, 100);
    n.polar(-90, 80);
    const JunctionSet set = build_junctions(n.g, *uk(), {});
    REQUIRE(set.junctions.size() == 1);
    const Junction* j = set.find(n.centre);
    REQUIRE(j);
    CHECK(j->cls == NodeClass::Junction);
    CHECK(j->arms.size() == 3);
    CHECK(j->trims.size() == 3);
    CHECK(j->corner_radii.size() == 3);
    CHECK(j->major_arms.size() == 3);
    CHECK(j->side == DrivingSide::Left);
}

// ---- golden numbers the parts must hit (enable when implemented) ----

TEST_CASE("[A golden] T junction of two residential roads: 6 connectors") {
    Net n;
    n.polar(0, 100);
    n.polar(180, 100);
    n.polar(-90, 80);
    const JunctionSet set = build_junctions(n.g, *uk(), {});
    const Junction& j = set.junctions.begin()->second;
    REQUIRE(j.connectors.size() == 6);
    int straight = 0, nearside = 0, offside = 0;
    for (const auto& c : j.connectors) {
        straight += c.kind == TurnKind::Straight;
        nearside += c.kind == TurnKind::Nearside;
        offside += c.kind == TurnKind::Offside;
        REQUIRE(c.path.size() >= 2);
    }
    CHECK(straight == 2);
    CHECK(nearside == 2);
    CHECK(offside == 2);
}

TEST_CASE("[B golden] X junction: 4 corners of radius 6 m, trims 11 m, carriageway is one polygon") {
    Net n;
    for (double d : {0.0, 90.0, 180.0, 270.0}) n.polar(d, 100);
    const JunctionSet set = build_junctions(n.g, *uk(), {});
    const Junction& j = set.junctions.begin()->second;
    CHECK(j.corners.size() == 4);
    CHECK(j.carriageway.size() == 1);
}
