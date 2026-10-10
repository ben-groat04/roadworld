// Edit loop: dirty-set reporting (RoadGraph), the change-road-type command, and incremental street lights
// matching a full rebuild after edits.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iterator>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include "doctest/doctest.h"
#include "cp/junction.h"
#include "cp/junction_roundabout.h"
#include "cp/road_commands.h"
#include "cp/street_lights.h"
#include "portable_rng.h"

using namespace cp;

namespace {

struct EditNet {
    std::shared_ptr<const RegionPack> region;
    RoadGraph g;
    CommandStack<RoadGraph> st{g};

    EditNet() {
        auto p = load_region_pack_by_id(CP_DATA_DIR, "uk");
        REQUIRE_MESSAGE(p, p.error);
        region = std::make_shared<const RegionPack>(*p);
        g.set_region(region);
    }

    bool road(Vec3 a, Vec3 b, const char* type = "residential", NodeId na = {}, NodeId nb = {}) {
        DrawRoadSpec s;
        s.road_type = type;
        s.start.pos = a;
        s.end.pos = b;
        s.start.existing_node = na;
        s.end.existing_node = nb;
        return st.execute(make_draw_road(s));
    }
    JunctionSet junctions() const {
        JunctionSet js = build_plain_junctions(g, *region, {});
        apply_roundabouts(js, g, *region, {});
        return js;
    }
};

// Value snapshot of the parts of the graph an incremental consumer sees.
struct Snapshot {
    std::vector<std::tuple<std::uint64_t, std::uint64_t, std::uint64_t, std::string, double, double>> edges;
    std::vector<std::tuple<std::uint64_t, double, double, double>> nodes;
    bool operator==(const Snapshot&) const = default;
};
Snapshot snapshot(const RoadGraph& g) {
    Snapshot s;
    for (const auto& [id, e] : g.edges()) {
        const double len = e.horizontal.length();
        s.edges.emplace_back(id.value, e.a.value, e.b.value, e.road_type, len, e.vertical.z(len * 0.5));
    }
    for (const auto& [id, n] : g.nodes()) s.nodes.emplace_back(id.value, n.pos.x, n.pos.y, n.pos.z);
    return s;
}

bool same_light(const StreetLight& a, const StreetLight& b) {
    return std::abs(a.position.x - b.position.x) < 1e-9 && std::abs(a.position.y - b.position.y) < 1e-9 &&
           std::abs(a.position.z - b.position.z) < 1e-9 && std::abs(a.heading - b.heading) < 1e-9 &&
           a.style == b.style && a.edge == b.edge && a.node == b.node && std::abs(a.s - b.s) < 1e-9 &&
           a.side == b.side && a.origin == b.origin && a.twin_arm == b.twin_arm && a.partner_edge == b.partner_edge;
}
bool same_lights(const std::vector<StreetLight>& a, const std::vector<StreetLight>& b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i)
        if (!same_light(a[i], b[i])) return false;
    return true;
}
double point_segment_distance(Vec2 p, Vec2 a, Vec2 b) {
    const double dx = b.x - a.x, dy = b.y - a.y;
    const double l2 = dx * dx + dy * dy;
    const double t = l2 > 0 ? std::clamp(((p.x - a.x) * dx + (p.y - a.y) * dy) / l2, 0.0, 1.0) : 0.0;
    return std::hypot(p.x - (a.x + t * dx), p.y - (a.y + t * dy));
}
bool segments_cross(Vec2 a, Vec2 b, Vec2 c, Vec2 d) {
    const auto side = [](Vec2 p, Vec2 q, Vec2 r) { return (q.x - p.x) * (r.y - p.y) - (q.y - p.y) * (r.x - p.x); };
    return side(a, b, c) * side(a, b, d) < 0 && side(c, d, a) * side(c, d, b) < 0;
}
// True when no two non-adjacent edges (straight node-to-node) cross or come within 15 m of each other,
// and no node is within 15 m of an edge it does not belong to.
bool planar_and_apart(const RoadGraph& g) {
    for (const auto& [ia, ea] : g.edges()) {
        const Vec2 a0 = g.node(ea.a)->pos.xy(), a1 = g.node(ea.b)->pos.xy();
        for (const auto& [ib, eb] : g.edges()) {
            if (!(ia < ib)) continue;
            if (ea.a == eb.a || ea.a == eb.b || ea.b == eb.a || ea.b == eb.b) continue;
            const Vec2 b0 = g.node(eb.a)->pos.xy(), b1 = g.node(eb.b)->pos.xy();
            if (segments_cross(a0, a1, b0, b1)) return false;
            if (std::min({point_segment_distance(a0, b0, b1), point_segment_distance(a1, b0, b1),
                          point_segment_distance(b0, a0, a1), point_segment_distance(b1, a0, a1)}) < 15)
                return false;
        }
    }
    return true;
}

