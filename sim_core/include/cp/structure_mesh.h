#pragma once
// Structure meshes (docs/planning-log.md, "Elevation and terrain"; port of the structure part of
// prototypes/road-draw/src/roadmesh.js): bridges (deck, parapets, piers with caps and footings,
// closed abutments with stepped wing walls, solid-fill blocks for short spans), tunnels (lining,
// portal headwalls with coping, lamp positions) and retaining walls (stepped level panels with
// coping). The road surface itself, kerbs and footways come from render_mesh.h; this module adds
// only what carries or surrounds them. Derived data only.
//
// Conventions follow render_mesh.h: sim frame (x east, y north, z up), counter-clockwise winding
// seen from the outward normal, UV0 in metres (u along the element, v up on vertical faces and
// across on horizontal ones; faces whose u would make the tangent frame mirrored get -u so
// normal x tangent always matches +v), one WorldMesh per surface class (= material slot), and
// SectionRange records (edge, kind, s range, index range) per element. Faces are flat shaded
// (vertices are not shared between faces); the tunnel lining carries smooth analytic normals.
// Elements are closed solids unless StructureElement::open_edges says otherwise.
//
// Meshes versus instance lists:
//   meshes: deck, edge beams, parapet walls, parapet end pilasters, piers (shaft, cap, footing),
//           abutments (stem, ballast wall), wing walls, solid blocks, tunnel lining, headwalls and
//           their coping, retaining walls and their coping.
//   instance lists (InstanceList, for Nanite/ISM instancing): "parapet_post" (a post every
//           parapet_post_spacing along both parapets) and "tunnel_lamp" (fittings on the roof).
// Vertex colours (StructureMeshes::colours, parallel to each surface's vertices): R = distance
// from the nearest tunnel portal in units of depth_range / 255 (tunnel lining only, 0 elsewhere),
// G = B = 0, A = 255. build_structure_chunks() writes them into MeshBuffers::colours.
#include <array>
#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "cp/heightfield.h"
#include "cp/render_mesh.h"
#include "cp/road_graph.h"

namespace cp {

// Surface class names used by this module (texture slots: structural_concrete, kerb_concrete,
// wall_brick in unreal/CityPainter/SourceArt/Textures/PolyHaven/manifest.json).
namespace structure_surface {
inline constexpr const char* concrete = "structural_concrete";  // deck, piers, abutments, wing walls, headwall faces
inline constexpr const char* parapet = "parapet";
inline constexpr const char* lining = "tunnel_lining";
inline constexpr const char* wall = "wall_facing";
inline constexpr const char* coping = "coping";
}  // namespace structure_surface

// ------------------------------------------------------------------ inputs

// Ground under and around structures. Natural or graded; the grading module will provide the
// graded one (see HeightfieldGround for a plain heightfield). Pure, exception-free, thread-safe.
class GroundSampler {
public:
    virtual ~GroundSampler() = default;
    virtual double height(double x, double y) const = 0;  // ground (graded where roads grade it) at (x, y)
    // Natural terrain, used for foundation depths; defaults to height().
    virtual double natural_height(double x, double y) const { return height(x, y); }
    virtual bool is_water(double /*x*/, double /*y*/) const { return false; }
};

class HeightfieldGround final : public GroundSampler {
public:
    explicit HeightfieldGround(const Heightfield& hf, double water_below = -1e30)
        : hf_(&hf), water_below_(water_below) {}
    double height(double x, double y) const override { return hf_->height(x, y); }
    bool is_water(double x, double y) const override { return hf_->height(x, y) < water_below_; }

private:
    const Heightfield* hf_;
    double water_below_;
};

// Retaining wall input: same members as grading.h's WallPoint / WallRun, so the grading port's
// wall_runs() output is copied field for field (grading.h itself cannot be included next to
// render_mesh.h: both define cp::TileKey).
struct StructureWallPoint {
    double x = 0, y = 0;        // on the wall line (road outer edge)
    double zTop = 0, zBot = 0;  // retained height range at the wall line
    double top = 0;             // top requirement (cut: ground + 0.3; fill: road + 1.0)
    double h = 0;               // road heading here
};

struct StructureWallRun {
    char side = 'L';  // 'L' = left of the road direction
    bool cut = false;
    double lo = 0, hi = 0;  // lateral solid range from the wall line (hi - lo = thickness)
    std::vector<StructureWallPoint> pts;
};

// A tunnel portal the walls must meet: the last wall panel beside a headwall is lifted to the
// headwall stub top (prototype: wallEnd/stubTop).
struct WallPortal {
    Vec2 pos;             // portal station
    double heading = 0;   // road heading there
    int dir = 1;          // +1 when the tunnel lies in the direction of the heading, else -1
    double half_width = 0;  // road half cross-section width (lateral offset of the wall line)
};

// Where walls are dropped or must not taper: discs around bridge spans and abutments (the wing
// walls continue the wall there) and the portals.
struct WallContext {
    std::vector<std::pair<Vec2, double>> exclusion;  // centre, radius
    std::vector<WallPortal> portals;
};

// ------------------------------------------------------------------ parameters

// Every dimension in metres unless stated. Defaults are tuned by eye and meant to be overridden by
// the region pack (see structure_params_for and the field list in the report).
struct StructureParams {
    // ---- sampling ----
    double station_step = 4.0;     // m, longest ring spacing along a structure
    double chord_tolerance = 0.02; // m, plan and vertical chord error of the stations

