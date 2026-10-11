// No exceptions: sim_core builds with -fno-exceptions.
// Port of the edit half of prototypes/road-draw/src/input.js; function by function:
//   processMouse (edit branch)   -> EditSession::hover / pick
//   pointerdown on a handle      -> EditSession::begin_drag
//   pointermove while dragging   -> EditSession::hover (drag_position + evaluate)
//   pointerup / commitHandleEdit -> EditSession::end_drag / commit_move
//   adjustHeight (edit branch)   -> EditSession::adjust_point_height / set_point_height
//   Delete key                   -> EditSession::delete_selected
#include "cp/edit_tool.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>

#include "cp/edge_sampling.h"

namespace cp {

namespace {

constexpr double kInf = std::numeric_limits<double>::infinity();

void mix(std::uint64_t& h, std::uint64_t v) {
    h ^= v + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2);
}
void mix(std::uint64_t& h, double d) {
    std::uint64_t u = 0;
    std::memcpy(&u, &d, sizeof u);
    mix(h, u);
}

std::uint64_t edge_key(const Edge& e) {
    std::uint64_t h = 1469598103934665603ULL;
    mix(h, e.id.value);
    mix(h, e.a.value);
    mix(h, e.b.value);
    mix(h, e.horizontal.length());
    mix(h, static_cast<std::uint64_t>(e.interior.size()));
    for (const Vec3& p : e.interior) {
        mix(h, p.x);
        mix(h, p.y);
        mix(h, p.z);
    }
    for (const VerticalProfile::Break& b : e.vertical.breaks) {
        mix(h, b.s);
        mix(h, b.z);
        mix(h, b.length);
    }
    for (const StructureSpan& s : e.structure) {
        mix(h, s.s0);
        mix(h, s.s1);
        mix(h, static_cast<std::uint64_t>(s.kind));
    }
    for (const char c : e.road_type) mix(h, static_cast<std::uint64_t>(c));
    mix(h, static_cast<std::uint64_t>(e.one_way));
    return h;
}

std::uint64_t graph_key(const RoadGraph& g) {
    std::uint64_t h = 1469598103934665603ULL;
    for (const auto& [id, n] : g.nodes()) {
        mix(h, id.value);
        mix(h, n.pos.x);
        mix(h, n.pos.y);
        mix(h, n.pos.z);
    }
    for (const auto& [id, e] : g.edges()) mix(h, edge_key(e));
    return h;
}

// RoadGraph::refit with one node's position overridden (the graph itself stays untouched). pts receives the
// control points a, interior..., b as they would be.
bool refit_with(const RoadGraph& g, Edge& e, NodeId moved, Vec3 to, std::vector<Vec3>& pts) {
    const Node* na = g.node(e.a);
    const Node* nb = g.node(e.b);
    if (!na || !nb) return false;
    Vec3 pa = na->pos, pb = nb->pos;
    if (e.a == moved) pa = to;
    if (e.b == moved) pb = to;
    pts.clear();
    pts.push_back(pa);
    for (const Vec3& p : e.interior) pts.push_back(p);
    pts.push_back(pb);
    std::vector<Vec2> xy;
    std::vector<double> zs;
    for (const Vec3& p : pts) {
        xy.push_back(p.xy());
        zs.push_back(p.z);
    }
    auto chain = fit_sane_chain(xy, e.heading_a, e.heading_b);
    if (!chain) return false;
    std::vector<double> s{0};
    for (const auto& span : chain->spans) s.push_back(s.back() + span.length);
    e.horizontal = std::move(*chain);
    e.vertical = VerticalProfile::build(s, zs, g.curve_params_for(e));
    return true;
}

Vec3 handle_pos(const RoadGraph& g, const Edge& e, std::size_t h) {
    if (h == 0) return g.node(e.a) ? g.node(e.a)->pos : Vec3{};
    if (h == e.interior.size() + 1) return g.node(e.b) ? g.node(e.b)->pos : Vec3{};
    return e.interior[h - 1];
}

double norm3(const Vec3& v) { return std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z); }

// Distance from a point to a ray (dir unit length), and the distance along the ray to the closest point.
double ray_point_distance(const Ray& r, const Vec3& dir, const Vec3& p, double& t) {
    const Vec3 d{p.x - r.origin.x, p.y - r.origin.y, p.z - r.origin.z};
    t = d.x * dir.x + d.y * dir.y + d.z * dir.z;
    const Vec3 c{d.x - dir.x * t, d.y - dir.y * t, d.z - dir.z * t};
    return norm3(c);
}

