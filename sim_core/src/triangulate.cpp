#include "cp/triangulate.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace cp {

namespace {

using u32 = std::uint32_t;
using Tri = std::array<u32, 3>;
using Ring = std::vector<u32>;

constexpr double kInf = std::numeric_limits<double>::infinity();

double area2(Vec2 a, Vec2 b, Vec2 c) { return (b - a).cross(c - a); }

double ring_area(const std::vector<Vec2>& pts, const Ring& r) {
    double a = 0;
    for (std::size_t i = 0; i < r.size(); ++i) a += pts[r[i]].cross(pts[r[(i + 1) % r.size()]]);
    return a / 2;
}

double dist_point_segment(Vec2 p, Vec2 a, Vec2 b) {
    const Vec2 ab = b - a;
    const double l2 = ab.dot(ab);
    const double t = l2 > 0 ? std::clamp((p - a).dot(ab) / l2, 0.0, 1.0) : 0.0;
    return (p - (a + ab * t)).length();
}

bool point_in_ring(Vec2 p, const std::vector<Vec2>& pts, const Ring& r) {
    bool in = false;
    for (std::size_t i = 0, j = r.size() - 1; i < r.size(); j = i++) {
        const Vec2 a = pts[r[i]], b = pts[r[j]];
        if ((a.y > p.y) != (b.y > p.y) && p.x < (b.x - a.x) * (p.y - a.y) / (b.y - a.y) + a.x) in = !in;
    }
    return in;
}

// True when ring `inner` lies inside ring `outer` (vertices on the outer boundary are ignored; the
// majority of the others decides).
bool ring_inside(const std::vector<Vec2>& pts, const Ring& inner, const Ring& outer) {
    int in = 0, out = 0;
    for (u32 v : inner) {
        double d = kInf;
        for (std::size_t i = 0; i < outer.size(); ++i)
            d = std::min(d, dist_point_segment(pts[v], pts[outer[i]], pts[outer[(i + 1) % outer.size()]]));
        if (d < 1e-7) continue;
        (point_in_ring(pts[v], pts, outer) ? in : out)++;
    }
    return in > out;
}

bool same_point(Vec2 a, Vec2 b) { return std::abs(a.x - b.x) < 1e-9 && std::abs(a.y - b.y) < 1e-9; }

// Joins `hole` (clockwise) into `loop` (counter-clockwise) through a zero-width bridge.
bool bridge_hole(const std::vector<Vec2>& pts, Ring& loop, const Ring& hole) {
    std::size_t mi = 0;
    for (std::size_t i = 1; i < hole.size(); ++i)
        if (pts[hole[i]].x > pts[hole[mi]].x) mi = i;
    const Vec2 M = pts[hole[mi]];
    double best_x = kInf;
    std::size_t p = loop.size();
    for (std::size_t i = 0; i < loop.size(); ++i) {
        const Vec2 a = pts[loop[i]], b = pts[loop[(i + 1) % loop.size()]];
        if (same_point(a, M)) {  // touching vertex: bridge of zero length
            best_x = M.x;
            p = i;
            break;
        }
        if (a.y == b.y || !((a.y <= M.y && b.y >= M.y) || (b.y <= M.y && a.y >= M.y))) continue;
        const double x = a.x + (M.y - a.y) * (b.x - a.x) / (b.y - a.y);
        if (x >= M.x - 1e-9 && x < best_x) {
            best_x = x;
            p = a.x > b.x ? i : (i + 1) % loop.size();
        }
    }
    if (p == loop.size()) return false;
    if (!same_point(pts[loop[p]], M)) {
        // A vertex inside the triangle (M, hit, P) blocks the bridge; take the one closest in angle.
        const Vec2 I{best_x, M.y}, P = pts[loop[p]];
        double best_ang = kInf, best_d = kInf;
        std::size_t q = p;
        for (std::size_t i = 0; i < loop.size(); ++i) {
            const Vec2 Q = pts[loop[i]];
            if (i == p || Q.x < M.x - 1e-12 || same_point(Q, M)) continue;
            const double d1 = area2(M, I, Q), d2 = area2(I, P, Q), d3 = area2(P, M, Q);
            const bool in = (d1 >= 0 && d2 >= 0 && d3 >= 0) || (d1 <= 0 && d2 <= 0 && d3 <= 0);
            if (!in) continue;
            const double ang = std::abs(std::atan2(Q.y - M.y, Q.x - M.x)), d = (Q - M).length();
            if (ang < best_ang - 1e-12 || (ang <= best_ang + 1e-12 && d < best_d)) {
                best_ang = ang;
                best_d = d;
                q = i;
            }
        }
        p = q;
    }
    Ring merged(loop.begin(), loop.begin() + static_cast<std::ptrdiff_t>(p) + 1);
    for (std::size_t k = 0; k <= hole.size(); ++k) merged.push_back(hole[(mi + k) % hole.size()]);
    merged.push_back(loop[p]);
    merged.insert(merged.end(), loop.begin() + static_cast<std::ptrdiff_t>(p) + 1, loop.end());
    loop = std::move(merged);
    return true;
}

bool ear_clip(const std::vector<Vec2>& pts, const Ring& loop, std::vector<Tri>& out) {
    const int n = static_cast<int>(loop.size());
    if (n < 3) return true;
    std::vector<int> prv(loop.size()), nxt(loop.size());
    for (int i = 0; i < n; ++i) {
        prv[static_cast<std::size_t>(i)] = (i + n - 1) % n;
        nxt[static_cast<std::size_t>(i)] = (i + 1) % n;
    }
    const auto P = [&](int i) { return pts[loop[static_cast<std::size_t>(i)]]; };
    const auto N = [&](int i) { return nxt[static_cast<std::size_t>(i)]; };
    const auto Pv = [&](int i) { return prv[static_cast<std::size_t>(i)]; };
    const auto remove = [&](int i) {
        nxt[static_cast<std::size_t>(Pv(i))] = N(i);
        prv[static_cast<std::size_t>(N(i))] = Pv(i);
    };
    const auto emit = [&](int a, int b, int c) {
        out.push_back({loop[static_cast<std::size_t>(a)], loop[static_cast<std::size_t>(b)], loop[static_cast<std::size_t>(c)]});
    };
    const auto empty_ear = [&](int a, int b, int c) {
        const Vec2 A = P(a), B = P(b), C = P(c);
        for (int j = N(c); j != a; j = N(j)) {
            const Vec2 Q = P(j);
            if (same_point(Q, A) || same_point(Q, B) || same_point(Q, C)) continue;
            if (area2(A, B, Q) >= -1e-10 && area2(B, C, Q) >= -1e-10 && area2(C, A, Q) >= -1e-10) return false;
        }
        return true;
    };
    bool ok = true;
    int remaining = n, cur = 0;
    while (remaining > 3) {
        bool clipped = false;
        int i = cur;
        do {
            const int a = Pv(i), c = N(i);
            if (area2(P(a), P(i), P(c)) > 1e-7 && empty_ear(a, i, c)) {
                emit(a, i, c);
                remove(i);
                cur = c;
                --remaining;
                clipped = true;
                break;
            }
            i = N(i);
        } while (i != cur);
        if (clipped) continue;
        // No clean ear: drop a collinear vertex, else cut the sharpest convex one (and report it).
        int pick = -1;
        double best = -kInf;
        bool collinear = false;
        i = cur;
        do {
            const double cr = area2(P(Pv(i)), P(i), P(N(i)));
            if (std::abs(cr) <= 1e-7) {
                pick = i;
                collinear = true;
                break;
            }
            if (cr > best) {
                best = cr;
                pick = i;
            }
            i = N(i);
        } while (i != cur);
        if (pick < 0 || (!collinear && best <= 0)) return false;
        if (!collinear) {
            emit(Pv(pick), pick, N(pick));
            ok = false;
        }
        cur = N(pick);
        remove(pick);
        --remaining;
    }
    const int a = cur, b = N(a), c = N(b);
    if (area2(P(a), P(b), P(c)) > 1e-7) emit(a, b, c);
    return ok;
}

}  // namespace

