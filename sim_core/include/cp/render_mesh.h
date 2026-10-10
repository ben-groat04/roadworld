#pragma once
// Render-ready road meshes (docs/unreal-photorealism.md): carriageway, kerbs, pavements, verges,
// junction surfaces and roundabout islands, as indexed triangle meshes the engine plugin uploads
// without further geometry work. Derived data only; rebuilt from the road graph + junctions.
//
// Conventions
//  - Sim frame: metres, x east, y north, z up, right-handed (the plugin converts in CPRoadMeshBuild.h).
//  - Winding is counter-clockwise seen from the front face (outward normal), so every upward
//    facing surface has a +z normal. Kerb faces point toward the carriageway.
//  - Surface classes are strings: LaneType::surface of the lane that owns a strip ("asphalt",
//    "paving", "kerb", "grass", ... whatever the data says); roundabout islands use
//    RoundaboutRules::central_island_surface / splitter_surface and RenderMeshParams::island_kerb_surface
//    for their faces. One buffer per class = one material slot.
//  - What is meshed: edges cover exactly their cross-section (every lane of the road type, including
//    verges and hard strips). Nothing is generated between the two carriageways of a dual road
//    (the central reservation) or beyond the outermost lane: that ground is terrain. EdgeBoundary
//    gives the outer boundary polylines the terrain should meet. Edges meet junctions at the trim
//    lines; 2-arm nodes whose road types or headings differ get a short lofted joint (build_joint_mesh).
//  - Kerbs exist only where the cross-section has a lane of profile role "kerb" (plus the step faces
//    that follow from a raised lane meeting the carriageway directly). Hard strips and verges are flush.
//  - UV0 is in metres (1 uv unit = 1 m, so a tiling texture has the same density everywhere):
//      edge road/pavement/verge surfaces: u runs across the road along the cross-section path (left to
//        right looking along the edge), v is the arc length of that edge. v starts at 0 on every edge,
//        so it jumps at a node where two edges meet (the material must not depend on v continuity
//        across edges; isotropic tiling textures such as asphalt do not);
//      kerb faces and kerb tops (edge and junction): u is metres along the kerb line, v is metres up
//        the face (then across the top);
//      junction and joint surfaces: world-planar in the frame of the junction's major arm
//        (u = -lateral, v = along the arm, origin at its trim centre), exactly 1 uv unit per metre in
//        every direction, never stretched. Orientation and phase differ from the edge meshes across
//        the seam, which is invisible for isotropic asphalt/paving textures.
//      islands: world-planar (x, y) on tops, along/up on faces.
//  - UV1 (edge meshes, driving lanes): x = metres from the centre of the lane the vertex belongs to
//    (+ = left of travel a -> b), y = that lane's width. (0, 0) everywhere else, including junctions:
//    y == 0 tells the material that R is already the final wear mask. Wheel paths are a function of
//    uv1.x in the material (peaks at +-wheel_path_offset = 0.9 m from the lane centre, falloff ~0.35 m),
//    which keeps one strip per lane instead of seven; lane boundaries have split vertices so uv1.x is
//    exact at both lane edges.
//  - Vertex colour (RGBA8, each 0..255 = 0..1), the same channels everywhere:
//      R wear amplitude: edge driving lanes 0.55, rising to 1.0 over the last wear_approach_length
//        metres before a junction stop line on the approach side; other surfaces 0. The material
//        multiplies it by the wheel-path pattern of uv1.x. Junction carriageway: the final mask
//        (0.3 .. 1) from the distance to the nearest connector path (a blob, no pattern).
//      G edge dirt: 1 on the carriageway edge and on kerb surfaces, linear to 0 at edge_dirt_width
//        (0.5 m) inside the carriageway (the outermost lanes are split there so the ramp is resolved);
//        pavements 0.5, verges 0.3. Junction carriageways: same distance falloff to the polygon
//        boundary, but only as well resolved as the junction triangles (max edge 3.7 m at 2 cm).
//      B low-frequency variation: smooth stable hash of (edge id, floor(s / lowfreq_period)) with
//        lowfreq_period = 30 m; junctions blend their arms' values (exact on the trim lines).
//      A reserved for future use (wetness / patch mask), currently 0.
//  - Tangents: unit, orthogonal to the normal, the direction of increasing u of UV0; the w component
//    carries the handedness (bitangent = w * normal x tangent points along increasing v).
//  - Cross-section shape (docs/road-graph-model.md, lane profile roles): every lane type has a
//    role (LaneType::profile): carriageway lanes follow the road type's crossfall (camber about the
//    centreline for two-way edges, a single fall to the nearside for one-way/dual), kerb lanes are
//    a flat strip at upstand height above the carriageway edge with a vertical face, raised lanes
//    (pavements) continue at kerb-top level falling toward the carriageway, verge lanes fall away.
#include <array>
#include <cstdint>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "cp/junction.h"
#include "cp/junction_roundabout.h"
#include "cp/road_graph.h"
#include "cp/tile_key.h"

