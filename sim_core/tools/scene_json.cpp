// JSON exporter, contract version 1 (+ the "roundabouts" and "roundaboutProblems" arrays).
#include <charconv>
#include <cmath>
#include <cstdio>
#include <sstream>

#include "cp/junction_roundabout.h"
#include "scene_fixture.h"

namespace cp::tools {

std::string json_number(double v, int decimals, bool* finite) {
    if (!std::isfinite(v)) {
        if (finite) *finite = false;
        return "null";
    }
    char b[400];
    const auto r = std::to_chars(b, b + sizeof b, v == 0 ? 0.0 : v, std::chars_format::fixed, decimals);
    if (r.ec != std::errc()) {
        if (finite) *finite = false;
        return "null";
    }
    std::string s(b, r.ptr);
    // "-0.000" -> "0.000"
    if (!s.empty() && s[0] == '-' && s.find_first_of("123456789") == std::string::npos) s.erase(0, 1);
    return s;
}

std::string json_string(const std::string& s) {
    std::string out = "\"";
    for (unsigned char c : s) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            default:
                if (c < 0x20) {
                    char u[8];
                    std::snprintf(u, sizeof u, "\\u%04x", c);
                    out += u;
                } else {
                    out += static_cast<char>(c);
                }
        }
    }
    out += '"';
    return out;
}

namespace {

struct W {
    std::string out;
    int errors = 0;  // non-finite numbers written as null
    void raw(const std::string& s) { out += s; }
    void num(double v, int decimals = 3) {
        bool finite = true;
        out += json_number(v, decimals, &finite);
        if (!finite) ++errors;
    }
    void str(const std::string& s) { out += json_string(s); }
    void pt(double x, double y, double z) {
        out += '[';
        num(x);
        out += ',';
        num(y);
        out += ',';
        num(z);
        out += ']';
    }
};

double nearest_arm_z(const Junction& j, Vec2 p) {
    double best = 1e18, z = j.pos.z;
    for (const JunctionArm& a : j.arms)
        for (const EdgeSample& s : a.centreline) {
            const double d = (s.pos.xy() - p).length();
            if (d < best) {
                best = d;
                z = s.pos.z;
            }
        }
    return z;
}

void poly_set(W& w, const Junction& j, const PolygonSet& ps) {
    w.raw("[");
    for (std::size_t i = 0; i < ps.size(); ++i) {
        if (i) w.raw(",");
        w.raw("[");
        for (std::size_t k = 0; k < ps[i].size(); ++k) {
            if (k) w.raw(",");
            w.pt(ps[i][k].x, ps[i][k].y, nearest_arm_z(j, ps[i][k]));
        }
        w.raw("]");
    }
    w.raw("]");
}

// The island polygons shown on a roundabout's first ring node (older viewers read them from
// junction.islands); the same polygons are in the group-level "roundabouts" array.
void islands(W& w, const std::map<NodeId, PolygonSet>& by_node, const Junction& j) {
    const auto it = by_node.find(j.node);
    poly_set(w, j, it == by_node.end() ? PolygonSet{} : it->second);
}

const char* kind_name(const Junction& j) {
    if (j.kind == JunctionKind::Roundabout) return "roundabout";
    switch (j.cls) {
        case NodeClass::Merge: return "merge";
        case NodeClass::Diverge: return "diverge";
        default: return "junction";
    }
}

const char* turn_name(TurnKind k) {
    switch (k) {
        case TurnKind::Straight: return "straight";
        case TurnKind::Nearside: return "nearside";
        case TurnKind::Offside: return "offside";
        case TurnKind::UTurn: return "uturn";
    }
    return "straight";
}

}  // namespace

