#include "cp/road_commands.h"

#include <algorithm>
#include <cmath>

namespace cp {
namespace {

// Heading of the edge's centreline at arc length s.
double edge_heading(const Edge& e, double s) {
    double acc = 0;
    for (std::size_t i = 0; i < e.horizontal.spans.size(); ++i) {
        const Clothoid& c = e.horizontal.spans[i];
        if (s <= acc + c.length || i + 1 == e.horizontal.spans.size())
            return c.heading(std::clamp(s - acc, 0.0, c.length));
        acc += c.length;
    }
    return 0;
}

// Arc length of each interior control point (spans start at the points).
std::vector<double> interior_s(const Edge& e) {
    std::vector<double> s;
    double acc = 0;
    for (std::size_t i = 0; i + 1 < e.horizontal.spans.size(); ++i) {
        acc += e.horizontal.spans[i].length;
        s.push_back(acc);
    }
    return s;
}

// Splits an edge at arc length s into two edges joined by a new node. The
// split point keeps the original tangent so the shape barely changes.
class SplitEdge : public RoadCommand {
public:
    SplitEdge(EdgeId edge, double s) : edge_(edge), s_(s) {}

    NodeId new_node() const { return node_; }

    bool apply(RoadGraph& g) override {
        const Edge* e = g.edge(edge_);
        if (!e) return false;
        original_ = *e;
        const double L = e->horizontal.length();
        if (!(s_ > 0.5 && s_ < L - 0.5)) return false;  // too close to an end: attach to the node instead

        const Vec3 p = edge_point(*e, s_);
        const double h = edge_heading(*e, s_);
        const auto is = interior_s(*e);

        Edge left = original_, right = original_;
        left.id = left_;
        right.id = right_;
        left.interior.clear();
        right.interior.clear();
        for (std::size_t i = 0; i < is.size(); ++i) {
            if (std::abs(is[i] - s_) < 0.5) continue;  // drop a point that would sit on the split
            (is[i] < s_ ? left : right).interior.push_back(original_.interior[i]);
        }
        left.heading_b = h;
        right.heading_a = h;

        g.erase_edge(edge_);
        node_ = g.insert_node(Node{node_, p});
        left.b = node_;
        right.a = node_;
        left_ = g.insert_edge(left);
        right_ = g.insert_edge(right);
        if (!left_ || !right_) {
            revert(g);
            return false;
        }
        return true;
    }

    void revert(RoadGraph& g) override {
        if (left_ && g.edge(left_)) g.erase_edge(left_);
        if (right_ && g.edge(right_)) g.erase_edge(right_);
        if (node_ && g.node(node_)) g.erase_node(node_);
        g.insert_edge(original_);
    }

    std::string name() const override { return "split edge"; }

private:
    EdgeId edge_;
    double s_;
    Edge original_;
    NodeId node_;          // kept across redo so IDs are stable
    EdgeId left_, right_;
};

class AddNode : public RoadCommand {
public:
    explicit AddNode(Vec3 p) : pos_(p) {}
    NodeId id() const { return id_; }
    bool apply(RoadGraph& g) override {
        id_ = g.insert_node(Node{id_, pos_});
        return true;
    }
    void revert(RoadGraph& g) override { g.erase_node(id_); }
    std::string name() const override { return "add node"; }

private:
    Vec3 pos_;
    NodeId id_;
};

class AddEdge : public RoadCommand {
public:
    explicit AddEdge(Edge e) : edge_(std::move(e)) {}
    bool apply(RoadGraph& g) override {
        edge_.id = g.insert_edge(edge_);
        return edge_.id.valid();
    }
    void revert(RoadGraph& g) override { g.erase_edge(edge_.id); }
    std::string name() const override { return "add edge"; }

private:
    Edge edge_;
};

// Resolves each end (existing node / split / new node) at apply time, so the
// whole draw is one undo step and redo reproduces the same IDs.
class DrawRoad : public RoadCommand {
public:
    explicit DrawRoad(DrawRoadSpec spec) : spec_(std::move(spec)) {}