    // ---- deck ----
    double deck_top_gap = 0.04;    // m, deck top below the road surface (kept off the road's z-fight)
    double slab_depth = 1.0;       // m, slab thickness under the road surface
    double edge_beam_depth = 1.35; // m, thickness at the fascia (edge beam)
    double edge_beam_width = 0.6;  // m, full-depth part of the edge beam
    double edge_beam_haunch = 0.5; // m, sloped transition from the beam to the slab soffit

    // ---- parapets ----
    double parapet_height = 1.0;        // m above the road surface at the edge (region: bridge parapet)
    double parapet_thickness = 0.30;
    double parapet_embed = 0.10;        // m, base below the road surface
    double parapet_post_spacing = 3.0;  // m, instance list "parapet_post"
    double parapet_end_length = 0.5;    // m, end pilaster along the road
    double parapet_end_width = 0.55;    // m, end pilaster across (towards the road)
    double parapet_end_extra = 0.15;    // m, end pilaster is this much taller than the parapet

    // ---- short spans ----
    double min_bridge_length = 20.0;  // m: a shorter span is a solid block unless it crosses a road or water
    double crossing_drop = 2.0;       // m: another carriageway this much below the road means a crossing
    double block_bury = 1.0;          // m, block foot below the lowest ground under it

    // ---- piers ----
    double max_pier_spacing = 30.0;      // m, longest clear distance between supports (region; typical UK overbridge span)
    double pier_search = 0.4;            // fraction of the spacing a pier may move to avoid roads
    double pier_shaft_along = 1.4;       // m, thickness along the road
    double pier_shaft_width = 0.5;       // fraction of the road half-width, each side of the centre
    double pier_cap_along = 2.4;
    double pier_cap_inset = 0.8;         // m, cap ends this far inside the fascia
    double pier_cap_depth = 0.9;
    double pier_footing_margin = 0.3;    // m, footing larger than the shaft all round
    double pier_footing_above = 0.35;    // m, footing top above the highest ground under it
    double pier_footing_bury = 1.2;      // m, footing foot below the lowest ground under it
    double pier_min_height = 0.5;        // m, shorter shafts are skipped
    double pier_road_clearance = 2.0;    // m, footing keeps this far from another road's cross-section
    double pier_road_clearance_min = 0.3; // m, fallback when nothing fits (a narrow central reservation)
    double pier_junction_radius = 12.0;  // m, no pier within this of a junction node
    double pier_pier_clearance = 6.0;    // m, no pier this close to another bridge's pier
    double pier_ground_clearance = 1.0;  // m, a road is "below" the deck if it is this far under the soffit

