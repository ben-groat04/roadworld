#pragma once
// The road-editing tool (the prototype's EDIT mode, Tab) as a headless state machine, the counterpart of
// DrawSession (cp/road_tool.h): hover picks, selection, handle drag with a live preview, height change of a
// point, delete, change type / one-way. Port of the edit half of prototypes/road-draw/src/input.js
// (processMouse, pointerdown / move / up, commitHandleEdit, adjustHeight, the Delete key).
//
// An EditSession NEVER mutates the graph. Everything that would change it comes back as a cp::Command
// (EditResult::command) the host executes on its CommandStack: one command per user gesture, so one undo step
// each (a whole drag is one step, taken from the grab to the release). After executing a command the host calls
// invalidate() (the road index is a snapshot; a size check also notices most changes by itself).
//
// Handles: a road's control points are node a, interior[0..n-1], node b, numbered 0..n+1 (the prototype's
// `pts` indices). Handles 0 and n+1 are junction / end NODES: moving one moves every road attached to it
// (make_move_node). Interior handles move only their road (make_move_interior_point).
//
// A "road" is what the user drew, not one engine edge: a road through a junction is several edges (the stroke is
// split at every junction it makes), and the prototype's road is the whole stroke. Selecting, the handles, the
// profile, the length in the HUD and Delete all work on the road chain: the edges that continue each other
// straight through a JUNCTION node of three or more edges (same type and one-way flag, tangents within
// RoadToolRules::chain_max_turn_deg, each the other's best continuation). A road that bends sharply at a junction
// is two roads, and so are two roads that merely meet end to end (a road continued from another's end).
//
// Like the prototype, an edit that lands the road across another road at the same level makes a junction there
// (JS commitHandleEdit -> conformRoad): the command then replaces the edited edge by the drawn stroke through its
// new control points (the same code as drawing, so a T or a crossing is a real junction), still ONE undo step.
//
// Differences from the prototype (on purpose, see docs/unreal-ui-parity.md section E):
//   - the nearest handle in reach wins (the prototype took the last one in index order);
//   - a drag is checked before it is committed with the same errors / advisories / profile as drawing, and a
//     refused drag returns the reason instead of snapping back silently;
//   - hovering any road thing (node, interior point, road) says which road to light up (EditPreview::hover_chain),
//     so a host highlights the road only, as the prototype did, while the pick kind stays available.
//
// Units: metres, doubles, x east, y north, z up.
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "cp/road_commands.h"
#include "cp/road_tool.h"

