#include "cp/vertical_profile.h"

#include <algorithm>
#include <cmath>

namespace cp {
namespace {

double span_grade(const VerticalProfile::Break& a, const VerticalProfile::Break& b) {
    return (b.z - a.z) / (b.s - a.s);
}

}  // namespace

VerticalProfile VerticalProfile::build(const std::vector<double>& s, const std::vector<double>& z,
                                       const VerticalCurveParams& p) {
    VerticalProfile vp;
    const std::size_t n = std::min(s.size(), z.size());
    for (std::size_t i = 0; i < n; ++i) vp.breaks.push_back({s[i], z[i], 0});
    for (std::size_t i = 1; i + 1 < n; ++i) {
        const double g0 = span_grade(vp.breaks[i - 1], vp.breaks[i]);
        const double g1 = span_grade(vp.breaks[i], vp.breaks[i + 1]);
        const double dg_pct = (g1 - g0) * 100;
        if (std::abs(dg_pct) < 1e-9) continue;
        const double K = dg_pct < 0 ? p.k_crest : p.k_sag;  // grade falling = crest
        double L = std::max(K * std::abs(dg_pct), p.min_length);
        const double room = std::min(vp.breaks[i].s - vp.breaks[i - 1].s, vp.breaks[i + 1].s - vp.breaks[i].s);
        vp.breaks[i].length = std::min(L, room);  // half on each side, at most half of each span
    }
    return vp;
}

double VerticalProfile::z(double s) const {
    if (breaks.empty()) return 0;
    if (breaks.size() == 1) return breaks[0].z;
    // Locate the span containing s on the tangent (piecewise linear) profile.
    std::size_t i = 0;
    while (i + 2 < breaks.size() && s > breaks[i + 1].s) ++i;
    const Break& a = breaks[i];
    const Break& b = breaks[i + 1];
    double zt = a.z + span_grade(a, b) * (s - a.s);
    // Apply the parabolic correction from whichever vertical curve covers s.
    for (std::size_t j = 1; j + 1 < breaks.size(); ++j) {
        const Break& v = breaks[j];
        const double h = v.length / 2;
        if (h <= 0 || s < v.s - h || s > v.s + h) continue;
        const double g0 = span_grade(breaks[j - 1], v), g1 = span_grade(v, breaks[j + 1]);
        const double x = s - (v.s - h);  // distance into the curve
        // Parabola: z = zBVC + g0 x + (g1 - g0) x^2 / (2L). Offset from the tangent it replaces:
        const double zbvc = v.z - g0 * h;
        const double zc = zbvc + g0 * x + (g1 - g0) * x * x / (2 * v.length);
        return zc;
    }
    return zt;
}

double VerticalProfile::grade(double s) const {
    if (breaks.size() < 2) return 0;
    for (std::size_t j = 1; j + 1 < breaks.size(); ++j) {
        const Break& v = breaks[j];
        const double h = v.length / 2;
        if (h <= 0 || s < v.s - h || s > v.s + h) continue;
        const double g0 = span_grade(breaks[j - 1], v), g1 = span_grade(v, breaks[j + 1]);
        return g0 + (g1 - g0) * (s - (v.s - h)) / v.length;
    }
    std::size_t i = 0;
    while (i + 2 < breaks.size() && s > breaks[i + 1].s) ++i;
    return span_grade(breaks[i], breaks[i + 1]);
}

}  // namespace cp