    // ---- abutments and wing walls ----
    double bearing_width = 1.2;     // m, stem under the deck end
    double ballast_thickness = 0.5; // m, stem above the shelf, behind the deck end
    double abutment_bury = 1.0;     // m, foot below the lowest ground
    double wing_length = 8.0;       // m, longest wing wall (beyond it the retaining wall of the grading takes over)
    double wing_panel = 3.0;        // m, panel length (level tops)
    double wing_thickness = 1.5;    // m, flush with the retaining walls so no ground shows behind it
    double wing_cover = 0.15;       // m, panel top above the retained ground
    double wing_end_exposed = 0.3;  // m, a wing ends where its panel shows less than this

    // ---- tunnels ----
    double tunnel_clear_height = 5.5;  // m, springline above the road (region grading.tunnel_clear_height)
    double tunnel_rise_k = 0.2;        // arch rise as a fraction of the half-width
    int tunnel_arch_segments = 16;
    double headwall_thickness = 2.0;   // m, along the road behind the portal plane
    double headwall_foot = 2.5;        // m below the road surface
    double headwall_crown = 1.5;       // m of headwall above the arch crown at least
    double headwall_ground_margin = 0.4;  // m, top above the highest ground behind the face
    double lamp_spacing = 6.0;         // m, instance list "tunnel_lamp"
    double lamp_lateral = 3.3;         // m, rows at +-this from the centreline (clamped inside the walls)
    double lamp_drop = 0.05;           // m below the roof
    double depth_range = 64.0;         // m, vertex colour R = 255 at this distance from a portal

