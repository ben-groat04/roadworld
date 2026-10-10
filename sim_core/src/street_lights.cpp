// Street light placement (include/cp/street_lights.h). No exceptions: sim_core builds with -fno-exceptions.
#include "cp/street_lights.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <optional>
#include <set>

#include "cp/edge_sampling.h"
#include "cp/lanes.h"

namespace cp {

namespace {

constexpr double kInf = std::numeric_limits<double>::infinity();

// +1 when the nearside of travel is on the left (same convention as lanes.cpp).
double nearside_sign(DrivingSide side) { return side == DrivingSide::Left ? 1.0 : -1.0; }

// ---------------------------------------------------------------- planar helpers

double dist_point_seg(Vec2 p, Vec2 a, Vec2 b, Vec2* foot = nullptr) {
    const Vec2 ab = b - a;
    const double l2 = ab.dot(ab);
    const double t = l2 > 1e-18 ? std::clamp((p - a).dot(ab) / l2, 0.0, 1.0) : 0.0;
    const Vec2 f = a + ab * t;
    if (foot) *foot = f;
    return (p - f).length();
}

bool in_ring(Vec2 p, const std::vector<Vec2>& ring) {
    bool in = false;
    for (std::size_t i = 0, j = ring.size() - 1; i < ring.size(); j = i++) {
        const Vec2 a = ring[i], b = ring[j];
        if ((a.y > p.y) != (b.y > p.y) && p.x < (b.x - a.x) * (p.y - a.y) / (b.y - a.y) + a.x) in = !in;
    }
    return in;
}

double dist_ring(Vec2 p, const std::vector<Vec2>& ring) {
    double d = kInf;
    for (std::size_t i = 0, j = ring.size() - 1; i < ring.size(); j = i++) d = std::min(d, dist_point_seg(p, ring[j], ring[i]));
    return d;
}

// ---------------------------------------------------------------- centreline frame

struct Frame {
    Vec2 p;
    double h = 0;  // heading, rad CCW from +x
    double k = 0;  // curvature, + = turning left
};

Frame frame_at(const Edge& e, double s) {
    double acc = 0;
    for (std::size_t i = 0; i < e.horizontal.spans.size(); ++i) {
        const Clothoid& c = e.horizontal.spans[i];
        if (s <= acc + c.length || i + 1 == e.horizontal.spans.size()) {
            const double t = std::clamp(s - acc, 0.0, c.length);
            const Pose po = c.pose(t);
            return {po.pos, po.heading, c.curvature(t)};
        }
        acc += c.length;
    }
    return {};
}

StructureKind structure_at(const Edge& e, double s) {
    for (const StructureSpan& sp : e.structure)
        if (s >= sp.s0 && s <= sp.s1) return sp.kind;
    return StructureKind::Ground;
}

// ---------------------------------------------------------------- cross-section heights

enum class Role { Carriageway, Kerb, Raised, Verge };

Role role_of(const std::string& profile) {
    if (profile == "kerb") return Role::Kerb;
    if (profile == "raised") return Role::Raised;
    if (profile == "verge") return Role::Verge;
    return Role::Carriageway;
}

// The cross-section of one edge: surface heights across the road (same shape rules as render_mesh.cpp:
// carriageway crossfall, kerb upstand, footway and verge falls), where the footways are and where the
// carriageway ends. Offsets are lateral from the centreline, + = left of a -> b.
struct Section {
    struct Strip {
        double lo = 0, hi = 0;
        Role role = Role::Carriageway;
        double z_lo = 0, z_hi = 0;
    };
    std::vector<Strip> strips;  // left to right (descending offset)
    bool ok = false;
    bool camber = false;
    double cf = 0, ns = 1;
    double cw_lo = 0, cw_hi = 0;      // carriageway-role extent
    double ext_lo = 0, ext_hi = 0;    // whole cross-section
    bool foot_left = false, foot_right = false;
    double foot_hi = 0, foot_lo = 0;  // back of the outermost footway on the left (hi) / right (lo)

