#pragma once
// Road markings (docs/road-marking-rules.md, docs/v0-brief.md "Road markings"). Derived data only:
// generated from the lane graph, junction geometry and a rule file, never saved.
//
// Markings are GEOMETRY (docs/unreal-photorealism.md): every mark is a flat polygon lying exactly on
// the road surface (vertex z = vertical profile / arm surface, no lift: the renderer applies its own
// depth bias). Dashes are already resolved into individual marks.
//
// All rules and numbers come from the region pack's marking_rules.json (load_marking_rules). The code
// never branches on left/right except through DrivingSide helpers (turn_is_left, nearside/offside):
// lane lines come from lane boundaries, junction markings from arms/lanes/connectors, so both driving
// sides mirror exactly.
#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <vector>

#include "cp/junction.h"
#include "cp/junction_roundabout.h"
#include "cp/region_pack.h"
#include "cp/road_graph.h"

namespace cp {

// ---------------------------------------------------------------- output

enum class MarkingCategory : std::uint8_t {
    CentreLine,    // between opposing flows (also its warning variant: see the style id)
    LaneLine,      // between same-direction lanes
    EdgeLine,      // carriageway edge, hard shoulder / strip side, junction-mouth and slip edges
    ParkingBay,    // bay line along a parking lane and the transverse bay dividers
    GiveWayLine,   // transverse, broken
    StopLine,      // transverse, continuous
    GiveWayTriangle,
    Hatching,      // gore / ghost island stripes
    Zebra,         // crossing stripes and their give-way lines
    Arrow,         // lane arrows
    RoundaboutEntry,  // give-way line across the entering lanes of a roundabout approach
    Chevron,          // gore chevrons (same-direction flows either side)
};
const char* marking_category_name(MarkingCategory c);

// One mark: a simple polygon (3+ vertices, CCW in plan, implicit close) in MarkingSet::vertices.
// strip: the ring is a stroke strip (right side forward, then left side backward), so
// ring[i] and ring[n - 1 - i] are the two sides of the same cross-section; a renderer may emit
// quads between consecutive pairs instead of triangulating.
struct Marking {
    MarkingCategory category = MarkingCategory::EdgeLine;
    std::uint16_t style = 0;   // index into MarkingSet::styles
    std::uint16_t colour = 0;  // index into MarkingSet::colours
    std::uint32_t first = 0;   // index into MarkingSet::vertices
    std::uint32_t count = 0;
    bool strip = false;
    EdgeId edge;               // set for marks generated from an edge
    NodeId node;               // set for marks generated from a junction
    int lane = -1;             // line marks: boundary index (0 = left of the leftmost lane); else -1
    float wear = 0;            // 0..1 deterministic paint wear (wheel paths, low-frequency patchiness) for the renderer; no geometry change
};

struct MarkingSet {
    std::vector<std::string> styles;   // style ids (keys of MarkingRules::styles)
    std::vector<std::string> colours;  // colour ids (keys of MarkingRules::colours)
    std::vector<Vec3> vertices;
    std::vector<Marking> marks;
    std::vector<std::string> warnings;  // non-fatal problems (unknown marking set, folded offsets, ...)

