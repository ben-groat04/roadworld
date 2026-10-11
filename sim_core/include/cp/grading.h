#pragma once
// ONE graded ground surface for the whole road network (docs/planning-log.md, "Elevation and
// terrain"; port of prototypes/road-draw/src/earthworks.js). Roads never modify the natural
// terrain: a pure, tile-based function turns natural heights plus the roads' sampled centrelines
// into graded heights on a fine grid (0.5 m), which the renderer draws as a height-texture
// clipmap. Retaining walls, bridges and tunnel portals are the only meshes; every ground step
// is hidden inside one of them. This is a faithful port: the prototype's behaviour (including
// its oddities, listed in the .cpp) is reproduced, checked against goldens made by running the
// JS (tests/data/grading, tools in prototypes/road-draw/tools/grade_golden.mjs).
//
// Pipeline (all steps pure; thread-safe on distinct data):
//   1. roads = sample_graph_for_grading(graph, params)         (or fill SampledRoad directly)
//   2. classify_road(road, ground, params, mode) per road      (structures, wall flags)
//   3. prepare_grading_roads(roads, ground, params)            (medians, wall weights, reach, planes)
//   4. grade_tile(prepared, params, tile, natural heights)     (per tile, any thread)
//   5. wall_runs / tunnel_volumes / pack_tunnels               (structure meshes, terrain shader)
// `ground(x, y)` is the natural ground height (e.g. ground_fn(heightfield)); world coordinates
// are doubles, texel heights are float within a tile.
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "cp/heightfield.h"
#include "cp/region_pack.h"
#include "cp/tile_key.h"
#include "cp/road_graph.h"
#include "cp/vec.h"

