// Tests for the render mesh builder (sim_core/include/cp/render_mesh.h).
#include <algorithm>
#include <cmath>
#include <cstring>
#include <memory>

#include "doctest/doctest.h"
#include "portable_rng.h"
#include "cp/render_mesh.h"
#include "cp/road_commands.h"
#include "cp/triangulate.h"
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
            const Vec3 t = m.tangents[i];
            REQUIRE(std::abs(std::sqrt(t.x * t.x + t.y * t.y + t.z * t.z) - 1) < 1e-6);
            REQUIRE(std::abs(t.x * n.x + t.y * n.y + t.z * n.z) < 1e-3);  // a proper tangent frame
            REQUIRE(std::abs(std::abs(m.tangent_w[i]) - 1) < 1e-9);
        }
        for (std::size_t i = 0; i < m.indices.size(); i += 3) {
            const Vec3 a = m.positions[m.indices[i]], b = m.positions[m.indices[i + 1]], c = m.positions[m.indices[i + 2]];
            for (std::uint32_t k = 0; k < 3; ++k) REQUIRE(m.indices[i + k] < m.positions.size());
            const double ux = b.x - a.x, uy = b.y - a.y, uz = b.z - a.z, vx = c.x - a.x, vy = c.y - a.y, vz = c.z - a.z;
            const double nx = uy * vz - uz * vy, ny = uz * vx - ux * vz, nz = ux * vy - uy * vx;
            REQUIRE(std::sqrt(nx * nx + ny * ny + nz * nz) > 1e-10);  // not degenerate
            if (name != "kerb") REQUIRE(nz > 0);                       // not flipped
            REQUIRE(std::sqrt(nx * nx + ny * ny + nz * nz) > 2e-8);  // area above 1e-8 m2
            {
                // The winding agrees with the stored (averaged) vertex normals.
                Vec3 avg{0, 0, 0};
                for (std::uint32_t k = 0; k < 3; ++k) {
                    const Vec3 vn = m.normals[m.indices[i + k]];
                    avg = {avg.x + vn.x, avg.y + vn.y, avg.z + vn.z};
                }
                REQUIRE(nx * avg.x + ny * avg.y + nz * avg.z > 0);
            }
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
            if (B.empty()) {  // an edge swallowed by its two trims has no mesh of its own
                const Edge* ee = g.edge(arm.edge);
                const Junction* other = js.find(arm.at_a ? ee->b : ee->a);
                CHECK(ee->horizontal.length() < j.trims[i] + (other ? other->trims.empty() ? 0 : *std::max_element(other->trims.begin(), other->trims.end()) : 0) + 0.02);
                continue;
            }
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
                // No paving beside a merge / diverge gore (photos: no footway at a slip nose): the pavement is absent there.
                if ((j.cls == NodeClass::Merge || j.cls == NodeClass::Diverge) && (lat(b) > arm.kerb_left - 1e-3 || lat(b) < -arm.kerb_right + 1e-3)) continue;
                CHECK_MESSAGE(near(b, A, duv) < 1e-3, "seam: node ", j.node.value, " cls ", static_cast<int>(j.cls), " arm ", i, " lat ", lat(b), " kl ", arm.kerb_left, " kr ", arm.kerb_right, " hl ", arm.half_width_left, " hr ", arm.half_width_right, " a_lo ", a_lo, " a_hi ", a_hi, " z ", b.p.z);
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
        CHECK(ch.origin.x == static_cast<double>(tk.i) * 256.0);
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

TEST_CASE("render mesh: highway edges have no kerb surface") {
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

// ---------------------------------------------------------------- round 2

namespace {

// Road surface height at p from the nearest of the edges (centreline + SurfaceProfile).
double nearest_road_z(const RoadGraph& g, const RegionPack& r, const std::vector<EdgeId>& edges, Vec2 p) {
    double best = 1e9, z = 0;
    for (EdgeId id : edges) {
        const Edge& e = *g.edge(id);
        const SurfaceProfile sp = make_surface_profile(r, e);
        const auto c = sample_centreline(e, {0.005, 0.01, 0.2});
        for (std::size_t i = 0; i + 1 < c.size(); ++i) {
            const Vec2 a = c[i].pos.xy(), b = c[i + 1].pos.xy(), ab = b - a;
            const double l2 = ab.dot(ab), t = l2 > 0 ? std::clamp((p - a).dot(ab) / l2, 0.0, 1.0) : 0.0;
            const double d = (p - (a + ab * t)).length();
            if (d < best) {
                best = d;
                z = c[i].pos.z + (c[i + 1].pos.z - c[i].pos.z) * t + sp.offset(ab.cross(p - a) >= 0 ? d : -d);
            }
        }
    }
    return z;
}

bool in_any_triangle(const WorldMeshSet& set, Vec2 p) {
    for (const auto& [name, m] : set)
        for (std::size_t i = 0; i < m.indices.size(); i += 3) {
            const Vec2 a = m.positions[m.indices[i]].xy(), b = m.positions[m.indices[i + 1]].xy(), c = m.positions[m.indices[i + 2]].xy();
            const double d1 = (b - a).cross(p - a), d2 = (c - b).cross(p - b), d3 = (a - c).cross(p - c);
            if ((d1 >= -1e-9 && d2 >= -1e-9 && d3 >= -1e-9) || (d1 <= 1e-9 && d2 <= 1e-9 && d3 <= 1e-9)) return true;
        }
    return false;
}

}  // namespace

TEST_CASE("render mesh: highways have no kerbs, also through merge and diverge junctions") {
    std::string err;
    auto sc = tools::build_scene("interchange", CP_DATA_DIR, err);
    REQUIRE_MESSAGE(sc, err);
    const JunctionSet js = tools::scene_junctions(*sc);
    int merges = 0;
    for (const auto& [n, j] : js.junctions) {
        if (j.cls != NodeClass::Merge && j.cls != NodeClass::Diverge) continue;
        ++merges;
        const WorldMeshSet jm = build_junction_mesh(sc->graph, *sc->region, j);
        check_valid(jm);
        for (std::size_t i = 0; i < j.arms.size(); ++i) {
            const JunctionArm& arm = j.arms[i];
            const ArmPoint c = arm_point(arm, j.trims[i], 0);
            const Vec2 dir{std::cos(c.heading_out), std::sin(c.heading_out)}, left{-dir.y, dir.x};
            double lo = 1e9, hi = -1e9;
            for (const auto& r : j.pavements)
                for (const Vec2& p : r) {
                    const Vec2 d = p - c.pos.xy();
                    if (std::abs(d.dot(dir)) < 3e-3) {
                        lo = std::min(lo, d.dot(left));
                        hi = std::max(hi, d.dot(left));
                    }
                }
            INFO("node " << n.value << " arm " << i);
            const bool has_pavement = arm.half_width_left - arm.kerb_left > 1e-3;
            if (!has_pavement) {
                // Dual carriageway arm: no kerb face runs along its outer edge inside the junction.
                if (jm.count("kerb")) {
                    const WorldMesh& m = jm.at("kerb");
                    for (std::size_t k = 0; k < m.indices.size(); k += 3) {
                        Vec2 cen{0, 0};
                        for (std::size_t q = 0; q < 3; ++q) cen = cen + m.positions[m.indices[k + q]].xy() * (1.0 / 3);
                        const Vec2 d = cen - c.pos.xy();
                        const double t = d.dot(dir), l = d.dot(left);
                        const bool along = t < -3.0 && t > -j.trims[i] + 3.0;
                        CHECK_FALSE((along && std::abs(std::abs(l) - arm.half_width_left) < 0.3));
                    }
                }
            } else {
                // Pavement polygons reach the arm's full cross-section at the trim line on every side that is
                // not blocked by a neighbouring carriageway, and at least on one side.
                CHECK(std::max(hi, -lo) == doctest::Approx(arm.half_width_left).epsilon(1e-3));
                if (j.cls != NodeClass::Merge && j.cls != NodeClass::Diverge) {  // a slip gore has no pavement beside it
                    CHECK(hi >= arm.kerb_left - 1e-3);
                CHECK(-lo >= arm.kerb_right - 1e-3);
                }
            }
        }
        // Pavement never overlaps the carriageway polygon.
        if (jm.count("paving")) {
            const WorldMesh& m = jm.at("paving");
            for (std::size_t k = 0; k < m.indices.size(); k += 3) {
                Vec2 cen{0, 0};
                for (std::size_t q = 0; q < 3; ++q) cen = cen + m.positions[m.indices[k + q]].xy() * (1.0 / 3);
                for (const auto& ring : j.carriageway) {
                    bool in = false;
                    double dmin = 1e9;
                    for (std::size_t a = 0, b = ring.size() - 1; a < ring.size(); b = a++) {
                        const Vec2 p = ring[a], q = ring[b];
                        if ((p.y > cen.y) != (q.y > cen.y) && cen.x < (q.x - p.x) * (cen.y - p.y) / (q.y - p.y) + p.x) in = !in;
                        const Vec2 ab = q - p;
                        const double l2 = ab.dot(ab), t = l2 > 0 ? std::clamp((cen - p).dot(ab) / l2, 0.0, 1.0) : 0.0;
                        dmin = std::min(dmin, (cen - (p + ab * t)).length());
                    }
                    CHECK_FALSE((in && dmin > 0.05));
                }
            }
        }
    }
    CHECK(merges >= 4);
    // Whole-fixture: kerb surface triangles never sit on a highway edge.
    for (const auto& [id, e] : sc->graph.edges()) {
        if (e.road_type != "a_road_dual" && e.road_type != "motorway") continue;
        CHECK(build_edge_mesh(sc->graph, *sc->region, id, js).count("kerb") == 0);
    }
}

TEST_CASE("render mesh: roundabout islands take their surfaces from the region pack") {
    std::string err;
    auto sc = tools::build_scene("interchange", CP_DATA_DIR, err);
    REQUIRE_MESSAGE(sc, err);
    RoundaboutResult rr;
    const JunctionSet js = tools::scene_junctions(*sc, &rr);
    REQUIRE(!rr.layouts.empty());
    int checked = 0;
    for (const RoundaboutLayout& l : rr.layouts) {
        if (!l.ok) continue;
        const WorldMeshSet m = build_roundabout_mesh(sc->graph, *sc->region, l);
        check_valid(m);
        CHECK_FALSE(m.count("island"));
        REQUIRE(m.count("grass"));  // planted central island
        {
            double expect = polygon_set_area(l.island);  // splitter islands sharing the grass surface add to it
            const std::string& stop = l.splitter_surface.empty() ? sc->region->roundabout.splitter_surface : l.splitter_surface;
            if (stop == "grass")
                for (const SplitterIsland& sp : l.splitters) expect += polygon_set_area(PolygonSet{sp.polygon});
            CHECK(plan_area(m.at("grass")) == doctest::Approx(expect).epsilon(0.02));
        }
        if (!l.splitters.empty()) {
            const std::string& top = l.splitter_surface;  // the size class's surface (UK: asphalt, grass on the largest ring)
            REQUIRE(!top.empty());
            REQUIRE(m.count(top));
            double a = 0;
            for (const auto& s : l.splitters) a += polygon_set_area(PolygonSet{s.polygon});
            // (a splitter surface equal to the central island's, e.g. grass, shares one mesh with it)
            const double expect = top == "grass" ? a + polygon_set_area(l.island) : a;
            CHECK(plan_area(m.at(top)) == doctest::Approx(expect).epsilon(0.05));
        }
        REQUIRE(m.count("kerb"));
        ++checked;

        // Kerb feet follow the adjacent carriageway: every kerb vertex is at the road height or one
        // upstand above it (central island: the ring edges; splitters: their approach).
        std::vector<EdgeId> ring_edges = l.group.ring_edges;
        std::vector<EdgeId> all = ring_edges;
        for (const auto& arm : l.group.arms) all.push_back(arm.edge);
        std::size_t good = 0, total = 0;
        for (const Vec3& p : m.at("kerb").positions) {
            // Island vertices lie on the ring side; use whichever edge set is nearer.
            ++total;
            const double h = sc->region->roundabout.island_kerb_upstand;
            bool matched = false;
            for (EdgeId id : all) {  // on (or one upstand above) the road surface of an adjacent edge
                const double zr = nearest_road_z(sc->graph, *sc->region, {id}, p.xy());
                if (std::abs(p.z - zr) < 0.01 || std::abs(p.z - (zr + h)) < 0.01) {
                    matched = true;
                    break;
                }
            if (!matched)  // splitters stand on the ring node's junction surface (not the arm profile)
                for (const auto& [nd, jn] : l.ring_junctions) {
                    const double zj = junction_surface_z(sc->graph, *sc->region, jn, p.xy(), true);
                    if (std::abs(p.z - zj) < 0.01 || std::abs(p.z - (zj + h)) < 0.01) matched = true;
                }
            }
            if (matched) ++good;
            else if (total - good < 6)
                MESSAGE("unmatched kerb vertex " << p.x << " " << p.y << " " << p.z << " ring z "
                                                 << nearest_road_z(sc->graph, *sc->region, ring_edges, p.xy()));
            if (!matched && total - good < 2)
                for (EdgeId id : all) MESSAGE("   edge " << id.value << " type " << sc->graph.edge(id)->road_type << " z " << nearest_road_z(sc->graph, *sc->region, {id}, p.xy()));
        }
        CHECK(good == total);
    }
    CHECK(checked >= 1);
}

TEST_CASE("render mesh: vertex masks and lane-relative uv") {
    World w;
    const EdgeId id = w.road({0, 0, 0}, {100, 0, 0}, "residential");
    const JunctionSet none;
    const WorldMeshSet a = build_edge_mesh(w.g, *w.region, id, none), b = build_edge_mesh(w.g, *w.region, id, none);
    check_valid(a);
    const WorldMesh& m = a.at("asphalt");
    REQUIRE(m.colours.size() == m.positions.size());
    REQUIRE(m.uv1.size() == m.positions.size());
    double bmin = 1, bmax = 0;
    bool saw_edge = false, saw_inner = false, saw_driving = false;
    for (std::size_t i = 0; i < m.positions.size(); ++i) {
        const Float4 c = m.colours[i];
        CHECK(c.w == 0);  // reserved
        CHECK((c.x >= 0 && c.x <= 1 && c.y >= 0 && c.y <= 1 && c.z >= 0 && c.z <= 1));
        bmin = std::min<double>(bmin, c.z);
        bmax = std::max<double>(bmax, c.z);
        const double y = std::abs(m.positions[i].y);
        if (y > 5 - 1e-9) {  // carriageway edge at the kerb foot (parking lane outer edge)
            CHECK(c.y == doctest::Approx(1.0));
            saw_edge = true;
        }
        if (y > 4.5 + 1e-9 && y < 5 - 1e-9) CHECK(c.y > 0);
        if (y < 4.5 - 1e-9) {
            CHECK(c.y == doctest::Approx(0.0));
            saw_inner = true;
        }
        if (m.uv1[i].y > 0) {  // driving lane
            saw_driving = true;
            CHECK(m.uv1[i].y == doctest::Approx(3.0));
            CHECK(std::abs(m.uv1[i].x) <= 1.5 + 1e-9);
            CHECK(c.x == doctest::Approx(0.55));
            CHECK(y <= 3.0 + 1e-9);
        } else {
            CHECK(c.x == 0);  // parking lane: no wear
        }
    }
    CHECK(saw_edge);
    CHECK(saw_inner);
    CHECK(saw_driving);
    CHECK(bmax - bmin > 0.05);  // low-frequency variation changes along the edge
    // Lane boundaries are split vertices: both lane-relative extremes exist in each lane.
    bool lo = false, hi = false;
    for (const Float2 u : [&] { std::vector<Float2> v; for (const Vec2& q : m.uv1) v.push_back({static_cast<float>(q.x), static_cast<float>(q.y)}); return v; }()) {
        if (u.y > 0 && u.x < -1.499f) lo = true;
        if (u.y > 0 && u.x > 1.499f) hi = true;
    }
    CHECK(lo);
    CHECK(hi);
    // Deterministic.
    REQUIRE(b.at("asphalt").colours.size() == m.colours.size());
    for (std::size_t i = 0; i < m.colours.size(); ++i) CHECK(b.at("asphalt").colours[i].z == m.colours[i].z);
    // Float buffers carry the same channels as bytes.
    const MeshBuffers fb = pack_buffers(m, {0, 0, 0});
    REQUIRE(fb.colours.size() == m.colours.size());
    REQUIRE(fb.uv1.size() == m.colours.size());
    CHECK(fb.colours[0][3] == 0);
}

TEST_CASE("render mesh: wear rises on the approach to a junction") {
    World w;
    w.road({0, 0, 0}, {100, 0, 0}, "residential");
    const NodeId c = w.g.edges().begin()->second.a;
    const auto spoke = [&](Vec3 to) {
        DrawRoadSpec s;
        s.road_type = "residential";
        s.start.existing_node = c;
        s.end.pos = to;
        REQUIRE(w.st.execute(make_draw_road(s)));
    };
    spoke({-100, 0, 0});
    spoke({0, 100, 0});
    const JunctionSet js = build_junctions(w.g, *w.region, {});
    REQUIRE(js.find(c));
    const Junction& j = *js.find(c);
    const EdgeId id = w.g.edges().begin()->first;
    const WorldMeshSet set = build_edge_mesh(w.g, *w.region, id, js);
    const WorldMesh& m = set.at("asphalt");
    const double trim = j.trims[static_cast<std::size_t>(std::find_if(j.arms.begin(), j.arms.end(), [&](const JunctionArm& a) { return a.edge == id; }) - j.arms.begin())];
    bool boosted = false, plain_near = false;
    for (std::size_t i = 0; i < m.positions.size(); ++i) {
        if (m.uv1[i].y <= 0) continue;
        const double s = m.uv[i].y;
        if (s > trim + 20 + 1e-6) CHECK(m.colours[i].x == doctest::Approx(0.55));
        if (s < trim + 1e-6) {
            if (m.colours[i].x > 0.99f) boosted = true;
            if (m.colours[i].x < 0.56f) plain_near = true;  // the lane leaving the junction
        }
        CHECK(m.colours[i].x <= 1.0f);
    }
    CHECK(boosted);
    CHECK(plain_near);
    // The junction carriageway carries a final wear mask from the connector paths.
    const WorldMeshSet jm = build_junction_mesh(w.g, *w.region, j);
    const WorldMesh& ja = jm.at("asphalt");
    float hi = 0, lo = 1;
    for (const Float4& c4 : ja.colours) {
        hi = std::max(hi, c4.x);
        lo = std::min(lo, c4.x);
    }
    CHECK(hi > 0.9f);
    CHECK(lo < 0.5f);
    for (const Vec2& q : ja.uv1) CHECK(q.y == 0);
}

TEST_CASE("render mesh: junction uv is planar at 1 uv unit per metre; kerb uv runs along the kerb") {
    std::string err;
    auto sc = tools::build_scene("interchange", CP_DATA_DIR, err);
    REQUIRE_MESSAGE(sc, err);
    const JunctionSet js = tools::scene_junctions(*sc);
    std::size_t checked = 0;
    for (const auto& [n, j] : js.junctions) {
        if (j.carriageway.empty()) continue;
        const WorldMeshSet jm = build_junction_mesh(sc->graph, *sc->region, j);
        for (const auto& [name, m] : jm) {
            if (name == "kerb") continue;
            for (std::size_t i = 0; i < m.indices.size(); i += 3)
                for (int k = 0; k < 3; ++k) {
                    const std::uint32_t p = m.indices[i + static_cast<std::size_t>(k)], q = m.indices[i + static_cast<std::size_t>((k + 1) % 3)];
                    const double dxy = (m.positions[p].xy() - m.positions[q].xy()).length();
                    const double duv = std::hypot(m.uv[p].x - m.uv[q].x, m.uv[p].y - m.uv[q].y);
                    if (dxy < 1e-6) continue;
                    ++checked;
                    REQUIRE(duv / dxy == doctest::Approx(1.0).epsilon(1e-3));  // never stretched
                }
        }
        if (jm.count("kerb"))
            for (const Vec2& q : jm.at("kerb").uv) CHECK(q.y >= -1e-9);  // v is up the face
    }
    CHECK(checked > 1000);

    // Straight road: kerb u = distance along the road, v = up the face (then across the top).
    World w;
    const EdgeId id = w.road({0, 0, 0}, {100, 0, 0}, "b_road");
    const JunctionSet none;
    const WorldMeshSet set = build_edge_mesh(w.g, *w.region, id, none);
    const WorldMesh& k = set.at("kerb");
    double vmax = 0;
    for (std::size_t i = 0; i < k.positions.size(); ++i) {
        CHECK(k.uv[i].x == doctest::Approx(k.positions[i].x));
        vmax = std::max(vmax, k.uv[i].y);
        CHECK(k.uv[i].y >= 0);
    }
    CHECK(vmax == doctest::Approx(0.125 + 0.15).epsilon(1e-6));
}

TEST_CASE("render mesh: coarse tolerance collapses straights to long quads") {
    World w;
    const EdgeId id = w.road({0, 0, 0}, {300, 0, 0}, "b_road");
    const JunctionSet none;
    RenderMeshParams fine, coarse;
    coarse.chord_tolerance = 2.0;
    const std::size_t nf = tri_count(build_edge_mesh(w.g, *w.region, id, none, fine));
    const std::size_t nc = tri_count(build_edge_mesh(w.g, *w.region, id, none, coarse));
    CHECK(nc * 5 <= nf);
    // A single long interval: one quad per strip.
    const WorldMeshSet coarse_set = build_edge_mesh(w.g, *w.region, id, none, coarse);
    const WorldMesh& m = coarse_set.at("asphalt");
    double vmax = 0;
    for (const Vec2& q : m.uv) vmax = std::max(vmax, q.y);
    CHECK(vmax == doctest::Approx(300));
    CHECK(m.positions.size() <= 40);
}

TEST_CASE("triangulate: holes, orientation and failure reporting") {
    const auto total_area = [](const std::vector<Vec2>& pts, const std::vector<std::array<std::uint32_t, 3>>& t) {
        double a = 0;
        for (const auto& tr : t) {
            const double s = (pts[tr[1]] - pts[tr[0]]).cross(pts[tr[2]] - pts[tr[0]]) / 2;
            REQUIRE(s > 0);  // counter-clockwise
            a += s;
        }
        return a;
    };
    std::vector<Vec2> pts{{0, 0}, {10, 0}, {10, 10}, {0, 10}};
    std::vector<std::array<std::uint32_t, 3>> tris;
    // Hole touching the outer ring at a vertex.
    {
        pts = {{0, 0}, {10, 0}, {10, 10}, {0, 10}, {0, 0}, {4, 1}, {3, 4}};
        tris.clear();
        CHECK(triangulate_polygons(pts, {{0, 1, 2, 3}, {4, 5, 6}}, tris));
        CHECK(total_area(pts, tris) == doctest::Approx(100 - 6.5));
    }
    // Three holes side by side (given counter-clockwise: orientation is irrelevant).
    {
        pts = {{0, 0}, {10, 0}, {10, 10}, {0, 10}};
        std::vector<std::vector<std::uint32_t>> rings{{0, 1, 2, 3}};
        for (double x : {1.0, 4.0, 7.0}) {
            const auto b = static_cast<std::uint32_t>(pts.size());
            pts.push_back({x, 4});
            pts.push_back({x + 2, 4});
            pts.push_back({x + 2, 6});
            pts.push_back({x, 6});
            rings.push_back({b, b + 1, b + 2, b + 3});
        }
        tris.clear();
        CHECK(triangulate_polygons(pts, rings, tris));
        CHECK(total_area(pts, tris) == doctest::Approx(100 - 12));
    }
    // Clockwise outer ring is not dropped.
    {
        pts = {{0, 0}, {0, 10}, {10, 10}, {10, 0}};
        tris.clear();
        CHECK(triangulate_polygons(pts, {{0, 1, 2, 3}}, tris));
        CHECK(total_area(pts, tris) == doctest::Approx(100));
    }
    // Island inside a hole inside an outer ring (nesting depth 2 is filled again).
    {
        pts = {{0, 0}, {20, 0}, {20, 20}, {0, 20}, {4, 4}, {16, 4}, {16, 16}, {4, 16}, {8, 8}, {12, 8}, {12, 12}, {8, 12}};
        tris.clear();
        CHECK(triangulate_polygons(pts, {{0, 1, 2, 3}, {4, 5, 6, 7}, {8, 9, 10, 11}}, tris));
        CHECK(total_area(pts, tris) == doctest::Approx(400 - 144 + 16));
    }
    // Concave outline with collinear points.
    {
        pts = {{0, 0}, {5, 0}, {10, 0}, {10, 10}, {5, 5}, {0, 10}};
        tris.clear();
        CHECK(triangulate_polygons(pts, {{0, 1, 2, 3, 4, 5}}, tris));
        CHECK(total_area(pts, tris) == doctest::Approx(75));
    }
    // Failure is reported, not hidden: a bad index, and a non-finite point.
    {
        tris.clear();
        CHECK_FALSE(triangulate_polygons(pts, {{0, 1, 99}}, tris));
        pts = {{0, 0}, {1, 0}, {std::nan(""), 1}};
        CHECK_FALSE(triangulate_polygons(pts, {{0, 1, 2}}, tris));
    }
}

TEST_CASE("render mesh: surface height queries match the mesh") {
    World w;
    const EdgeId id = w.road({0, 0, 0}, {120, 0, 6}, "residential", false, {{60, 0, 2}});
    const JunctionSet none;
    const WorldMeshSet set = build_edge_mesh(w.g, *w.region, id, none);
    const Edge& e = *w.g.edge(id);
    const SurfaceProfile sp = make_surface_profile(*w.region, e);
    REQUIRE(sp.valid());
    CHECK(sp.left() == doctest::Approx(7.15));
    CHECK(sp.right() == doctest::Approx(-7.15));
    std::size_t checked = 0;
    for (const char* surface : {"asphalt", "paving"}) {
        for (const Vec3& p : set.at(surface).positions) {
            // Straight road along +x from the origin: s = x (to first order), lateral = y.
            const double s = p.x;
            const bool pave = std::string(surface) == "paving";
            CHECK(p.z == doctest::Approx(edge_point(e, s).z + sp.offset(p.y, !pave)).epsilon(1e-6).scale(1.0));
            if (!pave) CHECK(p.z == doctest::Approx(edge_surface_z(w.g, *w.region, id, s, p.y)).epsilon(1e-6).scale(1.0));
            ++checked;
        }
    }
    CHECK(checked > 100);
    // Camber and kerb: crown at the centreline, kerb top = carriageway edge + upstand.
    CHECK(sp.offset(0) == doctest::Approx(0));
    CHECK(sp.offset(5.0) == doctest::Approx(-0.125));
    CHECK(sp.offset(5.0, false) == doctest::Approx(0.0));  // kerb top at the same lateral
}

TEST_CASE("render mesh: junction surface function matches the trim lines and the mesh") {
    World w;
    w.road({0, 0, 0}, {100, 0, 1}, "residential");
    const NodeId c = w.g.edges().begin()->second.a;
    const auto spoke = [&](Vec3 to, const char* type) {
        DrawRoadSpec s;
        s.road_type = type;
        s.start.existing_node = c;
        s.end.pos = to;
        REQUIRE(w.st.execute(make_draw_road(s)));
    };
    spoke({-100, 0, 2}, "residential");
    spoke({10, 90, 3}, "b_road");
    const JunctionSet js = build_junctions(w.g, *w.region, {});
    const Junction& j = *js.find(c);
    const JunctionSurface js_surface(w.g, *w.region, j);
    REQUIRE(js_surface.valid());
    for (std::size_t i = 0; i < j.arms.size(); ++i) {
        const JunctionArm& arm = j.arms[i];
        const Edge& e = *w.g.edge(arm.edge);
        const SurfaceProfile sp = make_surface_profile(*w.region, e);
        for (double lat : {-2.5, 0.0, 1.0, 4.0}) {
            const ArmPoint ap = arm_point(arm, j.trims[i], lat);
            const double s = arm.at_a ? j.trims[i] : e.horizontal.length() - j.trims[i];
            const double ed = edge_point(e, s).z + sp.offset(arm.at_a ? lat : -lat);
            CHECK(js_surface.z(ap.pos.xy()) == doctest::Approx(ed).epsilon(1e-6).scale(0.01));
        }
    }
    const WorldMeshSet jm = build_junction_mesh(w.g, *w.region, j);
    for (const Vec3& p : jm.at("asphalt").positions) CHECK(p.z == doctest::Approx(js_surface.z(p.xy())).epsilon(1e-9).scale(1.0));
    CHECK(junction_surface_z(w.g, *w.region, j, {0, 0}) == doctest::Approx(js_surface.z({0, 0})));
}

TEST_CASE("render mesh: edge boundary polylines") {
    World w;
    const EdgeId id = w.road({0, 0, 0}, {100, 0, 3}, "b_road");
    const JunctionSet none;
    EdgeBoundary bd;
    const WorldMeshSet set = build_edge_mesh(w.g, *w.region, id, none, {}, &bd);
    REQUIRE(bd.left.size() >= 2);
    REQUIRE(bd.left.size() == bd.right.size());
    const SurfaceProfile sp = make_surface_profile(*w.region, *w.g.edge(id));
    for (std::size_t i = 0; i < bd.left.size(); ++i) {
        CHECK(bd.left[i].y == doctest::Approx(sp.left()));
        CHECK(bd.right[i].y == doctest::Approx(sp.right()));
        CHECK(bd.left[i].z == doctest::Approx(edge_point(*w.g.edge(id), bd.left[i].x).z + sp.offset(sp.left(), false)));
    }
    CHECK(bd.left.front().x == doctest::Approx(0));
    CHECK(bd.left.back().x == doctest::Approx(100));
}

TEST_CASE("render mesh: 2-arm joints are filled (type change and heading kink) and seamless pairs are left alone") {
    struct Case {
        const char* second_type;
        Vec3 end;
        bool needs_joint;
    };
    for (const Case& cs : {Case{"b_road", {200, 0, 0}, true}, Case{"residential", {200, 40, 0}, true},
                           Case{"residential", {200, 0, 0}, false}}) {
        World w;
        const EdgeId first = w.road({0, 0, 0}, {100, 0, 0}, "residential");
        const NodeId mid = w.g.edge(first)->b;
        DrawRoadSpec s;
        s.road_type = cs.second_type;
        s.start.existing_node = mid;
        s.end.pos = cs.end;
        REQUIRE(w.st.execute(make_draw_road(s)));
        const EdgeId second = w.g.edges().rbegin()->first;
        const JunctionSet none;
        INFO(cs.second_type << " to (" << cs.end.x << ", " << cs.end.y << ")");
        const WorldMeshSet joint = build_joint_mesh(w.g, *w.region, mid);
        double a_max = -1e9, b_min = 1e9;
        for (const auto& [name, m] : build_edge_mesh(w.g, *w.region, first, none))
            for (const Vec3& p : m.positions) a_max = std::max(a_max, p.x);
        for (const auto& [name, m] : build_edge_mesh(w.g, *w.region, second, none))
            for (const Vec3& p : m.positions) b_min = std::min(b_min, p.x);
        if (!cs.needs_joint) {
            CHECK(joint.empty());
            CHECK(a_max == doctest::Approx(100).epsilon(1e-6));
            CHECK(b_min == doctest::Approx(100).epsilon(1e-6));
            continue;
        }
        check_valid(joint);
        CHECK(a_max < 100 - 3.9);  // both edges are cut back by joint_length
        CHECK(b_min > 100 + (cs.end.y != 0 ? 0.9 : 3.9));
        // The gap between the end rings is covered over the narrower cross-section.
        for (double lat : {-4.5, -2.0, 0.0, 2.0, 4.5}) CHECK(in_any_triangle(joint, {100, lat}));
        // Rings weld: joint vertices on the edge end lines exist in the edge meshes.
        for (EdgeId id : {first, second}) {
            if (cs.end.y != 0) break;
            const WorldMeshSet em = build_edge_mesh(w.g, *w.region, id, none);
            std::size_t matched = 0, on_line = 0;
            for (const auto& [name, jm] : joint)
                for (const Vec3& p : jm.positions) {
                    const double line = id == first ? a_max : b_min;
                    if (std::abs(p.x - line) > 0.05) continue;
                    ++on_line;
                    for (const auto& [en, e2] : em) {
                        bool hit = false;
                        for (const Vec3& q : e2.positions)
                            if (std::pow(q.x - p.x, 2) + std::pow(q.y - p.y, 2) + std::pow(q.z - p.z, 2) < 1e-6) { hit = true; break; }
                        if (hit) { ++matched; break; }
                    }
                }
            CHECK(on_line > 4);
            CHECK(matched > 0);
        }
    }
}

TEST_CASE("render mesh: merged-surface packing carries the same triangles with per-vertex class ids") {
    std::string err;
    auto sc = tools::build_scene("interchange", CP_DATA_DIR, err);
    REQUIRE_MESSAGE(sc, err);
    RoundaboutResult rr;
    const JunctionSet js = tools::scene_junctions(*sc, &rr);
    SurfaceTable table, table2;
    const auto separate = build_mesh_chunks(sc->graph, *sc->region, js, &rr.layouts, {}, {}, nullptr, &table);
    ChunkParams cp;
    cp.merge_surfaces = true;
    const auto merged = build_mesh_chunks(sc->graph, *sc->region, js, &rr.layouts, {}, cp, nullptr, &table2);
    REQUIRE(separate.size() == merged.size());
    REQUIRE(!table.names.empty());
    CHECK(table.names == table2.names);
    CHECK(std::is_sorted(table.names.begin(), table.names.end()));
    CHECK(table.id("asphalt") >= 0);
    CHECK(table.id("no such class") == -1);

    // A triangle's identity: class name plus positions, normals, uv0, uv1 and colour of its three vertices.
    const auto key = [](const std::string& cls, const MeshBuffers& b, std::size_t k) {
        std::string s = cls;
        for (std::size_t q = 0; q < 3; ++q) {
            const std::uint32_t v = b.indices[k + q];
            char buf[256];
            std::snprintf(buf, sizeof buf, "|%.5f,%.5f,%.5f;%.4f,%.4f,%.4f;%.4f,%.4f;%.4f,%.4f;%d,%d,%d,%d", static_cast<double>(b.positions[v].x),
                          static_cast<double>(b.positions[v].y), static_cast<double>(b.positions[v].z), static_cast<double>(b.normals[v].x),
                          static_cast<double>(b.normals[v].y), static_cast<double>(b.normals[v].z), static_cast<double>(b.uv0[v].x),
                          static_cast<double>(b.uv0[v].y), static_cast<double>(b.uv1[v].x), static_cast<double>(b.uv1[v].y), b.colours[v][0],
                          b.colours[v][1], b.colours[v][2], b.colours[v][3]);
            s += buf;
        }
        return s;
    };
    std::size_t triangles = 0;
    for (const auto& [tk, ch] : separate) {
        std::multiset<std::string> expect;
        for (const auto& [name, b] : ch.surfaces) {
            REQUIRE(b.surface_ids.size() == b.positions.size());
            for (std::size_t k = 0; k + 2 < b.indices.size(); k += 3) {
                expect.insert(key(name, b, k));
                for (std::size_t q = 0; q < 3; ++q) CHECK(table.names[b.surface_ids[b.indices[k + q]]] == name);
            }
        }
        const MeshChunk& mc = merged.at(tk);
        REQUIRE(mc.surfaces.size() == 1);
        const MeshBuffers& b = mc.surfaces.begin()->second;
        CHECK(mc.surfaces.begin()->first == "road");
        REQUIRE(b.sections.size() == 1);
        CHECK(b.sections[0].index_count == b.indices.size());
        REQUIRE(b.surface_ids.size() == b.positions.size());
        for (std::size_t k = 0; k + 2 < b.indices.size(); k += 3) {
            const std::string& cls = table.names[b.surface_ids[b.indices[k]]];
            for (std::size_t q = 1; q < 3; ++q) CHECK(table.names[b.surface_ids[b.indices[k + q]]] == cls);
            const auto it = expect.find(key(cls, b, k));
            REQUIRE(it != expect.end());  // same triangle, same class, same attributes
            expect.erase(it);
            ++triangles;
        }
        CHECK(expect.empty());
    }
    CHECK(triangles > 1000);
    // The default mode keeps its sections per class and structure span.
    for (const auto& [tk, ch] : separate)
        for (const auto& [name, b] : ch.surfaces) CHECK_FALSE(b.sections.empty());
}
