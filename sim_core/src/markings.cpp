// Road markings generator and rule loader (include/cp/markings.h). No exceptions: sim_core builds with
// -fno-exceptions; errors travel as messages / warnings.
#include "cp/markings.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <fstream>
#include <limits>
#include <map>
#include <memory>
#include <set>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "cp/edge_sampling.h"
#include "cp/geom.h"
#include "cp/lanes.h"
#include "cp/render_mesh.h"
#include "cp/units.h"
#include "json_util.h"
#include "nlohmann/json.hpp"

namespace cp {

namespace {

using json = nlohmann::json;

constexpr double kMinArea = 1e-6;  // m^2, polygons below this are dropped as degenerate

std::uint16_t intern(std::vector<std::string>& v, const std::string& s) {
    for (std::size_t i = 0; i < v.size(); ++i)
        if (v[i] == s) return static_cast<std::uint16_t>(i);
    v.push_back(s);
    return static_cast<std::uint16_t>(v.size() - 1);
}

}  // namespace

// ================================================================ output helpers

const char* marking_category_name(MarkingCategory c) {
    switch (c) {
        case MarkingCategory::CentreLine: return "centre_line";
        case MarkingCategory::LaneLine: return "lane_line";
        case MarkingCategory::EdgeLine: return "edge_line";
        case MarkingCategory::ParkingBay: return "parking_bay";
        case MarkingCategory::GiveWayLine: return "give_way_line";
        case MarkingCategory::StopLine: return "stop_line";
        case MarkingCategory::GiveWayTriangle: return "give_way_triangle";
        case MarkingCategory::Hatching: return "hatching";
        case MarkingCategory::Zebra: return "zebra";
        case MarkingCategory::Arrow: return "arrow";
        case MarkingCategory::RoundaboutEntry: return "roundabout_entry";
        case MarkingCategory::Chevron: return "chevron";
    }
    return "?";
}

void MarkingSet::append(const MarkingSet& o) {
    std::vector<std::uint16_t> sm, cm;
    for (const std::string& s : o.styles) sm.push_back(intern(styles, s));
    for (const std::string& s : o.colours) cm.push_back(intern(colours, s));
    const auto base = static_cast<std::uint32_t>(vertices.size());
    vertices.insert(vertices.end(), o.vertices.begin(), o.vertices.end());
    for (Marking m : o.marks) {
        m.style = sm[m.style];
        m.colour = cm[m.colour];
        m.first += base;
        marks.push_back(m);
    }
    warnings.insert(warnings.end(), o.warnings.begin(), o.warnings.end());
}

void MarkingSet::clear() {
    styles.clear();
    colours.clear();
    vertices.clear();
    marks.clear();
    warnings.clear();
}

namespace {

double ring_area(const Vec3* p, std::size_t n) {
    double a = 0;
    for (std::size_t i = 0; i < n; ++i) {
        const Vec3& u = p[i];
        const Vec3& v = p[(i + 1) % n];
        a += u.x * v.y - v.x * u.y;
    }
    return a / 2;
}

bool in_triangle(Vec2 p, Vec2 a, Vec2 b, Vec2 c) {
    const double d1 = (b - a).cross(p - a), d2 = (c - b).cross(p - b), d3 = (a - c).cross(p - c);
    return d1 >= -1e-12 && d2 >= -1e-12 && d3 >= -1e-12;
}

}  // namespace

std::vector<std::uint32_t> triangulate_marking(const MarkingSet& set, const Marking& m) {
    std::vector<std::uint32_t> out;
    const std::span<const Vec3> p = set.points(m);
    const std::size_t n = p.size();
    if (n < 3) return out;
    if (m.strip && n % 2 == 0) {
        for (std::size_t i = 0; i + 1 < n / 2; ++i) {
            const auto a = static_cast<std::uint32_t>(i), b = static_cast<std::uint32_t>(i + 1);
            const auto c = static_cast<std::uint32_t>(n - 2 - i), d = static_cast<std::uint32_t>(n - 1 - i);
            out.insert(out.end(), {a, b, c, a, c, d});
        }
        return out;
    }
    std::vector<std::uint32_t> idx(n);
    for (std::size_t i = 0; i < n; ++i) idx[i] = static_cast<std::uint32_t>(i);
    if (ring_area(p.data(), n) < 0) std::reverse(idx.begin(), idx.end());
    // Drop duplicate and collinear vertices (clipping leaves them) so ear clipping cannot stall.
    for (bool again = true; again && idx.size() > 3;) {
        again = false;
        for (std::size_t i = 0; i < idx.size(); ++i) {
            const Vec2 a = p[idx[(i + idx.size() - 1) % idx.size()]].xy(), bq = p[idx[i]].xy(), c = p[idx[(i + 1) % idx.size()]].xy();
            if ((bq - a).length() < 1e-9 || std::fabs((bq - a).cross(c - bq)) < 1e-10) {
                idx.erase(idx.begin() + static_cast<std::ptrdiff_t>(i));
                again = true;
                break;
            }
        }
    }
    std::size_t guard = 0;
    while (idx.size() > 3 && guard++ < n * n) {
        bool clipped = false;
        for (std::size_t i = 0; i < idx.size(); ++i) {
            const std::size_t i0 = (i + idx.size() - 1) % idx.size(), i2 = (i + 1) % idx.size();
            const Vec2 a = p[idx[i0]].xy(), b = p[idx[i]].xy(), c = p[idx[i2]].xy();
            if ((b - a).cross(c - b) <= 1e-12) continue;  // reflex or collinear
            bool inside = false;
            for (std::size_t k = 0; k < idx.size() && !inside; ++k) {
                if (k == i0 || k == i || k == i2) continue;
                const Vec2 q = p[idx[k]].xy();
                if (in_triangle(q, a, b, c)) inside = true;
            }
            if (inside) continue;
            out.insert(out.end(), {idx[i0], idx[i], idx[i2]});
            idx.erase(idx.begin() + static_cast<std::ptrdiff_t>(i));
            clipped = true;
            break;
        }
        if (!clipped) break;  // degenerate polygon
    }
    if (idx.size() == 3) out.insert(out.end(), {idx[0], idx[1], idx[2]});
    return out;
}

// ================================================================ dashes

std::vector<DashSpan> layout_dashes(double length, double mark, double gap, double min_mark) {
    std::vector<DashSpan> out;
    if (!std::isfinite(length) || !(length > 0) || !(mark > 0)) return out;
    min_mark = std::max(min_mark, 0.0);
    if (gap <= 0) {  // continuous
        if (length >= std::max(min_mark, kEps)) out.push_back({0, length});
        return out;
    }
    const double module = mark + gap;
    constexpr double kMaxMarks = 200000;  // guard against absurd lengths (a 1e7 m line at 6 m would be 1.6 M marks)
    if ((length + gap) / module > kMaxMarks) return out;
    const int n = static_cast<int>(std::floor((length + gap) / module + kEps));
    if (n < 1) {  // shorter than one mark: a single short mark if it is long enough
        if (length >= min_mark - kEps) out.push_back({0, length});
        return out;
    }
    if (mark < min_mark - kEps) return out;  // whole marks below the minimum are not painted
    const double base = n * mark + (n - 1) * gap;
    const double r = std::max(length - base, 0.0);
    const double p = r / 2 - gap;  // partial mark that fits at each end after its gap
    const bool partial = p > kEps && p >= min_mark - kEps;
    double start = r / 2;
    if (partial) {
        out.push_back({0, p});
        start = p + gap;
    }
    for (int i = 0; i < n; ++i) {
        const double s0 = start + i * module;
        out.push_back({s0, s0 + mark});
    }
    if (partial) out.push_back({length - p, length});
    return out;
}

namespace {

// Dashes that start and end flush with the line (transverse lines, crossing stripes): the gap is stretched
// or shrunk (never below half of `gap`) so the first mark begins at 0 and the last ends at `length`.
std::vector<DashSpan> layout_flush(double length, double mark, double gap, double min_mark) {
    std::vector<DashSpan> out;
    if (!std::isfinite(length) || !(length > 0) || !(mark > 0)) return out;
    if (gap <= 0 || length <= mark * 1.5) {
        if (length >= std::max(min_mark, kEps)) out.push_back({0, length});
        return out;
    }
    const double module = mark + gap;
    if ((length + gap) / module > 200000) return out;
    int n = static_cast<int>(std::floor((length + gap) / module + 0.5));
    while (n > 1 && (length - n * mark) / (n - 1) < 0.5 * gap) --n;
    if (n <= 1) {
        out.push_back({0, length});
        return out;
    }
    const double g = (length - n * mark) / (n - 1);
    for (int i = 0; i < n; ++i) out.push_back({i * (mark + g), i * (mark + g) + mark});
    out.back().s1 = length;
    return out;
}

}  // namespace

// ================================================================ conditions and selectors

bool cond_matches(const MarkCond& c, const MarkCtx& x) {
    constexpr double eps = 1e-6;
    if (c.speed_max && x.speed > *c.speed_max + eps) return false;
    if (c.speed_min && x.speed <= *c.speed_min + eps) return false;
    if (c.width_min && x.width < *c.width_min - eps) return false;
    if (c.width_max && x.width > *c.width_max + eps) return false;
    if (c.lanes_min && x.lanes < *c.lanes_min) return false;
    if (c.lanes_max && x.lanes > *c.lanes_max) return false;
    if (c.shoulder && x.shoulder != *c.shoulder) return false;
    if (c.one_way && x.one_way != *c.one_way) return false;
    return true;
}

const std::string* select_style(const StyleSel& sel, const MarkCtx& ctx) {
    for (const StyleChoice& c : sel)
        if (cond_matches(c.when, ctx)) return c.style.empty() ? nullptr : &c.style;
    return nullptr;
}

bool select_value(const ValueSel& sel, const MarkCtx& ctx, double& out) {
    for (const ValueChoice& c : sel)
        if (cond_matches(c.when, ctx)) {
            out = c.value;
            return true;
        }
    return false;
}

double LineStyle::module() const {
    if (strands.empty() || strands[0].gap <= 0) return 0;
    return strands[0].mark + strands[0].gap;
}

const RuleSet* MarkingRules::set(const std::string& id) const {
    auto it = sets.find(id);
    return it == sets.end() ? nullptr : &it->second;
}

// ================================================================ loader

namespace {

using jsonu::Err;

using jsonu::member;

void check_keys(const json& j, std::initializer_list<const char*> allowed, const std::string& ctx, Err& err) {
    if (!j.is_object()) {
        err.set(ctx + ": expected an object");
        return;
    }
    for (auto it = j.begin(); it != j.end(); ++it) {
        const std::string& k = it.key();
        if (!k.empty() && k[0] == '_') continue;
        bool ok = false;
        for (const char* a : allowed)
            if (k == a) ok = true;
        if (!ok) err.set(ctx + ": unknown key \"" + k + "\"");
    }
}

double num(const json& j, const char* key, double def, const std::string& ctx, Err& err) {
    const json* v = member(j, key);
    if (!v) return def;
    if (!v->is_number()) {
        err.set(ctx + ": \"" + key + "\" must be a number");
        return def;
    }
    return v->get<double>();
}

// Integer member with a range check (casting an out-of-range double to int is undefined behaviour).
int int_num(const json& j, const char* key, int def, int lo, int hi, const std::string& ctx, Err& err) {
    const double v = num(j, key, def, ctx, err);
    if (!(v >= lo && v <= hi) || v != std::floor(v)) {
        err.set(ctx + ": \"" + key + "\" must be a whole number in [" + std::to_string(lo) + ", " + std::to_string(hi) + "]");
        return def;
    }
    return static_cast<int>(v);
}

std::string str(const json& j, const char* key, const std::string& def, const std::string& ctx, Err& err) {
    const json* v = member(j, key);
    if (!v) return def;
    if (!v->is_string()) {
        err.set(ctx + ": \"" + key + "\" must be a string");
        return def;
    }
    return v->get<std::string>();
}

bool flag(const json& j, const char* key, bool def, const std::string& ctx, Err& err) {
    const json* v = member(j, key);
    if (!v) return def;
    if (!v->is_boolean()) {
        err.set(ctx + ": \"" + key + "\" must be a boolean");
        return def;
    }
    return v->get<bool>();
}

double unit_to_ms(const std::string& u, Err& err) {
    double f = 1.0;
    if (!speed_unit_to_mps(u, f)) err.set("marking rules: unknown speed_unit \"" + u + "\" (expected " + kSpeedUnitSpellings + ")");
    return f;
}

MarkCond parse_cond(const json* j, double unit, const std::string& ctx, Err& err) {
    MarkCond c;
    if (!j) return c;
    check_keys(*j, {"speed_max", "speed_min", "width_min", "width_max", "lanes_min", "lanes_max", "shoulder", "one_way"},
               ctx + " \"if\"", err);
    if (err.failed()) return c;
    if (member(*j, "speed_max")) c.speed_max = num(*j, "speed_max", 0, ctx, err) * unit;
    if (member(*j, "speed_min")) c.speed_min = num(*j, "speed_min", 0, ctx, err) * unit;
    if (member(*j, "width_min")) c.width_min = num(*j, "width_min", 0, ctx, err);
    if (member(*j, "width_max")) c.width_max = num(*j, "width_max", 0, ctx, err);
    if (member(*j, "lanes_min")) c.lanes_min = int_num(*j, "lanes_min", 0, 0, 64, ctx, err);
    if (member(*j, "lanes_max")) c.lanes_max = int_num(*j, "lanes_max", 0, 0, 64, ctx, err);
    if (member(*j, "shoulder")) c.shoulder = flag(*j, "shoulder", false, ctx, err);
    if (member(*j, "one_way")) c.one_way = flag(*j, "one_way", false, ctx, err);
    return c;
}

StyleSel parse_style_sel(const json* j, double unit, const std::string& ctx, Err& err) {
    StyleSel sel;
    if (!j || err.failed()) return sel;
    if (j->is_string()) {
        sel.push_back({{}, j->get<std::string>()});
        return sel;
    }
    if (!j->is_array()) {
        err.set(ctx + ": \"style\" must be a string or a list of {if, style}");
        return sel;
    }
    for (const json& e : *j) {
        check_keys(e, {"if", "style"}, ctx, err);
        if (err.failed()) return sel;
        StyleChoice c;
        c.when = parse_cond(member(e, "if"), unit, ctx, err);
        const json* s = member(e, "style");
        if (!s || !s->is_string()) {
            err.set(ctx + ": a style choice needs a string \"style\"");
            return sel;
        }
        c.style = s->get<std::string>();
        sel.push_back(std::move(c));
    }
    return sel;
}

ValueSel parse_value_sel(const json* j, double unit, const std::string& ctx, Err& err) {
    ValueSel sel;
    if (!j || err.failed()) return sel;
    if (j->is_number()) {
        sel.push_back({{}, j->get<double>()});
        return sel;
    }
    if (!j->is_array()) {
        err.set(ctx + ": expected a number or a list of {if, value}");
        return sel;
    }
    for (const json& e : *j) {
        check_keys(e, {"if", "value"}, ctx, err);
        if (err.failed()) return sel;
        ValueChoice c;
        c.when = parse_cond(member(e, "if"), unit, ctx, err);
        const json* v = member(e, "value");
        if (!v || !v->is_number()) {
            err.set(ctx + ": a value choice needs a numeric \"value\"");
            return sel;
        }
        c.value = v->get<double>();
        sel.push_back(std::move(c));
    }
    return sel;
}

bool parse_category(const std::string& name, MarkingCategory& out) {
    for (int i = 0; i <= static_cast<int>(MarkingCategory::Chevron); ++i) {
        const auto c = static_cast<MarkingCategory>(i);
        if (name == marking_category_name(c)) {
            out = c;
            return true;
        }
    }
    return false;
}

std::vector<std::vector<Vec2>> parse_polygons(const json& j, const std::string& ctx, Err& err) {
    std::vector<std::vector<Vec2>> out;
    if (!j.is_array()) {
        err.set(ctx + ": a glyph is a list of polygons");
        return out;
    }
    for (const json& poly : j) {
        if (!poly.is_array() || poly.size() < 3) {
            err.set(ctx + ": a glyph polygon needs at least 3 points");
            return out;
        }
        std::vector<Vec2> ring;
        for (const json& pt : poly) {
            if (!pt.is_array() || pt.size() != 2 || !pt[0].is_number() || !pt[1].is_number()) {
                err.set(ctx + ": glyph points are [x, y]");
                return out;
            }
            ring.push_back({pt[0].get<double>(), pt[1].get<double>()});
        }
        out.push_back(std::move(ring));
    }
    return out;
}

std::vector<std::vector<Vec2>> mirrored(std::vector<std::vector<Vec2>> polys) {
    for (auto& ring : polys) {
        for (Vec2& p : ring) p.y = -p.y;
        std::reverse(ring.begin(), ring.end());
    }
    return polys;
}

void parse_styles(const json& root, MarkingRules& r, Err& err) {
    const json* st = member(root, "styles");
    if (!st || !st->is_object()) {
        err.set("marking rules: missing \"styles\" object");
        return;
    }
    for (auto it = st->begin(); it != st->end(); ++it) {
        if (!it.key().empty() && it.key()[0] == '_') continue;
        const std::string ctx = "style \"" + it.key() + "\"";
        const json& e = it.value();
        check_keys(e, {"colour", "fill", "strands", "min_mark_m", "source"}, ctx, err);
        if (err.failed()) return;
        LineStyle ls;
        ls.id = it.key();
        ls.colour = str(e, "colour", "", ctx, err);
        ls.fill = flag(e, "fill", false, ctx, err);
        ls.min_mark = num(e, "min_mark_m", 0, ctx, err);
        ls.source = str(e, "source", "", ctx, err);
        if (ls.colour.empty()) err.set(ctx + ": missing \"colour\"");
        const json* sa = member(e, "strands");
        if (sa) {
            if (!sa->is_array() || sa->empty()) err.set(ctx + ": \"strands\" must be a non-empty list");
            else
                for (const json& s : *sa) {
                    check_keys(s, {"offset_m", "width_m", "mark_m", "gap_m"}, ctx + " strand", err);
                    if (err.failed()) return;
                    Strand sd;
                    sd.offset = num(s, "offset_m", 0, ctx, err);
                    sd.width = num(s, "width_m", 0, ctx, err);
                    sd.mark = num(s, "mark_m", 1, ctx, err);
                    sd.gap = num(s, "gap_m", 0, ctx, err);
                    if (!(sd.width > 0)) err.set(ctx + ": strand width_m must be > 0");
                    if (!(sd.mark > 0)) err.set(ctx + ": strand mark_m must be > 0");
                    if (sd.gap < 0) err.set(ctx + ": strand gap_m must be >= 0");
                    ls.strands.push_back(sd);
                }
        } else if (!ls.fill) {
            err.set(ctx + ": needs \"strands\" (or \"fill\": true)");
        }
        if (err.failed()) return;
        r.styles.emplace(ls.id, std::move(ls));
    }
}

void parse_sets(const json& root, double unit, MarkingRules& r, Err& err) {
    const json* ss = member(root, "sets");
    if (!ss || !ss->is_object()) {
        err.set("marking rules: missing \"sets\" object");
        return;
    }
    for (auto it = ss->begin(); it != ss->end(); ++it) {
        if (!it.key().empty() && it.key()[0] == '_') continue;
        const std::string sctx = "set \"" + it.key() + "\"";
        check_keys(it.value(), {"boundaries", "bays", "give_way_triangle"}, sctx, err);
        if (err.failed()) return;
        RuleSet rs;
        rs.id = it.key();
        if (const json* bs = member(it.value(), "boundaries")) {
            if (!bs->is_array()) err.set(sctx + ": \"boundaries\" must be a list");
            else
                for (const json& b : *bs) {
                    check_keys(b, {"id", "category", "left", "right", "relation", "style", "inset_m", "zones", "source"},
                               sctx + " boundary", err);
                    if (err.failed()) return;
                    BoundaryRule br;
                    br.id = str(b, "id", "", sctx, err);
                    const std::string ctx = sctx + " boundary \"" + br.id + "\"";
                    const std::string cat = str(b, "category", "", ctx, err);
                    if (!parse_category(cat, br.category)) err.set(ctx + ": unknown category \"" + cat + "\"");
                    br.lane_left = str(b, "left", "*", ctx, err);
                    br.lane_right = str(b, "right", "*", ctx, err);
                    const std::string rel = str(b, "relation", "any", ctx, err);
                    if (rel == "any") br.relation = BoundaryRule::Relation::Any;
                    else if (rel == "opposing") br.relation = BoundaryRule::Relation::Opposing;
                    else if (rel == "same") br.relation = BoundaryRule::Relation::Same;
                    else err.set(ctx + ": relation must be any, opposing or same");
                    br.style = parse_style_sel(member(b, "style"), unit, ctx, err);
                    br.inset_m = num(b, "inset_m", 0, ctx, err);
                    br.source = str(b, "source", "", ctx, err);
                    if (br.source.empty()) err.set(ctx + ": missing \"source\"");
                    if (const json* zs = member(b, "zones")) {
                        if (!zs->is_array()) err.set(ctx + ": \"zones\" must be a list");
                        else
                            for (const json& z : *zs) {
                                check_keys(z, {"kind", "distance_m", "min_marks", "min_curvature", "min_length_m", "style", "source"},
                                           ctx + " zone", err);
                                if (err.failed()) return;
                                ZoneRule zr;
                                const std::string kind = str(z, "kind", "", ctx, err);
                                if (kind == "approach") zr.kind = ZoneRule::Kind::Approach;
                                else if (kind == "curvature") zr.kind = ZoneRule::Kind::Curvature;
                                else err.set(ctx + ": zone kind must be approach or curvature");
                                zr.distance_m = parse_value_sel(member(z, "distance_m"), unit, ctx, err);
                                zr.min_marks = parse_value_sel(member(z, "min_marks"), unit, ctx, err);
                                zr.min_curvature = num(z, "min_curvature", 0, ctx, err);
                                zr.min_length_m = num(z, "min_length_m", 0, ctx, err);
                                zr.style = parse_style_sel(member(z, "style"), unit, ctx, err);
                                zr.source = str(z, "source", "", ctx, err);
                                if (zr.kind == ZoneRule::Kind::Curvature && !(zr.min_curvature > 0))
                                    err.set(ctx + ": a curvature zone needs min_curvature > 0");
                                br.zones.push_back(std::move(zr));
                            }
                    }
                    if (err.failed()) return;
                    rs.boundaries.push_back(std::move(br));
                }
        }
        if (const json* bs = member(it.value(), "bays")) {
            if (!bs->is_array()) err.set(sctx + ": \"bays\" must be a list");
            else
                for (const json& b : *bs) {
                    check_keys(b, {"lane_type", "length_m", "style", "keepout_m", "edge_clear_m", "source"}, sctx + " bay", err);
                    if (err.failed()) return;
                    BayRule br;
                    br.lane_type = str(b, "lane_type", "", sctx, err);
                    br.length_m = parse_value_sel(member(b, "length_m"), unit, sctx, err);
                    br.style = str(b, "style", "", sctx, err);
                    br.keepout_m = num(b, "keepout_m", 0, sctx, err);
                    br.edge_clear_m = num(b, "edge_clear_m", 0.06, sctx, err);
                    br.source = str(b, "source", "", sctx, err);
                    if (br.lane_type.empty() || br.style.empty() || br.length_m.empty())
                        err.set(sctx + ": a bay rule needs lane_type, length_m and style");
                    rs.bays.push_back(std::move(br));
                }
        }
        if (const json* gt = member(it.value(), "give_way_triangle")) {
            if (!gt->is_string()) err.set(sctx + ": give_way_triangle must be a string");
            else if (gt->get<std::string>() == "always") rs.give_way_triangle = TriangleMode::Always;
            else if (gt->get<std::string>() == "never") rs.give_way_triangle = TriangleMode::Never;
            else if (gt->get<std::string>() == "conditional") rs.give_way_triangle = TriangleMode::Conditional;
            else err.set(sctx + ": give_way_triangle must be always, never or conditional");
        }
        if (err.failed()) return;
        r.sets.emplace(rs.id, std::move(rs));
    }
}

void parse_junction(const json& root, double unit, MarkingRules& r, Err& err) {
    const json* jn = member(root, "junction");
    if (!jn) return;
    check_keys(*jn, {"give_way", "stop_line", "zebra", "roundabout_entry", "gore", "arrows", "mouth_edge", "splitter"}, "junction", err);
    if (err.failed()) return;
    if (const json* g = member(*jn, "give_way")) {
        check_keys(*g, {"style", "setback_m", "line_gap_m", "triangle", "equal_class", "equal_class_max_deviation_deg", "source"}, "give_way", err);
        {
            const std::string eq = str(*g, "equal_class", "straightest_pair_has_priority", "give_way", err);
            if (eq == "straightest_pair_has_priority") r.give_way.equal_class_straight_pair = true;
            else if (eq == "none") r.give_way.equal_class_straight_pair = false;
            else err.set("give_way: equal_class must be straightest_pair_has_priority or none");
            r.give_way.equal_class_max_deviation_deg = num(*g, "equal_class_max_deviation_deg", 30, "give_way", err);
        }
        r.give_way.enabled = true;
        r.give_way.style = parse_style_sel(member(*g, "style"), unit, "give_way", err);
        r.give_way.setback_m = num(*g, "setback_m", 0.5, "give_way", err);
        r.give_way.line_gap_m = num(*g, "line_gap_m", 0.3, "give_way", err);
        r.give_way.source = str(*g, "source", "", "give_way", err);
        if (const json* t = member(*g, "triangle")) {
            check_keys(*t, {"style", "mode", "major_speed_at_least", "major_class_at_least", "gap_from_line_m", "base_m", "height_m", "stroke_m"}, "give_way triangle", err);
            r.give_way.triangle = true;
            r.give_way.triangle_style = str(*t, "style", "", "give_way triangle", err);
            r.give_way.triangle_gap_m = num(*t, "gap_from_line_m", 1.0, "give_way triangle", err);
            r.give_way.triangle_base_m = num(*t, "base_m", 1.8, "give_way triangle", err);
            r.give_way.triangle_height_m = num(*t, "height_m", 3.0, "give_way triangle", err);
            r.give_way.triangle_stroke_m = num(*t, "stroke_m", 0.2, "give_way triangle", err);
            const std::string mode = str(*t, "mode", "always", "give_way triangle", err);
            if (mode == "always") r.give_way.triangle_mode = TriangleMode::Always;
            else if (mode == "never") r.give_way.triangle_mode = TriangleMode::Never;
            else if (mode == "conditional") r.give_way.triangle_mode = TriangleMode::Conditional;
            else err.set("give_way triangle: mode must be always, never or conditional");
            if (member(*t, "major_speed_at_least")) r.give_way.triangle_major_speed_min = num(*t, "major_speed_at_least", 0, "give_way triangle", err) * unit;
            if (member(*t, "major_class_at_least")) r.give_way.triangle_major_class_min = int_num(*t, "major_class_at_least", 0, 0, 100000, "give_way triangle", err);
            if (r.give_way.triangle_mode == TriangleMode::Conditional && !r.give_way.triangle_major_speed_min &&
                !r.give_way.triangle_major_class_min)
                err.set("give_way triangle: conditional needs major_speed_at_least or major_class_at_least");
        }
    }
    if (const json* g = member(*jn, "stop_line")) {
        check_keys(*g, {"style", "setback_m", "line_gap_m", "source"}, "stop_line", err);
        r.stop_line.enabled = true;
        r.stop_line.style = str(*g, "style", "", "stop_line", err);
        r.stop_line.setback_m = num(*g, "setback_m", 0.5, "stop_line", err);
        r.stop_line.line_gap_m = num(*g, "line_gap_m", 0.3, "stop_line", err);
        r.stop_line.source = str(*g, "source", "", "stop_line", err);
    }
    if (const json* g = member(*jn, "zebra")) {
        check_keys(*g, {"style", "stripe_width_m", "gap_m", "band_width_m", "clear_m", "give_way_style",
                        "give_way_gap_m", "min_start_gap_m", "source"}, "zebra", err);
        ZebraRule& z = r.zebra;
        z.enabled = true;
        z.style = str(*g, "style", "", "zebra", err);
        z.stripe_width_m = num(*g, "stripe_width_m", 0.5, "zebra", err);
        z.gap_m = num(*g, "gap_m", 0.6, "zebra", err);
        z.band_width_m = num(*g, "band_width_m", 3.0, "zebra", err);
        z.clear_m = num(*g, "clear_m", 1.0, "zebra", err);
        z.give_way_style = str(*g, "give_way_style", "", "zebra", err);
        z.give_way_gap_m = num(*g, "give_way_gap_m", 1.0, "zebra", err);
        z.min_start_gap_m = num(*g, "min_start_gap_m", 1.0, "zebra", err);
        z.source = str(*g, "source", "", "zebra", err);
        if (!(z.stripe_width_m > 0) || !(z.band_width_m > 0) || z.gap_m < 0) err.set("zebra: widths must be positive");
    }
    if (const json* g = member(*jn, "roundabout_entry")) {
        check_keys(*g, {"style", "setback_m", "line_gap_m", "source"}, "roundabout_entry", err);
        r.roundabout_entry.enabled = true;
        r.roundabout_entry.style = parse_style_sel(member(*g, "style"), unit, "roundabout_entry", err);
        r.roundabout_entry.setback_m = num(*g, "setback_m", 0.5, "roundabout_entry", err);
        r.roundabout_entry.line_gap_m = num(*g, "line_gap_m", 0.3, "roundabout_entry", err);
        r.roundabout_entry.source = str(*g, "source", "", "roundabout_entry", err);
    }
    if (const json* g = member(*jn, "gore")) {
        check_keys(*g, {"style", "stripe_width_m", "pitch_m", "angle_deg", "chevron", "same_direction", "opposing", "border_style", "min_area_m2", "edge_style", "edge_inset_m", "source"},
                   "gore", err);
        GoreRule& z = r.gore;
        z.enabled = true;
        z.style = str(*g, "style", "", "gore", err);
        z.stripe_width_m = num(*g, "stripe_width_m", 0.3, "gore", err);
        z.pitch_m = num(*g, "pitch_m", 1.2, "gore", err);
        z.angle_deg = num(*g, "angle_deg", 60, "gore", err);
        z.min_area_m2 = num(*g, "min_area_m2", 0.05, "gore", err);
        z.chevron.style = z.style;
        z.chevron.stripe_width_m = z.stripe_width_m;
        z.chevron.pitch_m = z.pitch_m;
        z.chevron.angle_deg = 35;
        if (const json* c = member(*g, "chevron")) {
            check_keys(*c, {"style", "stripe_width_m", "pitch_m", "angle_deg"}, "gore chevron", err);
            z.chevron.style = str(*c, "style", z.style, "gore chevron", err);
            z.chevron.stripe_width_m = num(*c, "stripe_width_m", z.stripe_width_m, "gore chevron", err);
            z.chevron.pitch_m = num(*c, "pitch_m", z.pitch_m, "gore chevron", err);
            z.chevron.angle_deg = num(*c, "angle_deg", 35, "gore chevron", err);
            if (!(z.chevron.stripe_width_m > 0) || !(z.chevron.pitch_m > z.chevron.stripe_width_m) ||
                !(z.chevron.angle_deg > 5 && z.chevron.angle_deg < 85))
                err.set("gore chevron: need 0 < stripe_width_m < pitch_m and 5 < angle_deg < 85");
        }
        z.same_direction = str(*g, "same_direction", "chevron", "gore", err);
        z.opposing = str(*g, "opposing", "diagonal", "gore", err);
        for (const std::string* m : {&z.same_direction, &z.opposing})
            if (*m != "chevron" && *m != "diagonal" && *m != "none") err.set("gore: patterns are chevron, diagonal or none");
        z.border_style = parse_style_sel(member(*g, "border_style"), unit, "gore", err);
        z.edge_style = parse_style_sel(member(*g, "edge_style"), unit, "gore", err);
        z.edge_inset_m = num(*g, "edge_inset_m", 0.225, "gore", err);
        z.source = str(*g, "source", "", "gore", err);
        if (!(z.stripe_width_m > 0) || !(z.pitch_m > z.stripe_width_m)) err.set("gore: need 0 < stripe_width_m < pitch_m");
    }
    if (const json* g = member(*jn, "mouth_edge")) {
        check_keys(*g, {"style", "max_deviation_deg", "source"}, "mouth_edge", err);
        r.mouth_edge.enabled = true;
        r.mouth_edge.style = parse_style_sel(member(*g, "style"), unit, "mouth_edge", err);
        r.mouth_edge.max_deviation_deg = num(*g, "max_deviation_deg", 30, "mouth_edge", err);
        r.mouth_edge.source = str(*g, "source", "", "mouth_edge", err);
    }
    if (const json* g = member(*jn, "splitter")) {
        check_keys(*g, {"outline_style", "outline_gap_m", "hatch_style", "hatch_length_m", "hatch_width_m", "stripe_width_m", "pitch_m",
                        "angle_deg", "ladder_pitch_m", "source"}, "splitter", err);
        SplitterRule& s = r.splitter;
        s.enabled = true;
        s.outline_style = parse_style_sel(member(*g, "outline_style"), unit, "splitter", err);
        s.outline_gap_m = num(*g, "outline_gap_m", 0.15, "splitter", err);
        s.hatch_style = str(*g, "hatch_style", "", "splitter", err);
        s.hatch_length_m = num(*g, "hatch_length_m", 12, "splitter", err);
        s.hatch_width_m = num(*g, "hatch_width_m", 1.4, "splitter", err);
        s.stripe_width_m = num(*g, "stripe_width_m", 0.3, "splitter", err);
        s.pitch_m = num(*g, "pitch_m", 1.0, "splitter", err);
        s.angle_deg = num(*g, "angle_deg", 60, "splitter", err);
        s.ladder_pitch_m = num(*g, "ladder_pitch_m", 2.0, "splitter", err);
        s.source = str(*g, "source", "", "splitter", err);
        if (!(s.ladder_pitch_m > s.stripe_width_m)) err.set("splitter: need ladder_pitch_m > stripe_width_m");
        if (!(s.pitch_m > s.stripe_width_m) || !(s.stripe_width_m > 0)) err.set("splitter: need 0 < stripe_width_m < pitch_m");
    }
    if (const json* g = member(*jn, "arrows")) {
        check_keys(*g, {"min_inbound_lanes", "distances_m", "length_m", "width_m", "style", "glyphs", "source"}, "arrows", err);
        ArrowRule& a = r.arrows;
        a.enabled = true;
        a.min_inbound_lanes = int_num(*g, "min_inbound_lanes", 2, 0, 64, "arrows", err);
        a.length_m = parse_value_sel(member(*g, "length_m"), unit, "arrows", err);
        a.width_m = num(*g, "width_m", 1.0, "arrows", err);
        a.style = str(*g, "style", "", "arrows", err);
        a.source = str(*g, "source", "", "arrows", err);
        if (const json* d = member(*g, "distances_m")) {
            if (!d->is_array()) err.set("arrows: distances_m must be a list");
            else
                for (const json& v : *d) {
                    if (!v.is_number()) err.set("arrows: distances_m entries must be numbers");
                    else a.distances_m.push_back(v.get<double>());
                }
        }
        const json* gl = member(*g, "glyphs");
        if (!gl || !gl->is_object()) err.set("arrows: missing \"glyphs\"");
        else {
            for (auto it = gl->begin(); it != gl->end() && !err.failed(); ++it) {
                if (!it.key().empty() && it.key()[0] == '_') continue;
                if (it.value().is_array()) {
                    Glyph gp;
                    gp.polygons = parse_polygons(it.value(), "glyph \"" + it.key() + "\"", err);
                    a.glyphs.emplace(it.key(), std::move(gp));
                }
            }
            for (auto it = gl->begin(); it != gl->end() && !err.failed(); ++it) {
                if (!it.value().is_object()) continue;
                const json* parts = member(it.value(), "parts");
                if (!parts || !parts->is_array()) {
                    err.set("glyph \"" + it.key() + "\": expected polygons or {\"parts\": [...]}");
                    break;
                }
                Glyph gp;
                for (const json& p : *parts) {
                    if (!p.is_string()) {
                        err.set("glyph \"" + it.key() + "\": parts are strings");
                        break;
                    }
                    std::string name = p.get<std::string>();
                    bool mirror = false;
                    const std::string suffix = ":mirror";
                    if (name.size() > suffix.size() && name.compare(name.size() - suffix.size(), suffix.size(), suffix) == 0) {
                        mirror = true;
                        name.resize(name.size() - suffix.size());
                    }
                    auto src = a.glyphs.find(name);
                    if (src == a.glyphs.end()) {
                        err.set("glyph \"" + it.key() + "\": unknown part \"" + name + "\"");
                        break;
                    }
                    auto polys = mirror ? mirrored(src->second.polygons) : src->second.polygons;
                    gp.polygons.insert(gp.polygons.end(), polys.begin(), polys.end());
                }
                if (!err.failed()) a.glyphs.emplace(it.key(), std::move(gp));
            }
            for (const char* need : {"ahead", "turn", "ahead_turn", "fork"})
                if (!err.failed() && !a.glyphs.count(need)) err.set(std::string("arrows: missing glyph \"") + need + "\"");
        }
    }
}

// Every style / colour reference must resolve.
void validate(const MarkingRules& r, Err& err) {
    auto need = [&](const std::string& style, const std::string& ctx) {
        if (!style.empty() && !r.styles.count(style)) err.set(ctx + ": unknown style \"" + style + "\"");
    };
    auto need_sel = [&](const StyleSel& sel, const std::string& ctx) {
        for (const StyleChoice& c : sel) need(c.style, ctx);
    };
    for (const auto& [id, st] : r.styles)
        if (!r.colours.count(st.colour)) err.set("style \"" + id + "\": unknown colour \"" + st.colour + "\"");
    for (const auto& [sid, rs] : r.sets) {
        for (const BoundaryRule& b : rs.boundaries) {
            const std::string ctx = "set \"" + sid + "\" boundary \"" + b.id + "\"";
            need_sel(b.style, ctx);
            for (const ZoneRule& z : b.zones) need_sel(z.style, ctx);
        }
        for (const BayRule& b : rs.bays) need(b.style, "set \"" + sid + "\" bay");
    }
    need_sel(r.give_way.style, "give_way");
    need(r.give_way.triangle_style, "give_way triangle");
    need(r.stop_line.style, "stop_line");
    need(r.zebra.style, "zebra");
    need(r.zebra.give_way_style, "zebra give_way_style");
    need_sel(r.roundabout_entry.style, "roundabout_entry");
    need(r.gore.style, "gore");
    need(r.gore.chevron.style, "gore chevron");
    need_sel(r.gore.border_style, "gore border_style");
    need_sel(r.gore.edge_style, "gore edge_style");
    need(r.arrows.style, "arrows");
    need_sel(r.mouth_edge.style, "mouth_edge");
    need_sel(r.splitter.outline_style, "splitter outline");
    need(r.splitter.hatch_style, "splitter hatch");
    if (r.give_way.enabled && r.give_way.triangle && r.give_way.triangle_style.empty()) err.set("give_way triangle: missing style");
    if (r.stop_line.enabled && r.stop_line.style.empty()) err.set("stop_line: missing style");
    if (r.zebra.enabled && r.zebra.style.empty()) err.set("zebra: missing style");
    if (r.gore.enabled && r.gore.style.empty()) err.set("gore: missing style");
    if (r.arrows.enabled && (r.arrows.style.empty() || r.arrows.length_m.empty())) err.set("arrows: need style and length_m");
}

}  // namespace

Loaded<MarkingRules> parse_marking_rules(const std::string& text) {
    Loaded<MarkingRules> out;
    Err err;
    json root = json::parse(text, nullptr, /*allow_exceptions=*/false);
    if (root.is_discarded() || !root.is_object()) {
        out.error = "marking rules: invalid JSON";
        return out;
    }
    MarkingRules r;
    check_keys(root, {"version", "speed_unit", "min_mark_m", "shoulder_lane_types", "colours", "styles", "sets", "junction", "wear"}, "marking rules", err);
    r.version = int_num(root, "version", 0, 0, 1000000, "marking rules", err);
    r.speed_unit = str(root, "speed_unit", "", "marking rules", err);
    if (r.speed_unit.empty()) err.set("marking rules: missing \"speed_unit\" (mph, kph or ms)");
    const double unit = unit_to_ms(r.speed_unit, err);
    r.min_mark_m = num(root, "min_mark_m", 0.3, "marking rules", err);
    if (const json* cs = member(root, "colours")) {
        if (!cs->is_object()) err.set("marking rules: \"colours\" must be an object");
        else
            for (auto it = cs->begin(); it != cs->end(); ++it) {
                if (!it.key().empty() && it.key()[0] == '_') continue;
                const json* rgb = member(it.value(), "rgb");
                MarkingColour c;
                if (!rgb || !rgb->is_array() || rgb->size() != 3) err.set("colour \"" + it.key() + "\": rgb must be [r, g, b]");
                else
                    for (std::size_t i = 0; i < 3; ++i) {
                        if (!(*rgb)[i].is_number()) err.set("colour \"" + it.key() + "\": rgb entries must be numbers");
                        else c.rgb[i] = (*rgb)[i].get<double>();
                    }
                r.colours.emplace(it.key(), c);
            }
    } else {
        err.set("marking rules: missing \"colours\"");
    }
    if (const json* w = member(root, "wear")) {
        check_keys(*w, {"strength", "patch_period_m", "patch_weight", "wheel_boost", "wheel_offset_m", "wheel_half_width_m", "source"}, "wear", err);
        WearRule& wr = r.wear;
        wr.strength = num(*w, "strength", 0.08, "wear", err);
        wr.patch_period_m = num(*w, "patch_period_m", 60, "wear", err);
        wr.patch_weight = num(*w, "patch_weight", 0.5, "wear", err);
        wr.wheel_boost = num(*w, "wheel_boost", 0.14, "wear", err);
        wr.wheel_offset_m = num(*w, "wheel_offset_m", 0.8, "wear", err);
        wr.wheel_half_width_m = num(*w, "wheel_half_width_m", 0.25, "wear", err);
        wr.source = str(*w, "source", "", "wear", err);
        if (!(wr.patch_period_m > 0) || wr.strength < 0 || wr.strength > 1) err.set("wear: need patch_period_m > 0 and 0 <= strength <= 1");
    }
    if (const json* sh = member(root, "shoulder_lane_types")) {
        if (!sh->is_array()) err.set("marking rules: shoulder_lane_types must be a list");
        else
            for (const json& v : *sh) {
                if (!v.is_string()) err.set("marking rules: shoulder_lane_types entries must be strings");
                else r.shoulder_lane_types.push_back(v.get<std::string>());
            }
    }
    if (!err.failed()) parse_styles(root, r, err);
    if (!err.failed()) parse_sets(root, unit, r, err);
    if (!err.failed()) parse_junction(root, unit, r, err);
    if (!err.failed()) validate(r, err);
    if (err.failed()) {
        out.error = err.msg;
        return out;
    }
    out.value = std::move(r);
    return out;
}

Loaded<MarkingRules> load_marking_rules(const std::filesystem::path& file) {
    std::ifstream in(file, std::ios::binary);
    if (!in) {
        Loaded<MarkingRules> out;
        out.error = "cannot open " + file.string();
        return out;
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    Loaded<MarkingRules> out = parse_marking_rules(ss.str());
    if (!out) out.error = file.string() + ": " + out.error;
    return out;
}

Loaded<MarkingRules> load_marking_rules_by_id(const std::filesystem::path& data_dir, const std::string& region_id) {
    return load_marking_rules(data_dir / "regions" / region_id / "marking_rules.json");
}

// ================================================================ geometry helpers

namespace {

Vec2 normalized(Vec2 v) {
    const double l = v.length();
    return l > 1e-12 ? Vec2{v.x / l, v.y / l} : Vec2{0, 0};
}

Vec2 left_normal(Vec2 d) { return {-d.y, d.x}; }

bool finite3(const Vec3& p) { return std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z); }

// Left normals and mitre scales of an open polyline (consecutive duplicates must be removed).
void polyline_normals(const std::vector<Vec3>& p, std::vector<Vec2>& normal, std::vector<double>& miter) {
    const std::size_t n = p.size();
    normal.assign(n, {0, 0});
    miter.assign(n, 1.0);
    std::vector<Vec2> seg(n > 0 ? n - 1 : 0);
    for (std::size_t i = 0; i + 1 < n; ++i) seg[i] = left_normal(normalized(p[i + 1].xy() - p[i].xy()));
    for (std::size_t i = 0; i < n; ++i) {
        if (i == 0) normal[i] = seg[0];
        else if (i + 1 == n) normal[i] = seg[n - 2];
        else {
            Vec2 m = normalized(seg[i - 1] + seg[i]);
            if (m.length() < 0.5) m = seg[i];
            normal[i] = m;
            miter[i] = std::min(1.0 / std::max(m.dot(seg[i]), 0.5), 2.0);
        }
    }
}

std::vector<Vec3> dedupe(const std::vector<Vec3>& in) {
    std::vector<Vec3> out;
    for (const Vec3& p : in)
        if (out.empty() || (p.xy() - out.back().xy()).length() > 1e-6) out.push_back(p);
    return out;
}

// Shifts an open polyline sideways (+ = left of travel).
std::vector<Vec3> offset_points(const std::vector<Vec3>& in, double shift) {
    std::vector<Vec3> p = dedupe(in);
    if (p.size() < 2) return p;
    std::vector<Vec2> n;
    std::vector<double> m;
    polyline_normals(p, n, m);
    for (std::size_t i = 0; i < p.size(); ++i) {
        p[i].x += n[i].x * shift * m[i];
        p[i].y += n[i].y * shift * m[i];
    }
    return p;
}

// Stroke strip ring around a centreline: right side forward, then left side backward (CCW).
bool stroke_ring(const std::vector<Vec3>& in, double width, std::vector<Vec3>& ring,
                 const std::function<double(std::size_t, double)>* zf = nullptr) {
    const std::vector<Vec3> c = dedupe(in);
    ring.clear();
    if (c.size() < 2) return false;
    std::vector<Vec2> n;
    std::vector<double> m;
    polyline_normals(c, n, m);
    const double hw = width / 2;
    for (std::size_t i = 0; i < c.size(); ++i)
        ring.push_back({c[i].x - n[i].x * hw * m[i], c[i].y - n[i].y * hw * m[i], zf ? (*zf)(i, -1.0) : c[i].z});
    for (std::size_t i = c.size(); i-- > 0;)
        ring.push_back({c[i].x + n[i].x * hw * m[i], c[i].y + n[i].y * hw * m[i], zf ? (*zf)(i, 1.0) : c[i].z});
    return true;
}

// Deterministic hashing for the wear value (mix64/hash01 from cp/geom.h; no std distributions: identical on every library).

// Smooth 2D value noise in [0, 1] with unit lattice spacing.
double value_noise(double x, double y, std::uint64_t seed) {
    const double fx = std::floor(x), fy = std::floor(y);
    const double tx = x - fx, ty = y - fy;
    const double sx = tx * tx * (3 - 2 * tx), sy = ty * ty * (3 - 2 * ty);
    auto corner = [&](double cx, double cy) {
        return hash01(seed ^ mix64(static_cast<std::uint64_t>(static_cast<std::int64_t>(cx)) * 0x9E3779B1ull) ^
                      mix64(static_cast<std::uint64_t>(static_cast<std::int64_t>(cy)) + 0x7F4A7C15ull));
    };
    const double a = corner(fx, fy), b = corner(fx + 1, fy), c = corner(fx, fy + 1), d = corner(fx + 1, fy + 1);
    return (a + (b - a) * sx) * (1 - sy) + (c + (d - c) * sx) * sy;
}

// Collects marks into a MarkingSet, interning ids and dropping degenerate polygons.
struct Builder {
    MarkingSet& out;
    const MarkingRules& rules;
    const MarkingParams& params;
    EdgeId edge;
    NodeId node;

    // wheel: 0..1, how much of the mark lies in the wheel tracks (transverse lines, arrows).
    void add(MarkingCategory cat, const std::string& style, std::vector<Vec3>& ring, bool strip, int lane = -1,
             double wheel = 0) {
        if (ring.size() < 3) return;
        for (const Vec3& p : ring)
            if (!finite3(p)) return;
        double a = ring_area(ring.data(), ring.size());
        if (std::fabs(a) < kMinArea) return;
        if (a < 0) std::reverse(ring.begin(), ring.end());
        auto it = rules.styles.find(style);
        if (it == rules.styles.end()) return;
        Marking m;
        m.category = cat;
        m.style = intern(out.styles, style);
        m.colour = intern(out.colours, it->second.colour);
        m.first = static_cast<std::uint32_t>(out.vertices.size());
        m.count = static_cast<std::uint32_t>(ring.size());
        m.strip = strip;
        m.edge = edge;
        m.node = node;
        m.lane = lane;
        // Wear: per-mark noise + low-frequency patchiness, both from the position and the ids only.
        const WearRule& w = rules.wear;
        double cx = 0, cy = 0;
        for (const Vec3& p : ring) cx += p.x, cy += p.y;
        cx /= static_cast<double>(ring.size());
        cy /= static_cast<double>(ring.size());
        const std::uint64_t seed = mix64(edge.value * 0x9E3779B97F4A7C15ull + node.value * 0xC2B2AE3D27D4EB4Full + 17);
        const std::uint64_t qx = static_cast<std::uint64_t>(static_cast<std::int64_t>(std::llround(cx * 100)));
        const std::uint64_t qy = static_cast<std::uint64_t>(static_cast<std::int64_t>(std::llround(cy * 100)));
        const double per_mark = hash01(seed ^ mix64(qx) ^ mix64(qy * 31 + 7));
        const double patch = value_noise(cx / w.patch_period_m, cy / w.patch_period_m, 0x51ED270Bull);
        double wear = w.strength * 2 * ((1 - w.patch_weight) * per_mark + w.patch_weight * patch);
        wear += w.wheel_boost * std::clamp(wheel, 0.0, 1.0);
        wear *= params.wear_scale;
        m.wear = static_cast<float>(std::clamp(wear, 0.0, 1.0));
        out.vertices.insert(out.vertices.end(), ring.begin(), ring.end());
        out.marks.push_back(m);
    }
};

// ---- offset polylines of an edge, addressed by plan length

struct LinePoly {
    std::vector<double> s, cum;
    std::vector<Vec2> xy;
    std::vector<FoldRange> folds;
    double lateral = 0;  // offset of this line from the edge centreline (+ left of a -> b)
    bool valid() const { return xy.size() >= 2; }
    double total() const { return cum.empty() ? 0 : cum.back(); }

    double len_of_s(double sv) const {
        if (s.empty()) return 0;
        if (sv <= s.front()) return 0;
        if (sv >= s.back()) return cum.back();
        const std::size_t i = static_cast<std::size_t>(std::upper_bound(s.begin(), s.end(), sv) - s.begin()) - 1;
        const double ds = s[i + 1] - s[i];
        const double t = ds > kEps ? (sv - s[i]) / ds : 0;
        return cum[i] + t * (cum[i + 1] - cum[i]);
    }
    double s_of_len(double l) const {
        if (cum.empty()) return 0;
        if (l <= 0) return s.front();
        if (l >= cum.back()) return s.back();
        const std::size_t i = static_cast<std::size_t>(std::upper_bound(cum.begin(), cum.end(), l) - cum.begin()) - 1;
        const double dl = cum[i + 1] - cum[i];
        const double t = dl > kEps ? (l - cum[i]) / dl : 0;
        return s[i] + t * (s[i + 1] - s[i]);
    }
    Vec2 xy_at_len(double l) const {
        if (xy.empty()) return {};
        if (l <= 0) return xy.front();
        if (l >= cum.back()) return xy.back();
        const std::size_t i = static_cast<std::size_t>(std::upper_bound(cum.begin(), cum.end(), l) - cum.begin()) - 1;
        const double dl = cum[i + 1] - cum[i];
        const double t = dl > kEps ? (l - cum[i]) / dl : 0;
        return xy[i] + (xy[i + 1] - xy[i]) * t;
    }
};

LinePoly make_line_poly(const Edge& e, double lateral, const SamplingParams& sp) {
    LinePoly lp;
    const OffsetPolyline op = sample_offset(e, lateral, sp);
    lp.folds = op.folds;
    lp.lateral = lateral;
    double acc = 0;
    for (const EdgeSample& smp : op.samples) {
        const Vec2 p = smp.pos.xy();
        if (!lp.xy.empty()) {
            const double d = (p - lp.xy.back()).length();
            if (d < 1e-9) continue;
            acc += d;
        }
        lp.s.push_back(smp.s);
        lp.xy.push_back(p);
        lp.cum.push_back(acc);
    }
    return lp;
}

// Points of the offset line between plan lengths [l0, l1], z from the vertical profile at the true
// centreline arc length; extra points keep the vertical chord error under `tol`.
// Params whose surface_height follows the render mesh's cross-section (crossfall, kerb upstand) of the given
// edges unless the caller supplied its own callback (which always wins).
MarkingParams with_mesh_surface(const MarkingParams& p, const RegionPack& region, const RoadGraph& g, const std::vector<EdgeId>& edges) {
    MarkingParams eff = p;
    if (p.surface_height) return eff;
    auto profiles = std::make_shared<std::map<EdgeId, SurfaceProfile>>();
    for (EdgeId id : edges) {
        const Edge* e = g.edge(id);
        if (!e) continue;
        SurfaceProfile sp = make_surface_profile(region, *e);
        if (sp.valid()) profiles->emplace(id, sp);
    }
    eff.surface_height = [profiles](EdgeId id, double, double lateral, double centre_z) {
        auto it = profiles->find(id);
        return it == profiles->end() ? centre_z : centre_z + it->second.offset(lateral);
    };
    return eff;
}

// Road surface z of an edge at (s, lateral): the callback when supplied, else the centreline profile.
double edge_surface_z(const MarkingParams& p, const Edge& e, double s, double lateral) {
    const double z = e.vertical.z(s);
    return p.surface_height ? p.surface_height(e.id, s, lateral, z) : z;
}

std::vector<Vec3> slice_points(const Edge& e, const LinePoly& lp, double l0, double l1, const MarkingParams& params,
                               std::vector<double>* s_out = nullptr) {
    const double tol = params.chord_tolerance;
    std::vector<double> ls{l0};
    for (double c : lp.cum)
        if (c > l0 + 1e-6 && c < l1 - 1e-6) ls.push_back(c);
    ls.push_back(l1);
    auto z_at = [&](double l) { return edge_surface_z(params, e, lp.s_of_len(l), lp.lateral); };
    int budget = 4096;
    for (std::size_t i = 0; i + 1 < ls.size();) {
        const double a = ls[i], b = ls[i + 1];
        if (b - a > 0.2 && budget > 0) {
            const double mid = (a + b) / 2;
            const double err = std::fabs(z_at(mid) - (z_at(a) + z_at(b)) / 2);
            if (err > tol) {
                ls.insert(ls.begin() + static_cast<std::ptrdiff_t>(i) + 1, mid);
                --budget;
                continue;
            }
        }
        ++i;
    }
    std::vector<Vec3> pts;
    for (double l : ls) {
        const Vec2 p = lp.xy_at_len(l);
        pts.push_back({p.x, p.y, z_at(l)});
        if (s_out) s_out->push_back(lp.s_of_len(l));
    }
    return pts;
}

struct Interval {
    double a = 0, b = 0;
};


double strand_depth(const LineStyle& st, double& env_min) {
    double lo = 1e9, hi = -1e9;
    for (const Strand& s : st.strands) {
        lo = std::min(lo, s.offset - s.width / 2);
        hi = std::max(hi, s.offset + s.width / 2);
    }
    if (st.strands.empty()) return 0;
    env_min = lo;
    return hi - lo;
}

// ---- contexts

MarkCtx make_ctx(const Edge& e, const RoadType& rt, const std::vector<Lane>& lanes, const MarkingRules& rules) {
    MarkCtx c;
    c.speed = rt.speed_limit;
    for (const Lane& l : lanes) {
        if (l.dir != TravelDir::None) {
            c.width += l.width;
            ++c.lanes;
        } else if (std::find(rules.shoulder_lane_types.begin(), rules.shoulder_lane_types.end(), l.lane_type) !=
                   rules.shoulder_lane_types.end()) {
            c.shoulder = true;
        }
    }
    c.one_way = e.one_way || rt.dual || !rt.two_way;
    return c;
}

const LineStyle* style_of(const MarkingRules& r, const std::string& id) {
    auto it = r.styles.find(id);
    return it == r.styles.end() ? nullptr : &it->second;
}

bool is_ring_arm(const RoadGraph& g, const RegionPack& region, const JunctionArm& arm) {
    const Edge* e = g.edge(arm.edge);
    const RoadType* rt = e ? region.road_type(e->road_type) : nullptr;
    return rt && rt->roundabout_ring;
}

double trim_of(const Junction& j, int arm) {
    return j.trims.size() == j.arms.size() ? j.trims[static_cast<std::size_t>(arm)] : 0.0;
}

// Arms that have priority at a junction: the highest road class (width breaks ties) from collect_arms'
// major_arms; when every arm ties, the straightest pair is the through route (rules.give_way), unless
// it is too bent to count (a symmetric Y stays unmarked) -- then all arms keep priority.
std::vector<int> priority_arms(const Junction& j, const MarkingRules& rules) {
    std::vector<int> prio = j.major_arms;
    const std::size_t n = j.arms.size();
    if (prio.size() < n || n < 3 || !rules.give_way.equal_class_straight_pair) return prio;
    double best = 1e9;
    int bi = -1, bk = -1;
    for (std::size_t i = 0; i < n; ++i)
        for (std::size_t k = i + 1; k < n; ++k) {
            const double sep = std::fabs(wrap_angle(j.arms[i].heading_out - j.arms[k].heading_out));
            const double dev = (kPi - sep) * 180 / kPi;
            // Ties (a perfect cross) go to the pair with the smallest edge id: deterministic.
            const bool tie = std::fabs(dev - best) < 1.0;
            const std::uint64_t id_new = std::min(j.arms[i].edge.value, j.arms[k].edge.value);
            const std::uint64_t id_old = bi < 0 ? 0 : std::min(j.arms[static_cast<std::size_t>(bi)].edge.value, j.arms[static_cast<std::size_t>(bk)].edge.value);
            if (dev < best - 1.0 || (tie && id_new < id_old)) {
                best = std::min(best, dev);
                bi = static_cast<int>(i);
                bk = static_cast<int>(k);
            }
        }
    if (bi < 0 || best > rules.give_way.equal_class_max_deviation_deg) return prio;
    return {bi, bk};
}

// ---- per-arm transverse marking layout, shared by the edge and the junction generators

struct ArmLayout {
    bool give_way = false, stop = false, entry = false;
    const std::string* style = nullptr;  // line style id (give way / stop / entry)
    double line_s0 = 0, line_s1 = 0;     // distance from the node: near and far edge of the line
    bool zebra = false;
    double zs0 = 0, zs1 = 0;            // stripe band, distance from the node
    double zline_far = 0, zline_near = -1;  // zebra give way lines: near edge distance (<0: none)
    double clear = 0;                    // longitudinal lines stop this far from the node
    bool triangle = false;               // paint a give-way triangle in each inbound lane
    MarkCtx ctx;
};

std::vector<ArmLayout> compute_arm_layout(const RoadGraph& g, const RegionPack& region, const MarkingRules& rules,
                                          const Junction& j, const MarkingParams& params) {
    std::vector<ArmLayout> out(j.arms.size());
    const bool priority_class = j.cls == NodeClass::Junction && j.arms.size() >= 3;
    const std::vector<int> prio = priority_arms(j, rules);
    double major_speed = 0;
    int major_class = 0;
    for (int pi : prio) {
        const Edge* me = g.edge(j.arms[static_cast<std::size_t>(pi)].edge);
        const RoadType* mt = me ? region.road_type(me->road_type) : nullptr;
        if (mt) major_speed = std::max(major_speed, mt->speed_limit), major_class = std::max(major_class, mt->road_class);
    }
    std::set<int> gw_entries;
    if (params.use_roundabout_give_way)
        for (const GiveWay& gw : params.roundabout_give_way)
            if (gw.node == j.node && gw.connector >= 0 && static_cast<std::size_t>(gw.connector) < j.connectors.size())
                gw_entries.insert(j.connectors[static_cast<std::size_t>(gw.connector)].arm_from);
    for (std::size_t i = 0; i < j.arms.size(); ++i) {
        ArmLayout& L = out[i];
        const JunctionArm& arm = j.arms[i];
        const Edge* e = g.edge(arm.edge);
        const RoadType* rt = e ? region.road_type(e->road_type) : nullptr;
        L.clear = trim_of(j, static_cast<int>(i));
        if (!rt || !priority_class) continue;
        L.ctx = make_ctx(*e, *rt, instantiate_lanes(*e, *rt, region.driving_side), rules);
        const bool ring = rt->roundabout_ring;
        const double trim = L.clear;
        if (arm.inbound_count() > 0 && !ring) {
            if (j.kind == JunctionKind::Roundabout) {
                if (rules.roundabout_entry.enabled && (!params.use_roundabout_give_way || gw_entries.count(static_cast<int>(i)))) {
                    L.style = select_style(rules.roundabout_entry.style, L.ctx);
                    if (L.style) {
                        L.entry = true;
                        // The line crosses the entry at the ring's outer kerb, not at the trim line: a shaped
                        // (splitter) approach is trimmed tens of metres out, where its flare ends.
                        double ring_ext = 0;
                        for (const JunctionArm& ra : j.arms) {
                            const Edge* re = g.edge(ra.edge);
                            const RoadType* rrt = re ? region.road_type(re->road_type) : nullptr;
                            if (rrt && rrt->roundabout_ring) ring_ext = std::max({ring_ext, ra.kerb_left, ra.kerb_right});
                        }
                        L.line_s0 = std::min(trim, ring_ext) + rules.roundabout_entry.setback_m;
                    }
                }
            } else if (params.stop_arms.count({j.node, arm.edge}) && rules.stop_line.enabled) {
                L.stop = true;
                L.style = &rules.stop_line.style;
                L.line_s0 = trim + rules.stop_line.setback_m;
            } else if (rules.give_way.enabled && prio.size() < j.arms.size() &&
                       std::find(prio.begin(), prio.end(), static_cast<int>(i)) == prio.end()) {
                L.style = select_style(rules.give_way.style, L.ctx);
                if (L.style) {
                    L.give_way = true;
                    TriangleMode mode = rules.give_way.triangle_mode;
                    if (const RuleSet* rs = rules.set(rt->markings); rs && rs->give_way_triangle) mode = *rs->give_way_triangle;
                    const GiveWayRule& gw = rules.give_way;
                    L.triangle = gw.triangle && (mode == TriangleMode::Always ||
                                 (mode == TriangleMode::Conditional &&
                                  ((gw.triangle_major_speed_min && major_speed >= *gw.triangle_major_speed_min - 1e-6) ||
                                   (gw.triangle_major_class_min && major_class >= *gw.triangle_major_class_min))));
                    L.line_s0 = trim + rules.give_way.setback_m;
                }
            }
        }
        double line_gap = 0;
        if (L.style) {
            const LineStyle* st = style_of(rules, *L.style);
            double envmin = 0;
            L.line_s1 = L.line_s0 + (st ? strand_depth(*st, envmin) : 0.2);
            line_gap = L.stop ? rules.stop_line.line_gap_m : L.entry ? rules.roundabout_entry.line_gap_m : rules.give_way.line_gap_m;
            L.clear = std::max(L.clear, L.line_s1 + line_gap);
        }
        if (rules.zebra.enabled && !ring && params.crossing_arms.count({j.node, arm.edge})) {
            const CrossingSpot* first = nullptr;
            for (const CrossingSpot& cs : j.crossings)
                if (cs.arm == static_cast<int>(i)) {
                    first = &cs;
                    break;
                }
            if (first) {
                const ZebraRule& z = rules.zebra;
                double s0 = first->s - z.band_width_m / 2;
                if (L.style) s0 = std::max(s0, L.line_s1 + z.min_start_gap_m);
                s0 = std::max(s0, trim);
                L.zebra = true;
                L.zs0 = s0;
                L.zs1 = s0 + z.band_width_m;
                double edge_far = L.zs1;
                if (!z.give_way_style.empty()) {
                    const LineStyle* st = style_of(rules, z.give_way_style);
                    double envmin = 0;
                    const double depth = st ? strand_depth(*st, envmin) : 0.2;
                    L.zline_far = L.zs1 + z.give_way_gap_m;  // near edge of the far-side line
                    edge_far = L.zline_far + depth;
                    const double near_edge = L.zs0 - z.give_way_gap_m - depth;
                    if (near_edge >= (L.style ? L.line_s1 + z.min_start_gap_m : trim) + 0.2) L.zline_near = near_edge;
                }
                L.clear = std::max(L.clear, edge_far + z.clear_m);
            }
        }
    }
    return out;
}

// ---- transverse mark emission in an arm's frame (s along heading_out, lateral + = left)

using SL = std::pair<double, double>;  // (s, lateral)

// Road surface z at an arm point: the callback (edge-frame s and lateral) when supplied, else the centreline z.
double arm_surface_z(const MarkingParams& p, const JunctionArm& arm, double s, double lat, double centre_z) {
    if (!p.surface_height) return centre_z;
    const double es = arm.at_a ? s : arm.edge_length - s;
    const double el = arm.at_a ? lat : -lat;
    return p.surface_height(arm.edge, es, el, centre_z);
}

std::vector<Vec3> arm_ring(const Builder& b, const JunctionArm& arm, const std::vector<SL>& pts) {
    std::vector<Vec3> r;
    for (const SL& p : pts) {
        Vec3 q = arm_point(arm, p.first, p.second).pos;
        q.z = arm_surface_z(b.params, arm, p.first, p.second, q.z);
        r.push_back(q);
    }
    return r;
}

// How much of lateral range [l0, l1] lies in the wheel tracks of the arm's inbound lanes (0 or 1).
double wheel_overlap(const MarkingRules& rules, const JunctionArm& arm, double l0, double l1) {
    const WearRule& w = rules.wear;
    for (const LaneRef& l : arm.lanes) {
        if (!l.inbound) continue;
        for (double sgn : {-1.0, 1.0}) {
            const double c = l.lateral + sgn * w.wheel_offset_m;
            if (l1 >= c - w.wheel_half_width_m && l0 <= c + w.wheel_half_width_m) return 1.0;
        }
    }
    return 0.0;
}

void emit_arm_quad(Builder& b, MarkingCategory cat, const std::string& style, const JunctionArm& arm, double s0, double s1,
                   double l0, double l1) {
    std::vector<Vec3> r = arm_ring(b, arm, {{s0, l0}, {s1, l0}, {s1, l1}, {s0, l1}});
    b.add(cat, style, r, false, -1, wheel_overlap(b.rules, arm, l0, l1));
}

// A transverse line across lateral [lo, hi] whose near edge (closest to the node) is at s_near.
void emit_transverse(Builder& b, const MarkingRules& rules, MarkingCategory cat, const std::string& style_id,
                     const JunctionArm& arm, double s_near, double lo, double hi) {
    const LineStyle* st = style_of(rules, style_id);
    if (!st || hi - lo < 1e-3) return;
    double envmin = 0;
    strand_depth(*st, envmin);
    const double min_mark = st->min_mark > 0 ? st->min_mark : rules.min_mark_m;
    for (const Strand& sd : st->strands) {
        const double c = s_near - envmin + sd.offset;
        for (const DashSpan& d : layout_flush(hi - lo, sd.mark, sd.gap, min_mark))
            emit_arm_quad(b, cat, style_id, arm, c - sd.width / 2, c + sd.width / 2, lo + d.s0, lo + d.s1);
    }
}

bool lane_extent(const JunctionArm& arm, bool inbound, double& lo, double& hi) {
    bool any = false;
    lo = 1e9;
    hi = -1e9;
    for (const LaneRef& l : arm.lanes)
        if (l.inbound == inbound) {
            lo = std::min(lo, l.lateral - l.width / 2);
            hi = std::max(hi, l.lateral + l.width / 2);
            any = true;
        }
    return any;
}

}  // namespace

// ================================================================ edge markings

namespace {

struct EndRef {
    const Junction* j = nullptr;
    int arm = -1;
};

EndRef find_end(const JunctionSet& js, const Edge& e, bool at_a) {
    EndRef r;
    const Junction* j = js.find(at_a ? e.a : e.b);
    if (!j) return r;
    for (std::size_t i = 0; i < j->arms.size(); ++i)
        if (j->arms[i].edge == e.id && j->arms[i].at_a == at_a) {
            r.j = j;
            r.arm = static_cast<int>(i);
            return r;
        }
    return r;
}

bool type_matches(const std::string& spec, const std::string& type) {
    const std::string t = type.empty() ? "outside" : type;
    std::size_t pos = 0;
    while (pos <= spec.size()) {
        std::size_t bar = spec.find('|', pos);
        if (bar == std::string::npos) bar = spec.size();
        const std::string part = spec.substr(pos, bar - pos);
        if (part == t || (part == "*" && !type.empty())) return true;
        pos = bar + 1;
    }
    return false;
}

struct StyleSeg {
    double a, b;  // chain coordinate S (centreline length along the chain's usable range)
    const std::string* style;
};

struct BoundaryLine {
    int k = 0;  // boundary index (0 = left of the leftmost lane)
    const BoundaryRule* rule = nullptr;
    double lateral = 0;  // line centre in the edge frame (inset applied)
};

const BoundaryRule* match_boundary(const RuleSet& rs, const Lane* l, const Lane* r) {
    const std::string lt = l ? l->lane_type : "", rtn = r ? r->lane_type : "";
    BoundaryRule::Relation rel = BoundaryRule::Relation::Any;
    bool related = false;
    if (l && r && l->dir != TravelDir::None && r->dir != TravelDir::None) {
        related = true;
        rel = l->dir == r->dir ? BoundaryRule::Relation::Same : BoundaryRule::Relation::Opposing;
    }
    for (const BoundaryRule& br : rs.boundaries) {
        if (br.relation != BoundaryRule::Relation::Any && (!related || br.relation != rel)) continue;
        const bool fwd = type_matches(br.lane_left, lt) && type_matches(br.lane_right, rtn);
        const bool rev = type_matches(br.lane_left, rtn) && type_matches(br.lane_right, lt);
        if (fwd || rev) return &br;
    }
    return nullptr;
}

// Every lane boundary that has a marking rule, with the line's lateral position.
std::vector<BoundaryLine> edge_boundaries(const std::vector<Lane>& lanes, const RuleSet& rs) {
    std::vector<BoundaryLine> out;
    const int n = static_cast<int>(lanes.size());
    for (int k = 0; k <= n; ++k) {
        const Lane* left = k > 0 ? &lanes[static_cast<std::size_t>(k - 1)] : nullptr;
        const Lane* right = k < n ? &lanes[static_cast<std::size_t>(k)] : nullptr;
        const BoundaryRule* br = match_boundary(rs, left, right);
        if (!br) continue;
        double lat = right ? right->offset + right->width / 2 : left->offset - left->width / 2;
        const bool left_drives = left && left->dir != TravelDir::None, right_drives = right && right->dir != TravelDir::None;
        if (left_drives && !right_drives) lat += br->inset_m;
        else if (right_drives && !left_drives) lat -= br->inset_m;
        out.push_back({k, br, lat});
    }
    return out;
}

// What one end of an edge asks of its longitudinal lines.
struct EndInfo {
    double clear = 0;        // lines stop this far from the node
    bool junction = false;   // a real junction (3+ arms): bay keep-out applies
    bool approach = false;   // warning-line zone applies
};

EndInfo end_info(const RoadGraph& graph, const RegionPack& region, const MarkingRules& rules, const JunctionSet& js,
                 const Edge& e, bool at_a, const RoadType& rt, const MarkingParams& params) {
    EndInfo info;
    const EndRef ref = find_end(js, e, at_a);
    if (!ref.j) return info;
    const std::vector<ArmLayout> lay = compute_arm_layout(graph, region, rules, *ref.j, params);
    info.clear = lay.empty() ? 0 : lay[static_cast<std::size_t>(ref.arm)].clear;
    info.clear = std::max(info.clear, trim_of(*ref.j, ref.arm));
    info.junction = ref.j->arms.size() >= 3;
    info.approach = ref.j->cls == NodeClass::Junction && info.junction && !rt.roundabout_ring;
    return info;
}

// ---- chains: edges joined by plain 2-edge nodes that continue the same boundary lines, so dash phase
// carries across the joint (and closes evenly round a ring)

struct ChainEl {
    const Edge* e = nullptr;
    bool flip = false;  // chain direction runs b -> a
    double lo = 0, hi = 0;  // usable s range in the edge's own coordinates
    double S = 0;           // chain coordinate of the element's start
    double len() const { return hi - lo; }
};

struct Chain {
    std::vector<ChainEl> els;
    std::size_t self = 0;
    bool closed = false;
    bool approach_start = false, approach_end = false;
    double total = 0;
};

bool directional(const Edge& e, const RoadType& rt) { return e.one_way || rt.dual || !rt.two_way; }

const Edge* chain_neighbour(const RoadGraph& g, const Edge& cur, NodeId node) {
    if (cur.a == cur.b) return nullptr;
    const std::vector<EdgeId> es = g.edges_at(node);
    if (es.size() != 2) return nullptr;
    const EdgeId other = es[0] == cur.id ? es[1] : es[0];
    const Edge* o = g.edge(other);
    if (!o || o->a == o->b || o->id == cur.id) return nullptr;
    if (o->road_type != cur.road_type || o->one_way != cur.one_way) return nullptr;
    return o;
}

Chain build_chain(const RoadGraph& graph, const RegionPack& region, const MarkingRules& rules, const JunctionSet& js,
                  const Edge& self, const RoadType& rt, const MarkingParams& params) {
    Chain c;
    const bool dir = directional(self, rt);
    c.els.push_back({&self, false, 0, 0, 0});
    // Forward from self.b.
    NodeId node = self.b;
    const Edge* cur = &self;
    bool curflip = false;
    for (int guard = 0; guard < 100000; ++guard) {
        const Edge* o = chain_neighbour(graph, *cur, node);
        if (!o) break;
        if (o->id == self.id) {
            c.closed = true;
            break;
        }
        const bool f = o->a != node;
        if (dir && f != curflip) break;
        c.els.push_back({o, f, 0, 0, 0});
        node = f ? o->a : o->b;
        cur = o;
        curflip = f;
    }
    if (!c.closed) {
        node = self.a;
        cur = &self;
        curflip = false;
        for (int guard = 0; guard < 100000; ++guard) {
            const Edge* o = chain_neighbour(graph, *cur, node);
            if (!o || o->id == self.id) break;
            const bool f = o->b != node;
            if (dir && f != curflip) break;
            c.els.insert(c.els.begin(), ChainEl{o, f, 0, 0, 0});
            node = f ? o->b : o->a;
            cur = o;
            curflip = f;
        }
    }
    c.self = 0;
    for (std::size_t i = 0; i < c.els.size(); ++i)
        if (c.els[i].e->id == self.id) c.self = i;
    if (c.closed) {  // canonical start: the smallest edge id, in its own a -> b direction
        auto rotate_min_first = [&]() {
            std::size_t m = 0;
            for (std::size_t i = 1; i < c.els.size(); ++i)
                if (c.els[i].e->id < c.els[m].e->id) m = i;
            std::rotate(c.els.begin(), c.els.begin() + static_cast<std::ptrdiff_t>(m), c.els.end());
        };
        rotate_min_first();
        if (c.els[0].flip) {
            std::reverse(c.els.begin(), c.els.end());
            for (ChainEl& el : c.els) el.flip = !el.flip;
            rotate_min_first();
        }
        for (std::size_t i = 0; i < c.els.size(); ++i)
            if (c.els[i].e->id == self.id) c.self = i;
    }
    // Usable ranges: only the two open ends of the chain are cleared.
    double S = 0;
    for (std::size_t i = 0; i < c.els.size(); ++i) {
        ChainEl& el = c.els[i];
        const double L = el.e->horizontal.length();
        double clear_start = 0, clear_end = 0;
        if (!c.closed && i == 0) {
            const EndInfo ei = end_info(graph, region, rules, js, *el.e, !el.flip, *region.road_type(el.e->road_type), params);
            clear_start = ei.clear;
            c.approach_start = ei.approach;
        }
        if (!c.closed && i + 1 == c.els.size()) {
            const EndInfo ei = end_info(graph, region, rules, js, *el.e, el.flip, *region.road_type(el.e->road_type), params);
            clear_end = ei.clear;
            c.approach_end = ei.approach;
        }
        el.lo = el.flip ? clear_end : clear_start;
        el.hi = L - (el.flip ? clear_start : clear_end);
        if (el.hi < el.lo) el.hi = el.lo;
        el.S = S;
        S += el.len();
    }
    c.total = S;
    return c;
}

// Whole-ring layout: n evenly spaced marks, none straddling the closure point.
std::vector<DashSpan> layout_cyclic(double length, double mark, double gap) {
    std::vector<DashSpan> out;
    if (!(length > 0) || !(mark > 0) || gap <= 0) {
        if (length > 0) out.push_back({0, length});
        return out;
    }
    const double module = mark + gap;
    if (length / module > 200000) return out;
    const int n = std::max(1, static_cast<int>(std::floor(length / module + 0.5)));
    const double m = length / n;
    for (int i = 0; i < n; ++i) out.push_back({(i + 0.5) * m - mark / 2, (i + 0.5) * m + mark / 2});
    return out;
}

struct EdgeGen {
    const RoadGraph& graph;
    const RegionPack& region;
    const MarkingRules& rules;
    const MarkingParams& params;
    const Edge& e;
    const RoadType& rt;
    std::vector<Lane> lanes;
    MarkCtx ctx;
    MarkingSet& out;
    Builder b;
    SamplingParams sp;
    std::map<std::pair<std::uint64_t, long long>, LinePoly> cache;
    std::map<std::uint64_t, std::vector<EdgeSample>> centres;
    Chain chain;
    bool junction_a = false, junction_b = false;

