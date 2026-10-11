#include "cp/test_world.h"

#include <algorithm>
#include <cmath>

#include "cp/geom.h"

namespace cp {
namespace {

using u64 = std::uint64_t;

double smoothstep(double t) {
    t = std::clamp(t, 0.0, 1.0);
    return t * t * (3.0 - 2.0 * t);
}
double lerp(double a, double b, double t) { return a + (b - a) * t; }

// Rise profile P(t), t in [0,1] -> [0,1]: the slope is a smoothstep ramp over r at each end and constant in the
// middle (so the steep side holds one slope instead of peaking in a single spot). Closed-form integral.
double rise_profile(double t) {
    constexpr double r = 0.25;
    t = std::clamp(t, 0.0, 1.0);
    const double total = 1.0 - r;
    auto ramp = [](double u) { return u * u * u - 0.5 * u * u * u * u; };  // integral of smoothstep over [0,u]
    double g;
    if (t < r) {
        g = r * ramp(t / r);
    } else if (t <= 1.0 - r) {
        g = 0.5 * r + (t - r);
    } else {
        const double w = (1.0 - t) / r;
        g = total - r * ramp(w);
    }
    return g / total;
}

// Fixed unit vectors with exact rational components; one per octave so no two octaves share a lattice direction.
struct Rot {
    double c, s;
};
constexpr Rot kRots[] = {{1.0, 0.0}, {0.8, 0.6}, {0.6, -0.8}, {0.96, 0.28}, {-0.28, 0.96}, {0.6, 0.8}};

double lattice(u64 key, long long ix, long long iy) {
    const u64 h = mix64(mix64(key ^ static_cast<u64>(ix)) + static_cast<u64>(iy));
    return hash01(h) * 2.0 - 1.0;
}

double len2(double x, double y) { return std::sqrt(x * x + y * y); }

}  // namespace

TestWorld::TestWorld(TestWorldParams p) : p_(p) {
    axis_x0_ = (hash01(mix64(p_.seed) ^ 0x71) - 0.5) * 300.0;
    side_ = hash01(mix64(p_.seed) ^ 0x73) < 0.5 ? -1 : 1;
    // Valley direction: (s, c) with s/c = tan in [-max, max]; scarp position along the valley.
    const double tn = (hash01(mix64(p_.seed) ^ 0x74) * 2.0 - 1.0) * p_.valley_angle_max_tan;
    c_ = 1.0 / std::sqrt(1.0 + tn * tn);
    s_ = tn * c_;
    scarp_t_ = (hash01(mix64(p_.seed) ^ 0x72) - 0.5) * 0.3 * p_.size_m;
    // Regional tilt direction: a unit vector from two hashes.
    double tx = hash01(mix64(p_.seed) ^ 0x75) - 0.5, ty = hash01(mix64(p_.seed) ^ 0x76) - 0.5;
    double tl = len2(tx, ty);
    if (tl < 1e-3) { tx = 1; ty = 0; tl = 1; }
    tilt_x_ = p_.tilt * tx / tl;
    tilt_y_ = p_.tilt * ty / tl;
}

double TestWorld::noise(int channel, double x, double y, double wl, int rot) const {
    const Rot r = kRots[rot % 6];
    const double u = (r.c * x - r.s * y) / wl + 13.37 * (channel + 1);
    const double v = (r.s * x + r.c * y) / wl + 7.11 * (channel + 1);
    const double fu = std::floor(u), fv = std::floor(v);
    const double tx = u - fu, ty = v - fv;
    auto q = [](double t) { return t * t * t * (t * (t * 6.0 - 15.0) + 10.0); };
    const u64 key = mix64(p_.seed * 0x9E3779B97F4A7C15ull + static_cast<u64>(channel) * 0xD1B54A32D192ED03ull + 17);
    const long long ix = static_cast<long long>(fu), iy = static_cast<long long>(fv);
    const double a = lattice(key, ix, iy), b = lattice(key, ix + 1, iy);
    const double c = lattice(key, ix, iy + 1), d = lattice(key, ix + 1, iy + 1);
    const double sx = q(tx), sy = q(ty);
    return lerp(lerp(a, b, sx), lerp(c, d, sx), sy);
}

double TestWorld::rolling(double x, double y) const {
    const double wx = x + p_.warp_m * noise(10, x, y, 700, 1);
    const double wy = y + p_.warp_m * noise(11, x, y, 700, 2);
    static constexpr double wl[7] = {1100, 520, 240, 110, 60, 90, 40};
    static constexpr double amp[7] = {4.5, 4.5, 2.4, 0.9, 0.3, 0.9, 0.5};
    double sum = 0;
    for (int k = 0; k < 7; ++k) sum += amp[k] * (k >= 5 ? p_.detail_amp : 1.0) * noise(20 + k, wx, wy, wl[k], k + 1);
    return sum * p_.rolling_amp;
}

double TestWorld::axis_cross(double t) const {
    const double n = noise(1, 0, t, 1400, 0) + 0.5 * noise(2, 0, t, 600, 0);
    return axis_x0_ + p_.valley_meander_m * (n / 1.5);
}

Vec2 TestWorld::valley_axis_point(double t) const {
    const double a = axis_cross(t);
    return {t * s_ + a * c_, t * c_ - a * s_};
}

double TestWorld::valley_distance(double x, double y) const {
    const double t = valley_t(x, y);
    const double xr = c_ * x - s_ * y;
    const double dadt = (axis_cross(t + 2) - axis_cross(t - 2)) / 4.0;
    const double k = 1.0 / std::sqrt(1.0 + dadt * dadt);
    return (xr - axis_cross(t)) * k * side_;
}

double TestWorld::valley_floor_half_width(double t) const { return p_.valley_floor_half_width_m * (1.0 + 0.3 * noise(31, 0, t, 900, 0)); }

double TestWorld::scarp_weight(double t) const {
    const double at = std::fabs(t - scarp_t_);
    return 1.0 - smoothstep((at - 0.5 * p_.scarp_stretch_m) / std::max(p_.scarp_fade_m, 1e-6));
}

double TestWorld::height(double x, double y) const {
    // The valley edge wobbles (distance perturbed by a 320 m noise) and its floor width varies along its length, so the
    // sides are not parallel bands; the perturbation is small enough to add at most ~20 % to a slope.
    const double t = valley_t(x, y);
    const double d = valley_distance(x, y) + p_.edge_wobble_m * noise(30, x, y, 320, 3);
    const double floor_w = valley_floor_half_width(t);
    double rise, len;
    double shift = 0;
    if (d > 0) {
        const double w = scarp_weight(t);
        rise = lerp(p_.scarp_side_gentle_rise_m, p_.scarp_rise_m, w);
        len = lerp(p_.scarp_side_gentle_len_m, p_.scarp_len_m, w);
        // Spurs and re-entrants: the foot of the steep side wanders along the valley, and its run varies.
        shift = w * p_.spur_amp_m * noise(40, 0, t, p_.spur_wavelength_m, 0);
        len *= 1.0 + w * p_.scarp_len_var * noise(41, 0, t, 420, 0);
    } else {
        rise = p_.gentle_rise_m;
        len = p_.gentle_len_m;
    }
    const double e = std::max(std::fabs(d) - floor_w + shift, 0.0);
    const double valley = rise * rise_profile(e / len);
    const double roll_scale = p_.floor_roughness + (1.0 - p_.floor_roughness) * smoothstep(e / 150.0);
    const double drain = p_.drainage_fall_m * (t + 0.5 * p_.size_m) / p_.size_m;
    return valley + rolling(x, y) * roll_scale - drain + tilt_x_ * x + tilt_y_ * y;
}

double TestWorld::slope(double x, double y) const {
    constexpr double h = 3.0;
    const double gx = (height(x + h, y) - height(x - h, y)) / (2 * h);
    const double gy = (height(x, y + h) - height(x, y - h)) / (2 * h);
    return std::sqrt(gx * gx + gy * gy);
}

bool TestWorld::valley_ribbon(double x, double y) const {
    const double t = valley_t(x, y);
    if (noise(51, 0, t, 420, 0) <= p_.ribbon_gate) return false;
    const double e = std::fabs(valley_distance(x, y)) - valley_floor_half_width(t);
    const double width = p_.ribbon_width_m * (0.6 + 0.6 * noise(50, 0, t, 200, 0)) * (1.0 + 0.5 * noise(52, x, y, 70, 2));
    return e > -4.0 && e < width;
}

bool TestWorld::field_axis(Vec2 p, Vec2& dir) const {
    constexpr double h = 6.0;
    const double gx = (height(p.x + h, p.y) - height(p.x - h, p.y)) / (2 * h);
    const double gy = (height(p.x, p.y + h) - height(p.x, p.y - h)) / (2 * h);
    const double g = len2(gx, gy);
    if (g > p_.field_axis_slope) {
        dir = {-gy / g, gx / g};  // along the contour
        return true;
    }
    if (std::fabs(valley_distance(p.x, p.y)) < valley_floor_half_width(valley_t(p.x, p.y)) + 160) {
        dir = valley_dir();
        return true;
    }
    return false;
}

Heightfield TestWorld::make_heightfield() const {
    const int n = static_cast<int>(std::floor(p_.size_m / p_.cell_m + 0.5)) + 1;
    Heightfield hf(world_min(), p_.cell_m, n, n);
    for (int j = 0; j < n; ++j)
        for (int i = 0; i < n; ++i)
            hf.set(i, j, static_cast<float>(height(world_min().x + i * p_.cell_m, world_min().y + j * p_.cell_m)));
    return hf;
}

// ------------------------------------------------------------------ cover

namespace {

class TestWorldCover final : public WorldCoverSource {
public:
    TestWorldCover(std::shared_ptr<const TestWorld> w, std::shared_ptr<const VegetationRules> r, std::shared_ptr<const WorldCoverSource> b)
        : w_(std::move(w)), r_(std::move(r)), base_(std::move(b)), p_(w_->params()), wc_(r_->world_cover) {}

