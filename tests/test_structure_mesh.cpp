// Tests for the structure meshes (sim_core/include/cp/structure_mesh.h): bridges, tunnels, retaining walls.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "doctest/doctest.h"
#include "portable_rng.h"
#include "cp/edge_sampling.h"
#include "cp/render_mesh.h"
#include "cp/road_commands.h"
#include "cp/structure_mesh.h"
#include "scene_fixture.h"

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

std::shared_ptr<const RegionPack> uk_right() {
    static const std::shared_ptr<const RegionPack> pack = [] {
        RegionPack p = *uk();
        p.driving_side = DrivingSide::Right;
        return std::make_shared<const RegionPack>(p);
    }();
    return pack;
}

struct World {
    RoadGraph g;
    CommandStack<RoadGraph> st{g};
    std::shared_ptr<const RegionPack> region;
    explicit World(std::shared_ptr<const RegionPack> r = uk()) : region(std::move(r)) { g.set_region(region); }

    EdgeId road(Vec3 a, Vec3 b, const char* type, bool one_way = false, std::vector<Vec3> interior = {}) {
        DrawRoadSpec s;
        s.road_type = type;
        s.one_way = one_way;
        s.start.pos = a;
        s.end.pos = b;
        s.interior = std::move(interior);
        REQUIRE(st.execute(make_draw_road(s)));
        return g.edges().rbegin()->first;
    }
    void span(EdgeId e, double s0, double s1, StructureKind k) { REQUIRE(st.execute(make_set_structure_span(e, s0, s1, k))); }
};

struct FnGround final : GroundSampler {
    std::function<double(double, double)> f;
    std::function<bool(double, double)> water;
    explicit FnGround(std::function<double(double, double)> fn, std::function<bool(double, double)> w = {})
        : f(std::move(fn)), water(std::move(w)) {}
    double height(double x, double y) const override { return f(x, y); }
    bool is_water(double x, double y) const override { return water ? water(x, y) : false; }
};

// ---- mesh inspection

using Key = std::array<long long, 3>;
Key key(Vec3 p) {
    return {std::llround(p.x * 1e5), std::llround(p.y * 1e5), std::llround(p.z * 1e5)};
}
Vec3 from_key(const Key& k) { return {k[0] * 1e-5, k[1] * 1e-5, k[2] * 1e-5}; }
using DirEdge = std::pair<Key, Key>;

const SectionRange& range_of(const StructureMeshes& s, const StructureElement& el) {
    return s.meshes.at(el.surface).sections.at(el.section);
}

std::map<DirEdge, int> directed_edges(const StructureMeshes& s, const StructureElement& el) {
    const WorldMesh& m = s.meshes.at(el.surface);
    const SectionRange& r = range_of(s, el);
    std::map<DirEdge, int> d;
    for (std::uint32_t k = r.first_index; k < r.first_index + r.index_count; k += 3)
        for (int e = 0; e < 3; ++e) {
            const Key a = key(m.positions[m.indices[k + static_cast<std::uint32_t>(e)]]);
            const Key b = key(m.positions[m.indices[k + static_cast<std::uint32_t>((e + 1) % 3)]]);
            if (a != b) ++d[{a, b}];
        }
    return d;
}

// Directed edges without an opposite partner (boundary), as endpoint pairs.
std::vector<std::pair<Vec3, Vec3>> boundary_edges(const StructureMeshes& s, const StructureElement& el) {
    const auto d = directed_edges(s, el);
    std::vector<std::pair<Vec3, Vec3>> out;
    for (const auto& [e, c] : d) {
        const auto it = d.find({e.second, e.first});
        const int r = it == d.end() ? 0 : it->second;
        if (c != r) out.push_back({from_key(e.first), from_key(e.second)});
    }
    return out;
}

double signed_volume(const StructureMeshes& s, const StructureElement& el) {
    const WorldMesh& m = s.meshes.at(el.surface);
    const SectionRange& r = range_of(s, el);
    const Vec3 o = m.positions[m.indices[r.first_index]];
    double v = 0;
    for (std::uint32_t k = r.first_index; k < r.first_index + r.index_count; k += 3) {
        Vec3 a = m.positions[m.indices[k]], b = m.positions[m.indices[k + 1]], c = m.positions[m.indices[k + 2]];
        a = {a.x - o.x, a.y - o.y, a.z - o.z};
        b = {b.x - o.x, b.y - o.y, b.z - o.z};
        c = {c.x - o.x, c.y - o.y, c.z - o.z};
        v += (a.x * (b.y * c.z - b.z * c.y) + a.y * (b.z * c.x - b.x * c.z) + a.z * (b.x * c.y - b.y * c.x)) / 6;
    }
    return v;
}

std::vector<Vec3> element_points(const StructureMeshes& s, const StructureElement& el) {
    const WorldMesh& m = s.meshes.at(el.surface);
    const SectionRange& r = range_of(s, el);
    std::vector<Vec3> out;
    for (std::uint32_t k = r.first_index; k < r.first_index + r.index_count; ++k) out.push_back(m.positions[m.indices[k]]);
    return out;
}

struct Box3 {
    Vec3 lo{1e18, 1e18, 1e18}, hi{-1e18, -1e18, -1e18};
};
Box3 box_of(const std::vector<Vec3>& pts) {
    Box3 b;
    for (const Vec3& p : pts) {
        b.lo = {std::min(b.lo.x, p.x), std::min(b.lo.y, p.y), std::min(b.lo.z, p.z)};
        b.hi = {std::max(b.hi.x, p.x), std::max(b.hi.y, p.y), std::max(b.hi.z, p.z)};
    }
    return b;
}

std::vector<const StructureElement*> of_kind(const StructureMeshes& s, ElementKind k) {
    std::vector<const StructureElement*> out;
    for (const StructureElement& e : s.elements)
        if (e.kind == k) out.push_back(&e);
    return out;
}