    const LinePoly& poly(const Edge& ed, double lateral) {
        const auto key = std::make_pair(ed.id.value, static_cast<long long>(std::llround(lateral * 1e6)));
        auto it = cache.find(key);
        if (it == cache.end()) it = cache.emplace(key, make_line_poly(ed, lateral, sp)).first;
        return it->second;
    }
    const std::vector<EdgeSample>& centreline(const Edge& ed) {
        auto it = centres.find(ed.id.value);
        if (it == centres.end()) it = centres.emplace(ed.id.value, sample_centreline(ed, sp)).first;
        return it->second;
    }

    std::vector<Interval> zone_intervals(const ZoneRule& z, const LineStyle* zone_style) {
        std::vector<Interval> iv;
        const double T = chain.total;
        if (z.kind == ZoneRule::Kind::Approach) {
            double d = 0, marks = 0;
            select_value(z.distance_m, ctx, d);
            select_value(z.min_marks, ctx, marks);
            d = std::max(d, marks * (zone_style ? zone_style->module() : 0.0));
            if (d <= 0) return iv;
            if (chain.approach_start) iv.push_back({0, std::min(d, T)});
            if (chain.approach_end) iv.push_back({std::max(T - d, 0.0), T});
        } else {
            std::vector<Interval> raw;
            for (const ChainEl& el : chain.els) {
                const std::vector<EdgeSample>& cl = centreline(*el.e);
                for (std::size_t i = 0; i + 1 < cl.size(); ++i) {
                    if (!(std::fabs(cl[i].curvature) > z.min_curvature || std::fabs(cl[i + 1].curvature) > z.min_curvature)) continue;
                    const double a = std::clamp(cl[i].s, el.lo, el.hi), c = std::clamp(cl[i + 1].s, el.lo, el.hi);
                    if (c - a <= 0) continue;
                    // Edge s -> chain S.
                    const double sa = el.flip ? el.S + (el.hi - c) : el.S + (a - el.lo);
                    const double sb = el.flip ? el.S + (el.hi - a) : el.S + (c - el.lo);
                    raw.push_back({sa, sb});
                }
            }
            std::sort(raw.begin(), raw.end(), [](const Interval& p, const Interval& q) { return p.a < q.a; });
            std::vector<Interval> merged;
            for (const Interval& r : raw) {
                if (!merged.empty() && r.a - merged.back().b < 1e-6) merged.back().b = std::max(merged.back().b, r.b);
                else merged.push_back(r);
            }
            for (const Interval& r : merged)
                if (r.b - r.a >= std::max(z.min_length_m, 1e-3)) iv.push_back(r);
        }
        return iv;
    }

