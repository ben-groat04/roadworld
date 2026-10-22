#pragma once
// The prototype's tuning panel (the "Tuning" button, eight sliders) as data: the fields with their labels, ranges
// and steps, the values, and where each one goes. Defaults come from the region pack, never from code.
//
//   key          label (as shown)             where the value lives
//   snap_angle   "Snap angle °"               DrawParams::snap_angle_deg
//   snap_length  "Snap length m"              DrawParams::snap_length
//   wall_cap     "Wall cap m (bridge/tunnel)" RegionPack::grading.wall_cap   (cut / fill depth beyond which a road is a bridge / tunnel)
//   wall_start   "Wall start m"               RegionPack::grading.wall_start (slope height above which a retaining wall replaces the slope)
//   batter       "Batter (H per V)"           RegionPack::grading.batter
//   k_multiplier "Vert. curve K ×"            every RoadType's vertical_k_crest / vertical_k_sag
//   min_span     "Min segment m"              DrawParams::min_span, EditParams::min_span
//   water_level  "Water level m"              world setting; DrawParams / EditParams carry it for the profile series
//
// The region-side values are applied by building a TUNED COPY of the region pack (tuned_region) and giving it to
// the graph (RoadGraph::set_region refits every edge so the K multiplier takes effect); the host then re-runs
// grading after a short delay, as the prototype did (120 ms; 150 ms for water). Tuning is not undoable.
#include <array>
#include <memory>
#include <string>

#include "cp/edit_tool.h"
#include "cp/region_pack.h"
#include "cp/road_tool.h"

namespace cp {

struct ToolTuning {
    double snap_angle = 15;
    double snap_length = 8;
    double wall_cap = 12;
    double wall_start = 12;
    double batter = 2;
    double k_multiplier = 1;
    double min_span = 4;
    double water_level = 14;

    bool operator==(const ToolTuning&) const = default;
};

// Who owns the value: a per-user tool setting, region data (the graded world depends on it), or a world setting.
enum class TuningScope { Tool, Region, World };

struct TuningField {
    const char* key;
    const char* label;
    double ToolTuning::*member;
    double min, max, step;
    TuningScope scope;
    bool regrades;  // changing it re-runs grading of the whole network
};

// The eight fields in panel order, with the prototype's ranges and steps.
const std::array<TuningField, 8>& tuning_fields();

// The region's defaults (RoadToolRules plus the grading rules); a null region gives the prototype's values.
ToolTuning default_tuning(const RegionPack* region);
// Clamp every value to its field's range and snap it to the field's step (what a slider can produce).
ToolTuning clamped(const ToolTuning& t);

// Fold the tool-side values into the sessions' parameters (snap, min span, water level).
void apply_tuning(const ToolTuning& t, DrawParams& p);
void apply_tuning(const ToolTuning& t, EditParams& p);
// A copy of `base` with the region-side values (wall cap / start, batter, K multiplier) replaced. The K multiplier
// is applied to the BASE pack's K values, so applying 1.0 again restores them.
std::shared_ptr<const RegionPack> tuned_region(const RegionPack& base, const ToolTuning& t);
// "Wall cap m (bridge/tunnel)" -> 12: the value text the panel shows next to the label (no units, trailing zeros trimmed).
std::string tuning_value_text(const TuningField& f, double value);

}  // namespace cp
