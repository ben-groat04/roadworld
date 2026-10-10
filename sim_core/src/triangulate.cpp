#include "cp/triangulate.h"

#include "cp/geom.h"

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

// Does segment p-q cross or touch segment a-b anywhere other than at p or q themselves?
bool blocks(Vec2 p, Vec2 q, Vec2 a, Vec2 b) {
    const auto side = [](Vec2 u, Vec2 v, Vec2 w) {
        const double c = (v - u).cross(w - u);
        return std::abs(c) < 1e-9 ? 0 : (c > 0 ? 1 : -1);
    };
    const auto on_seg = [](Vec2 u, Vec2 v, Vec2 w) {  // w on segment u-v (collinear assumed)
        return std::min(u.x, v.x) - 1e-9 <= w.x && w.x <= std::max(u.x, v.x) + 1e-9 &&
               std::min(u.y, v.y) - 1e-9 <= w.y && w.y <= std::max(u.y, v.y) + 1e-9;
    };
    const bool a_end = same_point(a, p) || same_point(a, q), b_end = same_point(b, p) || same_point(b, q);
    // An edge meeting the segment at one of its ends blocks it only by running back along it.
    if (a_end || b_end) {
        const Vec2 other = a_end ? b : a;
        if (side(p, q, other) != 0) return false;
        const Vec2 end = a_end ? (same_point(a, p) ? p : q) : (same_point(b, p) ? p : q);
        const Vec2 far = same_point(end, p) ? q : p;
        return on_seg(end, far, other) && !same_point(other, end);
    }
    const int s1 = side(p, q, a), s2 = side(p, q, b), s3 = side(a, b, p), s4 = side(a, b, q);
    if (s1 != s2 && s3 != s4) return true;  // proper crossing or touching
    if (s1 == 0 && on_seg(p, q, a)) return true;
    if (s2 == 0 && on_seg(p, q, b)) return true;
    if (s3 == 0 && on_seg(a, b, p)) return true;
    if (s4 == 0 && on_seg(a, b, q)) return true;
    return false;
}

// Joins `hole` (clockwise) into `loop` (counter-clockwise) through a zero-width bridge: the shortest
// hole vertex to loop vertex segment that no edge blocks and that runs through the polygon's interior.
bool bridge_hole(const std::vector<Vec2>& pts, Ring& loop, const Ring& hole) {
    double best = kInf;
    std::size_t bh = 0, bl = 0;
    bool found = false;
    for (std::size_t hi = 0; hi < hole.size(); ++hi)
        for (std::size_t li = 0; li < loop.size(); ++li) {
            const Vec2 H = pts[hole[hi]], L = pts[loop[li]];
            const double d = (H - L).length();
            if (d >= best) continue;
            if (d > 1e-9) {
                bool clear = true;
                for (std::size_t e = 0; e < loop.size() && clear; ++e)
                    clear = !blocks(H, L, pts[loop[e]], pts[loop[(e + 1) % loop.size()]]);
                for (std::size_t e = 0; e < hole.size() && clear; ++e)
                    clear = !blocks(H, L, pts[hole[e]], pts[hole[(e + 1) % hole.size()]]);
                if (!clear) continue;
                const Vec2 mid = (H + L) * 0.5;
                if (!point_in_ring(mid, pts, loop) || point_in_ring(mid, pts, hole)) continue;
            }
            best = d;
            bh = hi;
            bl = li;
            found = true;
        }
    if (!found) return false;
    Ring merged(loop.begin(), loop.begin() + static_cast<std::ptrdiff_t>(bl) + 1);
    for (std::size_t k = 0; k <= hole.size(); ++k) merged.push_back(hole[(bh + k) % hole.size()]);
    merged.push_back(loop[bl]);
    merged.insert(merged.end(), loop.begin() + static_cast<std::ptrdiff_t>(bl) + 1, loop.end());
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
