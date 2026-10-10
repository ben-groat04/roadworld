#include "doctest/doctest.h"
#include "cp/clothoid_chain.h"

#include <cmath>
#include "portable_rng.h"

using namespace cp;

namespace {
void check_passes_through(const ClothoidChain& c, const std::vector<Vec2>& pts) {
    REQUIRE(c.spans.size() == pts.size() - 1);
    for (std::size_t i = 0; i < c.spans.size(); ++i) {
        CHECK((c.spans[i].start - pts[i]).length() < 1e-9);
        CHECK((c.spans[i].end_pose().pos - pts[i + 1]).length() < 1e-6);
        if (i > 0) CHECK(std::abs(wrap_angle(c.spans[i - 1].end_pose().heading - c.spans[i].heading0)) < 1e-9);
    }
}

}  // namespace

TEST_CASE("two points with natural ends is a straight line") {
    auto c = fit_clothoid_chain({{0, 0}, {100, 50}});
    REQUIRE(c);
    CHECK(c->g2);
    CHECK(std::abs(c->spans[0].k0) < 1e-9);
    CHECK(c->length() == doctest::Approx(std::hypot(100, 50)));
}

TEST_CASE("collinear points stay straight") {
    auto c = fit_clothoid_chain({{0, 0}, {30, 0}, {80, 0}, {200, 0}});
    REQUIRE(c);
    CHECK(c->g2);
    for (auto& s : c->spans) CHECK(std::abs(s.k0) < 1e-9);
}

TEST_CASE("bend is G2 with natural ends") {
    std::vector<Vec2> pts{{0, 0}, {100, 0}, {170, 60}, {200, 160}};
    auto c = fit_clothoid_chain(pts);
    REQUIRE(c);
    CHECK(c->g2);
    CHECK(c->max_kink < 1e-8);
    check_passes_through(*c, pts);
    CHECK(std::abs(c->spans.front().k0) < 1e-8);
    const auto& last = c->spans.back();
    CHECK(std::abs(last.curvature(last.length)) < 1e-8);
}

TEST_CASE("pinned start heading is honoured") {
    std::vector<Vec2> pts{{0, 0}, {100, 20}, {200, 0}};
    auto c = fit_clothoid_chain(pts, ChainEnd{0.0}, {});
    REQUIRE(c);
    CHECK(c->spans.front().heading0 == doctest::Approx(0.0));
    check_passes_through(*c, pts);
    CHECK(c->g2);
}

TEST_CASE("fuzz: plausible clicked roads fit G2 through every point") {
    PortableRng rng{7};
    int g2 = 0;
    const int trials = 300;
    for (int t = 0; t < trials; ++t) {
        std::vector<Vec2> pts{{0, 0}};
        double heading = 0;
        const int n = 2 + static_cast<int>(rng.next() % 8);
        for (int i = 1; i < n; ++i) {
            heading += rng.uniform(-1.2, 1.2);
            const double d = rng.uniform(20, 200);
            pts.push_back(pts.back() + Vec2{std::cos(heading), std::sin(heading)} * d);
        }
        auto c = fit_clothoid_chain(pts);
        REQUIRE(c);
        check_passes_through(*c, pts);
        if (c->g2) ++g2;
    }
    MESSAGE("G2 converged on " << g2 << " / " << trials);
    CHECK(g2 >= trials * 95 / 100);
}