bool triangulate_polygons(const std::vector<Vec2>& pts, const std::vector<Ring>& input, std::vector<Tri>& out) {
    std::vector<Ring> rings;
    for (const Ring& r : input) {
        if (r.size() < 3) continue;
        bool in_range = true;
        for (u32 v : r) in_range = in_range && v < pts.size() && std::isfinite(pts[v].x) && std::isfinite(pts[v].y);
        if (!in_range) return false;
        if (std::abs(ring_area(pts, r)) < 1e-9) continue;
        rings.push_back(r);
    }
    const std::size_t n = rings.size();
    // Nesting depth and parent (smallest container).
    std::vector<int> depth(n, 0), parent(n, -1);
    std::vector<double> area(n);
    for (std::size_t i = 0; i < n; ++i) area[i] = std::abs(ring_area(pts, rings[i]));
    for (std::size_t i = 0; i < n; ++i)
        for (std::size_t j = 0; j < n; ++j) {
            if (i == j || area[j] <= area[i] || !ring_inside(pts, rings[i], rings[j])) continue;
            ++depth[i];
            if (parent[i] < 0 || area[j] < area[static_cast<std::size_t>(parent[i])]) parent[i] = static_cast<int>(j);
        }
    bool ok = true;
    double expected = 0, covered = 0;
    const std::size_t first = out.size();
    for (std::size_t o = 0; o < n; ++o) {
        if (depth[o] % 2 != 0) continue;
        Ring loop = rings[o];
        if (ring_area(pts, loop) < 0) std::reverse(loop.begin(), loop.end());
        expected += area[o];
        std::vector<Ring> holes;
        for (std::size_t h = 0; h < n; ++h)
            if (depth[h] % 2 == 1 && parent[h] == static_cast<int>(o)) {
                Ring hr = rings[h];
                if (ring_area(pts, hr) > 0) std::reverse(hr.begin(), hr.end());
                expected -= area[h];
                holes.push_back(std::move(hr));
            }
        const auto max_x = [&](const Ring& h) {
            double m = -kInf;
            for (u32 v : h) m = std::max(m, pts[v].x);
            return m;
        };
        std::sort(holes.begin(), holes.end(), [&](const Ring& a, const Ring& b) { return max_x(a) > max_x(b); });
        for (const Ring& h : holes)
            if (!bridge_hole(pts, loop, h)) ok = false;
        if (!ear_clip(pts, loop, out)) ok = false;
    }
    for (std::size_t i = first; i < out.size(); ++i) covered += area2(pts[out[i][0]], pts[out[i][1]], pts[out[i][2]]) / 2;
    if (std::abs(covered - expected) > 1e-6 * std::max(1.0, expected) + 1e-9) ok = false;
    return ok;
}

}  // namespace cp
