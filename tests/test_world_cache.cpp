// WorldCache (cp/world_cache.h): incremental update == full rebuild, change-list completeness and minimality,
// versions, snapshot immutability, and a test-double layer that plugs in without touching the built-in ones.
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "cp/test_layouts.h"
#include "cp/world_cache.h"
#include "doctest/doctest.h"
#include "portable_rng.h"

using namespace cp;

namespace {

// ------------------------------------------------------------------ fixture

struct Net {
    std::shared_ptr<const RegionPack> region;
    std::shared_ptr<const MarkingRules> rules;
    RoadGraph g;
    CommandStack<RoadGraph> st{g};
    std::unique_ptr<WorldCache> cache;

    explicit Net(WorldCacheParams params = {}, bool with_rules = true) {
        auto p = load_region_pack_by_id(CP_DATA_DIR, "uk");
        REQUIRE_MESSAGE(p, p.error);
        region = std::make_shared<const RegionPack>(*p);
        g.set_region(region);
        if (with_rules) {
            auto r = load_marking_rules_by_id(CP_DATA_DIR, "uk");
            REQUIRE_MESSAGE(r, r.error);
            rules = std::make_shared<const MarkingRules>(*r);
        }
        cache = std::make_unique<WorldCache>(g, region, rules, std::move(params));
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
};

// ------------------------------------------------------------------ structural comparison

bool same_v3(Vec3 a, Vec3 b) { return a.x == b.x && a.y == b.y && a.z == b.z; }
bool same_v3s(const std::vector<Vec3>& a, const std::vector<Vec3>& b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i)
        if (!same_v3(a[i], b[i])) return false;
    return true;
}
bool same_polys(const PolygonSet& a, const PolygonSet& b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (a[i].size() != b[i].size()) return false;
        for (std::size_t k = 0; k < a[i].size(); ++k)
            if (a[i][k].x != b[i][k].x || a[i][k].y != b[i][k].y) return false;
    }
    return true;
}

bool same_junction(const Junction& a, const Junction& b) {
    if (a.node != b.node || !same_v3(a.pos, b.pos) || a.cls != b.cls || a.kind != b.kind || a.group_nodes != b.group_nodes ||
        a.arms.size() != b.arms.size() || a.major_arms != b.major_arms || a.corner_radii != b.corner_radii || a.trims != b.trims ||
        a.corners.size() != b.corners.size() || !same_polys(a.carriageway, b.carriageway) || !same_polys(a.pavements, b.pavements) ||
        !same_polys(a.gore, b.gore) || a.crossings.size() != b.crossings.size() || a.pockets.size() != b.pockets.size() ||
        a.degraded != b.degraded || a.degraded_reason != b.degraded_reason || a.connectors.size() != b.connectors.size())
        return false;
    for (std::size_t i = 0; i < a.corners.size(); ++i)
        if (!same_v3s(a.corners[i].kerb, b.corners[i].kerb) || a.corners[i].radius != b.corners[i].radius) return false;
    for (std::size_t i = 0; i < a.connectors.size(); ++i)
        if (!same_v3s(a.connectors[i].path, b.connectors[i].path) || a.connectors[i].kind != b.connectors[i].kind ||
            a.connectors[i].conflicts != b.connectors[i].conflicts || a.connectors[i].enabled != b.connectors[i].enabled)
            return false;
    for (std::size_t i = 0; i < a.arms.size(); ++i)
        if (a.arms[i].edge != b.arms[i].edge || a.arms[i].heading_out != b.arms[i].heading_out ||
            a.arms[i].lanes.size() != b.arms[i].lanes.size())
            return false;
    return true;
}

bool same_layout(const RoundaboutLayout& a, const RoundaboutLayout& b) {
    if (a.group.nodes != b.group.nodes || a.group.ring_edges != b.group.ring_edges || a.group.arms.size() != b.group.arms.size() ||
        a.ok != b.ok || a.error != b.error || !same_polys(a.island, b.island) || !same_polys(a.apron, b.apron) ||
        a.splitters.size() != b.splitters.size() || a.give_way.size() != b.give_way.size() ||
        a.ring_junctions.size() != b.ring_junctions.size())
        return false;
    for (std::size_t i = 0; i < a.splitters.size(); ++i)
        if (!same_polys({a.splitters[i].polygon}, {b.splitters[i].polygon})) return false;
    return true;
}

bool same_snapshot(std::string_view layer, const void* a, const void* b) {
    if (layer == kLayerRoadMesh) return same_content(*static_cast<const MeshChunk*>(a), *static_cast<const MeshChunk*>(b));
    if (layer == kLayerMarkings) return same_content(*static_cast<const MarkingChunk*>(a), *static_cast<const MarkingChunk*>(b));
    if (layer == kLayerStreetLights)
        return same_content(*static_cast<const StreetLightGroup*>(a), *static_cast<const StreetLightGroup*>(b));
    return false;
}

const char* const kTileLayers[] = {"road_mesh", "markings", "street_lights"};

