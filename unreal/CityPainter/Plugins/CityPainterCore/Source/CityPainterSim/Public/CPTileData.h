#pragma once

// Plain tile snapshots handed from the Sim module to anything that draws (no Unreal types, no sim_core types): one
// FCPLayerTile per (sim layer, tile), holding one or more mesh sections (road surface: one per surface class;
// markings: one per paint colour). A change set lists the tiles one world update added, replaced or removed. It must
// not know how a section is rendered, and it is the only thing a render layer needs to follow the sim.

#include "CPMeshData.h"
#include "CPSimApi.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

// Names of the sim layers whose tiles are exported (the names are sim_core's WorldCache layer names).
namespace CPLayer
{
inline constexpr const char* RoadMesh = "road_mesh";
inline constexpr const char* Markings = "markings";
}  // namespace CPLayer

struct FCPKeyedSection
{
	std::string Key;                       // surface class ("asphalt", "kerb", ...) or marking colour id ("white", ...)
	std::array<float, 3> Rgb = {1, 1, 1};  // markings: sRGB 0..1 from the marking rules; surfaces: unused
	FCPMeshSection Mesh;
};

enum class ECPTileChange : std::uint8_t
{
	Added,
	Changed,
	Removed,  // Sections is empty
};

struct FCPLayerTile
{
	std::string Layer;                    // CPLayer::*
	std::int64_t TileX = 0, TileY = 0;    // sim tile key (east, north) on the 256 m grid
	std::uint64_t Version = 0;            // the world version at which this tile's content last changed
	ECPTileChange Change = ECPTileChange::Added;
	std::vector<FCPKeyedSection> Sections;
};

struct FCPChangeSet
{
	std::uint64_t WorldVersion = 0;
	std::vector<FCPLayerTile> Tiles;
	std::vector<std::string> Warnings;    // problems met while converting (nothing is dropped silently)

	bool IsEmpty() const { return Tiles.empty(); }
	std::size_t Count(const char* Layer, ECPTileChange Change) const
	{
		std::size_t N = 0;
		for (const FCPLayerTile& T : Tiles)
		{
			N += (T.Layer == Layer && T.Change == Change) ? 1 : 0;
		}
		return N;
	}
};
