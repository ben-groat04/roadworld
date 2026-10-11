#pragma once
// Spatial queries over a RoadGraph for pointer tools: nearest point on any edge centreline, nearest node,
// nearest road end. A snapshot: build it from a graph, rebuild after the graph changes (it copies what it
// needs, so it never dangles). Uniform grid over 2 m centreline chords; correctness first.
// Port of roadEnds() / nearestOnRoad() in prototypes/road-draw/src/input.js.
#include <cstdint>
#include <optional>
#include <unordered_map>
#include <vector>

#include "cp/road_graph.h"

namespace cp {

struct RoadHit {
    EdgeId edge;
    double s = 0;         // arc length along the edge
    Vec3 point;           // exact centreline point at s (z from the vertical profile)
    double heading = 0;   // centreline heading at s, rad CCW from +x
    double distance = 0;  // plan distance from the query point
    double lateral = 0;   // signed: + when the query point is left of the direction a -> b (JS: sd)
};

struct NodeHit {
    NodeId node;
    Vec3 pos;
    double distance = 0;
    std::size_t degree = 0;  // edges at the node
};

// A node with exactly one edge. out_heading points away from the road (the direction a continuation takes).
struct EndHit {
    NodeId node;
    EdgeId edge;
    bool at_start = false;  // the node is the edge's `a`
    Vec3 pos;
    double out_heading = 0;
    double distance = 0;
};

class RoadIndex {
public:
    RoadIndex() = default;
    // chord_step: max centreline chord length (m); cell: grid cell size (m).
    explicit RoadIndex(const RoadGraph& g, double chord_step = 2.0, double cell = 50.0);

    bool empty() const { return edges_.empty() && nodes_.empty(); }

    // Closest point on any edge centreline within `radius` (plan). When `level` is set, chords whose start
    // is more than `level_tol` away in height are ignored (JS: |a.z - hitZ| > 15). `skip` excludes one edge.
    std::optional<RoadHit> nearest_point(Vec2 p, double radius, std::optional<double> level = {}, double level_tol = 15,
                                         EdgeId skip = {}) const;
    // Closest node of any degree.
    std::optional<NodeHit> nearest_node(Vec2 p, double radius, std::optional<double> level = {}, double level_tol = 15) const;
    // Closest degree-1 node.
    std::optional<EndHit> nearest_free_end(Vec2 p, double radius, std::optional<double> level = {}, double level_tol = 15) const;

    // Every degree-1 node (the road ends the tool offers to continue).
    const std::vector<EndHit>& free_ends() const { return ends_; }
    // The copied edge (null when absent): lets callers draw guide lines without holding the graph.
    const Edge* edge(EdgeId id) const;
    std::size_t edge_count() const { return edges_.size(); }

private:
    struct Chord {
        std::uint32_t edge;  // index into edges_
        std::uint32_t seg;   // chord index within the edge's samples
    };
    struct Sample {
        double s, x, y, z;
    };
    struct EdgeData {
        Edge e;
        std::vector<Sample> samples;
    };
    using CellKey = std::int64_t;
    static CellKey key(std::int32_t ix, std::int32_t iy) {
        return (static_cast<std::int64_t>(ix) << 32) ^ static_cast<std::int64_t>(static_cast<std::uint32_t>(iy));
    }
    std::int32_t cell_of(double v) const;

    double cell_ = 50;
    std::vector<EdgeData> edges_;
    std::unordered_map<EdgeId, std::uint32_t> edge_slot_;
    std::unordered_map<CellKey, std::vector<Chord>> chord_cells_;
    std::vector<NodeHit> nodes_;
    std::vector<EndHit> ends_;
};

}  // namespace cp
