// Roundabouts: the place tool, recognition of hand-drawn rings, layout and the junction post-pass
// (sim_core/src/junction_roundabout.cpp, docs/junctions.md section 7).
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>
#include <set>
#include <string>

#include "doctest/doctest.h"
#include "cp/junction_roundabout.h"

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

    EdgeId last_edge() const { return g.edges().rbegin()->first; }

    // Two-way roads from `len` m out at each bearing into one centre node.
    void spokes(const std::vector<double>& deg, double len, const char* type = "b_road") {
        for (double d : deg) {
            DrawRoadSpec s;
            s.road_type = type;
            RoadEnd far, c;
            far.pos = {len * std::cos(d * kDeg), len * std::sin(d * kDeg), 0};
            c.pos = {0, 0, 0};
            c.existing_node = centre;
            s.start = far;
            s.end = c;
            REQUIRE(st.execute(make_draw_road(s)));
            if (!centre.valid()) centre = g.edge(last_edge())->b;
        }
    }

    bool place(double radius, const char* type = "b_road") {
        return st.execute(make_place_roundabout({0, 0, 0}, radius, type, *region));
    }

    // A ring drawn by hand: nodes at the given bearings (in circulation order, monotone), one edge
    // between consecutive nodes with two interior points and tangent headings. Nodes flagged in
    // `spoke` get a two-way road out to `R + 60`. `two_way_edge` >= 0 draws that ring edge two-way.
    void hand_ring(double R, const std::vector<double>& node_deg, const std::vector<bool>& spoke,
                   int two_way_edge = -1) {
        const std::size_t n = node_deg.size();
        const bool ccw = node_deg.back() > node_deg.front();
        const auto pt = [&](double d) { return Vec3{R * std::cos(d * kDeg), R * std::sin(d * kDeg), 0}; };
        const auto tangent = [&](double d) { return d * kDeg + (ccw ? kPi / 2 : -kPi / 2); };
        std::vector<NodeId> ids(n);
        for (std::size_t k = 0; k < n; ++k) {
            const double a = node_deg[k];
            const double b = k + 1 < n ? node_deg[k + 1] : node_deg[0] + (ccw ? 360.0 : -360.0);
            DrawRoadSpec s;
            s.road_type = "b_road";
            s.one_way = static_cast<int>(k) != two_way_edge;
            s.start.pos = pt(a);
            s.start.heading = tangent(a);
            s.end.pos = pt(b);
            s.end.heading = tangent(b);
            if (k > 0) s.start.existing_node = ids[k];
            if (k + 1 == n) s.end.existing_node = ids[0];
            s.interior = {pt(a + (b - a) / 3), pt(a + 2 * (b - a) / 3)};
            REQUIRE(st.execute(make_draw_road(s)));
            const Edge* e = g.edge(last_edge());
            if (k == 0) ids[0] = e->a;
            if (k == 0 && n > 1) ids[1] = e->b;
            else if (k + 1 < n) ids[k + 1] = e->b;
        }
        for (std::size_t k = 0; k < n; ++k) {
            if (!spoke[k]) continue;
            DrawRoadSpec s;
            s.road_type = "b_road";
            s.start.pos = {(R + 60) * std::cos(node_deg[k] * kDeg), (R + 60) * std::sin(node_deg[k] * kDeg), 0};
            s.end.existing_node = ids[k];
            REQUIRE(st.execute(make_draw_road(s)));
        }
    }

    JunctionSet junctions() const {
        JunctionSet js = build_junctions(g, *region, {});
        apply_roundabouts(js, g, *region, {});
        return js;
    }
};

std::string snapshot(const RoadGraph& g) {
    std::string s;
    char b[200];
    for (const auto& [id, n] : g.nodes()) {
        std::snprintf(b, sizeof b, "N%llu %.9g %.9g %.9g\n", static_cast<unsigned long long>(id.value), n.pos.x, n.pos.y,
                      n.pos.z);
        s += b;
    }
    for (const auto& [id, e] : g.edges()) {
        std::snprintf(b, sizeof b, "E%llu %llu %llu %s %d %.9g %zu %zu\n", static_cast<unsigned long long>(id.value),
                      static_cast<unsigned long long>(e.a.value), static_cast<unsigned long long>(e.b.value),
                      e.road_type.c_str(), e.one_way ? 1 : 0, e.horizontal.length(), e.interior.size(),
                      e.structure.size());
        s += b;
    }
    return s;
}

double area(const std::vector<Vec2>& r) {
    double a = 0;
    for (std::size_t i = 0; i < r.size(); ++i) a += r[i].cross(r[(i + 1) % r.size()]);
    return a / 2;
}