// Every mesh is well formed; closed elements are watertight with outward winding.
void check_valid(const StructureMeshes& s) {
    for (const auto& [name, m] : s.meshes) {
        INFO("surface " << name);
        REQUIRE(m.normals.size() == m.positions.size());
        REQUIRE(m.tangents.size() == m.positions.size());
        REQUIRE(m.uv.size() == m.positions.size());
        REQUIRE(s.colours.at(name).size() == m.positions.size());
        REQUIRE(m.indices.size() % 3 == 0);
        for (std::size_t i = 0; i < m.indices.size(); i += 3) {
            const Vec3 a = m.positions[m.indices[i]], b = m.positions[m.indices[i + 1]], c = m.positions[m.indices[i + 2]];
            const Vec3 u{b.x - a.x, b.y - a.y, b.z - a.z}, v{c.x - a.x, c.y - a.y, c.z - a.z};
            const Vec3 n{u.y * v.z - u.z * v.y, u.z * v.x - u.x * v.z, u.x * v.y - u.y * v.x};
            const double len = std::sqrt(n.x * n.x + n.y * n.y + n.z * n.z);
            REQUIRE(len > 2e-8);  // not degenerate
            for (int k = 0; k < 3; ++k) {
                const Vec3 vn = m.normals[m.indices[i + static_cast<std::size_t>(k)]];
                REQUIRE(std::abs(std::sqrt(vn.x * vn.x + vn.y * vn.y + vn.z * vn.z) - 1) < 1e-6);
                REQUIRE(vn.x * n.x + vn.y * n.y + vn.z * n.z > 0);  // vertex normal agrees with the winding
            }
        }
    }
    for (const StructureElement& el : s.elements) {
        INFO("element kind " << static_cast<int>(el.kind) << " tag " << el.tag);
        if (!el.closed) continue;
        CHECK(boundary_edges(s, el).empty());
        CHECK(signed_volume(s, el) > 0);  // outward winding
    }
}

std::size_t tri_count(const StructureMeshes& s) {
    std::size_t n = 0;
    for (const auto& [name, m] : s.meshes) n += m.indices.size() / 3;
    return n;
}

// Height of a triangle mesh at (x, y) among triangles whose normal points up (z > 0.5); NaN if none.
double top_height_at(const WorldMesh& m, double x, double y, const std::uint32_t* first = nullptr, std::uint32_t count = 0) {
    std::uint32_t k0 = first ? *first : 0, k1 = first ? *first + count : static_cast<std::uint32_t>(m.indices.size());
    for (std::uint32_t k = k0; k < k1; k += 3) {
        const Vec3 a = m.positions[m.indices[k]], b = m.positions[m.indices[k + 1]], c = m.positions[m.indices[k + 2]];
        const double d = (b.y - c.y) * (a.x - c.x) + (c.x - b.x) * (a.y - c.y);
        if (std::abs(d) < 1e-12) continue;
        const double l1 = ((b.y - c.y) * (x - c.x) + (c.x - b.x) * (y - c.y)) / d;
        const double l2 = ((c.y - a.y) * (x - c.x) + (a.x - c.x) * (y - c.y)) / d;
        const double l3 = 1 - l1 - l2;
        if (l1 < -1e-9 || l2 < -1e-9 || l3 < -1e-9) continue;
        const Vec3 u{b.x - a.x, b.y - a.y, b.z - a.z}, v{c.x - a.x, c.y - a.y, c.z - a.z};
        if (u.x * v.y - u.y * v.x <= 0) continue;  // not facing up
        return l1 * a.z + l2 * b.z + l3 * c.z;
    }
    return std::nan("");
}

// A straight b_road along +y at x = 0, level at z, with a bridge span; returns the edge.
struct Bridged {
    World w;
    EdgeId e;
    double span0 = 0, span1 = 0;
    explicit Bridged(std::shared_ptr<const RegionPack> r, double z = 8, double s0 = 60, double s1 = 140) : w(std::move(r)) {
        e = w.road({0, -100, z}, {0, 100, z}, "b_road");
        span0 = s0;
        span1 = s1;
        w.span(e, s0, s1, StructureKind::Bridge);
    }
};

const FnGround kFlat0([](double, double) { return 0.0; });

}  // namespace

TEST_CASE("bridge: parts, closed solids, counts") {
    StructureParams P40;
    P40.max_pier_spacing = 40;
    Bridged b(uk());
    const StructureMeshes s = build_edge_structures(b.w.g, *b.w.region, b.e, kFlat0, P40);
    check_valid(s);
    REQUIRE(s.bridges.size() == 1);
    const BridgeInfo& info = s.bridges[0];
    CHECK_FALSE(info.solid_block);
    CHECK_FALSE(info.crosses_road);
    CHECK(info.pier_s.size() == 1);  // 80 m span, 40 m maximum spacing
    CHECK(info.longest_span <= P40.max_pier_spacing + 1e-6);
    CHECK_FALSE(info.span_too_long);
    CHECK(of_kind(s, ElementKind::Deck).size() == 1);
    CHECK(of_kind(s, ElementKind::Parapet).size() == 2);
    CHECK(of_kind(s, ElementKind::ParapetEnd).size() == 2);
    CHECK(of_kind(s, ElementKind::Pier).size() == 1);
    CHECK(of_kind(s, ElementKind::PierCap).size() == 1);
    CHECK(of_kind(s, ElementKind::PierFooting).size() == 1);
    CHECK(of_kind(s, ElementKind::AbutmentStem).size() == 2);
    CHECK(of_kind(s, ElementKind::WingWall).size() == 8);  // body + coping, four wings
    CHECK(s.meshes.count(structure_surface::concrete));
    CHECK(s.meshes.count(structure_surface::parapet));
    CHECK(s.meshes.count(structure_surface::coping));
    // Parapet posts are an instance list: both sides, every 3 m.
    REQUIRE(s.instances.size() == 1);
    CHECK(s.instances[0].kind == "parapet_post");
    CHECK(s.instances[0].items.size() == 2 * 26);
    // Sections carry the bridge kind and the span.
    for (const StructureElement& el : s.elements) {
        if (el.kind == ElementKind::WingWall || el.kind == ElementKind::AbutmentStem) continue;
        const SectionRange& r = range_of(s, el);
        CHECK(r.kind == StructureKind::Bridge);
        CHECK(r.edge == b.e);
    }
}

