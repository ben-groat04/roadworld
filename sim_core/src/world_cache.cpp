// WorldCache: layered derived-data cache with dirty consumption and a tile change list (cp/world_cache.h).
//
// How the invariant "incremental == full rebuild" is met, layer by layer:
//  - junctions: dirty nodes (live and removed) plus every node of an old roundabout ring they touch are
//    rebuilt with build_plain_junctions / apply_roundabouts(only). Rings that stop being rings leave stale
//    Roundabout junctions behind unless ALL their nodes are rebuilt, hence that widening.
//  - road_mesh: every object (edge, joint, junction, roundabout) keeps a footprint (the tiles its triangles can
//    fall in, from geometry grown by the margin render_mesh's own filter uses). Objects that changed give
//    old footprint + new footprint = candidate tiles; build_mesh_chunks(only = candidates) rebuilds those tiles
//    from scratch, so a tile is never patched, always regenerated whole in the same object order as a full
//    build. Changed objects: dirty edges, edges at dirty or rebuilt-junction nodes, joints at those nodes,
//    junctions rebuilt, roundabouts touched.
//  - markings: per-owner (edge / junction / splitter) pieces split by tile; a tile is the concatenation of
//    its owners' pieces in id order, the same as a full build. Owners regenerated: dirty edges, edges at dirty
//    or rebuilt-junction nodes, junctions at those nodes, splitters of touched rings; owners whose object is
//    gone are dropped (rebuild_dirty_markings never drops splitters; this layer does).
//  - street_lights: rebuild_dirty_street_lights (which widens by one hop itself); the changed tiles are then
//    found by regrouping all lights and comparing with the snapshots (O(lights), a few ms per 100k lights).
#include "cp/world_cache.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <utility>

#include "cp/edge_sampling.h"

namespace cp {
namespace {

// ------------------------------------------------------------------ footprints

void add_box_tiles(std::set<TileKey>& out, Vec2 lo, Vec2 hi, double ts) {
    if (lo.x > hi.x || lo.y > hi.y) return;
    const TileKey a = tile_of(lo, ts), b = tile_of(hi, ts);
    for (std::int64_t i = a.i; i <= b.i; ++i)
        for (std::int64_t j = a.j; j <= b.j; ++j) out.insert({i, j});
}

struct Box {
    Vec2 lo{1e300, 1e300}, hi{-1e300, -1e300};
    void add(Vec2 p) {
        lo = {std::min(lo.x, p.x), std::min(lo.y, p.y)};
        hi = {std::max(hi.x, p.x), std::max(hi.y, p.y)};
    }
    void add(const PolygonSet& ps) {
        for (const auto& r : ps)
            for (const Vec2& p : r) add(p);
    }
    void grow(double m) {
        lo = {lo.x - m, lo.y - m};
        hi = {hi.x + m, hi.y + m};
    }
};

std::set<TileKey> box_foot(const Box& b, double ts) {
    std::set<TileKey> out;
    add_box_tiles(out, b.lo, b.hi, ts);
    return out;
}

// Tiles within `margin` of the centreline (render_mesh samples edges at <= 50 m, so every point of the line is
// within 25 m of a sample; margin >= 26 therefore covers the whole line).
std::set<TileKey> edge_foot(const Edge& e, double margin, double ts) {
    std::set<TileKey> out;
    const double m = std::max(margin, 26.0);
    for (const EdgeSample& s : sample_centreline(e, SamplingParams{0.5, 0.3, 50.0}))
        add_box_tiles(out, {s.pos.x - m, s.pos.y - m}, {s.pos.x + m, s.pos.y + m}, ts);
    return out;
}

std::set<TileKey> joint_foot(const RoadGraph& g, const Node& n, const RenderMeshParams& mp, double ts) {
    const std::size_t arms = g.edges_at(n.id).size();
    if (arms < 1 || arms > 2) return {};
    Box b;
    b.add(n.pos.xy());
    b.grow(mp.joint_length + 15);
    return box_foot(b, ts);
}

std::set<TileKey> junction_foot(const Junction& j, double ts) {
    Box b;
    b.add(j.carriageway);
    b.add(j.pavements);
    b.grow(2);
    return box_foot(b, ts);
}

std::set<TileKey> roundabout_foot(const RoundaboutLayout& l, double ts) {
    Box b;
    b.add(l.island);
    for (const SplitterIsland& s : l.splitters) {
        for (const Vec2& p : s.polygon) b.add(p);
        for (const Vec2& p : s.paint) b.add(p);  // the painted gap (lane edge lines, ladder bars) runs past the island's nose
    }
    b.grow(2);
    return box_foot(b, ts);
}

enum FootKind { kFootEdge = 0, kFootJoint = 1, kFootJunction = 2, kFootRoundabout = 3 };
struct FootKey {
    int kind = 0;
    std::uint64_t id = 0;
    friend auto operator<=>(const FootKey&, const FootKey&) = default;
};

class Footprints {
public:
    void clear() { m_.clear(); }
    // Replaces the footprint of `k`; both the old and the new tiles are added to `dirty`.
    void set(FootKey k, const std::set<TileKey>& now, std::set<TileKey>& dirty) {
        const auto it = m_.find(k);
        if (it != m_.end()) {
            dirty.insert(it->second.begin(), it->second.end());
            if (now.empty()) {
                m_.erase(it);
                return;
            }
        }
        dirty.insert(now.begin(), now.end());
        if (!now.empty()) m_[k] = std::vector<TileKey>(now.begin(), now.end());
    }
    template <class Pred>
    std::vector<FootKey> keys_where(int kind, Pred keep_out) const {
        std::vector<FootKey> out;
        for (const auto& [k, v] : m_)
            if (k.kind == kind && keep_out(k.id)) out.push_back(k);
        return out;
    }

private:
    std::map<FootKey, std::vector<TileKey>> m_;
};

// ------------------------------------------------------------------ equality

template <class V>
bool same_bytes(const std::vector<V>& a, const std::vector<V>& b) {
    return a.size() == b.size() && (a.empty() || std::memcmp(a.data(), b.data(), a.size() * sizeof(V)) == 0);
}
bool same_sections(const std::vector<SectionRange>& a, const std::vector<SectionRange>& b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i) {
        const SectionRange &x = a[i], &y = b[i];
        if (x.edge != y.edge || x.node != y.node || x.kind != y.kind || x.s0 != y.s0 || x.s1 != y.s1 ||
            x.first_index != y.first_index || x.index_count != y.index_count)
            return false;
    }
    return true;
}
bool same_vec3(Vec3 a, Vec3 b) { return a.x == b.x && a.y == b.y && a.z == b.z; }
bool same_light(const StreetLight& a, const StreetLight& b) {
    return same_vec3(a.position, b.position) && a.heading == b.heading && a.mounting_height == b.mounting_height &&
           a.outreach == b.outreach && a.style == b.style && a.edge == b.edge && a.node == b.node && a.s == b.s &&
           a.side == b.side && a.origin == b.origin && a.on_structure == b.on_structure && a.twin_arm == b.twin_arm &&
           a.partner_edge == b.partner_edge;
}

// ------------------------------------------------------------------ junction layer

class JunctionLayer final : public WorldLayer {
public:
    std::string_view name() const override { return kLayerJunctions; }
    const void* state() const override { return &state_; }

