#pragma once
// Authored road graph (docs/road-graph-model.md): nodes and edges only.
// Derived data (lanes, junction geometry, markings) is built elsewhere from this.
// std::map keeps iteration order deterministic.
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "cp/clothoid_chain.h"
#include "cp/ids.h"
#include "cp/vec.h"
#include "cp/vertical_profile.h"

namespace cp {

struct NodeTag;
struct EdgeTag;
using NodeId = Id<NodeTag>;
using EdgeId = Id<EdgeTag>;

struct Node {
    NodeId id;
    Vec3 pos;
};

struct Edge {
    EdgeId id;
    NodeId a, b;
    std::vector<Vec3> interior;          // clicked points between the end nodes
    std::optional<double> heading_a;     // pinned tangents (continuation, snapping)
    std::optional<double> heading_b;
    std::string road_type;
    bool one_way = false;                // true: travel a -> b only

    // Fitted geometry, stored so it stays stable; refit only on edit.
    ClothoidChain horizontal;
    VerticalProfile vertical;
};

class RoadGraph {
public:
    VerticalCurveParams curve_params;  // TODO: per road type from the region pack

    const Node* node(NodeId id) const;
    const Edge* edge(EdgeId id) const;
    const std::map<NodeId, Node>& nodes() const { return nodes_; }
    const std::map<EdgeId, Edge>& edges() const { return edges_; }
    std::vector<EdgeId> edges_at(NodeId n) const;

    // Primitive mutations, used only by commands. Insert functions keep the
    // object's existing ID when it is set (undo/redo), else allocate a new one.
    NodeId insert_node(Node n);
    void erase_node(NodeId id);
    EdgeId insert_edge(Edge e);  // fits geometry; fails (returns null) if unfittable
    void erase_edge(EdgeId id);
    void move_node(NodeId id, Vec3 pos);  // refits attached edges

    // Refit horizontal + vertical geometry from control points.
    bool refit(Edge& e) const;

    const std::set<NodeId>& dirty_nodes() const { return dirty_nodes_; }
    void clear_dirty() { dirty_nodes_.clear(); }

private:
    std::map<NodeId, Node> nodes_;
    std::map<EdgeId, Edge> edges_;
    std::map<NodeId, std::set<EdgeId>> adjacency_;
    IdAllocator<NodeTag> node_ids_;
    IdAllocator<EdgeTag> edge_ids_;
    std::set<NodeId> dirty_nodes_;
};

// Sample the edge's 3D centreline at arc length s.
Vec3 edge_point(const Edge& e, double s);

}  // namespace cp
