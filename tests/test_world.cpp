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
    // Recorded from a gcc 16 build (round 4 terrain: scarp rise 42 m over 160 m); the MSVC build must reproduce them (no libm transcendental is used).
    const G golden[] = {
        {0, 0, -1480}, {-900, -900, 30816}, {500, 300, 21094}, {-300, 700, 596}, {1000, -1000, 3199}, {123.4, -567.8, 41553},
    };
    for (const G& g : golden) {
        const long long mm = std::llround(w.height(g.x, g.y) * 1000.0);
        INFO("at " << g.x << "," << g.y << " got " << mm);
        CHECK(mm == g.mm);
    }
}

TEST_CASE("test_world: default seed relief, slope and continuity statistics (measured, rolling detail and tilt on)") {
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
    CHECK(s.smax < 0.50);  // peak spots on the steep side (round 4: measured 0.42 at seed 1; the earlier 0.57 relaxation is undone)
    CHECK(s.max_step <= 0.55 * hf.cell());  // no step above 55 % between adjacent cells (2.2 m at 4 m cells)
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
    const Vec2 ax = w.valley_axis_point(-300), cr = w.cross_dir();
    double lo = 1e9, hi = -1e9;
    for (double d = -60; d <= 60; d += 4) {
        const double hgt = w.height(ax.x + cr.x * d, ax.y + cr.y * d);
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

TEST_CASE("test_world: the steep stretch exists, 30-45 % over 100-300 m, on one side only") {
    for (std::uint64_t seed : {1ull, 2ull, 3ull, 7ull}) {
        TestWorldParams p;
        p.seed = seed;
        const TestWorld w{p};
        const Vec2 sc = w.scarp_centre();
        const Vec2 cd = w.cross_dir();  // from the axis towards the scarp side
        // Walk across the valley at the scarp centre, find the longest run with gradient >= 30 %.
        double run = 0, best = 0, best_side = 0, maxs = 0, at = 0;
        for (double s = -700; s <= 700; s += 1.0) {
            const double sl = w.slope(sc.x + cd.x * s, sc.y + cd.y * s);
            maxs = std::max(maxs, sl);
            if (sl >= 0.30) {
                run += 1;
                if (run > best) { best = run; best_side = s; at = s; }
            } else {
                run = 0;
            }
        }
        std::printf("[test_world] seed %llu steep stretch (t=%.0f): longest run >=30%% is %.0f m, max slope %.3f, on side %s (offset %.0f m)\n",
                    static_cast<unsigned long long>(seed), w.scarp_centre_t(), best, maxs, best_side > 0 ? "scarp" : "other", at);
        CHECK(best >= 60);
        CHECK(best <= 300);
        CHECK(maxs <= 0.47);
        CHECK(best_side > 0);
        // Far from the scarp the same side is gentle.
        const double ft = w.scarp_centre_t() + (w.scarp_centre_t() > 0 ? -1.0 : 1.0) * 800;
        const Vec2 fa = w.valley_axis_point(ft);
        double gentle = 0;
        for (double s = -700; s <= 700; s += 2) gentle = std::max(gentle, w.slope(fa.x + cd.x * s, fa.y + cd.y * s));
        std::printf("[test_world]   max slope across the valley far from the scarp: %.3f\n", gentle);
        CHECK(gentle < 0.22);
    }
}

TEST_CASE("test_world: valley direction varies per seed and the scarp has spurs") {
    std::vector<double> angles;
    for (std::uint64_t seed : {1ull, 2ull, 3ull, 4ull, 5ull, 6ull}) {
        TestWorldParams p;
        p.seed = seed;
        angles.push_back(TestWorld(p).valley_dir().x);  // sin of the angle from +y
    }
    const double lo = *std::min_element(angles.begin(), angles.end()), hi = *std::max_element(angles.begin(), angles.end());
    std::printf("[test_world] valley direction sin(angle) over 6 seeds: %.2f .. %.2f\n", lo, hi);
    CHECK(hi - lo > 0.4);
    // Along the valley, the position of the foot of the steep side wanders by tens of metres (spurs).
    TestWorldParams p;
    const TestWorld w{p};
    const Vec2 cd = w.cross_dir();
    double mn = 1e9, mx = -1e9;
    for (double t = w.scarp_centre_t() - 250; t <= w.scarp_centre_t() + 250; t += 10) {
        const Vec2 a = w.valley_axis_point(t);
        double s = 100;  // the foot of the steep side: first point beyond the floor where the slope exceeds 20 %
        while (s < 500 && w.slope(a.x + cd.x * s, a.y + cd.y * s) < 0.20) s += 1;
        mn = std::min(mn, s);
        mx = std::max(mx, s);
    }
    std::printf("[test_world] foot of the steep side along the scarp: %.0f .. %.0f m from the axis\n", mn, mx);
    CHECK(mx - mn > 12);
}

TEST_CASE("test_world: terrain-aware cover is pure and follows slope") {
    auto rules = uk_rules();
    TestWorldParams p;
    auto tw = std::make_shared<const TestWorld>(p);
    const TestWorldBundle bundle = make_test_world(p, rules), bundle2 = make_test_world(p, rules);  // with the terrain hints
    auto cover = bundle.cover;
    auto cover2 = bundle2.cover;

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
        CHECK(cover->ground_at(q).land == cover2->ground_at(q).land);
    }

    // Steep ground is woodland; the valley line is wet/rough; woodland tree density on steep ground exceeds the floor's.
    int steep_n = 0, steep_wood = 0;
    for (double y = -1000; y <= 1000; y += 16)
        for (double x = -1000; x <= 1000; x += 16)
            if (tw->slope(x, y) > p.wood_slope_hi) {
                ++steep_n;
                steep_wood += cover->ground_at({x, y}).land == LandClass::Wood;
            }
    std::printf("[test_world] steep (> %.0f%%) samples %d, woodland %d\n", p.wood_slope_hi * 100, steep_n, steep_wood);
    CHECK(steep_n > 100);
    // Wood follows whole fields: part of the steep ground (the rest is open scarp grass), far more than the flat ground has.
    int flat_n = 0, flat_wood = 0;
    for (double y = -1000; y <= 1000; y += 16)
        for (double x = -1000; x <= 1000; x += 16)
            if (tw->slope(x, y) < 0.04 && std::fabs(tw->valley_distance(x, y)) > 150) {
                ++flat_n;
                flat_wood += cover->ground_at({x, y}).land == LandClass::Wood;
            }
    std::printf("[test_world] wood share on steep ground %.2f, on flat ground %.2f\n", static_cast<double>(steep_wood) / steep_n, static_cast<double>(flat_wood) / flat_n);
    CHECK(static_cast<double>(steep_wood) / steep_n > 0.3);
    CHECK(static_cast<double>(steep_wood) / steep_n < 0.95);  // broken up: not one wide band
    CHECK(static_cast<double>(steep_wood) / steep_n > 2.0 * static_cast<double>(flat_wood) / flat_n);
    for (double t = -900; t <= 900; t += 150) {
        const Vec2 a = tw->valley_axis_point(t);
        const CoverGround g = cover->ground_at(a);
        CHECK(g.land == LandClass::Rough);
        CHECK(g.type_id == rules->world_cover.rough_grass_type);
        CHECK(g.density == rules->world_cover.rough_density);
    }
    const Vec2 sc = tw->scarp_centre(), cd = tw->cross_dir();
    const Vec2 on = sc + cd * (p.valley_floor_half_width_m + 0.5 * p.scarp_len_m);
    (void)on;
    std::vector<CoverPoint> allv;
    cover->points_in({-1000, -1000}, {1000, 1000}, allv);
    std::size_t steep_t = 0, floor_t = 0;
    for (const auto& c : allv) {
        if (tw->slope(c.pos.x, c.pos.y) > p.wood_slope_lo) ++steep_t;
        else if (std::fabs(tw->valley_distance(c.pos.x, c.pos.y)) < tw->valley_floor_half_width(tw->valley_t(c.pos.x, c.pos.y))) ++floor_t;
    }
    int steep_area = 0, floor_area = 0;
    for (double y = -1000; y <= 1000; y += 16)
        for (double x = -1000; x <= 1000; x += 16) {
            if (tw->slope(x, y) > p.wood_slope_lo) ++steep_area;
            else if (std::fabs(tw->valley_distance(x, y)) < tw->valley_floor_half_width(tw->valley_t(x, y))) ++floor_area;
        }
    const double dens_steep = static_cast<double>(steep_t) / steep_area, dens_floor = static_cast<double>(floor_t) / floor_area;
    std::printf("[test_world] trees per sample cell: steep %.2f, valley floor %.2f\n", dens_steep, dens_floor);
    CHECK(dens_steep > 1.5 * dens_floor);
    // The stream tree line: woodland wherever the ribbon is (it exists in stretches), and fields never straddle the stream.
    int ribbon_wood = 0, ribbon_n = 0, stretch = 0;
    for (double t = -900; t <= 900; t += 6) {
        const Vec2 a = tw->valley_axis_point(t);
        for (double d = -70; d <= 70; d += 2) {
            const Vec2 q = a + cd * d;
            if (!tw->valley_ribbon(q.x, q.y)) continue;
            ++stretch;
            if (std::fabs(tw->valley_distance(q.x, q.y)) < p.wet_half_width_m) continue;  // the wet strip stays rough
            ++ribbon_n;
            ribbon_wood += cover->ground_at(q).land == LandClass::Wood;
        }
    }
    std::printf("[test_world] stream tree line samples %d, wood %d\n", ribbon_n, ribbon_wood);
    CHECK(stretch > 100);
    CHECK(ribbon_wood == ribbon_n);
    int straddle = 0, pairs = 0;
    auto hinted = [&] {
        CoverHints h;
        h.stream_offset = [tw](Vec2 q) { return tw->stream_offset(q.x, q.y); };
        return make_default_cover_source(rules, p.seed, h);
    }();
    for (double t = -900; t <= 900; t += 25) {
        const Vec2 a = tw->valley_axis_point(t);
        for (double d = -60; d <= 60; d += 3) {
            const Vec2 q = a + cd * d;
            if (std::fabs(tw->stream_offset(q.x, q.y)) > 1.5) continue;
            ++pairs;
            straddle += hinted->ground_at(q - cd * 12).field_id == hinted->ground_at(q + cd * 12).field_id;
        }
    }
    std::printf("[test_world] stream crossings checked %d, fields straddling it %d\n", pairs, straddle);
    CHECK(pairs > 20);
    CHECK(straddle == 0);
}

TEST_CASE("test_world: default cover is irregular: field sizes, aspects, orientations, woods, hedges (measured)") {
    auto rules = uk_rules();
    for (std::uint64_t seed : {1ull, 2ull, 3ull}) {
        auto src = make_default_cover_source(rules, seed);
        const Vec2 lo{-1024, -1024}, hi{1024, 1024};
        std::vector<double> area, asp;
        default_cover_field_stats(*src, {-4096, -4096}, {4096, 4096}, area, asp);  // ~100 blocks: one world holds only 6-9
        REQUIRE(area.size() > 30);
        std::sort(area.begin(), area.end());
        std::sort(asp.begin(), asp.end());
        double mean = 0;
        for (double a : area) mean += a / static_cast<double>(area.size());
        const double a10 = area[area.size() / 10], a90 = area[area.size() * 9 / 10], amax = area.back();
        const double asp50 = asp[asp.size() / 2];
        // Land classes and wood fraction on a 32 m grid.
        int n = 0, wood = 0, arable = 0, rough = 0;
        for (double y = lo.y; y < hi.y; y += 32)
            for (double x = lo.x; x < hi.x; x += 32) {
                const CoverGround g = src->ground_at({x, y});
                ++n;
                wood += g.land == LandClass::Wood;
                arable += g.land == LandClass::Arable;
                rough += g.land == LandClass::Rough;
                CHECK((g.land == LandClass::Arable) == g.type_id.empty());
            }
        // Hedges: orientation (fraction of length within 8 degrees of an axis), widths, total length.
        std::vector<CoverLine> lines;
        src->lines_touching(lo, hi, lines);
        double len = 0, axis_len = 0, wmin = 1e9, wmax = 0;
        for (const auto& l : lines) {
            wmin = std::min<double>(wmin, l.width);
            wmax = std::max<double>(wmax, l.width);
            for (std::size_t k = 0; k + 1 < l.points.size(); ++k) {
                const Vec2 d = l.points[k + 1] - l.points[k];
                const double L = std::sqrt(d.x * d.x + d.y * d.y);
                if (L < 1e-9) continue;
                const double ang = std::fabs(std::atan2(d.y, d.x)) * 180 / 3.14159265358979;  // 0..180
                const double m = std::fmod(ang, 90.0);
                len += L;
                if (m < 8 || m > 82) axis_len += L;
            }
        }
        std::vector<CoverPoint> pts;
        src->points_in(lo, hi, pts);
        std::printf("[test_world] default cover seed %llu: %zu fields, mean %.1f ha (p10 %.1f, p90 %.1f, max %.1f), median aspect %.2f; "
                    "wood %.1f%% arable %.1f%% rough %.1f%%; hedge %.1f km (%.0f m/ha), widths %.1f..%.1f m, %.0f%% within 8 deg of an axis; %zu trees\n",
                    static_cast<unsigned long long>(seed), area.size(), mean, a10, a90, amax, asp50, 100.0 * wood / n, 100.0 * arable / n,
                    100.0 * rough / n, len / 1000, len / 419.0, wmin, wmax, 100.0 * axis_len / std::max(len, 1.0), pts.size());
        CHECK(mean > 1.5);
        CHECK(mean < 8.0);
        CHECK(a10 < 1.5);   // districts of small fields exist
        CHECK(a90 > 3.5);   // and districts of large ones
        CHECK(amax < 45);
        CHECK(asp50 > 1.25);
        CHECK(static_cast<double>(wood) / n > 0.04);
        CHECK(static_cast<double>(wood) / n < 0.20);
        CHECK(static_cast<double>(arable) / n > 0.15);
        CHECK(wmin >= 5.5);   // hedges 6-10 m, tree lines 12-20 m (fences emit nothing)
        CHECK(wmax > 12.0);
        CHECK(wmax <= 20.5);
        CHECK(axis_len / len < 0.9);  // rows follow the terrain frame, so a share lies near an axis; not all of it
    }
    // Different seeds give different layouts.
    auto a = make_default_cover_source(rules, 1), b = make_default_cover_source(rules, 2);
    int diff = 0;
    for (int k = 0; k < 100; ++k) diff += a->ground_at({-900 + 17.3 * k, -700 + 13.1 * k}).land != b->ground_at({-900 + 17.3 * k, -700 + 13.1 * k}).land;
    CHECK(diff > 20);
}

TEST_CASE("test_world: default cover structure: T junctions only, corner and aspect gates, land-use shares, boundary weights (measured)") {
    auto rules = uk_rules();
    for (std::uint64_t seed : {1ull, 2ull, 3ull}) {
        auto src = make_default_cover_source(rules, seed);
        CoverStructureStats S;
        default_cover_structure_stats(*src, {-3000, -3000}, {3000, 3000}, S);
        REQUIRE(S.area_ha.size() > 300);
        double minc = 180, maxa = 0;
        int under4 = 0;
        for (std::size_t i = 0; i < S.area_ha.size(); ++i) {
            minc = std::min(minc, S.min_corner_deg[i]);
            maxa = std::max(maxa, S.aspect[i]);
            under4 += S.corners[i] < 4;
        }
        double tmin = 180;
        for (double a : S.t_angle_deg) tmin = std::min(tmin, a);
        const double bl = S.len_hedge_m + S.len_fence_m + S.len_treeline_m, bl2 = S.len_hedge_m + S.len_treeline_m;
        std::printf("[test_world] structure seed %llu: %zu fields, min corner %.1f deg, max aspect %.2f, T %d Y %d X %d (min T angle %.1f), hedge %.0f%% fence %.0f%% treeline %.0f%%, gateway gaps %.1f%%\n",
                    static_cast<unsigned long long>(seed), S.area_ha.size(), minc, maxa, S.t_junctions, S.y_junctions, S.x_junctions, tmin,
                    100 * S.len_hedge_m / bl, 100 * S.len_fence_m / bl, 100 * S.len_treeline_m / bl, 100 * S.len_gap_m / bl2);
        CHECK(minc >= 55.0 - 1e-6);       // no field corner under 55 degrees
        CHECK(maxa <= 4.0 + 1e-6);        // aspect no worse than 4:1
        CHECK(under4 == 0);               // no triangles
        CHECK(S.t_junctions > 1000);
        CHECK(S.y_junctions + S.x_junctions < S.t_junctions / 100);  // T junctions only (a handful of coincidences at most)
        CHECK(tmin >= 55.0);              // cuts meet at 55-90 on the small side
        CHECK(S.len_hedge_m / bl > 0.6);
        CHECK(S.len_hedge_m / bl < 0.8);
        CHECK(S.len_fence_m / bl > 0.1);
        CHECK(S.len_treeline_m / bl > 0.05);
        CHECK(S.len_treeline_m / bl < 0.16);
        CHECK(S.len_gap_m / bl2 > 0.02);
        CHECK(S.len_gap_m / bl2 < 0.08);  // gateways: a few percent
        int seen[8] = {0};
        for (std::uint8_t u : S.use) ++seen[u];
        for (int u = 0; u < 8; ++u) CHECK(seen[u] > 0);  // every land use occurs
    }
}

TEST_CASE("test_world: golden default cover (field ids, uses, tints, row directions, hedge length: same on gcc and MSVC)") {
    auto src = make_default_cover_source(uk_rules(), 1);
    struct G { double x, y; std::uint64_t id; int use; long long tint, rx, ry; };
    // Recorded from a gcc build (round 4); the MSVC build must reproduce them: the generator uses only + - * / sqrt floor and integer hashing.
    const G golden[] = {
        {0.00, 0.00, 0x4125a9a280cf3165ull, 6, 286479, -52081, 998643},
        {-731.50, 412.25, 0x9f6889136a5287e2ull, 5, 689722, 978353, -206944},
        {905.10, -233.70, 0xd1a4a6aa5593aba8ull, 5, 938773, 83471, 996510},
        {-120.30, -880.90, 0x562bcce55a6cdfacull, 4, 469915, -999985, -5475},
        {333.30, 777.70, 0x159b1aa659ec73f5ull, 4, 77668, -997837, -65739},
        {1500.50, 1500.50, 0xc2ee856503b81d5full, 4, 979286, -25065, 999686},
    };
    for (const G& g : golden) {
        const CoverGround c = src->ground_at({g.x, g.y});
        INFO("at " << g.x << "," << g.y);
        CHECK(c.field_id == g.id);
        CHECK(static_cast<int>(c.use) == g.use);
        CHECK(std::llround(c.tint * 1e6) == g.tint);
        CHECK(std::llround(c.row_dir.x * 1e6) == g.rx);
        CHECK(std::llround(c.row_dir.y * 1e6) == g.ry);
    }
    std::vector<CoverLine> ls;
    src->lines_touching({-500, -500}, {500, 500}, ls);
    REQUIRE(!ls.empty());
    double len = 0;
    for (const auto& l : ls)
        for (std::size_t k = 0; k + 1 < l.points.size(); ++k) len += (l.points[k + 1] - l.points[k]).length();
    CHECK(ls.size() == 22);
    CHECK(std::llround(len * 1000) == 5616482);
    CHECK(ls.front().seed == 0x36b2a5a90c046f81ull);
}

TEST_CASE("test_world: per-field attributes are stable and constant over a field") {
    auto rules = uk_rules();
    auto src = make_default_cover_source(rules, 2);
    std::map<std::uint64_t, CoverGround> first;
    std::size_t fields = 0;
    for (double y = -500; y < 500; y += 7.3)
        for (double x = -500; x < 500; x += 7.3) {
            const CoverGround g = src->ground_at({x, y});
            CHECK(g.field_id != 0);
            CHECK(std::fabs(g.row_dir.x * g.row_dir.x + g.row_dir.y * g.row_dir.y - 1.0) < 1e-9);
            CHECK(g.tint >= 0.0f);
            CHECK(g.tint <= 1.0f);
            CHECK((g.land == LandClass::Wood) == (g.use == LandUse::Woodland));
            CHECK((g.land == LandClass::Arable) == (g.use == LandUse::Ploughed || g.use == LandUse::Cereal || g.use == LandUse::Stubble || g.use == LandUse::OtherCrop));
            auto it = first.find(g.field_id);
            if (it == first.end()) {
                first[g.field_id] = g;
                ++fields;
                continue;
            }
            CHECK(it->second.tint == g.tint);
            CHECK(it->second.row_dir.x == g.row_dir.x);
            CHECK(it->second.row_dir.y == g.row_dir.y);
            if (g.use != LandUse::Woodland && it->second.use != LandUse::Woodland) CHECK(it->second.use == g.use);
        }
    CHECK(fields > 20);
    // Same seed, same ids; another source instance reproduces them.
    auto again = make_default_cover_source(rules, 2);
    CHECK(again->ground_at({123.4, -77.7}).field_id == src->ground_at({123.4, -77.7}).field_id);
    CHECK(make_default_cover_source(rules, 3)->ground_at({123.4, -77.7}).field_id != src->ground_at({123.4, -77.7}).field_id);
}

TEST_CASE("test_world: default cover hedges are tile independent and woods are not the field cells") {
    auto rules = uk_rules();
    auto src = make_default_cover_source(rules, 4);
    std::vector<CoverLine> all;
    src->lines_touching({-500, -500}, {500, 500}, all);
    REQUIRE(all.size() > 20);
    // Every line a 250 m tile sees is a line the big box sees, identical (whole lines, same seed and points).
    std::size_t checked = 0;
    for (double ty = -500; ty < 500; ty += 250)
        for (double tx = -500; tx < 500; tx += 250) {
            std::vector<CoverLine> t;
            src->lines_touching({tx, ty}, {tx + 250, ty + 250}, t);
            for (const auto& l : t) {
                bool found = false;
                for (const auto& m : all)
                    if (m.seed == l.seed && m.points.size() == l.points.size() && m.points.front().x == l.points.front().x && m.points.back().y == l.points.back().y) found = true;
                if (!found && l.points.front().x > -480 && l.points.front().x < 480 && l.points.front().y > -480 && l.points.front().y < 480) CHECK(found);
                ++checked;
            }
        }
    CHECK(checked > 20);
    // Trees: a tile's points are the points of the big box that lie in it.
    std::vector<CoverPoint> big, part;
    src->points_in({-400, -400}, {400, 400}, big);
    src->points_in({-100, -50}, {150, 200}, part);
    std::size_t in = 0;
    for (const auto& p : big) in += p.pos.x >= -100 && p.pos.x < 150 && p.pos.y >= -50 && p.pos.y < 200;
    CHECK(in == part.size());
    // Ground classes: wood is not rectangles. Over every connected wood patch of the default source the share of its
    // bounding box that it fills (a rectangle fills 100 %) averages, weighted by area, well under 75 %.
    const int N = 256;
    std::vector<int> lab(static_cast<std::size_t>(N) * N, 0);
    std::vector<unsigned char> wood(static_cast<std::size_t>(N) * N, 0);
    for (int j = 0; j < N; ++j)
        for (int i = 0; i < N; ++i) wood[static_cast<std::size_t>(j) * N + i] = src->ground_at({-1024 + 8.0 * i, -1024 + 8.0 * j}).land == LandClass::Wood;
    int nlab = 0;
    double fill_sum = 0, area_sum = 0;
    for (int j = 0; j < N; ++j)
        for (int i = 0; i < N; ++i) {
            const std::size_t k = static_cast<std::size_t>(j) * N + i;
            if (!wood[k] || lab[k]) continue;
            ++nlab;
            std::vector<std::pair<int, int>> st{{i, j}};
            lab[k] = nlab;
            int cnt = 0, x0 = i, x1 = i, y0 = j, y1 = j;
            while (!st.empty()) {
                const auto [ci, cj] = st.back();
                st.pop_back();
                ++cnt;
                x0 = std::min(x0, ci); x1 = std::max(x1, ci); y0 = std::min(y0, cj); y1 = std::max(y1, cj);
                const int di[4] = {1, -1, 0, 0}, dj[4] = {0, 0, 1, -1};
                for (int q = 0; q < 4; ++q) {
                    const int ni = ci + di[q], nj = cj + dj[q];
                    if (ni < 0 || nj < 0 || ni >= N || nj >= N) continue;
                    const std::size_t nk = static_cast<std::size_t>(nj) * N + ni;
                    if (wood[nk] && !lab[nk]) { lab[nk] = nlab; st.push_back({ni, nj}); }
                }
            }
            if (cnt < 20) continue;  // under 1.3 ha
            fill_sum += cnt * (static_cast<double>(cnt) / static_cast<double>((x1 - x0 + 1) * (y1 - y0 + 1)));
            area_sum += cnt;
        }
    std::printf("[test_world] wood patches: %d, area-weighted bounding-box fill %.2f\n", nlab, area_sum > 0 ? fill_sum / area_sum : 0.0);
    CHECK(area_sum > 0);
    CHECK(fill_sum / area_sum < 0.75);
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
    // The per-seed variation stays within the stated fractions of the region's values.
    for (std::uint64_t seed : {1ull, 2ull, 3ull, 4ull}) {
        TestWorldParams q;
        q.seed = seed;
        const TestWorldBundle c = make_test_world(q, rules);
        const double r = c.rules->world_cover.field_area_ha / rules->world_cover.field_area_ha;
        CHECK(r >= 1.0 - q.field_area_var - 1e-9);
        CHECK(r <= 1.0 + q.field_area_var + 1e-9);
    }
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
        CHECK(cpprev::write_png(tag + "cover.png", cpprev::cover_map(*b.world, *b.terrain, *b.cover, *b.rules, 2.0)));
        // A closer top-down crop, a 640 x 960 m portrait patch at 1 m per pixel, north up, for photo comparison.
        const TestWorld& w = *b.world;
        const Vec2 sc = w.scarp_centre();
        CHECK(cpprev::write_png(tag + "cover_close.png", cpprev::cover_crop(*b.world, *b.terrain, *b.cover, *b.rules, {sc.x - 320, sc.y - 480}, 640, 960, 1.0)));
        // Same ground width (2000 m, north up) as the 2 km vertical aerials 46-59 (they are 1 m per pixel; this is 2 m per pixel).
        CHECK(cpprev::write_png(tag + "match_2km.png", cpprev::cover_crop(*b.world, *b.terrain, *b.cover, *b.rules, {-1000, -1000}, 2000, 2000, 2.0)));
        auto hz = [&](double x, double y) { return b.terrain->height(x, y); };
        {  // along the valley, from the low end
            const Vec2 a = w.valley_axis_point(-900), c = w.valley_axis_point(0);
            CHECK(cpprev::write_png(tag + "oblique_valley.png", cpprev::oblique(w, *b.terrain, b.cover.get(), b.rules.get(), a.x, a.y, hz(a.x, a.y) + 120,
                                                                              c.x, c.y, hz(c.x, c.y), 960, 540, 60)));
        }
        {  // across the valley, looking at the scarp
            const Vec2 cd = w.cross_dir();
            const Vec2 tgt = sc + cd * 190, eye = sc - cd * 520 - w.valley_dir() * 250;
            CHECK(cpprev::write_png(tag + "oblique_scarp.png", cpprev::oblique(w, *b.terrain, b.cover.get(), b.rules.get(), eye.x, eye.y, hz(eye.x, eye.y) + 60,
                                                                             tgt.x, tgt.y, hz(tgt.x, tgt.y) + 15, 960, 540, 65)));
        }
        {  // high corner view of the whole world
            CHECK(cpprev::write_png(tag + "oblique_overview.png", cpprev::oblique(w, *b.terrain, b.cover.get(), b.rules.get(), -1000, -1000, 380,
                                                                                0, 200, 0, 960, 540, 60)));
        }
    }
}
