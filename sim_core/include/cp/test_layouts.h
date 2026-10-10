#pragma once
// Built-in test layouts, built purely through commands (CommandStack), for engine-side look tests.
// The `interchange` layout is an independent copy of the construction in sim_core/tools/scene_fixture.cpp
// (the tools fixture is the original and is owned by the tooling; keep both in step by hand: the node/edge
// counts and junction kinds are checked against the fixture in tests/test_test_layouts.cpp).
#include <string>

#include "cp/command.h"
#include "cp/region_pack.h"
#include "cp/road_commands.h"
#include "cp/road_graph.h"

namespace cp {

// Geometry of the `interchange` layout (metres, x east, y north; dual carriageway along the x axis).
struct InterchangeLayoutDims {
    static constexpr double dual_offset = 5.5;   // each carriageway centre is +-5.5 m from y = 0
    static constexpr double deck_z = 7.0;        // bridge deck level over the dual
    static constexpr double rb_y = 80.0;         // north roundabout centre on the minor road
    static constexpr double rb_radius = 25.0;
    static constexpr double tee_y = 230.0;       // T-junction (150 m north of the roundabout)
    static constexpr double slip_x = 260.0;      // diverge/merge nodes at x = +-260
};

enum class TestLayout { Interchange };
const char* test_layout_name(TestLayout layout);

// Draws the layout into `graph` through `stack` (which must be bound to `graph`; the region pack must already
// be set on the graph with RoadGraph::set_region). Returns false and fills `error` on failure.
bool build_test_layout(TestLayout layout, RoadGraph& graph, CommandStack<RoadGraph>& stack, const RegionPack& region,
                       std::string& error);

// East-west A-road dual carriageway, a b_road minor road crossing it on a bridge, four slip roads (two diverges,
// two merges), a roundabout at (0, 80) and a residential T-junction to the east at y = 230.
bool build_interchange_layout(RoadGraph& graph, CommandStack<RoadGraph>& stack, const RegionPack& region,
                              std::string& error);

}  // namespace cp
