// Part A: lane connectors (docs/junctions.md section 4).
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
    RegionPack r = *p;
    r.driving_side = side;
    return std::make_shared<const RegionPack>(r);
}

struct Net {
    std::shared_ptr<const RegionPack> region;
    RoadGraph g;
    CommandStack<RoadGraph> st{g};
    NodeId centre;

    explicit Net(DrivingSide side = DrivingSide::Left) : region(pack(side)) { g.set_region(region); }

    void polar(double deg, double len, const char* type = "residential", bool one_way = false, bool inbound = false) {
        DrawRoadSpec s;
        s.road_type = type;
        s.one_way = one_way;
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
    Junction build(const JunctionOverrides& ov = {}) {
        const JunctionSet set = build_junctions(g, *region, ov);
        REQUIRE(set.junctions.size() == 1);
        return set.junctions.begin()->second;
    }
};

Net make_t(DrivingSide side = DrivingSide::Left) {
    Net n(side);
    n.polar(0, 100);
    n.polar(180, 100);
    n.polar(-90, 80);
    return n;
}
Net make_x(DrivingSide side = DrivingSide::Left) {
    Net n(side);
    for (double d : {0.0, 90.0, 180.0, 270.0}) n.polar(d, 100);
    return n;
}

int count(const Junction& j, TurnKind k) {
    return static_cast<int>(std::count_if(j.connectors.begin(), j.connectors.end(),
                                          [&](const LaneConnector& c) { return c.kind == k; }));
}

bool shares_to_lane(const LaneConnector& a, const LaneConnector& b) {
    return a.arm_to == b.arm_to && a.lane_to == b.lane_to;
}

}  // namespace

TEST_CASE("A: T junction golden, both driving sides") {
    for (DrivingSide side : {DrivingSide::Left, DrivingSide::Right}) {
        Net n = make_t(side);
        const Junction j = n.build();
        REQUIRE(j.connectors.size() == 6);
        CHECK(count(j, TurnKind::Straight) == 2);
        CHECK(count(j, TurnKind::Nearside) == 2);
        CHECK(count(j, TurnKind::Offside) == 2);
        for (const auto& c : j.connectors) {
            CHECK(c.enabled);
            CHECK_FALSE(c.overridden);
            // endpoints on the trim lines, tangent to the lanes
            const auto& fa = j.arms[static_cast<std::size_t>(c.arm_from)];
            const auto& ta = j.arms[static_cast<std::size_t>(c.arm_to)];
            const ArmPoint p0 = arm_point(fa, j.trims[static_cast<std::size_t>(c.arm_from)],
                                          fa.lanes[static_cast<std::size_t>(c.lane_from)].lateral);
            const ArmPoint p1 = arm_point(ta, j.trims[static_cast<std::size_t>(c.arm_to)],
                                          ta.lanes[static_cast<std::size_t>(c.lane_to)].lateral);
            REQUIRE(c.path.size() >= 2);
            CHECK(std::hypot(c.path.front().x - p0.pos.x, c.path.front().y - p0.pos.y) < 1e-3);
            CHECK(std::hypot(c.path.back().x - p1.pos.x, c.path.back().y - p1.pos.y) < 1e-3);
            const Vec3 &a = c.path[0], &b = c.path[1];
            CHECK(std::fabs(wrap_angle(std::atan2(b.y - a.y, b.x - a.x) - (p0.heading_out + kPi))) < kDeg);
            const Vec3 &y = c.path[c.path.size() - 2], &z = c.path.back();
            CHECK(std::fabs(wrap_angle(std::atan2(z.y - y.y, z.x - y.x) - p1.heading_out)) < kDeg);
        }
    }
}

TEST_CASE("A: T junction conflicts: straights cross side-road turns, side-arm merge") {
    Net n = make_t();
    const Junction j = n.build();
    int side_arm = -1;
    for (int i = 0; i < 3; ++i)
        if (std::fabs(j.arms[static_cast<std::size_t>(i)].heading_out + kPi / 2) < 1e-6) side_arm = i;
    REQUIRE(side_arm >= 0);
    int merges = 0;
    for (std::size_t i = 0; i < j.connectors.size(); ++i) {
        const auto& c = j.connectors[i];
        if (c.kind == TurnKind::Straight) CHECK_FALSE(c.conflicts.empty());
        for (int o : c.conflicts) {
            CHECK(o != static_cast<int>(i));
            const auto& oc = j.connectors[static_cast<std::size_t>(o)];
            CHECK(std::find(oc.conflicts.begin(), oc.conflicts.end(), static_cast<int>(i)) != oc.conflicts.end());
        }
        CHECK(std::is_sorted(c.conflicts.begin(), c.conflicts.end()));
        if (c.arm_to == side_arm)
            for (int o : c.conflicts) merges += shares_to_lane(c, j.connectors[static_cast<std::size_t>(o)]);
    }
    CHECK(merges == 2);  // the two connectors into the side road's outbound lane, each seeing the other
}