    void rebuild_all(const LayerContext& ctx, LayerChanges&) override {
        const RoadGraph& g = ctx.graph();
        state_.junctions = build_plain_junctions(g, ctx.region(), ctx.params().overrides);
        state_.roundabouts = apply_roundabouts(state_.junctions, g, ctx.region(), ctx.params().overrides);
        sort_layouts();
    }

    void update(const LayerContext& ctx, const ObjectChanges& gr, LayerChanges& out) override {
        const RoadGraph& g = ctx.graph();
        std::set<NodeId> rebuild, removed;
        for (NodeId n : gr.nodes) (g.node(n) ? rebuild : removed).insert(n);
        // A ring touched by a dirty node is rebuilt whole: if it stops being a ring, its other nodes would
        // otherwise keep their stale Roundabout junctions.
        for (const RoundaboutLayout& l : state_.roundabouts.layouts) {
            bool hit = false;
            for (NodeId n : l.group.nodes)
                if (gr.nodes.count(n) || !g.node(n)) hit = true;
            if (!hit) continue;
            for (NodeId n : l.group.nodes) (g.node(n) ? rebuild : removed).insert(n);
        }
        JunctionSet part;
        RoundaboutResult rr;
        if (!rebuild.empty()) {  // `only` empty would mean the whole graph
            const std::vector<NodeId> only(rebuild.begin(), rebuild.end());
            part = build_plain_junctions(g, ctx.region(), ctx.params().overrides, only);
            rr = apply_roundabouts(part, g, ctx.region(), ctx.params().overrides, only);
        }
        for (NodeId n : rebuild) state_.junctions.junctions.erase(n);
        for (NodeId n : removed) state_.junctions.junctions.erase(n);
        std::set<NodeId> touched = rebuild;
        for (const RoundaboutLayout& l : rr.layouts)
            for (NodeId n : l.group.nodes) touched.insert(n);

        auto& layouts = state_.roundabouts.layouts;
        for (auto it = layouts.begin(); it != layouts.end();) {
            bool hit = false;
            for (NodeId n : it->group.nodes)
                if (touched.count(n) || removed.count(n) || !g.node(n)) hit = true;
            it = hit ? layouts.erase(it) : it + 1;
        }
        auto& problems = state_.roundabouts.problems;
        for (auto it = problems.begin(); it != problems.end();) {
            bool hit = false;
            for (NodeId n : it->nodes)
                if (touched.count(n) || removed.count(n) || !g.node(n)) hit = true;
            it = hit ? problems.erase(it) : it + 1;
        }
        for (auto& [id, j] : part.junctions) state_.junctions.junctions[id] = std::move(j);
        for (auto& l : rr.layouts) layouts.push_back(std::move(l));
        for (auto& p : rr.problems) problems.push_back(std::move(p));
        sort_layouts();

        out.objects.nodes = touched;
        out.objects.nodes.insert(removed.begin(), removed.end());
    }

private:
    void sort_layouts() {
        auto& l = state_.roundabouts.layouts;
        std::stable_sort(l.begin(), l.end(), [](const RoundaboutLayout& a, const RoundaboutLayout& b) {
            const NodeId x = a.group.nodes.empty() ? NodeId{} : a.group.nodes.front();
            const NodeId y = b.group.nodes.empty() ? NodeId{} : b.group.nodes.front();
            return x < y;
        });
    }
    JunctionState state_;
};

// ------------------------------------------------------------------ road mesh layer

class RoadMeshLayer final : public WorldLayer {
public:
    std::string_view name() const override { return kLayerRoadMesh; }
    std::vector<std::string> reads() const override { return {std::string(kLayerJunctions)}; }

