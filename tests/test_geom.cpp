#include <cstdint>
#include <vector>

#include "cp/geom.h"
#include "doctest/doctest.h"

using namespace cp;

TEST_CASE("geom: signed_area sign follows winding") {
    const std::vector<Vec2> ccw{{0, 0}, {4, 0}, {4, 3}, {0, 3}};
    CHECK(signed_area(ccw) == doctest::Approx(12.0));
    const std::vector<Vec2> cw(ccw.rbegin(), ccw.rend());
    CHECK(signed_area(cw) == doctest::Approx(-12.0));
    CHECK(signed_area({}) == 0.0);
    CHECK(signed_area({{1, 1}, {2, 2}}) == 0.0);
}

TEST_CASE("geom: dist_point_segment") {
    CHECK(dist_point_segment({1, 1}, {0, 0}, {2, 0}) == doctest::Approx(1.0));   // perpendicular foot
    CHECK(dist_point_segment({-3, 4}, {0, 0}, {2, 0}) == doctest::Approx(5.0));  // clamps to a
    CHECK(dist_point_segment({5, 4}, {0, 0}, {2, 0}) == doctest::Approx(5.0));   // clamps to b
    CHECK(dist_point_segment({3, 4}, {1, 1}, {1, 1}) == doctest::Approx(std::hypot(2.0, 3.0)));  // degenerate segment
}

TEST_CASE("geom: Vec3 cross and dot") {
    const Vec3 x{1, 0, 0}, y{0, 1, 0};
    const Vec3 z = cross(x, y);
    CHECK(z.x == 0.0);
    CHECK(z.y == 0.0);
    CHECK(z.z == 1.0);  // right-handed
    CHECK(dot(x, y) == 0.0);
    CHECK(length(Vec3{2, 3, 6}) == doctest::Approx(7.0));
    const Vec3 s = Vec3{1, 2, 3} + Vec3{1, 1, 1} * 2.0 - Vec3{0, 0, 1};
    CHECK(s.x == 3.0);
    CHECK(s.y == 4.0);
    CHECK(s.z == 4.0);
}

TEST_CASE("geom: hash01 is deterministic, in [0,1), and the two-key form matches the mixed one-key form") {
    CHECK(hash01(12345) == hash01(12345));
    CHECK(hash01(1) != hash01(2));
    CHECK(mix64(0) == 0xE220A8397B1DCDAFull);  // splitmix64 reference value for seed 0 (first output)
    for (std::uint64_t i = 0; i < 1000; ++i) {
        const double h = hash01(i);
        CHECK(h >= 0.0);
        CHECK(h < 1.0);
    }
    CHECK(hash01(7, -3) == hash01(mix64(7) ^ static_cast<std::uint64_t>(std::int64_t{-3})));
    CHECK(hash01(7, 1) != hash01(7, 2));
}
