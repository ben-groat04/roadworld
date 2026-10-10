// Adds graded terrain, retaining walls and terrain-aware structures to an export_mesh directory.
//   export_graded [--data <dir>] --terrain <name> [--step <m>] [--margin <m>] --out <export_mesh dir> (--layout <file> | <fixture>)
// Run export_mesh first with the same layout/fixture into the same --out. This tool then (1) grades a natural terrain
// (analytic, selected by --terrain) around the roads with cp::grading, (2) rebuilds the structures on the GRADED ground
// including retaining walls (export_mesh builds them on flat ground at z = 0 with no walls), (3) writes
// <name>_terrain.obj (terrain cells inside a tunnel bore are dropped) and overwrites <name>_structure_*.obj, and
// (4) adds those files to export.json (kind "terrain" tells mesh-view.js to skip its flat grass plane).
// Build: see docs/progress/2026-10-10-structures/INDEX.md.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "cp/grading.h"
#include "cp/layout_io.h"
#include "cp/render_mesh.h"
#include "cp/structure_mesh.h"
#include "scene_fixture.h"

namespace {
using namespace cp;

// ------------------------------------------------------------------ natural terrains (sim metres)
double hill(double x, double y, double cx, double cy, double h, double rx, double ry) {
    const double dx = (x - cx) / rx, dy = (y - cy) / ry;
    return h * std::exp(-(dx * dx + dy * dy));
}

GroundFn make_terrain(const std::string& n) {
    if (n == "flat") return [](double, double) { return 0.0; };
    // single hill centred on the origin, 38 m high: the tunnel scenario
    if (n == "hill") return [](double x, double y) { return hill(x, y, 0, 0, 38, 70, 90); };
    // 10 m ridge across a road running east-west: a cutting at x = 0
    if (n == "ridge") return [](double x, double) { return 10.0 * std::exp(-(x / 45.0) * (x / 45.0)); };
    // steep side-hill rising to the north: walls on both sides of a contour road
    if (n == "sidehill") return [](double, double y) { return 0.85 * y + 2.0; };
    // moderate side-hill rising to the north: two stacked roads
    if (n == "terrace") return [](double, double y) { return 0.45 * y + 2.0; };
    // shallow ditch (~12 m wide, 5 m deep) along y = 0 crossing a north-south road
    if (n == "ditch") return [](double, double y) { return -5.0 * std::exp(-(y / 6.0) * (y / 6.0)); };
    return nullptr;
}

// ------------------------------------------------------------------ graded ground
struct GradedGround final : GroundSampler {
    double x0 = 0, y0 = 0, res = 0.5;
    int nx = 0, ny = 0;
    std::vector<float> h;
    GroundFn natural;
    double at(int i, int j) const { return h[static_cast<std::size_t>(std::clamp(j, 0, ny - 1)) * nx + std::clamp(i, 0, nx - 1)]; }
    double height(double x, double y) const override {
        const double fx = (x - x0) / res, fy = (y - y0) / res;
        if (fx < 0 || fy < 0 || fx > nx - 1 || fy > ny - 1) return natural(x, y);
        const int i = static_cast<int>(std::floor(fx)), j = static_cast<int>(std::floor(fy));
        const double tx = fx - i, ty = fy - j;
        return (at(i, j) * (1 - tx) + at(i + 1, j) * tx) * (1 - ty) + (at(i, j + 1) * (1 - tx) + at(i + 1, j + 1) * tx) * ty;
    }
    double natural_height(double x, double y) const override { return natural(x, y); }
};

std::string read_all(const std::filesystem::path& p) {
    std::ifstream f(p, std::ios::binary);
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

std::string json_string_after(const std::string& s, const std::string& key) {
    const std::size_t k = s.find("\"" + key + "\"");
    if (k == std::string::npos) return {};
    const std::size_t a = s.find('"', s.find(':', k)) + 1, b = s.find('"', a);
    return s.substr(a, b - a);
}
}  // namespace

int main(int argc, char** argv) {
    std::string data = CP_DATA_DIR, out_dir, name, layout_file, terrain = "flat";
    double step = 1.0, margin = 60.0;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--data" && i + 1 < argc) data = argv[++i];
        else if (a == "--out" && i + 1 < argc) out_dir = argv[++i];
        else if (a == "--layout" && i + 1 < argc) layout_file = argv[++i];
        else if (a == "--terrain" && i + 1 < argc) terrain = argv[++i];
        else if (a == "--step" && i + 1 < argc) step = std::atof(argv[++i]);
        else if (a == "--margin" && i + 1 < argc) margin = std::atof(argv[++i]);
        else name = a;
    }
    if ((name.empty() == layout_file.empty()) || out_dir.empty()) {
        std::fprintf(stderr, "usage: export_graded [--data d] --terrain flat|hill|ridge|sidehill|terrace|ditch [--step m] [--margin m] --out <export_mesh dir> (--layout f | fixture)\n");
        return 2;
    }
    const GroundFn natural = make_terrain(terrain);
    if (!natural) {
        std::fprintf(stderr, "export_graded: unknown terrain '%s'\n", terrain.c_str());
        return 2;
    }
    std::string err;
    std::unique_ptr<tools::Scene> sc;
    std::unique_ptr<LoadedLayout> lay;
    const RoadGraph* graph = nullptr;
    const RegionPack* region = nullptr;
    if (!layout_file.empty()) {
        lay = load_layout_file(layout_file, data, err);
        if (!lay) { std::fprintf(stderr, "export_graded: %s\n", err.c_str()); return 1; }
        graph = &lay->graph;
        region = lay->region.get();
    } else {
        sc = tools::build_scene(name, data, err);
        if (!sc) { std::fprintf(stderr, "export_graded: %s\n", err.c_str()); return 1; }
        graph = &sc->graph;
        region = sc->region.get();
    }

