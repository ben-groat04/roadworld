// Roundabouts: groups by explicit road type, the place preset, per-node junctions, give-way,
// overrides, islands (sim_core/src/junction_roundabout.cpp, docs/junctions.md section 7).
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <map>
#include <memory>
#include <set>
#include <string>

#include "doctest/doctest.h"
#include "cp/junction_roundabout.h"

using namespace cp;

namespace {

constexpr double kDeg = kPi / 180;
const char* const kRing = "roundabout_ring";

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

    // Two-way roads from `len` m out at each bearing into one centre node. `far_z` raises the far ends.
    void spokes(const std::vector<double>& deg, double len, const char* type = "b_road", double far_z = 0) {
        for (double d : deg) {
            DrawRoadSpec s;
            s.road_type = type;
            RoadEnd far, c;
            far.pos = {len * std::cos(d * kDeg), len * std::sin(d * kDeg), far_z};
            c.pos = {0, 0, 0};
            c.existing_node = centre;
            s.start = far;
            s.end = c;
            REQUIRE(st.execute(make_draw_road(s)));
            if (!centre.valid()) centre = g.edge(last_edge())->b;
        }
    }

    bool place(double radius, const char* type = kRing, Vec3 at = {0, 0, 0}) {
        return st.execute(make_place_roundabout(at, radius, type, *region));
    }

    // A ring drawn by hand: n nodes at start_deg + k * 360 / n (counter-clockwise or clockwise), one edge
    // between consecutive nodes with two interior points and tangent headings. `types[k]` is the road type
    // of edge k (default: the roundabout ring type), `first` an existing node at the first position.
    std::vector<NodeId> ring(Vec2 c, double R, double start_deg, int n, bool ccw, std::vector<std::string> types = {},
                             NodeId first = {}) {
        if (types.empty()) types.assign(static_cast<std::size_t>(n), kRing);
        const double step = (ccw ? 360.0 : -360.0) / n;
        const auto pt = [&](double d) { return Vec3{c.x + R * std::cos(d * kDeg), c.y + R * std::sin(d * kDeg), 0}; };
        const auto tangent = [&](double d) { return d * kDeg + (ccw ? kPi / 2 : -kPi / 2); };
        std::vector<NodeId> ids(static_cast<std::size_t>(n));
        for (int k = 0; k < n; ++k) {
            const double a = start_deg + k * step, b = a + step;
            DrawRoadSpec s;
            s.road_type = types[static_cast<std::size_t>(k)];
            s.one_way = true;
            s.start.pos = pt(a);
            s.start.heading = tangent(a);
            s.end.pos = pt(b);
            s.end.heading = tangent(b);
            if (k > 0) s.start.existing_node = ids[static_cast<std::size_t>(k)];
            else if (first.valid()) s.start.existing_node = first;
            if (k + 1 == n) s.end.existing_node = ids[0];
            s.interior = {pt(a + (b - a) / 3), pt(a + 2 * (b - a) / 3)};
            REQUIRE(st.execute(make_draw_road(s)));
            const Edge* e = g.edge(last_edge());
            if (k == 0) {
                ids[0] = e->a;
                if (n > 1) ids[1] = e->b;
            } else if (k + 1 < n) {
                ids[static_cast<std::size_t>(k) + 1] = e->b;
            }
        }
        return ids;
    }

    // A hand ring at the origin with a two-way b_road out of each flagged ring node.
    std::vector<NodeId> hand_ring(double R, int n, const std::vector<bool>& spoke, bool ccw, std::vector<std::string> types = {}) {
        const std::vector<NodeId> ids = ring({0, 0}, R, 0, n, ccw, std::move(types));
        const double step = (ccw ? 360.0 : -360.0) / n;
        for (int k = 0; k < n; ++k) {
            if (!spoke[static_cast<std::size_t>(k)]) continue;
            const double d = k * step * kDeg;
            DrawRoadSpec s;
            s.road_type = "b_road";
            s.start.pos = {(R + 60) * std::cos(d), (R + 60) * std::sin(d), 0};
            s.end.existing_node = ids[static_cast<std::size_t>(k)];
            REQUIRE(st.execute(make_draw_road(s)));
        }
        return ids;
    }

