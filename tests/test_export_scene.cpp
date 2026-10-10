// The `interchange` fixture (sim_core/tools/scene_fixture.cpp) and its JSON export.
#include <algorithm>
#include <clocale>
#include <cmath>
#include <memory>
#include <string>

#include "doctest/doctest.h"
#include "scene_fixture.h"

using namespace cp;
using namespace cp::tools;

namespace {

struct Fixture {
    std::unique_ptr<Scene> scene;
    JunctionSet js;
    RoundaboutResult rb;
    Fixture() {
        std::string err;
        scene = build_scene("interchange", CP_DATA_DIR, err);
        REQUIRE_MESSAGE(scene, err);
        js = scene_junctions(*scene, &rb);
    }
};

const Fixture& fx() {
    static const Fixture f;
    return f;
}

bool segs_cross(Vec2 a, Vec2 b, Vec2 c, Vec2 d) {
    const auto o = [](Vec2 p, Vec2 q, Vec2 r) { return (q - p).cross(r - p); };
    const double d1 = o(a, b, c), d2 = o(a, b, d), d3 = o(c, d, a), d4 = o(c, d, b);
    return ((d1 > 1e-9 && d2 < -1e-9) || (d1 < -1e-9 && d2 > 1e-9)) &&
           ((d3 > 1e-9 && d4 < -1e-9) || (d3 < -1e-9 && d4 > 1e-9));
}

// Finite, >= 3 points, non-zero area, no self-intersection.
bool ring_valid(const std::vector<Vec2>& r) {
    if (r.size() < 3) return false;
    double a = 0;
    for (std::size_t i = 0; i < r.size(); ++i) {
        if (!std::isfinite(r[i].x) || !std::isfinite(r[i].y)) return false;
        a += r[i].cross(r[(i + 1) % r.size()]);
    }
    if (std::abs(a) < 1e-3) return false;
    const std::size_t n = r.size();
    for (std::size_t i = 0; i < n; ++i)
        for (std::size_t k = i + 2; k < n; ++k) {
            if (i == 0 && k == n - 1) continue;
            if (segs_cross(r[i], r[(i + 1) % n], r[k], r[(k + 1) % n])) return false;
        }
    return true;
}

}  // namespace

TEST_CASE("interchange fixture builds with the expected junction kinds") {
    const Fixture& f = fx();
    int merges = 0, diverges = 0, four = 0, three = 0;
    for (const auto& [n, j] : f.js.junctions) {
        if (j.kind == JunctionKind::Roundabout) continue;
        else if (j.cls == NodeClass::Merge) ++merges;
        else if (j.cls == NodeClass::Diverge) ++diverges;
        else if (j.arms.size() == 4) ++four;
        else if (j.arms.size() == 3) ++three;
    }
    CHECK(merges >= 2);
    CHECK(diverges >= 2);
    CHECK(f.rb.layouts.size() == 1);  // one roundabout GROUP, however many ring nodes it has
    CHECK(f.rb.problems.empty());
    REQUIRE(f.rb.layouts.size() == 1);
    const RoundaboutLayout& lay = f.rb.layouts[0];
    CHECK(lay.ok);
    CHECK(lay.island.size() == 1);
    CHECK(!lay.give_way.empty());
    // Every ring node has a Roundabout-kind junction of the same group.
    for (NodeId n : lay.group.nodes) {
        const Junction* j = f.js.find(n);
        REQUIRE(j);
        CHECK(j->kind == JunctionKind::Roundabout);
        CHECK(j->group_nodes == lay.group.nodes);
        CHECK(!j->degraded);
    }
    int ring_kind = 0;
    for (const auto& [n, j] : f.js.junctions) ring_kind += j.kind == JunctionKind::Roundabout ? 1 : 0;
    CHECK(ring_kind == static_cast<int>(lay.group.nodes.size()));  // no stray roundabout junctions
    CHECK(four >= 1);
    CHECK(three >= 1);
    CHECK(f.scene->region->driving_side == DrivingSide::Left);
}

TEST_CASE("interchange: every inbound lane has an enabled connector") {
    for (const auto& [n, j] : fx().js.junctions)
        for (std::size_t a = 0; a < j.arms.size(); ++a)
            for (std::size_t l = 0; l < j.arms[a].lanes.size(); ++l) {
                if (!j.arms[a].lanes[l].inbound) continue;
                const bool ok = std::any_of(j.connectors.begin(), j.connectors.end(), [&](const LaneConnector& c) {
                    return c.enabled && c.arm_from == static_cast<int>(a) && c.lane_from == static_cast<int>(l);
                });
                CHECK_MESSAGE(ok, "node " << n.value << " arm " << a << " lane " << l);
            }
}

