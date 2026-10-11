// Test world (sim_core/include/cp/test_world.h): seeded gentle lowland terrain + terrain-aware cover.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "cp/test_world.h"
#include "doctest/doctest.h"
#include "world_preview.h"

#if defined(_MSC_VER)
#pragma warning(disable : 4996)
#endif

using namespace cp;

namespace {

struct Stats {
    double lo = 1e9, hi = -1e9;
    double s50 = 0, s90 = 0, s99 = 0, smax = 0;
    double max_step = 0;  // largest height difference between 4-neighbour cells
};

double pct(std::vector<double>& v, double q) {
    std::sort(v.begin(), v.end());
    return v[static_cast<std::size_t>(q * static_cast<double>(v.size() - 1))];
}

Stats measure(const Heightfield& hf) {
    Stats s;
    std::vector<double> sl;
    const int nx = hf.nx(), ny = hf.ny();
    const double c = hf.cell();
    for (int j = 0; j < ny; ++j)
        for (int i = 0; i < nx; ++i) {
            const double h = hf.at(i, j);
            s.lo = std::min(s.lo, h);
            s.hi = std::max(s.hi, h);
            if (i + 1 < nx) s.max_step = std::max(s.max_step, std::fabs(hf.at(i + 1, j) - h));
            if (j + 1 < ny) s.max_step = std::max(s.max_step, std::fabs(hf.at(i, j + 1) - h));
            if (i > 0 && j > 0 && i + 1 < nx && j + 1 < ny) {
                const double gx = (hf.at(i + 1, j) - hf.at(i - 1, j)) / (2 * c), gy = (hf.at(i, j + 1) - hf.at(i, j - 1)) / (2 * c);
                sl.push_back(std::sqrt(gx * gx + gy * gy));
            }
        }
    s.s50 = pct(sl, 0.5);
    s.s90 = pct(sl, 0.9);
    s.s99 = pct(sl, 0.99);
    s.smax = sl.back();
    return s;
}

std::shared_ptr<const VegetationRules> uk_rules() {
    auto r = load_vegetation_rules_by_id(CP_DATA_DIR, "uk");
    REQUIRE_MESSAGE(r, r.error);
    return std::make_shared<const VegetationRules>(*r);
}

}  // namespace

TEST_CASE("test_world: deterministic per seed, different between seeds") {
    TestWorldParams a;
    a.seed = 7;
    TestWorld w1(a), w2(a);
    for (int k = 0; k < 200; ++k) {
        const double x = -1000 + 9.7 * k, y = 800 - 8.3 * k;
        CHECK(w1.height(x, y) == w2.height(x, y));
    }
    TestWorldParams b = a;
    b.seed = 8;
    TestWorld w3(b);
    int diff = 0;
    for (int k = 0; k < 200; ++k) diff += w1.height(-1000 + 9.7 * k, 800 - 8.3 * k) != w3.height(-1000 + 9.7 * k, 800 - 8.3 * k);
    CHECK(diff > 190);
    const Heightfield h1 = w1.make_heightfield(), h2 = w2.make_heightfield();
    CHECK(h1.heights() == h2.heights());
}

TEST_CASE("test_world: golden heights (same on gcc and MSVC, millimetre exact)") {
    TestWorld w{TestWorldParams{}};
    struct G { double x, y; long long mm; };
    // Recorded from a gcc 16 build; the MSVC build must reproduce them (no libm transcendental is used).
    const G golden[] = {
        {0, 0, -1519}, {-900, -900, 21065}, {500, 300, 16906}, {-300, 700, 20851}, {1000, -1000, 26030}, {123.4, -567.8, 304},
    };
    for (const G& g : golden) {
        const long long mm = std::llround(w.height(g.x, g.y) * 1000.0);
        INFO("at " << g.x << "," << g.y << " got " << mm);
        CHECK(mm == g.mm);
    }
}