    JunctionSet junctions(RoundaboutResult* rr = nullptr) const {
        JunctionSet js = build_plain_junctions(g, *region, {});
        RoundaboutResult r = apply_roundabouts(js, g, *region, {});
        if (rr) *rr = std::move(r);
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

RoundaboutScan scan(const Net& net) { return find_roundabouts(net.g, *net.region); }

JunctionGroup the_group(const Net& net) {
    const RoundaboutScan s = scan(net);
    REQUIRE(s.problems.empty());
    REQUIRE(s.groups.size() == 1);
    return s.groups[0];
}

struct Counts {
    std::size_t nodes, arms, junction_nodes, connectors, splitters, entries, exits, give_way;
    bool operator==(const Counts&) const = default;
};

Counts counts(const Net& net) {
    const JunctionGroup grp = the_group(net);
    RoundaboutResult rr;
    const JunctionSet js = net.junctions(&rr);
    REQUIRE(rr.layouts.size() == 1);
    const RoundaboutLayout& lay = rr.layouts[0];
    REQUIRE_MESSAGE(lay.ok, lay.error);
    Counts c{};
    c.nodes = grp.nodes.size();
    c.arms = grp.arms.size();
    for (NodeId n : grp.nodes) {
        const Junction* j = js.find(n);
        REQUIRE(j);
        c.junction_nodes += j->cls == NodeClass::Junction ? 1 : 0;
        c.connectors += j->connectors.size();
    }
    c.splitters = lay.splitters.size();
    for (const RoundaboutArm& a : grp.arms) {
        c.entries += a.entry ? 1 : 0;
        c.exits += a.exit ? 1 : 0;
    }
    c.give_way = lay.give_way.size();
    return c;
}

// Max heading change per metre along a polyline.
double max_kink(const std::vector<Vec3>& p) {
    double worst = 0;
    for (std::size_t i = 0; i + 2 < p.size(); ++i) {
        const double ax = p[i + 1].x - p[i].x, ay = p[i + 1].y - p[i].y;
        const double bx = p[i + 2].x - p[i + 1].x, by = p[i + 2].y - p[i + 1].y;
        const double la = std::hypot(ax, ay), lb = std::hypot(bx, by);
        if (la < 1e-6 || lb < 1e-6) continue;
        const double turn = std::fabs(wrap_angle(std::atan2(by, bx) - std::atan2(ay, ax)));
        worst = std::max(worst, turn / ((la + lb) / 2));
    }
    return worst;
}

const NodeId* first_junction_node(const JunctionGroup& g, const JunctionSet& js) {
    for (const NodeId& n : g.nodes)
        if (js.find(n) && js.find(n)->cls == NodeClass::Junction) return &n;
    return nullptr;
}

}  // namespace

TEST_CASE("roundabout: only the explicit road type makes a roundabout") {
    {  // a one-way loop of ordinary streets, even a perfectly round one with roads attached
        Net net;
        net.hand_ring(20, 4, {true, true, true, true}, false, std::vector<std::string>(4, "b_road"));
        const RoundaboutScan s = scan(net);
        CHECK(s.groups.empty());
        CHECK(s.problems.empty());
        RoundaboutResult rr;
        const JunctionSet js = net.junctions(&rr);
        for (const auto& [n, j] : js.junctions) CHECK(j.kind == JunctionKind::Plain);
        CHECK(rr.layouts.empty());
    }
    {  // the same loop in the ring type is one
        Net net;
        net.hand_ring(20, 4, {true, true, true, true}, false);
        CHECK(scan(net).groups.size() == 1);
    }
    {  // a one-way block of ordinary streets
        Net net;
        net.ring({0, 0}, 40, 45, 4, false, std::vector<std::string>(4, "residential"));
        CHECK(scan(net).groups.empty());
        CHECK(scan(net).problems.empty());
    }
    {  // a grid of dual carriageways
        Net net;
        std::map<std::pair<int, int>, NodeId> ids;
        const auto link = [&](int i, int j, int i2, int j2) {
            DrawRoadSpec s;
            s.road_type = "a_road_dual";
            s.start.pos = {i * 120.0, j * 120.0, 0};
            s.end.pos = {i2 * 120.0, j2 * 120.0, 0};
            if (ids.count({i, j})) s.start.existing_node = ids[{i, j}];
            if (ids.count({i2, j2})) s.end.existing_node = ids[{i2, j2}];
            REQUIRE(net.st.execute(make_draw_road(s)));
            const Edge* e = net.g.edge(net.last_edge());
            ids[{i, j}] = e->a;
            ids[{i2, j2}] = e->b;
        };
        for (int i = 0; i < 4; ++i)
            for (int j = 0; j < 3; ++j) {
                link(i, j, i, j + 1);
                link(j, i, j + 1, i);
            }
        CHECK(scan(net).groups.empty());
        CHECK(scan(net).problems.empty());
    }
}

TEST_CASE("roundabout: malformed flagged loops are reported, not guessed") {
    {  // flagged and unflagged edges mixed: the flagged part is an open chain
        Net net;
        net.hand_ring(20, 4, {true, true, true, true}, false, {kRing, kRing, kRing, "b_road"});
        const RoundaboutScan s = scan(net);
        CHECK(s.groups.empty());
        REQUIRE(s.problems.size() == 1);
        CHECK(s.problems[0].what.find("not closed") != std::string::npos);
        CHECK(s.problems[0].edges.size() == 3);
        // The junctions at the chain's nodes are flagged.
        JunctionSet js = build_junctions(net.g, *net.region, {});
        const RoundaboutResult rr = apply_roundabouts(js, net.g, *net.region, {});
        CHECK(rr.layouts.empty());
        CHECK(rr.problems.size() == 1);
        int degraded = 0;
        for (NodeId n : s.problems[0].nodes)
            if (js.find(n) && js.find(n)->degraded) ++degraded;
        CHECK(degraded >= 2);
    }
    {  // circulating against the driving side
        Net net(DrivingSide::Left);
        net.hand_ring(20, 4, {true, true, true, true}, true);
        const RoundaboutScan s = scan(net);
        CHECK(s.groups.empty());
        REQUIRE(s.problems.size() == 1);
        CHECK(s.problems[0].what.find("against the driving side") != std::string::npos);
        Net ok(DrivingSide::Right);
        ok.hand_ring(20, 4, {true, true, true, true}, true);
        CHECK(scan(ok).groups.size() == 1);
    }
    {  // figure of eight: two rings sharing a node
        Net net;
        const std::vector<NodeId> a = net.ring({-20, 0}, 20, 0, 4, false);
        net.ring({20, 0}, 20, 180, 4, false, {}, a[0]);
        const RoundaboutScan s = scan(net);
        CHECK(s.groups.empty());
        REQUIRE(s.problems.size() == 1);
        CHECK(s.problems[0].what.find("exactly one") != std::string::npos);
    }
    {  // a two-way edge in the ring type
        Net net;
        net.hand_ring(20, 4, {true, true, true, true}, false);
        DrawRoadSpec s;
        s.road_type = kRing;
        s.one_way = false;
        s.start.pos = {100, 100, 0};
        s.end.pos = {160, 100, 0};
        // A ring-type road drawn two-way is not a ring edge: reported, and the real ring is unaffected.
        REQUIRE(net.st.execute(make_draw_road(s)));
        const RoundaboutScan sc = scan(net);
        CHECK(sc.groups.size() == 1);
        REQUIRE(sc.problems.size() == 1);
        CHECK(sc.problems[0].what.find("two-way") != std::string::npos);
    }
    {  // two separate valid rings
        Net net;
        net.ring({0, 0}, 20, 0, 4, false);
        net.ring({300, 0}, 20, 0, 4, false);
        const RoundaboutScan s = scan(net);
        CHECK(s.problems.empty());
        CHECK(s.groups.size() == 2);
    }
}

TEST_CASE("roundabout: group finding is cheap on a big ordinary grid and supports dirty rebuilds") {
    Net net;
    std::map<std::pair<int, int>, NodeId> ids;
    const auto link = [&](int i, int j, int i2, int j2) {
        DrawRoadSpec s;
        s.road_type = "residential";
        s.one_way = true;
        s.start.pos = {i * 40.0, j * 40.0, 0};
        s.end.pos = {i2 * 40.0, j2 * 40.0, 0};
        if (ids.count({i, j})) s.start.existing_node = ids[{i, j}];
        if (ids.count({i2, j2})) s.end.existing_node = ids[{i2, j2}];
        REQUIRE(net.st.execute(make_draw_road(s)));
        const Edge* e = net.g.edge(net.last_edge());
        ids[{i, j}] = e->a;
        ids[{i2, j2}] = e->b;
    };
    for (int i = 0; i < 10; ++i)
        for (int j = 0; j < 9; ++j) {
            // alternate directions so the blocks circulate
            if (i % 2 == 0) link(i, j, i, j + 1);
            else link(i, j + 1, i, j);
            if (i % 2 == 0) link(j, i, j + 1, i);
            else link(j + 1, i, j, i);
        }
    // One real ring far away.
    net.ring({2000, 2000}, 20, 0, 4, false);

    const auto t0 = std::chrono::steady_clock::now();
    const RoundaboutScan s = scan(net);
    const double ms_full = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    CHECK(s.groups.size() == 1);
    CHECK(s.problems.empty());
    CHECK(ms_full < 250.0);

    // Dirty scan: a node of the grid touches no ring; a ring node finds exactly its ring.
    CHECK(find_roundabouts(net.g, *net.region, {ids[{3, 3}]}).groups.empty());
    CHECK(find_roundabouts(net.g, *net.region, {s.groups[0].nodes[2]}).groups.size() == 1);
    const auto t1 = std::chrono::steady_clock::now();
    for (int i = 0; i < 100; ++i) find_roundabouts(net.g, *net.region, {ids[{3, 3}]});
    const double ms_dirty = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t1).count();
    CHECK(ms_dirty < 250.0);
}