TEST_CASE("bridge: deck top sits just under the road surface and parapets stand at the cross-section edges") {
    Bridged b(uk());
    const StructureMeshes s = build_edge_structures(b.w.g, *b.w.region, b.e, kFlat0);
    const WorldMeshSet road = build_edge_mesh(b.w.g, *b.w.region, b.e, JunctionSet{});
    REQUIRE(road.count("asphalt"));
    const WorldMesh& asphalt = road.at("asphalt");
    const StructureElement& deck = *of_kind(s, ElementKind::Deck)[0];
    const WorldMesh& dm = s.meshes.at(deck.surface);
    const SectionRange& dr = range_of(s, deck);
    const double gap = StructureParams{}.deck_top_gap;
    PortableRng rng{7};
    int checked = 0;
    for (int i = 0; i < 60; ++i) {
        const double x = rng.uniform(-3.1, 3.1), y = rng.uniform(-100 + b.span0 + 1, -100 + b.span1 - 1);
        const double zr = top_height_at(asphalt, x, y);
        const double zd = top_height_at(dm, x, y, &dr.first_index, dr.index_count);
        if (std::isnan(zr) || std::isnan(zd)) continue;
        ++checked;
        CHECK(zr - zd == doctest::Approx(gap).epsilon(0.0).scale(1.0).epsilon(0.03));
    }
    CHECK(checked > 40);
    // Parapets: outer face on the road mesh's outermost edge, 1 m above the road there.
    double road_edge = 0;
    for (const auto& [name, mm] : road)
        for (const Vec3& p : mm.positions) road_edge = std::max(road_edge, std::abs(p.x));
    CHECK(road_edge > 5.0);
    double outer = 0, top = -1e9;
    for (const StructureElement* el : of_kind(s, ElementKind::Parapet)) {
        const Box3 bx = box_of(element_points(s, *el));
        outer = std::max({outer, std::abs(bx.lo.x), std::abs(bx.hi.x)});
        top = std::max(top, bx.hi.z);
    }
    CHECK(outer == doctest::Approx(road_edge).epsilon(0.001));
    CHECK(top == doctest::Approx(8.0 + 1.0).epsilon(0.02));
}

TEST_CASE("bridge: piers reach the ground and stand on footings") {
    StructureParams P40;
    P40.max_pier_spacing = 40;
    Bridged b(uk());
    const FnGround rolling([](double x, double y) { return 1.0 + 0.5 * std::sin(0.1 * y) + 0.02 * x; });
    const StructureMeshes s = build_edge_structures(b.w.g, *b.w.region, b.e, rolling, P40);
    check_valid(s);
    const auto piers = of_kind(s, ElementKind::Pier), foots = of_kind(s, ElementKind::PierFooting), caps = of_kind(s, ElementKind::PierCap);
    REQUIRE(piers.size() == 1);
    REQUIRE(foots.size() == 1);
    const Box3 sh = box_of(element_points(s, *piers[0])), ft = box_of(element_points(s, *foots[0])), cp = box_of(element_points(s, *caps[0]));
    // Footing buried below all the ground under it, shaft starts above all of it, cap hangs under the deck.
    double gmin = 1e9, gmax = -1e9;
    for (double x = ft.lo.x; x <= ft.hi.x; x += 0.25)
        for (double y = ft.lo.y; y <= ft.hi.y; y += 0.25) {
            gmin = std::min(gmin, rolling.height(x, y));
            gmax = std::max(gmax, rolling.height(x, y));
        }
    CHECK(ft.lo.z <= gmin - 1.0);
    CHECK(ft.hi.z >= gmax + 0.3);
    CHECK(sh.lo.z == doctest::Approx(ft.hi.z));
    CHECK(sh.hi.z == doctest::Approx(cp.lo.z));
    CHECK(cp.hi.z < 8.0);
    CHECK(ft.hi.x > sh.hi.x);  // the footing is wider than the shaft
}

TEST_CASE("bridge: piers keep out of the roads crossing underneath") {
    World w;
    // E-W bridge, 120 m span at z = 8; a dual carriageway runs N-S at x = 0 (where the middle pier would go) on the ground.
    const EdgeId br = w.road({-100, 0, 8}, {100, 0, 8}, "b_road");
    w.road({0, -100, 0}, {0, 100, 0}, "a_road_dual", true);
    w.span(br, 40, 160, StructureKind::Bridge);
    const StructureMeshes s = build_edge_structures(w.g, *w.region, br, kFlat0);
    check_valid(s);
    REQUIRE(s.bridges.size() == 1);
    const BridgeInfo& info = s.bridges[0];
    CHECK(info.crosses_road);
    CHECK_FALSE(info.solid_block);
    CHECK(info.pier_s.size() >= 3);
    CHECK_FALSE(info.span_too_long);
    const RoadType* rt = w.region->road_type("a_road_dual");
    REQUIRE(rt);
    double half = 0;  // dual carriageway: whole cross-section is centred on x = 0
    for (const CrossSectionSlot& sl : rt->carriageway) half += sl.width;
    half /= 2;
    for (const StructureElement* el : of_kind(s, ElementKind::PierFooting)) {
        const Box3 bx = box_of(element_points(s, *el));
        const double xc = 0.5 * (bx.lo.x + bx.hi.x);
        CHECK(std::abs(xc) >= half + StructureParams{}.pier_road_clearance);
        CHECK((bx.hi.x < -half || bx.lo.x > half));  // footing clear of the cross-section
    }
    // Without the crossing road the middle pier stands at the ideal place.
    World w2;
    const EdgeId br2 = w2.road({-100, 0, 8}, {100, 0, 8}, "b_road");
    w2.span(br2, 40, 160, StructureKind::Bridge);
    const StructureMeshes s2 = build_edge_structures(w2.g, *w2.region, br2, kFlat0);
    REQUIRE(s2.bridges.size() == 1);
    bool centre = false;
    for (double ps : s2.bridges[0].pier_s) centre |= std::abs(ps - 100) < 0.6;
    CHECK(centre);
}