    // Style segments of one boundary rule along the chain [0, total].
    std::vector<StyleSeg> segments(const BoundaryRule& br) {
        const std::string* base = select_style(br.style, ctx);
        struct Z {
            std::vector<Interval> iv;
            const std::string* style;
        };
        std::vector<Z> zones;
        std::vector<double> bp{0.0, chain.total};
        for (const ZoneRule& z : br.zones) {
            const std::string* zs = select_style(z.style, ctx);
            if (!zs) continue;
            Z zz{zone_intervals(z, style_of(rules, *zs)), zs};
            for (const Interval& i : zz.iv) {
                bp.push_back(i.a);
                bp.push_back(i.b);
            }
            zones.push_back(std::move(zz));
        }
        std::sort(bp.begin(), bp.end());
        std::vector<StyleSeg> segs;
        for (std::size_t i = 0; i + 1 < bp.size(); ++i) {
            const double a = bp[i], c = bp[i + 1];
            if (c - a < 1e-6) continue;
            const double mid = (a + c) / 2;
            const std::string* st = base;
            for (const Z& z : zones) {
                bool cov = false;
                for (const Interval& iv : z.iv)
                    if (mid >= iv.a && mid <= iv.b) cov = true;
                if (cov) {
                    st = z.style;
                    break;
                }
            }
            if (!segs.empty() && segs.back().b >= a - 1e-6 &&
                ((segs.back().style == nullptr && st == nullptr) || (segs.back().style && st && *segs.back().style == *st)))
                segs.back().b = c;
            else
                segs.push_back({a, c, st});
        }
        return segs;
    }

