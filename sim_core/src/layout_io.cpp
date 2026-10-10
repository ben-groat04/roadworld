// No exceptions: sim_core builds with -fno-exceptions. Errors travel as a message; the first error wins.
#include "cp/layout_io.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <fstream>
#include <initializer_list>
#include <limits>
#include <sstream>

#include "cp/junction_roundabout.h"
#include "cp/road_commands.h"
#include "json_util.h"
#include "nlohmann/json.hpp"

namespace cp {

namespace {

using json = nlohmann::json;

constexpr double kDeg = kPi / 180;
constexpr double kSnapTol = 1.0;   // m: a road end this close to a node / centreline attaches to it
constexpr double kNodeTol = 0.75;  // m: an existing node this close to an interior stretch splits the road
constexpr double kSampleStep = 0.5;

std::string idx(const char* what, std::size_t i) { return std::string(what) + "[" + std::to_string(i) + "]"; }

// ---------------------------------------------------------------- parsing

using jsonu::Err;

bool ignored_key(const std::string& k) { return (!k.empty() && k[0] == '_') || k == "notes"; }

void check_keys(const json& j, std::initializer_list<const char*> allowed, const std::string& ctx, Err& err) {
    if (err.failed()) return;
    if (!j.is_object()) {
        err.set(ctx + ": expected an object");
        return;
    }
    for (auto it = j.begin(); it != j.end(); ++it) {
        if (ignored_key(it.key())) continue;
        bool ok = false;
        for (const char* a : allowed) ok = ok || it.key() == a;
        if (!ok) {
            err.set(ctx + ": unknown key \"" + it.key() + "\"");
            return;
        }
    }
}

using jsonu::member;

const json* need(const json& j, const char* key, const std::string& ctx, Err& err) {
    if (err.failed()) return nullptr;
    const json* v = member(j, key);
    if (!v) err.set(ctx + ": missing \"" + key + "\"");
    return v;
}

std::string get_string(const json& j, const char* key, const std::string& ctx, Err& err, bool required,
                       const std::string& def = {}) {
    const json* v = required ? need(j, key, ctx, err) : member(j, key);
    if (!v) return def;
    if (!v->is_string()) {
        err.set(ctx + ": \"" + key + "\" must be a string");
        return def;
    }
    return v->get<std::string>();
}

bool get_bool(const json& j, const char* key, const std::string& ctx, Err& err, bool def) {
    const json* v = member(j, key);
    if (!v) return def;
    if (!v->is_boolean()) {
        err.set(ctx + ": \"" + key + "\" must be true or false");
        return def;
    }
    return v->get<bool>();
}

double get_number(const json& j, const char* key, const std::string& ctx, Err& err, bool required, double def = 0) {
    const json* v = required ? need(j, key, ctx, err) : member(j, key);
    if (!v) return def;
    if (!v->is_number() || !std::isfinite(v->get<double>())) {
        err.set(ctx + ": \"" + key + "\" must be a finite number");
        return def;
    }
    return v->get<double>();
}

// [x, y] or [x, y, z]; z defaults to 0 (and is dropped when `want_z` is false).
Vec3 read_vec(const json& v, bool want_z, const std::string& ctx, Err& err) {
    Vec3 r;
    if (err.failed()) return r;
    if (!v.is_array() || v.size() < 2 || v.size() > 3) {
        err.set(ctx + std::string(": expected [x, y") + (want_z ? ", z]" : "]") + " (numbers, metres)");
        return r;
    }
    double c[3] = {0, 0, 0};
    for (std::size_t i = 0; i < v.size(); ++i) {
        if (!v[i].is_number() || !std::isfinite(v[i].get<double>())) {
            err.set(ctx + ": coordinates must be finite numbers");
            return r;
        }
        c[i] = v[i].get<double>();
    }
    return {c[0], c[1], want_z ? c[2] : 0.0};
}

Vec3 get_vec(const json& j, const char* key, bool want_z, const std::string& ctx, Err& err) {
    const json* v = need(j, key, ctx, err);
    return v ? read_vec(*v, want_z, ctx + "." + key, err) : Vec3{};
}

std::string lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

void parse_road(const json& j, std::size_t ri, LayoutRoad& road, Err& err) {
    const std::string ctx = idx("roads", ri);
    check_keys(j, {"type", "one_way", "points", "structures"}, ctx, err);
    road.type = get_string(j, "type", ctx, err, true);
    road.one_way = get_bool(j, "one_way", ctx, err, false);
    const json* pts = need(j, "points", ctx, err);
    if (!pts || err.failed()) return;
    if (!pts->is_array() || pts->size() < 2) {
        err.set(ctx + ": \"points\" needs at least 2 points");
        return;
    }
    for (std::size_t i = 0; i < pts->size() && !err.failed(); ++i) {
        const json& p = (*pts)[i];
        const std::string pctx = ctx + ".points[" + std::to_string(i) + "]";
        LayoutPoint lp;
        if (p.is_object()) {
            check_keys(p, {"p", "heading_deg"}, pctx, err);
            lp.pos = get_vec(p, "p", true, pctx, err);
            if (member(p, "heading_deg")) {
                lp.heading = wrap_angle(get_number(p, "heading_deg", pctx, err, true) * kDeg);
                if (i != 0 && i + 1 != pts->size())
                    err.set(pctx + ": a pinned heading is only allowed on the first and last point");
            }
        } else {
            lp.pos = read_vec(p, true, pctx, err);
        }
        road.points.push_back(lp);
    }
    if (err.failed()) return;
    // Consecutive points must be distinct: a zero-length segment cannot be fitted.
    for (std::size_t i = 1; i < road.points.size(); ++i)
        if ((road.points[i].pos.xy() - road.points[i - 1].pos.xy()).length() < 0.05) {
            err.set(ctx + ": points " + std::to_string(i - 1) + " and " + std::to_string(i) + " coincide");
            return;
        }
    if (const json* ss = member(j, "structures")) {
        if (!ss->is_array()) {
            err.set(ctx + ": \"structures\" must be an array");
            return;
        }
        for (std::size_t k = 0; k < ss->size() && !err.failed(); ++k) {
            const json& s = (*ss)[k];
            const std::string sctx = ctx + ".structures[" + std::to_string(k) + "]";
            check_keys(s, {"kind", "at", "length"}, sctx, err);
            LayoutStructure ls;
            const std::string kind = lower(get_string(s, "kind", sctx, err, true));
            if (kind == "bridge") ls.kind = StructureKind::Bridge;
            else if (kind == "tunnel") ls.kind = StructureKind::Tunnel;
            else err.set(sctx + ": \"kind\" must be \"bridge\" or \"tunnel\"");
            ls.at = get_vec(s, "at", false, sctx, err).xy();
            ls.length = get_number(s, "length", sctx, err, true);
            if (!err.failed() && ls.length <= 0) err.set(sctx + ": \"length\" must be positive");
            road.structures.push_back(ls);
        }
    }
}

void parse_doc(const json& root, LayoutDoc& doc, Err& err) {
    check_keys(root, {"format", "version", "name", "region", "driving_side", "roads", "roundabouts", "junctions", "cameras"},
               "layout", err);
    if (err.failed()) return;
    if (const json* f = member(root, "format"); f && (!f->is_string() || f->get<std::string>() != "cp-test-layout"))
        err.set("layout: \"format\" must be \"cp-test-layout\"");
    if (const json* v = member(root, "version"); v && (!v->is_number() || v->get<double>() != 1))
        err.set("layout: unsupported \"version\" (expected 1)");
    doc.name = get_string(root, "name", "layout", err, false);
    doc.region = get_string(root, "region", "layout", err, false);  // no default: the caller's pack decides
    if (member(root, "driving_side")) {
        const std::string side = lower(get_string(root, "driving_side", "layout", err, false));
        if (side == "left") doc.driving_side = DrivingSide::Left;
        else if (side == "right") doc.driving_side = DrivingSide::Right;
        else err.set("layout: \"driving_side\" must be \"left\" or \"right\"");
    }
    if (err.failed()) return;

    const json* roads = need(root, "roads", "layout", err);
    if (roads && !err.failed()) {
        if (!roads->is_array()) err.set("layout: \"roads\" must be an array");
        for (std::size_t i = 0; roads->is_array() && i < roads->size() && !err.failed(); ++i) {
            LayoutRoad r;
            parse_road((*roads)[i], i, r, err);
            doc.roads.push_back(std::move(r));
        }
    }
    if (const json* rs = member(root, "roundabouts"); rs && !err.failed()) {
        if (!rs->is_array()) err.set("layout: \"roundabouts\" must be an array");
        for (std::size_t i = 0; rs->is_array() && i < rs->size() && !err.failed(); ++i) {
            const json& j = (*rs)[i];
            const std::string ctx = idx("roundabouts", i);
            check_keys(j, {"centre", "radius", "type"}, ctx, err);
            LayoutRoundabout r;
            r.centre = get_vec(j, "centre", true, ctx, err);
            r.radius = get_number(j, "radius", ctx, err, true);
            if (!err.failed() && r.radius <= 0) err.set(ctx + ": \"radius\" must be positive");
            r.type = get_string(j, "type", ctx, err, false);
            doc.roundabouts.push_back(r);
        }
    }
    if (const json* js = member(root, "junctions"); js && !err.failed()) {
        check_keys(*js, {"corner_radii", "crossings"}, "junctions", err);
        if (const json* cr = member(*js, "corner_radii"); cr && !err.failed()) {
            if (!cr->is_array()) err.set("junctions: \"corner_radii\" must be an array");
            for (std::size_t i = 0; cr->is_array() && i < cr->size() && !err.failed(); ++i) {
                const json& j = (*cr)[i];
                const std::string ctx = idx("junctions.corner_radii", i);
                check_keys(j, {"node", "arm_a", "arm_b", "radius"}, ctx, err);
                LayoutCornerRadius c;
                c.node = get_vec(j, "node", false, ctx, err).xy();
                c.arm_a = get_vec(j, "arm_a", false, ctx, err).xy();
                c.arm_b = get_vec(j, "arm_b", false, ctx, err).xy();
                c.radius = get_number(j, "radius", ctx, err, true);
                if (!err.failed() && c.radius < 0) err.set(ctx + ": \"radius\" must not be negative");
                doc.corner_radii.push_back(c);
            }
        }
        if (const json* cs = member(*js, "crossings"); cs && !err.failed()) {
            if (!cs->is_array()) err.set("junctions: \"crossings\" must be an array");
            for (std::size_t i = 0; cs->is_array() && i < cs->size() && !err.failed(); ++i) {
                const json& j = (*cs)[i];
                const std::string ctx = idx("junctions.crossings", i);
                check_keys(j, {"node", "arm"}, ctx, err);
                LayoutCrossing c;
                c.node = get_vec(j, "node", false, ctx, err).xy();
                c.arm = get_vec(j, "arm", false, ctx, err).xy();
                doc.crossings.push_back(c);
            }
        }
    }
    if (const json* cams = member(root, "cameras"); cams && !err.failed()) {
        if (!cams->is_array()) err.set("layout: \"cameras\" must be an array");
        for (std::size_t i = 0; cams->is_array() && i < cams->size() && !err.failed(); ++i) {
            const json& j = (*cams)[i];
            const std::string ctx = idx("cameras", i);
            check_keys(j, {"name", "pos", "target", "fov_deg", "aspect", "photo"}, ctx, err);
            LayoutCamera c;
            c.name = get_string(j, "name", ctx, err, true);
            c.pos = get_vec(j, "pos", true, ctx, err);
            c.target = get_vec(j, "target", true, ctx, err);
            c.fov_deg = get_number(j, "fov_deg", ctx, err, false, 60);
            c.aspect = get_number(j, "aspect", ctx, err, false, 0);
            c.photo = get_string(j, "photo", ctx, err, false);
            if (const json* n = member(j, "notes"); n && n->is_string()) c.notes = n->get<std::string>();
            if (!err.failed() && (c.fov_deg <= 1 || c.fov_deg >= 170)) err.set(ctx + ": \"fov_deg\" must be in (1, 170)");
            if (!err.failed() && c.aspect < 0) err.set(ctx + ": \"aspect\" must not be negative");
            if (!err.failed() && (c.pos.xy() - c.target.xy()).length() + std::abs(c.pos.z - c.target.z) < 1e-6)
                err.set(ctx + ": camera position and target coincide");
            doc.cameras.push_back(std::move(c));
        }
    }
}

// ---------------------------------------------------------------- building

struct Builder {
    LoadedLayout& L;
    Err err;