TEST_CASE("bridge: short span rule (plain ground, over a road, over water)") {
    const double z = 5;
    auto make = [&](bool with_road) {
        auto w = std::make_unique<World>();
        const EdgeId br = w->road({0, -50, z}, {0, 50, z}, "b_road");
        if (with_road) w->road({-50, 0, 0}, {50, 0, 0}, "b_road");
        w->span(br, 44, 56, StructureKind::Bridge);  // 12 m
        return std::pair<std::unique_ptr<World>, EdgeId>(std::move(w), br);
    };
    {  // plain ground: solid block with parapets
        auto [w, br] = make(false);
        const StructureMeshes s = build_edge_structures(w->g, *w->region, br, kFlat0);
        check_valid(s);
        REQUIRE(s.bridges.size() == 1);
        CHECK(s.bridges[0].solid_block);
        CHECK(of_kind(s, ElementKind::Block).size() == 1);
        CHECK(of_kind(s, ElementKind::Parapet).size() == 2);
        CHECK(of_kind(s, ElementKind::Deck).empty());
        CHECK(of_kind(s, ElementKind::Pier).empty());
        CHECK(of_kind(s, ElementKind::AbutmentStem).empty());
        const Box3 bx = box_of(element_points(s, *of_kind(s, ElementKind::Block)[0]));
        CHECK(bx.lo.z <= -1.0 + 1e-6);  // buried below the ground
    }
    {  // over another road: a real bridge
        auto [w, br] = make(true);
        const StructureMeshes s = build_edge_structures(w->g, *w->region, br, kFlat0);
        check_valid(s);
        REQUIRE(s.bridges.size() == 1);
        CHECK(s.bridges[0].crosses_road);
        CHECK_FALSE(s.bridges[0].solid_block);
        CHECK(of_kind(s, ElementKind::Deck).size() == 1);
        CHECK(of_kind(s, ElementKind::AbutmentStem).size() == 2);
        CHECK(of_kind(s, ElementKind::Block).empty());
        CHECK(s.bridges[0].pier_s.empty());
    }
    {  // over water: a real bridge
        auto [w, br] = make(false);
        const FnGround river([](double, double) { return -2.0; }, [](double, double y) { return std::abs(y) < 8; });
        const StructureMeshes s = build_edge_structures(w->g, *w->region, br, river);
        check_valid(s);
        REQUIRE(s.bridges.size() == 1);
        CHECK(s.bridges[0].over_water);
        CHECK_FALSE(s.bridges[0].solid_block);
        CHECK(of_kind(s, ElementKind::Deck).size() == 1);
    }
    {  // a road more than 2 m below is needed: one only 1 m under the deck does not count
        World w;
        const EdgeId br = w.road({0, -50, 1}, {0, 50, 1}, "b_road");
        w.road({-50, 0, 0}, {50, 0, 0}, "b_road");
        w.span(br, 44, 56, StructureKind::Bridge);
        const StructureMeshes s = build_edge_structures(w.g, *w.region, br, kFlat0);
        REQUIRE(s.bridges.size() == 1);
        CHECK_FALSE(s.bridges[0].crosses_road);
        CHECK(s.bridges[0].solid_block);
    }
}

TEST_CASE("bridge: abutments close against the embankment and wing walls reach the ground") {
    // Flat ground far below: the abutment is a tall wall and the wing walls run their full length.
    Bridged b(uk());
    const StructureMeshes deep = build_edge_structures(b.w.g, *b.w.region, b.e, kFlat0);
    const StructureParams P;
    const auto wings = of_kind(deep, ElementKind::WingWall);
    REQUIRE(wings.size() == 8);
    for (const StructureElement* el : wings) {
        const Box3 bx = box_of(element_points(deep, *el));
        if (el->surface == structure_surface::concrete) CHECK(bx.lo.z <= -1.0 + 1e-6);  // buried below ground 0
        CHECK((-100 + b.span0) - bx.lo.y < P.wing_length + P.bearing_width + 1.0);
    }
    for (const StructureElement* el : of_kind(deep, ElementKind::AbutmentStem)) {
        const Box3 bx = box_of(element_points(deep, *el));
        CHECK(bx.lo.z <= -1.0 + 1e-6);
        CHECK(bx.hi.z > 7.0);  // the ballast wall reaches the road
    }
    // Ground that is already at road level beside the abutment: the wings end after one panel.
    const FnGround bank([](double x, double y) {
        const bool under = y > -40 && y < 40;
        const double yy = std::abs(y) - 40;
        (void)x;
        (void)yy;
        return under ? 0.0 : 8.0;
    });
    const StructureMeshes sh = build_edge_structures(b.w.g, *b.w.region, b.e, bank);
    check_valid(sh);
    double longest = 0;
    for (const StructureElement* el : of_kind(sh, ElementKind::WingWall)) {
        const Box3 bx = box_of(element_points(sh, *el));
        // distance beyond the abutment end (y = -40 at the start end, +40 at the far end)
        const double beyond = std::max(-40.0 - bx.lo.y, bx.hi.y - 40.0);
        longest = std::max(longest, beyond);
    }
    CHECK(longest <= P.wing_panel + 0.5);
    CHECK(longest > 0.5);
}


