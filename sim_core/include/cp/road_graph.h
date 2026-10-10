#pragma once
// Authored road graph (docs/road-graph-model.md): nodes and edges only.
// Derived data (lanes, junction geometry, markings) is built elsewhere from this.
// std::map keeps iteration order deterministic.
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "cp/clothoid_chain.h"
#include "cp/ids.h"
#include "cp/region_pack.h"
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

enum class StructureKind { Ground, Bridge, Tunnel };

// Arc-length range [s0, s1] of an edge on a structure. Spans are sorted, non-overlapping;
// stretches not covered are ground.
struct StructureSpan {
    double s0 = 0, s1 = 0;
    StructureKind kind = StructureKind::Bridge;
    friend bool operator==(const StructureSpan&, const StructureSpan&) = default;
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

    std::vector<StructureSpan> structure;  // bridge/tunnel ranges along s
};

class RoadGraph {
public:
    // Fallback vertical curve parameters, used when no region pack is set or the edge's
    // road type is not in it. With a pack, K crest/sag come from the edge's road type.
    VerticalCurveParams curve_params;

    // Sets the region pack and refits every edge so vertical curves pick up its K values.
    void set_region(std::shared_ptr<const RegionPack> region);
    const RegionPack* region() const { return region_.get(); }
    VerticalCurveParams curve_params_for(const Edge& e) const;

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

    // Everything changed since the last clear_dirty(), including REMOVED objects: a removed node or edge
    // stays in the set (node(id) / edge(id) return null) so incremental consumers can clear what they
    // built for it. Nodes: inserted, moved, removed, or an end of any inserted / erased / refitted edge.
    const std::set<NodeId>& dirty_nodes() const { return dirty_nodes_; }
    // Edges inserted, erased (removed ids stay) or refitted. Consumers should still also treat the edges
    // at dirty nodes as dirty.
    const std::set<EdgeId>& dirty_edges() const { return dirty_edges_; }
    void clear_dirty() {
        dirty_nodes_.clear();
        dirty_edges_.clear();
    }

private:
    std::shared_ptr<const RegionPack> region_;
    std::map<NodeId, Node> nodes_;
    std::map<EdgeId, Edge> edges_;
    std::map<NodeId, std::set<EdgeId>> adjacency_;
    IdAllocator<NodeTag> node_ids_;
    IdAllocator<EdgeTag> edge_ids_;
    std::set<NodeId> dirty_nodes_;
    std::set<EdgeId> dirty_edges_;
};

// Sample the edge's 3D centreline at arc length s.
Vec3 edge_point(const Edge& e, double s);

}  // namespace cp