    double z_tol() const { return L.region->plan_crossing_junction_dz_m; }

    // Edge whose centreline passes closest to p (xy) within max_dist, with the arc length of that point.
    EdgeId edge_near(Vec2 p, double* s_out, double max_dist, const std::string* type = nullptr) const {
        EdgeId best;
        double best_d = max_dist, best_s = 0;
        for (const auto& [id, e] : L.graph.edges()) {
            if (type && e.road_type != *type) continue;
            const double len = e.horizontal.length();
            for (double s = 0;; s += kSampleStep) {
                if (s > len) s = len;
                const double d = (edge_point(e, s).xy() - p).length();
                if (d < best_d) {
                    best_d = d;
                    best = id;
                    best_s = s;
                }
                if (s >= len) break;
            }
        }
        if (s_out) *s_out = best_s;
        return best;
    }

    NodeId node_near(Vec2 p, double tol) const {
        NodeId best;
        double best_d = tol;
        for (const auto& [id, n] : L.graph.nodes()) {
            const double d = (n.pos.xy() - p).length();
            if (d < best_d) {
                best_d = d;
                best = id;
            }
        }
        return best;
    }

    RoadEnd end_at(Vec3 p, std::optional<double> heading) const {
        RoadEnd r;
        r.pos = p;
        r.heading = heading;
        if (NodeId n = node_near(p.xy(), kSnapTol); n.valid()) {
            r.existing_node = n;
        } else {
            double s = 0;
            if (EdgeId e = edge_near(p.xy(), &s, kSnapTol); e.valid()) {
                r.split_edge = e;
                r.split_s = s;
            }
        }
        return r;
    }

