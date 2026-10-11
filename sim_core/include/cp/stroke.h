#pragma once
// A drawn stroke: one road drawn through clicked points, resolved against the existing graph. This is the
// single place that decides how a new road meets the roads it touches, shared by the road tool
// (cp/road_tool.h) and by test-layout building (layout_io.cpp), so a drawn road and a layout road behave alike:
//   - an end within snap_tol (1 m) of an existing node or road centreline attaches to it (splitting the road);
//   - a stretch that crosses an existing road in plan at the same level (height difference below the join
//     tolerance, region plan_crossing_junction_dz_m) is split there and joined with a junction node;
//   - a stretch that passes through an existing node at the same level is split there;
//   - any other crossing passes over or under (a bridge or tunnel is the grading code's business).
// Optionally (StrokeParams::refuse_band, the road tool turns it on) a crossing whose height difference lies in
// [join tolerance, headroom) is refused: too close to join, too low to clear. Port of conformRoad / planHits
// in prototypes/road-draw/src/input.js; the thresholds come from the region pack (plan_crossing_junction_dz_m,
// headroom_m), not constants.
//
// The whole stroke is ONE undo step and mutates the graph only inside apply().
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "cp/road_commands.h"

namespace cp {

struct StrokeSpec {
    std::vector<Vec3> points;  // control points, at least 2; z is the authored height
    std::optional<double> heading_a, heading_b;  // pinned end tangents (continuation, snapping)
    std::string road_type;
    bool one_way = false;
};

struct StrokeParams {
    // Height difference below which a plan crossing joins. Negative: the region pack's plan_crossing_junction_dz_m
    // (and when that is 0 / there is no region, every plan crossing joins, as layouts always did).
    double join_dz = -1;
    // Clear height above the join tolerance. Negative: the region pack's headroom_m.
    double headroom = -1;
    // Refuse crossings with join_dz <= height difference < headroom (the road tool). Off for layouts.
    bool refuse_band = false;
    // Find crossings on the fitted centreline of the stroke (tool) instead of the straight segments between the
    // clicked points (layouts, whose golden results depend on that). The curve version pins the heading at each
    // crossing to the stroke's own heading there.
    bool follow_curve = false;
    // Existing edges left out of the crossing tests (editing a road: its old geometry must not cross its new one).
    std::vector<EdgeId> ignore_edges;
    double snap_tol = 1.0;    // m: a road end this close to a node / centreline attaches to it (layout kSnapTol)
    double node_tol = 0.75;   // m: an existing node this close to an interior stretch splits the road (kNodeTol)
    double sample_step = 0.5; // m: centreline sampling of existing edges for crossings (kSampleStep)
    double curve_step = 1.0;  // m: chord length of the stroke centreline when follow_curve
};

// fit_clothoid_chain with a sanity bound: a chain more than 20 times longer than the polyline through its points
// is refused (nullopt). The solver returns such chains (up to 1e15 m, which hangs anything that samples them) for
// a stroke that doubles back exactly onto an earlier point. Used by the road tool, the edit tool and the stroke.
std::optional<ClothoidChain> fit_sane_chain(const std::vector<Vec2>& points, std::optional<double> heading_a = {},
                                            std::optional<double> heading_b = {});

enum class CrossingVerdict { Join, Refused, Clear };

struct StrokeCrossing {
    double key = 0;         // position along the stroke polyline: segment index + parameter
    Vec3 pos;               // on the stroke
    double existing_z = 0;  // height of the crossed road there
    double dz = 0;          // |stroke z - existing z|
    CrossingVerdict verdict = CrossingVerdict::Clear;
    EdgeId edge;
    double edge_s = 0;
    double join_limit = 0;  // m: height difference below which the crossing joins (1e9 when joining is not configured)
    double headroom = 0;    // m: height difference from which the crossing is clear
};

// "crossing too close vertically (3.0 m): raise/lower to junction or clear 5.3 m": the refusal text used by
// check_stroke, DrawStroke and the road tool's preview, so a host shows the same words everywhere.
std::string crossing_refusal_text(double dz, double headroom);

// A snapshot of the centrelines of every edge, sampled for crossing tests. Building one costs one pass over the
// graph; a pointer tool builds it once per graph change and then asks per mouse move (the overload without a
// cache re-samples every edge on every call). Rebuild after the graph changes (it copies what it needs).
class CrossingCache {
public:
    CrossingCache() = default;
    explicit CrossingCache(const RoadGraph& g, double sample_step = 0.5);
    bool empty() const { return !impl_; }
    struct Impl;
    const Impl* impl() const { return impl_.get(); }

private:
    std::shared_ptr<const Impl> impl_;
};

// Every plan crossing of the stroke with an existing edge (ends excluded: those attach), ordered along the
// stroke, with the verdict the thresholds give. Read-only. Empty when the stroke cannot be fitted.
std::vector<StrokeCrossing> stroke_crossings(const RoadGraph& g, const StrokeSpec& spec, const StrokeParams& params = {});
// The same against a prebuilt cache (cheap: only edges whose bounding box meets the stroke are tested).
// StrokeParams::sample_step is ignored (the cache's step applies).
std::vector<StrokeCrossing> stroke_crossings(const RoadGraph& g, const CrossingCache& cache, const StrokeSpec& spec,
                                             const StrokeParams& params = {});

// Empty when the stroke may be drawn; else the reason (a refused crossing, "cannot fit curve"). Read-only.
std::string check_stroke(const RoadGraph& g, const StrokeSpec& spec, const StrokeParams& params = {});

struct StrokeError {
    enum class Kind { None, BadSpec, CannotFit, Refused, PieceFailed, EndsInsideJunction, TooManyCrossings };
    Kind kind = Kind::None;
    std::string message;
};

class DrawStroke : public RoadCommand {
public:
    // `sink`, when set, receives the reason apply() failed (the stack discards a failed command, so the caller
    // cannot ask the command afterwards). It must outlive the command.
    DrawStroke(StrokeSpec spec, StrokeParams params, StrokeError* sink = nullptr);
    bool apply(RoadGraph& g) override;
    void revert(RoadGraph& g) override;
    std::string name() const override { return "draw stroke"; }

private:
    bool fail(StrokeError::Kind k, std::string msg);
    StrokeSpec spec_;
    StrokeParams params_;
    StrokeError* sink_;
    std::vector<RoadCommandPtr> parts_;
};

std::unique_ptr<DrawStroke> make_draw_stroke(StrokeSpec spec, StrokeParams params = {}, StrokeError* sink = nullptr);

}  // namespace cp
