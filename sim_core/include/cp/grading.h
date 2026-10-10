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
    // ---- sampling ----
    double sample_step = 2.0;      // m, adapter centreline sampling step
    // ---- structures and walls (sample counts) ----
    int structure_gap = 8;         // auto bridge / tunnel: gaps shorter than this are filled
    int structure_min_run = 8;     // auto bridge / tunnel: runs shorter than this are dropped
    int wall_gap = 8;              // gaps between wall runs shorter than this are bridged
    int wall_min_run = 8;          // a wall run is at least this long (grown, never dropped)
    int portal_wall = 12;          // walled stretch before a portal / after an abutment
    int wall_pad = 5;              // wall runs are padded by this much beyond the stations that need them
    double wall_ramp = 10.0;       // m, wall weight blends 0 -> 1 over this run from a free end
    // ---- portals and reach ----
    double plane_over = 0.75;      // m, grading stops this far past a portal plane
    int plane_span = 10;           // decimated segments before / after a portal carrying its plane test
    double reach_margin = 2.0;     // m, added to the wall-start reach (reach cap)
    double box_margin = 1.0;       // m, road bounding box margin beyond the reach cap
    double reach_pad = 2.5;        // m, added to the measured daylight reach of unwalled roads
    TunnelParams tunnel;

    static GradingParams from_rules(const GradingRules& rules);
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

// Fills br / tu / portal / wL / wR / wL0 / wR0 and road.portals. Walls only where a batter
// slope cannot daylight within wall_start; runs are bridged, padded, grown to a minimum, and
// walled before portals and abutments.
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
    std::vector<GradingSample> samples;    // decimated (every 2nd sample + portal neighbours)
    std::vector<Box2> seg_boxes;           // coarse boxes (pairs of segments, grown by reach) for tile culling
    std::vector<PortalPlane> planes;
};

// Runs mark_medians and compute_wall_weights (mutating `roads`), then packs every road with at
// least two samples. Roads closer than their reach to a tile contribute to it.
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

struct TileKey {
    std::int64_t i = 0, j = 0;
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
void grade_tile_into(const std::vector<GradingRoad>& roads, const GradingParams& p, const TileSpec& t,
                     const float* natural, float* out, std::uint8_t* footprint = nullptr);
GradedTile grade_tile(const std::vector<GradingRoad>& roads, const GradingParams& p, const TileSpec& t,
                      const std::vector<float>& natural);

// ---- wall runs (structure meshes) ----

struct WallPoint {
    double x = 0, y = 0;      // on the wall line (road outer edge)
    double zTop = 0, zBot = 0;  // retained height range at the wall line
    double top = 0;           // wall top (cut: ground + 0.3; fill: road + 1.0)
    double h = 0;             // heading
};

// Contiguous runs at the wall line per side; a run splits at bridge / tunnel stations and where
// a cut wall turns into a fill wall. A portal station joins the wall run beside it.
struct WallRun {
    char side = 'L';
    bool cut = false;
    double lo = 0, hi = 0;    // lateral solid range from the wall line: [0, wall_thickness]
    std::vector<WallPoint> pts;
};

std::vector<WallRun> wall_runs(const SampledRoad& road, const GroundFn& ground, const GradingParams& p);

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

// The natural ground function of a heightfield (bilinear).
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