    // ---- retaining walls and coping ----
    double wall_thickness = 1.5;       // m (region grading.wall_thickness)
    double wall_panel = 4.0;           // m, nominal panel length (region)
    double wall_quant = 0.25;          // m, panel tops are multiples of this
    double wall_max_step = 0.5;        // m, largest step between neighbouring panels (multiple of wall_quant)
    double wall_min_panel = 1.0;       // m, panels are never shortened below this
    double wall_station_step = 1.0;    // m, resampling of the input runs
    double wall_min_run = 6.0;         // m, shorter wall runs are dropped
    double wall_foot_bury = 1.0;       // m, foot below the lowest ground
    double wall_foot_smooth = 4.0;     // m, half-window of the foot smoothing
    double wall_taper = 4.0;           // m, free ends blend down to the ground over this length
    double coping_thickness = 0.15;
    double coping_overhang = 0.04;
};

// Defaults with the values the region pack already has (grading.wall_thickness,
// grading.tunnel_clear_height).
StructureParams structure_params_for(const RegionPack& region);

// ------------------------------------------------------------------ outputs

enum class ElementKind {
    Deck,
    Parapet,
    ParapetEnd,
    Pier,
    PierCap,
    PierFooting,
    AbutmentStem,   // bearing + ballast wall (two boxes: both are Abutment elements)
    WingWall,
    Block,          // solid infill of a short bridge span
    TunnelLining,   // open: bottom edge lines (at the road surface) and the two portal rings
    Headwall,       // open: the tunnel opening on the front face
    HeadwallCoping,
    RetainingWall,
    WallCoping,
};

struct StructureElement {
    ElementKind kind = ElementKind::Deck;
    EdgeId edge;                  // null for retaining walls
    std::string surface;          // key into StructureMeshes::meshes
    std::uint32_t section = 0;    // index into meshes[surface].sections: the element's index range
    bool closed = true;           // false: has declared openings (see ElementKind)
    int tag = 0;                  // bridge / tunnel / wall-run ordinal within the edge or call, side for walls
};

struct InstanceTransform {
    Vec3 position;       // world, metres
    double yaw = 0;      // rad CCW from +x: the road direction
    double pitch = 0;    // rad, road grade angle (nose up positive)
    double roll = 0;
};

struct InstanceList {
    std::string kind;    // "parapet_post", "tunnel_lamp"
    EdgeId edge;
    std::vector<InstanceTransform> items;
};

struct BridgeInfo {
    EdgeId edge;
    double s0 = 0, s1 = 0;
    bool solid_block = false;    // short span on plain ground: infill block, no deck or piers
    bool crosses_road = false;   // another carriageway more than crossing_drop below, within the footprint
    bool over_water = false;     // water under the footprint
    std::vector<double> pier_s;  // arc length of every pier built
    bool pier_in_water = false;  // a footing stands in water (allowed, flagged)
    bool pier_skipped = false;   // a pier position was dropped (too short a shaft)
    bool span_too_long = false;  // roads underneath left no pier position: a clear span exceeds max_pier_spacing
    double longest_span = 0;     // m, longest clear distance between supports (abutment or pier centres)
};

// Tunnel volume for the terrain bore cut-out. Same data as grading.h's TunnelProfile / TunnelVolume
// (W, Hs, Hc, R, zc, rise; station list), kept here because grading.h cannot be included next to
// render_mesh.h. Merge when convenient: fill grading's structs from these field for field.
struct TunnelSection {
    double W = 0;   // inner half-width
    double Hs = 0;  // springline height above the road
    double Hc = 0;  // crown height above the road
    double R = 0;   // arch radius
    double zc = 0;  // arch centre height above the road
    double rise = 0;
};
struct TunnelStationInfo {
    double x = 0, y = 0, z = 0, h = 0, s = 0;
};
struct TunnelBore {
    EdgeId edge;
    double s0 = 0, s1 = 0;
    TunnelSection section;
    std::vector<TunnelStationInfo> stations;  // portal plane to portal plane
};

// Level of the last wall panel beside a headwall (the headwall stub is built level with it).
struct WallEnd {
    Vec2 portal;
    int side = 1;  // +1 left of the road direction, -1 right
    double level = 0;
};

struct StructureMeshes {
    WorldMeshSet meshes;  // by surface class
    std::map<std::string, std::vector<std::array<std::uint8_t, 4>>> colours;  // per vertex of meshes[surface]
    std::vector<StructureElement> elements;
    std::vector<InstanceList> instances;
    std::vector<BridgeInfo> bridges;
    std::vector<TunnelBore> tunnels;
    std::vector<WallEnd> wall_ends;  // retaining walls only
};

// ------------------------------------------------------------------ builders

// Structures of one edge: every bridge span (deck, parapets, piers or block, abutments) and tunnel
// span (lining, headwalls, lamps). `trim_a` / `trim_b` clip spans to the part of the edge the road
// mesh covers (junction trims). `wall_ends` (optional, from build_retaining_walls) lets the
// headwall stubs meet the last wall panel; build_structures does that automatically.
StructureMeshes build_edge_structures(const RoadGraph& graph, const RegionPack& region, EdgeId edge,
                                      const GroundSampler& ground, const StructureParams& params = {},
                                      double trim_a = 0, double trim_b = 0,
                                      const std::vector<WallEnd>* wall_ends = nullptr);

// Retaining walls from wall runs (grading.h wall_runs output).
StructureMeshes build_retaining_walls(const std::vector<StructureWallRun>& runs, const GroundSampler& ground,
                                      const StructureParams& params = {}, const WallContext& context = {});

// Walls context for a graph: exclusion discs on bridge spans and abutments, and portals.
WallContext wall_context_for_graph(const RoadGraph& graph, const RegionPack& region, const StructureParams& params = {});

// Every edge (or only `edges` when not empty), then the walls when given.
StructureMeshes build_structures(const RoadGraph& graph, const RegionPack& region, const GroundSampler& ground,
                                 const StructureParams& params = {}, const std::vector<StructureWallRun>* walls = nullptr,
                                 const std::vector<EdgeId>& edges = {});

// Appends `src` to `dst`.
void append_structures(StructureMeshes& dst, const StructureMeshes& src);

// Per-tile chunks like build_mesh_chunks (a triangle belongs to the tile of its centroid).
std::map<TileKey, MeshChunk> build_structure_chunks(const StructureMeshes& s, const ChunkParams& chunk = {},
                                                    const std::set<TileKey>* only = nullptr);

}  // namespace cp
