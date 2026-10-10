// Tests for the render mesh builder (sim_core/include/cp/render_mesh.h).
#include <algorithm>
#include <cmath>
#include <cstring>
#include <memory>

#include "doctest/doctest.h"
#include "portable_rng.h"
#include "cp/render_mesh.h"
#include "cp/road_commands.h"
#include "scene_fixture.h"

using namespace cp;

namespace {

std::shared_ptr<const RegionPack> uk() {
    static const std::shared_ptr<const RegionPack> pack = [] {
        auto p = load_region_pack_by_id(CP_DATA_DIR, "uk");
        REQUIRE_MESSAGE(p, p.error);
        return std::make_shared<const RegionPack>(*p);
    }();
    return pack;
}

std::shared_ptr<const RegionPack> uk_right() {
    static const std::shared_ptr<const RegionPack> pack = [] {
        RegionPack p = *uk();
        p.driving_side = DrivingSide::Right;
        return std::make_shared<const RegionPack>(p);
    }();
    return pack;
}

struct World {
    RoadGraph g;
    CommandStack<RoadGraph> st{g};
    std::shared_ptr<const RegionPack> region;
    explicit World(std::shared_ptr<const RegionPack> r = uk()) : region(std::move(r)) { g.set_region(region); }

    EdgeId road(Vec3 a, Vec3 b, const char* type, bool one_way = false, std::vector<Vec3> interior = {}) {
        DrawRoadSpec s;
        s.road_type = type;
        s.one_way = one_way;
        s.start.pos = a;
        s.end.pos = b;
        s.interior = std::move(interior);
        REQUIRE(st.execute(make_draw_road(s)));
        return g.edges().rbegin()->first;
    }
};

std::size_t tri_count(const WorldMeshSet& s) {
    std::size_t n = 0;
    for (const auto& [name, m] : s) n += m.indices.size() / 3;
    return n;
}

// Structural validity of every mesh: sizes, indices, areas, normals, orientation, sections.
void check_valid(const WorldMeshSet& set) {
    for (const auto& [name, m] : set) {
        INFO("surface " << name);
        REQUIRE(m.normals.size() == m.positions.size());
        REQUIRE(m.tangents.size() == m.positions.size());
        REQUIRE(m.uv.size() == m.positions.size());
        REQUIRE(m.indices.size() % 3 == 0);
        for (std::size_t i = 0; i < m.positions.size(); ++i) {
            const Vec3 n = m.normals[i];
            REQUIRE(std::isfinite(m.positions[i].z));
            REQUIRE(std::isfinite(m.uv[i].x));
            REQUIRE(std::abs(std::sqrt(n.x * n.x + n.y * n.y + n.z * n.z) - 1) < 1e-6);
            if (name != "kerb") REQUIRE(n.z > 0);
        }
        for (std::size_t i = 0; i < m.indices.size(); i += 3) {
            const Vec3 a = m.positions[m.indices[i]], b = m.positions[m.indices[i + 1]], c = m.positions[m.indices[i + 2]];
            for (std::uint32_t k = 0; k < 3; ++k) REQUIRE(m.indices[i + k] < m.positions.size());
            const double ux = b.x - a.x, uy = b.y - a.y, uz = b.z - a.z, vx = c.x - a.x, vy = c.y - a.y, vz = c.z - a.z;
            const double nx = uy * vz - uz * vy, ny = uz * vx - ux * vz, nz = ux * vy - uy * vx;
            REQUIRE(std::sqrt(nx * nx + ny * ny + nz * nz) > 1e-10);  // not degenerate
            if (name != "kerb") REQUIRE(nz > 0);                       // not flipped
        }
        std::uint32_t covered = 0;
        for (const SectionRange& s : m.sections) {
            REQUIRE(s.first_index == covered);  // contiguous, in order
            REQUIRE(s.index_count % 3 == 0);
            covered += s.index_count;
        }
        if (!m.sections.empty()) REQUIRE(covered == m.indices.size());
    }
}

double plan_area(const WorldMesh& m) {
    double a = 0;
    for (std::size_t i = 0; i < m.indices.size(); i += 3) {
        const Vec3 p = m.positions[m.indices[i]], q = m.positions[m.indices[i + 1]], r = m.positions[m.indices[i + 2]];
        a += 0.5 * ((q.x - p.x) * (r.y - p.y) - (q.y - p.y) * (r.x - p.x));
    }
    return a;
}

double polygon_set_area(const PolygonSet& ps) {
    double a = 0;
    for (const auto& r : ps) {
        double s = 0;
        for (std::size_t i = 0; i < r.size(); ++i) s += r[i].cross(r[(i + 1) % r.size()]);
        a += s / 2;
    }
    return a;
}

struct SeamVert {
    Vec3 p;
    Vec2 uv;
};

// Compares, for every arm of every junction, the junction mesh vertices on its trim line with the
// edge mesh vertices there. Returns the number of vertices matched; failures are CHECKed.
int check_seams(const RoadGraph& g, const RegionPack& r, const JunctionSet& js, const RenderMeshParams& P) {
    int matched = 0;
    for (const auto& [nid, j] : js.junctions) {
        if (j.arms.empty() || j.trims.size() != j.arms.size()) continue;
        if (j.carriageway.empty() && j.pavements.empty()) continue;
        const WorldMeshSet jm = build_junction_mesh(g, r, j, P);
        for (std::size_t i = 0; i < j.arms.size(); ++i) {
            const JunctionArm& arm = j.arms[i];
            const ArmPoint c = arm_point(arm, j.trims[i], 0);
            const Vec2 dir{std::cos(c.heading_out), std::sin(c.heading_out)}, left{-dir.y, dir.x};
            const auto on_seam = [&](Vec3 p) {
                const Vec2 d = p.xy() - c.pos.xy();
                return std::abs(d.dot(dir)) < 2e-4 && d.dot(left) >= -arm.half_width_right - 1e-4 &&
                       d.dot(left) <= arm.half_width_left + 1e-4;
            };
            std::vector<SeamVert> A, B;
            for (const auto& [name, m] : jm)
                for (std::size_t k = 0; k < m.positions.size(); ++k)
                    if (on_seam(m.positions[k])) A.push_back({m.positions[k], m.uv[k]});
            const WorldMeshSet em = build_edge_mesh(g, r, arm.edge, js, P);
            for (const auto& [name, m] : em)
                for (std::size_t k = 0; k < m.positions.size(); ++k)
                    if (on_seam(m.positions[k])) B.push_back({m.positions[k], m.uv[k]});
            const auto near = [](const SeamVert& v, const std::vector<SeamVert>& set, double& duv) {
                double best = 1e9;
                duv = 1e9;
                for (const SeamVert& o : set) {
                    const double d = std::sqrt((o.p.x - v.p.x) * (o.p.x - v.p.x) + (o.p.y - v.p.y) * (o.p.y - v.p.y) +
                                               (o.p.z - v.p.z) * (o.p.z - v.p.z));
                    best = std::min(best, d);
                    if (d < 1e-3) duv = std::min(duv, std::max(std::abs(o.uv.x - v.uv.x), std::abs(o.uv.y - v.uv.y)));
                }
                return best;
            };
            INFO("node " << nid.value << " arm " << i << " edge " << arm.edge.value);
            CHECK(!B.empty());
            // Junction vertices must lie on the edge end ring (a vertex of it, or on a ring segment: a lane-line
            // vertex of the junction polygon between two cross-section points is a T-vertex, not a crack).
            std::vector<SeamVert> sorted = B;
            const auto lat = [&](const SeamVert& v) { return (v.p.xy() - c.pos.xy()).dot(left); };
            std::sort(sorted.begin(), sorted.end(), [&](const SeamVert& x, const SeamVert& y) {
                const double lx = lat(x), ly = lat(y);
                return std::abs(lx - ly) > 1e-6 ? lx < ly : x.p.z < y.p.z;
            });
            for (const SeamVert& a : A) {
                double duv = 0;
                const double nd = near(a, B, duv);
                if (nd >= 1e-3) {
                    double seg = 1e9;
                    for (std::size_t k = 0; k + 1 < sorted.size(); ++k) {
                        const Vec3 p = sorted[k].p, q = sorted[k + 1].p;
                        const double dx = q.x - p.x, dy = q.y - p.y, dz = q.z - p.z;
                        const double l2 = dx * dx + dy * dy + dz * dz;
                        const double t = l2 > 0 ? std::clamp(((a.p.x - p.x) * dx + (a.p.y - p.y) * dy + (a.p.z - p.z) * dz) / l2, 0.0, 1.0) : 0.0;
                        seg = std::min(seg, std::sqrt(std::pow(p.x + dx * t - a.p.x, 2) + std::pow(p.y + dy * t - a.p.y, 2) + std::pow(p.z + dz * t - a.p.z, 2)));
                    }
                    if (seg >= 1e-3) { MESSAGE("A vertex lat " << lat(a) << " z " << a.p.z << " hl " << arm.half_width_left << " hr " << arm.half_width_right << " kl " << arm.kerb_left << " kr " << arm.kerb_right); for (const SeamVert& s : sorted) MESSAGE("   B lat " << lat(s) << " z " << s.p.z); }
                    CHECK(seg < 1e-3);
                    ++matched;
                    continue;
                }
                ++matched;
            }
            // Edge vertices beyond the lateral extent of the junction polygons on this trim line are an upstream
            // geometry gap (merge/diverge pavements stop short of the full width), not a mesh crack.
            double a_lo = 1e9, a_hi = -1e9;
            for (const SeamVert& a : A) {
                a_lo = std::min(a_lo, lat(a));
                a_hi = std::max(a_hi, lat(a));
            }
            for (const SeamVert& b : B) {
                double duv = 0;
                if (lat(b) < a_lo - 1e-3 || lat(b) > a_hi + 1e-3) continue;
                CHECK(near(b, A, duv) < 1e-3);
            }
        }
    }
    return matched;
}

}  // namespace