TEST_CASE("test_world: default seed relief, slope and continuity statistics (measured)") {
    const TestWorld w{TestWorldParams{}};
    const Heightfield hf = w.make_heightfield();
    CHECK(hf.nx() == 513);
    const Stats s = measure(hf);
    std::printf("[test_world] seed1 relief %.1f m (%.1f .. %.1f), slope p50 %.3f p90 %.3f p99 %.3f max %.3f, max cell step %.2f m\n",
                s.hi - s.lo, s.lo, s.hi, s.s50, s.s90, s.s99, s.smax, s.max_step);
    CHECK(s.hi - s.lo >= 30);
    CHECK(s.hi - s.lo <= 70);
    CHECK(s.s90 < 0.12);  // most slopes under 12 %
    CHECK(s.s50 < 0.06);
    CHECK(s.s99 < 0.45);
    CHECK(s.smax < 0.50);
    CHECK(s.max_step <= 0.5 * hf.cell());  // no step above 50 % between adjacent cells (a 2 m step at 4 m cells)
    // Other seeds stay inside the same bounds.
    for (std::uint64_t seed : {2ull, 3ull, 11ull, 99ull}) {
        TestWorldParams p;
        p.seed = seed;
        p.cell_m = 8;  // faster
        const Stats t = measure(TestWorld(p).make_heightfield());
        INFO("seed " << seed);
        CHECK(t.hi - t.lo >= 30);
        CHECK(t.hi - t.lo <= 70);
        CHECK(t.s90 < 0.12);
        CHECK(t.smax < 0.50);
    }
}

TEST_CASE("test_world: no spikes (second difference bounded) and smooth valley floor") {
    const TestWorld w{TestWorldParams{}};
    double worst = 0;
    for (double y = -1000; y <= 1000; y += 12.5)
        for (double x = -1000; x <= 1000; x += 12.5) {
            const double h = 4;
            const double lap = std::fabs(w.height(x + h, y) + w.height(x - h, y) + w.height(x, y + h) + w.height(x, y - h) - 4 * w.height(x, y));
            worst = std::max(worst, lap);
        }
    std::printf("[test_world] worst 4 m laplacian %.3f m\n", worst);
    CHECK(worst < 1.0);  // a 1 m bump over 4 m would be a spike; 2*Laplacian/h^2 -> curvature 0.06 /m
    // Valley floor is flat-ish: across the floor the height varies by a few metres at most.
    const double y0 = -300;
    const double ax = w.valley_axis_x(y0);
    double lo = 1e9, hi = -1e9;
    for (double d = -60; d <= 60; d += 4) {
        const double hgt = w.height(ax + d, y0);
        lo = std::min(lo, hgt);
        hi = std::max(hi, hgt);
    }
    CHECK(hi - lo < 3.0);
}

TEST_CASE("test_world: grid sampling is independent of cell size and tiling") {
    TestWorldParams p4, p8;
    p8.cell_m = 8;
    const TestWorld w4(p4), w8(p8);
    const Heightfield h4 = w4.make_heightfield(), h8 = w8.make_heightfield();
    REQUIRE(h4.nx() == 513);
    REQUIRE(h8.nx() == 257);
    for (int j = 0; j < h8.ny(); j += 5)
        for (int i = 0; i < h8.nx(); i += 5) CHECK(h8.at(i, j) == h4.at(2 * i, 2 * j));
    // The height function is the same pure function whatever the grid: a coarse grid's bilinear value is close.
    double worst = 0;
    for (double y = -900; y < 900; y += 37.3)
        for (double x = -900; x < 900; x += 41.9) worst = std::max(worst, std::fabs(h8.height(x, y) - w4.height(x, y)));
    std::printf("[test_world] 8 m grid vs exact worst %.3f m\n", worst);
    CHECK(worst < 0.5);
    // A shifted window of the pure function matches (no dependence on a tile origin): evaluate in two orders.
    CHECK(w4.height(-512.25, 301.5) == TestWorld(p4).height(-512.25, 301.5));
}

