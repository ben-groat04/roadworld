#pragma once

// FCPTileSectionLayer: the shared bookkeeping of the layers that draw one sim layer's tiles as one mesh per tile with one
// section per surface class / paint colour (road surface, markings). It follows the sim by change sets: an added or
// changed tile gets a new mesh and the old one is dropped after it, a removed tile is cleared. Subclasses only name the
// sim layer and say which material a section gets. It must not know about roads, input or other layers.

#include "CPMaterialTable.h"
#include "CPTileData.h"
#include "ICPRenderLayer.h"
#include "ICPTileMesh.h"

class CITYPAINTERRENDER_API FCPTileSectionLayer : public ICPRenderLayer
{
public:
	FCPTileSectionLayer(const FCPTileSectionLayer&) = delete;   // exported class with a TMap of unique pointers: no implicit copy
	FCPTileSectionLayer& operator=(const FCPTileSectionLayer&) = delete;
	virtual void Build(const FCPRenderContext& Context) override;
	virtual void Teardown() override;
	virtual void OnTilesChanged(const FCPChangeSet& Changes) override;

	/** Number of tiles currently shown (for tests and logs). */
	int32 NumTiles() const { return Tiles.Num(); }

protected:
	explicit FCPTileSectionLayer(const char* InSimLayer);
	virtual UMaterialInterface* MaterialFor(const FCPKeyedSection& Section) = 0;

	FCPMaterialTable Materials;

private:
	const FString SimLayer;
	TWeakObjectPtr<AActor> Root;
	TMap<FIntPoint, TUniquePtr<ICPTileMesh>> Tiles;
};
