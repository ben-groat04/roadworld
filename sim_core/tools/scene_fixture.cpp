#include "scene_fixture.h"

#include "cp/junction_roundabout.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace cp::tools {
namespace {

constexpr double kDeg = kPi / 180;
using D = InterchangeDims;

struct Builder {
    Scene& sc;
    std::string err;

    bool fail(const std::string& m) {
        if (err.empty()) err = m;
        return false;
    }

    // Edge whose centreline passes closest to p (xy), with the arc length of that point.
    EdgeId edge_near(Vec2 p, double* s_out, double max_dist = 1.0) const {
        EdgeId best;
        double best_d = max_dist, best_s = 0;
        for (const auto& [id, e] : sc.graph.edges()) {
            const double L = e.horizontal.length();
            for (double s = 0; s <= L; s += 0.25) {
                const double d = (edge_point(e, s).xy() - p).length();
                if (d < best_d) {
                    best_d = d;
                    best = id;
                    best_s = s;
                }
            }
        }
        if (s_out) *s_out = best_s;
        return best;
    }

    NodeId node_near(Vec2 p, double tol = 0.5) const {
        for (const auto& [id, n] : sc.graph.nodes())
            if ((n.pos.xy() - p).length() < tol) return id;
        return {};
    }

    RoadEnd end_at(Vec3 p, std::optional<double> heading) {
        RoadEnd r;
        r.pos = p;
        r.heading = heading;
        // Attach to an existing node, else split the edge passing through p, else a new node.
        if (NodeId n = node_near(p.xy()); n.valid()) {
            r.existing_node = n;
        } else {
            double s = 0;
            if (EdgeId e = edge_near(p.xy(), &s); e.valid()) {
                r.split_edge = e;
                r.split_s = s;
            }
        }
        return r;
    }

    EdgeId draw(Vec3 a, std::optional<double> ha, std::vector<Vec3> interior, Vec3 b, std::optional<double> hb,
                const char* type, bool one_way, bool snap_a, bool snap_b) {
        DrawRoadSpec s;
        s.road_type = type;
        s.one_way = one_way;
        s.start = snap_a ? end_at(a, ha) : RoadEnd{a, {}, {}, 0, ha};
        s.end = snap_b ? end_at(b, hb) : RoadEnd{b, {}, {}, 0, hb};
        s.interior = std::move(interior);
        const std::size_t before = sc.graph.edges().size();
        (void)before;
        if (!sc.stack.execute(make_draw_road(std::move(s)))) {
            fail(std::string("draw_road failed for ") + type);
            return {};
        }
        return sc.graph.edges().rbegin()->first;  // most recently allocated id
    }

    // One slip road. `canon` is the north-west diverge (EB off-slip) polyline from the
    // dual (first) to the terminal (last); the other three are mirrors/rotations of it.
    //   mirror_x: x -> -x and reverse (on-slip);  rot180: (x,y) -> (-x,-y).
    void slip(bool mirror_x, bool rot180) {
        std::vector<Vec3> pts = {
            {-D::slip_x, D::dual_offset, 0}, {-190, 23, 1.5}, {-120, 52, 3.7}, {-50, 72, 5.4}, {0, D::rb_y, 6.25}};
        double h0 = 10 * kDeg, h1 = 0;  // heading leaving the dual / arriving at the terminal
        if (mirror_x) {
            for (auto& p : pts) p.x = -p.x;
            std::reverse(pts.begin(), pts.end());
            h0 = 0;           // departs the terminal heading east
            h1 = -10 * kDeg;  // joins the dual heading south-east
        }
        if (rot180) {
            for (auto& p : pts) {
                p.x = -p.x;
                p.y = -p.y;
            }
            h0 = wrap_angle(h0 + kPi);
            h1 = wrap_angle(h1 + kPi);
        }
        std::vector<Vec3> interior(pts.begin() + 1, pts.end() - 1);
        draw(pts.front(), h0, interior, pts.back(), h1, "b_road", true, true, true);
    }
};

}  // namespace