    // Plan length (along the strand polyline) of the chain position S, counted from the chain start.
    // lps[j] is element j's polyline; cum[j] the plan length before element j.
    static double length_at(const Chain& ch, const std::vector<const LinePoly*>& lps, const std::vector<double>& cum, double S) {
        for (std::size_t j = 0; j < ch.els.size(); ++j) {
            const ChainEl& el = ch.els[j];
            if (S <= el.S + el.len() + 1e-9 || j + 1 == ch.els.size()) {
                const double local = std::clamp(S - el.S, 0.0, el.len());
                const double se = el.flip ? el.hi - local : el.lo + local;
                const double l = el.flip ? lps[j]->len_of_s(el.hi) - lps[j]->len_of_s(se) : lps[j]->len_of_s(se) - lps[j]->len_of_s(el.lo);
                return cum[j] + l;
            }
        }
        return 0;
    }

    void emit_line(const BoundaryLine& bl) {
        const BoundaryRule& br = *bl.rule;
        const std::vector<StyleSeg> segs = segments(br);
        const ChainEl& me = chain.els[chain.self];
        for (std::size_t si = 0; si < segs.size(); ++si) {
            const StyleSeg& sg = segs[si];
            if (!sg.style) continue;
            const LineStyle* st = style_of(rules, *sg.style);
            if (!st) continue;
            const double min_mark = st->min_mark > 0 ? st->min_mark : rules.min_mark_m;
            for (const Strand& sd : st->strands) {
                std::vector<const LinePoly*> lps;
                std::vector<double> cum;
                double acc = 0;
                bool ok = true;
                for (const ChainEl& el : chain.els) {
                    const LinePoly& lp = poly(*el.e, (el.flip ? -1.0 : 1.0) * (bl.lateral + sd.offset));
                    if (!lp.valid()) ok = false;
                    lps.push_back(&lp);
                    cum.push_back(acc);
                    acc += lp.len_of_s(el.hi) - lp.len_of_s(el.lo);
                }
                if (!ok) continue;
                const std::size_t j = chain.self;
                const double La = length_at(chain, lps, cum, sg.a), Lb = length_at(chain, lps, cum, sg.b);
                std::vector<DashSpan> dashes;
                if (chain.closed && segs.size() == 1 && sg.a <= 1e-9 && sg.b >= chain.total - 1e-9)
                    dashes = layout_cyclic(Lb - La, sd.mark, sd.gap);
                else
                    dashes = layout_dashes(Lb - La, sd.mark, sd.gap, min_mark);
                const LinePoly& lp = *lps[j];
                const double cj = cum[j], lj = lp.len_of_s(me.hi) - lp.len_of_s(me.lo);
                for (const DashSpan& d : dashes) {
                    const double u0 = std::max(La + d.s0, cj), u1 = std::min(La + d.s1, cj + lj);
                    if (u1 - u0 < 1e-4) continue;
                    double l0, l1;
                    if (me.flip) {
                        l1 = lp.len_of_s(me.hi) - (u0 - cj);
                        l0 = lp.len_of_s(me.hi) - (u1 - cj);
                    } else {
                        l0 = lp.len_of_s(me.lo) + (u0 - cj);
                        l1 = lp.len_of_s(me.lo) + (u1 - cj);
                    }
                    // Skip dashes over folded stretches of a tight curve.
                    bool folded = false;
                    const double s0 = lp.s_of_len(l0), s1 = lp.s_of_len(l1);
                    for (const FoldRange& f : lp.folds)
                        if (f.s1 > std::min(s0, s1) && f.s0 < std::max(s0, s1)) folded = true;
                    if (folded) {
                        const std::string w = "edge " + std::to_string(e.id.value) + ": marking line folds on a tight curve";
                        if (std::find(out.warnings.begin(), out.warnings.end(), w) == out.warnings.end()) out.warnings.push_back(w);
                        continue;
                    }
                    std::vector<double> sv;
                    const std::vector<Vec3> raw = slice_points(e, lp, l0, l1, params, &sv);
                    std::vector<Vec3> c;
                    std::vector<double> cs;
                    for (std::size_t q = 0; q < raw.size(); ++q)
                        if (c.empty() || (raw[q].xy() - c.back().xy()).length() > 1e-6) c.push_back(raw[q]), cs.push_back(sv[q]);
                    // Side vertices follow the surface height at their own lateral offset (camber, kerb falls).
                    const double hw = sd.width / 2, lat0 = lp.lateral;
                    const std::function<double(std::size_t, double)> zf = [&](std::size_t qi, double side) {
                        return edge_surface_z(params, e, cs[qi], lat0 + side * hw);
                    };
                    std::vector<Vec3> ring;
                    if (stroke_ring(c, sd.width, ring, &zf)) b.add(br.category, *sg.style, ring, true, bl.k);
                }
            }
        }
    }

