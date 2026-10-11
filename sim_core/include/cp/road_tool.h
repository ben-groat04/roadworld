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
// Finishing: click() on the last placed point, finish() (the road ends at the last CLICKED point; the prototype's
// Enter / right-click) and finish_at_cursor() (the road ends where the preview's ghost ends: what you see is what
// you get) all return the one command to execute. In the would_finish state the preview shows exactly the
// geometry finish() would commit.
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
    // NEW (no JS equivalent), OFF by default so the tool behaves as the prototype: there a click on the last placed
    // point is just another point (a segment under min_span: the MinSpan error, no ghost, screenshot 12), and a
    // stroke ends with Enter / right-click / double-click / a snap onto a road. Set it to e.g. 10: a click within
    // that many pixels of the last placed point finishes the stroke (DrawPreview::would_finish shows the finish cue).
    double finish_pick_px = 0;
    // ---- added 11 Oct 2026 (parity work) ----
    double ghost_lift = 0.35;         // JS ghostMesh: the ghost ribbon sits 0.35 m above the centreline (echoed in Ribbon::lift)
    double profile_step = 2.0;        // NEW: spacing of profile stations (m); control points and curve ends are added exactly
    bool profile_series = true;       // build DrawPreview::profile (off: skips the station arrays, keeps the advisories)
    // Crossings of the stroke-so-far-plus-hover with existing roads are reported in DrawPreview::crossings, and a
    // refused one (height difference in the band neither joined nor cleared) is an error that refuses the click.
    // Off: the prototype's behaviour, where it was found only when the road is finished.
    bool crossing_feedback = true;
    // The height of the water plane (tuning panel "Water level m"; ToolTuning). Set: the profile series carries it and
    // each station says how deep the water is over the ground there.
    std::optional<double> water_level;
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

// What the HUD and the profile panel need of one road (the stroke being drawn, or a road being edited).
// Lengths are metres along the centreline; heights metres above the datum of the heightfield.
struct ProfileStation {
    double s = 0;          // m along the centreline
    double road_z = 0;     // m, road surface (vertical profile incl. vertical curves) at the centreline
    double ground_z = 0;   // m, ORIGINAL ground at the centreline (the heightfield, never the graded ground)
    double depth = 0;      // m, road_z - ground_z: > 0 fill, < 0 cut (the host colours it against ProfileSeries::wall_cap)
    double grade_pct = 0;  // %, signed slope of the road surface at this station
    double water_depth = 0;  // m, water over the original ground here (ProfileSeries::water_level - ground_z when positive; else 0)
};
// One constant-gradient run between two consecutive control points (what the panel labels).
struct ProfileGrade {
    double s0 = 0, s1 = 0;  // m, the two control points
    double z0 = 0, z1 = 0;  // m, authored heights there
    double s_mid = 0;       // m, where the panel centres the label
    double grade_pct = 0;   // %, (z1 - z0) / (s1 - s0) * 100
    std::string label;      // "+9.5%": sign always, one decimal
    bool over_limit = false;  // |grade| above the road type's advisory maximum (the panel draws it amber)
};
// A control point and the vertical curve centred on it (length 0: a plain point or an end).
struct ProfileCurve {
    double s = 0, z = 0;    // m, the control point (authored height)
    double length = 0;      // m, curve length (0 at the ends)
    double s0 = 0, s1 = 0;  // m, extent of the curve: s -/+ length / 2
};
// A stretch the cut / fill rule says needs a structure (grading.h required_structures: beyond the wall cap).
struct ProfileStructure {
    double s0 = 0, s1 = 0;  // m, first and last station of the range
    StructureKind kind = StructureKind::Bridge;
};
struct ProfileSeries {
    bool valid = false;           // false when there was nothing to profile (no fit, no region, series switched off)
    double length = 0;            // m, centreline length (the panel's horizontal axis runs 0 .. length)
    double step = 0;              // m, nominal station spacing (stations at the control points / curve ends are extra)
    double wall_cap = 0;          // m, region wall cap: |depth| beyond it is the bridge / tunnel zone
    std::optional<double> water_level;  // m, the water plane (the tuning panel's value) when the host gave one
    double max_grade_pct = 0;     // %, the road type's advisory maximum grade (0 = unset)
    double z_min = 0, z_max = 0;  // m, extent over road and ground (before any padding)
    std::vector<ProfileStation> stations;      // ascending s, first 0, last = length
    std::vector<ProfileGrade> grades;          // one per run between control points
    std::vector<ProfileCurve> curves;          // one per control point (the panel's dots and yellow bands)
    std::vector<ProfileStructure> structures;  // bridge / tunnel ranges required by the cut / fill depth
    std::vector<StructureSpan> authored_structures;  // existing roads: the spans stored on the edge (empty for a stroke)
};