    double base(double d) const {
        d = std::clamp(d, cw_lo, cw_hi);
        return camber ? -cf * std::abs(d) : -cf * ns * d;
    }
    double z_at(double d) const {
        if (!ok) return 0;
        if (d >= ext_hi) return strips.front().z_hi;
        if (d <= ext_lo) return strips.back().z_lo;
        for (const Strip& s : strips) {
            if (d > s.hi || d < s.lo) continue;
            if (s.role == Role::Carriageway) return base(d);
            const double w = s.hi - s.lo;
            return w > 1e-12 ? s.z_lo + (s.z_hi - s.z_lo) * (d - s.lo) / w : s.z_lo;
        }
        return 0;
    }
};

Section build_section(const Edge& e, const RoadType& rt, const RegionPack& region, const StreetLightParams& P) {
    Section sec;
    const std::vector<Lane> lanes = instantiate_lanes(e, rt, region.driving_side);
    if (lanes.empty()) return sec;
    const double cf = rt.crossfall.value_or(P.default_crossfall);
    const double H = rt.kerb_upstand.value_or(P.default_kerb_upstand);
    const double fcf = rt.footway_crossfall.value_or(P.default_footway_crossfall);
    const double vcf = rt.verge_crossfall.value_or(P.default_verge_crossfall);

    double cw_lo = kInf, cw_hi = -kInf;
    for (const Lane& l : lanes) {
        Section::Strip s;
        const LaneType* lt = region.lane_type(l.lane_type);
        s.role = lt ? role_of(lt->profile) : Role::Carriageway;
        s.hi = l.offset + l.width / 2;
        s.lo = l.offset - l.width / 2;
        if (s.role == Role::Carriageway) {
            cw_lo = std::min(cw_lo, s.lo);
            cw_hi = std::max(cw_hi, s.hi);
        }
        sec.strips.push_back(s);
    }
    if (cw_lo > cw_hi) cw_lo = cw_hi = 0;
    sec.cw_lo = cw_lo;
    sec.cw_hi = cw_hi;
    sec.cf = cf;
    sec.ns = nearside_sign(region.driving_side);
    sec.camber = rt.two_way && !e.one_way && !rt.dual;
    const double mid = (cw_lo + cw_hi) / 2;

    // Sweep outward from the carriageway on each side, carrying the height of the previous strip.
    std::vector<std::size_t> left_side, right_side;
    for (std::size_t i = 0; i < sec.strips.size(); ++i)
        ((sec.strips[i].lo + sec.strips[i].hi) / 2 > mid ? left_side : right_side).push_back(i);
    std::sort(left_side.begin(), left_side.end(), [&](std::size_t a, std::size_t b) { return sec.strips[a].lo < sec.strips[b].lo; });
    std::sort(right_side.begin(), right_side.end(), [&](std::size_t a, std::size_t b) { return sec.strips[a].hi > sec.strips[b].hi; });
    const auto sweep = [&](const std::vector<std::size_t>& idx, bool left) {
        double z_prev = sec.base(left ? cw_hi : cw_lo);
        bool prev_cw = true;
        for (std::size_t i : idx) {
            Section::Strip& s = sec.strips[i];
            const double inner = left ? s.lo : s.hi, w = s.hi - s.lo;
            double zi = 0, zo = 0;
            switch (s.role) {
                case Role::Carriageway: zi = sec.base(inner); zo = sec.base(left ? s.hi : s.lo); break;
                case Role::Kerb: zi = zo = z_prev + (prev_cw ? H : 0.0); break;
                case Role::Raised: zi = z_prev + (prev_cw ? H : 0.0); zo = zi + fcf * w; break;
                case Role::Verge: zi = z_prev; zo = zi - vcf * w; break;
            }
            prev_cw = s.role == Role::Carriageway;
            z_prev = zo;
            s.z_hi = left ? zo : zi;
            s.z_lo = left ? zi : zo;
            if (s.role == Role::Raised) {
                if (left) {
                    sec.foot_hi = sec.foot_left ? std::max(sec.foot_hi, s.hi) : s.hi;
                    sec.foot_left = true;
                } else {
                    sec.foot_lo = sec.foot_right ? std::min(sec.foot_lo, s.lo) : s.lo;
                    sec.foot_right = true;
                }
            }
        }
    };
    sweep(left_side, true);
    sweep(right_side, false);
    std::sort(sec.strips.begin(), sec.strips.end(), [](const Section::Strip& a, const Section::Strip& b) { return a.lo > b.lo; });
    sec.ext_hi = sec.strips.front().hi;
    sec.ext_lo = sec.strips.back().lo;
    sec.ok = true;
    return sec;
}

// Lateral offset of a column on one side of the road.
double side_offset(const Section& sec, const StreetLightRules& r, bool left, bool parapet, const StreetLightParams& P) {
    if (parapet) return left ? sec.ext_hi - P.parapet_inset : sec.ext_lo + P.parapet_inset;
    if (left) {
        if (sec.foot_left) return sec.foot_hi - r.footway_inset;
        const double room = sec.ext_hi - sec.cw_hi;
        return room > 0.6 ? std::min(sec.cw_hi + r.setback, sec.ext_hi - 0.2) : sec.cw_hi + r.setback;
    }
    if (sec.foot_right) return sec.foot_lo + r.footway_inset;
    const double room = sec.cw_lo - sec.ext_lo;
    return room > 0.6 ? std::max(sec.cw_lo - r.setback, sec.ext_lo + 0.2) : sec.cw_lo - r.setback;
}

// ---------------------------------------------------------------- obstacles near junctions

struct Obstacles {
    struct Item {
        const Junction* j = nullptr;
        double x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    };
    struct Splitter {
        const std::vector<Vec2>* poly = nullptr;
        double x0 = kInf, y0 = kInf, x1 = -kInf, y1 = -kInf;
    };
    std::vector<Item> items;
    std::vector<Splitter> splitters;

    void build(const JunctionSet& set, const std::vector<RoundaboutLayout>* layouts) {
        for (const auto& [id, j] : set.junctions) {
            double reach = 15;
            for (double t : j.trims) reach = std::max(reach, t + 15);
            items.push_back({&j, j.pos.x - reach, j.pos.y - reach, j.pos.x + reach, j.pos.y + reach});
        }
        if (!layouts) return;
        for (const RoundaboutLayout& l : *layouts) {
            if (!l.ok) continue;
            for (const SplitterIsland& sp : l.splitters) {
                Splitter s;
                s.poly = &sp.polygon;
                for (Vec2 v : sp.polygon) {
                    s.x0 = std::min(s.x0, v.x);
                    s.y0 = std::min(s.y0, v.y);
                    s.x1 = std::max(s.x1, v.x);
                    s.y1 = std::max(s.y1, v.y);
                }
                if (!sp.polygon.empty()) splitters.push_back(s);
            }
        }
    }

