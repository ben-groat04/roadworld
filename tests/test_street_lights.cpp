// Street light placement (sim_core/src/street_lights.cpp, data/regions/uk/road_types.json).
#include <algorithm>
#include <cmath>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "doctest/doctest.h"
#include "cp/lanes.h"
#include "cp/road_commands.h"
#include "cp/street_lights.h"
#include "scene_fixture.h"

using namespace cp;

namespace {

constexpr double kDeg = kPi / 180;

std::shared_ptr<const RegionPack> pack(DrivingSide side, const std::function<void(RegionPack&)>& tweak = {}) {
    auto p = load_region_pack_by_id(CP_DATA_DIR, "uk");
    REQUIRE_MESSAGE(p, p.error);
    p.value->driving_side = side;
    if (tweak) tweak(*p.value);
    return std::make_shared<const RegionPack>(*p);
}

struct Net {
    std::shared_ptr<const RegionPack> region;
    RoadGraph g;
    CommandStack<RoadGraph> st{g};

    explicit Net(DrivingSide side = DrivingSide::Left, const std::function<void(RegionPack&)>& tweak = {})
        : region(pack(side, tweak)) {
        g.set_region(region);
    }

    // Draws a road a -> b (new nodes unless existing ones are given); returns the new edge.
    EdgeId road(Vec3 a, Vec3 b, const char* type = "residential", bool one_way = false, NodeId na = {}, NodeId nb = {},
                std::vector<Vec3> via = {}) {
        DrawRoadSpec s;
        s.road_type = type;
        s.one_way = one_way;
        s.start.pos = a;
        s.end.pos = b;
        s.start.existing_node = na;
        s.end.existing_node = nb;
        s.interior = std::move(via);
        REQUIRE(st.execute(make_draw_road(s)));
        return g.edges().rbegin()->first;
    }
    // Spokes out of one centre node at the origin.
    NodeId spokes(const std::vector<double>& deg, double len, const char* type = "b_road") {
        NodeId centre;
        for (double d : deg) {
            const EdgeId id = road({0, 0, 0}, {len * std::cos(d * kDeg), len * std::sin(d * kDeg), 0}, type, false, centre);
            if (!centre.valid()) centre = g.edge(id)->a;
        }
        return centre;
    }
    JunctionSet junctions(RoundaboutResult* rr = nullptr) const {
        JunctionSet js = build_plain_junctions(g, *region, {});
        RoundaboutResult r = apply_roundabouts(js, g, *region, {});
        if (rr) *rr = std::move(r);
        return js;
    }
};

std::vector<RoundaboutLayout> layouts_of(const RoundaboutResult& rr) { return rr.layouts; }

std::vector<StreetLight> all_lights(const Net& n, const JunctionSet& js, const std::vector<RoundaboutLayout>* rb = nullptr,
                                    const StreetLightParams& p = {}) {
    return generate_street_lights(n.g, *n.region, js, rb, p).merged();
}

Pose pose_at(const Edge& e, double s) {
    double acc = 0;
    for (std::size_t i = 0; i < e.horizontal.spans.size(); ++i) {
        const Clothoid& c = e.horizontal.spans[i];
        if (s <= acc + c.length || i + 1 == e.horizontal.spans.size()) return c.pose(std::clamp(s - acc, 0.0, c.length));
        acc += c.length;
    }
    return {};
}

// Lateral offset of a light's base from its edge's centreline at its s (+ = left of a -> b).
double lateral_of(const RoadGraph& g, const StreetLight& l) {
    const Edge* e = g.edge(l.edge);
    REQUIRE(e);
    const Pose p = pose_at(*e, l.s);
    return (l.position.x - p.pos.x) * -std::sin(p.heading) + (l.position.y - p.pos.y) * std::cos(p.heading);
}

bool in_poly(Vec2 p, const std::vector<Vec2>& ring) {
    bool in = false;
    for (std::size_t i = 0, j = ring.size() - 1; i < ring.size(); j = i++) {
        const Vec2 a = ring[i], b = ring[j];
        if ((a.y > p.y) != (b.y > p.y) && p.x < (b.x - a.x) * (p.y - a.y) / (b.y - a.y) + a.x) in = !in;
    }
    return in;
}

bool in_carriageway(const JunctionSet& js, Vec2 p) {
    for (const auto& [id, j] : js.junctions) {
        bool in = false;
        for (const auto& ring : j.carriageway)
            if (ring.size() >= 3 && in_poly(p, ring)) in = !in;
        if (in) return true;
    }
    return false;
}

bool finite(const StreetLight& l) {
    return std::isfinite(l.position.x) && std::isfinite(l.position.y) && std::isfinite(l.position.z) && std::isfinite(l.heading);
}

// Not on a driving / parking / shoulder lane of its own edge (median columns sit between edges).
bool off_traffic_lanes(const Net& n, const StreetLight& l) {
    if (l.side == LightSide::Median || !l.edge.valid()) return true;
    const Edge* e = n.g.edge(l.edge);
    const RoadType* rt = n.region->road_type(e->road_type);
    const double d = lateral_of(n.g, l);
    for (const Lane& lane : instantiate_lanes(*e, *rt, n.region->driving_side)) {
        const LaneType* lt = n.region->lane_type(lane.lane_type);
        if (!lt || lt->profile != "carriageway") continue;
        if (d > lane.offset - lane.width / 2 && d < lane.offset + lane.width / 2) return false;
    }
    return true;
}

std::vector<StreetLight> of_origin(const std::vector<StreetLight>& v, LightOrigin o) {
    std::vector<StreetLight> out;
    for (const StreetLight& l : v)
        if (l.origin == o) out.push_back(l);
    return out;
}

bool same(const StreetLight& a, const StreetLight& b, double tol = 0) {
    return std::abs(a.position.x - b.position.x) <= tol && std::abs(a.position.y - b.position.y) <= tol &&
           std::abs(a.position.z - b.position.z) <= tol && std::abs(a.heading - b.heading) <= tol && a.style == b.style &&
           a.edge == b.edge && a.node == b.node && std::abs(a.s - b.s) <= tol && a.side == b.side && a.origin == b.origin &&
           a.on_structure == b.on_structure && a.twin_arm == b.twin_arm && a.partner_edge == b.partner_edge;
}

bool same_set(const std::vector<StreetLight>& a, const std::vector<StreetLight>& b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i)
        if (!same(a[i], b[i])) return false;
    return true;
}

}  // namespace

