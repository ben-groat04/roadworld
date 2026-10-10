#include "cp/clothoid.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace cp {
namespace {

// 10-point Gauss-Legendre on [-1, 1].
constexpr std::array<double, 5> kGlX = {0.1488743389816312, 0.4333953941292472, 0.6794095682990244,
                                        0.8650633666889845, 0.9739065285171717};
constexpr std::array<double, 5> kGlW = {0.2955242247147529, 0.2692667193099963, 0.2190863625159820,
                                        0.1494513491505806, 0.0666713443086881};

// Integrates f(t) * (cos phi(t), sin phi(t)) over [0, 1], phi(t) = a/2 t^2 + b t + c,
// with weight t^m. Subintervals keep the phase change per piece small, so the
// result is accurate to ~1e-14 and deterministic for a given input.
void integrate(double a, double b, double c, int m, double& X, double& Y) {
    const double phase_span = std::abs(a) + std::abs(b);  // bounds |phi'(t)| = |a t + b| on [0, 1]
    const int n = std::clamp(static_cast<int>(std::ceil(phase_span / 0.5)), 1, 4096);
    const double h = 1.0 / n;
    X = Y = 0;
    for (int i = 0; i < n; ++i) {
        const double mid = (i + 0.5) * h, half = 0.5 * h;
        for (std::size_t j = 0; j < kGlX.size(); ++j) {
            for (double sgn : {-1.0, 1.0}) {
                const double t = mid + sgn * half * kGlX[j];
                const double phi = (0.5 * a * t + b) * t + c;
                const double w = kGlW[j] * half * (m == 0 ? 1.0 : m == 1 ? t : t * t);
                X += w * std::cos(phi);
                Y += w * std::sin(phi);
            }
        }
    }
}

}  // namespace

void fresnel_xy(double a, double b, double c, double& X, double& Y) { integrate(a, b, c, 0, X, Y); }

Vec2 Clothoid::point(double s) const {
    if (s == 0) return start;
    // Substitute t = u / s: int_0^s cos(theta(u)) du = s * int_0^1 cos(theta(s t)) dt.
    double X, Y;
    fresnel_xy(dk * s * s, k0 * s, heading0, X, Y);
    return {start.x + s * X, start.y + s * Y};
}

std::optional<Clothoid> fit_clothoid_g1(Vec2 p0, double theta0, Vec2 p1, double theta1) {
    const Vec2 d = p1 - p0;
    const double r = d.length();
    if (!(r > 1e-9)) return std::nullopt;
    const double phi = std::atan2(d.y, d.x);
    const double phi0 = wrap_angle(theta0 - phi);
    const double phi1 = wrap_angle(theta1 - phi);
    const double delta = phi1 - phi0;

    // Solve g(A) = Y(2A, delta - A, phi0) = 0 by damped Newton. 3(phi0 + phi1)
    // is the paper's simple initial guess and converges for |phi0|, |phi1| < pi.
    double A = 3.0 * (phi0 + phi1);
    for (int it = 0; it < 50; ++it) {
        double X, Y, X1, Y1, X2, Y2;
        const double a = 2 * A, b = delta - A;
        integrate(a, b, phi0, 0, X, Y);
        integrate(a, b, phi0, 1, X1, Y1);
        integrate(a, b, phi0, 2, X2, Y2);
        // dY/dA = int cos(phi(t)) * (t^2 - t) dt
        const double dg = X2 - X1;
        if (std::abs(Y) < 1e-14) break;
        if (dg == 0) break;
        double step = Y / dg;
        step = std::clamp(step, -1.0, 1.0);  // damp wild steps on hard cases
        A -= step;
        if (std::abs(step) < 1e-15 * std::max(1.0, std::abs(A))) break;
    }
    double X, Y;
    integrate(2 * A, delta - A, phi0, 0, X, Y);
    if (!(X > 0)) return std::nullopt;
    const double L = r / X;
    Clothoid c;
    c.start = p0;
    c.heading0 = theta0;
    c.length = L;
    c.k0 = (delta - A) / L;
    c.dk = 2 * A / (L * L);
    return c;
}

}  // namespace cp