TEST_CASE("A: X of residential roads: 12 connectors, none crossing from the same approach") {
    Net n = make_x();
    const Junction j = n.build();
    REQUIRE(j.connectors.size() == 12);
    CHECK(count(j, TurnKind::Straight) == 4);
    CHECK(count(j, TurnKind::Nearside) == 4);
    CHECK(count(j, TurnKind::Offside) == 4);
    for (std::size_t i = 0; i < j.connectors.size(); ++i)
        for (int o : j.connectors[i].conflicts)
            if (j.connectors[i].arm_from == j.connectors[static_cast<std::size_t>(o)].arm_from)
                CHECK(shares_to_lane(j.connectors[i], j.connectors[static_cast<std::size_t>(o)]));
}

TEST_CASE("A: X conflict counts") {
    Net n = make_x();
    const Junction j = n.build();
    REQUIRE(j.connectors.size() == 12);
    // One lane per approach: a straight meets the opposing offside turn, both crossing
    // straights, and the two turns that reach its own exit or cross it. Each connector
    // conflicts with at least 2 others, and nearside turns have the fewest.
    for (const auto& c : j.connectors) {
        CHECK(c.conflicts.size() >= 2);
        if (c.kind == TurnKind::Straight) CHECK(c.conflicts.size() >= 5);
    }
    std::size_t near_max = 0, str_min = 99;
    for (const auto& c : j.connectors) {
        if (c.kind == TurnKind::Nearside) near_max = std::max(near_max, c.conflicts.size());
        if (c.kind == TurnKind::Straight) str_min = std::min(str_min, c.conflicts.size());
    }
    CHECK(near_max < str_min);
    // Symmetric: every connector of the same kind has the same count (4-fold symmetry).
    for (const auto& c : j.connectors)
        for (const auto& d : j.connectors)
            if (c.kind == d.kind) CHECK(c.conflicts.size() == d.conflicts.size());
}

TEST_CASE("A: T with a one-way side arm only uses its lanes in one direction") {
    {  // side road flows toward the junction: it has no outbound lane
        Net n(DrivingSide::Left);
        n.polar(0, 100);
        n.polar(180, 100);
        n.polar(-90, 80, "residential", true, true);
        const Junction j = n.build();
        CHECK(j.connectors.size() == 4);  // 2 from the side arm, 1 straight each way
        for (const auto& c : j.connectors) {
            const auto& fa = j.arms[static_cast<std::size_t>(c.arm_from)];
            const auto& ta = j.arms[static_cast<std::size_t>(c.arm_to)];
            CHECK(fa.lanes[static_cast<std::size_t>(c.lane_from)].inbound);
            CHECK_FALSE(ta.lanes[static_cast<std::size_t>(c.lane_to)].inbound);
        }
    }
    {  // flowing away: no inbound lane
        Net n(DrivingSide::Left);
        n.polar(0, 100);
        n.polar(180, 100);
        n.polar(-90, 80, "residential", true, false);
        const Junction j = n.build();
        CHECK(j.connectors.size() == 4);  // 2 into the side arm, 1 straight each way
        for (const auto& c : j.connectors) CHECK(j.arms[static_cast<std::size_t>(c.arm_from)].inbound_count() > 0);
    }
}

TEST_CASE("A: dual carriageway T keeps lanes in order and never crosses from one approach") {
    Net n;
    // A dual edge is one carriageway (2 lanes, one direction): eastbound in from the west, out to the east.
    n.polar(180, 150, "a_road_dual", false, true);
    n.polar(0, 150, "a_road_dual");
    n.polar(-90, 80);
    const Junction j = n.build();
    // West carriageway: 2 straight (lane to lane) + 1 side-road turn from its offside lane;
    // side road (1 lane) into the 2 outbound lanes of the east carriageway.
    CHECK(j.connectors.size() == 5);
    CHECK(count(j, TurnKind::Straight) == 2);
    for (const auto& c : j.connectors) {
        const auto& fa = j.arms[static_cast<std::size_t>(c.arm_from)];
        if (fa.inbound_count() == 2 && c.kind != TurnKind::Straight) {
            // the turn leaves from the lane on its own side: planar left turn from the planar-left
            // travel lane, i.e. the inbound lane with the lower lateral
            double lo = 1e9, hi = -1e9;
            for (const auto& l : fa.lanes)
                if (l.inbound) lo = std::min(lo, l.lateral), hi = std::max(hi, l.lateral);
            const double lat = fa.lanes[static_cast<std::size_t>(c.lane_from)].lateral;
            CHECK(lat == doctest::Approx(turn_is_left(c.kind, n.region->driving_side) ? lo : hi));
        }
    }
    for (std::size_t i = 0; i < j.connectors.size(); ++i)
        for (int o : j.connectors[i].conflicts) {
            const auto& a = j.connectors[i];
            const auto& b = j.connectors[static_cast<std::size_t>(o)];
            if (a.arm_from == b.arm_from) CHECK(shares_to_lane(a, b));
        }
}