// Empty string when the two caches hold structurally identical derived data, else what differs first.
std::string compare_caches(const WorldCache& a, const WorldCache& b) {
    const JunctionSet &ja = a.junctions(), &jb = b.junctions();
    if (ja.junctions.size() != jb.junctions.size()) return "junction count " + std::to_string(ja.junctions.size()) + " vs " + std::to_string(jb.junctions.size());
    auto i = ja.junctions.begin();
    auto j = jb.junctions.begin();
    for (; i != ja.junctions.end(); ++i, ++j) {
        if (i->first != j->first) return "junction ids differ at " + std::to_string(i->first.value);
        if (!same_junction(i->second, j->second)) return "junction " + std::to_string(i->first.value) + " differs";
    }
    if (a.roundabouts().size() != b.roundabouts().size()) return "roundabout count differs";
    for (std::size_t k = 0; k < a.roundabouts().size(); ++k)
        if (!same_layout(a.roundabouts()[k], b.roundabouts()[k])) return "roundabout layout " + std::to_string(k) + " differs";
    for (const char* layer : kTileLayers) {
        std::vector<TileKey> ta = a.tiles(layer), tb = b.tiles(layer);
        std::sort(ta.begin(), ta.end());
        std::sort(tb.begin(), tb.end());
        if (ta != tb) return std::string(layer) + ": tile sets differ (" + std::to_string(ta.size()) + " vs " + std::to_string(tb.size()) + ")";
        for (const TileKey& t : ta) {
            const auto da = a.tile<char>(layer, t), db = b.tile<char>(layer, t);
            if (!da || !db || !same_snapshot(layer, da.get(), db.get()))
            {
                std::string extra;
                if (std::string_view(layer) == kLayerMarkings && da && db) {
                    const auto &ma = static_cast<const MarkingChunk*>(static_cast<const void*>(da.get()))->colours, &mb = static_cast<const MarkingChunk*>(static_cast<const void*>(db.get()))->colours;
                    for (const auto& [c, b] : ma) extra += " " + c + ":" + std::to_string(b.indices.size()) + "/" + (mb.count(c) ? std::to_string(mb.at(c).indices.size()) : "-");
                }
                return std::string(layer) + ": tile (" + std::to_string(t.i) + "," + std::to_string(t.j) + ") differs" + extra;
            }
        }
    }
    return "";
}

// ------------------------------------------------------------------ change-list verification

struct Held {
    std::shared_ptr<const void> data;
    std::uint64_t version = 0;
};
using Snap = std::map<std::string, std::map<TileKey, Held>>;

Snap take(const WorldCache& c) {
    Snap s;
    for (const char* layer : kTileLayers)
        for (const TileKey& t : c.tiles(layer)) s[layer][t] = {c.tile<char>(layer, t), c.tile_version(layer, t)};
    return s;
}

// Every tile whose content differs from before is reported (and nothing else is), versions only go up on
// change, unchanged tiles keep their snapshot object and version.
void verify_changes(const Snap& before, const WorldCache& c, const ChangeList& cl) {
    for (const char* layer : kTileLayers) {
        INFO("layer " << layer);
        const LayerChanges* lc = cl.find(layer);
        std::map<TileKey, std::uint64_t> added, changed;
        std::set<TileKey> removed;
        if (lc) {
            for (const TileVersion& v : lc->added) CHECK(added.emplace(v.tile, v.version).second);
            for (const TileVersion& v : lc->changed) CHECK(changed.emplace(v.tile, v.version).second);
            for (const TileKey& t : lc->removed) CHECK(removed.insert(t).second);
        }
        const auto old_it = before.find(layer);
        std::map<TileKey, Held> old_tiles = old_it == before.end() ? std::map<TileKey, Held>{} : old_it->second;
        std::set<TileKey> now;
        for (const TileKey& t : c.tiles(layer)) now.insert(t);
        std::set<TileKey> all = now;
        for (const auto& [t, h] : old_tiles) all.insert(t);
        for (const TileKey& t : all) {
            INFO("tile " << t.i << "," << t.j);
            const bool was = old_tiles.count(t) > 0, is = now.count(t) > 0;
            if (!was && is) {
                CHECK(added.count(t) == 1);
                CHECK(added[t] == cl.world_version);
                CHECK(c.tile_version(layer, t) == cl.world_version);
            } else if (was && !is) {
                CHECK(removed.count(t) == 1);
            } else {
                const auto cur = c.tile<char>(layer, t);
                const bool differs = !same_snapshot(layer, old_tiles[t].data.get(), cur.get());
                CHECK(changed.count(t) == (differs ? 1u : 0u));
                if (differs) {
                    CHECK(changed[t] == cl.world_version);
                    CHECK(c.tile_version(layer, t) > old_tiles[t].version);
                } else {
                    CHECK(c.tile_version(layer, t) == old_tiles[t].version);
                    CHECK(cur.get() == old_tiles[t].data.get());  // the old snapshot object is kept
                }
            }
            if (!(was && is)) {
                CHECK(changed.count(t) == 0);
            }
            if (is) CHECK(removed.count(t) == 0);
            if (was) CHECK(added.count(t) == 0);
        }
        CHECK(added.size() + changed.size() + removed.size() <= all.size());
    }
}

// Updates the cache, checks the change list, then compares against a cache built from scratch.
bool matches_fresh(Net& n, ChangeList* out = nullptr) {
    const Snap before = take(*n.cache);
    const ChangeList cl = n.cache->update();
    verify_changes(before, *n.cache, cl);
    WorldCache fresh(n.g, n.region, n.rules, n.cache->params());
    fresh.rebuild_all();
    const std::string d = compare_caches(*n.cache, fresh);
    if (!d.empty()) std::printf("[world_cache] MISMATCH: %s\n", d.c_str());
    if (out) *out = cl;
    return d.empty();
}

