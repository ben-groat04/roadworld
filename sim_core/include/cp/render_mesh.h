#pragma once
// Render-ready road meshes (docs/unreal-photorealism.md): carriageway, kerbs, pavements, verges,
// junction surfaces and roundabout islands, as indexed triangle meshes the engine plugin uploads
// without further geometry work. Derived data only; rebuilt from the road graph + junctions.
//
// Conventions
//  - Sim frame: metres, x east, y north, z up, right-handed (the plugin converts in CPCoords.h).
//  - Winding is counter-clockwise seen from the front face (outward normal), so every upward
//    facing surface has a +z normal. Kerb faces point toward the carriageway.
//  - Surface classes are strings: LaneType::surface of the lane that owns a strip ("asphalt",
//    "paving", "kerb", "grass", ... whatever the data says), plus RenderMeshParams::island_surface
//    for roundabout islands. One buffer per class = one material slot.
//  - UV0 is in metres: u runs across the road along the cross-section path (left to right looking
//    along the edge, so kerb faces and tops continue the same coordinate), v is the centreline
//    arc length along the edge (continuous over the whole edge and over junction seams).
//  - Cross-section shape (docs/road-graph-model.md, lane profile roles): every lane type has a
//    role (LaneType::profile): carriageway lanes follow the road type's crossfall (camber about the
//    centreline for two-way edges, a single fall to the nearside for one-way/dual), kerb lanes are
//    a flat strip at upstand height above the carriageway edge with a vertical face, raised lanes
//    (pavements) continue at kerb-top level falling toward the carriageway, verge lanes fall away.
#include <array>
#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "cp/junction.h"
#include "cp/junction_roundabout.h"
#include "cp/road_graph.h"

namespace cp {

struct RenderMeshParams {
    // Chord-error tolerance of the tessellation (m): plan curvature of every lane boundary and
    // vertical curves. The same function gives LODs: ~0.02 near, ~0.1 mid, ~0.5 far.
    double chord_tolerance = 0.02;
    double max_station_step = 25.0;  // m, longest ring spacing along an edge
    double min_station_step = 0.25;  // m, shortest spacing the refinement may produce

    // Used when the road type in the region pack leaves the field unset (RoadType::crossfall etc.).
    double default_crossfall = 0.025;          // rise/run
    double default_kerb_upstand = 0.125;       // m
    double default_footway_crossfall = 0.02;   // rise/run, footways fall toward the carriageway
    double default_verge_crossfall = 0.05;     // rise/run, verges fall away from the carriageway

    std::string default_surface = "asphalt";   // for lanes whose type has no surface, and junction carriageways
    std::string island_surface = "island";     // top of roundabout and splitter islands
    std::string island_kerb_surface = "kerb";  // faces around islands
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
    std::vector<Vec3> normals;   // unit, smooth within a surface strip, hard at kerb faces
    std::vector<Vec3> tangents;  // unit, direction of increasing u
    std::vector<Vec2> uv;
    std::vector<std::uint32_t> indices;  // triples, counter-clockwise
    std::vector<SectionRange> sections;
};

using WorldMeshSet = std::map<std::string, WorldMesh>;  // by surface class

// Float mesh the plugin uploads. Positions are relative to the chunk origin (a double).
struct MeshBuffers {
    std::vector<Float3> positions;
    std::vector<Float3> normals;
    std::vector<Float4> tangents;  // xyz = direction of +u, w = +1 (bitangent = normal x tangent)
    std::vector<Float2> uv0;       // metres
    // Reserved for wear / blend masks (RGBA8). Currently constant {0, 0, 0, 255}.
    std::vector<std::array<std::uint8_t, 4>> colours;
    std::vector<std::uint32_t> indices;
    std::vector<SectionRange> sections;
};

struct TileKey {
    std::int32_t x = 0, y = 0;
    friend constexpr auto operator<=>(const TileKey&, const TileKey&) = default;
};

struct MeshChunk {
    TileKey tile;
    Vec3 origin;                                 // world position of the chunk origin (tile south-west corner, z = 0)
    std::map<std::string, MeshBuffers> surfaces;  // by surface class
};

// Converts a world mesh to float buffers relative to `origin` (all vertices kept).
MeshBuffers pack_buffers(const WorldMesh& mesh, Vec3 origin);

// ------------------------------------------------------------------ builders

// Mesh of one edge between its junction trims (the ends at nodes without a junction in `junctions`
// are untrimmed). Ring vertices at a trim line coincide exactly with the junction mesh boundary
// (same arm_point() frame), and the end rings contain every cross-section vertex.
WorldMeshSet build_edge_mesh(const RoadGraph& graph, const RegionPack& region, EdgeId edge,
                             const JunctionSet& junctions, const RenderMeshParams& params = {});

// Mesh of one junction: carriageway and pavement polygons triangulated with heights blended from
// the arms (see render_mesh.cpp: inverse-distance blend of every arm's extrapolated profile, exact
// on each trim line), plus kerb faces along the carriageway/pavement boundary. Degraded or empty
// junctions give an empty (or partial) set, never garbage.
WorldMeshSet build_junction_mesh(const RoadGraph& graph, const RegionPack& region, const Junction& junction,
                                 const RenderMeshParams& params = {});

// Central island and splitter islands of a roundabout group: raised tops plus kerb faces.
WorldMeshSet build_roundabout_mesh(const RoadGraph& graph, const RegionPack& region, const RoundaboutLayout& layout,
                                   const RenderMeshParams& params = {});

// Appends `src` to `dst` (surface by surface, indices rebased).
void append_mesh(WorldMeshSet& dst, const WorldMeshSet& src);

// ------------------------------------------------------------------ chunking

struct ChunkParams {
    double tile_size = 256.0;  // m
};

TileKey tile_of(Vec2 p, double tile_size);
Vec3 tile_origin(TileKey t, double tile_size);

// Builds every edge, junction and roundabout island into per-tile chunks. A triangle belongs to the
// tile containing its centroid (vertices are duplicated across tile borders), so a tile's content
// depends only on the geometry touching it and is bit-identical between runs. `only` (when given)
// restricts output, and the work, to those tiles (dirty-tile rebuild). Chunks with no triangles are
// omitted. Roundabout layouts are optional (null: no islands).
std::map<TileKey, MeshChunk> build_mesh_chunks(const RoadGraph& graph, const RegionPack& region,
                                               const JunctionSet& junctions,
                                               const std::vector<RoundaboutLayout>* roundabouts,
                                               const RenderMeshParams& params = {}, const ChunkParams& chunk = {},
                                               const std::set<TileKey>* only = nullptr);

}  // namespace cp