bool place_interchange_roundabout(Scene& sc, NodeId node, double radius, std::string& error);  // below

namespace {

bool build_interchange(Scene& sc, std::string& error) {
    Builder b{sc, {}};
    const double o = D::dual_offset;
    const double dual_end = 400;

    // East-west A-road dual carriageway: eastbound on the north side (left-hand traffic).
    b.draw({-dual_end, o, 0}, 0.0, {}, {dual_end, o, 0}, 0.0, "a_road_dual", true, false, false);
    b.draw({dual_end, -o, 0}, kPi, {}, {-dual_end, -o, 0}, kPi, "a_road_dual", true, false, false);

    // Minor road south -> north with constant-gradient approaches to the bridge deck.
    const double zt = 6.25;  // level of the terminals
    std::vector<Vec3> mi = {{0, -215, 0},  {0, -95, zt}, {0, -45, zt}, {0, -30, D::deck_z},
                            {0, 30, D::deck_z}, {0, 45, zt}, {0, 105, zt}, {0, 215, 0}};
    b.draw({0, -250, 0}, kPi / 2, mi, {0, 350, 0}, kPi / 2, "b_road", false, false, false);

    // Slips: EB off (to the north terminal), EB on, WB off (south), WB on.
    b.slip(false, false);  // EB off-slip
    b.slip(true, false);   // EB on-slip
    // South terminal: rotate 180 degrees; the terminal is on the minor road at y = -rb_y.
    b.slip(false, true);   // WB off-slip
    b.slip(true, true);    // WB on-slip

    // T-junction: residential side road to the east, 150 m north of the roundabout.
    b.draw({0, D::tee_y, 0}, 0.0, {}, {160, D::tee_y, 0}, 0.0, "residential", false, true, false);
    if (!b.err.empty()) {
        error = b.err;
        return false;
    }

    // Bridge: minor road over the dual, +-20 m of the crossing.
    double s_mid = 0;
    EdgeId mid = b.edge_near({0, 0}, &s_mid, 0.6);
    // Find the minor-road piece covering y = 0 (the dual edges also pass near (0, +-5.5)).
    mid = {};
    for (const auto& [id, e] : sc.graph.edges()) {
        if (e.road_type != "b_road" || e.one_way) continue;
        const double L = e.horizontal.length();
        for (double s = 0; s <= L; s += 0.25)
            if (std::abs(edge_point(e, s).y) < 0.13 && std::abs(edge_point(e, s).x) < 0.5) {
                mid = id;
                s_mid = s;
            }
    }
    if (!mid.valid()) {
        error = "minor road piece over the dual not found";
        return false;
    }
    if (!sc.stack.execute(make_set_structure_span(mid, s_mid - 20, s_mid + 20, StructureKind::Bridge))) {
        error = "set_structure_span failed";
        return false;
    }

    // North terminal: roundabout on the minor road.
    NodeId rb = b.node_near({0, D::rb_y});
    if (!rb.valid()) {
        error = "roundabout node missing";
        return false;
    }
    return place_interchange_roundabout(sc, rb, D::rb_radius, error);
}

}  // namespace

std::vector<std::string> scene_names() { return {"interchange"}; }

std::unique_ptr<Scene> build_scene(const std::string& name, const std::string& data_dir, std::string& error) {
    auto sc = std::make_unique<Scene>();
    sc->name = name;
    auto pack = load_region_pack_by_id(data_dir, "uk");
    if (!pack) {
        error = pack.error;
        return nullptr;
    }
    sc->region = std::make_shared<const RegionPack>(*pack);
    sc->graph.set_region(sc->region);
    if (name == "interchange") {
        if (!build_interchange(*sc, error)) return nullptr;
        return sc;
    }
    error = "unknown scene '" + name + "'";
    return nullptr;
}

JunctionSet scene_junctions(const Scene& scene) {
    JunctionSet js = build_junctions(scene.graph, *scene.region, scene.overrides);
    apply_roundabouts(js, scene.graph, *scene.region, scene.overrides);
    return js;
}

}  // namespace cp::tools
