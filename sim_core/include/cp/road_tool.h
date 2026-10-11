#pragma once
// The road-drawing tool as a headless state machine: a host (the browser prototype, Unreal) feeds it pointer
// rays and gets back everything it needs to show and to execute, with no rules of its own. Port of the drawing
// interaction in prototypes/road-draw/src/input.js (computeSnap, buildPreview, startOrAddPoint, finishRoad).
//
// A DrawSession NEVER mutates the graph. hover() and click() read the graph / heightfield it was given;
// finish() (or an auto-finishing click) returns a cp::Command the host executes on its CommandStack, which is
// the one undo step of the whole stroke. After the host executes any command it calls invalidate() (the road
// index is a snapshot; a cheap size check also notices most changes by itself).
//
// Units: metres, radians, doubles. x east, y north, z up. Driving-side agnostic: dual carriageways are laid
// out from the region's driving side, everything else is symmetric.
#include <optional>
#include <string>
#include <vector>

#include "cp/ground_pick.h"
#include "cp/road_index.h"
#include "cp/stroke.h"

namespace cp {

// Tunables. Defaults are the prototype's values; the JS name each came from is in the comment.
struct DrawParams {
    double snap_angle_deg = 15;       // T.snapAngle: angle snap step from the previous segment (or pinned start heading)
    double snap_length = 8;           // T.snapLen: length snap step (and the minimum snapped length)
    double min_span = 4;              // T.minSpan: shortest segment between clicked points
    double end_pick_px = 14;          // computeSnap: road end / node pick radius, 14 * px
    double road_pick_px = 10;         // computeSnap: on-road pick radius, 10 * px
    double extension_pick_px = 9;     // computeSnap: end-extension guide radius, 9 * px
    double offset_pick_px = 7;        // computeSnap: parallel-offset guide tolerance, 7 * px
    double offset_search = 80;        // computeSnap: nearestOnRoad(.., 80, ..) search radius for the offset guide (m)
    int offset_multiples = 3;         // computeSnap: for (k of [1, 2, 3])
    double level_tol = 15;            // computeSnap / nearestOnRoad: roads more than 15 m above / below the cursor are ignored
    double repeat_tol = 0.01;         // computeSnap: an end snap onto the last clicked point is ignored (0.01 m)
    double extension_min = 1;         // computeSnap: the end-extension guide starts 1 m out from the end
    double guide_drape_step = 3;      // draped(): guide polylines sampled every 3 m
    double guide_lift = 0.5;          // draped(): guides sit 0.5 m above the ground
    double pick_max_distance = 14000; // rayTerrain: max = 14000
    double finish_pick_px = 10;       // NEW (no JS equivalent): a click within 10 * px of the last placed point finishes the stroke
};

struct Modifiers {
    bool snap_off = false;  // JS: state.shift (hold to draw free)
};

struct PointerInput {
    Ray ray;
    // Pixel scale: metres on the ground per screen pixel at the cursor. Either give it, or give
    // metres_per_pixel_per_metre (= 2 tan(fov/2) / viewport_height_px, the JS `wpp / dist`) and the session
    // multiplies by the hit distance. metres_per_pixel > 0 wins.
    double metres_per_pixel = 0;
    double metres_per_pixel_per_metre = 0;
    // Optional host hit on a road surface (bridge deck) nearer than the ground: the session then uses this point
    // (JS: pick() returning `road`) and, when it ends up unsnapped, takes its height.
    std::optional<Vec3> surface_hit;
};

enum class SnapKind {
    Free,   // cursor on the ground
    Angle,  // snapped to the angle / length grid from the last point (JS 'angle')
    Guide,  // end-extension or parallel-offset guide (JS 'guide')
    End,    // a free road end: continues the road tangentially (JS 'end')
    Node,   // an existing junction node (no pinned heading)
    Road,   // on an existing road centreline (JS 'road')
};

struct SnapGuide {
    enum class Type { None, Line, Offset } type = Type::None;
    std::vector<Vec3> polyline;  // draped on the ground (guide_lift above it)
    bool faint = false;          // the angle / length grid line
    EdgeId edge;                 // Offset: the road it is parallel to
    double offset = 0;           // Offset: signed lateral offset from it (+ left of a -> b)
};

enum class IssueCode {
    MinSpan,              // error: a segment is shorter than min_span
    Fold,                 // error: radius smaller than the half-width, the road edge folds
    CannotFit,            // error: no curve fits the points
    Crossing,             // error (finish): a crossing in the refused height band
    NoRegion,             // error: no region pack / unknown road type
    RadiusBelowMinimum,   // advisory
    GradeAboveAdvisory,   // advisory
    BridgeNeeded,         // advisory
    TunnelNeeded,         // advisory
    G1Fallback,           // advisory: the G2 solve fell back to G1 (curvature kink)
};

struct Issue {
    IssueCode code = IssueCode::CannotFit;
    std::string text;
};

struct Readouts {
    double length = 0;         // m, whole stroke including the hover segment
    double span_length = 0;    // m, the hover segment
    double min_radius = 0;     // m, tightest radius of the stroke (infinity on a straight)
    double end_radius = 0;     // m, radius at the cursor
    double max_grade_pct = 0;  // %, steepest point
    double grade_pct = 0;      // %, mean grade of the hover segment
    double height = 0;         // m, cursor height
    double heading_deg = 0;    // compass degrees (0 = north, clockwise) at the cursor
};

struct Ribbon {
    std::vector<Vec3> committed;  // fitted centreline of the stroke so far (up to the last clicked point)
    std::vector<Vec3> hover;      // last clicked point to the cursor
    double width = 0;             // m, full width of the carriageway the ribbon represents
};

struct DrawPreview {
    bool has_pick = false;        // the ray hit something; nothing below is meaningful otherwise
    Vec3 cursor;                  // resolved (snapped) cursor point
    SnapKind snap = SnapKind::Free;
    SnapGuide guide;
    EdgeId snap_edge;             // Road / End snaps
    NodeId snap_node;             // End / Node snaps
    std::vector<Vec3> clicked;    // points placed so far
    std::vector<Ribbon> ribbons;  // one per carriageway (a dual road has two); empty before the first click
    std::vector<Issue> errors;    // hard: a click is refused
    std::vector<Issue> advisories;
    Readouts readouts;
    bool would_finish = false;    // the cursor is on the last placed point: a click finishes the stroke
    bool ok() const { return has_pick && errors.empty(); }
};

struct FinishResult {
    RoadCommandPtr command;  // null on error
    std::string error;
    bool ok() const { return command != nullptr; }
};

struct ClickResult {
    bool added = false;          // a point was placed (the stroke now has one more point)
    bool auto_finished = false;  // the click snapped onto a road end / node / road and the stroke ended
    bool finished = false;       // the stroke ended (auto_finished, or the click was on the last placed point): finish.command is set
    std::vector<Issue> errors;   // why nothing was added (the preview's errors)
    FinishResult finish;         // auto_finished (or an attempted one): the command / the refusal reason
};

class DrawSession {
public:
    DrawSession(const RoadGraph& graph, const Heightfield& ground, DrawParams params = {});