class SetOneWay : public RoadCommand {
public:
    SetOneWay(EdgeId id, bool v) : id_(id), v_(v) {}
    bool apply(RoadGraph& g) override {
        const Edge* e = g.edge(id_);
        if (!e || e->one_way == v_) return false;
        before_ = *e;
        Edge changed = *e;
        changed.one_way = v_;
        g.erase_edge(id_);
        g.insert_edge(changed);
        return g.edge(id_) != nullptr;
    }
    void revert(RoadGraph& g) override {
        g.erase_edge(id_);
        g.insert_edge(before_);
    }
    std::string name() const override { return v_ ? "make road one-way" : "make road two-way"; }

private:
    EdgeId id_;
    bool v_;
    Edge before_;
};

}  // namespace

struct EditSession::Trial {
    bool found = false;
    NodeId node;                  // set when the handle is an end node
    Vec3 to;
    std::vector<EditGhost> ghosts;
    std::vector<Issue> errors;
    EdgeAnalysis an;
    bool analysed = false;
    Ribbon ribbon;
};

EditSession::EditSession(const RoadGraph& graph, const Heightfield& ground, EditParams params)
    : graph_(graph), ground_(ground), params_(params) {}

void EditSession::ensure_index() {
    const std::uint64_t key = graph_key(graph_);
    if (!index_valid_ || key != fingerprint_) {
        index_ = RoadIndex(graph_);
        cross_cache_ = CrossingCache();
        index_valid_ = true;
        fingerprint_ = key;
    }
}

EdgeId EditSession::selected() const { return graph_.edge(sel_) ? sel_ : EdgeId{}; }

void EditSession::select(EdgeId id) { sel_ = graph_.edge(id) ? id : EdgeId{}; }

std::vector<Handle> EditSession::handles() const {
    std::vector<Handle> out;
    const Edge* e = graph_.edge(sel_);
    if (!e) return out;
    const std::size_t n = e->interior.size();
    for (std::size_t i = 0; i < n + 2; ++i) {
        Handle h;
        h.edge = sel_;
        h.index = i;
        h.pos = handle_pos(graph_, *e, i);
        if (i == 0) h.node = e->a;
        if (i == n + 1) h.node = e->b;
        out.push_back(h);
    }
    return out;
}

std::vector<Vec3> EditSession::centreline(EdgeId id, double step) const {
    std::vector<Vec3> out;
    const Edge* e = graph_.edge(id);
    if (!e) return out;
    SamplingParams sp;
    sp.max_step = std::max(step, 0.5);
    for (const EdgeSample& s : sample_centreline(*e, sp)) out.push_back(s.pos);
    return out;
}

EditSession::Ground EditSession::resolve(const PointerInput& in) const {
    Ground r;
    GroundPickParams gpp;
    gpp.max_distance = params_.pick_max_distance;
    const auto gh = pick_ground(ground_, in.ray, gpp);
    const double dlen = norm3(in.ray.dir);
    if (gh) {
        r.hit = gh->point;
        r.dist = gh->distance;
        r.has_pick = true;
    }
    if (in.surface_hit && dlen > 0) {
        const Vec3 sh = *in.surface_hit;
        const double sd = norm3({sh.x - in.ray.origin.x, sh.y - in.ray.origin.y, sh.z - in.ray.origin.z});
        if (!gh || sd < gh->distance + 0.3) {
            r.hit = sh;
            r.dist = sd;
            r.on_surface = true;
            r.has_pick = true;
        }
    }
    r.px = in.metres_per_pixel > 0 ? in.metres_per_pixel : in.metres_per_pixel_per_metre * r.dist;
    return r;
}

