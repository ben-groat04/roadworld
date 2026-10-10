#include "cp/road_graph.h"

#include <algorithm>
#include <cassert>

namespace cp {

const Node* RoadGraph::node(NodeId id) const {
    auto it = nodes_.find(id);
    return it == nodes_.end() ? nullptr : &it->second;
}

const Edge* RoadGraph::edge(EdgeId id) const {
    auto it = edges_.find(id);
    return it == edges_.end() ? nullptr : &it->second;
}

std::vector<EdgeId> RoadGraph::edges_at(NodeId n) const {
    auto it = adjacency_.find(n);
    if (it == adjacency_.end()) return {};
    return {it->second.begin(), it->second.end()};
}

NodeId RoadGraph::insert_node(Node n) {
    if (!n.id) n.id = node_ids_.next();
    else node_ids_.reserve_through(n.id.value);
    assert(!nodes_.count(n.id));
    nodes_[n.id] = n;
    adjacency_[n.id];
    dirty_nodes_.insert(n.id);
    return n.id;
}

void RoadGraph::erase_node(NodeId id) {
    assert(adjacency_[id].empty());
    nodes_.erase(id);
    adjacency_.erase(id);
    dirty_nodes_.erase(id);
}

void RoadGraph::set_region(std::shared_ptr<const RegionPack> region) {
    region_ = std::move(region);
    for (auto& [id, e] : edges_) {
        refit(e);
        dirty_nodes_.insert(e.a);
        dirty_nodes_.insert(e.b);
    }
}

VerticalCurveParams RoadGraph::curve_params_for(const Edge& e) const {
    VerticalCurveParams p = curve_params;
    if (region_) {
        if (const RoadType* rt = region_->road_type(e.road_type)) {
            p.k_crest = rt->vertical_k_crest;
            p.k_sag = rt->vertical_k_sag;
        }
    }
    return p;
}

bool RoadGraph::refit(Edge& e) const {
    const Node* na = node(e.a);
    const Node* nb = node(e.b);
    if (!na || !nb) return false;
    std::vector<Vec2> pts{na->pos.xy()};
    std::vector<double> zs{na->pos.z};
    for (const Vec3& p : e.interior) {
        pts.push_back(p.xy());
        zs.push_back(p.z);
    }
    pts.push_back(nb->pos.xy());
    zs.push_back(nb->pos.z);
    auto chain = fit_clothoid_chain(pts, {e.heading_a}, {e.heading_b});
    if (!chain) return false;
    std::vector<double> s{0};
    for (const auto& span : chain->spans) s.push_back(s.back() + span.length);
    e.horizontal = std::move(*chain);
    e.vertical = VerticalProfile::build(s, zs, curve_params_for(e));
    return true;
}

EdgeId RoadGraph::insert_edge(Edge e) {
    if (e.a == e.b || !node(e.a) || !node(e.b)) return {};
    if (!refit(e)) return {};
    if (!e.id) e.id = edge_ids_.next();
    else edge_ids_.reserve_through(e.id.value);
    assert(!edges_.count(e.id));
    adjacency_[e.a].insert(e.id);
    adjacency_[e.b].insert(e.id);
    dirty_nodes_.insert(e.a);
    dirty_nodes_.insert(e.b);
    const EdgeId id = e.id;
    edges_[id] = std::move(e);
    return id;
}

void RoadGraph::erase_edge(EdgeId id) {
    auto it = edges_.find(id);
    if (it == edges_.end()) return;
    adjacency_[it->second.a].erase(id);
    adjacency_[it->second.b].erase(id);
    dirty_nodes_.insert(it->second.a);
    dirty_nodes_.insert(it->second.b);
    edges_.erase(it);
}

void RoadGraph::move_node(NodeId id, Vec3 pos) {
    nodes_.at(id).pos = pos;
    dirty_nodes_.insert(id);
    for (EdgeId eid : adjacency_[id]) {
        Edge& e = edges_.at(eid);
        refit(e);
        dirty_nodes_.insert(e.a);
        dirty_nodes_.insert(e.b);
    }
}

Vec3 edge_point(const Edge& e, double s) {
    double acc = 0;
    for (std::size_t i = 0; i < e.horizontal.spans.size(); ++i) {
        const Clothoid& c = e.horizontal.spans[i];
        if (s <= acc + c.length || i + 1 == e.horizontal.spans.size()) {
            const Vec2 p = c.point(std::clamp(s - acc, 0.0, c.length));
            return {p.x, p.y, e.vertical.z(s)};
        }
        acc += c.length;
    }
    return {};
}

}  // namespace cp
