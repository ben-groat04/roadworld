#include "doctest/doctest.h"
#include "cp/region_pack.h"

using namespace cp;

namespace {

const char* kLaneTypes = R"({"lane_types":[
  {"id":"driving","users":["car"],"may_park":false,"connects_at_junctions":true,"surface":"asphalt"},
  {"id":"kerb","users":[],"may_park":false,"connects_at_junctions":false,"surface":"kerb"}]})";

const char* kRegion = R"({"id":"t","driving_side":"right","speed_unit":"kph"})";

std::string road_types(const std::string& half_slot) {
    return R"({"road_types":[{"id":"r","two_way":true,"speed_limit":50,"design_speed_kph":50,
      "half":[)" + half_slot + R"(],"one_way_variant":[{"lane":"driving","width_m":3.0}],
      "vertical_k":{"crest":10,"sag":9}}]})";
}

}  // namespace

TEST_CASE("UK region pack loads from the repo data dir") {
    auto pack = load_region_pack_by_id(CP_DATA_DIR, "uk");
    REQUIRE_MESSAGE(pack, pack.error);
    CHECK(pack->id == "uk");
    CHECK(pack->driving_side == DrivingSide::Left);
    CHECK(pack->headroom_m == doctest::Approx(5.3));
    CHECK(pack->lane_types.size() == 7);
    CHECK(pack->road_types.size() == 9);

    const RoadType* res = pack->road_type("residential");
    REQUIRE(res);
    CHECK(res->two_way);
    CHECK(res->half.size() == 4);
    CHECK(res->one_way_variant.size() == 7);
    CHECK(res->speed_limit == doctest::Approx(30 * 0.44704));  // 13.4112 m/s
    CHECK(res->design_speed == doctest::Approx(50 / 3.6));
    CHECK(res->vertical_k_crest == 10);
    CHECK(res->vertical_k_sag == 9);
    CHECK(res->min_radius == 20);
    CHECK(res->advisory_max_grade == doctest::Approx(0.10));
    CHECK(res->half[0].lane_type == "driving");
    CHECK(res->half[0].dir == TravelDir::Forward);  // car lane default
    CHECK(res->half[1].dir == TravelDir::None);     // parking
    CHECK(res->one_way_variant[3].dir == TravelDir::Forward);

    const RoadType* dual = pack->road_type("a_road_dual");
    REQUIRE(dual);
    CHECK(dual->dual);
    CHECK(dual->median_gap == doctest::Approx(2.5));
    CHECK(dual->carriageway.size() == 5);
    CHECK(dual->carriageway[0].side == SlotSide::Offside);
    CHECK(dual->carriageway[1].side == SlotSide::Nearside);

    CHECK(pack->road_type("motorway")->carriageway.size() == 6);
    CHECK(pack->road_type("nope") == nullptr);
    CHECK(pack->lane_type("kerb")->surface == "kerb");
    CHECK(pack->lane_type("pavement")->users.at(0) == "pedestrian");
}

TEST_CASE("speed unit conversion") {
    auto lt = parse_lane_types(kLaneTypes);
    REQUIRE(lt);
    auto pack = parse_region_pack(*lt, kRegion, road_types(R"({"lane":"driving","width_m":3.0})"));
    REQUIRE_MESSAGE(pack, pack.error);
    CHECK(pack->driving_side == DrivingSide::Right);
    CHECK(pack->road_type("r")->speed_limit == doctest::Approx(50 / 3.6));
}

TEST_CASE("region pack validation reports clear errors") {
    auto lt = parse_lane_types(kLaneTypes);
    REQUIRE(lt);

    auto unknown = parse_region_pack(*lt, kRegion, road_types(R"({"lane":"bogus","width_m":3.0})"));
    CHECK_FALSE(unknown);
    CHECK(unknown.error.find("unknown lane type id \"bogus\"") != std::string::npos);
    CHECK(unknown.error.find("road type \"r\"") != std::string::npos);

    auto zero = parse_region_pack(*lt, kRegion, road_types(R"({"lane":"driving","width_m":0})"));
    CHECK_FALSE(zero);
    CHECK(zero.error.find("width_m must be positive") != std::string::npos);

    auto neg = parse_region_pack(*lt, kRegion, road_types(R"({"lane":"driving","width_m":-1.5})"));
    CHECK_FALSE(neg);

    auto bad_unit = parse_region_pack(*lt, R"({"id":"t","driving_side":"left","speed_unit":"furlongs"})",
                                      road_types(R"({"lane":"driving","width_m":3.0})"));
    CHECK_FALSE(bad_unit);
    CHECK(bad_unit.error.find("speed_unit") != std::string::npos);

    auto bad_side = parse_region_pack(*lt, R"({"id":"t","driving_side":"up","speed_unit":"mph"})",
                                      road_types(R"({"lane":"driving","width_m":3.0})"));
    CHECK_FALSE(bad_side);

    auto junk = parse_region_pack(*lt, "{not json", "{}");
    CHECK_FALSE(junk);
    CHECK(junk.error.find("invalid JSON") != std::string::npos);

    auto dup = parse_lane_types(R"({"lane_types":[{"id":"a"},{"id":"a"}]})");
    CHECK_FALSE(dup);

    CHECK_FALSE(load_region_pack_by_id(CP_DATA_DIR, "atlantis"));
}

