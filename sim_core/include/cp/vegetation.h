#pragma once
// Vegetation PLACEMENT (headless, deterministic): decides WHERE plants go and outputs instance lists. It makes
// no meshes and knows no engine. Product framing (this is a game: the player places a road into an existing
// open world and ONLY that action is judged against reference photos; nothing is hand-dressed):
//
//   source 1  world_cover  The EXISTING WORLD's cover, there before any road: ground cover (open grass, rough
//                          grass, woodland floor), woodland blocks, scattered trees, field-boundary hedgerows
//                          (some carrying trees). A pure function of position and world seed, independent of
//                          roads, behind the replaceable WorldCoverSource interface (the seam for a real world
//                          generator); then CLEARED by the corridor mask below.
//   source 2  vegetation   ROAD-EDGE scatter that the road itself generates: verge grass strips (mown inner,
//                          rough outer), ragged creep over hard strips, small scrub on the verge, dual
//                          central-reservation grass, optional per-road-type street trees, roundabout island
//                          and splitter planting. All from region data (`vegetation` block of region.json).
//
//   corridor mask (layer veg_corridor, shared by both): from every road cross-section (+ its verge width, + half
//   the median gap on a dual's median side), junction polygons (+ verge), roundabout islands and a hook for the
//   graded earthworks footprint. clear(p) is 0 inside the corridor and ramps (smoothstep) to 1 over
//   `blend_band_m`; "tall" things (trees, hedges) are also forbidden in junction visibility triangles. This is
//   what makes a placed road look cut into its surroundings.
//
// Determinism and incremental == full (the WorldCache invariant, exact equality)
//  - Everything is a pure function of (graph, region data, world seed, VegetationParams). Seeds come from stable
//    ids / global lattice indices via mix64/hash01 (cp/geom.h), never from iteration order or tile borders.
//  - ASSIGN BY POSITION, GENERATE BY OBJECT: a tile owns the point instances whose position lies in it, and the
//    segments (hedge, strip) whose midpoint lies in it. Runs/strips crossing a tile border are split into
//    pieces that share their vertex and carry s0 (arc length of the piece start along the whole run) and the run
//    seed, so a renderer instancing modules along them produces the same result on both sides.
//  - world_cover is built per tile from scratch; only tiles that overlap (old or new corridor box + blend band)
//    of a changed object are rebuilt. So editing a road changes world cover only in its old and new corridor
//    plus blend band; deleting the road restores the original cover exactly (same seeds).
//  - vegetation (road edge) is built per object (edge, roundabout). Spatial dependency is handled by a
//    CONSERVATIVE WIDENING over stored object boxes: an object is rebuilt when its box grown by
//    VegetationRules::reach() meets the old or new box of any object that changed (the corridor model reports
//    them), plus the edges of dirty/rebuilt nodes and the run-mates (joined through 2-arm same-type nodes) of
//    those, because street-light phase (read for tree clearance) is laid out over a whole run. Street lights are
//    called as pure functions (generate_edge_street_lights), so the layer needs no later layer's state; it
//    inherits that layer's documented "roads >= 15 m apart" limit only for the light clearance of street trees.
//
// Z: world_cover uses VegetationParams::ground_height (default flat 0). Road-edge items use the road
// cross-section height clamped to its extent; the renderer should snap them to the final terrain.
//
// HOOK (out of scope, marked): scrub on earthwork slopes. Grading output is not read yet; see
// VegetationParams::grading_footprint (corridor clearing) and `slope_scrub` TODO in vegetation.cpp.
#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "cp/junction.h"
#include "cp/junction_roundabout.h"
#include "cp/region_pack.h"
#include "cp/render_mesh.h"
#include "cp/road_graph.h"
#include "cp/tile_key.h"
#include "cp/world_cache.h"