TEST_CASE("street lights: region pack data") {
    Net n;
    const RoadType* res = n.region->road_type("residential");
    REQUIRE(res);
    CHECK(res->street_lights.lit);
    CHECK(res->street_lights.spacing > 20);
    CHECK_FALSE(n.region->road_type("motorway")->street_lights.lit);  // UK motorway: unlit by default
    CHECK(n.region->road_type("a_road_dual")->street_lights.arrangement == LightArrangement::CentralReservation);
    CHECK(n.region->road_type("roundabout_ring")->street_lights.island_min_radius > 0);

    // Loader validation.
    auto lt = load_region_pack_by_id(CP_DATA_DIR, "uk");
    REQUIRE(lt);
    const std::string rj = R"({"id":"x","driving_side":"left","speed_unit":"mph"})";
    const std::string lanes = R"({"lane_types":[{"id":"driving","users":["car"],"profile":"carriageway"}]})";
    const auto lane_types = parse_lane_types(lanes);
    REQUIRE(lane_types);
    const auto road = [&](const std::string& sl) {
        return std::string(R"({"road_types":[{"id":"r","two_way":true,"speed_limit":30,"design_speed_kph":50,
            "half":[{"lane":"driving","width_m":3}],"one_way_variant":[{"lane":"driving","width_m":3}],
            "vertical_k":{"crest":10,"sag":9},"street_lights":)") + sl + "}]}";
    };
    CHECK(parse_region_pack(*lane_types, rj, road(R"({"spacing_m":30,"arrangement":"staggered"})")));
    CHECK_FALSE(parse_region_pack(*lane_types, rj, road(R"({"arrangement":"central_reservation"})")));  // not a dual type
    CHECK_FALSE(parse_region_pack(*lane_types, rj, road(R"({"arrangement":"sideways"})")));
    CHECK_FALSE(parse_region_pack(*lane_types, rj, road(R"({"spacing_m":1})")));
    CHECK_FALSE(parse_region_pack(*lane_types, rj, road(R"({"mounting_height_m":-3})")));
    auto none = parse_region_pack(*lane_types, rj, road(R"({"lit":false})"));
    REQUIRE(none);
    CHECK_FALSE(none->road_types.at("r").street_lights.lit);
}

