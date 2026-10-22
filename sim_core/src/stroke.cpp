// No exceptions: sim_core builds with -fno-exceptions.
// The crossing / splitting logic here was lifted unchanged from layout_io.cpp (Builder::first_crossing and
// Builder::draw); the polygon mode (follow_curve = false) reproduces it exactly.
#include "cp/stroke.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>

#include "cp/clothoid_chain.h"
#include "cp/edge_sampling.h"

namespace cp {

namespace {

constexpr double kInf = std::numeric_limits<double>::infinity();

// The stroke centreline as a polyline: the clicked points themselves (polygon mode) or a dense sampling of the
// fitted curve that contains the clicked points as vertices (curve mode).
struct Poly {
    std::vector<Vec3> pts;
    std::vector<double> head;        // curve mode: heading at each vertex
    std::vector<std::size_t> ctrl;   // vertex index of each clicked point
};

struct EdgePoly {
    EdgeId id;
    std::vector<double> s;
    std::vector<Vec3> p;
    double x0 = 0, y0 = 0, x1 = 0, y1 = 0;  // plan bounding box of p
};

EdgePoly sample_edge_poly(EdgeId id, const Edge& e, double step) {
    EdgePoly ep;
    ep.id = id;
    const double len = e.horizontal.length();
    for (double s = 0;; s += step) {
        if (s > len) s = len;
        ep.s.push_back(s);
        ep.p.push_back(edge_point(e, s));
        if (s >= len) break;
    }
    ep.x0 = ep.y0 = kInf;
    ep.x1 = ep.y1 = -kInf;
    for (const Vec3& q : ep.p) {
        ep.x0 = std::min(ep.x0, q.x);
        ep.x1 = std::max(ep.x1, q.x);
        ep.y0 = std::min(ep.y0, q.y);
        ep.y1 = std::max(ep.y1, q.y);
    }
    return ep;
}

struct Crossing {
    double key = kInf;  // segment index + parameter along the stroke
    std::size_t seg = 0;
    Vec3 pos;
    NodeId node;
    EdgeId edge;
    double s = 0;
};

std::string format_refusal(double dz, double headroom) { return crossing_refusal_text(dz, headroom); }

struct Resolver {
    const RoadGraph& g;
    const StrokeParams& P;
    double join_dz = 1e9;
    double headroom = 0;
    std::vector<EdgePoly> own;
    const std::vector<EdgePoly>* use = &own;
    bool ignored(EdgeId id) const { return std::find(P.ignore_edges.begin(), P.ignore_edges.end(), id) != P.ignore_edges.end(); }

    Resolver(const RoadGraph& graph, const StrokeParams& params) : g(graph), P(params) {
        double jd = P.join_dz;
        if (jd < 0 && g.region()) jd = g.region()->plan_crossing_junction_dz_m;
        join_dz = jd > 0 ? jd : 1e9;
        headroom = P.headroom >= 0 ? P.headroom : (g.region() ? g.region()->headroom_m : 0);
    }

    void refresh() {
        own.clear();
        use = &own;
        for (const auto& [id, e] : g.edges()) {
            if (ignored(id)) continue;
            own.push_back(sample_edge_poly(id, e, P.sample_step));
        }
    }
    void attach(const std::vector<EdgePoly>& polys) { use = &polys; }

    // Edge whose centreline passes closest to p (xy) within max_dist, with the arc length of that point.
    EdgeId edge_near(Vec2 p, double* s_out, double max_dist) const {
        EdgeId best;
        double best_d = max_dist, best_s = 0;
        for (const EdgePoly& ep : *use)
            for (std::size_t k = 0; k < ep.s.size(); ++k) {
                if (ignored(ep.id)) continue;
                const double d = (ep.p[k].xy() - p).length();
                if (d < best_d) {
                    best_d = d;
                    best = ep.id;
                    best_s = ep.s[k];
                }
            }
        if (s_out) *s_out = best_s;
        return best;
    }

