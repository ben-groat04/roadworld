#pragma once
// Small geometry and hashing helpers shared by the sim_core sources (header-only, no dependencies
// beyond vec.h). Only helpers whose copies were byte-for-byte equivalent live here; variants with
// a different tolerance, winding convention or signature stay local to their file.
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "cp/vec.h"

namespace cp {

// General-purpose tolerance for "is this zero" tests on lengths and arc positions (m).
inline constexpr double kEps = 1e-9;

// ---------------------------------------------------------------- Vec3 arithmetic

inline Vec3 operator+(Vec3 a, Vec3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
inline Vec3 operator-(Vec3 a, Vec3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
inline Vec3 operator*(Vec3 a, double s) { return {a.x * s, a.y * s, a.z * s}; }
inline Vec3 cross(Vec3 a, Vec3 b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
inline double dot(Vec3 a, Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline double length(Vec3 a) { return std::sqrt(dot(a, a)); }

// ---------------------------------------------------------------- 2D polygons and segments

// Signed area of a closed ring (implicit close): positive when counter-clockwise.
inline double signed_area(const std::vector<Vec2>& r) {
    double a = 0;
    for (std::size_t i = 0; i < r.size(); ++i) a += r[i].cross(r[(i + 1) % r.size()]);
    return a / 2;
}

// Distance from p to the closed segment ab; a degenerate segment (a == b) measures to the point a.
inline double dist_point_segment(Vec2 p, Vec2 a, Vec2 b) {
    const Vec2 ab = b - a;
    const double l2 = ab.dot(ab);
    const double t = l2 > 0 ? std::clamp((p - a).dot(ab) / l2, 0.0, 1.0) : 0.0;
    return (p - (a + ab * t)).length();
}

// ---------------------------------------------------------------- stable hashing

// splitmix64 finaliser: the same inputs give the same value on every platform and library.
inline std::uint64_t mix64(std::uint64_t z) {
    z += 0x9E3779B97F4A7C15ull;
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
}

// Uniform value in [0, 1) from the top 53 bits of the mixed key.
inline double hash01(std::uint64_t z) { return static_cast<double>(mix64(z) >> 11) * (1.0 / 9007199254740992.0); }
inline double hash01(std::uint64_t a, std::int64_t b) { return hash01(mix64(a) ^ static_cast<std::uint64_t>(b)); }

}  // namespace cp
