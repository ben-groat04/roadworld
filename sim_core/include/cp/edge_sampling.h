#pragma once
// Adaptive sampling of an edge's 3D centreline and of offset polylines (lane edges,
// kerbs). Offsets of clothoids are not clothoids, so offset curves are sampled from
// the centreline's geometry, with the step refined near folds.
#include <vector>

#include "cp/road_graph.h"
#include "cp/vec.h"

namespace cp {

struct SamplingParams {
    double max_chord_error = 0.01;    // m, max distance between the curve and a chord
    double max_heading_change = 0.05; // rad per step (about 2.9 degrees)
    double max_step = 25.0;           // m of centreline arc length
};

struct EdgeSample {
    double s = 0;       // centreline arc length
    Vec3 pos;           // sampled point (on the offset curve for offset polylines)
    double heading = 0; // centreline heading, rad CCW from +x
    double curvature = 0;  // of the sampled curve, 1/m, + = turning left
    double grade = 0;      // centreline dz/ds
    bool folded = false;   // offset polylines: inside a fold (offset * curvature >= 1)
};

// Centreline arc-length range where an offset is folded.
struct FoldRange {
    double s0 = 0, s1 = 0;
};

struct OffsetPolyline {
    double offset = 0;  // + = left of a -> b
    std::vector<EdgeSample> samples;
    std::vector<FoldRange> folds;  // non-empty means the offset "breaks graphically"
    bool has_fold() const { return !folds.empty(); }
    double length() const;  // polyline length in plan (xy)
};

// Centreline samples from s = 0 to the edge length (both ends included).
std::vector<EdgeSample> sample_centreline(const Edge& e, const SamplingParams& p = {});

// Heading of the edge's centreline at arc length s (rad CCW from +x; s outside the edge clamps to the
// first/last span; 0 for an edge with no spans).
double edge_heading(const Edge& e, double s);

// Exact ranges (per clothoid span, then merged) where offset * curvature >= 1.
std::vector<FoldRange> find_folds(const Edge& e, double offset);

// Polyline of the curve at lateral `offset` (+ = left). Samples carry the centreline s,
// heading and grade, and the curvature of the offset curve k / (1 - offset * k).
OffsetPolyline sample_offset(const Edge& e, double offset, const SamplingParams& p = {});

}  // namespace cp
