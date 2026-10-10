#include <algorithm>
#include <cmath>
#include <functional>

#include "doctest/doctest.h"
#include "cp/earthworks.h"

using namespace cp;

namespace {

Edge make_edge(double length, double k = 0) {
    Edge e;
    Clothoid c;
    c.heading0 = 0;
    c.k0 = k;
    c.dk = 0;
    c.length = length;
    e.horizontal.spans.push_back(c);
    e.vertical = VerticalProfile::build({0, length}, {0, 0}, {});
    return e;
}

// Grid covering x in [-20, 220], y in [-80, 80] at 1 m, filled from f(x, y).
Heightfield make_terrain(const std::function<double(double, double)>& f) {
    Heightfield hf({-20, -80}, 1.0, 241, 161);
    for (int j = 0; j < 161; ++j)
        for (int i = 0; i < 241; ++i)
            hf.set(i, j, static_cast<float>(f(-20.0 + i, -80.0 + j)));
    return hf;
}

constexpr double kHW = 5.0;

bool segs_cross(Vec2 a, Vec2 b, Vec2 c, Vec2 d) {
    auto o = [](Vec2 p, Vec2 q, Vec2 r) { return (q - p).cross(r - p); };
    return o(a, b, c) * o(a, b, d) < 0 && o(c, d, a) * o(c, d, b) < 0;
}

}  // namespace

TEST_CASE("flat ground at formation level gives zero-height slopes") {
    EarthworkParams p;
    const auto hf = make_terrain([&](double, double) { return -p.formation_drop; });
    const auto cs = compute_cross_sections(make_edge(100), kHW, hf, p, 10);
    REQUIRE(cs.size() == 11);
    for (const auto& c : cs) {
        CHECK(c.structure == Structure::Ground);
        for (const SideSection* s : {&c.left, &c.right}) {
            CHECK(s->kind == SideSection::Slope);
            CHECK(s->d1 == doctest::Approx(s->edge));
            CHECK(s->z1 == doctest::Approx(s->z0));
        }
    }
}

TEST_CASE("constant side slope gives analytic daylight on cut and fill sides") {
    EarthworkParams p;
    const double m = 0.2;
    const auto hf = make_terrain([&](double, double y) { return m * y; });
    const auto cs = compute_cross_sections(make_edge(100), kHW, hf, p, 20);
    const double z0 = -p.formation_drop;
    const double dl = (5 * m + 0.15) / (0.5 - m);  // left: cut, ground rises with y
    const double dr = (5 * m - 0.15) / (0.5 - m);  // right: fill
    for (const auto& c : cs) {
        CHECK(c.left.cut);
        CHECK(c.left.kind == SideSection::Slope);
        CHECK(c.left.d1 - c.left.edge == doctest::Approx(dl).epsilon(0.01 / dl));
        CHECK(c.left.z1 == doctest::Approx(z0 + dl / 2).epsilon(0.01));
        CHECK_FALSE(c.right.cut);
        CHECK(c.right.d1 - c.right.edge == doctest::Approx(dr).epsilon(0.01 / dr));
        CHECK(c.right.z1 == doctest::Approx(z0 - dr / 2).epsilon(0.01));
    }
}

TEST_CASE("wall_start flips Slope to Wall") {
    EarthworkParams p;
    const auto hf = make_terrain([&](double, double y) { return 0.2 * y; });
    // Left slope height is about 1.9 m.
    p.wall_start = 2.5;
    CHECK(compute_cross_sections(make_edge(50), kHW, hf, p, 25)[0].left.kind == SideSection::Slope);
    p.wall_start = 1.5;
    const auto cs = compute_cross_sections(make_edge(50), kHW, hf, p, 25);
    CHECK(cs[0].left.kind == SideSection::Wall);
    CHECK(cs[0].left.d1 == doctest::Approx(kHW + p.wall_thickness));
    CHECK(cs[0].left.z1 == doctest::Approx(0.2 * (kHW + p.wall_thickness)).epsilon(1e-3));
}

TEST_CASE("deep valley gives Bridge stations") {
    EarthworkParams p;
    const auto hf = make_terrain([](double, double) { return -30.0; });
    const auto cs = compute_cross_sections(make_edge(100), kHW, hf, p, 10);
    for (const auto& c : cs) {
        CHECK(c.structure == Structure::Bridge);
        CHECK(c.left.kind == SideSection::None);
    }
    CHECK(corridor_footprint(cs).empty());
}

