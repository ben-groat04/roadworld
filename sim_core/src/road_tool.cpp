// No exceptions: sim_core builds with -fno-exceptions.
// Port of the drawing interaction in prototypes/road-draw/src/input.js; function by function:
//   rayTerrain / pick            -> pick_ground (cp/ground_pick.h) + DrawSession::resolve
//   roadEnds / nearestOnRoad     -> RoadIndex (cp/road_index.h)
//   computeSnap                  -> DrawSession::compute_snap
//   buildPreview                 -> DrawSession::build_preview / build_geometry (+ analyse_edge)
//   renderProfile (data)         -> ProfileSeries (analyse_edge, profile_of_edge)
//   startOrAddPoint              -> DrawSession::click
//   planHits / conformRoad       -> cp/stroke.h (crossings, join / refuse / clear)
//   finishRoad                   -> DrawSession::finish_with (finish / finish_at_cursor / click)
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
    double total = 0;     // m, full width of what one edge of this type represents (a dual type: one carriageway)
    double carriage = 0;  // m, the part of it with the "carriageway" lane profile
    bool dual = false;
    double gap = 0;       // m, median gap (dual types)
};

TypeDims dims_of(const RoadGraph& g, const std::string& type, bool one_way) {
    TypeDims d;
    if (!g.region()) return d;
    d.rt = g.region()->road_type(type);
    if (!d.rt) return d;
    Edge e;
    e.road_type = type;
    e.one_way = one_way;
    const std::vector<Lane> lanes = instantiate_lanes(e, *d.rt, g.region()->driving_side);
    d.total = cross_section_width(lanes);
    for (const Lane& l : lanes) {
        const LaneType* lt = g.region()->lane_type(l.lane_type);
        if (lt && lt->profile == "carriageway") d.carriage += l.width;
    }
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
    auto chain = fit_sane_chain(xy, hs, he);
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

bool same_points(const std::vector<Vec3>& a, const std::vector<Vec3>& b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i)
        if (std::abs(a[i].x - b[i].x) > 1e-6 || std::abs(a[i].y - b[i].y) > 1e-6 || std::abs(a[i].z - b[i].z) > 1e-6) return false;
    return true;
}

// The strokes a drawn centreline commits: one for a single road, two one-way strokes either side of the drawn
// centreline for a dual type (JS finishRoad: fwd / back, offset gap / 2 + total / 2, on the region's driving side).
// `centre` is the fitted centreline through pts (its span lengths give the stations of the offset points).
std::vector<StrokeSpec> stroke_specs(const RoadGraph& g, const TypeDims& ty, const Edge& centre, const std::vector<Vec3>& pts,
                                     std::optional<double> hs, std::optional<double> he, const std::string& type, bool one_way) {
    std::vector<StrokeSpec> out;
    if (!(ty.dual && !one_way)) {
        out.push_back(StrokeSpec{pts, hs, he, type, one_way});
        return out;
    }
    const double off = ty.gap / 2 + ty.total / 2;
    const double side_sign = g.region()->driving_side == DrivingSide::Left ? 1.0 : -1.0;
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
    out.push_back(StrokeSpec{off_pts(side_sign), hs, he, type, true});
    std::vector<Vec3> bp = off_pts(-side_sign);
    std::reverse(bp.begin(), bp.end());
    out.push_back(StrokeSpec{std::move(bp), he ? std::optional<double>(wrap_angle(*he + kPi)) : std::nullopt,
                             hs ? std::optional<double>(wrap_angle(*hs + kPi)) : std::nullopt, type, true});
    return out;
}

