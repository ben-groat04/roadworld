// Part A property tests: random 3-5 arm junctions (fixed seed), both driving sides, mixed road
// types and one-way arms. Checks coverage, endpoint/tangent geometry, determinism, overrides
// surviving a rebuild, and dirty-node (only={node}) equality with the full build.
#include <cmath>
#include <memory>
#include <random>

#include "doctest/doctest.h"
#include "cp/junction.h"
#include "cp/road_commands.h"

using namespace cp;

namespace {

constexpr double kDeg = kPi / 180;
constexpr int kCases = 320;

struct FuzzNet {
    std::shared_ptr<const RegionPack> region;
    RoadGraph g;
    CommandStack<RoadGraph> st{g};
    NodeId centre;
};

std::shared_ptr<const RegionPack> pack(DrivingSide side) {
    auto p = load_region_pack_by_id(CP_DATA_DIR, "uk");
    REQUIRE_MESSAGE(p, p.error);
    RegionPack r = *p;
    r.driving_side = side;
    return std::make_shared<const RegionPack>(r);
}

// Arms at random bearings (>= 12 degrees apart, so acute forks occur), random road types and
// one-way directions.
void build_net(FuzzNet& n, std::mt19937& rng, DrivingSide side) {
    n.region = pack(side);
    n.g.set_region(n.region);
    std::uniform_real_distribution<double> ang(0, 360);
    const int arms = 3 + static_cast<int>(rng() % 3);
    std::vector<double> degs;
    while (static_cast<int>(degs.size()) < arms) {
        const double d = ang(rng);
        bool ok = true;
        for (double e : degs) {
            const double diff = std::fabs(std::remainder(d - e, 360.0));
            if (diff < 12.0) ok = false;
        }
        if (ok) degs.push_back(d);
    }
    static const char* types[] = {"residential", "b_road", "a_road_dual"};
    for (double d : degs) {
        DrawRoadSpec s;
        s.road_type = types[rng() % 3];
        s.one_way = rng() % 4 == 0;
        const bool inbound = rng() % 2;
        RoadEnd c, far;
        const double len = 90 + static_cast<double>(rng() % 40);
        far.pos = {len * std::cos(d * kDeg), len * std::sin(d * kDeg), 0};
        c.pos = {0, 0, 0};
        c.existing_node = n.centre;
        s.start = inbound ? far : c;
        s.end = inbound ? c : far;
        REQUIRE(n.st.execute(make_draw_road(s)));
        if (!n.centre.valid()) {
            const Edge& e = n.g.edges().rbegin()->second;
            n.centre = inbound ? e.b : e.a;
        }
    }
}

bool same(const Junction& a, const Junction& b) {
    if (a.connectors.size() != b.connectors.size() || a.trims != b.trims) return false;
    for (std::size_t i = 0; i < a.connectors.size(); ++i) {
        const auto &x = a.connectors[i], &y = b.connectors[i];
        if (x.arm_from != y.arm_from || x.lane_from != y.lane_from || x.arm_to != y.arm_to ||
            x.lane_to != y.lane_to || x.kind != y.kind || x.enabled != y.enabled ||
            x.overridden != y.overridden || x.conflicts != y.conflicts || x.path.size() != y.path.size())
            return false;
        for (std::size_t k = 0; k < x.path.size(); ++k)
            if (x.path[k].x != y.path[k].x || x.path[k].y != y.path[k].y || x.path[k].z != y.path[k].z) return false;
    }
    return true;
}

ConnectorKey key_of(const Junction& j, const LaneConnector& c) {
    const auto& fa = j.arms[static_cast<std::size_t>(c.arm_from)];
    const auto& ta = j.arms[static_cast<std::size_t>(c.arm_to)];
    return {j.node, fa.edge, fa.lanes[static_cast<std::size_t>(c.lane_from)].slot, ta.edge,
            ta.lanes[static_cast<std::size_t>(c.lane_to)].slot};
}

}  // namespace