EditHover EditSession::pick(const PointerInput& in, const Ground& gr) {
    EditHover h;
    const double dlen = norm3(in.ray.dir);
    const Vec3 dir = dlen > 0 ? Vec3{in.ray.dir.x / dlen, in.ray.dir.y / dlen, in.ray.dir.z / dlen} : Vec3{0, 0, -1};
    auto handle_radius = [&](double t) {
        return params_.handle_pick_px * (in.metres_per_pixel > 0 ? in.metres_per_pixel : in.metres_per_pixel_per_metre * std::max(t, 0.0));
    };
    // 1. the selected road's handles: within handle_pick_px of the lifted point, nearest wins
    if (const Edge* se = graph_.edge(sel_)) {
        double best = kInf;
        for (std::size_t i = 0; i < se->interior.size() + 2; ++i) {
            Vec3 p = handle_pos(graph_, *se, i);
            const Vec3 lifted{p.x, p.y, p.z + params_.handle_lift};
            double t = 0;
            const double d = ray_point_distance(in.ray, dir, lifted, t);
            if (t <= 0 || d >= handle_radius(t) || d >= best) continue;
            best = d;
            h = {};
            h.kind = EditHoverKind::Handle;
            h.edge = sel_;
            h.handle = i;
            h.point = p;
            h.distance = d;
            if (i == 0) h.node = se->a;
            if (i == se->interior.size() + 1) h.node = se->b;
        }
        if (h.kind == EditHoverKind::Handle) return h;
    }
    if (!gr.has_pick) return h;
    const Vec2 q = gr.hit.xy();
    // 2. a junction / road end of any road
    if (auto nd = index_.nearest_node(q, params_.handle_pick_px * gr.px, gr.hit.z, params_.level_tol)) {
        h.kind = EditHoverKind::Node;
        h.node = nd->node;
        h.point = nd->pos;
        h.distance = nd->distance;
        const std::vector<EdgeId> at = graph_.edges_at(nd->node);
        if (!at.empty()) {
            h.edge = *std::min_element(at.begin(), at.end());
            const Edge* e = graph_.edge(h.edge);
            h.handle = e && e->a == nd->node ? 0 : (e ? e->interior.size() + 1 : 0);
        }
        return h;
    }
    // 3. an interior control point of any road
    {
        double best = params_.handle_pick_px * gr.px;
        for (const auto& [id, e] : graph_.edges())
            for (std::size_t i = 0; i < e.interior.size(); ++i) {
                const Vec3& p = e.interior[i];
                if (std::abs(p.z - gr.hit.z) > params_.level_tol) continue;
                const double d = (p.xy() - q).length();
                if (d >= best) continue;
                best = d;
                h = {};
                h.kind = EditHoverKind::InteriorPoint;
                h.edge = id;
                h.handle = i + 1;
                h.point = p;
                h.distance = d;
            }
        if (h.kind == EditHoverKind::InteriorPoint) return h;
    }
    // 4. the centreline of any road
    if (auto on = index_.nearest_point(q, params_.road_pick_px * gr.px, gr.hit.z, params_.level_tol)) {
        h.kind = EditHoverKind::Road;
        h.edge = on->edge;
        h.point = on->point;
        h.s = on->s;
        h.distance = on->distance;
    }
    return h;
}

bool EditSession::drag_position(const PointerInput& in, Vec3& out) const {
    if (!drag_) return false;
    const Vec3& o = in.ray.origin;
    const Vec3& d = in.ray.dir;
    if (std::abs(d.z) < 1e-5) return false;
    const double t = (drag_->plane_z - o.z) / d.z;
    if (!(t > 0)) return false;
    const double x0 = ground_.origin().x + params_.world_margin, y0 = ground_.origin().y + params_.world_margin;
    const double x1 = ground_.origin().x + (ground_.nx() - 1) * ground_.cell() - params_.world_margin;
    const double y1 = ground_.origin().y + (ground_.ny() - 1) * ground_.cell() - params_.world_margin;
    const double x = std::clamp(o.x + d.x * t, x0, std::max(x0, x1)), y = std::clamp(o.y + d.y * t, y0, std::max(y0, y1));
    out = {x, y, ground_.height(x, y) + drag_->dz_terrain};
    return true;
}