TEST_CASE("street lights: spacing arithmetic") {
    // 300 m at 30 m: ten cells of exactly 30 m.
    LightPattern p = plan_light_pattern(300, 30);
    CHECK(p.count == 10);
    CHECK(p.cell == doctest::Approx(30));
    CHECK(p.within_tolerance);
    // 310 m: still ten cells (31 m), not eleven short ones.
    p = plan_light_pattern(310, 30);
    CHECK(p.count == 10);
    CHECK(p.cell == doctest::Approx(31));
    // Anything past about three spacings lands within +-20%.
    for (double len = 90; len < 900; len += 7.3) {
        p = plan_light_pattern(len, 30);
        CHECK(p.within_tolerance);
        CHECK(std::abs(p.cell / 30 - 1) <= 0.2 + 1e-9);
        CHECK(p.cell * p.count == doctest::Approx(len));
    }
    // Very short lengths: none below half a spacing, one above.
    CHECK(plan_light_pattern(10, 30).count == 0);
    CHECK(plan_light_pattern(0, 30).count == 0);
    CHECK(plan_light_pattern(-5, 30).count == 0);
    CHECK(plan_light_pattern(16, 30).count == 1);
    CHECK(plan_light_pattern(40, 30).count == 1);
    CHECK(plan_light_pattern(50, 30).count == 2);

    // On a real straight edge: counts follow, and the columns are symmetric about the edge.
    Net n;
    const EdgeId id = n.road({0, 0, 0}, {200, 0, 0});
    const JunctionSet js = n.junctions();
    const std::vector<StreetLight> v = generate_edge_street_lights(n.g, *n.region, js, id);
    const LightPattern expect = plan_light_pattern(200, n.region->road_type("residential")->street_lights.spacing);
    REQUIRE(static_cast<int>(v.size()) == expect.count);
    for (std::size_t i = 0; i < v.size(); ++i) {
        CHECK(v[i].s == doctest::Approx((static_cast<double>(i) + 0.5) * expect.cell));
        CHECK(v[i].s + v[v.size() - 1 - i].s == doctest::Approx(200));
    }
    // A very short edge gets none, a mid one gets one.
    Net m;
    const EdgeId tiny = m.road({0, 0, 0}, {12, 0, 0});
    CHECK(generate_edge_street_lights(m.g, *m.region, m.junctions(), tiny).empty());
    Net o;
    const EdgeId mid = o.road({0, 0, 0}, {35, 0, 0});
    CHECK(generate_edge_street_lights(o.g, *o.region, o.junctions(), mid).size() == 1);
}

