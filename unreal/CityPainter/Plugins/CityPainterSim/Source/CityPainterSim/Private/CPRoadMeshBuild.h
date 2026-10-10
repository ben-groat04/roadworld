#pragma once

// UE-free build step for ACPRoadMeshActor: owns the sim world (graph, command stack, region pack, marking
// rules), builds junctions / mesh chunks / markings with sim_core, and converts the result into plain arrays in
// Unreal's frame and winding. This header and CPRoadMeshBuild.cpp use ONLY the standard library and sim_core
// (the sim_core headers are included in the .cpp, never here), so they compile outside Unreal; see
// unreal/tools/README.md ("Standalone check of CPRoadMeshBuild").
//
// FRAME AND WINDING RULE (the single place in the plugin where sim -> UE coordinates are defined)
//   sim (metres, x east, y north, z up, right-handed)  ->  UE (centimetres, X north, Y east, Z up, left-handed)
//     UE.X = sim.y * 100,  UE.Y = sim.x * 100,  UE.Z = sim.z * 100
//   Heading: sim heading h (radians CCW from +x east) -> UE yaw degrees = 90 - degrees(h).
//   Directions (normals, tangents) use the same swap without the scale: (nx, ny, nz) -> (ny, nx, nz).
//   The swap is a reflection (determinant -1). Seen from above, the map looks identical in both frames (north up,
//   east right), so a triangle that is counter-clockwise from its front face in sim_core is still counter-clockwise
//   from above in UE, but Unreal's front face is CLOCKWISE. Rule: swap indices 1 and 2 of every triangle
//   ((a, b, c) -> (a, c, b)). Normals / tangent directions are NOT negated.
//   Tangent sign: sim_core's bitangent = normal x tangent (right-handed) points along +v. With the swap, Unreal's
//   TangentY = TangentZ x TangentX (same component formula, left-handed frame) points along -v, so
//   FProcMeshTangent::bFlipTangentY must be TRUE for sim w = +1 (stored here as TangentFlipY).
//
// PRECISION: every chunk has a double-precision origin (UE cm, in actor space); vertex floats are relative to
// that origin (tile size 256 m = 25600 cm, float resolution about 0.002 cm).

#include <array>
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace CPBuild
{

enum class ELayout : int
{
	Interchange = 0,
	Empty = 1,  // region + rules loaded, nothing drawn (for tests that add roads through the command stack)
};

struct FSettings
{
	std::string DataDir;  // absolute path of <repo>/data
	std::string RegionId = "uk";
	ELayout Layout = ELayout::Interchange;
	double TileSizeM = 256.0;
	double ChordToleranceM = 0.02;
	double MarkingLiftCm = 0.3;  // 3 mm
	bool bGenerateMarkings = true;
	// Debug geometry (FOutput::DebugLines), only collected when asked.
	bool bDebugConnectors = false;
	bool bDebugTrimLines = false;
	bool bDebugLaneCentrelines = false;
	bool bDebugDegraded = true;
};

struct FFloat2 { float X = 0, Y = 0; };
struct FFloat3 { float X = 0, Y = 0, Z = 0; };

// One mesh section (one surface class, or one marking colour) of one tile, already in Unreal's frame/winding.
struct FSection
{
	std::string Key;                              // surface class, or marking colour id
	float Rgb[3] = {1, 1, 1};                     // markings: sRGB 0..1 from the rules; surfaces: unused
	std::vector<FFloat3> Position;                // UE cm, relative to FTile::Origin
	std::vector<FFloat3> Normal;                  // unit, UE frame
	std::vector<FFloat3> TangentX;                // unit, UE frame, direction of +u
	std::vector<std::uint8_t> TangentFlipY;       // 1 = FProcMeshTangent::bFlipTangentY
	std::vector<FFloat2> UV0;                     // metres (u across, v along); markings: tile-relative sim metres
	std::vector<std::array<std::uint8_t, 4>> Colour;  // R, G, B, A
	std::vector<std::int32_t> Indices;            // UE (clockwise front face) winding, triples
};

struct FTile
{
	std::int64_t TileX = 0, TileY = 0;           // sim tile key (cp::TileKey i east, j north), 64-bit like TileKey
	double Origin[3] = {0, 0, 0};                // UE cm (X north, Y east, Z up), actor space
	std::vector<FSection> Surfaces;
	std::vector<FSection> Markings;              // one per colour id
};

enum class EDebugKind : int { Connector, TrimLine, LaneCentre, DegradedJunction };

struct FDebugLine
{
	EDebugKind Kind = EDebugKind::Connector;
	std::vector<std::array<double, 3>> Points;   // UE cm, actor space
};

struct FStats
{
	std::size_t Nodes = 0, Edges = 0, Junctions = 0, RoundaboutRingJunctions = 0, DegradedJunctions = 0;
	std::size_t Roundabouts = 0, Tiles = 0, Marks = 0, MarkTriangles = 0;
	std::map<std::string, std::size_t> TrianglesBySurface;  // total over all tiles (border triangles counted once)
	std::map<std::string, std::size_t> TrianglesByMarkingColour;
};

struct FOutput
{
	std::vector<FTile> Tiles;
	FStats Stats;
	std::vector<std::string> Messages;   // warnings / problems from sim_core, already formatted
	std::vector<FDebugLine> DebugLines;
	std::map<std::string, std::array<float, 3>> MarkingColours;  // colour id -> sRGB 0..1
};

// Opaque: the sim state (cp::RoadGraph, cp::CommandStack, cp::RegionPack, cp::MarkingRules, junctions).
struct FWorld;
struct FWorldDeleter { void operator()(FWorld* W) const; };
using FWorldPtr = std::unique_ptr<FWorld, FWorldDeleter>;

FWorldPtr CreateWorld();

// Loads <DataDir>/lane_types.json + regions/<RegionId>/ and marking_rules.json into the world. On failure `Error`
// is filled (it names the file and the reason) and the world stays unusable. Marking rule failure is not fatal:
// the world builds without markings and `Warning` says why.
bool LoadData(FWorld& World, const FSettings& Settings, std::string& Error, std::string& Warning);

// Draws the layout through the command stack. Call once after LoadData.
bool BuildLayout(FWorld& World, ELayout Layout, std::string& Error);

bool Undo(FWorld& World);
bool Redo(FWorld& World);

// Junctions + roundabouts + mesh chunks + markings for the whole graph, converted to Unreal's frame.
// TODO(dirty tiles): the graph reports dirty nodes; a per-tile rebuild would pass `only` to build_mesh_chunks.
FOutput BuildRender(FWorld& World, const FSettings& Settings);

// Sim-frame world position of the first graph nodes, for scripts (sim metres).
std::vector<std::array<double, 3>> NodePositions(const FWorld& World);

}  // namespace CPBuild