    // origin offset and fixture name written by export_mesh
    const std::filesystem::path ej = std::filesystem::path(out_dir) / "export.json";
    std::string json = read_all(ej);
    if (json.empty()) { std::fprintf(stderr, "export_graded: %s missing: run export_mesh first\n", ej.string().c_str()); return 1; }
    const std::string fixture = json_string_after(json, "fixture");
    double ox = 0, oy = 0;
    {
        const std::size_t k = json.find("\"origin_offset\"");
        std::sscanf(json.c_str() + json.find('[', k), "[%lf, %lf", &ox, &oy);
    }

    // ---- grading
    const GradingParams p = GradingParams::from_rules(region->grading);
    std::vector<SampledRoad> roads = sample_graph_for_grading(*graph, p);
    if (roads.empty()) { std::fprintf(stderr, "export_graded: no roads\n"); return 1; }
    for (auto& r : roads) classify_road(r, natural, p, StructureMode::Authored);
    const std::vector<GradingRoad> prepared = prepare_grading_roads(roads, natural, p);

    double bx0 = 1e300, by0 = 1e300, bx1 = -1e300, by1 = -1e300;
    for (const auto& r : roads)
        for (const auto& s : r.samples) { bx0 = std::min(bx0, s.x); by0 = std::min(by0, s.y); bx1 = std::max(bx1, s.x); by1 = std::max(by1, s.y); }
    bx0 = std::floor((bx0 - margin) / 0.5) * 0.5; by0 = std::floor((by0 - margin) / 0.5) * 0.5;
    bx1 += margin; by1 += margin;
    auto gg = std::make_unique<GradedGround>();
    gg->natural = natural;
    gg->x0 = bx0; gg->y0 = by0; gg->res = 0.5;
    gg->nx = static_cast<int>(std::ceil((bx1 - bx0) / 0.5)) + 1;
    gg->ny = static_cast<int>(std::ceil((by1 - by0) / 0.5)) + 1;
    const TileSpec t{bx0, by0, gg->nx, gg->ny, 0.5};
    std::vector<float> nat = sample_natural_tile(natural, t);
    gg->h.assign(nat.size(), 0.f);
    grade_tile_into(prepared, p, t, nat.data(), gg->h.data(), nullptr);
    std::printf("graded %d x %d texels (%.0f x %.0f m), %zu roads\n", gg->nx, gg->ny, gg->nx * 0.5, gg->ny * 0.5, roads.size());

