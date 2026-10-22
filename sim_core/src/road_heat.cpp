// No exceptions: sim_core builds with -fno-exceptions.
// Port of heatCurv / heatGrade and the per-vertex surface colours of prototypes/road-draw/src/roadmesh.js.
#include "cp/road_heat.h"

#include <algorithm>
#include <cmath>

namespace cp {

ColourMode next_colour_mode(ColourMode m) {
    switch (m) {
        case ColourMode::Plain: return ColourMode::Curvature;
        case ColourMode::Curvature: return ColourMode::Gradient;
        case ColourMode::Gradient: return ColourMode::Plain;
    }
    return ColourMode::Plain;
}

const char* colour_mode_name(ColourMode m) {
    switch (m) {
        case ColourMode::Plain: return "plain";
        case ColourMode::Curvature: return "curvature";
        case ColourMode::Gradient: return "gradient";
    }
    return "plain";
}

HeatClass heat_class_curvature(double curvature, double min_radius, const HeatMapRules& r) {
    if (!(min_radius > 0)) return HeatClass::Ok;
    const double k = std::abs(curvature);
    const double radius = k > 1e-9 ? 1 / k : 1e9;  // the prototype's straight: R = 1e9
    if (radius < min_radius * r.curvature_red) return HeatClass::Red;
    if (radius < min_radius * r.curvature_amber) return HeatClass::Amber;
    if (radius < min_radius * r.curvature_caution) return HeatClass::Caution;
    return HeatClass::Ok;
}

HeatClass heat_class_gradient(double grade, double max_grade, const HeatMapRules& r) {
    if (!(max_grade > 0)) return HeatClass::Ok;
    const double g = std::abs(grade);
    if (g > max_grade * r.gradient_red) return HeatClass::Red;
    if (g > max_grade * r.gradient_amber) return HeatClass::Amber;
    if (g > max_grade * r.gradient_caution) return HeatClass::Caution;
    return HeatClass::Ok;
}

std::uint32_t heat_colour(HeatClass c, const HeatMapRules& r) {
    switch (c) {
        case HeatClass::Ok: return r.colour_ok;
        case HeatClass::Caution: return r.colour_caution;
        case HeatClass::Amber: return r.colour_amber;
        case HeatClass::Red: return r.colour_red;
    }
    return r.colour_ok;
}

std::array<float, 3> heat_rgb(HeatClass c, const HeatMapRules& rules) {
    const std::uint32_t v = heat_colour(c, rules);
    return {static_cast<float>((v >> 16) & 0xff) / 255.0f, static_cast<float>((v >> 8) & 0xff) / 255.0f,
            static_cast<float>(v & 0xff) / 255.0f};
}

std::vector<HeatVertex> heat_vertices(const RoadGraph& g, const Edge& edge, ColourMode mode, double max_step) {
    std::vector<HeatVertex> out;
    if (mode == ColourMode::Plain || !g.region()) return out;
    const RoadType* rt = g.region()->road_type(edge.road_type);
    if (!rt || edge.horizontal.spans.empty()) return out;
    const HeatMapRules& rules = g.region()->road_tool.heat;
    SamplingParams sp;
    sp.max_step = std::max(max_step, 0.25);
    for (const EdgeSample& s : sample_centreline(edge, sp)) {
        HeatVertex v;
        v.s = s.s;
        v.pos = s.pos;
        if (mode == ColourMode::Curvature) {
            v.value = std::abs(s.curvature);
            v.cls = heat_class_curvature(s.curvature, rt->min_radius, rules);
        } else {
            v.value = std::abs(s.grade);
            v.cls = heat_class_gradient(s.grade, rt->advisory_max_grade, rules);
        }
        v.colour = heat_colour(v.cls, rules);
        out.push_back(v);
    }
    return out;
}

}  // namespace cp