EditSession::Trial EditSession::evaluate(EdgeId edge_id, std::size_t handle, Vec3 to) const {
    Trial t;
    t.to = to;
    const Edge* e0 = graph_.edge(edge_id);
    if (!e0 || !graph_.region()) {
        t.errors.push_back({IssueCode::NoRegion, e0 ? "no region pack" : "that road no longer exists"});
        return t;
    }
    const std::size_t n = e0->interior.size();
    if (handle > n + 1) {
        t.errors.push_back({IssueCode::CannotFit, "no such handle"});
        return t;
    }
    t.found = true;
    if (handle == 0) t.node = e0->a;
    if (handle == n + 1) t.node = e0->b;
    std::vector<EdgeId> affected{edge_id};
    if (t.node.valid()) affected = graph_.edges_at(t.node);
    bool fit_failed = false, span_failed = false;
    for (const EdgeId id : affected) {
        const Edge* src = graph_.edge(id);
        if (!src) continue;
        Edge moved = *src;
        if (id == edge_id && !t.node.valid()) moved.interior[handle - 1] = to;
        std::vector<Vec3> pts;
        EditGhost gh;
        gh.edge = id;
        if (!refit_with(graph_, moved, t.node, to, pts)) {
            gh.fits = false;
            fit_failed = true;
        } else {
            for (const EdgeSample& s : sample_centreline(moved)) gh.centreline.push_back(s.pos);
            for (std::size_t i = 0; i + 1 < pts.size(); ++i)
                if ((pts[i + 1].xy() - pts[i].xy()).length() < params_.min_span) span_failed = true;
        }
        if (id == edge_id && gh.fits) {
            // The edited road: the same analysis as a drawn stroke.
            AnalysisOptions opt;
            opt.road_type = moved.road_type;
            opt.one_way = moved.one_way;
            opt.dual_pair = false;
            opt.profile_step = params_.profile_step;
            opt.want_profile = params_.profile_series;
            opt.edit_mode = true;
            if (params_.crossing_feedback) {
                opt.crossing_specs.push_back(StrokeSpec{pts, moved.heading_a, moved.heading_b, moved.road_type, moved.one_way});
                opt.ignore_edges = affected;
                if (!cross_cache_.empty()) opt.cross_cache = &cross_cache_;
            }
            t.an = analyse_edge(graph_, ground_, moved, opt);
            t.analysed = true;
            const RoadWidths w = road_widths(graph_, moved.road_type, moved.one_way);
            t.ribbon.width = t.ribbon.total_width = w.total;
            t.ribbon.carriageway_width = w.carriageway;
            t.ribbon.lift = params_.ghost_lift;
            for (const EdgeSample& s : sample_offset(moved, 0).samples) t.ribbon.committed.push_back(s.pos);
        }
        t.ghosts.push_back(std::move(gh));
    }
    if (fit_failed) t.errors.push_back({IssueCode::CannotFit, "cannot fit curve"});
    if (span_failed) {
        char buf[64];
        std::snprintf(buf, sizeof buf, "segment shorter than %.0f m", params_.min_span);
        t.errors.push_back({IssueCode::MinSpan, buf});
    }
    for (const Issue& i : t.an.errors) t.errors.push_back(i);
    return t;
}

ProfileSeries EditSession::cached_profile(EdgeId id) {
    const Edge* e = graph_.edge(id);
    if (!e) return {};
    const std::uint64_t key = edge_key(*e);
    if (profile_cache_.id == id && profile_cache_.key == key) return profile_cache_.series;
    profile_cache_.id = id;
    profile_cache_.key = key;
    profile_cache_.series = profile_of_edge(graph_, ground_, id, params_.profile_step);
    return profile_cache_.series;
}

EditPreview EditSession::hover(const PointerInput& in) {
    EditPreview pv;
    ensure_index();
    const Ground gr = resolve(in);
    pv.has_pick = gr.has_pick;
    pv.cursor = gr.hit;
    if (drag_) {
        Vec3 pos = drag_->current;
        if (drag_position(in, pos)) drag_->current = pos;
        pv.has_pick = true;
        pv.dragging = true;
        pv.cursor = pos;
        pv.hover.kind = EditHoverKind::Handle;
        pv.hover.edge = drag_->edge;
        pv.hover.handle = drag_->handle;
        pv.hover.point = pos;
        if (cross_cache_.empty() && params_.crossing_feedback) cross_cache_ = CrossingCache(graph_);
        Trial t = evaluate(drag_->edge, drag_->handle, pos);
        pv.profile_edge = drag_->edge;
        pv.ghosts = std::move(t.ghosts);
        pv.errors = std::move(t.errors);
        if (t.analysed) {
            pv.advisories = std::move(t.an.advisories);
            pv.readouts = t.an.readouts;
            pv.profile = std::move(t.an.profile);
            pv.crossings = std::move(t.an.crossings);
            pv.ribbons.push_back(std::move(t.ribbon));
        }
        return pv;
    }
    pv.hover = pick(in, gr);
    if (pv.hover.kind != EditHoverKind::None) pv.has_pick = true;
    const EdgeId shown = selected().valid() ? selected() : pv.hover.edge;
    if (params_.profile_series && shown.valid()) {
        pv.profile_edge = shown;
        pv.profile = cached_profile(shown);
    }
    return pv;
}

EditHover EditSession::click(const PointerInput& in) {
    ensure_index();
    const Ground gr = resolve(in);
    const EditHover h = pick(in, gr);
    if (h.kind == EditHoverKind::Handle) return h;  // a click on a handle of the selected road keeps the selection
    sel_ = h.kind == EditHoverKind::None ? EdgeId{} : h.edge;
    return h;
}

bool EditSession::begin_drag(const PointerInput& in) {
    ensure_index();
    const EditHover h = pick(in, resolve(in));
    if (h.kind != EditHoverKind::Handle || h.edge != sel_) return false;
    const Edge* e = graph_.edge(h.edge);
    if (!e) return false;
    Drag d;
    d.edge = h.edge;
    d.handle = h.handle;
    d.start = handle_pos(graph_, *e, h.handle);
    d.plane_z = d.start.z;
    d.dz_terrain = d.start.z - ground_.height(d.start.x, d.start.y);
    d.current = d.start;
    drag_ = d;
    return true;
}