    std::span<const Vec3> points(const Marking& m) const { return {vertices.data() + m.first, m.count}; }
    const std::string& style_id(const Marking& m) const { return styles[m.style]; }
    const std::string& colour_id(const Marking& m) const { return colours[m.colour]; }
    // Appends `o` (re-mapping its style / colour indices); warnings are appended too.
    void append(const MarkingSet& o);
    void clear();
    bool empty() const { return marks.empty(); }
};

// Triangle indices (local to marking.count, 3 per triangle) for a mark: a fan over strip pairs for
// strips, ear clipping otherwise. Empty for degenerate polygons.
std::vector<std::uint32_t> triangulate_marking(const MarkingSet& set, const Marking& m);

// ---------------------------------------------------------------- dash arithmetic

struct DashSpan {
    double s0 = 0, s1 = 0;  // along the line, 0 .. length
};
// Lays marks of length `mark` separated by `gap` over a line of `length`, symmetric about its middle.
// Whole marks only, except that a leftover long enough for a partial mark of at least `min_mark` (after
// its gap) is added at both ends; a run shorter than `mark` but at least `min_mark` becomes one short
// mark; anything shorter yields nothing. gap <= 0 gives one continuous mark. No mark is ever shorter than
// min_mark (except the single short mark above, which is at least min_mark).
std::vector<DashSpan> layout_dashes(double length, double mark, double gap, double min_mark);

// ---------------------------------------------------------------- rules (data: marking_rules.json)

// Conditions are all optional and ANDed. Speeds are m/s (converted from the file's speed_unit).
struct MarkCond {
    std::optional<double> speed_max, speed_min;  // speed <= max, speed > min
    std::optional<double> width_min, width_max;  // carriageway width (m, driving lanes only)
    std::optional<int> lanes_min, lanes_max;     // driving lanes of the edge (both directions)
    std::optional<bool> shoulder;                // a hard_shoulder / hard_strip lane exists
    std::optional<bool> one_way;                 // the edge is one-way (or the type is a dual / ring carriageway)
};

struct MarkCtx {
    double speed = 0;  // m/s, the road type's speed limit
    double width = 0;
    int lanes = 0;
    bool shoulder = false;
    bool one_way = false;
};
bool cond_matches(const MarkCond& c, const MarkCtx& ctx);

struct StyleChoice {
    MarkCond when;
    std::string style;  // "" = no marking
};
struct ValueChoice {
    MarkCond when;
    double value = 0;
};
using StyleSel = std::vector<StyleChoice>;
using ValueSel = std::vector<ValueChoice>;
// First matching choice; select_style returns nullptr for "no marking" or no match.
const std::string* select_style(const StyleSel& sel, const MarkCtx& ctx);
bool select_value(const ValueSel& sel, const MarkCtx& ctx, double& out);

struct MarkingColour {
    double rgb[3] = {1, 1, 1};  // sRGB 0..1
};

// One painted strand of a line style. gap <= 0: continuous. offset: lateral shift from the line's
// nominal position (+ = left of a -> b / planar left of travel).
struct Strand {
    double offset = 0;
    double width = 0.1;
    double mark = 1;
    double gap = 0;
};
struct LineStyle {
    std::string id;
    std::string colour;
    std::vector<Strand> strands;  // 1 = single line, 2 = double, ...
    double min_mark = 0;          // 0 = MarkingRules::min_mark_m
    bool fill = false;            // polygon style (stripes, arrows, triangle): colour only, no strands
    std::string source;
    double module() const;        // mark + gap of the first strand (0 when continuous)
};

// Where along an edge a boundary rule's style is replaced.
struct ZoneRule {
    enum class Kind { Approach, Curvature };
    Kind kind = Kind::Approach;
    ValueSel distance_m;  // Approach: warning zone length from the junction trim
    ValueSel min_marks;   // Approach: at least this many module lengths (the larger of the two wins)
    double min_curvature = 0;   // Curvature: |1/R| above this (1/m)
    double min_length_m = 0;    // Curvature: ignore shorter stretches
    StyleSel style;
    std::string source;
};

struct BoundaryRule {
    enum class Relation { Any, Opposing, Same };
    std::string id;
    MarkingCategory category = MarkingCategory::EdgeLine;
    std::string lane_left, lane_right;  // lane type ids; "*" any real lane; "outside" = beyond the cross-section
    Relation relation = Relation::Any;
    StyleSel style;
    double inset_m = 0;  // line shifted this far into the lane that carries traffic
    std::vector<ZoneRule> zones;  // first covering zone wins
    std::string source;
};

// Transverse parking bay dividers along a lane of this type.
struct BayRule {
    std::string lane_type;
    ValueSel length_m;      // bay length along the road
    std::string style;      // divider style (width = first strand width)
    double keepout_m = 0;   // no bays this close to a junction
    double edge_clear_m = 0.06;  // divider stops this far short of the bay line
    std::string source;
};

// When a give-way triangle is painted behind the line. Conditional: the major road's speed limit or road
// class reaches the threshold (either one, when both are set).
enum class TriangleMode { Never, Always, Conditional };

struct RuleSet {
    std::string id;
    std::vector<BoundaryRule> boundaries;
    std::vector<BayRule> bays;
    std::optional<TriangleMode> give_way_triangle;  // overrides GiveWayRule::triangle_mode for minor arms of this set
};


struct GiveWayRule {
    bool enabled = false;
    StyleSel style;
    double setback_m = 0.5;  // junction trim -> near edge of the line
    double line_gap_m = 0.3; // longitudinal lines stop this far short of the line
    // triangle (outline of 3 trapezoids) in each inbound lane, apex towards approaching traffic
    bool triangle = false;
    TriangleMode triangle_mode = TriangleMode::Always;
    std::optional<double> triangle_major_speed_min;  // m/s, Conditional: major road speed limit at least this
    std::optional<int> triangle_major_class_min;     // Conditional: major road RoadType::road_class at least this
    std::string triangle_style;
    double triangle_gap_m = 1.0;     // line far edge -> triangle base
    double triangle_base_m = 1.8;
    double triangle_height_m = 3.0;
    double triangle_stroke_m = 0.2;
    // Junctions whose arms all share one road class: the straightest pair of arms is the through route
    // and the rest give way ("straightest_pair_has_priority"), or nobody is marked ("none"). The pair must
    // be within equal_class_max_deviation_deg of straight, so a symmetric Y stays unmarked.
    bool equal_class_straight_pair = true;
    double equal_class_max_deviation_deg = 30;
    std::string source;
};

struct StopLineRule {
    bool enabled = false;  // lines are only made for MarkingParams::stop_arms
    std::string style;
    double setback_m = 0.5;
    double line_gap_m = 0.3;
    std::string source;
};

struct ZebraRule {
    bool enabled = false;
    std::string style;
    double stripe_width_m = 0.5;
    double gap_m = 0.6;
    double band_width_m = 3.0;     // along the road
    double clear_m = 1.0;          // longitudinal lines stop this far from the stripes' outer edge
    std::string give_way_style;    // "" = none
    double give_way_gap_m = 1.0;   // stripe edge -> give-way line near edge
    double min_start_gap_m = 1.0;  // gap between a junction give-way / stop line and the stripes
    std::string source;
};

struct Glyph {
    // Polygons in the travel frame, x 0..1 (tail -> tip), y -0.5..0.5 (+ = left of travel).
    std::vector<std::vector<Vec2>> polygons;
};

struct ArrowRule {
    bool enabled = false;
    int min_inbound_lanes = 2;
    std::vector<double> distances_m;  // tip distance from the junction trim, one arrow per entry
    ValueSel length_m;
    double width_m = 1.0;             // glyph width (capped at 70% of the lane)
    std::string style;
    std::map<std::string, Glyph> glyphs;  // ahead, turn (to the left), ahead_turn (ahead + left), fork (left + right)
    std::string source;
};

// Stripe pattern for a gore.
struct GorePattern {
    std::string style;        // fill style
    double stripe_width_m = 0.3;
    double pitch_m = 1.2;     // stripe repeat, measured across the stripes
    double angle_deg = 60;    // diagonal: stripe angle from the gore long axis; chevron: wing angle from the axis
};

// Gore / nose areas. The pattern is chosen by comparing the travel directions of the lanes either side of
// the gore: the same direction on both sides (merge, diverge) uses `same_direction`, opposing flows use
// `opposing`; each names "chevron", "diagonal" or "none". Chevron V apexes point the way the traffic
// travels. Either pattern is bordered by a continuous line when border_style is set.
struct GoreRule {
    bool enabled = false;
    std::string style;          // diagonal stripes (kept at this level for source compatibility)
    double stripe_width_m = 0.3;
    double pitch_m = 1.2;
    double angle_deg = 60;
    GorePattern chevron;
    std::string same_direction = "chevron";
    std::string opposing = "diagonal";
    StyleSel border_style;      // continuous boundary line round the gore (inside the polygon); empty = none
    double min_area_m2 = 0.05;
    StyleSel edge_style;        // empty / no match = no edge-line continuity through merge / diverge junctions
    double edge_inset_m = 0.225;  // edge line centre in from the lane edge
    std::string source;
};

struct RoundaboutEntryRule {
    bool enabled = false;
    StyleSel style;
    double setback_m = 0.5;
    double line_gap_m = 0.3;
    std::string source;
};

// Broken edge line carried across a side-road mouth on the through route (the line does not follow the
// kerb radii: it runs straight between the two arms' edge lines).
struct MouthEdgeRule {
    bool enabled = false;
    StyleSel style;
    double max_deviation_deg = 30;  // the two arms must be this close to straight to count as a through route
    std::string source;
};

// Unknown keys in marking_rules.json (and region.json) are errors on purpose (check_keys): a data file newer than the
// binary that reads it fails loudly instead of silently ignoring a rule. An older plugin build therefore rejects a
// newer file (e.g. splitter.ladder_pitch_m): rebuild the plugin with the matching sim_core.
// Roundabout splitter islands: outline round the island and diagonal hatching on the approach ahead of
// its nose. Needs MarkingParams::roundabouts; skipped cleanly without it.
struct SplitterRule {
    bool enabled = false;
    StyleSel outline_style;       // continuous line just outside the island
    double outline_gap_m = 0.15;  // island edge -> outline near edge
    std::string hatch_style;      // fill style of the bars; "" = no hatching
    double hatch_length_m = 12;   // approach hatching length ahead of the nose
    double hatch_width_m = 1.4;   // hatched width at the nose (tapers to a point)
    double stripe_width_m = 0.3;
    double pitch_m = 1.0;
    double angle_deg = 60;
    double ladder_pitch_m = 2.0;  // two-way approach: spacing of the ladder bars across the paint gap round the island
    std::string source;
};

// Paint wear for the renderer (Marking::wear): value noise plus a boost where a mark crosses the wheel
// tracks. strength 0 turns it off.
struct WearRule {
    double strength = 0.08;         // typical 0..1 wear (share of the paint eroded) of an untrodden mark: 5-15% on a normal road (reference photos); old minor roads 30-50%, see MarkingParams::wear_scale
    double patch_period_m = 60;     // wavelength of the low-frequency (old / new section) term
    double patch_weight = 0.5;      // share of `strength` that is the low-frequency term (rest is per-mark noise)
    double wheel_boost = 0.14;      // added where transverse marks and arrows cross the wheel tracks
    double wheel_offset_m = 0.8;    // wheel track centre from the lane centre
    double wheel_half_width_m = 0.25;
    std::string source;
};

struct MarkingRules {
    int version = 0;
    std::string speed_unit;  // "mph", "kph" or "ms" (required in the file)
    double min_mark_m = 0.3;  // no mark shorter than this (unless a style overrides)
    std::map<std::string, MarkingColour> colours;
    std::map<std::string, LineStyle> styles;
    std::map<std::string, RuleSet> sets;  // key = RoadType::markings
    GiveWayRule give_way;
    StopLineRule stop_line;
    ZebraRule zebra;
    ArrowRule arrows;
    GoreRule gore;
    RoundaboutEntryRule roundabout_entry;
    MouthEdgeRule mouth_edge;
    SplitterRule splitter;
    WearRule wear;
    // Lane types counted as a hard shoulder / strip (condition "shoulder").
    std::vector<std::string> shoulder_lane_types;