    // ---- tool state ----
    void set_road_type(std::string id) { type_ = std::move(id); }
    void set_one_way(bool v) { one_way_ = v; }
    // PgUp / PgDn: raise or lower the next point (JS adjustHeight / state.dz). Reset by every click.
    void adjust_height(double delta) { dz_ += delta; }
    void set_height_offset(double dz) { dz_ = dz; }
    double height_offset() const { return dz_; }
    const std::string& road_type() const { return type_; }
    bool one_way() const { return one_way_; }
    const DrawParams& params() const { return params_; }
    DrawParams& params() { return params_; }

    // ---- stroke ----
    bool active() const { return !pts_.empty(); }
    const std::vector<Vec3>& points() const { return pts_; }
    // Everything to show for a cursor ray, without changing the stroke.
    DrawPreview hover(const PointerInput& in, Modifiers mods = {});
    // Place a point where hover() would show the cursor. A snap onto a road end / node / road after the first
    // point ends the stroke (auto_finished) and returns its command.
    ClickResult click(const PointerInput& in, Modifiers mods = {});
    // End the stroke at the last clicked point (Enter / right-click / double-click). On success the session
    // resets; on error the stroke stays open.
    FinishResult finish();
    void cancel();        // Escape / right-click with fewer than two points
    bool undo_point();    // Backspace: drop the last point; false when there is none

    // Call after the host executed a command (the road index is rebuilt on the next query).
    void invalidate() { index_valid_ = false; }

private:
    struct Snap;
    struct Resolved;
    Resolved resolve(const PointerInput& in, Modifiers mods);
    Snap compute_snap(const Vec3& hit, double mpp, bool on_surface, Modifiers mods);
    void build_preview(DrawPreview& pv, const Snap& sn);
    FinishResult finish_with(const std::vector<Vec3>& pts, std::optional<double> hs, std::optional<double> he);
    void ensure_index();
    double ground_z(double x, double y) const { return ground_.height(x, y); }

    const RoadGraph& graph_;
    const Heightfield& ground_;
    DrawParams params_;
    std::string type_;
    bool one_way_ = false;
    double dz_ = 0;
    std::vector<Vec3> pts_;
    std::optional<double> hs_;  // pinned start heading (JS d.hs)
    RoadIndex index_;
    bool index_valid_ = false;
    std::size_t fp_nodes_ = 0, fp_edges_ = 0;
    std::uint64_t fp_last_edge_ = 0;
};

}  // namespace cp
