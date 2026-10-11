#pragma once
// Fits a G2 (curvature-continuous) chain of clothoids through clicked points.
// Each span is a G1 Hermite clothoid; the headings at the points are solved so
// curvature matches across every interior point.
#include <optional>
#include <vector>

#include "cp/clothoid.h"

namespace cp {

struct ChainEnd {
    // Pinned heading (snapping, tangent continuation from an existing road).
    // When empty the end is "natural": zero curvature there.
    std::optional<double> heading;
};

struct ClothoidChain {
    std::vector<Clothoid> spans;
    bool g2 = false;          // false if the solver fell back to G1 headings
    double max_kink = 0;      // largest curvature jump at an interior point (1/m)

    double length() const;
};

// nullopt when no sane chain exists: two points on top of each other, or a stroke whose spans would need to be
// more than 20 times their chord (it doubles back onto an earlier point; the G1 solve then spirals for ever).
std::optional<ClothoidChain> fit_clothoid_chain(const std::vector<Vec2>& points, ChainEnd start = {},
                                                ChainEnd end = {});

}  // namespace cp