ProfileSeries build_profile(const Edge& edge, const RoadType* rt, double wall_cap, const Heightfield& ground, double step,
                            const std::vector<ProfileStructure>& req) {
    ProfileSeries ps;
    const double L = edge.horizontal.length();
    if (!(L > 0) || edge.vertical.breaks.size() < 2) return ps;
    step = std::max(step, 0.25);
    ps.length = L;
    ps.step = step;
    ps.wall_cap = wall_cap;
    ps.max_grade_pct = rt ? rt->advisory_max_grade * 100 : 0;
    ps.structures = req;
    ps.authored_structures = edge.structure;

    std::vector<double> ss;
    const int n = std::max(1, static_cast<int>(std::ceil(L / step)));
    for (int i = 0; i <= n; ++i) ss.push_back(std::min(L, i * (L / n)));  // evenly divided: never a sliver at the end
    for (const VerticalProfile::Break& b : edge.vertical.breaks) {
        ss.push_back(std::clamp(b.s, 0.0, L));
        if (b.length > 0) {
            ss.push_back(std::clamp(b.s - b.length / 2, 0.0, L));
            ss.push_back(std::clamp(b.s + b.length / 2, 0.0, L));
        }
    }
    std::sort(ss.begin(), ss.end());
    ss.erase(std::unique(ss.begin(), ss.end(), [](double a, double b) { return b - a < 1e-6; }), ss.end());

    ps.z_min = kInf;
    ps.z_max = -kInf;
    ps.stations.reserve(ss.size());
    for (const double s : ss) {
        const Vec3 p = edge_point(edge, s);
        ProfileStation st;
        st.s = s;
        st.road_z = p.z;
        st.ground_z = ground.height(p.x, p.y);
        st.depth = st.road_z - st.ground_z;
        st.grade_pct = edge.vertical.grade(s) * 100;
        ps.z_min = std::min({ps.z_min, st.road_z, st.ground_z});
        ps.z_max = std::max({ps.z_max, st.road_z, st.ground_z});
        ps.stations.push_back(st);
    }
    const auto& br = edge.vertical.breaks;
    for (std::size_t i = 0; i < br.size(); ++i) {
        ProfileCurve c;
        c.s = br[i].s;
        c.z = br[i].z;
        c.length = br[i].length;
        c.s0 = br[i].s - br[i].length / 2;
        c.s1 = br[i].s + br[i].length / 2;
        ps.curves.push_back(c);
        if (i + 1 < br.size() && br[i + 1].s > br[i].s) {
            ProfileGrade g;
            g.s0 = br[i].s;
            g.s1 = br[i + 1].s;
            g.z0 = br[i].z;
            g.z1 = br[i + 1].z;
            g.s_mid = (g.s0 + g.s1) / 2;
            g.grade_pct = (g.z1 - g.z0) / (g.s1 - g.s0) * 100;
            char buf[32];
            std::snprintf(buf, sizeof buf, "%+.1f%%", g.grade_pct);
            g.label = buf;
            g.over_limit = ps.max_grade_pct > 0 && std::abs(g.grade_pct) > ps.max_grade_pct;
            ps.grades.push_back(std::move(g));
        }
    }
    ps.valid = true;
    return ps;
}

std::vector<ProfileStructure> to_profile_structures(const SampledRoad& sr, const std::vector<StructureRange>& ranges) {
    std::vector<ProfileStructure> out;
    for (const StructureRange& r : ranges) {
        if (r.first < 0 || r.last < r.first || r.last >= static_cast<int>(sr.samples.size())) continue;
        out.push_back({sr.samples[static_cast<std::size_t>(r.first)].s, sr.samples[static_cast<std::size_t>(r.last)].s, r.kind});
    }
    return out;
}

}  // namespace

RoadWidths road_widths(const RoadGraph& g, const std::string& road_type, bool one_way) {
    const TypeDims d = dims_of(g, road_type, one_way);
    RoadWidths w;
    w.known = d.rt != nullptr;
    w.total = d.total;
    w.carriageway = d.carriage;
    return w;
}