    bool apply(RoadGraph& g) override {
        parts_.clear();
        NodeId ends[2];
        RoadEnd* specs[2] = {&spec_.start, &spec_.end};
        for (int i = 0; i < 2; ++i) {
            RoadEnd& r = *specs[i];
            if (r.existing_node) {
                if (!g.node(r.existing_node)) return rollback(g);
                ends[i] = r.existing_node;
            } else if (r.split_edge) {
                if (!split_[i]) split_[i] = std::make_unique<SplitEdge>(r.split_edge, r.split_s);
                if (!split_[i]->apply(g)) return rollback(g);
                parts_.push_back(split_[i].get());
                ends[i] = split_[i]->new_node();
            } else {
                if (!add_[i]) add_[i] = std::make_unique<AddNode>(r.pos);
                add_[i]->apply(g);
                parts_.push_back(add_[i].get());
                ends[i] = add_[i]->id();
            }
        }
        Edge e;
        e.id = edge_id_;
        e.a = ends[0];
        e.b = ends[1];
        e.interior = spec_.interior;
        e.heading_a = spec_.start.heading;
        e.heading_b = spec_.end.heading;
        e.road_type = spec_.road_type;
        e.one_way = spec_.one_way;
        edge_id_ = g.insert_edge(e);
        if (!edge_id_) return rollback(g);
        return true;
    }

    void revert(RoadGraph& g) override {
        g.erase_edge(edge_id_);
        undo_parts(g);
    }

    std::string name() const override { return "draw road"; }

private:
    bool rollback(RoadGraph& g) {
        undo_parts(g);
        return false;
    }
    void undo_parts(RoadGraph& g) {
        for (auto it = parts_.rbegin(); it != parts_.rend(); ++it) (*it)->revert(g);
        parts_.clear();
    }

    DrawRoadSpec spec_;
    std::unique_ptr<SplitEdge> split_[2];
    std::unique_ptr<AddNode> add_[2];
    std::vector<RoadCommand*> parts_;
    EdgeId edge_id_;
};

class DeleteEdge : public RoadCommand {
public:
    explicit DeleteEdge(EdgeId id) : id_(id) {}
    bool apply(RoadGraph& g) override {
        const Edge* e = g.edge(id_);
        if (!e) return false;
        saved_ = *e;
        g.erase_edge(id_);
        // Remove end nodes left with no edges.
        orphans_.clear();
        for (NodeId n : {saved_.a, saved_.b}) {
            if (g.edges_at(n).empty()) {
                orphans_.push_back(*g.node(n));
                g.erase_node(n);
            }
        }
        return true;
    }
    void revert(RoadGraph& g) override {
        for (const Node& n : orphans_) g.insert_node(n);
        g.insert_edge(saved_);
    }
    std::string name() const override { return "delete road"; }

private:
    EdgeId id_;
    Edge saved_;
    std::vector<Node> orphans_;
};

class MoveInterior : public RoadCommand {
public:
    MoveInterior(EdgeId id, std::size_t i, Vec3 to) : id_(id), i_(i), to_(to) {}
    bool apply(RoadGraph& g) override {
        const Edge* e = g.edge(id_);
        if (!e || i_ >= e->interior.size()) return false;
        before_ = *e;
        Edge moved = *e;
        moved.interior[i_] = to_;
        if (!g.refit(moved)) return false;
        g.erase_edge(id_);
        g.insert_edge(moved);
        return true;
    }
    void revert(RoadGraph& g) override {
        g.erase_edge(id_);
        g.insert_edge(before_);
    }
    std::string name() const override { return "move point"; }

private:
    EdgeId id_;
    std::size_t i_;
    Vec3 to_;
    Edge before_;
};

class MoveNode : public RoadCommand {
public:
    MoveNode(NodeId id, Vec3 to) : id_(id), to_(to) {}
    bool apply(RoadGraph& g) override {
        const Node* n = g.node(id_);
        if (!n) return false;
        from_ = n->pos;
        g.move_node(id_, to_);
        return true;
    }
    void revert(RoadGraph& g) override { g.move_node(id_, from_); }
    std::string name() const override { return "move node"; }

private:
    NodeId id_;
    Vec3 to_, from_;
};

}  // namespace

RoadCommandPtr make_draw_road(DrawRoadSpec spec) { return std::make_unique<DrawRoad>(std::move(spec)); }
RoadCommandPtr make_delete_edge(EdgeId id) { return std::make_unique<DeleteEdge>(id); }
RoadCommandPtr make_move_interior_point(EdgeId id, std::size_t index, Vec3 to) {
    return std::make_unique<MoveInterior>(id, index, to);
}
RoadCommandPtr make_move_node(NodeId id, Vec3 to) { return std::make_unique<MoveNode>(id, to); }

}  // namespace cp