    std::vector<TileKey> tiles() const override {
        std::vector<TileKey> t;
        for (const auto& [k, e] : store_.entries()) t.push_back(k);
        return t;
    }
    std::shared_ptr<const void> tile_data(TileKey t) const override { return store_.get(t); }
    std::uint64_t tile_version(TileKey t) const override { return store_.version(t); }

    void rebuild_all(const LayerContext& ctx, LayerChanges& out) override {
        const JunctionState& js = *ctx.state<JunctionState>(kLayerJunctions);
        const RoadGraph& g = ctx.graph();
        const WorldCacheParams& P = ctx.params();
        foot_.clear();
        std::set<TileKey> ignored;
        for (const auto& [id, e] : g.edges()) foot_.set({kFootEdge, id.value}, edge_foot(e, P.mesh_footprint_margin, P.tile_size), ignored);
        for (const auto& [id, n] : g.nodes()) foot_.set({kFootJoint, id.value}, joint_foot(g, n, P.mesh, P.tile_size), ignored);
        for (const auto& [id, j] : js.junctions.junctions) foot_.set({kFootJunction, id.value}, junction_foot(j, P.tile_size), ignored);
        for (const RoundaboutLayout& l : js.roundabouts.layouts)
            if (!l.group.nodes.empty()) foot_.set({kFootRoundabout, l.group.nodes.front().value}, roundabout_foot(l, P.tile_size), ignored);
        std::map<TileKey, MeshChunk> chunks =
            build_mesh_chunks(g, ctx.region(), js.junctions, &js.roundabouts.layouts, P.mesh, chunk_params(P), nullptr);
        store_.commit_all(chunks, ctx.world_version(), out, [](const MeshChunk& a, const MeshChunk& b) { return same_content(a, b); });
    }

