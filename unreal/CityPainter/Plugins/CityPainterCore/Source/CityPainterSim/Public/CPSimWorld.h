#pragma once

// FCPSimWorld: the engine-facing facade over sim_core and the ONLY place (with the Private/ files of this module)
// that includes cp/ headers. Plain C++: no UObjects, no Unreal types. Queries are in the SIM frame (metres,
// doubles); mesh output is in the UE frame (see CPFrame.h).
//
// Content: the seeded test world (terrain, ground picking, terrain tile meshes) and, when the region pack loads, the
// road graph with its command stack and world cache. Edits (Submit / Undo / Redo) run the cache update and return the
// tiles that changed as plain snapshots (CPTileData.h); the road-drawing tool session is reached through ToolSession().
// The update runs synchronously on the calling thread (milestone M2); it is a single private function
// (RunCacheUpdate) so a worker can take it over without touching callers (milestone M4).
// It must not know about actors, components, materials, input or the viewport.

#include "CPFrame.h"
#include "CPMeshData.h"
#include "CPSimApi.h"
#include "CPTileData.h"
#include "CPToolSession.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

struct FCPSimWorldConfig
{
	std::string DataDir;           // absolute <repo>/data; empty = no region pack, so no roads
	std::string RegionId = "uk";
	std::uint64_t Seed = 1;
	double SizeM = 2048.0;         // square test world centred on the sim origin
	double CellM = 4.0;            // terrain heightfield cell size
	bool bVegetationLayers = true; // register the world_cover / vegetation sim layers (not rendered yet)
};

struct FCPTerrainGrid
{
	double MinX = 0, MinY = 0, MaxX = 0, MaxY = 0;  // sim metres
	double CellM = 1;
	int Nx = 0, Ny = 0;                              // heightfield samples
};

struct FCPGroundPick
{
	CPFrame::FD3 PointSim;     // sim metres
	double Distance = 0;       // metres along the ray
	double HeightM = 0;        // == PointSim.Z
	double SlopeRiseRun = 0;   // gradient magnitude at the point
};

struct FCPGraphStats
{
	std::size_t Nodes = 0, Edges = 0;
	std::size_t UndoDepth = 0;
	bool bCanUndo = false, bCanRedo = false;
};

/** What one edit did: success, why not, the tiles it changed and the graph afterwards. */
struct FCPEditResult
{
	bool bOk = false;
	std::string Error;
	FCPChangeSet Changes;
	FCPGraphStats Graph;
	double UpdateMs = 0;       // wall time of the command and the cache update
};

class CITYPAINTERSIM_API FCPSimWorld
{
public:
	// Null on failure (Error filled). A missing region pack is only a warning (see Warning): the world then has no roads.
	static std::unique_ptr<FCPSimWorld> Create(const FCPSimWorldConfig& Config, std::string& Error, std::string& Warning);
	~FCPSimWorld();

	// ---- terrain ----
	const FCPTerrainGrid& TerrainGrid() const { return Grid; }
	double HeightM(double SimX, double SimY) const;
	double SlopeRiseRun(double SimX, double SimY) const;

	// Ray in the sim frame. False when it misses the terrain.
	bool PickGround(CPFrame::FD3 OriginSim, CPFrame::FD3 DirSim, FCPGroundPick& Out) const;

	// Terrain tile covering heightfield cells [CellI0, CellI0+Cells] x [CellJ0, CellJ0+Cells] (clamped to the grid).
	// Vertices are shared between neighbouring tiles at the borders and normals are taken from the full field, so
	// tiles join without lighting seams.
	void BuildTerrainTile(int CellI0, int CellJ0, int Cells, FCPMeshSection& Out) const;

	// ---- roads ----
	/** False when the region pack did not load; RoadsUnavailableReason() says why. */
	bool HasRoads() const;
	const std::string& RoadsUnavailableReason() const { return RoadsReason; }
	/** Road types of the loaded region pack, sorted by id. */
	std::vector<FCPRoadTypeInfo> RoadTypes() const;

	/** Executes a command (one undo step), updates the world cache and reports the changed tiles. */
	FCPEditResult Submit(FCPCommandHandle Command);
	FCPEditResult Undo();
	FCPEditResult Redo();

	FCPGraphStats GraphStats() const;
	std::uint64_t WorldVersion() const;
	/** Wall time of the first cache build done in Create (all layers on the empty graph). */
	double PrimeMs() const { return PrimeMsValue; }
	/** Every currently exported tile as Added: lets a render layer that starts late catch up. */
	FCPChangeSet SnapshotAll() const;

	/** The road-drawing tool (one per sim world); null without roads. */
	FCPToolSession* ToolSession() const;

private:
	FCPSimWorld();
	FCPEditResult RunCacheUpdate(double ExtraMs);  // THE function a worker takes over in M4

	struct FImpl;
	std::unique_ptr<FImpl> Impl;
	FCPTerrainGrid Grid;
	std::string RoadsReason;
	double PrimeMsValue = 0;
};