    // True when a column at p would sit inside a junction carriageway or too near a corner kerb, a
    // crossing or a splitter island.
    bool blocked(Vec2 p, const StreetLightParams& P) const {
        for (const Item& it : items) {
            if (p.x < it.x0 || p.x > it.x1 || p.y < it.y0 || p.y > it.y1) continue;
            const Junction& j = *it.j;
            bool inside = false;
            double dmin = kInf;
            for (const auto& ring : j.carriageway) {
                if (ring.size() < 3) continue;
                if (in_ring(p, ring)) inside = !inside;
                dmin = std::min(dmin, dist_ring(p, ring));
            }
            if (inside || dmin < P.carriageway_clearance) return true;
            for (const CornerCurve& c : j.corners)
                for (std::size_t i = 1; i < c.kerb.size(); ++i)
                    if (dist_point_seg(p, c.kerb[i - 1].xy(), c.kerb[i].xy()) < P.corner_clearance) return true;
            for (const CrossingSpot& c : j.crossings) {
                const Vec2 d{std::cos(c.heading) * c.length / 2, std::sin(c.heading) * c.length / 2};
                const Vec2 m = c.centre.xy();
                if (dist_point_seg(p, m - d, m + d) < P.crossing_half_width + P.crossing_clearance) return true;
            }
        }
        for (const Splitter& s : splitters) {
            const double m = P.island_clearance;
            if (p.x < s.x0 - m || p.x > s.x1 + m || p.y < s.y0 - m || p.y > s.y1 + m) continue;
            if (s.poly->size() >= 3 && (in_ring(p, *s.poly) || dist_ring(p, *s.poly) < m)) return true;
        }
        return false;
    }
};

// ---------------------------------------------------------------- generation context

struct DualEdge {
    const Edge* e = nullptr;
    std::vector<EdgeSample> pts;
    double x0 = kInf, y0 = kInf, x1 = -kInf, y1 = -kInf;
};

struct Partner {
    EdgeId edge;
    Vec2 point;
};

// One edge piece of a run, in run direction. fwd: the run travels the edge a -> b.
struct Piece {
    const Edge* e = nullptr;
    bool fwd = true;
    double off = 0, len = 0;
};

struct Run {
    std::vector<Piece> pieces;
    double length = 0;
    bool closed = false;
};

struct Ctx {
    const RoadGraph& graph;
    const RegionPack& region;
    const JunctionSet& js;
    const StreetLightParams& P;
    const std::vector<RoundaboutLayout>* layouts = nullptr;
    std::set<EdgeId> ring_edges;  // covered by ok roundabout layouts
    Obstacles obs;
    std::map<EdgeId, Section> sections;
    std::map<EdgeId, std::vector<StreetLight>> pattern_cache;
    std::set<EdgeId> pattern_done;
    std::optional<std::vector<DualEdge>> duals;

    Ctx(const RoadGraph& g, const RegionPack& r, const JunctionSet& j, const std::vector<RoundaboutLayout>* l,
        const StreetLightParams& p)
        : graph(g), region(r), js(j), P(p), layouts(l) {
        obs.build(js, layouts);
        if (layouts)
            for (const RoundaboutLayout& lay : *layouts)
                if (lay.ok)
                    for (EdgeId id : lay.group.ring_edges) ring_edges.insert(id);
    }

