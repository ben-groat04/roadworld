#pragma once
// World vectors are double precision metres (20 km+ maps). x east, y north, z up.
#include <cmath>

namespace cp {

struct Vec2 {
    double x = 0, y = 0;
    constexpr Vec2 operator+(Vec2 o) const { return {x + o.x, y + o.y}; }
    constexpr Vec2 operator-(Vec2 o) const { return {x - o.x, y - o.y}; }
    constexpr Vec2 operator*(double s) const { return {x * s, y * s}; }
    constexpr double dot(Vec2 o) const { return x * o.x + y * o.y; }
    constexpr double cross(Vec2 o) const { return x * o.y - y * o.x; }
    double length() const { return std::hypot(x, y); }
};

struct Vec3 {
    double x = 0, y = 0, z = 0;
    constexpr Vec2 xy() const { return {x, y}; }
};

inline constexpr double kPi = 3.14159265358979323846;

// Wraps an angle to (-pi, pi].
inline double wrap_angle(double a) {
    a = std::fmod(a + kPi, 2 * kPi);
    if (a <= 0) a += 2 * kPi;
    return a - kPi;
}

}  // namespace cp
