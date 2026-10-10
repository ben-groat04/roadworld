#pragma once
// Minimal regular-grid terrain heightfield. Sample (i, j) sits at
// origin + (i * cell, j * cell); heights are metres. Queries outside the grid clamp to
// the border. Tiling (20 km+ maps) comes later.
#include <vector>

#include "cp/vec.h"

namespace cp {

class Heightfield {
public:
    Heightfield() = default;
    Heightfield(Vec2 origin, double cell, int nx, int ny, float fill = 0.0f);

    Vec2 origin() const { return origin_; }
    double cell() const { return cell_; }
    int nx() const { return nx_; }
    int ny() const { return ny_; }
    bool empty() const { return heights_.empty(); }

    float at(int i, int j) const { return heights_[index(i, j)]; }
    void set(int i, int j, float h) { heights_[index(i, j)] = h; }
    const std::vector<float>& heights() const { return heights_; }

    // Bilinear interpolation (exact for planes). 0 for an empty grid.
    double height(double x, double y) const;

private:
    std::size_t index(int i, int j) const {
        return static_cast<std::size_t>(j) * static_cast<std::size_t>(nx_) + static_cast<std::size_t>(i);
    }
    Vec2 origin_;
    double cell_ = 1.0;
    int nx_ = 0, ny_ = 0;
    std::vector<float> heights_;
};

}  // namespace cp