TEST_CASE("tunnel: lining on a curved sloping centreline, closed to the headwalls") {
    World w;
    const EdgeId e = w.road({0, 0, 0}, {160, 60, 12}, "b_road", false, {{80, -20, 6}});
    const double len = w.g.edge(e)->horizontal.length();
    w.span(e, 30, len - 30, StructureKind::Tunnel);
    const FnGround hill([](double, double) { return 45.0; });
    const StructureMeshes s = build_edge_structures(w.g, *w.region, e, hill);
    check_valid(s);
    REQUIRE(s.tunnels.size() == 1);
    const TunnelBore& bore = s.tunnels[0];
    CHECK(bore.stations.size() > 20);
    CHECK(bore.section.W == doctest::Approx(5.4).epsilon(0.01));
    const auto lin = of_kind(s, ElementKind::TunnelLining);
    REQUIRE(lin.size() == 1);
    const WorldMesh& lm = s.meshes.at(structure_surface::lining);
    // The crown follows every station of the curved, sloping centreline.
    std::set<Key> verts;
    for (const Vec3& p : lm.positions) verts.insert(key(p));
    int found = 0;
    for (const TunnelStationInfo& st : bore.stations) found += verts.count(key({st.x, st.y, st.z + bore.section.Hc})) ? 1 : 0;
    CHECK(found == static_cast<int>(bore.stations.size()));
    // The centreline really bends and climbs.
    double turn = 0;
    for (std::size_t i = 1; i < bore.stations.size(); ++i) turn += std::abs(wrap_angle(bore.stations[i].h - bore.stations[i - 1].h));
    CHECK(turn > 0.3);
    CHECK(bore.stations.back().z - bore.stations.front().z > 5);

    // Open edges of the lining: the two portal rings and the two bottom lines only.
    const Edge& ed = *w.g.edge(e);
    SamplingParams fine;
    fine.max_chord_error = 0.002;
    fine.max_heading_change = 0.01;
    fine.max_step = 1.0;
    const OffsetPolyline lo = sample_offset(ed, -5.4, fine), hi = sample_offset(ed, 5.4, fine);
    const auto near_poly = [](const OffsetPolyline& pl, Vec3 p) {
        double best = 1e9;
        for (std::size_t i = 0; i + 1 < pl.samples.size(); ++i) {
            const Vec3 a = pl.samples[i].pos, b = pl.samples[i + 1].pos;
            const double dx = b.x - a.x, dy = b.y - a.y, l2 = dx * dx + dy * dy;
            const double t = l2 > 0 ? std::clamp(((p.x - a.x) * dx + (p.y - a.y) * dy) / l2, 0.0, 1.0) : 0.0;
            best = std::min(best, std::hypot(p.x - a.x - dx * t, p.y - a.y - dy * t));
        }
        return best;
    };
    const TunnelStationInfo &st0 = bore.stations.front(), &st1 = bore.stations.back();
    const auto on_plane = [](const TunnelStationInfo& st, Vec3 p) {
        return std::abs((p.x - st.x) * std::cos(st.h) + (p.y - st.y) * std::sin(st.h)) < 1e-4;
    };
    std::set<std::pair<Key, Key>> ring;
    for (const auto& [a, b] : boundary_edges(s, *lin[0])) {
        const bool r0 = on_plane(st0, a) && on_plane(st0, b), r1 = on_plane(st1, a) && on_plane(st1, b);
        if (r0 || r1) {
            ring.insert({std::min(key(a), key(b)), std::max(key(a), key(b))});
            continue;
        }
        const bool bottom = (near_poly(lo, a) < 0.03 && near_poly(lo, b) < 0.03) || (near_poly(hi, a) < 0.03 && near_poly(hi, b) < 0.03);
        CHECK(bottom);
    }
    CHECK(ring.size() >= 2 * 18);  // two walls + sixteen arch segments at each end

    // Every ring edge is a boundary edge of a headwall's front face (no gap between lining and headwall).
    const auto heads = of_kind(s, ElementKind::Headwall);
    REQUIRE(heads.size() == 2);
    std::set<std::pair<Key, Key>> head_edges;
    for (const StructureElement* h : heads)
        for (const auto& [a, b] : boundary_edges(s, *h)) head_edges.insert({std::min(key(a), key(b)), std::max(key(a), key(b))});
    int missing = 0;
    for (const auto& e2 : ring) missing += head_edges.count(e2) ? 0 : 1;
    CHECK(missing == 0);
    // Headwall open edges are on the opening only.
    for (const StructureElement* h : heads)
        for (const auto& [a, b] : boundary_edges(s, *h)) {
            const TunnelStationInfo& st = std::hypot(a.x - st0.x, a.y - st0.y) < std::hypot(a.x - st1.x, a.y - st1.y) ? st0 : st1;
            CHECK(std::hypot(a.x - st.x, a.y - st.y) < 5.4 + 2.5);
            CHECK(a.z < st.z + bore.section.Hc + 0.01);
        }
    // Coping tops the headwall above the hill (the terrain step it hides).
    for (const StructureElement* c : of_kind(s, ElementKind::HeadwallCoping)) {
        const Box3 bx = box_of(element_points(s, *c));
        CHECK(bx.hi.z >= 45.0 + StructureParams{}.headwall_ground_margin - 1e-6);
        CHECK(bx.hi.z - bx.lo.z == doctest::Approx(StructureParams{}.coping_thickness).epsilon(0.01));
    }
    // Lamps: an instance list, in rows under the roof.
    REQUIRE(s.instances.size() == 1);
    CHECK(s.instances[0].kind == "tunnel_lamp");
    CHECK(s.instances[0].items.size() >= 2 * 8);
    // Depth from the portals rides in the vertex colour R: 0 at a portal, larger inside.
    const auto& col = s.colours.at(structure_surface::lining);
    int zero = 0, deep = 0;
    for (std::size_t i = 0; i < lm.positions.size(); ++i) {
        const bool at_portal = on_plane(st0, lm.positions[i]) || on_plane(st1, lm.positions[i]);
        if (at_portal) CHECK(col[i][0] == 0);
        zero += at_portal ? 1 : 0;
        deep += col[i][0] > 10 ? 1 : 0;
    }
    CHECK(zero > 0);
    CHECK(deep > 0);
    CHECK(lm.sections.at(lin[0]->section).kind == StructureKind::Tunnel);
}

namespace {

StructureWallRun straight_run(double x0, double x1, double y, char side = 'L') {
    StructureWallRun r;
    r.side = side;
    r.cut = true;
    r.lo = 0;
    r.hi = 1.5;
    for (double x = x0; x <= x1 + 1e-9; x += 2.0) {
        StructureWallPoint p;
        p.x = x;
        p.y = y;
        p.zBot = 0;
        p.zTop = 0;
        p.top = 0;
        p.h = 0.0;
        r.pts.push_back(p);
    }
    return r;
}

// (smallest x of each upward coping top triangle, level), sorted by x.
std::vector<std::pair<double, double>> coping_levels(const StructureMeshes& s) {
    std::vector<std::pair<double, double>> out;
    for (const StructureElement* el : of_kind(s, ElementKind::WallCoping)) {
        const WorldMesh& m = s.meshes.at(el->surface);
        const SectionRange& r = range_of(s, *el);
        for (std::uint32_t k = r.first_index; k < r.first_index + r.index_count; k += 3) {
            const Vec3 a = m.positions[m.indices[k]], b = m.positions[m.indices[k + 1]], c = m.positions[m.indices[k + 2]];
            if (std::abs(a.z - b.z) > 1e-9 || std::abs(a.z - c.z) > 1e-9) continue;
            if ((b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x) <= 0) continue;  // not facing up
            out.push_back({std::min({a.x, b.x, c.x}), a.z});
        }
    }
    std::sort(out.begin(), out.end());
    return out;
}

}  // namespace

TEST_CASE("walls: level panels, hysteresis, maximum step, coping") {
    const auto g = [](double x, double) {
        if (x < 32) return 2.0;
        if (x < 36) return 0.5;   // one panel low: no dip
        if (x < 70) return 2.0;
        if (x < 110) return 5.0;  // sudden rise: rises at once, the approach steps up
        return 1.0;               // sustained drop: steps down gradually
    };
    const FnGround ground(g);
    const StructureParams P;
    const StructureMeshes s = build_retaining_walls({straight_run(0, 160, 0)}, ground, P);
    check_valid(s);
    CHECK(of_kind(s, ElementKind::RetainingWall).size() == 1);
    CHECK(of_kind(s, ElementKind::WallCoping).size() == 1);
    CHECK(s.meshes.count(structure_surface::wall));
    CHECK(s.meshes.count(structure_surface::coping));
    const auto lv = coping_levels(s);
    REQUIRE(lv.size() > 50);
    double prev = lv.front().second;
    for (const auto& [x, z] : lv) {
        CHECK(std::abs(z / P.wall_quant - std::round(z / P.wall_quant)) < 1e-9);  // quantised
        if (x > 6 && x < 154) CHECK(z >= g(x, 0) + 0.3 - 1e-9);                   // covers the retained ground
        CHECK(std::abs(z - prev) <= P.wall_max_step + 1e-9);                       // no step above the limit
        prev = z;
        if (x > 32.5 && x < 35) CHECK(z >= 2.5 - 1e-9);                            // single low panel: held
        if (x > 70.5 && x < 74) CHECK(z >= 5.5 - 1e-9);                            // rises immediately
        if (x > 146 && x < 154) CHECK(z == doctest::Approx(1.5));                  // sustained lower need: stepped down 0.5 m per panel
    }
    // Free end tapers below the full-height level.
    CHECK(lv.front().second <= lv[20].second);
    std::set<double> levels;
    for (const auto& [x, z] : lv) levels.insert(z);
    CHECK(levels.size() >= 6);
    // Coping: a thin cap (15 cm, 5 cm overhang each side) on a 0.4 m wall, standing in the band the grading reserves (1.5 m).
    const Box3 bw = box_of(element_points(s, *of_kind(s, ElementKind::RetainingWall)[0]));
    const Box3 bc = box_of(element_points(s, *of_kind(s, ElementKind::WallCoping)[0]));
    CHECK(bc.hi.y - bc.lo.y == doctest::Approx(P.wall_face_thickness + 2 * P.coping_overhang).epsilon(0.001));
    CHECK(bc.hi.y - bc.lo.y < 0.6);
    // The wall is the face (0.4 m) plus a backing for the rest of the band (1.1 m), whose top is below the coping.
    CHECK(bw.hi.y - bw.lo.y == doctest::Approx(1.5).epsilon(0.001));
    CHECK(s.meshes.at(structure_surface::coping).positions.size() > 0);
    CHECK(std::string(structure_surface::coping) != structure_surface::wall);
}

