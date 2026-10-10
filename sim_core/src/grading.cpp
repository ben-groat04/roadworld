// Ground grading: port of prototypes/road-draw/src/earthworks.js (see grading.h).
//
// Behaviour is reproduced, including these oddities of the prototype (all kept on purpose):
//  * gradeTile keeps per-road intermediate values in Float32 arrays and reads them back, so the
//    distance, formation height and wall weight used per texel are float-rounded (mirrored here
//    with float arrays).
//  * Wall type / weight of a texel come from ONE nearest segment of each road (hard flip at the
//    segment midpoint), not interpolated.
//  * slopeReach was cached per road version in the JS and not invalidated by median or terrain
//    changes; here it is recomputed from the current flags (the intended value).
//  * The prototype culls tiles with pair-of-segment boxes built from the end points only; here
//    the middle sample is included so a bend cannot poke out of its box (culling cannot change
//    results otherwise).
//  * In the JS the plane test position is rebuilt as (rx + A.x), not x0 + i * res: kept.
//  * A tunnel whose portal is the road's first or last station gets no portal plane (the JS looks
//    at the neighbouring decimated sample, which does not exist): kept.
//  * The JS classifies with hBase (triangle interpolation of the coarse grid) but grades against
//    hBaseFine (bicubic). Here one GroundFn serves classification and sample_natural_tile
//    serves grading; the caller decides whether they are the same surface.
//  * The JS comment in classify() says the tunnel approach is not walled, but the code walls
//    portal_wall samples before every portal: the code is what is ported.
#include "cp/grading.h"

#include <algorithm>
#include <cmath>
#include <utility>

#include "cp/lanes.h"