EdgeAnalysis analyse_edge(const RoadGraph& g, const Heightfield& ground, const Edge& edge, const AnalysisOptions& opt) {
    EdgeAnalysis out;
    const TypeDims ty = dims_of(g, opt.road_type, opt.one_way);
    if (!ty.rt) {
        out.errors.push_back({IssueCode::NoRegion, g.region() ? "unknown road type " + opt.road_type : "no region pack"});
        return out;
    }
    const double half_w = opt.dual_pair ? ty.gap / 2 + ty.total : ty.total / 2;
    const std::vector<EdgeSample> samples = sample_centreline(edge);
    if (samples.empty() || edge.horizontal.spans.empty()) {
        out.errors.push_back({IssueCode::CannotFit, "cannot fit curve"});
        return out;
    }
    double kmax = 0, gmax = 0;
    for (const EdgeSample& s : samples) {
        kmax = std::max(kmax, std::abs(s.curvature));
        gmax = std::max(gmax, std::abs(s.grade));
    }
    const double min_r = kmax > 1e-9 ? 1 / kmax : kInf;
    const EdgeSample& tail = samples.back();
    const double span_len = edge.horizontal.spans.back().length;
    Readouts& ro = out.readouts;
    ro.length = edge.horizontal.length();
    ro.span_length = span_len;
    ro.min_radius = min_r;
    ro.end_radius = std::abs(tail.curvature) > 1e-9 ? 1 / std::abs(tail.curvature) : kInf;
    ro.max_grade_pct = gmax * 100;
    const auto& br = edge.vertical.breaks;
    if (br.size() >= 2 && br.back().s > br[br.size() - 2].s)
        ro.grade_pct = (br.back().z - br[br.size() - 2].z) / (br.back().s - br[br.size() - 2].s) * 100;
    ro.height = br.empty() ? 0 : br.back().z;
    ro.heading_deg = std::fmod(std::fmod(90 - tail.heading * 180 / kPi, 360) + 360, 360);
    ro.g2 = edge.horizontal.g2;

    if (!find_folds(edge, half_w).empty() || !find_folds(edge, -half_w).empty())
        out.errors.push_back({IssueCode::Fold, fmt("too tight: radius %.1f m < half-width %.1f m (edge folds)", min_r, half_w)});

    const RoadType& rt = *ty.rt;
    if (rt.min_radius > 0 && min_r < rt.min_radius)
        out.advisories.push_back({IssueCode::RadiusBelowMinimum, fmt("radius %.0f m below comfortable %.0f m", min_r, rt.min_radius) + " for " + rt.name});
    if (rt.advisory_max_grade > 0 && gmax > rt.advisory_max_grade)
        out.advisories.push_back({IssueCode::GradeAboveAdvisory, fmt("grade %.1f%% above advisory %.1f%%", gmax * 100, rt.advisory_max_grade * 100)});
    std::vector<ProfileStructure> required;
    double wall_cap = 0;
    if (g.region()) {
        const GradingParams gp = GradingParams::from_rules(g.region()->grading);
        wall_cap = gp.wall_cap;
        const SampledRoad sr = sample_edge_for_grading(edge, opt.road_type, half_w, opt.dual_pair, ty.gap, gp);
        const std::vector<StructureRange> ranges = required_structures(sr, ground_fn(ground), gp);
        bool bridge = false, tunnel = false;
        for (const StructureRange& r : ranges) (r.kind == StructureKind::Bridge ? bridge : tunnel) = true;
        if (bridge) out.advisories.push_back({IssueCode::BridgeNeeded, fmt("fill over %.0f m wall cap: becomes a bridge", gp.wall_cap)});
        if (tunnel) out.advisories.push_back({IssueCode::TunnelNeeded, fmt("cut over %.0f m wall cap: becomes a tunnel", gp.wall_cap)});
        required = to_profile_structures(sr, ranges);
    }
    if (!edge.horizontal.g2) out.advisories.push_back({IssueCode::G1Fallback, "G2 solve fell back to G1 (curvature kink)"});

    if (opt.want_profile) out.profile = build_profile(edge, &rt, wall_cap, ground, opt.profile_step, required);

    // Crossings with existing roads, one test per carriageway of the stroke.
    StrokeParams sp;
    sp.follow_curve = true;
    sp.refuse_band = true;
    sp.ignore_edges = opt.ignore_edges;
    for (std::size_t i = 0; i < opt.crossing_specs.size(); ++i) {
        const StrokeSpec& spec = opt.crossing_specs[i];
        if (opt.crossing_specs.size() > 1) {  // an offset carriageway can fail to fit where the centreline does not
            std::vector<Vec2> xy;
            for (const Vec3& p : spec.points) xy.push_back(p.xy());
            if (!fit_sane_chain(xy, spec.heading_a, spec.heading_b)) {
                out.errors.push_back({IssueCode::CannotFit, "cannot fit curve"});
                continue;
            }
        }
        const std::vector<StrokeCrossing> found = opt.cross_cache ? stroke_crossings(g, *opt.cross_cache, spec, sp) : stroke_crossings(g, spec, sp);
        for (const StrokeCrossing& c : found) {
            CrossingReport r;
            r.pos = c.pos;
            r.edge = c.edge;
            r.edge_s = c.edge_s;
            r.existing_z = c.existing_z;
            r.level_difference = c.dz;
            r.verdict = c.verdict;
            r.join_limit = c.join_limit;
            r.clearance = c.headroom;
            r.carriageway = opt.crossing_specs.size() > 1 ? static_cast<int>(i) + 1 : 0;
            if (c.verdict == CrossingVerdict::Refused) {
                r.text = crossing_refusal_text(c.dz, c.headroom);
                const bool dup = std::any_of(out.errors.begin(), out.errors.end(), [&](const Issue& e) { return e.text == r.text; });
                if (!dup) out.errors.push_back({IssueCode::Crossing, r.text});
            } else if (c.verdict == CrossingVerdict::Join && opt.edit_mode) {
                out.advisories.push_back({IssueCode::CrossingAtGrade,
                                          fmt("crosses a road at the same level (%.1f m apart) without a junction", c.dz)});
            }
            out.crossings.push_back(std::move(r));
        }
    }
    return out;
}