TEST_CASE("roundabout ring road type and tunables") {
    auto pack = load_region_pack_by_id(CP_DATA_DIR, "uk");
    REQUIRE_MESSAGE(pack, pack.error);
    int flagged = 0;
    for (const auto& [id, t] : pack->road_types) {
        if (!t.roundabout_ring) continue;
        ++flagged;
        CHECK(!t.two_way);
        CHECK(!t.dual);
        CHECK(id == "roundabout_ring");
    }
    CHECK(flagged == 1);
    const RoadType* ring = pack->road_type("roundabout_ring");
    REQUIRE(ring);
    int driving = 0;
    for (const auto& s : ring->one_way_variant) driving += s.lane_type == "driving" ? 1 : 0;
    CHECK(driving == 2);
    CHECK(pack->road_type("b_road")->roundabout_ring == false);
    const RoundaboutRules& r = pack->roundabout;
    CHECK(r.max_tool_radius == doctest::Approx(100));
    CHECK(r.min_tool_radius == doctest::Approx(8));
    CHECK(r.entry_corner_radius == doctest::Approx(8));
    CHECK(r.exit_corner_radius == doctest::Approx(12));
}

TEST_CASE("roundabout_ring validation and fallbacks") {
    auto lt = parse_lane_types(kLaneTypes);
    REQUIRE(lt);
    const std::string two_way_ring = R"({"road_types":[{"id":"r","two_way":true,"roundabout_ring":true,"speed_limit":50,
      "design_speed_kph":50,"half":[{"lane":"driving","width_m":3.0}],"one_way_variant":[{"lane":"driving","width_m":3.0}],
      "vertical_k":{"crest":10,"sag":9}}]})";
    auto bad = parse_region_pack(*lt, kRegion, two_way_ring);
    CHECK_FALSE(bad);
    CHECK(bad.error.find("one-way") != std::string::npos);

    // A pack that omits the "roundabout" object gets the RoundaboutRules fallbacks.
    auto plain = parse_region_pack(*lt, kRegion, road_types(R"({"lane":"driving","width_m":3.0})"));
    REQUIRE_MESSAGE(plain, plain.error);
    CHECK(plain->roundabout.max_tool_radius == doctest::Approx(RoundaboutRules{}.max_tool_radius));
    CHECK(plain->road_type("r")->roundabout_ring == false);

    auto odd = parse_region_pack(*lt, R"({"id":"t","driving_side":"right","speed_unit":"kph",
        "roundabout":{"max_tool_radius_m":50,"min_tool_radius_m":60}})",
                                 road_types(R"({"lane":"driving","width_m":3.0})"));
    CHECK_FALSE(odd);
    auto set = parse_region_pack(*lt, R"({"id":"t","driving_side":"right","speed_unit":"kph",
        "roundabout":{"max_tool_radius_m":50}})",
                                 road_types(R"({"lane":"driving","width_m":3.0})"));
    REQUIRE_MESSAGE(set, set.error);
    CHECK(set->roundabout.max_tool_radius == doctest::Approx(50));
}

