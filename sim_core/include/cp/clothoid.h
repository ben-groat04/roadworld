#pragma once
// Clothoid (Euler spiral) primitives: curvature varies linearly with arc length,
// k(s) = k0 + dk * s. Lines (k0 = dk = 0) and arcs (dk = 0) are degenerate cases,
// so one type covers every piece of a road centreline.
#include <optional>

#include "cp/vec.h"

namespace cp {

struct Pose {
    Vec2 pos;
    double heading = 0;  // radians, CCW from +x
};

struct Clothoid {
    Vec2 start;
    double heading0 = 0;
    double k0 = 0;
    double dk = 0;       // curvature rate (1/m^2)
    double length = 0;

    double curvature(double s) const { return k0 + dk * s; }
    double heading(double s) const { return heading0 + k0 * s + 0.5 * dk * s * s; }
    Vec2 point(double s) const;
    Pose pose(double s) const { return {point(s), heading(s)}; }
    Pose end_pose() const { return pose(length); }
};

// G1 Hermite interpolation: the unique clothoid from (p0, theta0) to
// (p1, theta1) with the shortest length (Bertolazzi & Frego, 2015).
// Returns nullopt only for degenerate input (coincident points).
std::optional<Clothoid> fit_clothoid_g1(Vec2 p0, double theta0, Vec2 p1, double theta1);

// Generalised Fresnel integrals over t in [0, 1]:
//   X = int cos(a/2 t^2 + b t + c) dt,  Y = int sin(a/2 t^2 + b t + c) dt.
// Exposed for tests.
void fresnel_xy(double a, double b, double c, double& X, double& Y);

}  // namespace cp