EditResult EditSession::commit_move(EdgeId edge, std::size_t handle, Vec3 to) const {
    EditResult r;
    Trial t = evaluate(edge, handle, to);
    if (!t.errors.empty()) {
        r.error = t.errors.front().text;
        r.error_code = t.errors.front().code;
        return r;
    }
    r.advisories = std::move(t.an.advisories);
    r.command = t.node.valid() ? make_move_node(t.node, to) : make_move_interior_point(edge, handle - 1, to);
    return r;
}

EditResult EditSession::end_drag(const PointerInput& in) {
    EditResult r;
    if (!drag_) {
        r.error = "no drag in progress";
        return r;
    }
    ensure_index();
    Vec3 pos = drag_->current;
    Vec3 p;
    if (drag_position(in, p)) pos = p;
    const Drag d = *drag_;
    drag_.reset();
    if ((pos.xy() - d.start.xy()).length() < 1e-6 && std::abs(pos.z - d.start.z) < 1e-6) {
        r.error = "the handle did not move";
        return r;
    }
    if (params_.crossing_feedback && cross_cache_.empty()) cross_cache_ = CrossingCache(graph_);
    return commit_move(d.edge, d.handle, pos);
}

EditResult EditSession::set_point_height(EdgeId edge, std::size_t handle, double z) {
    EditResult r;
    const Edge* e = graph_.edge(edge);
    if (!e) {
        r.error = "that road no longer exists";
        return r;
    }
    if (handle > e->interior.size() + 1) {
        r.error = "no such handle";
        return r;
    }
    Vec3 to = handle_pos(graph_, *e, handle);
    if (std::abs(to.z - z) < 1e-9) {
        r.error = "the point is already at that height";
        return r;
    }
    to.z = z;
    return commit_move(edge, handle, to);
}

EditResult EditSession::adjust_point_height(EdgeId edge, std::size_t handle, double delta) {
    const Edge* e = graph_.edge(edge);
    if (!e || handle > e->interior.size() + 1) return set_point_height(edge, handle, 0);  // reports the error
    return set_point_height(edge, handle, handle_pos(graph_, *e, handle).z + delta);
}

EditResult EditSession::delete_selected() {
    EditResult r;
    const EdgeId id = selected();
    if (!id.valid()) {
        r.error = "no road is selected";
        return r;
    }
    r.command = make_delete_edge(id);
    return r;
}

EditResult EditSession::change_type(const std::string& road_type) {
    EditResult r;
    const Edge* e = graph_.edge(sel_);
    if (!e) {
        r.error = "no road is selected";
        return r;
    }
    if (!graph_.region() || !graph_.region()->road_type(road_type)) {
        r.error = graph_.region() ? "unknown road type " + road_type : "no region pack";
        r.error_code = IssueCode::NoRegion;
        return r;
    }
    if (e->road_type == road_type) {
        r.error = "the road already has that type";
        return r;
    }
    ensure_index();
    Edge changed = *e;
    changed.road_type = road_type;
    std::vector<Vec3> pts;
    if (!refit_with(graph_, changed, NodeId{}, {}, pts)) {
        r.error = "cannot fit curve";
        return r;
    }
    AnalysisOptions opt;
    opt.road_type = road_type;
    opt.one_way = changed.one_way;
    opt.want_profile = false;
    opt.edit_mode = true;
    if (params_.crossing_feedback) {
        opt.crossing_specs.push_back(StrokeSpec{pts, changed.heading_a, changed.heading_b, road_type, changed.one_way});
        opt.ignore_edges = {sel_};
    }
    EdgeAnalysis an = analyse_edge(graph_, ground_, changed, opt);
    if (!an.errors.empty()) {
        r.error = an.errors.front().text;
        r.error_code = an.errors.front().code;
        return r;
    }
    r.advisories = std::move(an.advisories);
    r.command = make_change_road_type(sel_, road_type);
    return r;
}

EditResult EditSession::set_one_way(bool one_way) {
    EditResult r;
    const Edge* e = graph_.edge(sel_);
    if (!e) {
        r.error = "no road is selected";
        return r;
    }
    if (e->one_way == one_way) {
        r.error = one_way ? "the road is already one-way" : "the road is already two-way";
        return r;
    }
    r.command = std::make_unique<SetOneWay>(sel_, one_way);
    return r;
}

}  // namespace cp
