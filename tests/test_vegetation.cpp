// Vegetation placement (sim_core/src/vegetation.cpp, include/cp/vegetation.h): world cover, road-edge scatter,
// the corridor mask, determinism, both driving sides, incremental == full.
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "cp/lanes.h"
#include "cp/test_layouts.h"
#include "cp/vegetation.h"
#include "cp/world_cache.h"
#include "doctest/doctest.h"
#include "portable_rng.h"

using namespace cp;

namespace {

// ------------------------------------------------------------------ fixture

struct Net {
    std::shared_ptr<const RegionPack> region;
    std::shared_ptr<const VegetationRules> rules;
    RoadGraph g;
    CommandStack<RoadGraph> st{g};
    std::unique_ptr<WorldCache> cache;
    VegetationParams vp;
    WorldCacheParams wp;

    explicit Net(const char* id = "uk", const std::function<void(RegionPack&)>& tweak_region = {},
                 const std::function<void(VegetationRules&)>& tweak_rules = {}, double tile_size = 256.0,
                 Vec2 wmin = {-256, -256}, Vec2 wmax = {512, 512}) {
        auto p = load_region_pack_by_id(CP_DATA_DIR, id);
        REQUIRE_MESSAGE(p, p.error);
        if (tweak_region) tweak_region(*p.value);
        region = std::make_shared<const RegionPack>(*p);
        g.set_region(region);
        auto r = load_vegetation_rules_by_id(CP_DATA_DIR, id);
        REQUIRE_MESSAGE(r, r.error);
        if (tweak_rules) tweak_rules(*r.value);
        rules = std::make_shared<const VegetationRules>(*r);
        wp.tile_size = tile_size;
        vp.world_min = wmin;
        vp.world_max = wmax;
        cache = std::make_unique<WorldCache>(g, region, nullptr, wp);
        std::string err;
        REQUIRE_MESSAGE(add_vegetation_layers(*cache, rules, vp, &err), err);
    }

