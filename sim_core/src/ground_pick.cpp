#include "cp/ground_pick.h"

#include <algorithm>
#include <cmath>

namespace cp {

std::optional<GroundHit> pick_ground(const Heightfield& hf, const Ray& ray, const GroundPickParams& p) {
    if (hf.empty() || hf.nx() < 2 || hf.ny() < 2) return std::nullopt;
    const double len = std::sqrt(ray.dir.x * ray.dir.x + ray.dir.y * ray.dir.y + ray.dir.z * ray.dir.z);
    if (!(len > 1e-12)) return std::nullopt;
    const Vec3 d{ray.dir.x / len, ray.dir.y / len, ray.dir.z / len};
    const Vec3 o = ray.origin;

    // Clip to the footprint (slab test on x and y).
    const double x0 = hf.origin().x, y0 = hf.origin().y;
    const double x1 = x0 + hf.cell() * (hf.nx() - 1), y1 = y0 + hf.cell() * (hf.ny() - 1);
    double t0 = 0, t1 = p.max_distance;
    auto slab = [&](double oc, double dc, double lo, double hi) {
        if (std::abs(dc) < 1e-15) return oc >= lo && oc <= hi;
        double ta = (lo - oc) / dc, tb = (hi - oc) / dc;
        if (ta > tb) std::swap(ta, tb);
        t0 = std::max(t0, ta);
        t1 = std::min(t1, tb);
        return t0 <= t1;
    };
    if (!slab(o.x, d.x, x0, x1) || !slab(o.y, d.y, y0, y1)) return std::nullopt;

    auto f = [&](double t) {  // ray height above the ground at distance t
        const double x = o.x + d.x * t, y = o.y + d.y * t;
        return o.z + d.z * t - hf.height(x, y);
    };
    auto result = [&](double t) {
        return GroundHit{Vec3{o.x + d.x * t, o.y + d.y * t, o.z + d.z * t}, t};
    };

    double fa = f(t0);
    if (fa < 0) {
        // Starting below the surface: a camera under the ground has no hit; entering the footprint below it hits there.
        if (t0 <= 0) return std::nullopt;
        return result(t0);
    }
    const double hlen = std::hypot(d.x, d.y);
    double dt = std::min(0.5 * hf.cell() / std::max(hlen, 1e-9), hf.cell());
    dt = std::max(dt, 1e-3);
    const double span = t1 - t0;
    if (span / dt > p.max_steps) dt = span / p.max_steps;

    double a = t0;
    for (double t = t0 + dt;; t += dt) {
        const bool last = t >= t1;
        if (last) t = t1;
        const double ft = f(t);
        if (ft < 0) {
            double lo = a, hi = t;
            for (int i = 0; i < p.bisect_iterations && hi - lo > 1e-9; ++i) {
                const double m = 0.5 * (lo + hi);
                if (f(m) < 0) hi = m;
                else lo = m;
            }
            return result(0.5 * (lo + hi));
        }
        a = t;
        if (last) break;
    }
    return std::nullopt;
}

}  // namespace cp
