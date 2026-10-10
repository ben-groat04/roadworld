// The `interchange` fixture (sim_core/tools/scene_fixture.cpp) and its JSON export.
#include <algorithm>
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
    Fixture() {
        std::string err;
        scene = build_scene("interchange", CP_DATA_DIR, err);
        REQUIRE_MESSAGE(scene, err);
        js = scene_junctions(*scene);
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
    int merges = 0, diverges = 0, roundabouts = 0, four = 0, three = 0;
    for (const auto& [n, j] : f.js.junctions) {
        if (j.kind == JunctionKind::Roundabout) ++roundabouts;
        else if (j.cls == NodeClass::Merge) ++merges;
        else if (j.cls == NodeClass::Diverge) ++diverges;
        else if (j.arms.size() == 4) ++four;
        else if (j.arms.size() == 3) ++three;
    }
    CHECK(merges >= 2);
    CHECK(diverges >= 2);
    CHECK(roundabouts == 1);
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
        CHECK_MESSAGE(!j.carriageway.empty(), "node " << n.value);
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