namespace cp {

struct RenderMeshParams {
    // Chord-error tolerance of the tessellation (m): plan curvature of every lane boundary, vertical
    // curves, junction polygon boundaries and island outlines (Douglas-Peucker), station spacing on
    // straights (grows with sqrt(tolerance)) and junction triangle size. The same function gives
    // LODs: ~0.02 near, ~0.1 mid, ~0.5 far.
    double chord_tolerance = 0.02;
    double max_station_step = 25.0;  // m, longest ring spacing along an edge at 2 cm (scales with the tolerance)
    double min_station_step = 0.25;  // m, shortest spacing the refinement may produce

    // Used when the road type in the region pack leaves the field unset (RoadType::crossfall etc.).
    double default_crossfall = 0.025;          // rise/run
    double default_kerb_upstand = 0.125;       // m
    double default_footway_crossfall = 0.02;   // rise/run, footways fall toward the carriageway
    double default_verge_crossfall = 0.05;     // rise/run, verges fall away from the carriageway

    std::string default_surface = "asphalt";   // for lanes whose type has no surface, and junction carriageways
    std::string island_kerb_surface = "kerb";  // faces around islands and junction kerbs without a kerb lane type

    // Vertex mask shaping (see the file header).
    double wheel_path_offset = 0.9;      // m from the lane centre; consumed by the material, documented here
    double edge_dirt_width = 0.5;        // m
    double wear_approach_length = 20.0;  // m before a junction trim line
    double lowfreq_period = 30.0;        // m
    double joint_length = 4.0;           // m each side of a 2-arm node that needs a filler
};

struct Float2 { float x = 0, y = 0; };
struct Float3 { float x = 0, y = 0, z = 0; };
struct Float4 { float x = 0, y = 0, z = 0, w = 0; };

// Index range of a mesh that belongs to one edge section (bridge/tunnel/ground) or one junction.
// Edge ranges are contiguous and ordered along the edge. Index ranges of one section never mix
// with another's, so the renderer can hide or restyle a structure span.
struct SectionRange {
    EdgeId edge;                    // null for junction / island geometry
    NodeId node;                    // junction node for junction geometry, else null
    StructureKind kind = StructureKind::Ground;
    double s0 = 0, s1 = 0;          // edge arc length range (0, 0 for junctions)
    std::uint32_t first_index = 0;
    std::uint32_t index_count = 0;
};

// Double precision mesh in world coordinates (what the builders produce; tests compare against it).
struct WorldMesh {
    std::vector<Vec3> positions;
    std::vector<Vec3> normals;       // unit, smooth within a surface strip, hard at kerb faces
    std::vector<Vec3> tangents;      // unit, orthogonal to the normal, direction of increasing u
    std::vector<double> tangent_w;   // +1 or -1, handedness
    std::vector<Vec3> bitangents;    // construction scratch: empty in every returned mesh
    std::vector<Vec2> uv;
    std::vector<Vec2> uv1;
    std::vector<Float4> colours;     // r, g, b, a in 0..1
    std::vector<std::uint32_t> indices;  // triples, counter-clockwise
    std::vector<SectionRange> sections;
};

using WorldMeshSet = std::map<std::string, WorldMesh>;  // by surface class

// Float mesh the plugin uploads. Positions are relative to the chunk origin (a double).
struct MeshBuffers {
    std::vector<Float3> positions;
    std::vector<Float3> normals;
    std::vector<Float4> tangents;  // xyz = direction of +u, w = handedness (+1 / -1)
    std::vector<Float2> uv0;       // metres
    std::vector<Float2> uv1;       // lane-relative, see the file header
    // RGBA8 masks, channel layout in the file header (R wear, G edge dirt, B variation, A reserved = 0).
    std::vector<std::array<std::uint8_t, 4>> colours;
    // Per-vertex surface class id (index into SurfaceTable::names); filled by build_mesh_chunks, empty from
    // pack_buffers. Lets one master material pick the texture layer when classes are merged.
    std::vector<std::uint8_t> surface_ids;
    std::vector<std::uint32_t> indices;
    std::vector<SectionRange> sections;
};

struct MeshChunk {
    TileKey tile;
    Vec3 origin;                                 // world position of the chunk origin (tile south-west corner, z = 0)
    std::map<std::string, MeshBuffers> surfaces;  // by surface class
};

// Converts a world mesh to float buffers relative to `origin` (all vertices kept).
MeshBuffers pack_buffers(const WorldMesh& mesh, Vec3 origin);

// ------------------------------------------------------------------ surface height queries
//
// The one definition of "where the road surface is", shared with the markings generator: the mesh
// builders and these functions evaluate the same cross-section, so a mark placed with them lies on
// the rendered surface.

// Cross-section height of one edge: surface z minus the edge's centreline profile z (e.vertical) as a
// function of lateral offset (+ = left of a -> b). Includes crossfall/camber, kerb upstand, footway
// and verge fall. Cheap to copy and to query.
class SurfaceProfile {
public:
    struct Impl;
    SurfaceProfile() = default;
    explicit SurfaceProfile(std::shared_ptr<const Impl> impl) : impl_(std::move(impl)) {}
    bool valid() const { return impl_ != nullptr; }
    // `carriageway` picks the carriageway-level strip where two meet at a step (the kerb foot, as
    // opposed to the kerb top / pavement edge); outside the cross-section carriageway values clamp
    // and raised values extrapolate their slope. 0 when invalid.
    double offset(double lateral, bool carriageway = true) const;
    // Lateral extent of the cross-section, left (+) and right (-) edges.
    double left() const;
    double right() const;

private:
    std::shared_ptr<const Impl> impl_;
};

// Builds the profile of `edge` (lanes, crossfall and kerb parameters come from its road type and the
// region pack, defaults from `params`). Invalid when the road type is unknown.
SurfaceProfile make_surface_profile(const RegionPack& region, const Edge& edge, const RenderMeshParams& params = {});

// z of the surface of an edge at centreline arc length s and lateral offset: edge_point(e, s).z plus
// the profile offset. Builds the profile on every call; cache a SurfaceProfile when calling many times.
// Valid on the edge's own cross-section, not inside junctions or between their trim lines (use
// JunctionSurface there). 0 when the edge or its road type is unknown.
double edge_surface_z(const RoadGraph& graph, const RegionPack& region, EdgeId edge, double s, double lateral,
                      const RenderMeshParams& params = {});

// Surface height inside a junction at a plan point: the same blend of the arms' profiles the
// junction mesh uses (exact on each trim line, matching the edge surface there). Construction does
// the work (profiles of all arms); z() is cheap.
class JunctionSurface {
public:
    struct Impl;
    JunctionSurface(const RoadGraph& graph, const RegionPack& region, const Junction& junction,
                    const RenderMeshParams& params = {});
    bool valid() const { return impl_ != nullptr; }
    // `carriageway` true: the carriageway polygon level; false: the pavement polygon level (kerb top).
    double z(Vec2 p, bool carriageway = true) const;

private:
    std::shared_ptr<const Impl> impl_;
};

// One-shot convenience (builds a JunctionSurface per call; do not use in loops).
double junction_surface_z(const RoadGraph& graph, const RegionPack& region, const Junction& junction, Vec2 p,
                          bool carriageway = true, const RenderMeshParams& params = {});

// ------------------------------------------------------------------ builders

// Outer boundary of an edge mesh, for the terrain / verge blend: the leftmost and rightmost
// cross-section vertices (left = left of a -> b) at every station, trim to trim, at their final
// heights.
struct EdgeBoundary {
    std::vector<Vec3> left, right;
};

// Mesh of one edge between its trims. Ends at junctions (3+ arms) are cut at Junction::trims; ends
// at 2-arm nodes are cut at RenderMeshParams::joint_length when the neighbouring edge differs (road
// type or heading), else not at all. Ring vertices at a trim line coincide exactly with the junction
// or joint mesh boundary (same arm_point() frame), and the end rings contain every cross-section
// vertex. `boundary` (optional) receives the outer boundary polylines.
WorldMeshSet build_edge_mesh(const RoadGraph& graph, const RegionPack& region, EdgeId edge,
                             const JunctionSet& junctions, const RenderMeshParams& params = {},
                             EdgeBoundary* boundary = nullptr);

// Mesh of one junction: carriageway and pavement polygons triangulated, refined to the LOD's triangle
// size, heights blended from the arms (inverse-distance blend of every arm's extrapolated profile,
// exact on each trim line), plus kerb faces along the carriageway/pavement boundary. Degraded or empty
// junctions give an empty (or partial) set, never garbage. `ok` (optional) is set to false when some
// polygon could not be triangulated completely.
WorldMeshSet build_junction_mesh(const RoadGraph& graph, const RegionPack& region, const Junction& junction,
                                 const RenderMeshParams& params = {}, bool* ok = nullptr);

// Filler for a 2-arm node (type change, heading kink, roundabout ring joint): a loft between the end
// rings of the two edges (each cut joint_length from the node) matching the cross-sections by
// lateral position, tapering where one is narrower. Empty when the two edges meet seamlessly.
WorldMeshSet build_joint_mesh(const RoadGraph& graph, const RegionPack& region, NodeId node,
                              const RenderMeshParams& params = {});

// Central island and splitter islands of a roundabout group: raised tops (surfaces from
// RoundaboutRules) plus kerb faces whose feet follow the adjacent carriageway surface.
WorldMeshSet build_roundabout_mesh(const RoadGraph& graph, const RegionPack& region, const RoundaboutLayout& layout,
                                   const RenderMeshParams& params = {});

// Appends `src` to `dst` (surface by surface, indices rebased).
void append_mesh(WorldMeshSet& dst, const WorldMeshSet& src);

// ------------------------------------------------------------------ chunking

struct ChunkParams {
    double tile_size = 256.0;  // m
    // false (default): one buffer per surface class, keyed by class name, with edge / structure sections.
    // true: all classes of a tile in ONE buffer keyed "road" with one section (one draw call per tile and
    // shadow pass); surface_ids says which class each vertex came from. Every class currently belongs to
    // that single opaque group; a class that must stay separate (e.g. transparent) would get the second
    // key, none exists yet. Triangles, positions, normals, uvs and masks are identical in both modes.
    bool merge_surfaces = false;
};

// Surface class id table: names in sorted order, id = index (at most 255 classes). Built from the region
// pack (every lane type surface, the roundabout surfaces, the default and island kerb surfaces), so ids
// are stable for a given data set regardless of what a build contains.
struct SurfaceTable {
    std::vector<std::string> names;
    int id(const std::string& name) const;  // -1 if unknown
};
SurfaceTable make_surface_table(const RegionPack& region, const RenderMeshParams& params = {});

// Builds every edge, junction, joint and roundabout island into per-tile chunks. A triangle belongs to
// the tile containing its centroid (vertices are duplicated across tile borders), so a tile's content
// depends only on the geometry touching it and is bit-identical between runs. `only` (when given)
// restricts output, and the work, to those tiles (dirty-tile rebuild). Chunks with no triangles are
// omitted. Roundabout layouts are optional (null: no islands).
std::map<TileKey, MeshChunk> build_mesh_chunks(const RoadGraph& graph, const RegionPack& region,
                                               const JunctionSet& junctions,
                                               const std::vector<RoundaboutLayout>* roundabouts,
                                               const RenderMeshParams& params = {}, const ChunkParams& chunk = {},
                                               const std::set<TileKey>* only = nullptr,
                                               SurfaceTable* table = nullptr);

}  // namespace cp