ProfileSeries profile_of_edge(const RoadGraph& g, const Heightfield& ground, EdgeId id, double step) {
    const Edge* e = g.edge(id);
    if (!e || !g.region()) return {};
    const RoadType* rt = g.region()->road_type(e->road_type);
    if (!rt) return {};
    const GradingParams gp = GradingParams::from_rules(g.region()->grading);
    std::vector<Lane> lanes = instantiate_lanes(*e, *rt, g.region()->driving_side);
    const double hw = cross_section_width(lanes) / 2;
    const SampledRoad sr = sample_edge_for_grading(*e, rt->id, hw, rt->dual, rt->median_gap, gp);
    const std::vector<StructureRange> ranges = required_structures(sr, ground_fn(ground), gp);
    return build_profile(*e, rt, gp.wall_cap, ground, step, to_profile_structures(sr, ranges));
}

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
        cross_cache_ = CrossingCache();  // rebuilt lazily by the first preview that needs it
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
    std::vector<Vec3> pts = pts_;
    pts.push_back(pv.cursor);
    const std::optional<double> he = sn.kind == SnapKind::End && sn.out ? std::optional<double>(*sn.out + kPi) : std::nullopt;
    build_geometry(pv, pts, he, true);
}

void DrawSession::build_geometry(DrawPreview& pv, const std::vector<Vec3>& pts, std::optional<double> he, bool has_hover) {
    const RoadGraph& g = graph_;
    const TypeDims ty = dims_of(g, type_, one_way_);
    if (!ty.rt) {
        pv.errors.push_back({IssueCode::NoRegion, g.region() ? "unknown road type " + type_ : "no region pack"});
        return;
    }
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

    AnalysisOptions opt;
    opt.road_type = type_;
    opt.one_way = one_way_;
    opt.dual_pair = dual_pair;
    opt.profile_step = params_.profile_step;
    opt.want_profile = params_.profile_series;
    std::vector<StrokeSpec> specs;
    if (params_.crossing_feedback || dual_pair) specs = stroke_specs(g, ty, edge, pts, hs_, he, type_, one_way_);
    if (params_.crossing_feedback) {
        opt.crossing_specs = specs;
        if (cross_cache_.empty()) cross_cache_ = CrossingCache(g);
        opt.cross_cache = &cross_cache_;
    }
    EdgeAnalysis an = analyse_edge(g, ground_, edge, opt);
    const double cursor_h = pv.readouts.height;
    pv.readouts = an.readouts;
    pv.readouts.height = cursor_h;
    for (Issue& i : an.errors) pv.errors.push_back(std::move(i));
    pv.advisories = std::move(an.advisories);
    pv.profile = std::move(an.profile);
    pv.crossings = std::move(an.crossings);

    // Ribbons: one polyline per carriageway, split at the last clicked point (no split without a hover segment).
    const double span_len = edge.horizontal.spans.back().length;
    const double s_last = has_hover ? edge.horizontal.length() - span_len : edge.horizontal.length();
    auto make_ribbon = [&]() {
        Ribbon rb;
        rb.width = rb.total_width = ty.total;
        rb.carriageway_width = ty.carriage;
        rb.lift = params_.ghost_lift;
        return rb;
    };
    if (!dual_pair) {
        Ribbon rb = make_ribbon();
        for (const EdgeSample& s : sample_offset(edge, 0).samples) {
            if (s.s <= s_last + 1e-9) rb.committed.push_back(s.pos);
            if (has_hover && s.s >= s_last - 1e-9) rb.hover.push_back(s.pos);
        }
        pv.ribbons.push_back(std::move(rb));
        return;
    }
    // A dual stroke commits two one-way roads refitted through the offset points (stroke_specs), not the offset
    // curves of the drawn centreline, so the ghost is built from those fits: the left ribbon first, then the right
    // (looking along the drawn direction), each running in the drawn direction.
    const double off = ty.gap / 2 + ty.total / 2;
    const double side_sign = g.region()->driving_side == DrivingSide::Left ? 1.0 : -1.0;
    for (const double sg : {1.0, -1.0}) {
        Ribbon rb = make_ribbon();
        const bool forward = sg == side_sign;  // specs[0] is the forward carriageway (offset side_sign), specs[1] the reversed one
        const StrokeSpec& sp = specs[forward ? 0 : 1];
        Edge ce;
        if (fit_edge(g, type_, sp.points, sp.heading_a, sp.heading_b, ce)) {
            std::vector<Vec3> line;
            for (const EdgeSample& s : sample_centreline(ce)) line.push_back(s.pos);
            if (!forward) std::reverse(line.begin(), line.end());  // the reversed carriageway runs against the drawn direction
            // The committed part ends at the vertex nearest the offset point of the last clicked point.
            const Vec3 cut_pt = forward ? sp.points[sp.points.size() - 2] : sp.points[1];
            std::size_t ci = 0;
            double bd = 1e18;
            for (std::size_t i = 0; i < line.size(); ++i) {
                const double dd = (line[i].xy() - cut_pt.xy()).length();
                if (dd < bd) {
                    bd = dd;
                    ci = i;
                }
            }
            for (std::size_t i = 0; i < line.size(); ++i) {
                if (!has_hover || i <= ci) rb.committed.push_back(line[i]);
                if (has_hover && i >= ci) rb.hover.push_back(line[i]);
            }
        } else {
            for (const EdgeSample& s : sample_offset(edge, sg * off).samples) {
                if (s.s <= s_last + 1e-9) rb.committed.push_back(s.pos);
                if (has_hover && s.s >= s_last - 1e-9) rb.hover.push_back(s.pos);
            }
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
        // On the last placed point: a click ends the stroke there, so show the stroke as finish() would commit it.
        r.pv.would_finish = true;
        r.pv.cursor = pts_.back();
        r.pv.clicked = pts_;
        r.pv.readouts.height = pts_.back().z;
        if (pts_.size() >= 2) build_geometry(r.pv, pts_, std::nullopt, false);
        return r;
    }
    r.sn = compute_snap(hit, px, on_surface, mods);
    build_preview(r.pv, r.sn);
    return r;
}

DrawPreview DrawSession::hover(const PointerInput& in, Modifiers mods) {
    Resolved r = resolve(in, mods);
    if (r.has_pick && refusal_) {
        std::vector<Vec3> with_cursor = pts_;
        if (!r.pv.would_finish) with_cursor.push_back(r.pv.cursor);
        if (refusal_in_force(with_cursor)) {
            const bool dup = std::any_of(r.pv.errors.begin(), r.pv.errors.end(), [&](const Issue& e) { return e.text == refusal_->issue.text; });
            if (!dup) r.pv.errors.push_back(refusal_->issue);
            r.pv.refusal_active = true;
        }
    }
    return std::move(r.pv);
}

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
        refusal_.reset();
        out.added = true;
        return out;
    }
    pts_.push_back(r.pv.cursor);
    out.added = true;
    refusal_.reset();
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
            set_refusal(out.finish.error, pts_, false);  // pts_ still holds the refused point
            out.finish.error_code = refusal_->issue.code;
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
        refusal_.reset();
    } else {
        set_refusal(r.error, pts_, true);
    }
    return r;
}

