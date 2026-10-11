// No exceptions: sim_core builds with -fno-exceptions.
// Port of the drawing interaction in prototypes/road-draw/src/input.js; function by function:
//   rayTerrain / pick            -> pick_ground (cp/ground_pick.h) + DrawSession::resolve
//   roadEnds / nearestOnRoad     -> RoadIndex (cp/road_index.h)
//   computeSnap                  -> DrawSession::compute_snap
//   buildPreview                 -> DrawSession::build_preview
//   startOrAddPoint              -> DrawSession::click
//   planHits / conformRoad       -> cp/stroke.h (crossings, join / refuse / clear)
//   finishRoad                   -> DrawSession::finish_with
//   Backspace / Escape / PgUp    -> undo_point / cancel / adjust_height
#include "cp/road_tool.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>

#include "cp/edge_sampling.h"
#include "cp/grading.h"
#include "cp/lanes.h"

namespace cp {

namespace {

constexpr double kInf = std::numeric_limits<double>::infinity();

struct TypeDims {
    const RoadType* rt = nullptr;
    double total = 0;  // m, full width of what one edge of this type represents (a dual type: one carriageway)
    bool dual = false;
    double gap = 0;    // m, median gap (dual types)
};

TypeDims dims_of(const RoadGraph& g, const std::string& type, bool one_way) {
    TypeDims d;
    if (!g.region()) return d;
    d.rt = g.region()->road_type(type);
    if (!d.rt) return d;
    Edge e;
    e.road_type = type;
    e.one_way = one_way;
    d.total = cross_section_width(instantiate_lanes(e, *d.rt, g.region()->driving_side));
    d.dual = d.rt->dual;
    d.gap = d.rt->median_gap;
    return d;
}

std::string fmt(const char* f, double a, double b = 0, double c = 0) {
    char buf[200];
    std::snprintf(buf, sizeof buf, f, a, b, c);
    return buf;
}

bool fit_edge(const RoadGraph& g, const std::string& type, const std::vector<Vec3>& pts, std::optional<double> hs,
              std::optional<double> he, Edge& out) {
    std::vector<Vec2> xy;
    for (const Vec3& p : pts) xy.push_back(p.xy());
    auto chain = fit_clothoid_chain(xy, {hs}, {he});
    if (!chain) return false;
    std::vector<double> s{0}, z{pts.front().z};
    for (std::size_t i = 0; i < chain->spans.size(); ++i) {
        s.push_back(s.back() + chain->spans[i].length);
        z.push_back(pts[i + 1].z);
    }
    out = Edge{};
    out.road_type = type;
    out.heading_a = hs;
    out.heading_b = he;
    out.horizontal = std::move(*chain);
    out.vertical = VerticalProfile::build(s, z, g.curve_params_for(out));
    return true;
}

std::vector<Vec3> drape(const std::vector<Vec2>& poly, const Heightfield& hf, double step, double lift) {
    std::vector<Vec3> out;
    if (poly.empty()) return out;
    for (std::size_t i = 0; i + 1 < poly.size(); ++i) {
        const double l = (poly[i + 1] - poly[i]).length();
        const int n = std::max(1, static_cast<int>(std::ceil(l / step)));
        for (int k = 0; k < n; ++k) {
            const Vec2 p = poly[i] + (poly[i + 1] - poly[i]) * (static_cast<double>(k) / n);
            out.push_back({p.x, p.y, hf.height(p.x, p.y) + lift});
        }
    }
    const Vec2 e = poly.back();
    out.push_back({e.x, e.y, hf.height(e.x, e.y) + lift});
    return out;
}

}  // namespace

struct DrawSession::Snap {
    double x = 0, y = 0;
    std::optional<double> z;       // fixed height (snapped road / surface), else ground + height offset
    SnapKind kind = SnapKind::Free;
    SnapGuide guide;
    EdgeId edge;
    NodeId node;
    std::optional<double> out;     // End: heading pointing away from the road (JS `out`)
};

struct DrawSession::Resolved {
    bool has_pick = false;
    Snap sn;
    DrawPreview pv;
};

DrawSession::DrawSession(const RoadGraph& graph, const Heightfield& ground, DrawParams params)
    : graph_(graph), ground_(ground), params_(params) {
    if (graph.region()) {
        // Default to the first road type of the pack, so a fresh session is usable.
        if (!graph.region()->road_types.empty()) type_ = graph.region()->road_types.begin()->first;
    }
}

void DrawSession::ensure_index() {
    const std::uint64_t last = graph_.edges().empty() ? 0 : graph_.edges().rbegin()->first.value;
    if (!index_valid_ || fp_nodes_ != graph_.nodes().size() || fp_edges_ != graph_.edges().size() || fp_last_edge_ != last) {
        index_ = RoadIndex(graph_);
        index_valid_ = true;
        fp_nodes_ = graph_.nodes().size();
        fp_edges_ = graph_.edges().size();
        fp_last_edge_ = last;
    }
}

DrawSession::Snap DrawSession::compute_snap(const Vec3& hit, double px, bool on_surface, Modifiers mods) {
    const double mx = hit.x, my = hit.y, hz = hit.z;
    const Vec2 m{mx, my};
    const bool has_last = !pts_.empty();
    const Vec3 last = has_last ? pts_.back() : Vec3{};
    Snap res;
    res.x = mx;
    res.y = my;
    if (!mods.snap_off) {
        // 1. a road end / junction node
        if (auto nd = index_.nearest_node(m, params_.end_pick_px * px, hz, params_.level_tol)) {
            const bool is_last = has_last && (nd->pos.xy() - last.xy()).length() < params_.repeat_tol;
            if (!is_last) {
                Snap s;
                s.x = nd->pos.x;
                s.y = nd->pos.y;
                s.z = nd->pos.z;
                s.node = nd->node;
                if (nd->degree == 1) {
                    if (auto e = index_.nearest_free_end(m, nd->distance + 1e-6, hz, params_.level_tol)) {
                        s.kind = SnapKind::End;
                        s.out = e->out_heading;
                        s.edge = e->edge;
                        return s;
                    }
                }
                s.kind = SnapKind::Node;
                return s;
            }
        }
        // 2. on a road
        if (auto on = index_.nearest_point(m, params_.road_pick_px * px, hz, params_.level_tol)) {
            Snap s;
            s.x = on->point.x;
            s.y = on->point.y;
            s.z = on->point.z;
            s.kind = SnapKind::Road;
            s.edge = on->edge;
            return s;
        }
        // 3. extension of a free road end
        struct Ext {
            double dd = kInf, x = 0, y = 0, x0 = 0, y0 = 0;
            bool found = false;
        } bg;
        for (const EndHit& e : index_.free_ends()) {
            const double ux = std::cos(e.out_heading), uy = std::sin(e.out_heading);
            const double t = (mx - e.pos.x) * ux + (my - e.pos.y) * uy;
            if (t <= params_.extension_min) continue;
            const double qx = e.pos.x + ux * t, qy = e.pos.y + uy * t, dd = std::hypot(mx - qx, my - qy);
            if (dd < params_.extension_pick_px * px && dd < bg.dd) bg = {dd, qx, qy, e.pos.x, e.pos.y, true};
        }
        if (bg.found) {
            res.x = bg.x;
            res.y = bg.y;
            res.kind = SnapKind::Guide;
            res.guide.type = SnapGuide::Type::Line;
            res.guide.polyline = drape({{bg.x0, bg.y0}, {bg.x, bg.y}}, ground_, params_.guide_drape_step, params_.guide_lift);
        } else if (auto near = index_.nearest_point(m, params_.offset_search, hz, params_.level_tol)) {
            // 4. parallel to a nearby road, at a multiple of the lateral spacing that puts the two cross-sections side by side
            const Edge* ne = index_.edge(near->edge);
            const TypeDims A = ne ? dims_of(graph_, ne->road_type, ne->one_way) : TypeDims{};
            const TypeDims B = dims_of(graph_, type_, one_way_);
            if (A.rt && B.rt) {
                const bool single = B.dual && one_way_;  // one carriageway at a time: centre-to-centre = median gap + carriageway width
                const double base = single ? A.total / 2 + (A.dual ? A.gap / 2 : 0) + B.total / 2 + B.gap / 2
                                           : A.total / 2 + (B.dual ? B.total + B.gap / 2 : B.total / 2);
                for (int k = 1; k <= params_.offset_multiples; ++k) {
                    const double off = base * k;
                    if (std::abs(std::abs(near->lateral) - off) < params_.offset_pick_px * px) {
                        const double nx = -std::sin(near->heading), ny = std::cos(near->heading);
                        const double sg = near->lateral >= 0 ? 1.0 : -1.0;
                        res.x = near->point.x + nx * off * sg;
                        res.y = near->point.y + ny * off * sg;
                        res.kind = SnapKind::Guide;
                        res.guide.type = SnapGuide::Type::Offset;
                        res.guide.edge = near->edge;
                        res.guide.offset = off * sg;
                        std::vector<Vec2> line;
                        for (const EdgeSample& es : sample_offset(*ne, off * sg).samples) line.push_back(es.pos.xy());
                        res.guide.polyline = drape(line, ground_, params_.guide_drape_step, params_.guide_lift);
                        break;
                    }
                }
            }
        }
        // 5. angle / length grid from the last point
        if (res.kind == SnapKind::Free && has_last) {
            double base = 0;
            if (pts_.size() >= 2) {
                const Vec3& p = pts_[pts_.size() - 2];
                base = std::atan2(last.y - p.y, last.x - p.x);
            } else if (hs_) {
                base = *hs_;
            }
            const double step = params_.snap_angle_deg * kPi / 180;
            const double ang = base + std::floor(wrap_angle(std::atan2(my - last.y, mx - last.x) - base) / step + 0.5) * step;
            const double len = std::max(params_.snap_length,
                                        std::floor(std::hypot(mx - last.x, my - last.y) / params_.snap_length + 0.5) * params_.snap_length);
            res.x = last.x + std::cos(ang) * len;
            res.y = last.y + std::sin(ang) * len;
            res.kind = SnapKind::Angle;
            res.guide.type = SnapGuide::Type::Line;
            res.guide.faint = true;
            res.guide.polyline = drape({{last.x, last.y}, {res.x, res.y}}, ground_, params_.guide_drape_step, params_.guide_lift);
        }
    }
    if (res.kind == SnapKind::Free && on_surface) res.z = hz;  // clicked onto a bridge deck / road surface
    return res;
}

void DrawSession::build_preview(DrawPreview& pv, const Snap& sn) {
    const RoadGraph& g = graph_;
    pv.cursor.x = sn.x;
    pv.cursor.y = sn.y;
    // A snapped road end / junction / road keeps that height; everything else sits on the ground (or the deck
    // it was picked on) plus the PgUp / PgDn offset.
    const bool fixed = sn.kind == SnapKind::End || sn.kind == SnapKind::Node || sn.kind == SnapKind::Road;
    pv.cursor.z = fixed ? *sn.z : (sn.z ? *sn.z : ground_z(sn.x, sn.y)) + dz_;
    pv.snap = sn.kind;
    pv.guide = sn.guide;
    pv.snap_edge = sn.edge;
    pv.snap_node = sn.node;
    pv.clicked = pts_;
    pv.readouts.height = pv.cursor.z;
    if (pts_.empty()) return;
    const TypeDims ty = dims_of(g, type_, one_way_);
    if (!ty.rt) {
        pv.errors.push_back({IssueCode::NoRegion, g.region() ? "unknown road type " + type_ : "no region pack"});
        return;
    }
    std::vector<Vec3> pts = pts_;
    pts.push_back(pv.cursor);
    const std::optional<double> he = sn.kind == SnapKind::End && sn.out ? std::optional<double>(*sn.out + kPi) : std::nullopt;

    for (std::size_t i = 0; i + 1 < pts.size(); ++i)
        if ((pts[i + 1].xy() - pts[i].xy()).length() < params_.min_span) {
            pv.errors.push_back({IssueCode::MinSpan, fmt("segment shorter than %.0f m", params_.min_span)});
            return;
        }
    Edge edge;
    if (!fit_edge(g, type_, pts, hs_, he, edge)) {
        pv.errors.push_back({IssueCode::CannotFit, "cannot fit curve"});
        return;
    }
    const bool dual_pair = ty.dual && !one_way_;
    const double half_w = dual_pair ? ty.gap / 2 + ty.total : ty.total / 2;
    const std::vector<EdgeSample> samples = sample_centreline(edge);
    double kmax = 0, gmax = 0;
    for (const EdgeSample& s : samples) {
        kmax = std::max(kmax, std::abs(s.curvature));
        gmax = std::max(gmax, std::abs(s.grade));
    }
    const double min_r = kmax > 1e-9 ? 1 / kmax : kInf;
    const EdgeSample& tail = samples.back();
    const double span_len = edge.horizontal.spans.back().length;
    Readouts& ro = pv.readouts;
    ro.length = edge.horizontal.length();
    ro.span_length = span_len;
    ro.min_radius = min_r;
    ro.end_radius = std::abs(tail.curvature) > 1e-9 ? 1 / std::abs(tail.curvature) : kInf;
    ro.max_grade_pct = gmax * 100;
    ro.grade_pct = (pts.back().z - pts[pts.size() - 2].z) / span_len * 100;
    ro.heading_deg = std::fmod(std::fmod(90 - tail.heading * 180 / kPi, 360) + 360, 360);

    if (!find_folds(edge, half_w).empty() || !find_folds(edge, -half_w).empty())
        pv.errors.push_back({IssueCode::Fold, fmt("too tight: radius %.1f m < half-width %.1f m (edge folds)", min_r, half_w)});

    const RoadType& rt = *ty.rt;
    if (rt.min_radius > 0 && min_r < rt.min_radius)
        pv.advisories.push_back({IssueCode::RadiusBelowMinimum, fmt("radius %.0f m below comfortable %.0f m", min_r, rt.min_radius) + " for " + rt.name});
    if (rt.advisory_max_grade > 0 && gmax > rt.advisory_max_grade)
        pv.advisories.push_back({IssueCode::GradeAboveAdvisory, fmt("grade %.1f%% above advisory %.1f%%", gmax * 100, rt.advisory_max_grade * 100)});
    if (g.region()) {
        const GradingParams gp = GradingParams::from_rules(g.region()->grading);
        const SampledRoad sr = sample_edge_for_grading(edge, type_, half_w, dual_pair, ty.gap, gp);
        bool br = false, tu = false;
        for (const StructureRange& r : required_structures(sr, ground_fn(ground_), gp)) (r.kind == StructureKind::Bridge ? br : tu) = true;
        if (br) pv.advisories.push_back({IssueCode::BridgeNeeded, fmt("fill over %.0f m wall cap: becomes a bridge", gp.wall_cap)});
        if (tu) pv.advisories.push_back({IssueCode::TunnelNeeded, fmt("cut over %.0f m wall cap: becomes a tunnel", gp.wall_cap)});
    }
    if (!edge.horizontal.g2) pv.advisories.push_back({IssueCode::G1Fallback, "G2 solve fell back to G1 (curvature kink)"});

    // Ribbons: one polyline per carriageway, split at the last clicked point.
    const double s_last = edge.horizontal.length() - span_len;
    std::vector<double> offsets{0.0};
    if (dual_pair) offsets = {ty.gap / 2 + ty.total / 2, -(ty.gap / 2 + ty.total / 2)};
    for (const double off : offsets) {
        Ribbon rb;
        rb.width = ty.total;
        for (const EdgeSample& s : sample_offset(edge, off).samples) {
            if (s.s <= s_last + 1e-9) rb.committed.push_back(s.pos);
            if (s.s >= s_last - 1e-9) rb.hover.push_back(s.pos);
        }
        pv.ribbons.push_back(std::move(rb));
    }
}

DrawSession::Resolved DrawSession::resolve(const PointerInput& in, Modifiers mods) {
    Resolved r;
    ensure_index();
    GroundPickParams gpp;
    gpp.max_distance = params_.pick_max_distance;
    const auto ground_hit = pick_ground(ground_, in.ray, gpp);
    Vec3 hit;
    double dist = 0;
    bool on_surface = false;
    const double dlen = std::sqrt(in.ray.dir.x * in.ray.dir.x + in.ray.dir.y * in.ray.dir.y + in.ray.dir.z * in.ray.dir.z);
    if (ground_hit) {
        hit = ground_hit->point;
        dist = ground_hit->distance;
    }
    if (in.surface_hit && dlen > 0) {
        const Vec3 sh = *in.surface_hit;
        const double sd = std::sqrt((sh.x - in.ray.origin.x) * (sh.x - in.ray.origin.x) + (sh.y - in.ray.origin.y) * (sh.y - in.ray.origin.y) +
                                    (sh.z - in.ray.origin.z) * (sh.z - in.ray.origin.z));
        if (!ground_hit || sd < ground_hit->distance + 0.3) {  // JS: hits[0].distance < tt + 0.3
            hit = sh;
            dist = sd;
            on_surface = true;
        }
    }
    if (!ground_hit && !on_surface) return r;
    r.has_pick = true;
    r.pv.has_pick = true;
    const double px = in.metres_per_pixel > 0 ? in.metres_per_pixel : in.metres_per_pixel_per_metre * dist;
    if (!pts_.empty() && (hit.xy() - pts_.back().xy()).length() < params_.finish_pick_px * px) {
        r.pv.would_finish = true;  // on the last placed point: a click ends the stroke
        r.pv.cursor = pts_.back();
        r.pv.clicked = pts_;
        r.pv.readouts.height = pts_.back().z;
        return r;
    }
    r.sn = compute_snap(hit, px, on_surface, mods);
    build_preview(r.pv, r.sn);
    return r;
}

DrawPreview DrawSession::hover(const PointerInput& in, Modifiers mods) { return resolve(in, mods).pv; }

ClickResult DrawSession::click(const PointerInput& in, Modifiers mods) {
    ClickResult out;
    Resolved r = resolve(in, mods);
    if (!r.has_pick) return out;
    if (r.pv.would_finish) {
        out.finish = finish();
        out.finished = out.finish.ok();
        return out;
    }
    if (!r.pv.errors.empty()) {
        out.errors = r.pv.errors;
        return out;
    }
    if (pts_.empty()) {
        pts_.push_back(r.pv.cursor);
        hs_ = r.sn.kind == SnapKind::End ? r.sn.out : std::nullopt;
        dz_ = 0;
        out.added = true;
        return out;
    }
    pts_.push_back(r.pv.cursor);
    out.added = true;
    const SnapKind k = r.sn.kind;
    if (k == SnapKind::End || k == SnapKind::Node || k == SnapKind::Road) {
        const std::optional<double> he = k == SnapKind::End && r.sn.out ? std::optional<double>(*r.sn.out + kPi) : std::nullopt;
        out.finish = finish_with(pts_, hs_, he);
        if (out.finish.ok()) {
            out.auto_finished = true;
            out.finished = true;
            pts_.clear();
            hs_.reset();
        } else {
            pts_.pop_back();  // refused: keep drawing from the previous point
            out.added = false;
        }
    }
    dz_ = 0;
    return out;
}

FinishResult DrawSession::finish() {
    if (pts_.size() < 2) {
        FinishResult r;
        r.error = "a road needs at least two points";
        return r;
    }
    FinishResult r = finish_with(pts_, hs_, std::nullopt);
    if (r.ok()) {
        pts_.clear();
        hs_.reset();
        dz_ = 0;
    }
    return r;
}

void DrawSession::cancel() {
    pts_.clear();
    hs_.reset();
    dz_ = 0;
}

bool DrawSession::undo_point() {
    if (pts_.empty()) return false;
    pts_.pop_back();
    if (pts_.empty()) hs_.reset();
    return true;
}

FinishResult DrawSession::finish_with(const std::vector<Vec3>& pts, std::optional<double> hs, std::optional<double> he) {
    FinishResult out;
    const TypeDims ty = dims_of(graph_, type_, one_way_);
    if (!ty.rt) {
        out.error = graph_.region() ? "unknown road type " + type_ : "no region pack";
        return out;
    }
    for (std::size_t i = 0; i + 1 < pts.size(); ++i)
        if ((pts[i + 1].xy() - pts[i].xy()).length() < params_.min_span) {
            out.error = fmt("segment shorter than %.0f m", params_.min_span);
            return out;
        }
    Edge centre;
    if (!fit_edge(graph_, type_, pts, hs, he, centre)) {
        out.error = "cannot fit curve";
        return out;
    }
    StrokeParams sp;
    sp.follow_curve = true;
    sp.refuse_band = true;
    if (!(ty.dual && !one_way_)) {
        StrokeSpec spec{pts, hs, he, type_, one_way_};
        out.error = check_stroke(graph_, spec, sp);
        if (out.error.empty()) out.command = make_draw_stroke(std::move(spec), sp);
        return out;
    }
    // Dual carriageway: two one-way strokes either side of the drawn centreline, on the region's driving side.
    const double off = ty.gap / 2 + ty.total / 2;
    const double side_sign = graph_.region()->driving_side == DrivingSide::Left ? 1.0 : -1.0;
    std::vector<double> sarc{0};
    for (const Clothoid& c : centre.horizontal.spans) sarc.push_back(sarc.back() + c.length);
    auto off_pts = [&](double sg) {
        std::vector<Vec3> v;
        for (std::size_t i = 0; i < pts.size(); ++i) {
            const double h = edge_heading(centre, sarc[i]);
            v.push_back({pts[i].x - std::sin(h) * off * sg, pts[i].y + std::cos(h) * off * sg, pts[i].z});
        }
        return v;
    };
    StrokeSpec fwd{off_pts(side_sign), hs, he, type_, true};
    std::vector<Vec3> bp = off_pts(-side_sign);
    std::reverse(bp.begin(), bp.end());
    StrokeSpec back{std::move(bp), he ? std::optional<double>(wrap_angle(*he + kPi)) : std::nullopt,
                    hs ? std::optional<double>(wrap_angle(*hs + kPi)) : std::nullopt, type_, true};
    for (const StrokeSpec* s : {&fwd, &back}) {
        out.error = check_stroke(graph_, *s, sp);
        if (!out.error.empty()) return out;
    }
    auto both = std::make_unique<CompositeCommand<RoadGraph>>("draw dual carriageway");
    both->add(make_draw_stroke(std::move(fwd), sp));
    both->add(make_draw_stroke(std::move(back), sp));
    out.command = std::move(both);
    return out;
}

}  // namespace cp
