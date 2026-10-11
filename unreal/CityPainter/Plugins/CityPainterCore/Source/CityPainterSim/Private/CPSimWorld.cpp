// FCPSimWorld facade implementation. Includes sim_core; keep the includes of cp/ headers inside Private/.
#include "CPSimWorld.h"

#include "CPTerrainMesher.h"

#include "cp/ground_pick.h"
#include "cp/heightfield.h"
#include "cp/region_pack.h"
#include "cp/test_world.h"

#include <cmath>

struct FCPSimWorld::FImpl
{
	cp::TestWorldBundle Bundle;
};

FCPSimWorld::FCPSimWorld() : Impl(std::make_unique<FImpl>()) {}
FCPSimWorld::~FCPSimWorld() = default;

std::unique_ptr<FCPSimWorld> FCPSimWorld::Create(const FCPSimWorldConfig& Config, std::string& Error, std::string& Warning)
{
	if (Config.SizeM <= 0 || Config.CellM <= 0 || Config.SizeM / Config.CellM < 2)
	{
		Error = "world size and cell size must be positive and give at least two cells";
		return nullptr;
	}
	std::unique_ptr<FCPSimWorld> W(new FCPSimWorld());
	cp::TestWorldParams P;
	P.seed = Config.Seed;
	P.size_m = Config.SizeM;
	P.cell_m = Config.CellM;
	W->Impl->Bundle = cp::make_test_world(P, nullptr);
	const cp::Heightfield& Hf = *W->Impl->Bundle.terrain;
	if (Hf.empty())
	{
		Error = "test world produced an empty heightfield";
		return nullptr;
	}
	W->Grid.MinX = Hf.origin().x;
	W->Grid.MinY = Hf.origin().y;
	W->Grid.CellM = Hf.cell();
	W->Grid.Nx = Hf.nx();
	W->Grid.Ny = Hf.ny();
	W->Grid.MaxX = W->Grid.MinX + (Hf.nx() - 1) * Hf.cell();
	W->Grid.MaxY = W->Grid.MinY + (Hf.ny() - 1) * Hf.cell();

	if (!Config.DataDir.empty())
	{
		auto Region = cp::load_region_pack_by_id(Config.DataDir, Config.RegionId);
		if (!Region)
		{
			Warning = "region pack '" + Config.RegionId + "' not loaded from " + Config.DataDir + ": " + Region.error;
		}
	}
	return W;
}

double FCPSimWorld::HeightM(double SimX, double SimY) const { return Impl->Bundle.terrain->height(SimX, SimY); }

double FCPSimWorld::SlopeRiseRun(double SimX, double SimY) const { return Impl->Bundle.world->slope(SimX, SimY); }

bool FCPSimWorld::PickGround(CPFrame::FD3 OriginSim, CPFrame::FD3 DirSim, FCPGroundPick& Out) const
{
	cp::Ray Ray{{OriginSim.X, OriginSim.Y, OriginSim.Z}, {DirSim.X, DirSim.Y, DirSim.Z}};
	const auto Hit = cp::pick_ground(*Impl->Bundle.terrain, Ray);
	if (!Hit)
	{
		return false;
	}
	Out.PointSim = {Hit->point.x, Hit->point.y, Hit->point.z};
	Out.Distance = Hit->distance;
	Out.HeightM = Hit->point.z;
	Out.SlopeRiseRun = Impl->Bundle.world->slope(Hit->point.x, Hit->point.y);
	return true;
}

void FCPSimWorld::BuildTerrainTile(int CellI0, int CellJ0, int Cells, FCPMeshSection& Out) const
{
	CPTerrain::BuildTile(*Impl->Bundle.terrain, CellI0, CellJ0, Cells, Out);
}
