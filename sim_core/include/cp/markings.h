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

struct RuleSet {
    std::string id;
    std::vector<BoundaryRule> boundaries;
    std::vector<BayRule> bays;
};

struct GiveWayRule {
    bool enabled = false;
    StyleSel style;
    double setback_m = 0.5;  // junction trim -> near edge of the line
    double line_gap_m = 0.3; // longitudinal lines stop this far short of the line
    // triangle (outline of 3 trapezoids), apex towards approaching traffic
    bool triangle = false;
    std::string triangle_style;
    double triangle_gap_m = 1.0;     // line far edge -> triangle base
    double triangle_base_m = 1.8;
    double triangle_height_m = 3.0;
    double triangle_stroke_m = 0.2;
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
    MarkCond when;           // e.g. speed_max (zebras are not used on fast roads)
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

struct GoreRule {
    bool enabled = false;
    std::string style;          // stripes
    double stripe_width_m = 0.3;
    double pitch_m = 1.2;       // stripe repeat along the gore
    double angle_deg = 60;      // stripe angle from the gore's long axis
    double min_area_m2 = 0.05;
    StyleSel edge_style;        // empty / no match = no edge-line continuity through merge / diverge junctions
    double edge_inset_m = 0.225;  // edge line centre in from the lane edge
    std::string source;
};

struct RoundaboutEntryRule {
    bool enabled = false;
    std::string style;
    double setback_m = 0.5;
    double line_gap_m = 0.3;
    std::string source;
};

struct MarkingRules {
    int version = 0;
    std::string speed_unit = "mph";
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

    const RuleSet* set(const std::string& id) const;
};

// Exception-free, validated (unknown style / colour references, non-positive widths, ...). On error
// `error` names the file and the offending rule.
Loaded<MarkingRules> parse_marking_rules(const std::string& json_text);
Loaded<MarkingRules> load_marking_rules(const std::filesystem::path& file);
// Convenience: <data_dir>/regions/<region_id>/marking_rules.json.
Loaded<MarkingRules> load_marking_rules_by_id(const std::filesystem::path& data_dir, const std::string& region_id);

// ---------------------------------------------------------------- generation

struct MarkingParams {
    double chord_tolerance = 0.01;  // m, LOD: plan chord error and vertical (z) chord error of every mark
    // (junction node, arm edge) pairs that get a stop line instead of a give-way line (data says
    // where stop lines exist only when the player or a signal rule asks for them; v0 has no signals).
    std::set<std::pair<NodeId, EdgeId>> stop_arms;
    // Optional roundabout priority data (build_roundabout().give_way). When use_roundabout_give_way is
    // false every non-ring approach arm with inbound lanes of a Roundabout junction gets an entry line.
    bool use_roundabout_give_way = false;
    std::vector<GiveWay> roundabout_give_way;
};

// Lines and bay dividers of one edge (everything between the junction boundaries / arm markings at its
// two ends). Unknown road type / marking set gives an empty set with a warning.
MarkingSet generate_edge_markings(const RoadGraph& graph, const RegionPack& region, const MarkingRules& rules,
                                  const JunctionSet& junctions, EdgeId edge, const MarkingParams& params = {});

// Give-way / stop / entry lines and triangles, zebra stripes, lane arrows, gore hatching and slip edge
// lines of one junction.
MarkingSet generate_junction_markings(const RoadGraph& graph, const RegionPack& region, const MarkingRules& rules,
                                      const Junction& junction, const MarkingParams& params = {});

struct MarkingResult {
    std::map<EdgeId, MarkingSet> edges;
    std::map<NodeId, MarkingSet> junctions;
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
