// Conversion of world cache tile snapshots to plain tiles. The vertex conventions are those of the legacy road build
// (Legacy/CPRoadMeshBuild.cpp ConvertBuffers and the marking code), so the two paths look the same:
//  - positions: float metres relative to the chunk origin (tile south-west corner, z = 0) -> UE centimetres; the origin
//    itself goes to OriginUeCm (double);
//  - normals and tangent directions: the same axis swap without the scale, NOT negated; indices copied unchanged;
//  - tangent sign: sim w = +1 needs FProcMeshTangent::bFlipTangentY in Unreal;
//  - UV0 metres, UV1 (road surfaces) = metres from the lane centre and lane width;
//  - vertex colour: surfaces carry the cache's RGBA8 masks (R wear, G edge dirt, B variation, A 0 -> stored as given);
//    markings carry wear in R (Marking::wear 0..1 -> 0..255), G = B = 0, A = 255.
// The lift of markings above the surface is already inside the positions (WorldCacheParams::marking_lift_m).
#include "CPTileConvert.h"

#include "CPFrame.h"

#include <algorithm>
#include <cmath>

namespace CPTile
{
namespace
{
using FConvert = bool (*)(const cp::WorldCache&, cp::TileKey, FCPLayerTile&, std::vector<std::string>&);

void SetOrigin(FCPMeshSection& Mesh, const cp::Vec3& SimOrigin)
{
	const CPFrame::FD3 O = CPFrame::SimToUE({SimOrigin.x, SimOrigin.y, SimOrigin.z});
	Mesh.OriginUeCm[0] = O.X;
	Mesh.OriginUeCm[1] = O.Y;
	Mesh.OriginUeCm[2] = O.Z;
}

void PushPosition(FCPMeshSection& Mesh, const cp::Float3& P)
{
	const CPFrame::FD3 U = CPFrame::SimToUE({static_cast<double>(P.x), static_cast<double>(P.y), static_cast<double>(P.z)});
	Mesh.Position.push_back({static_cast<float>(U.X), static_cast<float>(U.Y), static_cast<float>(U.Z)});
}

void CopyIndices(FCPMeshSection& Mesh, const std::vector<std::uint32_t>& Indices)
{
	// Winding rule (CPFrame.h / CPRoadMeshBuild.h): counter-clockwise seen from outside in both frames; copy unchanged.
	Mesh.Indices.assign(Indices.begin(), Indices.end());
}

void ConvertSurface(const cp::MeshBuffers& Src, FCPMeshSection& Dst, std::vector<std::string>& Warnings, const std::string& Key)
{
	const std::size_t N = Src.positions.size();
	Dst.Position.reserve(N);
	Dst.Normal.reserve(N);
	Dst.TangentX.reserve(N);
	Dst.UV0.reserve(N);
	std::size_t Flipped = 0;
	for (std::size_t I = 0; I < N; ++I)
	{
		PushPosition(Dst, Src.positions[I]);
		const cp::Float3& Nrm = Src.normals[I];
		Dst.Normal.push_back({Nrm.y, Nrm.x, Nrm.z});
		const cp::Float4& T = Src.tangents[I];
		Dst.TangentX.push_back({T.y, T.x, T.z});
		Flipped += T.w > 0.f ? 1 : 0;
		Dst.UV0.push_back({Src.uv0[I].x, Src.uv0[I].y});
	}
	if (Src.uv1.size() == N)
	{
		for (const cp::Float2& V : Src.uv1)
		{
			Dst.UV1.push_back({V.x, V.y});
		}
	}
	if (Src.colours.size() == N)
	{
		Dst.Colour.assign(Src.colours.begin(), Src.colours.end());
	}
	// The flip flag is per section in the contract; sim_core writes one handedness for a whole surface class.
	if (Flipped != 0 && Flipped != N)
	{
		Warnings.push_back("surface '" + Key + "': mixed tangent handedness in one section; using the majority");
	}
	Dst.bTangentFlipY = Flipped * 2 >= N;
	CopyIndices(Dst, Src.indices);
}

bool ConvertRoadMesh(const cp::WorldCache& Cache, cp::TileKey Tile, FCPLayerTile& Out, std::vector<std::string>& Warnings)
{
	const std::shared_ptr<const cp::MeshChunk> Chunk = Cache.road_mesh(Tile);
	if (!Chunk)
	{
		return false;
	}
	for (const auto& [Class, Buffers] : Chunk->surfaces)
	{
		if (Buffers.indices.empty())
		{
			continue;
		}
		FCPKeyedSection& Sec = Out.Sections.emplace_back();
		Sec.Key = Class;
		SetOrigin(Sec.Mesh, Chunk->origin);
		ConvertSurface(Buffers, Sec.Mesh, Warnings, Class);
	}
	return true;
}

bool ConvertMarkings(const cp::WorldCache& Cache, cp::TileKey Tile, FCPLayerTile& Out, std::vector<std::string>&)
{
	const std::shared_ptr<const cp::MarkingChunk> Chunk = Cache.markings(Tile);
	if (!Chunk)
	{
		return false;
	}
	for (const auto& [Colour, Buffers] : Chunk->colours)
	{
		if (Buffers.indices.empty())
		{
			continue;
		}
		FCPKeyedSection& Sec = Out.Sections.emplace_back();
		Sec.Key = Colour;
		Sec.Rgb = Buffers.rgb;
		FCPMeshSection& Mesh = Sec.Mesh;
		SetOrigin(Mesh, Chunk->origin);
		const std::size_t N = Buffers.positions.size();
		for (std::size_t I = 0; I < N; ++I)
		{
			PushPosition(Mesh, Buffers.positions[I]);
			Mesh.Normal.push_back({0.f, 0.f, 1.f});
			Mesh.TangentX.push_back({0.f, 1.f, 0.f});  // +u = sim east = UE +Y
			Mesh.UV0.push_back({Buffers.uv0[I].x, Buffers.uv0[I].y});
			const auto Wear = static_cast<std::uint8_t>(std::lround(std::clamp(static_cast<double>(Buffers.wear[I]), 0.0, 1.0) * 255.0));
			Mesh.Colour.push_back({Wear, 0, 0, 255});
		}
		Mesh.bTangentFlipY = false;  // up-facing tangent frame; see the legacy marking code
		CopyIndices(Mesh, Buffers.indices);
	}
	return true;
}

struct FRow
{
	std::string_view Layer;
	FConvert Convert;
};

// THE table of exported layers: add a row (and a converter) for each new rendered layer.
constexpr FRow Table[] = {
	{cp::kLayerRoadMesh, &ConvertRoadMesh},
	{cp::kLayerMarkings, &ConvertMarkings},
};

const FRow* Find(std::string_view Layer)
{
	for (const FRow& Row : Table)
	{
		if (Row.Layer == Layer)
		{
			return &Row;
		}
	}
	return nullptr;
}

FCPLayerTile MakeTile(std::string_view Layer, cp::TileKey Tile, std::uint64_t Version, ECPTileChange Change)
{
	FCPLayerTile T;
	T.Layer = std::string(Layer);
	T.TileX = Tile.i;
	T.TileY = Tile.j;
	T.Version = Version;
	T.Change = Change;
	return T;
}
}  // namespace

bool IsExported(std::string_view Layer)
{
	return Find(Layer) != nullptr;
}

bool ConvertTile(const cp::WorldCache& Cache, std::string_view Layer, cp::TileKey Tile, std::uint64_t Version, ECPTileChange Change,
	FCPLayerTile& Out, std::vector<std::string>& Warnings)
{
	const FRow* Row = Find(Layer);
	if (!Row)
	{
		return false;
	}
	Out = MakeTile(Layer, Tile, Version, Change);
	return Row->Convert(Cache, Tile, Out, Warnings);
}

FCPChangeSet ConvertChanges(const cp::WorldCache& Cache, const cp::ChangeList& Changes)
{
	FCPChangeSet Set;
	Set.WorldVersion = Changes.world_version;
	for (const cp::LayerChanges& L : Changes.layers)
	{
		if (!IsExported(L.layer))
		{
			continue;
		}
		auto AddList = [&](const std::vector<cp::TileVersion>& List, ECPTileChange Change)
		{
			for (const cp::TileVersion& TV : List)
			{
				FCPLayerTile T;
				if (ConvertTile(Cache, L.layer, TV.tile, TV.version, Change, T, Set.Warnings))
				{
					Set.Tiles.push_back(std::move(T));
				}
				else
				{
					Set.Warnings.push_back("layer '" + L.layer + "' reports tile " + std::to_string(TV.tile.i) + "," + std::to_string(TV.tile.j) + " but has no snapshot of it");
				}
			}
		};
		AddList(L.added, ECPTileChange::Added);
		AddList(L.changed, ECPTileChange::Changed);
		for (const cp::TileKey& K : L.removed)
		{
			Set.Tiles.push_back(MakeTile(L.layer, K, Changes.world_version, ECPTileChange::Removed));
		}
	}
	return Set;
}

FCPChangeSet SnapshotAll(const cp::WorldCache& Cache)
{
	FCPChangeSet Set;
	Set.WorldVersion = Cache.world_version();
	for (const FRow& Row : Table)
	{
		for (const cp::TileKey& K : Cache.tiles(Row.Layer))
		{
			FCPLayerTile T;
			if (ConvertTile(Cache, Row.Layer, K, Cache.tile_version(Row.Layer, K), ECPTileChange::Added, T, Set.Warnings))
			{
				Set.Tiles.push_back(std::move(T));
			}
		}
	}
	return Set;
}
}  // namespace CPTile
