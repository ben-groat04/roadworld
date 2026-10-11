// FCPSimWorld facade implementation. Includes sim_core; keep the includes of cp/ headers inside Private/.
#include "CPSimWorld.h"

#include "CPCommandImpl.h"
#include "CPSimCore.h"
#include "CPTerrainMesher.h"
#include "CPTileConvert.h"
#include "CPToolSessionImpl.h"

#include <chrono>
#include <cmath>

namespace
{
double NowMs()
{
	using Clock = std::chrono::steady_clock;
	return std::chrono::duration<double, std::milli>(Clock::now().time_since_epoch()).count();
}
}  // namespace

// Declaration order = construction order; destruction runs in reverse, so the session (which points into the graph and
// the terrain) goes first and the cache before the graph it reads.
struct FCPSimWorld::FImpl
{
	cp::TestWorldBundle Bundle;
	std::shared_ptr<const cp::RegionPack> Region;
	std::shared_ptr<const cp::MarkingRules> Rules;
	cp::RoadGraph Graph;
	std::unique_ptr<cp::CommandStack<cp::RoadGraph>> Stack;
	std::unique_ptr<cp::WorldCache> Cache;
	std::unique_ptr<FCPToolSession> Session;
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
	FImpl& I = *W->Impl;

	// Region data first: the vegetation rules (cover source of the test world) come from it.
	std::shared_ptr<const cp::VegetationRules> VegRules;
	if (Config.DataDir.empty())
	{
		W->RoadsReason = "no data directory given";
	}
	else
	{
		auto Region = cp::load_region_pack_by_id(Config.DataDir, Config.RegionId);
		if (!Region)
		{
			W->RoadsReason = "region pack '" + Config.RegionId + "' not loaded from " + Config.DataDir + ": " + Region.error;
			Warning = W->RoadsReason;
		}
		else
		{
			I.Region = std::make_shared<const cp::RegionPack>(*Region);
			auto Rules = cp::load_marking_rules_by_id(Config.DataDir, Config.RegionId);
			if (Rules)
			{
				I.Rules = std::make_shared<const cp::MarkingRules>(*Rules);
			}
			else
			{
				Warning += "marking rules not loaded (roads get no markings): " + Rules.error + "\n";
			}
			if (Config.bVegetationLayers)
			{
				auto Veg = cp::load_vegetation_rules_by_id(Config.DataDir, Config.RegionId, I.Region.get());
				if (Veg)
				{
					VegRules = std::make_shared<const cp::VegetationRules>(*Veg);
				}
				else
				{
					Warning += "vegetation rules not loaded (no vegetation layers): " + Veg.error + "\n";
				}
			}
		}
	}

	cp::TestWorldParams P;
	P.seed = Config.Seed;
	P.size_m = Config.SizeM;
	P.cell_m = Config.CellM;
	I.Bundle = cp::make_test_world(P, VegRules);
	const cp::Heightfield& Hf = *I.Bundle.terrain;
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

	if (!I.Region)
	{
		return W;
	}

	// The road graph, its command stack and the world cache with its layers.
	I.Graph.set_region(I.Region);
	I.Stack = std::make_unique<cp::CommandStack<cp::RoadGraph>>(I.Graph);
	cp::WorldCacheParams WP;
	WP.mesh.chord_tolerance = 0.02;  // as the legacy road build (near LOD)
	WP.markings = cp::MarkingParams::for_mesh_tolerance(WP.mesh.chord_tolerance);
	WP.marking_lift_m = 0.003;       // 3 mm, as the legacy build
	I.Cache = std::make_unique<cp::WorldCache>(I.Graph, I.Region, I.Rules, WP);
	if (VegRules)
	{
		cp::VegetationParams VP;
		cp::apply_to_vegetation_params(I.Bundle, Config.Seed, VP);
		std::string LayerError;
		if (!cp::add_vegetation_layers(*I.Cache, VegRules, VP, &LayerError))
		{
			Warning += "vegetation layers not registered: " + LayerError + "\n";
		}
	}
	// Build every layer once on the empty graph now (not at the first click); the result is empty or not rendered.
	const double T0 = NowMs();
	I.Cache->update();
	W->PrimeMsValue = NowMs() - T0;