    NodeId node_near(Vec2 p, double tol) const {
        NodeId best;
        double best_d = tol;
        for (const auto& [id, n] : g.nodes()) {
            const double d = (n.pos.xy() - p).length();
            if (d < best_d) {
                best_d = d;
                best = id;
            }
        }
        return best;
    }

    RoadEnd end_at(Vec3 p, std::optional<double> heading) const {
        RoadEnd r;
        r.pos = p;
        r.heading = heading;
        if (NodeId n = node_near(p.xy(), P.snap_tol); n.valid()) {
            r.existing_node = n;
        } else {
            double s = 0;
            if (EdgeId e = edge_near(p.xy(), &s, P.snap_tol); e.valid()) {
                r.split_edge = e;
                r.split_s = s;
            }
        }
        return r;
    }

    // Polyline of the stroke. Polygon mode: the clicked points. Curve mode: the fitted centreline.
    bool make_poly(const std::vector<Vec3>& pts, std::optional<double> ha, std::optional<double> hb,
                   const std::string& road_type, Poly& out) const {
        out = {};
        if (!P.follow_curve) {
            out.pts = pts;
            for (std::size_t i = 0; i < pts.size(); ++i) out.ctrl.push_back(i);
            return true;
        }
        std::vector<Vec2> xy;
        for (const Vec3& p : pts) xy.push_back(p.xy());
        auto chain = fit_sane_chain(xy, ha, hb);
        if (!chain) return false;
        std::vector<double> s{0}, z{pts.front().z};
        for (std::size_t i = 0; i < chain->spans.size(); ++i) {
            s.push_back(s.back() + chain->spans[i].length);
            z.push_back(pts[i + 1].z);
        }
        Edge tmp;
        tmp.road_type = road_type;
        const VerticalProfile vp = VerticalProfile::build(s, z, g.curve_params_for(tmp));
        double acc = 0;
        for (std::size_t i = 0; i < chain->spans.size(); ++i) {
            const Clothoid& c = chain->spans[i];
            const int n = std::max(1, static_cast<int>(std::ceil(c.length / P.curve_step)));
            for (int j = i == 0 ? 0 : 1; j <= n; ++j) {
                const double u = c.length * j / n;
                const Pose pose = c.pose(u);
                out.pts.push_back({pose.pos.x, pose.pos.y, vp.z(acc + u)});
                out.head.push_back(pose.heading);
                if (j == 0 || j == n) out.ctrl.push_back(out.pts.size() - 1);
            }
            acc += c.length;
        }
        // The last control point's vertex is the last vertex; its z is the authored one.
        out.pts.back().z = pts.back().z;
        return true;
    }