    void update(const LayerContext& ctx, const ObjectChanges& gr, LayerChanges& out) override {
        const ObjectChanges& rj = *ctx.changes_of(kLayerJunctions);
        if (rj.everything || gr.everything) return rebuild_all(ctx, out);
        const JunctionState& js = *ctx.state<JunctionState>(kLayerJunctions);
        const RoadGraph& g = ctx.graph();
        const WorldCacheParams& P = ctx.params();
        std::set<TileKey> cand;

        // Edges: dirty ones, edges at dirty / rebuilt-junction nodes, and any cached edge that no longer exists.
        std::set<EdgeId> edges(gr.edges.begin(), gr.edges.end());
        for (const std::set<NodeId>* ns : {&gr.nodes, &rj.nodes})
            for (NodeId n : *ns)
                for (EdgeId e : g.edges_at(n)) edges.insert(e);
        for (const FootKey& k : foot_.keys_where(kFootEdge, [&](std::uint64_t id) { return g.edge(EdgeId{id}) == nullptr; }))
            edges.insert(EdgeId{k.id});
        for (EdgeId id : edges) {
            const Edge* e = g.edge(id);
            foot_.set({kFootEdge, id.value}, e ? edge_foot(*e, P.mesh_footprint_margin, P.tile_size) : std::set<TileKey>{}, cand);
        }
        // Joints, junctions: nodes whose edges changed or whose junction was rebuilt.
        std::set<NodeId> nodes(gr.nodes.begin(), gr.nodes.end());
        nodes.insert(rj.nodes.begin(), rj.nodes.end());
        std::map<NodeId, const RoundaboutLayout*> by_first;
        for (const RoundaboutLayout& l : js.roundabouts.layouts)
            if (!l.group.nodes.empty()) by_first[l.group.nodes.front()] = &l;
        for (NodeId n : nodes) {
            const Node* nd = g.node(n);
            foot_.set({kFootJoint, n.value}, nd ? joint_foot(g, *nd, P.mesh, P.tile_size) : std::set<TileKey>{}, cand);
            const Junction* j = js.junctions.find(n);
            foot_.set({kFootJunction, n.value}, j ? junction_foot(*j, P.tile_size) : std::set<TileKey>{}, cand);
            const auto it = by_first.find(n);
            foot_.set({kFootRoundabout, n.value}, it != by_first.end() ? roundabout_foot(*it->second, P.tile_size) : std::set<TileKey>{}, cand);
        }
        if (cand.empty()) return;
        std::map<TileKey, MeshChunk> chunks =
            build_mesh_chunks(g, ctx.region(), js.junctions, &js.roundabouts.layouts, P.mesh, chunk_params(P), &cand);
        store_.commit(cand, chunks, ctx.world_version(), out, [](const MeshChunk& a, const MeshChunk& b) { return same_content(a, b); });
    }

private:
    static ChunkParams chunk_params(const WorldCacheParams& P) {
        ChunkParams c;
        c.tile_size = P.tile_size;
        c.merge_surfaces = P.merge_surfaces;
        return c;
    }
    TileStore<MeshChunk> store_;
    Footprints foot_;
};

// ------------------------------------------------------------------ markings layer

struct Owner {
    int kind = 0;  // 0 edge, 1 junction, 2 roundabout splitters (by first ring node): the order of MarkingResult::merged()
    std::uint64_t id = 0;
    friend auto operator<=>(const Owner&, const Owner&) = default;
};

class MarkingsLayer final : public WorldLayer {
public:
    std::string_view name() const override { return kLayerMarkings; }
    std::vector<std::string> reads() const override { return {std::string(kLayerJunctions)}; }

    std::vector<TileKey> tiles() const override {
        std::vector<TileKey> t;
        for (const auto& [k, e] : store_.entries()) t.push_back(k);
        return t;
    }
    std::shared_ptr<const void> tile_data(TileKey t) const override { return store_.get(t); }
    std::uint64_t tile_version(TileKey t) const override { return store_.version(t); }

    void rebuild_all(const LayerContext& ctx, LayerChanges& out) override {
        pieces_.clear();
        tile_owners_.clear();
        const JunctionState& js = *ctx.state<JunctionState>(kLayerJunctions);
        const RoadGraph& g = ctx.graph();
        const MarkingParams mp = params_for(ctx, js);
        std::set<TileKey> ignored;
        for (const auto& [id, e] : g.edges()) regen_edge(ctx, js, mp, id, ignored);
        for (const auto& [id, j] : js.junctions.junctions) regen_junction(ctx, js, mp, id, ignored);
        for (const RoundaboutLayout& l : js.roundabouts.layouts) regen_splitter(ctx, js, mp, l, ignored);
        std::map<TileKey, MarkingChunk> next;
        for (const auto& [t, owners] : tile_owners_) next[t] = assemble(t, owners);
        store_.commit_all(next, ctx.world_version(), out, [](const MarkingChunk& a, const MarkingChunk& b) { return same_content(a, b); });
    }