TEST_CASE("roundabout: tool-placed 4-arm ring has one group and a junction on every ring node") {
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
        for (std::size_t i = 0; i < g.nodes.size(); ++i) {
            CHECK(net.g.node(g.nodes[i])->pos.xy().length() == doctest::Approx(20).epsilon(1e-6));
            const Edge* e = net.g.edge(g.ring_edges[i]);
            REQUIRE(e);
            CHECK(e->one_way);
            CHECK(e->road_type == kRing);
            CHECK(e->a == g.nodes[i]);
            CHECK(e->b == g.nodes[(i + 1) % g.nodes.size()]);
            for (double s = 0; s <= e->horizontal.length(); s += 1.0)
                CHECK(std::fabs(edge_point(*e, s).xy().length() - 20) < 0.3);
        }
        for (const auto& [id, n] : net.g.nodes()) CHECK(n.pos.xy().length() > 19.9);

        RoundaboutResult rr;
        const JunctionSet js = net.junctions(&rr);
        CHECK(rr.problems.empty());
        REQUIRE(rr.layouts.size() == 1);
        for (NodeId n : g.nodes) {
            const Junction* j = js.find(n);
            REQUIRE(j);
            CHECK(j->kind == JunctionKind::Roundabout);
            CHECK(j->group_nodes == g.nodes);
            CHECK(j->cls == NodeClass::Junction);
            CHECK(!j->degraded);
            CHECK(j->arms.size() == 3);  // ring in, ring out, one approach
        }
    }
}

TEST_CASE("roundabout: 3-arm ring has joint nodes, which still get a junction") {
    for (DrivingSide side : {DrivingSide::Left, DrivingSide::Right}) {
        Net net(side);
        net.spokes({90, 210, 330}, 100);
        REQUIRE(net.place(20));
        const JunctionGroup g = the_group(net);
        CHECK(g.nodes.size() == 6);
        CHECK(g.arms.size() == 3);
        const JunctionSet js = net.junctions();
        int joints = 0, junctions = 0;
        for (NodeId n : g.nodes) {
            const Junction* j = js.find(n);
            REQUIRE(j);
            CHECK(j->kind == JunctionKind::Roundabout);
            CHECK(j->group_nodes == g.nodes);
            if (j->cls == NodeClass::Joint) {
                ++joints;
                CHECK(j->arms.size() == 2);
                CHECK(!j->connectors.empty());  // the lane continuations
            } else {
                ++junctions;
            }
        }
        CHECK(joints == 3);
        CHECK(junctions == 3);
    }
}

TEST_CASE("roundabout: hand-drawn ring equals the tool's (4 and 3 arms, both sides) and both sides mirror") {
    Counts mirror[2] = {};
    int side_i = 0;
    for (DrivingSide side : {DrivingSide::Left, DrivingSide::Right}) {
        const bool ccw = side == DrivingSide::Right;
        Net tool4(side), hand4(side);
        tool4.spokes({0, 90, 180, 270}, 100);
        REQUIRE(tool4.place(20));
        hand4.hand_ring(20, 4, {true, true, true, true}, ccw);
        CHECK(counts(tool4) == counts(hand4));
        mirror[side_i++] = counts(tool4);

        Net tool3(side), hand3(side);
        tool3.spokes({90, 210, 330}, 100);
        REQUIRE(tool3.place(20));
        hand3.hand_ring(20, 6, {true, false, true, false, true, false}, ccw);
        const Counts c3 = counts(tool3);
        CHECK(c3.nodes == 6);
        CHECK(c3.arms == 3);
        CHECK(c3.junction_nodes == 3);
        CHECK(c3 == counts(hand3));
    }
    CHECK(mirror[0] == mirror[1]);
    CHECK(mirror[0].splitters == 4);
    CHECK(mirror[0].entries == 4);
    CHECK(mirror[0].exits == 4);
}