TEST_CASE("render cross-section fields are optional and validated") {
    auto lt = parse_lane_types(R"({"lane_types":[
      {"id":"driving","surface":"asphalt","connects_at_junctions":true,"users":["car"]},
      {"id":"kerb","surface":"kerb","profile":"kerb"}]})");
    REQUIRE_MESSAGE(lt, lt.error);
    CHECK(lt->at("driving").profile == "carriageway");
    CHECK(lt->at("kerb").profile == "kerb");
    CHECK_FALSE(parse_lane_types(R"({"lane_types":[{"id":"x","profile":"moat"}]})"));

    const auto rt = [](const std::string& extra) {
        return std::string(R"({"road_types":[{"id":"r","two_way":true,"speed_limit":50,"design_speed_kph":50,
          "half":[{"lane":"driving","width_m":3.0}],"one_way_variant":[{"lane":"driving","width_m":3.0}],
          "vertical_k":{"crest":10,"sag":9})") + extra + "}]}";
    };
    auto plain = parse_region_pack(*lt, kRegion, rt(""));
    REQUIRE_MESSAGE(plain, plain.error);
    CHECK_FALSE(plain->road_type("r")->crossfall.has_value());
    CHECK_FALSE(plain->road_type("r")->kerb_upstand.has_value());
    auto set = parse_region_pack(*lt, kRegion, rt(R"(,"crossfall":0.03,"kerb_upstand_m":0.1,"footway_crossfall":0.02,"verge_crossfall":0.05)"));
    REQUIRE_MESSAGE(set, set.error);
    CHECK(*set->road_type("r")->crossfall == doctest::Approx(0.03));
    CHECK(*set->road_type("r")->kerb_upstand == doctest::Approx(0.1));
    CHECK(*set->road_type("r")->footway_crossfall == doctest::Approx(0.02));
    CHECK(*set->road_type("r")->verge_crossfall == doctest::Approx(0.05));
    CHECK_FALSE(parse_region_pack(*lt, kRegion, rt(R"(,"crossfall":-0.1)")));
    CHECK_FALSE(parse_region_pack(*lt, kRegion, rt(R"(,"kerb_upstand_m":2)")));
    CHECK_FALSE(parse_region_pack(*lt, kRegion, rt(R"(,"crossfall":"flat")")));
}

TEST_CASE("roundabout island surfaces come from the region pack with defaults") {
    auto uk_pack = load_region_pack_by_id(CP_DATA_DIR, "uk");
    REQUIRE_MESSAGE(uk_pack, uk_pack.error);
    CHECK(uk_pack->roundabout.central_island_surface == "grass");
    CHECK(uk_pack->roundabout.splitter_surface == "paving");
    CHECK(uk_pack->roundabout.island_kerb_upstand == doctest::Approx(0.125));
    auto lt = parse_lane_types(kLaneTypes);
    REQUIRE(lt);
    auto bare = parse_region_pack(*lt, kRegion, road_types(R"({"lane":"driving","width_m":3.0})"));
    REQUIRE_MESSAGE(bare, bare.error);
    CHECK(bare->roundabout.central_island_surface == "grass");
    CHECK(bare->roundabout.splitter_surface == "paving");
    CHECK(bare->roundabout.island_kerb_upstand == 0);
    const std::string region = R"({"id":"t","driving_side":"right","speed_unit":"kph","roundabout":{"central_island_surface":"shrub","splitter_surface":"cobble","island_kerb_upstand_m":0.2}})";
    auto custom = parse_region_pack(*lt, region, road_types(R"({"lane":"driving","width_m":3.0})"));
    REQUIRE_MESSAGE(custom, custom.error);
    CHECK(custom->roundabout.central_island_surface == "shrub");
    CHECK(custom->roundabout.splitter_surface == "cobble");
    CHECK(custom->roundabout.island_kerb_upstand == doctest::Approx(0.2));
    const std::string bad = R"({"id":"t","driving_side":"right","speed_unit":"kph","roundabout":{"island_kerb_upstand_m":-1}})";
    CHECK_FALSE(parse_region_pack(*lt, bad, road_types(R"({"lane":"driving","width_m":3.0})")));
}

