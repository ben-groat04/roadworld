#pragma once

// FCPMarkingsLayer: draws the sim's markings layer (lane lines, edge lines, hatching: the paint) as tile meshes, one
// section per paint colour, lifted 3 mm above the surface by the sim. It must not know about the road surface layer.

#include "CPTileSectionLayer.h"

class CITYPAINTERRENDER_API FCPMarkingsLayer : public FCPTileSectionLayer
{
public:
	FCPMarkingsLayer();
	static FName Name() { return FName(TEXT("Markings")); }
	virtual FName LayerName() const override { return Name(); }

protected:
	virtual UMaterialInterface* MaterialFor(const FCPKeyedSection& Section) override;
};