// ------------------------------------------------------------------ geometry helpers (as test_edit_loop.cpp)

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
// True when no two non-adjacent edges (straight node-to-node) cross or come within 15 m of each other, and no
// node is within 15 m of an edge it does not belong to. Street lights depend spatially on nearby junction
// polygons of unconnected roads, which no graph-hop widening covers (test_edit_loop.cpp header).
bool planar_and_apart(const RoadGraph& g) {
    for (const auto& [ia, ea] : g.edges()) {
        const Vec2 a0 = g.node(ea.a)->pos.xy(), a1 = g.node(ea.b)->pos.xy();
        for (const auto& [ib, eb] : g.edges()) {
            if (!(ia < ib)) continue;
            if (ea.a == eb.a || ea.a == eb.b || ea.b == eb.a || ea.b == eb.b) continue;
            const Vec2 b0 = g.node(eb.a)->pos.xy(), b1 = g.node(eb.b)->pos.xy();
            if (segments_cross(a0, a1, b0, b1)) return false;
            if (std::min({point_segment_distance(a0, b0, b1), point_segment_distance(a1, b0, b1), point_segment_distance(b0, a0, a1),
                          point_segment_distance(b1, a0, a1)}) < 15)
                return false;
        }
    }
    return true;
}

// A rows x cols grid of nodes `spacing` apart, every neighbour pair linked by a residential road.
std::map<std::pair<int, int>, NodeId> build_grid(Net& n, int cols, int rows, double spacing, double jitter, PortableRng* rng) {
    std::map<std::pair<int, int>, NodeId> at;
    std::map<std::pair<int, int>, Vec3> p;
    for (int j = 0; j < rows; ++j)
        for (int i = 0; i < cols; ++i)
            p[{i, j}] = {i * spacing + (rng ? rng->uniform(-jitter, jitter) : 0), j * spacing + (rng ? rng->uniform(-jitter, jitter) : 0), 0};
    const auto link = [&](std::pair<int, int> a, std::pair<int, int> b) {
        REQUIRE(n.road(p[a], p[b], "residential", at[a], at[b]));
        const Edge& e = n.g.edges().rbegin()->second;
        at[a] = e.a;
        at[b] = e.b;
    };
    for (int j = 0; j < rows; ++j)
        for (int i = 0; i < cols; ++i) {
            if (i > 0) link({i - 1, j}, {i, j});
            if (j > 0) link({i, j - 1}, {i, j});
        }
    return at;
}

double now_ms() {
    using namespace std::chrono;
    return duration<double, std::milli>(steady_clock::now().time_since_epoch()).count();
}

}  // namespace

// ================================================================== tests

TEST_CASE("world_cache: first update builds everything, an idle update does nothing") {
    Net n;
    REQUIRE(n.road({0, 0, 0}, {150, 0, 0}));
    REQUIRE(n.road({150, 0, 0}, {300, 40, 0}, "residential", n.g.edges().begin()->second.b));
    CHECK(n.cache->world_version() == 0);
    CHECK(n.cache->layer_names() == std::vector<std::string>{"junctions", "road_mesh", "markings", "street_lights"});
    const ChangeList first = n.cache->update();
    CHECK(first.world_version == 1);
    REQUIRE(first.find("road_mesh") != nullptr);
    CHECK_FALSE(first.find("road_mesh")->added.empty());
    CHECK(first.find("road_mesh")->changed.empty());
    CHECK_FALSE(first.find("markings")->added.empty());
    CHECK_FALSE(first.find("street_lights")->added.empty());
    CHECK(first.find("junctions")->objects.everything);
    CHECK(n.g.dirty_nodes().empty());  // the cache is the dirty set's consumer
    CHECK(n.g.dirty_edges().empty());
    const ChangeList idle = n.cache->update();
    CHECK(idle.layers.empty());
    CHECK(idle.world_version == 1);
    CHECK(n.cache->world_version() == 1);
    CHECK(n.cache->road_mesh(n.cache->tiles("road_mesh").front()) != nullptr);
    CHECK(n.cache->road_mesh({9999, 9999}) == nullptr);
    CHECK(n.cache->tile_version("road_mesh", {9999, 9999}) == 0);
}

TEST_CASE("world_cache: the full build equals the library's one-shot calls") {
    Net n;
    std::string err;
    REQUIRE(build_test_layout(TestLayout::Interchange, n.g, n.st, *n.region, err));
    n.cache->update();
    JunctionSet js = build_plain_junctions(n.g, *n.region, {});
    RoundaboutResult rr = apply_roundabouts(js, n.g, *n.region, {});
    REQUIRE(js.junctions.size() == n.cache->junctions().junctions.size());
    for (const auto& [id, j] : js.junctions) {
        const Junction* c = n.cache->junctions().find(id);
        REQUIRE(c != nullptr);
        CHECK(same_junction(j, *c));
    }
    REQUIRE(rr.layouts.size() == n.cache->roundabouts().size());
    CHECK(rr.layouts.size() == 1);
    // Mesh: the same chunks as build_mesh_chunks over the whole graph.
    const WorldCacheParams& P = n.cache->params();
    ChunkParams cp;
    cp.tile_size = P.tile_size;
    const auto chunks = build_mesh_chunks(n.g, *n.region, js, &rr.layouts, P.mesh, cp);
    CHECK(chunks.size() == n.cache->tiles("road_mesh").size());
    for (const auto& [t, ch] : chunks) {
        const auto c = n.cache->road_mesh(t);
        REQUIRE(c != nullptr);
        CHECK(same_content(ch, *c));
    }
    // Lights: the same number as generate_street_lights.
    std::size_t lights = 0;
    for (const TileKey& t : n.cache->tiles("street_lights")) lights += n.cache->street_lights(t)->lights.size();
    CHECK(lights == generate_street_lights(n.g, *n.region, js, &rr.layouts).merged().size());
    CHECK(lights > 0);
    // Markings: every mark of the one-shot generator lands in a tile (triangulable marks only), colours match.
    MarkingParams mp = P.markings;
    mp.roundabouts = &rr;
    const MarkingSet set = generate_markings(n.g, *n.region, *n.rules, js, mp).merged();
    std::size_t tris = 0, drawable = 0;
    for (const Marking& m : set.marks) {
        const auto t = triangulate_marking(set, m);
        if (!t.empty()) ++drawable;
        tris += t.size() / 3;
    }
    std::size_t cache_tris = 0;
    for (const TileKey& t : n.cache->tiles("markings"))
        for (const auto& [cid, b] : n.cache->markings(t)->colours) cache_tris += b.indices.size() / 3;
    CHECK(drawable > 0);
    CHECK(cache_tris == tris);
}