    // 1 where woodland is wanted: steep ground (ramp over the slope range) and the valley ribbons.
    double wood_share(Vec2 p) const {
        if (w_->valley_ribbon(p.x, p.y)) return 1.0;
        return smoothstep((w_->slope(p.x, p.y) - p_.wood_slope_lo) / std::max(p_.wood_slope_hi - p_.wood_slope_lo, 1e-6));
    }
    // 1 on the valley floor, 0 beyond floor half width + 60 m.
    double floor_factor(Vec2 p) const {
        const double a = std::fabs(w_->valley_distance(p.x, p.y));
        return 1.0 - smoothstep((a - w_->valley_floor_half_width(w_->valley_t(p.x, p.y))) / 60.0);
    }

    CoverGround ground_at(Vec2 p) const override {
        CoverGround g = base_->ground_at(p);
        const double a = std::fabs(w_->valley_distance(p.x, p.y));
        if (a < p_.wet_half_width_m)
            return {wc_.rough_grass_type, wc_.rough_density, static_cast<std::uint8_t>(wc_.rough_height_class), LandClass::Rough};
        const double ws = wood_share(p);
        if (ws >= 0.5)
            return {wc_.woodland_floor_type, wc_.woodland_density, static_cast<std::uint8_t>(wc_.woodland_height_class), LandClass::Wood};
        if (g.land == LandClass::Arable && ws > 0.3)  // arable on a steep field: grass instead
            return {wc_.open_grass_type, wc_.open_density, static_cast<std::uint8_t>(wc_.open_height_class), LandClass::Pasture};
        return g;
    }