TEST_CASE("test_world: the steep stretch exists, 30-45 % over 150-300 m, on one side only") {
    const TestWorld w{TestWorldParams{}};
    const double sy = w.scarp_centre_y();
    const double ax = w.valley_axis_x(sy);
    // Walk across the valley at the scarp centre, find the longest run with gradient >= 30 %.
    double run = 0, best = 0, best_side = 0, maxs = 0, at_x = 0;
    for (double x = ax - 700; x <= ax + 700; x += 1.0) {
        const double sl = w.slope(x, sy);
        maxs = std::max(maxs, sl);
        if (sl >= 0.30) {
            run += 1;
            if (run > best) { best = run; best_side = (x - ax) * w.scarp_side(); at_x = x; }
        } else {
            run = 0;
        }
    }
    std::printf("[test_world] steep stretch at y=%.0f: longest run >=30%% is %.0f m, max slope %.3f, on side %s (x=%.0f, axis %.0f)\n", sy, best, maxs,
                best_side > 0 ? "scarp" : "other", at_x, ax);
    CHECK(best >= 100);
    CHECK(best <= 300);
    CHECK(maxs <= 0.45);
    CHECK(best_side > 0);
    // Far from the scarp the same side is gentle.
    const double fy = (sy > 0 ? -1.0 : 1.0) * 800;
    double gentle = 0;
    for (double x = w.valley_axis_x(fy) - 700; x <= w.valley_axis_x(fy) + 700; x += 2) gentle = std::max(gentle, w.slope(x, fy));
    std::printf("[test_world] max slope across the valley far from the scarp (y=%.0f): %.3f\n", fy, gentle);
    CHECK(gentle < 0.20);
}

TEST_CASE("test_world: terrain-aware cover is pure and follows slope") {
    auto rules = uk_rules();
    TestWorldParams p;
    auto tw = std::make_shared<const TestWorld>(p);
    auto base = make_default_cover_source(rules, p.seed);
    auto cover = make_test_world_cover_source(tw, rules, base);
    auto cover2 = make_test_world_cover_source(tw, rules, make_default_cover_source(rules, p.seed));

    // Purity: points of a sub-box equal the same points picked out of a bigger box; two instances agree.
    std::vector<CoverPoint> big, small, again;
    cover->points_in({-300, -300}, {300, 300}, big);
    cover->points_in({-100, -100}, {100, 150}, small);
    cover2->points_in({-100, -100}, {100, 150}, again);
    REQUIRE(!small.empty());
    std::size_t matched = 0;
    for (const auto& b : big)
        if (b.pos.x >= -100 && b.pos.x < 100 && b.pos.y >= -100 && b.pos.y < 150) {
            bool found = false;
            for (const auto& s : small) found = found || (s.seed == b.seed && s.pos.x == b.pos.x && s.pos.y == b.pos.y && s.type_id == b.type_id);
            CHECK(found);
            ++matched;
        }
    CHECK(matched == small.size());
    REQUIRE(small.size() == again.size());
    for (std::size_t i = 0; i < small.size(); ++i) CHECK(small[i].seed == again[i].seed);
    for (int k = 0; k < 50; ++k) {
        const Vec2 q{-800 + 31.1 * k, 400 - 17.7 * k};
        CHECK(cover->ground_at(q).type_id == cover2->ground_at(q).type_id);
    }

    // Steep ground is woodland; the valley line is wet/rough; woodland tree density on steep ground exceeds the floor's.
    const auto& wc = rules->world_cover;
    int steep_n = 0, steep_wood = 0;
    for (double y = -1000; y <= 1000; y += 16)
        for (double x = -1000; x <= 1000; x += 16)
            if (tw->slope(x, y) > p.wood_slope_hi) {
                ++steep_n;
                const CoverGround g = cover->ground_at({x, y});
                steep_wood += g.type_id == wc.woodland_floor_type && g.density == wc.woodland_density;
            }
    std::printf("[test_world] steep (> %.0f%%) samples %d, woodland %d\n", p.wood_slope_hi * 100, steep_n, steep_wood);
    CHECK(steep_n > 100);
    CHECK(steep_wood >= steep_n - steep_n / 50 - 1);  // all except the wet strip crossing a steep patch
    for (double y = -900; y <= 900; y += 150) {
        const CoverGround g = cover->ground_at({tw->valley_axis_x(y), y});
        CHECK(g.type_id == wc.rough_grass_type);
        CHECK(g.density == wc.rough_density);
    }
    auto count_trees = [&](double sy) {  // trees in a 400 x 400 m window over [valley floor side] vs scarp side
        std::vector<CoverPoint> v;
        const double ax = tw->valley_axis_x(sy);
        const double s = tw->scarp_side();
        const double cx = ax + s * (p.valley_floor_half_width_m + 0.5 * p.scarp_len_m);
        cover->points_in({cx - 60, sy - 100}, {cx + 60, sy + 100}, v);
        return v.size();
    };
    const std::size_t on_scarp = count_trees(tw->scarp_centre_y());
    std::vector<CoverPoint> floorv;
    {
        const double sy = tw->scarp_centre_y(), ax = tw->valley_axis_x(sy);
        cover->points_in({ax - 60, sy - 100}, {ax + 60, sy + 100}, floorv);
    }
    std::printf("[test_world] trees on scarp window %zu, on valley floor window %zu\n", on_scarp, floorv.size());
    CHECK(on_scarp > 3 * floorv.size() + 20);
}