    void emit_bays(const RuleSet& rs) {
        const ChainEl& me = chain.els[chain.self];
        const double lo = me.lo, hi = me.hi;
        for (const BayRule& bay : rs.bays) {
            double len = 0;
            if (!select_value(bay.length_m, ctx, len) || len <= 0) continue;
            const LineStyle* st = style_of(rules, bay.style);
            if (!st || st->strands.empty()) continue;
            const double hw = st->strands[0].width / 2;
            const double a = lo + (junction_a ? bay.keepout_m : 0.0), c = hi - (junction_b ? bay.keepout_m : 0.0);
            const int n = static_cast<int>(std::floor((c - a) / len + kEps));
            if (n < 1) continue;
            const double start = a + ((c - a) - n * len) / 2;
            for (std::size_t k = 0; k < lanes.size(); ++k) {
                const Lane& ln = lanes[k];
                if (ln.lane_type != bay.lane_type) continue;
                double top = ln.offset + ln.width / 2, bottom = ln.offset - ln.width / 2;
                // Stop short of the bay line on the side that faces a traffic lane.
                if (k > 0 && lanes[k - 1].dir != TravelDir::None) top -= bay.edge_clear_m;
                if (k + 1 < lanes.size() && lanes[k + 1].dir != TravelDir::None) bottom += bay.edge_clear_m;
                const LinePoly& pt = poly(e, top);
                const LinePoly& pb = poly(e, bottom);
                if (!pt.valid() || !pb.valid()) continue;
                for (int i = 0; i <= n; ++i) {
                    const double s = std::clamp(start + i * len, lo + hw, hi - hw);  // dividers never overhang the edge ends
                    auto pos = [&](const LinePoly& lp, double sv) {
                        const Vec2 p = lp.xy_at_len(lp.len_of_s(sv));
                        return Vec3{p.x, p.y, edge_surface_z(params, e, sv, lp.lateral)};
                    };
                    std::vector<Vec3> ring{pos(pb, s - hw), pos(pb, s + hw), pos(pt, s + hw), pos(pt, s - hw)};
                    b.add(MarkingCategory::ParkingBay, bay.style, ring, false, static_cast<int>(k));
                }
            }
        }
    }
};

}  // namespace

MarkingSet generate_edge_markings(const RoadGraph& graph, const RegionPack& region, const MarkingRules& rules,
                                  const JunctionSet& junctions, EdgeId edge, const MarkingParams& params_in) {
    MarkingSet out;
    const Edge* e = graph.edge(edge);
    if (!e) return out;
    const MarkingParams params = with_mesh_surface(params_in, region, graph, {edge});
    const RoadType* rt = region.road_type(e->road_type);
    if (!rt) {
        out.warnings.push_back("edge " + std::to_string(edge.value) + ": unknown road type \"" + e->road_type + "\"");
        return out;
    }
    const RuleSet* rs = rules.set(rt->markings);
    if (!rs) {
        if (!rt->markings.empty())
            out.warnings.push_back("edge " + std::to_string(edge.value) + ": no marking set \"" + rt->markings + "\"");
        return out;
    }
    const double length = e->horizontal.length();
    if (!(length > 0)) return out;

    EdgeGen g{graph, region, rules, params, *e, *rt, instantiate_lanes(*e, *rt, region.driving_side), {}, out,
              Builder{out, rules, params, edge, NodeId{}}, {}, {}, {}, {}, false, false};
    g.ctx = make_ctx(*e, *rt, g.lanes, rules);
    g.sp.max_chord_error = std::max(params.chord_tolerance, 1e-4);
    g.chain = build_chain(graph, region, rules, junctions, *e, *rt, params);
    g.junction_a = end_info(graph, region, rules, junctions, *e, true, *rt, params).junction;
    g.junction_b = end_info(graph, region, rules, junctions, *e, false, *rt, params).junction;
    if (g.chain.total < rules.min_mark_m) return out;

    for (const BoundaryLine& bl : edge_boundaries(g.lanes, *rs)) g.emit_line(bl);
    g.emit_bays(*rs);
    return out;
}

// ================================================================ junction markings

namespace {

struct JGen {
    const RoadGraph& graph;
    const RegionPack& region;
    const MarkingRules& rules;
    const Junction& j;
    const MarkingParams& params;
    Builder b;
    std::vector<ArmLayout> layout;
    const JunctionSurface* surface = nullptr;
    // True when the caller gave no surface_height callback: marks deep inside a junction then follow the junction's
    // blended render surface (a caller-supplied callback is the contract and is never overridden).
    bool follow_junction_surface = false;