    struct Crossing {
        double key = std::numeric_limits<double>::infinity();  // segment index + parameter along the new road
        std::size_t seg = 0;
        Vec3 pos;
        NodeId node;
        EdgeId edge;
        double s = 0;
    };

    // First place along `pts` where the new road must be split: an existing road crossed in plan at the same
    // level, or an existing node on the way. Stretches near the two ends are left to end_at().
    bool first_crossing(const std::vector<Vec3>& pts, Crossing& out) const {
        const double dz = z_tol() > 0 ? z_tol() : 1e9;
        out = {};
        auto near_end = [&](Vec3 p) {
            return (p.xy() - pts.front().xy()).length() < kSnapTol || (p.xy() - pts.back().xy()).length() < kSnapTol;
        };
        for (std::size_t i = 0; i + 1 < pts.size(); ++i) {
            const Vec3 a = pts[i], b = pts[i + 1];
            const Vec2 ab = b.xy() - a.xy();
            const double ab2 = ab.dot(ab);
            if (ab2 < 1e-12) continue;
            // existing nodes on the segment
            for (const auto& [nid, n] : L.graph.nodes()) {
                const double t = std::clamp((n.pos.xy() - a.xy()).dot(ab) / ab2, 0.0, 1.0);
                const Vec3 q{a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t};
                if ((q.xy() - n.pos.xy()).length() > kNodeTol || std::abs(q.z - n.pos.z) >= dz || near_end(n.pos)) continue;
                const double key = static_cast<double>(i) + t;
                if (key < out.key) {
                    out = {};
                    out.key = key;
                    out.seg = i;
                    out.pos = n.pos;
                    out.node = nid;
                }
            }
            // existing edge centrelines
            for (const auto& [eid, e] : L.graph.edges()) {
                const double len = e.horizontal.length();
                Vec3 p0 = edge_point(e, 0);
                double s0 = 0;
                for (double s = kSampleStep;; s += kSampleStep) {
                    if (s > len) s = len;
                    const Vec3 p1 = edge_point(e, s);
                    const Vec2 cd = p1.xy() - p0.xy();
                    const double den = ab.cross(cd);
                    if (std::abs(den) > 1e-12) {
                        const Vec2 ac = p0.xy() - a.xy();
                        const double t = ac.cross(cd) / den, u = ac.cross(ab) / den;
                        if (t > 0 && t < 1 && u >= 0 && u <= 1) {
                            const Vec3 q{a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t};
                            const double ez = p0.z + (p1.z - p0.z) * u;
                            const double key = static_cast<double>(i) + t;
                            if (std::abs(q.z - ez) < dz && !near_end(q) && key < out.key) {
                                out = {};
                                out.key = key;
                                out.seg = i;
                                out.pos = {q.x, q.y, ez};
                                out.edge = eid;
                                out.s = s0 + (s - s0) * u;
                            }
                        }
                    }
                    if (s >= len) break;
                    p0 = p1;
                    s0 = s;
                }
            }
        }
        if (out.key == std::numeric_limits<double>::infinity()) return false;
        // A crossing within 1 m of an existing node attaches to that node.
        if (!out.node.valid())
            if (NodeId n = node_near(out.pos.xy(), kSnapTol); n.valid()) {
                out.node = n;
                out.edge = {};
            }
        return true;
    }