TEST_CASE("walls: steep ground stays within the step limit") {
    // A 3 m cliff over 2 m of wall: panels split locally; steps never exceed the limit.
    const FnGround ground([](double x, double) { return x < 50 ? 1.0 : (x < 52 ? 1.0 + 1.5 * (x - 50) : 4.0); });
    const StructureParams P;
    const StructureMeshes s = build_retaining_walls({straight_run(0, 100, 0)}, ground, P);
    check_valid(s);
    const auto lv = coping_levels(s);
    REQUIRE_FALSE(lv.empty());
    double prev = lv.front().second;
    for (const auto& [x, z] : lv) {
        CHECK(std::abs(z - prev) <= P.wall_max_step + 1e-9);
        prev = z;
        if (x > 6 && x < 94) CHECK(z >= ground.height(x, 0) + 0.3 - 1e-9);
    }
}

TEST_CASE("walls: fold-safe on a tight curve") {
    // Wall on the inside of a circle of radius 1.2 m: the 1.5 m offset face folds through the centre.
    StructureWallRun r;
    r.side = 'L';
    r.cut = true;
    r.lo = 0;
    r.hi = 1.5;
    for (int i = 0; i <= 64; ++i) {
        const double a = 2 * kPi * i / 64;
        StructureWallPoint p;
        p.x = 1.2 * std::cos(a);
        p.y = 1.2 * std::sin(a);
        p.h = a + kPi / 2;
        p.zBot = 0;
        r.pts.push_back(p);
    }
    const FnGround ground([](double, double) { return 2.0; });
    const StructureMeshes s = build_retaining_walls({r}, ground);
    check_valid(s);
    // A gentle arc keeps every station.
    StructureWallRun arc = r;
    arc.pts.clear();
    for (int i = 0; i <= 40; ++i) {
        const double a = 0.04 * i;
        StructureWallPoint p;
        p.x = 30 * std::sin(a);
        p.y = 30 * (1 - std::cos(a));
        p.h = a;
        arc.pts.push_back(p);
    }
    const StructureMeshes s2 = build_retaining_walls({arc}, ground);
    check_valid(s2);
    CHECK(tri_count(s2) > 100);
}

namespace {

// x of the centre of each joint pilaster (clusters of vertices of the WallJoint element), sorted.
std::vector<double> joint_centres_x(const StructureMeshes& s) {
    std::vector<double> xs;
    for (const StructureElement* el : of_kind(s, ElementKind::WallJoint))
        for (const Vec3& p : element_points(s, *el)) xs.push_back(p.x);
    std::sort(xs.begin(), xs.end());
    std::vector<double> out;
    std::size_t i = 0;
    while (i < xs.size()) {
        std::size_t j = i;
        while (j + 1 < xs.size() && xs[j + 1] - xs[j] < 0.5) ++j;
        out.push_back(0.5 * (xs[i] + xs[j]));
        i = j + 1;
    }
    return out;
}

}  // namespace

TEST_CASE("walls: panels are exactly the rule long with a joint at every boundary, steps stay within the rule") {
    const StructureParams P;
    // Ground rising 0.1 m per metre: every panel steps up by 0.4 m (< 0.5 max step), the tops follow in 0.25 m quanta.
    const FnGround ground([](double x, double) { return 0.1 * x; });
    {
        const StructureMeshes s = build_retaining_walls({straight_run(0, 158, 0)}, ground, P);
        check_valid(s);
        REQUIRE(s.meshes.count(structure_surface::joint));
        const std::vector<double> jx = joint_centres_x(s);
        // Boundaries at 4, 8, ... 156; the last panel (156..158) is the shorter remainder.
        REQUIRE(jx.size() == 39);
        for (std::size_t k = 0; k < jx.size(); ++k) CHECK(jx[k] == doctest::Approx(4.0 * static_cast<double>(k + 1)).epsilon(1e-6).scale(1.0));
        // Steps: every change of the coping level happens at a panel boundary and is within the maximum step.
        const auto lv = coping_levels(s);
        REQUIRE(lv.size() > 100);
        int steps = 0;
        for (std::size_t i = 1; i < lv.size(); ++i) {
            const double dz = lv[i].second - lv[i - 1].second;
            if (std::abs(dz) > 1e-9) {
                ++steps;
                CHECK(std::abs(dz) <= P.wall_max_step + 1e-9);
                const double r = std::fmod(lv[i].first, P.wall_panel);
                CHECK(std::min(r, P.wall_panel - r) < 0.01);
            }
        }
        CHECK(steps >= 20);
    }
    {  // a remainder shorter than the minimum panel joins the last full panel
        const StructureMeshes s = build_retaining_walls({straight_run(0, 160.5, 0)}, ground, P);
        const std::vector<double> jx = joint_centres_x(s);
        REQUIRE(jx.size() == 39);
        CHECK(jx.back() == doctest::Approx(156.0).epsilon(1e-6).scale(1.0));
    }
    {  // flat ground: no steps at all, the joints are still there
        const FnGround flat([](double, double) { return 1.0; });
        const StructureMeshes s = build_retaining_walls({straight_run(0, 80, 0)}, flat, P);
        const auto lv = coping_levels(s);
        std::set<double> levels;
        for (const auto& [x, z] : lv)
            if (x > 8 && x < 72) levels.insert(z);
        CHECK(levels.size() == 1);
        CHECK(joint_centres_x(s).size() == 19);
        const Box3 bc = box_of(element_points(s, *of_kind(s, ElementKind::WallCoping)[0]));
        const Box3 bw = box_of(element_points(s, *of_kind(s, ElementKind::RetainingWall)[0]));
        CHECK(bc.hi.z - bw.hi.z == doctest::Approx(P.coping_thickness).epsilon(1e-6).scale(1.0));  // a thin coping on top of the wall
    }
    {  // joints are pilasters that stand proud of the visible face and are their own surface
        const FnGround flat([](double, double) { return 1.0; });
        const StructureMeshes s = build_retaining_walls({straight_run(0, 80, 0)}, flat, P);
        const Box3 bj = box_of(element_points(s, *of_kind(s, ElementKind::WallJoint)[0]));
        CHECK(bj.lo.y == doctest::Approx(-P.wall_joint_proud).epsilon(1e-6).scale(1.0));  // the face is at y = 0, the road side
        CHECK(bj.hi.y == doctest::Approx(0.0).epsilon(1e-6).scale(1.0));
        CHECK(std::string(structure_surface::joint) != structure_surface::wall);
    }
    {  // the step-halving variant stays within the limit too (opt-in)
        StructureParams Ps = P;
        Ps.wall_allow_short_panels = true;
        const FnGround cliff([](double x, double) { return x < 50 ? 1.0 : (x < 52 ? 1.0 + 1.5 * (x - 50) : 4.0); });
        const StructureMeshes s = build_retaining_walls({straight_run(0, 100, 0)}, cliff, Ps);
        check_valid(s);
        const auto lv = coping_levels(s);
        for (std::size_t i = 1; i < lv.size(); ++i) CHECK(std::abs(lv[i].second - lv[i - 1].second) <= Ps.wall_max_step + 1e-9);
    }
}

