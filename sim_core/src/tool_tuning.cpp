// No exceptions: sim_core builds with -fno-exceptions.
// Port of the TUNE table / T defaults of prototypes/road-draw/src (config.js, ui.js).
#include "cp/tool_tuning.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace cp {

const std::array<TuningField, 8>& tuning_fields() {
    static const std::array<TuningField, 8> f{{
        {"snap_angle", "Snap angle \xC2\xB0", &ToolTuning::snap_angle, 5, 45, 5, TuningScope::Tool, false},
        {"snap_length", "Snap length m", &ToolTuning::snap_length, 1, 50, 1, TuningScope::Tool, false},
        {"wall_cap", "Wall cap m (bridge/tunnel)", &ToolTuning::wall_cap, 3, 30, 1, TuningScope::Region, true},
        {"wall_start", "Wall start m", &ToolTuning::wall_start, 2, 12, 1, TuningScope::Region, true},
        {"batter", "Batter (H per V)", &ToolTuning::batter, 1, 4, 0.1, TuningScope::Region, true},
        {"k_multiplier", "Vert. curve K \xC3\x97", &ToolTuning::k_multiplier, 0.25, 2, 0.05, TuningScope::Region, true},
        {"min_span", "Min segment m", &ToolTuning::min_span, 1, 20, 1, TuningScope::Tool, false},
        {"water_level", "Water level m", &ToolTuning::water_level, 0, 60, 0.5, TuningScope::World, true},
    }};
    return f;
}

ToolTuning default_tuning(const RegionPack* region) {
    ToolTuning t;
    if (!region) return t;
    t.snap_angle = region->road_tool.snap_angle_deg;
    t.snap_length = region->road_tool.snap_length;
    t.min_span = region->road_tool.min_span;
    t.water_level = region->road_tool.water_level;
    t.k_multiplier = region->road_tool.k_multiplier;
    t.wall_cap = region->grading.wall_cap;
    t.wall_start = region->grading.wall_start;
    t.batter = region->grading.batter;
    return t;
}

ToolTuning clamped(const ToolTuning& in) {
    ToolTuning t = in;
    for (const TuningField& f : tuning_fields()) {
        double v = std::clamp(t.*f.member, f.min, f.max);
        v = f.min + std::floor((v - f.min) / f.step + 0.5) * f.step;  // the slider's grid
        v = std::clamp(v, f.min, f.max);
        t.*f.member = std::round(v * 1e6) / 1e6;
    }
    return t;
}

void apply_tuning(const ToolTuning& t, DrawParams& p) {
    p.snap_angle_deg = t.snap_angle;
    p.snap_length = t.snap_length;
    p.min_span = t.min_span;
    p.water_level = t.water_level;
}

void apply_tuning(const ToolTuning& t, EditParams& p) {
    p.min_span = t.min_span;
    p.water_level = t.water_level;
}

std::shared_ptr<const RegionPack> tuned_region(const RegionPack& base, const ToolTuning& t) {
    auto out = std::make_shared<RegionPack>(base);
    out->grading.wall_cap = t.wall_cap;
    out->grading.wall_start = t.wall_start;
    out->grading.batter = t.batter;
    for (auto& [id, rt] : out->road_types) {
        const RoadType* b = base.road_type(id);
        if (!b) continue;
        rt.vertical_k_crest = b->vertical_k_crest * t.k_multiplier;
        rt.vertical_k_sag = b->vertical_k_sag * t.k_multiplier;
    }
    out->road_tool.snap_angle_deg = t.snap_angle;
    out->road_tool.snap_length = t.snap_length;
    out->road_tool.min_span = t.min_span;
    out->road_tool.water_level = t.water_level;
    return out;
}

std::string tuning_value_text(const TuningField&, double value) {
    char buf[32];
    std::snprintf(buf, sizeof buf, "%.4f", value);
    std::string s = buf;
    while (!s.empty() && s.back() == '0') s.pop_back();
    if (!s.empty() && s.back() == '.') s.pop_back();
    return s;
}

}  // namespace cp