    void points_in(Vec2 lo, Vec2 hi, std::vector<CoverPoint>& out) const override {
        std::vector<CoverPoint> pts;
        base_->points_in(lo, hi, pts);
        for (const CoverPoint& c : pts) {
            const double ff = floor_factor(c.pos);
            if (ff > 0.0 && base_->ground_at(c.pos).land == LandClass::Wood) {
                const double keep = lerp(1.0, p_.valley_wood_keep, ff);
                if (hash01(c.seed ^ 0x5EEDull) >= keep) continue;
            }
            out.push_back(c);
        }
        // Extra woodland on steep ground and valley ribbons, on a fine lattice (same construction as the default's woodland).
        const double F = 5;
        const double prob = std::min(1.0, wc_.woodland_trees_per_ha * F * F / 1e4);
        const std::vector<TreeClass>& cl = wc_.woodland_classes.empty() ? wc_.tree_classes : wc_.woodland_classes;
        if (prob <= 0 || cl.empty()) return;
        double wsum = 0;
        for (const TreeClass& t : cl) wsum += t.weight;
        if (wsum <= 0) return;
        const u64 key = mix64(w_->params().seed ^ 0xA11C0DEull);
        for (long long cy = static_cast<long long>(std::floor(lo.y / F)); cy <= static_cast<long long>(std::floor(hi.y / F)); ++cy)
            for (long long cx = static_cast<long long>(std::floor(lo.x / F)); cx <= static_cast<long long>(std::floor(hi.x / F)); ++cx) {
                auto h = [&](u64 k) { return hash01(mix64(key ^ k) + mix64(static_cast<u64>(cx) * 0x9E3779B1ull) + static_cast<u64>(cy)); };
                const Vec2 pos{(static_cast<double>(cx) + h(1)) * F, (static_cast<double>(cy) + h(2)) * F};
                if (pos.x < lo.x || pos.x >= hi.x || pos.y < lo.y || pos.y >= hi.y) continue;
                const double ws = wood_share(pos);
                if (ws <= 0.0 || h(3) >= prob * ws) continue;
                if (base_->ground_at(pos).land == LandClass::Wood) continue;  // the base already plants this
                if (std::fabs(w_->valley_distance(pos.x, pos.y)) < p_.wet_half_width_m) continue;
                double u = h(4) * wsum;
                const TreeClass* pick = &cl.back();
                for (const TreeClass& t : cl) {
                    if (u < t.weight) { pick = &t; break; }
                    u -= t.weight;
                }
                const VegTypeDef* def = r_->type(pick->type);
                CoverPoint c;
                c.type_id = pick->type;
                c.pos = pos;
                c.yaw = h(5) * 6.283185307179586;
                c.scale = lerp(wc_.tree_scale_min, wc_.tree_scale_max, h(6));
                c.size_class = static_cast<std::uint8_t>(def ? def->size_class : 1);
                c.seed = mix64(key ^ (static_cast<u64>(cx) * 0x100000001B3ull) ^ (static_cast<u64>(cy) << 20) ^ 0x7);
                c.tall = true;
                out.push_back(std::move(c));
            }
    }