// How the stroke-so-far-plus-hover (or an edited road) meets an existing road in plan.
struct CrossingReport {
    Vec3 pos;                  // on the new road
    EdgeId edge;               // the existing road
    double edge_s = 0;         // m along it
    double existing_z = 0;     // m, its height there
    double level_difference = 0;  // m, |new - existing| height
    CrossingVerdict verdict = CrossingVerdict::Clear;
    double join_limit = 0;     // m: below this the crossing becomes a junction
    double clearance = 0;      // m: clear when level_difference >= clearance (the region's headroom)
    int carriageway = 0;       // 0 the centreline / a single road; 1 and 2 the two carriageways of a dual stroke
    std::string text;          // Refused: the reason (same words as the committing command); else empty
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
    CrossingAtGrade,      // advisory (editing only): the edited road crosses another at the same level; an edit makes no junction
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
    bool g2 = true;            // the curvature-continuous solve succeeded (false: G1 fallback; JS "G2 ✓" / "G1")
};

struct Ribbon {
    std::vector<Vec3> committed;  // fitted centreline of the stroke so far (up to the last clicked point)
    std::vector<Vec3> hover;      // last clicked point to the cursor (empty in the would_finish state: the whole ribbon is committed)
    double width = 0;             // m: = total_width (the prototype ghost draws the TOTAL width, JS ROAD_TYPES.total)
    double total_width = 0;       // m, everything one edge of the type represents: lanes, kerbs, footways, verges (one carriageway of a dual type)
    double carriageway_width = 0; // m, the part with the "carriageway" lane profile only (road surface, no kerbs / footways / verges)
    double lift = 0;              // m, DrawParams::ghost_lift
};

struct DrawPreview {
    // The tool state the HUD header shows, echoed so a host needs no second lookup (set even without a pick).
    std::string road_type;        // the session's road type id
    std::string road_type_name;   // its name for the HUD ("A road / dual c'way": RoadType::hud_name()); empty without a region
    bool one_way = false;         // the one-way flag (O)
    bool dual_type = false;       // the type draws two carriageways from one stroke (O then draws one: "single carriageway")
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
    ProfileSeries profile;        // the whole stroke including the hover segment (DrawParams::profile_series)
    std::vector<CrossingReport> crossings;  // every plan crossing of the stroke (+ hover) with an existing road (DrawParams::crossing_feedback)
    // A remembered refusal of finish() / a click that is still in force (also listed in errors): see DrawSession::last_refusal().
    bool refusal_active = false;
    // The cursor is on the last placed point: a click finishes the stroke. The ribbon, readouts, errors and
    // profile are then those of the stroke as finish() would commit it (no hover segment).
    bool would_finish = false;
    bool ok() const { return has_pick && errors.empty(); }
};

struct FinishResult {
    RoadCommandPtr command;  // null on error
    std::string error;
    IssueCode error_code = IssueCode::CannotFit;  // what kind of refusal error is (Crossing, MinSpan, CannotFit, NoRegion)
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
    // End the stroke at the last clicked point (the prototype's Enter / right-click / double-click). On success
    // the session resets; on error the stroke stays open (and last_refusal() keeps the reason, see below).
    FinishResult finish();
    // End the stroke where the preview's ghost ends: the cursor point (snapped exactly as hover() / click()
    // show it) becomes the last point. The stroke hover() previewed is exactly the stroke committed. With the
    // cursor on the last placed point (would_finish) this is finish(). A cursor that has no pick or whose
    // preview carries errors refuses, with the reason. Success resets the session; error leaves it open.
    FinishResult finish_at_cursor(const PointerInput& in, Modifiers mods = {});
    // The reason of the latest refused finish / auto-finish, kept until its cause is gone: the stroke's points,
    // road type, one-way flag or the graph changed (or cancel / undo_point / a new stroke). While in force
    // every hover() lists it in errors and sets refusal_active, so a host cannot lose it on the next mouse move.
    std::optional<Issue> last_refusal() const {
        if (refusal_) return refusal_->issue;
        return std::nullopt;
    }
    void cancel();        // Escape / right-click with fewer than two points
    bool undo_point();    // Backspace: drop the last point; false when there is none

