// Road markings generator and rule loader (include/cp/markings.h). No exceptions: sim_core builds with
// -fno-exceptions; errors travel as messages / warnings.
#include "cp/markings.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <limits>
#include <sstream>

#include "cp/edge_sampling.h"
#include "cp/lanes.h"
#include "nlohmann/json.hpp"

namespace cp {

namespace {

using json = nlohmann::json;

constexpr double kEps = 1e-9;
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
    const int n = static_cast<int>(std::floor((length + gap) / module + kEps));
    if (n < 1) {  // shorter than one mark: a single short mark if it is long enough
        if (length >= min_mark - kEps) out.push_back({0, length});
        return out;
    }
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

struct Err {
    std::string msg;
    bool failed() const { return !msg.empty(); }
    void set(const std::string& m) {
        if (msg.empty()) msg = m;
    }
};

const json* mem(const json& j, const char* key) {
    if (!j.is_object()) return nullptr;
    auto it = j.find(key);
    return it == j.end() ? nullptr : &*it;
}

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
    const json* v = mem(j, key);
    if (!v) return def;
    if (!v->is_number()) {
        err.set(ctx + ": \"" + key + "\" must be a number");
        return def;
    }
    return v->get<double>();
}

std::string str(const json& j, const char* key, const std::string& def, const std::string& ctx, Err& err) {
    const json* v = mem(j, key);
    if (!v) return def;
    if (!v->is_string()) {
        err.set(ctx + ": \"" + key + "\" must be a string");
        return def;
    }
    return v->get<std::string>();
}

bool flag(const json& j, const char* key, bool def, const std::string& ctx, Err& err) {
    const json* v = mem(j, key);
    if (!v) return def;
    if (!v->is_boolean()) {
        err.set(ctx + ": \"" + key + "\" must be a boolean");
        return def;
    }
    return v->get<bool>();
}

double unit_to_ms(const std::string& u, Err& err) {
    if (u == "mph") return 0.44704;
    if (u == "kph" || u == "kmh") return 1.0 / 3.6;
    if (u == "ms" || u == "m/s") return 1.0;
    err.set("marking rules: unknown speed_unit \"" + u + "\"");
    return 1.0;
}

MarkCond parse_cond(const json* j, double unit, const std::string& ctx, Err& err) {
    MarkCond c;
    if (!j) return c;
    check_keys(*j, {"speed_max", "speed_min", "width_min", "width_max", "lanes_min", "lanes_max", "shoulder", "one_way"},
               ctx + " \"if\"", err);
    if (err.failed()) return c;
    if (mem(*j, "speed_max")) c.speed_max = num(*j, "speed_max", 0, ctx, err) * unit;
    if (mem(*j, "speed_min")) c.speed_min = num(*j, "speed_min", 0, ctx, err) * unit;
    if (mem(*j, "width_min")) c.width_min = num(*j, "width_min", 0, ctx, err);
    if (mem(*j, "width_max")) c.width_max = num(*j, "width_max", 0, ctx, err);
    if (mem(*j, "lanes_min")) c.lanes_min = static_cast<int>(num(*j, "lanes_min", 0, ctx, err));
    if (mem(*j, "lanes_max")) c.lanes_max = static_cast<int>(num(*j, "lanes_max", 0, ctx, err));
    if (mem(*j, "shoulder")) c.shoulder = flag(*j, "shoulder", false, ctx, err);
    if (mem(*j, "one_way")) c.one_way = flag(*j, "one_way", false, ctx, err);
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
        c.when = parse_cond(mem(e, "if"), unit, ctx, err);
        const json* s = mem(e, "style");
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
        c.when = parse_cond(mem(e, "if"), unit, ctx, err);
        const json* v = mem(e, "value");
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
    for (int i = 0; i <= static_cast<int>(MarkingCategory::RoundaboutEntry); ++i) {
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
    const json* st = mem(root, "styles");
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
        const json* sa = mem(e, "strands");
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
    const json* ss = mem(root, "sets");
    if (!ss || !ss->is_object()) {
        err.set("marking rules: missing \"sets\" object");
        return;
    }
    for (auto it = ss->begin(); it != ss->end(); ++it) {
        if (!it.key().empty() && it.key()[0] == '_') continue;
        const std::string sctx = "set \"" + it.key() + "\"";
        check_keys(it.value(), {"boundaries", "bays"}, sctx, err);
        if (err.failed()) return;
        RuleSet rs;
        rs.id = it.key();
        if (const json* bs = mem(it.value(), "boundaries")) {
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
                    br.style = parse_style_sel(mem(b, "style"), unit, ctx, err);
                    br.inset_m = num(b, "inset_m", 0, ctx, err);
                    br.source = str(b, "source", "", ctx, err);
                    if (br.source.empty()) err.set(ctx + ": missing \"source\"");
                    if (const json* zs = mem(b, "zones")) {
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
                                zr.distance_m = parse_value_sel(mem(z, "distance_m"), unit, ctx, err);
                                zr.min_marks = parse_value_sel(mem(z, "min_marks"), unit, ctx, err);
                                zr.min_curvature = num(z, "min_curvature", 0, ctx, err);
                                zr.min_length_m = num(z, "min_length_m", 0, ctx, err);
                                zr.style = parse_style_sel(mem(z, "style"), unit, ctx, err);
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
        if (const json* bs = mem(it.value(), "bays")) {
            if (!bs->is_array()) err.set(sctx + ": \"bays\" must be a list");
            else
                for (const json& b : *bs) {
                    check_keys(b, {"lane_type", "length_m", "style", "keepout_m", "edge_clear_m", "source"}, sctx + " bay", err);
                    if (err.failed()) return;
                    BayRule br;
                    br.lane_type = str(b, "lane_type", "", sctx, err);
                    br.length_m = parse_value_sel(mem(b, "length_m"), unit, sctx, err);
                    br.style = str(b, "style", "", sctx, err);
                    br.keepout_m = num(b, "keepout_m", 0, sctx, err);
                    br.edge_clear_m = num(b, "edge_clear_m", 0.06, sctx, err);
                    br.source = str(b, "source", "", sctx, err);
                    if (br.lane_type.empty() || br.style.empty() || br.length_m.empty())
                        err.set(sctx + ": a bay rule needs lane_type, length_m and style");
                    rs.bays.push_back(std::move(br));
                }
        }
        if (err.failed()) return;
        r.sets.emplace(rs.id, std::move(rs));
    }
}

void parse_junction(const json& root, double unit, MarkingRules& r, Err& err) {
    const json* jn = mem(root, "junction");
    if (!jn) return;
    check_keys(*jn, {"give_way", "stop_line", "zebra", "roundabout_entry", "gore", "arrows"}, "junction", err);
    if (err.failed()) return;
    if (const json* g = mem(*jn, "give_way")) {
        check_keys(*g, {"style", "setback_m", "line_gap_m", "triangle", "source"}, "give_way", err);
        r.give_way.enabled = true;
        r.give_way.style = parse_style_sel(mem(*g, "style"), unit, "give_way", err);
        r.give_way.setback_m = num(*g, "setback_m", 0.5, "give_way", err);
        r.give_way.line_gap_m = num(*g, "line_gap_m", 0.3, "give_way", err);
        r.give_way.source = str(*g, "source", "", "give_way", err);
        if (const json* t = mem(*g, "triangle")) {
            check_keys(*t, {"style", "gap_from_line_m", "base_m", "height_m", "stroke_m"}, "give_way triangle", err);
            r.give_way.triangle = true;
            r.give_way.triangle_style = str(*t, "style", "", "give_way triangle", err);
            r.give_way.triangle_gap_m = num(*t, "gap_from_line_m", 1.0, "give_way triangle", err);
            r.give_way.triangle_base_m = num(*t, "base_m", 1.8, "give_way triangle", err);
            r.give_way.triangle_height_m = num(*t, "height_m", 3.0, "give_way triangle", err);
            r.give_way.triangle_stroke_m = num(*t, "stroke_m", 0.2, "give_way triangle", err);
        }
    }
    if (const json* g = mem(*jn, "stop_line")) {
        check_keys(*g, {"style", "setback_m", "line_gap_m", "source"}, "stop_line", err);
        r.stop_line.enabled = true;
        r.stop_line.style = str(*g, "style", "", "stop_line", err);
        r.stop_line.setback_m = num(*g, "setback_m", 0.5, "stop_line", err);
        r.stop_line.line_gap_m = num(*g, "line_gap_m", 0.3, "stop_line", err);
        r.stop_line.source = str(*g, "source", "", "stop_line", err);
    }
    if (const json* g = mem(*jn, "zebra")) {
        check_keys(*g, {"if", "style", "stripe_width_m", "gap_m", "band_width_m", "clear_m", "give_way_style",
                        "give_way_gap_m", "min_start_gap_m", "source"}, "zebra", err);
        ZebraRule& z = r.zebra;
        z.enabled = true;
        z.when = parse_cond(mem(*g, "if"), unit, "zebra", err);
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
    if (const json* g = mem(*jn, "roundabout_entry")) {
        check_keys(*g, {"style", "setback_m", "line_gap_m", "source"}, "roundabout_entry", err);
        r.roundabout_entry.enabled = true;
        r.roundabout_entry.style = parse_style_sel(mem(*g, "style"), unit, "roundabout_entry", err);
        r.roundabout_entry.setback_m = num(*g, "setback_m", 0.5, "roundabout_entry", err);
        r.roundabout_entry.line_gap_m = num(*g, "line_gap_m", 0.3, "roundabout_entry", err);
        r.roundabout_entry.source = str(*g, "source", "", "roundabout_entry", err);
    }
    if (const json* g = mem(*jn, "gore")) {
        check_keys(*g, {"style", "stripe_width_m", "pitch_m", "angle_deg", "min_area_m2", "edge_style", "edge_inset_m", "source"},
                   "gore", err);
        GoreRule& z = r.gore;
        z.enabled = true;
        z.style = str(*g, "style", "", "gore", err);
        z.stripe_width_m = num(*g, "stripe_width_m", 0.3, "gore", err);
        z.pitch_m = num(*g, "pitch_m", 1.2, "gore", err);
        z.angle_deg = num(*g, "angle_deg", 60, "gore", err);
        z.min_area_m2 = num(*g, "min_area_m2", 0.05, "gore", err);
        z.edge_style = parse_style_sel(mem(*g, "edge_style"), unit, "gore", err);
        z.edge_inset_m = num(*g, "edge_inset_m", 0.225, "gore", err);
        z.source = str(*g, "source", "", "gore", err);
        if (!(z.stripe_width_m > 0) || !(z.pitch_m > z.stripe_width_m)) err.set("gore: need 0 < stripe_width_m < pitch_m");
    }
    if (const json* g = mem(*jn, "arrows")) {
        check_keys(*g, {"min_inbound_lanes", "distances_m", "length_m", "width_m", "style", "glyphs", "source"}, "arrows", err);
        ArrowRule& a = r.arrows;
        a.enabled = true;
        a.min_inbound_lanes = static_cast<int>(num(*g, "min_inbound_lanes", 2, "arrows", err));
        a.length_m = parse_value_sel(mem(*g, "length_m"), unit, "arrows", err);
        a.width_m = num(*g, "width_m", 1.0, "arrows", err);
        a.style = str(*g, "style", "", "arrows", err);
        a.source = str(*g, "source", "", "arrows", err);
        if (const json* d = mem(*g, "distances_m")) {
            if (!d->is_array()) err.set("arrows: distances_m must be a list");
            else
                for (const json& v : *d) {
                    if (!v.is_number()) err.set("arrows: distances_m entries must be numbers");
                    else a.distances_m.push_back(v.get<double>());
                }
        }
        const json* gl = mem(*g, "glyphs");
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
                const json* parts = mem(it.value(), "parts");
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
    need_sel(r.gore.edge_style, "gore edge_style");
    need(r.arrows.style, "arrows");
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
    check_keys(root, {"version", "speed_unit", "min_mark_m", "colours", "styles", "sets", "junction"}, "marking rules", err);
    r.version = static_cast<int>(num(root, "version", 0, "marking rules", err));
    r.speed_unit = str(root, "speed_unit", "mph", "marking rules", err);
    const double unit = unit_to_ms(r.speed_unit, err);
    r.min_mark_m = num(root, "min_mark_m", 0.3, "marking rules", err);
    if (const json* cs = mem(root, "colours")) {
        if (!cs->is_object()) err.set("marking rules: \"colours\" must be an object");
        else
            for (auto it = cs->begin(); it != cs->end(); ++it) {
                if (!it.key().empty() && it.key()[0] == '_') continue;
                const json* rgb = mem(it.value(), "rgb");
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
bool stroke_ring(const std::vector<Vec3>& in, double width, std::vector<Vec3>& ring) {
    const std::vector<Vec3> c = dedupe(in);
    ring.clear();
    if (c.size() < 2) return false;
    std::vector<Vec2> n;
    std::vector<double> m;
    polyline_normals(c, n, m);
    const double hw = width / 2;
    for (std::size_t i = 0; i < c.size(); ++i) ring.push_back({c[i].x - n[i].x * hw * m[i], c[i].y - n[i].y * hw * m[i], c[i].z});
    for (std::size_t i = c.size(); i-- > 0;) ring.push_back({c[i].x + n[i].x * hw * m[i], c[i].y + n[i].y * hw * m[i], c[i].z});
    return true;
}

// Collects marks into a MarkingSet, interning ids and dropping degenerate polygons.
struct Builder {
    MarkingSet& out;
    const MarkingRules& rules;
    EdgeId edge;
    NodeId node;

    void add(MarkingCategory cat, const std::string& style, std::vector<Vec3>& ring, bool strip, int lane = -1) {
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
        out.vertices.insert(out.vertices.end(), ring.begin(), ring.end());
        out.marks.push_back(m);
    }
};

// ---- offset polylines of an edge, addressed by plan length

struct LinePoly {
    std::vector<double> s, cum;
    std::vector<Vec2> xy;
    std::vector<FoldRange> folds;
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
std::vector<Vec3> slice_points(const Edge& e, const LinePoly& lp, double l0, double l1, double tol) {
    std::vector<double> ls{l0};
    for (double c : lp.cum)
        if (c > l0 + 1e-6 && c < l1 - 1e-6) ls.push_back(c);
    ls.push_back(l1);
    auto z_at = [&](double l) { return e.vertical.z(lp.s_of_len(l)); };
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
    }
    return pts;
}

struct Interval {
    double a = 0, b = 0;
};

std::vector<Interval> subtract(const std::vector<Interval>& in, const std::vector<FoldRange>& holes) {
    std::vector<Interval> cur = in;
    for (const FoldRange& h : holes) {
        std::vector<Interval> next;
        for (const Interval& iv : cur) {
            if (h.s1 <= iv.a || h.s0 >= iv.b) {
                next.push_back(iv);
                continue;
            }
            if (h.s0 > iv.a) next.push_back({iv.a, h.s0});
            if (h.s1 < iv.b) next.push_back({h.s1, iv.b});
        }
        cur = std::move(next);
    }
    return cur;
}

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

MarkCtx make_ctx(const Edge& e, const RoadType& rt, const std::vector<Lane>& lanes, const RegionPack& region) {
    MarkCtx c;
    c.speed = rt.speed_limit;
    for (const Lane& l : lanes) {
        if (l.dir != TravelDir::None) {
            c.width += l.width;
            ++c.lanes;
        } else if (const LaneType* lt = region.lane_type(l.lane_type)) {
            if (lt->users.empty() && !lt->may_park && lt->surface == "asphalt") c.shoulder = true;
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

// ---- per-arm transverse marking layout, shared by the edge and the junction generators

struct ArmLayout {
    bool give_way = false, stop = false, entry = false;
    const std::string* style = nullptr;  // line style id (give way / stop / entry)
    double line_s0 = 0, line_s1 = 0;     // distance from the node: near and far edge of the line
    bool zebra = false;
    double zs0 = 0, zs1 = 0;            // stripe band, distance from the node
    double zline_far = 0, zline_near = -1;  // zebra give way lines: near edge distance (<0: none)
    double clear = 0;                    // longitudinal lines stop this far from the node
    MarkCtx ctx;
};

std::vector<ArmLayout> compute_arm_layout(const RoadGraph& g, const RegionPack& region, const MarkingRules& rules,
                                          const Junction& j, const MarkingParams& params) {
    std::vector<ArmLayout> out(j.arms.size());
    const bool priority_class = j.cls == NodeClass::Junction && j.arms.size() >= 3;
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
        L.ctx = make_ctx(*e, *rt, instantiate_lanes(*e, *rt, region.driving_side), region);
        const bool ring = rt->roundabout_ring;
        const double trim = L.clear;
        if (arm.inbound_count() > 0 && !ring) {
            if (j.kind == JunctionKind::Roundabout) {
                if (rules.roundabout_entry.enabled && (!params.use_roundabout_give_way || gw_entries.count(static_cast<int>(i)))) {
                    L.style = select_style(rules.roundabout_entry.style, L.ctx);
                    if (L.style) {
                        L.entry = true;
                        L.line_s0 = trim + rules.roundabout_entry.setback_m;
                    }
                }
            } else if (params.stop_arms.count({j.node, arm.edge}) && rules.stop_line.enabled) {
                L.stop = true;
                L.style = &rules.stop_line.style;
                L.line_s0 = trim + rules.stop_line.setback_m;
            } else if (rules.give_way.enabled && j.major_arms.size() < j.arms.size() &&
                       std::find(j.major_arms.begin(), j.major_arms.end(), static_cast<int>(i)) == j.major_arms.end()) {
                L.style = select_style(rules.give_way.style, L.ctx);
                if (L.style) {
                    L.give_way = true;
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
        if (rules.zebra.enabled && !ring && cond_matches(rules.zebra.when, L.ctx)) {
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

std::vector<Vec3> arm_ring(const JunctionArm& arm, const std::vector<SL>& pts) {
    std::vector<Vec3> r;
    for (const SL& p : pts) r.push_back(arm_point(arm, p.first, p.second).pos);
    return r;
}

void emit_arm_quad(Builder& b, MarkingCategory cat, const std::string& style, const JunctionArm& arm, double s0, double s1,
                   double l0, double l1) {
    std::vector<Vec3> r = arm_ring(arm, {{s0, l0}, {s1, l0}, {s1, l1}, {s0, l1}});
    b.add(cat, style, r, false);
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
        for (const DashSpan& d : layout_dashes(hi - lo, sd.mark, sd.gap, min_mark))
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
    double a, b;
    const std::string* style;
};

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
    std::map<long long, LinePoly> cache;
    std::vector<EdgeSample> centre;
    bool have_centre = false;
    double lo = 0, hi = 0;
    bool approach_a = false, approach_b = false, junction_a = false, junction_b = false;

    const LinePoly& poly(double lateral) {
        const long long key = std::llround(lateral * 1e6);
        auto it = cache.find(key);
        if (it == cache.end()) it = cache.emplace(key, make_line_poly(e, lateral, sp)).first;
        return it->second;
    }
    const std::vector<EdgeSample>& centreline() {
        if (!have_centre) {
            centre = sample_centreline(e, sp);
            have_centre = true;
        }
        return centre;
    }

    const BoundaryRule* match(const RuleSet& rs, const Lane* l, const Lane* r) const {
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

    std::vector<Interval> zone_intervals(const ZoneRule& z, const LineStyle* zone_style) {
        std::vector<Interval> iv;
        if (z.kind == ZoneRule::Kind::Approach) {
            double d = 0, marks = 0;
            select_value(z.distance_m, ctx, d);
            select_value(z.min_marks, ctx, marks);
            d = std::max(d, marks * (zone_style ? zone_style->module() : 0.0));
            if (d <= 0) return iv;
            if (approach_a) iv.push_back({lo, std::min(lo + d, hi)});
            if (approach_b) iv.push_back({std::max(hi - d, lo), hi});
        } else {
            const std::vector<EdgeSample>& cl = centreline();
            std::vector<Interval> raw;
            for (std::size_t i = 0; i + 1 < cl.size(); ++i)
                if (std::fabs(cl[i].curvature) > z.min_curvature || std::fabs(cl[i + 1].curvature) > z.min_curvature) {
                    if (!raw.empty() && cl[i].s - raw.back().b < 1e-6) raw.back().b = cl[i + 1].s;
                    else raw.push_back({cl[i].s, cl[i + 1].s});
                }
            for (const Interval& r : raw) {
                const Interval c{std::max(r.a, lo), std::min(r.b, hi)};
                if (c.b - c.a >= std::max(z.min_length_m, 1e-3)) iv.push_back(c);
            }
        }
        return iv;
    }

    // Style segments of one boundary rule along [lo, hi].
    std::vector<StyleSeg> segments(const BoundaryRule& br) {
        const std::string* base = select_style(br.style, ctx);
        struct Z {
            std::vector<Interval> iv;
            const std::string* style;
        };
        std::vector<Z> zones;
        std::vector<double> bp{lo, hi};
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

    void emit_line(const BoundaryRule& br, int boundary, double lateral) {
        for (const StyleSeg& sg : segments(br)) {
            if (!sg.style) continue;
            const LineStyle* st = style_of(rules, *sg.style);
            if (!st) continue;
            const double min_mark = st->min_mark > 0 ? st->min_mark : rules.min_mark_m;
            for (const Strand& sd : st->strands) {
                const LinePoly& lp = poly(lateral + sd.offset);
                if (!lp.valid()) continue;
                std::vector<Interval> parts = subtract({{sg.a, sg.b}}, lp.folds);
                if (!lp.folds.empty()) out.warnings.push_back("edge " + std::to_string(e.id.value) + ": marking line folds on a tight curve");
                for (const Interval& iv : parts) {
                    const double l0 = lp.len_of_s(iv.a), l1 = lp.len_of_s(iv.b);
                    for (const DashSpan& d : layout_dashes(l1 - l0, sd.mark, sd.gap, min_mark)) {
                        std::vector<Vec3> c = slice_points(e, lp, l0 + d.s0, l0 + d.s1, params.chord_tolerance);
                        std::vector<Vec3> ring;
                        if (stroke_ring(c, sd.width, ring)) b.add(br.category, *sg.style, ring, true, boundary);
                    }
                }
            }
        }
    }

    void emit_bays(const RuleSet& rs) {
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
                const LinePoly& pt = poly(top);
                const LinePoly& pb = poly(bottom);
                if (!pt.valid() || !pb.valid()) continue;
                for (int i = 0; i <= n; ++i) {
                    const double s = start + i * len;
                    auto pos = [&](const LinePoly& lp, double sv) {
                        const double l = lp.len_of_s(sv);
                        const Vec2 p = lp.xy_at_len(l);
                        return Vec3{p.x, p.y, e.vertical.z(sv)};
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
                                  const JunctionSet& junctions, EdgeId edge, const MarkingParams& params) {
    MarkingSet out;
    const Edge* e = graph.edge(edge);
    if (!e) return out;
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
              Builder{out, rules, edge, NodeId{}}, {}, {}, {}, false, 0, 0, false, false, false, false};
    g.ctx = make_ctx(*e, *rt, g.lanes, region);
    g.sp.max_chord_error = std::max(params.chord_tolerance, 1e-4);

    // Usable range between the arm markings at both ends.
    double clear_a = 0, clear_b = 0;
    for (int end = 0; end < 2; ++end) {
        const bool at_a = end == 0;
        const EndRef ref = find_end(junctions, *e, at_a);
        if (!ref.j) continue;
        const std::vector<ArmLayout> lay = compute_arm_layout(graph, region, rules, *ref.j, params);
        double clear = lay.empty() ? 0 : lay[static_cast<std::size_t>(ref.arm)].clear;
        clear = std::max(clear, trim_of(*ref.j, ref.arm));
        const bool junction = ref.j->arms.size() >= 3;
        const bool approach = ref.j->cls == NodeClass::Junction && junction && !rt->roundabout_ring;
        (at_a ? clear_a : clear_b) = clear;
        (at_a ? g.junction_a : g.junction_b) = junction;
        (at_a ? g.approach_a : g.approach_b) = approach;
    }
    g.lo = clear_a;
    g.hi = length - clear_b;
    if (g.hi - g.lo < rules.min_mark_m) return out;

    // Longitudinal lines at every lane boundary.
    const int n = static_cast<int>(g.lanes.size());
    for (int k = 0; k <= n; ++k) {
        const Lane* left = k > 0 ? &g.lanes[static_cast<std::size_t>(k - 1)] : nullptr;
        const Lane* right = k < n ? &g.lanes[static_cast<std::size_t>(k)] : nullptr;
        const BoundaryRule* br = g.match(*rs, left, right);
        if (!br) continue;
        double lat = right ? right->offset + right->width / 2 : left->offset - left->width / 2;
        const bool left_drives = left && left->dir != TravelDir::None, right_drives = right && right->dir != TravelDir::None;
        if (left_drives && !right_drives) lat += br->inset_m;
        else if (right_drives && !left_drives) lat -= br->inset_m;
        g.emit_line(*br, k, lat);
    }
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

    double surface_z(Vec2 p) const {
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
            emit_transverse(b, rules, cat, *L.style, arm, L.line_s0, lo, hi);
            if (L.give_way && rules.give_way.triangle) triangle(arm, L, lo, hi);
        }
    }

    void triangle(const JunctionArm& arm, const ArmLayout& L, double lo, double hi) {
        const GiveWayRule& g = rules.give_way;
        const double base = L.line_s1 + g.triangle_gap_m, c = (lo + hi) / 2;
        if (arm.edge_length < base + g.triangle_height_m + 1.0) return;
        const double H = g.triangle_height_m, B = g.triangle_base_m;
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
            std::vector<Vec3> ring = arm_ring(arm, {o[t], o[u], in[u], in[t]});
            b.add(MarkingCategory::GiveWayTriangle, g.triangle_style, ring, false);
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
                for (const DashSpan& d : layout_dashes(cs.length, z.stripe_width_m, z.gap_m, min_mark))
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
            const MarkCtx ctx = make_ctx(*e, *rt, instantiate_lanes(*e, *rt, region.driving_side), region);
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
                        std::vector<Vec3> ring = arm_ring(arm, pts);
                        b.add(MarkingCategory::Arrow, ar.style, ring, false);
                    }
                }
            }
        }
    }

    // Diagonal hatching of the gore, clipped to its polygon.
    void gore() {
        const GoreRule& gr = rules.gore;
        if (!gr.enabled) return;
        for (const std::vector<Vec2>& ring : j.gore) {
            if (ring.size() < 3) continue;
            Vec2 c{0, 0};
            for (const Vec2& p : ring) c = c + p;
            c = c * (1.0 / static_cast<double>(ring.size()));
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
                std::vector<Vec2> poly = ring;
                poly = clip_halfplane(poly, nrm, c, t);                       // keep nrm.(p-c) >= t
                poly = clip_halfplane(poly, nrm * -1.0, c, -(t + gr.stripe_width_m));  // keep <= t + w
                if (poly.size() < 3) continue;
                double area = 0;
                for (std::size_t k = 0; k < poly.size(); ++k) area += poly[k].cross(poly[(k + 1) % poly.size()]);
                if (std::fabs(area) / 2 < gr.min_area_m2) continue;
                std::vector<Vec3> r;
                for (const Vec2& p : poly) r.push_back({p.x, p.y, surface_z(p)});
                b.add(MarkingCategory::Hatching, gr.style, r, false);
            }
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
            std::vector<Vec3> ring;
            if (stroke_ring(pts, st->strands[0].width, ring)) b.add(MarkingCategory::EdgeLine, *sid, ring, true);
        }
    }

    MarkCtx arm_ctx(std::size_t i) const {
        const Edge* e = graph.edge(j.arms[i].edge);
        const RoadType* rt = e ? region.road_type(e->road_type) : nullptr;
        if (!rt) return {};
        return make_ctx(*e, *rt, instantiate_lanes(*e, *rt, region.driving_side), region);
    }
};

}  // namespace

MarkingSet generate_junction_markings(const RoadGraph& graph, const RegionPack& region, const MarkingRules& rules,
                                      const Junction& junction, const MarkingParams& params) {
    MarkingSet out;
    if (junction.cls == NodeClass::End || junction.cls == NodeClass::Joint) return out;
    if (!junction.trims.empty() && junction.trims.size() != junction.arms.size()) {
        out.warnings.push_back("junction " + std::to_string(junction.node.value) + ": trims do not match arms");
        return out;
    }
    JGen g{graph, region, rules, junction, params, Builder{out, rules, EdgeId{}, junction.node}, {}};
    g.layout = compute_arm_layout(graph, region, rules, junction, params);
    g.give_way_and_stop();
    g.zebras();
    g.arrows();
    g.gore();
    g.slip_edges();
    return out;
}

// ================================================================ aggregate

MarkingSet MarkingResult::merged() const {
    MarkingSet out;
    for (const auto& [id, s] : edges) out.append(s);
    for (const auto& [id, s] : junctions) out.append(s);
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
}

}  // namespace cp
