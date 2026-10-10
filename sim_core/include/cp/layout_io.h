#pragma once
// Test-layout files: a small, engine-neutral JSON description of a road layout plus named cameras.
// THIS IS NOT THE SAVE FORMAT. It is a hand-authorable fixture format for look tests and
// like-for-like reference recreations (data/layouts/); the save format is separate and will carry
// ids, undo history and everything else a real save needs.
//
// A layout never touches the graph directly: build_layout() replays it through cp::Command objects on a
// cp::CommandStack (make_draw_road, make_set_structure_span, make_place_roundabout), the same commands a
// player edit uses, so roads that meet or cross are split into junctions exactly as in the game.
//
// JSON (x east, y north, z up, metres; headings in degrees, 0 = east, counter-clockwise; keys starting
// with '_' and "notes" are ignored):
//   { "format": "cp-test-layout", "version": 1, "name": "...", "region": "uk", "driving_side": "left",
//     "roads": [ { "type": "b_road", "one_way": false,
//                  "points": [ [x, y, z], {"p": [x, y, z], "heading_deg": 90}, [x, y] ],   // z defaults to 0
//                  "structures": [ {"kind": "bridge"|"tunnel", "at": [x, y], "length": 40} ] } ],
//     "roundabouts": [ { "centre": [x, y, z], "radius": 25, "type": "roundabout_ring" } ],   // type optional
//     "junctions": { "corner_radii": [ {"node": [x, y], "arm_a": [x, y], "arm_b": [x, y], "radius": 12} ],
//                    "crossings":    [ {"node": [x, y], "arm": [x, y]} ] },
//     "cameras": [ {"name": "...", "pos": [x, y, z], "target": [x, y, z], "fov_deg": 70,
//                   "aspect": 2.3, "photo": "file name", "notes": "..."} ] }
// A pinned heading is only allowed on the first and last point of a road. Road ends within 1 m of an
// existing node or road centreline attach to it (splitting the road); an interior stretch that crosses
// an existing road in plan at the same level (dz < region plan_crossing_junction_dz_m) or passes through
// an existing node splits the new road there. A bridge/tunnel `at` point selects the edge of that
// road type passing within 1.5 m of it; the span is centred on it. Corner and crossing arms are given
// by any point on the arm's road near the node (nearest arm wins).
// Order of replay: roads, roundabouts, structures, junction overrides.
// No exceptions are thrown anywhere; errors come back as a message naming the offending item.
#include <filesystem>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "cp/command.h"
#include "cp/junction.h"
#include "cp/road_graph.h"
#include "cp/vec.h"

namespace cp {

struct LayoutPoint {
    Vec3 pos;
    std::optional<double> heading;  // rad, pinned tangent (first/last point only)
};

struct LayoutStructure {
    StructureKind kind = StructureKind::Bridge;
    Vec2 at;
    double length = 0;
};

struct LayoutRoad {
    std::string type;
    bool one_way = false;
    std::vector<LayoutPoint> points;
    std::vector<LayoutStructure> structures;
};

struct LayoutRoundabout {
    Vec3 centre;
    double radius = 0;
    std::string type;  // empty: the region's default ring road type
};

struct LayoutCornerRadius {
    Vec2 node, arm_a, arm_b;
    double radius = 0;
};

struct LayoutCrossing {
    Vec2 node, arm;
};

struct LayoutCamera {
    std::string name;
    Vec3 pos, target;
    double fov_deg = 60;  // horizontal field of view
    double aspect = 0;    // width / height of the matched photo; 0 = unspecified
    std::string photo;    // file name of the reference photo this camera matches (may be empty)
    std::string notes;
};

struct LayoutDoc {
    std::string name;
    // Both optional in the file and NOT defaulted by the parser: an empty region / unset driving side means
    // "the caller's pack decides". build_layout checks a stated value against the pack and fills both in
    // (LoadedLayout::doc then carries the resolved values).
    std::string region;
    std::optional<DrivingSide> driving_side;
    std::vector<LayoutRoad> roads;
    std::vector<LayoutRoundabout> roundabouts;
    std::vector<LayoutCornerRadius> corner_radii;
    std::vector<LayoutCrossing> crossings;
    std::vector<LayoutCamera> cameras;
};

// Syntax and range validation only (no region pack needed). False + message (with the road index)
// on error; `out` is then unspecified.
bool parse_layout(const std::string& json_text, LayoutDoc& out, std::string& error);
// Pretty JSON that parse_layout reads back to an equal document.
std::string layout_to_json(const LayoutDoc& doc);

// A layout built into a graph. Not movable (the stack references the graph).
struct LoadedLayout {
    LayoutDoc doc;
    std::shared_ptr<const RegionPack> region;
    RoadGraph graph;
    CommandStack<RoadGraph> stack{graph};
    JunctionOverrides overrides;
    std::set<std::pair<NodeId, EdgeId>> crossing_arms;  // for MarkingParams::crossing_arms
    LoadedLayout() = default;
    LoadedLayout(const LoadedLayout&) = delete;
    LoadedLayout& operator=(const LoadedLayout&) = delete;
};

// Replays `doc` into `out` (which must be fresh). A region id or driving side stated in the document must match
// `region`; one the document leaves out is inherited from `region` (driving side comes from the pack unless stated).
bool build_layout(const LayoutDoc& doc, std::shared_ptr<const RegionPack> region, LoadedLayout& out, std::string& error);

// parse_layout + load the region pack from <data_dir>/regions/<id>/ + build_layout. The file must name its
// region: nothing else says which pack to load.
std::unique_ptr<LoadedLayout> load_layout(const std::string& json_text, const std::filesystem::path& data_dir,
                                          std::string& error);
// parse_layout + build_layout against a pack the caller already holds. A layout without "region" inherits
// the pack; one that names a different region is an error.
std::unique_ptr<LoadedLayout> load_layout(const std::string& json_text, std::shared_ptr<const RegionPack> region,
                                          std::string& error);
std::unique_ptr<LoadedLayout> load_layout_file(const std::filesystem::path& file, const std::filesystem::path& data_dir,
                                               std::string& error);

}  // namespace cp