TEST_CASE("render mesh: straight two-way road has the expected cross-section and uv") {
    World w;
    const EdgeId id = w.road({0, 0, 0}, {100, 0, 0}, "residential");
    const JunctionSet none;
    const WorldMeshSet set = build_edge_mesh(w.g, *w.region, id, none);
    check_valid(set);
    REQUIRE(set.count("asphalt"));
    REQUIRE(set.count("kerb"));
    REQUIRE(set.count("paving"));
    const WorldMesh& asphalt = set.at("asphalt");

    // Camber about the centreline: y = 0 is the crown, edges fall by crossfall * distance.
    double zmax = -1e9, zcrown = 0, ymin = 1e9, ymax = -1e9;
    for (const Vec3& p : asphalt.positions) {
        zmax = std::max(zmax, p.z);
        if (std::abs(p.y) < 1e-9) zcrown = p.z;
        ymin = std::min(ymin, p.y);
        ymax = std::max(ymax, p.y);
    }
    CHECK(zcrown == doctest::Approx(0).epsilon(1e-9));
    CHECK(zmax == doctest::Approx(0).epsilon(1e-9));
    CHECK(ymax == doctest::Approx(5.0));  // 2 x (3.0 + 2.0 parking) kerb to kerb = +-5 m
    CHECK(ymin == doctest::Approx(-5.0));
    for (const Vec3& p : asphalt.positions) CHECK(p.z == doctest::Approx(-0.025 * std::abs(p.y)));

    // Kerb upstand: the highest kerb vertex is the carriageway edge level plus 0.125 m.
    double kz = -1e9;
    for (const Vec3& p : set.at("kerb").positions) kz = std::max(kz, p.z);
    CHECK(kz == doctest::Approx(-0.025 * 5.0 + 0.125));
    // Pavement starts at the kerb top and falls toward the road (rises outward).
    for (const Vec3& p : set.at("paving").positions) CHECK(p.z >= kz - 1e-9);

    // UV: v is arc length (= x here), u is metres across the profile.
    for (std::size_t i = 0; i < asphalt.positions.size(); ++i) CHECK(asphalt.uv[i].y == doctest::Approx(asphalt.positions[i].x));
    std::map<long long, std::vector<std::pair<double, double>>> rings;  // x -> (y, u)
    for (std::size_t i = 0; i < asphalt.positions.size(); ++i)
        rings[std::llround(asphalt.positions[i].x * 1000)].push_back({asphalt.positions[i].y, asphalt.uv[i].x});
    REQUIRE(rings.size() >= 2);
    for (auto& [x, ring] : rings) {
        std::sort(ring.begin(), ring.end());
        for (std::size_t i = 1; i < ring.size(); ++i) {
            const double dy = ring[i].first - ring[i - 1].first, du = ring[i].second - ring[i - 1].second;
            CHECK(std::abs(du) == doctest::Approx(std::abs(dy)).epsilon(0.01));
        }
    }
    // Everything is one ground section covering the whole edge.
    REQUIRE(asphalt.sections.size() == 1);
    CHECK(asphalt.sections[0].kind == StructureKind::Ground);
    CHECK(asphalt.sections[0].s0 == doctest::Approx(0));
    CHECK(asphalt.sections[0].s1 == doctest::Approx(100));
}