template <class Map>
bool same_map(const Map& a, const Map& b) {
    if (a.size() != b.size()) return false;
    auto i = a.begin();
    auto j = b.begin();
    for (; i != a.end(); ++i, ++j)
        if (!(i->first == j->first) || !same_lights(i->second, j->second)) return false;
    return true;
}
bool same_result(const StreetLightResult& a, const StreetLightResult& b) {
    return same_map(a.edges, b.edges) && same_map(a.junctions, b.junctions) && same_map(a.roundabouts, b.roundabouts);
}

// Brings the cache up to date from the graph's dirty set (as an incremental consumer would), clears the
// dirty set and compares with a full rebuild.
bool incremental_matches_full(EditNet& n, StreetLightResult& cache) {
    const JunctionSet js = n.junctions();
    const std::vector<NodeId> dn(n.g.dirty_nodes().begin(), n.g.dirty_nodes().end());
    const std::vector<EdgeId> de(n.g.dirty_edges().begin(), n.g.dirty_edges().end());
    rebuild_dirty_street_lights(cache, n.g, *n.region, js, nullptr, dn, de);
    n.g.clear_dirty();
    const StreetLightResult full = generate_street_lights(n.g, *n.region, js, nullptr);
    return same_result(cache, full);
}

}  // namespace

TEST_CASE("edit loop: erase_node reports the removed nodes and the edge that ended at them") {
    EditNet n;
    // Both end nodes are created by the draw; undoing removes the edge and both nodes.
    REQUIRE(n.road({0, 0, 0}, {100, 0, 0}));
    const EdgeId e = n.g.edges().begin()->first;
    const NodeId a = n.g.edge(e)->a, b = n.g.edge(e)->b;
    n.g.clear_dirty();
    REQUIRE(n.st.undo());
    CHECK(n.g.nodes().empty());
    CHECK(n.g.node(a) == nullptr);
    CHECK(n.g.dirty_nodes().count(a) == 1);
    CHECK(n.g.dirty_nodes().count(b) == 1);
    CHECK(n.g.dirty_edges().count(e) == 1);
    n.g.clear_dirty();
    CHECK(n.g.dirty_nodes().empty());
    CHECK(n.g.dirty_edges().empty());
}

TEST_CASE("edit loop: undoing a street drawn between two split nodes reports every touched id") {
    EditNet n;
    REQUIRE(n.road({0, 0, 0}, {200, 0, 0}));
    REQUIRE(n.road({0, 100, 0}, {200, 100, 0}));
    std::set<EdgeId> before;
    for (const auto& [id, e] : n.g.edges()) before.insert(id);
    const EdgeId first = *before.begin(), last = *before.rbegin();

    DrawRoadSpec s;
    s.road_type = "residential";
    s.start.split_edge = first;
    s.start.split_s = 100;
    s.end.split_edge = last;
    s.end.split_s = 100;
    REQUIRE(n.st.execute(make_draw_road(s)));
    std::set<NodeId> created;
    for (const auto& [id, nd] : n.g.nodes())
        if (std::abs(nd.pos.x - 100) < 1e-6) created.insert(id);
    REQUIRE(created.size() == 2);
    n.g.clear_dirty();

    REQUIRE(n.st.undo());
    for (NodeId id : created) {
        CHECK(n.g.node(id) == nullptr);
        CHECK(n.g.dirty_nodes().count(id) == 1);  // removed nodes are reported
    }
    std::set<EdgeId> after;
    for (const auto& [id, e] : n.g.edges()) after.insert(id);
    CHECK(after == before);
    CHECK(n.g.dirty_edges().size() >= 5);  // split halves, the new street and the restored originals
    for (EdgeId id : before) {
        CHECK(n.g.dirty_edges().count(id) == 1);
        CHECK(n.g.dirty_nodes().count(n.g.edge(id)->a) == 1);
        CHECK(n.g.dirty_nodes().count(n.g.edge(id)->b) == 1);
    }
}