namespace cp {

inline constexpr std::string_view kLayerVegCorridor = "veg_corridor";  // state(): const CorridorModel*; no tiles
inline constexpr std::string_view kLayerWorldCover = "world_cover";     // tile_data(): VegetationTile
inline constexpr std::string_view kLayerVegetation = "vegetation";      // tile_data(): VegetationTile

// ------------------------------------------------------------------ output (plain structs)

// One plant. Position is tile-relative (add VegetationTile::origin, a double). Instances are grouped by type.
// Which of the three kinds of picture content an instance is (docs/research/vegetation-plan.md): WorldCover exists
// before any road; RoadEdge is created by a road; CutEdge is world cover standing at the cut face of a corridor.
enum class VegLayer : std::uint8_t { WorldCover, RoadEdge, CutEdge };
enum class LeafState : std::uint8_t { Leaf, Bare, Budding };

struct VegInstance {
    Float3 pos;                  // m relative to the tile origin
    float yaw = 0;               // rad CCW from +x
    float scale = 1;             // uniform
    float tilt = 0;              // rad, lean from vertical (small; saplings and tussocks lean more)
    float tint = 0.5f;           // 0..1 hue / health variation
    std::uint8_t size_class = 0; // from the type's data (0 small, 1 medium, 2 large)
    std::uint8_t variant = 0;    // 0 .. VegTypeDef::variants-1
    LeafState leaf_state = LeafState::Leaf;
    VegLayer layer = VegLayer::WorldCover;
    std::uint64_t seed = 0;      // stable per-instance seed (variation in the renderer)
};
struct VegInstanceGroup {
    std::string type_id;
    std::vector<VegInstance> instances;
};

enum class HedgeStyle : std::uint8_t { Trimmed, Overgrown };

// A hedge RUN: polyline a renderer instances modules along. Hedges exist only in world_cover (field boundaries).
struct HedgeRun {
    std::string type_id;
    HedgeStyle style = HedgeStyle::Trimmed;
    float height = 1.2f, width = 0.8f;  // m
    std::uint64_t seed = 0;             // the whole run's seed (same on every piece)
    double s0 = 0;                      // arc length along the source line at points[0]
    std::vector<Float3> points;         // tile-relative
};

// A ribbon between two polylines of equal length (verge strips): the renderer scatters inside the quads
// inner[i], inner[i+1], outer[i+1], outer[i]. `inner` is the side next to the road.
struct GrassStrip {
    std::string type_id;
    float density = 1;             // 0..1 relative to the type's full density
    std::uint8_t height_class = 0; // 0 mown .. 2 tall
    std::uint64_t seed = 0;
    double s0 = 0;                 // arc length along the road edge at inner[0]
    EdgeId edge;
    std::int8_t side = 0;          // +1 left of a -> b, -1 right
    std::vector<Float3> inner, outer;
};

// One cell of ground cover (world_cover): a square, adaptively subdivided (to min_cell) next to the corridor so
// that nothing grows inside it. `density` already includes the blend-band ramp.
struct GroundPatch {
    Float3 centre;
    float half_size = 8;
    float density = 1;
    std::uint8_t height_class = 0;
};
struct GroundPatchGroup {
    std::string type_id;
    std::vector<GroundPatch> patches;
};

// A planted polygon (roundabout island, splitter): scatter grass inside; CCW ring, tile-relative.
struct AreaPlanting {
    std::string type_id;
    float density = 1;
    std::uint8_t height_class = 0;
    std::uint64_t seed = 0;
    std::vector<Float3> ring;
};

struct VegetationTile {
    TileKey tile;
    Vec3 origin;                              // tile south-west corner, z = 0 (world doubles)
    std::vector<VegInstanceGroup> groups;     // by type_id (sorted)
    std::vector<HedgeRun> hedges;
    std::vector<GrassStrip> strips;
    std::vector<GroundPatchGroup> patches;    // by type_id (sorted)
    std::vector<AreaPlanting> areas;
    bool empty() const { return groups.empty() && hedges.empty() && strips.empty() && patches.empty() && areas.empty(); }
};

// Exact (bitwise for floats) content equality.
bool same_content(const VegetationTile& a, const VegetationTile& b);

// ------------------------------------------------------------------ region data

enum class VegKind { Grass, Hedge, Tree, Shrub };

struct VegTypeDef {
    std::string id;
    VegKind kind = VegKind::Grass;
    double height_min = 0, height_max = 0;  // m (hedge run height; plants larger than the sight height are "tall")
    double width_min = 0, width_max = 0;    // m (hedge width)
    double crown_radius = 0;                // m (trees, shrubs)
    int size_class = 0;
    int variants = 3;                        // renderer meshes per type
};

struct SightRow {
    double max_speed = 1e18;  // m/s, applies up to and including this speed
    double distance = 25;     // m
};
struct SightRules {
    double height = 1.05;  // m: types taller than this are kept out of junction visibility triangles
    std::vector<SightRow> by_speed;
    double distance_for(double speed_limit) const;  // m/s
};

struct VergeRule {
    double width = 0;          // m from the back of the footway / carriageway edge outwards (0 = none)
    double mown_width = 0;     // m of it (nearest the road) with mown_type; the rest is rough_type
    std::string mown_type, rough_type;
    int mown_height_class = 0, rough_height_class = 1;
    double density = 1;
    // Ragged growth at the carriageway edge: a weed_creep ribbon whose inner edge wobbles inward by up to
    // creep (over a hard strip / shoulder, limited to 40% of its width) plus creep_overrun (over the outermost
    // carriageway lane, only where no kerb or footway lies beyond it). Nothing else ever lies on a carriageway.
    double creep = 0, creep_overrun = 0, creep_density = 0.6;
    std::string creep_type;    // empty = no creep
    bool median_grass = true;  // dual types: grass over the half median gap on the median side
    std::string median_type;   // empty = rough_type
    int median_height_class = 2;
};
struct TreeClass {
    std::string type;
    double weight = 1;
};
struct ScrubRule {
    bool enabled = false;
    std::string type;
    double spacing = 12, probability = 0.3;
    double band_lo = 0.5, band_hi = 1.0;       // fraction of the verge width the scrub may occupy
    double scale_min = 0.8, scale_max = 1.3;
};
struct StreetTreeRule {
    bool enabled = false;
    double spacing = 18, spacing_jitter = 0.25;   // jitter as a fraction of spacing
    double setback = 2.5;            // m minimum from the carriageway edge to the trunk
    double offset_from_back = 0.6;   // m from the back of the footway / carriageway edge
    double lateral_jitter = 0.3;     // m
    int rows = 1;                    // >1 makes a BELT (dual carriageways, motorways): rows of trees/scrub
    double row_spacing = 6;          // m between rows
    double probability = 0.8;
    double paved_clearance = 1.0;    // m trunk to any other road surface
    double light_clearance = 3.0;    // m trunk to a street light column
    double structure_clearance = 4.0;// m from a bridge / tunnel span (along the road)
    double scale_min = 0.85, scale_max = 1.2;
    std::vector<TreeClass> classes;
};
struct RoadVegetationRule {
    VergeRule verge;
    ScrubRule scrub;
    StreetTreeRule street_trees;
};
struct IslandRule {
    bool enabled = true;
    std::string grass_type;          // empty = no grass area
    double grass_density = 1;
    int grass_height_class = 0;
    double inset = 0.4;              // m from the kerb
    std::string shrub_type;          // empty = none
    double shrub_spacing = 4, shrub_probability = 0.6;
    double min_area = 6;             // m2
    double tree_probability = 1;     // chance the island gets its tree
    std::string tree_type;           // one tree at the centre when the island radius (sqrt(area/pi)) is at least tree_min_radius
    double tree_min_radius = 0;      // 0 = never
};

// Parameters of the default world-cover generator: irregular fields from a warped Voronoi partition, woodland as
// ribbons, belts, copses, blocks and plantation cells (never on the field grid), varied hedges. Every one is region data.
struct WorldCoverRules {
    // Fields. Sites come from per-block rotated, stretched, jittered lattices (block_m squares); fields are the Voronoi cells
    // of all sites, then warped. Block scale, aspect and orientation vary block to block.
    double field_area_ha = 5;                        // mean field area
    double block_m = 450;                            // size of a block that shares one lattice
    double block_scale_min = 0.55, block_scale_max = 1.6;  // linear size multiplier per block
    double smallholding_share = 0.12, smallholding_scale = 0.4;  // blocks of small paddocks
    double aspect_min = 1.0, aspect_max = 2.6;       // field length : width per block
    double block_angle_var = 0.5;                    // per-block orientation scatter (perpendicular mix, ~ +-27 deg at 0.5)
    double site_jitter = 0.35;                       // lattice jitter, fraction of spacing (< 0.5)
    double regional_warp_m = 60, regional_warp_wavelength_m = 900;  // bends the whole pattern
    double boundary_wobble_m = 4, boundary_wobble_wavelength_m = 70;  // boundary irregularity
    // Land classes.
    double woodland_share = 0.04;                    // fraction of fields that are woodland plantation cells
    double woodland_edge_ragged_m = 8;               // plantation / woodland edge noise amplitude
    double arable_share = 0.4;                       // fraction of the non-wood fields in arable (no grass instances: the renderer's ground material)
    double rough_share = 0.1;                        // fraction of the non-wood fields in rough ground
    double arable_cluster = 0.3, arable_cluster_wavelength_m = 1500;  // arable share varies +-cluster over this wavelength
    // Other woodland shapes (each a pure function of position).
    double ribbon_band = 0.018;                      // value-noise isoline half band: valley-side ribbons (0 = none)
    double ribbon_wavelength_m = 900;
    double ribbon_gate = 0.55;                       // ribbons exist where a second noise exceeds this
    double belt_share = 0.12;                        // fraction of field boundaries that may carry a shelter belt
    double belt_width_min = 20, belt_width_max = 40; // m
    double belt_length_m = 400;                      // wavelength of the gate that starts and stops a belt
    double belt_gate = 0.5;                          // belt present where its gate noise exceeds this
    double copse_per_km2 = 3;                        // copses (1-3 ha rounded woods)
    double copse_area_min_ha = 1, copse_area_max_ha = 3;
    double wood_block_per_km2 = 0.4;                 // larger woods with ragged lobed edges
    double wood_block_area_min_ha = 15, wood_block_area_max_ha = 40;
    double wood_ragged = 0.3;                        // copse / block radial edge noise, fraction of radius
    // Hedges.
    double hedge_share = 0.7;                        // fraction of field boundaries with a hedge (the rest: fence or open, nothing emitted)
    double hedge_overgrown_share = 0.4;
    std::string hedge_trimmed_type, hedge_overgrown_type;
    double hedge_width_min = 0, hedge_width_max = 0; // m; 0 = the hedge type's own range. Skewed to the thin end.
    double hedge_tree_share = 0.3;                   // fraction of hedges carrying trees
    double hedge_tree_spacing = 60, hedge_tree_jitter = 0.4;
    double hedge_gap_per_km = 4, hedge_gap_min = 4, hedge_gap_max = 6;  // gateways
    double hedge_decay_per_km = 0, hedge_decay_min = 15, hedge_decay_max = 45;  // missing stretches
    double scatter_trees_per_ha = 2;                 // in open and rough fields
    double arable_tree_factor = 0.2;                 // scattered tree density in arable relative to pasture
    double woodland_trees_per_ha = 250;
    std::vector<TreeClass> tree_classes;             // open fields and hedges
    std::vector<TreeClass> woodland_classes;         // empty = tree_classes
    double tree_scale_min = 0.8, tree_scale_max = 1.25;
    std::string open_grass_type, rough_grass_type, woodland_floor_type;
    double open_density = 1, rough_density = 0.9, woodland_density = 0.5;
    int open_height_class = 0, rough_height_class = 1, woodland_height_class = 1;
    double ground_cell = 16, min_cell = 2;          // m: base ground-patch size and the finest size at the corridor edge
};

struct VegetationRules {
    std::vector<VegTypeDef> types;
    SightRules sight;
    double blend_band = 3.0;        // m, density ramp 0 -> 1 outside the corridor
    double clear_margin = 1.5;      // m beyond the corridor edge that is cleared hard (hedges end here: a cut face)
    double cut_edge = 4.0;          // m: world cover closer than this to the corridor edge is tagged CutEdge
    double junction_clearance = 30; // m: no tall world cover this close to roundabout rings, islands and ramps
    RoadVegetationRule default_road;
    std::map<std::string, RoadVegetationRule> road_types;  // by road type id; unlisted ones use default_road
    IslandRule island, splitter;
    WorldCoverRules world_cover;