TEST_CASE("walls: fill walls show the outer face, the wall face never sits inside the road cross-section") {
    const StructureParams P;
    const FnGround ground([](double, double) { return 1.0; });
    // A fill wall (visible face at the outer side of the band) and a cut wall (inner side), left of the road direction.
    StructureWallRun fill = straight_run(0, 40, 6.0);
    fill.cut = false;
    const StructureMeshes sf = build_retaining_walls({fill}, ground, P);
    const Box3 bf = box_of(element_points(sf, *of_kind(sf, ElementKind::RetainingWall)[0]));
    CHECK(bf.lo.y >= 6.0 + (1.5 - P.wall_face_thickness) - 1e-6);  // face and body sit at the outer end of the band
    CHECK(bf.hi.y == doctest::Approx(6.0 + 1.5).epsilon(1e-6).scale(1.0));
    const Box3 bj = box_of(element_points(sf, *of_kind(sf, ElementKind::WallJoint)[0]));
    CHECK(bj.hi.y == doctest::Approx(6.0 + 1.5 + P.wall_joint_proud).epsilon(1e-6).scale(1.0));
    const StructureMeshes sc = build_retaining_walls({straight_run(0, 40, 6.0)}, ground, P);
    const Box3 bc = box_of(element_points(sc, *of_kind(sc, ElementKind::RetainingWall)[0]));
    CHECK(bc.lo.y >= 6.0 - 1e-6);  // never inside the road cross-section (the run line is its edge)
    // The run line the grading produces is the edge of the road type's full cross-section, which already holds
    // the verge lane: wall line - carriageway edge >= the 1.5 m verge rule for the rural types.
    World w;
    for (const char* type : {"a_road_rural", "b_road_rural"}) {
        const RoadType* rt = w.region->road_type(type);
        REQUIRE(rt);
        const EdgeId e = w.road({0, 0, 0}, {100, 0, 0}, type);
        const Edge* ed = w.g.edge(e);
        const std::vector<Lane> lanes = instantiate_lanes(*ed, *rt, w.region->driving_side);
        double cw = 0, all = 0;
        for (const Lane& l : lanes) {
            all = std::max(all, l.offset + l.width / 2);
            const LaneType* lt = w.region->lane_type(l.lane_type);
            if (lt && lt->profile != "verge") cw = std::max(cw, l.offset + l.width / 2);
        }
        CHECK(all - cw >= 1.5 - 1e-9);
    }
}

TEST_CASE("bridge: a short span is a closed solid block with both parapets attached to its edges, and the wing coping is thin") {
    World w;
    const EdgeId br = w.road({0, -50, 5}, {0, 50, 5}, "b_road");
    w.span(br, 44, 56, StructureKind::Bridge);
    // A ditch under the block: the block's sides must run down to the ditch floor on both sides.
    const FnGround ditch([](double, double y) { return std::abs(y) < 6 ? -4.0 : 0.0; });
    const StructureMeshes s = build_edge_structures(w.g, *w.region, br, ditch);
    check_valid(s);
    REQUIRE(s.bridges.size() == 1);
    REQUIRE(s.bridges[0].solid_block);
    const auto blocks = of_kind(s, ElementKind::Block);
    REQUIRE(blocks.size() == 1);
    CHECK(blocks[0]->closed);
    CHECK(signed_volume(s, *blocks[0]) > 0);
    const Box3 bb = box_of(element_points(s, *blocks[0]));
    CHECK(bb.lo.z <= -4.0 - 1.0 + 1e-6);  // below the ditch floor
    const auto parapets = of_kind(s, ElementKind::Parapet);
    REQUIRE(parapets.size() == 2);
    double lo_x = 1e9, hi_x = -1e9;
    for (const StructureElement* el : parapets) {
        const Box3 bp = box_of(element_points(s, *el));
        // attached: the parapet's outer face is the block's side, it overlaps the block top, spans the block lengthwise
        const bool left = 0.5 * (bp.lo.x + bp.hi.x) < 0;
        CHECK((left ? bp.lo.x : bp.hi.x) == doctest::Approx(left ? bb.lo.x : bb.hi.x).epsilon(1e-6).scale(1.0));
        CHECK(bp.lo.z < bb.hi.z);
        CHECK(bp.hi.z > bb.hi.z + 0.9);
        CHECK(bp.lo.y == doctest::Approx(bb.lo.y).epsilon(1e-6).scale(1.0));
        CHECK(bp.hi.y == doctest::Approx(bb.hi.y).epsilon(1e-6).scale(1.0));
        lo_x = std::min(lo_x, bp.lo.x);
        hi_x = std::max(hi_x, bp.hi.x);
    }
    CHECK(lo_x == doctest::Approx(-hi_x).epsilon(1e-6).scale(1.0));  // symmetric about the centreline
    // Wing coping (real bridge over a road): a cap barely wider than the 0.4 m wing face.
    Bridged b(uk());
    const StructureMeshes deep = build_edge_structures(b.w.g, *b.w.region, b.e, kFlat0);
    int caps = 0;
    for (const StructureElement* el : of_kind(deep, ElementKind::WingWall)) {
        if (el->surface != structure_surface::coping) continue;
        ++caps;
        const Box3 bx = box_of(element_points(deep, *el));
        CHECK(bx.hi.x - bx.lo.x < 0.6);
        CHECK(bx.hi.z - bx.lo.z < 0.2);
    }
    CHECK(caps == 4);
}

