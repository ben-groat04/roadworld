// Isolated so the dependency on the roundabout work (junction_roundabout.cpp) is in one place.
#include "scene_fixture.h"

namespace cp::tools {

bool place_interchange_roundabout(Scene& sc, NodeId node, double radius, std::string& error) {
    (void)sc; (void)node; (void)radius;
    error = "make_place_roundabout not available yet";
    return false;
}

}  // namespace cp::tools
