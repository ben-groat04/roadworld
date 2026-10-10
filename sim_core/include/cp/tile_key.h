#pragma once
// Integer tile address on a global grid, shared by every module that tiles the world (render
// meshes, graded ground, ...): 64-bit so 20 km+ maps stay exact. Tile (i, j) covers
// [i * size, (i + 1) * size) x [j * size, (j + 1) * size) in world metres (x east, y north).
#include <cmath>
#include <compare>
#include <cstdint>

#include "cp/vec.h"

namespace cp {

struct TileKey {
    std::int64_t i = 0, j = 0;
    friend constexpr auto operator<=>(const TileKey&, const TileKey&) = default;
};

// The tile containing plan point p for square tiles of `tile_size` metres.
inline TileKey tile_of(Vec2 p, double tile_size) {
    return {static_cast<std::int64_t>(std::floor(p.x / tile_size)), static_cast<std::int64_t>(std::floor(p.y / tile_size))};
}

// World position of the tile's south-west corner (z = 0).
inline Vec3 tile_origin(TileKey t, double tile_size) {
    return {static_cast<double>(t.i) * tile_size, static_cast<double>(t.j) * tile_size, 0};
}

}  // namespace cp