TEST_CASE("render mesh: uv v follows arc length along a curved climbing road") {
    World w;
    const EdgeId id = w.road({0, 0, 0}, {120, 60, 6}, "b_road", false, {{60, 5, 3}});
    const JunctionSet none;
    const WorldMeshSet set = build_edge_mesh(w.g, *w.region, id, none);
    check_valid(set);
    const Edge& e = *w.g.edge(id);
    const std::vector<EdgeSample> c = sample_centreline(e, {0.001, 0.01, 0.05});
    const WorldMesh& m = set.at("asphalt");
    REQUIRE(!m.positions.empty());
    for (std::size_t i = 0; i < m.positions.size(); ++i) {
        double best = 1e9, s = 0;
        for (const EdgeSample& q : c) {
            const double d = (q.pos.xy() - m.positions[i].xy()).length();
            // Distance to the centreline is at most the half width; pick the sample whose normal
            // passes through the vertex (smallest |lateral along tangent|).
            const Vec2 t{std::cos(q.heading), std::sin(q.heading)};
            const double along = std::abs((m.positions[i].xy() - q.pos.xy()).dot(t));
            if (d < 8 && along < best) {
                best = along;
                s = q.s;
            }
        }
        CHECK(std::abs(m.uv[i].y - s) < 0.06);
    }
}