TEST_CASE("street lights: arrangements on a straight road") {
    for (DrivingSide side : {DrivingSide::Left, DrivingSide::Right}) {
        const double ns = side == DrivingSide::Left ? 1.0 : -1.0;
        {  // single_side: every column on the nearside of the run direction, at the back of the footway
            Net n(side);
            const EdgeId id = n.road({0, 0, 0}, {240, 0, 0}, "residential");
            const std::vector<StreetLight> v = all_lights(n, n.junctions());
            REQUIRE(v.size() >= 6);
            const RoadType* rt = n.region->road_type("residential");
            for (const StreetLight& l : v) {
                CHECK(l.edge == id);
                CHECK(l.side == (ns > 0 ? LightSide::Left : LightSide::Right));
                CHECK(lateral_of(n.g, l) == doctest::Approx(ns * (3.0 + 2.0 + 0.15 + 2.0 - rt->street_lights.footway_inset)));
                CHECK(l.mounting_height == doctest::Approx(6));
                CHECK(l.style == "residential_column_6m");
                CHECK(l.position.z > -0.01);
                CHECK(l.position.z < 0.1);
                // Bracket points at the carriageway: perpendicular to +x, toward y = 0.
                CHECK(std::sin(l.heading) * l.position.y < 0);
                CHECK(std::abs(std::cos(l.heading)) < 1e-9);
                CHECK(off_traffic_lanes(n, l));
            }
        }
        {  // staggered: alternate sides, nearside first, consecutive spacing near the rule
            Net n(side);
            n.road({0, 0, 0}, {350, 0, 0}, "b_road");
            const std::vector<StreetLight> v = all_lights(n, n.junctions());
            REQUIRE(v.size() >= 8);
            for (std::size_t i = 0; i < v.size(); ++i) {
                const bool near_first = (i % 2 == 0);
                CHECK((v[i].position.y * ns > 0) == near_first);
                CHECK(std::abs(lateral_of(n.g, v[i])) == doctest::Approx(5.4 - 0.3));
                if (i > 0) CHECK(v[i].s - v[i - 1].s == doctest::Approx(v[1].s - v[0].s));
            }
            CHECK(v[1].s - v[0].s >= 35 * 0.8);
            CHECK(v[1].s - v[0].s <= 35 * 1.2);
        }
        {  // opposite: a column on each side at every station
            Net n(side, [](RegionPack& r) { r.road_types["b_road"].street_lights.arrangement = LightArrangement::Opposite; });
            n.road({0, 0, 0}, {350, 0, 0}, "b_road");
            const std::vector<StreetLight> v = all_lights(n, n.junctions());
            REQUIRE(v.size() >= 8);
            CHECK(v.size() % 2 == 0);
            std::map<long long, int> left, right;
            for (const StreetLight& l : v) (l.position.y > 0 ? left : right)[std::llround(l.s * 1e3)]++;
            CHECK(left == right);
        }
    }
}

TEST_CASE("street lights: verge placement and unlit types") {
    // A lone dual carriageway edge has no partner: verge-side columns beyond the hard strip.
    Net n;
    const EdgeId id = n.road({0, 0, 0}, {400, 0, 0}, "a_road_dual", true);
    const std::vector<StreetLight> v = all_lights(n, n.junctions());
    REQUIRE(v.size() >= 6);
    const Edge* e = n.g.edge(id);
    std::vector<Lane> lanes = instantiate_lanes(*e, *n.region->road_type("a_road_dual"), n.region->driving_side);
    double cw_hi = -1e9, ext_hi = -1e9;
    for (const Lane& l : lanes) {
        ext_hi = std::max(ext_hi, l.offset + l.width / 2);
        if (n.region->lane_type(l.lane_type)->profile == "carriageway") cw_hi = std::max(cw_hi, l.offset + l.width / 2);
    }
    for (const StreetLight& l : v) {
        CHECK_FALSE(l.twin_arm);
        CHECK(l.side == LightSide::Left);
        const double d = lateral_of(n.g, l);
        CHECK(d > cw_hi);
        CHECK(d < ext_hi);
        CHECK(off_traffic_lanes(n, l));
    }
    // Motorway is unlit in the UK pack, lit when the rule says so.
    Net m;
    m.road({0, 0, 0}, {400, 0, 0}, "motorway", true);
    CHECK(all_lights(m, m.junctions()).empty());
    Net lit(DrivingSide::Left, [](RegionPack& r) { r.road_types["motorway"].street_lights.lit = true; });
    lit.road({0, 0, 0}, {400, 0, 0}, "motorway", true);
    CHECK_FALSE(all_lights(lit, lit.junctions()).empty());
}

TEST_CASE("street lights: dual carriageway pair lights the median once") {
    for (DrivingSide side : {DrivingSide::Left, DrivingSide::Right}) {
        Net n(side);
        // Left driving: eastbound on the north side; right driving: eastbound on the south side.
        const double o = side == DrivingSide::Left ? 5.5 : -5.5;
        const EdgeId east = n.road({0, o, 0}, {500, o, 0}, "a_road_dual", true);
        const EdgeId west = n.road({500, -o, 0}, {0, -o, 0}, "a_road_dual", true);
        const JunctionSet js = n.junctions();
        const std::vector<StreetLight> v = all_lights(n, js);
        REQUIRE(v.size() >= 8);
        const LightPattern pat = plan_light_pattern(500, 50);
        CHECK(static_cast<int>(v.size()) == pat.count);  // once, not once per carriageway
        const EdgeId owner = std::min(east, west);
        for (const StreetLight& l : v) {
            CHECK(l.twin_arm);
            CHECK(l.side == LightSide::Median);
            CHECK(l.edge == owner);
            CHECK(l.partner_edge == std::max(east, west));
            CHECK(l.position.y == doctest::Approx(0).epsilon(1e-6));
            // Points at the owner's carriageway.
            const double oy = owner == east ? o : -o;
            CHECK(std::sin(l.heading) * oy > 0);
        }
        // The same query per edge: the non-owner contributes nothing.
        CHECK(generate_edge_street_lights(n.g, *n.region, js, std::max(east, west)).empty());
        CHECK(generate_edge_street_lights(n.g, *n.region, js, owner).size() == v.size());
    }
}