    // First place along the stroke where it must be split: an existing road crossed in plan at the same level,
    // or an existing node on the way. Stretches near the two ends are left to end_at().
    bool first_crossing(const Poly& poly, Crossing& out) const {
        const std::vector<Vec3>& pts = poly.pts;
        out = {};
        auto near_end = [&](Vec3 p) {
            return (p.xy() - pts.front().xy()).length() < P.snap_tol || (p.xy() - pts.back().xy()).length() < P.snap_tol;
        };
        const double dz = join_dz;
        for (std::size_t i = 0; i + 1 < pts.size(); ++i) {
            const Vec3 a = pts[i], b = pts[i + 1];
            const Vec2 ab = b.xy() - a.xy();
            const double ab2 = ab.dot(ab);
            if (ab2 < 1e-12) continue;
            for (const auto& [nid, n] : g.nodes()) {
                const double t = std::clamp((n.pos.xy() - a.xy()).dot(ab) / ab2, 0.0, 1.0);
                const Vec3 q{a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t};
                if ((q.xy() - n.pos.xy()).length() > P.node_tol || std::abs(q.z - n.pos.z) >= dz || near_end(n.pos)) continue;
                const double key = static_cast<double>(i) + t;
                if (key < out.key) {
                    out = {};
                    out.key = key;
                    out.seg = i;
                    out.pos = n.pos;
                    out.node = nid;
                }
            }
            for (const EdgePoly& ep : *use) {
                if (ignored(ep.id)) continue;
                for (std::size_t k = 1; k < ep.p.size(); ++k) {
                    const Vec3 p0 = ep.p[k - 1], p1 = ep.p[k];
                    const double s0 = ep.s[k - 1], s = ep.s[k];
                    const Vec2 cd = p1.xy() - p0.xy();
                    const double den = ab.cross(cd);
                    if (std::abs(den) <= 1e-12) continue;
                    const Vec2 ac = p0.xy() - a.xy();
                    const double t = ac.cross(cd) / den, u = ac.cross(ab) / den;
                    if (!((P.follow_curve ? t >= 0 : t > 0) && t < 1 && u >= 0 && u <= 1)) continue;
                    const Vec3 q{a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t};
                    const double ez = p0.z + (p1.z - p0.z) * u;
                    const double key = static_cast<double>(i) + t;
                    if (std::abs(q.z - ez) < dz && !near_end(q) && key < out.key) {
                        out = {};
                        out.key = key;
                        out.seg = i;
                        out.pos = {q.x, q.y, ez};
                        out.edge = ep.id;
                        out.s = s0 + (s - s0) * u;
                    }
                }
            }
        }
        if (out.key == kInf) return false;
        // A crossing within snap_tol of an existing node attaches to that node.
        if (!out.node.valid())
            if (NodeId n = node_near(out.pos.xy(), P.snap_tol); n.valid()) {
                out.node = n;
                out.edge = {};
            }
        return true;
    }

