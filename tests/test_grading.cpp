// Tests for cp/grading.h. The golden files (tests/data/grading/*.json) are produced by running the
// prototype's earthworks.js: prototypes/road-draw/tools/grade_golden.mjs. The C++ result must match
// them within kTolHeight (graded heights are stored quantised to 0.1 mm, so +-0.05 mm of the
// tolerance is the golden's own rounding). The remaining tests port the prototype's invariants
// (tools/scene.mjs --check) that are properties of the grading itself, evaluated on C++ output.
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "doctest/doctest.h"
#include "nlohmann/json.hpp"
#include "cp/grading.h"
#include "cp/road_commands.h"

using namespace cp;
using json = nlohmann::json;

namespace {

constexpr double kTolHeight = 1.5e-4;  // m: 0.15 mm (golden quantum 0.1 mm + float32 storage)
constexpr double kTolGeom = 1e-9;      // m: classification outputs (doubles on both sides)

const char* const kCases[] = {"straight_crossfall", "cutting",     "embankment",        "tight_curve_high_fill",
                              "wall_steep_side",    "bridge_valley", "tunnel_curved_grade", "dual_pair",
                              "crossing_and_ends"};

bool verbose() {
#if defined(_MSC_VER)
    char* v = nullptr;
    std::size_t len = 0;
    const bool on = _dupenv_s(&v, &len, "CP_GRADING_VERBOSE") == 0 && v != nullptr;
    std::free(v);
    return on;
#else
    return std::getenv("CP_GRADING_VERBOSE") != nullptr;
#endif
}

// ---- analytic natural ground (mirror of terrainFn in grade_golden.mjs) ----
struct Term {
    std::string k;
    double c0 = 0, cx = 0, cy = 0, amp = 0, sx = 0, sy = 0, slope = 0, yc = 0, sig = 0, fx = 0, fy = 0, ph = 0;
};

struct Terrain {
    double ox = 0, oy = 0;
    std::vector<Term> terms;
    double operator()(double X, double Y) const {
        const double x = X - ox, y = Y - oy;
        double z = 0;
        for (const Term& t : terms) {
            if (t.k == "plane") z += t.c0 + t.cx * x + t.cy * y;
            else if (t.k == "gauss")
                z += t.amp * std::exp(-((x - t.cx) * (x - t.cx) / (2 * t.sx * t.sx) + (y - t.cy) * (y - t.cy) / (2 * t.sy * t.sy)));
            else if (t.k == "slopeEnv") z += t.slope * (y - t.yc) * std::exp(-((x - t.cx) * (x - t.cx)) / (2 * t.sig * t.sig));
            else if (t.k == "sin") z += t.amp * std::sin(t.fx * x + t.fy * y + t.ph);
        }
        return z;
    }
};

double num(const json& j, const char* key) { return j.contains(key) ? j[key].get<double>() : 0.0; }

Terrain parse_terrain(const json& j) {
    Terrain t;
    t.ox = j["off"][0].get<double>();
    t.oy = j["off"][1].get<double>();
    for (const json& e : j["terms"]) {
        Term m;
        m.k = e["k"].get<std::string>();
        m.c0 = num(e, "c0"); m.cx = num(e, "cx"); m.cy = num(e, "cy"); m.amp = num(e, "amp");
        m.sx = num(e, "sx"); m.sy = num(e, "sy"); m.slope = num(e, "slope"); m.yc = num(e, "yc");
        m.sig = num(e, "sig"); m.fx = num(e, "fx"); m.fy = num(e, "fy"); m.ph = num(e, "ph");
        t.terms.push_back(m);
    }
    return t;
}

GradingParams parse_params(const json& j) {
    GradingParams p;
    p.formation_drop = j["formDrop"];
    p.batter = j["batter"];
    p.wall_thickness = j["wallThick"];
    p.verge = j["verge"];
    p.plane_over = j["planeC"];
    p.wall_start = j["wallStart"];
    p.wall_cap = j["wallCap"];
    p.wall_slack = j["wallSlack"];
    const json& t = j["tunnel"];
    p.tunnel.lining = t["lining"];
    p.tunnel.margin = t["margin"];
    p.tunnel.floor_depth = t["floorDepth"];
    p.tunnel.rise_k = t["riseK"];
    p.tunnel.clear_height = t["clearHeight"];
    p.tunnel.head_t = t["headT"];
    p.tunnel.chunk = t["chunk"];
    return p;
}

// One golden case with the C++ pipeline already run on its sampled roads.
struct Case {
    std::string name;
    json g;
    Terrain terrain;
    GroundFn ground;
    GradingParams params;
    std::vector<SampledRoad> roads;
    std::vector<GradingRoad> prepared;
};

std::string read_file(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

std::unique_ptr<Case> make_case(const std::string& name) {
    auto c = std::make_unique<Case>();
    c->name = name;
    const std::string text = read_file(std::string(CP_TEST_DATA_DIR) + "/grading/" + name + ".json");
    REQUIRE_MESSAGE(!text.empty(), "missing golden " << name << " (run prototypes/road-draw/tools/grade_golden.mjs)");
    c->g = json::parse(text, nullptr, false);
    REQUIRE(!c->g.is_discarded());
    c->terrain = parse_terrain(c->g["terrain"]);
    const Terrain* tp = &c->terrain;
    c->ground = [tp](double x, double y) { return (*tp)(x, y); };
    c->params = parse_params(c->g["params"]);
    for (const json& rj : c->g["roads"]) {
        SampledRoad r;
        r.type_id = rj["type_id"];
        r.half_width = rj["hw"];
        r.dual = rj["dual"];
        r.median_gap = rj["gap"];
        for (const json& s : rj["samples"]) {
            RoadSample rs;
            rs.s = s[0]; rs.x = s[1]; rs.y = s[2]; rs.z = s[3]; rs.h = s[4];
            r.samples.push_back(rs);
        }
        c->roads.push_back(std::move(r));
    }
    for (SampledRoad& r : c->roads) classify_road(r, c->ground, c->params, StructureMode::Auto);
    c->prepared = prepare_grading_roads(c->roads, c->ground, c->params);
    return c;
}

TileSpec golden_tile(const json& t) {
    return {t["x0"], t["y0"], t["nx"], t["ny"], t["res"]};
}

// Graded ground for a whole case region (tile big enough to hold every road plus margin).
struct Graded {
    TileSpec t;
    std::vector<float> base, h;
    double at(double x, double y) const {
        const double fx = std::clamp((x - t.x0) / t.res, 0.0, t.nx - 1.0001), fy = std::clamp((y - t.y0) / t.res, 0.0, t.ny - 1.0001);
        const int i = static_cast<int>(fx), j = static_cast<int>(fy);
        const double u = fx - i, v = fy - j;
        auto g = [&](int a, int b) { return static_cast<double>(h[static_cast<std::size_t>(b) * static_cast<std::size_t>(t.nx) + static_cast<std::size_t>(a)]); };
        return (g(i, j) * (1 - u) + g(i + 1, j) * u) * (1 - v) + (g(i, j + 1) * (1 - u) + g(i + 1, j + 1) * u) * v;
    }
};

Graded grade_region(const Case& c, double margin = 45) {
    double x0 = 1e18, y0 = 1e18, x1 = -1e18, y1 = -1e18;
    for (const SampledRoad& r : c.roads)
        for (const RoadSample& s : r.samples) {
            x0 = std::min(x0, s.x); x1 = std::max(x1, s.x); y0 = std::min(y0, s.y); y1 = std::max(y1, s.y);
        }
    Graded g;
    g.t.res = 0.5;
    g.t.x0 = std::floor((x0 - margin) * 2) / 2;
    g.t.y0 = std::floor((y0 - margin) * 2) / 2;
    g.t.nx = static_cast<int>(std::ceil((x1 + margin - g.t.x0) * 2)) + 1;
    g.t.ny = static_cast<int>(std::ceil((y1 + margin - g.t.y0) * 2)) + 1;
    g.base = sample_natural_tile(c.ground, g.t);
    g.h = grade_tile(c.prepared, c.params, g.t, g.base).height;
    return g;
}

}  // namespace

// =============================================================================================
// Golden cross-checks against the JS
// =============================================================================================

TEST_CASE("grading: classification, walls, medians, reach and portal planes match the prototype") {
    for (const char* name : kCases) {
        auto c = make_case(name);
        INFO("case " << name);
        REQUIRE(c->roads.size() == c->g["roads"].size());
        for (std::size_t ri = 0; ri < c->roads.size(); ++ri) {
            const SampledRoad& r = c->roads[ri];
            const json& ex = c->g["roads"][ri]["expect"];
            const std::size_t n = r.samples.size();
            int flag_diffs = 0;
            double ww_diff = 0;
            for (std::size_t i = 0; i < n; ++i) {
                const RoadSample& s = r.samples[i];
                flag_diffs += (s.br != (ex["br"][i].get<int>() != 0)) + (s.tu != (ex["tu"][i].get<int>() != 0)) +
                              (s.portal != (ex["portal"][i].get<int>() != 0)) + (s.wL != ex["wL"][i].get<int>()) +
                              (s.wR != ex["wR"][i].get<int>()) + (s.wL0 != ex["wL0"][i].get<int>()) +
                              (s.wR0 != ex["wR0"][i].get<int>()) + (s.mL != (ex["mL"][i].get<int>() != 0)) +
                              (s.mR != (ex["mR"][i].get<int>() != 0));
                ww_diff = std::max({ww_diff, std::abs(s.wwL - ex["wwL"][i].get<double>()), std::abs(s.wwR - ex["wwR"][i].get<double>())});
            }
            CHECK_MESSAGE(flag_diffs == 0, "road " << ri << ": " << flag_diffs << " flag differences");
            CHECK_MESSAGE(ww_diff < kTolGeom, "road " << ri << ": wall weight diff " << ww_diff);
            std::vector<int> portals = ex["portals"].get<std::vector<int>>();
            CHECK(r.portals == portals);
            const GradingRoad& g = c->prepared[ri];
            CHECK(g.reach == doctest::Approx(ex["reach"].get<double>()).epsilon(1e-12));
            CHECK(g.bbox.x0 == doctest::Approx(ex["bbox"][0].get<double>()));
            CHECK(g.bbox.y0 == doctest::Approx(ex["bbox"][1].get<double>()));
            CHECK(g.bbox.x1 == doctest::Approx(ex["bbox"][2].get<double>()));
            CHECK(g.bbox.y1 == doctest::Approx(ex["bbox"][3].get<double>()));
            CHECK(static_cast<int>(g.samples.size()) == ex["ndec"].get<int>());
            REQUIRE(g.planes.size() == ex["planes"].size());
            for (std::size_t k = 0; k < g.planes.size(); ++k) {
                const json& pj = ex["planes"][k];
                CHECK(g.planes[k].k0 == pj[0].get<int>());
                CHECK(g.planes[k].k1 == pj[1].get<int>());
                CHECK(g.planes[k].x == doctest::Approx(pj[2].get<double>()));
                CHECK(g.planes[k].y == doctest::Approx(pj[3].get<double>()));
                CHECK(g.planes[k].nx == doctest::Approx(pj[4].get<double>()));
                CHECK(g.planes[k].ny == doctest::Approx(pj[5].get<double>()));
            }
        }
    }
}

TEST_CASE("grading: graded tiles match the prototype within 0.15 mm") {
    for (const char* name : kCases) {
        auto c = make_case(name);
        INFO("case " << name);
        double worst = 0;
        std::size_t texels = 0, over = 0;
        for (const json& tj : c->g["tiles"]) {
            const TileSpec t = golden_tile(tj);
            const std::vector<float> nat = sample_natural_tile(c->ground, t);
            const GradedTile gt = grade_tile(c->prepared, c->params, t, nat);
            REQUIRE(gt.height.size() == tj["h"].size());
            for (std::size_t o = 0; o < gt.height.size(); ++o) {
                const double d = std::abs(static_cast<double>(gt.height[o]) - tj["h"][o].get<double>() * 1e-4);
                worst = std::max(worst, d);
                over += d > kTolHeight;
                ++texels;
            }
        }
        if (verbose()) std::fprintf(stderr, "[grading golden] %-24s %zu texels, max |dh| = %.3g mm\n", name, texels, worst * 1000);
        CHECK_MESSAGE(over == 0, over << " texels differ by more than " << kTolHeight * 1000 << " mm, worst " << worst * 1000 << " mm");
    }
}

TEST_CASE("grading: wall runs match the prototype") {
    int total = 0;
    for (const char* name : kCases) {
        auto c = make_case(name);
        INFO("case " << name);
        for (std::size_t ri = 0; ri < c->roads.size(); ++ri) {
            const std::vector<WallRun> runs = wall_runs(c->roads[ri], c->ground, c->params);
            const json& ex = c->g["walls"][ri];
            REQUIRE(runs.size() == ex.size());
            for (std::size_t k = 0; k < runs.size(); ++k) {
                const json& rj = ex[k];
                CHECK(runs[k].side == rj["side"].get<std::string>()[0]);
                CHECK(runs[k].cut == rj["cut"].get<bool>());
                CHECK(runs[k].lo == doctest::Approx(rj["lo"].get<double>()));
                CHECK(runs[k].hi == doctest::Approx(rj["hi"].get<double>()));
                REQUIRE(runs[k].pts.size() == rj["pts"].size());
                for (std::size_t q = 0; q < runs[k].pts.size(); ++q) {
                    const WallPoint& w = runs[k].pts[q];
                    const json& pj = rj["pts"][q];
                    const double v[6] = {w.x, w.y, w.zTop, w.zBot, w.top, w.h};
                    for (int m = 0; m < 6; ++m) CHECK(std::abs(v[m] - pj[static_cast<std::size_t>(m)].get<double>()) < 1e-4);
                    ++total;
                }
            }
        }
    }
    CHECK(total > 200);  // the cases contain real walls
}

TEST_CASE("grading: tunnel table and bore probes match the prototype") {
    int tunnels = 0, probes_in = 0, probes_out = 0;
    for (const char* name : kCases) {
        auto c = make_case(name);
        INFO("case " << name);
        const json& tj = c->g["tunnel"];
        const std::vector<TunnelVolume> vols = tunnel_volumes(c->roads, c->params);
        REQUIRE(vols.size() == tj["vols"].size());
        for (std::size_t i = 0; i < vols.size(); ++i) {
            CHECK(vols[i].road == tj["vols"][i]["road"].get<int>());
            CHECK(vols[i].a == tj["vols"][i]["a"].get<int>());
            CHECK(vols[i].b == tj["vols"][i]["b"].get<int>());
            CHECK(static_cast<int>(vols[i].st.size()) == tj["vols"][i]["n"].get<int>());
        }
        tunnels += static_cast<int>(vols.size());
        std::vector<float> data;
        const int rows = pack_tunnels(data, 64, vols, c->params);
        const int width = tunnel_table_width(c->params);
        CHECK(width == tj["width"].get<int>());
        REQUIRE(rows == tj["rows"].get<int>());
        const std::size_t cnt = static_cast<std::size_t>(rows) * static_cast<std::size_t>(width) * 4;
        REQUIRE(cnt == tj["data"].size());
        double worst = 0;
        for (std::size_t i = 0; i < cnt; ++i) worst = std::max(worst, std::abs(static_cast<double>(data[i]) - tj["data"][i].get<double>() * 1e-4));
        CHECK_MESSAGE(worst < 1e-4, "table differs by " << worst);
        int wrong = 0;
        for (const json& pj : tj["probes"]) {
            const bool got = in_bore(data.data(), width, rows, pj[0], pj[1], pj[2], c->params);
            wrong += got != (pj[3].get<int>() != 0);
            (got ? probes_in : probes_out)++;
        }
        CHECK_MESSAGE(wrong == 0, wrong << " bore probes differ");
    }
    CHECK(tunnels >= 1);
    CHECK(probes_in > 50);
    CHECK(probes_out > 50);
}

// =============================================================================================
// Properties and determinism
// =============================================================================================

TEST_CASE("grading: result does not depend on how the world is cut into tiles") {
    auto c = make_case("wall_steep_side");
    const Graded big = grade_region(*c);
    const double res = 0.5;
    // a 40 x 40 texel tile at an arbitrary integer offset inside the region
    const TileSpec t{big.t.x0 + 71 * res, big.t.y0 + 33 * res, 40, 40, res};
    const GradedTile small = grade_tile(c->prepared, c->params, t, sample_natural_tile(c->ground, t));
    int bad = 0;
    for (int j = 0; j < t.ny; ++j)
        for (int i = 0; i < t.nx; ++i)
            bad += small.height[static_cast<std::size_t>(j * t.nx + i)] !=
                   big.h[static_cast<std::size_t>((j + 33) * big.t.nx + (i + 71))];
    CHECK(bad == 0);
    // tile_spec addresses 20 km+ maps by integer keys
    const TileSpec far = tile_spec({40000, -30000}, 128, 0.5);
    CHECK(far.x0 == 2560000.0);
    CHECK(far.y0 == -1920000.0);
    CHECK(far.nx == 128);
}

TEST_CASE("grading: no roads leaves natural ground untouched and tile call is deterministic") {
    const TileSpec t{100, 200, 64, 48, 0.5};
    auto ground = [](double x, double y) { return 0.1 * x + 0.05 * y; };
    const std::vector<float> nat = sample_natural_tile(ground, t);
    const GradedTile a = grade_tile({}, GradingParams{}, t, nat);
    CHECK(a.height == nat);
    CHECK(std::count(a.footprint.begin(), a.footprint.end(), 1) == 0);
    auto c = make_case("tunnel_curved_grade");
    const TileSpec t2 = golden_tile(c->g["tiles"][0]);
    const std::vector<float> n2 = sample_natural_tile(c->ground, t2);
    CHECK(grade_tile(c->prepared, c->params, t2, n2).height == grade_tile(c->prepared, c->params, t2, n2).height);
}

TEST_CASE("grading: footprint mask covers the carriageway and nothing far from roads") {
    auto c = make_case("straight_crossfall");
    const Graded g = grade_region(*c);
    const GradedTile gt = grade_tile(c->prepared, c->params, g.t, g.base);
    const SampledRoad& r = c->roads[0];
    for (std::size_t i = 4; i + 4 < r.samples.size(); i += 5) {
        const RoadSample& s = r.samples[i];
        const int ti = static_cast<int>(std::lround((s.x - g.t.x0) / 0.5)), tj = static_cast<int>(std::lround((s.y - g.t.y0) / 0.5));
        CHECK(gt.footprint[static_cast<std::size_t>(tj * g.t.nx + ti)] == 1);
    }
    CHECK(gt.footprint[0] == 0);
}

TEST_CASE("grading: Auto structure rule reports required bridge and tunnel spans") {
    auto bridge = make_case("bridge_valley");
    const auto b = required_structures(bridge->roads[0], bridge->ground, bridge->params);
    REQUIRE(b.size() == 1);
    CHECK(b[0].kind == StructureKind::Bridge);
    CHECK(b[0].last > b[0].first);
    auto tunnel = make_case("tunnel_curved_grade");
    const auto t = required_structures(tunnel->roads[0], tunnel->ground, tunnel->params);
    REQUIRE(t.size() == 1);
    CHECK(t[0].kind == StructureKind::Tunnel);
    // classify(Auto) agrees with the query
    int tu = 0;
    for (const RoadSample& s : tunnel->roads[0].samples) tu += s.tu;
    CHECK(tu == t[0].last - t[0].first + 1);
    auto flat = make_case("straight_crossfall");
    CHECK(required_structures(flat->roads[0], flat->ground, flat->params).empty());
}

TEST_CASE("grading: authored structure spans are the truth, whatever the ground says") {
    auto c = make_case("straight_crossfall");  // plain ground: Auto finds no structure
    SampledRoad r = c->roads[0];
    for (std::size_t i = 40; i <= 60; ++i) r.samples[i].tu = true;
    for (std::size_t i = 80; i <= 90; ++i) r.samples[i].br = true;
    classify_road(r, c->ground, c->params, StructureMode::Authored);
    CHECK(r.samples[40].portal);
    CHECK(r.samples[60].portal);
    CHECK_FALSE(r.samples[50].portal);
    CHECK(r.portals == std::vector<int>{40, 60});
    CHECK(r.samples[85].br);
    CHECK_FALSE(r.samples[85].tu);
    CHECK_FALSE(r.samples[10].br);
    // walled approach next to the structures
    CHECK(r.samples[39].wL != 0);
    CHECK(r.samples[61].wR != 0);
}

// =============================================================================================
// Prototype invariants (tools/scene.mjs --check) on the C++ output
// =============================================================================================

namespace {

struct Hit {
    int count = 0, total = 0;
    std::string worst;
    double worst_v = -1;
    void bad(double v, const std::string& what) {
        ++count;
        if (v > worst_v) { worst_v = v; worst = what; }
    }
};

// Is another road's footprint / slope reach within `reach` m of (x, y)?
bool other_road_near(const Case& c, std::size_t self, double x, double y, double reach) {
    for (std::size_t ri = 0; ri < c.roads.size(); ++ri) {
        if (ri == self) continue;
        const double lim = reach + c.roads[ri].half_width;
        for (const RoadSample& s : c.roads[ri].samples)
            if ((s.x - x) * (s.x - x) + (s.y - y) * (s.y - y) < lim * lim) return true;
    }
    return false;
}

struct PortalPlaneQ {
    const TunnelStation* p;
    double nx, ny, lx, ly, hw;
};

std::vector<PortalPlaneQ> portal_planes(const TunnelVolume& v, double hw) {
    const TunnelStation &a = v.st.front(), &b = v.st.back();
    std::vector<PortalPlaneQ> out;
    out.push_back({&a, std::cos(a.h), std::sin(a.h), -std::sin(a.h), std::cos(a.h), hw});
    out.push_back({&b, -std::cos(b.h), -std::sin(b.h), -std::sin(b.h), std::cos(b.h), hw});
    return out;
}

}  // namespace

TEST_CASE("grading invariants: ground under road, slopes daylight, median flat") {
    int under_total = 0, slopes_total = 0, median_total = 0;
    for (const char* name : kCases) {
        auto c = make_case(name);
        INFO("case " << name);
        const Graded g = grade_region(*c);
        Hit under, slopes, median;
        for (std::size_t ri = 0; ri < c->roads.size(); ++ri) {
            const SampledRoad& r = c->roads[ri];
            const double hw = r.half_width, reach = hw + c->params.wall_start * c->params.batter + 2;
            for (std::size_t si = 0; si < r.samples.size(); ++si) {
                const RoadSample& s = r.samples[si];
                if (s.br || s.tu) continue;
                // ground_under_road: road - 0.6 <= ground <= road - 0.05 across the whole footprint
                for (double lat = -hw; lat <= hw + 1e-6; lat += 0.5) {
                    const double l = std::min(hw - 0.01, std::max(-hw + 0.01, lat));
                    const double x = s.x - std::sin(s.h) * l, y = s.y + std::cos(s.h) * l;
                    ++under.total;
                    const double d = s.z - g.at(x, y);
                    if ((d < 0.05 - 1e-4 || d > 0.6 + 1e-4) && !other_road_near(*c, ri, x, y, 6))
                        under.bad(d < 0.05 ? 0.05 - d : d - 0.6, "road " + std::to_string(ri) + " station " + std::to_string(si));
                }
                // slopes_daylight: unwalled sides never overshoot the natural ground by more than 0.3 m and reach it
                for (const int sg : {1, -1}) {
                    if (sg == 1 ? s.wL : s.wR) continue;
                    bool skip = false, counted = false;
                    double worst = 0;
                    for (double d = hw + 0.5; d <= reach + 4; d += 0.5) {
                        const double x = s.x - std::sin(s.h) * sg * d, y = s.y + std::cos(s.h) * sg * d;
                        if (other_road_near(*c, ri, x, y, c->params.wall_start * c->params.batter + 2)) { skip = true; break; }
                        const double b = c->ground(x, y), gg = g.at(x, y);
                        const bool cut = b > s.z - c->params.formation_drop;
                        worst = std::max(worst, cut ? gg - b : b - gg);
                        if (d >= reach + 3.5 && std::abs(gg - b) > 0.15 && !counted) {
                            slopes.bad(std::abs(gg - b), "not converged at road " + std::to_string(ri) + " station " + std::to_string(si));
                            counted = true;
                        }
                    }
                    if (skip) continue;
                    ++slopes.total;
                    if (worst > 0.3) slopes.bad(worst, "overshoot at road " + std::to_string(ri) + " station " + std::to_string(si));
                }
                // median_flat: no step > 0.5 m between adjacent texels between the carriageways of a dual pair
                if (!r.dual) continue;
                const double nx = -std::sin(s.h), ny = std::cos(s.h), lim = r.median_gap + 2 * hw + 3;
                const RoadSample* partner = nullptr;
                double bd = 1e18, sgn = 1;
                for (std::size_t r2 = 0; r2 < c->roads.size(); ++r2) {
                    if (r2 == ri || c->roads[r2].type_id != r.type_id) continue;
                    for (const RoadSample& t : c->roads[r2].samples) {
                        const double dx = t.x - s.x, dy = t.y - s.y, d = std::hypot(dx, dy);
                        if (d > lim) continue;
                        const double lat = dx * nx + dy * ny;
                        if (std::abs(d - std::abs(lat)) < 2 && d < bd && std::cos(t.h - s.h) < -0.7) { bd = d; partner = &t; sgn = lat > 0 ? 1 : -1; }
                    }
                }
                if (!partner) continue;
                const double L = std::hypot(partner->x - s.x, partner->y - s.y);
                double prev = 0;
                bool have = false;
                for (double d = 0; d <= L + 1e-6; d += 0.5) {
                    const double gg = g.at(s.x + nx * sgn * d, s.y + ny * sgn * d);
                    if (have) {
                        ++median.total;
                        if (std::abs(gg - prev) > 0.5) median.bad(std::abs(gg - prev), "step at road " + std::to_string(ri) + " station " + std::to_string(si));
                    }
                    prev = gg;
                    have = true;
                }
            }
        }
        CHECK_MESSAGE(under.count == 0, "ground_under_road: " << under.count << "/" << under.total << " bad, worst " << under.worst_v << " " << under.worst);
        CHECK_MESSAGE(slopes.count == 0, "slopes_daylight: " << slopes.count << "/" << slopes.total << " bad, worst " << slopes.worst_v << " " << slopes.worst);
        CHECK_MESSAGE(median.count == 0, "median_flat: " << median.count << "/" << median.total << " bad, worst " << median.worst_v << " " << median.worst);
        under_total += under.total;
        slopes_total += slopes.total;
        median_total += median.total;
    }
    CHECK(under_total > 1000);
    CHECK(slopes_total > 500);
    CHECK(median_total > 100);
}

TEST_CASE("grading invariants: wall height cap, ground hidden by walls, no bare steps") {
    int wall_pts = 0, hidden_samples = 0, steps_checked = 0;
    for (const char* name : kCases) {
        auto c = make_case(name);
        INFO("case " << name);
        const Graded g = grade_region(*c);
        Hit cap, hidden, bare;
        struct WallQ { WallPoint p; double sg, lo, hi; };
        std::map<long long, std::vector<WallQ>> wall_grid;
        const double WC = 4;
        auto key = [&](long long a, long long b) { return a * 100003 + b; };
        for (std::size_t ri = 0; ri < c->roads.size(); ++ri) {
            const std::vector<WallRun> runs = wall_runs(c->roads[ri], c->ground, c->params);
            for (const WallRun& run : runs) {
                const double sg = run.side == 'L' ? 1 : -1;
                std::vector<double> cum{0.0};
                for (std::size_t i = 1; i < run.pts.size(); ++i)
                    cum.push_back(cum.back() + std::hypot(run.pts[i].x - run.pts[i - 1].x, run.pts[i].y - run.pts[i - 1].y));
                for (std::size_t pi = 0; pi < run.pts.size(); ++pi) {
                    const WallPoint& p = run.pts[pi];
                    ++wall_pts;
                    // wall_height_cap: wall height never exceeds wall_cap + 0.5
                    if (p.zTop - p.zBot > c->params.wall_cap + 0.5) cap.bad(p.zTop - p.zBot, "wall at " + std::to_string(p.x));
                    wall_grid[key(static_cast<long long>(std::floor(p.x / WC)), static_cast<long long>(std::floor(p.y / WC)))].push_back({p, sg, run.lo, run.hi});
                    // ground_hidden_by_walls: across the faces the ground never pokes through the wall
                    const bool end_zone = cum[pi] < 12 || cum.back() - cum[pi] < 12;
                    const double ux = -std::sin(p.h) * sg, uy = std::cos(p.h) * sg;
                    for (double e = run.lo - 0.3; e <= run.hi + 0.3 + 1e-9; e += 0.1) {
                        const double x = p.x + ux * e, y = p.y + uy * e, gg = g.at(x, y);
                        ++hidden.total;
                        const bool inside = e >= run.lo - 1e-9 && e <= run.hi + 1e-9;
                        const double lim = inside ? p.top - 0.3 : p.top;
                        if (gg > lim + 0.02) hidden.bad(gg - lim, std::string(run.cut ? "cut" : "fill") + " wall ground above top at e=" + std::to_string(e));
                        else if (!end_zone && e > run.hi + 0.1) {
                            bool near_other = false;
                            for (std::size_t o = 0; o < c->roads.size() && !near_other; ++o) {
                                if (o == ri) continue;
                                for (const RoadSample& q : c->roads[o].samples)
                                    if (std::abs(q.x - x) < 45 && std::abs(q.y - y) < 45 && std::hypot(q.x - x, q.y - y) < c->roads[o].half_width + 12) { near_other = true; break; }
                            }
                            if (!near_other && std::abs(gg - c->ground(x, y)) > 0.25) hidden.bad(std::abs(gg - c->ground(x, y)), "ramp outside the outer face at e=" + std::to_string(e));
                        }
                    }
                }
            }
        }
        // no_bare_steps: every texel step > 1 m lies inside a wall solid or within a bridge / tunnel zone
        auto in_wall = [&](double x, double y) {
            const long long cx = static_cast<long long>(std::floor(x / WC)), cy = static_cast<long long>(std::floor(y / WC));
            for (long long a = -1; a <= 1; ++a)
                for (long long b = -1; b <= 1; ++b) {
                    const auto it = wall_grid.find(key(cx + a, cy + b));
                    if (it == wall_grid.end()) continue;
                    for (const WallQ& q : it->second) {
                        const double dx = x - q.p.x, dy = y - q.p.y;
                        if (dx * dx + dy * dy > 3.5 * 3.5) continue;
                        const double e = (dx * -std::sin(q.p.h) + dy * std::cos(q.p.h)) * q.sg, al = dx * std::cos(q.p.h) + dy * std::sin(q.p.h);
                        if (e >= q.lo - 0.8 && e <= q.hi + 0.8 && std::abs(al) <= 2.5) return true;
                    }
                }
            return false;
        };
        auto in_struct_zone = [&](double x, double y) {
            for (const SampledRoad& r : c->roads)
                for (const RoadSample& s : r.samples)
                    if ((s.br || s.tu) && (s.x - x) * (s.x - x) + (s.y - y) * (s.y - y) < (r.half_width + 12) * (r.half_width + 12)) return true;
            return false;
        };
        const int nx = g.t.nx, ny = g.t.ny;
        for (int j = 0; j < ny; ++j)
            for (int i = 0; i < nx; ++i) {
                const double h0 = g.h[static_cast<std::size_t>(j * nx + i)];
                for (const int di : {0, 1}) {
                    const int dj = 1 - di;
                    if (i + di >= nx || j + dj >= ny) continue;
                    const double st = std::abs(g.h[static_cast<std::size_t>((j + dj) * nx + i + di)] - h0);
                    if (st <= 1.0) continue;
                    ++bare.total;
                    const double x = g.t.x0 + (i + di / 2.0) * 0.5, y = g.t.y0 + (j + dj / 2.0) * 0.5;
                    if (in_wall(x, y) || in_struct_zone(x, y)) continue;
                    bare.bad(st, "bare step at " + std::to_string(x) + "," + std::to_string(y));
                }
            }
        steps_checked += bare.total;
        hidden_samples += hidden.total;
        CHECK_MESSAGE(cap.count == 0, "wall_height_cap: " << cap.count << " bad, worst " << cap.worst_v);
        CHECK_MESSAGE(hidden.count == 0, "ground_hidden_by_walls: " << hidden.count << "/" << hidden.total << " bad, worst " << hidden.worst_v << " " << hidden.worst);
        CHECK_MESSAGE(bare.count == 0, "no_bare_steps: " << bare.count << "/" << bare.total << " bad, worst " << bare.worst_v << " " << bare.worst);
    }
    CHECK(wall_pts > 200);
    CHECK(hidden_samples > 5000);
    CHECK(steps_checked > 100);
}

TEST_CASE("grading invariants: tunnel bore clear and step at the portal plane") {
    auto c = make_case("tunnel_curved_grade");
    const Graded g = grade_region(*c);
    const GradingParams& p = c->params;
    const std::vector<TunnelVolume> vols = tunnel_volumes(c->roads, p);
    REQUIRE(vols.size() == 1);
    std::vector<float> data;
    const int rows = pack_tunnels(data, 64, vols, p);
    const int width = tunnel_table_width(p);
    auto discard = [&](double x, double y, double up) { return in_bore(data.data(), width, rows, x, y, up, p); };
    const double off = p.tunnel.lining + p.tunnel.margin;
    Hit bore, step;
    for (const TunnelVolume& v : vols) {
        const TunnelProfile& pr = v.prof;
        const double W = pr.W, hw = c->roads[static_cast<std::size_t>(v.road)].half_width;
        // (a) the discard volume is complete along the bore (every station, between stations, curved bends)
        for (std::size_t k = 0; k + 1 < v.st.size(); ++k)
            for (const double f : {0.03, 0.25, 0.5, 0.75, 0.97}) {
                const TunnelStation &a = v.st[k], &b = v.st[k + 1];
                const double x = a.x + (b.x - a.x) * f, y = a.y + (b.y - a.y) * f, z = a.z + (b.z - a.z) * f;
                const double h = std::atan2(b.y - a.y, b.x - a.x), lx = -std::sin(h), ly = std::cos(h);
                for (const double q : {0.0, W * 0.5, W - 0.05, -(W - 0.05)})
                    for (const double rel : {0.05, tunnel_roof(pr, std::abs(q)) - 0.05}) {
                        ++bore.total;
                        if (!discard(x + lx * q, y + ly * q, z + rel)) bore.bad(1, "point inside the bore not discarded");
                    }
                ++bore.total;
                if (discard(x, y, z + tunnel_roof(pr, 0, off) + 0.3)) bore.bad(1, "discard reaches above the crown");
                ++bore.total;
                if (discard(x + lx * (W + off + 0.3), y + ly * (W + off + 0.3), z + 2)) bore.bad(1, "discard reaches beside the bore");
            }
        // (b) ground: every terrain point whose height lies inside the bore volume is discarded; none in front of the plane
        for (const PortalPlaneQ& q : portal_planes(v, hw)) {
            for (double al = -3; al <= 3; al += 0.25)
                for (double la = -(W + off + 1); la <= W + off + 1; la += 0.25) {
                    const double x = q.p->x + q.nx * al + q.lx * la, y = q.p->y + q.ny * al + q.ly * la, gg = g.at(x, y);
                    const double rel = gg - q.p->z, roof_o = tunnel_roof(pr, std::min(std::abs(la), W + off), off);
                    const bool inside = al > 0.1 && std::abs(la) <= W + off - 0.1 && rel >= -p.tunnel.floor_depth + 0.3 && rel <= roof_o - 0.3;
                    ++bore.total;
                    if (inside && !discard(x, y, gg)) bore.bad(1, "ground inside the bore left undiscarded");
                    else if (!inside && al < -0.05 && std::abs(la) <= W && discard(x, y, gg)) bore.bad(1, "discard reaches in front of the portal plane");
                }
        }
        // tunnel_step_at_portal: natural by planeC + one texel diagonal behind the plane; formation in front of it
        const double nat = p.plane_over + std::sqrt(2.0) * 0.5 + 0.05;
        for (const PortalPlaneQ& q : portal_planes(v, hw)) {
            for (double la = -(hw + 1.5); la <= hw + 1.5 + 1e-9; la += 0.25) {
                for (double al = nat; al <= p.tunnel.head_t + 1e-9; al += 0.25) {
                    const double x = q.p->x + q.nx * al + q.lx * la, y = q.p->y + q.ny * al + q.ly * la;
                    ++step.total;
                    const double d = std::abs(g.at(x, y) - c->ground(x, y));
                    if (d > 0.3) step.bad(d, "ground not back at natural behind the portal plane");
                }
                if (std::abs(la) > hw) continue;
                for (double al = -4; al <= 0; al += 0.25) {
                    const double x = q.p->x + q.nx * al + q.lx * la, y = q.p->y + q.ny * al + q.ly * la;
                    ++step.total;
                    const double d = g.at(x, y) - (q.p->z - p.formation_drop);
                    if (std::abs(d) > 0.15) step.bad(std::abs(d), "ground off formation in front of the portal plane, al " + std::to_string(al));
                }
            }
        }
    }
    CHECK_MESSAGE(bore.count == 0, "tunnel_bore_clear: " << bore.count << "/" << bore.total << " bad: " << bore.worst);
    CHECK_MESSAGE(step.count == 0, "tunnel_step_at_portal: " << step.count << "/" << step.total << " bad, worst " << step.worst_v << " " << step.worst);
    CHECK(bore.total > 1000);
    CHECK(step.total > 100);
}

// =============================================================================================
// RoadGraph adapter
// =============================================================================================

TEST_CASE("grading adapter: edge sampling follows the centreline, grade and structure spans") {
    Edge e;
    Clothoid c;
    c.heading0 = 0;
    c.k0 = 0;
    c.dk = 0;
    c.length = 200;
    e.horizontal.spans.push_back(c);
    e.vertical = VerticalProfile::build({0, 200}, {10, 14}, {});
    e.structure.push_back({61.3, 120.7, StructureKind::Tunnel});
    e.structure.push_back({150, 170, StructureKind::Bridge});
    const GradingParams p;
    const SampledRoad r = sample_edge_for_grading(e, "t", 5, false, 0, p);
    REQUIRE(r.samples.size() > 90);
    CHECK(r.samples.front().s == 0);
    CHECK(r.samples.back().s == doctest::Approx(200));
    double max_step = 0;
    for (std::size_t i = 1; i < r.samples.size(); ++i) {
        CHECK(r.samples[i].s > r.samples[i - 1].s);
        max_step = std::max(max_step, r.samples[i].s - r.samples[i - 1].s);
    }
    CHECK(max_step <= p.sample_step + 1e-9);
    for (const RoadSample& s : r.samples) {
        CHECK(s.x == doctest::Approx(s.s));
        CHECK(s.y == doctest::Approx(0).epsilon(1e-9));
        CHECK(s.z == doctest::Approx(10 + 0.02 * s.s));
        CHECK(s.br == (s.s >= 150 - 1e-6 && s.s <= 170 + 1e-6));
        CHECK(s.tu == (s.s >= 61.3 - 1e-6 && s.s <= 120.7 + 1e-6));
    }
    // stations sit exactly on the span boundaries, so portals are where the structure meshes put them
    bool a = false, b = false;
    for (const RoadSample& s : r.samples) { a = a || std::abs(s.s - 61.3) < 1e-9; b = b || std::abs(s.s - 120.7) < 1e-9; }
    CHECK(a);
    CHECK(b);
    // a bend: station spacing tightens so heading changes by at most about 0.05 rad per step
    Edge bend = e;
    bend.structure.clear();
    bend.horizontal.spans[0].k0 = 1.0 / 30;
    const SampledRoad rb = sample_edge_for_grading(bend, "t", 5, false, 0, p);
    for (std::size_t i = 1; i < rb.samples.size(); ++i) CHECK(std::abs(rb.samples[i].h - rb.samples[i - 1].h) <= 0.05 + 1e-9);
    CHECK(rb.samples.back().h == doctest::Approx(200.0 / 30));
}

TEST_CASE("grading adapter: graph roads grade with the region pack and honour authored structures") {
    auto pack = load_region_pack_by_id(CP_DATA_DIR, "uk");
    REQUIRE_MESSAGE(pack, pack.error);
    const GradingParams p = GradingParams::from_rules(pack->grading);
    CHECK(p.batter == doctest::Approx(2.0));
    CHECK(p.wall_start == doctest::Approx(12.0));
    CHECK(p.tunnel.clear_height == doctest::Approx(5.5));
    RoadGraph g;
    g.set_region(std::make_shared<RegionPack>(*pack));
    CommandStack<RoadGraph> st(g);
    DrawRoadSpec spec;
    spec.start.pos = {0, 0, 20};
    spec.end.pos = {240, 0, 20};
    spec.road_type = "b_road";
    REQUIRE(st.execute(make_draw_road(spec)));
    const EdgeId id = g.edges().begin()->first;
    REQUIRE(st.execute(make_set_structure_span(id, 100, 140, StructureKind::Bridge)));
    std::vector<SampledRoad> roads = sample_graph_for_grading(g, p);
    REQUIRE(roads.size() == 1);
    // half width = half of the lane cross-section: b_road one-way-less two-way = 2 x (3.25 + 0.15 + 2.0)
    CHECK(roads[0].half_width == doctest::Approx(5.4));
    CHECK(roads[0].type_id == "b_road");
    // gentle cross slope: left (y > 0) is up. Ground at the road is 20 + 0.1 y.
    auto ground = [](double, double y) { return 20 + 0.1 * y; };
    classify_road(roads[0], ground, p, StructureMode::Authored);
    int br = 0;
    for (const RoadSample& s : roads[0].samples) br += s.br;
    CHECK(br > 15);
    CHECK(roads[0].samples[10].wL == 0);
    // the road is ground-level: graded surface is the road's formation under the carriageway
    const std::vector<GradingRoad> prepared = prepare_grading_roads(roads, ground, p);
    REQUIRE(prepared.size() == 1);
    const TileSpec t{40, -20, 64, 64, 0.5};
    const GradedTile gt = grade_tile(prepared, p, t, sample_natural_tile(GroundFn(ground), t));
    const std::size_t o = static_cast<std::size_t>(32 * 64 + 20);  // (x = 50, y = -4)... inside the carriageway
    CHECK(gt.footprint[o] == 1);
    CHECK(gt.height[o] == doctest::Approx(20 - p.formation_drop).epsilon(1e-6));
    // under the bridge the ground is untouched (natural), well inside the span
    const TileSpec tb{110, -10, 40, 40, 0.5};
    const std::vector<float> nb = sample_natural_tile(GroundFn(ground), tb);
    const GradedTile gb = grade_tile(prepared, p, tb, nb);
    const std::size_t m = static_cast<std::size_t>(20 * 40 + 20);
    CHECK(gb.height[m] == doctest::Approx(nb[m]));
    // a graph without a region pack, or with unknown road types, yields no roads
    RoadGraph bare;
    CHECK(sample_graph_for_grading(bare, p).empty());
}

TEST_CASE("grading: region pack carries and validates the grading rules") {
    auto pack = load_region_pack_by_id(CP_DATA_DIR, "uk");
    REQUIRE_MESSAGE(pack, pack.error);
    const GradingRules d;
    CHECK(pack->grading.batter == doctest::Approx(d.batter));
    CHECK(pack->grading.verge == doctest::Approx(1.5));
    CHECK(pack->grading.wall_thickness == doctest::Approx(1.5));
    CHECK(pack->grading.wall_slack == doctest::Approx(0.75));
    // a pack that omits the object gets the fallbacks; a nonsense value is refused
    const std::string roads = R"({"road_types":[]})";
    auto plain = parse_region_pack(pack->lane_types, R"({"id":"t","driving_side":"left","speed_unit":"kph"})", roads);
    if (plain) CHECK(plain->grading.wall_cap == doctest::Approx(d.wall_cap));
    auto bad = parse_region_pack(pack->lane_types, R"({"id":"t","driving_side":"left","speed_unit":"kph","grading":{"batter":0}})", roads);
    CHECK_FALSE(bad);
}

// =============================================================================================
// Performance (informational): 256 x 256 texel tile, ~10 roads
// =============================================================================================

TEST_CASE("grading: performance of a 256 x 256 tile with 10 roads (informational)") {
    auto c = make_case("crossing_and_ends");
    // 10 roads: the golden roads plus parallel copies shifted across the tile
    std::vector<SampledRoad> roads;
    for (int k = 0; k < 5; ++k)
        for (const SampledRoad& r : c->roads) {
            SampledRoad q = r;
            for (RoadSample& s : q.samples) { s.x += 14.0 * k; s.y += 9.0 * k; }
            roads.push_back(std::move(q));
        }
    for (SampledRoad& r : roads) classify_road(r, c->ground, c->params, StructureMode::Auto);
    const std::vector<GradingRoad> prepared = prepare_grading_roads(roads, c->ground, c->params);
    CHECK(roads.size() == 10);
    const TileSpec t{20, -30, 256, 256, 0.5};
    const std::vector<float> nat = sample_natural_tile(c->ground, t);
    std::vector<float> out(nat.size());
    std::vector<std::uint8_t> fp(nat.size());
    grade_tile_into(prepared, c->params, t, nat.data(), out.data(), fp.data());  // warm up
    const int reps = 5;
    const auto t0 = std::chrono::steady_clock::now();
    for (int i = 0; i < reps; ++i) grade_tile_into(prepared, c->params, t, nat.data(), out.data(), fp.data());
    const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count() / reps;
    if (verbose()) std::fprintf(stderr, "[grading perf] 256x256 tile, 10 roads: %.2f ms\n", ms);
    CHECK(ms < 2000);  // generous: only guards against a pathological slowdown in Debug builds
    CHECK(std::count(fp.begin(), fp.end(), 1) > 1000);
}
