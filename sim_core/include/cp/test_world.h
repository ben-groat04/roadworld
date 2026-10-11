#pragma once
// TEST WORLD: a small seeded, non-flat stand-in for the "existing open world" the player places roads into.
// Gentle lowland countryside: rolling ground with a slight regional tilt, one broad valley (its direction varies per
// seed) with a flat-ish floor, one steeper valley side (scarp) over a limited stretch of the valley, broken into spurs
// and re-entrants. It will be replaced by a real world generator; the seam is
//   TestWorldBundle { terrain Heightfield, ground_height function, WorldCoverSource }
// which is exactly what grading (Heightfield) and VegetationParams (ground_height, cover) consume.
//
// Determinism (gcc == MSVC, any platform): the height is a pure function of (params, x, y) built ONLY from
// + - * / (IEEE-754 exact), std::floor and std::sqrt (both exactly rounded by the standard), integer hashing
// (mix64) and fixed rational rotation matrices. No sin/cos/exp/pow/atan, no std distributions, no <random>.
// Compile flags that could fuse multiply-adds are not relied on: tests compare against golden millimetre values
// that must match on both compilers.
#include <cstdint>
#include <functional>
#include <memory>

#include "cp/heightfield.h"
#include "cp/vec.h"
#include "cp/vegetation.h"

namespace cp {

struct TestWorldParams {
    std::uint64_t seed = 1;
    double size_m = 2048;       // square, centred on the origin: [-size/2, size/2]
    double cell_m = 4;          // heightfield cell size

    // Rolling ground: sum of 7 rotated value-noise octaves (wavelengths 1100/520/240/110/60/90/40 m), scaled by this.
    double rolling_amp = 1.3;   // 1 = about +-9 m of rolling over the plateau; default 1.3 gives slopes mostly 2-8 %
    double detail_amp = 1.0;    // extra multiplier on the two short octaves (90 and 40 m): 1-3 m rolling detail on the plateau
    double warp_m = 40;         // domain warp amplitude (breaks up any lattice alignment)
    double tilt = 0.006;        // regional tilt, rise/run, direction per seed

    // Valley: runs along a direction that varies per seed (up to valley_angle_max_tan from +y), meandering. The floor is
    // flat-ish and falls gently along the valley.
    double valley_angle_max_tan = 0.8;     // |tan| of the largest rotation of the valley axis away from +y
    double valley_meander_m = 160;         // sideways wander of the valley axis (amplitude)
    double valley_floor_half_width_m = 110;
    double drainage_fall_m = 5;            // fall of the whole world over its size along the valley (0.25 %)
    double edge_wobble_m = 12;             // irregularity of the valley edge (distance noise amplitude)
    double floor_roughness = 0.3;          // rolling amplitude multiplier on the valley floor (0..1)

    // Valley sides (a profile with a constant-slope middle and smooth feet).
    double gentle_rise_m = 22, gentle_len_m = 400;       // the gentle side (and the scarp side away from the scarp)
    double scarp_side_gentle_rise_m = 24, scarp_side_gentle_len_m = 410;
    double scarp_rise_m = 46, scarp_len_m = 150;         // the steep stretch: mean slope rise/len = 30 %, plateau ~40 %
    double scarp_stretch_m = 560;          // length along the valley where the scarp is fully developed
    double scarp_fade_m = 420;             // blend length at each end
    double spur_amp_m = 55, spur_wavelength_m = 340;     // the scarp crest wanders sideways: spurs and re-entrants
    double scarp_len_var = 0.08;           // fractional variation of the scarp's run along the valley

    // Cover (terrain-aware cover source).
    double wood_slope_lo = 0.10, wood_slope_hi = 0.20;   // woodland share ramps over this slope range (rise/run)
    double valley_wood_keep = 0.35;        // fraction of default woodland trees kept on the valley floor
    double wet_half_width_m = 7;           // rough/wet ground along the valley line (floor only)
    double ribbon_width_m = 32;            // stream tree line along the valley: mean width, varies 8-32 m, in stretches
    double ribbon_gate = -0.1;            // ribbon exists where a noise along the valley exceeds this (-1 = everywhere)
    double field_axis_slope = 0.045;       // fields align with contours where the slope exceeds this
    // Per-seed variation of the cover rules (fractions): field area and wood shapes are scaled within +-.
    double field_area_var = 0.25, wood_var = 0.4;
};

// Pure terrain model. Cheap to copy-construct via shared_ptr; all queries are const and thread-safe.
class TestWorld {
public:
    explicit TestWorld(TestWorldParams p = {});
    const TestWorldParams& params() const { return p_; }