TEST_CASE("roundabout: empty ground gives a valid ring with an island but no arms") {
    Net net;
    REQUIRE(net.place(15));
    CHECK(net.g.nodes().size() == 4);
    CHECK(net.g.edges().size() == 4);
    RoundaboutResult rr;
    const JunctionSet js = net.junctions(&rr);
    REQUIRE(rr.layouts.size() == 1);
    CHECK(rr.layouts[0].ok);
    CHECK(rr.layouts[0].group.arms.empty());
    CHECK(rr.layouts[0].island.size() == 1);
    CHECK(rr.layouts[0].give_way.empty());
    for (NodeId n : rr.layouts[0].group.nodes) {
        REQUIRE(js.find(n));
        CHECK(js.find(n)->kind == JunctionKind::Roundabout);
        CHECK(js.find(n)->cls == NodeClass::Joint);
    }
}

TEST_CASE("roundabout: entries give way to the ring and every full-circle manoeuvre is available") {
    for (DrivingSide side : {DrivingSide::Left, DrivingSide::Right}) {
        Net net(side);
        net.spokes({0, 90, 180, 270}, 100);
        REQUIRE(net.place(20));
        const JunctionGroup g = the_group(net);
        RoundaboutResult rr;
        const JunctionSet js = net.junctions(&rr);
        REQUIRE(rr.layouts.size() == 1);
        const RoundaboutLayout& lay = rr.layouts[0];
        REQUIRE_MESSAGE(lay.ok, lay.error);
        const std::set<EdgeId> ring(g.ring_edges.begin(), g.ring_edges.end());
        CHECK(!lay.give_way.empty());
        std::set<std::pair<NodeId, int>> entries;
        for (const GiveWay& gw : lay.give_way) {
            const Junction* j = js.find(gw.node);
            REQUIRE(j);
            REQUIRE(gw.connector >= 0);
            REQUIRE(static_cast<std::size_t>(gw.connector) < j->connectors.size());
            const LaneConnector& c = j->connectors[static_cast<std::size_t>(gw.connector)];
            CHECK(c.enabled);
            CHECK(!ring.count(j->arms[static_cast<std::size_t>(c.arm_from)].edge));  // from the approach
            CHECK(ring.count(j->arms[static_cast<std::size_t>(c.arm_to)].edge) > 0);  // onto the ring
            CHECK(!gw.yields_to.empty());
            for (int t : gw.yields_to) {
                const LaneConnector& circ = j->connectors[static_cast<std::size_t>(t)];
                CHECK(ring.count(j->arms[static_cast<std::size_t>(circ.arm_from)].edge) > 0);  // ring-in ...
                CHECK(circ.arm_to == c.arm_to);                                                 // ... -> same ring-out
            }
            entries.insert({gw.node, gw.connector});
        }
        // Every approach-to-ring connector is an entry; no ring connector yields.
        for (NodeId n : g.nodes) {
            const Junction* j = js.find(n);
            for (std::size_t ci = 0; ci < j->connectors.size(); ++ci) {
                const LaneConnector& c = j->connectors[ci];
                const bool from_ring = ring.count(j->arms[static_cast<std::size_t>(c.arm_from)].edge) > 0;
                const bool to_ring = ring.count(j->arms[static_cast<std::size_t>(c.arm_to)].edge) > 0;
                CHECK((entries.count({n, static_cast<int>(ci)}) > 0) == (!from_ring && to_ring));
            }
            // Full-circle U-turn available: spoke in -> ring out here, ring in -> spoke out here.
            bool onto = false, off = false;
            for (const LaneConnector& c : j->connectors) {
                const bool from_ring = ring.count(j->arms[static_cast<std::size_t>(c.arm_from)].edge) > 0;
                const bool to_ring = ring.count(j->arms[static_cast<std::size_t>(c.arm_to)].edge) > 0;
                onto = onto || (!from_ring && to_ring);
                off = off || (from_ring && !to_ring);
            }
            CHECK(onto);
            CHECK(off);
        }
    }
}

