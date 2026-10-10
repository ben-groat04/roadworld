// Writes the render meshes of a fixture to Wavefront .obj files, one per surface class, for
// inspection in Blender. Coordinates are the sim frame (x east, y north, z up): import with
// "Forward: Y, Up: Z". Usage: export_mesh [--data <dir>] [--tol <metres>] --out <dir> <fixture>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>

#include "cp/render_mesh.h"
#include "scene_fixture.h"

int main(int argc, char** argv) {
    std::string data = CP_DATA_DIR, out_dir, name;
    cp::RenderMeshParams params;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--data" && i + 1 < argc) data = argv[++i];
        else if (a == "--out" && i + 1 < argc) out_dir = argv[++i];
        else if (a == "--tol" && i + 1 < argc) params.chord_tolerance = std::atof(argv[++i]);
        else name = a;
    }
    if (name.empty() || out_dir.empty()) {
        std::fprintf(stderr, "usage: export_mesh [--data <dir>] [--tol <m>] --out <dir> <fixture>   fixtures:");
        for (const auto& n : cp::tools::scene_names()) std::fprintf(stderr, " %s", n.c_str());
        std::fprintf(stderr, "\n");
        return 2;
    }
    std::string err;
    auto sc = cp::tools::build_scene(name, data, err);
    if (!sc) {
        std::fprintf(stderr, "export_mesh: %s\n", err.c_str());
        return 1;
    }
    cp::RoundaboutResult rr;
    const cp::JunctionSet js = cp::tools::scene_junctions(*sc, &rr);
    cp::WorldMeshSet all;
    for (const auto& [id, e] : sc->graph.edges())
        cp::append_mesh(all, cp::build_edge_mesh(sc->graph, *sc->region, id, js, params));
    for (const auto& [n, j] : js.junctions)
        cp::append_mesh(all, cp::build_junction_mesh(sc->graph, *sc->region, j, params));
    for (const auto& l : rr.layouts)
        cp::append_mesh(all, cp::build_roundabout_mesh(sc->graph, *sc->region, l, params));

    std::error_code ec;
    std::filesystem::create_directories(out_dir, ec);
    for (const auto& [surface, m] : all) {
        const std::filesystem::path p = std::filesystem::path(out_dir) / (name + "_" + surface + ".obj");
        std::FILE* f = std::fopen(p.string().c_str(), "wb");
        if (!f) {
            std::fprintf(stderr, "export_mesh: cannot write %s\n", p.string().c_str());
            return 1;
        }
        std::fprintf(f, "# %s surface '%s': %zu vertices, %zu triangles\no %s_%s\n", name.c_str(), surface.c_str(),
                     m.positions.size(), m.indices.size() / 3, name.c_str(), surface.c_str());
        for (const auto& v : m.positions) std::fprintf(f, "v %.4f %.4f %.4f\n", v.x, v.y, v.z);
        for (const auto& t : m.uv) std::fprintf(f, "vt %.4f %.4f\n", t.x, t.y);
        for (const auto& n : m.normals) std::fprintf(f, "vn %.5f %.5f %.5f\n", n.x, n.y, n.z);
        for (std::size_t i = 0; i + 2 < m.indices.size(); i += 3) {
            const unsigned a = m.indices[i] + 1, b = m.indices[i + 1] + 1, c = m.indices[i + 2] + 1;
            std::fprintf(f, "f %u/%u/%u %u/%u/%u %u/%u/%u\n", a, a, a, b, b, b, c, c, c);
        }
        std::fclose(f);
        std::printf("%s: %zu vertices, %zu triangles\n", p.string().c_str(), m.positions.size(), m.indices.size() / 3);
    }
    return 0;
}