    const VegTypeDef* type(const std::string& id) const;
    const RoadVegetationRule& for_road(const std::string& road_type) const;
    // How far (m) from an edge's cross-section any of its own items can lie, plus clearances: the conservative
    // widening radius of the incremental update.
    double reach() const;
    // The clear-margin / blend-band curve: 0 up to the margin, then smoothstep over blend_band.
    double density_at(double signed_distance) const;
};

// Parses the `vegetation` object (the text of region.json, or of a standalone vegetation.json holding a
// top-level "vegetation" key). Errors name the offending key.
Loaded<VegetationRules> parse_vegetation_rules(const std::string& json_text, const RegionPack* region = nullptr);
// <region_dir>/vegetation.json if present, else region.json.
Loaded<VegetationRules> load_vegetation_rules(const std::filesystem::path& region_dir, const RegionPack* region = nullptr);
Loaded<VegetationRules> load_vegetation_rules_by_id(const std::filesystem::path& data_dir, const std::string& region_id,
                                                    const RegionPack* region = nullptr);

// ------------------------------------------------------------------ the world-cover seam

// UNMASKED entities of the existing world. A source must be a pure function of position and its own seed
// (query boxes only select; the same entity is produced identically whatever box asked for it).
struct CoverPoint {
    std::string type_id;
    Vec2 pos;
    double yaw = 0, scale = 1;
    std::uint8_t size_class = 0;
    std::uint64_t seed = 0;
    bool tall = false;  // keep out of visibility triangles
};
struct CoverLine {
    std::string type_id;
    HedgeStyle style = HedgeStyle::Trimmed;
    float height = 1.2f, width = 0.8f;
    std::uint64_t seed = 0;
    std::vector<Vec2> points;
    std::vector<double> s;  // arc length of each point along this line
};
// Land class of a position (what kind of ground it is), independent of which ground type renders it.
enum class LandClass : std::uint8_t { Pasture, Arable, Rough, Wood };
struct CoverGround {
    std::string type_id;
    double density = 1;
    std::uint8_t height_class = 0;
    LandClass land = LandClass::Pasture;
};

// HOW A REAL WORLD GENERATOR PLUGS IN: implement this interface (points from its tree/forest data, lines from
// its field boundaries / hedgerow data, ground_at from its land-cover map), return it from your own factory and
// pass it in VegetationParams::cover. Nothing else changes: clearing, blending, tiling, change tracking and
// the instance/run/patch output kinds are the layer's job.
class WorldCoverSource {
public:
    virtual ~WorldCoverSource() = default;
    // Every point entity with lo <= pos < hi.
    virtual void points_in(Vec2 lo, Vec2 hi, std::vector<CoverPoint>& out) const = 0;
    // Every whole line that has a point in (or within a few metres of) the box. Lines are whole: s counts from
    // the line's own start.
    virtual void lines_touching(Vec2 lo, Vec2 hi, std::vector<CoverLine>& out) const = 0;
    // Ground cover at a position.
    virtual CoverGround ground_at(Vec2 p) const = 0;
};

// Field cell statistics of a default source (empty for other sources): the unclipped cells whose site lies in [lo, hi).
void default_cover_field_stats(const WorldCoverSource& src, Vec2 lo, Vec2 hi, std::vector<double>& area_ha, std::vector<double>& aspect);

// Optional terrain hints for the default source (pure functions; evaluated at block centres only).
struct CoverHints {
    // Preferred long-axis direction of fields near a position (unit vector); return false for "no preference".
    // A terrain-aware world returns the contour direction on slopes and the valley direction near a valley.
    std::function<bool(Vec2, Vec2&)> field_axis;
};

// The default: irregular warped-Voronoi fields with hedgerows, woodland ribbons / belts / copses / blocks, scattered trees.
std::shared_ptr<const WorldCoverSource> make_default_cover_source(std::shared_ptr<const VegetationRules> rules,
                                                                  std::uint64_t world_seed, CoverHints hints = {});

// ------------------------------------------------------------------ corridor mask

struct Aabb2 {
    Vec2 lo{1e300, 1e300}, hi{-1e300, -1e300};
    bool valid() const { return lo.x <= hi.x && lo.y <= hi.y; }
};

class CorridorModel {
public:
    struct Impl;
    CorridorModel();
    ~CorridorModel();
    CorridorModel(CorridorModel&&) noexcept;
    CorridorModel& operator=(CorridorModel&&) noexcept;

