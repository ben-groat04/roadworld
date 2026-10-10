#include "doctest/doctest.h"
#include "cp/lanes.h"

using namespace cp;

namespace {

const RegionPack& uk() {
    static const RegionPack pack = [] {
        auto p = load_region_pack_by_id(CP_DATA_DIR, "uk");
        REQUIRE_MESSAGE(p, p.error);
        return *p;
    }();
    return pack;
}

std::vector<Lane> lanes(const char* type, bool one_way, DrivingSide side) {
    Edge e;
    e.one_way = one_way;
    e.road_type = type;
    return instantiate_lanes(e, *uk().road_type(type), side);
}

}  // namespace

TEST_CASE("residential two-way, left-hand driving: 8 mirrored slots") {
    const auto l = lanes("residential", false, DrivingSide::Left);
    REQUIRE(l.size() == 8);
    const char* types[] = {"pavement", "kerb", "parking", "driving", "driving", "parking", "kerb", "pavement"};
    const double offs[] = {6.15, 5.075, 4.0, 1.5, -1.5, -4.0, -5.075, -6.15};
    const double widths[] = {2.0, 0.15, 2.0, 3.0, 3.0, 2.0, 0.15, 2.0};
    for (std::size_t i = 0; i < 8; ++i) {
        CHECK(l[i].slot == static_cast<int>(i));
        CHECK(l[i].lane_type == types[i]);
        CHECK(l[i].offset == doctest::Approx(offs[i]));
        CHECK(l[i].width == doctest::Approx(widths[i]));
    }
    // UK: a -> b traffic keeps left, so the forward driving lane is on the + (left) side.
    CHECK(l[3].dir == TravelDir::Forward);
    CHECK(l[4].dir == TravelDir::Backward);
    CHECK(l[0].dir == TravelDir::None);
    CHECK(l[2].source_slot == 1);
    CHECK(cross_section_width(l) == doctest::Approx(2 * 7.15));
}

TEST_CASE("residential two-way, right-hand driving swaps the driving lanes") {
    const auto l = lanes("residential", false, DrivingSide::Right);
    REQUIRE(l.size() == 8);
    CHECK(l[3].offset == doctest::Approx(1.5));
    CHECK(l[3].dir == TravelDir::Backward);
    CHECK(l[4].offset == doctest::Approx(-1.5));
    CHECK(l[4].dir == TravelDir::Forward);
}

TEST_CASE("one-way edge uses the one_way_variant, centred") {
    const auto l = lanes("residential", true, DrivingSide::Left);
    REQUIRE(l.size() == 7);
    CHECK(cross_section_width(l) == doctest::Approx(11.55));
    // Symmetric variant: the single driving lane sits on the centreline, forward.
    CHECK(l[3].lane_type == "driving");
    CHECK(l[3].offset == doctest::Approx(0.0));
    CHECK(l[3].dir == TravelDir::Forward);
    CHECK(l[0].offset == doctest::Approx(4.775));
    for (std::size_t i = 0; i < l.size(); ++i)
        CHECK(l[i].offset == doctest::Approx(-l[l.size() - 1 - i].offset));
}

TEST_CASE("dual carriageway: UK vs right-hand") {
    const auto uk_l = lanes("a_road_dual", true, DrivingSide::Left);
    REQUIRE(uk_l.size() == 5);
    // Nearside is left for UK: verge on the left, offside hard strip (median side) on the right.
    const char* ltypes[] = {"verge", "hard_strip", "driving", "driving", "hard_strip"};
    const double loffs[] = {4.4, 2.9, 0.575, -3.075, -5.15};
    for (std::size_t i = 0; i < 5; ++i) {
        CHECK(uk_l[i].lane_type == ltypes[i]);
        CHECK(uk_l[i].offset == doctest::Approx(loffs[i]));
    }
    CHECK(uk_l[2].dir == TravelDir::Forward);
    CHECK(uk_l[3].dir == TravelDir::Forward);
    CHECK(uk_l[0].dir == TravelDir::None);
    CHECK(cross_section_width(uk_l) == doctest::Approx(10.8));

    const auto rh = lanes("a_road_dual", true, DrivingSide::Right);
    REQUIRE(rh.size() == 5);
    const char* rtypes[] = {"hard_strip", "driving", "driving", "hard_strip", "verge"};
    const double roffs[] = {5.15, 3.075, -0.575, -2.9, -4.4};
    for (std::size_t i = 0; i < 5; ++i) {
        CHECK(rh[i].lane_type == rtypes[i]);
        CHECK(rh[i].offset == doctest::Approx(roffs[i]));
    }
    // Dual types are carriageways whatever the edge's one_way flag says.
    CHECK(lanes("a_road_dual", false, DrivingSide::Left).size() == 5);
}

TEST_CASE("motorway has three driving lanes and a hard shoulder on the nearside") {
    const auto l = lanes("motorway", true, DrivingSide::Right);
    int driving = 0;
    for (const Lane& x : l) driving += x.lane_type == "driving" && x.dir == TravelDir::Forward;
    CHECK(driving == 3);
    CHECK(l[l.size() - 2].lane_type == "hard_shoulder");  // nearside = right = last slots
    CHECK(l.back().lane_type == "verge");
}