TEST_CASE("roundabout: forced fillets, and connector and corner overrides are honoured and survive regeneration") {
    Net net;
    net.spokes({0, 90, 180, 270}, 100);
    REQUIRE(net.place(20));
    const JunctionGroup g = the_group(net);
    const RoundaboutSizeClass prm = net.region->roundabout.size_class(g.radius);
    const JunctionSet base = net.junctions();
    const NodeId n0 = g.nodes[0];
    const Junction& j0 = *base.find(n0);
    const EdgeId out0 = g.ring_edges[0], in0 = g.ring_edges.back();

    // Forced radii: entry corner (spoke / ring-out) and exit corner (ring-in / spoke).
    int entry_corners = 0, exit_corners = 0;
    for (NodeId n : g.nodes) {
        const Junction& j = *base.find(n);
        const std::size_t i = static_cast<std::size_t>(std::find(g.nodes.begin(), g.nodes.end(), n) - g.nodes.begin());
        const EdgeId ro = g.ring_edges[i], ri = g.ring_edges[(i + g.nodes.size() - 1) % g.nodes.size()];
        for (std::size_t k = 0; k < j.corners.size(); ++k) {
            const EdgeId r = j.corners[k].edge_r, l = j.corners[k].edge_l;
            const bool r_ring = r == ro || r == ri, l_ring = l == ro || l == ri;
            if (r_ring == l_ring) continue;
            const EdgeId rng = r_ring ? r : l;
            CHECK(j.corner_radii[k] == doctest::Approx(rng == ro ? prm.entry_corner_radius : prm.exit_corner_radius));
            (rng == ro ? entry_corners : exit_corners)++;
        }
    }
    CHECK(entry_corners == 4);
    CHECK(exit_corners == 4);

    // Pick the spoke -> ring-out connector with the highest index at n0 and a corner next to the spoke.
    int spoke_arm = -1, out_arm = -1;
    for (std::size_t a = 0; a < j0.arms.size(); ++a) {
        if (j0.arms[a].edge == out0) out_arm = static_cast<int>(a);
        else if (j0.arms[a].edge != in0) spoke_arm = static_cast<int>(a);
    }
    REQUIRE(spoke_arm >= 0);
    REQUIRE(out_arm >= 0);
    const LaneConnector* target = nullptr;
    for (const LaneConnector& c : j0.connectors)
        if (c.arm_from == spoke_arm && c.arm_to == out_arm) target = &c;
    REQUIRE(target);
    const LaneRef& lf = j0.arms[static_cast<std::size_t>(target->arm_from)].lanes[static_cast<std::size_t>(target->lane_from)];
    const LaneRef& lt = j0.arms[static_cast<std::size_t>(target->arm_to)].lanes[static_cast<std::size_t>(target->lane_to)];
    ConnectorKey ck;
    ck.node = n0;
    ck.from_edge = lf.edge;
    ck.from_slot = lf.slot;
    ck.to_edge = lt.edge;
    ck.to_slot = lt.slot;
    ConnectorOverride off;
    off.enabled = false;

    // The corner between the spoke and the ring-out edge.
    CornerKey cornerkey{};
    for (const CornerCurve& cc : j0.corners)
        if ((cc.edge_r == out0 && cc.edge_l == j0.arms[static_cast<std::size_t>(spoke_arm)].edge) ||
            (cc.edge_l == out0 && cc.edge_r == j0.arms[static_cast<std::size_t>(spoke_arm)].edge))
            cornerkey = CornerKey{n0, cc.edge_l, cc.edge_r};
    REQUIRE(cornerkey.node.valid());

    JunctionOverrides ov;
    ov.connectors[ck] = off;
    ov.corner_radius[cornerkey] = 3.0;

    const auto check = [&](const JunctionSet& js, const RoundaboutResult& rr, const char* what) {
        INFO(what);
        const Junction* j = js.find(n0);
        REQUIRE(j);
        CHECK(j->kind == JunctionKind::Roundabout);
        const int ci = j->find_connector(spoke_arm, static_cast<int>(target->lane_from), out_arm, static_cast<int>(target->lane_to));
        REQUIRE(ci >= 0);
        CHECK(!j->connectors[static_cast<std::size_t>(ci)].enabled);
        CHECK(j->connectors[static_cast<std::size_t>(ci)].overridden);
        // Disabled entries no longer take part in give-way.
        REQUIRE(rr.layouts.size() == 1);
        for (const GiveWay& gw : rr.layouts[0].give_way)
            if (gw.node == n0) CHECK(gw.connector != ci);
        int radius_hits = 0;
        for (std::size_t k = 0; k < j->corners.size(); ++k)
            if (j->corners[k].edge_l == cornerkey.edge_l && j->corners[k].edge_r == cornerkey.edge_r) {
                CHECK(j->corner_radii[k] == doctest::Approx(3.0));
                ++radius_hits;
            }
        CHECK(radius_hits == 1);
        // The other forced radii are untouched.
        for (std::size_t k = 0; k < j->corners.size(); ++k) {
            if (j->corners[k].edge_l == cornerkey.edge_l && j->corners[k].edge_r == cornerkey.edge_r) continue;
            const bool r_ring = j->corners[k].edge_r == out0 || j->corners[k].edge_r == in0;
            const bool l_ring = j->corners[k].edge_l == out0 || j->corners[k].edge_l == in0;
            if (r_ring != l_ring) CHECK(j->corner_radii[k] > 7.0);
        }
    };
    // Twice from scratch (regeneration), and through the dirty path.
    for (int round = 0; round < 2; ++round) {
        JunctionSet js = build_junctions(net.g, *net.region, ov);
        const RoundaboutResult rr = apply_roundabouts(js, net.g, *net.region, ov);
        check(js, rr, "full rebuild");
    }
    {
        JunctionSet js = build_junctions(net.g, *net.region, ov, {n0});
        const RoundaboutResult rr = apply_roundabouts(js, net.g, *net.region, ov, {n0});
        check(js, rr, "dirty rebuild");
        for (NodeId n : g.nodes) CHECK(js.find(n) != nullptr);
    }
}

TEST_CASE("roundabout: dirty rebuild of one ring node equals the full rebuild") {
    Net net;
    net.spokes({0, 90, 180, 270}, 100);
    REQUIRE(net.place(20));
    const JunctionGroup g = the_group(net);
    const JunctionSet full = net.junctions();
    for (NodeId dirty : {g.nodes[0], g.nodes[2]}) {
        JunctionSet part = build_junctions(net.g, *net.region, {}, {dirty});
        const RoundaboutResult rr = apply_roundabouts(part, net.g, *net.region, {}, {dirty});
        CHECK(rr.layouts.size() == 1);
        for (NodeId n : g.nodes) {
            const Junction* a = full.find(n);
            const Junction* b = part.find(n);
            REQUIRE(a);
            REQUIRE(b);
            CHECK(a->kind == b->kind);
            CHECK(a->group_nodes == b->group_nodes);
            CHECK(a->trims == b->trims);
            REQUIRE(a->connectors.size() == b->connectors.size());
            for (std::size_t c = 0; c < a->connectors.size(); ++c) {
                CHECK(a->connectors[c].enabled == b->connectors[c].enabled);
                CHECK(a->connectors[c].path.size() == b->connectors[c].path.size());
            }
        }
    }
    // A node that is not on a ring touches no group.
    const NodeId far = net.g.edge(g.arms[0].edge)->a == g.nodes[g.arms[0].ring_index] ? net.g.edge(g.arms[0].edge)->b
                                                                                      : net.g.edge(g.arms[0].edge)->a;
    JunctionSet none = build_junctions(net.g, *net.region, {}, {far});
    CHECK(apply_roundabouts(none, net.g, *net.region, {}, {far}).layouts.empty());
}