TEST_CASE("street lights: joint continuity") {
    // Two collinear edges of one type behave as a single edge; a type change breaks the run.
    Net a;
    a.road({0, 0, 0}, {300, 0, 0}, "residential");
    const std::vector<StreetLight> whole = all_lights(a, a.junctions());
    Net b;
    const EdgeId e1 = b.road({0, 0, 0}, {137, 0, 0}, "residential");
    const NodeId mid = b.g.edge(e1)->b;
    b.road({137, 0, 0}, {300, 0, 0}, "residential", false, mid);
    const std::vector<StreetLight> split = all_lights(b, b.junctions());
    REQUIRE(whole.size() == split.size());
    REQUIRE(whole.size() >= 8);
    for (std::size_t i = 0; i < whole.size(); ++i) {
        CHECK(whole[i].position.x == doctest::Approx(split[i].position.x));
        CHECK(whole[i].position.y == doctest::Approx(split[i].position.y));
    }
    // Different types: each side lays out its own pattern from the joint.
    Net c;
    const EdgeId c1 = c.road({0, 0, 0}, {137, 0, 0}, "residential");
    c.road({137, 0, 0}, {300, 0, 0}, "b_road", false, c.g.edge(c1)->b);
    const std::vector<StreetLight> mixed = all_lights(c, c.junctions());
    CHECK(mixed.size() >= 8);
    // The first edge's columns are exactly those of a lone 137 m residential edge.
    Net d;
    d.road({0, 0, 0}, {137, 0, 0}, "residential");
    const std::vector<StreetLight> lone = all_lights(d, d.junctions());
    std::vector<StreetLight> first;
    for (const StreetLight& l : mixed)
        if (l.edge == c1) first.push_back(l);
    REQUIRE(first.size() == lone.size());
    for (std::size_t i = 0; i < lone.size(); ++i) CHECK(first[i].position.x == doctest::Approx(lone[i].position.x));
}

TEST_CASE("street lights: bends, structures and heights") {
    {  // a tight left bend: single_side columns go to the outside (the right of a -> b)
        Net n;
        std::vector<Vec3> via;
        for (int k = 1; k < 8; ++k) {
            const double a = k * 12.0 * kDeg;
            via.push_back({60 * std::sin(a), 60 - 60 * std::cos(a), 0});
        }
        const EdgeId id = n.road({0, 0, 0}, {60 * std::sin(96 * kDeg), 60 - 60 * std::cos(96 * kDeg), 0}, "residential", false, {}, {}, via);
        const std::vector<StreetLight> v = all_lights(n, n.junctions());
        REQUIRE(v.size() >= 3);
        const double len = n.g.edge(id)->horizontal.length();
        int mid = 0;
        for (const StreetLight& l : v) {
            CHECK(l.edge == id);
            if (l.s < 0.3 * len || l.s > 0.7 * len) continue;  // the ends are nearly straight
            ++mid;
            CHECK(lateral_of(n.g, l) < 0);
            CHECK(l.side == LightSide::Right);
        }
        CHECK(mid >= 1);
    }
    {  // bridge: columns at the parapet line and flagged; tunnel: none
        Net n;
        StreetLightParams P;
        P.parapet_inset = 0.1;
        const EdgeId id = n.road({0, 0, 0}, {400, 0, 0}, "residential");
        REQUIRE(n.st.execute(make_set_structure_span(id, 100, 200, StructureKind::Bridge)));
        REQUIRE(n.st.execute(make_set_structure_span(id, 260, 340, StructureKind::Tunnel)));
        const std::vector<StreetLight> v = all_lights(n, n.junctions(), nullptr, P);
        REQUIRE(v.size() >= 8);
        int on_bridge = 0;
        for (const StreetLight& l : v) {
            CHECK_FALSE((l.s >= 260 && l.s <= 340));
            if (l.s >= 100 && l.s <= 200) {
                ++on_bridge;
                CHECK(l.on_structure);
                CHECK(std::abs(lateral_of(n.g, l)) == doctest::Approx(7.15 - 0.1));
            } else {
                CHECK_FALSE(l.on_structure);
                CHECK(std::abs(lateral_of(n.g, l)) == doctest::Approx(7.15 - 0.3));
            }
        }
        CHECK(on_bridge >= 2);
    }
    {  // the base follows the vertical profile (footway sits a little above the centreline)
        Net n;
        const EdgeId id = n.road({0, 0, 0}, {300, 0, 6}, "b_road");
        const std::vector<StreetLight> v = all_lights(n, n.junctions());
        REQUIRE(v.size() >= 6);
        const Edge* e = n.g.edge(id);
        for (const StreetLight& l : v) {
            const double dz = l.position.z - e->vertical.z(l.s);
            CHECK(dz > -0.01);
            CHECK(dz < 0.15);
        }
        // Flat edge: exact kerb + footway height (crown 0 - cf * 3.25 + upstand 0.125 + 0.02 * 1.7).
        Net f;
        f.road({0, 0, 3}, {300, 0, 3}, "b_road");
        for (const StreetLight& l : all_lights(f, f.junctions())) {
            const double cf = f.region->road_type("b_road")->crossfall.value_or(0.025);
            CHECK(l.position.z == doctest::Approx(3 - cf * 3.25 + 0.125 + 0.02 * (2.0 - 0.3)).epsilon(1e-9));
        }
    }
}

