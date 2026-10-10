#pragma once
#include <memory>

#include "cp/command.h"
#include "cp/road_graph.h"

namespace cp {

using RoadCommand = Command<RoadGraph>;
using RoadCommandPtr = std::unique_ptr<RoadCommand>;

// Where one end of a drawn road attaches.
struct RoadEnd {
    Vec3 pos;                       // used when creating a new node
    NodeId existing_node;           // attach to this node, inheriting its height
    EdgeId split_edge;              // or split this edge at split_s
    double split_s = 0;
    std::optional<double> heading;  // pinned tangent at this end
};

struct DrawRoadSpec {
    RoadEnd start, end;
    std::vector<Vec3> interior;
    std::string road_type;
    bool one_way = false;
};

// One undo step: create or attach end nodes, split edges as needed, add the edge.
RoadCommandPtr make_draw_road(DrawRoadSpec spec);
RoadCommandPtr make_delete_edge(EdgeId id);
// index 0..interior.size()-1 moves an interior point; use make_move_node for ends.
RoadCommandPtr make_move_interior_point(EdgeId id, std::size_t index, Vec3 to);
RoadCommandPtr make_move_node(NodeId id, Vec3 to);

}  // namespace cp
