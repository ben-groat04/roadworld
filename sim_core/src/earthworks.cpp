#include "cp/earthworks.h"

#include <algorithm>
#include <cmath>

namespace cp {
namespace {

Vec2 normal_of(double heading) { return {-std::sin(heading), std::cos(heading)}; }

double heading_at(const Edge& e, double s) {
    double acc = 0;
    const auto& spans = e.horizontal.spans;
    for (std::size_t i = 0; i < spans.size(); ++i) {
        const Clothoid& c = spans[i];
        if (s <= acc + c.length || i + 1 == spans.size())
            return c.heading(std::clamp(s - acc, 0.0, c.length));
        acc += c.length;
    }
    return 0;
}

struct SideResult {
    SideSection sec;
    bool needs_structure = false;
};

SideResult compute_side(Vec3 centre, double heading, double sign, double hw, const Heightfield& hf,
                        const EarthworkParams& p) {
    SideResult r;
    SideSection& sec = r.sec;
    sec.edge = hw;
    sec.batter = p.batter;
    sec.z0 = centre.z - p.formation_drop;
    sec.d1 = hw;
    sec.z1 = sec.z0;
    const Vec2 n = normal_of(heading) * sign;
    const Vec2 base = centre.xy() + n * hw;
    auto ground = [&](double d) {
        const Vec2 q = base + n * d;
        return hf.height(q.x, q.y);
    };
    const double g0 = ground(0);
    sec.kind = SideSection::Slope;
    if (std::abs(g0 - sec.z0) < 1e-3) return r;

    sec.cut = g0 > sec.z0;
    const double dir = sec.cut ? 1.0 : -1.0;
    // h > 0 while the slope has not yet reached natural ground.
    auto h = [&](double d) { return dir * (ground(d) - (sec.z0 + dir * d / p.batter)); };

    const double dmax = p.wall_cap * p.batter;
    const double step = 0.25;
    double lo = 0, hi = -1;
    for (double d = step;; d += step) {
        const double dd = std::min(d, dmax);
        if (h(dd) <= 0) {
            hi = dd;
            break;
        }
        lo = dd;
        if (dd >= dmax) break;
    }
    if (hi < 0) {
        r.needs_structure = true;
        sec.kind = SideSection::None;
        return r;
    }
    while (hi - lo > 0.005) {
        const double mid = 0.5 * (lo + hi);
        (h(mid) <= 0 ? hi : lo) = mid;
    }
    const double off = 0.5 * (lo + hi);
    const double height = off / p.batter;
    if (height > p.wall_start) {
        sec.kind = SideSection::Wall;
        sec.d1 = hw + p.wall_thickness;
        sec.z1 = ground(p.wall_thickness);
    } else {
        sec.d1 = hw + off;
        sec.z1 = sec.z0 + dir * height;
    }
    return r;
}

// Per-structure smoothing over station flags.
void smooth_structure(std::vector<CrossSection>& cs, double ds) {
    const std::size_t n = cs.size();
    if (n == 0) return;
    for (Structure target : {Structure::Bridge, Structure::Tunnel}) {
        // Fill gaps (maximal runs of != target between two target stations) shorter than 20 m.
        std::size_t i = 0;
        while (i < n) {
            if (cs[i].structure == target) { ++i; continue; }
            std::size_t j = i;
            while (j < n && cs[j].structure != target) ++j;
            const bool between = i > 0 && j < n;
            if (between && static_cast<double>(j - i) * ds < 20.0)
                for (std::size_t k = i; k < j; ++k) cs[k].structure = target;
            i = j;
        }
    }
    // Drop structure runs shorter than 10 m.
    std::size_t i = 0;
    while (i < n) {
        if (cs[i].structure == Structure::Ground) { ++i; continue; }
        std::size_t j = i;
        while (j < n && cs[j].structure == cs[i].structure) ++j;
        if (static_cast<double>(j - i) * ds < 10.0)
            for (std::size_t k = i; k < j; ++k) cs[k].structure = Structure::Ground;
        i = j;
    }
}

}  // namespace

std::vector<CrossSection> compute_cross_sections(const Edge& e, double hw, const Heightfield& hf,
                                                 const EarthworkParams& p, double station_step) {
    std::vector<CrossSection> out;
    const double len = e.horizontal.length();
    if (!(len > 0) || !(station_step > 0)) return out;
    const int segs = std::max(1, static_cast<int>(std::ceil(len / station_step - 1e-9)));
    const double ds = len / segs;
    for (int i = 0; i <= segs; ++i) {
        CrossSection c;
        c.s = i == segs ? len : i * ds;
        c.centre = edge_point(e, c.s);
        c.heading = heading_at(e, c.s);
        const SideResult l = compute_side(c.centre, c.heading, +1, hw, hf, p);
        const SideResult r = compute_side(c.centre, c.heading, -1, hw, hf, p);
        c.left = l.sec;
        c.right = r.sec;
        if (l.needs_structure || r.needs_structure) {
            const bool fill = (l.needs_structure && !l.sec.cut) || (r.needs_structure && !r.sec.cut);
            c.structure = fill ? Structure::Bridge : Structure::Tunnel;
        }
        out.push_back(c);
    }
    smooth_structure(out, ds);
    for (CrossSection& c : out) {
        if (c.structure != Structure::Ground) {
            c.left.kind = SideSection::None;
            c.right.kind = SideSection::None;
        }
    }
    return out;
}

namespace {

struct RowPt {
    double d, z;
};

// kEarthworkRows points in (lateral offset, z), edge first.
void side_rows(const SideSection& s, double overlap, RowPt out[kEarthworkRows]) {
    const double dz_ext = overlap / s.batter;
    auto mid = [](RowPt a, RowPt b) { return RowPt{0.5 * (a.d + b.d), 0.5 * (a.z + b.z)}; };
    if (s.kind == SideSection::Wall) {
        if (s.cut) {
            // Vertical inner face up to ground, a cap across the wall, then under the ground.
            out[0] = {s.edge, s.z0};
            out[1] = mid(out[0], {s.edge, s.z1});
            out[2] = {s.edge, s.z1};
            out[3] = {s.d1, s.z1};
            out[4] = {s.d1 + overlap, s.z1 - dz_ext};
        } else {
            // Top of the wall, outer face down to ground, then straight down under it.
            out[0] = {s.edge, s.z0};
            out[1] = {s.d1, s.z0};
            out[2] = {s.d1, 0.5 * (s.z0 + s.z1)};
            out[3] = {s.d1, s.z1};
            out[4] = {s.d1, s.z1 - overlap};
        }
        return;
    }
    out[0] = {s.edge, s.z0};
    out[1] = mid(out[0], {s.d1, s.z1});
    out[2] = {s.d1, s.z1};
    const double zext = s.z1 - dz_ext;  // fill: same slope; cut: turned back down
    out[4] = {s.d1 + overlap, zext};
    out[3] = mid(out[2], out[4]);
}

bool has_earthwork(const CrossSection& c) {
    return c.structure == Structure::Ground && c.left.kind != SideSection::None &&
           c.right.kind != SideSection::None;
}

}  // namespace

MeshData build_slope_mesh(const std::vector<CrossSection>& cs, double overlap) {
    MeshData m;
    const std::size_t n = cs.size();
    if (n == 0) return m;
    constexpr std::size_t R = kEarthworkRows;
    m.positions.resize(2 * n * R);
    m.normals.assign(2 * n * R, Vec3{0, 0, 0});
    for (std::size_t side = 0; side < 2; ++side) {
        for (std::size_t i = 0; i < n; ++i) {
            const CrossSection& c = cs[i];
            const SideSection& s = side == 0 ? c.left : c.right;
            const double sign = side == 0 ? 1.0 : -1.0;
            const Vec2 nrm = normal_of(c.heading) * sign;
            RowPt rows[R];
            side_rows(s, overlap, rows);
            for (std::size_t r = 0; r < R; ++r) {
                const Vec2 q = c.centre.xy() + nrm * rows[r].d;
                m.positions[(side * n + i) * R + r] = {q.x, q.y, rows[r].z};
            }
        }
    }
    auto emit = [&](std::uint32_t a, std::uint32_t b, std::uint32_t c) {
        const Vec3 &pa = m.positions[a], &pb = m.positions[b], &pc = m.positions[c];
        const Vec3 u{pb.x - pa.x, pb.y - pa.y, pb.z - pa.z};
        const Vec3 v{pc.x - pa.x, pc.y - pa.y, pc.z - pa.z};
        const Vec3 nn{u.y * v.z - u.z * v.y, u.z * v.x - u.x * v.z, u.x * v.y - u.y * v.x};
        if (std::sqrt(nn.x * nn.x + nn.y * nn.y + nn.z * nn.z) < 1e-9) return;  // degenerate
        for (std::uint32_t k : {a, b, c}) {
            m.normals[k].x += nn.x;
            m.normals[k].y += nn.y;
            m.normals[k].z += nn.z;
        }
        m.indices.insert(m.indices.end(), {a, b, c});
    };
    for (std::size_t side = 0; side < 2; ++side) {
        for (std::size_t i = 0; i + 1 < n; ++i) {
            if (!has_earthwork(cs[i]) || !has_earthwork(cs[i + 1])) continue;
            for (std::size_t r = 0; r + 1 < R; ++r) {
                const auto A = static_cast<std::uint32_t>((side * n + i) * R + r);
                const auto B = static_cast<std::uint32_t>((side * n + i + 1) * R + r);
                const auto C = A + 1;
                const auto D = B + 1;
                if (side == 0) {
                    emit(A, B, D);
                    emit(A, D, C);
                } else {
                    emit(A, D, B);
                    emit(A, C, D);
                }
            }
        }
    }
    for (Vec3& v : m.normals) {
        const double l = std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
        if (l > 1e-12) v = {v.x / l, v.y / l, v.z / l};
        else v = {0, 0, 1};
    }
    return m;
}

std::vector<std::vector<Vec2>> corridor_footprint(const std::vector<CrossSection>& cs, double inset) {
    std::vector<std::vector<Vec2>> out;
    auto offset_of = [&](const SideSection& s) {
        if (s.kind == SideSection::Wall) return s.d1;
        return std::max(s.edge, s.d1 - inset);
    };
    std::size_t i = 0;
    while (i < cs.size()) {
        if (cs[i].structure != Structure::Ground) { ++i; continue; }
        std::size_t j = i;
        while (j < cs.size() && cs[j].structure == Structure::Ground) ++j;
        if (j - i >= 2) {
            std::vector<Vec2> poly;
            for (std::size_t k = i; k < j; ++k)
                poly.push_back(cs[k].centre.xy() + normal_of(cs[k].heading) * offset_of(cs[k].left));
            for (std::size_t k = j; k-- > i;)
                poly.push_back(cs[k].centre.xy() - normal_of(cs[k].heading) * offset_of(cs[k].right));
            out.push_back(std::move(poly));
        }
        i = j;
    }
    return out;
}

}  // namespace cp
