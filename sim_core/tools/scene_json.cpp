// JSON exporter, contract version 1. See prototypes/road-draw/fixtures/README or docs/junctions.md.
#include <cmath>
#include <cstdio>
#include <sstream>

#include "scene_fixture.h"

namespace cp::tools {
namespace {

struct W {
    std::string out;
    void raw(const std::string& s) { out += s; }
    void num(double v) {
        char b[48];
        std::snprintf(b, sizeof b, "%.3f", v == 0 ? 0.0 : v);  // avoid "-0.000"
        std::string s = b;
        if (s == "-0.000") s = "0.000";
        out += s;
    }
    void str(const std::string& s) {
        out += '"';
        for (char c : s) {
            if (c == '"' || c == '\\') out += '\\';
            out += c;
        }
        out += '"';
    }
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

std::string export_scene_json(const Scene& sc, const JunctionSet& js) {
    W w;
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
            char hb[32];
            std::snprintf(hb, sizeof hb, "%.5f", h);
            w.raw(hb);
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
        poly_set(w, j, j.islands);
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
    w.raw("]}\n");
    return w.out;
}

}  // namespace cp::tools