    void update(const LayerContext& ctx, const ObjectChanges& gr, LayerChanges& out) override {
        const ObjectChanges& rj = *ctx.changes_of(kLayerJunctions);
        if (rj.everything || gr.everything) return rebuild_all(ctx, out);
        const JunctionState& js = *ctx.state<JunctionState>(kLayerJunctions);
        const RoadGraph& g = ctx.graph();
        const MarkingParams mp = params_for(ctx, js);
        std::set<TileKey> cand;

        std::set<NodeId> nodes(gr.nodes.begin(), gr.nodes.end());
        nodes.insert(rj.nodes.begin(), rj.nodes.end());
        std::set<EdgeId> edges(gr.edges.begin(), gr.edges.end());
        for (NodeId n : nodes)
            for (EdgeId e : g.edges_at(n)) edges.insert(e);
        // An edge's markings depend on its whole chain (markings.cpp build_chain: edges joined through plain
        // 2-edge nodes share dash phase, and only the chain's two open ends are cleared by the junctions
        // there), so every edge in the chain of a changed edge is regenerated.
        for (EdgeId e : std::set<EdgeId>(edges)) add_chain(g, e, edges);
        // Owners whose object is gone.
        std::set<NodeId> first_nodes;
        for (const RoundaboutLayout& l : js.roundabouts.layouts)
            if (!l.group.nodes.empty()) first_nodes.insert(l.group.nodes.front());
        std::vector<Owner> gone_splitters;
        for (const auto& [o, p] : pieces_) {
            if (o.kind == 0 && !g.edge(EdgeId{o.id})) edges.insert(EdgeId{o.id});
            if (o.kind == 1 && !js.junctions.find(NodeId{o.id})) nodes.insert(NodeId{o.id});
            if (o.kind == 2 && !first_nodes.count(NodeId{o.id})) gone_splitters.push_back(o);
        }
        for (const Owner& o : gone_splitters) set_owner(o, {}, cand);
        for (EdgeId id : edges) regen_edge(ctx, js, mp, id, cand);
        for (NodeId id : nodes) regen_junction(ctx, js, mp, id, cand);
        for (const RoundaboutLayout& l : js.roundabouts.layouts) {
            bool hit = false;
            for (NodeId n : l.group.nodes)
                if (nodes.count(n)) hit = true;
            if (hit) regen_splitter(ctx, js, mp, l, cand);
        }
        if (cand.empty()) return;
        std::map<TileKey, MarkingChunk> next;
        for (const TileKey& t : cand) {
            const auto it = tile_owners_.find(t);
            if (it != tile_owners_.end()) next[t] = assemble(t, it->second);
        }
        store_.commit(cand, next, ctx.world_version(), out, [](const MarkingChunk& a, const MarkingChunk& b) { return same_content(a, b); });
    }

private:
    using Pieces = std::map<TileKey, MarkingChunk>;

    // Adds `e` and every edge reachable from it through nodes with exactly two edges (a superset of the
    // markings chain: road type and one-way flag are not compared).
    static void add_chain(const RoadGraph& g, EdgeId e, std::set<EdgeId>& out) {
        const Edge* start = g.edge(e);
        if (!start || start->a == start->b) return;
        for (const NodeId from : {start->a, start->b}) {
            NodeId node = from;
            EdgeId cur = e;
            for (int guard = 0; guard < 100000; ++guard) {
                const std::vector<EdgeId> es = g.edges_at(node);
                if (es.size() != 2) break;
                const EdgeId other = es[0] == cur ? es[1] : es[0];
                const Edge* o = g.edge(other);
                if (!o || o->a == o->b || out.count(other) != 0) break;
                out.insert(other);
                node = o->a == node ? o->b : o->a;
                cur = other;
            }
        }
    }

    static MarkingParams params_for(const LayerContext& ctx, const JunctionState& js) {
        MarkingParams mp = ctx.params().markings;
        mp.roundabouts = &js.roundabouts;
        return mp;
    }

    // Replaces the pieces of one owner; the tiles it used to and now covers are added to `dirty`.
    void set_owner(Owner o, Pieces&& now, std::set<TileKey>& dirty) {
        const auto it = pieces_.find(o);
        if (it != pieces_.end()) {
            for (const auto& [t, c] : it->second) {
                dirty.insert(t);
                const auto ot = tile_owners_.find(t);
                if (ot != tile_owners_.end()) {
                    ot->second.erase(o);
                    if (ot->second.empty()) tile_owners_.erase(ot);
                }
            }
            pieces_.erase(it);
        }
        if (now.empty()) return;
        for (const auto& [t, c] : now) {
            dirty.insert(t);
            tile_owners_[t].insert(o);
        }
        pieces_[o] = std::move(now);
    }

    void regen_edge(const LayerContext& ctx, const JunctionState& js, const MarkingParams& mp, EdgeId id, std::set<TileKey>& dirty) {
        Pieces p;
        if (ctx.graph().edge(id))
            p = split(generate_edge_markings(ctx.graph(), ctx.region(), *ctx.rules(), js.junctions, id, mp), ctx);
        set_owner({0, id.value}, std::move(p), dirty);
    }
    void regen_junction(const LayerContext& ctx, const JunctionState& js, const MarkingParams& mp, NodeId id, std::set<TileKey>& dirty) {
        Pieces p;
        if (const Junction* j = js.junctions.find(id))
            p = split(generate_junction_markings(ctx.graph(), ctx.region(), *ctx.rules(), *j, mp), ctx);
        set_owner({1, id.value}, std::move(p), dirty);
    }
    void regen_splitter(const LayerContext& ctx, const JunctionState& js, const MarkingParams& mp, const RoundaboutLayout& l, std::set<TileKey>& dirty) {
        if (l.group.nodes.empty()) return;
        Pieces p = split(generate_splitter_markings(ctx.graph(), ctx.region(), *ctx.rules(), js.junctions, l, mp), ctx);
        set_owner({2, l.group.nodes.front().value}, std::move(p), dirty);
    }