    // Rules of an edge's road type, or null when the edge is unlit / unknown.
    const StreetLightRules* rules(const Edge& e) const {
        const RoadType* rt = region.road_type(e.road_type);
        return rt && rt->street_lights.lit ? &rt->street_lights : nullptr;
    }
    const Section& section(const Edge& e) {
        auto it = sections.find(e.id);
        if (it != sections.end()) return it->second;
        const RoadType* rt = region.road_type(e.road_type);
        Section s;
        if (rt) s = build_section(e, *rt, region, P);
        return sections.emplace(e.id, std::move(s)).first->second;
    }
    const std::vector<DualEdge>& dual_edges() {
        if (duals) return *duals;
        duals.emplace();
        for (const auto& [id, e] : graph.edges()) {
            const StreetLightRules* r = rules(e);
            if (!r || r->arrangement != LightArrangement::CentralReservation) continue;
            DualEdge d;
            d.e = &e;
            d.pts = sample_centreline(e);
            for (const EdgeSample& s : d.pts) {
                d.x0 = std::min(d.x0, s.pos.x);
                d.y0 = std::min(d.y0, s.pos.y);
                d.x1 = std::max(d.x1, s.pos.x);
                d.y1 = std::max(d.y1, s.pos.y);
            }
            duals->push_back(std::move(d));
        }
        return *duals;
    }
};

// ---------------------------------------------------------------- one column

StreetLight base_light(const Edge& e, const StreetLightRules& r, double s, LightOrigin origin) {
    StreetLight l;
    l.mounting_height = r.mounting_height;
    l.outreach = r.outreach;
    l.style = r.style;
    l.edge = e.id;
    l.s = s;
    l.origin = origin;
    return l;
}

// A column at arc length s of edge e on the planar left or right of a -> b. False when it cannot stand
// there (tunnel, folded offset, blocked by junction geometry).
bool make_light(Ctx& c, const Edge& e, const StreetLightRules& r, double s, bool left, LightOrigin origin, NodeId node,
                StreetLight& out) {
    const StructureKind kind = structure_at(e, s);
    if (kind == StructureKind::Tunnel) return false;
    const Section& sec = c.section(e);
    if (!sec.ok) return false;
    const double d = side_offset(sec, r, left, kind == StructureKind::Bridge, c.P);
    const Frame f = frame_at(e, s);
    if (std::abs(d * f.k) >= c.P.fold_guard) return false;
    const Vec2 n{-std::sin(f.h), std::cos(f.h)};
    const Vec2 p = f.p + n * d;
    if (c.obs.blocked(p, c.P)) return false;
    out = base_light(e, r, s, origin);
    out.node = node;
    out.position = {p.x, p.y, e.vertical.z(s) + sec.z_at(d)};
    out.heading = wrap_angle(f.h + (left ? -kPi / 2 : kPi / 2));
    out.side = left ? LightSide::Left : LightSide::Right;
    out.on_structure = kind == StructureKind::Bridge;
    return true;
}

// ---------------------------------------------------------------- runs

// The other edge of a 2-arm Joint node when the run continues through it.
const Edge* continue_through(const Ctx& c, NodeId n, const Edge& from) {
    const std::vector<EdgeId> at = c.graph.edges_at(n);
    if (at.size() != 2) return nullptr;
    const EdgeId other_id = at[0] == from.id ? at[1] : at[0];
    if (other_id == from.id) return nullptr;
    const Edge* o = c.graph.edge(other_id);
    if (!o || o->a == o->b || o->road_type != from.road_type || o->one_way != from.one_way) return nullptr;
    if (c.ring_edges.count(o->id) != 0) return nullptr;
    return o;
}

void reverse_run(Run& r) {
    std::reverse(r.pieces.begin(), r.pieces.end());
    for (Piece& p : r.pieces) p.fwd = !p.fwd;
    double off = 0;
    for (Piece& p : r.pieces) {
        p.off = off;
        off += p.len;
    }
}

NodeId piece_start(const Piece& p) { return p.fwd ? p.e->a : p.e->b; }
NodeId piece_end(const Piece& p) { return p.fwd ? p.e->b : p.e->a; }

Run build_run(const Ctx& c, const Edge& e) {
    Run run;
    std::vector<Piece> before, after;  // before: nearest first (walking toward the start)
    std::set<EdgeId> seen{e.id};
    const auto walk = [&](NodeId n, const Edge* cur, bool toward_end, std::vector<Piece>& out) {
        for (;;) {
            const Edge* o = continue_through(c, n, *cur);
            if (!o) return;
            if (o->id == e.id) {
                run.closed = true;
                return;
            }
            if (!seen.insert(o->id).second) return;
            Piece p;
            p.e = o;
            // Walking toward the end of the run the edge is entered at n; toward the start it is left at n.
            p.fwd = toward_end ? o->a == n : o->b == n;
            p.len = o->horizontal.length();
            out.push_back(p);
            n = toward_end ? piece_end(p) : piece_start(p);
            cur = o;
        }
    };
    walk(e.b, &e, true, after);
    if (!run.closed) walk(e.a, &e, false, before);
    std::reverse(before.begin(), before.end());
    Piece mid;
    mid.e = &e;
    mid.fwd = true;
    mid.len = e.horizontal.length();
    run.pieces = before;
    run.pieces.push_back(mid);
    run.pieces.insert(run.pieces.end(), after.begin(), after.end());

    // Canonical direction so every edge of the run computes the same pattern.
    const auto min_edge_first = [&]() {
        std::size_t best = 0;
        for (std::size_t i = 1; i < run.pieces.size(); ++i)
            if (run.pieces[i].e->id < run.pieces[best].e->id) best = i;
        std::rotate(run.pieces.begin(), run.pieces.begin() + static_cast<std::ptrdiff_t>(best), run.pieces.end());
    };
    double off = 0;
    for (Piece& p : run.pieces) {
        p.off = off;
        off += p.len;
    }
    run.length = off;
    if (run.closed) {
        min_edge_first();
        if (!run.pieces.front().fwd) {
            reverse_run(run);
            min_edge_first();
        }
        off = 0;
        for (Piece& p : run.pieces) {
            p.off = off;
            off += p.len;
        }
    } else {
        const NodeId s = piece_start(run.pieces.front()), t = piece_end(run.pieces.back());
        const bool flip = s.value != t.value ? s.value > t.value : run.pieces.front().e->id > run.pieces.back().e->id;
        if (flip) reverse_run(run);
    }
    return run;
}

// Where a run starts being lit at one of its ends: past the junction trim, its crossings and any
// roundabout splitter island of that arm.
double arm_clear(const Ctx& c, const Junction& j, std::size_t arm_idx) {
    const JunctionArm& arm = j.arms[arm_idx];
    double s = (arm_idx < j.trims.size() ? j.trims[arm_idx] : 0.0) + c.P.junction_offset;
    for (const CrossingSpot& cs : j.crossings)
        if (cs.arm == static_cast<int>(arm_idx)) s = std::max(s, cs.s + c.P.crossing_half_width + c.P.crossing_clearance);
    if (c.layouts) {
        const Vec2 dir{std::cos(arm.heading_out), std::sin(arm.heading_out)};
        for (const RoundaboutLayout& lay : *c.layouts) {
            if (!lay.ok) continue;
            for (const SplitterIsland& sp : lay.splitters) {
                if (sp.arm < 0 || static_cast<std::size_t>(sp.arm) >= lay.group.arms.size()) continue;
                const RoundaboutArm& ra = lay.group.arms[static_cast<std::size_t>(sp.arm)];
                if (ra.edge != arm.edge) continue;
                if (ra.ring_index < 0 || static_cast<std::size_t>(ra.ring_index) >= lay.group.nodes.size() ||
                    lay.group.nodes[static_cast<std::size_t>(ra.ring_index)] != j.node)
                    continue;
                for (Vec2 v : sp.polygon) s = std::max(s, (v - j.pos.xy()).dot(dir) + c.P.island_clearance);
            }
        }
    }
    return s;
}

const Junction* real_junction(const Ctx& c, NodeId n) {
    const Junction* j = c.js.find(n);
    return j && j->cls != NodeClass::End && j->cls != NodeClass::Joint && j->trims.size() == j->arms.size() ? j : nullptr;
}

// Clear length at the end of a piece (the node it enters / leaves), 0 when there is no junction.
double end_clear(const Ctx& c, const Piece& p, bool at_run_start) {
    const bool at_a = at_run_start ? p.fwd : !p.fwd;
    const NodeId n = at_a ? p.e->a : p.e->b;
    const Junction* j = real_junction(c, n);
    if (!j) return 0;
    for (std::size_t i = 0; i < j->arms.size(); ++i)
        if (j->arms[i].edge == p.e->id && j->arms[i].at_a == at_a) return arm_clear(c, *j, i);
    return 0;
}

// ---------------------------------------------------------------- median partner

std::optional<Partner> find_partner(Ctx& c, const Edge& owner, double s) {
    const StreetLightRules* r = c.rules(owner);
    const RoadType* rt = c.region.road_type(owner.road_type);
    if (!r || !rt) return std::nullopt;
    const Section& so = c.section(owner);
    if (!so.ok) return std::nullopt;
    const Frame f = frame_at(owner, s);
    const Vec2 odir{std::cos(f.h), std::sin(f.h)};
    const Vec2 nl{-odir.y, odir.x};
    const double offside_sign = -nearside_sign(c.region.driving_side);
    const double cos_tol = std::cos(c.P.median_heading_tol);
    std::optional<Partner> best;
    double best_d = kInf;
    for (const DualEdge& d : c.dual_edges()) {
        if (d.e->id == owner.id) continue;
        const Section& sp = c.section(*d.e);
        if (!sp.ok) continue;
        const double expected = ((so.ext_hi - so.ext_lo) + (sp.ext_hi - sp.ext_lo)) / 2;
        const double lo = 0.8 * expected, hi = expected + rt->median_gap + c.P.median_slack;
        if (f.p.x < d.x0 - hi || f.p.x > d.x1 + hi || f.p.y < d.y0 - hi || f.p.y > d.y1 + hi) continue;
        for (std::size_t i = 1; i < d.pts.size(); ++i) {
            const Vec2 a = d.pts[i - 1].pos.xy(), b = d.pts[i].pos.xy();
            Vec2 foot;
            const double dist = dist_point_seg(f.p, a, b, &foot);
            if (dist < lo || dist > hi || dist >= best_d) continue;
            const Vec2 sd = b - a;
            const double sl = sd.length();
            if (sl < 1e-9) continue;
            if (sd.dot(odir) / sl > -cos_tol) continue;  // must run the other way
            if ((foot - f.p).dot(nl) * offside_sign <= 0) continue;  // must lie on the offside
            best_d = dist;
            best = Partner{d.e->id, foot};
        }
    }
    return best;
}

// ---------------------------------------------------------------- edge patterns

void emit_pattern_column(Ctx& c, const Piece& p, const StreetLightRules& r, double s_run, int index,
                         std::vector<StreetLight>& out) {
    const Edge& e = *p.e;
    const double s_local = p.fwd ? s_run - p.off : p.len - (s_run - p.off);
    const double ns = nearside_sign(c.region.driving_side);
    // Run frame -> edge frame: the planar left of the run is the edge's left when the run travels a -> b.
    const auto edge_left = [&](bool run_left) { return p.fwd ? run_left : !run_left; };
    const bool near_left = ns > 0;  // nearside is planar left of the run direction

    const auto single = [&](bool run_left, bool bend_rule) {
        bool left = edge_left(run_left);
        if (bend_rule) {
            const double k = frame_at(e, s_local).k;
            if (std::abs(k) * c.P.bend_outside_radius >= 1.0) left = k < 0;  // outside of the bend
        }
        StreetLight l;
        if (make_light(c, e, r, s_local, left, LightOrigin::EdgePattern, NodeId{}, l)) out.push_back(l);
    };

    switch (r.arrangement) {
        case LightArrangement::SingleSide: single(near_left, true); break;
        case LightArrangement::Staggered: single(index % 2 == 0 ? near_left : !near_left, false); break;
        case LightArrangement::Opposite:
            single(true, false);
            single(false, false);
            break;
        case LightArrangement::CentralReservation: {
            if (structure_at(e, s_local) == StructureKind::Tunnel) break;
            const std::optional<Partner> pt = find_partner(c, e, s_local);
            if (!pt) {
                single(near_left, true);  // no opposite carriageway here: verge-side
                break;
            }
            if (pt->edge < e.id) break;  // the lower edge id of the pair emits
            const Frame f = frame_at(e, s_local);
            const Section& sec = c.section(e);
            const Vec2 m = (f.p + pt->point) * 0.5;
            if (c.obs.blocked(m, c.P)) break;
            StreetLight l = base_light(e, r, s_local, LightOrigin::EdgePattern);
            const double off_d = nearside_sign(c.region.driving_side) > 0 ? sec.cw_lo : sec.cw_hi;  // offside carriageway edge
            l.position = {m.x, m.y, e.vertical.z(s_local) + sec.z_at(off_d)};
            l.heading = std::atan2(f.p.y - m.y, f.p.x - m.x);  // toward this edge's carriageway
            l.side = LightSide::Median;
            l.twin_arm = true;
            l.partner_edge = pt->edge;
            l.on_structure = structure_at(e, s_local) == StructureKind::Bridge;
            out.push_back(l);
            break;
        }
    }
}

// Computes every column of the run `e` belongs to and files them under their edges in the cache.
void compute_run(Ctx& c, const Edge& e) {
    const StreetLightRules* r = c.rules(e);
    if (!r || c.ring_edges.count(e.id) != 0 || !c.section(e).ok) {
        c.pattern_done.insert(e.id);
        return;
    }
    const Run run = build_run(c, e);
    for (const Piece& p : run.pieces) c.pattern_done.insert(p.e->id);
    const double cs = run.closed ? 0.0 : end_clear(c, run.pieces.front(), true);
    const double ce = run.closed ? 0.0 : end_clear(c, run.pieces.back(), false);
    const LightPattern pat = plan_light_pattern(run.length - cs - ce, r->spacing, c.P.spacing_tolerance);
    for (int i = 0; i < pat.count; ++i) {
        const double s_run = cs + (i + 0.5) * pat.cell;
        std::size_t pi = 0;
        while (pi + 1 < run.pieces.size() && s_run >= run.pieces[pi].off + run.pieces[pi].len) ++pi;
        std::vector<StreetLight> cols;
        emit_pattern_column(c, run.pieces[pi], *r, s_run, i, cols);
        for (StreetLight& l : cols) c.pattern_cache[l.edge].push_back(std::move(l));
    }
}

std::vector<StreetLight> edge_lights(Ctx& c, const Edge& e) {
    if (!c.pattern_done.count(e.id)) compute_run(c, e);
    auto it = c.pattern_cache.find(e.id);
    if (it == c.pattern_cache.end()) return {};
    std::vector<StreetLight> out = it->second;
    std::stable_sort(out.begin(), out.end(), [](const StreetLight& a, const StreetLight& b) { return a.s < b.s; });
    return out;
}

// ---------------------------------------------------------------- junction corners

std::vector<StreetLight> junction_lights(Ctx& c, NodeId node) {
    std::vector<StreetLight> out;
    const Junction* j = real_junction(c, node);
    if (!j) return out;
    const std::size_t n = j->arms.size();
    if (j->corners.size() != n) return out;

    // Tries one arm: a column on its left (looking outward) or right side, past the clear zone.
    const auto try_arm = [&](std::size_t ai, bool outward_left) {
        const JunctionArm& arm = j->arms[ai];
        const Edge* e = c.graph.edge(arm.edge);
        if (!e) return false;
        const RoadType* rt = c.region.road_type(e->road_type);
        const StreetLightRules* r = c.rules(*e);
        if (!r || !rt || rt->roundabout_ring || c.ring_edges.count(e->id) != 0) return false;
        const double len = e->horizontal.length();
        const double s0 = arm_clear(c, *j, ai);
        const bool left = arm.at_a ? outward_left : !outward_left;  // outward frame -> a -> b frame
        for (int step = 0; step <= c.P.max_shift_steps; ++step) {
            const double s_out = s0 + step;
            if (s_out > arm.edge_length - 1 || s_out > len - 1) break;
            const double s = arm.at_a ? s_out : len - s_out;
            StreetLight l;
            if (make_light(c, *e, *r, s, left, LightOrigin::JunctionCorner, node, l)) {
                out.push_back(l);
                return true;
            }
        }
        return false;
    };

    for (std::size_t k = 0; k < n; ++k) {
        const CornerCurve& corner = j->corners[k];
        if (corner.angle >= kPi - 0.05 || corner.angle < c.P.min_corner_angle) continue;
        if (!try_arm(k, true)) try_arm((k + 1) % n, false);
    }
    return out;
}

// ---------------------------------------------------------------- roundabouts

std::vector<StreetLight> roundabout_lights(Ctx& c, const RoundaboutLayout& lay) {
    std::vector<StreetLight> out;
    const JunctionGroup& g = lay.group;
    if (!lay.ok || g.ring_edges.empty()) return out;
    const Edge* first = c.graph.edge(g.ring_edges.front());
    if (!first) return out;
    const StreetLightRules* r = c.rules(*first);
    if (!r) return out;

    // Outside path: the offset curve at the back of the outer footway; u runs round it from nodes[0].
    struct Leg {
        const Edge* e = nullptr;
        std::vector<double> u, s;  // cumulative plan length and centreline arc length per sample
    };
    std::vector<Leg> legs;
    double total = 0;
    for (EdgeId id : g.ring_edges) {
        const Edge* e = c.graph.edge(id);
        if (!e) return out;
        const Section& sec = c.section(*e);
        if (!sec.ok) return out;
        const double d = side_offset(sec, *r, g.clockwise, false, c.P);
        OffsetPolyline op = sample_offset(*e, d);
        if (op.has_fold()) op = sample_offset(*e, 0.0);
        Leg leg;
        leg.e = e;
        double acc = 0;
        for (std::size_t i = 0; i < op.samples.size(); ++i) {
            if (i > 0) acc += (op.samples[i].pos.xy() - op.samples[i - 1].pos.xy()).length();
            leg.u.push_back(total + acc);
            leg.s.push_back(op.samples[i].s);
        }
        total += acc;
        legs.push_back(std::move(leg));
    }
    const LightPattern pat = plan_light_pattern(total, r->spacing, c.P.spacing_tolerance);
    std::size_t li = 0;
    for (int i = 0; i < pat.count; ++i) {
        const double u = (i + 0.5) * pat.cell;
        while (li + 1 < legs.size() && u > legs[li].u.back()) ++li;
        const Leg& leg = legs[li];
        const auto it = std::lower_bound(leg.u.begin(), leg.u.end(), u);
        const std::size_t hi = std::clamp<std::size_t>(static_cast<std::size_t>(it - leg.u.begin()), 1, leg.u.size() - 1);
        const double span = leg.u[hi] - leg.u[hi - 1];
        const double t = span > 1e-12 ? std::clamp((u - leg.u[hi - 1]) / span, 0.0, 1.0) : 0.0;
        const double s = leg.s[hi - 1] + (leg.s[hi] - leg.s[hi - 1]) * t;
        StreetLight l;
        if (make_light(c, *leg.e, *r, s, g.clockwise, LightOrigin::RoundaboutRing, NodeId{}, l)) out.push_back(l);
    }

    // Central island column.
    if (r->island_min_radius > 0 && !lay.island.empty() && lay.island.front().size() >= 3) {
        double radius = kInf;
        for (Vec2 v : lay.island.front()) radius = std::min(radius, (v - g.centre).length());
        if (radius >= r->island_min_radius) {
            double z = 0;
            int count = 0;
            for (NodeId nid : g.nodes)
                if (const Node* nd = c.graph.node(nid)) {
                    z += nd->pos.z;
                    ++count;
                }
            const RoadType* rt = c.region.road_type(first->road_type);
            StreetLight l;
            l.position = {g.centre.x, g.centre.y, (count ? z / count : 0.0) + (rt ? rt->kerb_upstand.value_or(c.P.default_kerb_upstand) : 0.0)};
            l.mounting_height = r->mounting_height;
            l.outreach = r->outreach;
            l.style = r->style;
            l.side = LightSide::Median;
            l.origin = LightOrigin::RoundaboutIsland;
            out.push_back(l);
        }
    }
    return out;
}

void append_nonempty(std::map<EdgeId, std::vector<StreetLight>>& m, EdgeId id, std::vector<StreetLight>&& v) {
    if (!v.empty()) m[id] = std::move(v);
}

template <class K>
void erase_if_missing(std::map<K, std::vector<StreetLight>>& m, const std::set<K>& keep) {
    for (auto it = m.begin(); it != m.end();) it = keep.count(it->first) ? std::next(it) : m.erase(it);
}

}  // namespace

// ================================================================ public

LightPattern plan_light_pattern(double available_length, double spacing, double tolerance) {
    LightPattern r;
    if (!(spacing > 0) || !(available_length > 0)) return r;
    const double x = available_length / spacing;
    if (x < 0.5) return r;
    const int lo = static_cast<int>(std::floor(x));
    const auto dev = [&](int n) { return std::abs(available_length / n / spacing - 1.0); };
    int best = lo + 1;
    if (lo >= 1 && dev(lo) < dev(lo + 1)) best = lo;
    r.count = best;
    r.cell = available_length / best;
    r.within_tolerance = dev(best) <= tolerance + 1e-9;
    return r;
}

std::vector<StreetLight> generate_edge_street_lights(const RoadGraph& graph, const RegionPack& region,
                                                     const JunctionSet& junctions, EdgeId edge,
                                                     const std::vector<RoundaboutLayout>* roundabouts,
                                                     const StreetLightParams& params) {
    const Edge* e = graph.edge(edge);
    if (!e) return {};
    Ctx c(graph, region, junctions, roundabouts, params);
    return edge_lights(c, *e);
}

std::vector<StreetLight> generate_junction_street_lights(const RoadGraph& graph, const RegionPack& region,
                                                         const JunctionSet& junctions, NodeId node,
                                                         const std::vector<RoundaboutLayout>* roundabouts,
                                                         const StreetLightParams& params) {
    Ctx c(graph, region, junctions, roundabouts, params);
    return junction_lights(c, node);
}

std::vector<StreetLight> generate_roundabout_street_lights(const RoadGraph& graph, const RegionPack& region,
                                                           const JunctionSet& junctions, const RoundaboutLayout& layout,
                                                           const std::vector<RoundaboutLayout>* roundabouts,
                                                           const StreetLightParams& params) {
    Ctx c(graph, region, junctions, roundabouts, params);
    return roundabout_lights(c, layout);
}

std::vector<StreetLight> StreetLightResult::merged() const {
    std::vector<StreetLight> out;
    for (const auto& [id, v] : edges) out.insert(out.end(), v.begin(), v.end());
    for (const auto& [id, v] : junctions) out.insert(out.end(), v.begin(), v.end());
    for (const auto& [id, v] : roundabouts) out.insert(out.end(), v.begin(), v.end());
    return out;
}

namespace {

bool group_touches(const RoundaboutLayout& lay, const std::set<NodeId>& nodes, const std::set<EdgeId>& edges) {
    for (NodeId n : lay.group.nodes)
        if (nodes.count(n)) return true;
    for (EdgeId e : lay.group.ring_edges)
        if (edges.count(e)) return true;
    return false;
}

void regenerate(Ctx& c, StreetLightResult& res, const std::set<EdgeId>& edges, const std::set<NodeId>& nodes,
                const std::set<NodeId>& group_firsts) {
    for (EdgeId id : edges) {
        res.edges.erase(id);
        if (const Edge* e = c.graph.edge(id)) append_nonempty(res.edges, id, edge_lights(c, *e));
    }
    for (NodeId n : nodes) {
        res.junctions.erase(n);
        std::vector<StreetLight> v = junction_lights(c, n);
        if (!v.empty()) res.junctions[n] = std::move(v);
    }
    if (!c.layouts) return;
    for (const RoundaboutLayout& lay : *c.layouts) {
        if (lay.group.nodes.empty() || !group_firsts.count(lay.group.nodes.front())) continue;
        res.roundabouts.erase(lay.group.nodes.front());
        std::vector<StreetLight> v = roundabout_lights(c, lay);
        if (!v.empty()) res.roundabouts[lay.group.nodes.front()] = std::move(v);
    }
}

}  // namespace

StreetLightResult generate_street_lights(const RoadGraph& graph, const RegionPack& region, const JunctionSet& junctions,
                                         const std::vector<RoundaboutLayout>* roundabouts,
                                         const StreetLightParams& params, const std::vector<EdgeId>& only_edges,
                                         const std::vector<NodeId>& only_nodes) {
    Ctx c(graph, region, junctions, roundabouts, params);
    StreetLightResult res;
    const bool all = only_edges.empty() && only_nodes.empty();
    std::set<EdgeId> edges(only_edges.begin(), only_edges.end());
    std::set<NodeId> nodes(only_nodes.begin(), only_nodes.end());
    if (all) {
        for (const auto& [id, e] : graph.edges()) edges.insert(id);
        for (const auto& [id, j] : junctions.junctions) nodes.insert(id);
    }
    std::set<NodeId> firsts;
    if (roundabouts)
        for (const RoundaboutLayout& lay : *roundabouts)
            if (!lay.group.nodes.empty() && (all || group_touches(lay, nodes, edges))) firsts.insert(lay.group.nodes.front());
    regenerate(c, res, edges, nodes, firsts);
    return res;
}

void rebuild_dirty_street_lights(StreetLightResult& cache, const RoadGraph& graph, const RegionPack& region,
                                 const JunctionSet& junctions, const std::vector<RoundaboutLayout>* roundabouts,
                                 const std::vector<NodeId>& dirty_nodes, const std::vector<EdgeId>& dirty_edges,
                                 const StreetLightParams& params) {
    Ctx c(graph, region, junctions, roundabouts, params);

    // Drop entries whose object is gone.
    std::set<EdgeId> live_edges;
    for (const auto& [id, e] : graph.edges()) live_edges.insert(id);
    std::set<NodeId> live_nodes;
    for (const auto& [id, j] : junctions.junctions) live_nodes.insert(id);
    std::set<NodeId> live_groups;
    if (roundabouts)
        for (const RoundaboutLayout& lay : *roundabouts)
            if (!lay.group.nodes.empty()) live_groups.insert(lay.group.nodes.front());
    bool partner_changed = false;
    const auto has_twin = [](const std::vector<StreetLight>& v) {
        return std::any_of(v.begin(), v.end(), [](const StreetLight& l) { return l.twin_arm; });
    };
    for (const auto& [id, v] : cache.edges)
        if (!live_edges.count(id) && has_twin(v)) partner_changed = true;
    erase_if_missing(cache.edges, live_edges);
    erase_if_missing(cache.junctions, live_nodes);
    erase_if_missing(cache.roundabouts, live_groups);

    std::set<EdgeId> edges;
    std::set<NodeId> nodes(dirty_nodes.begin(), dirty_nodes.end());
    for (EdgeId id : dirty_edges)
        if (live_edges.count(id)) edges.insert(id);
    for (NodeId n : dirty_nodes)
        for (EdgeId id : graph.edges_at(n)) edges.insert(id);

    // A dirty edge changes the pattern phase of every edge of its run, and a dual edge changes which
    // columns its partners emit.
    const std::set<EdgeId> seed = edges;
    for (EdgeId id : seed) {
        const Edge* e = graph.edge(id);
        if (!e) continue;
        auto it = cache.edges.find(id);
        if (it != cache.edges.end() && has_twin(it->second)) partner_changed = true;
        const StreetLightRules* r = c.rules(*e);
        if (r && r->arrangement == LightArrangement::CentralReservation) partner_changed = true;
        for (const Piece& p : build_run(c, *e).pieces) edges.insert(p.e->id);
    }
    if (partner_changed)
        for (const auto& [id, e] : graph.edges()) {
            const StreetLightRules* r = c.rules(e);
            if (r && r->arrangement == LightArrangement::CentralReservation) edges.insert(id);
        }
    for (EdgeId id : edges)
        if (const Edge* e = graph.edge(id)) {
            nodes.insert(e->a);
            nodes.insert(e->b);
        }
    std::set<NodeId> live_junction_nodes;
    for (NodeId n : nodes)
        if (live_nodes.count(n)) live_junction_nodes.insert(n);
    std::set<NodeId> firsts;
    if (roundabouts)
        for (const RoundaboutLayout& lay : *roundabouts)
            if (!lay.group.nodes.empty() && group_touches(lay, nodes, edges)) firsts.insert(lay.group.nodes.front());
    // Nodes that stopped being junctions lose their entry.
    for (NodeId n : nodes) cache.junctions.erase(n);
    regenerate(c, cache, edges, live_junction_nodes, firsts);
}

}  // namespace cp
