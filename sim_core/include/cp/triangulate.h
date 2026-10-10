#pragma once
// Polygon triangulation (ear clipping with hole bridging), shared by the render mesh builder.
// Exception-free, no dependencies beyond vec.h.
#include <array>
#include <cstdint>
#include <vector>

#include "cp/vec.h"

namespace cp {

// Triangulates polygons with holes. `rings` are loops of indices into `pts` (implicit close). Input
// orientation is irrelevant: nesting by containment decides the roles (even depth = outer ring, odd
// = hole of its smallest container) and every outer is treated counter-clockwise, every hole
// clockwise. Holes touching their outer ring at a vertex are supported. Triangles come out
// counter-clockwise and use only the given points. Returns false when some part could not be
// triangulated (the covered area differs from the polygon area); the triangles found are still
// appended, so a caller can keep the partial result but should flag it.
bool triangulate_polygons(const std::vector<Vec2>& pts, const std::vector<std::vector<std::uint32_t>>& rings,
                          std::vector<std::array<std::uint32_t, 3>>& out);

}  // namespace cp
