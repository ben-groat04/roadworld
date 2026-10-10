// Writes the render meshes of a fixture to Wavefront .obj files, one per surface class, plus one per
// marking colour id and export.json (files, bounds, origin offset, node positions), for inspection.
// Coordinates are the sim frame (x east, y north, z up) with the common origin offset subtracted
// (see export.json). Usage: export_mesh [--data <dir>] [--tol <metres>] --out <dir> (<fixture> | --layout <file>)
// A layout file (cp/layout_io.h, data/layouts/) is built through the same commands as a player edit; its named
// cameras go to export.json ("cameras", absolute sim metres, NOT minus origin_offset).
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "cp/layout_io.h"
#include "cp/markings.h"
#include "cp/render_mesh.h"
#include "cp/structure_mesh.h"
#include "scene_fixture.h"

namespace {
constexpr double kMarkingLift = 0.005;  // m above the surface (stand-in for a renderer depth bias)

// Layouts have no terrain: structures stand on a flat plane at z = 0.
struct FlatGround final : cp::GroundSampler {
    double height(double, double) const override { return 0.0; }
};
}

int main(int argc, char** argv) {
    std::string data = CP_DATA_DIR, out_dir, name, layout_file;
    cp::RenderMeshParams params;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--data" && i + 1 < argc) data = argv[++i];
        else if (a == "--out" && i + 1 < argc) out_dir = argv[++i];
        else if (a == "--layout" && i + 1 < argc) layout_file = argv[++i];
        else if (a == "--tol" && i + 1 < argc) params.chord_tolerance = std::atof(argv[++i]);
        else name = a;
    }
    if ((name.empty() == layout_file.empty()) || out_dir.empty()) {
        std::fprintf(stderr, "usage: export_mesh [--data <dir>] [--tol <m>] --out <dir> (--layout <file> | <fixture>)   fixtures:");
        for (const auto& n : cp::tools::scene_names()) std::fprintf(stderr, " %s", n.c_str());
        std::fprintf(stderr, "\n");
        return 2;
    }
    std::string err;
    std::unique_ptr<cp::tools::Scene> sc;
    std::unique_ptr<cp::LoadedLayout> lay;
    const cp::RoadGraph* graph = nullptr;
    const cp::RegionPack* region = nullptr;
    const cp::JunctionOverrides* overrides = nullptr;
    cp::MarkingParams mark_params;
    std::vector<cp::LayoutCamera> cameras;
    if (!layout_file.empty()) {
        lay = cp::load_layout_file(layout_file, data, err);
        if (!lay) {
            std::fprintf(stderr, "export_mesh: %s\n", err.c_str());
            return 1;
        }
        name = lay->doc.name.empty() ? std::filesystem::path(layout_file).stem().string() : lay->doc.name;
        graph = &lay->graph;
        region = lay->region.get();
        overrides = &lay->overrides;
        mark_params.crossing_arms = lay->crossing_arms;
        cameras = lay->doc.cameras;
    } else {
        sc = cp::tools::build_scene(name, data, err);
        if (!sc) {
            std::fprintf(stderr, "export_mesh: %s\n", err.c_str());
            return 1;
        }
        graph = &sc->graph;
        region = sc->region.get();
        overrides = &sc->overrides;
    }
    cp::JunctionSet js = cp::build_plain_junctions(*graph, *region, *overrides);
    const cp::RoundaboutResult rr = cp::apply_roundabouts(js, *graph, *region, *overrides);
    for (const auto& p : rr.problems) std::fprintf(stderr, "export_mesh: roundabout problem: %s\n", p.what.c_str());
    cp::WorldMeshSet all;
    for (const auto& [id, e] : graph->edges())
        cp::append_mesh(all, cp::build_edge_mesh(*graph, *region, id, js, params));
    for (const auto& [n, j] : js.junctions)
        cp::append_mesh(all, cp::build_junction_mesh(*graph, *region, j, params));
    for (const auto& l : rr.layouts)
        cp::append_mesh(all, cp::build_roundabout_mesh(*graph, *region, l, params));

    // Structures (bridges, tunnels) on flat ground at z = 0; there are no retaining walls without grading.
    const FlatGround flat;
    const cp::StructureMeshes structs = cp::build_structures(*graph, *region, flat, cp::structure_params_for(*region));

    // Markings: one mesh per colour id, lifted kMarkingLift above the surface.
    std::map<std::string, cp::WorldMesh> mark_meshes;
    {
        auto rules = cp::load_marking_rules_by_id(data, region->id);
        if (!rules) {
            std::fprintf(stderr, "export_mesh: markings skipped: %s\n", rules.error.c_str());
        } else {
            const cp::MarkingSet ms = cp::generate_markings(*graph, *region, *rules, js, mark_params).merged();
            for (const auto& w : ms.warnings) std::fprintf(stderr, "export_mesh: marking warning: %s\n", w.c_str());
            for (const auto& m : ms.marks) {
                const auto tri = cp::triangulate_marking(ms, m);
                if (tri.empty()) {
                    std::fprintf(stderr, "export_mesh: marking skipped (no triangles): %s at (%.2f, %.2f)\n",
                                 cp::marking_category_name(m.category), ms.points(m)[0].x, ms.points(m)[0].y);
                    continue;
                }
                cp::WorldMesh& wm = mark_meshes[ms.colour_id(m)];
                const auto base = static_cast<std::uint32_t>(wm.positions.size());
                for (const auto& v : ms.points(m)) {
                    wm.positions.push_back({v.x, v.y, v.z + kMarkingLift});
                    wm.normals.push_back({0, 0, 1});
                    wm.uv.push_back({v.x, v.y});
                }
                for (const auto t : tri) wm.indices.push_back(base + t);
            }
        }
    }

    // Common origin: centre of the xy bounds of the surface meshes, rounded to whole metres.
    double lo[3] = {1e300, 1e300, 1e300}, hi[3] = {-1e300, -1e300, -1e300};
    for (const auto& [k, m] : all)
        for (const auto& v : m.positions) {
            const double c[3] = {v.x, v.y, v.z};
            for (int a = 0; a < 3; ++a) {
                lo[a] = std::min(lo[a], c[a]);
                hi[a] = std::max(hi[a], c[a]);
            }
        }
    if (lo[0] > hi[0]) {
        std::fprintf(stderr, "export_mesh: empty mesh\n");
        return 1;
    }
    const double ox = std::round((lo[0] + hi[0]) / 2), oy = std::round((lo[1] + hi[1]) / 2);

    std::error_code ec;
    std::filesystem::create_directories(out_dir, ec);
    std::string files_json;
    auto write_obj = [&](const std::string& file, const std::string& kind, const std::string& id,
                         const cp::WorldMesh& m) -> bool {
        const std::filesystem::path p = std::filesystem::path(out_dir) / file;
        std::FILE* f = std::fopen(p.string().c_str(), "wb");
        if (!f) {
            std::fprintf(stderr, "export_mesh: cannot write %s\n", p.string().c_str());
            return false;
        }
        std::fprintf(f, "# %s %s '%s': %zu vertices, %zu triangles (origin offset %.0f %.0f 0 subtracted)\no %s\n",
                     name.c_str(), kind.c_str(), id.c_str(), m.positions.size(), m.indices.size() / 3, ox, oy,
                     file.c_str());
        for (const auto& v : m.positions) std::fprintf(f, "v %.4f %.4f %.4f\n", v.x - ox, v.y - oy, v.z);
        for (const auto& t : m.uv) std::fprintf(f, "vt %.4f %.4f\n", t.x, t.y);
        for (const auto& n : m.normals) std::fprintf(f, "vn %.5f %.5f %.5f\n", n.x, n.y, n.z);
        for (std::size_t i = 0; i + 2 < m.indices.size(); i += 3) {
            const unsigned a = m.indices[i] + 1, b = m.indices[i + 1] + 1, c = m.indices[i + 2] + 1;
            std::fprintf(f, "f %u/%u/%u %u/%u/%u %u/%u/%u\n", a, a, a, b, b, b, c, c, c);
        }
        std::fclose(f);
        std::printf("%s: %zu vertices, %zu triangles\n", p.string().c_str(), m.positions.size(), m.indices.size() / 3);
        if (!files_json.empty()) files_json += ",\n";
        files_json += "    {\"file\": " + cp::tools::json_string(file) + ", \"kind\": " + cp::tools::json_string(kind) +
                      ", \"id\": " + cp::tools::json_string(id) + ", \"triangles\": " +
                      std::to_string(m.indices.size() / 3) + "}";
        return true;
    };
    for (const auto& [surface, m] : all)
        if (!write_obj(name + "_" + surface + ".obj", "surface", surface, m)) return 1;
    for (const auto& [colour, m] : mark_meshes)
        if (!write_obj("markings_" + colour + ".obj", "marking", colour, m)) return 1;
    for (const auto& [surface, m] : structs.meshes)
        if (!write_obj(name + "_structure_" + surface + ".obj", "structure", surface, m)) return 1;

    auto num = [](double v) { return cp::tools::json_number(v, 3); };
    std::string nodes_json;
    for (const auto& [id, n] : graph->nodes()) {
        if (!nodes_json.empty()) nodes_json += ", ";
        nodes_json += "[" + num(n.pos.x - ox) + ", " + num(n.pos.y - oy) + ", " + num(n.pos.z) + "]";
    }
    std::string cameras_json;
    for (const auto& c : cameras) {
        if (!cameras_json.empty()) cameras_json += ",\n";
        cameras_json += "    {\"name\": " + cp::tools::json_string(c.name) + ", \"pos\": [" + num(c.pos.x) + ", " + num(c.pos.y) + ", " +
                        num(c.pos.z) + "], \"target\": [" + num(c.target.x) + ", " + num(c.target.y) + ", " + num(c.target.z) +
                        "], \"fov_deg\": " + num(c.fov_deg) + ", \"aspect\": " + num(c.aspect) +
                        ", \"photo\": " + cp::tools::json_string(c.photo) + "}";
    }
    const auto flag = [](bool b) { return b ? "true" : "false"; };
    std::string structures_json = "{\n    \"bridges\": [";
    for (std::size_t i = 0; i < structs.bridges.size(); ++i) {
        const cp::BridgeInfo& b = structs.bridges[i];
        std::string piers;
        for (const double s : b.pier_s) piers += (piers.empty() ? "" : ", ") + num(s);
        structures_json += std::string(i ? ",\n      " : "\n      ") + "{\"edge\": " + std::to_string(b.edge.value) +
                           ", \"s0\": " + num(b.s0) + ", \"s1\": " + num(b.s1) + ", \"solid_block\": " + flag(b.solid_block) +
                           ", \"crosses_road\": " + flag(b.crosses_road) + ", \"over_water\": " + flag(b.over_water) +
                           ", \"pier_in_water\": " + flag(b.pier_in_water) + ", \"span_too_long\": " + flag(b.span_too_long) +
                           ", \"longest_span\": " + num(b.longest_span) + ", \"pier_s\": [" + piers + "]}";
    }
    structures_json += "\n    ],\n    \"tunnels\": [";
    for (std::size_t i = 0; i < structs.tunnels.size(); ++i) {
        const cp::TunnelBore& t = structs.tunnels[i];
        structures_json += std::string(i ? ",\n      " : "\n      ") + "{\"edge\": " + std::to_string(t.edge.value) +
                           ", \"s0\": " + num(t.s0) + ", \"s1\": " + num(t.s1) + ", \"W\": " + num(t.section.W) +
                           ", \"Hs\": " + num(t.section.Hs) + ", \"Hc\": " + num(t.section.Hc) + ", \"R\": " + num(t.section.R) +
                           ", \"zc\": " + num(t.section.zc) + ", \"stations\": " + std::to_string(t.stations.size()) + "}";
    }
    structures_json += "\n    ],\n    \"instances\": [";
    for (std::size_t i = 0; i < structs.instances.size(); ++i) {
        const cp::InstanceList& l = structs.instances[i];
        structures_json += std::string(i ? ",\n      " : "\n      ") + "{\"kind\": " + cp::tools::json_string(l.kind) +
                           ", \"edge\": " + std::to_string(l.edge.value) + ", \"items\": [";
        for (std::size_t k = 0; k < l.items.size(); ++k) {
            const cp::InstanceTransform& t = l.items[k];
            structures_json += std::string(k ? ", " : "") + "[" + num(t.position.x - ox) + ", " + num(t.position.y - oy) + ", " +
                               num(t.position.z) + ", " + num(t.yaw) + ", " + num(t.pitch) + ", " + num(t.roll) + "]";
        }
        structures_json += "]}";
    }
    structures_json += "\n    ]\n  }";
    const std::filesystem::path jp = std::filesystem::path(out_dir) / "export.json";
    std::FILE* f = std::fopen(jp.string().c_str(), "wb");
    if (!f) {
        std::fprintf(stderr, "export_mesh: cannot write %s\n", jp.string().c_str());
        return 1;
    }
    const std::string side = region->driving_side == cp::DrivingSide::Left ? "left" : "right";
    std::fprintf(f,
                 "{\n  \"fixture\": %s,\n  \"region\": %s,\n  \"driving_side\": %s,\n"
                 "  \"origin_offset\": [%s, %s, 0],\n  \"marking_lift_m\": %s,\n"
                 "  \"bounds_min\": [%s, %s, %s],\n  \"bounds_max\": [%s, %s, %s],\n"
                 "  \"nodes\": [%s],\n  \"cameras\": [\n%s\n  ],\n  \"files\": [\n%s\n  ]\n}\n",
                 cp::tools::json_string(name).c_str(), cp::tools::json_string(region->id).c_str(),
                 cp::tools::json_string(side).c_str(), num(ox).c_str(), num(oy).c_str(), num(kMarkingLift).c_str(),
                 num(lo[0] - ox).c_str(), num(lo[1] - oy).c_str(), num(lo[2]).c_str(), num(hi[0] - ox).c_str(),
                 num(hi[1] - oy).c_str(), num(hi[2]).c_str(), nodes_json.c_str(), structures_json.c_str(), cameras_json.c_str(), files_json.c_str());
    std::fclose(f);
    return 0;
}