TEST_CASE("street lights: junction corners") {
    for (DrivingSide side : {DrivingSide::Left, DrivingSide::Right}) {
        {  // crossroads: one column per corner, clear of kerbs and the carriageway
            Net n(side);
            n.spokes({0, 90, 180, 270}, 120, "b_road");
            const JunctionSet js = n.junctions();
            REQUIRE(js.junctions.size() == 1);
            const Junction& j = js.junctions.begin()->second;
            const std::vector<StreetLight> v = all_lights(n, js);
            const std::vector<StreetLight> corners = of_origin(v, LightOrigin::JunctionCorner);
            CHECK(corners.size() == 4);
            for (const StreetLight& l : corners) {
                CHECK(l.node == j.node);
                CHECK_FALSE(in_carriageway(js, l.position.xy()));
                CHECK(off_traffic_lanes(n, l));
                for (const CornerCurve& c : j.corners)
                    for (Vec3 k : c.kerb) CHECK((k.xy() - l.position.xy()).length() >= 1.0);
                // Beyond the trim line of its arm.
                CHECK(l.position.xy().length() > 10);
            }
            // No two junction columns on the same arm side, and each corner has one in its quadrant.
            std::set<int> quadrants;
            for (const StreetLight& l : corners)
                quadrants.insert((l.position.x > 0 ? 1 : 0) + (l.position.y > 0 ? 2 : 0));
            CHECK(quadrants.size() == 4);
            for (const StreetLight& l : v) CHECK_FALSE(in_carriageway(js, l.position.xy()));
        }
        {  // T-junction: the two corners beside the stem; the straight side has none
            Net n(side);
            n.spokes({0, 180, 270}, 120, "b_road");
            const JunctionSet js = n.junctions();
            const std::vector<StreetLight> corners = of_origin(all_lights(n, js), LightOrigin::JunctionCorner);
            CHECK(corners.size() == 2);
            for (const StreetLight& l : corners) {
                CHECK_FALSE(in_carriageway(js, l.position.xy()));
                CHECK(l.position.y < 0);  // beside the stem (heading south), not opposite it
            }
        }
    }
}

