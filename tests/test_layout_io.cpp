// cp::layout_io: the JSON test-layout format (sim_core/include/cp/layout_io.h).
#include <memory>
#include <string>

#include "cp/junction.h"
#include "cp/junction_roundabout.h"
#include "cp/layout_io.h"
#include "doctest/doctest.h"

using namespace cp;

namespace {

std::unique_ptr<LoadedLayout> load(const std::string& json, std::string& err) {
    return load_layout(json, CP_DATA_DIR, err);
}

std::string error_of(const std::string& json) {
    std::string err;
    auto l = load(json, err);
    CHECK(!l);
    CHECK(!err.empty());
    return err;
}

bool contains(const std::string& s, const std::string& part) { return s.find(part) != std::string::npos; }

const char* kTee = R"({
  "format": "cp-test-layout", "version": 1, "name": "tee", "region": "uk", "driving_side": "left",
  "roads": [
    { "type": "b_road", "points": [[-100, 0, 0], [0, 0, 0], [100, 0, 0]] },
    { "type": "residential", "points": [[0, 0, 0], [0, 40, 0], [0, 80, 0]] }
  ],
  "cameras": [ { "name": "c1", "pos": [0, 60, 2.5], "target": [0, -10, 0.5], "fov_deg": 70, "aspect": 2.3, "photo": "p.png" } ]
})";

}  // namespace

TEST_CASE("layout_io: a T-junction built from JSON has the expected graph") {
    std::string err;
    auto l = load(kTee, err);
    REQUIRE_MESSAGE(l, err);
    CHECK(l->graph.nodes().size() == 4);  // two main ends, the junction, the side road end
    CHECK(l->graph.edges().size() == 3);  // main split in two + side road
    CHECK(l->stack.undo_depth() == 2);    // one draw-road command per road
    JunctionSet js = build_plain_junctions(l->graph, *l->region, l->overrides);
    CHECK(js.junctions.size() == 1);
    REQUIRE(l->doc.cameras.size() == 1);
    CHECK(l->doc.cameras[0].name == "c1");
    CHECK(l->doc.cameras[0].fov_deg == doctest::Approx(70));
    CHECK(l->doc.cameras[0].photo == "p.png");
}

TEST_CASE("layout_io: a road crossing another mid-way is split into a crossroads") {
    std::string err;
    auto l = load(R"({ "roads": [
        { "type": "b_road", "points": [[-100, 0], [100, 0]] },
        { "type": "b_road", "points": [[0, -80], [0, -20], [0, 20], [0, 80]] } ] })", err);
    REQUIRE_MESSAGE(l, err);
    CHECK(l->graph.nodes().size() == 5);
    CHECK(l->graph.edges().size() == 4);
    JunctionSet js = build_plain_junctions(l->graph, *l->region, l->overrides);
    CHECK(js.junctions.size() == 1);
    CHECK(js.junctions.begin()->second.arms.size() == 4);
}

TEST_CASE("layout_io: a road at a different level does not split") {
    std::string err;
    auto l = load(R"({ "roads": [
        { "type": "b_road", "points": [[-100, 0, 0], [100, 0, 0]] },
        { "type": "b_road", "points": [[0, -80, 8], [0, -20, 8], [0, 20, 8], [0, 80, 8]] } ] })", err);
    REQUIRE_MESSAGE(l, err);
    CHECK(l->graph.nodes().size() == 4);
    CHECK(l->graph.edges().size() == 2);
}

TEST_CASE("layout_io: a roundabout placed through the place command") {
    std::string err;
    auto l = load(R"({ "roads": [
        { "type": "b_road", "points": [[-120, 0], [120, 0]] },
        { "type": "b_road", "points": [[0, -120], [0, 120]] } ],
      "roundabouts": [ { "centre": [0, 0, 0], "radius": 20 } ] })", err);
    REQUIRE_MESSAGE(l, err);
    JunctionSet js = build_plain_junctions(l->graph, *l->region, l->overrides);
    RoundaboutResult rr = apply_roundabouts(js, l->graph, *l->region, l->overrides);
    CHECK(rr.problems.empty());
    REQUIRE(rr.layouts.size() == 1);
    CHECK(rr.layouts[0].ok);
    CHECK(rr.layouts[0].group.arms.size() == 4);
    // Four approach stubs and the ring edges; a ring node per arm at least.
    CHECK(rr.layouts[0].group.nodes.size() >= 4);
    int ring = 0;
    for (const auto& [n, j] : js.junctions)
        if (j.kind == JunctionKind::Roundabout) ++ring;
    CHECK(ring == static_cast<int>(rr.layouts[0].group.nodes.size()));
    CHECK(l->graph.edges().size() == 4 + rr.layouts[0].group.ring_edges.size());
}