    bool draw_piece(const LayoutRoad& road, RoadEnd a, std::vector<Vec3> interior, RoadEnd b, const std::string& ctx) {
        DrawRoadSpec spec;
        spec.road_type = road.type;
        spec.one_way = road.one_way;
        spec.start = a;
        spec.end = b;
        spec.interior = std::move(interior);
        if (!L.stack.execute(make_draw_road(std::move(spec)))) {
            err.set(ctx + ": the draw-road command failed (a stretch between two clicked points could not be fitted, or "
                          "the road type cannot be drawn like this)");
            return false;
        }
        return true;
    }

    void draw(const LayoutRoad& road, std::size_t ri) {
        const std::string ctx = idx("roads", ri);
        std::vector<Vec3> pts;
        for (const auto& p : road.points) pts.push_back(p.pos);
        const std::optional<double> head_a = road.points.front().heading, head_b = road.points.back().heading;
        RoadEnd start = end_at(pts.front(), head_a);
        for (int guard = 0; guard < 64; ++guard) {
            Crossing x;
            if (!first_crossing(pts, x)) {
                draw_piece(road, start, std::vector<Vec3>(pts.begin() + 1, pts.end() - 1), end_at(pts.back(), head_b), ctx);
                return;
            }
            std::vector<Vec3> interior;
            for (std::size_t k = 1; k <= x.seg; ++k)
                if ((pts[k].xy() - x.pos.xy()).length() >= kSnapTol) interior.push_back(pts[k]);
            RoadEnd end;
            end.pos = x.pos;
            if (x.node.valid()) {
                end.existing_node = x.node;
            } else {
                end.split_edge = x.edge;
                end.split_s = x.s;
            }
            if (!draw_piece(road, start, std::move(interior), end, ctx)) return;
            std::vector<Vec3> rest{x.pos};
            for (std::size_t k = x.seg + 1; k < pts.size(); ++k)
                if (k + 1 == pts.size() || (pts[k].xy() - x.pos.xy()).length() >= kSnapTol) rest.push_back(pts[k]);
            if (rest.size() < 2) {
                err.set(ctx + ": the road ends inside a junction it crosses");
                return;
            }
            pts = std::move(rest);
            start = end_at(pts.front(), std::nullopt);
        }
        err.set(ctx + ": more than 64 crossings");
    }

