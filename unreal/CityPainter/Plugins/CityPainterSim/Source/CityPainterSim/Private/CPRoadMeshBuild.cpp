// UE-free: standard library + sim_core only. See CPRoadMeshBuild.h for the frame / winding rule.
#if defined(_MSC_VER)
// sim_core headers pull <filesystem> (region_pack.h); the module is built with exceptions off (see KNOWN_RISKS.md B).
#pragma warning(push)
#pragma warning(disable : 4244 4267 4100 4189 4456 4457 4458 4459 4668 4996 5038 4127 4702 4530 4577)
#endif
#include "CPRoadMeshBuild.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "cp/command.h"
#include "cp/edge_sampling.h"
#include "cp/junction.h"
#include "cp/junction_roundabout.h"
#include "cp/lanes.h"
#include "cp/markings.h"
#include "cp/region_pack.h"
#include "cp/render_mesh.h"
#include "cp/road_graph.h"
#include "cp/test_layouts.h"

namespace CPBuild
{

struct FWorld
{
	std::shared_ptr<const cp::RegionPack> Region;
	cp::RoadGraph Graph;
	cp::CommandStack<cp::RoadGraph> Stack{Graph};
	std::optional<cp::MarkingRules> Rules;
	cp::JunctionOverrides Overrides;
};

void FWorldDeleter::operator()(FWorld* W) const
{
	delete W;
}

FWorldPtr CreateWorld()
{
	return FWorldPtr(new FWorld());
}

bool LoadData(FWorld& World, const FSettings& Settings, std::string& Error, std::string& Warning)
{
	auto Pack = cp::load_region_pack_by_id(Settings.DataDir, Settings.RegionId);
	if (!Pack)
	{
		Error = "region pack '" + Settings.RegionId + "' from data dir '" + Settings.DataDir + "': " + Pack.error;
		return false;
	}
	World.Region = std::make_shared<const cp::RegionPack>(*Pack);
	World.Graph.set_region(World.Region);

	auto Rules = cp::load_marking_rules_by_id(Settings.DataDir, Settings.RegionId);
	if (!Rules)
	{
		Warning = "marking rules not loaded (markings disabled): " + Rules.error;
		World.Rules.reset();
	}
	else
	{
		World.Rules = *Rules;
	}
	return true;
}

bool BuildLayout(FWorld& World, ELayout Layout, std::string& Error)
{
	if (!World.Region)
	{
		Error = "region pack not loaded";
		return false;
	}
	switch (Layout)
	{
	case ELayout::Interchange:
		return cp::build_test_layout(cp::TestLayout::Interchange, World.Graph, World.Stack, *World.Region, Error);
	case ELayout::Empty:
		return true;
	}
	Error = "unknown layout";
	return false;
}

bool Undo(FWorld& World)
{
	return World.Stack.undo();
}

bool Redo(FWorld& World)
{
	return World.Stack.redo();
}

std::vector<std::array<double, 3>> NodePositions(const FWorld& World)
{
	std::vector<std::array<double, 3>> Out;
	for (const auto& Pair : World.Graph.nodes())
	{
		Out.push_back({Pair.second.pos.x, Pair.second.pos.y, Pair.second.pos.z});
	}
	return Out;
}

namespace
{

constexpr double MetresToCm = 100.0;

// sim metres -> UE cm (the swap documented in the header).
std::array<double, 3> SimToUE(double X, double Y, double Z)
{
	return {Y * MetresToCm, X * MetresToCm, Z * MetresToCm};
}

std::array<double, 3> SimToUE(const cp::Vec3& P)
{
	return SimToUE(P.x, P.y, P.z);
}

using TileIndex = std::map<std::pair<std::int64_t, std::int64_t>, std::size_t>;

FTile& TileFor(FOutput& Out, TileIndex& Index, cp::TileKey Key, double TileSizeM)
{
	const auto Id = std::make_pair(Key.i, Key.j);
	auto It = Index.find(Id);
	if (It != Index.end())
	{
		return Out.Tiles[It->second];
	}
	FTile Tile;
	Tile.TileX = Key.i;
	Tile.TileY = Key.j;
	const cp::Vec3 O = cp::tile_origin(Key, TileSizeM);
	const auto UE = SimToUE(O);
	Tile.Origin[0] = UE[0];
	Tile.Origin[1] = UE[1];
	Tile.Origin[2] = UE[2];
	Index[Id] = Out.Tiles.size();
	Out.Tiles.push_back(std::move(Tile));
	return Out.Tiles.back();
}

FSection& SectionFor(std::vector<FSection>& Sections, const std::string& Key)
{
	for (FSection& S : Sections)
	{
		if (S.Key == Key)
		{
			return S;
		}
	}
	Sections.emplace_back();
	Sections.back().Key = Key;
	return Sections.back();
}

// Surface chunk buffers (float metres relative to the chunk origin) -> UE section.
void ConvertBuffers(const cp::MeshBuffers& Src, FSection& Dst)
{
	const std::size_t N = Src.positions.size();
	Dst.Position.resize(N);
	Dst.Normal.resize(N);
	Dst.TangentX.resize(N);
	Dst.TangentFlipY.resize(N);
	Dst.UV0.resize(N);
	Dst.Colour.resize(N);
	for (std::size_t i = 0; i < N; ++i)
	{
		const cp::Float3& P = Src.positions[i];
		Dst.Position[i] = {static_cast<float>(static_cast<double>(P.y) * MetresToCm),
						   static_cast<float>(static_cast<double>(P.x) * MetresToCm),
						   static_cast<float>(static_cast<double>(P.z) * MetresToCm)};
		const cp::Float3& Nrm = Src.normals[i];
		Dst.Normal[i] = {Nrm.y, Nrm.x, Nrm.z};
		const cp::Float4& T = Src.tangents[i];
		Dst.TangentX[i] = {T.y, T.x, T.z};
		Dst.TangentFlipY[i] = T.w > 0.f ? 1 : 0;  // see header: sim w=+1 needs bFlipTangentY in UE
		Dst.UV0[i] = {Src.uv0[i].x, Src.uv0[i].y};
		Dst.Colour[i] = Src.colours.empty() ? std::array<std::uint8_t, 4>{0, 0, 0, 255} : Src.colours[i];
	}
	const std::size_t Base = Dst.Indices.size();
	Dst.Indices.resize(Base + Src.indices.size());
	for (std::size_t i = 0; i + 2 < Src.indices.size(); i += 3)
	{
		// Winding rule: swap indices 1 and 2.
		Dst.Indices[Base + i] = static_cast<std::int32_t>(Src.indices[i]);
		Dst.Indices[Base + i + 1] = static_cast<std::int32_t>(Src.indices[i + 2]);
		Dst.Indices[Base + i + 2] = static_cast<std::int32_t>(Src.indices[i + 1]);
	}
}

void AddLine(FOutput& Out, EDebugKind Kind, const std::vector<cp::Vec3>& Pts, double LiftM = 0.15)
{
	if (Pts.size() < 2)
	{
		return;
	}
	FDebugLine L;
	L.Kind = Kind;
	for (const cp::Vec3& P : Pts)
	{
		L.Points.push_back(SimToUE(P.x, P.y, P.z + LiftM));
	}
	Out.DebugLines.push_back(std::move(L));
}

void CollectDebug(FWorld& W, const cp::JunctionSet& Junctions, const FSettings& S, FOutput& Out)
{
	if (S.bDebugConnectors || S.bDebugTrimLines || S.bDebugDegraded)
	{
		for (const auto& Pair : Junctions.junctions)
		{
			const cp::Junction& J = Pair.second;
			if (S.bDebugConnectors)
			{
				for (const cp::LaneConnector& C : J.connectors)
				{
					if (C.enabled)
					{
						AddLine(Out, EDebugKind::Connector, C.path);
					}
				}
			}
			if (S.bDebugTrimLines)
			{
				for (std::size_t a = 0; a < J.arms.size() && a < J.trims.size(); ++a)
				{
					const cp::JunctionArm& Arm = J.arms[a];
					const cp::ArmPoint L = cp::arm_point(Arm, J.trims[a], Arm.half_width_left);
					const cp::ArmPoint R = cp::arm_point(Arm, J.trims[a], -Arm.half_width_right);
					AddLine(Out, EDebugKind::TrimLine, {L.pos, R.pos});
				}
			}
			if (S.bDebugDegraded && J.degraded)
			{
				for (const auto& Ring : J.carriageway)
				{
					std::vector<cp::Vec3> Pts;
					for (const cp::Vec2& P : Ring)
					{
						Pts.push_back({P.x, P.y, J.pos.z});
					}
					if (!Pts.empty())
					{
						Pts.push_back(Pts.front());
					}
					AddLine(Out, EDebugKind::DegradedJunction, Pts, 0.3);
				}
			}
		}
	}
	if (S.bDebugLaneCentrelines)
	{
		for (const auto& Pair : W.Graph.edges())
		{
			const cp::Edge& E = Pair.second;
			const cp::RoadType* RT = W.Region->road_type(E.road_type);
			if (!RT)
			{
				continue;
			}
			for (const cp::Lane& Lane : cp::instantiate_lanes(E, *RT, W.Region->driving_side))
			{
				if (Lane.dir == cp::TravelDir::None)
				{
					continue;
				}
				const cp::OffsetPolyline PL = cp::sample_offset(E, Lane.offset);
				std::vector<cp::Vec3> Pts;
				for (const cp::EdgeSample& Sm : PL.samples)
				{
					Pts.push_back(Sm.pos);
				}
				AddLine(Out, EDebugKind::LaneCentre, Pts, 0.1);
			}
		}
	}
}

}  // namespace

FOutput BuildRender(FWorld& W, const FSettings& S)
{
	FOutput Out;
	if (!W.Region)
	{
		Out.Messages.push_back("BuildRender: region pack not loaded");
		return Out;
	}
	const cp::RegionPack& Region = *W.Region;
	Out.Stats.Nodes = W.Graph.nodes().size();
	Out.Stats.Edges = W.Graph.edges().size();

	// Junctions and roundabouts (the same two calls the tools fixture uses).
	cp::JunctionSet Junctions = cp::build_plain_junctions(W.Graph, Region, W.Overrides);
	cp::RoundaboutResult Roundabouts = cp::apply_roundabouts(Junctions, W.Graph, Region, W.Overrides);
	Out.Stats.Junctions = Junctions.junctions.size();
	Out.Stats.Roundabouts = Roundabouts.layouts.size();
	for (const auto& Pair : Junctions.junctions)
	{
		const cp::Junction& J = Pair.second;
		if (J.kind == cp::JunctionKind::Roundabout)
		{
			++Out.Stats.RoundaboutRingJunctions;
		}
		if (J.degraded)
		{
			++Out.Stats.DegradedJunctions;
			Out.Messages.push_back("junction at node " + std::to_string(Pair.first.value) + " degraded: " + J.degraded_reason);
		}
	}
	for (const cp::RoundaboutProblem& P : Roundabouts.problems)
	{
		Out.Messages.push_back("roundabout problem: " + P.what);
	}
	for (const cp::RoundaboutLayout& L : Roundabouts.layouts)
	{
		if (!L.ok)
		{
			Out.Messages.push_back("roundabout layout not ok: " + L.error);
		}
	}

	// Surface chunks.
	cp::RenderMeshParams RP;
	RP.chord_tolerance = S.ChordToleranceM;
	cp::ChunkParams CPar;
	CPar.tile_size = S.TileSizeM;
	const std::map<cp::TileKey, cp::MeshChunk> Chunks =
		cp::build_mesh_chunks(W.Graph, Region, Junctions, &Roundabouts.layouts, RP, CPar);

	TileIndex Index;
	for (const auto& Pair : Chunks)
	{
		const cp::MeshChunk& Chunk = Pair.second;
		FTile& Tile = TileFor(Out, Index, Chunk.tile, S.TileSizeM);
		// The chunk origin is the tile south-west corner at z = 0: it must equal TileFor's origin.
		const auto ChunkOrigin = SimToUE(Chunk.origin);
		if (std::abs(ChunkOrigin[0] - Tile.Origin[0]) > 1e-6 || std::abs(ChunkOrigin[1] - Tile.Origin[1]) > 1e-6 || std::abs(ChunkOrigin[2] - Tile.Origin[2]) > 1e-6)
		{
			Out.Messages.push_back("chunk origin differs from tile_origin() for tile " + std::to_string(Chunk.tile.i) + "," + std::to_string(Chunk.tile.j));
		}
		for (const auto& SPair : Chunk.surfaces)
		{
			FSection& Sec = SectionFor(Tile.Surfaces, SPair.first);
			ConvertBuffers(SPair.second, Sec);
			Out.Stats.TrianglesBySurface[SPair.first] += SPair.second.indices.size() / 3;
		}
	}

	// Markings.
	if (S.bGenerateMarkings && W.Rules)
	{
		cp::MarkingParams MP;  // defaults: no placed crossings, no stop lines
		const cp::MarkingSet Set = cp::generate_markings(W.Graph, Region, *W.Rules, Junctions, MP).merged();
		for (const std::string& Warn : Set.warnings)
		{
			Out.Messages.push_back("marking warning: " + Warn);
		}
		for (const std::string& C : Set.colours)
		{
			const auto It = W.Rules->colours.find(C);
			if (It != W.Rules->colours.end())
			{
				Out.MarkingColours[C] = {static_cast<float>(It->second.rgb[0]), static_cast<float>(It->second.rgb[1]),
										 static_cast<float>(It->second.rgb[2])};
			}
		}
		for (const cp::Marking& M : Set.marks)
		{
			const std::vector<std::uint32_t> Tri = cp::triangulate_marking(Set, M);
			if (Tri.empty())
			{
				continue;
			}
			const auto Pts = Set.points(M);
			double Cx = 0, Cy = 0;
			for (const cp::Vec3& P : Pts)
			{
				Cx += P.x;
				Cy += P.y;
			}
			Cx /= static_cast<double>(Pts.size());
			Cy /= static_cast<double>(Pts.size());
			const cp::TileKey Key = cp::tile_of({Cx, Cy}, S.TileSizeM);
			FTile& Tile = TileFor(Out, Index, Key, S.TileSizeM);
			const cp::Vec3 O = cp::tile_origin(Key, S.TileSizeM);
			FSection& Sec = SectionFor(Tile.Markings, Set.colour_id(M));
			const auto CIt = Out.MarkingColours.find(Sec.Key);
			if (CIt != Out.MarkingColours.end())
			{
				Sec.Rgb[0] = CIt->second[0];
				Sec.Rgb[1] = CIt->second[1];
				Sec.Rgb[2] = CIt->second[2];
			}
			const auto Base = static_cast<std::int32_t>(Sec.Position.size());
			for (const cp::Vec3& P : Pts)
			{
				// Relative in double, then to float: precision comes from the tile origin.
				Sec.Position.push_back({static_cast<float>((P.y - O.y) * MetresToCm), static_cast<float>((P.x - O.x) * MetresToCm),
										static_cast<float>(P.z * MetresToCm + S.MarkingLiftCm)});
				Sec.Normal.push_back({0.f, 0.f, 1.f});
				Sec.TangentX.push_back({0.f, 1.f, 0.f});
				Sec.TangentFlipY.push_back(0);
				Sec.UV0.push_back({static_cast<float>(P.x - O.x), static_cast<float>(P.y - O.y)});
				Sec.Colour.push_back({255, 255, 255, 255});
			}
			for (std::size_t i = 0; i + 2 < Tri.size(); i += 3)
			{
				const std::uint32_t A = Tri[i], B = Tri[i + 1], C = Tri[i + 2];
				// Make the triangle counter-clockwise in sim plan first (do not trust the ring orientation),
				// then apply the winding rule (swap 1 and 2).
				const cp::Vec2 Ab{Pts[B].x - Pts[A].x, Pts[B].y - Pts[A].y};
				const cp::Vec2 Ac{Pts[C].x - Pts[A].x, Pts[C].y - Pts[A].y};
				const bool bCcw = Ab.cross(Ac) >= 0.0;
				Sec.Indices.push_back(Base + static_cast<std::int32_t>(A));
				Sec.Indices.push_back(Base + static_cast<std::int32_t>(bCcw ? C : B));
				Sec.Indices.push_back(Base + static_cast<std::int32_t>(bCcw ? B : C));
				++Out.Stats.MarkTriangles;
				Out.Stats.TrianglesByMarkingColour[Sec.Key] += 1;
			}
			++Out.Stats.Marks;
		}
	}

	CollectDebug(W, Junctions, S, Out);
	Out.Stats.Tiles = Out.Tiles.size();
	return Out;
}

}  // namespace CPBuild
#if defined(_MSC_VER)
#pragma warning(pop)
#endif