    bool road(Vec3 a, Vec3 b, const char* type = "b_road", NodeId na = {}, NodeId nb = {}, bool one_way = false) {
        DrawRoadSpec s;
        s.road_type = type;
        s.one_way = one_way;
        s.start.pos = a;
        s.end.pos = b;
        s.start.existing_node = na;
        s.end.existing_node = nb;
        return st.execute(make_draw_road(s));
    }
};

using TileMap = std::map<TileKey, std::shared_ptr<const VegetationTile>>;

TileMap tiles_of(const WorldCache& c, std::string_view layer) {
    TileMap m;
    for (const TileKey& t : c.tiles(layer)) m[t] = c.tile<VegetationTile>(layer, t);
    return m;
}

std::string compare_veg(const WorldCache& a, const WorldCache& b) {
    for (std::string_view layer : {kLayerWorldCover, kLayerVegetation}) {
        std::vector<TileKey> ta = a.tiles(layer), tb = b.tiles(layer);
        std::sort(ta.begin(), ta.end());
        std::sort(tb.begin(), tb.end());
        if (ta != tb) return std::string(layer) + ": tile sets differ (" + std::to_string(ta.size()) + " vs " + std::to_string(tb.size()) + ")";
        for (const TileKey& t : ta) {
            const auto x = a.tile<VegetationTile>(layer, t), y = b.tile<VegetationTile>(layer, t);
            if (!x || !y || !same_content(*x, *y)) return std::string(layer) + ": tile " + std::to_string(t.i) + "," + std::to_string(t.j) + " differs";
        }
    }
    return "";
}

// Updates and compares with a cache built from scratch.
bool matches_fresh(Net& n) {
    n.cache->update();
    WorldCache fresh(n.g, n.region, nullptr, n.wp);
    std::string err;
    REQUIRE_MESSAGE(add_vegetation_layers(fresh, n.rules, n.vp, &err), err);
    fresh.rebuild_all();
    const std::string d = compare_veg(*n.cache, fresh);
    if (!d.empty()) std::printf("[vegetation] MISMATCH: %s\n", d.c_str());
    return d.empty();
}

// ------------------------------------------------------------------ flattening

struct Flat {
    std::string type;
    double x = 0, y = 0, z = 0;
    std::uint64_t seed = 0;
    VegLayer layer = VegLayer::WorldCover;
    float scale = 1;
    std::uint8_t size_class = 0;
};

std::vector<Flat> instances_of(const WorldCache& c, std::string_view layer) {
    std::vector<Flat> out;
    for (const TileKey& t : c.tiles(layer)) {
        const auto vt = c.tile<VegetationTile>(layer, t);
        for (const VegInstanceGroup& g : vt->groups)
            for (const VegInstance& v : g.instances)
                out.push_back({g.type_id, vt->origin.x + static_cast<double>(v.pos.x), vt->origin.y + static_cast<double>(v.pos.y),
                               vt->origin.z + static_cast<double>(v.pos.z), v.seed, v.layer, v.scale, v.size_class});
    }
    return out;
}

Vec2 world2(const VegetationTile& t, const Float3& p) { return {t.origin.x + static_cast<double>(p.x), t.origin.y + static_cast<double>(p.y)}; }

bool in_polyset(const PolygonSet& ps, Vec2 p) {
    bool in = false;
    for (const auto& ring : ps) {
        bool r = false;
        for (std::size_t i = 0, j = ring.size() - 1; i < ring.size(); j = i++) {
            const Vec2 a = ring[i], b = ring[j];
            if ((a.y > p.y) != (b.y > p.y) && p.x < (b.x - a.x) * (p.y - a.y) / (b.y - a.y) + a.x) r = !r;
        }
        if (r) in = !in;
    }
    return in;
}
bool in_junction(const JunctionSet& js, Vec2 p) {
    for (const auto& [id, j] : js.junctions)
        if (in_polyset(j.carriageway, p) || in_polyset(j.pavements, p)) return true;
    return false;
}
bool in_island(const std::vector<RoundaboutLayout>& rb, Vec2 p) {
    for (const RoundaboutLayout& l : rb) {
        if (in_polyset(l.island, p) || in_polyset(l.apron, p)) return true;
        for (const SplitterIsland& s : l.splitters)
            if (in_polyset(PolygonSet{s.polygon}, p)) return true;
    }
    return false;
}

double carriageway_half(const RegionPack& r, const Edge& e) {
    const RoadType* rt = r.road_type(e.road_type);
    double m = 0;
    for (const Lane& l : instantiate_lanes(e, *rt, r.driving_side)) {
        const LaneType* lt = r.lane_type(l.lane_type);
        if (!lt || lt->profile == "carriageway") m = std::max(m, std::abs(l.offset) + l.width / 2);
    }
    return m;
}

double total_km(const RoadGraph& g) {
    double s = 0;
    for (const auto& [id, e] : g.edges()) s += e.horizontal.length();
    return s / 1000.0;
}

StreetTreeRule test_trees(double spacing, double setback) {
    StreetTreeRule t;
    t.enabled = true;
    t.spacing = spacing;
    t.spacing_jitter = 0.2;
    t.setback = setback;
    t.offset_from_back = 1.0;
    t.lateral_jitter = 0.3;
    t.probability = 1.0;
    t.classes = {{"tree_broadleaf_round", 1.0}};
    return t;
}

}  // namespace

// ================================================================== tests

TEST_CASE("vegetation: region data loads for uk and testland and the packs differ") {
    auto uk = load_vegetation_rules_by_id(CP_DATA_DIR, "uk");
    auto tl = load_vegetation_rules_by_id(CP_DATA_DIR, "testland");
    REQUIRE_MESSAGE(uk, uk.error);
    REQUIRE_MESSAGE(tl, tl.error);
    CHECK(uk->types.size() >= 12);
    CHECK(uk->type("tree_broadleaf_round") != nullptr);
    CHECK(uk->type("weed_creep") != nullptr);
    CHECK(uk->clear_margin == doctest::Approx(1.5));
    CHECK(uk->clear_margin != tl->clear_margin);
    CHECK(uk->blend_band != tl->blend_band);
    CHECK(uk->world_cover.hedge_share != tl->world_cover.hedge_share);
    CHECK(uk->world_cover.woodland_share != tl->world_cover.woodland_share);
    CHECK(uk->for_road("a_road_rural").verge.width != tl->for_road("rural_trunk").verge.width);
    CHECK(uk->sight.distance_for(10.0) != tl->sight.distance_for(10.0));
    CHECK(uk->reach() > 20);
    // The region pack itself still loads with the extra key.
    CHECK(load_region_pack_by_id(CP_DATA_DIR, "uk"));
    CHECK(load_region_pack_by_id(CP_DATA_DIR, "testland"));
    // Errors.
    CHECK_FALSE(parse_vegetation_rules("{}"));
    CHECK_FALSE(parse_vegetation_rules("not json"));
    const auto bad = parse_vegetation_rules(R"({"vegetation":{"types":[],"island":{"grass_type":"nope"}}})");
    CHECK_FALSE(bad);
    CHECK(bad.error.find("nope") != std::string::npos);
}

