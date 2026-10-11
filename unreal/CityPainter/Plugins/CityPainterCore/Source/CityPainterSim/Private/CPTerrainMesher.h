#pragma once

// Turns a rectangle of a cp::Heightfield into a FCPMeshSection in the UE frame (positions, smooth normals from the
// whole field, UVs in sim metres, tangents). Pure function; it must not know about tile lifetime, materials or Unreal.

#include "CPMeshData.h"

namespace cp
{
class Heightfield;
}

namespace CPTerrain
{
void BuildTile(const cp::Heightfield& Field, int CellI0, int CellJ0, int Cells, FCPMeshSection& Out);
}