    // ---- walls + structures on the graded ground
    std::vector<StructureWallRun> walls;
    for (const auto& r : roads)
        for (const WallRun& w : wall_runs(r, natural, p)) {
            StructureWallRun o;
            o.side = w.side; o.cut = w.cut; o.lo = w.lo; o.hi = w.hi;
            for (const WallPoint& q : w.pts) o.pts.push_back({q.x, q.y, q.zTop, q.zBot, q.top, q.h});
            walls.push_back(std::move(o));
        }
    std::printf("wall runs: %zu\n", walls.size());
    const StructureMeshes st = build_structures(*graph, *region, *gg, structure_params_for(*region), &walls);

    // ---- tunnel bores (terrain discard)
    const std::vector<TunnelVolume> vols = tunnel_volumes(roads, p);
    const int rows_max = 256, tw = tunnel_table_width(p);
    std::vector<float> table(static_cast<std::size_t>(rows_max) * tw * 4, 0.f);
    const int rows = vols.empty() ? 0 : pack_tunnels(table, rows_max, vols, p);

    auto write = [&](const std::string& file, const WorldMesh& m) {
        const std::filesystem::path pp = std::filesystem::path(out_dir) / file;
        std::FILE* f = std::fopen(pp.string().c_str(), "wb");
        if (!f) return false;
        std::fprintf(f, "o %s\n", file.c_str());
        for (const auto& v : m.positions) std::fprintf(f, "v %.4f %.4f %.4f\n", v.x - ox, v.y - oy, v.z);
        for (const auto& u : m.uv) std::fprintf(f, "vt %.4f %.4f\n", u.x, u.y);
        for (const auto& n : m.normals) std::fprintf(f, "vn %.4f %.4f %.4f\n", n.x, n.y, n.z);
        for (std::size_t i = 0; i + 2 < m.indices.size(); i += 3) {
            const unsigned a = m.indices[i] + 1, b = m.indices[i + 1] + 1, c = m.indices[i + 2] + 1;
            std::fprintf(f, "f %u/%u/%u %u/%u/%u %u/%u/%u\n", a, a, a, b, b, b, c, c, c);
        }
        std::fclose(f);
        std::printf("%s: %zu vertices, %zu triangles\n", pp.string().c_str(), m.positions.size(), m.indices.size() / 3);
        return true;
    };

    // terrain mesh on a `step` grid (step is rounded to a multiple of the 0.5 m texel)
    const int stride = std::max(1, static_cast<int>(std::lround(step / 0.5)));
    const int cx = (gg->nx - 1) / stride + 1, cy = (gg->ny - 1) / stride + 1;
    WorldMesh tm;
    std::vector<char> inbore(static_cast<std::size_t>(cx) * cy, 0);
    for (int j = 0; j < cy; ++j)
        for (int i = 0; i < cx; ++i) {
            const double x = gg->x0 + i * stride * 0.5, y = gg->y0 + j * stride * 0.5, z = gg->at(i * stride, j * stride);
            tm.positions.push_back({x, y, z});
            const double e = stride * 0.5;
            const double dzx = (gg->height(x + e, y) - gg->height(x - e, y)) / (2 * e), dzy = (gg->height(x, y + e) - gg->height(x, y - e)) / (2 * e);
            const double l = std::sqrt(dzx * dzx + dzy * dzy + 1);
            tm.normals.push_back({-dzx / l, -dzy / l, 1 / l});
            tm.uv.push_back({x, y});
            if (rows > 0 && in_bore(table.data(), tw, rows, x, y, z, p)) inbore[static_cast<std::size_t>(j) * cx + i] = 1;
        }
    for (int j = 0; j + 1 < cy; ++j)
        for (int i = 0; i + 1 < cx; ++i) {
            const std::uint32_t a = static_cast<std::uint32_t>(j * cx + i), b = a + 1, c = a + static_cast<std::uint32_t>(cx), d = c + 1;
            if (inbore[a] || inbore[b] || inbore[c] || inbore[d]) continue;
            for (const std::uint32_t v : {a, b, d, a, d, c}) tm.indices.push_back(v);
        }
    const std::string tfile = fixture + "_terrain.obj";
    if (!write(tfile, tm)) return 1;
    // remove the flat-ground structure files of the export_mesh run, then write the graded ones
    for (const auto& e : std::filesystem::directory_iterator(out_dir)) {
        const std::string fn = e.path().filename().string();
        if (fn.rfind(fixture + "_structure_", 0) == 0) { std::error_code ec; std::filesystem::remove(e.path(), ec); }
    }
    for (const auto& [surface, m] : st.meshes)
        if (!write(fixture + "_structure_" + surface + ".obj", m)) return 1;