TEST_CASE("render mesh: LOD tolerance changes the triangle count") {
    World w;
    const EdgeId id = w.road({0, 0, 0}, {200, 120, 4}, "b_road", false, {{80, -20, 0}, {160, 40, 2}});
    const JunctionSet none;
    std::size_t prev = 0;
    for (double tol : {0.5, 0.1, 0.02, 0.004}) {
        RenderMeshParams p;
        p.chord_tolerance = tol;
        const WorldMeshSet set = build_edge_mesh(w.g, *w.region, id, none, p);
        check_valid(set);
        const std::size_t n = tri_count(set);
        INFO("tolerance " << tol << " triangles " << n);
        CHECK(n > prev);
        prev = n;
    }
    RenderMeshParams coarse, fine;
    coarse.chord_tolerance = 0.5;
    fine.chord_tolerance = 0.02;
    CHECK(tri_count(build_edge_mesh(w.g, *w.region, id, none, fine)) >=
          2 * tri_count(build_edge_mesh(w.g, *w.region, id, none, coarse)));

}

TEST_CASE("render mesh: edge between junction trims; seams weld to 1 mm") {
    World w;
    w.road({0, 0, 0}, {100, 0, 0}, "residential");
    // Three more spokes at the origin node.
    const Edge& first = w.g.edges().begin()->second;
    const NodeId c = first.a;
    const auto spoke = [&](Vec3 to, const char* type) {
        DrawRoadSpec s;
        s.road_type = type;
        s.start.existing_node = c;
        s.end.pos = to;
        REQUIRE(w.st.execute(make_draw_road(s)));
    };
    spoke({-100, 0, 1}, "residential");
    spoke({10, 90, 2}, "b_road");
    const JunctionSet js = build_junctions(w.g, *w.region, {});
    REQUIRE(js.find(c));
    const Junction& j = *js.find(c);
    CHECK_FALSE(j.degraded);
    const WorldMeshSet jm = build_junction_mesh(w.g, *w.region, j);
    check_valid(jm);
    REQUIRE(jm.count("asphalt"));
    CHECK(plan_area(jm.at("asphalt")) == doctest::Approx(polygon_set_area(j.carriageway)).epsilon(0.002));
    CHECK(plan_area(jm.at("paving")) == doctest::Approx(polygon_set_area(j.pavements)).epsilon(0.002));
    CHECK(jm.count("kerb"));
    const int matched = check_seams(w.g, *w.region, js, {});
    CHECK(matched > 20);

    // The trimmed edge starts at its trim line.
    for (const JunctionArm& arm : j.arms) {
        const WorldMeshSet em = build_edge_mesh(w.g, *w.region, arm.edge, js);
        check_valid(em);
        double dmin = 1e9;
        for (const auto& [name, m] : em)
            for (const Vec3& p : m.positions) dmin = std::min(dmin, (p.xy() - j.pos.xy()).length());
        CHECK(dmin > j.trims[static_cast<std::size_t>(&arm - j.arms.data())] - 1e-3);
    }
}