TEST_CASE("world_cache: scripted edits (draw, delete, move, split, change type, undo, redo) match a full rebuild") {
    Net n;
    REQUIRE(n.road({0, 0, 0}, {150, 0, 0}));
    const NodeId mid = n.g.edges().begin()->second.b;
    REQUIRE(n.road({150, 0, 0}, {300, 30, 0}, "residential", mid));
    REQUIRE(n.road({150, 0, 0}, {150, 160, 0}, "residential", mid));
    CHECK(matches_fresh(n));  // initial build, nothing to compare changes against

    // change road type, then undo and redo
    const EdgeId e0 = n.g.edges().begin()->first;
    REQUIRE(n.st.execute(make_change_road_type(e0, "b_road")));
    CHECK(matches_fresh(n));
    REQUIRE(n.st.undo());
    CHECK(matches_fresh(n));
    REQUIRE(n.st.redo());
    CHECK(matches_fresh(n));

    // move a node
    REQUIRE(n.st.execute(make_move_node(mid, {160, 12, 0})));
    CHECK(matches_fresh(n));
    REQUIRE(n.st.undo());
    CHECK(matches_fresh(n));

    // draw a road ending on a split of an existing edge
    {
        const EdgeId e = n.g.edges().rbegin()->first;
        DrawRoadSpec s;
        s.road_type = "main_street";
        s.start.split_edge = e;
        s.start.split_s = n.g.edge(e)->horizontal.length() * 0.5;
        s.end.pos = {-100, 150, 0};
        REQUIRE(n.st.execute(make_draw_road(s)));
        CHECK(matches_fresh(n));
        REQUIRE(n.st.undo());
        CHECK(matches_fresh(n));
        REQUIRE(n.st.redo());
        CHECK(matches_fresh(n));
    }

    // delete an edge (leaves nodes behind), undo, redo
    REQUIRE(n.st.execute(make_delete_edge(n.g.edges().rbegin()->first)));
    CHECK(matches_fresh(n));
    REQUIRE(n.st.undo());
    CHECK(matches_fresh(n));
    REQUIRE(n.st.redo());
    CHECK(matches_fresh(n));
    while (n.st.can_undo()) {
        REQUIRE(n.st.undo());
        CHECK(matches_fresh(n));
    }
    CHECK(n.g.edges().empty());
    CHECK(n.cache->tiles("road_mesh").empty());
    while (n.st.can_redo()) {
        REQUIRE(n.st.redo());
        CHECK(matches_fresh(n));
    }
}

TEST_CASE("world_cache: interchange layout with merges, bridge and roundabout; ring edits and undo") {
    Net n;
    std::string err;
    REQUIRE(build_test_layout(TestLayout::Interchange, n.g, n.st, *n.region, err));
    CHECK(matches_fresh(n));
    REQUIRE(n.cache->roundabouts().size() == 1);
    const RoundaboutLayout ring = n.cache->roundabouts()[0];
    REQUIRE(ring.ok);

    // Break the ring (delete a ring edge): the other ring nodes must lose their Roundabout junctions.
    REQUIRE(n.st.execute(make_delete_edge(ring.group.ring_edges[0])));
    CHECK(matches_fresh(n));
    CHECK(n.cache->roundabouts().empty());
    for (const auto& [id, j] : n.cache->junctions().junctions) CHECK(j.kind == JunctionKind::Plain);
    REQUIRE(n.st.undo());
    CHECK(matches_fresh(n));
    CHECK(n.cache->roundabouts().size() == 1);

    // Move a ring node, change a ring edge's type, move an approach node.
    REQUIRE(n.st.execute(make_move_node(ring.group.nodes[1], n.g.node(ring.group.nodes[1])->pos)));  // same place: a no-op move
    CHECK(matches_fresh(n));
    const Vec3 q = n.g.node(ring.group.nodes[2])->pos;
    REQUIRE(n.st.execute(make_move_node(ring.group.nodes[2], {q.x + 1.5, q.y - 1.0, q.z})));
    CHECK(matches_fresh(n));
    REQUIRE(n.st.undo());
    CHECK(matches_fresh(n));
    REQUIRE(n.st.execute(make_change_road_type(ring.group.ring_edges[1], "b_road")));
    CHECK(matches_fresh(n));
    REQUIRE(n.st.undo());
    CHECK(matches_fresh(n));

    // Edit the dual carriageway / bridge: change the type of every edge once, undo each.
    std::vector<EdgeId> ids;
    for (const auto& [id, e] : n.g.edges()) ids.push_back(id);
    for (EdgeId id : ids) {
        if (std::find(ring.group.ring_edges.begin(), ring.group.ring_edges.end(), id) != ring.group.ring_edges.end()) continue;
        if (!n.st.execute(make_change_road_type(id, n.g.edge(id)->road_type == "b_road" ? "residential" : "b_road"))) continue;
        CHECK(matches_fresh(n));
        REQUIRE(n.st.undo());
        CHECK(matches_fresh(n));
    }
    // Place a second roundabout through the command, undo, redo.
    REQUIRE(n.road({400, 0, 0}, {700, 0, 0}));
    CHECK(matches_fresh(n));
    REQUIRE(n.st.execute(make_place_roundabout({550, 0, 0}, 20, default_ring_road_type(*n.region), *n.region)));
    CHECK(matches_fresh(n));
    CHECK(n.cache->roundabouts().size() == 2);
    REQUIRE(n.st.undo());
    CHECK(matches_fresh(n));
    CHECK(n.cache->roundabouts().size() == 1);
    REQUIRE(n.st.redo());
    CHECK(matches_fresh(n));
}