    // Full build (tests, tools). The layer uses rebuild()/update().
    static CorridorModel build(const RoadGraph& graph, const RegionPack& region, const VegetationRules& rules,
                               const JunctionSet& junctions, const std::vector<RoundaboutLayout>& roundabouts,
                               std::function<double(Vec2)> footprint_hook = {});

    // Signed distance (m) to the corridor union: < 0 inside the area that is cleared of world cover, values above
    // `cap` (<= 64) are reported as cap. Lipschitz-1 (usable for adaptive subdivision).
    double signed_distance(Vec2 p, double cap = 64) const;
    // Distance to PAVED surface only (cross-section, junction polygons, islands; no verge), skipping one edge.
    double paved_distance(Vec2 p, EdgeId exclude = {}, double cap = 64) const;
    bool in_sight_triangle(Vec2 p) const;
    // Tall things (trees, hedges) are forbidden here: a visibility triangle, or within junction_clearance of a roundabout / ramp.
    bool tall_blocked(Vec2 p) const;
    // Density multiplier 0..1: smoothstep(signed_distance / blend_band); `tall` also 0 inside visibility triangles.
    double clear(Vec2 p, bool tall = false) const;
    double band() const;

    // Layer interface.
    struct Inputs {
        const RoadGraph* graph = nullptr;
        const RegionPack* region = nullptr;
        const VegetationRules* rules = nullptr;
        const JunctionSet* junctions = nullptr;
        const std::vector<RoundaboutLayout>* roundabouts = nullptr;
    };
    void rebuild(const Inputs& in, std::function<double(Vec2)> footprint_hook);
    // Re-evaluates the candidate edges (and removed ones) and every junction / island; records the old and new
    // boxes of everything that really changed in dirty().
    void update(const Inputs& in, const std::vector<EdgeId>& candidate_edges);
    bool all_dirty() const;                  // last run was a full rebuild
    const std::vector<Aabb2>& dirty() const; // boxes (corridor extent) of objects whose geometry changed, old and new
    // Edge records (for the road layer and tests).
    std::vector<EdgeId> edge_ids() const;
    Aabb2 edge_box(EdgeId e) const;
    std::vector<NodeId> island_ids() const;  // first ring node of each ok roundabout
    Aabb2 island_box(NodeId first) const;

