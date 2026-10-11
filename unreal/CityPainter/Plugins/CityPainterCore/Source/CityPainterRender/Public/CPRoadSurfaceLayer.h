#pragma once

// FCPRoadSurfaceLayer: draws the sim's road_mesh layer (carriageway, kerb, footway, verge, junction surfaces) as tile
// meshes, one section per surface class. It must not know about markings, the tool, or how tile meshes are stored.

#include "CPTileSectionLayer.h"

class CITYPAINTERRENDER_API FCPRoadSurfaceLayer : public FCPTileSectionLayer
{
public:
	FCPRoadSurfaceLayer();
	static FName Name() { return FName(TEXT("RoadSurface")); }
	virtual FName LayerName() const override { return Name(); }

protected:
	virtual UMaterialInterface* MaterialFor(const FCPKeyedSection& Section) override;
};
