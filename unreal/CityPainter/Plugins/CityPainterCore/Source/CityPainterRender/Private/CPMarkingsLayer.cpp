#include "CPMarkingsLayer.h"

FCPMarkingsLayer::FCPMarkingsLayer() : FCPTileSectionLayer(CPLayer::Markings) {}

UMaterialInterface* FCPMarkingsLayer::MaterialFor(const FCPKeyedSection& Section)
{
	return Materials.ForMarking(UTF8_TO_TCHAR(Section.Key.c_str()));
}