TEST_CASE("vegetation: world cover exists with no roads at all") {
    Net n;
    n.cache->update();
    const TileMap wc = tiles_of(*n.cache, kLayerWorldCover);
    CHECK(wc.size() == 9);  // 3 x 3 tiles of the 768 m world
    CHECK(tiles_of(*n.cache, kLayerVegetation).empty());
    std::size_t trees = 0, hedges = 0, patches = 0;
    for (const auto& [k, t] : wc) {
        for (const auto& g : t->groups) trees += g.instances.size();
        hedges += t->hedges.size();
        for (const auto& g : t->patches) patches += g.patches.size();
    }
    std::printf("[vegetation] no roads: %zu tiles, %zu trees, %zu hedge runs, %zu ground patches\n", wc.size(), trees, hedges, patches);
    CHECK(trees > 50);
    CHECK(hedges > 3);
    CHECK(patches > 100);
    // Independent of roads and of tile size: a second cache with 128 m tiles holds the same trees.
    Net m("uk", {}, {}, 128.0);
    m.cache->update();
    auto a = instances_of(*n.cache, kLayerWorldCover);
    auto b = instances_of(*m.cache, kLayerWorldCover);
    REQUIRE(a.size() == b.size());
    std::map<std::uint64_t, Flat> by_seed;
    for (const Flat& f : a) by_seed[f.seed] = f;
    for (const Flat& f : b) {
        const auto it = by_seed.find(f.seed);
        REQUIRE(it != by_seed.end());
        CHECK(it->second.type == f.type);
        CHECK(std::abs(it->second.x - f.x) < 1e-2);
        CHECK(std::abs(it->second.y - f.y) < 1e-2);
    }
}