FinishResult DrawSession::finish_at_cursor(const PointerInput& in, Modifiers mods) {
    Resolved r = resolve(in, mods);
    if (!r.has_pick) {
        FinishResult f;
        f.error = "the cursor is not on the ground";
        return f;
    }
    if (r.pv.would_finish) return finish();
    FinishResult f;
    if (pts_.empty()) {
        f.error = "a road needs at least two points";
        return f;
    }
    if (!r.pv.errors.empty()) {
        f.error = r.pv.errors.front().text;
        f.error_code = r.pv.errors.front().code;
        return f;
    }
    std::vector<Vec3> pts = pts_;
    pts.push_back(r.pv.cursor);
    const SnapKind k = r.sn.kind;
    const std::optional<double> he = k == SnapKind::End && r.sn.out ? std::optional<double>(*r.sn.out + kPi) : std::nullopt;
    f = finish_with(pts, hs_, he);
    if (f.ok()) {
        pts_.clear();
        hs_.reset();
        dz_ = 0;
        refusal_.reset();
    } else {
        set_refusal(f.error, std::move(pts), false);
    }
    return f;
}

void DrawSession::cancel() {
    pts_.clear();
    hs_.reset();
    dz_ = 0;
    refusal_.reset();
}

bool DrawSession::undo_point() {
    if (pts_.empty()) return false;
    pts_.pop_back();
    if (pts_.empty()) hs_.reset();
    refusal_.reset();
    return true;
}