TEST_CASE("roundabout: connectors at ring nodes are G1 and start and end exactly on the trim lines") {
    for (DrivingSide side : {DrivingSide::Left, DrivingSide::Right}) {
        for (const std::vector<double>& bearings : {std::vector<double>{90, 210, 330}, std::vector<double>{0, 90, 180, 270}}) {
            Net net(side);
            net.spokes(bearings, 100);
            REQUIRE(net.place(20));
            const JunctionGroup g = the_group(net);
            const JunctionSet js = net.junctions();
            int checked = 0;
            for (NodeId n : g.nodes) {
                const Junction& j = *js.find(n);
                if (j.cls != NodeClass::Junction) continue;
                REQUIRE(j.trims.size() == j.arms.size());
                for (const LaneConnector& c : j.connectors) {
                    ++checked;
                    REQUIRE(c.path.size() >= 2);
                    const JunctionArm& fa = j.arms[static_cast<std::size_t>(c.arm_from)];
                    const JunctionArm& ta = j.arms[static_cast<std::size_t>(c.arm_to)];
                    const ArmPoint p0 = arm_point(fa, j.trims[static_cast<std::size_t>(c.arm_from)], fa.lanes[static_cast<std::size_t>(c.lane_from)].lateral);
                    const ArmPoint p1 = arm_point(ta, j.trims[static_cast<std::size_t>(c.arm_to)], ta.lanes[static_cast<std::size_t>(c.lane_to)].lateral);
                    CHECK((c.path.front().xy() - p0.pos.xy()).length() < 1e-3);
                    CHECK((c.path.back().xy() - p1.pos.xy()).length() < 1e-3);
                    // Tangent at both ends matches the lanes: travel in is heading_out + pi, out is heading_out.
                    const Vec3 &a = c.path[0], &b = c.path[1];
                    const Vec3 &y = c.path[c.path.size() - 2], &z = c.path.back();
                    CHECK(std::fabs(wrap_angle(std::atan2(b.y - a.y, b.x - a.x) - (p0.heading_out + kPi))) < 0.12);
                    CHECK(std::fabs(wrap_angle(std::atan2(z.y - y.y, z.x - y.x) - p1.heading_out)) < 0.12);
                    // No kinks: the tightest forced fillet is 8 m, so about 0.125 rad/m; allow some sampling slack.
                    CHECK_MESSAGE(max_kink(c.path) < 0.2, "node " << n.value << " connector kink " << max_kink(c.path));
                }
            }
            CHECK(checked > 0);
        }
    }
}

TEST_CASE("roundabout: polygons are valid, island inside the ring, splitters on two-way arms") {
    for (DrivingSide side : {DrivingSide::Left, DrivingSide::Right}) {
        Net net(side);
        net.spokes({0, 90, 180, 270}, 100);
        REQUIRE(net.place(20));
        RoundaboutResult rr;
        const JunctionSet js = net.junctions(&rr);
        REQUIRE(rr.layouts.size() == 1);
        const RoundaboutLayout& lay = rr.layouts[0];
        REQUIRE_MESSAGE(lay.ok, lay.error);
        REQUIRE(lay.island.size() == 1);
        CHECK(area(lay.island[0]) > 0);
        CHECK(simple(lay.island[0]));
        // Island radius: ring radius 20 minus the inner kerb face at 3.5 m (two 3.5 m lanes).
        CHECK(area(lay.island[0]) == doctest::Approx(kPi * 16.5 * 16.5).epsilon(0.03));
        CHECK(lay.splitters.size() == 4);
        for (const SplitterIsland& s : lay.splitters) {
            CHECK(area(s.polygon) > 0);
            CHECK(simple(s.polygon));
        }
        for (const auto& [n, j] : js.junctions)
            for (const PolygonSet* ps : {&j.carriageway, &j.pavements, &j.gore})
                for (const auto& r : *ps) CHECK_MESSAGE(simple(r), "node " << n.value);
    }
}

TEST_CASE("roundabout: ring on sloping ground, and a multi-lane ring") {
    Net net;
    net.spokes({0, 90, 180, 270}, 100, "b_road", 3.0);  // far ends 3 m up: a 3% gradient
    REQUIRE(net.place(20, kRing, {0, 0, 0}));
    const JunctionGroup g = the_group(net);
    for (NodeId n : g.nodes) CHECK(net.g.node(n)->pos.z == doctest::Approx(0.0));  // the ring is flat
    RoundaboutResult rr;
    const JunctionSet js = net.junctions(&rr);
    REQUIRE(rr.layouts.size() == 1);
    CHECK(rr.layouts[0].ok);
    for (NodeId n : g.nodes) {
        const Junction& j = *js.find(n);
        CHECK(!j.degraded);
        for (const LaneConnector& c : j.connectors)
            for (const Vec3& p : c.path) {
                CHECK(p.z > -0.2);
                CHECK(p.z < 3.2);
            }
    }
    // The ring type has two driving lanes; the one-lane approach feeds both ring lanes.
    const Junction& j = *js.find(g.nodes[0]);
    int spoke_arm = -1, out_arm = -1, in_arm = -1;
    for (std::size_t a = 0; a < j.arms.size(); ++a) {
        if (j.arms[a].edge == g.ring_edges[0]) out_arm = static_cast<int>(a);
        else if (j.arms[a].edge == g.ring_edges.back()) in_arm = static_cast<int>(a);
        else spoke_arm = static_cast<int>(a);
    }
    REQUIRE(spoke_arm >= 0);
    CHECK(j.arms[static_cast<std::size_t>(out_arm)].outbound_count() == 2);
    CHECK(j.arms[static_cast<std::size_t>(in_arm)].inbound_count() == 2);
    int entries = 0, through = 0;
    for (const LaneConnector& c : j.connectors) {
        if (c.arm_from == spoke_arm && c.arm_to == out_arm) ++entries;
        if (c.arm_from == in_arm && c.arm_to == out_arm) ++through;
    }
    CHECK(entries >= 2);
    CHECK(through >= 2);
}

