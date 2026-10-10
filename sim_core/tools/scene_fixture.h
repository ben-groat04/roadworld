#pragma once
// Named road fixtures built purely through commands (CommandStack), plus the JSON exporter
// the road-draw prototype reads. Tooling only: not part of the sim_core library.
#include <memory>
#include <string>
#include <vector>

#include "cp/command.h"
#include "cp/junction.h"
#include "cp/junction_roundabout.h"
#include "cp/road_commands.h"

namespace cp::tools {

// Geometry of the `interchange` fixture (metres, x east, y north; dual along the x axis).
struct InterchangeDims {
    // The dual-carriageway offset is not a constant: cp::interchange_dual_offset(region) (cp/test_layouts.h).
    static constexpr double deck_z = 7.0;            // bridge deck level over the dual
    static constexpr double deck_depth = 0.8;        // structure depth under the deck level
    static constexpr double rb_y = 80.0;             // north roundabout centre on the minor road
    static constexpr double rb_radius = 25.0;
    static constexpr double xroads_y = -80.0;        // south crossroads
    static constexpr double tee_y = 230.0;           // T-junction (150 m north of the roundabout)
    static constexpr double slip_x = 260.0;          // diverge/merge nodes at x = +-260
};

struct Scene {
    std::shared_ptr<const RegionPack> region;
    RoadGraph graph;
    CommandStack<RoadGraph> stack{graph};
    JunctionOverrides overrides;
    std::string name;
    Scene() = default;
    Scene(const Scene&) = delete;
    Scene& operator=(const Scene&) = delete;
};

// Names understood by build_scene().
std::vector<std::string> scene_names();

// Builds the named fixture. Returns null and fills `error` on failure.
std::unique_ptr<Scene> build_scene(const std::string& name, const std::string& data_dir, std::string& error);

// JSON contract version 1 (prototypes/road-draw reads exactly this).
std::string export_scene_json(const Scene& scene, const JunctionSet& junctions, int* export_errors = nullptr);

// JSON number with `decimals` places, independent of the C locale. Non-finite values come out as
// "null" and set *finite to false.
std::string json_number(double v, int decimals, bool* finite = nullptr);
// JSON string literal with quotes, backslashes and control characters escaped.
std::string json_string(const std::string& s);

// Inputs of export.json as written by export_mesh. The *_json members are already-rendered JSON fragments:
// nodes = comma-separated [x,y,z] triples, cameras / files = comma-separated objects, structures = one object.
struct MeshExportJson {
    std::string fixture, region, driving_side;
    double origin_x = 0, origin_y = 0, marking_lift = 0;
    double lo[3] = {0, 0, 0}, hi[3] = {0, 0, 0};  // bounds already shifted by the origin offset (x, y)
    std::string nodes_json, structures_json, cameras_json, files_json;
};
// The complete export.json document (what prototypes/mesh-view reads).
std::string mesh_export_json(const MeshExportJson& m);

// Builds junctions for the scene (all nodes) with its overrides, including the roundabout post-pass
// (whose group-level result is returned through `roundabouts` when given).
JunctionSet scene_junctions(const Scene& scene, RoundaboutResult* roundabouts = nullptr);

}  // namespace cp::tools
