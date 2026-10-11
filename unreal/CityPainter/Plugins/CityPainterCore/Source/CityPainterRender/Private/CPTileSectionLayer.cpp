#include "CPTileSectionLayer.h"

#include "CityPainterRender.h"

#include "Engine/World.h"
#include "GameFramework/Actor.h"

FCPTileSectionLayer::FCPTileSectionLayer(const char* InSimLayer) : SimLayer(UTF8_TO_TCHAR(InSimLayer)) {}

void FCPTileSectionLayer::Build(const FCPRenderContext& Context)
{
	Teardown();
	Root = Context.Root;  // meshes appear later, as change sets arrive
}

void FCPTileSectionLayer::Teardown()
{
	for (TPair<FIntPoint, TUniquePtr<ICPTileMesh>>& Pair : Tiles)
	{
		Pair.Value->Clear();
	}
	Tiles.Reset();
}

void FCPTileSectionLayer::OnTilesChanged(const FCPChangeSet& Changes)
{
	AActor* Owner = Root.Get();
	if (!Owner)
	{
		return;
	}
	int32 Added = 0, Replaced = 0, Removed = 0, Sections = 0;
	for (const FCPLayerTile& Tile : Changes.Tiles)
	{
		if (SimLayer != UTF8_TO_TCHAR(Tile.Layer.c_str()))
		{
			continue;
		}
		if (Tile.TileX < MIN_int32 || Tile.TileX > MAX_int32 || Tile.TileY < MIN_int32 || Tile.TileY > MAX_int32)
		{
			UE_LOG(LogCPRender, Error, TEXT("Layer '%s': tile %lld,%lld is outside the supported range"), *SimLayer, Tile.TileX, Tile.TileY);
			continue;
		}
		const FIntPoint Key(static_cast<int32>(Tile.TileX), static_cast<int32>(Tile.TileY));
		TUniquePtr<ICPTileMesh> Fresh;
		if (Tile.Change != ECPTileChange::Removed)
		{
			Fresh = ICPTileMesh::Create(*Owner, FName(*FString::Printf(TEXT("%s_%d_%d_v%llu"), *SimLayer, Key.X, Key.Y, static_cast<unsigned long long>(Tile.Version))));
			int32 SectionIndex = 0;
			for (const FCPKeyedSection& Section : Tile.Sections)
			{
				if (!Section.Mesh.IsEmpty())
				{
					Fresh->SetSection(SectionIndex++, Section.Mesh, MaterialFor(Section));
				}
			}
			Sections += SectionIndex;
		}
		// The replacement exists before the old mesh goes (the order the asynchronous swap of M4 needs).
		TUniquePtr<ICPTileMesh>* Old = Tiles.Find(Key);
		if (Old)
		{
			(*Old)->Clear();
			Tiles.Remove(Key);
			if (Fresh)
			{
				++Replaced;
			}
			else
			{
				++Removed;
			}
		}
		else if (Fresh)
		{
			++Added;
		}
		if (Fresh)
		{
			Tiles.Add(Key, MoveTemp(Fresh));
		}
	}
	if (Added + Replaced + Removed > 0)
	{
		UE_LOG(LogCPRender, Log, TEXT("Layer '%s': tiles +%d ~%d -%d (%d sections), %d shown, world v%llu"), *SimLayer, Added, Replaced, Removed, Sections, Tiles.Num(), static_cast<unsigned long long>(Changes.WorldVersion));
	}
}