namespace cp {

// Tunnel bore (all metres). Inner clear profile: vertical side walls from the road level up to
// clear_height (the springline), then a circular arch rising rise_k * half-width to the crown.
// The excavation (terrain discard) profile is the inner one offset by lining + margin.
struct TunnelParams {
    double lining = 0.6;
    double margin = 0.15;
    double floor_depth = 1.2;   // flat floor of the bore below the road level
    double rise_k = 0.2;        // arch rise as a fraction of the half-width
    double clear_height = 5.5;  // springline height above the road (GradingRules::tunnel_clear_height)
    double head_t = 2.0;        // headwall thickness: the one-texel ramp at a portal lies inside it
    int chunk = 16;             // segments per row of the packed tunnel table
};

// Every tunable of grading. Defaults are the prototype's values. The first block comes from
// the region pack (GradingParams::from_rules); the rest are algorithm constants, kept here so
// nothing is hidden. "Samples" are the road's sample stations, nominally sample_step apart.
struct GradingParams {
    // ---- region ----
    double formation_drop = 0.15;  // m, formation level below the road surface
    double batter = 2.0;           // horizontal : vertical of slopes
    double verge = 1.5;            // m, flat verge at formation level before the batter starts
    double wall_thickness = 1.5;   // m, solid retaining wall (lateral [hw, hw + thickness])
    double wall_start = 12.0;      // m, slope height above which a wall replaces the slope
    double wall_cap = 12.0;        // m, wall height above which bridge / tunnel is required
    double wall_slack = 0.75;      // m, a slope may be this much higher than wall_start before a wall is needed
    double wing_max = 10.0;        // m, longest wing wall beside a portal / abutment
    double round_daylight = 2.0;   // m (horizontal), roll-over of slope tops / toes into natural ground
    double round_hinge = 1.0;      // m (horizontal), parabolic verge -> slope hinge
    double wing_min_h = 0.3;       // m, no wing wall where the ground beside the structure is this close to formation
    double wall_max_height = 3.0;  // m, tallest single retaining wall (wall + slope, benching, or flagged over_height beyond it)
    double bench_min_width = 4.5;  // m, berm between two stacked walls (back of the lower wall to the face of the upper one)
    int max_tiers = 2;             // stacked walls per side at most; beyond that: rock cut (cut side) / one tall wall (fill side)
    double wall_slope_extra = 15.0;  // m, the slope above / below a wall may run this much farther than a plain slope may before it must daylight
    double berm_slope = 0.25;      // v / h, the strip between two tiers climbs (cut) / falls (fill) away from the lower wall at this slope
    double wall_hard_max = 6.0;    // m, tallest single fill wall; beyond it the station needs a structure (viaduct / half-bridge): needs_structure
    double rock_cut_batter = 0.5;  // h per 1 v of an engineered rock-cut face
    double rock_berm_width = 3.0;  // m, catch berm between rock-cut faces
    double rock_bench_height = 9.0;  // m, height of one rock-cut face between berms
    double fill_steep_batter = 1.5;  // h per 1 v, the steepest stable fill slope graded where a wall cannot hold the fill (needs_structure)
    double wall_min_length = 4.0;  // m, shorter wall runs become slope
    double toe_ditch_depth = 0.4;  // m, ditch between verge and slope at the toe of a cutting (0 disables)
    double toe_ditch_width = 1.5;  // m (the ditch is dug into the verge-to-slope hinge; the slope itself does not move)
    double end_taper_batter = 4.0; // horizontal : vertical run-out beyond a free road end
    double corridor_max = 90.0;    // m beyond the footprint edge up to which two-tier walls / rock cuts may extend
    double wall_vis_min = 0.35;    // m, a terraced wall lower than this is not built
    double reach_fade = 8.0;       // m, a plain slope that has not met the ground by the end of the road's reach fades into it over this run
    // ---- sampling ----
    double sample_step = 2.0;      // m, adapter centreline sampling step
    // ---- structures and walls (sample counts) ----
    int structure_gap = 8;         // auto bridge / tunnel: gaps shorter than this are filled
    int structure_min_run = 8;     // auto bridge / tunnel: runs shorter than this are dropped
    int wall_gap = 8;              // gaps between wall runs shorter than this are bridged
    int wall_min_run = 8;          // a wall run is at least this long (grown, never dropped)
    double wing_ratio = 1.0;       // m of wing wall per m of retained height at the structure
    int wall_pad = 5;              // wall runs are padded by this much beyond the stations that need them
    double wall_ramp = 11.0;       // m, wall weight blends 0 -> 1 over this run from a free end
    // ---- portals and reach ----
    double plane_over = 0.75;      // m, grading stops this far past a portal plane
    double plane_margin = 4.0;     // m, a portal's plane test covers segments within (road reach + this) of path length
    double reach_margin = 2.0;     // m, added to the wall-start reach (reach cap, which also covers wall_slack and the roll-overs)
    double box_margin = 1.0;       // m, road bounding box margin beyond the reach cap
    double reach_pad = 2.5;        // m, added to the measured daylight reach of unwalled roads
    TunnelParams tunnel;

