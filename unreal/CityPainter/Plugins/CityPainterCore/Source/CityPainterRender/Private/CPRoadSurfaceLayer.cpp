#include "CPRoadSurfaceLayer.h"

FCPRoadSurfaceLayer::FCPRoadSurfaceLayer() : FCPTileSectionLayer(CPLayer::RoadMesh) {}

UMaterialInterface* FCPRoadSurfaceLayer::MaterialFor(const FCPKeyedSection& Section)
{
	return Materials.ForSurface(UTF8_TO_TCHAR(Section.Key.c_str()));
}
