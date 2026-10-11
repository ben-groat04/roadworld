#pragma once
// Region pack loading (data/lane_types.json + data/regions/<id>/*.json) into plain
// structs. The core assumes nothing about any region: every value comes from here.
// Internal units: metres, m/s, radians; grades are rise over run (not %).
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace cp {

enum class DrivingSide { Left, Right };

// Travel direction of a lane relative to the edge's a -> b direction.
enum class TravelDir { None, Forward, Backward };

enum class SlotSide { Nearside, Offside };

struct LaneType {
    std::string id;
    std::vector<std::string> users;
    bool may_park = false;
    bool connects_at_junctions = false;
    std::string surface;
    // Cross-section role for the render mesh builder (optional "profile" in the data): carriageway
    // (default; road surface with crossfall), kerb (raised upstand strip), raised (footway at kerb-top
    // level) or verge (falls away from the carriageway).
    std::string profile = "carriageway";
};

// One strip of a cross-section. `dir` is resolved at load: an explicit "dir" wins,
// else lanes that carry cars and cannot park are Forward, everything else None.
// For a `half`, Forward means "this half's own direction of travel".
// `side` is the optional "side" field from the data (informational: slot order,
// offside -> nearside for carriageways and one-way variants, decides the layout).
struct CrossSectionSlot {
    std::string lane_type;
    double width = 0;  // m
    TravelDir dir = TravelDir::None;
    SlotSide side = SlotSide::Nearside;
};

// How street light columns are laid out along a road ("street_lights" object of a road type; docs:
// sim_core/include/cp/street_lights.h). `staggered` and `single_side` spacing is between consecutive
// columns along the road; `opposite` spacing is between stations (a column on each side at every one).
enum class LightArrangement {
    SingleSide,          // nearside of the run direction (outside of the bend on tight curves)
    Staggered,           // alternating sides, nearside first
    Opposite,            // a column on each side at every station
    CentralReservation,  // dual types: twin-arm columns in the median between two carriageways
};

struct StreetLightRules {
    bool lit = false;
    double spacing = 30;             // m
    LightArrangement arrangement = LightArrangement::SingleSide;
    double setback = 0.8;            // m, column centre from the carriageway edge when there is no footway
    double footway_inset = 0.3;      // m, column centre in from the back of the footway
    double mounting_height = 8;      // m, luminaire height above the column base
    double outreach = 1.0;           // m, bracket / arm length toward the carriageway
    std::string style;               // renderer mesh id
    // Roundabout ring types: > 0 adds one column on the central island when the island radius is at
    // least this (m). 0 = never.
    double island_min_radius = 0;
};

struct RoadType {
    std::string id;
    std::string name;
    bool two_way = false;
    bool dual = false;
    double speed_limit = 0;   // m/s
    double design_speed = 0;  // m/s
    int road_class = 0;        // relative importance (higher = more major); 0 = unset (use design speed in km/h)
    double corner_radius = 0;  // m, default junction kerb fillet radius; 0 = unset (not applicable)
    double median_gap = 0;    // m, dual types only
    // half: centre -> nearside, one direction (two-way types).
    // one_way_variant, carriageway: offside -> nearside, whole cross-section.
    std::vector<CrossSectionSlot> half;
    std::vector<CrossSectionSlot> one_way_variant;
    std::vector<CrossSectionSlot> carriageway;
    double vertical_k_crest = 0;   // m per % grade change
    double vertical_k_sag = 0;
    double min_radius = 0;          // m, advisory (0 = unset)
    double advisory_max_grade = 0;  // rise/run, advisory (0 = unset)
    std::string markings;
    // Render cross-section shape (all optional in the data; unset = the mesh builder default).
    // Crossfalls are rise over run (positive = falls away from the crown / toward the nearside).
    std::optional<double> crossfall;          // "crossfall": camber (two-way) or fall to the nearside
    std::optional<double> kerb_upstand;       // "kerb_upstand_m": kerb height above the carriageway edge
    std::optional<double> footway_crossfall;  // "footway_crossfall": footway falls toward the carriageway
    std::optional<double> verge_crossfall;    // "verge_crossfall": verge falls away from the carriageway
    // True for the circulatory carriageway of a roundabout: a one-way type whose closed loops form a
    // roundabout group (docs/junctions.md section 7). Never set on two_way or dual types.
    bool roundabout_ring = false;
    StreetLightRules street_lights;  // optional "street_lights" object; unset = unlit
};

// One ring-size class of the roundabout rules (region.json roundabout.size_classes). The class for a
// ring is the first whose max_radius is not below the ring centreline radius.
struct RoundaboutSizeClass {
    double max_radius = 1e18;            // m, ring centreline radius up to which this class applies
    double entry_corner_radius = 8.0;    // m, kerb fillet between an approach and the ring-out edge (tight: slows entries)
    double exit_corner_radius = 12.0;    // m, between the ring-in edge and an approach (generous: easy exits)
    double splitter_length = 12.0;       // m, base splitter island length before the approach-speed factor
    double splitter_width = 1.8;         // m, island width at the ring end (each side's kerb spreads out by half of it)
    double apron_width = 0.0;            // m, over-run apron collar round the central island (0 = none)
    std::string splitter_surface;        // render surface of this class's splitter islands; empty = RoundaboutRules::splitter_surface
};

// Splitter length factor by approach speed (region.json roundabout.splitter_speed_factors).
struct SpeedFactor {
    double max_speed = 1e18;  // m/s, applies up to and including this approach speed limit
    double factor = 1.0;
};

// Roundabout tool and layout tunables (region.json "roundabout" object). The values here are the
// fallbacks used when a pack omits a field; they are not tied to any region.
struct RoundaboutRules {
    double entry_corner_radius = 8.0;        // m, kerb fillet between an approach and the ring-out edge
    double exit_corner_radius = 12.0;        // m, between the ring-in edge and an approach
    double ring_corner_radius_factor = 1.0;  // island-side fillet = factor * ring radius
    double max_arc = 1.62;                   // rad, longest ring edge the tool makes (about 93 degrees)
    int max_ring_edges = 16;                 // tool: most edges in the ring it draws
    double min_island_radius = 3.0;          // m, smallest central island (layout and tool)
    double min_edge_length = 4.0;            // m, shortest ring edge or approach stub the tool makes
    double min_tool_radius = 8.0;            // m, tool: smallest centreline radius
    double max_tool_radius = 100.0;          // m, tool: largest centreline radius
    double splitter_length = 12.0;           // m beyond the trim line, two-way approaches only
    double splitter_width = 1.8;             // m at the ring end
    // Render surfaces (lane-type surface class names, e.g. "grass", "paving") and kerb height.
    std::string central_island_surface = "grass";  // planted central island
    std::string splitter_surface = "asphalt";      // splitter islands on the approaches (a size class may override it)
    double island_kerb_upstand = 0;                // m; 0 = the render default
    // Size classes (entry/exit radii, splitter and apron by ring radius). Empty = one class made from the
    // scalar fields above (entry_corner_radius, exit_corner_radius, splitter_length, splitter_width).
    std::vector<RoundaboutSizeClass> size_classes;
    std::vector<SpeedFactor> splitter_speed_factors;  // empty = factor 1
    double splitter_min_taper = 8.0;                  // m, shortest taper a splitter island may be squeezed to when space is short
    double apron_island_radius_max = 0;               // m; the apron is added when the island radius is below this
    std::string apron_surface = "paving";             // render surface of the over-run apron

    // The size class of a ring with this centreline radius.
    RoundaboutSizeClass size_class(double ring_radius) const;
    // Splitter length factor for an approach with this speed limit (m/s).
    double splitter_speed_factor(double speed_limit) const;
};

// One default kerb-radius rule of the junction rules: matches when the road class of the less
// important arm lies in [minor_min, minor_max] and that of the more important arm in [major_min, major_max].
struct CornerRadiusRule {
    int minor_min = 0, minor_max = 1 << 30;
    int major_min = 0, major_max = 1 << 30;
    double radius = 0;  // m
};

// Turn pockets (ghost-island offside-turn lanes) on priority junctions: region.json junction.turn_pockets.
struct TurnPocketRules {
    bool enabled = false;
    int major_class_min = 85;          // the through road's class must be at least this for a default pocket
    int minor_class_max = 1 << 30;     // the minor road's class must be at most this
    double lane_width = 3.25;          // m, width of the pocket lane
    double max_bend = 0.12;            // rad, the through road may turn by at most this at the junction
    double min_minor_angle = 1.0;      // rad, the minor arm must leave the major road at least this far from straight on
    double max_minor_angle = 2.14;     // ... and at most this (about 60 to 120 degrees)
    double min_taper_ratio = 8.0;      // 1:N, shortest taper the pocket may be squeezed to when space is short
    double min_storage = 10.0;         // m, shortest pocket lane (storage plus deceleration) accepted
    std::vector<SpeedFactor> taper_ratio;   // N of the 1:N taper by the through road speed limit (factor = N)
    std::vector<SpeedFactor> storage;       // m of pocket lane by the through road speed limit (factor = metres)
    // Used only when the matching table above is empty.
    double default_storage = 20.0;          // m ("default_storage_m")
    double default_taper_ratio = 15.0;      // N of the 1:N taper ("default_taper_ratio")
    double lookup(const std::vector<SpeedFactor>& table, double speed_limit, double fallback) const;
};

// Merge / diverge geometry (region.json junction.merge).
struct MergeRules {
    // Taper length (m) of the odd arm by the main carriageway design speed (factor = metres); the last row is the
    // open end. Empty = default_taper_length.
    std::vector<SpeedFactor> taper_length;
    double default_taper_length = 75.0;  // m ("default_taper_length_m"), used only when the table is empty
    double gore_length = 10.0;           // m of gore triangle beyond the nose point ("gore_length_m")
    double taper_length_for(double design_speed) const;  // design_speed in m/s
};

// Junction shaping rules (region.json "junction" object).
struct JunctionRules {
    double default_corner_radius = 6.0;  // m, kerb radius when neither the class table nor the road types give one
    MergeRules merge;
    // Default corner radius by the pair of road classes meeting; first match wins, none = the larger of the two
    // road types' own corner_radius. A player override always wins.
    std::vector<CornerRadiusRule> corner_radius;
    // Skewed corners: the radius is scaled by (corner angle / 90 degrees) clamped to [skew_min, skew_max]
    // (larger on the obtuse corner, smaller on the acute one). 1 and 1 = off.
    double skew_min = 1.0, skew_max = 1.0;
    TurnPocketRules turn_pockets;

    // Radius for arms of these road classes (0: no rule matched) and the skew factor for a corner angle.
    double corner_radius_for(int class_a, int class_b) const;
    double skew_factor(double angle) const;
};

// Ground grading and earthwork limits (region.json "grading" object, used by grading.h). The values
// here are the fallbacks used when a pack omits a field; they are not tied to any region.
struct GradingRules {
    double formation_drop = 0.15;      // m, formation level below the road surface
    double batter = 2.0;               // horizontal : vertical of cutting/embankment slopes
    double verge = 1.5;                // m, flat graded verge beside the road edge before the batter starts
    double wall_thickness = 1.5;       // m, solid retaining wall beside the road edge
    double wall_start = 12.0;          // m, slope height above which a retaining wall replaces the slope
    double wall_cap = 12.0;            // m, height above which the road needs a bridge or tunnel
    double wall_slack = 0.75;          // m, a slope may exceed wall_start by this much before a wall is needed
    double tunnel_clear_height = 5.5;  // m, tunnel springline height above the road (side-wall headroom)
    // retaining walls and earthworks (docs/planning-log.md, grading round 4)
    double wall_max_height = 3.0;      // m, tallest single retaining wall; taller needs wall + slope, benching, or (flagged) exceeds it
    double bench_min_width = 4.5;      // m, berm between two stacked walls, from the back of the lower wall to the face of the upper one
    double wall_min_length = 4.0;      // m, a wall run shorter than this (one panel) becomes a slope
    double toe_ditch_depth = 0.4;      // m, shallow ditch between verge and slope at the toe of a cutting (0 disables)
    double toe_ditch_width = 1.5;      // m
    double end_taper_batter = 4.0;     // horizontal : vertical of the run-out beyond a free road end (embankment eases into the ground along the road)
    double wing_max = 10.0;            // m, longest wing wall beside a portal / abutment
    double wing_ratio = 1.0;           // m of wing wall per m of retained height at the structure
    double wing_min_height = 0.3;      // m, no wing wall where the ground beside the structure is within this of formation
    double round_daylight = 2.0;       // m (horizontal), roll-over of slope tops / toes into natural ground
    double round_hinge = 1.0;          // m (horizontal), parabolic verge -> slope hinge
    // steep ground (grading round 5): at most max_tiers walls; beyond that a rock cut (cut side) or one tall wall / steep fill (fill side)
    int max_tiers = 2;                 // stacked walls per side at most
    double wall_slope_extra = 15.0;    // m, the slope above / below a wall may run this much farther than a plain slope may before it must daylight
    double berm_slope = 0.25;          // v / h, the strip between two tiers climbs (cut) / falls (fill) away from the lower wall
    double wall_hard_max = 6.0;        // m, tallest single fill wall; beyond it needs_structure (viaduct / half-bridge)
    double rock_cut_batter = 0.5;      // h per 1 v of an engineered rock-cut face
    double rock_berm_width = 3.0;      // m, catch berm between rock-cut faces
    double rock_bench_height = 9.0;    // m, height of one rock-cut face between berms
    double fill_steep_batter = 1.5;    // h per 1 v, steepest stable fill slope graded where a wall cannot hold the fill
    double corridor_max = 90.0;        // m beyond the footprint edge up to which two-tier walls / rock cuts may extend
    double wall_vis_min = 0.35;        // m, a terraced wall lower than this is not built
    double reach_fade = 8.0;           // m, a slope that has not met the ground by the end of the road's reach fades into it over this run
};

// Structure dimensions (region.json "structures" object, used by structure_mesh.h). The values here are the
// fallbacks used when a pack omits a field; they match StructureParams defaults.
struct StructureRules {
    double parapet_height = 1.0;        // m
    double max_pier_spacing = 30.0;     // m, longest clear span between supports
    double min_bridge_length = 20.0;    // m, shorter flagged spans are solid blocks unless over a road or water
    double pier_road_clearance = 2.0;   // m, footing clearance to another road's cross-section
    double wall_panel = 4.0;            // m, retaining wall panel length
    double tunnel_rise_k = 0.2;         // tunnel arch rise as a fraction of the half-width
    double coping_thickness = 0.15;     // m
    double coping_overhang = 0.05;      // m
    double wall_face_thickness = 0.4;   // m, the visible wall itself (grading reserves wall_thickness)
    double wall_joint_width = 0.12;     // m along the wall, pilaster at each panel boundary
    double wall_joint_proud = 0.04;     // m the pilaster stands out of the face
    bool wall_allow_short_panels = false;  // true: panels are halved where the step limit is exceeded
};

// Render cross-section shape used when a road type leaves the field unset (region.json "render" object;
// RoadType::crossfall etc. win). Crossfalls are rise over run.
struct RenderRules {
    double crossfall = 0.025;           // "crossfall"
    double kerb_upstand = 0.125;        // m, "kerb_upstand_m"; also the roundabout island kerb when island_kerb_upstand is 0
    double footway_crossfall = 0.02;    // "footway_crossfall": footways fall toward the carriageway
    double verge_crossfall = 0.05;      // "verge_crossfall": verges fall away from the carriageway
};

struct RegionPack {
    std::string id;
    int version = 0;
    DrivingSide driving_side = DrivingSide::Left;
    // RESERVED: read from "headroom_m" and kept, but nothing uses it yet. It is the minimum vertical clearance of a
    // bridge soffit over a road; the structure clearance checks (structure_mesh / grading) will read it. Today those
    // use StructureRules::pier_road_clearance and the soffit logic instead.
    double headroom_m = 0;
    RenderRules render;
    double plan_crossing_junction_dz_m = 0;
    RoundaboutRules roundabout;
    JunctionRules junction;
    GradingRules grading;
    StructureRules structures;
    std::map<std::string, LaneType> lane_types;
    std::map<std::string, RoadType> road_types;

    const LaneType* lane_type(const std::string& id) const;
    const RoadType* road_type(const std::string& id) const;
};

// Value-or-error result; `error` is a human-readable message when there is no value.
template <class T>
struct Loaded {
    std::optional<T> value;
    std::string error;
    explicit operator bool() const { return value.has_value(); }
    T& operator*() { return *value; }
    const T& operator*() const { return *value; }
    T* operator->() { return &*value; }
    const T* operator->() const { return &*value; }
};

using LaneTypeTable = std::map<std::string, LaneType>;

// String-based parsers (testable without files).
Loaded<LaneTypeTable> parse_lane_types(const std::string& json_text);
Loaded<RegionPack> parse_region_pack(const LaneTypeTable& lane_types, const std::string& region_json,
                                     const std::string& road_types_json);

// File-based loaders.
Loaded<LaneTypeTable> load_lane_types(const std::filesystem::path& file);
// region_dir contains region.json and road_types.json.
Loaded<RegionPack> load_region_pack(const std::filesystem::path& lane_types_file,
                                    const std::filesystem::path& region_dir);
// Convenience: <data_dir>/lane_types.json + <data_dir>/regions/<region_id>/.
Loaded<RegionPack> load_region_pack_by_id(const std::filesystem::path& data_dir, const std::string& region_id);

}  // namespace cp