TEST_CASE("walls: portal wall level is reported and the exclusion zone drops walls") {
    const FnGround ground([](double, double) { return 3.0; });
    WallContext ctx;
    WallPortal pt;
    pt.pos = {60, 0};
    pt.heading = 0;
    pt.dir = 1;
    pt.half_width = 5.4;
    ctx.portals.push_back(pt);
    const StructureMeshes s = build_retaining_walls({straight_run(0, 60, 5.4, 'L')}, ground, {}, ctx);
    check_valid(s);
    REQUIRE(s.wall_ends.size() == 1);
    CHECK(s.wall_ends[0].side == 1);
    CHECK(s.wall_ends[0].level >= 3.3);
    // An exclusion disc removes the wall inside it.
    WallContext ex;
    ex.exclusion.push_back({{30, 5.4}, 10});
    const StructureMeshes s2 = build_retaining_walls({straight_run(0, 60, 5.4)}, ground, {}, ex);
    REQUIRE_FALSE(of_kind(s2, ElementKind::RetainingWall).empty());
    for (const StructureElement* el : of_kind(s2, ElementKind::RetainingWall))
        for (const Vec3& p : element_points(s2, *el)) CHECK(!(p.x > 21 && p.x < 39));
}

TEST_CASE("structures: both driving sides mirror, and builds are deterministic") {
    Bridged l(uk());
    Bridged r(uk_right());
    const StructureMeshes sl = build_edge_structures(l.w.g, *l.w.region, l.e, kFlat0);
    const StructureMeshes sr = build_edge_structures(r.w.g, *r.w.region, r.e, kFlat0);
    check_valid(sr);
    CHECK(tri_count(sl) == tri_count(sr));
    CHECK(sl.elements.size() == sr.elements.size());
    CHECK(sl.bridges[0].pier_s.size() == sr.bridges[0].pier_s.size());
    for (const auto& [name, m] : sl.meshes) {
        const Box3 a = box_of(m.positions), b = box_of(sr.meshes.at(name).positions);
        CHECK(a.lo.x == doctest::Approx(b.lo.x).epsilon(1e-6));
        CHECK(a.hi.x == doctest::Approx(b.hi.x).epsilon(1e-6));
        CHECK(a.hi.z == doctest::Approx(b.hi.z).epsilon(1e-6));
    }
    // Deterministic: same input, bit-identical output.
    const StructureMeshes sl2 = build_edge_structures(l.w.g, *l.w.region, l.e, kFlat0);
    REQUIRE(sl.meshes.size() == sl2.meshes.size());
    for (const auto& [name, m] : sl.meshes) {
        const WorldMesh& m2 = sl2.meshes.at(name);
        REQUIRE(m.positions.size() == m2.positions.size());
        bool same = m.indices == m2.indices;
        for (std::size_t i = 0; i < m.positions.size() && same; ++i)
            same = m.positions[i].x == m2.positions[i].x && m.positions[i].y == m2.positions[i].y && m.positions[i].z == m2.positions[i].z;
        CHECK(same);
    }
}

TEST_CASE("structures: the interchange bridge") {
    std::string err;
    auto sc = tools::build_scene("interchange", CP_DATA_DIR, err);
    REQUIRE_MESSAGE(sc, err);
    const FnGround ground([](double, double) { return 0.0; });
    const StructureMeshes s = build_structures(sc->graph, *sc->region, ground);
    check_valid(s);
    REQUIRE(s.bridges.size() == 1);
    const BridgeInfo& b = s.bridges[0];
    CHECK(b.crosses_road);
    CHECK_FALSE(b.solid_block);
    CHECK(b.s1 - b.s0 == doctest::Approx(40.0).epsilon(0.01));
    // The fixture's central reservation is only 0.2 m wide (carriageways 11 m apart, 10.8 m cross-sections): no pier fits,
    // so the 40 m span is flagged rather than a pier put on a carriageway or hard strip.
    CHECK(b.span_too_long);
    CHECK(b.pier_s.empty());
    CHECK(of_kind(s, ElementKind::Deck).size() == 1);
    CHECK(of_kind(s, ElementKind::Parapet).size() == 2);
    CHECK(of_kind(s, ElementKind::AbutmentStem).size() == 2);
    CHECK(of_kind(s, ElementKind::WingWall).size() == 8);
    std::size_t deck_tris = 0;
    for (const StructureElement* el : of_kind(s, ElementKind::Deck)) deck_tris += range_of(s, *el).index_count / 3;
    CHECK(deck_tris > 100);
    CHECK(tri_count(s) > 1000);
    CHECK(tri_count(s) < 20000);
    MESSAGE("interchange bridge: " << tri_count(s) << " triangles, " << deck_tris << " in the deck, " << b.pier_s.size() << " piers");
}

TEST_CASE("structures: chunks keep every triangle and the vertex colours") {
    World w;
    const EdgeId e = w.road({0, 0, 0}, {300, 40, 10}, "b_road");
    const double len = w.g.edge(e)->horizontal.length();
    w.span(e, 50, 130, StructureKind::Bridge);
    w.span(e, 170, len - 40, StructureKind::Tunnel);
    const FnGround hill([](double, double) { return 30.0; });
    const StructureMeshes s = build_edge_structures(w.g, *w.region, e, hill);
    check_valid(s);
    const auto chunks = build_structure_chunks(s, ChunkParams{64.0});
    CHECK(chunks.size() > 3);
    std::size_t tris = 0;
    bool colour_deep = false;
    for (const auto& [tk, ch] : chunks)
        for (const auto& [name, buf] : ch.surfaces) {
            tris += buf.indices.size() / 3;
            CHECK(buf.colours.size() == buf.positions.size());
            CHECK(buf.tangents.size() == buf.positions.size());
            for (const auto& c : buf.colours) colour_deep |= c[0] > 100;
        }
    CHECK(tris == tri_count(s));
    CHECK(colour_deep);
}
