#include "cp/road_index.h"

#include <algorithm>
#include <cmath>

#include "cp/edge_sampling.h"

namespace cp {

std::int32_t RoadIndex::cell_of(double v) const {
    return static_cast<std::int32_t>(std::floor(std::clamp(v / cell_, -1e9, 1e9)));
}

RoadIndex::RoadIndex(const RoadGraph& g, double chord_step, double cell) : cell_(std::max(cell, 1.0)) {
    for (const auto& [id, e] : g.edges()) {
        EdgeData ed;
        ed.e = e;
        double acc = 0;
        for (std::size_t i = 0; i < e.horizontal.spans.size(); ++i) {
            const Clothoid& c = e.horizontal.spans[i];
            const double turn = std::abs(c.k0 * c.length + 0.5 * c.dk * c.length * c.length);
            const int n = std::max({2, static_cast<int>(std::ceil(c.length / chord_step)), static_cast<int>(std::ceil(turn / 0.05))});
            for (int j = i == 0 ? 0 : 1; j <= n; ++j) {
                const double u = c.length * j / n;
                const Vec2 pt = c.point(u);
                ed.samples.push_back({acc + u, pt.x, pt.y, e.vertical.z(acc + u)});
            }
            acc += c.length;
        }
        if (ed.samples.size() < 2) continue;
        const auto slot = static_cast<std::uint32_t>(edges_.size());
        for (std::size_t k = 0; k + 1 < ed.samples.size(); ++k) {
            const Sample &a = ed.samples[k], &b = ed.samples[k + 1];
            const std::int32_t ix0 = cell_of(std::min(a.x, b.x)), ix1 = cell_of(std::max(a.x, b.x));
            const std::int32_t iy0 = cell_of(std::min(a.y, b.y)), iy1 = cell_of(std::max(a.y, b.y));
            for (std::int32_t ix = ix0; ix <= ix1; ++ix)
                for (std::int32_t iy = iy0; iy <= iy1; ++iy)
                    chord_cells_[key(ix, iy)].push_back({slot, static_cast<std::uint32_t>(k)});
        }
        edge_slot_[id] = slot;
        edges_.push_back(std::move(ed));
    }
    for (const auto& [id, n] : g.nodes()) {
        const std::vector<EdgeId> at = g.edges_at(id);
        nodes_.push_back(NodeHit{id, n.pos, 0, at.size()});
        if (at.size() != 1) continue;
        const Edge* e = g.edge(at[0]);
        if (!e) continue;
        EndHit eh;
        eh.node = id;
        eh.edge = at[0];
        eh.at_start = e->a == id;
        eh.pos = n.pos;
        const double len = e->horizontal.length();
        eh.out_heading = eh.at_start ? wrap_angle(edge_heading(*e, 0) + kPi) : edge_heading(*e, len);
        ends_.push_back(eh);
    }
}

const Edge* RoadIndex::edge(EdgeId id) const {
    auto it = edge_slot_.find(id);
    return it == edge_slot_.end() ? nullptr : &edges_[it->second].e;
}

std::optional<RoadHit> RoadIndex::nearest_point(Vec2 p, double radius, std::optional<double> level, double level_tol,
                                                EdgeId skip) const {
    if (edges_.empty() || !(radius > 0)) return std::nullopt;
    double best_d = radius;
    bool found = false;
    std::uint32_t best_edge = 0, best_seg = 0;
    double best_t = 0;
    auto test = [&](const Chord& c) {
        const EdgeData& ed = edges_[c.edge];
        if (skip.valid() && ed.e.id == skip) return;
        const Sample &a = ed.samples[c.seg], &b = ed.samples[c.seg + 1];
        if (level && std::abs(a.z - *level) > level_tol) return;
        const double dx = b.x - a.x, dy = b.y - a.y, l2 = dx * dx + dy * dy;
        const double t = l2 > 0 ? std::clamp(((p.x - a.x) * dx + (p.y - a.y) * dy) / l2, 0.0, 1.0) : 0.0;
        const double d = std::hypot(p.x - (a.x + dx * t), p.y - (a.y + dy * t));
        if (d < best_d) {
            best_d = d;
            best_edge = c.edge;
            best_seg = c.seg;
            best_t = t;
            found = true;
        }
    };
    const std::int32_t ix0 = cell_of(p.x - radius), ix1 = cell_of(p.x + radius);
    const std::int32_t iy0 = cell_of(p.y - radius), iy1 = cell_of(p.y + radius);
    const std::int64_t span = (static_cast<std::int64_t>(ix1) - ix0 + 1) * (static_cast<std::int64_t>(iy1) - iy0 + 1);
    if (span > static_cast<std::int64_t>(chord_cells_.size())) {
        for (const auto& kv : chord_cells_)
            for (const Chord& c : kv.second) test(c);
    } else {
        for (std::int32_t ix = ix0; ix <= ix1; ++ix)
            for (std::int32_t iy = iy0; iy <= iy1; ++iy) {
                auto it = chord_cells_.find(key(ix, iy));
                if (it == chord_cells_.end()) continue;
                for (const Chord& c : it->second) test(c);
            }
    }
    if (!found) return std::nullopt;
    const EdgeData& ed = edges_[best_edge];
    const Sample &a = ed.samples[best_seg], &b = ed.samples[best_seg + 1];
    RoadHit h;
    h.edge = ed.e.id;
    h.s = a.s + (b.s - a.s) * best_t;
    h.point = edge_point(ed.e, h.s);
    h.heading = edge_heading(ed.e, h.s);
    h.distance = (h.point.xy() - p).length();
    // Side from the exact tangent (+ = the query point is left of a -> b).
    const Vec2 rel = p - h.point.xy();
    const double side = std::cos(h.heading) * rel.y - std::sin(h.heading) * rel.x;
    h.lateral = (side >= 0 ? 1.0 : -1.0) * h.distance;
    return h;
}

std::optional<NodeHit> RoadIndex::nearest_node(Vec2 p, double radius, std::optional<double> level, double level_tol) const {
    std::optional<NodeHit> best;
    double best_d = radius;
    for (const NodeHit& n : nodes_) {
        if (level && std::abs(n.pos.z - *level) > level_tol) continue;
        const double d = (n.pos.xy() - p).length();
        if (d < best_d) {
            best_d = d;
            best = n;
            best->distance = d;
        }
    }
    return best;
}

std::optional<EndHit> RoadIndex::nearest_free_end(Vec2 p, double radius, std::optional<double> level, double level_tol) const {
    std::optional<EndHit> best;
    double best_d = radius;
    for (const EndHit& e : ends_) {
        if (level && std::abs(e.pos.z - *level) > level_tol) continue;
        const double d = (e.pos.xy() - p).length();
        if (d < best_d) {
            best_d = d;
            best = e;
            best->distance = d;
        }
    }
    return best;
}

}  // namespace cp
