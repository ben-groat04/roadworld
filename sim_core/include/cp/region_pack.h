#pragma once
// Region pack loading (data/lane_types.json + data/regions/<id>/*.json) into plain
// structs. The core assumes nothing about any region: every value comes from here.
// Internal units: metres, m/s, radians; grades are rise over run (not %).
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace cp {

enum class DrivingSide { Left, Right };

// Travel direction of a lane relative to the edge's a -> b direction.
enum class TravelDir { None, Forward, Backward };

enum class SlotSide { Nearside, Offside };

struct LaneType {
    std::string id;
    std::vector<std::string> users;
    bool may_park = false;
    bool connects_at_junctions = false;
    std::string surface;
};

// One strip of a cross-section. `dir` is resolved at load: an explicit "dir" wins,
// else lanes that carry cars and cannot park are Forward, everything else None.
// For a `half`, Forward means "this half's own direction of travel".
// `side` is the optional "side" field from the data (informational: slot order,
// offside -> nearside for carriageways and one-way variants, decides the layout).
struct CrossSectionSlot {
    std::string lane_type;
    double width = 0;  // m
    TravelDir dir = TravelDir::None;
    SlotSide side = SlotSide::Nearside;
};

struct RoadType {
    std::string id;
    std::string name;
    bool two_way = false;
    bool dual = false;
    double speed_limit = 0;   // m/s
    double design_speed = 0;  // m/s
    int road_class = 0;        // relative importance (higher = more major); 0 = unset (use design speed in km/h)
    double corner_radius = 0;  // m, default junction kerb fillet radius; 0 = unset (not applicable)
    double median_gap = 0;    // m, dual types only
    // half: centre -> nearside, one direction (two-way types).
    // one_way_variant, carriageway: offside -> nearside, whole cross-section.
    std::vector<CrossSectionSlot> half;
    std::vector<CrossSectionSlot> one_way_variant;
    std::vector<CrossSectionSlot> carriageway;
    double vertical_k_crest = 0;   // m per % grade change
    double vertical_k_sag = 0;
    double min_radius = 0;          // m, advisory (0 = unset)
    double advisory_max_grade = 0;  // rise/run, advisory (0 = unset)
    std::string markings;
};

struct RegionPack {
    std::string id;
    int version = 0;
    DrivingSide driving_side = DrivingSide::Left;
    double headroom_m = 0;
    double plan_crossing_junction_dz_m = 0;
    std::map<std::string, LaneType> lane_types;
    std::map<std::string, RoadType> road_types;

    const LaneType* lane_type(const std::string& id) const;
    const RoadType* road_type(const std::string& id) const;
};

// Value-or-error result; `error` is a human-readable message when there is no value.
template <class T>
struct Loaded {
    std::optional<T> value;
    std::string error;
    explicit operator bool() const { return value.has_value(); }
    T& operator*() { return *value; }
    const T& operator*() const { return *value; }
    T* operator->() { return &*value; }
    const T* operator->() const { return &*value; }
};

using LaneTypeTable = std::map<std::string, LaneType>;

// String-based parsers (testable without files).
Loaded<LaneTypeTable> parse_lane_types(const std::string& json_text);
Loaded<RegionPack> parse_region_pack(const LaneTypeTable& lane_types, const std::string& region_json,
                                     const std::string& road_types_json);

// File-based loaders.
Loaded<LaneTypeTable> load_lane_types(const std::filesystem::path& file);
// region_dir contains region.json and road_types.json.
Loaded<RegionPack> load_region_pack(const std::filesystem::path& lane_types_file,
                                    const std::filesystem::path& region_dir);
// Convenience: <data_dir>/lane_types.json + <data_dir>/regions/<region_id>/.
Loaded<RegionPack> load_region_pack_by_id(const std::filesystem::path& data_dir, const std::string& region_id);

}  // namespace cp
