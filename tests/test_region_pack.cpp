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
    CHECK(pack->road_types.size() == 5);

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
