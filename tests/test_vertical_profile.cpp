#include "doctest/doctest.h"
#include "cp/vertical_profile.h"

#include <cmath>

using namespace cp;

TEST_CASE("constant grade has no curves") {
    auto vp = VerticalProfile::build({0, 100, 200}, {0, 5, 10}, {});
    CHECK(vp.breaks[1].length == 0);
    CHECK(vp.z(150) == doctest::Approx(7.5));
    CHECK(vp.grade(30) == doctest::Approx(0.05));
}

TEST_CASE("crest curve is continuous in height and grade") {
    // +4% then -4%: crest, dg = 8%, K = 10 -> L = 80 m.
    auto vp = VerticalProfile::build({0, 200, 400}, {0, 8, 0}, {10, 10, 0});
    CHECK(vp.breaks[1].length == doctest::Approx(80));
    const double eps = 1e-6;
    for (double s : {160.0, 240.0}) {  // curve ends
        CHECK(std::abs(vp.z(s - eps) - vp.z(s + eps)) < 1e-6);
        CHECK(vp.grade(s - eps) == doctest::Approx(vp.grade(s + eps)).epsilon(1e-6));
    }
    CHECK(vp.grade(200) == doctest::Approx(0).scale(1));
    CHECK(vp.z(200) < 8);  // curve passes below the tangent intersection on a crest
    CHECK(vp.z(200) == doctest::Approx(8 - 0.08 * 80 / 8));  // e = A L / 8
}

TEST_CASE("curves shrink to fit short spans") {
    auto vp = VerticalProfile::build({0, 20, 40}, {0, 2, 0}, {50, 50, 0});
    CHECK(vp.breaks[1].length == doctest::Approx(20));
}
