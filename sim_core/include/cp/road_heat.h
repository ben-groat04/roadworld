#pragma once
// The road colour modes of the road tool (the prototype's C key: plain / curvature / gradient) as data: for a
// road, one value and one class per centreline vertex, so a host can colour the carriageway mesh per vertex and
// let the GPU interpolate between them (what the prototype did, JS roadmesh.js heatCurv / heatGrade, `cc(k)`).
//
// Classes (region data: RoadToolRules::heat, defaults are the prototype's):
//   curvature, R the local radius, Rmin the road type's comfortable minimum radius:
//     R < 0.5 Rmin red, R < Rmin amber, R < 2 Rmin caution, else ok;
//   gradient, g the local grade, gmax the road type's advisory maximum grade:
//     g > 1.5 gmax red, g > gmax amber, g > 0.5 gmax caution, else ok.
// A road type with no Rmin / gmax set (0) is always Ok in that mode.
#include <array>
#include <cstdint>
#include <vector>

#include "cp/edge_sampling.h"
#include "cp/region_pack.h"

namespace cp {

enum class ColourMode { Plain, Curvature, Gradient };

enum class HeatClass { Ok, Caution, Amber, Red };

// Plain -> Curvature -> Gradient -> Plain (the C key).
ColourMode next_colour_mode(ColourMode m);
// "plain", "curvature", "gradient" (the HUD header's `colour: ...`).
const char* colour_mode_name(ColourMode m);

// `curvature` in 1/m (sign ignored); `min_radius` in m (0: unset -> Ok).
HeatClass heat_class_curvature(double curvature, double min_radius, const HeatMapRules& rules);
// `grade` rise over run (sign ignored); `max_grade` rise over run (0: unset -> Ok).
HeatClass heat_class_gradient(double grade, double max_grade, const HeatMapRules& rules);
// 0xRRGGBB of a class.
std::uint32_t heat_colour(HeatClass c, const HeatMapRules& rules);
// The same as three 0..1 floats, sRGB as written in the data (the host converts to linear if it needs to).
std::array<float, 3> heat_rgb(HeatClass c, const HeatMapRules& rules);

struct HeatVertex {
    double s = 0;      // m along the centreline
    Vec3 pos;          // on the centreline
    double value = 0;  // Curvature: |1/R| in 1/m (0 = straight); Gradient: |grade|, rise over run
    HeatClass cls = HeatClass::Ok;
    std::uint32_t colour = 0;  // 0xRRGGBB of cls
};

// One vertex per centreline sample of the edge (both ends included; `max_step` bounds the spacing, the sampler
// adds vertices in tight curves). Empty for Plain, an unknown road type or an edge with no geometry.
std::vector<HeatVertex> heat_vertices(const RoadGraph& g, const Edge& edge, ColourMode mode, double max_step = 2.0);

}  // namespace cp