    // One owner's marks, triangulated and split by the tile of each mark's centroid.
    static Pieces split(const MarkingSet& set, const LayerContext& ctx) {
        Pieces out;
        const double ts = ctx.params().tile_size;
        const double lift = ctx.params().marking_lift_m;
        for (const Marking& m : set.marks) {
            const std::vector<std::uint32_t> tri = triangulate_marking(set, m);
            const auto pts = set.points(m);
            if (tri.empty() || pts.empty()) continue;
            double cx = 0, cy = 0;
            for (const Vec3& p : pts) {
                cx += p.x;
                cy += p.y;
            }
            cx /= static_cast<double>(pts.size());
            cy /= static_cast<double>(pts.size());
            const TileKey key = tile_of({cx, cy}, ts);
            MarkingChunk& ch = out[key];
            ch.tile = key;
            ch.origin = tile_origin(key, ts);
            const std::string& cid = set.colour_id(m);
            MarkingBuffers& buf = ch.colours[cid];
            if (buf.positions.empty()) {
                const auto c = ctx.rules()->colours.find(cid);
                if (c != ctx.rules()->colours.end())
                    buf.rgb = {static_cast<float>(c->second.rgb[0]), static_cast<float>(c->second.rgb[1]), static_cast<float>(c->second.rgb[2])};
            }
            const auto base = static_cast<std::uint32_t>(buf.positions.size());
            for (const Vec3& p : pts) {
                // Relative in double, then to float: precision comes from the tile origin.
                const double x = p.x - ch.origin.x, y = p.y - ch.origin.y;
                buf.positions.push_back({static_cast<float>(x), static_cast<float>(y), static_cast<float>(p.z + lift)});
                buf.uv0.push_back({static_cast<float>(x), static_cast<float>(y)});
                buf.wear.push_back(m.wear);
            }
            for (std::size_t i = 0; i + 2 < tri.size(); i += 3) {
                const std::uint32_t a = tri[i], b = tri[i + 1], c = tri[i + 2];
                // Counter-clockwise in plan, whatever the ring orientation.
                const Vec2 ab{pts[b].x - pts[a].x, pts[b].y - pts[a].y};
                const Vec2 ac{pts[c].x - pts[a].x, pts[c].y - pts[a].y};
                const bool ccw = ab.cross(ac) >= 0.0;
                buf.indices.push_back(base + a);
                buf.indices.push_back(base + (ccw ? b : c));
                buf.indices.push_back(base + (ccw ? c : b));
            }
        }
        return out;
    }

    MarkingChunk assemble(TileKey t, const std::set<Owner>& owners) const {
        MarkingChunk out;
        for (const Owner& o : owners) {
            const auto po = pieces_.find(o);
            if (po == pieces_.end()) continue;
            const auto pt = po->second.find(t);
            if (pt == po->second.end()) continue;
            out.tile = pt->second.tile;
            out.origin = pt->second.origin;
            for (const auto& [cid, src] : pt->second.colours) {
                MarkingBuffers& dst = out.colours[cid];
                if (dst.positions.empty()) dst.rgb = src.rgb;
                const auto base = static_cast<std::uint32_t>(dst.positions.size());
                dst.positions.insert(dst.positions.end(), src.positions.begin(), src.positions.end());
                dst.uv0.insert(dst.uv0.end(), src.uv0.begin(), src.uv0.end());
                dst.wear.insert(dst.wear.end(), src.wear.begin(), src.wear.end());
                for (std::uint32_t i : src.indices) dst.indices.push_back(base + i);
            }
        }
        return out;
    }

    std::map<Owner, Pieces> pieces_;
    std::map<TileKey, std::set<Owner>> tile_owners_;
    TileStore<MarkingChunk> store_;
};

// ------------------------------------------------------------------ street lights layer

class StreetLightsLayer final : public WorldLayer {
public:
    std::string_view name() const override { return kLayerStreetLights; }
    std::vector<std::string> reads() const override { return {std::string(kLayerJunctions)}; }

    std::vector<TileKey> tiles() const override {
        std::vector<TileKey> t;
        for (const auto& [k, e] : store_.entries()) t.push_back(k);
        return t;
    }
    std::shared_ptr<const void> tile_data(TileKey t) const override { return store_.get(t); }
    std::uint64_t tile_version(TileKey t) const override { return store_.version(t); }

    void rebuild_all(const LayerContext& ctx, LayerChanges& out) override {
        const JunctionState& js = *ctx.state<JunctionState>(kLayerJunctions);
        cache_ = generate_street_lights(ctx.graph(), ctx.region(), js.junctions, &js.roundabouts.layouts, ctx.params().street_lights);
        commit(ctx, out);
    }