TEST_CASE("world_cache: fuzz, planar roads at least 15 m apart (all layers incl. street lights), several seeds") {
    for (std::uint64_t seed : {1ull, 2ull, 3ull, 4ull, 5ull, 6ull}) {
        INFO("seed " << seed);
        PortableRng rng{seed};
        Net n;
        build_grid(n, 3, 3, 70.0, 5.0, &rng);
        REQUIRE(matches_fresh(n));
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
                if (rng.uniform(0, 1) < 0.7) n.st.undo();
                else if (n.st.can_redo()) n.st.redo();
            }
            if (changed && !planar_and_apart(n.g)) n.st.undo();
            if (rng.uniform(0, 1) < 0.15) n.st.redo();  // a failed planarity check leaves a redo available
            INFO("step " << step);
            REQUIRE(matches_fresh(n));
        }
    }
}

TEST_CASE("world_cache: fuzz without the planarity restriction (street lights off): junctions, mesh, markings still match") {
    for (std::uint64_t seed : {11ull, 12ull, 13ull, 14ull}) {
        INFO("seed " << seed);
        PortableRng rng{seed};
        WorldCacheParams P;
        P.generate_street_lights = false;
        Net n(P);
        build_grid(n, 3, 3, 70.0, 5.0, &rng);
        REQUIRE(matches_fresh(n));
        const char* types[] = {"residential", "b_road", "main_street", "country_lane"};
        const auto pick = [&](std::size_t count) { return static_cast<std::size_t>(rng.uniform(0, 1) * static_cast<double>(count)) % count; };
        for (int step = 0; step < 40; ++step) {
            std::vector<NodeId> ids;
            for (const auto& [id, nd] : n.g.nodes()) ids.push_back(id);
            std::vector<EdgeId> eids;
            for (const auto& [id, e] : n.g.edges()) eids.push_back(id);
            const double r = rng.uniform(0, 1);
            if (r < 0.25 && ids.size() >= 2) {
                const NodeId a = ids[pick(ids.size())], b = ids[pick(ids.size())];
                if (a != b) n.road(n.g.node(a)->pos, n.g.node(b)->pos, types[pick(4)], a, b);
            } else if (r < 0.35 && !eids.empty()) {
                n.st.execute(make_delete_edge(eids[pick(eids.size())]));
            } else if (r < 0.6 && !ids.empty()) {
                const NodeId id = ids[pick(ids.size())];
                const Vec3 q = n.g.node(id)->pos;
                n.st.execute(make_move_node(id, {q.x + rng.uniform(-25, 25), q.y + rng.uniform(-25, 25), 0}));
            } else if (r < 0.75 && !eids.empty()) {
                n.st.execute(make_change_road_type(eids[pick(eids.size())], types[pick(4)]));
            } else if (r < 0.85 && !eids.empty()) {
                const EdgeId e = eids[pick(eids.size())];
                const double len = n.g.edge(e)->horizontal.length();
                const double s_split = len * rng.uniform(0.3, 0.7);
                const Vec3 from = edge_point(*n.g.edge(e), s_split);
                DrawRoadSpec s;
                s.road_type = types[pick(4)];
                s.start.split_edge = e;
                s.start.split_s = s_split;
                s.end.pos = {from.x + rng.uniform(-90, 90), from.y + rng.uniform(-90, 90), 0};
                n.st.execute(make_draw_road(s));
            } else if (n.st.can_undo()) {
                n.st.undo();
            }
            INFO("step " << step);
            REQUIRE(matches_fresh(n));
        }
    }
}

