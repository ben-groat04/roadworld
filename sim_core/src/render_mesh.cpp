#include "cp/render_mesh.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <iterator>
#include <limits>

#include "cp/edge_sampling.h"
#include "cp/lanes.h"

namespace cp {

namespace {

constexpr double kInf = std::numeric_limits<double>::infinity();
constexpr double kSnapTol = 3e-3;  // m, polygon vertices this close to a trim line are snapped onto it

using u32 = std::uint32_t;

Vec3 operator-(Vec3 a, Vec3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
Vec3 cross(Vec3 a, Vec3 b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
double length(Vec3 a) { return std::sqrt(a.x * a.x + a.y * a.y + a.z * a.z); }

// ---------------------------------------------------------------- cross-section profile

enum class Role { Carriageway, Kerb, Raised, Verge };

Role role_of(const std::string& profile) {
    if (profile == "kerb") return Role::Kerb;
    if (profile == "raised") return Role::Raised;
    if (profile == "verge") return Role::Verge;
    return Role::Carriageway;
}

// One strip of the cross-section, left to right (d0 >= d1; a kerb face has d0 == d1).
struct ProfileSeg {
    double d0 = 0, z0 = 0, u0 = 0;
    double d1 = 0, z1 = 0, u1 = 0;
    std::string surface;
    bool face = false;
    bool cw = false;  // carriageway role (junction carriageway polygons match these)
    int va = 0, vb = 0;  // ring vertex indices of the left and right end
};

struct ProfileVert {
    double d = 0, z = 0, u = 0;
    int seg = 0;  // a segment that owns it (surface)
};

struct Profile {
    std::vector<ProfileSeg> segs;
    std::vector<ProfileVert> verts;
    double d_left = 0, d_right = 0;
    std::string cw_surface, raised_surface, kerb_surface;

    // Height and u at lateral d (+ = left) on the tops of carriageway-role (cw) or other strips.
    // Beyond the strips: carriageway values are clamped, others extrapolate their slope.
    void eval(double d, bool cw, double& z, double& u) const {
        const ProfileSeg* best = nullptr;
        double best_dist = kInf;
        for (const ProfileSeg& s : segs) {
            if (s.face || s.cw != cw) continue;
            const double dist = d > s.d0 ? d - s.d0 : (d < s.d1 ? s.d1 - d : 0.0);
            if (dist < best_dist - 1e-12) {
                best_dist = dist;
                best = &s;
            }
        }
        if (!best) {
            if (!cw) eval(d, true, z, u);
            return;
        }
        const double w = best->d0 - best->d1;
        if (best_dist <= 0) {
            const double t = w > 1e-12 ? (best->d0 - d) / w : 0.0;
            z = best->z0 + (best->z1 - best->z0) * t;
            u = best->u0 + (best->u1 - best->u0) * t;
            return;
        }
        const bool above = d > best->d0;
        const double de = above ? best->d0 : best->d1;
        const double ze = above ? best->z0 : best->z1, ue = above ? best->u0 : best->u1;
        if (cw) {
            z = ze;
            u = ue;
            return;
        }
        const double slope = w > 1e-12 ? (best->z1 - best->z0) / (best->d1 - best->d0) : 0.0;
        z = ze + slope * (d - de);
        u = ue + (de - d) * std::sqrt(1 + slope * slope);
    }

    // Lateral positions of the strip ends of one role class (for matching junction boundaries).
    std::vector<double> ring_d(bool cw) const {
        std::vector<double> out;
        for (const ProfileSeg& s : segs)
            if (!s.face && s.cw == cw) {
                out.push_back(s.d0);
                out.push_back(s.d1);
            }
        std::sort(out.begin(), out.end());
        out.erase(std::unique(out.begin(), out.end(), [](double a, double b) { return std::abs(a - b) < 1e-9; }), out.end());
        return out;
    }
};

Profile build_profile(const Edge& e, const RoadType& rt, const RegionPack& region, const RenderMeshParams& P) {
    Profile pr;
    const std::vector<Lane> lanes = instantiate_lanes(e, rt, region.driving_side);
    if (lanes.empty()) return pr;
    const double cf = rt.crossfall.value_or(P.default_crossfall);
    const double H = rt.kerb_upstand.value_or(P.default_kerb_upstand);
    const double fcf = rt.footway_crossfall.value_or(P.default_footway_crossfall);
    const double vcf = rt.verge_crossfall.value_or(P.default_verge_crossfall);

    struct L {
        double hi, lo, centre;
        Role role;
        std::string surface;
        double z_hi = 0, z_lo = 0;
    };
    std::vector<L> ls;
    double cw_lo = kInf, cw_hi = -kInf;
    for (const Lane& l : lanes) {
        const LaneType* lt = region.lane_type(l.lane_type);
        L x;
        x.hi = l.offset + l.width / 2;
        x.lo = l.offset - l.width / 2;
        x.centre = l.offset;
        x.role = lt ? role_of(lt->profile) : Role::Carriageway;
        x.surface = lt && !lt->surface.empty() ? lt->surface : P.default_surface;
        if (x.role == Role::Carriageway) {
            cw_lo = std::min(cw_lo, x.lo);
            cw_hi = std::max(cw_hi, x.hi);
        }
        ls.push_back(std::move(x));
    }
    if (cw_lo > cw_hi) cw_lo = cw_hi = 0;
    const bool camber = rt.two_way && !e.one_way && !rt.dual;
    const double ns = region.driving_side == DrivingSide::Left ? 1.0 : -1.0;
    const auto base = [&](double d) {
        d = std::clamp(d, cw_lo, cw_hi);
        return camber ? -cf * std::abs(d) : -cf * ns * d;
    };

    // Sweep outward from the carriageway on each side, carrying the height of the previous strip.
    const double mid = (cw_lo + cw_hi) / 2;
    std::vector<std::size_t> left_side, right_side;
    for (std::size_t i = 0; i < ls.size(); ++i) (ls[i].centre > mid ? left_side : right_side).push_back(i);
    std::sort(left_side.begin(), left_side.end(), [&](std::size_t a, std::size_t b) { return ls[a].centre < ls[b].centre; });
    std::sort(right_side.begin(), right_side.end(), [&](std::size_t a, std::size_t b) { return ls[a].centre > ls[b].centre; });
    const auto sweep = [&](const std::vector<std::size_t>& idx, bool left) {
        double z_prev = base(left ? cw_hi : cw_lo);
        bool prev_cw = true;
        for (std::size_t i : idx) {
            L& l = ls[i];
            const double inner = left ? l.lo : l.hi, outer = left ? l.hi : l.lo, w = l.hi - l.lo;
            double zi = 0, zo = 0;
            switch (l.role) {
                case Role::Carriageway: zi = base(inner); zo = base(outer); break;
                case Role::Kerb: zi = zo = z_prev + (prev_cw ? H : 0.0); break;
                case Role::Raised: zi = z_prev + (prev_cw ? H : 0.0); zo = zi + fcf * w; break;
                case Role::Verge: zi = z_prev; zo = zi - vcf * w; break;
            }
            prev_cw = l.role == Role::Carriageway;
            z_prev = zo;
            l.z_hi = left ? zo : zi;
            l.z_lo = left ? zi : zo;
        }
    };
    sweep(left_side, true);
    sweep(right_side, false);

    pr.d_left = ls.front().hi;
    pr.d_right = ls.back().lo;
    double u = 0;
    const auto push = [&](double d0, double z0, double d1, double z1, const std::string& surf, bool face, bool cw) {
        if (std::abs(d0 - d1) < 1e-9 && std::abs(z0 - z1) < 1e-9) return;
        ProfileSeg s;
        s.d0 = d0; s.z0 = z0; s.u0 = u;
        u += std::hypot(d0 - d1, z0 - z1);
        s.d1 = d1; s.z1 = z1; s.u1 = u;
        s.surface = surf;
        s.face = face;
        s.cw = cw;
        pr.segs.push_back(std::move(s));
    };
    for (std::size_t k = 0; k < ls.size(); ++k) {
        const L& l = ls[k];
        if (k > 0) {
            const L& p = ls[k - 1];
            if (std::abs(p.z_lo - l.z_hi) > 1e-7)
                push(l.hi, p.z_lo, l.hi, l.z_hi, p.z_lo > l.z_hi ? p.surface : l.surface, true, false);
        }
        const bool cw = l.role == Role::Carriageway || l.role == Role::Verge;  // verges stay inside the junction carriageway polygon
        if (l.role == Role::Carriageway && camber && l.lo + 1e-9 < 0 && l.hi - 1e-9 > 0) {
            push(l.hi, l.z_hi, 0, base(0), l.surface, false, true);
            push(0, base(0), l.lo, l.z_lo, l.surface, false, true);
        } else {
            push(l.hi, l.z_hi, l.lo, l.z_lo, l.surface, false, cw);
        }
        if (l.role == Role::Carriageway && pr.cw_surface.empty()) pr.cw_surface = l.surface;
        if (l.role == Role::Raised && pr.raised_surface.empty()) pr.raised_surface = l.surface;
        if (l.role == Role::Kerb && pr.kerb_surface.empty()) pr.kerb_surface = l.surface;
    }

    // Ring vertices: shared between neighbouring strips of one surface unless a kerb face is involved.
    for (std::size_t k = 0; k < pr.segs.size(); ++k) {
        ProfileSeg& s = pr.segs[k];
        bool share = false;
        if (k > 0) {
            const ProfileSeg& p = pr.segs[k - 1];
            share = p.surface == s.surface && !p.face && !s.face && std::abs(p.d1 - s.d0) < 1e-9 &&
                    std::abs(p.z1 - s.z0) < 1e-9;
        }
        if (share) {
            s.va = pr.segs[k - 1].vb;
        } else {
            s.va = static_cast<int>(pr.verts.size());
            pr.verts.push_back({s.d0, s.z0, s.u0, static_cast<int>(k)});
        }
        s.vb = static_cast<int>(pr.verts.size());
        pr.verts.push_back({s.d1, s.z1, s.u1, static_cast<int>(k)});
    }
    return pr;
}

// ---------------------------------------------------------------- mesh building helpers

WorldMesh& surface_mesh(WorldMeshSet& set, const std::string& name) { return set[name]; }

u32 add_vertex(WorldMesh& m, Vec3 p, Vec2 uv, Vec3 tangent) {
    m.positions.push_back(p);
    m.normals.push_back({});
    m.tangents.push_back(tangent);
    m.uv.push_back(uv);
    return static_cast<u32>(m.positions.size() - 1);
}

// Appends a triangle and accumulates its area-weighted normal; false (nothing added) if degenerate.
bool add_tri(WorldMesh& m, u32 a, u32 b, u32 c, bool need_up) {
    const Vec3 n = cross(m.positions[b] - m.positions[a], m.positions[c] - m.positions[a]);
    const double len = length(n);
    if (!(len > 1e-10)) return false;
    if (need_up && !(n.z > 1e-10)) return false;
    for (u32 v : {a, b, c}) {
        m.normals[v].x += n.x;
        m.normals[v].y += n.y;
        m.normals[v].z += n.z;
    }
    m.indices.push_back(a);
    m.indices.push_back(b);
    m.indices.push_back(c);
    return true;
}

void finish_normals(WorldMeshSet& set) {
    for (auto& [name, m] : set) {
        for (Vec3& n : m.normals) {
            const double len = length(n);
            n = len > 1e-12 ? Vec3{n.x / len, n.y / len, n.z / len} : Vec3{0, 0, 1};
        }
    }
}

void drop_empty(WorldMeshSet& set) {
    for (auto it = set.begin(); it != set.end();) it = it->second.indices.empty() ? set.erase(it) : std::next(it);
}

// ---------------------------------------------------------------- edge sampling

struct Frame {
    Vec2 p;
    double h = 0;
    double z = 0;
};

Frame centre_frame(const Edge& e, double s) {
    double acc = 0;
    for (std::size_t i = 0; i < e.horizontal.spans.size(); ++i) {
        const Clothoid& c = e.horizontal.spans[i];
        if (s <= acc + c.length || i + 1 == e.horizontal.spans.size()) {
            const Pose po = c.pose(std::clamp(s - acc, 0.0, c.length));
            return {po.pos, po.heading, e.vertical.z(s)};
        }
        acc += c.length;
    }
    return {};
}

std::vector<double> select_stations(const Edge& e, double s0, double s1, const Profile& pr, const RenderMeshParams& P) {
    const double tol = std::max(P.chord_tolerance, 1e-4);
    SamplingParams sp;
    sp.max_chord_error = tol;
    sp.max_heading_change = std::clamp(0.05 * std::sqrt(tol / 0.02), 0.05, 0.4);
    sp.max_step = P.max_station_step;
    std::vector<double> st{s0, s1};
    std::vector<double> offsets{0.0};
    if (pr.d_left != 0) offsets.push_back(pr.d_left);
    if (pr.d_right != 0 && pr.d_right != pr.d_left) offsets.push_back(pr.d_right);
    for (double off : offsets)
        for (const EdgeSample& s : sample_offset(e, off, sp).samples)
            if (s.s > s0 && s.s < s1) st.push_back(s.s);
    for (const StructureSpan& span : e.structure)
        for (double s : {span.s0, span.s1})
            if (s > s0 && s < s1) st.push_back(s);
    for (const auto& b : e.vertical.breaks) {
        for (double s : {b.s - b.length / 2, b.s, b.s + b.length / 2})
            if (s > s0 && s < s1) st.push_back(s);
    }
    std::sort(st.begin(), st.end());
    std::vector<double> out;
    for (double s : st)
        if (out.empty() || s - out.back() > 1e-4) out.push_back(s);
    out.back() = s1;
    if (out.size() >= 2 && out[out.size() - 1] - out[out.size() - 2] < 1e-4) out.erase(out.end() - 2);

    // Refine where the vertical profile bends away from the chord.
    for (int pass = 0; pass < 12; ++pass) {
        bool changed = false;
        std::vector<double> next;
        for (std::size_t i = 0; i + 1 < out.size(); ++i) {
            next.push_back(out[i]);
            const double a = out[i], b = out[i + 1], m = 0.5 * (a + b);
            if (b - a > 2 * P.min_station_step &&
                std::abs(e.vertical.z(m) - 0.5 * (e.vertical.z(a) + e.vertical.z(b))) > tol) {
                next.push_back(m);
                changed = true;
            }
        }
        next.push_back(out.back());
        out = std::move(next);
        if (!changed) break;
    }
    return out;
}

struct EndRef {
    const JunctionArm* arm = nullptr;
    double trim = 0;
};

EndRef find_end(const JunctionSet& js, const Edge& e, bool at_a) {
    const Junction* j = js.find(at_a ? e.a : e.b);
    if (!j || j->trims.size() != j->arms.size()) return {};
    for (std::size_t i = 0; i < j->arms.size(); ++i)
        if (j->arms[i].edge == e.id && j->arms[i].at_a == at_a) return {&j->arms[i], j->trims[i]};
    return {};
}

}  // namespace

// ---------------------------------------------------------------- edge meshes

WorldMeshSet build_edge_mesh(const RoadGraph& graph, const RegionPack& region, EdgeId id, const JunctionSet& junctions,
                             const RenderMeshParams& P) {
    WorldMeshSet out;
    const Edge* e = graph.edge(id);
    if (!e || e->horizontal.spans.empty()) return out;
    const RoadType* rt = region.road_type(e->road_type);
    if (!rt) return out;
    const double len = e->horizontal.length();
    const EndRef ea = find_end(junctions, *e, true), eb = find_end(junctions, *e, false);
    const double s0 = ea.trim, s1 = len - eb.trim;
    if (!(s1 - s0 > 0.01)) return out;
    const Profile pr = build_profile(*e, *rt, region, P);
    if (pr.segs.empty()) return out;
    const std::vector<double> st = select_stations(*e, s0, s1, pr, P);
    const std::size_t n = st.size();
    if (n < 2) return out;

    // Vertices: one ring per station.
    std::vector<std::vector<u32>> vid(n, std::vector<u32>(pr.verts.size()));
    for (std::size_t i = 0; i < n; ++i) {
        const EndRef* end = i == 0 && ea.arm ? &ea : (i + 1 == n && eb.arm ? &eb : nullptr);
        Frame f = centre_frame(*e, st[i]);
        double cz = f.z;
        double right_x = std::sin(f.h), right_y = -std::cos(f.h);
        if (end) {
            const ArmPoint c = arm_point(*end->arm, end->trim, 0);
            cz = c.pos.z;
            const double h = end->arm->at_a ? c.heading_out : c.heading_out + kPi;
            right_x = std::sin(h);
            right_y = -std::cos(h);
        }
        for (std::size_t v = 0; v < pr.verts.size(); ++v) {
            const ProfileVert& pv = pr.verts[v];
            Vec3 p;
            if (end) {
                const ArmPoint a = arm_point(*end->arm, end->trim, end->arm->at_a ? pv.d : -pv.d);
                p = {a.pos.x, a.pos.y, cz + pv.z};
            } else {
                p = {f.p.x - pv.d * std::sin(f.h), f.p.y + pv.d * std::cos(f.h), cz + pv.z};
            }
            WorldMesh& m = surface_mesh(out, pr.segs[static_cast<std::size_t>(pv.seg)].surface);
            vid[i][v] = add_vertex(m, p, {pv.u, st[i]}, {right_x, right_y, 0});
        }
    }

    // Sections by structure kind, per interval.
    const auto kind_at = [&](double s) {
        for (const StructureSpan& sp : e->structure)
            if (s >= sp.s0 && s <= sp.s1) return sp.kind;
        return StructureKind::Ground;
    };
    std::vector<int> sec_of(n - 1);
    std::vector<SectionRange> secs;
    for (std::size_t i = 0; i + 1 < n; ++i) {
        const StructureKind k = kind_at(0.5 * (st[i] + st[i + 1]));
        if (secs.empty() || secs.back().kind != k) {
            SectionRange r;
            r.edge = id;
            r.kind = k;
            r.s0 = st[i];
            secs.push_back(r);
        }
        secs.back().s1 = st[i + 1];
        sec_of[i] = static_cast<int>(secs.size() - 1);
    }
    std::map<std::string, int> open_sec;
    for (std::size_t i = 0; i + 1 < n; ++i) {
        for (const ProfileSeg& s : pr.segs) {
            WorldMesh& m = surface_mesh(out, s.surface);
            const u32 l0 = vid[i][static_cast<std::size_t>(s.va)], r0 = vid[i][static_cast<std::size_t>(s.vb)];
            const u32 l1 = vid[i + 1][static_cast<std::size_t>(s.va)], r1 = vid[i + 1][static_cast<std::size_t>(s.vb)];
            const u32 first = static_cast<u32>(m.indices.size());
            const bool up = !s.face;
            const bool a = add_tri(m, r0, r1, l1, up);
            const bool b = add_tri(m, r0, l1, l0, up);
            const u32 count = (a ? 3u : 0u) + (b ? 3u : 0u);
            if (count == 0) continue;
            auto [it, fresh] = open_sec.try_emplace(s.surface, -1);
            if (fresh || it->second != sec_of[i]) {
                SectionRange r = secs[static_cast<std::size_t>(sec_of[i])];
                r.first_index = first;
                m.sections.push_back(r);
                it->second = sec_of[i];
            }
            m.sections.back().index_count += count;
        }
    }
    finish_normals(out);
    drop_empty(out);
    return out;
}

namespace {

// ---------------------------------------------------------------- triangulation (ear clipping with holes)

struct Tri {
    u32 a, b, c;
};

double area2(Vec2 a, Vec2 b, Vec2 c) { return (b - a).cross(c - a); }

double ring_area(const std::vector<Vec2>& pts, const std::vector<u32>& r) {
    double a = 0;
    for (std::size_t i = 0; i < r.size(); ++i) a += pts[r[i]].cross(pts[r[(i + 1) % r.size()]]);
    return a / 2;
}

bool point_in_ring(Vec2 p, const std::vector<Vec2>& pts, const std::vector<u32>& r) {
    bool in = false;
    for (std::size_t i = 0, j = r.size() - 1; i < r.size(); j = i++) {
        const Vec2 a = pts[r[i]], b = pts[r[j]];
        if ((a.y > p.y) != (b.y > p.y) && p.x < (b.x - a.x) * (p.y - a.y) / (b.y - a.y) + a.x) in = !in;
    }
    return in;
}

bool same_point(Vec2 a, Vec2 b) { return std::abs(a.x - b.x) < 1e-9 && std::abs(a.y - b.y) < 1e-9; }

// Joins `hole` (clockwise) into `loop` (counter-clockwise) through a zero-width bridge.
bool bridge_hole(const std::vector<Vec2>& pts, std::vector<u32>& loop, const std::vector<u32>& hole) {
    std::size_t mi = 0;
    for (std::size_t i = 1; i < hole.size(); ++i)
        if (pts[hole[i]].x > pts[hole[mi]].x) mi = i;
    const Vec2 M = pts[hole[mi]];
    double best_x = kInf;
    std::size_t p = loop.size();
    for (std::size_t i = 0; i < loop.size(); ++i) {
        const Vec2 a = pts[loop[i]], b = pts[loop[(i + 1) % loop.size()]];
        if (a.y == b.y || !((a.y <= M.y && b.y >= M.y) || (b.y <= M.y && a.y >= M.y))) continue;
        const double x = a.x + (M.y - a.y) * (b.x - a.x) / (b.y - a.y);
        if (x >= M.x - 1e-9 && x < best_x) {
            best_x = x;
            p = a.x > b.x ? i : (i + 1) % loop.size();
        }
    }
    if (p == loop.size()) return false;
    // A reflex vertex inside the triangle (M, hit, P) blocks the bridge; take the one closest in angle.
    const Vec2 I{best_x, M.y}, P = pts[loop[p]];
    double best_ang = kInf;
    std::size_t q = p;
    for (std::size_t i = 0; i < loop.size(); ++i) {
        const Vec2 Q = pts[loop[i]];
        if (i == p || Q.x < M.x) continue;
        const double d1 = area2(M, I, Q), d2 = area2(I, P, Q), d3 = area2(P, M, Q);
        const bool in = (d1 >= 0 && d2 >= 0 && d3 >= 0) || (d1 <= 0 && d2 <= 0 && d3 <= 0);
        if (!in) continue;
        const double ang = std::abs(std::atan2(Q.y - M.y, Q.x - M.x));
        if (ang < best_ang) {
            best_ang = ang;
            q = i;
        }
    }
    p = q;
    std::vector<u32> merged(loop.begin(), loop.begin() + static_cast<std::ptrdiff_t>(p) + 1);
    for (std::size_t k = 0; k <= hole.size(); ++k) merged.push_back(hole[(mi + k) % hole.size()]);
    merged.push_back(loop[p]);
    merged.insert(merged.end(), loop.begin() + static_cast<std::ptrdiff_t>(p) + 1, loop.end());
    loop = std::move(merged);
    return true;
}

void ear_clip(const std::vector<Vec2>& pts, const std::vector<u32>& loop, std::vector<Tri>& out) {
    const int n = static_cast<int>(loop.size());
    if (n < 3) return;
    std::vector<int> prv(loop.size()), nxt(loop.size());
    for (int i = 0; i < n; ++i) {
        prv[static_cast<std::size_t>(i)] = (i + n - 1) % n;
        nxt[static_cast<std::size_t>(i)] = (i + 1) % n;
    }
    const auto P = [&](int i) { return pts[loop[static_cast<std::size_t>(i)]]; };
    const auto N = [&](int i) { return nxt[static_cast<std::size_t>(i)]; };
    const auto Pv = [&](int i) { return prv[static_cast<std::size_t>(i)]; };
    const auto remove = [&](int i) {
        nxt[static_cast<std::size_t>(Pv(i))] = N(i);
        prv[static_cast<std::size_t>(N(i))] = Pv(i);
    };
    const auto empty_ear = [&](int a, int b, int c) {
        const Vec2 A = P(a), B = P(b), C = P(c);
        for (int j = N(c); j != a; j = N(j)) {
            const Vec2 Q = P(j);
            if (same_point(Q, A) || same_point(Q, B) || same_point(Q, C)) continue;
            if (area2(A, B, Q) >= -1e-10 && area2(B, C, Q) >= -1e-10 && area2(C, A, Q) >= -1e-10) return false;
        }
        return true;
    };
    int remaining = n, cur = 0;
    while (remaining > 3) {
        bool clipped = false;
        int i = cur;
        do {
            const int a = Pv(i), c = N(i);
            if (area2(P(a), P(i), P(c)) > 1e-7 && empty_ear(a, i, c)) {
                out.push_back({loop[static_cast<std::size_t>(a)], loop[static_cast<std::size_t>(i)], loop[static_cast<std::size_t>(c)]});
                remove(i);
                cur = c;
                --remaining;
                clipped = true;
                break;
            }
            i = N(i);
        } while (i != cur);
        if (clipped) continue;
        // No clean ear: drop a collinear vertex, else cut the sharpest convex one.
        int pick = -1;
        double best = -kInf;
        bool collinear = false;
        i = cur;
        do {
            const double cr = area2(P(Pv(i)), P(i), P(N(i)));
            if (std::abs(cr) <= 1e-7) {
                pick = i;
                collinear = true;
                break;
            }
            if (cr > best) {
                best = cr;
                pick = i;
            }
            i = N(i);
        } while (i != cur);
        if (pick < 0 || (!collinear && best <= 0)) return;
        if (!collinear)
            out.push_back({loop[static_cast<std::size_t>(Pv(pick))], loop[static_cast<std::size_t>(pick)],
                           loop[static_cast<std::size_t>(N(pick))]});
        cur = N(pick);
        remove(pick);
        --remaining;
    }
    const int a = cur, b = N(a), c = N(b);
    if (area2(P(a), P(b), P(c)) > 1e-7)
        out.push_back({loop[static_cast<std::size_t>(a)], loop[static_cast<std::size_t>(b)], loop[static_cast<std::size_t>(c)]});
}

// Triangulates polygons given as rings of indices into pts (counter-clockwise outers, clockwise holes).
void triangulate(const std::vector<Vec2>& pts, const std::vector<std::vector<u32>>& rings, std::vector<Tri>& out) {
    std::vector<std::size_t> outers, holes;
    for (std::size_t i = 0; i < rings.size(); ++i) {
        if (rings[i].size() < 3) continue;
        const double a = ring_area(pts, rings[i]);
        if (std::abs(a) < 1e-9) continue;
        (a > 0 ? outers : holes).push_back(i);
    }
    std::vector<std::vector<std::size_t>> owned(outers.size());
    for (std::size_t h : holes) {
        double best = kInf;
        std::size_t owner = outers.size();
        for (std::size_t o = 0; o < outers.size(); ++o) {
            const double a = ring_area(pts, rings[outers[o]]);
            if (a < best && point_in_ring(pts[rings[h][0]], pts, rings[outers[o]])) {
                best = a;
                owner = o;
            }
        }
        if (owner < outers.size()) owned[owner].push_back(h);
    }
    for (std::size_t o = 0; o < outers.size(); ++o) {
        std::vector<u32> loop = rings[outers[o]];
        auto& hs = owned[o];
        const auto max_x = [&](std::size_t h) {
            double m = -kInf;
            for (u32 v : rings[h]) m = std::max(m, pts[v].x);
            return m;
        };
        std::sort(hs.begin(), hs.end(), [&](std::size_t a, std::size_t b) { return max_x(a) > max_x(b); });
        for (std::size_t h : hs) bridge_hole(pts, loop, rings[h]);
        ear_clip(pts, loop, out);
    }
}

// ---------------------------------------------------------------- junction meshes

struct ArmCtx {
    const JunctionArm* arm = nullptr;
    Profile prof;
    double trim = 0, edge_len = 0;
    Vec3 tc;
    Vec2 dir, left;
    double grade = 0;  // dz per metre going outward
    double hl = 0, hr = 0;
    bool at_a = true;
};

struct Blended {
    double z = 0, u = 0, v = 0;
    Vec3 tangent;
};

Blended blend(const std::vector<ArmCtx>& arms, Vec2 p, bool cw) {
    Blended acc;
    double wsum = 0;
    Vec2 tan{};
    for (const ArmCtx& a : arms) {
        const Vec2 r = p - a.tc.xy();
        const double l = r.dot(a.left), t = r.dot(a.dir);
        const double lc = std::clamp(l, -a.hr, a.hl);
        const double dist = std::hypot(l - lc, t);
        double pz = 0, pu = 0;
        a.prof.eval(a.at_a ? l : -l, cw, pz, pu);
        const double z = a.tc.z + a.grade * t + pz;
        const double v = a.at_a ? a.trim + t : a.edge_len - a.trim - t;
        const Vec2 right = a.at_a ? Vec2{a.dir.y, -a.dir.x} : Vec2{-a.dir.y, a.dir.x};
        if (dist < 1e-7) return {z, pu, v, {right.x, right.y, 0}};
        const double w = 1.0 / (dist * dist);
        acc.z += w * z;
        acc.u += w * pu;
        acc.v += w * v;
        tan = tan + right * w;
        wsum += w;
    }
    if (wsum <= 0) return {0, 0, 0, {1, 0, 0}};
    const double tl = tan.length();
    acc.z /= wsum;
    acc.u /= wsum;
    acc.v /= wsum;
    acc.tangent = tl > 1e-12 ? Vec3{tan.x / tl, tan.y / tl, 0} : Vec3{1, 0, 0};
    return acc;
}

// Snaps vertices that lie on an arm's trim line exactly onto it (same arm_point() as the edge mesh)
// and inserts the edge cross-section's ring points along trim-line segments, so seams are welded.
std::vector<Vec2> conform_ring(const std::vector<Vec2>& ring, const std::vector<ArmCtx>& arms, bool cw) {
    struct SV {
        Vec2 p;
        int arm = -1;
        double l = 0;
    };
    std::vector<SV> sv;
    for (const Vec2& p : ring) {
        SV v{p, -1, 0};
        for (std::size_t i = 0; i < arms.size() && v.arm < 0; ++i) {
            const ArmCtx& a = arms[i];
            const Vec2 r = p - a.tc.xy();
            const double t = r.dot(a.dir);
            double l = r.dot(a.left);
            if (std::abs(t) > kSnapTol || l < -a.hr - kSnapTol || l > a.hl + kSnapTol) continue;
            for (double c : {a.arm->kerb_left, -a.arm->kerb_right, a.hl, -a.hr})
                if (std::abs(l - c) < kSnapTol) {
                    l = c;
                    break;
                }
            v.arm = static_cast<int>(i);
            v.l = l;
            v.p = arm_point(*a.arm, a.trim, l).pos.xy();
        }
        if (!sv.empty() && (sv.back().p - v.p).length() < 1e-7) continue;
        sv.push_back(v);
    }
    while (sv.size() > 1 && (sv.front().p - sv.back().p).length() < 1e-7) sv.pop_back();
    std::vector<Vec2> out;
    for (std::size_t i = 0; i < sv.size(); ++i) {
        const SV &a = sv[i], &b = sv[(i + 1) % sv.size()];
        out.push_back(a.p);
        if (a.arm < 0 || a.arm != b.arm) continue;
        const ArmCtx& ac = arms[static_cast<std::size_t>(a.arm)];
        std::vector<double> ls;
        for (double d : ac.prof.ring_d(cw)) {
            const double l = ac.at_a ? d : -d;
            if (l > std::min(a.l, b.l) + 1e-6 && l < std::max(a.l, b.l) - 1e-6) ls.push_back(l);
        }
        std::sort(ls.begin(), ls.end());
        if (a.l > b.l) std::reverse(ls.begin(), ls.end());
        for (double l : ls) out.push_back(arm_point(*ac.arm, ac.trim, l).pos.xy());
    }
    return out;
}

bool finite_ring(const std::vector<Vec2>& r) {
    for (const Vec2& p : r)
        if (!std::isfinite(p.x) || !std::isfinite(p.y)) return false;
    return true;
}

double dist_point_segment(Vec2 p, Vec2 a, Vec2 b) {
    const Vec2 ab = b - a;
    const double l2 = ab.dot(ab);
    const double t = l2 > 0 ? std::clamp((p - a).dot(ab) / l2, 0.0, 1.0) : 0.0;
    return (p - (a + ab * t)).length();
}

// Triangulates conformed rings into `m`, heights from the arm blend.
void add_blended_polygon(WorldMesh& m, const std::vector<std::vector<Vec2>>& rings, const std::vector<ArmCtx>& arms,
                         bool cw) {
    std::vector<Vec2> pts;
    std::vector<std::vector<u32>> idx;
    for (const auto& r : rings) {
        if (r.size() < 3) continue;
        std::vector<u32> ring;
        for (const Vec2& p : r) {
            ring.push_back(static_cast<u32>(pts.size()));
            pts.push_back(p);
        }
        idx.push_back(std::move(ring));
    }
    std::vector<Tri> tris;
    triangulate(pts, idx, tris);
    if (tris.empty()) return;
    std::vector<u32> vmap(pts.size(), 0xFFFFFFFFu);
    const auto vert = [&](u32 i) {
        if (vmap[i] == 0xFFFFFFFFu) {
            const Blended b = blend(arms, pts[i], cw);
            vmap[i] = add_vertex(m, {pts[i].x, pts[i].y, b.z}, {b.u, b.v}, b.tangent);
        }
        return vmap[i];
    };
    for (const Tri& t : tris) add_tri(m, vert(t.a), vert(t.b), vert(t.c), true);
}

// Vertical face along A -> B between heights bottom and top; the front side is the left of A -> B
// when `left_facing`, else the right.
void add_face(WorldMesh& m, Vec3 a, Vec3 b, double dza, double dzb, Vec2 uva, Vec2 uvb, bool left_facing) {
    const Vec3 t{b.x - a.x, b.y - a.y, 0};
    const double tl = length(t);
    const Vec3 tan = tl > 1e-12 ? Vec3{t.x / tl, t.y / tl, 0} : Vec3{1, 0, 0};
    const u32 va = add_vertex(m, a, uva, tan), vb = add_vertex(m, b, uvb, tan);
    const u32 ta = add_vertex(m, {a.x, a.y, a.z + dza}, {uva.x + dza, uva.y}, tan);
    const u32 tb = add_vertex(m, {b.x, b.y, b.z + dzb}, {uvb.x + dzb, uvb.y}, tan);
    if (left_facing) {
        add_tri(m, va, ta, tb, false);
        add_tri(m, va, tb, vb, false);
    } else {
        add_tri(m, va, vb, tb, false);
        add_tri(m, va, tb, ta, false);
    }
}

void add_section(WorldMeshSet& set, NodeId node) {
    for (auto& [name, m] : set) {
        if (m.indices.empty() || !m.sections.empty()) continue;
        SectionRange r;
        r.node = node;
        r.index_count = static_cast<u32>(m.indices.size());
        m.sections.push_back(r);
    }
}

}  // namespace

WorldMeshSet build_junction_mesh(const RoadGraph& graph, const RegionPack& region, const Junction& j,
                                 const RenderMeshParams& P) {
    WorldMeshSet out;
    if (j.arms.empty() || j.trims.size() != j.arms.size()) return out;
    if (j.carriageway.empty() && j.pavements.empty()) return out;

    std::vector<ArmCtx> arms;
    for (std::size_t i = 0; i < j.arms.size(); ++i) {
        ArmCtx a;
        a.arm = &j.arms[i];
        const Edge* e = graph.edge(a.arm->edge);
        const RoadType* rt = e ? region.road_type(e->road_type) : nullptr;
        if (!e || !rt || a.arm->centreline.empty()) continue;
        a.prof = build_profile(*e, *rt, region, P);
        if (a.prof.segs.empty()) continue;
        a.trim = j.trims[i];
        a.edge_len = e->horizontal.length();
        a.at_a = a.arm->at_a;
        const ArmPoint c = arm_point(*a.arm, a.trim, 0);
        a.tc = c.pos;
        a.dir = {std::cos(c.heading_out), std::sin(c.heading_out)};
        a.left = {-a.dir.y, a.dir.x};
        const double dl = std::min(2.0, a.arm->edge_length - a.trim);
        if (dl > 0.1) a.grade = (arm_point(*a.arm, a.trim + dl, 0).pos.z - c.pos.z) / dl;
        a.hl = a.arm->half_width_left;
        a.hr = a.arm->half_width_right;
        arms.push_back(std::move(a));
    }
    if (arms.empty()) return out;
    std::string cw_surface = P.default_surface, band_surface, kerb_surface = P.island_kerb_surface;
    for (const ArmCtx& a : arms) {
        if (!a.prof.cw_surface.empty()) { cw_surface = a.prof.cw_surface; break; }
    }
    for (const ArmCtx& a : arms) {
        if (!a.prof.raised_surface.empty()) { band_surface = a.prof.raised_surface; break; }
    }
    for (const ArmCtx& a : arms) {
        if (!a.prof.kerb_surface.empty()) { kerb_surface = a.prof.kerb_surface; break; }
    }
    if (band_surface.empty()) band_surface = cw_surface;

    std::vector<std::vector<Vec2>> cw_rings, band_rings;
    for (const auto& r : j.carriageway) {
        if (r.size() < 3 || !finite_ring(r)) continue;
        std::vector<Vec2> c = conform_ring(r, arms, true);
        if (c.size() >= 3) cw_rings.push_back(std::move(c));
    }
    for (const auto& r : j.pavements) {
        if (r.size() < 3 || !finite_ring(r)) continue;
        std::vector<Vec2> c = conform_ring(r, arms, false);
        if (c.size() >= 3) band_rings.push_back(std::move(c));
    }
    add_blended_polygon(surface_mesh(out, cw_surface), cw_rings, arms, true);
    add_blended_polygon(surface_mesh(out, band_surface), band_rings, arms, false);

    // Kerb faces where the carriageway boundary runs along a pavement edge (not on a trim line).
    struct Seg {
        Vec2 a, b;
    };
    std::vector<Seg> pav;
    for (const auto& r : j.pavements)
        for (std::size_t i = 0; i < r.size(); ++i) pav.push_back({r[i], r[(i + 1) % r.size()]});
    const auto on_trim = [&](Vec2 p) {
        for (const ArmCtx& a : arms)
            if (std::abs((p - a.tc.xy()).dot(a.dir)) < 1e-6) return true;
        return false;
    };
    WorldMesh& kerb = surface_mesh(out, kerb_surface);
    for (const auto& r : cw_rings) {
        for (std::size_t i = 0; i < r.size(); ++i) {
            const Vec2 A = r[i], B = r[(i + 1) % r.size()];
            if ((B - A).length() < 1e-4 || (on_trim(A) && on_trim(B))) continue;
            const Vec2 M = (A + B) * 0.5;
            bool along_pavement = false;
            for (const Seg& s : pav)
                if (dist_point_segment(M, s.a, s.b) < 2e-3) { along_pavement = true; break; }
            if (!along_pavement) continue;
            const Blended ba = blend(arms, A, true), bb = blend(arms, B, true);
            const double dza = blend(arms, A, false).z - ba.z, dzb = blend(arms, B, false).z - bb.z;
            if (dza < 1e-4 || dzb < 1e-4) continue;
            add_face(kerb, {A.x, A.y, ba.z}, {B.x, B.y, bb.z}, dza, dzb, {ba.u, ba.v}, {bb.u, bb.v}, true);
        }
    }
    finish_normals(out);
    drop_empty(out);
    add_section(out, j.node);
    return out;
}

namespace {

// Height of the road surface at p on `edge`: nearest centreline point plus the carriageway profile.
double road_height_at(const Edge& e, const Profile& pr, Vec2 p) {
    const std::vector<EdgeSample> c = sample_centreline(e);
    double best = kInf, z = 0, lat = 0;
    for (std::size_t i = 0; i + 1 < c.size(); ++i) {
        const Vec2 a = c[i].pos.xy(), b = c[i + 1].pos.xy();
        const Vec2 ab = b - a;
        const double l2 = ab.dot(ab);
        const double t = l2 > 0 ? std::clamp((p - a).dot(ab) / l2, 0.0, 1.0) : 0.0;
        const Vec2 q = a + ab * t;
        const double d = (p - q).length();
        if (d < best) {
            best = d;
            z = c[i].pos.z + (c[i + 1].pos.z - c[i].pos.z) * t;
            const double side = ab.cross(p - a);
            lat = side >= 0 ? d : -d;
        }
    }
    double pz = 0, pu = 0;
    pr.eval(lat, true, pz, pu);
    return z + pz;
}

// Raised polygon (top + faces) over a base height function.
void add_raised_polygon(WorldMeshSet& out, const PolygonSet& poly, const std::function<double(Vec2)>& base_z, double H,
                        const RenderMeshParams& P) {
    std::vector<Vec2> pts;
    std::vector<std::vector<u32>> idx;
    for (const auto& r : poly) {
        if (r.size() < 3 || !finite_ring(r)) continue;
        std::vector<u32> ring;
        for (const Vec2& p : r) {
            ring.push_back(static_cast<u32>(pts.size()));
            pts.push_back(p);
        }
        idx.push_back(std::move(ring));
    }
    std::vector<Tri> tris;
    triangulate(pts, idx, tris);
    WorldMesh& top = surface_mesh(out, P.island_surface);
    std::vector<u32> vmap(pts.size(), 0xFFFFFFFFu);
    const auto vert = [&](u32 i) {
        if (vmap[i] == 0xFFFFFFFFu) vmap[i] = add_vertex(top, {pts[i].x, pts[i].y, base_z(pts[i]) + H}, pts[i], {1, 0, 0});
        return vmap[i];
    };
    for (const Tri& t : tris) add_tri(top, vert(t.a), vert(t.b), vert(t.c), true);
    WorldMesh& kerb = surface_mesh(out, P.island_kerb_surface);
    for (const auto& r : poly) {
        if (r.size() < 3 || !finite_ring(r)) continue;
        double run = 0;
        for (std::size_t i = 0; i < r.size(); ++i) {
            const Vec2 A = r[i], B = r[(i + 1) % r.size()];
            const double l = (B - A).length();
            if (l < 1e-4) continue;
            add_face(kerb, {A.x, A.y, base_z(A)}, {B.x, B.y, base_z(B)}, H, H, {0, run}, {0, run + l}, false);
            run += l;
        }
    }
}

}  // namespace

WorldMeshSet build_roundabout_mesh(const RoadGraph& graph, const RegionPack& region, const RoundaboutLayout& layout,
                                   const RenderMeshParams& P) {
    WorldMeshSet out;
    if (!layout.ok) return out;
    double ring_z = 0, H = P.default_kerb_upstand;
    int count = 0;
    for (NodeId n : layout.group.nodes)
        if (const Node* nd = graph.node(n)) {
            ring_z += nd->pos.z;
            ++count;
        }
    if (count > 0) ring_z /= count;
    for (EdgeId id : layout.group.ring_edges)
        if (const Edge* e = graph.edge(id))
            if (const RoadType* rt = region.road_type(e->road_type); rt && rt->kerb_upstand) {
                H = *rt->kerb_upstand;
                break;
            }
    if (!layout.island.empty()) add_raised_polygon(out, layout.island, [&](Vec2) { return ring_z; }, H, P);
    for (const SplitterIsland& sp : layout.splitters) {
        if (sp.arm < 0 || static_cast<std::size_t>(sp.arm) >= layout.group.arms.size() || sp.polygon.size() < 3) continue;
        const Edge* e = graph.edge(layout.group.arms[static_cast<std::size_t>(sp.arm)].edge);
        const RoadType* rt = e ? region.road_type(e->road_type) : nullptr;
        if (!rt) continue;
        const Profile pr = build_profile(*e, *rt, region, P);
        add_raised_polygon(out, PolygonSet{sp.polygon}, [&](Vec2 p) { return road_height_at(*e, pr, p); }, H, P);
    }
    finish_normals(out);
    drop_empty(out);
    add_section(out, NodeId{});
    return out;
}

void append_mesh(WorldMeshSet& dst, const WorldMeshSet& src) {
    for (const auto& [name, m] : src) {
        WorldMesh& d = dst[name];
        const u32 base = static_cast<u32>(d.positions.size());
        const u32 ibase = static_cast<u32>(d.indices.size());
        d.positions.insert(d.positions.end(), m.positions.begin(), m.positions.end());
        d.normals.insert(d.normals.end(), m.normals.begin(), m.normals.end());
        d.tangents.insert(d.tangents.end(), m.tangents.begin(), m.tangents.end());
        d.uv.insert(d.uv.end(), m.uv.begin(), m.uv.end());
        for (u32 i : m.indices) d.indices.push_back(i + base);
        for (SectionRange s : m.sections) {
            s.first_index += ibase;
            d.sections.push_back(s);
        }
    }
}

// ---------------------------------------------------------------- float buffers and chunks

namespace {

void push_vertex(MeshBuffers& b, const WorldMesh& m, std::size_t i, Vec3 origin) {
    const Vec3 p = m.positions[i], n = m.normals[i], t = m.tangents[i];
    b.positions.push_back({static_cast<float>(p.x - origin.x), static_cast<float>(p.y - origin.y), static_cast<float>(p.z - origin.z)});
    b.normals.push_back({static_cast<float>(n.x), static_cast<float>(n.y), static_cast<float>(n.z)});
    b.tangents.push_back({static_cast<float>(t.x), static_cast<float>(t.y), static_cast<float>(t.z), 1.0f});
    b.uv0.push_back({static_cast<float>(m.uv[i].x), static_cast<float>(m.uv[i].y)});
    b.colours.push_back({0, 0, 0, 255});
}

}  // namespace

MeshBuffers pack_buffers(const WorldMesh& mesh, Vec3 origin) {
    MeshBuffers b;
    for (std::size_t i = 0; i < mesh.positions.size(); ++i) push_vertex(b, mesh, i, origin);
    b.indices = mesh.indices;
    b.sections = mesh.sections;
    return b;
}

TileKey tile_of(Vec2 p, double tile_size) {
    return {static_cast<std::int32_t>(std::floor(p.x / tile_size)), static_cast<std::int32_t>(std::floor(p.y / tile_size))};
}

Vec3 tile_origin(TileKey t, double tile_size) { return {t.x * tile_size, t.y * tile_size, 0}; }

namespace {

struct Box {
    Vec2 lo{kInf, kInf}, hi{-kInf, -kInf};
    void add(Vec2 p) {
        lo = {std::min(lo.x, p.x), std::min(lo.y, p.y)};
        hi = {std::max(hi.x, p.x), std::max(hi.y, p.y)};
    }
    void grow(double m) {
        lo = {lo.x - m, lo.y - m};
        hi = {hi.x + m, hi.y + m};
    }
};

bool touches(const Box& b, const std::set<TileKey>* only, double ts) {
    if (!only) return true;
    if (b.lo.x > b.hi.x) return false;
    const TileKey a = tile_of(b.lo, ts), c = tile_of(b.hi, ts);
    for (const TileKey& t : *only)
        if (t.x >= a.x && t.x <= c.x && t.y >= a.y && t.y <= c.y) return true;
    return false;
}

void distribute(std::map<TileKey, MeshChunk>& chunks, const WorldMeshSet& set, const ChunkParams& cp,
                const std::set<TileKey>* only) {
    for (const auto& [name, m] : set) {
        std::map<TileKey, std::vector<u32>> remap;
        std::vector<SectionRange> secs = m.sections;
        if (secs.empty()) {
            SectionRange r;
            r.index_count = static_cast<u32>(m.indices.size());
            secs.push_back(r);
        }
        for (const SectionRange& sec : secs) {
            std::set<TileKey> opened;
            for (u32 k = sec.first_index; k + 2 < sec.first_index + sec.index_count + 1 && k + 2 < m.indices.size(); k += 3) {
                const u32 i0 = m.indices[k], i1 = m.indices[k + 1], i2 = m.indices[k + 2];
                const Vec3 c{(m.positions[i0].x + m.positions[i1].x + m.positions[i2].x) / 3,
                             (m.positions[i0].y + m.positions[i1].y + m.positions[i2].y) / 3, 0};
                const TileKey tk = tile_of(c.xy(), cp.tile_size);
                if (only && !only->count(tk)) continue;
                MeshChunk& ch = chunks[tk];
                ch.tile = tk;
                ch.origin = tile_origin(tk, cp.tile_size);
                MeshBuffers& buf = ch.surfaces[name];
                if (opened.insert(tk).second) {
                    SectionRange r = sec;
                    r.first_index = static_cast<u32>(buf.indices.size());
                    r.index_count = 0;
                    buf.sections.push_back(r);
                }
                std::vector<u32>& rm = remap[tk];
                if (rm.empty()) rm.assign(m.positions.size(), 0xFFFFFFFFu);
                for (u32 v : {i0, i1, i2}) {
                    if (rm[v] == 0xFFFFFFFFu) {
                        rm[v] = static_cast<u32>(buf.positions.size());
                        push_vertex(buf, m, v, ch.origin);
                    }
                    buf.indices.push_back(rm[v]);
                }
                buf.sections.back().index_count += 3;
            }
        }
    }
}

}  // namespace

std::map<TileKey, MeshChunk> build_mesh_chunks(const RoadGraph& graph, const RegionPack& region,
                                               const JunctionSet& junctions,
                                               const std::vector<RoundaboutLayout>* roundabouts,
                                               const RenderMeshParams& params, const ChunkParams& chunk,
                                               const std::set<TileKey>* only) {
    std::map<TileKey, MeshChunk> chunks;
    for (const auto& [id, e] : graph.edges()) {
        if (only) {
            Box b;
            for (const EdgeSample& s : sample_centreline(e, {0.5, 0.3, 50.0})) b.add(s.pos.xy());
            b.grow(30);
            if (!touches(b, only, chunk.tile_size)) continue;
        }
        distribute(chunks, build_edge_mesh(graph, region, id, junctions, params), chunk, only);
    }
    for (const auto& [node, j] : junctions.junctions) {
        if (only) {
            Box b;
            for (const auto& poly : {&j.carriageway, &j.pavements})
                for (const auto& r : *poly)
                    for (const Vec2& p : r) b.add(p);
            b.grow(2);
            if (!touches(b, only, chunk.tile_size)) continue;
        }
        distribute(chunks, build_junction_mesh(graph, region, j, params), chunk, only);
    }
    if (roundabouts) {
        for (const RoundaboutLayout& l : *roundabouts) {
            if (only) {
                Box b;
                for (const auto& r : l.island)
                    for (const Vec2& p : r) b.add(p);
                for (const SplitterIsland& s : l.splitters)
                    for (const Vec2& p : s.polygon) b.add(p);
                b.grow(2);
                if (!touches(b, only, chunk.tile_size)) continue;
            }
            distribute(chunks, build_roundabout_mesh(graph, region, l, params), chunk, only);
        }
    }
    return chunks;
}

}  // namespace cp