    // Surface height inside the junction: the render mesh's blend of the arms' profiles; the nearest
    // connector / arm point when that is unavailable.
    double surface_z(Vec2 p) const {
        if (surface && surface->valid()) return surface->z(p);
        double best = 1e18, z = j.pos.z;
        auto consider = [&](const Vec3& q) {
            const double d = (q.xy() - p).length();
            if (d < best) {
                best = d;
                z = q.z;
            }
        };
        for (const LaneConnector& c : j.connectors)
            for (const Vec3& q : c.path) consider(q);
        for (std::size_t i = 0; i < j.arms.size(); ++i) consider(arm_point(j.arms[i], trim_of(j, static_cast<int>(i)), 0).pos);
        return z;
    }

    void give_way_and_stop() {
        for (std::size_t i = 0; i < j.arms.size(); ++i) {
            const ArmLayout& L = layout[i];
            if (!L.style || !(L.give_way || L.stop || L.entry)) continue;
            const JunctionArm& arm = j.arms[i];
            if (arm.edge_length < L.line_s1 + 1.0) continue;
            double lo, hi;
            if (!lane_extent(arm, true, lo, hi)) continue;
            b.edge = arm.edge;
            const MarkingCategory cat = L.stop ? MarkingCategory::StopLine : L.entry ? MarkingCategory::RoundaboutEntry : MarkingCategory::GiveWayLine;
            if (L.entry) {  // a flared approach: the lanes lie further out beside the splitter
                const bool left_side = lo + hi > 0;  // the whole lane band moves out with its side's kerb
                const double shift = left_side ? flare_spread(arm, L.line_s0, true) : -flare_spread(arm, L.line_s0, false);
                lo += shift;
                hi += shift;
            }
            const std::size_t first_vertex = b.out.vertices.size();
            emit_transverse(b, rules, cat, *L.style, arm, L.line_s0, lo, hi);
            // An entry line sits deep inside a ring node's junction, where the render mesh blends the arms'
            // surfaces: follow that surface, or the line is buried under it by 10-25 cm.
            if (L.entry && follow_junction_surface)
                for (std::size_t vi = first_vertex; vi < b.out.vertices.size(); ++vi) b.out.vertices[vi].z = surface_z(b.out.vertices[vi].xy());
            if (L.give_way && L.triangle)
                for (const LaneRef& lane : arm.lanes)
                    if (lane.inbound) triangle(arm, L, lane.lateral - lane.width / 2, lane.lateral + lane.width / 2);
        }
    }

    void triangle(const JunctionArm& arm, const ArmLayout& L, double lo, double hi) {
        const GiveWayRule& g = rules.give_way;
        // Behind the zebra when the arm has one (the crossing sits between the line and the triangle).
        const double base = std::max(L.line_s1 + g.triangle_gap_m, L.zebra ? L.clear : 0.0), c = (lo + hi) / 2;
        if (arm.edge_length < base + g.triangle_height_m + 1.0) return;
        const double H = g.triangle_height_m, B = std::min(g.triangle_base_m, 0.7 * (hi - lo));
        const SL o[3] = {{base, c - B / 2}, {base, c + B / 2}, {base + H, c}};
        const double cs = (o[0].first + o[1].first + o[2].first) / 3, cl = (o[0].second + o[1].second + o[2].second) / 3;
        auto dist = [](SL p, SL q) { return std::hypot(p.first - q.first, p.second - q.second); };
        const double a = dist(o[0], o[1]), bb = dist(o[1], o[2]), cc = dist(o[2], o[0]);
        const double area = std::fabs((o[1].first - o[0].first) * (o[2].second - o[0].second) -
                                      (o[2].first - o[0].first) * (o[1].second - o[0].second)) / 2;
        const double inradius = area / ((a + bb + cc) / 2);
        const double k = 1.0 - g.triangle_stroke_m / inradius;
        if (!(k > 0.1)) return;
        SL in[3];
        for (int t = 0; t < 3; ++t) in[t] = {cs + (o[t].first - cs) * k, cl + (o[t].second - cl) * k};
        for (int t = 0; t < 3; ++t) {
            const int u = (t + 1) % 3;
            std::vector<Vec3> ring = arm_ring(b, arm, {o[t], o[u], in[u], in[t]});
            b.add(MarkingCategory::GiveWayTriangle, g.triangle_style, ring, false, -1, 0.5);
        }
    }

    void zebras() {
        const ZebraRule& z = rules.zebra;
        for (std::size_t i = 0; i < j.arms.size(); ++i) {
            const ArmLayout& L = layout[i];
            if (!L.zebra) continue;
            const JunctionArm& arm = j.arms[i];
            if (arm.edge_length < L.zs1 + z.clear_m + 1.0) continue;
            b.edge = arm.edge;
            const LineStyle* st = style_of(rules, z.style);
            const double min_mark = st && st->min_mark > 0 ? st->min_mark : rules.min_mark_m;
            for (const CrossingSpot& cs : j.crossings) {
                if (cs.arm != static_cast<int>(i)) continue;
                const ArmPoint p0 = arm_point(arm, cs.s, 0);
                const Vec2 nrm = left_normal({std::cos(p0.heading_out), std::sin(p0.heading_out)});
                const double lat_c = (cs.centre.xy() - p0.pos.xy()).dot(nrm);
                const double lo = lat_c - cs.length / 2;
                for (const DashSpan& d : layout_flush(cs.length, z.stripe_width_m, z.gap_m, min_mark))
                    emit_arm_quad(b, MarkingCategory::Zebra, z.style, arm, L.zs0, L.zs1, lo + d.s0, lo + d.s1);
            }
            if (!z.give_way_style.empty()) {
                double lo, hi;
                if (lane_extent(arm, true, lo, hi)) emit_transverse(b, rules, MarkingCategory::Zebra, z.give_way_style, arm, L.zline_far, lo, hi);
                if (L.zline_near >= 0 && lane_extent(arm, false, lo, hi))
                    emit_transverse(b, rules, MarkingCategory::Zebra, z.give_way_style, arm, L.zline_near, lo, hi);
            }
        }
    }

    void arrows() {
        const ArrowRule& ar = rules.arrows;
        if (!ar.enabled || j.cls != NodeClass::Junction) return;
        for (std::size_t i = 0; i < j.arms.size(); ++i) {
            const JunctionArm& arm = j.arms[i];
            if (arm.inbound_count() < ar.min_inbound_lanes || is_ring_arm(graph, region, arm)) continue;
            const Edge* e = graph.edge(arm.edge);
            const RoadType* rt = e ? region.road_type(e->road_type) : nullptr;
            if (!rt) continue;
            const MarkCtx ctx = make_ctx(*e, *rt, instantiate_lanes(*e, *rt, region.driving_side), rules);
            double len = 0;
            if (!select_value(ar.length_m, ctx, len) || len <= 0) continue;
            const double trim = trim_of(j, static_cast<int>(i));
            const double min_tip = std::max(trim + 1.0, layout[i].clear + 2.0);
            b.edge = arm.edge;
            for (std::size_t li = 0; li < arm.lanes.size(); ++li) {
                const LaneRef& lane = arm.lanes[li];
                if (!lane.inbound) continue;
                bool straight = false, left = false, right = false;
                for (const LaneConnector& c : j.connectors) {
                    if (!c.enabled || c.arm_from != static_cast<int>(i) || c.lane_from != static_cast<int>(li)) continue;
                    if (c.kind == TurnKind::Straight) straight = true;
                    else if (c.kind == TurnKind::Nearside || c.kind == TurnKind::Offside) (turn_is_left(c.kind, j.side) ? left : right) = true;
                }
                if (!straight && !left && !right) continue;
                const char* name = (left && right) ? "fork" : left ? (straight ? "ahead_turn" : "turn") : right ? (straight ? "ahead_turn" : "turn") : "ahead";
                const bool mirror = right && !left;
                auto git = ar.glyphs.find(name);
                if (git == ar.glyphs.end()) continue;
                const double wscale = std::min(ar.width_m, 0.7 * lane.width);
                for (double d : ar.distances_m) {
                    const double s_tip = std::max(trim + d, min_tip), s_tail = s_tip + len;
                    if (s_tail > arm.edge_length - 0.5) continue;
                    for (const std::vector<Vec2>& poly : git->second.polygons) {
                        std::vector<SL> pts;
                        for (const Vec2& p : poly) {
                            const double x = p.x, y = mirror ? -p.y : p.y;
                            pts.push_back({s_tail - x * len, lane.lateral - y * wscale});
                        }
                        std::vector<Vec3> ring = arm_ring(b, arm, pts);
                        b.add(MarkingCategory::Arrow, ar.style, ring, false, -1, 1.0);
                    }
                }
            }
        }
    }