void DrawSession::set_refusal(const std::string& reason, std::vector<Vec3> pts, bool whole_stroke) {
    Refusal r;
    r.issue.text = reason;
    r.issue.code = reason.rfind("crossing", 0) == 0     ? IssueCode::Crossing
                   : reason.rfind("segment", 0) == 0    ? IssueCode::MinSpan
                   : reason.rfind("no region", 0) == 0 || reason.rfind("unknown road", 0) == 0 ? IssueCode::NoRegion
                                                                                                : IssueCode::CannotFit;
    r.pts = std::move(pts);
    r.whole_stroke = whole_stroke;
    r.type = type_;
    r.one_way = one_way_;
    r.nodes = graph_.nodes().size();
    r.edges = graph_.edges().size();
    r.last_edge = graph_.edges().empty() ? 0 : graph_.edges().rbegin()->first.value;
    refusal_ = std::move(r);
}

bool DrawSession::refusal_in_force(const std::vector<Vec3>& with_cursor) const {
    if (!refusal_) return false;
    const Refusal& r = *refusal_;
    const std::uint64_t last = graph_.edges().empty() ? 0 : graph_.edges().rbegin()->first.value;
    if (r.type != type_ || r.one_way != one_way_ || r.nodes != graph_.nodes().size() || r.edges != graph_.edges().size() || r.last_edge != last)
        return false;
    return same_points(r.pts, r.whole_stroke ? pts_ : with_cursor);
}

FinishResult DrawSession::finish_with(const std::vector<Vec3>& pts, std::optional<double> hs, std::optional<double> he) {
    FinishResult out;
    const TypeDims ty = dims_of(graph_, type_, one_way_);
    if (!ty.rt) {
        out.error = graph_.region() ? "unknown road type " + type_ : "no region pack";
        out.error_code = IssueCode::NoRegion;
        return out;
    }
    for (std::size_t i = 0; i + 1 < pts.size(); ++i)
        if ((pts[i + 1].xy() - pts[i].xy()).length() < params_.min_span) {
            out.error = fmt("segment shorter than %.0f m", params_.min_span);
            out.error_code = IssueCode::MinSpan;
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
    std::vector<StrokeSpec> specs = stroke_specs(graph_, ty, centre, pts, hs, he, type_, one_way_);
    for (const StrokeSpec& s : specs) {
        out.error = check_stroke(graph_, s, sp);
        if (!out.error.empty()) {
            out.error_code = out.error.rfind("crossing", 0) == 0 ? IssueCode::Crossing : IssueCode::CannotFit;
            return out;
        }
    }
    if (specs.size() == 1) {
        out.command = make_draw_stroke(std::move(specs[0]), sp);
        return out;
    }
    auto both = std::make_unique<CompositeCommand<RoadGraph>>("draw dual carriageway");
    for (StrokeSpec& s : specs) both->add(make_draw_stroke(std::move(s), sp));
    out.command = std::move(both);
    return out;
}

}  // namespace cp