    double height(double x, double y) const;           // m
    double slope(double x, double y) const;            // rise/run (gradient magnitude, central difference over 3 m)
    // The valley runs along unit vector valley_dir() (rotated from +y by a per-seed angle). `t` is the coordinate along it
    // (m, 0 at the world centre's projection); the axis meanders sideways of the straight line.
    Vec2 valley_dir() const { return {s_, c_}; }
    Vec2 valley_axis_point(double t) const;            // world point on the valley axis at along-valley coordinate t
    double valley_t(double x, double y) const { return s_ * x + c_ * y; }
    double valley_distance(double x, double y) const;  // signed, ~perpendicular, m; > 0 on the scarp side
    double valley_floor_half_width(double t) const;    // m, varies along the valley
    double scarp_weight(double t) const;               // 0..1 how developed the scarp is at along-valley coordinate t
    double scarp_centre_t() const { return scarp_t_; }
    Vec2 scarp_centre() const { return valley_axis_point(scarp_t_); }
    int scarp_side() const { return side_; }           // +1: the scarp lies at +cross of the axis, -1 at -cross
    Vec2 cross_dir() const { return {c_ * side_, -s_ * side_}; }  // unit vector from the axis towards the scarp side
    Vec2 world_min() const { return {-p_.size_m / 2, -p_.size_m / 2}; }
    Vec2 world_max() const { return {p_.size_m / 2, p_.size_m / 2}; }

    // Thin sinuous tree line on the stream (in stretches, width varying); a pure function of position.
    bool valley_ribbon(double x, double y) const;
    // Preferred field direction (contour direction on slopes, valley direction near the valley); false = none.
    bool field_axis(Vec2 p, Vec2& dir) const;

    // Samples the terrain on the params grid (nx = ny = size/cell + 1).
    Heightfield make_heightfield() const;

private:
    double noise(int channel, double x, double y, double wavelength, int rot) const;
    double rolling(double x, double y) const;
    double axis_cross(double t) const;                 // cross coordinate of the axis at t
    TestWorldParams p_;
    double axis_x0_ = 0, scarp_t_ = 0;
    double c_ = 1, s_ = 0;                             // valley direction (s_, c_), cross direction (c_, -s_)
    double tilt_x_ = 0, tilt_y_ = 0;
    int side_ = 1;
};

// Terrain-aware cover: wraps `base` (normally make_default_cover_source with the terrain's field_axis hint). Woodland is
// favoured on slopes above params.wood_slope_lo and beside the valley floor (ribbons), thinned on the valley floor, and a
// wet/rough strip follows the valley line. A pure function of position (and the base's own purity).
std::shared_ptr<const WorldCoverSource> make_test_world_cover_source(std::shared_ptr<const TestWorld> world,
                                                                     std::shared_ptr<const VegetationRules> rules,
                                                                     std::shared_ptr<const WorldCoverSource> base);

// Everything a caller needs for the test world. Replace by a real generator's equivalent later.
struct TestWorldBundle {
    std::shared_ptr<const TestWorld> world;
    std::shared_ptr<const Heightfield> terrain;
    std::function<double(double, double)> ground_height;     // reads `terrain` (bilinear); keeps it alive
    std::shared_ptr<const WorldCoverSource> cover;           // null when no rules were given
    std::shared_ptr<const VegetationRules> rules;            // the per-seed varied rules the cover uses (null without rules)
    Vec2 world_min, world_max;                               // for VegetationParams
};
// `rules` may be null (then cover is null). The cover uses a per-seed variation of `rules` (field area, wood shapes).
TestWorldBundle make_test_world(const TestWorldParams& params, std::shared_ptr<const VegetationRules> rules = nullptr);
// Fills the world-related fields of VegetationParams (world_seed, extent, ground_height, cover).
void apply_to_vegetation_params(const TestWorldBundle& b, std::uint64_t world_seed, VegetationParams& vp);

}  // namespace cp