TEST_CASE("vegetation: drawing a road clears world cover in its corridor and only near it; deleting restores it exactly") {
    Net n;
    n.cache->update();
    const TileMap before = tiles_of(*n.cache, kLayerWorldCover);
    const double margin = n.rules->clear_margin, band = n.rules->blend_band;

    REQUIRE(n.road({-330, -280, 0}, {340, 300, 0}, "a_road_rural"));
    const ChangeList cl = n.cache->update();
    const LayerChanges* lc = cl.find(kLayerWorldCover);
    REQUIRE(lc != nullptr);
    CHECK_FALSE(lc->changed.empty());
    const CorridorModel cm = CorridorModel::build(n.g, *n.region, *n.rules, n.cache->junctions(), n.cache->roundabouts());
    const TileMap after = tiles_of(*n.cache, kLayerWorldCover);

    // Everything that is left lies outside the corridor (trees, hedges, patches).
    std::size_t near_cut = 0;
    for (const auto& [k, t] : after) {
        for (const auto& g : t->groups)
            for (const VegInstance& v : g.instances) CHECK(cm.signed_distance(world2(*t, v.pos)) > margin - 1e-6);
        for (const HedgeRun& h : t->hedges)
            for (const Float3& p : h.points) {
                const double sd = cm.signed_distance(world2(*t, p));
                CHECK(sd >= margin - 1e-3);
                if (sd < margin + 0.2) ++near_cut;
            }
        for (const auto& g : t->patches)
            for (const GroundPatch& p : g.patches) {
                const double sd = cm.signed_distance(world2(*t, p.centre));
                CHECK(sd > margin);
                CHECK(sd - static_cast<double>(p.half_size) * 1.4143 > -1e-6);  // the whole cell is outside the corridor
            }
    }
    std::printf("[vegetation] hedge vertices at the cut face: %zu\n", near_cut);
    CHECK(near_cut > 0);  // the road really cuts hedges

    // Whatever disappeared was within the blend band of the new corridor; nothing new appeared except finer patches.
    std::set<std::uint64_t> kept;
    for (const auto& [k, t] : after)
        for (const auto& g : t->groups)
            for (const VegInstance& v : g.instances) kept.insert(v.seed);
    std::size_t removed = 0;
    for (const auto& [k, t] : before)
        for (const auto& g : t->groups)
            for (const VegInstance& v : g.instances) {
                if (kept.count(v.seed)) continue;
                ++removed;
                CHECK(cm.signed_distance(world2(*t, v.pos)) < margin + band + 1e-6);
            }
    CHECK(removed > 0);
    std::set<std::uint64_t> was;
    for (const auto& [k, t] : before)
        for (const auto& g : t->groups)
            for (const VegInstance& v : g.instances) was.insert(v.seed);
    for (const std::uint64_t s : kept) CHECK(was.count(s) == 1);
    // Hedge vertices that vanished were within the cut margin (plus the 2 m sample step).
    std::set<std::pair<long long, long long>> kept_pts;
    const auto q = [](Vec2 p) { return std::make_pair(std::llround(p.x * 100), std::llround(p.y * 100)); };
    for (const auto& [k, t] : after)
        for (const HedgeRun& h : t->hedges)
            for (const Float3& p : h.points) kept_pts.insert(q(world2(*t, p)));
    for (const auto& [k, t] : before)
        for (const HedgeRun& h : t->hedges)
            for (const Float3& p : h.points) {
                const Vec2 w = world2(*t, p);
                if (!kept_pts.count(q(w))) CHECK(cm.signed_distance(w) < margin + 2.5);
            }
    // Tiles far from the road are untouched: same snapshot object and version.
    for (const auto& [k, t] : before) {
        const double cy = (static_cast<double>(k.j) + 0.5) * 256.0;
        if (std::abs(cy - 70) > 256 + 128 + 40) CHECK(after.at(k).get() == t.get());
    }

    // Delete the road: the original cover returns exactly (same seeds, bit-identical tiles).
    REQUIRE(n.st.undo());
    n.cache->update();
    const TileMap restored = tiles_of(*n.cache, kLayerWorldCover);
    REQUIRE(restored.size() == before.size());
    for (const auto& [k, t] : before) {
        REQUIRE(restored.count(k) == 1);
        CHECK(same_content(*restored.at(k), *t));
    }
}

