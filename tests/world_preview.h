#pragma once
// Test-side PNG previews of the test world (no deps: PNG with stored deflate blocks). Included by test_world.cpp;
// runs only when the env var CP_WORLD_PREVIEW_DIR is set.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "cp/test_world.h"

namespace cpprev {

struct Img {
    int w = 0, h = 0;
    std::vector<std::uint8_t> rgb;
    Img(int w_, int h_) : w(w_), h(h_), rgb(static_cast<std::size_t>(w_) * h_ * 3, 0) {}
    void put(int x, int y, double r, double g, double b) {
        if (x < 0 || y < 0 || x >= w || y >= h) return;
        auto c = [](double v) { return static_cast<std::uint8_t>(std::clamp(v, 0.0, 255.0) + 0.5); };
        std::uint8_t* p = &rgb[(static_cast<std::size_t>(y) * w + x) * 3];
        p[0] = c(r); p[1] = c(g); p[2] = c(b);
    }
    void get(int x, int y, double& r, double& g, double& b) const {
        const std::uint8_t* p = &rgb[(static_cast<std::size_t>(y) * w + x) * 3];
        r = p[0]; g = p[1]; b = p[2];
    }
};

inline std::uint32_t crc32(const std::uint8_t* d, std::size_t n, std::uint32_t c = 0) {
    static std::uint32_t t[256];
    static bool init = false;
    if (!init) {
        for (std::uint32_t i = 0; i < 256; ++i) {
            std::uint32_t v = i;
            for (int k = 0; k < 8; ++k) v = (v & 1) ? 0xEDB88320u ^ (v >> 1) : v >> 1;
            t[i] = v;
        }
        init = true;
    }
    c = ~c;
    for (std::size_t i = 0; i < n; ++i) c = t[(c ^ d[i]) & 255] ^ (c >> 8);
    return ~c;
}

inline void be32(std::vector<std::uint8_t>& v, std::uint32_t x) {
    for (int s = 24; s >= 0; s -= 8) v.push_back(static_cast<std::uint8_t>(x >> s));
}
inline void chunk(std::vector<std::uint8_t>& out, const char* type, const std::vector<std::uint8_t>& data) {
    be32(out, static_cast<std::uint32_t>(data.size()));
    std::vector<std::uint8_t> td(type, type + 4);
    td.insert(td.end(), data.begin(), data.end());
    out.insert(out.end(), td.begin(), td.end());
    be32(out, crc32(td.data(), td.size()));
}

inline bool write_png(const std::string& path, const Img& im) {
    std::vector<std::uint8_t> raw;
    for (int y = 0; y < im.h; ++y) {
        raw.push_back(0);
        raw.insert(raw.end(), im.rgb.begin() + static_cast<std::ptrdiff_t>(y) * im.w * 3, im.rgb.begin() + static_cast<std::ptrdiff_t>(y + 1) * im.w * 3);
    }
    std::vector<std::uint8_t> z = {0x78, 0x01};
    std::size_t pos = 0;
    while (pos < raw.size()) {
        const std::size_t n = std::min<std::size_t>(65535, raw.size() - pos);
        z.push_back(pos + n == raw.size() ? 1 : 0);
        z.push_back(static_cast<std::uint8_t>(n & 255)); z.push_back(static_cast<std::uint8_t>(n >> 8));
        z.push_back(static_cast<std::uint8_t>(~n & 255)); z.push_back(static_cast<std::uint8_t>((~n >> 8) & 255));
        z.insert(z.end(), raw.begin() + static_cast<std::ptrdiff_t>(pos), raw.begin() + static_cast<std::ptrdiff_t>(pos + n));
        pos += n;
    }
    std::uint32_t a = 1, b = 0;
    for (std::uint8_t c : raw) { a = (a + c) % 65521; b = (b + a) % 65521; }
    be32(z, (b << 16) | a);
    std::vector<std::uint8_t> out = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
    std::vector<std::uint8_t> ihdr;
    be32(ihdr, static_cast<std::uint32_t>(im.w)); be32(ihdr, static_cast<std::uint32_t>(im.h));
    ihdr.push_back(8); ihdr.push_back(2); ihdr.push_back(0); ihdr.push_back(0); ihdr.push_back(0);
    chunk(out, "IHDR", ihdr);
    chunk(out, "IDAT", z);
    chunk(out, "IEND", {});
    std::FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return false;
    std::fwrite(out.data(), 1, out.size(), f);
    std::fclose(f);
    return true;
}

struct Rgb { double r, g, b; };
inline double lerp3(double a, double b, double t) { return a + (b - a) * t; }

// Lambert shading from the NW at 40 degrees elevation, vertical exaggeration 1.
inline double shade(const cp::Heightfield& hf, double x, double y) {
    const double h = 3;
    const double gx = (hf.height(x + h, y) - hf.height(x - h, y)) / (2 * h);
    const double gy = (hf.height(x, y + h) - hf.height(x, y - h)) / (2 * h);
    const double n = std::sqrt(gx * gx + gy * gy + 1);
    const double lx = -0.5, ly = 0.5, lz = 0.7071;  // towards the light (NW, up)
    const double l = std::sqrt(lx * lx + ly * ly + lz * lz);
    const double d = (-gx * lx - gy * ly + lz) / (n * l);
    return std::clamp(0.25 + 0.9 * d, 0.0, 1.2);
}

// Colour of a ground sample by land use (round 4): base colours read off the vertical aerials 46-59, per-field tint, and
// tramline / furrow hatching along the field's row direction for cropped fields (the stripes that make the aerials look like aerials).
inline double vnoise(double x, double y, double wl, double salt) {
    const double u = x / wl + salt * 17.3, v = y / wl + salt * 5.1;
    const double fu = std::floor(u), fv = std::floor(v);
    auto h = [&](double i, double j) {
        const double t = std::sin(i * 127.1 + j * 311.7 + salt * 74.7) * 43758.5453;
        return t - std::floor(t);
    };
    const double tx = (u - fu) * (u - fu) * (3 - 2 * (u - fu)), ty = (v - fv) * (v - fv) * (3 - 2 * (v - fv));
    return lerp3(lerp3(h(fu, fv), h(fu + 1, fv), tx), lerp3(h(fu, fv + 1), h(fu + 1, fv + 1), tx), ty);
}

inline Rgb ground_colour(const cp::CoverGround& g, const cp::VegetationRules& r, cp::Vec2 p = {0, 0}, bool hatch = true) {
    (void)r;
    Rgb c{96, 118, 70};
    switch (g.use) {
        case cp::LandUse::Woodland: c = {50, 72, 46}; break;
        case cp::LandUse::PermanentPasture: c = {98, 120, 72}; break;
        case cp::LandUse::ImprovedGrass: c = {114, 138, 80}; break;
        case cp::LandUse::Ploughed: c = {128, 106, 88}; break;
        case cp::LandUse::Cereal: c = {84, 112, 70}; break;
        case cp::LandUse::Stubble: c = {172, 154, 116}; break;
        case cp::LandUse::OtherCrop: c = {60, 88, 60}; break;
        case cp::LandUse::RoughGrazing: c = {112, 116, 82}; break;
    }
    // Per-field tint: modest, as neighbouring fields in the aerials differ in tone but not in hue.
    const double t = g.tint;
    const double f = 0.90 + 0.20 * t;
    c = {c.r * f * (1.0 + 0.08 * (t - 0.5)), c.g * f, c.b * f * (1.0 - 0.08 * (t - 0.5))};
    // Mottling inside a field (grazing patches, soil, crop vigour).
    const double m = 0.93 + 0.14 * (0.6 * vnoise(p.x, p.y, 40, 1) + 0.4 * vnoise(p.x, p.y, 11, 2));
    c = {c.r * m, c.g * m, c.b * m};
    const bool cropped = g.use == cp::LandUse::Cereal || g.use == cp::LandUse::OtherCrop || g.use == cp::LandUse::Stubble || g.use == cp::LandUse::Ploughed;
    if (hatch && cropped) {
        const double across = p.x * -g.row_dir.y + p.y * g.row_dir.x;
        const double sp = g.use == cp::LandUse::Ploughed ? 6.0 : 27.0;  // tramlines about 24-28 m apart
        double ph = std::fmod(across + 1000.0 * static_cast<double>(g.field_id % 977) / 977.0, sp);
        if (ph < 0) ph += sp;
        if (ph < 1.2) {
            const double d = g.use == cp::LandUse::Ploughed ? -8 : g.use == cp::LandUse::Cereal ? 16 : g.use == cp::LandUse::Stubble ? -8 : 10;
            c = {c.r + d, c.g + d, c.b + d};
        }
    }
    return c;
}

// Top-down images. `px` metres per pixel.
inline Img hillshade(const cp::TestWorld& tw, const cp::Heightfield& hf, double px) {
    const int n = static_cast<int>(tw.params().size_m / px);
    Img im(n, n);
    const cp::Vec2 lo = tw.world_min();
    auto hgt = [&](int i, int j) { return hf.height(lo.x + (i + 0.5) * px, lo.y + (n - 1 - j + 0.5) * px); };
    for (int j = 0; j < n; ++j)
        for (int i = 0; i < n; ++i) {
            const double x = lo.x + (i + 0.5) * px, y = lo.y + (n - 1 - j + 0.5) * px;
            const double s = shade(hf, x, y);
            const double h = hgt(i, j);
            // contours every 5 m (thicker every 25 m)
            auto band = [&](double step, double v) { return std::floor(v / step); };
            double edge = 0;
            if (i + 1 < n && j + 1 < n) {
                if (band(5, h) != band(5, hgt(i + 1, j)) || band(5, h) != band(5, hgt(i, j + 1))) edge = 1;
                if (band(25, h) != band(25, hgt(i + 1, j)) || band(25, h) != band(25, hgt(i, j + 1))) edge = 2;
            }
            // hypsometric tint: low = green-blue, high = tan
            const double t = std::clamp((h + 10) / 70.0, 0.0, 1.0);
            Rgb c{lerp3(120, 215, t), lerp3(165, 200, t), lerp3(120, 150, t)};
            c = {c.r * s, c.g * s, c.b * s};
            if (edge == 1) c = {c.r * 0.6, c.g * 0.55, c.b * 0.5};
            if (edge == 2) c = {c.r * 0.3, c.g * 0.25, c.b * 0.2};
            im.put(i, j, c.r, c.g, c.b);
        }
    return im;
}

inline Img slope_map(const cp::TestWorld& tw, const cp::Heightfield& hf, double px) {
    const int n = static_cast<int>(tw.params().size_m / px);
    Img im(n, n);
    const cp::Vec2 lo = tw.world_min();
    for (int j = 0; j < n; ++j)
        for (int i = 0; i < n; ++i) {
            const double x = lo.x + (i + 0.5) * px, y = lo.y + (n - 1 - j + 0.5) * px;
            const double h = 3;
            const double gx = (hf.height(x + h, y) - hf.height(x - h, y)) / (2 * h);
            const double gy = (hf.height(x, y + h) - hf.height(x, y - h)) / (2 * h);
            const double s = std::sqrt(gx * gx + gy * gy);
            Rgb c;
            if (s < 0.03) c = {232, 244, 220};
            else if (s < 0.06) c = {190, 224, 150};
            else if (s < 0.12) c = {240, 220, 110};
            else if (s < 0.20) c = {236, 150, 60};
            else if (s < 0.30) c = {210, 60, 40};
            else c = {120, 20, 90};
            im.put(i, j, c.r, c.g, c.b);
        }
    return im;
}

inline Img cover_map(const cp::TestWorld& tw, const cp::Heightfield& hf, const cp::WorldCoverSource& src,
                     const cp::VegetationRules& rules, double px) {
    const int n = static_cast<int>(tw.params().size_m / px);
    Img im(n, n);
    const cp::Vec2 lo = tw.world_min(), hi = tw.world_max();
    for (int j = 0; j < n; ++j)
        for (int i = 0; i < n; ++i) {
            const double x = lo.x + (i + 0.5) * px, y = lo.y + (n - 1 - j + 0.5) * px;
            const Rgb c = ground_colour(src.ground_at({x, y}), rules, {x, y});
            const double s = shade(hf, x, y);
            im.put(i, j, c.r * s, c.g * s, c.b * s);
        }
    auto px_of = [&](cp::Vec2 p, int& i, int& j) {
        i = static_cast<int>((p.x - lo.x) / px);
        j = n - 1 - static_cast<int>((p.y - lo.y) / px);
    };
    std::vector<cp::CoverLine> lines;
    src.lines_touching(lo, hi, lines);
    for (const auto& l : lines)
        for (std::size_t k = 0; k + 1 < l.points.size(); ++k) {
            const cp::Vec2 a = l.points[k], b = l.points[k + 1];
            const int steps = std::max(1, static_cast<int>((b - a).length() / (px * 0.5)));
            for (int s = 0; s <= steps; ++s) {
                const cp::Vec2 p = a + (b - a) * (static_cast<double>(s) / steps);
                int i, j;
                px_of(p, i, j);
                im.put(i, j, 28, 62, 28);
            }
        }
    std::vector<cp::CoverPoint> pts;
    src.points_in(lo, hi, pts);
    for (const auto& p : pts) {
        int i, j;
        px_of(p.pos, i, j);
        im.put(i, j, 20, 48, 24);
    }
    return im;
}

// Top-down crop of the cover (north up): ground classes, hedges drawn at their width, tree crowns as discs.
inline Img cover_crop(const cp::TestWorld& tw, const cp::Heightfield& hf, const cp::WorldCoverSource& src,
                      const cp::VegetationRules& rules, cp::Vec2 origin, double w_m, double h_m, double px) {
    (void)tw;
    const int W = static_cast<int>(w_m / px), H = static_cast<int>(h_m / px);
    Img im(W, H);
    std::vector<std::uint64_t> ids(static_cast<std::size_t>(W) * H, 0);
    for (int j = 0; j < H; ++j)
        for (int i = 0; i < W; ++i) {
            const double x = origin.x + (i + 0.5) * px, y = origin.y + (H - 1 - j + 0.5) * px;
            const cp::CoverGround g = src.ground_at({x, y});
            ids[static_cast<std::size_t>(j) * W + i] = g.field_id;
            const Rgb c = ground_colour(g, rules, {x, y});
            const double s = 0.75 + 0.25 * shade(hf, x, y);
            im.put(i, j, c.r * s, c.g * s, c.b * s);
        }
    // Field boundaries without a hedge (fences, banks) still show as a faint line.
    for (int j = 0; j + 1 < H; ++j)
        for (int i = 0; i + 1 < W; ++i) {
            const std::size_t k = static_cast<std::size_t>(j) * W + i;
            if (ids[k] != ids[k + 1] || ids[k] != ids[k + static_cast<std::size_t>(W)]) {
                double r, g, b;
                im.get(i, j, r, g, b);
                im.put(i, j, r * 0.80, g * 0.80, b * 0.80);
            }
        }
    auto disc = [&](cp::Vec2 p, double r, double cr, double cg, double cb) {
        const int ci = static_cast<int>((p.x - origin.x) / px), cj = H - 1 - static_cast<int>((p.y - origin.y) / px);
        const int ri = std::max(2, static_cast<int>(r / px));
        for (int dj = -ri; dj <= ri; ++dj)
            for (int di = -ri; di <= ri; ++di)
                if (di * di + dj * dj <= ri * ri) im.put(ci + di, cj + dj, cr, cg, cb);
    };
    std::vector<cp::CoverLine> lines;
    src.lines_touching(origin, {origin.x + w_m, origin.y + h_m}, lines);
    for (const auto& l : lines)
        for (std::size_t k = 0; k + 1 < l.points.size(); ++k) {
            const cp::Vec2 a = l.points[k], b = l.points[k + 1];
            const int steps = std::max(1, static_cast<int>((b - a).length() / (px * 0.5)));
            for (int s = 0; s <= steps; ++s) disc(a + (b - a) * (static_cast<double>(s) / steps), 0.42 * l.width, 44, 66, 40);
        }
    std::vector<cp::CoverPoint> pts;
    src.points_in(origin, {origin.x + w_m, origin.y + h_m}, pts);
    for (const auto& p : pts) {
        const bool in_wood = src.ground_at(p.pos).use == cp::LandUse::Woodland;
        if (in_wood) disc(p.pos, 2.5 * p.scale, 38, 68, 36);
        else disc(p.pos, 4.0 * p.scale, 30, 62, 30);
    }
    return im;
}

// Oblique view by ray marching the bilinear heightfield. Camera in sim metres (z up).
inline Img oblique(const cp::TestWorld& tw, const cp::Heightfield& hf, const cp::WorldCoverSource* src,
                   const cp::VegetationRules* rules, double ex, double ey, double ez, double tx, double ty, double tz,
                   int w, int h, double fov_deg) {
    Img im(w, h);
    double fx = tx - ex, fy = ty - ey, fz = tz - ez;
    double fl = std::sqrt(fx * fx + fy * fy + fz * fz);
    fx /= fl; fy /= fl; fz /= fl;
    double rx = fy, ry = -fx, rz = 0;  // right = forward x up(0,0,1)
    const double rl = std::sqrt(rx * rx + ry * ry);
    rx /= rl; ry /= rl;
    const double ux = ry * fz - rz * fy, uy = rz * fx - rx * fz, uz = rx * fy - ry * fx;  // right x forward... = up
    const double th = std::tan(fov_deg * 3.14159265358979 / 360.0);
    const cp::Vec2 lo = tw.world_min(), hi = tw.world_max();
    for (int j = 0; j < h; ++j)
        for (int i = 0; i < w; ++i) {
            const double a = (2.0 * (i + 0.5) / w - 1) * th, b = (1 - 2.0 * (j + 0.5) / h) * th * h / w;
            double dx = fx + a * rx + b * ux, dy = fy + a * ry + b * uy, dz = fz + a * rz + b * uz;
            const double dl = std::sqrt(dx * dx + dy * dy + dz * dz);
            dx /= dl; dy /= dl; dz /= dl;
            double t = 1, hit = -1;
            double prev_t = 0;
            for (int it = 0; it < 1600 && t < 6000; ++it) {
                const double x = ex + dx * t, y = ey + dy * t, z = ez + dz * t;
                if (x < lo.x - 1 || x > hi.x + 1 || y < lo.y - 1 || y > hi.y + 1) { if (it > 5 && dz < 0 && z < -200) break; }
                if (z < hf.height(x, y) && x >= lo.x && x <= hi.x && y >= lo.y && y <= hi.y) {
                    double a0 = prev_t, a1 = t;
                    for (int k = 0; k < 8; ++k) {
                        const double m = 0.5 * (a0 + a1);
                        if (ez + dz * m < hf.height(ex + dx * m, ey + dy * m)) a1 = m; else a0 = m;
                    }
                    hit = a1;
                    break;
                }
                prev_t = t;
                t += std::max(2.0, t * 0.004);
            }
            if (hit < 0) {
                const double s = std::clamp(0.5 + 1.5 * dz, 0.0, 1.0);
                im.put(i, j, lerp3(205, 120, s), lerp3(220, 165, s), lerp3(235, 215, s));
                continue;
            }
            const double x = ex + dx * hit, y = ey + dy * hit;
            Rgb c{130, 160, 90};
            if (src && rules) c = ground_colour(src->ground_at({x, y}), *rules, {x, y}, false);
            const double s = shade(hf, x, y);
            const double fog = std::clamp(hit / 5000.0, 0.0, 0.7);
            im.put(i, j, lerp3(c.r * s, 200, fog), lerp3(c.g * s, 212, fog), lerp3(c.b * s, 225, fog));
        }
    return im;
}

}  // namespace cpprev