TEST_CASE("A: mirrored driving side mirrors turn kinds") {
    Net l = make_t(DrivingSide::Left), r = make_t(DrivingSide::Right);
    const Junction jl = l.build(), jr = r.build();
    REQUIRE(jl.connectors.size() == jr.connectors.size());
    for (std::size_t i = 0; i < jl.connectors.size(); ++i) {
        const auto& a = jl.connectors[i];
        const auto& b = jr.connectors[i];
        CHECK(a.arm_from == b.arm_from);
        CHECK(a.arm_to == b.arm_to);
        const bool a_left = a.kind != TurnKind::Straight && turn_is_left(a.kind, DrivingSide::Left);
        const bool b_left = b.kind != TurnKind::Straight && turn_is_left(b.kind, DrivingSide::Right);
        CHECK(a_left == b_left);  // the same planar turn...
        if (a.kind != TurnKind::Straight) CHECK(a.kind != b.kind);  // ...is the other kind
    }
}

TEST_CASE("A: overrides: disable, enable u-turn, custom curve, lane switch survive regeneration") {
    Net n = make_t();
    const Junction base = n.build();
    auto key = [&](const LaneConnector& c) {
        const auto& fa = base.arms[static_cast<std::size_t>(c.arm_from)];
        const auto& ta = base.arms[static_cast<std::size_t>(c.arm_to)];
        return ConnectorKey{n.centre, fa.edge, fa.lanes[static_cast<std::size_t>(c.lane_from)].slot, ta.edge,
                            ta.lanes[static_cast<std::size_t>(c.lane_to)].slot};
    };
    JunctionOverrides ov;
    ov.connectors[key(base.connectors[0])].enabled = false;
    {
        const Junction j = n.build(ov);
        REQUIRE(j.connectors.size() == 6);
        int enabled = 0;
        for (const auto& c : j.connectors) enabled += c.enabled;
        CHECK(enabled == 5);
        CHECK_FALSE(j.connectors[0].enabled);
        CHECK(j.connectors[0].overridden);
        CHECK(j.connectors[0].conflicts.empty());
        for (const auto& c : j.connectors)
            for (int o : c.conflicts) CHECK(o != 0);
    }
    // U-turn on arm 0: its inbound lane to its own outbound lane.
    {
        const auto& a = base.arms[0];
        int in = -1, outl = -1;
        for (int i = 0; i < static_cast<int>(a.lanes.size()); ++i)
            (a.lanes[static_cast<std::size_t>(i)].inbound ? in : outl) = i;
        REQUIRE(in >= 0);
        REQUIRE(outl >= 0);
        ConnectorKey k{n.centre, a.edge, a.lanes[static_cast<std::size_t>(in)].slot, a.edge,
                       a.lanes[static_cast<std::size_t>(outl)].slot};
        JunctionOverrides ov2 = ov;
        ov2.connectors[k].enabled = true;
        const Junction j = n.build(ov2);
        REQUIRE(j.connectors.size() == 7);
        const int idx = j.find_connector(0, in, 0, outl);
        REQUIRE(idx >= 0);
        CHECK(j.connectors[static_cast<std::size_t>(idx)].kind == TurnKind::UTurn);
        CHECK(j.connectors[static_cast<std::size_t>(idx)].overridden);
        CHECK(j.connectors[static_cast<std::size_t>(idx)].path.size() >= 2);
    }
    // Custom curve: interior via point is honoured and end points follow the trims.
    {
        const LaneConnector& c0 = base.connectors[1];
        JunctionOverrides ov3;
        ov3.connectors[key(c0)].via = std::vector<Vec3>{{2.0, 7.0, 0.0}};
        const Junction j = n.build(ov3);
        const LaneConnector& c = j.connectors[1];
        CHECK(c.overridden);
        double best = 1e9;
        for (const Vec3& p : c.path) best = std::min(best, std::hypot(p.x - 2.0, p.y - 7.0));
        CHECK(best < 0.5);
        CHECK(c.path.front().x == doctest::Approx(c0.path.front().x));
        CHECK(c.path.back().y == doctest::Approx(c0.path.back().y));
    }
    // lane_connects off: the lane takes no part.
    {
        const auto& a = base.arms[1];
        JunctionOverrides ov4;
        for (const auto& l : a.lanes) ov4.lane_connects[{n.centre, a.edge, l.slot}] = false;
        const Junction j = n.build(ov4);
        for (const auto& c : j.connectors) CHECK(c.arm_from != 1);
        for (const auto& c : j.connectors) CHECK(c.arm_to != 1);
        CHECK(j.connectors.size() == 2);
    }
}