TEST_CASE("world_cache: change lists are minimal-ish (corner edit on a large grid) and complete") {
    Net n;
    const auto at = build_grid(n, 6, 6, 250.0, 0.0, nullptr);  // 1.25 km square, ~25 mesh tiles
    n.cache->update();
    const std::size_t total_mesh = n.cache->tiles("road_mesh").size();
    REQUIRE(total_mesh >= 25);
    const NodeId corner = at.at({0, 0});

    const Snap before = take(*n.cache);
    REQUIRE(n.st.execute(make_move_node(corner, {18, 11, 0})));
    const ChangeList cl = n.cache->update();
    verify_changes(before, *n.cache, cl);
    // The edit touches the two edges at the corner (x,y in 0..250) and the three junctions next to them.
    // Everything reported must lie within the 2x2 tile block around them (tiles -1..2 on each axis is already
    // generous: 30 m margins either side of 0..500 m).
    std::size_t reported = 0;
    for (const LayerChanges& lc : cl.layers) {
        for (const TileVersion& v : lc.added) {
            CHECK(v.tile.i >= -1);
            CHECK(v.tile.i <= 2);
            CHECK(v.tile.j >= -1);
            CHECK(v.tile.j <= 2);
        }
        for (const TileVersion& v : lc.changed) {
            CHECK(v.tile.i >= -1);
            CHECK(v.tile.i <= 2);
            CHECK(v.tile.j >= -1);
            CHECK(v.tile.j <= 2);
            if (lc.layer == "road_mesh") ++reported;
        }
        for (const TileKey& t : lc.removed) {
            CHECK(t.i <= 2);
            CHECK(t.j <= 2);
        }
    }
    CHECK(reported >= 1);
    CHECK(reported <= 9);
    CHECK(reported * 3 <= total_mesh);
    const LayerChanges* mesh = cl.find("road_mesh");
    REQUIRE(mesh != nullptr);
    std::printf("[world_cache] corner edit on 6x6 grid: %zu of %zu mesh tiles reported (markings %zu, lights %zu changed)\n", reported,
                total_mesh, cl.find("markings") ? cl.find("markings")->changed.size() : 0u,
                cl.find("street_lights") ? cl.find("street_lights")->changed.size() : 0u);

    // Junction layer: only the corner and its two neighbours are rebuilt (plus nothing else).
    const LayerChanges* jl = cl.find("junctions");
    REQUIRE(jl != nullptr);
    CHECK(jl->objects.nodes.size() <= 3);
    CHECK(jl->objects.nodes.count(corner) == 1);

    // And the same edit's result equals a full rebuild.
    WorldCache fresh(n.g, n.region, n.rules, n.cache->params());
    fresh.rebuild_all();
    CHECK(compare_caches(*n.cache, fresh) == "");

    // A no-op rebuild_all reports no tile changes: rebuild is deterministic.
    const ChangeList again = n.cache->rebuild_all();
    CHECK(again.tiles_empty());
    CHECK(again.world_version == cl.world_version + 1);
}

TEST_CASE("world_cache: versions strictly increase per tile, also across removal and re-adding") {
    Net n;
    REQUIRE(n.road({0, 0, 0}, {120, 0, 0}));
    const auto tiles_version = [&] { return n.cache->tile_version("road_mesh", {0, 0}); };
    n.cache->update();
    const std::uint64_t v1 = tiles_version();
    CHECK(v1 == 1);
    REQUIRE(n.st.undo());  // road gone: tile removed
    const ChangeList removed = n.cache->update();
    REQUIRE(removed.find("road_mesh") != nullptr);
    CHECK(removed.find("road_mesh")->removed.size() >= 1);
    CHECK(tiles_version() == 0);
    REQUIRE(n.st.redo());
    const ChangeList added = n.cache->update();
    REQUIRE(added.find("road_mesh") != nullptr);
    CHECK_FALSE(added.find("road_mesh")->added.empty());
    CHECK(tiles_version() > v1);
    const std::uint64_t v3 = tiles_version();
    REQUIRE(n.st.execute(make_change_road_type(n.g.edges().begin()->first, "b_road")));
    n.cache->update();
    CHECK(tiles_version() > v3);
    // An edit far away leaves the tile's version alone.
    const std::uint64_t v4 = tiles_version();
    REQUIRE(n.road({5000, 5000, 0}, {5120, 5000, 0}));
    n.cache->update();
    CHECK(tiles_version() == v4);
    CHECK(n.cache->tile_version("road_mesh", {19, 19}) > v4);
}

TEST_CASE("world_cache: published snapshots are immutable and survive later updates") {
    Net n;
    REQUIRE(n.road({0, 0, 0}, {120, 0, 0}));
    REQUIRE(n.road({120, 0, 0}, {240, 60, 0}, "residential", n.g.edges().begin()->second.b));
    n.cache->update();
    const TileKey t{0, 0};
    const auto held_mesh = n.cache->road_mesh(t);
    const auto held_marks = n.cache->markings(t);
    const auto held_lights = n.cache->street_lights(t);
    REQUIRE(held_mesh);
    REQUIRE(held_marks);
    REQUIRE(held_lights);
    const MeshChunk mesh_copy = *held_mesh;
    const MarkingChunk marks_copy = *held_marks;
    const StreetLightGroup lights_copy = *held_lights;
    for (int i = 0; i < 4; ++i) {
        REQUIRE(n.st.execute(make_move_node(n.g.edges().begin()->second.b, {120 + 4.0 * (i + 1), 3.0 * (i + 1), 0})));
        n.cache->update();
    }
    CHECK(n.cache->road_mesh(t).get() != held_mesh.get());  // replaced, not mutated
    CHECK(same_content(mesh_copy, *held_mesh));
    CHECK(same_content(marks_copy, *held_marks));
    CHECK(same_content(lights_copy, *held_lights));
    CHECK_FALSE(same_content(mesh_copy, *n.cache->road_mesh(t)));
    CHECK(held_mesh.use_count() >= 1);
}