TEST_CASE("street lights: crossings and unlit arms") {
    Net n;
    n.spokes({0, 90, 180, 270}, 120, "b_road");
    const JunctionSet js = n.junctions();
    const Junction& j = js.junctions.begin()->second;
    REQUIRE_FALSE(j.crossings.empty());
    for (const StreetLight& l : all_lights(n, js)) {
        for (const CrossingSpot& c : j.crossings) {
            const Vec2 d{std::cos(c.heading) * c.length / 2, std::sin(c.heading) * c.length / 2};
            const Vec2 m = c.centre.xy(), p = l.position.xy();
            const Vec2 ab = d * 2;
            const double t = std::clamp((p - (m - d)).dot(ab) / ab.dot(ab), 0.0, 1.0);
            CHECK(((m - d) + ab * t - p).length() >= 2.5);
        }
    }
    // Unlit approach roads give no columns at their junction.
    Net u(DrivingSide::Left, [](RegionPack& r) { r.road_types["b_road"].street_lights.lit = false; });
    u.spokes({0, 90, 180, 270}, 120, "b_road");
    CHECK(all_lights(u, u.junctions()).empty());
}

TEST_CASE("street lights: roundabout ring and island") {
    for (DrivingSide side : {DrivingSide::Left, DrivingSide::Right}) {
        Net n(side);
        n.spokes({0, 90, 180, 270}, 150, "b_road");
        REQUIRE(n.st.execute(make_place_roundabout({0, 0, 0}, 25, "roundabout_ring", *n.region)));
        RoundaboutResult rr;
        const JunctionSet js = n.junctions(&rr);
        REQUIRE(rr.layouts.size() == 1);
        const std::vector<RoundaboutLayout> lay = layouts_of(rr);
        const std::vector<StreetLight> v = all_lights(n, js, &lay);
        const std::vector<StreetLight> ring = of_origin(v, LightOrigin::RoundaboutRing);
        const std::vector<StreetLight> island = of_origin(v, LightOrigin::RoundaboutIsland);
        CHECK(ring.size() >= 3);
        CHECK(ring.size() <= 9);
        for (const StreetLight& l : ring) {
            // Round the outside: outer footway line (half width 5.65 less the inset) outside the ring centreline.
            CHECK(l.position.xy().length() == doctest::Approx(25 + 5.65 - 0.3).epsilon(0.01));
            CHECK_FALSE(in_carriageway(js, l.position.xy()));
            CHECK(std::cos(l.heading) * l.position.x + std::sin(l.heading) * l.position.y < 0);  // bracket points inward
        }
        REQUIRE(island.size() == 1);
        CHECK(island[0].position.xy().length() < 1e-6);
        // The ring edges are lit by the roundabout generator only.
        for (EdgeId id : lay[0].group.ring_edges) CHECK(generate_edge_street_lights(n.g, *n.region, js, id, &lay).empty());
        // Without layouts the ring is lit edge by edge (outside of the bend).
        const std::vector<StreetLight> plain = all_lights(n, js);
        CHECK(of_origin(plain, LightOrigin::RoundaboutRing).empty());
        CHECK_FALSE(plain.empty());
        for (const StreetLight& l : plain) CHECK_FALSE(in_carriageway(js, l.position.xy()));
        // Approach arms still get corner columns past the splitter islands.
        CHECK(of_origin(v, LightOrigin::JunctionCorner).size() >= 4);
    }
}

TEST_CASE("street lights: mirror under driving side") {
    // The same network drawn with y negated and the driving side flipped mirrors the result.
    const auto build = [](DrivingSide side, double ysign) {
        auto n = std::make_unique<Net>(side);
        const auto Y = [ysign](double y) { return ysign * y; };
        n->spokes({0, 180, 270 * ysign}, 140, "b_road");
        const NodeId east = n->g.edges().begin()->second.b;
        const std::vector<Vec3> via{{170, Y(80), 1}, {140, Y(150), 2}};
        n->road({140, 0, 0}, {100, Y(260), 3}, "residential", false, east, {}, via);
        return n;
    };
    auto l = build(DrivingSide::Left, 1.0);
    auto r = build(DrivingSide::Right, -1.0);
    const std::vector<StreetLight> a = all_lights(*l, l->junctions());
    const std::vector<StreetLight> b = all_lights(*r, r->junctions());
    REQUIRE(a.size() >= 10);
    REQUIRE(a.size() == b.size());
    // Junction corners come out in a different order (arms are sorted CCW), so match by position.
    for (const StreetLight& p : a) {
        int found = 0;
        for (const StreetLight& q : b) {
            if (std::abs(p.position.x - q.position.x) > 1e-6 || std::abs(p.position.y + q.position.y) > 1e-6) continue;
            ++found;
            CHECK(p.position.z == doctest::Approx(q.position.z).epsilon(1e-9));
            CHECK(p.edge == q.edge);
            CHECK(p.origin == q.origin);
            CHECK(wrap_angle(p.heading + q.heading) == doctest::Approx(0).epsilon(1e-6));  // heading mirrors too
        }
        CHECK(found == 1);
    }
}