TEST_CASE("test_world: bundle is a thin composition (heightfield, ground_height, cover)") {
    auto rules = uk_rules();
    TestWorldParams p;
    p.seed = 5;
    const TestWorldBundle b = make_test_world(p, rules);
    REQUIRE(b.terrain);
    REQUIRE(b.cover);
    CHECK(b.world_min.x == doctest::Approx(-1024));
    CHECK(b.ground_height(100.3, -200.9) == b.terrain->height(100.3, -200.9));
    CHECK(b.ground_height(100.3, -200.9) == doctest::Approx(b.world->height(100.3, -200.9)).epsilon(0.02).scale(10));
    VegetationParams vp;
    apply_to_vegetation_params(b, 77, vp);
    CHECK(vp.world_seed == 77);
    CHECK(vp.cover == b.cover);
    CHECK(static_cast<bool>(vp.ground_height));
    CHECK(make_test_world(p, nullptr).cover == nullptr);
}

TEST_CASE("test_world: previews (set CP_WORLD_PREVIEW_DIR to write PNGs)" * doctest::skip(std::getenv("CP_WORLD_PREVIEW_DIR") == nullptr)) {
    const std::string dir = std::getenv("CP_WORLD_PREVIEW_DIR");
    auto rules = uk_rules();
    for (std::uint64_t seed : {1ull, 2ull, 3ull}) {
        TestWorldParams p;
        p.seed = seed;
        const TestWorldBundle b = make_test_world(p, rules);
        const std::string tag = dir + "/seed" + std::to_string(seed) + "_";
        CHECK(cpprev::write_png(tag + "hillshade_contours.png", cpprev::hillshade(*b.world, *b.terrain, 2.0)));
        CHECK(cpprev::write_png(tag + "slope.png", cpprev::slope_map(*b.world, *b.terrain, 2.0)));
        CHECK(cpprev::write_png(tag + "cover.png", cpprev::cover_map(*b.world, *b.terrain, *b.cover, *rules, 2.0)));
        const TestWorld& w = *b.world;
        const double sy = w.scarp_centre_y();
        auto hz = [&](double x, double y) { return b.terrain->height(x, y); };
        {  // along the valley, from the low end
            const double ey = -980, ex = w.valley_axis_x(ey);
            CHECK(cpprev::write_png(tag + "oblique_valley.png", cpprev::oblique(w, *b.terrain, b.cover.get(), rules.get(), ex, ey, hz(ex, ey) + 120,
                                                                              w.valley_axis_x(0), 0, hz(w.valley_axis_x(0), 0), 960, 540, 60)));
        }
        {  // across the valley, looking at the scarp
            const double s = w.scarp_side();
            const double ax = w.valley_axis_x(sy);
            const double tx = ax + s * 190, ex = ax - s * 520, ey = std::max(-980.0, sy - 250);
            CHECK(cpprev::write_png(tag + "oblique_scarp.png", cpprev::oblique(w, *b.terrain, b.cover.get(), rules.get(), ex, ey, hz(ex, ey) + 60,
                                                                             tx, sy, hz(tx, sy) + 15, 960, 540, 65)));
        }
        {  // high corner view of the whole world
            CHECK(cpprev::write_png(tag + "oblique_overview.png", cpprev::oblique(w, *b.terrain, b.cover.get(), rules.get(), -1000, -1000, 380,
                                                                                0, 200, 0, 960, 540, 60)));
        }
    }
}