TEST_CASE("world_cache: no marking rules means no markings layer; layers can be switched off") {
    Net n({}, /*with_rules=*/false);
    REQUIRE(n.road({0, 0, 0}, {120, 0, 0}));
    CHECK(n.cache->layer_names() == std::vector<std::string>{"junctions", "road_mesh", "street_lights"});
    n.cache->update();
    CHECK(n.cache->markings({0, 0}) == nullptr);
    CHECK(n.cache->tiles("markings").empty());
    CHECK(matches_fresh(n));
    WorldCacheParams P;
    P.generate_street_lights = false;
    P.generate_markings = false;
    Net m(P);
    CHECK(m.cache->layer_names() == std::vector<std::string>{"junctions", "road_mesh"});
}

TEST_CASE("world_cache: junction overrides schedule a full rebuild that equals a fresh cache with them") {
    Net n;
    REQUIRE(n.road({-150, 0, 0}, {0, 0, 0}));
    const NodeId c = n.g.edges().begin()->second.b;
    REQUIRE(n.road({0, 0, 0}, {150, 0, 0}, "residential", c));
    REQUIRE(n.road({0, 0, 0}, {0, 150, 0}, "residential", c));
    n.cache->update();
    const Junction* j = n.cache->junctions().find(c);
    REQUIRE(j != nullptr);
    REQUIRE_FALSE(j->corners.empty());
    JunctionOverrides ov;
    ov.corner_radius[{c, j->corners[0].edge_l, j->corners[0].edge_r}] = 2.0;
    const std::vector<Vec3> before = j->corners[0].kerb;
    n.cache->set_junction_overrides(ov);
    const ChangeList cl = n.cache->update();
    REQUIRE(cl.find("junctions") != nullptr);
    CHECK(cl.find("junctions")->objects.everything);
    CHECK_FALSE(cl.find("road_mesh")->changed.empty());
    CHECK_FALSE(same_v3s(before, n.cache->junctions().find(c)->corners[0].kerb));
    CHECK(matches_fresh(n));
}

// ------------------------------------------------------------------ plug-in layer test double

namespace {

// A complete example of a new layer: per-tile count of junctions (stands in for "vegetation near junctions",
// "kit instances at nodes", ...). It reads the junction layer's state and change report, owns a TileStore, and
// is registered with add_layer(): none of the four built-in layers is touched.
class JunctionCountLayer final : public WorldLayer {
public:
    explicit JunctionCountLayer(double tile_size) : ts_(tile_size) {}
    std::string_view name() const override { return "junction_count"; }
    std::vector<std::string> reads() const override { return {"junctions"}; }
    std::vector<TileKey> tiles() const override {
        std::vector<TileKey> t;
        for (const auto& [k, e] : store_.entries()) t.push_back(k);
        return t;
    }
    std::shared_ptr<const void> tile_data(TileKey t) const override { return store_.get(t); }
    std::uint64_t tile_version(TileKey t) const override { return store_.version(t); }

    void rebuild_all(const LayerContext& ctx, LayerChanges& out) override {
        where_.clear();
        for (const auto& [id, j] : ctx.state<JunctionState>("junctions")->junctions.junctions) where_[id] = tile_of(j.pos.xy(), ts_);
        std::map<TileKey, int> next;
        for (const auto& [id, t] : where_) ++next[t];
        store_.commit_all(next, ctx.world_version(), out, [](int a, int b) { return a == b; });
        ++full_runs;
    }
    void update(const LayerContext& ctx, const ObjectChanges&, LayerChanges& out) override {
        const ObjectChanges& rj = *ctx.changes_of("junctions");  // guaranteed to have run: we declared reads()
        const JunctionState& js = *ctx.state<JunctionState>("junctions");
        std::set<TileKey> cand;
        for (NodeId n : rj.nodes) {
            if (const auto it = where_.find(n); it != where_.end()) {
                cand.insert(it->second);
                where_.erase(it);
            }
            if (const Junction* j = js.junctions.find(n)) {
                where_[n] = tile_of(j->pos.xy(), ts_);
                cand.insert(where_[n]);
            }
        }
        std::map<TileKey, int> next;
        for (const auto& [id, t] : where_)
            if (cand.count(t)) ++next[t];
        store_.commit(cand, next, ctx.world_version(), out, [](int a, int b) { return a == b; });
        ++incremental_runs;
    }
    int full_runs = 0, incremental_runs = 0;

private:
    double ts_;
    std::map<NodeId, TileKey> where_;
    TileStore<int> store_;
};

// Declares a dependency on a layer that does not exist.
class OrphanLayer final : public WorldLayer {
public:
    std::string_view name() const override { return "orphan"; }
    std::vector<std::string> reads() const override { return {"ground"}; }
    void rebuild_all(const LayerContext&, LayerChanges&) override {}
    void update(const LayerContext&, const ObjectChanges&, LayerChanges&) override {}
};

}  // namespace