    const RuleSet* set(const std::string& id) const;
};

// Exception-free, validated (unknown style / colour references, non-positive widths, ...). On error
// `error` names the file and the offending rule.
Loaded<MarkingRules> parse_marking_rules(const std::string& json_text);
Loaded<MarkingRules> load_marking_rules(const std::filesystem::path& file);
// Convenience: <data_dir>/regions/<region_id>/marking_rules.json.
Loaded<MarkingRules> load_marking_rules_by_id(const std::filesystem::path& data_dir, const std::string& region_id);

// ---------------------------------------------------------------- generation

// Surface height callback: the z of the road surface at `lateral` (+ left of a -> b) and arc length `s`
// of `edge`, given the centreline z at that s. Marks follow it when set (the render mesh's crossfall and
// kerb heights), else the centreline z. Junction-arm marks pass the edge-frame s / lateral of the arm point.
using SurfaceHeightFn = std::function<double(EdgeId edge, double s, double lateral, double centre_z)>;

struct MarkingParams {
    double chord_tolerance = 0.01;  // m, LOD: plan chord error and vertical (z) chord error of every mark; use the render mesh's value (for_mesh_tolerance)
    // (junction node, arm edge) pairs that get a stop line instead of a give-way line (data says
    // where stop lines exist only when the player or a signal rule asks for them; v0 has no signals).
    std::set<std::pair<NodeId, EdgeId>> stop_arms;
    // Crossings the player has placed at junction arms (node, arm edge). Zebras are never automatic: with
    // the default (empty) no crossing marking is generated. The crossing position comes from
    // Junction::crossings for that arm.
    std::set<std::pair<NodeId, EdgeId>> crossing_arms;
    // Optional roundabout priority data (build_roundabout().give_way). When use_roundabout_give_way is
    // false every non-ring approach arm with inbound lanes of a Roundabout junction gets an entry line.
    bool use_roundabout_give_way = false;
    std::vector<GiveWay> roundabout_give_way;
    // Roundabout group results (find_roundabouts / apply_roundabouts) for splitter outlines and approach
    // hatching. Null = no splitter markings. Not owned; must outlive the call.
    const RoundaboutResult* roundabouts = nullptr;
    // Multiplies every mark's wear (WearRule): 1 = the region's default (5-15% of the paint eroded); about 4 gives
    // the 30-50% of an old minor road. Applies to the whole generation call.
    double wear_scale = 1.0;
    // Optional road surface height (see SurfaceHeightFn). Empty = centreline z.
    SurfaceHeightFn surface_height;

