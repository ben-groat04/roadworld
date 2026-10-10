#include "cp/heightfield.h"

#include <algorithm>
#include <cmath>

namespace cp {

Heightfield::Heightfield(Vec2 origin, double cell, int nx, int ny, float fill)
    : origin_(origin),
      cell_(cell),
      nx_(std::max(nx, 0)),
      ny_(std::max(ny, 0)),
      heights_(static_cast<std::size_t>(nx_) * static_cast<std::size_t>(ny_), fill) {}

double Heightfield::height(double x, double y) const {
    if (heights_.empty()) return 0;
    const double fx = std::clamp((x - origin_.x) / cell_, 0.0, static_cast<double>(nx_ - 1));
    const double fy = std::clamp((y - origin_.y) / cell_, 0.0, static_cast<double>(ny_ - 1));
    const int i0 = std::min(static_cast<int>(std::floor(fx)), std::max(nx_ - 2, 0));
    const int j0 = std::min(static_cast<int>(std::floor(fy)), std::max(ny_ - 2, 0));
    const int i1 = std::min(i0 + 1, nx_ - 1);
    const int j1 = std::min(j0 + 1, ny_ - 1);
    const double tx = fx - i0, ty = fy - j0;
    const double a = at(i0, j0) * (1 - tx) + at(i1, j0) * tx;
    const double b = at(i0, j1) * (1 - tx) + at(i1, j1) * tx;
    return a * (1 - ty) + b * ty;
}

}  // namespace cp
