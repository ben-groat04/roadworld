#include "cp/clothoid_chain.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace cp {
namespace {

// Tangent heading at b of the circle through a, b, c (falls back to chord a->c).
double circle_tangent(Vec2 a, Vec2 b, Vec2 c) {
    const Vec2 u = b - a, v = c - b;
    const double lu = u.length(), lv = v.length();
    // Weighted bisector of the two chord directions approximates the circle tangent.
    const double hu = std::atan2(u.y, u.x), hv = std::atan2(v.y, v.x);
    return hu + wrap_angle(hv - hu) * lu / (lu + lv);
}

bool solve_dense(std::vector<double>& A, std::vector<double>& b, int n) {
    for (int col = 0; col < n; ++col) {
        int piv = col;
        for (int r = col + 1; r < n; ++r)
            if (std::abs(A[static_cast<std::size_t>(r * n + col)]) >
                std::abs(A[static_cast<std::size_t>(piv * n + col)]))
                piv = r;
        if (std::abs(A[static_cast<std::size_t>(piv * n + col)]) < 1e-300) return false;
        if (piv != col) {
            for (int k = 0; k < n; ++k)
                std::swap(A[static_cast<std::size_t>(col * n + k)], A[static_cast<std::size_t>(piv * n + k)]);
            std::swap(b[static_cast<std::size_t>(col)], b[static_cast<std::size_t>(piv)]);
        }
        for (int r = col + 1; r < n; ++r) {
            const double f = A[static_cast<std::size_t>(r * n + col)] / A[static_cast<std::size_t>(col * n + col)];
            for (int k = col; k < n; ++k)
                A[static_cast<std::size_t>(r * n + k)] -= f * A[static_cast<std::size_t>(col * n + k)];
            b[static_cast<std::size_t>(r)] -= f * b[static_cast<std::size_t>(col)];
        }
    }
    for (int r = n - 1; r >= 0; --r) {
        double s = b[static_cast<std::size_t>(r)];
        for (int k = r + 1; k < n; ++k) s -= A[static_cast<std::size_t>(r * n + k)] * b[static_cast<std::size_t>(k)];
        b[static_cast<std::size_t>(r)] = s / A[static_cast<std::size_t>(r * n + r)];
    }
    return true;
}

struct Problem {
    const std::vector<Vec2>& pts;
    ChainEnd start, end;
    std::vector<int> free_idx;  // point indices whose heading is unknown

    bool build(const std::vector<double>& headings, std::vector<Clothoid>& spans) const {
        spans.clear();
        for (std::size_t i = 0; i + 1 < pts.size(); ++i) {
            auto c = fit_clothoid_g1(pts[i], headings[i], pts[i + 1], headings[i + 1]);
            if (!c) return false;
            spans.push_back(*c);
        }
        return true;
    }

    // One residual per unknown: curvature jump at interior points, end curvature at natural ends.
    bool residual(const std::vector<double>& headings, std::vector<double>& r) const {
        std::vector<Clothoid> spans;
        if (!build(headings, spans)) return false;
        r.clear();
        const std::size_t n = pts.size();
        for (int i : free_idx) {
            const auto ui = static_cast<std::size_t>(i);
            if (ui == 0) r.push_back(spans.front().k0);
            else if (ui == n - 1) r.push_back(spans.back().curvature(spans.back().length));
            else r.push_back(spans[ui - 1].curvature(spans[ui - 1].length) - spans[ui].k0);
        }
        return true;
    }
};

double norm2(const std::vector<double>& v) {
    double s = 0;
    for (double x : v) s += x * x;
    return s;
}

}  // namespace

double ClothoidChain::length() const {
    double L = 0;
    for (const auto& s : spans) L += s.length;
    return L;
}

