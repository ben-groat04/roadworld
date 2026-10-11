#pragma once

// Turns the world cache's immutable tile snapshots into the plain FCPLayerTile structs (UE frame and winding, see
// CPFrame.h). One row per exported layer in the table in CPTileConvert.cpp; a new rendered layer adds a row and a
// converter and touches no other layer. It must not know about the command stack, the tool session or Unreal.

#include "CPSimCore.h"
#include "CPTileData.h"

namespace CPTile
{
/** True when the sim layer has a converter (its tiles are exported to the renderer). */
bool IsExported(std::string_view Layer);

/** Converts the cache's current snapshot of (Layer, Tile) as an Added/Changed tile. False when the tile has no snapshot. */
bool ConvertTile(const cp::WorldCache& Cache, std::string_view Layer, cp::TileKey Tile, std::uint64_t Version, ECPTileChange Change,
	FCPLayerTile& Out, std::vector<std::string>& Warnings);

/** Converts everything one update() changed, for the exported layers only. */
FCPChangeSet ConvertChanges(const cp::WorldCache& Cache, const cp::ChangeList& Changes);

/** Every tile of the exported layers as Added (a render layer that starts after edits exist). */
FCPChangeSet SnapshotAll(const cp::WorldCache& Cache);
}  // namespace CPTile