	auto SessionImpl = std::make_unique<FCPToolSession::FImpl>(I.Graph, *I.Bundle.terrain);
	SessionImpl->Region = I.Region.get();
	I.Session = std::make_unique<FCPToolSession>(std::move(SessionImpl));
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

bool FCPSimWorld::HasRoads() const
{
	return Impl->Cache != nullptr;
}

std::vector<FCPRoadTypeInfo> FCPSimWorld::RoadTypes() const
{
	std::vector<FCPRoadTypeInfo> Out;
	if (Impl->Region)
	{
		for (const auto& [Id, Type] : Impl->Region->road_types)  // std::map: sorted by id
		{
			Out.push_back({Id, Type.name, Type.dual, Type.two_way, Type.roundabout_ring});
		}
	}
	return Out;
}

FCPGraphStats FCPSimWorld::GraphStats() const
{
	FCPGraphStats S;
	S.Nodes = Impl->Graph.nodes().size();
	S.Edges = Impl->Graph.edges().size();
	if (Impl->Stack)
	{
		S.UndoDepth = Impl->Stack->undo_depth();
		S.bCanUndo = Impl->Stack->can_undo();
		S.bCanRedo = Impl->Stack->can_redo();
	}
	return S;
}

std::uint64_t FCPSimWorld::WorldVersion() const
{
	return Impl->Cache ? Impl->Cache->world_version() : 0;
}

FCPChangeSet FCPSimWorld::SnapshotAll() const
{
	return Impl->Cache ? CPTile::SnapshotAll(*Impl->Cache) : FCPChangeSet();
}

FCPToolSession* FCPSimWorld::ToolSession() const
{
	return Impl->Session.get();
}

FCPEditResult FCPSimWorld::Submit(FCPCommandHandle Command)
{
	FCPEditResult Failed;
	if (!HasRoads())
	{
		Failed.Error = "roads unavailable: " + RoadsReason;
		return Failed;
	}
	if (!Command || !Command->Command)
	{
		Failed.Error = "no command";
		Failed.Graph = GraphStats();
		return Failed;
	}
	const double T0 = NowMs();
	const std::string Name = Command->Name;
	if (!Impl->Stack->execute(std::move(Command->Command)))
	{
		Failed.Error = "command '" + Name + "' was refused by the road graph";
		Failed.Graph = GraphStats();
		return Failed;
	}
	return RunCacheUpdate(NowMs() - T0);
}

FCPEditResult FCPSimWorld::Undo()
{
	FCPEditResult Failed;
	if (!HasRoads())
	{
		Failed.Error = "roads unavailable: " + RoadsReason;
		return Failed;
	}
	const double T0 = NowMs();
	if (!Impl->Stack->undo())
	{
		Failed.Error = "nothing to undo";
		Failed.Graph = GraphStats();
		return Failed;
	}
	return RunCacheUpdate(NowMs() - T0);
}

FCPEditResult FCPSimWorld::Redo()
{
	FCPEditResult Failed;
	if (!HasRoads())
	{
		Failed.Error = "roads unavailable: " + RoadsReason;
		return Failed;
	}
	const double T0 = NowMs();
	if (!Impl->Stack->redo())
	{
		Failed.Error = "nothing to redo";
		Failed.Graph = GraphStats();
		return Failed;
	}
	return RunCacheUpdate(NowMs() - T0);
}

FCPEditResult FCPSimWorld::RunCacheUpdate(double ExtraMs)
{
	// The graph changed: consume its dirty sets, rebuild what depends on them and publish immutable snapshots.
	const double T0 = NowMs();
	const cp::ChangeList Changes = Impl->Cache->update();
	FCPEditResult R;
	R.bOk = true;
	R.Changes = CPTile::ConvertChanges(*Impl->Cache, Changes);
	R.Graph = GraphStats();
	R.UpdateMs = ExtraMs + (NowMs() - T0);
	Impl->Session->Invalidate();
	return R;
}