    static GradingParams from_rules(const GradingRules& rules);
};

// ---- wall plans (stacked retaining walls; see earthworks.js planSide) ----

constexpr int kMaxWallTiers = 4;

// The cross-section of one side beyond the footprint edge (e = lateral distance from it): tier k is a wall whose
// solid occupies e in [a[k], a[k] + wall_thickness], its top at height z[k] relative to the formation (cut: up,
// fill: down), at most wall_max_height above the previous tier; between tiers a flat bench; after the last tier a
// plain slope, natural ground (nat), or a flat plateau up to a neighbouring road (plat: one structure shared by two
// roads, built by the lower one). The whole profile scales with the run weight (a[] and z[] shrink towards the plain
// verge + slope), so runs taper to nothing at their free ends.
struct TierSet {
    int n = 0;
    double a[kMaxWallTiers] = {0, 0, 0, 0};
    double z[kMaxWallTiers] = {0, 0, 0, 0};
    bool cut = false, plat = false, nat = false;
};

struct WallPlan {
    bool present = false;
    bool sup = false;      // plan_side result only: the neighbour's shared structure reaches this side (nothing to build; present stays false)
    bool cut = false;
    bool full = false;     // legacy single wall retaining the natural ground at its outer face (wing walls; over_height fallback)
    bool wing = false;     // full wall blended in with weight fw from the earthworks beside it (base tiers, or the plain slope)
    bool over = false;     // the wall cap could not be kept inside the corridor: reported (over_height)
    double fw = 0;
    TierSet t;             // own tiers (not full)
    bool has_base = false;
    TierSet base;          // wing: the adjacent run's tiers
    bool base_stairs = false;  // wing: the adjacent run is evaluated on the spot (base_kind / base_h / base_mt describe it; cut = this plan's)
    double base_h = 0;
    int base_mt = 0;
    int base_kind = 0;
    // Terraced section (stairs): walls of height h at a fixed pitch, evaluated on the spot from the natural ground along the normal (see stair_profile).
    bool stairs = false;
    int mt = 0;            // tiers allowed (1: one tall fill wall up to wall_hard_max, 2: two capped tiers)
    int kind = 0;          // 0 terrace (walls), 1 rock cut face (cut side, no walls), 2 steep fill slope (needs_structure, no walls)
    int own = 0;           // what this station alone needs: 0 two capped tiers, 1 one tall wall, 2 steep fill, 3 rock cut
    bool rock = false;     // rock_cut: the cut side is an engineered rock face (a renderer gives it a rock material)
    bool needs = false;    // needs_structure: a fill wall cannot hold this station; a viaduct / half-bridge is suggested
    double h = 0;          // tier height (wall_max_height, or wall_hard_max for one tall wall)
    int n_stairs = 0;      // tiers at this station
    double reach_e = 0;    // lateral extent beyond the footprint edge
    double along_max = 0;  // run-out length beyond a free road end over which the terraces end diagonally
    bool has_alt = false;  // beside a terraced run: blend with this station's own plan (alt) by sw
    TierSet alt;
    double sw = 1;
};

// ---- sampled roads ----

struct RoadSample {
    double s = 0;                 // arc length along the road
    double x = 0, y = 0, z = 0;   // centreline, road surface height
    double h = 0;                 // heading, rad CCW from +x
    bool br = false, tu = false;  // bridge / tunnel station
    bool portal = false;          // first or last station of a tunnel
    int wL = 0, wR = 0;           // wall per side after medians: 0 none, 1 cut wall, 2 fill wall
    int wL0 = 0, wR0 = 0;         // wall flags before median removal
    double wwL = 0, wwR = 0;      // wall weight 0..1 (0 at free run ends, 1 beyond wall_ramp)
    bool mL = false, mR = false;  // side faces a parallel opposite-direction road of the same dual type
    bool rockL = false, rockR = false;    // rock_cut per side (engineered rock face, no wall)
    bool needsL = false, needsR = false;  // needs_structure per side (fill too steep / high for a wall)
    bool sL = false, sR = false;  // side leaves its earthworks to a neighbour's shared structure (built by the lower road up to this one)
    double supL = 0, supR = 0;    // how much: the neighbour's structure weight at the nearest station (0 at its run ends)
    WallPlan pL, pR;              // wall cross-section per side (present when wL / wR != 0)
};

// One road as plain data: the golden tests fill it directly, sample_graph_for_grading builds it
// from a RoadGraph. Input fields: s, x, y, z, h (and br / tu when classifying with Authored).
struct SampledRoad {
    std::string type_id;      // road type; medians pair roads of one dual type
    double half_width = 0;    // m, half the full cross-section (carriageway + kerbs + footways)
    bool dual = false;
    double median_gap = 0;    // m, dual types only
    std::vector<RoadSample> samples;
    std::vector<int> portals;  // filled by classify_road: indices of portal stations
    bool over_height = false;  // some wall had to exceed wall_max_height (diagnostic, set by classify_walls)
};

using GroundFn = std::function<double(double, double)>;  // natural ground height at (x, y)

// Structure from the authored spans (Authored: br / tu already set on the samples, e.g. from
// the graph's StructureSpans) or decided from cut / fill depth (Auto: the prototype's rule).
enum class StructureMode { Auto, Authored };

struct StructureRange {
    int first = 0, last = 0;  // inclusive sample indices
    StructureKind kind = StructureKind::Bridge;
};

// Where the Auto rule says a bridge or tunnel is required (cut / fill beyond wall_cap at the
// centre or at either wall line, smoothed). Pure query; classify_road(Auto) uses the same rule.
std::vector<StructureRange> required_structures(const SampledRoad& road, const GroundFn& ground,
                                                const GradingParams& p);

// Fills br / tu / portal and road.portals (structure flags only).
void classify_structures(SampledRoad& road, const GroundFn& ground, const GradingParams& p, StructureMode mode);

// Fills wL / wR / wL0 / wR0 from the structure flags. Walls only where a batter slope cannot
// daylight within wall_start (nor meet a neighbouring road's earthworks: `near`, other roads
// whose boxes overlap, may be empty); runs are bridged, padded and grown to a minimum. The
// approach to a portal / abutment is an ordinary slope: only a wing wall of
// min(wing_max, wing_ratio * H) metres blends into a full wall right at the structure.
void classify_walls(SampledRoad& road, const GroundFn& ground, const GradingParams& p,
                    const std::vector<const SampledRoad*>& near);

// classify_structures + classify_walls without neighbours (prepare_grading_roads redoes the
// walls with the neighbours in place).
void classify_road(SampledRoad& road, const GroundFn& ground, const GradingParams& p, StructureMode mode);

// Dual carriageways: the inner side facing the opposite-direction parallel partner of the same
// type gets no wall (sets mL / mR and clears wL / wR from wL0 / wR0 first; idempotent).
void mark_medians(std::vector<SampledRoad>& roads, const GradingParams& p);

// Wall weight per sample and side (wwL / wwR) from the final wall runs.
void compute_wall_weights(SampledRoad& road, const GradingParams& p);

// Farthest lateral distance from the footprint edge at which a batter slope still meets the
// ground on an unwalled side (never more than the wall-start reach). Needs final wall flags.
double slope_reach(const SampledRoad& road, const GroundFn& ground, const GradingParams& p);

// ---- prepared roads for grade_tile ----

struct Box2 {
    double x0 = 0, y0 = 0, x1 = 0, y1 = 0;
};

struct GradingSample {
    double x = 0, y = 0, z = 0;
    double wwL = 0, wwR = 0;
    std::int8_t wL = 0, wR = 0;
    bool br = false, tu = false;
    double supL = 0, supR = 0;  // shared-structure suppression weight of the side's own earthworks (0 none, 1 all)
    double fwL = 0, fwR = 0;  // wing funnel weight
    WallPlan pL, pR;
};

struct PortalPlane {
    int k0 = 0, k1 = 0;       // decimated segments [k0, k1) the plane cuts
    double x = 0, y = 0;      // plane point
    double nx = 0, ny = 0;    // unit normal pointing into the tunnel
};

struct GradingRoad {
    double half_width = 0;
    double reach = 0;                      // m from the centreline at which a slope may still change ground
    Box2 bbox;
    Box2 core;                             // samples' box grown by the half width: the footprint's extent
    std::vector<GradingSample> samples;    // decimated (every 2nd sample + portal neighbours)
    std::vector<Box2> seg_boxes;           // coarse boxes (pairs of segments, grown by reach) for tile culling
    std::vector<PortalPlane> planes;
    std::vector<int> nbr;                  // indices of the other prepared roads whose boxes overlap: a slope never reaches across them
    double end_ext = 0;                    // run-out beyond a free end over which terraced sides carry on (0 when none)
    GroundFn ground;                       // natural ground (terraced sections sample it along the normal)
};

// Recomputes the walls of every road with its neighbours in place, runs mark_medians and
// compute_wall_weights (mutating `roads`), then packs every road with at least two samples.
// Roads closer than their reach to a tile contribute to it.
std::vector<GradingRoad> prepare_grading_roads(std::vector<SampledRoad>& roads, const GroundFn& ground,
                                               const GradingParams& p);

// Bounding box of a road's samples grown by wall-start reach + box_margin.
Box2 road_box(const SampledRoad& road, const GradingParams& p);

// ---- tiles ----

// A tile of nx * ny texels; texel (i, j) is at (x0 + i * res, y0 + j * res), row-major.
struct TileSpec {
    double x0 = 0, y0 = 0;
    int nx = 0, ny = 0;
    double res = 0.5;
};


// Tile `key` of cells x cells texels on the global grid of resolution res (texel (i, j) of the
// world is at (i * res, j * res)), so 20 km+ maps are addressed by integer keys.
inline TileSpec tile_spec(TileKey key, int cells = 128, double res = 0.5) {
    return {static_cast<double>(key.i) * cells * res, static_cast<double>(key.j) * cells * res, cells, cells, res};
}

struct GradedTile {
    std::vector<float> height;            // graded ground height per texel
    std::vector<std::uint8_t> footprint;  // 1 where the texel is under a road, verge-less formation or wall solid
};

// Natural heights of a tile (bilinear from the heightfield / from any ground function).
std::vector<float> sample_natural_tile(const Heightfield& hf, const TileSpec& t);
std::vector<float> sample_natural_tile(const GroundFn& ground, const TileSpec& t);

// The grading function: graded heights for one tile from its natural heights (nx * ny floats)
// and the prepared roads. Pure and deterministic: each texel depends only on its position, so
// results do not depend on how the world is cut into tiles. Writes `out` (nx * ny) and, when
// not null, `footprint`.
// Scratch arrays of grade_tile_into, reusable between calls (no allocation after the first call
// of a given size). One workspace per worker thread; never share one between threads.
struct GradeWorkspace {
    std::vector<float> fp_d, fp_z, cut_m, fill_m, rq_ww, rq_d, rq_z, cut_e, fill_e, rq_ex, rq_ey, rq_bt, rq_fw, rq_al, rq_fs, rq_sp, rq_sv, cut_r, fill_r, cut_d2, fill_d2, cut_u, fill_u, cut_k2, fill_k2;
    std::vector<std::uint8_t> rq_w, rq_fl, rq_by, cut_k;
    std::vector<const WallPlan*> rq_p;
};
void grade_tile_into(const std::vector<GradingRoad>& roads, const GradingParams& p, const TileSpec& t,
                     const float* natural, float* out, std::uint8_t* footprint, GradeWorkspace& ws);
// Same with a temporary workspace.
void grade_tile_into(const std::vector<GradingRoad>& roads, const GradingParams& p, const TileSpec& t,
                     const float* natural, float* out, std::uint8_t* footprint = nullptr);
GradedTile grade_tile(const std::vector<GradingRoad>& roads, const GradingParams& p, const TileSpec& t,
                      const std::vector<float>& natural);

// ---- wall runs (structure meshes) ----

struct WallPoint {
    double x = 0, y = 0;      // on the wall line (road outer edge)
    double zTop = 0, zBot = 0;  // retained height range at the wall line
    double top = 0;           // wall top (cut: retained ground + coping allowance; fill: road + 1.0 at tier 0)
    double h = 0;             // heading
    bool nat = false;         // natural ground carries on beyond this wall's outer face
};

// Contiguous runs at the wall line per side; a run splits at bridge / tunnel stations and where
// a cut wall turns into a fill wall. A portal station joins the wall run beside it.
struct WallRun {
    char side = 'L';
    bool cut = false;
    double lo = 0, hi = 0;    // lateral solid range from the point: [0, wall_thickness] (a tier's offset is baked into the points)
    int tier = 0;             // 0 = the wall beside the road; 1.. = stacked walls further out (benching)
    bool over = false;        // over_height: this wall exceeds wall_max_height
    std::vector<WallPoint> pts;
};

std::vector<WallRun> wall_runs(const SampledRoad& road, const GroundFn& ground, const GradingParams& p);

// Contiguous stations of one side graded as an engineered rock cut face (no wall) or as the steepest stable fill slope (needs_structure).
struct SurfaceRun {
    char side = 'L';
    bool cut = false;
    bool rock_cut = false;         // engineered rock face (give it a rock material)
    bool needs_structure = false;  // some station cannot be held by a wall: suggest a viaduct / half-bridge (or refuse)
    int first = 0, last = 0;       // inclusive sample indices
};
std::vector<SurfaceRun> surface_runs(const SampledRoad& road);

// ---- tunnels ----

struct TunnelProfile {
    double W = 0;     // inner half-width
    double Hs = 0;    // springline height above the road
    double Hc = 0;    // crown height above the road
    double R = 0;     // arch radius
    double zc = 0;    // arch centre height above the road
    double rise = 0;
};

TunnelProfile tunnel_profile(double half_width, const GradingParams& p);
// Roof height above the road at lateral q for the inner profile (off = 0) or offset by `off`.
double tunnel_roof(const TunnelProfile& prof, double q, double off = 0);

struct TunnelStation {
    double x = 0, y = 0, z = 0, h = 0, s = 0;
};

struct TunnelVolume {
    int road = 0;       // index into the roads vector
    int a = 0, b = 0;   // sample range (first / last = portal planes)
    TunnelProfile prof;
    std::vector<TunnelStation> st;
};

// Contiguous tunnel station ranges of one road (at least two stations).
std::vector<StructureRange> tunnel_runs(const SampledRoad& road);
std::vector<TunnelVolume> tunnel_volumes(const std::vector<SampledRoad>& roads, const GradingParams& p);

// Float table the terrain material reads (RGBA32F, texture width tunnel_table_width texels,
// one row per chunk of <= tunnel.chunk segments; max_rows rows, zero-filled by the caller):
//   texel 0: (min x, min y, min up, nSeg)   texel 1: (max x, max y, max up, 0)    (chunk AABB)
//   texel 2+2j: (x, y, road z, outer half-width)
//   texel 3+2j: (arch centre height zc, outer arch radius, end flag 1 = portal plane, 0)   j = 0..nSeg
// `origin` is subtracted from x / y so large worlds keep float precision (pass the same origin
// to in_bore). Returns the number of rows used.
int tunnel_table_width(const GradingParams& p);
int pack_tunnels(std::vector<float>& data, int max_rows, const std::vector<TunnelVolume>& vols,
                 const GradingParams& p, Vec2 origin = {});

// Is the point (x, y plan, up = height) inside any packed bore volume? Exactly the terrain
// material's inBore(): use it to discard terrain inside the bore.
bool in_bore(const float* data, int width_texels, int rows, double x, double y, double up,
             const GradingParams& p);

// ---- RoadGraph adapter ----

// The natural ground function of a heightfield (bilinear). The heightfield must outlive the function.
GroundFn ground_fn(const Heightfield& hf);

// Samples one edge like the prototype (stations at most sample_step apart, tighter on bends)
// with extra stations exactly at structure span boundaries, so portal planes and abutments sit
// where the structure meshes put them. br / tu come from the edge's StructureSpans; classify
// with StructureMode::Authored.
SampledRoad sample_edge_for_grading(const Edge& e, const std::string& type_id, double half_width, bool dual,
                                    double median_gap, const GradingParams& p);

// Every edge of the graph (its road type must be in the graph's region pack; others are
// skipped). Half-width comes from the road type's lane cross-section.
std::vector<SampledRoad> sample_graph_for_grading(const RoadGraph& g, const GradingParams& p);

}  // namespace cp