TEST_CASE("street lights: deterministic and dirty rebuild equals full rebuild") {
    Net n;
    n.spokes({0, 90, 180}, 160, "b_road");
    n.road({0, 160, 0}, {200, 160, 0}, "residential", false, std::next(n.g.edges().begin())->second.b);
    JunctionSet js = n.junctions();
    const std::vector<StreetLight> once = all_lights(n, js);
    const std::vector<StreetLight> twice = all_lights(n, js);
    CHECK(same_set(once, twice));
    REQUIRE(once.size() >= 10);

    // Build the cache, then add a spoke at the centre and a separate dual pair and rebuild only what is dirty.
    StreetLightResult cache = generate_street_lights(n.g, *n.region, js, nullptr);
    n.g.clear_dirty();
    NodeId centre;
    for (const auto& [id, j] : js.junctions) centre = id;
    REQUIRE(centre.valid());
    n.road({0, 0, 0}, {0, -150, 0}, "b_road", false, centre);
    n.road({-300, 411, 0}, {300, 411, 0}, "a_road_dual", true);
    n.road({300, 400, 0}, {-300, 400, 0}, "a_road_dual", true);
    js = n.junctions();
    const std::vector<NodeId> dirty(n.g.dirty_nodes().begin(), n.g.dirty_nodes().end());
    rebuild_dirty_street_lights(cache, n.g, *n.region, js, nullptr, dirty, {});
    const StreetLightResult full = generate_street_lights(n.g, *n.region, js, nullptr);
    CHECK(same_set(cache.merged(), full.merged()));
    CHECK(cache.edges.size() == full.edges.size());
    CHECK(cache.junctions.size() == full.junctions.size());

    // Deleting an edge of the dual pair: the survivor falls back to verge columns.
    n.g.clear_dirty();
    const EdgeId last = n.g.edges().rbegin()->first;
    REQUIRE(n.st.execute(make_delete_edge(last)));
    js = n.junctions();
    const std::vector<NodeId> dirty2(n.g.dirty_nodes().begin(), n.g.dirty_nodes().end());
    rebuild_dirty_street_lights(cache, n.g, *n.region, js, nullptr, dirty2, {last});
    const StreetLightResult full2 = generate_street_lights(n.g, *n.region, js, nullptr);
    CHECK(same_set(cache.merged(), full2.merged()));
}

TEST_CASE("street lights: interchange fixture") {
    std::string err;
    auto sc = tools::build_scene("interchange", CP_DATA_DIR, err);
    REQUIRE_MESSAGE(sc, err);
    RoundaboutResult rr;
    const JunctionSet js = tools::scene_junctions(*sc, &rr);
    const std::vector<StreetLight> v = generate_street_lights(sc->graph, *sc->region, js, &rr.layouts).merged();
    CHECK(v.size() > 40);
    CHECK(v.size() < 800);
    int median = 0, ring = 0, island = 0, corners = 0, bridge = 0;
    for (const StreetLight& l : v) {
        CHECK(finite(l));
        CHECK_FALSE(in_carriageway(js, l.position.xy()));
        median += l.twin_arm;
        ring += l.origin == LightOrigin::RoundaboutRing;
        island += l.origin == LightOrigin::RoundaboutIsland;
        corners += l.origin == LightOrigin::JunctionCorner;
        bridge += l.on_structure;
        if (l.edge.valid()) {
            const Edge* e = sc->graph.edge(l.edge);
            REQUIRE(e);
            for (const StructureSpan& sp : e->structure)
                if (sp.kind == StructureKind::Tunnel) CHECK_FALSE((l.s >= sp.s0 && l.s <= sp.s1));
        }
    }
    CHECK(median > 4);
    CHECK(ring >= 4);
    CHECK(island == 1);
    CHECK(corners >= 6);
    (void)bridge;
}