    void structure(const LayoutRoad& road, std::size_t ri, std::size_t si) {
        const LayoutStructure& st = road.structures[si];
        const std::string ctx = idx("roads", ri) + ".structures[" + std::to_string(si) + "]";
        double s = 0;
        const EdgeId e = edge_near(st.at, &s, 1.5, &road.type);
        if (!e.valid()) {
            err.set(ctx + ": no " + road.type + " edge passes within 1.5 m of (" + std::to_string(st.at.x) + ", " +
                    std::to_string(st.at.y) + ")");
            return;
        }
        const double len = L.graph.edge(e)->horizontal.length();
        const double s0 = std::max(0.0, s - st.length / 2), s1 = std::min(len, s + st.length / 2);
        if (!L.stack.execute(make_set_structure_span(e, s0, s1, st.kind)))
            err.set(ctx + ": the set-structure command failed (empty or out-of-range span)");
    }

    // Index of the arm whose road passes closest to p, or -1.
    int nearest_arm(const std::vector<JunctionArm>& arms, Vec2 p) const {
        int best = -1;
        double best_d = std::numeric_limits<double>::infinity();
        for (std::size_t k = 0; k < arms.size(); ++k) {
            const Edge* e = L.graph.edge(arms[k].edge);
            if (!e) continue;
            const double len = e->horizontal.length();
            for (double s = 0;; s += kSampleStep) {
                if (s > len) s = len;
                const double d = (edge_point(*e, s).xy() - p).length();
                if (d < best_d) {
                    best_d = d;
                    best = static_cast<int>(k);
                }
                if (s >= len) break;
            }
        }
        return best;
    }