bool segs_cross(Vec2 a, Vec2 b, Vec2 c, Vec2 d) {
    const auto o = [](Vec2 p, Vec2 q, Vec2 r) { return (q - p).cross(r - p); };
    const double d1 = o(a, b, c), d2 = o(a, b, d), d3 = o(c, d, a), d4 = o(c, d, b);
    return ((d1 > 1e-9 && d2 < -1e-9) || (d1 < -1e-9 && d2 > 1e-9)) &&
           ((d3 > 1e-9 && d4 < -1e-9) || (d3 < -1e-9 && d4 > 1e-9));
}

bool simple(const std::vector<Vec2>& r) {
    if (r.size() < 3 || std::fabs(area(r)) < 1e-3) return false;
    const std::size_t n = r.size();
    for (std::size_t i = 0; i < n; ++i)
        for (std::size_t k = i + 2; k < n; ++k) {
            if (i == 0 && k == n - 1) continue;
            if (segs_cross(r[i], r[(i + 1) % n], r[k], r[(k + 1) % n])) return false;
        }
    return true;
}

// One group out of a net, or fails the test.
JunctionGroup the_group(const Net& net) {
    const auto groups = recognise_roundabouts(net.g, *net.region);
    REQUIRE(groups.size() == 1);
    return groups[0];
}

std::size_t roundabout_junctions(const JunctionSet& js) {
    return static_cast<std::size_t>(std::count_if(js.junctions.begin(), js.junctions.end(), [](const auto& kv) {
        return kv.second.kind == JunctionKind::Roundabout;
    }));
}

struct Counts {
    std::size_t nodes, arms, connectors, splitters, entries, exits;
    bool operator==(const Counts&) const = default;
};

Counts counts(const Net& net) {
    const JunctionGroup grp = the_group(net);
    const RoundaboutLayout lay = build_roundabout(grp, net.g, *net.region, {});
    REQUIRE_MESSAGE(lay.ok, lay.error);
    const JunctionSet js = net.junctions();
    const Junction* j = js.find(grp.nodes[0]);
    REQUIRE(j);
    Counts c{};
    c.nodes = grp.nodes.size();
    c.arms = j->arms.size();
    c.connectors = j->connectors.size();
    c.splitters = lay.splitters.size();
    for (const RoundaboutArm& a : grp.arms) {
        c.entries += a.entry ? 1 : 0;
        c.exits += a.exit ? 1 : 0;
    }
    return c;
}

}  // namespace

TEST_CASE("roundabout: tool-placed 4-arm ring is recognised") {
    for (DrivingSide side : {DrivingSide::Left, DrivingSide::Right}) {
        Net net(side);
        net.spokes({0, 90, 180, 270}, 100);
        REQUIRE(net.place(20));
        const JunctionGroup g = the_group(net);
        CHECK(g.clockwise == (side == DrivingSide::Left));
        CHECK(g.nodes.size() == 4);
        CHECK(g.ring_edges.size() == 4);
        CHECK(g.arms.size() == 4);
        CHECK(g.radius == doctest::Approx(20).epsilon(0.02));
        CHECK(g.centre.length() < 0.2);
        CHECK(g.nodes[0] == *std::min_element(g.nodes.begin(), g.nodes.end()));
        for (const RoundaboutArm& a : g.arms) CHECK((a.entry && a.exit));
        // Ring nodes sit on the circle and every ring edge is one-way along the circulation.
        for (NodeId n : g.nodes) CHECK(net.g.node(n)->pos.xy().length() == doctest::Approx(20).epsilon(1e-6));
        for (std::size_t i = 0; i < g.nodes.size(); ++i) {
            const Edge* e = net.g.edge(g.ring_edges[i]);
            REQUIRE(e);
            CHECK(e->one_way);
            CHECK(e->a == g.nodes[i]);
            CHECK(e->b == g.nodes[(i + 1) % g.nodes.size()]);
            for (double s = 0; s <= e->horizontal.length(); s += 1.0)
                CHECK(std::fabs(edge_point(*e, s).xy().length() - 20) < 0.3);
        }
        // Nothing is left inside the circle.
        for (const auto& [id, n] : net.g.nodes()) CHECK(n.pos.xy().length() > 19.9);
    }
}