    const Impl& impl() const { return *impl_; }

private:
    std::unique_ptr<Impl> impl_;
};

// ------------------------------------------------------------------ layers

struct VegetationParams {
    std::uint64_t world_seed = 1;
    // World extent that world_cover fills (m, global frame). Small worlds only: everything inside is generated.
    Vec2 world_min{-1024, -1024}, world_max{1024, 1024};
    // Terrain height for world cover (x, y -> z); null = flat 0. The terrain is not owned here.
    std::function<double(double, double)> ground_height;
    // Hook for the graded earthworks footprint: signed distance (m, < 0 inside) of the area grading will change,
    // added to the corridor. Changing it needs WorldCache::rebuild_all() (grading will become a layer later).
    std::function<double(Vec2)> grading_footprint;
    // Source of the existing world's cover; null = make_default_cover_source(rules, world_seed).
    std::shared_ptr<const WorldCoverSource> cover;
    bool world_cover = true;  // register the world_cover layer
    bool road_edge = true;    // register the vegetation layer
    LeafState leaf_state = LeafState::Leaf;  // season, written on every tree instance
};

// Registers veg_corridor, then world_cover and vegetation (after the built-in layers). False + error on a name clash.
bool add_vegetation_layers(WorldCache& cache, std::shared_ptr<const VegetationRules> rules, VegetationParams params = {},
                           std::string* error = nullptr);

}  // namespace cp