    // Params whose chord tolerance matches a render mesh built with `mesh_chord_tolerance`: marks must be
    // tessellated with the same tolerance as the mesh lanes they sit on (or tighter, with z from
    // surface_height), otherwise they drift off the lane in plan at coarse mesh LODs.
    static MarkingParams for_mesh_tolerance(double mesh_chord_tolerance) {
        MarkingParams p;
        p.chord_tolerance = mesh_chord_tolerance;
        return p;
    }
};

// Lines and bay dividers of one edge (everything between the junction boundaries / arm markings at its
// two ends). Unknown road type / marking set gives an empty set with a warning.
MarkingSet generate_edge_markings(const RoadGraph& graph, const RegionPack& region, const MarkingRules& rules,
                                  const JunctionSet& junctions, EdgeId edge, const MarkingParams& params = {});

// Give-way / stop / entry lines and triangles, zebra stripes, lane arrows, gore hatching and slip edge
// lines of one junction.
MarkingSet generate_junction_markings(const RoadGraph& graph, const RegionPack& region, const MarkingRules& rules,
                                      const Junction& junction, const MarkingParams& params = {});

// Splitter island outline and approach hatching of one roundabout (needs build_roundabout() results; skipped
// cleanly, giving an empty set, when the layout is not ok or the rule is absent).
MarkingSet generate_splitter_markings(const RoadGraph& graph, const RegionPack& region, const MarkingRules& rules,
                                      const JunctionSet& junctions, const RoundaboutLayout& layout, const MarkingParams& params = {});

struct MarkingResult {
    std::map<EdgeId, MarkingSet> edges;
    std::map<NodeId, MarkingSet> junctions;
    std::map<NodeId, MarkingSet> splitters;  // by the first ring node of the roundabout (needs MarkingParams::roundabouts)
    // All marks, edges then junctions, in key order (deterministic).
    MarkingSet merged() const;
};

// Builds markings for the given edges / junction nodes; both empty = the whole graph / every junction in
// the set.
MarkingResult generate_markings(const RoadGraph& graph, const RegionPack& region, const MarkingRules& rules,
                                const JunctionSet& junctions, const MarkingParams& params = {},
                                const std::vector<EdgeId>& only_edges = {},
                                const std::vector<NodeId>& only_nodes = {});

// Dirty rebuild into an existing result: regenerates the dirty edges, the edges at dirty nodes (their
// trims and approach zones depend on the junction) and the junction markings of dirty nodes and of the
// end nodes of dirty edges; removes entries for edges / junctions that no longer exist.
void rebuild_dirty_markings(MarkingResult& cache, const RoadGraph& graph, const RegionPack& region,
                            const MarkingRules& rules, const JunctionSet& junctions,
                            const std::vector<NodeId>& dirty_nodes, const std::vector<EdgeId>& dirty_edges,
                            const MarkingParams& params = {});

}  // namespace cp