std::string export_scene_json(const Scene& sc, const JunctionSet& js, int* export_errors) {
    W w;
    // Group-level roundabout results (islands, splitters, give-way), derived from the graph.
    const RoundaboutScan scan = find_roundabouts(sc.graph, *sc.region);
    std::vector<RoundaboutLayout> layouts;
    std::map<NodeId, PolygonSet> compat;
    for (const JunctionGroup& g : scan.groups) {
        layouts.push_back(build_roundabout(g, sc.graph, *sc.region, sc.overrides));
        const RoundaboutLayout& lay = layouts.back();
        if (!lay.ok) continue;
        PolygonSet ps = lay.island;
        for (const SplitterIsland& s : lay.splitters) ps.push_back(s.polygon);
        compat[g.nodes[0]] = std::move(ps);
    }
    w.raw("{\"version\":1,\"region\":");
    w.str(sc.region->id);
    w.raw(",\"drivingSide\":");
    w.str(sc.region->driving_side == DrivingSide::Left ? "left" : "right");
    w.raw(",\"roads\":[");
    bool first = true;
    for (const auto& [id, e] : sc.graph.edges()) {
        if (!first) w.raw(",\n");
        first = false;
        w.raw("{\"id\":" + std::to_string(id.value) + ",\"type\":");
        w.str(e.road_type);
        w.raw(std::string(",\"oneWay\":") + (e.one_way ? "true" : "false") + ",\"samples\":[");
        const double L = e.horizontal.length();
        const int n = std::max(1, static_cast<int>(std::ceil(L / 2.0)));
        for (int i = 0; i <= n; ++i) {
            const double s = std::min(L, i * 2.0);
            const double s1 = std::min(L, s + 0.05), s0 = std::max(0.0, s - 0.05);
            const Vec3 p = edge_point(e, s), a = edge_point(e, s0), b = edge_point(e, s1);
            const double h = std::atan2(b.y - a.y, b.x - a.x);
            const char* st = "ground";
            for (const StructureSpan& sp : e.structure)
                if (s >= sp.s0 && s <= sp.s1) st = sp.kind == StructureKind::Bridge ? "bridge" : sp.kind == StructureKind::Tunnel ? "tunnel" : "ground";
            if (i) w.raw(",");
            w.raw("{\"s\":");
            w.num(s);
            w.raw(",\"x\":");
            w.num(p.x);
            w.raw(",\"y\":");
            w.num(p.y);
            w.raw(",\"z\":");
            w.num(p.z);
            w.raw(",\"h\":");
            w.num(h, 5);
            w.raw(std::string(",\"structure\":\"") + st + "\"}");
            if (s >= L) break;
        }
        w.raw("]}");
    }
    w.raw("],\n\"junctions\":[");
    first = true;
    for (const auto& [node, j] : js.junctions) {
        if (!first) w.raw(",\n");
        first = false;
        w.raw("{\"node\":" + std::to_string(node.value) + ",\"kind\":");
        w.str(kind_name(j));
        w.raw(",\"carriageway\":");
        poly_set(w, j, j.carriageway);
        w.raw(",\"pavements\":");
        poly_set(w, j, j.pavements);
        w.raw(",\"islands\":");
        islands(w, compat, j);
        w.raw(",\"gore\":");
        poly_set(w, j, j.gore);
        w.raw(",\"connectors\":[");
        for (std::size_t c = 0; c < j.connectors.size(); ++c) {
            const LaneConnector& k = j.connectors[c];
            if (c) w.raw(",");
            const JunctionArm& fa = j.arms[static_cast<std::size_t>(k.arm_from)];
            const JunctionArm& ta = j.arms[static_cast<std::size_t>(k.arm_to)];
            w.raw("{\"fromEdge\":" + std::to_string(fa.edge.value) +
                  ",\"fromLane\":" + std::to_string(fa.lanes[static_cast<std::size_t>(k.lane_from)].slot) +
                  ",\"toEdge\":" + std::to_string(ta.edge.value) +
                  ",\"toLane\":" + std::to_string(ta.lanes[static_cast<std::size_t>(k.lane_to)].slot) + ",\"turn\":");
            w.str(turn_name(k.kind));
            w.raw(std::string(",\"enabled\":") + (k.enabled ? "true" : "false") + ",\"path\":[");
            for (std::size_t q = 0; q < k.path.size(); ++q) {
                if (q) w.raw(",");
                w.pt(k.path[q].x, k.path[q].y, k.path[q].z);
            }
            w.raw("]}");
        }
        w.raw("],\"trims\":[");
        for (std::size_t a = 0; a < j.arms.size(); ++a) {
            if (a) w.raw(",");
            w.raw("{\"edge\":" + std::to_string(j.arms[a].edge.value) +
                  ",\"atA\":" + (j.arms[a].at_a ? "true" : "false") + ",\"dist\":");
            w.num(a < j.trims.size() ? j.trims[a] : 0.0);
            w.raw("}");
        }
        w.raw("]}");
    }
    w.raw("],\n\"roundabouts\":[");
    for (std::size_t gi = 0; gi < layouts.size(); ++gi) {
        const RoundaboutLayout& lay = layouts[gi];
        const JunctionGroup& g = lay.group;
        if (gi) w.raw(",\n");
        const auto zof = [&](Vec2 p, NodeId ring_node) {
            const Junction* jn = js.find(ring_node);
            return jn ? nearest_arm_z(*jn, p) : (sc.graph.node(ring_node) ? sc.graph.node(ring_node)->pos.z : 0.0);
        };
        const auto ring_poly = [&](const std::vector<Vec2>& poly, NodeId ring_node) {
            w.raw("[");
            for (std::size_t k = 0; k < poly.size(); ++k) {
                if (k) w.raw(",");
                w.pt(poly[k].x, poly[k].y, zof(poly[k], ring_node));
            }
            w.raw("]");
        };
        w.raw("{\"nodes\":[");
        for (std::size_t k = 0; k < g.nodes.size(); ++k) w.raw((k ? "," : "") + std::to_string(g.nodes[k].value));
        w.raw("],\"edges\":[");
        for (std::size_t k = 0; k < g.ring_edges.size(); ++k) w.raw((k ? "," : "") + std::to_string(g.ring_edges[k].value));
        w.raw("],\"centre\":");
        w.pt(g.centre.x, g.centre.y, zof(g.centre, g.nodes[0]));
        w.raw(",\"radius\":");
        w.num(g.radius);
        w.raw(std::string(",\"clockwise\":") + (g.clockwise ? "true" : "false") + ",\"ok\":" + (lay.ok ? "true" : "false") +
              ",\"error\":");
        if (lay.ok) w.raw("null");
        else w.str(lay.error);
        w.raw(",\"arms\":[");
        for (std::size_t k = 0; k < g.arms.size(); ++k) {
            if (k) w.raw(",");
            w.raw("{\"edge\":" + std::to_string(g.arms[k].edge.value) +
                  ",\"ringNode\":" + std::to_string(g.nodes[static_cast<std::size_t>(g.arms[k].ring_index)].value) +
                  ",\"entry\":" + (g.arms[k].entry ? "true" : "false") + ",\"exit\":" + (g.arms[k].exit ? "true" : "false") + "}");
        }
        w.raw("],\"island\":[");
        for (std::size_t k = 0; k < lay.island.size(); ++k) {
            if (k) w.raw(",");
            ring_poly(lay.island[k], g.nodes[0]);
        }
        w.raw("],\"splitters\":[");
        for (std::size_t k = 0; k < lay.splitters.size(); ++k) {
            if (k) w.raw(",");
            const RoundaboutArm& arm = g.arms[static_cast<std::size_t>(lay.splitters[k].arm)];
            w.raw("{\"edge\":" + std::to_string(arm.edge.value) + ",\"polygon\":");
            ring_poly(lay.splitters[k].polygon, g.nodes[static_cast<std::size_t>(arm.ring_index)]);
            w.raw("}");
        }
        w.raw("],\"giveWay\":[");
        for (std::size_t k = 0; k < lay.give_way.size(); ++k) {
            const GiveWay& gw = lay.give_way[k];
            if (k) w.raw(",");
            w.raw("{\"node\":" + std::to_string(gw.node.value) + ",\"connector\":" + std::to_string(gw.connector) + ",\"yieldsTo\":[");
            for (std::size_t q = 0; q < gw.yields_to.size(); ++q) w.raw((q ? "," : "") + std::to_string(gw.yields_to[q]));
            w.raw("]}");
        }
        w.raw("]}");
    }
    w.raw("],\n\"roundaboutProblems\":[");
    for (std::size_t pi = 0; pi < scan.problems.size(); ++pi) {
        const RoundaboutProblem& p = scan.problems[pi];
        if (pi) w.raw(",");
        w.raw("{\"nodes\":[");
        for (std::size_t k = 0; k < p.nodes.size(); ++k) w.raw((k ? "," : "") + std::to_string(p.nodes[k].value));
        w.raw("],\"edges\":[");
        for (std::size_t k = 0; k < p.edges.size(); ++k) w.raw((k ? "," : "") + std::to_string(p.edges[k].value));
        w.raw("],\"what\":");
        w.str(p.what);
        w.raw("}");
    }
    w.raw("],\n\"exportErrors\":" + std::to_string(w.errors) + "}\n");
    if (export_errors) *export_errors = w.errors;
    return w.out;
}

}  // namespace cp::tools
