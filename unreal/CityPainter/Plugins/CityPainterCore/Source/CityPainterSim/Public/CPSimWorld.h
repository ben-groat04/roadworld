#pragma once

// FCPSimWorld: the engine-facing facade over sim_core and the ONLY place (with the Private/ files of this module)
// that includes cp/ headers. Plain C++: no UObjects, no Unreal types. Queries are in the SIM frame (metres,
// doubles); mesh output is in the UE frame (see CPFrame.h). M1 content: the seeded test world (terrain), ground
// picking and terrain tile meshes. Roads, the command stack and the world cache arrive here in later milestones.
// It must not know about actors, components, materials, input or the viewport.

#include "CPFrame.h"
#include "CPMeshData.h"
#include "CPSimApi.h"

#include <cstdint>
#include <memory>
#include <string>

struct FCPSimWorldConfig
{
	std::string DataDir;           // absolute <repo>/data; empty = skip the region pack check
	std::string RegionId = "uk";
	std::uint64_t Seed = 1;
	double SizeM = 2048.0;         // square test world centred on the sim origin
	double CellM = 4.0;            // terrain heightfield cell size
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

class CITYPAINTERSIM_API FCPSimWorld
{
public:
	// Null on failure (Error filled). A missing region pack is only a warning (see Warning).
	static std::unique_ptr<FCPSimWorld> Create(const FCPSimWorldConfig& Config, std::string& Error, std::string& Warning);
	~FCPSimWorld();

	const FCPTerrainGrid& TerrainGrid() const { return Grid; }
	double HeightM(double SimX, double SimY) const;
	double SlopeRiseRun(double SimX, double SimY) const;

	// Ray in the sim frame. False when it misses the terrain.
	bool PickGround(CPFrame::FD3 OriginSim, CPFrame::FD3 DirSim, FCPGroundPick& Out) const;

	// Terrain tile covering heightfield cells [CellI0, CellI0+Cells] x [CellJ0, CellJ0+Cells] (clamped to the grid).
	// Vertices are shared between neighbouring tiles at the borders and normals are taken from the full field, so
	// tiles join without lighting seams.
	void BuildTerrainTile(int CellI0, int CellJ0, int Cells, FCPMeshSection& Out) const;

private:
	FCPSimWorld();
	struct FImpl;
	std::unique_ptr<FImpl> Impl;
	FCPTerrainGrid Grid;
};