TEST_CASE("layout_io: structures, corner radii and crossings") {
    std::string err;
    auto l = load(R"({ "roads": [
        { "type": "b_road", "points": [[-100, 0], [100, 0]],
          "structures": [ { "kind": "bridge", "at": [-60, 0], "length": 20 } ] },
        { "type": "residential", "points": [[0, 0], [0, 80]] } ],
      "junctions": { "corner_radii": [ { "node": [0, 0], "arm_a": [0, 30], "arm_b": [30, 0], "radius": 14 },
                                       { "node": [0, 0], "arm_a": [0, 30], "arm_b": [-30, 0], "radius": 3 } ],
                     "crossings": [ { "node": [0, 0], "arm": [0, 30] } ] } })", err);
    REQUIRE_MESSAGE(l, err);
    bool bridge = false;
    for (const auto& [id, e] : l->graph.edges())
        for (const auto& s : e.structure) bridge = bridge || s.kind == StructureKind::Bridge;
    CHECK(bridge);
    CHECK(l->overrides.corner_radius.size() == 2);
    CHECK(l->crossing_arms.size() == 1);
    // The overrides take effect in the junction build.
    JunctionSet js = build_plain_junctions(l->graph, *l->region, l->overrides);
    REQUIRE(js.junctions.size() == 1);
    bool has14 = false, has3 = false;
    for (double r : js.junctions.begin()->second.corner_radii) {
        has14 = has14 || r == doctest::Approx(14);
        has3 = has3 || r == doctest::Approx(3);
    }
    CHECK(has14);
    CHECK(has3);
}

TEST_CASE("layout_io: round trip") {
    LayoutDoc a;
    std::string err;
    REQUIRE_MESSAGE(parse_layout(R"({ "name": "rt", "roads": [
        { "type": "b_road", "one_way": true,
          "points": [ {"p": [0, 0, 1], "heading_deg": 90}, [10, 20, 1.5], {"p": [30, 50, 2], "heading_deg": -45.5} ],
          "structures": [ {"kind": "tunnel", "at": [10, 20], "length": 12} ] } ],
      "roundabouts": [ {"centre": [5, 5, 0], "radius": 22.5, "type": "roundabout_ring"} ],
      "junctions": { "corner_radii": [ {"node": [0, 0], "arm_a": [1, 1], "arm_b": [2, 2], "radius": 9} ],
                     "crossings": [ {"node": [0, 0], "arm": [1, 1]} ] },
      "cameras": [ {"name": "n", "pos": [1, 2, 2.5], "target": [3, 4, 0], "fov_deg": 66.5, "aspect": 2.2, "photo": "x.png", "notes": "hi"} ] })",
                    a, err),
                    err);
    const std::string text = layout_to_json(a);
    LayoutDoc b;
    REQUIRE_MESSAGE(parse_layout(text, b, err), err);
    CHECK(layout_to_json(b) == text);
    CHECK(b.name == "rt");
    REQUIRE(b.roads.size() == 1);
    CHECK(b.roads[0].one_way);
    REQUIRE(b.roads[0].points.size() == 3);
    CHECK(b.roads[0].points[0].heading.has_value());
    CHECK(b.roads[0].points[2].heading.value() == doctest::Approx(-45.5 * 3.14159265358979323846 / 180));
    CHECK(b.roads[0].structures[0].kind == StructureKind::Tunnel);
    CHECK(b.roundabouts[0].radius == doctest::Approx(22.5));
    CHECK(b.cameras[0].notes == "hi");
}

TEST_CASE("layout_io: errors name the offending item") {
    CHECK(contains(error_of("not json"), "not valid JSON"));
    CHECK(contains(error_of(R"({ "roads": [ { "type": "b_road", "points": [[0,0],[10,0]] },
                                             { "type": "no_such_type", "points": [[0,5],[10,5]] } ] })"),
                   "roads[1]: unknown road type"));
    CHECK(contains(error_of(R"({ "roads": [ { "type": "b_road", "points": [[0,0]] } ] })"), "roads[0]"));
    CHECK(contains(error_of(R"({ "roads": [ { "type": "b_road", "points": [[0,0],[10,0],[10,0]] } ] })"), "coincide"));
    CHECK(contains(error_of(R"({ "roads": [ { "type": "b_road", "points": [[0,0],[10,0],[10,10],[20,20]] },
                                             { "type": "b_road", "points": [[0,0],{"p":[5,0],"heading_deg":3},[9,9]] } ] })"),
                   "roads[1].points[1]"));
    CHECK(contains(error_of(R"({ "roads": [ { "type": "b_road", "points": [[0,0],["a",0]] } ] })"), "roads[0].points[1]"));
    CHECK(contains(error_of(R"({ "roads": [ { "type": "b_road", "colour": 1, "points": [[0,0],[1,1]] } ] })"), "unknown key"));
    CHECK(contains(error_of(R"({ "driving_side": "right", "roads": [ { "type": "b_road", "points": [[0,0],[10,0]] } ] })"),
                   "driving_side"));
    CHECK(contains(error_of(R"({ "region": "mars", "roads": [ { "type": "b_road", "points": [[0,0],[10,0]] } ] })"), "mars"));
    CHECK(contains(error_of(R"({ "roads": [ { "type": "b_road", "points": [[0,0],[50,0]],
                                              "structures": [ {"kind": "bridge", "at": [900, 900], "length": 10} ] } ] })"),
                   "roads[0].structures[0]"));
    CHECK(contains(error_of(R"({ "roads": [ { "type": "b_road", "points": [[0,0],[50,0]] } ],
                                 "roundabouts": [ {"centre": [0,0,0], "radius": 0.5} ] })"),
                   "roundabouts[0]"));
    CHECK(contains(error_of(R"({ "roads": [ { "type": "b_road", "points": [[0,0],[50,0]] } ],
                                 "cameras": [ {"name": "c", "pos": [0,0,1], "target": [0,0,1]} ] })"),
                   "cameras[0]"));
    CHECK(contains(error_of(R"({ "roads": [ { "type": "b_road", "points": [[0,0],[50,0]] } ],
                                 "junctions": { "crossings": [ {"node": [500, 500], "arm": [0, 0]} ] } })"),
                   "junctions.crossings[0]"));
}