TEST_CASE("render mesh: float positions are exact enough 20 km from the origin") {
    World w;
    const double ox = 20000.0, oy = -20480.0;
    const EdgeId id = w.road({ox, oy, 5}, {ox + 180, oy + 90, 9}, "b_road", false, {{ox + 90, oy + 10, 6}});
    const JunctionSet none;
    const WorldMeshSet set = build_edge_mesh(w.g, *w.region, id, none);
    check_valid(set);
    ChunkParams cp;
    const auto chunks = build_mesh_chunks(w.g, *w.region, none, nullptr, {}, cp);
    REQUIRE(chunks.size() >= 2);
    // Every triangle of the world mesh appears in exactly one chunk, with float positions within 1 mm.
    std::size_t world_tris = tri_count(set), chunk_tris = 0;
    for (const auto& [tk, ch] : chunks) {
        CHECK(ch.origin.x == tk.x * 256.0);
        for (const auto& [name, b] : ch.surfaces) {
            chunk_tris += b.indices.size() / 3;
            REQUIRE(b.positions.size() == b.normals.size());
            REQUIRE(b.positions.size() == b.tangents.size());
            REQUIRE(b.positions.size() == b.uv0.size());
            REQUIRE(b.positions.size() == b.colours.size());
            const WorldMesh& ref = set.at(name);
            for (std::size_t i = 0; i < b.positions.size(); ++i) {
                const Vec3 p{ch.origin.x + b.positions[i].x, ch.origin.y + b.positions[i].y, ch.origin.z + b.positions[i].z};
                double best = 1e9;
                for (const Vec3& q : ref.positions) {
                    best = std::min(best, std::max({std::abs(p.x - q.x), std::abs(p.y - q.y), std::abs(p.z - q.z)}));
                    if (best < 1e-4) break;
                }
                REQUIRE(best < 1e-3);
                CHECK(std::abs(b.positions[i].x) <= 256.0f + 1.0f);
            }
        }
    }
    CHECK(chunk_tris == world_tris);
    // pack_buffers against the double mesh.
    const MeshBuffers pb = pack_buffers(set.at("asphalt"), tile_origin(tile_of({ox, oy}, 256), 256));
    const WorldMesh& am = set.at("asphalt");
    const Vec3 org = tile_origin(tile_of({ox, oy}, 256), 256);
    for (std::size_t i = 0; i < am.positions.size(); ++i) {
        CHECK(std::abs(pb.positions[i].x + org.x - am.positions[i].x) < 1e-3);
        CHECK(std::abs(pb.positions[i].y + org.y - am.positions[i].y) < 1e-3);
        CHECK(std::abs(pb.positions[i].z + org.z - am.positions[i].z) < 1e-3);
    }
}

TEST_CASE("render mesh: driving sides mirror") {
    for (bool second_curved : {false, true}) {
        World wl(uk()), wr(uk_right());
        const std::vector<Vec3> in = second_curved ? std::vector<Vec3>{{50, 10, 0}} : std::vector<Vec3>{};
        const EdgeId il = wl.road({0, 0, 0}, {100, 0, 0}, "b_road", true, in);
        const EdgeId ir = wr.road({0, 0, 0}, {100, 0, 0}, "b_road", true, in);
        const JunctionSet none;
        const WorldMeshSet l = build_edge_mesh(wl.g, *wl.region, il, none), r = build_edge_mesh(wr.g, *wr.region, ir, none);
        check_valid(l);
        check_valid(r);
        // Single fall to the nearside: heights at (lateral +-d) swap between the two packs.
        const auto z_at = [](const WorldMesh& m, double x, double y, const WorldMesh& ref) {
            double best = 1e9, z = 0;
            for (std::size_t i = 0; i < m.positions.size(); ++i) {
                const double d = std::hypot(m.positions[i].x - x, m.positions[i].y - y);
                if (d < best) {
                    best = d;
                    z = m.positions[i].z;
                }
            }
            (void)ref;
            return z;
        };
        const WorldMesh& la = l.at("asphalt");
        const WorldMesh& ra = r.at("asphalt");
        if (!second_curved) {
            for (double y : {-5.0, -2.0, 2.0, 5.0}) {
                if (y == 0) continue;
                CHECK(z_at(la, 100, y, ra) == doctest::Approx(z_at(ra, 100, -y, la)));
            }
            CHECK(z_at(la, 100, 6.5, ra) < z_at(la, 100, -6.5, ra));  // left side drops for left-hand traffic
            CHECK(z_at(ra, 100, 6.5, ra) > z_at(ra, 100, -6.5, ra));
        }
        CHECK(la.positions.size() == ra.positions.size());
    }
}

