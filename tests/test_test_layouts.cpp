// cp::build_interchange_layout (sim_core/src/test_layouts.cpp) must match the tools fixture it copies.
#include <memory>
#include <string>

#include "cp/test_layouts.h"
#include "doctest/doctest.h"
#include "scene_fixture.h"

using namespace cp;

TEST_CASE("test_layouts: interchange matches the tools fixture") {
    std::string err;
    auto sc = tools::build_scene("interchange", CP_DATA_DIR, err);
    REQUIRE_MESSAGE(sc, err);

    auto pack = load_region_pack_by_id(CP_DATA_DIR, "uk");
    REQUIRE_MESSAGE(pack, pack.error);
    auto region = std::make_shared<const RegionPack>(*pack);
    RoadGraph graph;
    graph.set_region(region);
    CommandStack<RoadGraph> stack(graph);
    REQUIRE_MESSAGE(build_test_layout(TestLayout::Interchange, graph, stack, *region, err), err);

    CHECK(graph.nodes().size() == sc->graph.nodes().size());
    CHECK(graph.edges().size() == sc->graph.edges().size());
    CHECK(graph.nodes().size() > 10);

    JunctionOverrides ov;
    JunctionSet js = build_plain_junctions(graph, *region, ov);
    RoundaboutResult rr = apply_roundabouts(js, graph, *region, ov);
    RoundaboutResult ref_rr;
    JunctionSet ref = tools::scene_junctions(*sc, &ref_rr);
    CHECK(js.junctions.size() == ref.junctions.size());
    CHECK(rr.layouts.size() == 1);
    CHECK(rr.problems.empty());
    CHECK(rr.layouts.size() == ref_rr.layouts.size());

    int merges = 0, diverges = 0, ring = 0, ref_merges = 0, ref_diverges = 0, ref_ring = 0;
    for (const auto& [n, j] : js.junctions) {
        if (j.kind == JunctionKind::Roundabout) ++ring;
        else if (j.cls == NodeClass::Merge) ++merges;
        else if (j.cls == NodeClass::Diverge) ++diverges;
    }
    for (const auto& [n, j] : ref.junctions) {
        if (j.kind == JunctionKind::Roundabout) ++ref_ring;
        else if (j.cls == NodeClass::Merge) ++ref_merges;
        else if (j.cls == NodeClass::Diverge) ++ref_diverges;
    }
    CHECK(merges == ref_merges);
    CHECK(diverges == ref_diverges);
    CHECK(ring == ref_ring);
    CHECK(merges >= 2);
    CHECK(diverges >= 2);
    CHECK(ring >= 1);
}