    // Rewrite export.json from the directory: export_mesh's own export.json is malformed (its printf passes the
    // structures JSON where the cameras go and the cameras where the file list goes, so "files" is empty/garbage).
    {
        std::string files;
        auto count_tris = [](const std::filesystem::path& pth) {
            std::ifstream f(pth, std::ios::binary);
            std::string l;
            std::size_t n = 0;
            while (std::getline(f, l)) n += l.rfind("f ", 0) == 0;
            return n;
        };
        std::vector<std::string> names;
        for (const auto& e : std::filesystem::directory_iterator(out_dir))
            if (e.path().extension() == ".obj") names.push_back(e.path().filename().string());
        std::sort(names.begin(), names.end());
        for (const std::string& fn : names) {
            std::string kind = "surface", id;
            if (fn.rfind("markings_", 0) == 0) { kind = "marking"; id = fn.substr(9, fn.size() - 13); }
            else if (fn.rfind(fixture + "_structure_", 0) == 0) { kind = "structure"; id = fn.substr(fixture.size() + 11, fn.size() - fixture.size() - 15); }
            else if (fn == tfile) { kind = "terrain"; id = "grass"; }
            else id = fn.substr(fixture.size() + 1, fn.size() - fixture.size() - 5);
            if (!files.empty()) files += ",\n";
            files += "    {\"file\": \"" + fn + "\", \"kind\": \"" + kind + "\", \"id\": \"" + id + "\", \"triangles\": " + std::to_string(count_tris(std::filesystem::path(out_dir) / fn)) + "}";
        }
        auto triple = [&](const char* key, double* v) {
            const std::size_t k = json.find(std::string("\"") + key + "\"");
            std::sscanf(json.c_str() + json.find('[', k), "[%lf, %lf, %lf", v, v + 1, v + 2);
        };
        double lo[3] = {0, 0, 0}, hi[3] = {0, 0, 0};
        triple("bounds_min", lo);
        triple("bounds_max", hi);
        lo[2] = std::min(lo[2], static_cast<double>(*std::min_element(gg->h.begin(), gg->h.end())));
        char head[1024];
        std::snprintf(head, sizeof head,
                      "{\n  \"fixture\": \"%s\",\n  \"origin_offset\": [%.3f, %.3f, 0],\n  \"bounds_min\": [%.3f, %.3f, %.3f],\n"
                      "  \"bounds_max\": [%.3f, %.3f, %.3f],\n  \"cameras\": [],\n  \"files\": [\n",
                      fixture.c_str(), ox, oy, lo[0], lo[1], lo[2], hi[0], hi[1], hi[2]);
        std::ofstream f(ej, std::ios::binary);
        f << head << files << "\n  ]\n}\n";
    }
    std::printf("tunnels: %zu, bridges: %zu, structure elements: %zu\n", st.tunnels.size(), st.bridges.size(), st.elements.size());
    for (const auto& b : st.bridges)
        std::printf("bridge s %.1f..%.1f solid_block=%d crosses_road=%d piers=%zu span_too_long=%d\n", b.s0, b.s1, b.solid_block, b.crosses_road, b.pier_s.size(), b.span_too_long);
    return 0;
}
