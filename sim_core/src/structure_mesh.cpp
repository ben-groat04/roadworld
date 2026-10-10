#include "cp/structure_mesh.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "cp/edge_sampling.h"
#include "cp/lanes.h"

namespace cp {

namespace {

using u32 = std::uint32_t;
constexpr double kInf = std::numeric_limits<double>::infinity();

Vec3 operator+(Vec3 a, Vec3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
Vec3 operator-(Vec3 a, Vec3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
Vec3 operator*(Vec3 a, double s) { return {a.x * s, a.y * s, a.z * s}; }
double dot(Vec3 a, Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
Vec3 cross(Vec3 a, Vec3 b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
double length(Vec3 a) { return std::sqrt(dot(a, a)); }
Vec3 normalized(Vec3 a) {
    const double l = length(a);
    return l > 1e-12 ? a * (1.0 / l) : Vec3{1, 0, 0};
}

constexpr Vec3 kUp{0, 0, 1};
constexpr Vec3 kDown{0, 0, -1};

double ceil_to(double v, double q) { return std::ceil(v / q - 1e-6) * q; }

// ---------------------------------------------------------------- mesh sink

struct Vtx {
    Vec3 p;
    Vec2 uv;
    double depth = 0;  // m from the nearest tunnel portal (vertex colour R)
    bool has_n = false;
    Vec3 n;
};

Vtx vx(Vec3 p, double u, double v, double depth = 0) {
    Vtx r;
    r.p = p;
    r.uv = {u, v};
    r.depth = depth;
    return r;
}

// Collects triangles per surface class. Every face has its own vertices (flat shading unless a vertex
// carries a normal). The winding of each triangle follows `hint` (the outward direction).
class Sink {
public:
    Sink(StructureMeshes& out, double depth_range) : out_(out), depth_range_(std::max(depth_range, 1e-3)) {}

    void begin(ElementKind kind, const char* surface, EdgeId edge, StructureKind sk, double s0, double s1,
               bool closed = true, int tag = 0) {
        end();
        name_ = surface;
        m_ = &out_.meshes[name_];
        col_ = &out_.colours[name_];
        first_ = static_cast<u32>(m_->indices.size());
        el_ = StructureElement{kind, edge, name_, 0, closed, tag};
        sec_ = SectionRange{};
        sec_.edge = edge;
        sec_.kind = sk;
        sec_.s0 = s0;
        sec_.s1 = s1;
        active_ = true;
    }

    void end() {
        if (!active_) return;
        active_ = false;
        const u32 count = static_cast<u32>(m_->indices.size()) - first_;
        if (count == 0) return;
        sec_.first_index = first_;
        sec_.index_count = count;
        el_.section = static_cast<u32>(m_->sections.size());
        m_->sections.push_back(sec_);
        out_.elements.push_back(el_);
    }

    void tri(Vtx a, Vtx b, Vtx c, Vec3 hint) {
        Vec3 gn = cross(b.p - a.p, c.p - a.p);
        const double len = length(gn);
        if (!(len > 2e-7)) return;
        if (dot(gn, hint) < 0) {
            std::swap(b, c);
            gn = gn * -1.0;
        }
        const Vec3 n = gn * (1.0 / len);
        const Vec3 e1 = b.p - a.p, e2 = c.p - a.p;
        const double du1 = b.uv.x - a.uv.x, dv1 = b.uv.y - a.uv.y, du2 = c.uv.x - a.uv.x, dv2 = c.uv.y - a.uv.y;
        Vec3 tg = normalized(e1);
        const double det = du1 * dv2 - du2 * dv1;
        if (std::abs(det) > 1e-12) {
            Vec3 t = (e1 * dv2 - e2 * dv1) * (1.0 / det);
            const Vec3 bv = (e2 * du1 - e1 * du2) * (1.0 / det);
            if (dot(cross(n, t), bv) < 0) {  // mirrored uv: flip u so that normal x tangent points along +v
                a.uv.x = -a.uv.x;
                b.uv.x = -b.uv.x;
                c.uv.x = -c.uv.x;
                t = t * -1.0;
            }
            if (length(t) > 1e-12) tg = normalized(t);
        }
        for (const Vtx* v : {&a, &b, &c}) {
            m_->positions.push_back(v->p);
            m_->normals.push_back(v->has_n ? normalized(v->n) : n);
            m_->tangents.push_back(tg);
            m_->uv.push_back(v->uv);
            const double d = std::clamp(v->depth / depth_range_, 0.0, 1.0);
            col_->push_back({static_cast<std::uint8_t>(std::lround(d * 255.0)), 0, 0, 255});
            m_->indices.push_back(static_cast<u32>(m_->positions.size() - 1));
        }
    }

    void quad(const Vtx& a, const Vtx& b, const Vtx& c, const Vtx& d, Vec3 hint) {
        tri(a, b, c, hint);
        tri(a, c, d, hint);
    }

    // Convex polygon (collinear vertices allowed) as a fan from a vertex that gives no sliver.
    void poly(const std::vector<Vtx>& v, Vec3 hint) {
        const std::size_t n = v.size();
        if (n < 3) return;
        std::size_t apex = 0;
        for (std::size_t k = 0; k < n; ++k) {
            bool ok = true;
            for (std::size_t i = 1; i + 1 < n && ok; ++i)
                ok = length(cross(v[(k + i) % n].p - v[k].p, v[(k + i + 1) % n].p - v[k].p)) > 2e-7;
            if (ok) {
                apex = k;
                break;
            }
        }
        for (std::size_t i = 1; i + 1 < n; ++i) tri(v[apex], v[(apex + i) % n], v[(apex + i + 1) % n], hint);
    }

private:
    StructureMeshes& out_;
    double depth_range_;
    bool active_ = false;
    std::string name_;
    WorldMesh* m_ = nullptr;
    std::vector<std::array<std::uint8_t, 4>>* col_ = nullptr;
    u32 first_ = 0;
    StructureElement el_;
    SectionRange sec_;
};

// ---------------------------------------------------------------- cross-section top profile

// Top surface of the road cross-section (the same shape build_profile in render_mesh.cpp sweeps: camber or
// fall, kerb upstand, footway and verge crossfalls), as (lateral d, height relative to the centreline).
struct XProfile {
    std::vector<Vec2> top;  // ascending d; a kerb face (two heights at one d) keeps the lower one
    double d_left = 0, d_right = 0;
    double cw_lo = 0, cw_hi = 0;  // carriageway-role extent

    bool ok() const { return top.size() >= 2; }
    double z_at(double d) const {
        if (top.empty()) return 0;
        if (d <= top.front().x) return top.front().y;
        if (d >= top.back().x) return top.back().y;
        std::size_t i = 1;
        while (i + 1 < top.size() && top[i].x < d) ++i;
        const Vec2 a = top[i - 1], b = top[i];
        const double t = b.x > a.x ? (d - a.x) / (b.x - a.x) : 0.0;
        return a.y + (b.y - a.y) * t;
    }
    double z_min() const {
        double m = kInf;
        for (const Vec2& p : top) m = std::min(m, p.y);
        return m;
    }
};

XProfile make_profile(const Edge& e, const RoadType& rt, const RegionPack& region) {
    XProfile pr;
    const RenderMeshParams P;
    const std::vector<Lane> lanes = instantiate_lanes(e, rt, region.driving_side);
    if (lanes.empty()) return pr;
    const double cf = rt.crossfall.value_or(P.default_crossfall);
    const double H = rt.kerb_upstand.value_or(P.default_kerb_upstand);
    const double fcf = rt.footway_crossfall.value_or(P.default_footway_crossfall);
    const double vcf = rt.verge_crossfall.value_or(P.default_verge_crossfall);
    enum Role { Carriageway, Kerb, Raised, Verge };
    struct L {
        double hi, lo, centre;
        Role role;
        double z_hi = 0, z_lo = 0;
    };
    std::vector<L> ls;
    double cw_lo = kInf, cw_hi = -kInf;
    for (const Lane& l : lanes) {
        const LaneType* lt = region.lane_type(l.lane_type);
        L x{l.offset + l.width / 2, l.offset - l.width / 2, l.offset, Carriageway};
        if (lt) {
            if (lt->profile == "kerb") x.role = Kerb;
            else if (lt->profile == "raised") x.role = Raised;
            else if (lt->profile == "verge") x.role = Verge;
        }
        if (x.role == Carriageway) {
            cw_lo = std::min(cw_lo, x.lo);
            cw_hi = std::max(cw_hi, x.hi);
        }
        ls.push_back(x);
    }
    if (cw_lo > cw_hi) cw_lo = cw_hi = 0;
    const bool camber = rt.two_way && !e.one_way && !rt.dual;
    const double ns = region.driving_side == DrivingSide::Left ? 1.0 : -1.0;
    const auto base = [&](double d) {
        d = std::clamp(d, cw_lo, cw_hi);
        return camber ? -cf * std::abs(d) : -cf * ns * d;
    };
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
                case Carriageway: zi = base(inner); zo = base(outer); break;
                case Kerb: zi = zo = z_prev + (prev_cw ? H : 0.0); break;
                case Raised: zi = z_prev + (prev_cw ? H : 0.0); zo = zi + fcf * w; break;
                case Verge: zi = z_prev; zo = zi - vcf * w; break;
            }
            prev_cw = l.role == Carriageway;
            z_prev = zo;
            l.z_hi = left ? zo : zi;
            l.z_lo = left ? zi : zo;
        }
    };
    sweep(left_side, true);
    sweep(right_side, false);
    std::sort(ls.begin(), ls.end(), [](const L& a, const L& b) { return a.centre < b.centre; });
    const auto push = [&](double d, double z) {
        if (!pr.top.empty() && std::abs(pr.top.back().x - d) < 1e-9) pr.top.back().y = std::min(pr.top.back().y, z);
        else if (pr.top.empty() || d > pr.top.back().x) pr.top.push_back({d, z});
    };
    for (const L& l : ls) {
        push(l.lo, l.z_lo);
        if (l.role == Carriageway && camber && l.lo + 1e-9 < 0 && l.hi - 1e-9 > 0) push(0, base(0));
        push(l.hi, l.z_hi);
    }
    pr.d_right = ls.front().lo;
    pr.d_left = ls.back().hi;
    pr.cw_lo = cw_lo;
    pr.cw_hi = cw_hi;
    return pr;
}

// ---------------------------------------------------------------- frames and stations

struct Frame {
    Vec2 p;
    double h = 0;
    double z = 0;
    double s = 0;
};

Frame frame_at(const Edge& e, double s) {
    double acc = 0;
    for (std::size_t i = 0; i < e.horizontal.spans.size(); ++i) {
        const Clothoid& c = e.horizontal.spans[i];
        if (s <= acc + c.length || i + 1 == e.horizontal.spans.size()) {
            const Pose po = c.pose(std::clamp(s - acc, 0.0, c.length));
            return {po.pos, po.heading, e.vertical.z(s), s};
        }
        acc += c.length;
    }
    return {};
}

Vec3 lp(const Frame& f, double d, double z) { return {f.p.x - d * std::sin(f.h), f.p.y + d * std::cos(f.h), z}; }
Vec3 left_of(double h) { return {-std::sin(h), std::cos(h), 0}; }
Vec3 fwd_of(double h) { return {std::cos(h), std::sin(h), 0}; }

std::vector<double> select_stations(const Edge& e, double s0, double s1, const XProfile& pr, const StructureParams& P) {
    const double tol = std::max(P.chord_tolerance, 1e-4);
    SamplingParams sp;
    sp.max_chord_error = tol;
    sp.max_heading_change = 0.06;
    sp.max_step = P.station_step;
    std::vector<double> st{s0, s1};
    std::vector<double> offsets{0.0};
    if (pr.d_left != 0) offsets.push_back(pr.d_left);
    if (pr.d_right != 0 && pr.d_right != pr.d_left) offsets.push_back(pr.d_right);
    for (double off : offsets)
        for (const EdgeSample& s : sample_offset(e, off, sp).samples)
            if (s.s > s0 && s.s < s1) st.push_back(s.s);
    for (const auto& b : e.vertical.breaks)
        for (double s : {b.s - b.length / 2, b.s, b.s + b.length / 2})
            if (s > s0 && s < s1) st.push_back(s);
    std::sort(st.begin(), st.end());
    std::vector<double> out;
    for (double s : st)
        if (out.empty() || s - out.back() > 1e-3) out.push_back(s);
    out.back() = s1;
    if (out.size() >= 2 && out[out.size() - 1] - out[out.size() - 2] < 1e-3) out.erase(out.end() - 2);
    // Longest step, then vertical refinement.
    for (int pass = 0; pass < 12; ++pass) {
        bool changed = false;
        std::vector<double> next;
        for (std::size_t i = 0; i + 1 < out.size(); ++i) {
            next.push_back(out[i]);
            const double a = out[i], b = out[i + 1], m = 0.5 * (a + b);
            if ((b - a > P.station_step * 1.0001 && b - a > 0.5) ||
                (b - a > 0.5 && std::abs(e.vertical.z(m) - 0.5 * (e.vertical.z(a) + e.vertical.z(b))) > tol)) {
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

std::vector<Frame> frames_of(const Edge& e, const std::vector<double>& st) {
    std::vector<Frame> f;
    f.reserve(st.size());
    for (double s : st) f.push_back(frame_at(e, s));
    return f;
}

// ---------------------------------------------------------------- other roads

// Other edges' carriageway bands, for crossings and for keeping piers out of them.
class OtherRoads {
public:
    OtherRoads(const RoadGraph& graph, const RegionPack& region) {
        for (const auto& [id, e] : graph.edges()) {
            const RoadType* rt = region.road_type(e.road_type);
            if (!rt || e.horizontal.spans.empty()) continue;
            const XProfile pr = make_profile(e, *rt, region);
            if (!pr.ok()) continue;
            Band b;
            b.id = id;
            SamplingParams sp;
            sp.max_step = 2.0;
            sp.max_chord_error = 0.05;
            b.pts = sample_centreline(e, sp);
            b.cw_lo = pr.cw_lo;
            b.cw_hi = pr.cw_hi;
            b.full_lo = pr.d_right;
            b.full_hi = pr.d_left;
            const double ext = std::max(std::abs(pr.d_right), std::abs(pr.d_left));
            b.lo = {kInf, kInf};
            b.hi = {-kInf, -kInf};
            for (const EdgeSample& s : b.pts) {
                b.lo = {std::min(b.lo.x, s.pos.x - ext), std::min(b.lo.y, s.pos.y - ext)};
                b.hi = {std::max(b.hi.x, s.pos.x + ext), std::max(b.hi.y, s.pos.y + ext)};
            }
            bands_.push_back(std::move(b));
        }
    }

    // Is (x, y) inside the band of another edge (carriageway, or the whole cross-section when `full`,
    // grown by `margin`) whose road surface there is below z_limit?
    bool below(double x, double y, EdgeId self, bool full, double margin, double z_limit) const {
        for (const Band& b : bands_) {
            if (b.id == self) continue;
            if (x < b.lo.x - margin || x > b.hi.x + margin || y < b.lo.y - margin || y > b.hi.y + margin) continue;
            const double lo = (full ? b.full_lo : b.cw_lo) - margin, hi = (full ? b.full_hi : b.cw_hi) + margin;
            for (std::size_t i = 0; i + 1 < b.pts.size(); ++i) {
                const Vec3 a = b.pts[i].pos, c = b.pts[i + 1].pos;
                const double dx = c.x - a.x, dy = c.y - a.y, l2 = dx * dx + dy * dy;
                if (l2 < 1e-12) continue;
                const double t = std::clamp(((x - a.x) * dx + (y - a.y) * dy) / l2, 0.0, 1.0);
                const double ex = x - (a.x + dx * t), ey = y - (a.y + dy * t);
                const double dist = std::hypot(ex, ey);
                if (dist > std::max(std::abs(lo), std::abs(hi))) continue;
                const double side = (dx * ey - dy * ex) >= 0 ? 1.0 : -1.0;  // + = left of the road direction
                const double lat = side * dist;
                if (lat < lo || lat > hi) continue;
                if (a.z + (c.z - a.z) * t < z_limit) return true;
            }
        }
        return false;
    }

private:
    struct Band {
        EdgeId id;
        std::vector<EdgeSample> pts;
        double cw_lo = 0, cw_hi = 0, full_lo = 0, full_hi = 0;
        Vec2 lo, hi;
    };
    std::vector<Band> bands_;
};

// ---------------------------------------------------------------- shared builder environment

struct Env {
    const RoadGraph& graph;
    const RegionPack& region;
    const GroundSampler& ground;
    const StructureParams& P;
    Sink& sink;
    StructureMeshes& out;
    const OtherRoads* others;
    const Edge& e;
    EdgeId id;
    XProfile pr;
    double len = 0;
    double trim_a = 0, trim_b = 0;
    const std::vector<WallEnd>* wall_ends = nullptr;
};

void add_instance(StructureMeshes& out, const char* kind, EdgeId edge, const InstanceTransform& t) {
    for (InstanceList& l : out.instances)
        if (l.kind == kind && l.edge == edge) {
            l.items.push_back(t);
            return;
        }
    InstanceList l;
    l.kind = kind;
    l.edge = edge;
    l.items.push_back(t);
    out.instances.push_back(std::move(l));
}

// ---------------------------------------------------------------- solids

// Prism along the stations with the cross-section given by lateral breakpoints D: top / bottom heights
// zt[i][k], zb[i][k] (absolute). Faces: top, bottom, both fascias, both end caps, so the solid is closed.
void emit_prism(Sink& sink, const std::vector<Frame>& F, const std::vector<double>& D,
                const std::vector<std::vector<double>>& zt, const std::vector<std::vector<double>>& zb) {
    const std::size_t n = F.size(), m = D.size();
    if (n < 2 || m < 2) return;
    const auto T = [&](std::size_t i, std::size_t k, double u, double v) { return vx(lp(F[i], D[k], zt[i][k]), u, v); };
    const auto B = [&](std::size_t i, std::size_t k, double u, double v) { return vx(lp(F[i], D[k], zb[i][k]), u, v); };
    for (std::size_t i = 0; i + 1 < n; ++i) {
        for (std::size_t k = 0; k + 1 < m; ++k) {
            const double v0 = D[k] - D.front(), v1 = D[k + 1] - D.front();
            sink.quad(T(i, k, F[i].s, v0), T(i + 1, k, F[i + 1].s, v0), T(i + 1, k + 1, F[i + 1].s, v1),
                      T(i, k + 1, F[i].s, v1), kUp);
            sink.quad(B(i, k, F[i].s, v0), B(i + 1, k, F[i + 1].s, v0), B(i + 1, k + 1, F[i + 1].s, v1),
                      B(i, k + 1, F[i].s, v1), kDown);
        }
        for (const std::size_t k : {std::size_t{0}, m - 1}) {
            const double sgn = k == 0 ? -1.0 : 1.0;
            sink.quad(B(i, k, F[i].s, zb[i][k]), B(i + 1, k, F[i + 1].s, zb[i + 1][k]),
                      T(i + 1, k, F[i + 1].s, zt[i + 1][k]), T(i, k, F[i].s, zt[i][k]), left_of(F[i].h) * sgn);
        }
    }
    for (const std::size_t i : {std::size_t{0}, n - 1}) {
        const double sgn = i == 0 ? -1.0 : 1.0;
        for (std::size_t k = 0; k + 1 < m; ++k)
            sink.quad(B(i, k, D[k], zb[i][k]), B(i, k + 1, D[k + 1], zb[i][k + 1]), T(i, k + 1, D[k + 1], zt[i][k + 1]),
                      T(i, k, D[k], zt[i][k]), fwd_of(F[i].h) * sgn);
    }
}

// Oriented box: a along ex, d along ey, z absolute.
struct BoxFrame {
    Vec2 o;
    Vec2 ex, ey;
    Vec3 pt(double a, double d, double z) const { return {o.x + ex.x * a + ey.x * d, o.y + ex.y * a + ey.y * d, z}; }
    Vec3 dx() const { return {ex.x, ex.y, 0}; }
    Vec3 dy() const { return {ey.x, ey.y, 0}; }
};

BoxFrame box_frame(const Frame& f, double along_sign = 1.0) {
    const Vec3 t = fwd_of(f.h) * along_sign, n = left_of(f.h);
    return {f.p, {t.x, t.y}, {n.x, n.y}};
}

void emit_box(Sink& sink, const BoxFrame& b, double a0, double a1, double d0, double d1, double z0, double z1) {
    if (!(z1 - z0 > 1e-6) || !(a1 - a0 > 1e-6) || !(d1 - d0 > 1e-6)) return;
    const auto V = [&](double a, double d, double z, double u, double v) { return vx(b.pt(a, d, z), u, v); };
    sink.quad(V(a0, d0, z0, a0, d0), V(a1, d0, z0, a1, d0), V(a1, d1, z0, a1, d1), V(a0, d1, z0, a0, d1), kDown);
    sink.quad(V(a0, d0, z1, a0, d0), V(a1, d0, z1, a1, d0), V(a1, d1, z1, a1, d1), V(a0, d1, z1, a0, d1), kUp);
    sink.quad(V(a0, d0, z0, a0, z0), V(a1, d0, z0, a1, z0), V(a1, d0, z1, a1, z1), V(a0, d0, z1, a0, z1), b.dy() * -1.0);
    sink.quad(V(a0, d1, z0, a0, z0), V(a1, d1, z0, a1, z0), V(a1, d1, z1, a1, z1), V(a0, d1, z1, a0, z1), b.dy());
    sink.quad(V(a0, d0, z0, d0, z0), V(a0, d1, z0, d1, z0), V(a0, d1, z1, d1, z1), V(a0, d0, z1, d0, z1), b.dx() * -1.0);
    sink.quad(V(a1, d0, z0, d0, z0), V(a1, d1, z0, d1, z0), V(a1, d1, z1, d1, z1), V(a1, d0, z1, d0, z1), b.dx());
}

// Min / max of the ground (graded and natural) over an oriented footprint.
void ground_range(const GroundSampler& g, const BoxFrame& b, double a0, double a1, double d0, double d1, int na,
                  int nd, double& gmin, double& gmax, bool& water) {
    gmin = kInf;
    gmax = -kInf;
    for (int i = 0; i <= na; ++i)
        for (int j = 0; j <= nd; ++j) {
            const Vec3 q = b.pt(a0 + (a1 - a0) * i / na, d0 + (d1 - d0) * j / nd, 0);
            const double h = g.height(q.x, q.y), hn = g.natural_height(q.x, q.y);
            gmin = std::min({gmin, h, hn});
            gmax = std::max({gmax, h, hn});
            if (g.is_water(q.x, q.y)) water = true;
        }
}

// ---------------------------------------------------------------- stepped walls (retaining and wing walls)

struct WStation {
    Vec2 a;         // inner face point
    Vec2 u;         // outward unit normal (the wall occupies a + u * [0, T])
    double zb = 0;  // foot
    double zt = 0;  // top of coping
    bool dup = false;  // second station of a step: same place, the next panel's level
};

// Prototype wallStations: drop stations where the offset (outer) face folds back against the inner line.
std::vector<WStation> fold_filter(const std::vector<WStation>& st0, double T) {
    std::vector<WStation> st;
    for (const WStation& q : st0) {
        if (st.empty()) {
            st.push_back(q);
            continue;
        }
        const WStation& pr = st.back();
        if (q.dup) {
            if (std::abs(pr.a.x - q.a.x) < 1e-9 && std::abs(pr.a.y - q.a.y) < 1e-9) st.push_back(q);
            continue;
        }
        const Vec2 dA = q.a - pr.a;
        const Vec2 dB = (q.a + q.u * T) - (pr.a + pr.u * T);
        const double la = dA.length(), lb = dB.length();
        if (la < 1e-6 || lb < 0.25 * la || dA.dot(dB) < 0.3 * la * lb) continue;
        if (st.size() > 1) {
            const Vec2 dP = pr.a - st[st.size() - 2].a;
            if (dP.dot(dA) < 0) continue;
        }
        st.push_back(q);
    }
    return st;
}

struct WallSpec {
    double T = 1.5;
    double coping_t = 0.15;
    double overhang = 0.04;
    const char* body_surface = structure_surface::wall;
    const char* coping_surface = structure_surface::coping;
    ElementKind body_kind = ElementKind::RetainingWall;
    ElementKind coping_kind = ElementKind::WallCoping;
    EdgeId edge;
    StructureKind sk = StructureKind::Ground;
    double s0 = 0, s1 = 0;
    int tag = 0;
};

void emit_stepped_wall(Sink& sink, std::vector<WStation> st, const WallSpec& w) {
    st = fold_filter(st, w.T);
    if (st.size() < 2) return;
    const std::size_t n = st.size();
    std::vector<double> cum(n, 0.0);
    for (std::size_t i = 1; i < n; ++i) cum[i] = cum[i - 1] + (st[i].a - st[i - 1].a).length();
    // Panels: runs of stations between steps (a dup station starts the next panel).
    std::vector<std::pair<std::size_t, std::size_t>> panels;
    {
        std::size_t b = 0;
        for (std::size_t i = 1; i < n; ++i)
            if (st[i].dup) {
                panels.push_back({b, i - 1});
                b = i;
            }
        panels.push_back({b, n - 1});
    }
    const auto tangent2 = [&](std::size_t i0, std::size_t i1) {
        const Vec2 d = st[i1].a - st[i0].a;
        const double l = d.length();
        return l > 1e-9 ? d * (1.0 / l) : Vec2{1, 0};
    };
    const auto in_pt = [&](const WStation& s, double z, double off) {  // lateral offset `off` from the inner face
        return Vec3{s.a.x + s.u.x * off, s.a.y + s.u.y * off, z};
    };
    // ---- body
    sink.begin(w.body_kind, w.body_surface, w.edge, w.sk, w.s0, w.s1, true, w.tag);
    for (const auto& [i0, i1] : panels) {
        if (i1 <= i0) continue;
        const double zt = st[i0].zt - w.coping_t;
        for (std::size_t i = i0; i < i1; ++i) {
            const WStation &p = st[i], &q = st[i + 1];
            const Vec3 hin = Vec3{-(p.u.x + q.u.x), -(p.u.y + q.u.y), 0}, hout = hin * -1.0;
            sink.quad(vx(in_pt(p, p.zb, 0), cum[i], p.zb), vx(in_pt(q, q.zb, 0), cum[i + 1], q.zb),
                      vx(in_pt(q, zt, 0), cum[i + 1], zt), vx(in_pt(p, zt, 0), cum[i], zt), hin);
            sink.quad(vx(in_pt(p, p.zb, w.T), cum[i], p.zb), vx(in_pt(q, q.zb, w.T), cum[i + 1], q.zb),
                      vx(in_pt(q, zt, w.T), cum[i + 1], zt), vx(in_pt(p, zt, w.T), cum[i], zt), hout);
            sink.quad(vx(in_pt(p, zt, 0), cum[i], 0), vx(in_pt(q, zt, 0), cum[i + 1], 0), vx(in_pt(q, zt, w.T), cum[i + 1], w.T),
                      vx(in_pt(p, zt, w.T), cum[i], w.T), kUp);
            sink.quad(vx(in_pt(p, p.zb, 0), cum[i], 0), vx(in_pt(q, q.zb, 0), cum[i + 1], 0), vx(in_pt(q, q.zb, w.T), cum[i + 1], w.T),
                      vx(in_pt(p, p.zb, w.T), cum[i], w.T), kDown);
        }
        for (const std::size_t i : {i0, i1}) {
            const WStation& s = st[i];
            const Vec2 t = i == i0 ? tangent2(i0, i0 + 1) : tangent2(i1 - 1, i1);
            const double sgn = i == i0 ? -1.0 : 1.0;
            sink.quad(vx(in_pt(s, s.zb, 0), 0, s.zb), vx(in_pt(s, s.zb, w.T), w.T, s.zb), vx(in_pt(s, zt, w.T), w.T, zt),
                      vx(in_pt(s, zt, 0), 0, zt), Vec3{t.x * sgn, t.y * sgn, 0});
        }
    }
    sink.end();
    // ---- coping: a slab on each panel, overhanging the faces by `overhang` (and the free ends)
    sink.begin(w.coping_kind, w.coping_surface, w.edge, w.sk, w.s0, w.s1, true, w.tag);
    for (std::size_t pi = 0; pi < panels.size(); ++pi) {
        const auto [i0, i1] = panels[pi];
        if (i1 <= i0) continue;
        const double zt = st[i0].zt, zb = zt - w.coping_t;
        const auto cp_pt = [&](std::size_t i, double off, double z) {
            Vec3 p = in_pt(st[i], z, off);
            if (i == 0 || i == n - 1) {  // free wall ends overhang along the wall as well
                const Vec2 t = i == 0 ? tangent2(0, 1) : tangent2(n - 2, n - 1);
                const double sg = i == 0 ? -1.0 : 1.0;
                p.x += t.x * sg * w.overhang;
                p.y += t.y * sg * w.overhang;
            }
            return p;
        };
        const double lo = -w.overhang, hi = w.T + w.overhang;
        for (std::size_t i = i0; i < i1; ++i) {
            const WStation &p = st[i], &q = st[i + 1];
            const Vec3 hin = Vec3{-(p.u.x + q.u.x), -(p.u.y + q.u.y), 0}, hout = hin * -1.0;
            sink.quad(vx(cp_pt(i, lo, zt), cum[i], lo), vx(cp_pt(i + 1, lo, zt), cum[i + 1], lo), vx(cp_pt(i + 1, hi, zt), cum[i + 1], hi),
                      vx(cp_pt(i, hi, zt), cum[i], hi), kUp);
            sink.quad(vx(cp_pt(i, lo, zb), cum[i], lo), vx(cp_pt(i + 1, lo, zb), cum[i + 1], lo), vx(cp_pt(i + 1, hi, zb), cum[i + 1], hi),
                      vx(cp_pt(i, hi, zb), cum[i], hi), kDown);
            sink.quad(vx(cp_pt(i, lo, zb), cum[i], zb), vx(cp_pt(i + 1, lo, zb), cum[i + 1], zb), vx(cp_pt(i + 1, lo, zt), cum[i + 1], zt),
                      vx(cp_pt(i, lo, zt), cum[i], zt), hin);
            sink.quad(vx(cp_pt(i, hi, zb), cum[i], zb), vx(cp_pt(i + 1, hi, zb), cum[i + 1], zb), vx(cp_pt(i + 1, hi, zt), cum[i + 1], zt),
                      vx(cp_pt(i, hi, zt), cum[i], zt), hout);
        }
        for (const std::size_t i : {i0, i1}) {
            const Vec2 t = i == i0 ? tangent2(i0, i0 + 1) : tangent2(i1 - 1, i1);
            const double sgn = i == i0 ? -1.0 : 1.0;
            sink.quad(vx(cp_pt(i, lo, zb), lo, zb), vx(cp_pt(i, hi, zb), hi, zb), vx(cp_pt(i, hi, zt), hi, zt), vx(cp_pt(i, lo, zt), lo, zt),
                      Vec3{t.x * sgn, t.y * sgn, 0});
        }
    }
    sink.end();
}

// ---------------------------------------------------------------- bridges

double deck_depth(const Env& v, double d) {
    const StructureParams& P = v.P;
    const double dist = std::min(v.pr.d_left - d, d - v.pr.d_right);
    const double w = std::clamp(1.0 - (dist - P.edge_beam_width) / std::max(P.edge_beam_haunch, 1e-6), 0.0, 1.0);
    return P.slab_depth + (P.edge_beam_depth - P.slab_depth) * w;
}

// Lateral breakpoints shared by the top and soffit of the deck (so the solid has no T-junctions).
std::vector<double> deck_breaks(const Env& v) {
    const StructureParams& P = v.P;
    std::vector<double> D;
    for (const Vec2& p : v.pr.top) D.push_back(p.x);
    for (double d : {v.pr.d_right + P.edge_beam_width, v.pr.d_right + P.edge_beam_width + P.edge_beam_haunch,
                     v.pr.d_left - P.edge_beam_width, v.pr.d_left - P.edge_beam_width - P.edge_beam_haunch})
        if (d > v.pr.d_right + 1e-6 && d < v.pr.d_left - 1e-6) D.push_back(d);
    std::sort(D.begin(), D.end());
    std::vector<double> out;
    for (double d : D)
        if (out.empty() || d - out.back() > 1e-6) out.push_back(d);
    return out;
}

void emit_parapets(Env& v, const std::vector<Frame>& F, int tag) {
    const StructureParams& P = v.P;
    const double s0 = F.front().s, s1 = F.back().s;
    for (const int sg : {1, -1}) {
        const double d_edge = sg > 0 ? v.pr.d_left : v.pr.d_right;
        const double d_in = d_edge - sg * P.parapet_thickness;
        const std::vector<double> D = sg > 0 ? std::vector<double>{d_in, d_edge} : std::vector<double>{d_edge, d_in};
        std::vector<std::vector<double>> zt(F.size()), zb(F.size());
        for (std::size_t i = 0; i < F.size(); ++i) {
            const double top = F[i].z + v.pr.z_at(d_edge) + P.parapet_height;
            for (double d : D) {
                zt[i].push_back(top);
                zb[i].push_back(F[i].z + v.pr.z_at(d) - P.parapet_embed);
            }
        }
        v.sink.begin(ElementKind::Parapet, structure_surface::parapet, v.id, StructureKind::Bridge, s0, s1, true, tag * 2 + (sg > 0 ? 0 : 1));
        emit_prism(v.sink, F, D, zt, zb);
        v.sink.end();
        // End pilasters.
        v.sink.begin(ElementKind::ParapetEnd, structure_surface::parapet, v.id, StructureKind::Bridge, s0, s1, true, tag * 2 + (sg > 0 ? 0 : 1));
        for (const bool start : {true, false}) {
            const Frame& f = start ? F.front() : F.back();
            const BoxFrame bf = box_frame(f);
            const double a0 = start ? 0.0 : -P.parapet_end_length, a1 = start ? P.parapet_end_length : 0.0;
            const double d0 = sg > 0 ? d_edge - P.parapet_end_width : d_edge, d1 = sg > 0 ? d_edge : d_edge + P.parapet_end_width;
            emit_box(v.sink, bf, a0, a1, d0, d1, f.z + v.pr.z_at(d_edge) - P.parapet_embed,
                     f.z + v.pr.z_at(d_edge) + P.parapet_height + P.parapet_end_extra);
        }
        v.sink.end();
        // Post instances along the parapet.
        const double L = s1 - s0;
        const int count = static_cast<int>(std::floor(L / std::max(P.parapet_post_spacing, 0.5)));
        for (int k = 0; k < count; ++k) {
            const double s = s0 + L * (k + 0.5) / count;
            const Frame f = frame_at(v.e, s);
            const double d = d_edge - sg * P.parapet_thickness * 0.5;
            InstanceTransform t;
            t.position = lp(f, d, f.z + v.pr.z_at(d_edge));
            t.yaw = f.h;
            t.pitch = std::atan(v.e.vertical.grade(s));
            add_instance(v.out, "parapet_post", v.id, t);
        }
    }
}

// Positions along the span (arc length) for the piers, avoiding other roads' cross-sections where possible.
struct PierPlan {
    std::vector<double> s;
    bool too_long = false;
    double longest = 0;
};

PierPlan plan_piers(const Env& v, double s0, double s1) {
    const StructureParams& P = v.P;
    PierPlan plan;
    const double L = s1 - s0, maxsp = std::max(P.max_pier_spacing, 4.0);
    const double hw = std::max(v.pr.d_left, -v.pr.d_right);
    const double pw = std::max(0.6, P.pier_shaft_width * hw);
    const double ha = P.pier_shaft_along / 2 + P.pier_footing_margin + P.pier_road_clearance;
    const double hl = pw + P.pier_footing_margin + P.pier_road_clearance;
    const double min_sep = 4.0;
    const auto blocked = [&](double s) {
        if (!v.others) return false;
        const Frame f = frame_at(v.e, s);
        const double zlim = f.z + v.pr.z_at(0) - P.deck_top_gap - P.slab_depth - P.pier_ground_clearance;
        const int na = std::max(1, static_cast<int>(std::ceil(2 * ha / 0.75))), nd = std::max(1, static_cast<int>(std::ceil(2 * hl / 0.75)));
        const BoxFrame bf = box_frame(f);
        for (int i = 0; i <= na; ++i)
            for (int j = 0; j <= nd; ++j) {
                const Vec3 q = bf.pt(-ha + 2 * ha * i / na, -hl + 2 * hl * j / nd, 0);
                if (v.others->below(q.x, q.y, v.id, true, 0.0, zlim)) return true;
            }
        return false;
    };
    const auto place = [&](double ideal, double lo, double hi, double& out) {
        const double window = P.pier_search * maxsp;
        for (double off = 0; off <= window + 1e-9; off += 0.5)
            for (const double sg : {1.0, -1.0}) {
                if (off == 0 && sg < 0) continue;
                const double s = ideal + sg * off;
                if (s < lo + min_sep || s > hi - min_sep) continue;
                if (!blocked(s)) {
                    out = s;
                    return true;
                }
            }
        return false;
    };
    std::vector<double> supports{s0, s1};
    const int n = std::max(1, static_cast<int>(std::ceil(L / maxsp - 1e-9)));
    std::vector<double> piers;
    for (int q = 1; q < n; ++q) {
        double s = 0;
        if (place(s0 + L * q / n, s0, s1, s)) piers.push_back(s);
    }
    // Fill any gap that is still too long (moved or missing piers).
    for (int round = 0; round < 8; ++round) {
        std::vector<double> sup{s0};
        sup.insert(sup.end(), piers.begin(), piers.end());
        sup.push_back(s1);
        std::sort(sup.begin(), sup.end());
        bool added = false;
        for (std::size_t i = 0; i + 1 < sup.size(); ++i) {
            const double gap = sup[i + 1] - sup[i];
            if (gap <= maxsp + 1e-9) continue;
            const int m = static_cast<int>(std::ceil(gap / maxsp - 1e-9));
            for (int q = 1; q < m; ++q) {
                double s = 0;
                if (place(sup[i] + gap * q / m, sup[i], sup[i + 1], s)) {
                    piers.push_back(s);
                    added = true;
                }
            }
        }
        if (!added) break;
    }
    std::sort(piers.begin(), piers.end());
    std::vector<double> sup{s0};
    sup.insert(sup.end(), piers.begin(), piers.end());
    sup.push_back(s1);
    for (std::size_t i = 0; i + 1 < sup.size(); ++i) plan.longest = std::max(plan.longest, sup[i + 1] - sup[i]);
    plan.too_long = plan.longest > maxsp + 1e-6;
    plan.s = piers;
    return plan;
}

void emit_pier(Env& v, double s, BridgeInfo& info, int tag) {
    const StructureParams& P = v.P;
    const Frame f = frame_at(v.e, s);
    const BoxFrame bf = box_frame(f);
    const double hw = std::max(v.pr.d_left, -v.pr.d_right);
    const double pw = std::max(0.6, P.pier_shaft_width * hw);
    double cl0 = v.pr.d_right + P.pier_cap_inset, cl1 = v.pr.d_left - P.pier_cap_inset;
    if (cl1 - cl0 < 1.0) cl0 = -0.5, cl1 = 0.5;
    double soffit = -kInf;
    for (double d : {cl0, cl1, 0.0}) soffit = std::max(soffit, f.z + v.pr.z_at(d) - P.deck_top_gap - deck_depth(v, d));
    const double cap_top = soffit, cap_bot = cap_top - P.pier_cap_depth;
    const double ha = P.pier_shaft_along / 2, fm = P.pier_footing_margin;
    double gmin = 0, gmax = 0;
    bool water = false;
    ground_range(v.ground, bf, -ha - fm, ha + fm, -pw - fm, pw + fm, 3, 6, gmin, gmax, water);
    const double foot = gmin - P.pier_footing_bury, plinth_top = gmax + P.pier_footing_above;
    if (cap_bot - plinth_top < P.pier_min_height) {
        info.pier_skipped = true;
        return;
    }
    const double s0 = f.s, s1 = f.s;
    v.sink.begin(ElementKind::PierFooting, structure_surface::concrete, v.id, StructureKind::Bridge, s0, s1, true, tag);
    emit_box(v.sink, bf, -ha - fm, ha + fm, -pw - fm, pw + fm, foot, plinth_top);
    v.sink.end();
    v.sink.begin(ElementKind::Pier, structure_surface::concrete, v.id, StructureKind::Bridge, s0, s1, true, tag);
    emit_box(v.sink, bf, -ha, ha, -pw, pw, plinth_top, cap_bot);
    v.sink.end();
    v.sink.begin(ElementKind::PierCap, structure_surface::concrete, v.id, StructureKind::Bridge, s0, s1, true, tag);
    emit_box(v.sink, bf, -P.pier_cap_along / 2, P.pier_cap_along / 2, cl0, cl1, cap_bot, cap_top);
    v.sink.end();
    info.pier_s.push_back(s);
    if (water) info.pier_in_water = true;
}

// Abutment at one end of a bridge: bearing stem under the deck end, ballast wall behind it, and a wing wall
// on each side stepping down in level panels along the road.
void emit_abutment(Env& v, const Frame& fe, bool at_end, double deck_bottom, int tag) {
    const StructureParams& P = v.P;
    const double og = at_end ? 1.0 : -1.0;  // direction of increasing a (away from the bridge) in s
    const BoxFrame bf = box_frame(fe, og);
    const double d_r = v.pr.d_right, d_l = v.pr.d_left;
    const double ballast_top = fe.z + v.pr.z_min() - P.deck_top_gap - 0.01;
    const double shelf = deck_bottom;
    double gmin = 0, gmax = 0;
    bool water = false;
    ground_range(v.ground, bf, -P.bearing_width, P.ballast_thickness, d_r, d_l, 3, 6, gmin, gmax, water);
    const double foot = std::min(gmin, shelf - 0.5) - P.abutment_bury;
    v.sink.begin(ElementKind::AbutmentStem, structure_surface::concrete, v.id, StructureKind::Bridge, fe.s, fe.s, true, tag * 2 + (at_end ? 1 : 0));
    emit_box(v.sink, bf, -P.bearing_width, 0.0, d_r, d_l, foot, shelf);
    emit_box(v.sink, bf, 0.0, P.ballast_thickness, d_r, d_l, foot, ballast_top);
    v.sink.end();

    // Wing walls.
    const double T = P.wing_thickness;
    for (const int sg : {1, -1}) {
        const double d_in = sg > 0 ? d_l : d_r;
        std::vector<double> ab{-P.bearing_width, 0.0};
        for (double a = P.wing_panel; a < P.wing_length - 1e-9; a += P.wing_panel) ab.push_back(a);
        ab.push_back(P.wing_length);
        struct Panel {
            double a0, a1, top, foot;
        };
        std::vector<Panel> panels;
        std::vector<Frame> fr;
        for (double a : ab) {
            const double s = fe.s + og * a;
            fr.push_back(frame_at(v.e, std::clamp(s, 0.0, v.len)));
        }
        for (std::size_t k = 0; k + 1 < ab.size(); ++k) {
            const bool clipped = (fe.s + og * ab[k + 1] < 0 || fe.s + og * ab[k + 1] > v.len);
            double top = -kInf, g_in = -kInf, g_out = kInf, zfoot = kInf;
            for (int q = 0; q <= 4; ++q) {
                const double t = q / 4.0;
                const double a = ab[k] + (ab[k + 1] - ab[k]) * t;
                const Frame f = frame_at(v.e, std::clamp(fe.s + og * a, 0.0, v.len));
                top = std::max(top, f.z + v.pr.z_at(d_in));
                for (const double off : {0.0, T * 0.5, T}) {
                    const Vec3 p = lp(f, d_in + sg * off, 0);
                    g_in = std::max(g_in, v.ground.height(p.x, p.y));
                    zfoot = std::min({zfoot, v.ground.height(p.x, p.y), v.ground.natural_height(p.x, p.y)});
                }
                const Vec3 po = lp(f, d_in + sg * (T + 0.3), 0);
                g_out = std::min(g_out, v.ground.height(po.x, po.y));
            }
            Panel pn;
            pn.a0 = ab[k];
            pn.a1 = ab[k + 1];
            pn.top = ceil_to(std::max(top - P.deck_top_gap, g_in + P.wing_cover), P.wall_quant);
            pn.foot = zfoot - P.abutment_bury;
            panels.push_back(pn);
            if (k >= 1 && (pn.top - g_out < P.wing_end_exposed || clipped)) break;
        }
        // Smooth feet: no panel's foot above its neighbour's.
        for (std::size_t k = 0; k < panels.size(); ++k) {
            if (k > 0) panels[k].foot = std::min(panels[k].foot, panels[k - 1].foot);
        }
        for (std::size_t k = panels.size(); k-- > 1;) panels[k - 1].foot = std::min(panels[k - 1].foot, panels[k].foot);
        std::vector<WStation> st;
        for (std::size_t k = 0; k < panels.size(); ++k) {
            const Frame fa = fr[k], fb = fr[k + 1];
            const auto mkst = [&](const Frame& f, const Panel& pn, bool dup) {
                WStation w;
                const Vec3 p = lp(f, d_in, 0);
                w.a = {p.x, p.y};
                const Vec3 n = left_of(f.h) * static_cast<double>(sg);
                w.u = {n.x, n.y};
                w.zb = pn.foot;
                w.zt = pn.top;
                w.dup = dup;
                return w;
            };
            if (k == 0 || panels[k].top != panels[k - 1].top) st.push_back(mkst(fa, panels[k], k > 0));
            st.push_back(mkst(fb, panels[k], false));
        }
        WallSpec ws;
        ws.T = T;
        ws.coping_t = P.coping_thickness;
        ws.overhang = P.coping_overhang;
        ws.body_surface = structure_surface::concrete;
        ws.body_kind = ElementKind::WingWall;
        ws.coping_kind = ElementKind::WingWall;
        ws.edge = v.id;
        ws.sk = StructureKind::Bridge;
        ws.s0 = ws.s1 = fe.s;
        ws.tag = tag * 4 + (at_end ? 2 : 0) + (sg > 0 ? 1 : 0);
        emit_stepped_wall(v.sink, std::move(st), ws);
    }
    (void)water;  // abutments on water are not flagged separately
}

void build_bridge(Env& v, const StructureSpan& span, int tag) {
    const StructureParams& P = v.P;
    const double s0 = std::max(span.s0, v.trim_a), s1 = std::min(span.s1, v.len - v.trim_b);
    if (s1 - s0 < 0.5 || !v.pr.ok()) return;
    const std::vector<double> st = select_stations(v.e, s0, s1, v.pr, P);
    const std::vector<Frame> F = frames_of(v.e, st);
    if (F.size() < 2) return;
    BridgeInfo info;
    info.edge = v.id;
    info.s0 = s0;
    info.s1 = s1;

    // Footprint tests: crossing roads and water.
    const double d_r = v.pr.d_right, d_l = v.pr.d_left;
    for (const Frame& f : F)
        for (const double d : {d_r, 0.5 * d_r, 0.0, 0.5 * d_l, d_l}) {
            const Vec3 q = lp(f, d, 0);
            if (v.others && v.others->below(q.x, q.y, v.id, false, 0.0, f.z + v.pr.z_at(d) - P.crossing_drop)) info.crosses_road = true;
            if (v.ground.is_water(q.x, q.y)) info.over_water = true;
        }

    const std::vector<double> D = deck_breaks(v);
    const std::size_t n = F.size(), m = D.size();
    std::vector<std::vector<double>> zt(n, std::vector<double>(m)), zb(n, std::vector<double>(m));
    for (std::size_t i = 0; i < n; ++i)
        for (std::size_t k = 0; k < m; ++k) zt[i][k] = F[i].z + v.pr.z_at(D[k]) - P.deck_top_gap;

    if (s1 - s0 < P.min_bridge_length && !info.crosses_road && !info.over_water) {
        // Solid infill block: from below the ground up to the road surface.
        info.solid_block = true;
        for (std::size_t i = 0; i < n; ++i) {
            double gmin = kInf, ztmin = kInf;
            for (std::size_t k = 0; k < m; ++k) ztmin = std::min(ztmin, zt[i][k]);
            for (int j = 0; j <= 6; ++j) {
                const Vec3 q = lp(F[i], d_r + (d_l - d_r) * j / 6.0, 0);
                gmin = std::min({gmin, v.ground.height(q.x, q.y), v.ground.natural_height(q.x, q.y)});
            }
            const double foot = std::min(gmin, ztmin - 0.5) - P.block_bury;
            for (std::size_t k = 0; k < m; ++k) zb[i][k] = foot;
        }
        v.sink.begin(ElementKind::Block, structure_surface::concrete, v.id, StructureKind::Bridge, s0, s1, true, tag);
        emit_prism(v.sink, F, D, zt, zb);
        v.sink.end();
        emit_parapets(v, F, tag);
        v.out.bridges.push_back(info);
        return;
    }

    double deck_bottom_start = kInf, deck_bottom_end = kInf;
    for (std::size_t i = 0; i < n; ++i)
        for (std::size_t k = 0; k < m; ++k) {
            zb[i][k] = zt[i][k] - deck_depth(v, D[k]);
            if (i == 0) deck_bottom_start = std::min(deck_bottom_start, zb[i][k]);
            if (i == n - 1) deck_bottom_end = std::min(deck_bottom_end, zb[i][k]);
        }
    v.sink.begin(ElementKind::Deck, structure_surface::concrete, v.id, StructureKind::Bridge, s0, s1, true, tag);
    emit_prism(v.sink, F, D, zt, zb);
    v.sink.end();
    emit_parapets(v, F, tag);

    const PierPlan plan = plan_piers(v, s0, s1);
    info.span_too_long = plan.too_long;
    info.longest_span = plan.longest;
    for (double s : plan.s) emit_pier(v, s, info, tag);

    emit_abutment(v, F.front(), false, deck_bottom_start, tag);
    emit_abutment(v, F.back(), true, deck_bottom_end, tag);
    v.out.bridges.push_back(info);
}

// ---------------------------------------------------------------- tunnels

double portal_stub_top(const Env& v, const Frame& f, int dir, int sg, double hw) {
    const StructureParams& P = v.P;
    double g = -kInf;
    for (double al = 0; al <= P.headwall_thickness + 1e-6; al += 1.0)
        for (double e = 0; e <= P.wall_thickness + 1e-6; e += 0.5) {
            const double la = sg * (hw + e);
            const double x = f.p.x + std::cos(f.h) * al * dir - std::sin(f.h) * la, y = f.p.y + std::sin(f.h) * al * dir + std::cos(f.h) * la;
            g = std::max({g, v.ground.height(x, y), v.ground.natural_height(x, y)});
        }
    return g + 0.3;
}

void build_tunnel(Env& v, const StructureSpan& span, int tag) {
    const StructureParams& P = v.P;
    const double s0 = std::max(span.s0, v.trim_a), s1 = std::min(span.s1, v.len - v.trim_b);
    if (s1 - s0 < 1.0 || !v.pr.ok()) return;
    const std::vector<double> st = select_stations(v.e, s0, s1, v.pr, P);
    const std::vector<Frame> F = frames_of(v.e, st);
    const std::size_t n = F.size();
    if (n < 2) return;

    const double W = std::max(v.pr.d_left, -v.pr.d_right);
    if (W < 1.0) return;
    const int N = std::max(4, P.tunnel_arch_segments);
    const double zr = v.pr.z_at(v.pr.d_right), zl = v.pr.z_at(v.pr.d_left);
    const double Hs = std::max(P.tunnel_clear_height, std::max(zr, zl) + 2.0);
    const double rise = std::max(P.tunnel_rise_k * W, 0.05);
    const double R = (W * W + rise * rise) / (2 * rise);
    const double zc = Hs + rise - R;
    const auto roof = [&](double q) { return zc + std::sqrt(std::max(R * R - q * q, 0.0)); };

    TunnelBore bore;
    bore.edge = v.id;
    bore.s0 = s0;
    bore.s1 = s1;
    bore.section = {W, Hs, Hs + rise, R, zc, rise};
    for (const Frame& f : F) bore.stations.push_back({f.p.x, f.p.y, f.z, f.h, f.s});
    v.out.tunnels.push_back(bore);

    // ---- lining profile (right to left), segment normals pointing into the tunnel air
    struct PSeg {
        Vec2 a, b, na, nb;
    };
    std::vector<PSeg> segs;
    const auto radial = [&](double q, double z) {
        const double nx = -q, nz = zc - z, l = std::hypot(nx, nz);
        return l > 1e-9 ? Vec2{nx / l, nz / l} : Vec2{0, -1};
    };
    if (-W < v.pr.d_right - 1e-6) segs.push_back({{v.pr.d_right, zr}, {-W, zr}, {0, 1}, {0, 1}});
    segs.push_back({{-W, zr}, {-W, Hs}, {1, 0}, {1, 0}});
    for (int j = 0; j < N; ++j) {
        const double q0 = -W + 2 * W * j / N, q1 = -W + 2 * W * (j + 1) / N;
        const double z0 = j == 0 ? Hs : roof(q0), z1 = j + 1 == N ? Hs : roof(q1);
        segs.push_back({{q0, z0}, {q1, z1}, radial(q0, z0), radial(q1, z1)});
    }
    segs.push_back({{W, Hs}, {W, zl}, {-1, 0}, {-1, 0}});
    if (W > v.pr.d_left + 1e-6) segs.push_back({{W, zl}, {v.pr.d_left, zl}, {0, 1}, {0, 1}});
    std::vector<double> run(segs.size() + 1, 0.0);
    for (std::size_t k = 0; k < segs.size(); ++k) run[k + 1] = run[k] + (segs[k].b - segs[k].a).length();

    const auto depth_at = [&](double s) { return std::min(s - s0, s1 - s); };
    v.sink.begin(ElementKind::TunnelLining, structure_surface::lining, v.id, StructureKind::Tunnel, s0, s1, false, tag);
    for (std::size_t i = 0; i + 1 < n; ++i)
        for (std::size_t k = 0; k < segs.size(); ++k) {
            const PSeg& sg = segs[k];
            const auto mk = [&](std::size_t fi, Vec2 p, Vec2 nrm, double vcoord) {
                const Frame& f = F[fi];
                Vtx r = vx(lp(f, p.x, f.z + p.y), f.s, vcoord, depth_at(f.s));
                r.has_n = true;
                r.n = left_of(f.h) * nrm.x + Vec3{0, 0, nrm.y};
                return r;
            };
            const Vtx a0 = mk(i, sg.a, sg.na, run[k]), b0 = mk(i, sg.b, sg.nb, run[k + 1]);
            const Vtx a1 = mk(i + 1, sg.a, sg.na, run[k]), b1 = mk(i + 1, sg.b, sg.nb, run[k + 1]);
            const Vec3 hint = a0.n + b0.n + a1.n + b1.n;
            v.sink.quad(a0, a1, b1, b0, hint);
        }
    v.sink.end();

    // ---- lamps
    {
        const double L = s1 - s0;
        const int count = static_cast<int>(std::floor(L / std::max(P.lamp_spacing, 1.0)));
        const double lat = std::min(P.lamp_lateral, W - 1.0);
        for (int k = 0; k < count && lat > 0.5; ++k) {
            const double s = s0 + L * (k + 0.5) / count;
            const Frame f = frame_at(v.e, s);
            for (const double row : {-lat, lat}) {
                InstanceTransform t;
                t.position = lp(f, row, f.z + roof(row) - P.lamp_drop);
                t.yaw = f.h;
                t.pitch = std::atan(v.e.vertical.grade(s));
                add_instance(v.out, "tunnel_lamp", v.id, t);
            }
        }
    }

    // ---- headwalls
    for (const bool start : {true, false}) {
        const Frame& f = start ? F.front() : F.back();
        const int dir = start ? 1 : -1;
        const double c = std::cos(f.h), sn = std::sin(f.h);
        const auto V = [&](double al, double la, double z, double u, double vv) {
            return vx(Vec3{f.p.x + c * al * dir - sn * la, f.p.y + sn * al * dir + c * la, z}, u, vv);
        };
        const double HT = P.headwall_thickness, ct = P.coping_thickness, o = P.coping_overhang;
        const double Wh = W + P.wall_thickness;
        const double zb = f.z - P.headwall_foot;
        const Vec3 front = fwd_of(f.h) * static_cast<double>(-dir), back = front * -1.0, lat_dir = left_of(f.h);
        // Lateral breakpoints: stub outer, arch points, stub outer.
        std::vector<double> brk{-Wh};
        for (int j = 0; j <= N; ++j) brk.push_back(-W + 2 * W * j / N);
        brk.push_back(Wh);
        const std::size_t nb = brk.size();
        std::vector<double> top(nb);
        for (std::size_t k = 0; k < nb; ++k) {
            double g = -kInf;
            for (double al = 0; al <= HT + 1e-6; al += 1.0)
                for (const double d : {-0.4, 0.0, 0.4}) {
                    const Vec3 q = V(al, brk[k] + d, 0, 0, 0).p;
                    g = std::max({g, v.ground.height(q.x, q.y), v.ground.natural_height(q.x, q.y)});
                }
            double t = g + P.headwall_ground_margin;
            if (std::abs(brk[k]) <= W + 1e-9) t = std::max(t, f.z + roof(std::min(std::abs(brk[k]), W)) + P.headwall_crown);
            top[k] = t;
        }
        if (v.wall_ends)
            for (const WallEnd& we : *v.wall_ends)
                if ((we.portal - f.p).length() < 0.5) {
                    if (we.side > 0) top[nb - 1] = top[nb - 2] = we.level;
                    else top[0] = top[1] = we.level;
                }
        std::vector<double> bt(nb);
        for (std::size_t k = 0; k < nb; ++k) bt[k] = top[k] - ct;
        // Floor chain (lower pieces under the opening): the road's top profile between the walls.
        std::vector<double> fl{-W};
        for (const Vec2& p : v.pr.top)
            if (p.x > -W + 1e-6 && p.x < W - 1e-6) fl.push_back(p.x);
        fl.push_back(W);
        const auto zfl = [&](double d) { return f.z + v.pr.z_at(d); };
        const double zHs = f.z + Hs;

        v.sink.begin(ElementKind::Headwall, structure_surface::concrete, v.id, StructureKind::Tunnel, f.s, f.s, false, tag * 2 + (start ? 0 : 1));
        const auto face = [&](double al, Vec3 hint) {
            // Left stub (lateral -Wh .. -W) and right stub, with the opening's side edge as extra vertices.
            v.sink.poly({V(al, -Wh, zb, -Wh, zb), V(al, -W, zb, -W, zb), V(al, -W, zfl(-W), -W, zfl(-W)), V(al, -W, zHs, -W, zHs),
                         V(al, -W, bt[1], -W, bt[1]), V(al, -Wh, bt[0], -Wh, bt[0])},
                        hint);
            v.sink.poly({V(al, W, zb, W, zb), V(al, Wh, zb, Wh, zb), V(al, Wh, bt[nb - 1], Wh, bt[nb - 1]), V(al, W, bt[nb - 2], W, bt[nb - 2]),
                         V(al, W, zHs, W, zHs), V(al, W, zfl(W), W, zfl(W))},
                        hint);
            for (int j = 0; j < N; ++j) {
                const std::size_t k = static_cast<std::size_t>(j) + 1;
                const double q0 = brk[k], q1 = brk[k + 1];
                const double r0 = j == 0 ? zHs : f.z + roof(q0), r1 = j + 1 == N ? zHs : f.z + roof(q1);
                v.sink.quad(V(al, q0, r0, q0, r0), V(al, q1, r1, q1, r1), V(al, q1, bt[k + 1], q1, bt[k + 1]), V(al, q0, bt[k], q0, bt[k]), hint);
            }
            for (std::size_t k = 0; k + 1 < fl.size(); ++k)
                v.sink.quad(V(al, fl[k], zb, fl[k], zb), V(al, fl[k + 1], zb, fl[k + 1], zb), V(al, fl[k + 1], zfl(fl[k + 1]), fl[k + 1], zfl(fl[k + 1])),
                            V(al, fl[k], zfl(fl[k]), fl[k], zfl(fl[k])), hint);
        };
        face(0.0, front);
        face(HT, back);
        // Top (under the coping), bottom and the two closed side ends.
        for (std::size_t k = 0; k + 1 < nb; ++k)
            v.sink.quad(V(0, brk[k], bt[k], 0, brk[k]), V(HT, brk[k], bt[k], HT, brk[k]), V(HT, brk[k + 1], bt[k + 1], HT, brk[k + 1]),
                        V(0, brk[k + 1], bt[k + 1], 0, brk[k + 1]), kUp);
        {
            std::vector<double> bb{-Wh, -W};
            for (std::size_t k = 1; k + 1 < fl.size(); ++k) bb.push_back(fl[k]);
            bb.push_back(W);
            bb.push_back(Wh);
            // bb includes -W and W once each (fl.front()/back()).
            for (std::size_t k = 0; k + 1 < bb.size(); ++k)
                v.sink.quad(V(0, bb[k], zb, 0, bb[k]), V(HT, bb[k], zb, HT, bb[k]), V(HT, bb[k + 1], zb, HT, bb[k + 1]), V(0, bb[k + 1], zb, 0, bb[k + 1]), kDown);
        }
        for (const int sgn : {-1, 1}) {
            const double e = sgn * Wh, tt = sgn < 0 ? bt[0] : bt[nb - 1];
            v.sink.quad(V(0, e, zb, 0, zb), V(HT, e, zb, HT, zb), V(HT, e, tt, HT, tt), V(0, e, tt, 0, tt), lat_dir * static_cast<double>(sgn));
        }
        v.sink.end();

        // Coping slab over the whole headwall top.
        v.sink.begin(ElementKind::HeadwallCoping, structure_surface::coping, v.id, StructureKind::Tunnel, f.s, f.s, true, tag * 2 + (start ? 0 : 1));
        std::vector<double> cb{-Wh - o};
        cb.insert(cb.end(), brk.begin(), brk.end());
        cb.push_back(Wh + o);
        std::vector<double> ctop{top[0]};
        ctop.insert(ctop.end(), top.begin(), top.end());
        ctop.push_back(top[nb - 1]);
        const double a0 = -o, a1 = HT + o;
        for (std::size_t k = 0; k + 1 < cb.size(); ++k) {
            v.sink.quad(V(a0, cb[k], ctop[k], a0, cb[k]), V(a1, cb[k], ctop[k], a1, cb[k]), V(a1, cb[k + 1], ctop[k + 1], a1, cb[k + 1]),
                        V(a0, cb[k + 1], ctop[k + 1], a0, cb[k + 1]), kUp);
            v.sink.quad(V(a0, cb[k], ctop[k] - ct, a0, cb[k]), V(a1, cb[k], ctop[k] - ct, a1, cb[k]), V(a1, cb[k + 1], ctop[k + 1] - ct, a1, cb[k + 1]),
                        V(a0, cb[k + 1], ctop[k + 1] - ct, a0, cb[k + 1]), kDown);
            v.sink.quad(V(a0, cb[k], ctop[k] - ct, cb[k], ctop[k] - ct), V(a0, cb[k + 1], ctop[k + 1] - ct, cb[k + 1], ctop[k + 1] - ct),
                        V(a0, cb[k + 1], ctop[k + 1], cb[k + 1], ctop[k + 1]), V(a0, cb[k], ctop[k], cb[k], ctop[k]), front);
            v.sink.quad(V(a1, cb[k], ctop[k] - ct, cb[k], ctop[k] - ct), V(a1, cb[k + 1], ctop[k + 1] - ct, cb[k + 1], ctop[k + 1] - ct),
                        V(a1, cb[k + 1], ctop[k + 1], cb[k + 1], ctop[k + 1]), V(a1, cb[k], ctop[k], cb[k], ctop[k]), back);
        }
        for (const int sgn : {-1, 1}) {
            const double e = sgn < 0 ? cb.front() : cb.back(), tt = sgn < 0 ? ctop.front() : ctop.back();
            v.sink.quad(V(a0, e, tt - ct, a0, tt - ct), V(a1, e, tt - ct, a1, tt - ct), V(a1, e, tt, a1, tt), V(a0, e, tt, a0, tt), lat_dir * static_cast<double>(sgn));
        }
        v.sink.end();
    }
}

// ---------------------------------------------------------------- retaining walls

struct WPt {
    double x, y, top, zTop, zBot, h;
};

std::vector<WPt> resample_run(const std::vector<StructureWallPoint>& pts, double step) {
    std::vector<WPt> out;
    for (std::size_t i = 0; i + 1 < pts.size(); ++i) {
        const StructureWallPoint &a = pts[i], &b = pts[i + 1];
        const int m = std::max(1, static_cast<int>(std::ceil(std::hypot(b.x - a.x, b.y - a.y) / step)));
        for (int j = 0; j < m; ++j) {
            const double t = static_cast<double>(j) / m;
            out.push_back({a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.top + (b.top - a.top) * t, a.zTop + (b.zTop - a.zTop) * t,
                           a.zBot + (b.zBot - a.zBot) * t, a.h});
        }
    }
    const StructureWallPoint& l = pts.back();
    out.push_back({l.x, l.y, l.top, l.zTop, l.zBot, l.h});
    return out;
}

struct PanelPlan {
    std::vector<std::size_t> bnd;  // station indices; panel p spans bnd[p]..bnd[p+1]
    std::vector<double> level;     // top of coping per panel
};

std::vector<double> panel_levels(const std::vector<std::size_t>& bnd, const std::vector<double>& need, double q) {
    const std::size_t np = bnd.size() - 1, m = need.size();
    std::vector<double> lvl(np), pl(np);
    for (std::size_t p = 0; p < np; ++p) {
        double nd = -kInf;
        const std::size_t hi = p + 1 == np ? m - 1 : bnd[p + 1] - 1;
        for (std::size_t i = bnd[p]; i <= hi; ++i) nd = std::max(nd, need[i]);
        lvl[p] = ceil_to(nd, q);
    }
    double cur = lvl[0];
    pl[0] = cur;
    for (std::size_t p = 1; p < np; ++p) {
        if (lvl[p] > cur) cur = lvl[p];  // rise immediately
        else if (lvl[p] < cur) {         // step down only when the lower need holds for two panels
            const double tg = std::max(lvl[p], p + 1 < np ? lvl[p + 1] : lvl[p]);
            if (tg < cur) cur = tg;
        }
        pl[p] = cur;
    }
    return pl;
}

PanelPlan plan_panels(const std::vector<double>& need, const std::vector<double>& cum, const StructureParams& P) {
    const std::size_t m = need.size();
    PanelPlan plan;
    const double tot = cum.back();
    const double q = P.wall_quant;
    const double max_step = std::max(q, std::ceil(P.wall_max_step / q - 1e-9) * q);
    const int np0 = std::max(1, static_cast<int>(std::lround(tot / P.wall_panel)));
    plan.bnd.push_back(0);
    for (int p = 1; p < np0; ++p) {
        std::size_t idx = plan.bnd.back();
        while (idx + 1 < m && cum[idx] < tot * p / np0 - 1e-9) ++idx;
        if (idx > plan.bnd.back() && idx < m - 1) plan.bnd.push_back(idx);
    }
    plan.bnd.push_back(m - 1);
    const double step_len = tot / std::max<std::size_t>(1, m - 1);
    const std::size_t min_idx = std::max<std::size_t>(1, static_cast<std::size_t>(std::lround(P.wall_min_panel / std::max(step_len, 1e-6))));
    std::vector<double> pl = panel_levels(plan.bnd, need, q);
    for (int pass = 0; pass < 6; ++pass) {
        const std::size_t np = pl.size();
        std::vector<bool> split(np, false);
        bool any = false;
        for (std::size_t p = 1; p < np; ++p)
            if (std::abs(pl[p] - pl[p - 1]) > max_step + 1e-9) split[p - 1] = split[p] = true;
        std::vector<std::size_t> nb{plan.bnd.front()};
        for (std::size_t p = 0; p < np; ++p) {
            const std::size_t a = plan.bnd[p], b = plan.bnd[p + 1];
            if (split[p] && b - a >= 2 * min_idx) {
                nb.push_back(a + (b - a) / 2);
                any = true;
            }
            nb.push_back(b);
        }
        if (!any) break;
        plan.bnd = std::move(nb);
        pl = panel_levels(plan.bnd, need, q);
    }
    // Enforce the step limit by raising the lower of two neighbours (always safe: tops only go up).
    for (int it = 0; it < 200; ++it) {
        bool ch = false;
        for (std::size_t p = 0; p + 1 < pl.size(); ++p) {
            if (pl[p] < pl[p + 1] - max_step - 1e-9) pl[p] = pl[p + 1] - max_step, ch = true;
            else if (pl[p + 1] < pl[p] - max_step - 1e-9) pl[p + 1] = pl[p] - max_step, ch = true;
        }
        if (!ch) break;
    }
    plan.level = std::move(pl);
    return plan;
}

void build_wall_run(const StructureWallRun& run, const std::vector<WPt>& rs, const GroundSampler& ground, const StructureParams& P,
                    const WallContext& ctx, StructureMeshes& out, Sink& sink, int tag) {
    const std::size_t m = rs.size();
    if (m < 2) return;
    const int sg = run.side == 'L' ? 1 : -1;
    const double T = run.hi - run.lo > 0.1 ? run.hi - run.lo : P.wall_thickness;
    std::vector<double> cum(m, 0.0);
    for (std::size_t i = 1; i < m; ++i) cum[i] = cum[i - 1] + std::hypot(rs[i].x - rs[i - 1].x, rs[i].y - rs[i - 1].y);
    const double tot = cum.back();
    if (tot < P.wall_min_run) return;

    std::vector<Vec2> u(m);
    std::vector<double> zb(m), top_raw(m), g_hi(m);
    for (std::size_t i = 0; i < m; ++i) {
        const WPt& q = rs[i];
        const WPt &a = rs[i > 0 ? i - 1 : 0], &b = rs[i + 1 < m ? i + 1 : m - 1];
        double tx = b.x - a.x, ty = b.y - a.y;
        const double tl = std::hypot(tx, ty);
        if (tl > 1e-9) tx /= tl, ty /= tl;
        Vec2 un{-ty * sg, tx * sg};
        if (un.x * -std::sin(q.h) * sg + un.y * std::cos(q.h) * sg < 0) un = {-un.x, -un.y};
        u[i] = un;
        const double gi = ground.height(q.x, q.y), go = ground.height(q.x + un.x * T, q.y + un.y * T);
        const double ni = ground.natural_height(q.x, q.y), no = ground.natural_height(q.x + un.x * T, q.y + un.y * T);
        zb[i] = std::min({q.zBot, gi, go, ni, no}) - P.wall_foot_bury;
        double t = 0;
        if (run.cut) {
            double gm = -kInf;
            for (const double e : {0.0, T / 3, 2 * T / 3, T}) gm = std::max(gm, ground.height(q.x + un.x * e, q.y + un.y * e));
            t = gm + 0.3;
        } else {
            t = std::min({q.top, q.zTop + 0.7, std::max(std::min(gi, go) + 0.55, zb[i] + 0.1)});
        }
        top_raw[i] = t;
        g_hi[i] = std::max(gi, go);
    }
    // Smooth buried foot: moving minimum then moving average.
    {
        const double step = tot / static_cast<double>(m - 1);
        const std::size_t W = static_cast<std::size_t>(std::lround(P.wall_foot_smooth / std::max(step, 1e-6)));
        std::vector<double> mn(m);
        for (std::size_t i = 0; i < m; ++i) {
            double v = kInf;
            for (std::size_t j = i > W ? i - W : 0; j <= std::min(m - 1, i + W); ++j) v = std::min(v, zb[j]);
            mn[i] = v;
        }
        for (std::size_t i = 0; i < m; ++i) {
            double sum = 0;
            std::size_t c = 0;
            for (std::size_t j = i > W ? i - W : 0; j <= std::min(m - 1, i + W); ++j) sum += mn[j], ++c;
            zb[i] = sum / static_cast<double>(c);
        }
    }
    const auto near_portal = [&](const WPt& q) {
        for (const WallPortal& pt : ctx.portals)
            if (std::hypot(q.x - pt.pos.x, q.y - pt.pos.y) < P.wing_length + 6) return true;
        return false;
    };
    const bool tap_a = !near_portal(rs.front()), tap_b = !near_portal(rs.back());
    std::vector<double> need(m);
    for (std::size_t i = 0; i < m; ++i) {
        double nd = std::max(top_raw[i], zb[i] + 0.3);
        // Free ends blend down to the ground they retain.
        double f = 1.0;
        if (tap_a) f = std::min(f, cum[i] / P.wall_taper);
        if (tap_b) f = std::min(f, (tot - cum[i]) / P.wall_taper);
        f = std::clamp(f, 0.0, 1.0);
        const double base = std::max(g_hi[i] + 0.1, zb[i] + 0.3);
        if (nd > base) nd = base + (nd - base) * f;
        need[i] = nd;
    }
    // The wall panel beside a headwall reaches the headwall stub top.
    std::vector<std::pair<std::size_t, std::size_t>> portal_end;  // (station, portal index)
    for (std::size_t pi = 0; pi < ctx.portals.size(); ++pi) {
        const WallPortal& pt = ctx.portals[pi];
        const Vec2 n{-std::sin(pt.heading) * sg, std::cos(pt.heading) * sg};
        const Vec2 line{pt.pos.x + n.x * pt.half_width, pt.pos.y + n.y * pt.half_width};
        for (const std::size_t ie : {std::size_t{0}, m - 1}) {
            if (std::hypot(rs[ie].x - line.x, rs[ie].y - line.y) >= 0.75) continue;
            double g = -kInf;
            for (double al = 0; al <= P.headwall_thickness + 1e-6; al += 1.0)
                for (double e = 0; e <= P.wall_thickness + 1e-6; e += 0.5) {
                    const double x = pt.pos.x + std::cos(pt.heading) * al * pt.dir - std::sin(pt.heading) * sg * (pt.half_width + e);
                    const double y = pt.pos.y + std::sin(pt.heading) * al * pt.dir + std::cos(pt.heading) * sg * (pt.half_width + e);
                    g = std::max({g, ground.height(x, y), ground.natural_height(x, y)});
                }
            need[ie] = std::max(need[ie], g + 0.3);
            portal_end.push_back({ie, pi});
        }
    }
    const PanelPlan plan = plan_panels(need, cum, P);
    const auto panel_of = [&](std::size_t i) {
        std::size_t p = 0;
        while (p + 1 < plan.level.size() && plan.bnd[p + 1] <= i) ++p;
        return p;
    };
    for (const auto& [ie, pi] : portal_end) out.wall_ends.push_back({ctx.portals[pi].pos, sg, plan.level[panel_of(ie)]});

    std::vector<WStation> st;
    for (std::size_t p = 0; p < plan.level.size(); ++p) {
        for (std::size_t i = plan.bnd[p]; i <= plan.bnd[p + 1]; ++i) {
            const bool first = i == plan.bnd[p];
            if (first && p > 0 && plan.level[p] == plan.level[p - 1]) continue;  // no step: panels merge
            WStation w;
            w.a = {rs[i].x, rs[i].y};
            w.u = u[i];
            w.zb = zb[i];
            w.zt = plan.level[p];
            w.dup = first && p > 0;
            st.push_back(w);
        }
    }
    WallSpec ws;
    ws.T = T;
    ws.coping_t = P.coping_thickness;
    ws.overhang = P.coping_overhang;
    ws.tag = tag;
    emit_stepped_wall(sink, std::move(st), ws);
}

// ---------------------------------------------------------------- driver

void finish(StructureMeshes& out) {
    for (auto it = out.meshes.begin(); it != out.meshes.end();) {
        if (it->second.indices.empty()) {
            out.colours.erase(it->first);
            it = out.meshes.erase(it);
        } else {
            ++it;
        }
    }
    for (auto it = out.colours.begin(); it != out.colours.end();) it = out.meshes.count(it->first) ? std::next(it) : out.colours.erase(it);
}

void build_edge_into(StructureMeshes& out, Sink& sink, const RoadGraph& graph, const RegionPack& region, EdgeId id,
                     const GroundSampler& ground, const StructureParams& P, const OtherRoads* others, double trim_a,
                     double trim_b, const std::vector<WallEnd>* wall_ends) {
    const Edge* e = graph.edge(id);
    if (!e || e->horizontal.spans.empty() || e->structure.empty()) return;
    const RoadType* rt = region.road_type(e->road_type);
    if (!rt) return;
    XProfile pr = make_profile(*e, *rt, region);
    if (!pr.ok()) return;
    Env v{graph, region, ground, P, sink, out, others, *e, id, std::move(pr), e->horizontal.length(), trim_a, trim_b, wall_ends};
    int bi = 0, ti = 0;
    for (const StructureSpan& span : e->structure) {
        if (span.kind == StructureKind::Bridge) build_bridge(v, span, bi++);
        else if (span.kind == StructureKind::Tunnel) build_tunnel(v, span, ti++);
    }
    sink.end();
}

bool has_kind(const Edge& e, StructureKind k) {
    for (const StructureSpan& s : e.structure)
        if (s.kind == k) return true;
    return false;
}

}  // namespace

StructureParams structure_params_for(const RegionPack& region) {
    StructureParams p;
    p.wall_thickness = region.grading.wall_thickness;
    p.tunnel_clear_height = region.grading.tunnel_clear_height;
    return p;
}

StructureMeshes build_edge_structures(const RoadGraph& graph, const RegionPack& region, EdgeId edge, const GroundSampler& ground,
                                      const StructureParams& params, double trim_a, double trim_b,
                                      const std::vector<WallEnd>* wall_ends) {
    StructureMeshes out;
    const Edge* e = graph.edge(edge);
    if (!e) return out;
    Sink sink(out, params.depth_range);
    if (has_kind(*e, StructureKind::Bridge)) {
        const OtherRoads others(graph, region);
        build_edge_into(out, sink, graph, region, edge, ground, params, &others, trim_a, trim_b, wall_ends);
    } else {
        build_edge_into(out, sink, graph, region, edge, ground, params, nullptr, trim_a, trim_b, wall_ends);
    }
    finish(out);
    return out;
}

StructureMeshes build_retaining_walls(const std::vector<StructureWallRun>& runs, const GroundSampler& ground,
                                      const StructureParams& params, const WallContext& context) {
    StructureMeshes out;
    Sink sink(out, params.depth_range);
    int tag = 0;
    for (const StructureWallRun& run : runs) {
        if (run.pts.size() < 2) continue;
        const int sg = run.side == 'L' ? 1 : -1;
        // Drop wall inside the exclusion discs (bridge spans and abutments: the wing walls continue it).
        std::vector<std::vector<StructureWallPoint>> segs;
        std::vector<StructureWallPoint> cur;
        for (const StructureWallPoint& q : run.pts) {
            bool excluded = false;
            for (const auto& [c, r] : context.exclusion)
                if (std::hypot(q.x - c.x, q.y - c.y) < r) {
                    excluded = true;
                    break;
                }
            if (!excluded) cur.push_back(q);
            else {
                if (!cur.empty()) segs.push_back(std::move(cur));
                cur.clear();
            }
        }
        if (!cur.empty()) segs.push_back(std::move(cur));
        for (const auto& seg : segs) {
            if (seg.size() < 2) continue;
            const std::vector<WPt> rs = resample_run(seg, std::max(params.wall_station_step, 0.25));
            build_wall_run(run, rs, ground, params, context, out, sink, tag * 2 + (sg > 0 ? 0 : 1));
            ++tag;
        }
    }
    sink.end();
    finish(out);
    return out;
}

WallContext wall_context_for_graph(const RoadGraph& graph, const RegionPack& region, const StructureParams& params) {
    WallContext ctx;
    for (const auto& [id, e] : graph.edges()) {
        const RoadType* rt = region.road_type(e.road_type);
        if (!rt || e.horizontal.spans.empty() || e.structure.empty()) continue;
        const XProfile pr = make_profile(e, *rt, region);
        if (!pr.ok()) continue;
        const double hw = std::max(pr.d_left, -pr.d_right);
        const double len = e.horizontal.length();
        for (const StructureSpan& sp : e.structure) {
            const double s0 = std::max(sp.s0, 0.0), s1 = std::min(sp.s1, len);
            if (sp.kind == StructureKind::Bridge) {
                for (double s = s0; s <= s1 + 1e-9; s += 3.0) {
                    const Frame f = frame_at(e, s);
                    ctx.exclusion.push_back({f.p, hw + 3.0});
                }
                for (const double s : {s0, s1}) ctx.exclusion.push_back({frame_at(e, s).p, params.wing_length});
            } else if (sp.kind == StructureKind::Tunnel) {
                const Frame fa = frame_at(e, s0), fb = frame_at(e, s1);
                ctx.portals.push_back({fa.p, fa.h, 1, hw});
                ctx.portals.push_back({fb.p, fb.h, -1, hw});
            }
        }
    }
    return ctx;
}

StructureMeshes build_structures(const RoadGraph& graph, const RegionPack& region, const GroundSampler& ground,
                                 const StructureParams& params, const std::vector<StructureWallRun>* walls,
                                 const std::vector<EdgeId>& edges) {
    StructureMeshes out;
    if (walls && !walls->empty()) {
        const WallContext ctx = wall_context_for_graph(graph, region, params);
        out = build_retaining_walls(*walls, ground, params, ctx);
    }
    std::vector<EdgeId> ids = edges;
    if (ids.empty())
        for (const auto& [id, e] : graph.edges()) ids.push_back(id);
    const OtherRoads others(graph, region);
    const std::vector<WallEnd> wall_ends = out.wall_ends;
    for (EdgeId id : ids) {
        const Edge* e = graph.edge(id);
        if (!e || e->structure.empty()) continue;
        StructureMeshes part;
        Sink sink(part, params.depth_range);
        build_edge_into(part, sink, graph, region, id, ground, params, &others, 0, 0, &wall_ends);
        finish(part);
        append_structures(out, part);
    }
    return out;
}

void append_structures(StructureMeshes& dst, const StructureMeshes& src) {
    std::map<std::string, std::uint32_t> base;
    for (const auto& [name, m] : src.meshes) base[name] = static_cast<std::uint32_t>(dst.meshes.count(name) ? dst.meshes[name].sections.size() : 0);
    append_mesh(dst.meshes, src.meshes);
    for (const auto& [name, c] : src.colours) dst.colours[name].insert(dst.colours[name].end(), c.begin(), c.end());
    for (StructureElement el : src.elements) {
        el.section += base[el.surface];
        dst.elements.push_back(el);
    }
    for (const InstanceList& l : src.instances) {
        bool merged = false;
        for (InstanceList& d : dst.instances)
            if (d.kind == l.kind && d.edge == l.edge) {
                d.items.insert(d.items.end(), l.items.begin(), l.items.end());
                merged = true;
                break;
            }
        if (!merged) dst.instances.push_back(l);
    }
    dst.bridges.insert(dst.bridges.end(), src.bridges.begin(), src.bridges.end());
    dst.tunnels.insert(dst.tunnels.end(), src.tunnels.begin(), src.tunnels.end());
    dst.wall_ends.insert(dst.wall_ends.end(), src.wall_ends.begin(), src.wall_ends.end());
}

std::map<TileKey, MeshChunk> build_structure_chunks(const StructureMeshes& s, const ChunkParams& cp, const std::set<TileKey>* only) {
    std::map<TileKey, MeshChunk> chunks;
    for (const auto& [name, m] : s.meshes) {
        const auto cit = s.colours.find(name);
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
                        const Vec3 p = m.positions[v], n = m.normals[v], t = m.tangents[v];
                        buf.positions.push_back({static_cast<float>(p.x - ch.origin.x), static_cast<float>(p.y - ch.origin.y),
                                                 static_cast<float>(p.z - ch.origin.z)});
                        buf.normals.push_back({static_cast<float>(n.x), static_cast<float>(n.y), static_cast<float>(n.z)});
                        buf.tangents.push_back({static_cast<float>(t.x), static_cast<float>(t.y), static_cast<float>(t.z), 1.0f});
                        buf.uv0.push_back({static_cast<float>(m.uv[v].x), static_cast<float>(m.uv[v].y)});
                        buf.colours.push_back(cit != s.colours.end() && v < cit->second.size() ? cit->second[v]
                                                                                                : std::array<std::uint8_t, 4>{0, 0, 0, 255});
                    }
                    buf.indices.push_back(rm[v]);
                }
                buf.sections.back().index_count += 3;
            }
        }
    }
    return chunks;
}

}  // namespace cp