namespace cp {

GradingParams GradingParams::from_rules(const GradingRules& r) {
    GradingParams p;
    p.formation_drop = r.formation_drop;
    p.batter = r.batter;
    p.verge = r.verge;
    p.wall_thickness = r.wall_thickness;
    p.wall_start = r.wall_start;
    p.wall_cap = r.wall_cap;
    p.wall_slack = r.wall_slack;
    p.tunnel.clear_height = r.tunnel_clear_height;
    return p;
}

namespace {

using Flags = std::vector<char>;

// Fill false runs shorter than max_gap that sit between true samples.
void close_gaps(Flags& a, int max_gap) {
    const int n = static_cast<int>(a.size());
    int i = 0;
    while (i < n) {
        if (a[static_cast<std::size_t>(i)]) { ++i; continue; }
        int j = i;
        while (j < n && !a[static_cast<std::size_t>(j)]) ++j;
        if (i > 0 && j < n && j - i < max_gap)
            for (int k = i; k < j; ++k) a[static_cast<std::size_t>(k)] = 1;
        i = j;
    }
}

void drop_short(Flags& a, int min_run) {
    const int n = static_cast<int>(a.size());
    int i = 0;
    while (i < n) {
        if (!a[static_cast<std::size_t>(i)]) { ++i; continue; }
        int j = i;
        while (j < n && a[static_cast<std::size_t>(j)]) ++j;
        if (j - i < min_run)
            for (int k = i; k < j; ++k) a[static_cast<std::size_t>(k)] = 0;
        i = j;
    }
}

// Pad true runs by k samples each way (stops at locked samples and array ends).
void dilate(Flags& a, int k, const Flags& lock) {
    const int n = static_cast<int>(a.size());
    Flags o = a;
    for (int i = 0; i < n; ++i) {
        if (!a[static_cast<std::size_t>(i)]) continue;
        for (const int dir : {-1, 1})
            for (int d = 1; d <= k; ++d) {
                const int j = i + dir * d;
                if (j < 0 || j >= n || lock[static_cast<std::size_t>(j)]) break;
                o[static_cast<std::size_t>(j)] = 1;
            }
    }
    a = o;
}

// Grow every true run shorter than min_run to min_run samples (both ways, never into locked
// samples). Runs only grow: dropping one would leave a slope that cannot daylight.
void grow_short(Flags& a, int min_run, const Flags& lock) {
    const int n = static_cast<int>(a.size());
    int i = 0;
    while (i < n) {
        if (!a[static_cast<std::size_t>(i)]) { ++i; continue; }
        int j = i;
        while (j < n && a[static_cast<std::size_t>(j)]) ++j;
        int need = min_run - (j - i), lo = i, hi = j;  // [lo, hi)
        while (need > 0) {
            const bool can_l = lo > 0 && !lock[static_cast<std::size_t>(lo - 1)];
            const bool can_r = hi < n && !lock[static_cast<std::size_t>(hi)];
            if (!can_l && !can_r) break;
            if (can_l && (!can_r || (hi - lo) % 2 == 0)) {
                --lo;
                a[static_cast<std::size_t>(lo)] = 1;
            } else {
                a[static_cast<std::size_t>(hi)] = 1;
                ++hi;
            }
            --need;
        }
        i = std::max(j, hi);
    }
}

double slope_v(double e, double verge) { return e > verge ? e - verge : 0.0; }

// Reach cap: farthest lateral distance a wall-start slope can need.
double reach_cap(double hw, const GradingParams& p) {
    return hw + p.verge + p.wall_start * p.batter + p.reach_margin;
}

// Does the slope on side sg (+1 left, -1 right) of sample s need more than wall_start height
// before it daylights? 1 cut wall, 2 fill wall, 0 slope.
int needs_wall(const RoadSample& s, double hw, int sg, const GroundFn& ground, const GradingParams& p) {
    const double nx = -std::sin(s.h) * sg, ny = std::cos(s.h) * sg, bt = p.batter, z0 = s.z - p.formation_drop;
    auto base = [&](double d) { return ground(s.x + nx * d, s.y + ny * d); };
    const double g0 = base(hw) - z0;
    const bool cut = g0 > 0;
    if (std::abs(g0) < 1e-3) return 0;
    const double d_max = hw + p.verge + (p.wall_start + p.wall_slack) * bt;
    for (double d = hw + 0.5; d < d_max + 0.5; d += 0.5) {
        const double dd = std::min(d, d_max);
        const double zs = cut ? z0 + slope_v(dd - hw, p.verge) / bt : z0 - slope_v(dd - hw, p.verge) / bt;
        if (cut ? zs >= base(dd) : zs <= base(dd)) return 0;
        if (dd >= d_max) break;
    }
    return cut ? 1 : 2;
}

// Auto structure rule: br / tu per sample from cut / fill depth at the centre and both wall lines.
void auto_structures(const SampledRoad& r, const GroundFn& ground, const GradingParams& p, Flags& br, Flags& tu) {
    const std::size_t n = r.samples.size();
    const double hw = r.half_width;
    br.assign(n, 0);
    tu.assign(n, 0);
    Flags mb(n, 0), mt(n, 0);
    for (std::size_t i = 0; i < n; ++i) {
        const RoadSample& s = r.samples[i];
        const double nx = -std::sin(s.h), ny = std::cos(s.h);
        const double dz = s.z - ground(s.x, s.y), form = s.z - p.formation_drop;
        const double eL = ground(s.x + nx * hw, s.y + ny * hw), eR = ground(s.x - nx * hw, s.y - ny * hw);
        const bool b = dz > p.wall_cap, t = dz < -p.wall_cap;
        // a wall may never exceed wall_cap: judge both wall lines, not just the centreline
        mb[i] = b || form - eL > p.wall_cap || form - eR > p.wall_cap;
        mt[i] = t || eL - form > p.wall_cap || eR - form > p.wall_cap;
        br[i] = mb[i];
        tu[i] = mt[i] && !mb[i];
    }
    for (Flags* a : {&br, &tu}) {
        close_gaps(*a, p.structure_gap);
        drop_short(*a, p.structure_min_run);
    }
    for (std::size_t i = 0; i < n; ++i) {  // smoothing may extend, never shrink below the cap rule
        if (mb[i]) br[i] = 1;
        else if (mt[i]) tu[i] = 1;
    }
    for (std::size_t i = 0; i < n; ++i)
        if (br[i] && tu[i]) tu[i] = 0;
}

std::vector<StructureRange> runs_of(const Flags& a, StructureKind kind) {
    std::vector<StructureRange> out;
    const int n = static_cast<int>(a.size());
    int i = 0;
    while (i < n) {
        if (!a[static_cast<std::size_t>(i)]) { ++i; continue; }
        int j = i;
        while (j + 1 < n && a[static_cast<std::size_t>(j + 1)]) ++j;
        out.push_back({i, j, kind});
        i = j + 1;
    }
    return out;
}

}  // namespace

std::vector<StructureRange> required_structures(const SampledRoad& road, const GroundFn& ground,
                                                const GradingParams& p) {
    Flags br, tu;
    auto_structures(road, ground, p, br, tu);
    std::vector<StructureRange> out = runs_of(br, StructureKind::Bridge);
    for (const StructureRange& r : runs_of(tu, StructureKind::Tunnel)) out.push_back(r);
    std::sort(out.begin(), out.end(), [](const StructureRange& a, const StructureRange& b) { return a.first < b.first; });
    return out;
}

void classify_road(SampledRoad& road, const GroundFn& ground, const GradingParams& p, StructureMode mode) {
    std::vector<RoadSample>& S = road.samples;
    const std::size_t n = S.size();
    road.portals.clear();
    if (n == 0) return;
    const double hw = road.half_width;
    Flags br, tu;
    if (mode == StructureMode::Auto) {
        auto_structures(road, ground, p, br, tu);
    } else {
        br.assign(n, 0);
        tu.assign(n, 0);
        for (std::size_t i = 0; i < n; ++i) {
            br[i] = S[i].br;
            tu[i] = S[i].tu && !S[i].br;
        }
    }
    // Walls only where the batter slope (after the verge) cannot daylight within wall_start of
    // height. Per side: wall type per sample (1 cut, 2 fill), gaps bridged, runs grown, never
    // into bridge / tunnel samples, so no short stubs.
    Flags lock(n, 0);
    for (std::size_t i = 0; i < n; ++i) lock[i] = br[i] || tu[i];
    std::vector<int> typ[2] = {std::vector<int>(n, 0), std::vector<int>(n, 0)};
    const int ni = static_cast<int>(n);
    for (int k = 0; k < 2; ++k) {
        const int sg = k == 0 ? 1 : -1;
        std::vector<int>& t = typ[k];
        Flags w(n, 0);
        for (std::size_t i = 0; i < n; ++i)
            if (!lock[i]) {
                t[i] = needs_wall(S[i], hw, sg, ground, p);
                w[i] = t[i] > 0;
            }
        // portal / abutment approach (the embankment ends against a bridge over falling ground: a
        // slope cannot fit there): walls both sides for portal_wall samples before the entry / after the exit
        for (int i = 0; i < ni; ++i) {
            const std::size_t ui = static_cast<std::size_t>(i);
            if (!(tu[ui] || br[ui])) continue;
            const Flags& same = tu[ui] ? tu : br;
            if (i == 0 || !same[ui - 1])
                for (int q = i - 1; q >= std::max(0, i - p.portal_wall) && !lock[static_cast<std::size_t>(q)]; --q)
                    w[static_cast<std::size_t>(q)] = 1;
            if (i == ni - 1 || !same[ui + 1])
                for (int q = i + 1; q <= std::min(ni - 1, i + p.portal_wall) && !lock[static_cast<std::size_t>(q)]; ++q)
                    w[static_cast<std::size_t>(q)] = 1;
        }
        close_gaps(w, p.wall_gap);
        for (std::size_t i = 0; i < n; ++i)
            if (lock[i]) w[i] = 0;
        dilate(w, p.wall_pad, lock);
        grow_short(w, p.wall_min_run, lock);
        for (std::size_t i = 0; i < n; ++i) {
            if (!w[i]) { t[i] = 0; continue; }
            if (t[i] == 0) {  // bridged / grown sample: type from the ground at the wall's outer face
                const RoadSample& s = S[i];
                const double nx = -std::sin(s.h) * sg, ny = std::cos(s.h) * sg;
                t[i] = ground(s.x + nx * (hw + p.wall_thickness), s.y + ny * (hw + p.wall_thickness)) >
                               s.z - p.formation_drop
                           ? 1
                           : 2;
            }
        }
    }
    for (std::size_t i = 0; i < n; ++i) {
        RoadSample& s = S[i];
        s.br = br[i];
        s.tu = tu[i];
        s.portal = tu[i] && (i == 0 || !tu[i - 1] || i == n - 1 || !tu[i + 1]);
        if (s.portal) road.portals.push_back(static_cast<int>(i));
        s.wL = s.wL0 = typ[0][i];
        s.wR = s.wR0 = typ[1][i];
    }
}

Box2 road_box(const SampledRoad& road, const GradingParams& p) {
    const double m = reach_cap(road.half_width, p) + p.box_margin;
    Box2 b{1e9, 1e9, -1e9, -1e9};
    for (const RoadSample& s : road.samples) {
        b.x0 = std::min(b.x0, s.x);
        b.x1 = std::max(b.x1, s.x);
        b.y0 = std::min(b.y0, s.y);
        b.y1 = std::max(b.y1, s.y);
    }
    return {b.x0 - m, b.y0 - m, b.x1 + m, b.y1 + m};
}

void mark_medians(std::vector<SampledRoad>& roads, const GradingParams& p) {
    std::vector<SampledRoad*> rs;
    for (SampledRoad& r : roads)
        if (r.samples.size() > 1) rs.push_back(&r);
    for (SampledRoad* r : rs)
        for (RoadSample& s : r->samples) {
            s.mL = s.mR = false;
            s.wL = s.wL0;
            s.wR = s.wR0;
        }
    for (SampledRoad* r : rs) {
        if (!r->dual) continue;
        const double lim = r->median_gap + 2 * r->half_width + 2;
        const Box2 bb = road_box(*r, p);
        for (SampledRoad* r2 : rs) {
            if (r2 == r || r2->type_id != r->type_id) continue;
            const Box2 b2 = road_box(*r2, p);
            if (b2.x1 < bb.x0 || b2.x0 > bb.x1 || b2.y1 < bb.y0 || b2.y0 > bb.y1) continue;
            for (std::size_t i = 0; i < r->samples.size(); i += 2) {
                RoadSample& s = r->samples[i];
                const double nx = -std::sin(s.h), ny = std::cos(s.h);
                for (std::size_t k = 0; k < r2->samples.size(); k += 2) {
                    const RoadSample& t = r2->samples[k];
                    const double dx = t.x - s.x, dy = t.y - s.y;
                    if (std::abs(dx) > lim || std::abs(dy) > lim) continue;
                    const double d = std::hypot(dx, dy);
                    if (d > lim) continue;
                    const double lat = dx * nx + dy * ny;
                    if (std::abs(d - std::abs(lat)) < 2 && std::cos(t.h - s.h) < -0.7) {
                        if (lat > 0) s.mL = true;
                        else s.mR = true;
                    }
                }
            }
        }
    }
    for (SampledRoad* r : rs) {
        std::vector<RoadSample>& S = r->samples;
        for (std::size_t i = 1; i < S.size(); i += 2) {
            RoadSample& s = S[i];
            const RoadSample& a = S[i - 1];
            const RoadSample& b = S[std::min(i + 1, S.size() - 1)];
            s.mL = a.mL || b.mL || s.mL;
            s.mR = a.mR || b.mR || s.mR;
        }
    }
    for (SampledRoad* r : rs)
        for (RoadSample& s : r->samples) {
            if (s.mL) s.wL = 0;
            if (s.mR) s.wR = 0;
        }
}

void compute_wall_weights(SampledRoad& road, const GradingParams& p) {
    std::vector<RoadSample>& S = road.samples;
    const int n = static_cast<int>(S.size());
    for (int side = 0; side < 2; ++side) {
        auto flag = [&](int i) { return side == 0 ? S[static_cast<std::size_t>(i)].wL : S[static_cast<std::size_t>(i)].wR; };
        auto set_ww = [&](int i, double v) {
            (side == 0 ? S[static_cast<std::size_t>(i)].wwL : S[static_cast<std::size_t>(i)].wwR) = v;
        };
        int i = 0;
        while (i < n) {
            if (!flag(i)) { set_ww(i, 0); ++i; continue; }
            int j = i;
            while (j < n && flag(j)) ++j;
            std::vector<double> cum{0.0};
            for (int k = i + 1; k < j; ++k) {
                const RoadSample& a = S[static_cast<std::size_t>(k)];
                const RoadSample& b = S[static_cast<std::size_t>(k - 1)];
                cum.push_back(cum[static_cast<std::size_t>(k - i - 1)] + std::hypot(a.x - b.x, a.y - b.y));
            }
            const double tot = cum.back();
            // only free run ends (incl. the road's own ends) taper: structures (abutment, portal) hide the wall end
            const bool free0 = i == 0 || (!S[static_cast<std::size_t>(i - 1)].br && !S[static_cast<std::size_t>(i - 1)].tu);
            const bool free1 = j == n || (!S[static_cast<std::size_t>(j)].br && !S[static_cast<std::size_t>(j)].tu);
            for (int k = i; k < j; ++k) {
                const double c = cum[static_cast<std::size_t>(k - i)];
                const double d = std::min(free0 ? c : 1e9, free1 ? tot - c : 1e9) + 1e-9;
                const double u = std::min(1.0, d / p.wall_ramp);
                set_ww(k, u * u * (3 - 2 * u));  // run end -> 0 (smoothstep)
            }
            i = j;
        }
    }
}

double slope_reach(const SampledRoad& road, const GroundFn& ground, const GradingParams& p) {
    const std::vector<RoadSample>& S = road.samples;
    const double hw = road.half_width, cap = reach_cap(hw, p), bt = p.batter;
    double mx = 0;
    const std::size_t n = S.size();
    auto struct_at = [&](std::size_t i) { return S[i].br || S[i].tu; };
    for (std::size_t i = 0; i < n; i += 2) {
        const RoadSample& s = S[i];
        if (s.br || s.tu) continue;
        const double zf = s.z - p.formation_drop;
        // road end or next to a bridge / tunnel: the embankment also falls away along the road (cone around the end)
        const bool endish = i == 0 || i + 2 >= n - 1 || (i >= 2 && struct_at(i - 2)) || (i + 2 < n && struct_at(i + 2));
        const int sgs[4] = {1, -1, 2, 3};
        for (int q = 0; q < (endish ? 4 : 2); ++q) {
            const int sg = sgs[q];
            if (sg == 1 ? s.wL : sg == -1 ? s.wR : 0) continue;
            const double nx = sg > 1 ? std::cos(s.h) * (sg == 2 ? 1 : -1) : -std::sin(s.h) * sg;
            const double ny = sg > 1 ? std::sin(s.h) * (sg == 2 ? 1 : -1) : std::cos(s.h) * sg;
            for (double e = 0.5; hw + e <= cap; e += 1) {
                const double b = ground(s.x + nx * (hw + e), s.y + ny * (hw + e));
                const double v = b > zf ? zf + slope_v(e, p.verge) / bt : zf - slope_v(e, p.verge) / bt;
                if (b > zf ? v >= b : v <= b) {
                    if (e > mx) mx = e;
                    break;
                }
                if (hw + e + 1 > cap && e > mx) mx = e;
            }
        }
    }
    bool any = false;
    for (const RoadSample& s : S) any = any || s.br || s.tu;
    // embankment ends at bridges fall away along the road too: full reach
    return any ? cap : std::min(cap, hw + mx + p.reach_pad);
}

std::vector<GradingRoad> prepare_grading_roads(std::vector<SampledRoad>& roads, const GroundFn& ground,
                                               const GradingParams& p) {
    mark_medians(roads, p);
    for (SampledRoad& r : roads)
        if (r.samples.size() > 1) compute_wall_weights(r, p);
    std::vector<GradingRoad> out;
    for (const SampledRoad& r : roads) {
        if (r.samples.size() < 2) continue;
        const std::vector<RoadSample>& full = r.samples;
        GradingRoad g;
        g.half_width = r.half_width;
        g.reach = slope_reach(r, ground, p);
        g.bbox = road_box(r, p);
        const std::size_t n = full.size();
        std::vector<double> dec_h;
        // every 2nd sample, plus the last graded / first tunnel sample at each portal (the portal plane stays exact)
        for (std::size_t i = 0; i < n; ++i) {
            const RoadSample& a = full[i];
            if (!(i % 2 == 0 || i == n - 1 || full[i - 1].tu != a.tu || full[i + 1].tu != a.tu)) continue;
            GradingSample d;
            d.x = a.x; d.y = a.y; d.z = a.z;
            d.wwL = a.wwL; d.wwR = a.wwR;
            d.wL = static_cast<std::int8_t>(a.wL); d.wR = static_cast<std::int8_t>(a.wR);
            d.br = a.br; d.tu = a.tu;
            g.samples.push_back(d);
            dec_h.push_back(a.h);
        }
        const std::vector<GradingSample>& dec = g.samples;
        const int nd = static_cast<int>(dec.size());
        for (int i = 0; i + 1 < nd; i += 2) {  // coarse reach boxes for tile culling (middle sample included)
            const GradingSample &a = dec[static_cast<std::size_t>(i)], &m = dec[static_cast<std::size_t>(i + 1)],
                                &b = dec[static_cast<std::size_t>(std::min(i + 2, nd - 1))];
            g.seg_boxes.push_back({std::min({a.x, m.x, b.x}) - g.reach, std::min({a.y, m.y, b.y}) - g.reach,
                                   std::max({a.x, m.x, b.x}) + g.reach, std::max({a.y, m.y, b.y}) + g.reach});
        }
        // portal planes: decimated-segment range they cut, plane point, unit normal pointing into the tunnel
        for (int k = 0; k < nd; ++k) {
            const GradingSample& a = dec[static_cast<std::size_t>(k)];
            if (!a.tu) continue;
            const double h = dec_h[static_cast<std::size_t>(k)];
            if (k > 0 && !dec[static_cast<std::size_t>(k - 1)].tu)
                g.planes.push_back({std::max(0, k - p.plane_span), k, a.x, a.y, std::cos(h), std::sin(h)});
            if (k + 1 < nd && !dec[static_cast<std::size_t>(k + 1)].tu)
                g.planes.push_back({k, std::min(nd - 1, k + p.plane_span), a.x, a.y, -std::cos(h), -std::sin(h)});
        }
        out.push_back(std::move(g));
    }
    return out;
}

// ---------------------------------------------------------------------------------------------
// Tiles
// ---------------------------------------------------------------------------------------------

std::vector<float> sample_natural_tile(const GroundFn& ground, const TileSpec& t) {
    std::vector<float> out(static_cast<std::size_t>(t.nx) * static_cast<std::size_t>(t.ny));
    for (int j = 0; j < t.ny; ++j)
        for (int i = 0; i < t.nx; ++i)
            out[static_cast<std::size_t>(j) * static_cast<std::size_t>(t.nx) + static_cast<std::size_t>(i)] =
                static_cast<float>(ground(t.x0 + i * t.res, t.y0 + j * t.res));
    return out;
}

std::vector<float> sample_natural_tile(const Heightfield& hf, const TileSpec& t) {
    std::vector<float> out(static_cast<std::size_t>(t.nx) * static_cast<std::size_t>(t.ny));
    for (int j = 0; j < t.ny; ++j)
        for (int i = 0; i < t.nx; ++i)
            out[static_cast<std::size_t>(j) * static_cast<std::size_t>(t.nx) + static_cast<std::size_t>(i)] =
                static_cast<float>(hf.height(t.x0 + i * t.res, t.y0 + j * t.res));
    return out;
}

void grade_tile_into(const std::vector<GradingRoad>& roads, const GradingParams& p, const TileSpec& t,
                     const float* base, float* out, std::uint8_t* footprint) {
    const std::size_t N = static_cast<std::size_t>(t.nx) * static_cast<std::size_t>(t.ny);
    if (N == 0) return;
    const int nx = t.nx, ny = t.ny;
    const double res = t.res, x0 = t.x0, y0 = t.y0, bt = p.batter, fd = p.formation_drop, verge = p.verge;
    // wall band: texels with e <= wall_thickness - res take the inside (formation) value, so the
    // one-texel ramp to natural lies inside the solid
    const double wall_t = p.wall_thickness, half = wall_t - res;
    const double tx1 = x0 + (nx - 1) * res, ty1 = y0 + (ny - 1) * res;
    std::vector<float> fp_d(N, 9.0f), fp_z(N, 1e9f), cut_m(N, 1e9f), fill_m(N, -1e9f), rq_ww(N), rq_d(N), rq_z(N),
        cut_e(N), fill_e(N);
    std::vector<std::uint8_t> rq_w(N);
    std::vector<const PortalPlane*> ap;
    for (const GradingRoad& rd : roads) {
        const Box2& bb = rd.bbox;
        if (bb.x1 < x0 || bb.x0 > tx1 || bb.y1 < y0 || bb.y0 > ty1) continue;
        bool near_any = rd.seg_boxes.empty();
        for (const Box2& b : rd.seg_boxes)
            if (b.x1 >= x0 && b.x0 <= tx1 + res && b.y1 >= y0 && b.y0 <= ty1 + res) { near_any = true; break; }
        if (!near_any) continue;
        const std::vector<GradingSample>& S = rd.samples;
        const double hw = rd.half_width, R = rd.reach, R2 = R * R;
        std::fill(rq_d.begin(), rq_d.end(), 1e9f);  // per road: nearest-station formation; across roads: lowest wins
        for (std::size_t k = 0; k + 1 < S.size(); ++k) {
            const GradingSample &A = S[k], &B = S[k + 1];
            if ((A.br || A.tu) && (B.br || B.tu)) continue;  // bridge / tunnel stations are not graded; a segment with one structure end is graded up to the abutment / portal
            const double dx = B.x - A.x, dy = B.y - A.y, L2 = dx * dx + dy * dy;
            if (L2 < 1e-12) continue;
            const int i0 = std::max(0, static_cast<int>(std::floor((std::min(A.x, B.x) - R - x0) / res)));
            const int i1 = std::min(nx - 1, static_cast<int>(std::ceil((std::max(A.x, B.x) + R - x0) / res)));
            const int j0 = std::max(0, static_cast<int>(std::floor((std::min(A.y, B.y) - R - y0) / res)));
            const int j1 = std::min(ny - 1, static_cast<int>(std::ceil((std::max(A.y, B.y) + R - y0) / res)));
            const double dz = B.z - A.z;
            ap.clear();
            const int ki = static_cast<int>(k);
            for (const PortalPlane& pl : rd.planes)
                if (ki >= pl.k0 && ki < pl.k1) ap.push_back(&pl);
            const double sq = std::sqrt(L2);
            const double t_hi = 1 + p.plane_over / sq, t_lo = -p.plane_over / sq;
            const bool a_str = A.br || A.tu, b_str = B.br || B.tu;
            for (int j = j0; j <= j1; ++j) {
                const double ry = y0 + j * res - A.y;
                for (int i = i0; i <= i1; ++i) {
                    const double rx = x0 + i * res - A.x;
                    if (!ap.empty()) {  // past a tunnel portal plane: natural ground (the headwall hides the one-texel step)
                        const double px = rx + A.x, py = ry + A.y;
                        bool beyond = false;
                        for (const PortalPlane* pl : ap)
                            if ((px - pl->x) * pl->nx + (py - pl->y) * pl->ny > p.plane_over) { beyond = true; break; }
                        if (beyond) continue;
                    }
                    double tt = (rx * dx + ry * dy) / L2;
                    // tunnel portal: grading stops exactly at the portal plane (the first tunnel station); bridges keep the rounded embankment end
                    if ((B.tu && !A.tu && tt > t_hi) || (A.tu && !B.tu && tt < t_lo)) continue;
                    tt = tt < 0 ? 0 : tt > 1 ? 1 : tt;
                    const double ex = rx - tt * dx, ey = ry - tt * dy, d2 = ex * ex + ey * ey;
                    if (d2 > R2) continue;
                    const double dd = std::sqrt(d2);
                    const std::size_t o = static_cast<std::size_t>(j) * static_cast<std::size_t>(nx) + static_cast<std::size_t>(i);
                    if (dd >= rq_d[o]) continue;  // nearest station of this road decides (one zf, one wall flag per texel)
                    rq_d[o] = static_cast<float>(dd);
                    rq_z[o] = static_cast<float>(A.z + tt * dz - fd);
                    const GradingSample& E = a_str ? B : b_str ? A : tt < 0.5 ? A : B;
                    const bool lf = (dx * ry - dy * rx) > 0;  // cross > 0: texel on the left
                    rq_w[o] = static_cast<std::uint8_t>(lf ? E.wL : E.wR);
                    const double wa = lf ? A.wwL : A.wwR, wb = lf ? B.wwL : B.wwR;
                    rq_ww[o] = static_cast<float>(a_str ? wb : b_str ? wa : wa + (wb - wa) * tt);
                }
            }
        }
        for (std::size_t o = 0; o < N; ++o) {
            const double dd = rq_d[o];
            if (dd > 1e8) continue;
            const double zf = rq_z[o];
            if (dd <= hw) {  // union of footprints: lowest formation wins (never pokes through any road)
                fp_d[o] = 0;
                if (zf < fp_z[o]) fp_z[o] = static_cast<float>(zf);
                continue;
            }
            const double e = dd - hw, b = base[o];
            const bool cut_t = b > zf;
            int wf = rq_w[o];
            double ww = wf ? static_cast<double>(rq_ww[o]) : 0.0;
            if (wf && (wf == 1) != cut_t) { wf = 0; ww = 0; }  // flag says cut but this texel is fill (or vice versa): plain slope
            // Wall (ww = weight 0..1 along the run; verge == wall thickness so ww = 0 is exactly the plain verge + slope).
            // cut wall: the wall raises the formation level toward natural by ww (hidden behind the road-facing wall face), the slope starts above it.
            // fill wall: the wall's foot sits ww of the way from formation down to natural; the slope continues below the foot.
            if (wf == 1) {
                const double zfw = zf + ww * (std::max(zf, b - 0.1) - zf);
                if (e <= wall_t) {
                    fp_d[o] = 0;
                    const double zw = e <= half ? std::min(zf, b) : std::min(zfw, b);
                    if (zw < fp_z[o]) fp_z[o] = static_cast<float>(zw);
                    continue;
                }
                const double v = zfw + (e - verge) / bt;
                if (v < b && v < cut_m[o]) { cut_m[o] = static_cast<float>(v); cut_e[o] = static_cast<float>(e); }
                continue;
            }
            if (wf == 2) {
                if (e <= half) {
                    fp_d[o] = 0;
                    if (zf < fp_z[o]) fp_z[o] = static_cast<float>(zf);
                    continue;
                }
                const double zff = zf - ww * (zf - b);
                const double v = e <= wall_t ? zff : zff - (e - verge) / bt;
                if (v > b && v > fill_m[o]) { fill_m[o] = static_cast<float>(v); fill_e[o] = static_cast<float>(e); }
                continue;
            }
            if (cut_t) {
                const double v = zf + slope_v(e, verge) / bt;
                if (v < b && v < cut_m[o]) { cut_m[o] = static_cast<float>(v); cut_e[o] = static_cast<float>(e); }
            } else {
                const double v = zf - slope_v(e, verge) / bt;
                if (v > b && v > fill_m[o]) { fill_m[o] = static_cast<float>(v); fill_e[o] = static_cast<float>(e); }
            }
        }
    }
    for (std::size_t o = 0; o < N; ++o) {
        float g;
        if (fp_d[o] < 9) {
            g = fp_z[o];
        } else {
            g = base[o];
            const bool hc = cut_m[o] < g, hf = fill_m[o] > g;
            // never cross the base to the wrong side: take the nearer of a cut and a fill slope
            if (hc && hf) g = cut_e[o] <= fill_e[o] ? cut_m[o] : fill_m[o];
            else if (hc) g = cut_m[o];
            else if (hf) g = fill_m[o];
        }
        out[o] = g;
        if (footprint) footprint[o] = fp_d[o] < 9 ? 1 : 0;
    }
}

GradedTile grade_tile(const std::vector<GradingRoad>& roads, const GradingParams& p, const TileSpec& t,
                      const std::vector<float>& natural) {
    GradedTile g;
    const std::size_t N = static_cast<std::size_t>(t.nx) * static_cast<std::size_t>(t.ny);
    g.height.assign(N, 0.0f);
    g.footprint.assign(N, 0);
    if (natural.size() < N) return g;
    grade_tile_into(roads, p, t, natural.data(), g.height.data(), g.footprint.data());
    return g;
}

// ---------------------------------------------------------------------------------------------
// Wall runs
// ---------------------------------------------------------------------------------------------

std::vector<WallRun> wall_runs(const SampledRoad& road, const GroundFn& ground, const GradingParams& p) {
    const std::vector<RoadSample>& S = road.samples;
    const int n = static_cast<int>(S.size());
    const double hw = road.half_width;
    std::vector<WallRun> runs;
    for (const char side : {'L', 'R'}) {
        const int sg = side == 'L' ? 1 : -1;
        int cur = -1;
        auto w_flag = [&](int i) {
            if (i < 0 || i >= n) return false;
            const RoadSample& q = S[static_cast<std::size_t>(i)];
            return (side == 'L' ? q.wL : q.wR) != 0 && !q.br && !q.tu;
        };
        for (int i = 0; i < n; ++i) {
            const RoadSample& s = S[static_cast<std::size_t>(i)];
            // a portal station joins the wall run beside it: the approach wall runs right up to the headwall plane
            if (!(w_flag(i) || (s.tu && s.portal && (w_flag(i - 1) || w_flag(i + 1))))) { cur = -1; continue; }
            const double nx = -std::sin(s.h) * sg, ny = std::cos(s.h) * sg;
            const double x = s.x + nx * hw, y = s.y + ny * hw, zf = s.z - p.formation_drop;
            const double out = ground(s.x + nx * (hw + p.wall_thickness), s.y + ny * (hw + p.wall_thickness));
            const bool cut = out > zf;
            if (cur < 0 || runs[static_cast<std::size_t>(cur)].cut != cut) {
                WallRun r;
                r.side = side;
                r.cut = cut;
                r.lo = 0;
                r.hi = p.wall_thickness;
                runs.push_back(std::move(r));
                cur = static_cast<int>(runs.size()) - 1;
            }
            WallPoint wp;
            wp.x = x; wp.y = y;
            wp.zTop = cut ? out : zf;
            wp.zBot = cut ? zf : out;
            wp.top = cut ? out + 0.3 : s.z + 1.0;
            wp.h = s.h;
            runs[static_cast<std::size_t>(cur)].pts.push_back(wp);
        }
    }
    return runs;
}

// ---------------------------------------------------------------------------------------------
// Tunnels
// ---------------------------------------------------------------------------------------------

TunnelProfile tunnel_profile(double half_width, const GradingParams& p) {
    TunnelProfile t;
    t.W = half_width;
    t.rise = p.tunnel.rise_k * half_width;
    t.Hs = p.tunnel.clear_height;
    t.R = (half_width * half_width + t.rise * t.rise) / (2 * t.rise);
    t.Hc = t.Hs + t.rise;
    t.zc = t.Hs + t.rise - t.R;
    return t;
}

double tunnel_roof(const TunnelProfile& prof, double q, double off) {
    return prof.zc + std::sqrt(std::max((prof.R + off) * (prof.R + off) - q * q, 0.0));
}

std::vector<StructureRange> tunnel_runs(const SampledRoad& road) {
    const std::vector<RoadSample>& S = road.samples;
    const int n = static_cast<int>(S.size());
    std::vector<StructureRange> out;
    int i = 0;
    while (i < n) {
        if (!S[static_cast<std::size_t>(i)].tu) { ++i; continue; }
        int j = i;
        while (j + 1 < n && S[static_cast<std::size_t>(j + 1)].tu) ++j;
        if (j > i) out.push_back({i, j, StructureKind::Tunnel});
        i = j + 1;
    }
    return out;
}

std::vector<TunnelVolume> tunnel_volumes(const std::vector<SampledRoad>& roads, const GradingParams& p) {
    std::vector<TunnelVolume> out;
    for (std::size_t ri = 0; ri < roads.size(); ++ri) {
        const SampledRoad& r = roads[ri];
        if (r.samples.size() < 2) continue;
        const TunnelProfile prof = tunnel_profile(r.half_width, p);
        for (const StructureRange& run : tunnel_runs(r)) {
            TunnelVolume v;
            v.road = static_cast<int>(ri);
            v.a = run.first;
            v.b = run.last;
            v.prof = prof;
            for (int i = run.first; i <= run.last; ++i) {
                const RoadSample& s = r.samples[static_cast<std::size_t>(i)];
                v.st.push_back({s.x, s.y, s.z, s.h, s.s});
            }
            out.push_back(std::move(v));
        }
    }
    return out;
}

int tunnel_table_width(const GradingParams& p) { return 2 + 2 * (p.tunnel.chunk + 1); }

int pack_tunnels(std::vector<float>& data, int max_rows, const std::vector<TunnelVolume>& vols,
                 const GradingParams& p, Vec2 origin) {
    const int W = tunnel_table_width(p);
    data.assign(static_cast<std::size_t>(W) * static_cast<std::size_t>(std::max(0, max_rows)) * 4, 0.0f);
    const TunnelParams& tp = p.tunnel;
    int row = 0;
    for (const TunnelVolume& v : vols) {
        const int m = static_cast<int>(v.st.size());
        const double off = tp.lining + tp.margin;
        const TunnelProfile& pr = v.prof;
        for (int s0 = 0; s0 + 1 < m && row < max_rows; s0 += tp.chunk, ++row) {
            const int s1 = std::min(m - 1, s0 + tp.chunk), n_seg = s1 - s0;
            const std::size_t base = static_cast<std::size_t>(row) * static_cast<std::size_t>(W) * 4;
            double x0 = 1e18, y0 = 1e18, z0 = 1e18, x1 = -1e18, y1 = -1e18, z1 = -1e18;
            for (int j = 0; j <= n_seg; ++j) {
                const TunnelStation& st = v.st[static_cast<std::size_t>(s0 + j)];
                const std::size_t o = base + static_cast<std::size_t>(2 + 2 * j) * 4;
                const double wd = pr.W + off, sx = st.x - origin.x, sy = st.y - origin.y;
                data[o] = static_cast<float>(sx);
                data[o + 1] = static_cast<float>(sy);
                data[o + 2] = static_cast<float>(st.z);
                data[o + 3] = static_cast<float>(wd);
                data[o + 4] = static_cast<float>(pr.zc);
                data[o + 5] = static_cast<float>(pr.R + off);
                data[o + 6] = (s0 + j == 0 || s0 + j == m - 1) ? 1.0f : 0.0f;
                data[o + 7] = 0.0f;
                x0 = std::min(x0, sx - wd - 1);
                x1 = std::max(x1, sx + wd + 1);
                y0 = std::min(y0, sy - wd - 1);
                y1 = std::max(y1, sy + wd + 1);
                z0 = std::min(z0, st.z - tp.floor_depth - 0.1);
                z1 = std::max(z1, st.z + pr.Hc + off + 0.1);
            }
            data[base] = static_cast<float>(x0);
            data[base + 1] = static_cast<float>(y0);
            data[base + 2] = static_cast<float>(z0);
            data[base + 3] = static_cast<float>(n_seg);
            data[base + 4] = static_cast<float>(x1);
            data[base + 5] = static_cast<float>(y1);
            data[base + 6] = static_cast<float>(z1);
            data[base + 7] = 0.0f;
        }
    }
    return row;
}

bool in_bore(const float* data, int width_texels, int rows, double x, double y, double up, const GradingParams& p) {
    const std::size_t W = static_cast<std::size_t>(width_texels);
    for (int c = 0; c < rows; ++c) {
        const std::size_t b = static_cast<std::size_t>(c) * W * 4;
        if (x < data[b] || x > data[b + 4] || y < data[b + 1] || y > data[b + 5] || up < data[b + 2] || up > data[b + 6])
            continue;
        const int n = static_cast<int>(data[b + 3]);
        for (int i = 0; i < n; ++i) {
            const std::size_t a = b + static_cast<std::size_t>(2 + 2 * i) * 4, e = a + 8;
            const double ax = data[a], ay = data[a + 1], dx = data[e] - ax, dy = data[e + 1] - ay, L2 = dx * dx + dy * dy;
            if (L2 < 1e-9) continue;
            const double px = x - ax, py = y - ay, t = (px * dx + py * dy) / L2;
            if (t < (data[a + 6] > 0.5f ? 0.0 : -0.1) || t > (data[e + 6] > 0.5f ? 1.0 : 1.1)) continue;
            const double lat = (dx * py - dy * px) / std::sqrt(L2);
            const double tc = t < 0 ? 0 : t > 1 ? 1 : t;
            auto mx = [&](std::size_t k) { return static_cast<double>(data[a + k]) + (static_cast<double>(data[e + k]) - data[a + k]) * tc; };
            const double zr = mx(2), Wd = mx(3), zc = mx(4), R = mx(5), rel = up - zr, al = std::abs(lat);
            if (al <= Wd && rel >= -p.tunnel.floor_depth && rel <= zc + std::sqrt(std::max(R * R - al * al, 0.0))) return true;
        }
    }
    return false;
}

// ---------------------------------------------------------------------------------------------
// RoadGraph adapter
// ---------------------------------------------------------------------------------------------

GroundFn ground_fn(const Heightfield& hf) {
    return [&hf](double x, double y) { return hf.height(x, y); };
}

SampledRoad sample_edge_for_grading(const Edge& e, const std::string& type_id, double half_width, bool dual,
                                    double median_gap, const GradingParams& p) {
    SampledRoad r;
    r.type_id = type_id;
    r.half_width = half_width;
    r.dual = dual;
    r.median_gap = median_gap;
    const std::vector<Clothoid>& spans = e.horizontal.spans;
    if (spans.empty()) return r;
    // station arc lengths: per span at most sample_step apart and at most 0.05 rad of heading change per step
    std::vector<double> sv;
    std::vector<double> span_s0;
    double acc = 0;
    for (std::size_t i = 0; i < spans.size(); ++i) {
        const Clothoid& c = spans[i];
        span_s0.push_back(acc);
        const double turn = std::abs(c.k0 * c.length + 0.5 * c.dk * c.length * c.length);
        const int n = std::max({2, static_cast<int>(std::ceil(c.length / p.sample_step)), static_cast<int>(std::ceil(turn / 0.05))});
        for (int j = i == 0 ? 0 : 1; j <= n; ++j) sv.push_back(acc + c.length * j / n);
        acc += c.length;
    }
    const double total = acc, tol = 1e-6;
    // extra stations exactly at structure boundaries, so portal planes and abutments sit on the spans
    for (const StructureSpan& sp : e.structure)
        for (const double s : {sp.s0, sp.s1}) {
            if (s <= tol || s >= total - tol) continue;
            bool have = false;
            for (const double q : sv) have = have || std::abs(q - s) < tol;
            if (!have) sv.push_back(s);
        }
    std::sort(sv.begin(), sv.end());
    for (const double s : sv) {
        std::size_t si = 0;
        while (si + 1 < spans.size() && s > span_s0[si] + spans[si].length) ++si;
        const Clothoid& c = spans[si];
        const double u = std::clamp(s - span_s0[si], 0.0, c.length);
        const Vec2 pt = c.point(u);
        RoadSample rs;
        rs.s = s;
        rs.x = pt.x;
        rs.y = pt.y;
        rs.h = c.heading(u);
        rs.z = e.vertical.z(s);
        for (const StructureSpan& sp : e.structure) {
            if (s < sp.s0 - tol || s > sp.s1 + tol) continue;
            if (sp.kind == StructureKind::Bridge) rs.br = true;
            else if (sp.kind == StructureKind::Tunnel) rs.tu = true;
        }
        if (rs.br) rs.tu = false;
        r.samples.push_back(rs);
    }
    return r;
}

std::vector<SampledRoad> sample_graph_for_grading(const RoadGraph& g, const GradingParams& p) {
    std::vector<SampledRoad> out;
    const RegionPack* region = g.region();
    if (!region) return out;
    for (const auto& [id, e] : g.edges()) {
        const RoadType* rt = region->road_type(e.road_type);
        if (!rt) continue;
        const double hw = cross_section_width(instantiate_lanes(e, *rt, region->driving_side)) / 2;
        out.push_back(sample_edge_for_grading(e, rt->id, hw, rt->dual, rt->median_gap, p));
    }
    return out;
}

}  // namespace cp