TEST_CASE("world_cache: a new layer plugs in without touching the built-in ones") {
    Net n;
    // Registration rules.
    std::string err;
    CHECK_FALSE(n.cache->add_layer(std::make_unique<OrphanLayer>(), &err));
    CHECK(err.find("ground") != std::string::npos);
    CHECK_FALSE(n.cache->add_layer(nullptr, &err));

    auto owned = std::make_unique<JunctionCountLayer>(n.cache->params().tile_size);
    JunctionCountLayer* layer = owned.get();
    // Added after the cache was built once: the next update builds it from scratch.
    const auto at = build_grid(n, 4, 4, 130.0, 0.0, nullptr);
    CHECK(matches_fresh(n));
    REQUIRE(n.cache->add_layer(std::move(owned), &err));
    CHECK_FALSE(n.cache->add_layer(std::make_unique<JunctionCountLayer>(256.0), &err));  // duplicate name
    CHECK(n.cache->layer_names().back() == "junction_count");
    REQUIRE(n.cache->tiles("junction_count").empty());
    const ChangeList first = n.cache->update();  // no graph change, but the new layer is pending
    CHECK(layer->full_runs == 1);
    REQUIRE(first.find("junction_count") != nullptr);
    CHECK_FALSE(first.find("junction_count")->added.empty());
    CHECK(first.find("road_mesh") == nullptr);  // built-in layers were not rebuilt for it

    const auto expected = [&] {
        std::map<TileKey, int> e;
        for (const auto& [id, j] : n.cache->junctions().junctions) ++e[tile_of(j.pos.xy(), 256.0)];
        return e;
    };
    const auto check_counts = [&] {
        const std::map<TileKey, int> e = expected();
        std::vector<TileKey> tiles = n.cache->tiles("junction_count");
        CHECK(tiles.size() == e.size());
        for (const auto& [t, count] : e) {
            const auto v = n.cache->tile<int>("junction_count", t);
            REQUIRE(v);
            CHECK(*v == count);
        }
    };
    check_counts();

    // Edits flow through it incrementally and the built-in layers still equal a full rebuild.
    REQUIRE(n.st.execute(make_move_node(at.at({1, 1}), {190, 150, 0})));
    ChangeList cl;
    CHECK(matches_fresh(n, &cl));
    CHECK(layer->incremental_runs >= 1);
    check_counts();
    REQUIRE(n.st.execute(make_delete_edge(n.g.edges().begin()->first)));
    CHECK(matches_fresh(n));
    check_counts();
    while (n.st.can_undo()) {
        REQUIRE(n.st.undo());
        CHECK(matches_fresh(n));
    }
    check_counts();
    CHECK(layer->full_runs == 1);  // never rebuilt from scratch after its first build
    // rebuild_all reaches it too and reports no difference.
    const ChangeList again = n.cache->rebuild_all();
    CHECK(layer->full_runs == 2);
    CHECK(again.tiles_empty());
}

// ------------------------------------------------------------------ timing

TEST_CASE("world_cache: timing, incremental update vs full rebuild") {
    // The largest built-in test layout (interchange), then a 12x12 grid.
    {
        Net n;
        std::string err;
        REQUIRE(build_test_layout(TestLayout::Interchange, n.g, n.st, *n.region, err));
        const double t0 = now_ms();
        n.cache->update();
        const double build_ms = now_ms() - t0;
        std::vector<NodeId> ids;
        for (const auto& [id, nd] : n.g.nodes()) ids.push_back(id);
        std::vector<double> inc, full;
        PortableRng rng{5};
        for (int i = 0; i < 8; ++i) {
            const NodeId id = ids[static_cast<std::size_t>(rng.uniform(0, 1) * static_cast<double>(ids.size())) % ids.size()];
            const Vec3 q = n.g.node(id)->pos;
            if (!n.st.execute(make_move_node(id, {q.x + rng.uniform(-3, 3), q.y + rng.uniform(-3, 3), q.z}))) continue;
            double a = now_ms();
            n.cache->update();
            inc.push_back(now_ms() - a);
            a = now_ms();
            WorldCache fresh(n.g, n.region, n.rules, n.cache->params());
            fresh.rebuild_all();
            full.push_back(now_ms() - a);
        }
        std::sort(inc.begin(), inc.end());
        std::sort(full.begin(), full.end());
        REQUIRE_FALSE(inc.empty());
        std::printf("[world_cache] interchange (%zu nodes, %zu edges, %zu mesh tiles): first build %.1f ms; node-move update median %.1f ms vs full rebuild median %.1f ms\n",
                    n.g.nodes().size(), n.g.edges().size(), n.cache->tiles("road_mesh").size(), build_ms, inc[inc.size() / 2],
                    full[full.size() / 2]);
    }
    {
        Net n;
        const auto at = build_grid(n, 12, 12, 120.0, 0.0, nullptr);
        const double t0 = now_ms();
        n.cache->update();
        const double build_ms = now_ms() - t0;
        std::vector<double> inc, full;
        PortableRng rng{9};
        for (int i = 0; i < 8; ++i) {
            const NodeId id = at.at({4 + (i % 4), 4 + (i / 4)});
            const Vec3 q = n.g.node(id)->pos;
            REQUIRE(n.st.execute(make_move_node(id, {q.x + rng.uniform(-6, 6), q.y + rng.uniform(-6, 6), 0})));
            double a = now_ms();
            n.cache->update();
            inc.push_back(now_ms() - a);
            a = now_ms();
            WorldCache fresh(n.g, n.region, n.rules, n.cache->params());
            fresh.rebuild_all();
            full.push_back(now_ms() - a);
        }
        std::sort(inc.begin(), inc.end());
        std::sort(full.begin(), full.end());
        std::printf("[world_cache] 12x12 grid (%zu nodes, %zu edges, %zu mesh tiles): first build %.1f ms; node-move update median %.1f ms vs full rebuild median %.1f ms\n",
                    n.g.nodes().size(), n.g.edges().size(), n.cache->tiles("road_mesh").size(), build_ms, inc[inc.size() / 2],
                    full[full.size() / 2]);
        CHECK(inc[inc.size() / 2] < full[full.size() / 2]);
    }
}