namespace cp {

struct EditParams {
    double handle_pick_px = 11;      // JS processMouse: a handle is hovered within 11 px of its screen position
    double road_pick_px = 12;        // JS processMouse: nearestOnRoad(.., 12 * wpp, ..)
    double handle_lift = 0.5;        // JS: handles are projected / drawn 0.5 m above the point (p.z + 0.5)
    double level_tol = 15;           // JS nearestOnRoad: roads more than 15 m above / below the cursor are ignored
    double min_span = 4;             // JS T.minSpan: a move leaving two control points closer than this is refused
    double drag_threshold_px = 4;    // JS pointerup: a press that travels more than 4 px is a drag, not a click (the host measures it)
    double pick_max_distance = 14000;  // JS rayTerrain max
    double height_step = 1;          // JS PgUp / PgDn
    double height_step_large = 5;    // JS: with Shift
    double ghost_lift = 0.35;        // JS ghostMesh: ribbons sit 0.35 m above the centreline (echoed in Ribbon::lift)
    double world_margin = 1;         // JS clamp(x, 1, WORLD - 1): a drag stays this far inside the heightfield footprint
    double profile_step = 2.0;       // NEW: station spacing of the profile series (m)
    bool profile_series = true;      // build EditPreview::profile
    bool crossing_feedback = true;   // test the edited road against the others (a refused crossing refuses the edit)
    // JS commitHandleEdit -> conformRoad: an edit that makes the road cross another at the same level makes a
    // junction there (one undo step). Off: the edit is allowed and only advised (IssueCode::CrossingAtGrade).
    // Applies to interior handles and to free road ends; dragging a node shared by several roads only advises.
    bool make_junctions = true;
    // A click selects the whole road (the road chain), not one engine edge (JS: one polyline per road).
    bool chain_selection = true;
    std::optional<double> water_level;  // ProfileSeries::water_level (tuning panel "Water level m")
};

enum class EditHoverKind {
    None,
    Handle,         // a control point of the SELECTED road (the one a press starts dragging)
    Node,           // a junction / road end of any road
    InteriorPoint,  // an interior control point of any road
    Road,           // the centreline of any road
};

struct EditHover {
    EditHoverKind kind = EditHoverKind::None;
    EdgeId edge;               // the road (for Node: the lowest-id road at the node)
    std::size_t handle = 0;    // Handle / InteriorPoint / Node: handle index on `edge` (see the header)
    NodeId node;               // Handle on an end, and Node
    Vec3 point;                // the picked point: the handle's position, or the centreline point for Road
    double s = 0;              // Road: arc length of the point along `edge`
    double distance = 0;       // m from the pointer (3D ray distance for handles, plan distance for Road)
};

struct Handle {
    EdgeId edge;
    std::size_t index = 0;     // 0 = node a, 1..n = interior[index - 1], n + 1 = node b
    NodeId node;               // set for the two ends
    Vec3 pos;
    std::size_t position = 0;  // order along the road (0 = its first control point); a junction node appears once
    bool is_node() const { return node.valid(); }
};

// The edges that make one drawn road, in order along it. reversed[i]: edges[i] runs against that order.
struct RoadChain {
    std::vector<EdgeId> edges;
    std::vector<bool> reversed;
    double length = 0;  // m, along the centreline
    bool empty() const { return edges.empty(); }
};
// The road chain of an edge (see the header); a single-element chain when nothing continues it. max_turn_deg:
// RoadToolRules::chain_max_turn_deg. An unknown edge gives an empty chain.
RoadChain road_chain_of(const RoadGraph& g, EdgeId id, double max_turn_deg);

// What the HUD says about the selected road.
struct EditSelection {
    bool valid = false;
    EdgeId edge;                   // the edge that was clicked (anchor of the chain)
    RoadChain chain;               // the whole road
    int number = 0;                // the "#N": the lowest edge id in the chain
    double length = 0;             // m, whole road (the HUD's "200 m")
    std::string road_type;         // the road's own type (the prototype's header showed the draw tool's; the selected road's is the right one)
    std::string road_type_name;    // RoadType::hud_name()
    bool one_way = false;
    bool dual_type = false;
};

// The geometry a changed road would have (for drawing the ghost while dragging).
struct EditGhost {
    EdgeId edge;
    std::vector<Vec3> centreline;  // fitted, sampled
    bool fits = true;
};

struct EditPreview {
    bool has_pick = false;         // the ray hit something; nothing below is meaningful otherwise
    Vec3 cursor;                   // the point under the pointer (while dragging: the handle's new position)
    EditHover hover;
    bool dragging = false;
    // The road the profile panel shows: while dragging the edited road with its new geometry, else the selected
    // road, else the hovered one (JS renderProfile). Invalid when there is none.
    EdgeId profile_edge;
    ProfileSeries profile;          // the whole road chain (joined), with the dragged edge's new geometry while dragging
    EditSelection selection;        // the selected road (valid == false: none): HUD line, type name, length
    // The road to light up for this hover, whatever was picked (a node or interior point of a road lights that road;
    // the selected road's handle lights the selected road). Empty when nothing is hovered.
    std::vector<EdgeId> hover_chain;
    // Dragging only: everything below describes the road being edited AS IT WOULD BE after the move.
    std::vector<EditGhost> ghosts;  // every road the move changes (a node drag changes all roads at the node)
    std::vector<Ribbon> ribbons;    // the edited road's ghost ribbon (one; dual types are edited carriageway by carriageway)
    std::vector<Issue> errors;      // a release would be refused
    std::vector<Issue> advisories;
    Readouts readouts;
    std::vector<CrossingReport> crossings;
    bool ok() const { return has_pick && errors.empty(); }
};

struct EditResult {
    RoadCommandPtr command;  // null on error
    std::string error;
    IssueCode error_code = IssueCode::CannotFit;
    std::vector<Issue> advisories;  // allowed, but worth saying (as for drawing)
    // The edit made junctions where the road now crosses others (EditParams::make_junctions): the edited edge is
    // replaced by new edges, so its id (and the selection) is gone after the command executes. select_at(anchor)
    // re-selects the road.
    bool made_junction = false;
    Vec3 anchor;  // a point on the edited road that is not on any other road (the moved handle when it is clear of them): for select_at()
    bool ok() const { return command != nullptr; }
};

class EditSession {
public:
    EditSession(const RoadGraph& graph, const Heightfield& ground, EditParams params = {});