TEST_CASE("roundabout: hand-drawn ring gives the same result as the tool (4 and 3 arms)") {
    for (DrivingSide side : {DrivingSide::Left, DrivingSide::Right}) {
        const bool cw = side == DrivingSide::Left;
        const double sgn = cw ? -1 : 1;

        Net tool4(side), hand4(side);
        tool4.spokes({0, 90, 180, 270}, 100);
        REQUIRE(tool4.place(20));
        hand4.hand_ring(20, {0, sgn * 90, sgn * 180, sgn * 270}, {true, true, true, true});
        CHECK(counts(tool4) == counts(hand4));
        const RoundaboutLayout lt = build_roundabout(the_group(tool4), tool4.g, *tool4.region, {});
        const RoundaboutLayout lh = build_roundabout(the_group(hand4), hand4.g, *hand4.region, {});
        REQUIRE(lt.ok);
        REQUIRE(lh.ok);
        CHECK(std::fabs(area(lt.island[0])) == doctest::Approx(std::fabs(area(lh.island[0]))).epsilon(0.03));
        CHECK(lt.group.clockwise == lh.group.clockwise);

        // Three arms: the tool adds joint nodes so no ring edge exceeds ~90 degrees.
        Net tool3(side), hand3(side);
        tool3.spokes({90, 210, 330}, 100);
        REQUIRE(tool3.place(20));
        hand3.hand_ring(20, {90, 90 + sgn * 60, 90 + sgn * 120, 90 + sgn * 180, 90 + sgn * 240, 90 + sgn * 300},
                        {true, false, true, false, true, false});
        const Counts c3 = counts(tool3);
        CHECK(c3.nodes == 6);
        CHECK(c3.arms == 3);
        CHECK(c3.connectors == 6);  // each arm to each of the 2 others, one inbound lane each
        CHECK(c3 == counts(hand3));
    }
}

TEST_CASE("roundabout: both driving sides give mirror-identical counts") {
    Counts left{}, right{};
    for (DrivingSide side : {DrivingSide::Left, DrivingSide::Right}) {
        Net net(side);
        net.spokes({0, 90, 180, 270}, 100);
        REQUIRE(net.place(20));
        (side == DrivingSide::Left ? left : right) = counts(net);
    }
    CHECK(left == right);
    CHECK(left.nodes == 4);
    CHECK(left.connectors == 12);
    CHECK(left.splitters == 4);
    CHECK(left.entries == 4);
    CHECK(left.exits == 4);
}

TEST_CASE("roundabout: 3- and 4-arm junction output") {
    for (const std::vector<double>& bearings : {std::vector<double>{90, 210, 330}, std::vector<double>{0, 90, 180, 270}}) {
        Net net;
        net.spokes(bearings, 100);
        REQUIRE(net.place(20));
        const JunctionSet js = net.junctions();
        const JunctionGroup g = the_group(net);
        CHECK(roundabout_junctions(js) == 1);
        const Junction* j = js.find(g.nodes[0]);
        REQUIRE(j);
        CHECK(j->kind == JunctionKind::Roundabout);
        CHECK(j->group_nodes == g.nodes);
        CHECK(j->arms.size() == bearings.size());
        CHECK(!j->degraded);
        // The ring nodes' own plain junctions are gone.
        for (std::size_t i = 1; i < g.nodes.size(); ++i) CHECK(js.find(g.nodes[i]) == nullptr);
        // Arms run CCW about the ring centre.
        double prev = -1e9;
        for (const JunctionArm& a : j->arms) {
            const Vec2 p = net.g.node(a.at_a ? net.g.edge(a.edge)->a : net.g.edge(a.edge)->b)->pos.xy();
            const double ang = std::atan2(p.y, p.x);
            CHECK(ang >= prev - 1e-9);
            prev = ang;
        }
        // Every arm has a trim and every inbound lane a connector to a lane of another arm.
        CHECK(j->trims.size() == j->arms.size());
        for (std::size_t a = 0; a < j->arms.size(); ++a)
            for (std::size_t l = 0; l < j->arms[a].lanes.size(); ++l) {
                if (!j->arms[a].lanes[l].inbound) continue;
                int n = 0;
                for (const LaneConnector& c : j->connectors)
                    if (c.arm_from == static_cast<int>(a) && c.lane_from == static_cast<int>(l)) {
                        ++n;
                        CHECK(c.arm_to != static_cast<int>(a));
                        CHECK(c.path.size() >= 2);
                    }
                CHECK(n == static_cast<int>(bearings.size()) - 1);
            }
    }
}