    void lines_touching(Vec2 lo, Vec2 hi, std::vector<CoverLine>& out) const override { base_->lines_touching(lo, hi, out); }

private:
    std::shared_ptr<const TestWorld> w_;
    std::shared_ptr<const VegetationRules> r_;
    std::shared_ptr<const WorldCoverSource> base_;
    TestWorldParams p_;
    const WorldCoverRules& wc_;
};

}  // namespace

std::shared_ptr<const WorldCoverSource> make_test_world_cover_source(std::shared_ptr<const TestWorld> world,
                                                                     std::shared_ptr<const VegetationRules> rules,
                                                                     std::shared_ptr<const WorldCoverSource> base) {
    return std::make_shared<TestWorldCover>(std::move(world), std::move(rules), std::move(base));
}

TestWorldBundle make_test_world(const TestWorldParams& params, std::shared_ptr<const VegetationRules> rules) {
    TestWorldBundle b;
    b.world = std::make_shared<const TestWorld>(params);
    b.terrain = std::make_shared<const Heightfield>(b.world->make_heightfield());
    auto hf = b.terrain;
    b.ground_height = [hf](double x, double y) { return hf->height(x, y); };
    b.world_min = b.world->world_min();
    b.world_max = b.world->world_max();
    if (rules) {
        // Per-seed variation of the region's cover rules, within the stated fractions.
        auto varied = std::make_shared<VegetationRules>(*rules);
        WorldCoverRules& wc = varied->world_cover;
        const u64 k = mix64(params.seed ^ 0xC0FFEE5ull);
        auto u = [&](u64 s) { return hash01(mix64(k ^ s)) * 2.0 - 1.0; };
        wc.field_area_ha *= 1.0 + params.field_area_var * u(1);
        wc.wood_block_per_km2 *= 1.0 + params.wood_var * u(2);
        wc.copse_per_km2 *= 1.0 + params.wood_var * u(3);
        wc.belt_share *= 1.0 + params.wood_var * u(4);
        wc.ribbon_band *= 1.0 + 0.5 * params.wood_var * u(5);
        b.rules = varied;
        auto world = b.world;
        CoverHints hints;
        hints.field_axis = [world](Vec2 p, Vec2& d) { return world->field_axis(p, d); };
        b.cover = make_test_world_cover_source(b.world, b.rules, make_default_cover_source(b.rules, params.seed, std::move(hints)));
    }
    return b;
}

void apply_to_vegetation_params(const TestWorldBundle& b, std::uint64_t world_seed, VegetationParams& vp) {
    vp.world_seed = world_seed;
    vp.world_min = b.world_min;
    vp.world_max = b.world_max;
    vp.ground_height = b.ground_height;
    vp.cover = b.cover;
}

}  // namespace cp