TEST_CASE("vegetation: nothing road-edge on carriageway, footway, islands or junction polygons; creep is bounded") {
    Net n;
    std::string err;
    REQUIRE(build_test_layout(TestLayout::Interchange, n.g, n.st, *n.region, err));
    n.cache->update();
    const CorridorModel cm = CorridorModel::build(n.g, *n.region, *n.rules, n.cache->junctions(), n.cache->roundabouts());
    const JunctionSet& js = n.cache->junctions();
    const auto& rb = n.cache->roundabouts();
    REQUIRE(rb.size() == 1);

    std::map<std::string, std::size_t> counts;
    std::size_t strips = 0, creeps = 0, areas = 0, bad = 0;
    const auto flag = [&](const char* what, const std::string& type, Vec2 p) {
        ++bad;
        std::printf("[vegetation] BAD %s %s at %.2f %.2f paved %.3f\n", what, type.c_str(), p.x, p.y, cm.paved_distance(p));
    };
    for (const TileKey& k : n.cache->tiles(kLayerVegetation)) {
        const auto t = n.cache->tile<VegetationTile>(kLayerVegetation, k);
        for (const VegInstanceGroup& g : t->groups) {
            counts[g.type_id] += g.instances.size();
            for (const VegInstance& v : g.instances) {
                const Vec2 p = world2(*t, v.pos);
                // Island planting sits on islands by design; everything else off all paved surfaces and junction polygons.
                if (v.layer != VegLayer::RoadEdge) flag("layer", g.type_id, p);
                if (!in_island(rb, p)) {
                    if (cm.paved_distance(p) < -1e-3 || in_junction(js, p)) flag("instance", g.type_id, p);
                }
            }
        }
        for (const GrassStrip& s : t->strips) {
            ++strips;
            counts["strip:" + s.type_id] += s.inner.size();
            const bool creep = s.type_id == "weed_creep";
            if (creep) ++creeps;
            REQUIRE(s.inner.size() == s.outer.size());
            REQUIRE(s.inner.size() >= 2);
            for (std::size_t i = 0; i < s.inner.size(); ++i) {
                const Vec2 a = world2(*t, s.inner[i]), b = world2(*t, s.outer[i]);
                // Outer ends of every ribbon and both ends of ordinary ribbons stay off paved ground.
                if (cm.paved_distance(b, s.edge) < -1e-3) flag("outer", s.type_id, b);
                if (!creep && cm.paved_distance(a, s.edge) < -1e-3) flag("inner", s.type_id, a);
                if (creep) {
                    // Creep reaches inward by at most the rule's reach (hard strip 40% / overrun) of this pack.
                    const double depth = (a - b).length();
                    CHECK(depth <= 0.5);
                }
            }
        }
        areas += t->areas.size();
    }
    CHECK(bad == 0);
    CHECK(strips > 10);
    CHECK(creeps > 0);
    CHECK(areas >= 1);  // the roundabout's central island (surface grass) is planted
    const double km = total_km(n.g);
    std::printf("[vegetation] interchange: %.2f km of road\n", km);
    for (const auto& [type, c] : counts) std::printf("[vegetation]   %-34s %6zu  (%.0f per km)\n", type.c_str(), c, static_cast<double>(c) / km);
    std::size_t belt = counts["tree_broadleaf_round"] + counts["tree_broadleaf_open"] + counts["shrub_scrub"];
    CHECK(belt > 20);  // tree / scrub belts along the dual carriageway
    CHECK(counts["strip:grass_mown_clump"] > 0);
    CHECK(counts["strip:grass_tussock"] > 0);  // long-grass central reservation
}

TEST_CASE("vegetation: street-tree setbacks and spacing follow region data (uk vs testland)") {
    const auto tweak = [](VegetationRules& r) { r.road_types["b_road"].street_trees = test_trees(12, 3.0); };
    Net n("uk", {}, tweak);
    REQUIRE(n.road({-200, 0, 0}, {450, 0, 0}, "b_road"));
    n.cache->update();
    const Edge& e = n.g.edges().begin()->second;
    const double cw = carriageway_half(*n.region, e);
    std::vector<double> xs_pos, xs_neg;
    for (const Flat& f : instances_of(*n.cache, kLayerVegetation)) {
        if (f.type != "tree_broadleaf_round") continue;
        CHECK(std::abs(f.y) >= cw + 3.0 - 0.3 - 1e-3);
        (f.y > 0 ? xs_pos : xs_neg).push_back(f.x);
    }
    REQUIRE(xs_pos.size() > 20);
    REQUIRE(xs_neg.size() > 20);
    for (auto* xs : {&xs_pos, &xs_neg}) {
        std::sort(xs->begin(), xs->end());
        for (std::size_t i = 1; i < xs->size(); ++i) CHECK((*xs)[i] - (*xs)[i - 1] >= 12 * 0.6 - 1e-3);
    }
    // UK residential streets have no street trees in the data; testland's `local` type does, with its own setback.
    Net u;
    REQUIRE(u.road({-200, 0, 0}, {450, 0, 0}, "residential"));
    u.cache->update();
    std::size_t uk_trees = 0;
    for (const Flat& f : instances_of(*u.cache, kLayerVegetation)) uk_trees += f.type.rfind("tree_", 0) == 0;
    CHECK(uk_trees == 0);
    Net t("testland");
    REQUIRE(t.road({-200, 0, 0}, {450, 0, 0}, "local"));
    t.cache->update();
    const double cw2 = carriageway_half(*t.region, t.g.edges().begin()->second);
    std::size_t tl_trees = 0;
    for (const Flat& f : instances_of(*t.cache, kLayerVegetation)) {
        if (f.type.rfind("tree_", 0) != 0) continue;
        ++tl_trees;
        CHECK(std::abs(f.y) >= cw2 + 2.2 - 0.2 - 1e-3);
    }
    CHECK(tl_trees > 10);
}

