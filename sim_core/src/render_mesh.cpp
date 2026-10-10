#include "cp/render_mesh.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <functional>
#include <iterator>
#include <limits>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "cp/edge_sampling.h"
#include "cp/lanes.h"
#include "cp/triangulate.h"

namespace cp {

namespace {

constexpr double kInf = std::numeric_limits<double>::infinity();
constexpr double kSnapTol = 3e-3;  // m, polygon vertices this close to a trim line are snapped onto it
constexpr double kSnapLat = 0.02;   // m, lateral tolerance for snapping to kerb lines and cross-section edges

using u32 = std::uint32_t;

Vec3 operator-(Vec3 a, Vec3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
Vec3 operator+(Vec3 a, Vec3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
Vec3 operator*(Vec3 a, double s) { return {a.x * s, a.y * s, a.z * s}; }
Vec3 cross(Vec3 a, Vec3 b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
double dot(Vec3 a, Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
double length(Vec3 a) { return std::sqrt(dot(a, a)); }
Vec3 normalized(Vec3 a, Vec3 fallback) {
    const double l = length(a);
    return l > 1e-12 ? a * (1 / l) : fallback;
}

// splitmix64-based stable hash: the same inputs give the same value on every platform.
std::uint64_t mix(std::uint64_t z) {
    z += 0x9E3779B97F4A7C15ull;
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
}
double hash01(std::uint64_t a, std::int64_t b) {
    return static_cast<double>(mix(mix(a) ^ static_cast<std::uint64_t>(b)) >> 11) * (1.0 / 9007199254740992.0);
}
// Smooth stable value in 0..1 that changes every `period` metres along an edge.
double lowfreq(EdgeId e, double s, double period) {
    const double q = s / period, k = std::floor(q), f = q - k, t = f * f * (3 - 2 * f);
    const auto ki = static_cast<std::int64_t>(k);
    return hash01(e.value, ki) * (1 - t) + hash01(e.value, ki + 1) * t;
}

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
    bool cw = false;  // carriageway class: carriageway and verge lanes (junction carriageway polygons match these)
    Role role = Role::Carriageway;
    bool driving = false;
    TravelDir travel = TravelDir::None;
    int lane = -1;
    double lane_centre = 0, lane_width = 0;
    bool kerb_uv = false;  // uv = (along, up/across the kerb) instead of (across, along)
    double v_ref = 0;      // profile u at the foot of the kerb face
    int vdir = 0;          // direction of increasing kerb v: 0 right, 1 left, 2 up
    int va = 0, vb = 0;    // ring vertex indices of the left and right end
};

struct ProfileVert {
    double d = 0, z = 0, u = 0;
    int seg = 0;  // the segment that created it (surface, lane, uv rule)
};

struct Profile {
    std::vector<ProfileSeg> segs;
    std::vector<ProfileVert> verts;
    double d_left = 0, d_right = 0;
    double cw_lo = 0, cw_hi = 0;
    std::string cw_surface, raised_surface, kerb_surface;

    // The strip at lateral d among the (non-face) strips of one class; ties at a step go to the class
    // asked for. `dist` is how far d lies outside it (0 inside).
    const ProfileSeg* best_seg(double d, bool cw, double* dist = nullptr) const {
        const ProfileSeg* best = nullptr;
        double best_dist = kInf;
        for (const ProfileSeg& s : segs) {
            if (s.face || s.cw != cw) continue;
            const double dd = d > s.d0 ? d - s.d0 : (d < s.d1 ? s.d1 - d : 0.0);
            if (dd < best_dist - 1e-12) {
                best_dist = dd;
                best = &s;
            }
        }
        if (dist) *dist = best_dist;
        return best;
    }

    // Height (relative to the centreline) and u at lateral d (+ = left). Beyond the strips of the
    // class: carriageway values are clamped, others extrapolate their slope.
    void eval(double d, bool cw, double& z, double& u) const {
        double dist = 0;
        const ProfileSeg* best = best_seg(d, cw, &dist);
        if (!best) {
            if (!cw) eval(d, true, z, u);
            return;
        }
        const double w = best->d0 - best->d1;
        if (dist <= 0) {
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

    // Lateral positions of the strip ends of one class (for matching junction boundaries).
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
        TravelDir dir;
        double width;
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
        x.dir = l.dir;
        x.width = l.width;
        if (x.role == Role::Carriageway) {
            cw_lo = std::min(cw_lo, x.lo);
            cw_hi = std::max(cw_hi, x.hi);
        }
        ls.push_back(std::move(x));
    }
    if (cw_lo > cw_hi) cw_lo = cw_hi = 0;
    pr.cw_lo = cw_lo;
    pr.cw_hi = cw_hi;
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
    const auto push = [&](double d0, double z0, double d1, double z1, const L* lane, int lane_idx, bool face, bool cw,
                          const std::string& surf) {
        if (std::abs(d0 - d1) < 1e-9 && std::abs(z0 - z1) < 1e-9) return;
        ProfileSeg s;
        s.d0 = d0; s.z0 = z0; s.u0 = u;
        u += std::hypot(d0 - d1, z0 - z1);
        s.d1 = d1; s.z1 = z1; s.u1 = u;
        s.surface = surf;
        s.face = face;
        s.cw = cw;
        if (lane) {
            s.role = lane->role;
            s.lane = lane_idx;
            s.driving = lane->role == Role::Carriageway && lane->dir != TravelDir::None;
            s.travel = lane->dir;
            s.lane_centre = lane->centre;
            s.lane_width = lane->width;
        } else {
            s.role = Role::Kerb;
        }
        pr.segs.push_back(std::move(s));
    };
    const double dirt = P.edge_dirt_width;
    for (std::size_t k = 0; k < ls.size(); ++k) {
        const L& l = ls[k];
        const int li = static_cast<int>(k);
        if (k > 0) {
            const L& p = ls[k - 1];
            if (std::abs(p.z_lo - l.z_hi) > 1e-7)
                push(l.hi, p.z_lo, l.hi, l.z_hi, nullptr, -1, true, false, p.z_lo > l.z_hi ? p.surface : l.surface);
        }
        const bool cw = l.role == Role::Carriageway || l.role == Role::Verge;  // verges stay inside the junction carriageway polygon
        if (l.role == Role::Carriageway) {
            // Breaks: the camber crown and the edge-dirt ramps at the carriageway edges.
            std::vector<double> br{l.hi, l.lo};
            if (camber && l.lo + 1e-9 < 0 && l.hi - 1e-9 > 0) br.push_back(0);
            if (std::abs(l.hi - cw_hi) < 1e-9 && l.hi - l.lo > dirt + 0.05) br.push_back(l.hi - dirt);
            if (std::abs(l.lo - cw_lo) < 1e-9 && l.hi - l.lo > dirt + 0.05) br.push_back(l.lo + dirt);
            std::sort(br.begin(), br.end(), std::greater<double>());
            for (std::size_t b = 0; b + 1 < br.size(); ++b) push(br[b], base(br[b]), br[b + 1], base(br[b + 1]), &l, li, false, cw, l.surface);
        } else {
            push(l.hi, l.z_hi, l.lo, l.z_lo, &l, li, false, cw, l.surface);
        }
        if (l.role == Role::Carriageway && pr.cw_surface.empty()) pr.cw_surface = l.surface;
        if (l.role == Role::Raised && pr.raised_surface.empty()) pr.raised_surface = l.surface;
        if (l.role == Role::Kerb && pr.kerb_surface.empty()) pr.kerb_surface = l.surface;
    }

    // Kerb uv: along the kerb, then up the face and across the top.
    for (std::size_t k = 0; k < pr.segs.size(); ++k) {
        ProfileSeg& f = pr.segs[k];
        if (!f.face) continue;
        const double u_base = f.z0 < f.z1 ? f.u0 : f.u1;
        f.kerb_uv = true;
        f.v_ref = u_base;
        f.vdir = 2;
        if (k > 0 && pr.segs[k - 1].role == Role::Kerb && !pr.segs[k - 1].face) {
            ProfileSeg& t = pr.segs[k - 1];
            t.kerb_uv = true;
            t.v_ref = u_base;
            t.vdir = 1;  // left of the face: v grows leftward
        }
        if (k + 1 < pr.segs.size() && pr.segs[k + 1].role == Role::Kerb && !pr.segs[k + 1].face) {
            ProfileSeg& t = pr.segs[k + 1];
            t.kerb_uv = true;
            t.v_ref = u_base;
            t.vdir = 0;
        }
    }

    // Ring vertices: shared between neighbouring strips of one surface unless a kerb face is involved or the
    // strips belong to different driving lanes (their lane-relative uv1 differs).
    for (std::size_t k = 0; k < pr.segs.size(); ++k) {
        ProfileSeg& s = pr.segs[k];
        bool share = false;
        if (k > 0) {
            const ProfileSeg& p = pr.segs[k - 1];
            share = p.surface == s.surface && !p.face && !s.face && std::abs(p.d1 - s.d0) < 1e-9 &&
                    std::abs(p.z1 - s.z0) < 1e-9 && (p.lane == s.lane || (!p.driving && !s.driving)) &&
                    p.kerb_uv == s.kerb_uv;
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

struct Attr {
    Vec3 p;
    Vec2 uv, uv1;
    Float4 col;
    Vec3 tan, bit;  // direction of increasing u and v
};

u32 add_vertex(WorldMesh& m, const Attr& a) {
    m.positions.push_back(a.p);
    m.normals.push_back({});
    m.tangents.push_back(a.tan);
    m.tangent_w.push_back(1);
    m.bitangents.push_back(a.bit);
    m.uv.push_back(a.uv);
    m.uv1.push_back(a.uv1);
    m.colours.push_back(a.col);
    return static_cast<u32>(m.positions.size() - 1);
}

// Appends a triangle and accumulates its area-weighted normal; false (nothing added) if degenerate.
bool add_tri(WorldMesh& m, u32 a, u32 b, u32 c, bool need_up, double min_area2 = 2e-8) {
    const Vec3 n = cross(m.positions[b] - m.positions[a], m.positions[c] - m.positions[a]);
    const double len = length(n);
    if (!(len > min_area2)) return false;
    if (need_up && !(n.z > 0.05 * len)) return false;
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

// Unit normals; tangents made orthogonal to them with the handedness from the v direction.
void finish_normals(WorldMeshSet& set) {
    for (auto& [name, m] : set) {
        for (std::size_t i = 0; i < m.normals.size(); ++i) {
            const Vec3 n = normalized(m.normals[i], {0, 0, 1});
            m.normals[i] = n;
            Vec3 t = m.tangents[i] - n * dot(n, m.tangents[i]);
            if (!(length(t) > 1e-9)) t = cross(n, std::abs(n.z) < 0.9 ? Vec3{0, 0, 1} : Vec3{1, 0, 0});
            t = normalized(t, {1, 0, 0});
            m.tangents[i] = t;
            m.tangent_w[i] = i < m.bitangents.size() && dot(cross(n, t), m.bitangents[i]) < 0 ? -1.0 : 1.0;
        }
        m.bitangents.clear();
    }
}

void drop_empty(WorldMeshSet& set) {
    for (auto it = set.begin(); it != set.end();) it = it->second.indices.empty() ? set.erase(it) : std::next(it);
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

Float4 colour(double r, double g, double b) {
    return {static_cast<float>(std::clamp(r, 0.0, 1.0)), static_cast<float>(std::clamp(g, 0.0, 1.0)),
            static_cast<float>(std::clamp(b, 0.0, 1.0)), 0.0f};
}

}  // namespace

// ---------------------------------------------------------------- public surface queries

struct SurfaceProfile::Impl {
    Profile prof;
};

double SurfaceProfile::offset(double lateral, bool carriageway) const {
    if (!impl_) return 0;
    double z = 0, u = 0;
    impl_->prof.eval(lateral, carriageway, z, u);
    return z;
}
double SurfaceProfile::left() const { return impl_ ? impl_->prof.d_left : 0; }
double SurfaceProfile::right() const { return impl_ ? impl_->prof.d_right : 0; }

SurfaceProfile make_surface_profile(const RegionPack& region, const Edge& edge, const RenderMeshParams& params) {
    const RoadType* rt = region.road_type(edge.road_type);
    if (!rt) return {};
    auto impl = std::make_shared<SurfaceProfile::Impl>();
    impl->prof = build_profile(edge, *rt, region, params);
    if (impl->prof.segs.empty()) return {};
    return SurfaceProfile(std::move(impl));
}

double edge_surface_z(const RoadGraph& graph, const RegionPack& region, EdgeId id, double s, double lateral,
                      const RenderMeshParams& params) {
    const Edge* e = graph.edge(id);
    if (!e) return 0;
    const SurfaceProfile sp = make_surface_profile(region, *e, params);
    if (!sp.valid()) return 0;
    return edge_point(*e, s).z + sp.offset(lateral);
}

namespace {

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
    const double scale = std::clamp(std::sqrt(tol / 0.02), 1.0, 12.0);
    SamplingParams sp;
    sp.max_chord_error = tol;
    sp.max_heading_change = std::clamp(0.05 * std::sqrt(tol / 0.02), 0.05, 0.6);
    sp.max_step = P.max_station_step * scale;
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
        if (out.empty() || s - out.back() > 0.02) out.push_back(s);
    out.back() = s1;
    if (out.size() >= 3 && out[out.size() - 1] - out[out.size() - 2] < 0.02) out.erase(out.end() - 2);

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

// ---------------------------------------------------------------- arms, joints and their ends

// An arm with its trim line, profile and extrapolation frame (the junction surface is built from these).
struct ArmCtx {
    JunctionArm arm;
    Profile prof;
    EdgeId edge;
    double trim = 0, edge_len = 0;
    Vec3 tc;
    Vec2 dir, left;
    double grade = 0;  // dz per metre going outward
    double hl = 0, hr = 0;
    bool at_a = true;
    int index = 0;  // arm index in the junction
};

bool make_ctx(ArmCtx& a, const RoadGraph& graph, const RegionPack& region, const JunctionArm& arm, double trim,
              const RenderMeshParams& P) {
    a.arm = arm;
    const Edge* e = graph.edge(arm.edge);
    const RoadType* rt = e ? region.road_type(e->road_type) : nullptr;
    if (!e || !rt || arm.centreline.empty()) return false;
    a.prof = build_profile(*e, *rt, region, P);
    if (a.prof.segs.empty()) return false;
    a.edge = arm.edge;
    a.trim = trim;
    a.edge_len = e->horizontal.length();
    a.at_a = arm.at_a;
    const ArmPoint c = arm_point(a.arm, trim, 0);
    a.tc = c.pos;
    a.dir = {std::cos(c.heading_out), std::sin(c.heading_out)};
    a.left = {-a.dir.y, a.dir.x};
    const double dl = std::min(2.0, arm.edge_length - trim);
    if (dl > 0.1) a.grade = (arm_point(a.arm, trim + dl, 0).pos.z - c.pos.z) / dl;
    a.hl = arm.half_width_left;
    a.hr = arm.half_width_right;
    return true;
}

// Arm-frame lateral positions of the strip ends of one class.
std::vector<double> arm_laterals(const ArmCtx& a, bool cw) {
    std::vector<double> out;
    for (double d : a.prof.ring_d(cw)) out.push_back(a.at_a ? d : -d);
    return out;
}

struct JointInfo {
    JunctionArm arm[2];
    double trim[2] = {0, 0};
    bool needed = false;
};

// True when `node` has exactly two arms. Seamless pairs (same road type, headings continuing within
// a degree) need no filler and no trim.
bool joint_info(const RoadGraph& graph, const RegionPack& region, NodeId node, const RenderMeshParams& P, JointInfo& out) {
    int ends = 0;
    for (EdgeId id : graph.edges_at(node))
        if (const Edge* e = graph.edge(id)) ends += (e->a == node ? 1 : 0) + (e->b == node ? 1 : 0);
    if (ends != 2) return false;
    const std::vector<JunctionArm> arms = collect_arms(graph, node, region);
    if (arms.size() != 2) return false;
    out.arm[0] = arms[0];
    out.arm[1] = arms[1];
    const Edge *e0 = graph.edge(arms[0].edge), *e1 = graph.edge(arms[1].edge);
    if (!e0 || !e1) return false;
    const bool same = e0->road_type == e1->road_type && e0->one_way == e1->one_way &&
                      std::abs(wrap_angle(arms[0].heading_out - (arms[1].heading_out + kPi))) < 0.0175;
    out.needed = !same;
    for (int i = 0; i < 2; ++i)
        out.trim[i] = out.needed ? std::min(P.joint_length, arms[static_cast<std::size_t>(i)].edge_length / 2) : 0.0;
    return true;
}

struct EndRef {
    std::optional<JunctionArm> arm;
    double trim = 0;
    bool stop = false;  // a stop / give-way line sits here (junction with 3+ arms)
};

EndRef find_end(const RoadGraph& graph, const RegionPack& region, const JunctionSet& js, const Edge& e, bool at_a,
                const RenderMeshParams& P) {
    const NodeId node = at_a ? e.a : e.b;
    const Junction* j = js.find(node);
    if (j && j->arms.size() >= 3 && j->trims.size() == j->arms.size()) {
        for (std::size_t i = 0; i < j->arms.size(); ++i)
            if (j->arms[i].edge == e.id && j->arms[i].at_a == at_a) return {j->arms[i], j->trims[i], true};
        return {};
    }
    JointInfo ji;
    if (!joint_info(graph, region, node, P, ji) || !ji.needed) return {};
    for (int i = 0; i < 2; ++i)
        if (ji.arm[i].edge == e.id && ji.arm[i].at_a == at_a) return {ji.arm[i], ji.trim[i], false};
    return {};
}

}  // namespace

// ---------------------------------------------------------------- edge meshes

WorldMeshSet build_edge_mesh(const RoadGraph& graph, const RegionPack& region, EdgeId id, const JunctionSet& junctions,
                             const RenderMeshParams& P, EdgeBoundary* boundary) {
    WorldMeshSet out;
    if (boundary) *boundary = {};
    const Edge* e = graph.edge(id);
    if (!e || e->horizontal.spans.empty()) return out;
    const RoadType* rt = region.road_type(e->road_type);
    if (!rt) return out;
    const double len = e->horizontal.length();
    const EndRef ea = find_end(graph, region, junctions, *e, true, P), eb = find_end(graph, region, junctions, *e, false, P);
    const double s0 = ea.trim, s1 = len - eb.trim;
    if (!(s1 - s0 > 0.01)) return out;
    const Profile pr = build_profile(*e, *rt, region, P);
    if (pr.segs.empty()) return out;
    const std::vector<double> st = select_stations(*e, s0, s1, pr, P);
    const std::size_t n = st.size();
    if (n < 2) return out;

    std::vector<std::vector<u32>> vid(n, std::vector<u32>(pr.verts.size()));
    std::vector<std::vector<Vec3>> world(boundary ? n : 0);
    for (std::size_t i = 0; i < n; ++i) {
        const EndRef* end = i == 0 && ea.arm ? &ea : (i + 1 == n && eb.arm ? &eb : nullptr);
        const Frame f = centre_frame(*e, st[i]);
        double cz = f.z, h = f.h;
        if (end) {
            const ArmPoint c = arm_point(*end->arm, end->trim, 0);
            cz = c.pos.z;
            h = end->arm->at_a ? c.heading_out : c.heading_out + kPi;
        }
        const Vec3 right{std::sin(h), -std::cos(h), 0}, fwd{std::cos(h), std::sin(h), 0};
        const double low = lowfreq(id, st[i], P.lowfreq_period);
        if (boundary) world[i].resize(pr.verts.size());
        for (std::size_t v = 0; v < pr.verts.size(); ++v) {
            const ProfileVert& pv = pr.verts[v];
            const ProfileSeg& sg = pr.segs[static_cast<std::size_t>(pv.seg)];
            Vec3 p;
            if (end) {
                const ArmPoint a = arm_point(*end->arm, end->trim, end->arm->at_a ? pv.d : -pv.d);
                p = {a.pos.x, a.pos.y, cz + pv.z};
            } else {
                p = {f.p.x - pv.d * std::sin(f.h), f.p.y + pv.d * std::cos(f.h), cz + pv.z};
            }
            if (boundary) world[i][v] = p;
            Attr a;
            a.p = p;
            if (sg.kerb_uv) {
                a.uv = {st[i], std::abs(pv.u - sg.v_ref)};
                a.tan = fwd;
                a.bit = sg.vdir == 2 ? Vec3{0, 0, 1} : (sg.vdir == 0 ? right : right * -1.0);
            } else {
                a.uv = {pv.u, st[i]};
                const double dd = sg.d0 - sg.d1, dz = sg.z1 - sg.z0;
                a.tan = normalized(right * dd + Vec3{0, 0, dz}, right);
                a.bit = fwd;
            }
            // Masks (layout in the header).
            double R = 0, G = 0;
            if (sg.driving) {
                double boost = 0;
                if (ea.stop && sg.travel == TravelDir::Backward)
                    boost = std::max(boost, 1 - std::clamp((st[i] - s0) / P.wear_approach_length, 0.0, 1.0));
                if (eb.stop && sg.travel == TravelDir::Forward)
                    boost = std::max(boost, 1 - std::clamp((s1 - st[i]) / P.wear_approach_length, 0.0, 1.0));
                R = 0.55 + 0.45 * boost;
                a.uv1 = {pv.d - sg.lane_centre, sg.lane_width};
            }
            if (sg.face) G = 1;
            else if (sg.role == Role::Kerb) G = 1;
            else if (sg.role == Role::Raised) G = 0.5;
            else if (sg.role == Role::Verge) G = 0.3;
            else G = 1 - std::min(std::abs(pv.d - pr.cw_lo), std::abs(pr.cw_hi - pv.d)) / P.edge_dirt_width;
            a.col = colour(R, G, low);
            vid[i][v] = add_vertex(out[sg.surface], a);
        }
    }
    if (boundary) {
        for (std::size_t i = 0; i < n; ++i) {
            boundary->left.push_back(world[i][static_cast<std::size_t>(pr.segs.front().va)]);
            boundary->right.push_back(world[i][static_cast<std::size_t>(pr.segs.back().vb)]);
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
            WorldMesh& m = out[s.surface];
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

// ---------------------------------------------------------------- triangle refinement and ring simplification

using Tri = std::array<u32, 3>;

double area2p(Vec2 a, Vec2 b, Vec2 c) { return (b - a).cross(c - a); }

// Splits every edge longer than max_len (consistently for both triangles sharing it) until none is.
void refine(std::vector<Vec2>& pts, std::vector<Tri>& tris, double max_len) {
    for (int pass = 0; pass < 12; ++pass) {
        std::map<std::pair<u32, u32>, u32> mids;
        bool any = false;
        const auto is_long = [&](u32 a, u32 b) { return (pts[a] - pts[b]).length() > max_len; };
        const auto mid = [&](u32 a, u32 b) {
            const auto key = std::make_pair(std::min(a, b), std::max(a, b));
            auto it = mids.find(key);
            if (it != mids.end()) return it->second;
            pts.push_back((pts[a] + pts[b]) * 0.5);
            const u32 v = static_cast<u32>(pts.size() - 1);
            mids.emplace(key, v);
            return v;
        };
        std::vector<Tri> next;
        for (const Tri& t : tris) {
            const bool s[3] = {is_long(t[0], t[1]), is_long(t[1], t[2]), is_long(t[2], t[0])};
            const int count = (s[0] ? 1 : 0) + (s[1] ? 1 : 0) + (s[2] ? 1 : 0);
            if (count == 0) {
                next.push_back(t);
                continue;
            }
            any = true;
            if (count == 3) {
                const u32 a = mid(t[0], t[1]), b = mid(t[1], t[2]), c = mid(t[2], t[0]);
                next.push_back({t[0], a, c});
                next.push_back({a, t[1], b});
                next.push_back({c, b, t[2]});
                next.push_back({a, b, c});
                continue;
            }
            // Rotate so the split edges come first: (v0 v1) [and (v1 v2)].
            int k = 0;
            if (count == 1) {
                k = s[0] ? 0 : (s[1] ? 1 : 2);
            } else {
                k = !s[2] ? 0 : (!s[0] ? 1 : 2);  // the unsplit edge is (v(k+2), v(k))
            }
            const u32 v0 = t[static_cast<std::size_t>(k)], v1 = t[static_cast<std::size_t>((k + 1) % 3)],
                      v2 = t[static_cast<std::size_t>((k + 2) % 3)];
            if (count == 1) {
                const u32 m = mid(v0, v1);
                next.push_back({v0, m, v2});
                next.push_back({m, v1, v2});
            } else {
                const u32 m0 = mid(v0, v1), m1 = mid(v1, v2);
                next.push_back({m0, v1, m1});
                next.push_back({v0, m0, m1});
                next.push_back({v0, m1, v2});
            }
        }
        tris = std::move(next);
        if (!any) break;
    }
}

double incircle(Vec2 a, Vec2 b, Vec2 c, Vec2 d) {
    const double adx = a.x - d.x, ady = a.y - d.y, bdx = b.x - d.x, bdy = b.y - d.y, cdx = c.x - d.x, cdy = c.y - d.y;
    return (adx * adx + ady * ady) * (bdx * cdy - cdx * bdy) + (bdx * bdx + bdy * bdy) * (cdx * ady - adx * cdy) +
           (cdx * cdx + cdy * cdy) * (adx * bdy - bdx * ady);
}

// Lawson flips toward a Delaunay triangulation: turns the fans of slivers that ear clipping makes into
// well shaped triangles (boundary edges are never shared, so the outline is untouched).
void flip_delaunay(const std::vector<Vec2>& pts, std::vector<Tri>& tris) {
    for (int pass = 0; pass < 30; ++pass) {
        std::map<std::pair<u32, u32>, std::vector<std::size_t>> edges;
        for (std::size_t t = 0; t < tris.size(); ++t)
            for (std::size_t k = 0; k < 3; ++k) {
                const u32 a = tris[t][k], b = tris[t][(k + 1) % 3];
                edges[{std::min(a, b), std::max(a, b)}].push_back(t);
            }
        std::set<std::size_t> dirty;
        bool changed = false;
        for (const auto& [key, ts] : edges) {
            if (ts.size() != 2 || dirty.count(ts[0]) || dirty.count(ts[1])) continue;
            u32 a = key.first, b = key.second, c = 0, d = 0;
            for (u32 v : tris[ts[0]])
                if (v != a && v != b) c = v;
            for (u32 v : tris[ts[1]])
                if (v != a && v != b) d = v;
            if ((pts[a] - pts[b]).length() < 1e-9) continue;
            if (area2p(pts[a], pts[b], pts[c]) < 0) std::swap(a, b);
            if (!(area2p(pts[a], pts[b], pts[c]) > 1e-9) || !(area2p(pts[b], pts[a], pts[d]) > 1e-9)) continue;
            if (!(incircle(pts[a], pts[b], pts[c], pts[d]) > 1e-9)) continue;
            // The new diagonal c-d must lie inside the quad a d b c.
            if (!(area2p(pts[a], pts[d], pts[c]) > 1e-7 && area2p(pts[d], pts[b], pts[c]) > 1e-7)) continue;
            tris[ts[0]] = {a, d, c};
            tris[ts[1]] = {d, b, c};
            dirty.insert(ts[0]);
            dirty.insert(ts[1]);
            changed = true;
        }
        if (!changed) break;
    }
}

// Douglas-Peucker on an open run; returns keep flags (ends always kept).
std::vector<char> dp_run(const std::vector<Vec2>& run, double tol) {
    std::vector<char> keep(run.size(), 0);
    if (run.empty()) return keep;
    keep.front() = keep.back() = 1;
    std::vector<std::pair<std::size_t, std::size_t>> stack{{0, run.size() - 1}};
    while (!stack.empty()) {
        const auto [a, b] = stack.back();
        stack.pop_back();
        if (b <= a + 1) continue;
        double worst = -1;
        std::size_t wi = a;
        const Vec2 pa = run[a], pb = run[b];
        const Vec2 ab = pb - pa;
        const double l2 = ab.dot(ab);
        for (std::size_t i = a + 1; i < b; ++i) {
            const double t = l2 > 0 ? std::clamp((run[i] - pa).dot(ab) / l2, 0.0, 1.0) : 0.0;
            const double d = (run[i] - (pa + ab * t)).length();
            if (d > worst) {
                worst = d;
                wi = i;
            }
        }
        if (worst > tol) {
            keep[wi] = 1;
            stack.push_back({a, wi});
            stack.push_back({wi, b});
        }
    }
    return keep;
}

// Simplifies a closed ring to within tol of the original, never dropping anchored vertices.
std::vector<Vec2> simplify_ring(const std::vector<Vec2>& ring, std::vector<char> anchor, double tol) {
    const std::size_t n = ring.size();
    if (n < 4 || tol <= 1e-4) return ring;
    anchor.resize(n, 0);
    std::vector<std::size_t> an;
    for (std::size_t i = 0; i < n; ++i)
        if (anchor[i]) an.push_back(i);
    if (an.size() < 2) {
        std::size_t far = 0;
        double best = -1;
        const std::size_t first = an.empty() ? 0 : an[0];
        for (std::size_t i = 0; i < n; ++i) {
            const double d = (ring[i] - ring[first]).length();
            if (d > best) {
                best = d;
                far = i;
            }
        }
        anchor[first] = 1;
        anchor[far] = 1;
        an.clear();
        for (std::size_t i = 0; i < n; ++i)
            if (anchor[i]) an.push_back(i);
    }
    std::vector<char> keep(n, 0);
    for (std::size_t k = 0; k < an.size(); ++k) {
        const std::size_t a = an[k], b = an[(k + 1) % an.size()];
        std::vector<Vec2> run;
        std::vector<std::size_t> idx;
        for (std::size_t i = a;; i = (i + 1) % n) {
            run.push_back(ring[i]);
            idx.push_back(i);
            if (i == b && idx.size() > 1) break;
            if (idx.size() > n + 1) break;
        }
        const std::vector<char> kr = dp_run(run, tol);
        for (std::size_t i = 0; i < run.size(); ++i)
            if (kr[i]) keep[idx[i]] = 1;
    }
    std::vector<Vec2> out;
    for (std::size_t i = 0; i < n; ++i)
        if (keep[i]) out.push_back(ring[i]);
    return out.size() >= 3 ? out : ring;
}

double junction_edge_length(const RenderMeshParams& P) { return std::clamp(100 * P.chord_tolerance + 4.0, 4.0, 30.0); }

// ---------------------------------------------------------------- junction meshes

struct Blended {
    double z = 0, b = 0;
};

Blended blend(const std::vector<ArmCtx>& arms, Vec2 p, bool cw, double period) {
    Blended acc;
    double wsum = 0;
    for (const ArmCtx& a : arms) {
        const Vec2 r = p - a.tc.xy();
        const double l = r.dot(a.left), t = r.dot(a.dir);
        const double lc = std::clamp(l, -a.hr, a.hl);
        const double dist = std::hypot(l - lc, t);
        double pz = 0, pu = 0;
        a.prof.eval(a.at_a ? l : -l, cw, pz, pu);
        const double z = a.tc.z + a.grade * t + pz;
        const double s_edge = a.at_a ? a.trim + t : a.edge_len - a.trim - t;
        const double bv = lowfreq(a.edge, s_edge, period);
        if (dist < 1e-7) return {z, bv};
        const double w = 1.0 / (dist * dist);
        acc.z += w * z;
        acc.b += w * bv;
        wsum += w;
    }
    if (wsum <= 0) return {};
    acc.z /= wsum;
    acc.b /= wsum;
    return acc;
}

struct Conformed {
    std::vector<Vec2> pts;
    std::vector<char> anchor;
};

// Snaps vertices that lie on an arm's trim line exactly onto it (same arm_point() as the edge mesh)
// and inserts the edge cross-section's ring points along trim-line segments, so seams are welded.
Conformed conform_ring(const std::vector<Vec2>& ring, const std::vector<ArmCtx>& arms, bool cw) {
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
            if (std::abs(t) > kSnapTol || l < -a.hr - kSnapLat || l > a.hl + kSnapLat) continue;
            for (double c : {a.arm.kerb_left, -a.arm.kerb_right, a.hl, -a.hr})
                if (std::abs(l - c) < kSnapLat) {
                    l = c;
                    break;
                }
            // A pavement vertex inside the arm's carriageway width is a boolean-op artefact: move it to the kerb.
            if (!cw && l < a.arm.kerb_left - kSnapLat && l > -a.arm.kerb_right + kSnapLat)
                l = l >= 0 ? a.arm.kerb_left : -a.arm.kerb_right;
            v.arm = static_cast<int>(i);
            v.l = l;
            v.p = arm_point(a.arm, a.trim, l).pos.xy();
        }
        if (!sv.empty() && (sv.back().p - v.p).length() < 1e-7) continue;
        sv.push_back(v);
    }
    while (sv.size() > 1 && (sv.front().p - sv.back().p).length() < 1e-7) sv.pop_back();
    Conformed out;
    for (std::size_t i = 0; i < sv.size(); ++i) {
        const SV &a = sv[i], &b = sv[(i + 1) % sv.size()];
        out.pts.push_back(a.p);
        out.anchor.push_back(a.arm >= 0 ? 1 : 0);
        if (a.arm < 0 || a.arm != b.arm) continue;
        const ArmCtx& ac = arms[static_cast<std::size_t>(a.arm)];
        std::vector<double> ls;
        for (double l : arm_laterals(ac, cw))
            if (l > std::min(a.l, b.l) + 1e-6 && l < std::max(a.l, b.l) - 1e-6) ls.push_back(l);
        std::sort(ls.begin(), ls.end());
        if (a.l > b.l) std::reverse(ls.begin(), ls.end());
        for (double l : ls) {
            out.pts.push_back(arm_point(ac.arm, ac.trim, l).pos.xy());
            out.anchor.push_back(1);
        }
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

struct Seg2 {
    Vec2 a, b;
};

double min_dist(const std::vector<Seg2>& segs, Vec2 p) {
    double d = kInf;
    for (const Seg2& s : segs) d = std::min(d, dist_point_segment(p, s.a, s.b));
    return d;
}

// Vertical face along A -> B (feet at a, b; tops dza/dzb above). The front is the left of A -> B when
// `left_facing`. uv = (metres along the kerb, metres up the face).
void add_face(WorldMesh& m, Vec3 a, Vec3 b, double dza, double dzb, double run_a, double run_b, bool left_facing,
              Float4 col) {
    const Vec3 tan = normalized({b.x - a.x, b.y - a.y, 0}, {1, 0, 0});
    const Vec3 up{0, 0, 1};
    const auto V = [&](Vec3 p, double u, double v) {
        Attr at;
        at.p = p;
        at.uv = {u, v};
        at.col = col;
        at.tan = tan;
        at.bit = up;
        return add_vertex(m, at);
    };
    const u32 va = V(a, run_a, 0), vb = V(b, run_b, 0);
    const u32 ta = V({a.x, a.y, a.z + dza}, run_a, dza), tb = V({b.x, b.y, b.z + dzb}, run_b, dzb);
    if (left_facing) {
        add_tri(m, va, ta, tb, false);
        add_tri(m, va, tb, vb, false);
    } else {
        add_tri(m, va, vb, tb, false);
        add_tri(m, va, tb, ta, false);
    }
}

struct JunctionFrame {
    Vec2 o, dir, left;
    Vec2 uv(Vec2 p) const { return {-(p - o).dot(left), (p - o).dot(dir)}; }
};

}  // namespace

struct JunctionSurface::Impl {
    std::vector<ArmCtx> arms;
    double period = 30;
};

JunctionSurface::JunctionSurface(const RoadGraph& graph, const RegionPack& region, const Junction& j,
                                 const RenderMeshParams& P) {
    if (j.arms.empty() || j.trims.size() != j.arms.size()) return;
    auto impl = std::make_shared<Impl>();
    impl->period = P.lowfreq_period;
    for (std::size_t i = 0; i < j.arms.size(); ++i) {
        ArmCtx a;
        if (!make_ctx(a, graph, region, j.arms[i], j.trims[i], P)) continue;
        a.index = static_cast<int>(i);
        impl->arms.push_back(std::move(a));
    }
    if (!impl->arms.empty()) impl_ = std::move(impl);
}

double JunctionSurface::z(Vec2 p, bool carriageway) const {
    return impl_ ? blend(impl_->arms, p, carriageway, impl_->period).z : 0.0;
}

double junction_surface_z(const RoadGraph& graph, const RegionPack& region, const Junction& junction, Vec2 p,
                          bool carriageway, const RenderMeshParams& params) {
    return JunctionSurface(graph, region, junction, params).z(p, carriageway);
}

WorldMeshSet build_junction_mesh(const RoadGraph& graph, const RegionPack& region, const Junction& j,
                                 const RenderMeshParams& P, bool* ok_out) {
    WorldMeshSet out;
    bool ok = true;
    if (ok_out) *ok_out = true;
    if (j.arms.empty() || j.trims.size() != j.arms.size()) return out;
    if (j.carriageway.empty() && j.pavements.empty()) return out;

    std::vector<ArmCtx> arms;
    for (std::size_t i = 0; i < j.arms.size(); ++i) {
        ArmCtx a;
        if (!make_ctx(a, graph, region, j.arms[i], j.trims[i], P)) continue;
        a.index = static_cast<int>(i);
        arms.push_back(std::move(a));
    }
    if (arms.empty()) return out;
    std::string cw_surface = P.default_surface, band_surface, kerb_surface = P.island_kerb_surface;
    for (const ArmCtx& a : arms)
        if (!a.prof.cw_surface.empty()) { cw_surface = a.prof.cw_surface; break; }
    for (const ArmCtx& a : arms)
        if (!a.prof.raised_surface.empty()) { band_surface = a.prof.raised_surface; break; }
    for (const ArmCtx& a : arms)
        if (!a.prof.kerb_surface.empty()) { kerb_surface = a.prof.kerb_surface; break; }
    if (band_surface.empty()) band_surface = cw_surface;

    // Planar uv in the frame of the major arm.
    const ArmCtx* major = &arms.front();
    if (!j.major_arms.empty())
        for (const ArmCtx& a : arms)
            if (a.index == j.major_arms.front()) major = &a;
    const JunctionFrame frame{major->tc.xy(), major->dir, major->left};
    const Vec3 u_dir{frame.dir.y, -frame.dir.x, 0}, v_dir{frame.dir.x, frame.dir.y, 0};

    const double tol = P.chord_tolerance;
    const auto prepare = [&](const PolygonSet& set, bool cw) {
        std::vector<std::vector<Vec2>> rings;
        for (const auto& r : set) {
            if (r.size() < 3 || !finite_ring(r)) continue;
            Conformed c = conform_ring(r, arms, cw);
            if (c.pts.size() < 3) continue;
            std::vector<Vec2> s = simplify_ring(c.pts, c.anchor, tol);
            if (s.size() >= 3) rings.push_back(std::move(s));
        }
        return rings;
    };
    const auto cw_rings = prepare(j.carriageway, true);
    const auto band_rings = prepare(j.pavements, false);

    // Connector paths (wear) and carriageway boundary (dirt).
    std::vector<Seg2> paths;
    for (const LaneConnector& c : j.connectors) {
        if (!c.enabled) continue;
        for (std::size_t i = 0; i + 1 < c.path.size(); ++i) paths.push_back({c.path[i].xy(), c.path[i + 1].xy()});
    }
    const auto on_trim = [&](Vec2 p) {
        for (const ArmCtx& a : arms)
            if (std::abs((p - a.tc.xy()).dot(a.dir)) < 1e-6) return true;
        return false;
    };
    std::vector<Seg2> boundary;
    for (const auto& r : cw_rings)
        for (std::size_t i = 0; i < r.size(); ++i) {
            const Vec2 A = r[i], B = r[(i + 1) % r.size()];
            if (!(on_trim(A) && on_trim(B))) boundary.push_back({A, B});
        }

    const double maxlen = junction_edge_length(P);
    const auto polygon = [&](const std::vector<std::vector<Vec2>>& rings, bool cw, WorldMesh& m) {
        std::vector<Vec2> pts;
        std::vector<std::vector<u32>> idx;
        for (const auto& r : rings) {
            std::vector<u32> ring;
            for (const Vec2& p : r) {
                ring.push_back(static_cast<u32>(pts.size()));
                pts.push_back(p);
            }
            idx.push_back(std::move(ring));
        }
        std::vector<Tri> tris;
        if (!triangulate_polygons(pts, idx, tris)) ok = false;
        flip_delaunay(pts, tris);
        refine(pts, tris, maxlen);
        flip_delaunay(pts, tris);
        std::vector<u32> vmap(pts.size(), 0xFFFFFFFFu);
        const auto vert = [&](u32 i) {
            if (vmap[i] == 0xFFFFFFFFu) {
                const Blended bl = blend(arms, pts[i], cw, P.lowfreq_period);
                Attr a;
                a.p = {pts[i].x, pts[i].y, bl.z};
                a.uv = frame.uv(pts[i]);
                a.tan = u_dir;
                a.bit = v_dir;
                if (cw) {
                    const double dp = min_dist(paths, pts[i]);
                    const double wear = paths.empty() ? 0.3 : 0.3 + 0.7 * std::exp(-(dp / 1.8) * (dp / 1.8));
                    const double dirt = boundary.empty() ? 0.0 : 1 - min_dist(boundary, pts[i]) / P.edge_dirt_width;
                    a.col = colour(wear, dirt, bl.b);
                } else {
                    a.col = colour(0, 0.5, bl.b);
                }
                vmap[i] = add_vertex(m, a);
            }
            return vmap[i];
        };
        for (const Tri& t : tris) add_tri(m, vert(t[0]), vert(t[1]), vert(t[2]), true);
    };
    polygon(cw_rings, true, out[cw_surface]);
    polygon(band_rings, false, out[band_surface]);

    // Kerb faces where the carriageway boundary runs along a pavement edge (not on a trim line).
    std::vector<Seg2> pav;
    for (const auto& r : band_rings)
        for (std::size_t i = 0; i < r.size(); ++i) pav.push_back({r[i], r[(i + 1) % r.size()]});
    WorldMesh& kerb = out[kerb_surface];
    const double along_tol = 2e-3 + 1.5 * tol;
    for (const auto& r : cw_rings) {
        double run = 0;
        for (std::size_t i = 0; i < r.size(); ++i) {
            const Vec2 A = r[i], B = r[(i + 1) % r.size()];
            const double l = (B - A).length();
            const double run_a = run;
            run += l;
            if (l < 1e-4 || (on_trim(A) && on_trim(B))) continue;
            if (min_dist(pav, (A + B) * 0.5) >= along_tol) continue;
            const Blended ba = blend(arms, A, true, P.lowfreq_period), bb = blend(arms, B, true, P.lowfreq_period);
            const double dza = blend(arms, A, false, P.lowfreq_period).z - ba.z;
            const double dzb = blend(arms, B, false, P.lowfreq_period).z - bb.z;
            if (dza < 1e-4 || dzb < 1e-4) continue;
            add_face(kerb, {A.x, A.y, ba.z}, {B.x, B.y, bb.z}, dza, dzb, run_a, run, true, colour(0, 1, ba.b));
        }
    }
    finish_normals(out);
    drop_empty(out);
    add_section(out, j.node);
    if (ok_out) *ok_out = ok;
    return out;
}

// ---------------------------------------------------------------- joint filler

WorldMeshSet build_joint_mesh(const RoadGraph& graph, const RegionPack& region, NodeId node, const RenderMeshParams& P) {
    WorldMeshSet out;
    JointInfo ji;
    if (!joint_info(graph, region, node, P, ji) || !ji.needed) return out;
    ArmCtx c[2];
    for (int i = 0; i < 2; ++i)
        if (!make_ctx(c[i], graph, region, ji.arm[i], ji.trim[i], P)) return out;

    // Laterals in arm 0's frame; arm 1's lateral is the negative (its left is our right).
    const auto mapped = [&](bool cw) {
        std::vector<double> l = arm_laterals(c[0], cw);
        for (double v : arm_laterals(c[1], cw)) l.push_back(-v);
        std::sort(l.begin(), l.end(), std::greater<double>());
        l.erase(std::unique(l.begin(), l.end(), [](double a, double b) { return std::abs(a - b) < 1e-6; }), l.end());
        return l;
    };
    const auto extent = [&](const ArmCtx& a, bool arm1, double& lo, double& hi) {
        const double l0 = a.at_a ? a.prof.cw_lo : -a.prof.cw_hi, l1 = a.at_a ? a.prof.cw_hi : -a.prof.cw_lo;
        lo = arm1 ? -l1 : l0;
        hi = arm1 ? -l0 : l1;
    };
    double lo0, hi0, lo1, hi1;
    extent(c[0], false, lo0, hi0);
    extent(c[1], true, lo1, hi1);
    const double cw_lo = std::min(lo0, lo1), cw_hi = std::max(hi0, hi1);

    const JunctionFrame frame{c[0].tc.xy(), c[0].dir, c[0].left};
    const Vec3 u_dir{frame.dir.y, -frame.dir.x, 0}, v_dir{frame.dir.x, frame.dir.y, 0};
    const double low = lowfreq(c[0].edge, c[0].trim, P.lowfreq_period);
    const auto corner = [&](int arm, double l, bool cw) {
        const ArmCtx& a = c[arm];
        const double lat = std::clamp(arm == 0 ? l : -l, -a.hr, a.hl);
        const ArmPoint ap = arm_point(a.arm, a.trim, lat);
        double z = 0, u = 0;
        a.prof.eval(a.at_a ? lat : -lat, cw, z, u);
        return Vec3{ap.pos.x, ap.pos.y, a.tc.z + z};
    };
    const auto strip = [&](const std::vector<double>& ls, bool cw) {
        for (std::size_t k = 0; k + 1 < ls.size(); ++k) {
            const double l0 = ls[k], l1 = ls[k + 1], mid = 0.5 * (l0 + l1);
            if (!cw && mid > cw_lo && mid < cw_hi) continue;
            std::string surface;
            for (int arm = 0; arm < 2 && surface.empty(); ++arm) {
                double dist = 0;
                const double lat = arm == 0 ? mid : -mid;
                const ProfileSeg* s = c[arm].prof.best_seg(c[arm].at_a ? lat : -lat, cw, &dist);
                if (s && dist <= 1e-9) surface = s->surface;
            }
            if (surface.empty()) {
                const ProfileSeg* s = c[0].prof.best_seg(c[0].at_a ? mid : -mid, cw);
                if (!s) continue;
                surface = s->surface;
            }
            WorldMesh& m = out[surface];
            const Vec3 q[4] = {corner(0, l0, cw), corner(0, l1, cw), corner(1, l1, cw), corner(1, l0, cw)};
            u32 v[4];
            for (int i = 0; i < 4; ++i) {
                Attr a;
                a.p = q[i];
                a.uv = frame.uv(q[i].xy());
                a.tan = u_dir;
                a.bit = v_dir;
                a.col = colour(0, cw ? 0.3 : 0.5, low);
                v[i] = add_vertex(m, a);
            }
            const u32 tri[2][3] = {{v[0], v[1], v[2]}, {v[0], v[2], v[3]}};
            for (const auto& t : tri) {
                const Vec3 n = cross(m.positions[t[1]] - m.positions[t[0]], m.positions[t[2]] - m.positions[t[0]]);
                if (n.z >= 0) add_tri(m, t[0], t[1], t[2], true, 1e-6);
                else add_tri(m, t[0], t[2], t[1], true, 1e-6);
            }
        }
    };
    strip(mapped(true), true);
    strip(mapped(false), false);
    finish_normals(out);
    drop_empty(out);
    add_section(out, node);
    return out;
}

namespace {

// ---------------------------------------------------------------- roundabout islands

struct RoadRef {
    const Edge* e = nullptr;
    Profile prof;
    std::vector<EdgeSample> c;
};

// Road surface height at p from the nearest of the given edges: centreline + carriageway profile.
double road_height_at(const std::vector<RoadRef>& refs, Vec2 p) {
    double best = kInf, z = 0, lat = 0;
    const RoadRef* nearest = nullptr;
    for (const RoadRef& r : refs) {
        for (std::size_t i = 0; i + 1 < r.c.size(); ++i) {
            const Vec2 a = r.c[i].pos.xy(), b = r.c[i + 1].pos.xy();
            const Vec2 ab = b - a;
            const double l2 = ab.dot(ab);
            const double t = l2 > 0 ? std::clamp((p - a).dot(ab) / l2, 0.0, 1.0) : 0.0;
            const double d = (p - (a + ab * t)).length();
            if (d < best) {
                best = d;
                nearest = &r;
                z = r.c[i].pos.z + (r.c[i + 1].pos.z - r.c[i].pos.z) * t;
                lat = ab.cross(p - a) >= 0 ? d : -d;
            }
        }
    }
    if (!nearest) return 0;
    double pz = 0, pu = 0;
    nearest->prof.eval(lat, true, pz, pu);
    return z + pz;
}

// Raised polygon (top + kerb faces) over a base height function.
void add_raised_polygon(WorldMeshSet& out, const PolygonSet& poly, const std::string& top_surface,
                        const std::function<double(Vec2)>& base_z, double H, const RenderMeshParams& P) {
    std::vector<Vec2> pts;
    std::vector<std::vector<u32>> idx;
    std::vector<std::vector<Vec2>> rings;
    for (const auto& r : poly) {
        if (r.size() < 3 || !finite_ring(r)) continue;
        std::vector<Vec2> s = simplify_ring(r, {}, P.chord_tolerance);
        std::vector<u32> ring;
        for (const Vec2& p : s) {
            ring.push_back(static_cast<u32>(pts.size()));
            pts.push_back(p);
        }
        idx.push_back(std::move(ring));
        rings.push_back(std::move(s));
    }
    std::vector<Tri> tris;
    triangulate_polygons(pts, idx, tris);
    flip_delaunay(pts, tris);
    refine(pts, tris, junction_edge_length(P));
    flip_delaunay(pts, tris);
    WorldMesh& top = out[top_surface];
    std::vector<u32> vmap(pts.size(), 0xFFFFFFFFu);
    const auto vert = [&](u32 i) {
        if (vmap[i] == 0xFFFFFFFFu) {
            Attr a;
            a.p = {pts[i].x, pts[i].y, base_z(pts[i]) + H};
            a.uv = pts[i];
            a.col = colour(0, 0, 0.5);
            a.tan = {1, 0, 0};
            a.bit = {0, 1, 0};
            vmap[i] = add_vertex(top, a);
        }
        return vmap[i];
    };
    for (const Tri& t : tris) add_tri(top, vert(t[0]), vert(t[1]), vert(t[2]), true);
    WorldMesh& kerb = out[P.island_kerb_surface];
    for (const auto& r : rings) {
        double run = 0;
        for (std::size_t i = 0; i < r.size(); ++i) {
            const Vec2 A = r[i], B = r[(i + 1) % r.size()];
            const double l = (B - A).length();
            if (l < 1e-4) continue;
            add_face(kerb, {A.x, A.y, base_z(A)}, {B.x, B.y, base_z(B)}, H, H, run, run + l, false, colour(0, 1, 0.5));
            run += l;
        }
    }
}

}  // namespace

WorldMeshSet build_roundabout_mesh(const RoadGraph& graph, const RegionPack& region, const RoundaboutLayout& layout,
                                   const RenderMeshParams& P) {
    WorldMeshSet out;
    if (!layout.ok) return out;
    const RoundaboutRules& rules = region.roundabout;
    double H = rules.island_kerb_upstand > 0 ? rules.island_kerb_upstand : P.default_kerb_upstand;
    const auto make_ref = [&](EdgeId id) {
        RoadRef r;
        r.e = graph.edge(id);
        if (!r.e) return r;
        const RoadType* rt = region.road_type(r.e->road_type);
        if (!rt) {
            r.e = nullptr;
            return r;
        }
        if (rules.island_kerb_upstand <= 0 && rt->kerb_upstand) H = *rt->kerb_upstand;
        r.prof = build_profile(*r.e, *rt, region, P);
        r.c = sample_centreline(*r.e);
        return r;
    };
    std::vector<RoadRef> ring;
    for (EdgeId id : layout.group.ring_edges) {
        RoadRef r = make_ref(id);
        if (r.e && !r.prof.segs.empty()) ring.push_back(std::move(r));
    }
    if (!layout.island.empty() && !ring.empty())
        add_raised_polygon(out, layout.island, rules.central_island_surface,
                           [&](Vec2 p) { return road_height_at(ring, p); }, H, P);
    for (const SplitterIsland& sp : layout.splitters) {
        if (sp.arm < 0 || static_cast<std::size_t>(sp.arm) >= layout.group.arms.size() || sp.polygon.size() < 3) continue;
        RoadRef r = make_ref(layout.group.arms[static_cast<std::size_t>(sp.arm)].edge);
        if (!r.e || r.prof.segs.empty()) continue;
        const std::vector<RoadRef> one{std::move(r)};
        add_raised_polygon(out, PolygonSet{sp.polygon}, rules.splitter_surface,
                           [&](Vec2 p) { return road_height_at(one, p); }, H, P);
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
        d.tangent_w.insert(d.tangent_w.end(), m.tangent_w.begin(), m.tangent_w.end());
        d.uv.insert(d.uv.end(), m.uv.begin(), m.uv.end());
        d.uv1.insert(d.uv1.end(), m.uv1.begin(), m.uv1.end());
        d.colours.insert(d.colours.end(), m.colours.begin(), m.colours.end());
        for (u32 i : m.indices) d.indices.push_back(i + base);
        for (SectionRange s : m.sections) {
            s.first_index += ibase;
            d.sections.push_back(s);
        }
    }
}

// ---------------------------------------------------------------- float buffers and chunks

namespace {

std::uint8_t to_byte(float v) { return static_cast<std::uint8_t>(std::lround(std::clamp(v, 0.0f, 1.0f) * 255.0f)); }

void push_vertex(MeshBuffers& b, const WorldMesh& m, std::size_t i, Vec3 origin) {
    const Vec3 p = m.positions[i], n = m.normals[i], t = m.tangents[i];
    b.positions.push_back({static_cast<float>(p.x - origin.x), static_cast<float>(p.y - origin.y), static_cast<float>(p.z - origin.z)});
    b.normals.push_back({static_cast<float>(n.x), static_cast<float>(n.y), static_cast<float>(n.z)});
    b.tangents.push_back({static_cast<float>(t.x), static_cast<float>(t.y), static_cast<float>(t.z),
                          m.tangent_w[i] < 0 ? -1.0f : 1.0f});
    b.uv0.push_back({static_cast<float>(m.uv[i].x), static_cast<float>(m.uv[i].y)});
    b.uv1.push_back({static_cast<float>(m.uv1[i].x), static_cast<float>(m.uv1[i].y)});
    const Float4 c = m.colours[i];
    b.colours.push_back({to_byte(c.x), to_byte(c.y), to_byte(c.z), to_byte(c.w)});
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
                const Vec2 c{(m.positions[i0].x + m.positions[i1].x + m.positions[i2].x) / 3,
                             (m.positions[i0].y + m.positions[i1].y + m.positions[i2].y) / 3};
                const TileKey tk = tile_of(c, cp.tile_size);
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
    for (const auto& [nid, nd] : graph.nodes()) {
        if (only) {
            Box b;
            b.add(nd.pos.xy());
            b.grow(params.joint_length + 15);
            if (!touches(b, only, chunk.tile_size)) continue;
        }
        distribute(chunks, build_joint_mesh(graph, region, nid, params), chunk, only);
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