TEST_CASE("edit loop: change road type, execute / undo / redo") {
    EditNet n;
    REQUIRE(n.road({0, 0, 0}, {150, 0, 0}, "residential"));
    const EdgeId e = n.g.edges().begin()->first;
    REQUIRE(n.road({150, 0, 0}, {300, 30, 0}, "residential", n.g.edge(e)->b));
    const EdgeId other = n.g.edges().rbegin()->first;
    const Snapshot s0 = snapshot(n.g);
    n.g.clear_dirty();

    REQUIRE(n.st.execute(make_change_road_type(e, "b_road")));
    CHECK(n.g.edge(e)->road_type == "b_road");
    CHECK(n.g.edge(other)->road_type == "residential");
    const Snapshot s1 = snapshot(n.g);
    CHECK_FALSE(s1 == s0);

    // Dirty set: the edge and both of its end nodes.
    CHECK(n.g.dirty_edges().count(e) == 1);
    CHECK(n.g.dirty_nodes().count(n.g.edge(e)->a) == 1);
    CHECK(n.g.dirty_nodes().count(n.g.edge(e)->b) == 1);
    CHECK(n.g.dirty_edges().count(other) == 0);

    n.g.clear_dirty();
    REQUIRE(n.st.undo());
    CHECK(snapshot(n.g) == s0);
    CHECK(n.g.dirty_edges().count(e) == 1);
    CHECK(n.g.dirty_nodes().count(n.g.edge(e)->a) == 1);
    CHECK(n.g.dirty_nodes().count(n.g.edge(e)->b) == 1);

    REQUIRE(n.st.redo());
    CHECK(snapshot(n.g) == s1);
    REQUIRE(n.st.undo());
    CHECK(snapshot(n.g) == s0);
}

TEST_CASE("edit loop: change road type is rejected without touching the graph") {
    EditNet n;
    REQUIRE(n.road({0, 0, 0}, {150, 0, 0}, "residential"));
    const EdgeId e = n.g.edges().begin()->first;
    const Snapshot s0 = snapshot(n.g);
    n.g.clear_dirty();

    CHECK_FALSE(n.st.execute(make_change_road_type(e, "no_such_road_type")));
    CHECK_FALSE(n.st.execute(make_change_road_type(e, "residential")));       // already that type
    CHECK_FALSE(n.st.execute(make_change_road_type(EdgeId{999}, "b_road")));  // no such edge
    CHECK(snapshot(n.g) == s0);
    CHECK(n.g.dirty_nodes().empty());
    CHECK(n.g.dirty_edges().empty());
    CHECK(n.st.undo_depth() == 1);  // only the draw

    // Without a region pack the type cannot be validated.
    RoadGraph bare;
    CommandStack<RoadGraph> st(bare);
    const NodeId na = bare.insert_node(Node{{}, {0, 0, 0}});
    const NodeId nb = bare.insert_node(Node{{}, {100, 0, 0}});
    Edge ed;
    ed.a = na;
    ed.b = nb;
    ed.road_type = "residential";
    const EdgeId be = bare.insert_edge(ed);
    REQUIRE(be.valid());
    CHECK_FALSE(st.execute(make_change_road_type(be, "b_road")));
    CHECK(bare.edge(be)->road_type == "residential");
}

TEST_CASE("edit loop: street lights after road type changes equal a full rebuild") {
    EditNet n;
    REQUIRE(n.road({-200, 0, 0}, {0, 0, 0}, "residential"));
    const NodeId centre = n.g.edges().begin()->second.b;
    REQUIRE(n.road({0, 0, 0}, {200, 0, 0}, "residential", centre));
    REQUIRE(n.road({0, 0, 0}, {0, 200, 0}, "residential", centre));
    StreetLightResult cache = generate_street_lights(n.g, *n.region, n.junctions(), nullptr);
    n.g.clear_dirty();

    for (const char* type : {"b_road", "main_street", "country_lane", "residential"}) {
        const EdgeId e = std::next(n.g.edges().begin())->first;
        REQUIRE(n.st.execute(make_change_road_type(e, type)));
        CHECK(incremental_matches_full(n, cache));
    }
    REQUIRE(n.st.undo());
    CHECK(incremental_matches_full(n, cache));
    REQUIRE(n.st.redo());
    CHECK(incremental_matches_full(n, cache));
}