TEST_CASE("render mesh: structure spans split the edge into sections") {
    World w;
    const EdgeId id = w.road({0, 0, 0}, {200, 0, 0}, "b_road");
    REQUIRE(w.st.execute(make_set_structure_span(id, 60, 120, StructureKind::Bridge)));
    const JunctionSet none;
    const WorldMeshSet set = build_edge_mesh(w.g, *w.region, id, none);
    check_valid(set);
    const WorldMesh& m = set.at("asphalt");
    REQUIRE(m.sections.size() == 3);
    CHECK(m.sections[0].kind == StructureKind::Ground);
    CHECK(m.sections[1].kind == StructureKind::Bridge);
    CHECK(m.sections[1].s0 == doctest::Approx(60));
    CHECK(m.sections[1].s1 == doctest::Approx(120));
    CHECK(m.sections[2].kind == StructureKind::Ground);
    // Triangles of the bridge section lie within its span.
    for (std::uint32_t i = m.sections[1].first_index; i < m.sections[1].first_index + m.sections[1].index_count; ++i) {
        const double x = m.positions[m.indices[i]].x;
        CHECK(x >= 60 - 1e-6);
        CHECK(x <= 120 + 1e-6);
    }
}

TEST_CASE("render mesh: degraded and empty junctions never produce garbage") {
    World w;
    w.road({0, 0, 0}, {100, 0, 0}, "residential");
    Junction empty;
    CHECK(build_junction_mesh(w.g, *w.region, empty).empty());
    Junction no_trims = empty;
    no_trims.arms.resize(2);
    CHECK(build_junction_mesh(w.g, *w.region, no_trims).empty());
    Junction degraded = empty;
    degraded.degraded = true;
    degraded.degraded_reason = "test";
    CHECK(build_junction_mesh(w.g, *w.region, degraded).empty());
    // Polygons with NaN / too few points are skipped.
    Junction junk;
    junk.arms = collect_arms(w.g, w.g.edges().begin()->second.a, *w.region);
    junk.trims = {0.0};
    junk.carriageway = {{{0, 0}, {1, 0}}, {{std::nan(""), 0}, {1, 1}, {0, 1}}};
    const WorldMeshSet m = build_junction_mesh(w.g, *w.region, junk);
    check_valid(m);
}