    const EditParams& params() const { return params_; }
    EditParams& params() { return params_; }

    // ---- selection ----
    EdgeId selected() const;  // the clicked edge; invalid when nothing is selected or the road no longer exists
    void select(EdgeId id);
    void clear_selection() { sel_ = {}; }
    // Select the road under a point (after a command replaced the edge, or from a host's own pick); false when none.
    bool select_at(Vec3 point, double radius = 3.0);
    // The selected road as the HUD shows it.
    EditSelection selection() const;
    // The road chain of any edge (the whole road with chain_selection, else just that edge).
    RoadChain road_chain(EdgeId id) const;
    // The selected road's control points (the white handles), in order along the road.
    std::vector<Handle> handles() const;
    // A road's fitted centreline, sampled about every `step` metres (the hover / selection line).
    std::vector<Vec3> centreline(EdgeId id, double step = 2.0) const;

    // ---- pointer ----
    // Everything to show for a pointer ray, without changing anything. While a drag is active this previews it.
    EditPreview hover(const PointerInput& in);
    // A click (press and release without travel): selects the hovered road, or clears the selection when over
    // nothing; a click on a handle of the selected road changes nothing. Returns what was under the pointer.
    EditHover click(const PointerInput& in);

    // ---- drag ----
    // Start dragging the handle under the pointer (it must be a Handle of the selected road). The handle then
    // moves in the horizontal plane through its height, keeping its height above the ground (JS dzT).
    bool begin_drag(const PointerInput& in);  // a handle of the selected road (any edge of its chain)
    bool dragging() const { return drag_.has_value(); }
    // The live preview of the drag at this pointer position (== hover() while dragging).
    EditPreview drag_to(const PointerInput& in) { return hover(in); }
    // Release: the one command that moves the handle to where the pointer is, or the reason it is refused.
    // Either way the drag ends (a refused drag changes nothing: the road stays where it was).
    EditResult end_drag(const PointerInput& in);
    void cancel_drag() { drag_.reset(); }

    // ---- point height (PgUp / PgDn, or typing a value) ----
    EditResult set_point_height(EdgeId edge, std::size_t handle, double z);
    EditResult adjust_point_height(EdgeId edge, std::size_t handle, double delta);

    // ---- the selected road ----
    EditResult delete_selected();
    EditResult change_type(const std::string& road_type);
    EditResult set_one_way(bool one_way);

    // Call after the host executed a command (the road index is rebuilt on the next query).
    void invalidate() { index_valid_ = false; }

private:
    struct Drag {
        EdgeId edge;
        std::size_t handle = 0;
        Vec3 start;
        double plane_z = 0;
        double dz_terrain = 0;
        Vec3 current;
    };
    struct Trial;
    struct Ground {
        bool has_pick = false;
        Vec3 hit;
        double dist = 0;
        double px = 0;
        bool on_surface = false;
    };
    Ground resolve(const PointerInput& in) const;
    EditHover pick(const PointerInput& in, const Ground& gr);
    bool drag_position(const PointerInput& in, Vec3& out) const;
    Trial evaluate(EdgeId edge, std::size_t handle, Vec3 to);
    EditResult commit_move(EdgeId edge, std::size_t handle, Vec3 to);
    void ensure_index();
    ProfileSeries cached_profile(EdgeId id);
    ProfileSeries chain_profile(const RoadChain& chain, const std::map<EdgeId, ProfileSeries>& moved);
    double chain_turn_deg() const;

    const RoadGraph& graph_;
    const Heightfield& ground_;
    EditParams params_;
    EdgeId sel_;
    std::optional<Drag> drag_;
    RoadIndex index_;
    CrossingCache cross_cache_;
    bool index_valid_ = false;
    std::uint64_t fingerprint_ = 0;
    struct ProfileCache {
        std::uint64_t key = 0;
        ProfileSeries series;
    };
    std::map<EdgeId, ProfileCache> profile_cache_;
};

}  // namespace cp