TEST_CASE("roundabout: ring has priority, conflicts are on entries only, radii are forced") {
    Net net;
    net.spokes({0, 90, 180, 270}, 100);
    REQUIRE(net.place(20));
    const JunctionGroup g = the_group(net);
    const RoundaboutLayout lay = build_roundabout(g, net.g, *net.region, {});
    REQUIRE_MESSAGE(lay.ok, lay.error);
    int entries = 0, exits = 0, throughs = 0, rings = 0;
    for (const RoundaboutConnector& c : lay.connectors) {
        CHECK(c.path.size() >= 2);
        switch (c.role) {
            case RoundaboutConnectorRole::Entry: {
                ++entries;
                CHECK(c.give_way);
                CHECK(!c.conflicts.empty());
                for (int k : c.conflicts) {
                    const RoundaboutConnectorRole r = lay.connectors[static_cast<std::size_t>(k)].role;
                    CHECK((r == RoundaboutConnectorRole::Through || r == RoundaboutConnectorRole::Ring));
                }
                break;
            }
            case RoundaboutConnectorRole::Exit: ++exits; break;
            case RoundaboutConnectorRole::Through: ++throughs; break;
            case RoundaboutConnectorRole::Ring: ++rings; break;
        }
        if (c.role != RoundaboutConnectorRole::Entry) {
            CHECK(!c.give_way);
            CHECK(c.conflicts.empty());
        }
    }
    CHECK(entries >= 4);  // every spoke feeds the ring
    CHECK(exits >= 4);
    CHECK(throughs >= 4);
    CHECK(rings == 8);  // 4 ring edges x 2 lanes (b_road one-way variant)

    // Forced fillets: entry corners (spoke / ring-out) 8 m, exit corners (ring-in / spoke) 12 m.
    const RoundaboutParams prm = roundabout_params(*net.region);
    int entry_corners = 0, exit_corners = 0;
    for (std::size_t i = 0; i < g.nodes.size(); ++i) {
        const Junction& j = lay.ring_junctions.at(g.nodes[i]);
        const EdgeId in = g.ring_edges[(i + g.nodes.size() - 1) % g.nodes.size()], out = g.ring_edges[i];
        for (std::size_t k = 0; k < j.arms.size(); ++k) {
            const EdgeId r = j.arms[k].edge, l = j.arms[(k + 1) % j.arms.size()].edge;
            const bool r_ring = r == in || r == out, l_ring = l == in || l == out;
            if (r_ring == l_ring) continue;
            const EdgeId ring = r_ring ? r : l;
            CHECK(j.corner_radii[k] == doctest::Approx(ring == out ? prm.entry_corner_radius : prm.exit_corner_radius));
            (ring == out ? entry_corners : exit_corners)++;
        }
    }
    CHECK(entry_corners == 4);
    CHECK(exit_corners == 4);

    // A player corner override beats the forced radius.
    JunctionOverrides ov;
    const Junction& j0 = lay.ring_junctions.at(g.nodes[0]);
    for (std::size_t k = 0; k < j0.arms.size(); ++k) {
        const EdgeId r = j0.arms[k].edge, l = j0.arms[(k + 1) % j0.arms.size()].edge;
        const EdgeId in0 = g.ring_edges.back(), out0 = g.ring_edges[0];
        const bool r_ring = r == in0 || r == out0, l_ring = l == in0 || l == out0;
        if (r_ring != l_ring) ov.corner_radius[CornerKey{g.nodes[0], l, r}] = 3.0;  // both spoke corners
    }
    REQUIRE(!ov.corner_radius.empty());
    const RoundaboutLayout lay2 = build_roundabout(g, net.g, *net.region, ov);
    REQUIRE(lay2.ok);
    const Junction& k0 = lay2.ring_junctions.at(g.nodes[0]);
    int forced = 0;
    for (double r : k0.corner_radii) forced += std::fabs(r - 3.0) < 1e-9 ? 1 : 0;
    CHECK(forced == 2);
}

TEST_CASE("roundabout: polygons are valid, outer CCW, holes CW, island is the hole") {
    for (DrivingSide side : {DrivingSide::Left, DrivingSide::Right}) {
        Net net(side);
        net.spokes({0, 90, 180, 270}, 100);
        REQUIRE(net.place(20));
        const RoundaboutLayout lay = build_roundabout(the_group(net), net.g, *net.region, {});
        REQUIRE_MESSAGE(lay.ok, lay.error);
        REQUIRE(lay.island.size() == 1);
        CHECK(area(lay.island[0]) > 0);
        CHECK(simple(lay.island[0]));
        // Island: radius 20 - 3.25 (inner kerb of the 6.5 m one-way b_road ring).
        CHECK(area(lay.island[0]) == doctest::Approx(kPi * 16.75 * 16.75).epsilon(0.03));
        int outers = 0, holes = 0;
        for (const auto& r : lay.carriageway) {
            CHECK(simple(r));
            if (area(r) > 0) ++outers;
            else {
                ++holes;
                CHECK(std::fabs(area(r)) == doctest::Approx(area(lay.island[0])).epsilon(0.03));
            }
        }
        CHECK(outers >= 1);
        CHECK(holes == 1);
        CHECK(lay.splitters.size() == 4);
        for (const SplitterIsland& s : lay.splitters) {
            CHECK(area(s.polygon) > 0);
            CHECK(simple(s.polygon));
        }
        const JunctionSet js = net.junctions();
        for (const auto& [n, j] : js.junctions)
            for (const PolygonSet* ps : {&j.carriageway, &j.pavements, &j.gore})
                for (const auto& r : *ps) CHECK_MESSAGE(simple(r), "node " << n.value);
    }
}

