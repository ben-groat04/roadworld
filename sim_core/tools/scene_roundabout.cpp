// Isolated so the dependency on the roundabout work (junction_roundabout.cpp) is in one place.
#include "cp/junction_roundabout.h"
#include "scene_fixture.h"

namespace cp::tools {

bool place_interchange_roundabout(Scene& sc, NodeId node, double radius, std::string& error) {
    const Node* n = sc.graph.node(node);
    if (!n) {
        error = "roundabout node missing";
        return false;
    }
    if (!sc.stack.execute(make_place_roundabout(n->pos, radius, default_ring_road_type(*sc.region), *sc.region))) {
        error = "place_roundabout failed: " + check_place_roundabout(sc.graph, n->pos, radius, default_ring_road_type(*sc.region), *sc.region);
        return false;
    }
    error.clear();
    return true;
}

}  // namespace cp::tools