std::optional<ClothoidChain> fit_clothoid_chain(const std::vector<Vec2>& points, ChainEnd start, ChainEnd end) {
    const std::size_t n = points.size();
    if (n < 2) return std::nullopt;
    for (std::size_t i = 0; i + 1 < n; ++i)
        if ((points[i + 1] - points[i]).length() < 1e-6) return std::nullopt;

    // Initial headings: circle tangents inside, chord directions at the ends.
    std::vector<double> h(n);
    for (std::size_t i = 1; i + 1 < n; ++i) h[i] = circle_tangent(points[i - 1], points[i], points[i + 1]);
    auto chord = [&](std::size_t a, std::size_t b) {
        const Vec2 d = points[b] - points[a];
        return std::atan2(d.y, d.x);
    };
    // A natural end's tangent mirrors the turn at its neighbour, roughly (a parabola end).
    h[0] = n > 2 ? chord(0, 1) - wrap_angle(h[1] - chord(0, 1)) : chord(0, 1);
    h[n - 1] = n > 2 ? chord(n - 2, n - 1) + wrap_angle(chord(n - 2, n - 1) - h[n - 2]) : chord(0, 1);
    if (start.heading) h[0] = *start.heading;
    if (end.heading) h[n - 1] = *end.heading;
    // Keep each heading on the same branch as its chords so G1 fits stay short.
    for (std::size_t i = 1; i < n; ++i) {
        const double c = chord(i - 1, i);
        h[i] = c + wrap_angle(h[i] - c);
    }
    h[0] = chord(0, 1) + wrap_angle(h[0] - chord(0, 1));

    Problem prob{points, start, end, {}};
    for (std::size_t i = 0; i < n; ++i) {
        if (i == 0 && start.heading) continue;
        if (i == n - 1 && end.heading) continue;
        prob.free_idx.push_back(static_cast<int>(i));
    }

    const std::vector<double> h_initial = h;
    const int m = static_cast<int>(prob.free_idx.size());
    std::vector<double> r;
    bool ok = prob.residual(h, r);
    bool converged = ok && (m == 0 || norm2(r) < 1e-20);

    for (int it = 0; ok && !converged && it < 40; ++it) {
        // Finite-difference Jacobian (chains are short; dense is fine).
        std::vector<double> J(static_cast<std::size_t>(m * m));
        const double eps = 1e-7;
        for (int j = 0; j < m; ++j) {
            std::vector<double> hp = h, rp;
            hp[static_cast<std::size_t>(prob.free_idx[static_cast<std::size_t>(j)])] += eps;
            if (!prob.residual(hp, rp)) { ok = false; break; }
            for (int i = 0; i < m; ++i)
                J[static_cast<std::size_t>(i * m + j)] =
                    (rp[static_cast<std::size_t>(i)] - r[static_cast<std::size_t>(i)]) / eps;
        }
        if (!ok) break;
        std::vector<double> step = r;
        if (!solve_dense(J, step, m)) { ok = false; break; }

        // Backtracking line search on |r|^2; cap each heading change.
        double t = 1.0;
        const double r0 = norm2(r);
        bool improved = false;
        for (int ls = 0; ls < 20; ++ls, t *= 0.5) {
            std::vector<double> hn = h, rn;
            for (int j = 0; j < m; ++j) {
                const double d = std::clamp(t * step[static_cast<std::size_t>(j)], -0.5, 0.5);
                hn[static_cast<std::size_t>(prob.free_idx[static_cast<std::size_t>(j)])] -= d;
            }
            if (prob.residual(hn, rn) && norm2(rn) < r0) {
                h = hn;
                r = rn;
                improved = true;
                break;
            }
        }
        if (!improved) break;
        converged = norm2(r) < 1e-20;
    }

    ClothoidChain chain;
    if (!ok || !prob.build(h, chain.spans)) {
        // Fall back to G1 with the initial headings.
        h = h_initial;
        if (!prob.build(h, chain.spans)) return std::nullopt;
    }
    chain.g2 = ok && converged;
    for (std::size_t i = 1; i < chain.spans.size(); ++i) {
        const auto& a = chain.spans[i - 1];
        chain.max_kink = std::max(chain.max_kink, std::abs(a.curvature(a.length) - chain.spans[i].k0));
    }
    // Treat a numerically tiny kink as G2 even if the tolerance loop stopped early.
    if (chain.max_kink < 1e-9) chain.g2 = true;
    return chain;
}

}  // namespace cp