TEST_CASE("edit loop: incremental street lights equal a full rebuild over random edits") {
    for (std::uint64_t seed : {1ull, 2ull, 3ull, 4ull, 5ull, 6ull}) {
        INFO("seed " << seed);
        PortableRng rng{seed};
        EditNet n;
        // A 3x3 block grid with short edges so neighbouring junctions interact.
        std::map<std::pair<int, int>, NodeId> at;
        const auto pos = [&](int i, int j) { return Vec3{i * 70.0 + rng.uniform(-5, 5), j * 70.0 + rng.uniform(-5, 5), 0}; };
        std::map<std::pair<int, int>, Vec3> p;
        for (int j = 0; j < 3; ++j)
            for (int i = 0; i < 3; ++i) p[{i, j}] = pos(i, j);
        const auto link = [&](std::pair<int, int> a, std::pair<int, int> b) {
            REQUIRE(n.road(p[a], p[b], "residential", at[a], at[b]));
            const Edge& e = n.g.edges().rbegin()->second;
            at[a] = e.a;
            at[b] = e.b;
        };
        for (int j = 0; j < 3; ++j)
            for (int i = 0; i < 3; ++i) {
                if (i > 0) link({i - 1, j}, {i, j});
                if (j > 0) link({i, j - 1}, {i, j});
            }
        StreetLightResult cache = generate_street_lights(n.g, *n.region, n.junctions(), nullptr);
        n.g.clear_dirty();

        const char* types[] = {"residential", "b_road", "main_street", "country_lane"};
        const auto pick = [&](std::size_t count) { return static_cast<std::size_t>(rng.uniform(0, 1) * static_cast<double>(count)) % count; };
        for (int step = 0; step < 40; ++step) {
            std::vector<NodeId> ids;
            for (const auto& [id, nd] : n.g.nodes()) ids.push_back(id);
            std::vector<EdgeId> eids;
            for (const auto& [id, e] : n.g.edges()) eids.push_back(id);
            const double r = rng.uniform(0, 1);
            bool changed = false;
            if (r < 0.2 && ids.size() >= 2) {
                const NodeId a = ids[pick(ids.size())], b = ids[pick(ids.size())];
                if (a != b) changed = n.road(n.g.node(a)->pos, n.g.node(b)->pos, types[pick(4)], a, b);
            } else if (r < 0.3 && !eids.empty()) {
                changed = n.st.execute(make_delete_edge(eids[pick(eids.size())]));
            } else if (r < 0.55 && !ids.empty()) {
                const NodeId id = ids[pick(ids.size())];
                const Vec3 q = n.g.node(id)->pos;
                changed = n.st.execute(make_move_node(id, {q.x + rng.uniform(-15, 15), q.y + rng.uniform(-15, 15), 0}));
            } else if (r < 0.75 && !eids.empty()) {
                changed = n.st.execute(make_change_road_type(eids[pick(eids.size())], types[pick(4)]));
            } else if (r < 0.85 && !eids.empty()) {
                const EdgeId e = eids[pick(eids.size())];
                const double len = n.g.edge(e)->horizontal.length();
                const double s_split = len * rng.uniform(0.3, 0.7);
                const Vec3 from = edge_point(*n.g.edge(e), s_split);
                DrawRoadSpec s;
                s.road_type = types[pick(4)];
                s.start.split_edge = e;
                s.start.split_s = s_split;
                s.end.pos = {from.x + rng.uniform(-70, 70), from.y + rng.uniform(-70, 70), 0};
                changed = n.st.execute(make_draw_road(s));
            } else if (n.st.can_undo()) {
                n.st.undo();
            }
            // Keep the graph planar and roads apart: lights also depend on every junction polygon NEARBY
            // in the plane (a spatial, not graph, dependency), which no hop-based widening covers.
            if (changed && !planar_and_apart(n.g)) n.st.undo();
            INFO("step " << step);
            REQUIRE(incremental_matches_full(n, cache));
        }
    }
}