TEST_CASE("interchange: junction polygons are valid") {
    for (const auto& [n, j] : fx().js.junctions) {
        if (j.cls != NodeClass::Joint) CHECK_MESSAGE(!j.carriageway.empty(), "node " << n.value);
        for (const PolygonSet* ps : {&j.carriageway, &j.pavements, &j.gore})
            for (const auto& ring : *ps) CHECK_MESSAGE(ring_valid(ring), "node " << n.value);
        for (const LaneConnector& c : j.connectors) CHECK(c.path.size() >= 2);
    }
}

TEST_CASE("interchange: bridge clearance over both carriageways") {
    const Fixture& f = fx();
    const RoadGraph& g = f.scene->graph;
    // Deck: lowest bridge sample of the minor road within 12 m of each carriageway crossing.
    for (const double cy : {InterchangeDims::dual_offset, -InterchangeDims::dual_offset}) {
        double deck = -1e9, road = 1e9;
        bool on_bridge = false;
        for (const auto& [id, e] : g.edges()) {
            const double L = e.horizontal.length();
            for (double s = 0; s <= L; s += 0.5) {
                const Vec3 p = edge_point(e, s);
                if (std::abs(p.x) > 0.6 || std::abs(p.y - cy) > 0.6) continue;
                if (e.road_type == "a_road_dual") road = std::min(road, p.z);
                else if (e.road_type == "b_road" && !e.one_way) {
                    for (const StructureSpan& sp : e.structure)
                        if (sp.kind == StructureKind::Bridge && s >= sp.s0 && s <= sp.s1) on_bridge = true;
                    deck = std::max(deck, p.z);
                }
            }
        }
        REQUIRE(on_bridge);
        const double clearance = deck - InterchangeDims::deck_depth - road;
        CHECK(clearance >= f.scene->region->headroom_m);
        CHECK(deck - road == doctest::Approx(InterchangeDims::deck_z).epsilon(0.05));
    }
}

TEST_CASE("interchange: JSON export has the v1 shape") {
    const Fixture& f = fx();
    const std::string j = export_scene_json(*f.scene, f.js);
    CHECK(j.rfind("{\"version\":1,\"region\":\"uk\",\"drivingSide\":\"left\"", 0) == 0);
    for (const char* key : {"\"roads\"", "\"junctions\"", "\"carriageway\"", "\"pavements\"", "\"islands\"", "\"gore\"",
                            "\"connectors\"", "\"trims\"", "\"structure\":\"bridge\"", "\"kind\":\"roundabout\"",
                            "\"kind\":\"merge\"", "\"kind\":\"diverge\""})
        CHECK_MESSAGE(j.find(key) != std::string::npos, key);
}

TEST_CASE("interchange: JSON has the roundabout group, give-way and no export errors") {
    const Fixture& f = fx();
    int errors = -1;
    const std::string j = export_scene_json(*f.scene, f.js, &errors);
    CHECK(errors == 0);
    CHECK(j.find("\"roundabouts\":[{\"nodes\":[") != std::string::npos);
    for (const char* key : {"\"giveWay\":[{", "\"yieldsTo\"", "\"island\":[[[", "\"splitters\"", "\"centre\"", "\"roundaboutProblems\":[]",
                            "\"exportErrors\":0", "\"ok\":true"})
        CHECK_MESSAGE(j.find(key) != std::string::npos, key);
    CHECK(j.find("nan") == std::string::npos);
    CHECK(j.find("inf") == std::string::npos);
}

TEST_CASE("JSON helpers: non-finite numbers, escapes and locale independence") {
    bool finite = true;
    CHECK(json_number(std::nan(""), 3, &finite) == "null");
    CHECK_FALSE(finite);
    finite = true;
    CHECK(json_number(HUGE_VAL, 3, &finite) == "null");
    CHECK_FALSE(finite);
    finite = true;
    CHECK(json_number(-HUGE_VAL, 3, &finite) == "null");
    CHECK_FALSE(finite);
    finite = true;
    CHECK(json_number(1.5, 3, &finite) == "1.500");
    CHECK(finite);
    CHECK(json_number(-0.0, 3) == "0.000");
    CHECK(json_number(-0.0001, 3) == "0.000");
    CHECK(json_number(-2.5, 1) == "-2.5");
    CHECK(json_number(0.12345678, 5) == "0.12346");

    CHECK(json_string("a\"b\\c") == "\"a\\\"b\\\\c\"");
    CHECK(json_string("l1\nl2\t\r") == "\"l1\\nl2\\t\\r\"");
    CHECK(json_string(std::string("x\x01y\x1f")) == "\"x\\u0001y\\u001f\"");
    CHECK(json_string("plain") == "\"plain\"");

    // A comma-decimal C locale must not leak into the output (skipped if no such locale is installed).
    const std::string old = std::setlocale(LC_NUMERIC, nullptr);
    for (const char* name : {"de_DE.UTF-8", "de_DE", "German_Germany.1252", "fr_FR.UTF-8", "fr_FR"}) {
        if (!std::setlocale(LC_NUMERIC, name)) continue;
        CHECK(json_number(1.5, 3) == "1.500");
        break;
    }
    std::setlocale(LC_NUMERIC, old.c_str());
}