    void corner(const LayoutCornerRadius& c, std::size_t i) {
        const std::string ctx = idx("junctions.corner_radii", i);
        const NodeId node = node_near(c.node, 1.5);
        if (!node.valid()) {
            err.set(ctx + ": no node within 1.5 m of the \"node\" point");
            return;
        }
        const auto arms = collect_arms(L.graph, node, *L.region);
        const int a = nearest_arm(arms, c.arm_a), b = nearest_arm(arms, c.arm_b);
        if (a < 0 || b < 0 || a == b) {
            err.set(ctx + ": \"arm_a\" and \"arm_b\" must lie on two different arms of the node");
            return;
        }
        const int n = static_cast<int>(arms.size());
        for (int k = 0; k < n; ++k) {
            const int k1 = (k + 1) % n;  // corner k lies between arms[k] and arms[k1] (CCW)
            const EdgeId ek = arms[static_cast<std::size_t>(k)].edge, ek1 = arms[static_cast<std::size_t>(k1)].edge;
            if ((k == a && k1 == b) || (k == b && k1 == a)) {
                L.overrides.corner_radius[CornerKey{node, ek1, ek}] = c.radius;  // same key resolve_corner_radii builds
                return;
            }
        }
        err.set(ctx + ": the two arms are not neighbours around the node");
    }