TEST_CASE("vegetation: junction visibility triangles are clear of tall plants, in both layers") {
    // All-woodland world cover plus street trees on the road types: tall plants are everywhere except where forbidden.
    const auto tweak = [](VegetationRules& r) {
        r.world_cover.woodland_share = 1.0;
        r.world_cover.woodland_edge_ragged_m = 0;
        for (const char* t : {"b_road", "residential"}) r.road_types[t].street_trees = test_trees(8, 3.0);
    };
    Net n("uk", {}, tweak);
    NodeId centre;
    for (double deg : {0.0, 90.0, 200.0}) {
        const double a = deg * kPi / 180;
        REQUIRE(n.road({0, 0, 0}, {160 * std::cos(a), 160 * std::sin(a), 0}, deg == 90.0 ? "residential" : "b_road", centre));
        if (!centre.valid()) centre = n.g.edges().rbegin()->second.a;
    }
    n.cache->update();
    const CorridorModel cm = CorridorModel::build(n.g, *n.region, *n.rules, n.cache->junctions(), n.cache->roundabouts());
    REQUIRE(n.cache->junctions().junctions.size() == 1);
    std::size_t tall = 0, near_junction = 0;
    for (std::string_view layer : {kLayerWorldCover, kLayerVegetation})
        for (const Flat& f : instances_of(*n.cache, layer)) {
            const VegTypeDef* d = n.rules->type(f.type);
            if (!d || d->kind != VegKind::Tree) continue;
            ++tall;
            CHECK_FALSE(cm.in_sight_triangle({f.x, f.y}));
            CHECK_FALSE(cm.tall_blocked({f.x, f.y}));
            if (std::hypot(f.x, f.y) < 70) ++near_junction;
        }
    CHECK(tall > 200);
    CHECK(near_junction > 5);  // trees do stand near the junction, just not inside the splays
    // The triangles are real: some spot just off an arm, a few metres from the mouth, is inside one.
    bool any = false;
    for (double r = 12; r < 30 && !any; r += 2)
        for (double a = 0; a < 2 * kPi && !any; a += 0.1) any = cm.in_sight_triangle({r * std::cos(a), r * std::sin(a)});
    CHECK(any);
}

TEST_CASE("vegetation: determinism, unrelated edits and tile borders") {
    Net a, b;
    REQUIRE(a.road({-200, 60, 0}, {450, 80, 0}, "a_road_rural"));
    REQUIRE(b.road({-200, 60, 0}, {450, 80, 0}, "a_road_rural"));
    a.cache->update();
    b.cache->update();
    CHECK(compare_veg(*a.cache, *b.cache).empty());

    // A new road far away (> 2 tiles) changes nothing near the first one.
    Net c("uk", {}, {}, 256.0, {-256, -256}, {1280, 512});
    REQUIRE(c.road({-200, 60, 0}, {450, 80, 0}, "a_road_rural"));
    c.cache->update();
    const TileMap w0 = tiles_of(*c.cache, kLayerWorldCover), v0 = tiles_of(*c.cache, kLayerVegetation);
    std::map<TileKey, std::uint64_t> ver;
    for (const auto& [k, t] : w0) ver[k] = c.cache->tile_version(kLayerWorldCover, k);
    REQUIRE(c.road({900, 40, 0}, {1200, 300, 0}, "b_road_rural"));
    c.cache->update();
    for (const auto& [k, t] : w0)
        if (k.i <= 1) {
            CHECK(c.cache->tile<VegetationTile>(kLayerWorldCover, k).get() == t.get());
            CHECK(c.cache->tile_version(kLayerWorldCover, k) == ver[k]);
        }
    for (const auto& [k, t] : v0) CHECK(c.cache->tile<VegetationTile>(kLayerVegetation, k).get() == t.get());

    // Tile borders: with 128 m tiles every road-edge instance is identical (position, type, seed) to the 256 m run.
    Net d("uk", {}, {}, 128.0);
    REQUIRE(d.road({-200, 60, 0}, {450, 80, 0}, "a_road_rural"));
    d.cache->update();
    const auto i1 = instances_of(*a.cache, kLayerVegetation), i2 = instances_of(*d.cache, kLayerVegetation);
    REQUIRE(i1.size() == i2.size());
    REQUIRE(i1.size() > 10);
    std::map<std::uint64_t, Flat> by_seed;
    for (const Flat& f : i1) by_seed[f.seed] = f;
    for (const Flat& f : i2) {
        const auto it = by_seed.find(f.seed);
        REQUIRE(it != by_seed.end());
        CHECK(it->second.type == f.type);
        CHECK(std::abs(it->second.x - f.x) < 1e-2);
        CHECK(std::abs(it->second.y - f.y) < 1e-2);
    }
}