TEST_CASE("roundabout: an unbuildable ring is flagged visibly") {
    Net net;
    net.hand_ring(6, 4, {true, true, true, true}, false);  // island under the 3 m minimum
    RoundaboutResult rr;
    const JunctionSet js = net.junctions(&rr);
    REQUIRE(rr.layouts.size() == 1);
    CHECK(!rr.layouts[0].ok);
    CHECK(rr.layouts[0].error.find("too tight") != std::string::npos);
    CHECK(rr.layouts[0].island.empty());
    for (NodeId n : rr.layouts[0].group.nodes) {
        const Junction* j = js.find(n);
        REQUIRE(j);
        CHECK(j->kind == JunctionKind::Roundabout);
        CHECK(j->degraded);
        CHECK(j->degraded_reason.find("roundabout") != std::string::npos);
    }
}

TEST_CASE("roundabout: the place tool validates its radius and never deletes what it should not") {
    {  // radius limits (region pack: 8 .. 100 m)
        Net net;
        net.spokes({0, 90, 180, 270}, 150);
        const std::string before = snapshot(net.g);
        const std::size_t depth = net.st.undo_depth();
        for (double r : {1e5, 100.5, 7.9, 0.0, -5.0, std::nan(""), HUGE_VAL}) {
            CHECK(!net.place(r));
            CHECK_FALSE(check_place_roundabout(net.g, {0, 0, 0}, r, kRing, *net.region).empty());
        }
        CHECK(check_place_roundabout(net.g, {0, 0, 0}, 1e5, kRing, *net.region).find("maximum") != std::string::npos);
        CHECK(check_place_roundabout(net.g, {0, 0, 0}, 5, kRing, *net.region).find("minimum") != std::string::npos);
        CHECK(!net.place(20, "no_such_road"));
        CHECK(!net.place(20, "b_road"));  // exists but is not a ring type
        CHECK(!net.place(20, kRing, {std::nan(""), 0, 0}));
        CHECK(snapshot(net.g) == before);
        CHECK(net.st.undo_depth() == depth);
        CHECK(check_place_roundabout(net.g, {0, 0, 0}, 100, kRing, *net.region).empty());  // the maximum itself is fine
        CHECK(check_place_roundabout(net.g, {0, 0, 0}, 8.0 + 3.5, kRing, *net.region).empty());
    }
    {  // spokes shorter than the ring leave stubs that are too short
        Net stub;
        stub.spokes({0, 90, 180, 270}, 22);
        const std::string before = snapshot(stub.g);
        CHECK(!stub.place(20));
        CHECK(snapshot(stub.g) == before);
        CHECK(check_place_roundabout(stub.g, {0, 0, 0}, 20, kRing, *stub.region).find("stub") != std::string::npos);
    }
    {  // a single spoke ending at the centre: trimmed to the ring, the centre node goes, nothing is left inside
        Net net;
        net.spokes({0}, 100);
        REQUIRE(net.place(20));
        for (const auto& [id, n] : net.g.nodes()) CHECK(n.pos.xy().length() > 19.9);
        for (const auto& [id, e] : net.g.edges())
            for (double s = 0; s <= e.horizontal.length(); s += 1.0) CHECK(edge_point(e, s).xy().length() > 19.5);
        const JunctionGroup g = the_group(net);
        CHECK(g.arms.size() == 1);
        CHECK(g.nodes.size() >= 4);
    }
    {  // a road wholly inside the circle would sit in the island: refused
        Net net;
        net.spokes({0, 90, 180, 270}, 100);
        DrawRoadSpec s;
        s.road_type = "residential";
        s.start.pos = {4, 4, 0};
        s.end.pos = {9, 4, 0};
        REQUIRE(net.st.execute(make_draw_road(s)));
        const std::string before = snapshot(net.g);
        CHECK(!net.place(20));
        CHECK(snapshot(net.g) == before);
        CHECK(check_place_roundabout(net.g, {0, 0, 0}, 20, kRing, *net.region).find("inside") != std::string::npos);
    }
    {  // a bridge through the circle is refused; one clear of it is fine
        Net net;
        net.spokes({0, 90, 180, 270}, 100);
        const EdgeId e = net.g.edges().begin()->first;
        REQUIRE(net.st.execute(make_set_structure_span(e, 75, 95, StructureKind::Bridge)));
        CHECK(!net.place(20));
        CHECK(check_place_roundabout(net.g, {0, 0, 0}, 20, kRing, *net.region).find("bridge") != std::string::npos);
        Net ok;
        ok.spokes({0, 90, 180, 270}, 100);
        REQUIRE(ok.st.execute(make_set_structure_span(ok.g.edges().begin()->first, 20, 60, StructureKind::Bridge)));
        CHECK(ok.place(20));
    }
    {  // an existing ring is never cut up
        Net net;
        net.spokes({0, 90, 180, 270}, 100);
        REQUIRE(net.place(20));
        const std::string before = snapshot(net.g);
        CHECK(!net.place(22));
        CHECK(!net.place(18, kRing, {5, 0, 0}));
        CHECK(snapshot(net.g) == before);
        CHECK(check_place_roundabout(net.g, {0, 0, 0}, 22, kRing, *net.region).find("existing roundabout") != std::string::npos);
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
    CHECK(scan(net).groups.empty());
    REQUIRE(net.st.redo());
    CHECK(snapshot(net.g) == after);
    CHECK(scan(net).groups.size() == 1);
    REQUIRE(net.st.undo());
    CHECK(snapshot(net.g) == before);
    REQUIRE(net.place(20));
    CHECK(snapshot(net.g) != after);  // ids are never reused
    CHECK(scan(net).groups.size() == 1);
}

TEST_CASE("roundabout: editing the ring is ordinary editing") {
    Net net;
    net.spokes({0, 90, 180, 270}, 100);
    REQUIRE(net.place(20));
    JunctionGroup g = the_group(net);
    // Deleting one ring edge breaks the ring: reported as malformed, the other junctions stay.
    REQUIRE(net.st.execute(make_delete_edge(g.ring_edges[1])));
    const RoundaboutScan s = scan(net);
    CHECK(s.groups.empty());
    REQUIRE(s.problems.size() == 1);
    CHECK(s.problems[0].what.find("not closed") != std::string::npos);
    JunctionSet js = build_junctions(net.g, *net.region, {});
    apply_roundabouts(js, net.g, *net.region, {});
    for (const auto& [n, j] : js.junctions) CHECK(j.kind == JunctionKind::Plain);
    REQUIRE(net.st.undo());
    CHECK(scan(net).groups.size() == 1);
}

TEST_CASE("roundabout: realistic rings give valid, non-degraded geometry at every ring node") {
    struct Case {
        double radius;
        int arms;
        bool one_lane;
        DrivingSide side;
    };
    std::vector<Case> cases;
    for (double r : {11.0, 16.0, 25.0, 45.0, 100.0})
        for (int arms : {3, 4, 5, 6})
            for (bool one_lane : {false, true}) cases.push_back({r, arms, one_lane, DrivingSide::Left});
    cases.push_back({20, 4, false, DrivingSide::Right});
    cases.push_back({30, 5, true, DrivingSide::Right});

    int failures = 0;
    for (const Case& c : cases) {
        auto p = load_region_pack_by_id(CP_DATA_DIR, "uk");
        REQUIRE_MESSAGE(p, p.error);
        p.value->driving_side = c.side;
        if (c.one_lane) {  // a second, single-lane ring type
            RoadType one = p.value->road_types.at(kRing);
            one.id = "ring_1lane";
            std::vector<CrossSectionSlot> slots;
            bool kept = false;
            for (const CrossSectionSlot& s : one.one_way_variant) {
                if (s.lane_type == "driving") {
                    if (kept) continue;
                    kept = true;
                }
                slots.push_back(s);
            }
            one.one_way_variant = slots;
            p.value->road_types.emplace(one.id, one);
        }
        auto region = std::make_shared<const RegionPack>(*p);
        Net net(c.side);
        net.region = region;
        net.g.set_region(region);
        std::vector<double> bearings;
        for (int k = 0; k < c.arms; ++k) bearings.push_back(15.0 + k * 360.0 / c.arms);
        net.spokes(bearings, c.radius + 90);
        const char* type = c.one_lane ? "ring_1lane" : kRing;
        const std::string why = check_place_roundabout(net.g, {0, 0, 0}, c.radius, type, *region);
        INFO("radius " << c.radius << " arms " << c.arms << (c.one_lane ? " 1-lane" : " 2-lane"));
        REQUIRE_MESSAGE(why.empty(), why);
        REQUIRE(net.place(c.radius, type));
        RoundaboutResult rr;
        const JunctionSet js = net.junctions(&rr);
        REQUIRE(rr.layouts.size() == 1);
        const RoundaboutLayout& lay = rr.layouts[0];
        CHECK_MESSAGE(lay.ok, lay.error);
        for (NodeId n : lay.group.nodes) {
            const Junction* j = js.find(n);
            REQUIRE(j);
            CHECK(j->kind == JunctionKind::Roundabout);
            if (j->cls != NodeClass::Junction) continue;
            const bool bad = j->degraded || j->carriageway.empty();
            if (bad) ++failures;
            INFO("node " << n.value << ": " << j->degraded_reason);
            CHECK(!j->degraded);
            CHECK_MESSAGE(!j->carriageway.empty(), "node " << n.value << " has no carriageway");
            for (const PolygonSet* ps : {&j->carriageway, &j->pavements})
                for (const auto& r : *ps) CHECK_MESSAGE(simple(r), "node " << n.value);
            CHECK(j->corners.size() == j->arms.size());
            for (const CornerCurve& cc : j->corners)
                if (cc.angle < kPi - 0.01) CHECK_MESSAGE(!cc.kerb.empty(), "node " << n.value << " corner without a kerb");
        }
    }
    CHECK(failures == 0);
}

TEST_CASE("roundabout: build_junctions alone already returns Roundabout-kind ring junctions") {
    Net net;
    net.spokes({90, 210, 330}, 100);
    REQUIRE(net.place(20));
    const JunctionGroup g = the_group(net);
    const JunctionSet js = build_junctions(net.g, *net.region, {});
    for (NodeId n : g.nodes) {
        REQUIRE(js.find(n));
        CHECK(js.find(n)->kind == JunctionKind::Roundabout);
        CHECK(js.find(n)->group_nodes == g.nodes);
    }
    const JunctionSet part = build_junctions(net.g, *net.region, {}, {g.nodes[1]});
    for (NodeId n : g.nodes) CHECK(part.find(n) != nullptr);
    // Plain build: no marking.
    const JunctionSet plain = build_plain_junctions(net.g, *net.region, {});
    for (NodeId n : g.nodes)
        if (plain.find(n)) CHECK(plain.find(n)->kind == JunctionKind::Plain);
}
