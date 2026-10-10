#pragma once
// Vertical profile of an edge: constant gradient between control points, with a
// parabolic vertical curve centred on each grade break so gradient changes are
// smooth. Curve length L = K * |delta grade in %|, K from the road type.
#include <vector>

namespace cp {

struct VerticalCurveParams {
    double k_crest = 30;  // m per % grade change
    double k_sag = 20;
    double min_length = 0;  // m; 0 lets tiny breaks stay tiny
};

struct VerticalProfile {
    struct Break {
        double s;       // arc length of the control point
        double z;       // authored height at the control point
        double length;  // vertical curve length centred on s (0 at the ends)
    };
    std::vector<Break> breaks;  // sorted by s, first at s = 0

    // Builds curve lengths from K values, shrinking any that would overlap their
    // neighbours (each curve may use at most half of each adjacent span).
    static VerticalProfile build(const std::vector<double>& s, const std::vector<double>& z,
                                 const VerticalCurveParams& p);

    double z(double s) const;
    double grade(double s) const;  // dz/ds (rise over run, not %)
};

}  // namespace cp
