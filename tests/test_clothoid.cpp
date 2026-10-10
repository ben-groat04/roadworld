#include "doctest/doctest.h"
#include "cp/clothoid.h"

#include <cmath>
#include <random>

using namespace cp;

namespace {
void check_hits(const Clothoid& c, Vec2 p1, double theta1, double tol = 1e-8) {
    const Pose e = c.end_pose();
    CHECK((e.pos - p1).length() < tol);
    CHECK(std::abs(wrap_angle(e.heading - theta1)) < 1e-9);
}
}  // namespace

TEST_CASE("fresnel at zero phase is (1, 0)") {
    double X, Y;
    fresnel_xy(0, 0, 0, X, Y);
    CHECK(X == doctest::Approx(1.0).epsilon(1e-14));
    CHECK(Y == doctest::Approx(0.0));
}

TEST_CASE("straight line fits as zero curvature") {
    auto c = fit_clothoid_g1({0, 0}, 0.3, {std::cos(0.3) * 100, std::sin(0.3) * 100}, 0.3);
    REQUIRE(c);
    CHECK(c->length == doctest::Approx(100));
    CHECK(std::abs(c->k0) < 1e-12);
    CHECK(std::abs(c->dk) < 1e-14);
}

TEST_CASE("symmetric tangents give a circular arc") {
    // Quarter circle radius 50 from (50,0) heading north to (0,50) heading west.
    auto c = fit_clothoid_g1({50, 0}, kPi / 2, {0, 50}, kPi);
    REQUIRE(c);
    CHECK(c->k0 == doctest::Approx(1.0 / 50));
    CHECK(std::abs(c->dk) < 1e-12);
    CHECK(c->length == doctest::Approx(50 * kPi / 2));
    check_hits(*c, {0, 50}, kPi);
}

TEST_CASE("arc evaluation matches circle geometry") {
    Clothoid c{{0, 0}, 0, 0.01, 0, 100};
    const Vec2 p = c.point(100);
    CHECK(p.x == doctest::Approx(100 * std::sin(1.0)));
    CHECK(p.y == doctest::Approx(100 * (1 - std::cos(1.0))));
}

TEST_CASE("fuzz: G1 fit hits endpoint and heading") {
    std::mt19937_64 rng(12345);
    std::uniform_real_distribution<double> pos(-500, 500), ang(-kPi * 0.95, kPi * 0.95);
    int fitted = 0;
    for (int i = 0; i < 2000; ++i) {
        const Vec2 p0{pos(rng), pos(rng)}, p1{pos(rng), pos(rng)};
        const double phi = std::atan2(p1.y - p0.y, p1.x - p0.x);
        const double t0 = phi + ang(rng), t1 = phi + ang(rng);
        auto c = fit_clothoid_g1(p0, t0, p1, t1);
        REQUIRE(c);
        CHECK(c->length > 0);
        check_hits(*c, p1, t1, 1e-7 * (1 + (p1 - p0).length()));
        ++fitted;
    }
    CHECK(fitted == 2000);
}

TEST_CASE("coincident points are rejected") {
    CHECK_FALSE(fit_clothoid_g1({1, 1}, 0, {1, 1}, 1));
}