    // Flow sense of an arm: +1 all lanes inbound, -1 all outbound, 0 mixed (two-way).
    static int arm_flow(const JunctionArm& a) {
        if (a.inbound_count() > 0 && a.outbound_count() == 0) return 1;
        if (a.outbound_count() > 0 && a.inbound_count() == 0) return -1;
        return 0;
    }

    // Gore areas: a continuous border line, then chevrons (traffic passes on both sides in the same
    // direction: merge and diverge noses) or diagonal hatching (opposing flows), by the lanes either side.
    void gore() {
        const GoreRule& gr = rules.gore;
        if (!gr.enabled || j.arms.size() < 2) return;
        for (const std::vector<Vec2>& ring0 : j.gore) {
            std::vector<Vec2> ring = ring0;
            if (ring.size() < 3) continue;
            double a2 = 0;
            for (std::size_t k = 0; k < ring.size(); ++k) a2 += ring[k].cross(ring[(k + 1) % ring.size()]);
            if (std::fabs(a2) / 2 < gr.min_area_m2) continue;
            if (a2 < 0) std::reverse(ring.begin(), ring.end());
            Vec2 c{0, 0};
            for (const Vec2& p : ring) c = c + p;
            c = c * (1.0 / static_cast<double>(ring.size()));

            // The two arms flanking the gore: nearest trim points to its centroid.
            std::vector<std::pair<double, std::size_t>> near;
            for (std::size_t i = 0; i < j.arms.size(); ++i)
                near.push_back({(arm_point(j.arms[i], trim_of(j, static_cast<int>(i)), 0).pos.xy() - c).length(), i});
            std::sort(near.begin(), near.end());
            const std::size_t i0 = near[0].second, i1 = near[1].second;
            const int f0 = arm_flow(j.arms[i0]), f1 = arm_flow(j.arms[i1]);
            const bool same = f0 != 0 && f0 == f1;
            const std::string& mode = same ? gr.same_direction : gr.opposing;
            // Travel direction through the gore: inbound arms flow toward the node, outbound away from it.
            Vec2 flow{0, 0};
            for (std::size_t i : {i0, i1}) {
                const ArmPoint ap = arm_point(j.arms[i], trim_of(j, static_cast<int>(i)), 0);
                const Vec2 out{std::cos(ap.heading_out), std::sin(ap.heading_out)};
                const int fl = arm_flow(j.arms[i]);
                flow = flow + out * (fl >= 0 ? -1.0 : 1.0);
            }
            flow = normalized(flow);
            if (flow.length() < 0.5) flow = {std::cos(j.arms[i0].heading_out), std::sin(j.arms[i0].heading_out)};
            b.edge = j.arms[i0].edge;

            // Continuous border: each side of the polygon becomes a mitred strip inside it.
            std::vector<Vec2> inner = ring;
            const std::string* bs = select_style(gr.border_style, arm_ctx(i0));
            const LineStyle* bst = bs ? style_of(rules, *bs) : nullptr;
            if (bst && !bst->strands.empty()) {
                const double w = bst->strands[0].width;
                const std::size_t n = ring.size();
                std::vector<Vec2> bis(n);
                for (std::size_t k = 0; k < n; ++k) {
                    const Vec2 d0 = normalized(ring[k] - ring[(k + n - 1) % n]), d1 = normalized(ring[(k + 1) % n] - ring[k]);
                    bis[k] = normalized(left_normal(d0) + left_normal(d1));
                }
                for (std::size_t k = 0; k < n; ++k) {
                    const Vec2 a = ring[k], bq = ring[(k + 1) % n];
                    const Vec2 nrm = left_normal(normalized(bq - a));
                    std::vector<Vec2> strip = clip_halfplane(ring, nrm * -1.0, a, -w);  // n.(p-a) <= w
                    const Vec2 mid = (a + bq) * 0.5;
                    for (int end = 0; end < 2; ++end) {
                        const Vec2 v = end == 0 ? a : bq;
                        const Vec2 bv = bis[end == 0 ? k : (k + 1) % n];
                        Vec2 q = left_normal(bv);
                        if (q.dot(mid - v) < 0) q = q * -1.0;
                        strip = clip_halfplane(strip, q, v, 0.0);
                    }
                    emit_gore_poly(MarkingCategory::EdgeLine, *bs, strip, 1e-4);
                    inner = clip_halfplane(inner, nrm, a, w);
                }
            }
            if (inner.size() < 3 || mode == "none") continue;

            if (mode == "chevron") chevrons(inner, c, flow, gr.chevron);
            else diagonals(inner, c, gr);
        }
    }

    void emit_gore_poly(MarkingCategory cat, const std::string& style, const std::vector<Vec2>& poly, double min_area) {
        if (poly.size() < 3) return;
        double area2 = 0;
        for (std::size_t k = 0; k < poly.size(); ++k) area2 += poly[k].cross(poly[(k + 1) % poly.size()]);
        if (std::fabs(area2) / 2 < min_area) return;
        std::vector<Vec3> r;
        for (const Vec2& p : poly) r.push_back({p.x, p.y, surface_z(p)});
        b.add(cat, style, r, false);
    }

    void diagonals(const std::vector<Vec2>& ring, Vec2 c, const GoreRule& gr) {
        double cxx = 0, cxy = 0, cyy = 0;
        for (const Vec2& p : ring) {
            const Vec2 d = p - c;
            cxx += d.x * d.x;
            cxy += d.x * d.y;
            cyy += d.y * d.y;
        }
        const double th = 0.5 * std::atan2(2 * cxy, cxx - cyy);
        const double ang = th + gr.angle_deg * kPi / 180;
        const Vec2 dir{std::cos(ang), std::sin(ang)}, nrm = left_normal(dir);
        double tmin = 1e18, tmax = -1e18;
        for (const Vec2& p : ring) {
            const double t = nrm.dot(p - c);
            tmin = std::min(tmin, t);
            tmax = std::max(tmax, t);
        }
        for (double t = tmin + gr.pitch_m / 2; t < tmax; t += gr.pitch_m) {
            std::vector<Vec2> poly = clip_halfplane(ring, nrm, c, t);
            poly = clip_halfplane(poly, nrm * -1.0, c, -(t + gr.stripe_width_m));
            emit_gore_poly(MarkingCategory::Hatching, gr.style, poly, gr.min_area_m2);
        }
    }

    // Strictly inside a convex CCW polygon by at least `margin`.
    static bool inside_convex(const std::vector<Vec2>& ring, Vec2 p, double margin) {
        for (std::size_t i = 0; i < ring.size(); ++i) {
            const Vec2 a = ring[i], bq = ring[(i + 1) % ring.size()];
            const Vec2 n = left_normal(normalized(bq - a));
            if (n.dot(p - a) < margin) return false;
        }
        return true;
    }

    static bool convex_ccw(const std::vector<Vec2>& ring) {
        for (std::size_t i = 0; i < ring.size(); ++i) {
            const Vec2 a = ring[i], bq = ring[(i + 1) % ring.size()], c = ring[(i + 2) % ring.size()];
            if ((bq - a).cross(c - bq) < -1e-9) return false;
        }
        return true;
    }

    // V-shaped chevrons, each ONE polygon: its apex lies on the gore axis and points the way the traffic
    // travels. A chevron is emitted only if both its apexes fit inside the gore and both wings reach out
    // to the border; narrower places get nothing.
    void chevrons(const std::vector<Vec2>& ring, Vec2 c, Vec2 flow, const GorePattern& cp) {
        if (!convex_ccw(ring)) return;
        const Vec2 side = left_normal(flow);
        const double al = cp.angle_deg * kPi / 180, sa = std::sin(al), ca = std::cos(al);
        double umin = 1e18, umax = -1e18, ymax = 0;
        for (const Vec2& p : ring) {
            umin = std::min(umin, flow.dot(p - c));
            umax = std::max(umax, flow.dot(p - c));
            ymax = std::max(ymax, std::fabs(side.dot(p - c)));
        }
        const double back = cp.stripe_width_m / sa;  // inner apex sits this far behind the outer one
        const double Lw = (ymax + 2.0) / sa;
        const Vec2 dl = flow * -ca + side * sa, dr = flow * -ca - side * sa;
        const double step = cp.pitch_m / sa;
        for (double u = umin + back; u < umax; u += step) {
            const Vec2 A = c + flow * u, B = A - flow * back;
            if (!inside_convex(ring, A, 0.02) || !inside_convex(ring, B, 0.02)) continue;
            std::vector<Vec2> v{A, A + dl * Lw, B + dl * Lw, B, B + dr * Lw, A + dr * Lw};
            for (std::size_t i = 0; i < ring.size(); ++i) {
                const Vec2 a = ring[i], bq = ring[(i + 1) % ring.size()];
                v = clip_halfplane(v, left_normal(normalized(bq - a)), a, 0.0);
            }
            if (v.size() < 4) continue;
            double lmax = -1e18, lmin = 1e18;
            for (const Vec2& p : v) {
                const double l = side.dot(p - A);
                lmax = std::max(lmax, l);
                lmin = std::min(lmin, l);
            }
            if (lmax < 0.4 || lmin > -0.4) continue;  // a wing is cut short: not a complete V
            emit_gore_poly(MarkingCategory::Chevron, cp.style, v, rules.gore.min_area_m2 * 0.2);
        }
    }

    // Keeps the part of `poly` where n.(p - c) >= t (Sutherland-Hodgman).
    static std::vector<Vec2> clip_halfplane(const std::vector<Vec2>& poly, Vec2 n, Vec2 c, double t) {
        std::vector<Vec2> out;
        const std::size_t cnt = poly.size();
        for (std::size_t i = 0; i < cnt; ++i) {
            const Vec2 a = poly[i], bq = poly[(i + 1) % cnt];
            const double da = n.dot(a - c) - t, db = n.dot(bq - c) - t;
            if (da >= 0) out.push_back(a);
            if ((da >= 0) != (db >= 0)) {
                const double f = da / (da - db);
                out.push_back(a + (bq - a) * f);
            }
        }
        return out;
    }

    // Edge-line laterals of an arm's edge in the arm's outward frame (EdgeLine boundaries only).
    std::vector<double> arm_edge_laterals(const JunctionArm& arm) const {
        std::vector<double> out;
        const Edge* e = graph.edge(arm.edge);
        const RoadType* rt = e ? region.road_type(e->road_type) : nullptr;
        const RuleSet* rs = rt ? rules.set(rt->markings) : nullptr;
        if (!rs) return out;
        for (const BoundaryLine& bl : edge_boundaries(instantiate_lanes(*e, *rt, region.driving_side), *rs))
            if (bl.rule->category == MarkingCategory::EdgeLine) out.push_back(arm.at_a ? bl.lateral : -bl.lateral);
        return out;
    }

    // The through route's edge line on the side facing a side road is carried across the mouth as a
    // broken straight line between the two arms' edge lines (not round the kerb radii).
    void mouth_edges() {
        const MouthEdgeRule& mr = rules.mouth_edge;
        if (!mr.enabled || j.cls != NodeClass::Junction || j.kind == JunctionKind::Roundabout || j.arms.size() < 3) return;
        const std::vector<int> prio = priority_arms(j, rules);
        for (std::size_t pi = 0; pi < prio.size(); ++pi)
            for (std::size_t pk = pi + 1; pk < prio.size(); ++pk) {
                const int i = prio[pi], k = prio[pk];
                const JunctionArm &ai = j.arms[static_cast<std::size_t>(i)], &ak = j.arms[static_cast<std::size_t>(k)];
                const double dev = (kPi - std::fabs(wrap_angle(ai.heading_out - ak.heading_out))) * 180 / kPi;
                if (dev > mr.max_deviation_deg) continue;
                const std::vector<double> li = arm_edge_laterals(ai), lk = arm_edge_laterals(ak);
                for (int sgn : {1, -1}) {
                    bool side_road = false;  // another arm lies on this side of arm i
                    for (std::size_t m = 0; m < j.arms.size(); ++m)
                        if (static_cast<int>(m) != i && static_cast<int>(m) != k &&
                            (wrap_angle(j.arms[m].heading_out - ai.heading_out) > 0) == (sgn > 0))
                            side_road = true;
                    if (!side_road) continue;
                    auto pick = [](const std::vector<double>& v, int s, double& out_lat) {
                        bool found = false;
                        for (double l : v)
                            if (l * s > 0 && (!found || l * s > out_lat * s)) out_lat = l, found = true;
                        return found;
                    };
                    double lat_i = 0, lat_k = 0;
                    if (!pick(li, sgn, lat_i) || !pick(lk, -sgn, lat_k)) continue;
                    const std::string* sid = select_style(mr.style, arm_ctx(static_cast<std::size_t>(i)));
                    const LineStyle* st = sid ? style_of(rules, *sid) : nullptr;
                    if (!st || st->strands.empty()) continue;
                    Vec3 p0 = arm_point(ai, trim_of(j, i), lat_i).pos, p1 = arm_point(ak, trim_of(j, k), lat_k).pos;
                    p0.z = arm_surface_z(params, ai, trim_of(j, i), lat_i, p0.z);
                    p1.z = arm_surface_z(params, ak, trim_of(j, k), lat_k, p1.z);
                    const double len = (p1.xy() - p0.xy()).length();
                    const Strand& sd = st->strands[0];
                    const double min_mark = st->min_mark > 0 ? st->min_mark : rules.min_mark_m;
                    b.edge = ai.edge;
                    for (const DashSpan& d : layout_flush(len, sd.mark, sd.gap, min_mark)) {
                        const double t0 = d.s0 / len, t1 = d.s1 / len;
                        std::vector<Vec3> c{{p0.x + (p1.x - p0.x) * t0, p0.y + (p1.y - p0.y) * t0, 0.0},
                                            {p0.x + (p1.x - p0.x) * t1, p0.y + (p1.y - p0.y) * t1, 0.0}};
                        for (Vec3& q : c) q.z = surface_z(q.xy());
                        std::vector<Vec3> ring;
                        if (stroke_ring(c, sd.width, ring)) b.add(MarkingCategory::EdgeLine, *sid, ring, true);
                    }
                }
            }
    }

    // Edge lines continuing through a merge / diverge along the outer lane edges of the connectors.
    void slip_edges() {
        const GoreRule& gr = rules.gore;
        if (!gr.enabled || (j.cls != NodeClass::Merge && j.cls != NodeClass::Diverge)) return;
        struct Cand {
            std::size_t conn;
            int side;  // +1 left of travel, -1 right
            double turn;
        };
        auto lat_range = [&](const JunctionArm& a, double& lo, double& hi) {
            lo = 1e9;
            hi = -1e9;
            for (const LaneRef& l : a.lanes) {
                lo = std::min(lo, l.lateral);
                hi = std::max(hi, l.lateral);
            }
        };
        std::vector<Cand> cands;
        for (std::size_t ci = 0; ci < j.connectors.size(); ++ci) {
            const LaneConnector& c = j.connectors[ci];
            if (!c.enabled || c.path.size() < 2) continue;
            const JunctionArm& F = j.arms[static_cast<std::size_t>(c.arm_from)];
            const JunctionArm& T = j.arms[static_cast<std::size_t>(c.arm_to)];
            const LaneRef& lf = F.lanes[static_cast<std::size_t>(c.lane_from)];
            const LaneRef& lt = T.lanes[static_cast<std::size_t>(c.lane_to)];
            double flo, fhi, tlo, thi;
            lat_range(F, flo, fhi);
            lat_range(T, tlo, thi);
            // Inbound travel faces the node, so its left is the outward-frame right (lower lateral).
            const bool left_ok = (lf.inbound ? lf.lateral <= flo + 1e-6 : lf.lateral >= fhi - 1e-6) &&
                                 (lt.inbound ? lt.lateral <= tlo + 1e-6 : lt.lateral >= thi - 1e-6);
            const bool right_ok = (lf.inbound ? lf.lateral >= fhi - 1e-6 : lf.lateral <= flo + 1e-6) &&
                                  (lt.inbound ? lt.lateral >= thi - 1e-6 : lt.lateral <= tlo + 1e-6);
            const Vec2 d0 = c.path[1].xy() - c.path[0].xy();
            const Vec2 d1 = c.path.back().xy() - c.path[c.path.size() - 2].xy();
            const double turn = wrap_angle(std::atan2(d1.y, d1.x) - std::atan2(d0.y, d0.x));
            if (left_ok) cands.push_back({ci, +1, turn});
            if (right_ok) cands.push_back({ci, -1, turn});
        }
        auto shares = [&](const LaneConnector& a, const LaneConnector& c) {
            return (a.arm_from == c.arm_from && a.lane_from == c.lane_from) || (a.arm_to == c.arm_to && a.lane_to == c.lane_to);
        };
        for (std::size_t x = 0; x < cands.size(); ++x) {
            bool best = true;
            for (std::size_t y = 0; y < cands.size() && best; ++y) {
                if (x == y || cands[x].side != cands[y].side) continue;
                if (!shares(j.connectors[cands[x].conn], j.connectors[cands[y].conn])) continue;
                // The outer edge on the left is the one that turns most to the left, and vice versa.
                const double dx = cands[x].side * cands[x].turn, dy = cands[y].side * cands[y].turn;
                if (dy > dx + 1e-6 || (std::fabs(dy - dx) <= 1e-6 && y < x)) best = false;
            }
            if (!best) continue;
            const LaneConnector& c = j.connectors[cands[x].conn];
            const JunctionArm& F = j.arms[static_cast<std::size_t>(c.arm_from)];
            const double w = F.lanes[static_cast<std::size_t>(c.lane_from)].width;
            const MarkCtx ctx = arm_ctx(static_cast<std::size_t>(c.arm_from));
            const std::string* sid = select_style(gr.edge_style, ctx);
            if (!sid) continue;
            const LineStyle* st = style_of(rules, *sid);
            if (!st || st->strands.empty()) continue;
            const double shift = cands[x].side * (w / 2 - gr.edge_inset_m);
            std::vector<Vec3> pts = offset_points(c.path, shift);
            for (Vec3& q : pts) q.z = surface_z(q.xy());
            std::vector<Vec3> ring;
            if (stroke_ring(pts, st->strands[0].width, ring)) b.add(MarkingCategory::EdgeLine, *sid, ring, true);
        }
    }