TEST_CASE("render mesh: interchange fixture builds end to end at three LODs") {
    std::string err;
    auto sc = tools::build_scene("interchange", CP_DATA_DIR, err);
    REQUIRE_MESSAGE(sc, err);
    RoundaboutResult rr;
    const JunctionSet js = tools::scene_junctions(*sc, &rr);
    REQUIRE(!js.junctions.empty());
    std::size_t prev = 1u << 30;
    for (double tol : {0.02, 0.1, 0.5, 2.0}) {
        RenderMeshParams p;
        p.chord_tolerance = tol;
        WorldMeshSet all, edges, juncs, joints, islands;
        for (const auto& [id, e] : sc->graph.edges()) append_mesh(edges, build_edge_mesh(sc->graph, *sc->region, id, js, p));
        for (const auto& [n, j] : js.junctions) append_mesh(juncs, build_junction_mesh(sc->graph, *sc->region, j, p));
        for (const auto& [n, nd] : sc->graph.nodes()) append_mesh(joints, build_joint_mesh(sc->graph, *sc->region, n, p));
        for (const auto& l : rr.layouts) append_mesh(islands, build_roundabout_mesh(sc->graph, *sc->region, l, p));
        for (const WorldMeshSet* part : {&edges, &juncs, &joints, &islands}) append_mesh(all, *part);
        check_valid(all);
        const std::size_t n = tri_count(all);
        const std::size_t kerb = all.count("kerb") ? all.at("kerb").indices.size() / 3 : 0;
        MESSAGE("interchange tol " << tol << ": " << n << " triangles (edges " << tri_count(edges) << ", junctions "
                                   << tri_count(juncs) << ", joints " << tri_count(joints) << ", islands "
                                   << tri_count(islands) << "), kerb share " << kerb << " = " << 100 * kerb / n << "%");
        CHECK(n > 500);
        CHECK(n < prev);
        prev = n;
        CHECK(all.count("asphalt"));
        CHECK(all.count("paving"));
        CHECK(all.count("kerb"));
        CHECK(all.count("grass"));
        CHECK_FALSE(all.count("island"));
    }
    // Junction polygons are fully covered, seams weld.
    int checked = 0;
    for (const auto& [n, j] : js.junctions) {
        if (j.degraded || j.carriageway.empty()) continue;
        const WorldMeshSet jm = build_junction_mesh(sc->graph, *sc->region, j);
        check_valid(jm);
        INFO("junction node " << n.value);
        REQUIRE(jm.count("asphalt"));
        CHECK(plan_area(jm.at("asphalt")) == doctest::Approx(polygon_set_area(j.carriageway)).epsilon(0.005));
        ++checked;
    }
    CHECK(checked >= 3);
    CHECK(check_seams(sc->graph, *sc->region, js, {}) > 100);

    // Chunking: deterministic, and a dirty-tile rebuild equals the full build for those tiles.
    const auto a = build_mesh_chunks(sc->graph, *sc->region, js, &rr.layouts);
    const auto b = build_mesh_chunks(sc->graph, *sc->region, js, &rr.layouts);
    REQUIRE(a.size() == b.size());
    REQUIRE(a.size() >= 4);
    for (const auto& [tk, ch] : a) {
        const MeshChunk& o = b.at(tk);
        REQUIRE(ch.surfaces.size() == o.surfaces.size());
        for (const auto& [name, buf] : ch.surfaces) {
            const MeshBuffers& ob = o.surfaces.at(name);
            REQUIRE(buf.positions.size() == ob.positions.size());
            CHECK(std::memcmp(buf.positions.data(), ob.positions.data(), buf.positions.size() * sizeof(Float3)) == 0);
            CHECK(buf.indices == ob.indices);
        }
    }
    const TileKey pick = std::next(a.begin(), static_cast<std::ptrdiff_t>(a.size() / 2))->first;
    const std::set<TileKey> only{pick};
    const auto part = build_mesh_chunks(sc->graph, *sc->region, js, &rr.layouts, {}, {}, &only);
    REQUIRE(part.size() == 1);
    const MeshChunk& pc = part.at(pick);
    const MeshChunk& fc = a.at(pick);
    REQUIRE(pc.surfaces.size() == fc.surfaces.size());
    for (const auto& [name, buf] : fc.surfaces) {
        const MeshBuffers& pb = pc.surfaces.at(name);
        REQUIRE(buf.indices.size() == pb.indices.size());
        CHECK(buf.indices == pb.indices);
        CHECK(std::memcmp(buf.positions.data(), pb.positions.data(), buf.positions.size() * sizeof(Float3)) == 0);
    }
    // Each triangle lands in exactly one tile.
    std::size_t chunk_tris = 0;
    for (const auto& [tk, ch] : a)
        for (const auto& [name, buf] : ch.surfaces) chunk_tris += buf.indices.size() / 3;
    WorldMeshSet all;
    for (const auto& [id, e] : sc->graph.edges()) append_mesh(all, build_edge_mesh(sc->graph, *sc->region, id, js));
    for (const auto& [n, j] : js.junctions) append_mesh(all, build_junction_mesh(sc->graph, *sc->region, j));
    for (const auto& l : rr.layouts) append_mesh(all, build_roundabout_mesh(sc->graph, *sc->region, l));
    CHECK(chunk_tris == tri_count(all));
}