TEST_CASE("tall hill gives Tunnel stations with smoothing") {
    EarthworkParams p;
    // Hill x in [20,100) with a 10 m notch at x in [58,68); a lone 3 m bump near x=150.
    const auto hf = make_terrain([&](double x, double) {
        const bool hill = x >= 19.5 && x < 100.5 && !(x >= 57.5 && x < 68.5);
        const bool bump = x >= 149.5 && x < 152.5;
        return hill || bump ? 30.0 : -p.formation_drop;
    });
    const auto cs = compute_cross_sections(make_edge(200), kHW, hf, p, 5);
    REQUIRE(cs.size() == 41);
    for (const auto& c : cs) {
        const bool tunnel = c.s >= 20 && c.s <= 100;
        CHECK((c.structure == Structure::Tunnel) == tunnel);
        if (tunnel) CHECK(c.left.kind == SideSection::None);
    }
    // Notch stations (60, 65) were filled; the bump station (150) was dropped.
    CHECK(cs[12].structure == Structure::Tunnel);
    CHECK(cs[30].structure == Structure::Ground);
    // Two ground runs -> two footprint polygons.
    CHECK(corridor_footprint(cs).size() == 2);
}

TEST_CASE("slope mesh is valid and ends under natural ground") {
    EarthworkParams p;
    const double m = 0.2;
    const auto hf = make_terrain([&](double, double y) { return m * y; });
    const auto cs = compute_cross_sections(make_edge(100), kHW, hf, p, 10);
    const double overlap = 1.0;
    const MeshData mesh = build_slope_mesh(cs, overlap);
    REQUIRE(!mesh.indices.empty());
    CHECK(mesh.indices.size() % 3 == 0);
    CHECK(mesh.normals.size() == mesh.positions.size());
    for (auto i : mesh.indices) CHECK(i < mesh.positions.size());
    for (std::size_t t = 0; t < mesh.indices.size(); t += 3) {
        const Vec3 &a = mesh.positions[mesh.indices[t]], &b = mesh.positions[mesh.indices[t + 1]],
                   &c = mesh.positions[mesh.indices[t + 2]];
        const Vec3 u{b.x - a.x, b.y - a.y, b.z - a.z}, v{c.x - a.x, c.y - a.y, c.z - a.z};
        const Vec3 n{u.y * v.z - u.z * v.y, u.z * v.x - u.x * v.z, u.x * v.y - u.y * v.x};
        CHECK(std::sqrt(n.x * n.x + n.y * n.y + n.z * n.z) > 1e-6);
        CHECK(n.z > 0);  // CCW from above
    }
    for (const Vec3& n : mesh.normals) {
        CHECK(n.z > 0.3);
        CHECK(std::sqrt(n.x * n.x + n.y * n.y + n.z * n.z) == doctest::Approx(1.0));
    }
    // Last row of each side, station 0: below terrain.
    const std::size_t N = cs.size(), R = kEarthworkRows;
    const Vec3 lastL = mesh.positions[(0 * N + 0) * R + R - 1];
    const Vec3 lastR = mesh.positions[(1 * N + 0) * R + R - 1];
    CHECK(hf.height(lastL.x, lastL.y) - lastL.z == doctest::Approx(overlap * (0.5 + m)).epsilon(0.01));
    CHECK(hf.height(lastR.x, lastR.y) - lastR.z == doctest::Approx(overlap * (0.5 - m)).epsilon(0.01));
}

TEST_CASE("corridor footprint is closed-simple on a gentle curve") {
    EarthworkParams p;
    const auto hf = make_terrain([&](double, double y) { return 0.1 * y - p.formation_drop; });
    const auto cs = compute_cross_sections(make_edge(150, 1.0 / 400), kHW, hf, p, 10);
    const auto polys = corridor_footprint(cs, 1.0);
    REQUIRE(polys.size() == 1);
    const auto& poly = polys[0];
    CHECK(poly.size() == 2 * cs.size());
    const std::size_t n = poly.size();
    for (std::size_t i = 0; i < n; ++i)
        for (std::size_t j = i + 1; j < n; ++j) {
            if (j == i + 1 || (i == 0 && j == n - 1)) continue;
            CHECK_FALSE(segs_cross(poly[i], poly[(i + 1) % n], poly[j], poly[(j + 1) % n]));
        }
    // Left side is at d1 - inset, at least the road edge.
    CHECK((poly[0] - cs[0].centre.xy()).length() >= kHW - 1e-9);
}