    MarkCtx arm_ctx(std::size_t i) const {
        const Edge* e = graph.edge(j.arms[i].edge);
        const RoadType* rt = e ? region.road_type(e->road_type) : nullptr;
        if (!rt) return {};
        return make_ctx(*e, *rt, instantiate_lanes(*e, *rt, region.driving_side), rules);
    }
};

}  // namespace

MarkingSet generate_junction_markings(const RoadGraph& graph, const RegionPack& region, const MarkingRules& rules,
                                      const Junction& junction, const MarkingParams& params_in) {
    MarkingSet out;
    if (junction.cls == NodeClass::End || junction.cls == NodeClass::Joint) return out;
    if (!junction.trims.empty() && junction.trims.size() != junction.arms.size()) {
        out.warnings.push_back("junction " + std::to_string(junction.node.value) + ": trims do not match arms");
        return out;
    }
    std::vector<EdgeId> edges;
    for (const JunctionArm& a : junction.arms) edges.push_back(a.edge);
    const MarkingParams params = with_mesh_surface(params_in, region, graph, edges);
    const JunctionSurface surface(graph, region, junction);
    JGen g{graph, region, rules, junction, params, Builder{out, rules, params, EdgeId{}, junction.node}, {}, &surface};
    g.follow_junction_surface = !params_in.surface_height;
    g.layout = compute_arm_layout(graph, region, rules, junction, params);
    g.give_way_and_stop();
    g.zebras();
    g.arrows();
    g.gore();
    g.slip_edges();
    g.mouth_edges();
    return out;
}

MarkingSet generate_splitter_markings(const RoadGraph& graph, const RegionPack& region, const MarkingRules& rules,
                                      const JunctionSet& junctions, const RoundaboutLayout& layout, const MarkingParams& params_in) {
    MarkingSet out;
    const SplitterRule& sr = rules.splitter;
    if (!sr.enabled || !layout.ok || layout.group.nodes.empty()) return out;
    std::vector<EdgeId> arm_edges;
    for (const RoundaboutArm& a : layout.group.arms) arm_edges.push_back(a.edge);
    const MarkingParams params = with_mesh_surface(params_in, region, graph, arm_edges);
    Builder b{out, rules, params, EdgeId{}, layout.group.nodes.front()};
    for (const SplitterIsland& sp : layout.splitters) {
        if (sp.polygon.size() < 3 || sp.arm < 0 || static_cast<std::size_t>(sp.arm) >= layout.group.arms.size()) continue;
        const RoundaboutArm& ra = layout.group.arms[static_cast<std::size_t>(sp.arm)];
        if (ra.ring_index < 0 || static_cast<std::size_t>(ra.ring_index) >= layout.group.nodes.size()) continue;
        const NodeId node = layout.group.nodes[static_cast<std::size_t>(ra.ring_index)];
        const Junction* jn = junctions.find(node);
        if (!jn) {
            auto it = layout.ring_junctions.find(node);
            if (it != layout.ring_junctions.end()) jn = &it->second;
        }
        if (!jn) continue;
        const JunctionArm* arm = nullptr;
        for (const JunctionArm& a : jn->arms)
            if (a.edge == ra.edge) arm = &a;
        if (!arm) continue;
        b.edge = arm->edge;
        std::vector<Vec2> poly = sp.polygon;
        double a2 = 0;
        for (std::size_t k = 0; k < poly.size(); ++k) a2 += poly[k].cross(poly[(k + 1) % poly.size()]);
        if (a2 < 0) std::reverse(poly.begin(), poly.end());
        const Edge* e = graph.edge(arm->edge);
        const RoadType* rt = e ? region.road_type(e->road_type) : nullptr;
        if (!rt) continue;
        const MarkCtx ctx = make_ctx(*e, *rt, instantiate_lanes(*e, *rt, region.driving_side), rules);
        // Arm-frame (s, lateral) of a plan point, and the surface z there.
        auto frame = [&](Vec2 p) {
            double best = 1e18, bs = 0;
            for (double s = 0; s <= std::min(arm->edge_length, 80.0); s += 0.25) {
                const double d = (arm_point(*arm, s, 0).pos.xy() - p).length();
                if (d < best) best = d, bs = s;
            }
            const ArmPoint ap = arm_point(*arm, bs, 0);
            const Vec2 nrm = left_normal({std::cos(ap.heading_out), std::sin(ap.heading_out)});
            return SL{bs, (p - ap.pos.xy()).dot(nrm)};
        };
        auto z_of = [&](Vec2 p) {
            const SL sl = frame(p);
            return arm_surface_z(params, *arm, sl.first, sl.second, arm_point(*arm, sl.first, sl.second).pos.z);
        };

        // Outline: a mitred strip just outside the island, one trapezoid per side.
        const std::string* osid = select_style(sr.outline_style, ctx);
        const LineStyle* ost = osid ? style_of(rules, *osid) : nullptr;
        if (ost && !ost->strands.empty()) {
            const double w = ost->strands[0].width, g = sr.outline_gap_m;
            const std::size_t n = poly.size();
            std::vector<Vec2> in(n), outer(n);
            for (std::size_t k = 0; k < n; ++k) {
                const Vec2 d0 = normalized(poly[k] - poly[(k + n - 1) % n]), d1 = normalized(poly[(k + 1) % n] - poly[k]);
                const Vec2 n0 = left_normal(d0) * -1.0, n1 = left_normal(d1) * -1.0;  // outward for a CCW ring
                Vec2 m = normalized(n0 + n1);
                if (m.length() < 0.5) m = n1;
                const double miter = std::min(1.0 / std::max(m.dot(n1), 0.5), 2.0);
                in[k] = poly[k] + m * (g * miter);
                outer[k] = poly[k] + m * ((g + w) * miter);
            }
            for (std::size_t k = 0; k < n; ++k) {
                const std::size_t u = (k + 1) % n;
                std::vector<Vec3> r;
                for (const Vec2& p : {in[k], in[u], outer[u], outer[k]}) r.push_back({p.x, p.y, z_of(p)});
                b.add(MarkingCategory::EdgeLine, *osid, r, false);
            }
        }

        // Two-way approach: the flared gap round the kerbed island is paint. A solid line along the inner edge of each
        // lane, and ladder bars (perpendicular to the road) across the gap, left and right of the island and then
        // across the whole gap beyond its nose.
        if (!sp.paint.empty() && sp.nose_s > sp.ring_s) {
            const double s_end = std::min(arm->flare.length, arm->edge_length - 1.0);
            const double g = sr.outline_gap_m;
            if (ost && !ost->strands.empty()) {
                const double w = ost->strands[0].width;
                for (int side = 0; side < 2; ++side) {
                    const double sg = side == 0 ? 1.0 : -1.0;
                    std::vector<Vec3> pts;
                    for (double sx = sp.ring_s + 0.5; sx < s_end; sx += 1.0) {
                        const double lat = sg * (flare_spread(*arm, sx, side == 0) + g + w / 2);
                        const Vec2 q = arm_point(*arm, sx, lat).pos.xy();
                        pts.push_back({q.x, q.y, z_of(q)});
                    }
                    std::vector<Vec3> ring;
                    if (pts.size() >= 2 && stroke_ring(pts, w, ring)) b.add(MarkingCategory::EdgeLine, *osid, ring, true);
                }
            }
            if (!sr.hatch_style.empty()) {
                const double bw = sr.stripe_width_m, inset = g + (ost && !ost->strands.empty() ? ost->strands[0].width : 0.1) + 0.05;
                const auto island_half = [&](double sx) {  // the island's half width at sx (0 beyond its nose)
                    if (sx >= sp.nose_s) return 0.0;
                    const double s_c = sp.nose_s - sp.nose_r;
                    const double h = sx > s_c ? std::sqrt(std::max(0.0, sp.nose_r * sp.nose_r - (sx - s_c) * (sx - s_c)))
                                              : sp.nose_r + (sp.base_half - sp.nose_r) * (s_c - sx) / std::max(s_c - sp.ring_s, 1e-6);
                    return std::min(h, flare_spread(*arm, sx, true));
                };
                const auto bar = [&](double s0b, double l0, double l1) {
                    if (l1 - l0 < 0.15) return;
                    std::vector<Vec3> r;
                    for (const SL& q : {SL{s0b, l0}, SL{s0b + bw, l0}, SL{s0b + bw, l1}, SL{s0b, l1}}) {
                        const Vec2 pq = arm_point(*arm, q.first, q.second).pos.xy();
                        r.push_back({pq.x, pq.y, z_of(pq)});
                    }
                    b.add(MarkingCategory::Hatching, sr.hatch_style, r, false);
                };
                for (double sx = sp.ring_s + sr.ladder_pitch_m; sx + bw < s_end - 0.5; sx += sr.ladder_pitch_m) {
                    const double gl = flare_spread(*arm, sx, true) - inset, gr = flare_spread(*arm, sx + bw, false) - inset;
                    const double ih = island_half(sx) + g + (ost && !ost->strands.empty() ? ost->strands[0].width : 0.1) + 0.05;
                    if (sx > sp.nose_s) {
                        bar(sx, -gr, gl);
                    } else {
                        bar(sx, ih, gl);
                        bar(sx, -gr, -ih);
                    }
                }
            }
        } else
        // Approach hatching: a tapering triangle ahead of the nose, bars at an angle.
        if (!sr.hatch_style.empty() && sr.hatch_length_m > 1.0) {
            double s_nose = 0, lat_sum = 0;
            int cnt = 0;
            for (const Vec2& p : poly) s_nose = std::max(s_nose, frame(p).first);
            for (const Vec2& p : poly) {
                const SL sl = frame(p);
                if (sl.first > s_nose - 1.0) lat_sum += sl.second, ++cnt;
            }
            const double c = cnt ? lat_sum / cnt : 0.0;
            const double s0 = s_nose + 0.5, s1 = s0 + sr.hatch_length_m;
            if (arm->edge_length > s1 + 1.0) {
                std::vector<Vec2> tri;
                for (const SL& q : {SL{s0, c - sr.hatch_width_m / 2}, SL{s1, c}, SL{s0, c + sr.hatch_width_m / 2}})
                    tri.push_back(arm_point(*arm, q.first, q.second).pos.xy());
                double ta = 0;
                for (std::size_t k = 0; k < 3; ++k) ta += tri[k].cross(tri[(k + 1) % 3]);
                if (ta < 0) std::reverse(tri.begin(), tri.end());
                Vec2 cen{0, 0};
                for (const Vec2& p : tri) cen = cen + p;
                cen = cen * (1.0 / 3.0);
                const Vec2 ax = normalized(tri[0] - tri[1] + tri[2] - tri[1]);
                const double ang = std::atan2(ax.y, ax.x) + sr.angle_deg * kPi / 180;
                const Vec2 nrm = left_normal({std::cos(ang), std::sin(ang)});
                double tmin = 1e18, tmax = -1e18;
                for (const Vec2& p : tri) tmin = std::min(tmin, nrm.dot(p - cen)), tmax = std::max(tmax, nrm.dot(p - cen));
                for (double t = tmin + sr.pitch_m / 2; t < tmax; t += sr.pitch_m) {
                    std::vector<Vec2> pp = JGen::clip_halfplane(tri, nrm, cen, t);
                    pp = JGen::clip_halfplane(pp, nrm * -1.0, cen, -(t + sr.stripe_width_m));
                    if (pp.size() < 3) continue;
                    double pa = 0;
                    for (std::size_t k = 0; k < pp.size(); ++k) pa += pp[k].cross(pp[(k + 1) % pp.size()]);
                    if (std::fabs(pa) / 2 < 0.02) continue;
                    std::vector<Vec3> r;
                    for (const Vec2& p : pp) r.push_back({p.x, p.y, z_of(p)});
                    b.add(MarkingCategory::Hatching, sr.hatch_style, r, false);
                }
            }
        }
    }
    return out;
}

// ================================================================ aggregate

MarkingSet MarkingResult::merged() const {
    MarkingSet out;
    for (const auto& [id, s] : edges) out.append(s);
    for (const auto& [id, s] : junctions) out.append(s);
    for (const auto& [id, s] : splitters) out.append(s);
    return out;
}

MarkingResult generate_markings(const RoadGraph& graph, const RegionPack& region, const MarkingRules& rules,
                                const JunctionSet& junctions, const MarkingParams& params,
                                const std::vector<EdgeId>& only_edges, const std::vector<NodeId>& only_nodes) {
    MarkingResult r;
    if (only_edges.empty()) {
        for (const auto& [id, e] : graph.edges()) r.edges[id] = generate_edge_markings(graph, region, rules, junctions, id, params);
    } else {
        for (EdgeId id : only_edges)
            if (graph.edge(id)) r.edges[id] = generate_edge_markings(graph, region, rules, junctions, id, params);
    }
    if (only_nodes.empty()) {
        for (const auto& [id, j] : junctions.junctions) r.junctions[id] = generate_junction_markings(graph, region, rules, j, params);
    } else {
        for (NodeId id : only_nodes)
            if (const Junction* j = junctions.find(id)) r.junctions[id] = generate_junction_markings(graph, region, rules, *j, params);
    }
if (params.roundabouts)        for (const RoundaboutLayout& l : params.roundabouts->layouts) {            if (l.group.nodes.empty()) continue;            bool wanted = only_nodes.empty();            for (NodeId id : only_nodes)                if (std::find(l.group.nodes.begin(), l.group.nodes.end(), id) != l.group.nodes.end()) wanted = true;            if (wanted) r.splitters[l.group.nodes.front()] = generate_splitter_markings(graph, region, rules, junctions, l, params);        }
    return r;
}

void rebuild_dirty_markings(MarkingResult& cache, const RoadGraph& graph, const RegionPack& region,
                            const MarkingRules& rules, const JunctionSet& junctions, const std::vector<NodeId>& dirty_nodes,
                            const std::vector<EdgeId>& dirty_edges, const MarkingParams& params) {
    std::set<EdgeId> edges(dirty_edges.begin(), dirty_edges.end());
    std::set<NodeId> nodes(dirty_nodes.begin(), dirty_nodes.end());
    for (NodeId n : dirty_nodes)
        for (EdgeId e : graph.edges_at(n)) edges.insert(e);
    for (EdgeId id : dirty_edges)
        if (const Edge* e = graph.edge(id)) {
            nodes.insert(e->a);
            nodes.insert(e->b);
        }
    for (EdgeId id : edges) {
        if (graph.edge(id)) cache.edges[id] = generate_edge_markings(graph, region, rules, junctions, id, params);
        else cache.edges.erase(id);
    }
    for (NodeId id : nodes) {
        if (const Junction* j = junctions.find(id)) cache.junctions[id] = generate_junction_markings(graph, region, rules, *j, params);
        else cache.junctions.erase(id);
    }
if (params.roundabouts)        for (const RoundaboutLayout& l : params.roundabouts->layouts) {            if (l.group.nodes.empty()) continue;            bool touched = false;            for (NodeId id : l.group.nodes)                if (nodes.count(id)) touched = true;            if (touched) cache.splitters[l.group.nodes.front()] = generate_splitter_markings(graph, region, rules, junctions, l, params);        }
}

}  // namespace cp