TEST_CASE("vegetation: mirror symmetry under driving side") {
    const auto tweak = [](VegetationRules& r) {
        for (const char* t : {"b_road", "a_road_rural"}) r.road_types[t].street_trees = test_trees(14, 3.0);
    };
    const auto build = [&](DrivingSide side, double ys) {
        auto n = std::make_unique<Net>("uk", [side](RegionPack& r) { r.driving_side = side; }, tweak);
        const auto Y = [ys](double y) { return ys * y; };
        const NodeId east = [&] {
            REQUIRE(n->road({0, 0, 0}, {240, 0, 0}, "a_road_rural"));
            return n->g.edges().rbegin()->second.b;
        }();
        REQUIRE(n->road({240, 0, 0}, {330, Y(150), 0}, "b_road", east));
        REQUIRE(n->road({240, 0, 0}, {330, Y(-150), 0}, "b_road", east));
        return n;
    };
    auto l = build(DrivingSide::Left, 1.0);
    auto r = build(DrivingSide::Right, -1.0);
    l->cache->update();
    r->cache->update();
    const auto a = instances_of(*l->cache, kLayerVegetation), b = instances_of(*r->cache, kLayerVegetation);
    REQUIRE(a.size() > 20);
    REQUIRE(a.size() == b.size());
    std::map<std::uint64_t, Flat> by_seed;
    for (const Flat& f : b) by_seed[f.seed] = f;
    std::size_t matched = 0;
    for (const Flat& f : a) {
        const auto it = by_seed.find(f.seed);
        if (it == by_seed.end()) continue;
        ++matched;
        CHECK(it->second.type == f.type);
        CHECK(std::abs(it->second.x - f.x) < 1e-4);
        CHECK(std::abs(it->second.y + f.y) < 1e-4);
    }
    CHECK(matched == a.size());
    // Strips mirror too.
    const auto strips = [](const Net& n) {
        std::map<std::string, std::pair<std::size_t, double>> m;
        for (const TileKey& k : n.cache->tiles(kLayerVegetation))
            for (const GrassStrip& s : n.cache->tile<VegetationTile>(kLayerVegetation, k)->strips) {
                m[s.type_id].first += s.inner.size();
                m[s.type_id].second += std::abs(static_cast<double>(s.inner.front().y));
            }
        return m;
    };
    const auto sa = strips(*l), sb = strips(*r);
    CHECK(sa.size() == sb.size());
    for (const auto& [t, v] : sa) {
        REQUIRE(sb.count(t) == 1);
        CHECK(v.first == sb.at(t).first);
    }
}

TEST_CASE("vegetation: incremental update equals a full rebuild after scripted edits") {
    const auto tweak = [](VegetationRules& r) { r.road_types["b_road"].street_trees = test_trees(15, 3.0); };
    Net n("uk", {}, tweak);
    CHECK(matches_fresh(n));  // roads: none
    REQUIRE(n.road({-200, 60, 0}, {450, 80, 0}, "a_road_rural"));
    CHECK(matches_fresh(n));
    const NodeId mid_a = n.g.edges().begin()->second.a;
    REQUIRE(n.road({100, 70, 0}, {120, 400, 0}, "b_road"));  // not connected: crosses? starts on the line, free end
    CHECK(matches_fresh(n));
    REQUIRE(n.st.execute(make_move_node(mid_a, {-200, 90, 0})));
    CHECK(matches_fresh(n));
    REQUIRE(n.st.execute(make_change_road_type(n.g.edges().begin()->first, "country_lane")));
    CHECK(matches_fresh(n));
    while (n.st.can_undo()) {
        REQUIRE(n.st.undo());
        CHECK(matches_fresh(n));
    }
    while (n.st.can_redo()) {
        REQUIRE(n.st.redo());
        CHECK(matches_fresh(n));
    }
    // Interchange with the roundabout: ring edits and undo.
    Net m;
    std::string err;
    REQUIRE(build_test_layout(TestLayout::Interchange, m.g, m.st, *m.region, err));
    CHECK(matches_fresh(m));
    const RoundaboutLayout ring = m.cache->roundabouts()[0];
    REQUIRE(m.st.execute(make_delete_edge(ring.group.ring_edges[0])));
    CHECK(matches_fresh(m));
    REQUIRE(m.st.undo());
    CHECK(matches_fresh(m));
    const Vec3 q = m.g.node(ring.group.nodes[2])->pos;
    REQUIRE(m.st.execute(make_move_node(ring.group.nodes[2], {q.x + 1.5, q.y - 1.0, q.z})));
    CHECK(matches_fresh(m));
    REQUIRE(m.st.undo());
    CHECK(matches_fresh(m));
}