    void update(const LayerContext& ctx, const ObjectChanges& gr, LayerChanges& out) override {
        const ObjectChanges& rj = *ctx.changes_of(kLayerJunctions);
        if (rj.everything || gr.everything) return rebuild_all(ctx, out);
        const JunctionState& js = *ctx.state<JunctionState>(kLayerJunctions);
        std::set<NodeId> nodes(gr.nodes.begin(), gr.nodes.end());
        nodes.insert(rj.nodes.begin(), rj.nodes.end());
        const std::vector<NodeId> dn(nodes.begin(), nodes.end());
        const std::vector<EdgeId> de(gr.edges.begin(), gr.edges.end());
        rebuild_dirty_street_lights(cache_, ctx.graph(), ctx.region(), js.junctions, &js.roundabouts.layouts, dn, de, ctx.params().street_lights);
        commit(ctx, out);
    }

private:
    void commit(const LayerContext& ctx, LayerChanges& out) {
        const double ts = ctx.params().tile_size;
        std::map<TileKey, StreetLightGroup> next;
        const auto add = [&](const std::vector<StreetLight>& v) {
            for (const StreetLight& l : v) {
                const TileKey t = tile_of(l.position.xy(), ts);
                StreetLightGroup& grp = next[t];
                grp.tile = t;
                grp.origin = tile_origin(t, ts);
                grp.lights.push_back(l);
            }
        };
        for (const auto& [id, v] : cache_.edges) add(v);  // the order of StreetLightResult::merged()
        for (const auto& [id, v] : cache_.junctions) add(v);
        for (const auto& [id, v] : cache_.roundabouts) add(v);
        store_.commit_all(next, ctx.world_version(), out, [](const StreetLightGroup& a, const StreetLightGroup& b) { return same_content(a, b); });
    }
    StreetLightResult cache_;
    TileStore<StreetLightGroup> store_;
};

}  // namespace

// ------------------------------------------------------------------ public helpers

bool same_content(const MeshChunk& a, const MeshChunk& b) {
    if (a.tile != b.tile || !same_vec3(a.origin, b.origin) || a.surfaces.size() != b.surfaces.size()) return false;
    auto i = a.surfaces.begin();
    auto j = b.surfaces.begin();
    for (; i != a.surfaces.end(); ++i, ++j) {
        if (i->first != j->first) return false;
        const MeshBuffers &x = i->second, &y = j->second;
        if (!same_bytes(x.positions, y.positions) || !same_bytes(x.normals, y.normals) || !same_bytes(x.tangents, y.tangents) ||
            !same_bytes(x.uv0, y.uv0) || !same_bytes(x.uv1, y.uv1) || !same_bytes(x.colours, y.colours) ||
            !same_bytes(x.surface_ids, y.surface_ids) || !same_bytes(x.indices, y.indices) || !same_sections(x.sections, y.sections))
            return false;
    }
    return true;
}

bool same_content(const MarkingChunk& a, const MarkingChunk& b) {
    if (a.tile != b.tile || !same_vec3(a.origin, b.origin) || a.colours.size() != b.colours.size()) return false;
    auto i = a.colours.begin();
    auto j = b.colours.begin();
    for (; i != a.colours.end(); ++i, ++j) {
        if (i->first != j->first) return false;
        const MarkingBuffers &x = i->second, &y = j->second;
        if (x.rgb != y.rgb || !same_bytes(x.positions, y.positions) || !same_bytes(x.uv0, y.uv0) || !same_bytes(x.wear, y.wear) ||
            !same_bytes(x.indices, y.indices))
            return false;
    }
    return true;
}

bool same_content(const StreetLightGroup& a, const StreetLightGroup& b) {
    if (a.tile != b.tile || !same_vec3(a.origin, b.origin) || a.lights.size() != b.lights.size()) return false;
    for (std::size_t i = 0; i < a.lights.size(); ++i)
        if (!same_light(a.lights[i], b.lights[i])) return false;
    return true;
}

const LayerChanges* ChangeList::find(std::string_view layer) const {
    for (const LayerChanges& l : layers)
        if (l.layer == layer) return &l;
    return nullptr;
}

bool ChangeList::tiles_empty() const {
    for (const LayerChanges& l : layers)
        if (!l.tiles_empty()) return false;
    return true;
}

// ------------------------------------------------------------------ LayerContext

const RoadGraph& LayerContext::graph() const { return cache_->graph_; }
const RegionPack& LayerContext::region() const { return *cache_->region_; }
const MarkingRules* LayerContext::rules() const { return cache_->rules_.get(); }
const WorldCacheParams& LayerContext::params() const { return cache_->params_; }
std::uint64_t LayerContext::world_version() const { return cache_->running_version_; }