    // Every edge crossing of the polyline with its height difference (ends excluded).
    std::vector<StrokeCrossing> all_crossings(const Poly& poly) const {
        const std::vector<Vec3>& pts = poly.pts;
        std::vector<StrokeCrossing> out;
        auto near_end = [&](Vec3 p) {
            return (p.xy() - pts.front().xy()).length() < P.snap_tol || (p.xy() - pts.back().xy()).length() < P.snap_tol;
        };
        for (std::size_t i = 0; i + 1 < pts.size(); ++i) {
            const Vec3 a = pts[i], b = pts[i + 1];
            const Vec2 ab = b.xy() - a.xy();
            if (ab.dot(ab) < 1e-12) continue;
            const double sx0 = std::min(a.x, b.x) - 1e-6, sx1 = std::max(a.x, b.x) + 1e-6;
            const double sy0 = std::min(a.y, b.y) - 1e-6, sy1 = std::max(a.y, b.y) + 1e-6;
            for (const EdgePoly& ep : *use) {
                if (ignored(ep.id) || ep.x1 < sx0 || ep.x0 > sx1 || ep.y1 < sy0 || ep.y0 > sy1) continue;
                for (std::size_t k = 1; k < ep.p.size(); ++k) {
                    const Vec3 p0 = ep.p[k - 1], p1 = ep.p[k];
                    const Vec2 cd = p1.xy() - p0.xy();
                    const double den = ab.cross(cd);
                    if (std::abs(den) <= 1e-12) continue;
                    const Vec2 ac = p0.xy() - a.xy();
                    const double t = ac.cross(cd) / den, u = ac.cross(ab) / den;
                    if (!((P.follow_curve ? t >= 0 : t > 0) && t < 1 && u >= 0 && u <= 1)) continue;
                    const Vec3 q{a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t};
                    if (near_end(q)) continue;
                    StrokeCrossing c;
                    c.key = static_cast<double>(i) + t;
                    c.existing_z = p0.z + (p1.z - p0.z) * u;
                    c.pos = {q.x, q.y, q.z};
                    c.dz = std::abs(q.z - c.existing_z);
                    c.edge = ep.id;
                    c.edge_s = ep.s[k - 1] + (ep.s[k] - ep.s[k - 1]) * u;
                    c.join_limit = join_dz;
                    c.headroom = headroom;
                    bool dup = false;
                    for (const StrokeCrossing& o : out) dup = dup || (o.edge == c.edge && std::abs(o.edge_s - c.edge_s) < 0.05);
                    if (dup) continue;
                    c.verdict = c.dz < join_dz ? CrossingVerdict::Join
                                : (P.refuse_band && c.dz < headroom) ? CrossingVerdict::Refused
                                                                      : CrossingVerdict::Clear;
                    out.push_back(c);
                }
            }
        }
        std::sort(out.begin(), out.end(), [](const StrokeCrossing& l, const StrokeCrossing& r) { return l.key < r.key; });
        return out;
    }
};

}  // namespace

std::optional<ClothoidChain> fit_sane_chain(const std::vector<Vec2>& points, std::optional<double> heading_a,
                                            std::optional<double> heading_b) {
    // fit_clothoid_chain refuses runaway spirals itself (see kMaxSpanRatio there); nothing more to check here.
    return fit_clothoid_chain(points, {heading_a}, {heading_b});
}

std::string crossing_refusal_text(double dz, double headroom) {
    char buf[160];
    std::snprintf(buf, sizeof buf, "crossing too close vertically (%.1f m): raise/lower to junction or clear %.1f m", dz, headroom);
    return buf;
}

struct CrossingCache::Impl {
    std::vector<EdgePoly> polys;
};

CrossingCache::CrossingCache(const RoadGraph& g, double sample_step) {
    auto impl = std::make_shared<Impl>();
    for (const auto& [id, e] : g.edges()) impl->polys.push_back(sample_edge_poly(id, e, sample_step));
    impl_ = std::move(impl);
}

std::vector<StrokeCrossing> stroke_crossings(const RoadGraph& g, const CrossingCache& cache, const StrokeSpec& spec,
                                             const StrokeParams& params) {
    if (spec.points.size() < 2) return {};
    Resolver r(g, params);
    if (cache.impl()) r.attach(cache.impl()->polys);
    else r.refresh();
    Poly poly;
    if (!r.make_poly(spec.points, spec.heading_a, spec.heading_b, spec.road_type, poly)) return {};
    return r.all_crossings(poly);
}

std::vector<StrokeCrossing> stroke_crossings(const RoadGraph& g, const StrokeSpec& spec, const StrokeParams& params) {
    if (spec.points.size() < 2) return {};
    Resolver r(g, params);
    r.refresh();
    Poly poly;
    if (!r.make_poly(spec.points, spec.heading_a, spec.heading_b, spec.road_type, poly)) return {};
    return r.all_crossings(poly);
}

std::string check_stroke(const RoadGraph& g, const StrokeSpec& spec, const StrokeParams& params) {
    if (spec.points.size() < 2) return "a stroke needs at least two points";
    Resolver r(g, params);
    r.refresh();
    Poly poly;
    if (!r.make_poly(spec.points, spec.heading_a, spec.heading_b, spec.road_type, poly)) return "cannot fit curve";
    if (params.refuse_band)
        for (const StrokeCrossing& c : r.all_crossings(poly))
            if (c.verdict == CrossingVerdict::Refused) return format_refusal(c.dz, r.headroom);
    return {};
}

DrawStroke::DrawStroke(StrokeSpec spec, StrokeParams params, StrokeError* sink)
    : spec_(std::move(spec)), params_(params), sink_(sink) {}

bool DrawStroke::fail(StrokeError::Kind k, std::string msg) {
    if (sink_) {
        sink_->kind = k;
        sink_->message = std::move(msg);
    }
    return false;
}

void DrawStroke::revert(RoadGraph& g) {
    for (auto it = parts_.rbegin(); it != parts_.rend(); ++it) (*it)->revert(g);
}

bool DrawStroke::apply(RoadGraph& g) {
    if (sink_) *sink_ = {};
    // Redo: the pieces were resolved the first time; replaying them reproduces the same ids.
    if (!parts_.empty()) {
        for (std::size_t i = 0; i < parts_.size(); ++i)
            if (!parts_[i]->apply(g)) {
                while (i-- > 0) parts_[i]->revert(g);
                parts_.clear();
                return fail(StrokeError::Kind::PieceFailed, "redo of a drawn stroke failed");
            }
        return true;
    }
    if (spec_.points.size() < 2) return fail(StrokeError::Kind::BadSpec, "a stroke needs at least two points");

    Resolver r(g, params_);
    r.refresh();
    std::vector<Vec3> pts = spec_.points;
    std::optional<double> ha = spec_.heading_a;
    const std::optional<double> hb = spec_.heading_b;
    const double tol = params_.snap_tol;

    {  // refused band: decided on the whole stroke before anything is changed
        Poly poly;
        if (!r.make_poly(pts, ha, hb, spec_.road_type, poly)) return fail(StrokeError::Kind::CannotFit, "cannot fit curve");
        if (params_.refuse_band)
            for (const StrokeCrossing& c : r.all_crossings(poly))
                if (c.verdict == CrossingVerdict::Refused) return fail(StrokeError::Kind::Refused, format_refusal(c.dz, r.headroom));
    }

    auto piece = [&](RoadEnd a, std::vector<Vec3> interior, RoadEnd b) {
        DrawRoadSpec ds;
        ds.road_type = spec_.road_type;
        ds.one_way = spec_.one_way;
        ds.start = a;
        ds.end = b;
        ds.interior = std::move(interior);
        RoadCommandPtr cmd = make_draw_road(std::move(ds));
        if (!cmd->apply(g)) return false;
        parts_.push_back(std::move(cmd));
        return true;
    };
    auto rollback = [&](StrokeError::Kind k, std::string msg) {
        revert(g);
        parts_.clear();
        return fail(k, std::move(msg));
    };

    RoadEnd start = r.end_at(pts.front(), ha);
    for (int guard = 0; guard < 64; ++guard) {
        r.refresh();
        Poly poly;
        if (!r.make_poly(pts, ha, hb, spec_.road_type, poly)) return rollback(StrokeError::Kind::CannotFit, "cannot fit curve");
        Crossing x;
        if (!r.first_crossing(poly, x)) {
            if (!piece(start, std::vector<Vec3>(pts.begin() + 1, pts.end() - 1), r.end_at(pts.back(), hb)))
                return rollback(StrokeError::Kind::PieceFailed, "a stretch between two clicked points could not be fitted, or the road type cannot be drawn like this");
            return true;
        }
        std::vector<Vec3> interior;
        for (std::size_t k = 1; k + 1 < pts.size() && poly.ctrl[k] <= x.seg; ++k)
            if ((pts[k].xy() - x.pos.xy()).length() >= tol) interior.push_back(pts[k]);
        std::optional<double> cross_heading;
        if (params_.follow_curve && x.seg < poly.head.size()) cross_heading = poly.head[x.seg];
        RoadEnd end;
        end.pos = x.pos;
        end.heading = cross_heading;
        if (x.node.valid()) {
            end.existing_node = x.node;
        } else {
            end.split_edge = x.edge;
            end.split_s = x.s;
        }
        if (!piece(start, std::move(interior), end))
            return rollback(StrokeError::Kind::PieceFailed, "a stretch between two clicked points could not be fitted, or the road type cannot be drawn like this");
        std::vector<Vec3> rest{x.pos};
        for (std::size_t k = 1; k < pts.size(); ++k)
            if (poly.ctrl[k] > x.seg && (k + 1 == pts.size() || (pts[k].xy() - x.pos.xy()).length() >= tol)) rest.push_back(pts[k]);
        if (rest.size() < 2) return rollback(StrokeError::Kind::EndsInsideJunction, "the road ends inside a junction it crosses");
        pts = std::move(rest);
        ha = cross_heading;
        r.refresh();
        start = r.end_at(pts.front(), ha);
    }
    return rollback(StrokeError::Kind::TooManyCrossings, "more than 64 crossings");
}

std::unique_ptr<DrawStroke> make_draw_stroke(StrokeSpec spec, StrokeParams params, StrokeError* sink) {
    return std::make_unique<DrawStroke>(std::move(spec), params, sink);
}

}  // namespace cp
