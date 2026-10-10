// Part C: merges and diverges (sim_core/src/junction_merge.cpp).
#include <algorithm>
#include <cmath>
#include <memory>

#include "doctest/doctest.h"
#include "cp/junction.h"
#include "cp/road_commands.h"

using namespace cp;

namespace {

constexpr double kDeg = kPi / 180;

std::shared_ptr<const RegionPack> pack(DrivingSide side) {
    auto p = load_region_pack_by_id(CP_DATA_DIR, "uk");
    REQUIRE_MESSAGE(p, p.error);
    p.value->driving_side = side;
    return std::make_shared<const RegionPack>(*p);
}

struct Net {
    std::shared_ptr<const RegionPack> region;
    RoadGraph g;
    CommandStack<RoadGraph> st{g};
    NodeId centre;

    explicit Net(DrivingSide side = DrivingSide::Left) : region(pack(side)) { g.set_region(region); }

    void road(double deg, double len, const char* type, bool inbound) {
        DrawRoadSpec s;
        s.road_type = type;
        s.one_way = true;
        RoadEnd c, far;
        far.pos = {len * std::cos(deg * kDeg), len * std::sin(deg * kDeg), 0};
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
    // Merge: main (2 lanes) from 180 deg and slip (1 lane) from 180 -/+ sep, out at 0 deg.
    // The slip is on the nearside: planar left (y > 0, 180 - sep) when driving left.
    void build(bool merge, double sep, double len = 100) {
        const double slip = region->driving_side == DrivingSide::Left ? 180 - sep : 180 + sep;
        road(180, len, "b_road", merge);
        road(slip, len, "residential", merge);
        road(0, len, "b_road", !merge);
    }
    const Junction& junction() {
        static JunctionSet set;
        set = build_junctions(g, *region, {});
        const Junction* j = set.find(centre);
        REQUIRE(j);
        return *j;
    }
};

bool inside(const std::vector<Vec2>& r, Vec2 p) {
    bool in = false;
    for (std::size_t i = 0, k = r.size() - 1; i < r.size(); k = i++)
        if ((r[i].y > p.y) != (r[k].y > p.y) && p.x < (r[k].x - r[i].x) * (p.y - r[i].y) / (r[k].y - r[i].y) + r[i].x)
            in = !in;
    return in;
}

double area(const std::vector<Vec2>& r) {
    double a = 0;
    for (std::size_t i = 0; i < r.size(); ++i) a += r[i].cross(r[(i + 1) % r.size()]);
    return a / 2;
}

bool simple(const std::vector<Vec2>& r) {
    const std::size_t n = r.size();
    auto cross = [](Vec2 a, Vec2 b, Vec2 c, Vec2 d) {
        const double d1 = (b - a).cross(c - a), d2 = (b - a).cross(d - a), d3 = (d - c).cross(a - c),
                     d4 = (d - c).cross(b - c);
        return d1 * d2 < 0 && d3 * d4 < 0;
    };
    for (std::size_t i = 0; i < n; ++i)
        for (std::size_t k = i + 2; k < n; ++k) {
            if (i == 0 && k == n - 1) continue;
            if (cross(r[i], r[(i + 1) % n], r[k], r[(k + 1) % n])) return false;
        }
    return true;
}

// Index of the arm that is the odd one (the only arm with the other direction of travel).
int odd_arm(const Junction& j) {
    for (std::size_t i = 0; i < j.arms.size(); ++i)
        if (j.cls == NodeClass::Merge ? j.arms[i].outbound_count() > 0 : j.arms[i].inbound_count() > 0)
            return static_cast<int>(i);
    return -1;
}

// Checks the shared expectations and returns the nearside lane lateral of O.
void check_slip(const Junction& j, DrivingSide side, bool merge) {
    const int oi = odd_arm(j);
    REQUIRE(oi >= 0);
    const JunctionArm& O = j.arms[static_cast<std::size_t>(oi)];
    REQUIRE(O.lanes.size() == 2);
    // Nearside lane of O in its own frame: planar left (largest lateral) when driving left.
    const int near_lane = side == DrivingSide::Left ? 0 : 1;  // lanes are ordered left to right
    REQUIRE(j.connectors.size() == 3);
    int slip = 0, straight_main = 0;
    for (const LaneConnector& c : j.connectors) {
        const int pair = merge ? c.arm_from : c.arm_to;
        const int plane = merge ? c.lane_from : c.lane_to;
        const int olane = merge ? c.lane_to : c.lane_from;
        const JunctionArm& P = j.arms[static_cast<std::size_t>(pair)];
        CHECK(c.kind == TurnKind::Straight);
        CHECK(c.enabled);
        if (P.lanes.size() == 1) {  // the slip lane
            ++slip;
            CHECK(olane == near_lane);
        } else {
            ++straight_main;
            // collinear main lanes keep their lateral position (frames are opposed)
            CHECK(P.lanes[static_cast<std::size_t>(plane)].lateral ==
                  doctest::Approx(-O.lanes[static_cast<std::size_t>(olane)].lateral).epsilon(1e-6));
        }
        // path starts / ends on the trim lines
        const JunctionArm& fa = j.arms[static_cast<std::size_t>(c.arm_from)];
        const JunctionArm& ta = j.arms[static_cast<std::size_t>(c.arm_to)];
        const Vec3 a = arm_point(fa, j.trims[static_cast<std::size_t>(c.arm_from)],
                                 fa.lanes[static_cast<std::size_t>(c.lane_from)].lateral).pos;
        const Vec3 b = arm_point(ta, j.trims[static_cast<std::size_t>(c.arm_to)],
                                 ta.lanes[static_cast<std::size_t>(c.lane_to)].lateral).pos;
        REQUIRE(c.path.size() >= 2);
        CHECK((c.path.front().xy() - a.xy()).length() < 1e-3);
        CHECK((c.path.back().xy() - b.xy()).length() < 1e-3);
    }
    CHECK(slip == 1);
    CHECK(straight_main == 2);
    // The slip and the main nearside lane end in the same lane: they conflict, the offside lane is free.
    int conflicted = 0;
    for (const LaneConnector& c : j.connectors) conflicted += !c.conflicts.empty();
    CHECK(conflicted == (merge ? 2 : 0));
}

}  // namespace

TEST_CASE("merge_taper_length by design speed") {
    CHECK(merge_taper_length(50 / 3.6) == doctest::Approx(75));
    CHECK(merge_taper_length(70 / 3.6) == doctest::Approx(75));
    CHECK(merge_taper_length(100 / 3.6) == doctest::Approx(110));
    CHECK(merge_taper_length(120 / 3.6) == doctest::Approx(150));
}

TEST_CASE("merge: slip joins the nearside lane of a 2-lane one-way carriageway") {
    Net n;
    n.build(true, 25);
    const Junction& j = n.junction();
    REQUIRE(j.cls == NodeClass::Merge);
    check_slip(j, DrivingSide::Left, true);
    const int oi = odd_arm(j);
    // taper (75 m for the 70 km/h main road) clamped to the 40 m stored arm
    CHECK(j.trims[static_cast<std::size_t>(oi)] == doctest::Approx(40));
    for (double t : j.trims) CHECK(t > 0);
}

TEST_CASE("merge: trim of the odd arm follows the taper, clamped to half the edge") {
    Net n;
    n.build(true, 25, 60);  // edges 60 m: half = 30 m < taper
    const Junction& j = n.junction();
    CHECK(j.trims[static_cast<std::size_t>(odd_arm(j))] == doctest::Approx(30));
}

TEST_CASE("diverge: nearside lane of the carriageway leaves onto the slip") {
    Net n;
    n.build(false, 25);
    const Junction& j = n.junction();
    REQUIRE(j.cls == NodeClass::Diverge);
    check_slip(j, DrivingSide::Left, false);
}

TEST_CASE("mirrored side (driving right): same counts, nearside is planar right") {
    for (bool merge : {true, false}) {
        Net n(DrivingSide::Right);
        n.build(merge, 25);
        const Junction& j = n.junction();
        REQUIRE(j.cls == (merge ? NodeClass::Merge : NodeClass::Diverge));
        check_slip(j, DrivingSide::Right, merge);
    }
}

TEST_CASE("merge/diverge polygons are valid and the gore is off the asphalt") {
    for (bool merge : {true, false}) {
        Net n;
        n.build(merge, 25);
        const Junction& j = n.junction();
        REQUIRE_FALSE(j.carriageway.empty());
        for (const auto& r : j.carriageway) {
            CHECK(area(r) > 1.0);
            CHECK(simple(r));
        }
        REQUIRE(j.gore.size() == 1);
        const auto& g = j.gore[0];
        CHECK(area(g) > 1.0);
        CHECK(simple(g));
        const Vec2 c{(g[0].x + g[1].x + g[2].x) / 3, (g[0].y + g[1].y + g[2].y) / 3};
        for (const auto& r : j.carriageway) CHECK_FALSE(inside(r, c));
        for (const auto& r : j.pavements) {
            CHECK(area(r) > 0);
            CHECK(simple(r));
        }
        CHECK(j.corners.size() == 3);
    }
}

TEST_CASE("a slip at too steep an angle is a normal junction") {
    Net n;
    n.build(true, 60);
    CHECK(classify_node(n.g, n.centre, *n.region) == NodeClass::Junction);
    const Junction& j = n.junction();
    CHECK(j.cls == NodeClass::Junction);
    CHECK(j.gore.empty());
}