    void crossing(const LayoutCrossing& c, std::size_t i) {
        const std::string ctx = idx("junctions.crossings", i);
        const NodeId node = node_near(c.node, 1.5);
        if (!node.valid()) {
            err.set(ctx + ": no node within 1.5 m of the \"node\" point");
            return;
        }
        const auto arms = collect_arms(L.graph, node, *L.region);
        const int a = nearest_arm(arms, c.arm);
        if (a < 0) {
            err.set(ctx + ": the node has no arms");
            return;
        }
        L.crossing_arms.insert({node, arms[static_cast<std::size_t>(a)].edge});
    }
};

json vec_json(Vec3 v) { return json::array({v.x, v.y, v.z}); }
json vec2_json(Vec2 v) { return json::array({v.x, v.y}); }

}  // namespace

bool parse_layout(const std::string& json_text, LayoutDoc& out, std::string& error) {
    out = {};
    json root = json::parse(json_text, nullptr, false);
    if (root.is_discarded()) {
        error = "layout: not valid JSON";
        return false;
    }
    Err err;
    parse_doc(root, out, err);
    error = err.msg;
    return !err.failed();
}

std::string layout_to_json(const LayoutDoc& doc) {
    json root = json::object();
    root["format"] = "cp-test-layout";
    root["version"] = 1;
    root["name"] = doc.name;
    if (!doc.region.empty()) root["region"] = doc.region;
    if (doc.driving_side) root["driving_side"] = *doc.driving_side == DrivingSide::Left ? "left" : "right";
    json roads = json::array();
    for (const auto& r : doc.roads) {
        json jr = json::object();
        jr["type"] = r.type;
        jr["one_way"] = r.one_way;
        json pts = json::array();
        for (const auto& p : r.points) {
            if (p.heading) {
                json jp = json::object();
                jp["p"] = vec_json(p.pos);
                jp["heading_deg"] = *p.heading / kDeg;
                pts.push_back(jp);
            } else {
                pts.push_back(vec_json(p.pos));
            }
        }
        jr["points"] = pts;
        if (!r.structures.empty()) {
            json ss = json::array();
            for (const auto& s : r.structures) {
                json js = json::object();
                js["kind"] = s.kind == StructureKind::Tunnel ? "tunnel" : "bridge";
                js["at"] = vec2_json(s.at);
                js["length"] = s.length;
                ss.push_back(js);
            }
            jr["structures"] = ss;
        }
        roads.push_back(jr);
    }
    root["roads"] = roads;
    if (!doc.roundabouts.empty()) {
        json rs = json::array();
        for (const auto& r : doc.roundabouts) {
            json j = json::object();
            j["centre"] = vec_json(r.centre);
            j["radius"] = r.radius;
            if (!r.type.empty()) j["type"] = r.type;
            rs.push_back(j);
        }
        root["roundabouts"] = rs;
    }
    if (!doc.corner_radii.empty() || !doc.crossings.empty()) {
        json js = json::object();
        if (!doc.corner_radii.empty()) {
            json cr = json::array();
            for (const auto& c : doc.corner_radii) {
                json j = json::object();
                j["node"] = vec2_json(c.node);
                j["arm_a"] = vec2_json(c.arm_a);
                j["arm_b"] = vec2_json(c.arm_b);
                j["radius"] = c.radius;
                cr.push_back(j);
            }
            js["corner_radii"] = cr;
        }
        if (!doc.crossings.empty()) {
            json cs = json::array();
            for (const auto& c : doc.crossings) {
                json j = json::object();
                j["node"] = vec2_json(c.node);
                j["arm"] = vec2_json(c.arm);
                cs.push_back(j);
            }
            js["crossings"] = cs;
        }
        root["junctions"] = js;
    }
    if (!doc.cameras.empty()) {
        json cams = json::array();
        for (const auto& c : doc.cameras) {
            json j = json::object();
            j["name"] = c.name;
            j["pos"] = vec_json(c.pos);
            j["target"] = vec_json(c.target);
            j["fov_deg"] = c.fov_deg;
            if (c.aspect > 0) j["aspect"] = c.aspect;
            if (!c.photo.empty()) j["photo"] = c.photo;
            if (!c.notes.empty()) j["notes"] = c.notes;
            cams.push_back(j);
        }
        root["cameras"] = cams;
    }
    return root.dump(2) + "\n";
}

bool build_layout(const LayoutDoc& doc, std::shared_ptr<const RegionPack> region, LoadedLayout& out, std::string& error) {
    error.clear();
    if (!region) {
        error = "layout: no region pack";
        return false;
    }
    if (!doc.region.empty() && doc.region != region->id) {
        error = "layout: region \"" + doc.region + "\" does not match the loaded pack \"" + region->id + "\"";
        return false;
    }
    if (doc.driving_side && *doc.driving_side != region->driving_side) {
        error = "layout: \"driving_side\" does not match the region pack (one setting per save)";
        return false;
    }
    for (std::size_t i = 0; i < doc.roads.size(); ++i)
        if (!region->road_type(doc.roads[i].type)) {
            error = idx("roads", i) + ": unknown road type \"" + doc.roads[i].type + "\" in region \"" + region->id + "\"";
            return false;
        }
    for (std::size_t i = 0; i < doc.roundabouts.size(); ++i) {
        const std::string& t = doc.roundabouts[i].type;
        if (!t.empty() && !region->road_type(t)) {
            error = idx("roundabouts", i) + ": unknown road type \"" + t + "\"";
            return false;
        }
    }
    out.doc = doc;
    out.doc.region = region->id;  // resolved: stated and checked, or inherited from the pack
    out.doc.driving_side = region->driving_side;
    out.region = region;
    out.graph.set_region(region);
    Builder b{out, {}};
    for (std::size_t i = 0; i < doc.roads.size() && !b.err.failed(); ++i) b.draw(doc.roads[i], i);
    for (std::size_t i = 0; i < doc.roundabouts.size() && !b.err.failed(); ++i) {
        const LayoutRoundabout& r = doc.roundabouts[i];
        const std::string type = r.type.empty() ? default_ring_road_type(*region) : r.type;
        const std::string why = check_place_roundabout(out.graph, r.centre, r.radius, type, *region);
        if (!why.empty()) b.err.set(idx("roundabouts", i) + ": " + why);
        else if (!out.stack.execute(make_place_roundabout(r.centre, r.radius, type, *region)))
            b.err.set(idx("roundabouts", i) + ": the place-roundabout command failed");
    }
    for (std::size_t i = 0; i < doc.roads.size() && !b.err.failed(); ++i)
        for (std::size_t k = 0; k < doc.roads[i].structures.size() && !b.err.failed(); ++k)
            b.structure(doc.roads[i], i, k);
    for (std::size_t i = 0; i < doc.corner_radii.size() && !b.err.failed(); ++i) b.corner(doc.corner_radii[i], i);
    for (std::size_t i = 0; i < doc.crossings.size() && !b.err.failed(); ++i) b.crossing(doc.crossings[i], i);
    error = b.err.msg;
    return !b.err.failed();
}

std::unique_ptr<LoadedLayout> load_layout(const std::string& json_text, const std::filesystem::path& data_dir,
                                          std::string& error) {
    LayoutDoc doc;
    if (!parse_layout(json_text, doc, error)) return nullptr;
    if (doc.region.empty()) {
        error = "layout: \"region\" is required (no region pack was supplied to inherit)";
        return nullptr;
    }
    auto pack = load_region_pack_by_id(data_dir, doc.region);
    if (!pack) {
        error = "layout: " + pack.error;
        return nullptr;
    }
    auto out = std::make_unique<LoadedLayout>();
    if (!build_layout(doc, std::make_shared<const RegionPack>(*pack), *out, error)) return nullptr;
    return out;
}

std::unique_ptr<LoadedLayout> load_layout(const std::string& json_text, std::shared_ptr<const RegionPack> region,
                                          std::string& error) {
    LayoutDoc doc;
    if (!parse_layout(json_text, doc, error)) return nullptr;
    auto out = std::make_unique<LoadedLayout>();
    if (!build_layout(doc, std::move(region), *out, error)) return nullptr;
    return out;
}

std::unique_ptr<LoadedLayout> load_layout_file(const std::filesystem::path& file, const std::filesystem::path& data_dir,
                                               std::string& error) {
    std::ifstream in(file, std::ios::binary);
    if (!in) {
        error = "cannot read " + file.string();
        return nullptr;
    }
    std::stringstream ss;
    ss << in.rdbuf();
    auto out = load_layout(ss.str(), data_dir, error);
    if (!out) error = file.filename().string() + ": " + error;
    return out;
}

}  // namespace cp