TEST_CASE("vegetation: fuzz, planar roads at least 15 m apart, incremental == full for both layers") {
    const auto tweak = [](VegetationRules& r) {
        r.road_types["residential"].street_trees = test_trees(14, 3.0);
        r.road_types["residential"].verge.width = 1.5;
        r.road_types["residential"].verge.mown_width = 1.0;
        r.road_types["main_street"].scrub = {true, "shrub_scrub", 10, 0.5, 0.5, 1.0, 0.8, 1.3};
        r.road_types["main_street"].verge.width = 2.0;
    };
    for (std::uint64_t seed : {11ull, 12ull, 13ull}) {
        INFO("seed " << seed);
        PortableRng rng{seed};
        Net n("uk", {}, tweak);
        // 3 x 3 grid, 70 m apart.
        std::map<std::pair<int, int>, NodeId> at;
        std::map<std::pair<int, int>, Vec3> p;
        for (int j = 0; j < 3; ++j)
            for (int i = 0; i < 3; ++i) p[{i, j}] = {i * 70.0 + rng.uniform(-5, 5), j * 70.0 + rng.uniform(-5, 5), 0};
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
        REQUIRE(matches_fresh(n));
        const char* types[] = {"residential", "b_road", "main_street", "country_lane"};
        const auto pick = [&](std::size_t count) { return static_cast<std::size_t>(rng.uniform(0, 1) * static_cast<double>(count)) % count; };
        const auto planar = [&] {
            const auto dseg = [](Vec2 q, Vec2 a, Vec2 b) {
                const Vec2 ab = b - a;
                const double l2 = ab.dot(ab);
                const double t = l2 > 0 ? std::clamp((q - a).dot(ab) / l2, 0.0, 1.0) : 0.0;
                return (q - (a + ab * t)).length();
            };
            for (const auto& [ia, ea] : n.g.edges())
                for (const auto& [ib, eb] : n.g.edges()) {
                    if (!(ia < ib) || ea.a == eb.a || ea.a == eb.b || ea.b == eb.a || ea.b == eb.b) continue;
                    const Vec2 a0 = n.g.node(ea.a)->pos.xy(), a1 = n.g.node(ea.b)->pos.xy(), b0 = n.g.node(eb.a)->pos.xy(), b1 = n.g.node(eb.b)->pos.xy();
                    const auto side = [](Vec2 s, Vec2 t, Vec2 u) { return (t.x - s.x) * (u.y - s.y) - (t.y - s.y) * (u.x - s.x); };
                    if (side(a0, a1, b0) * side(a0, a1, b1) < 0 && side(b0, b1, a0) * side(b0, b1, a1) < 0) return false;
                    if (std::min({dseg(a0, b0, b1), dseg(a1, b0, b1), dseg(b0, a0, a1), dseg(b1, a0, a1)}) < 15) return false;
                }
            return true;
        };
        for (int step = 0; step < 18; ++step) {
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
            } else if (n.st.can_undo()) {
                if (rng.uniform(0, 1) < 0.7) n.st.undo();
                else if (n.st.can_redo()) n.st.redo();
            }
            if (changed && !planar()) n.st.undo();
            INFO("step " << step);
            REQUIRE(matches_fresh(n));
        }
    }
}
