#pragma once

// Plain mesh payload handed from the Sim module to anything that draws (no Unreal types, no sim_core types).
// Positions are UE centimetres RELATIVE to OriginUeCm (double, UE frame, produced through CPFrame.h); normals and
// tangents are unit vectors already in the UE frame; indices keep sim_core's winding (counter-clockwise seen from
// outside, see CPFrame.h). It must not know how it is rendered (procedural mesh, instancing, ...).

#include "CPSimApi.h"

#include <array>
#include <cstdint>
#include <vector>

struct FCPMeshSection
{
	double OriginUeCm[3] = {0, 0, 0};
	std::vector<std::array<float, 3>> Position;
	std::vector<std::array<float, 3>> Normal;
	std::vector<std::array<float, 3>> TangentX;
	bool bTangentFlipY = true;                     // FProcMeshTangent::bFlipTangentY for the whole section
	std::vector<std::array<float, 2>> UV0;         // sim metres (east, north), absolute, so tiles share one texture frame
	std::vector<std::int32_t> Indices;

	std::size_t TriangleCount() const { return Indices.size() / 3; }
	bool IsEmpty() const { return Indices.empty(); }
};