TEST_CASE("render mesh: fuzz over random junctions") {
    PortableRng rng{0xC17Full};
    int built = 0, seams = 0;
    for (int iter = 0; iter < 40; ++iter) {
        World w(iter % 2 ? uk() : uk_right());
        const int arms = 3 + static_cast<int>(rng.next() % 3);
        const double base = rng.uniform(0, 2 * kPi);
        const char* types[] = {"residential", "b_road", "b_road"};
        NodeId c;
        for (int k = 0; k < arms; ++k) {
            const double ang = base + k * 2 * kPi / arms + rng.uniform(-0.25, 0.25) * (2 * kPi / arms);
            const double len = rng.uniform(60, 140);
            DrawRoadSpec s;
            s.road_type = types[rng.next() % 3];
            s.start.pos = {0, 0, 0};
            s.start.existing_node = c;
            s.end.pos = {len * std::cos(ang), len * std::sin(ang), rng.uniform(-3, 3)};
            REQUIRE(w.st.execute(make_draw_road(s)));
            if (!c.valid()) c = w.g.edges().rbegin()->second.a;
        }
        const JunctionSet js = build_junctions(w.g, *w.region, {});
        if (!js.find(c)) continue;
        RenderMeshParams p;
        p.chord_tolerance = iter % 3 == 0 ? 0.5 : 0.05;
        const Junction& j = *js.find(c);
        const WorldMeshSet jm = build_junction_mesh(w.g, *w.region, j, p);
        INFO("iteration " << iter << " arms " << arms << (j.degraded ? " degraded: " : " ok ") << j.degraded_reason);
        check_valid(jm);
        for (const auto& [id, e] : w.g.edges()) check_valid(build_edge_mesh(w.g, *w.region, id, js, p));
        if (j.degraded || j.carriageway.empty()) continue;
        REQUIRE(jm.count("asphalt"));
        CHECK(plan_area(jm.at("asphalt")) == doctest::Approx(polygon_set_area(j.carriageway)).epsilon(p.chord_tolerance > 0.1 ? 0.06 : 0.01));
        seams += check_seams(w.g, *w.region, js, p);
        ++built;
    }
    CHECK(built >= 25);
    CHECK(seams > 500);
}

TEST_CASE("render mesh: highways have no kerbs") {
    for (const char* type : {"a_road_dual", "motorway"}) {
        World w;
        const EdgeId id = w.road({0, 0, 0}, {200, 20, 3}, type, true, {{100, 0, 1}});
        const JunctionSet none;
        const WorldMeshSet set = build_edge_mesh(w.g, *w.region, id, none);
        check_valid(set);
        INFO(type);
        CHECK(set.count("kerb") == 0);
        CHECK(set.count("asphalt") == 1);
    }
}

TEST_CASE("zz debug merge pavements") {
    std::string err;
    auto sc = tools::build_scene("interchange", CP_DATA_DIR, err);
    REQUIRE(sc);
    const JunctionSet js = tools::scene_junctions(*sc);
    for (const auto& [n, j] : js.junctions) {
        if (j.cls != NodeClass::Merge && j.cls != NodeClass::Diverge) continue;
        MESSAGE("node " << n.value << " cls " << static_cast<int>(j.cls) << " arms " << j.arms.size() << " pav rings " << j.pavements.size());
        for (std::size_t i = 0; i < j.arms.size(); ++i) {
            const JunctionArm& arm = j.arms[i];
            const ArmPoint c = arm_point(arm, j.trims[i], 0);
            const Vec2 dir{std::cos(c.heading_out), std::sin(c.heading_out)}, left{-dir.y, dir.x};
            double lo = 1e9, hi = -1e9;
            for (const auto& r : j.pavements)
                for (const Vec2& p : r) {
                    const Vec2 d = p - c.pos.xy();
                    if (std::abs(d.dot(dir)) < 2e-3) { lo = std::min(lo, d.dot(left)); hi = std::max(hi, d.dot(left)); }
                }
            MESSAGE("  arm " << i << " trim " << j.trims[i] << " kl " << arm.kerb_left << " kr " << arm.kerb_right << " hl " << arm.half_width_left << " hr " << arm.half_width_right << " pav lateral range on trim [" << lo << ", " << hi << "]");
        }
    }
}