const ObjectChanges* LayerContext::changes_of(std::string_view layer) const {
    const WorldCache::Slot* s = cache_->slot(layer);
    return s && s->ran_version == cache_->running_version_ ? &s->last : nullptr;
}

const void* LayerContext::state_of(std::string_view layer) const {
    const WorldCache::Slot* s = cache_->slot(layer);
    return s ? s->layer->state() : nullptr;
}

// ------------------------------------------------------------------ WorldCache

WorldCache::WorldCache(RoadGraph& graph, std::shared_ptr<const RegionPack> region, std::shared_ptr<const MarkingRules> rules,
                       WorldCacheParams params)
    : graph_(graph), region_(std::move(region)), rules_(std::move(rules)), params_(std::move(params)) {
    add_layer(std::make_unique<JunctionLayer>());
    add_layer(std::make_unique<RoadMeshLayer>());
    if (params_.generate_markings && rules_) add_layer(std::make_unique<MarkingsLayer>());
    if (params_.generate_street_lights) add_layer(std::make_unique<StreetLightsLayer>());
}

WorldCache::~WorldCache() = default;

const WorldCache::Slot* WorldCache::slot(std::string_view name) const {
    for (const Slot& s : slots_)
        if (s.layer->name() == name) return &s;
    return nullptr;
}

bool WorldCache::add_layer(std::unique_ptr<WorldLayer> layer, std::string* error) {
    const auto fail = [&](std::string m) {
        if (error) *error = std::move(m);
        return false;
    };
    if (!layer) return fail("null layer");
    if (slot(layer->name())) return fail("layer name already registered: " + std::string(layer->name()));
    for (const std::string& r : layer->reads())
        if (!slot(r)) return fail("layer " + std::string(layer->name()) + " reads " + r + ", which is not registered before it");
    Slot s;
    s.layer = std::move(layer);
    slots_.push_back(std::move(s));
    return true;
}

void WorldCache::set_junction_overrides(JunctionOverrides overrides) {
    params_.overrides = std::move(overrides);
    force_rebuild_ = true;
}

ChangeList WorldCache::update() { return run(false); }
ChangeList WorldCache::rebuild_all() { return run(true); }

ChangeList WorldCache::run(bool force) {
    force = force || force_rebuild_;
    ObjectChanges gc;
    gc.nodes = graph_.dirty_nodes();
    gc.edges = graph_.dirty_edges();
    graph_.clear_dirty();
    bool pending = false;
    for (const Slot& s : slots_)
        if (!s.initialised) pending = true;
    ChangeList cl;
    if (!force && !pending && gc.empty()) {
        cl.world_version = version_;
        return cl;
    }
    force_rebuild_ = false;
    running_version_ = ++version_;
    cl.world_version = version_;
    for (Slot& s : slots_) s.ran_version = 0;
    for (Slot& s : slots_) {
        LayerChanges lc;
        lc.layer = std::string(s.layer->name());
        const LayerContext ctx(*this);
        if (force || !s.initialised) {
            s.layer->rebuild_all(ctx, lc);
            lc.objects = ObjectChanges{};
            lc.objects.everything = true;
            s.initialised = true;
        } else {
            s.layer->update(ctx, gc, lc);
        }
        s.last = lc.objects;
        s.ran_version = running_version_;
        if (!lc.tiles_empty() || !lc.objects.empty()) cl.layers.push_back(std::move(lc));
    }
    return cl;
}

std::vector<std::string> WorldCache::layer_names() const {
    std::vector<std::string> out;
    for (const Slot& s : slots_) out.emplace_back(s.layer->name());
    return out;
}

std::vector<TileKey> WorldCache::tiles(std::string_view layer) const {
    const Slot* s = slot(layer);
    return s ? s->layer->tiles() : std::vector<TileKey>{};
}

std::uint64_t WorldCache::tile_version(std::string_view layer, TileKey tile) const {
    const Slot* s = slot(layer);
    return s ? s->layer->tile_version(tile) : 0;
}

std::shared_ptr<const void> WorldCache::tile_data(std::string_view layer, TileKey t) const {
    const Slot* s = slot(layer);
    return s ? s->layer->tile_data(t) : nullptr;
}

const JunctionSet& WorldCache::junctions() const {
    // The constructor always registers the junction layer first.
    return static_cast<const JunctionState*>(slot(kLayerJunctions)->layer->state())->junctions;
}

const std::vector<RoundaboutLayout>& WorldCache::roundabouts() const {
    return static_cast<const JunctionState*>(slot(kLayerJunctions)->layer->state())->roundabouts.layouts;
}

}  // namespace cp