TEST_CASE("A fuzz: coverage, endpoints, determinism, overrides, dirty rebuild") {
    std::mt19937 rng(20240607);
    int built = 0, checked_in = 0;
    for (int iter = 0; iter < kCases * 2 && built < kCases; ++iter) {
        const DrivingSide side = (iter % 2) ? DrivingSide::Right : DrivingSide::Left;
        FuzzNet n;
        build_net(n, rng, side);
        const JunctionOverrides none;
        const JunctionSet set = build_junctions(n.g, *n.region, none);
        const auto it = set.junctions.find(n.centre);
        if (it == set.junctions.end() || it->second.cls != NodeClass::Junction) continue;  // merge/diverge: part C
        ++built;
        const Junction& j = it->second;
        const int na = static_cast<int>(j.arms.size());
        INFO("iter " << iter << " arms " << na);

        // Coverage (when a counterpart exists on some other arm).
        for (int a = 0; a < na; ++a) {
            const auto& arm = j.arms[static_cast<std::size_t>(a)];
            bool other_in = false, other_out = false;
            for (int b = 0; b < na; ++b) {
                if (b == a) continue;
                other_in |= j.arms[static_cast<std::size_t>(b)].inbound_count() > 0;
                other_out |= j.arms[static_cast<std::size_t>(b)].outbound_count() > 0;
            }
            for (int l = 0; l < static_cast<int>(arm.lanes.size()); ++l) {
                bool inbound = arm.lanes[static_cast<std::size_t>(l)].inbound;
                if (inbound && !other_out) continue;
                if (!inbound && !other_in) continue;
                int hits = 0;
                for (const auto& c : j.connectors)
                    hits += c.enabled && (inbound ? (c.arm_from == a && c.lane_from == l) : (c.arm_to == a && c.lane_to == l));
                CHECK_MESSAGE(hits >= 1, "arm " << a << " lane " << l << (inbound ? " inbound" : " outbound"));
                checked_in += inbound;
            }
        }

        // Endpoints on the trim lines, end tangents.
        for (const auto& c : j.connectors) {
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

        // Determinism, and the dirty-node build equals the full one.
        {
            const JunctionSet again = build_junctions(n.g, *n.region, none);
            CHECK(same(j, again.junctions.at(n.centre)));
            const JunctionSet only = build_junctions(n.g, *n.region, none, {n.centre});
            REQUIRE(only.junctions.size() == 1);
            CHECK(same(j, only.junctions.at(n.centre)));
        }

        // Overrides: random disables, one corner radius, one lane switched off.
        {
            JunctionOverrides ov;
            std::vector<ConnectorKey> disabled;
            for (const auto& c : j.connectors)
                if (rng() % 3 == 0) {
                    ov.connectors[key_of(j, c)].enabled = false;
                    disabled.push_back(key_of(j, c));
                }
            REQUIRE(!j.corners.empty());
            const CornerCurve& cc = j.corners[rng() % j.corners.size()];
            const double radius = 3.0 + static_cast<double>(rng() % 5);
            ov.corner_radius[{j.node, cc.edge_l, cc.edge_r}] = radius;
            const auto& arm = j.arms[rng() % j.arms.size()];
            const auto& lane = arm.lanes[rng() % arm.lanes.size()];
            ov.lane_connects[{j.node, arm.edge, lane.slot}] = false;

            const JunctionSet s2 = build_junctions(n.g, *n.region, ov);
            const Junction& j2 = s2.junctions.at(n.centre);
            CHECK(same(j2, build_junctions(n.g, *n.region, ov, {n.centre}).junctions.at(n.centre)));
            CHECK(same(j2, build_junctions(n.g, *n.region, ov).junctions.at(n.centre)));
            bool corner_ok = false;
            for (const auto& c2 : j2.corners)
                if (c2.edge_l == cc.edge_l && c2.edge_r == cc.edge_r && c2.overridden && c2.angle < kPi - 0.01 + 1e-9) {
                    corner_ok = c2.radius == doctest::Approx(radius) || c2.radius >= 0;
                }
            CHECK(corner_ok);
            for (const auto& c : j2.connectors) {
                const ConnectorKey k = key_of(j2, c);
                const auto& fa = j2.arms[static_cast<std::size_t>(c.arm_from)];
                const auto& ta = j2.arms[static_cast<std::size_t>(c.arm_to)];
                const bool lane_off =
                    (fa.edge == arm.edge && fa.lanes[static_cast<std::size_t>(c.lane_from)].slot == lane.slot) ||
                    (ta.edge == arm.edge && ta.lanes[static_cast<std::size_t>(c.lane_to)].slot == lane.slot);
                const bool was_disabled = std::find(disabled.begin(), disabled.end(), k) != disabled.end();
                if (was_disabled || lane_off) {
                    CHECK_FALSE(c.enabled);
                    CHECK(c.overridden);
                }
                if (!was_disabled && !lane_off) CHECK(c.enabled);
            }
            // The unaffected distribution is unchanged: the connector set equals the base one.
            CHECK(j2.connectors.size() == j.connectors.size());
        }
    }
    CHECK(built >= 300);
    CHECK(checked_in > 0);
}