TEST_CASE("UK rural and urban main road types") {
    auto pack = load_region_pack_by_id(CP_DATA_DIR, "uk");
    REQUIRE_MESSAGE(pack, pack.error);
    const auto count = [](const std::vector<CrossSectionSlot>& v, const char* lane) {
        int n = 0;
        for (const auto& s : v) n += s.lane_type == lane ? 1 : 0;
        return n;
    };
    const auto width = [](const std::vector<CrossSectionSlot>& v, const char* lane) {
        double w = 0;
        for (const auto& s : v)
            if (s.lane_type == lane) w += s.width;
        return w;
    };

    // Dual carriageway and motorway: no kerb lane anywhere (kerbs on them would be a data bug).
    for (const char* id : {"a_road_dual", "motorway"}) {
        const RoadType* t = pack->road_type(id);
        REQUIRE(t);
        CHECK(count(t->carriageway, "kerb") == 0);
        CHECK(count(t->carriageway, "pavement") == 0);
    }

    // Kerbless rural types: no kerb or footway, a grass verge on the outside.
    for (const char* id : {"a_road_rural", "b_road_rural", "country_lane"}) {
        const RoadType* t = pack->road_type(id);
        REQUIRE_MESSAGE(t, id);
        CHECK(t->two_way);
        for (const auto* v : {&t->half, &t->one_way_variant}) {
            CHECK(count(*v, "kerb") == 0);
            CHECK(count(*v, "pavement") == 0);
            CHECK(count(*v, "verge") >= 1);
        }
        CHECK(!t->street_lights.lit);
        CHECK(t->crossfall.has_value());
        CHECK(t->verge_crossfall.has_value());
        CHECK(!t->roundabout_ring);
    }
    const RoadType* a = pack->road_type("a_road_rural");
    CHECK(width(a->half, "driving") == doctest::Approx(3.65));
    CHECK(width(a->half, "hard_strip") == doctest::Approx(1.0));
    CHECK(a->speed_limit == doctest::Approx(60 * 0.44704));
    CHECK(a->markings == "rural_single");
    const RoadType* b = pack->road_type("b_road_rural");
    CHECK(width(b->half, "driving") == doctest::Approx(3.2));
    CHECK(count(b->half, "hard_strip") == 0);
    const RoadType* lane = pack->road_type("country_lane");
    CHECK(2 * width(lane->half, "driving") == doctest::Approx(3.5));  // one surface, two half lanes
    CHECK(lane->markings.empty());
    CHECK(a->road_class > b->road_class);
    CHECK(b->road_class > lane->road_class);

    // Urban main street: kerbs, footways, parking bays.
    const RoadType* m = pack->road_type("main_street");
    REQUIRE(m);
    CHECK(count(m->half, "kerb") == 1);
    CHECK(count(m->half, "pavement") == 1);
    CHECK(count(m->half, "parking") == 1);
    CHECK(m->street_lights.lit);
    CHECK(m->markings == "urban_single");

    // Every non-empty marking set named by a road type is one of the sets in marking_rules.json.
    for (const auto& [id, t] : pack->road_types)
        if (!t.markings.empty())
            CHECK_MESSAGE((t.markings == "urban_single" || t.markings == "rural_single" || t.markings == "dual" ||
                           t.markings == "motorway"),
                          id);
}

TEST_CASE("structures block is optional, validated, and set in the UK pack") {
    auto lt = parse_lane_types(R"({"lane_types":[{"id":"driving","surface":"asphalt","connects_at_junctions":true,"users":["car"]}]})");
    REQUIRE_MESSAGE(lt, lt.error);
    const std::string rtj = road_types(R"({"lane":"driving","width_m":3.0})");
    const auto with = [&](const std::string& body) {
        return parse_region_pack(*lt, std::string(R"({"id":"t","driving_side":"left","speed_unit":"kph","structures":)") + body + "}", rtj);
    };
    auto plain = parse_region_pack(*lt, kRegion, rtj);
    REQUIRE_MESSAGE(plain, plain.error);
    CHECK(plain->structures.max_pier_spacing == doctest::Approx(30));
    CHECK(plain->structures.parapet_height == doctest::Approx(1.0));
    auto set = with(R"({"parapet_height_m":1.1,"max_pier_spacing_m":25,"wall_panel_m":3,"coping_thickness_m":0.2})");
    REQUIRE_MESSAGE(set, set.error);
    CHECK(set->structures.parapet_height == doctest::Approx(1.1));
    CHECK(set->structures.max_pier_spacing == doctest::Approx(25));
    CHECK(set->structures.wall_panel == doctest::Approx(3));
    CHECK(set->structures.coping_thickness == doctest::Approx(0.2));
    CHECK(set->structures.tunnel_rise_k == doctest::Approx(0.2));  // unset: default
    CHECK_FALSE(with(R"({"max_pier_spacing_m":1})"));
    CHECK_FALSE(with(R"({"parapet_height_m":0})"));
    CHECK_FALSE(with(R"({"wall_panel_m":"wide"})"));
    auto uk = load_region_pack_by_id(CP_DATA_DIR, "uk");
    REQUIRE_MESSAGE(uk, uk.error);
    CHECK(uk->structures.max_pier_spacing == doctest::Approx(30));
}