TEST_CASE("roundabout: loops that are not roundabouts are not recognised") {
    {  // one two-way edge breaks the ring
        Net net;
        net.hand_ring(20, {0, -90, -180, -270}, {true, true, true, true}, 2);
        CHECK(recognise_roundabouts(net.g, *net.region).empty());
    }
    {  // circulating against the driving side
        Net net(DrivingSide::Left);
        net.hand_ring(20, {0, 90, 180, 270}, {true, true, true, true});
        CHECK(recognise_roundabouts(net.g, *net.region).empty());
        Net ok(DrivingSide::Right);
        ok.hand_ring(20, {0, 90, 180, 270}, {true, true, true, true});
        CHECK(recognise_roundabouts(ok.g, *ok.region).size() == 1);
    }
    {  // too few approach roads
        Net net;
        net.hand_ring(20, {0, -90, -180, -270}, {true, false, true, false});
        CHECK(recognise_roundabouts(net.g, *net.region).empty());
    }
    {  // too large to be a roundabout
        Net net;
        net.hand_ring(100, {0, -45, -90, -135, -180, -225, -270, -315}, {true, false, true, false, true, false, true, false});
        CHECK(recognise_roundabouts(net.g, *net.region).empty());
    }
    {  // the same ring with enough roads is
        Net net;
        net.hand_ring(20, {0, -90, -180, -270}, {true, false, true, true});
        CHECK(recognise_roundabouts(net.g, *net.region).size() == 1);
    }
}

TEST_CASE("roundabout: the place command is one undo step and redo reproduces the ids") {
    Net net;
    net.spokes({0, 90, 180, 270}, 100);
    const std::string before = snapshot(net.g);
    const std::size_t depth = net.st.undo_depth();
    REQUIRE(net.place(20));
    const std::string after = snapshot(net.g);
    CHECK(after != before);
    CHECK(net.st.undo_depth() == depth + 1);

    REQUIRE(net.st.undo());
    CHECK(snapshot(net.g) == before);
    CHECK(recognise_roundabouts(net.g, *net.region).empty());
    REQUIRE(net.st.redo());
    CHECK(snapshot(net.g) == after);
    CHECK(recognise_roundabouts(net.g, *net.region).size() == 1);
    REQUIRE(net.st.undo());
    CHECK(snapshot(net.g) == before);
    // New ids are never reused: placing again after undo allocates fresh ones.
    REQUIRE(net.st.execute(make_place_roundabout({0, 0, 0}, 20, "b_road", *net.region)));
    CHECK(snapshot(net.g) != after);
    CHECK(recognise_roundabouts(net.g, *net.region).size() == 1);
}

TEST_CASE("roundabout: the place command fails cleanly") {
    Net net;
    net.spokes({0, 90, 180, 270}, 100);
    const std::string before = snapshot(net.g);
    const std::size_t depth = net.st.undo_depth();
    CHECK(!net.place(20, "no_such_road"));
    CHECK(!net.place(6));    // island would be smaller than the minimum
    CHECK(!net.place(0));
    CHECK(snapshot(net.g) == before);
    CHECK(net.st.undo_depth() == depth);

    Net stub;  // the spokes end 2 m outside the ring: the stubs are too short
    stub.spokes({0, 90, 180, 270}, 22);
    const std::string stub_before = snapshot(stub.g);
    CHECK(!stub.place(20));
    CHECK(snapshot(stub.g) == stub_before);
}

TEST_CASE("roundabout: the tool on empty ground makes a ring that waits for its roads") {
    Net net;
    REQUIRE(net.place(15));
    CHECK(net.g.nodes().size() == 4);
    CHECK(net.g.edges().size() == 4);
    CHECK(recognise_roundabouts(net.g, *net.region).empty());  // no approach roads yet
}