    // Call after the host executed a command (the road index is rebuilt on the next query).
    void invalidate() { index_valid_ = false; }

private:
    struct Refusal {
        Issue issue;
        std::vector<Vec3> pts;  // the stroke the refusal was about (clicked points, plus the cursor point for an auto-finish)
        bool whole_stroke = false;  // finish(): applies whatever the cursor does; else only with the cursor on pts.back()
        std::string type;
        bool one_way = false;
        std::size_t nodes = 0, edges = 0;
        std::uint64_t last_edge = 0;
    };
    void set_refusal(const std::string& reason, std::vector<Vec3> pts, bool whole_stroke);
    bool refusal_in_force(const std::vector<Vec3>& with_cursor) const;
    struct Snap;
    struct Resolved;
    Resolved resolve(const PointerInput& in, Modifiers mods);
    Snap compute_snap(const Vec3& hit, double mpp, bool on_surface, Modifiers mods);
    void build_preview(DrawPreview& pv, const Snap& sn);
    // Fit, check and describe the stroke through pts (the clicked points, plus the cursor when has_hover).
    void build_geometry(DrawPreview& pv, const std::vector<Vec3>& pts, std::optional<double> he, bool has_hover);
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
    CrossingCache cross_cache_;
    std::optional<Refusal> refusal_;
    bool index_valid_ = false;
    std::size_t fp_nodes_ = 0, fp_edges_ = 0;
    std::uint64_t fp_last_edge_ = 0;
};

// ---- analysis shared by the draw tool and the edit tool (cp/edit_tool.h) ----

// Everything the HUD and the profile show about one fitted road, computed the same way for a stroke being drawn
// and for a road being edited.
struct EdgeAnalysis {
    std::vector<Issue> errors;       // Fold, Crossing (refused)
    std::vector<Issue> advisories;   // radius, grade, bridge, tunnel, G1, (edit) crossing at grade
    Readouts readouts;
    ProfileSeries profile;
    std::vector<CrossingReport> crossings;
};
struct AnalysisOptions {
    std::string road_type;
    bool one_way = false;
    bool dual_pair = false;       // the edge is the centreline of a dual stroke: the footprint is both carriageways + median
    double profile_step = 2.0;
    bool want_profile = true;
    // Crossing tests: one StrokeSpec per carriageway (empty: none). ignore_edges: the road(s) being edited.
    std::vector<StrokeSpec> crossing_specs;
    const CrossingCache* cross_cache = nullptr;
    std::vector<EdgeId> ignore_edges;
    bool edit_mode = false;       // report a same-level crossing as the CrossingAtGrade advisory
    bool junction_on_join = false;  // edit_mode: the edit will make a junction at that crossing (changes the advisory's words only)
    std::optional<double> water_level;  // fill ProfileSeries::water_level and the stations' water_depth
};
// edge must be fitted (horizontal and vertical). Read-only.
EdgeAnalysis analyse_edge(const RoadGraph& g, const Heightfield& ground, const Edge& edge, const AnalysisOptions& opt);

// The profile series of a road already in the graph (edit-mode selection / hover), against the original ground.
ProfileSeries profile_of_edge(const RoadGraph& g, const Heightfield& ground, EdgeId id, double step = 2.0,
                              std::optional<double> water_level = std::nullopt);
// The same for an edge that is NOT in the graph (the geometry a drag would give it); it must be fitted.
ProfileSeries profile_of_geometry(const RoadGraph& g, const Heightfield& ground, const Edge& edge, double step = 2.0,
                                  std::optional<double> water_level = std::nullopt);

// Several roads that make one road (an edit-tool road chain) as one series, in the order given. `reversed[i]` says the
// i-th series runs against the chain direction (its s is flipped, grades negated, labels redone). The control point
// shared by two neighbours appears once. Gives an invalid series when any part is invalid or the list is empty.
ProfileSeries join_profiles(const std::vector<ProfileSeries>& parts, const std::vector<bool>& reversed);

// Widths of what one edge of a road type represents (a dual type: one carriageway), from the region's lanes.
struct RoadWidths {
    double total = 0;         // m, all lanes
    double carriageway = 0;   // m, lanes with the "carriageway" profile
    bool known = false;       // false: no region pack or unknown type
};
RoadWidths road_widths(const RoadGraph& g, const std::string& road_type, bool one_way);

}  // namespace cp
