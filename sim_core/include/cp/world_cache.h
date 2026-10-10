#pragma once
// The sim-to-render contract (docs/unreal-architecture.md section 2, decision 3): ONE object that owns
// every derived-data cache of a road graph and its dirty set, so the engine side only consumes results.
//
//   RoadGraph (authored, edited through CommandStack)
//        | dirty_nodes() / dirty_edges()            <- WorldCache::update() is their only consumer
//        v
//   layers, in dependency order:  junctions -> road_mesh, markings, street_lights  (+ future layers)
//        |
//        v
//   ChangeList: per layer, tiles added / changed / removed, each with a version; the new content is an
//   immutable snapshot (std::shared_ptr<const T>) the caller can hold as long as it likes.
//
// Rules
//  - Exception-free, no globals, no engine types, region-agnostic (everything comes from the RegionPack,
//    the MarkingRules and WorldCacheParams). Tiles are cp::TileKey; positions are double world metres.
//  - HARD INVARIANT: after any sequence of edits, update() leaves the cache structurally identical to what
//    rebuild_all() produces from scratch: same junction ids and polygons, same roundabout layouts, same
//    markings, same street lights, and bit-identical mesh / marking / light buffers per tile (exact
//    equality, not a tolerance: every object is built by the same pure function in both paths and
//    assembled per tile in the same order). tests/test_world_cache.cpp enforces it with scripted edits,
//    undo/redo and a fuzz test.
//  - update() must be the ONLY consumer of RoadGraph::dirty_*(): it clears them. Anything else that wants to
//    know what changed reads the ChangeList.
//
// Known limits (kept, not hidden)
//  - Street lights also depend on junction polygons that are spatially near an edge but not connected to it
//    (tests/test_edit_loop.cpp header). rebuild_dirty_street_lights widens by one graph hop only, so the
//    invariant for lights holds for planar networks whose roads are 15 m or more apart; the fuzz test keeps
//    that restriction. Everything else (junctions, markings, mesh) has no such restriction.
//  - Junction overrides (corner radii, connectors, ...) are held by the cache; changing them goes through
//    set_junction_overrides(), which schedules a full rebuild (finer-grained dirtying is a later step).
//
// THREAD SAFETY (no threading is added here)
//  - WorldCache itself is single-threaded: update(), rebuild_all(), add_layer(), set_junction_overrides(),
//    junctions(), roundabouts() and every accessor must be called from one thread (the thread that edits the
//    graph), and never while another thread edits the graph. junctions() / roundabouts() return references
//    into the cache: valid until the next update() on that thread; do not hand them to other threads.
//  - SAFE TO SHARE across threads: the std::shared_ptr<const T> returned by tile(), road_mesh(), markings()
//    and street_lights(), and the ChangeList. A snapshot is never mutated after it is published; the next
//    update() builds a NEW object for a changed tile and drops the cache's reference to the old one, so a
//    render thread or async task can keep reading the old snapshot (shared_ptr's reference count is atomic;
//    take the pointer on the cache thread, then pass the pointer). A snapshot owns all its data (no pointers
//    into the graph or the cache).
//  - NOT SAFE: reading the RoadGraph while it is edited, or calling any non-const WorldCache member from
//    two threads. To build off-thread later, copy the graph (or snapshot it) and give the copy its own cache.
//
// Plugging in a new layer (ground / grading, structures, kit instances, vegetation, buildings, ...): derive
// from WorldLayer, name the layers it reads in reads(), keep per-tile output in a TileStore<T>, and call
// WorldCache::add_layer() AFTER the layers it reads. No existing layer is edited. See the test double in
// tests/test_world_cache.cpp ("junction_count") for a complete ~60-line example.
#include <array>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "cp/junction.h"
#include "cp/junction_roundabout.h"
#include "cp/markings.h"
#include "cp/region_pack.h"
#include "cp/render_mesh.h"
#include "cp/road_graph.h"
#include "cp/street_lights.h"
#include "cp/tile_key.h"

namespace cp {

// ------------------------------------------------------------------ change reporting

// Graph objects (by stable id). In a graph delta: the dirty sets, removed ids included (node(id) /
// edge(id) is null for them). In a layer's report: the objects whose derived data was rebuilt (a superset
// of those whose output actually differs).
struct ObjectChanges {
    bool everything = false;  // layer was rebuilt from scratch: treat every object as changed
    std::set<NodeId> nodes;
    std::set<EdgeId> edges;
    bool empty() const { return !everything && nodes.empty() && edges.empty(); }
};

struct TileVersion {
    TileKey tile;
    std::uint64_t version = 0;  // the world version at which this tile's content last changed
};

// What one layer did in one update(). Tiles (or instance groups: a layer decides what its TileKey means,
// always on the global grid of WorldCacheParams::tile_size) are listed only when their content really
// differs from the previous snapshot: a rebuilt tile with identical content is not reported and keeps its
// version. Versions increase strictly for a tile every time it changes (removal then re-adding gives a
// higher version than before the removal).
struct LayerChanges {
    std::string layer;
    std::vector<TileVersion> added;    // tile had no snapshot before
    std::vector<TileVersion> changed;  // snapshot replaced by different content
    std::vector<TileKey> removed;      // tile has no content any more
    ObjectChanges objects;             // see ObjectChanges
    bool tiles_empty() const { return added.empty() && changed.empty() && removed.empty(); }
};

struct ChangeList {
    std::uint64_t world_version = 0;  // increases by one per update() / rebuild_all() that did any work
    std::vector<LayerChanges> layers;  // in layer (dependency) order; only layers that changed something
    const LayerChanges* find(std::string_view layer) const;
    // True when no layer reports a tile added / changed / removed.
    bool tiles_empty() const;
};

// ------------------------------------------------------------------ immutable tile snapshots

// Generic per-tile snapshot store for layer authors. Content is held as std::shared_ptr<const T>; it is
// replaced (never modified) when a tile changes.
template <class T>
class TileStore {
public:
    struct Entry {
        std::shared_ptr<const T> data;
        std::uint64_t version = 0;
    };

    std::shared_ptr<const T> get(TileKey t) const {
        const auto it = entries_.find(t);
        return it == entries_.end() ? nullptr : it->second.data;
    }
    std::uint64_t version(TileKey t) const {
        const auto it = entries_.find(t);
        return it == entries_.end() ? 0 : it->second.version;
    }
    const std::map<TileKey, Entry>& entries() const { return entries_; }
    void clear() { entries_.clear(); }

    // Brings the tiles in `candidates` up to date: for each, `next` holds the new content (absent = the tile
    // is empty). Equal content (per `same`) keeps the old snapshot and version; otherwise a new snapshot with
    // `version` replaces it and the tile is reported in `out`. Tiles not in `candidates` are untouched.
    template <class Same>
    void commit(const std::set<TileKey>& candidates, std::map<TileKey, T>& next, std::uint64_t version, LayerChanges& out,
                Same same) {
        for (const TileKey& t : candidates) {
            const auto old_it = entries_.find(t);
            const auto new_it = next.find(t);
            if (new_it == next.end()) {
                if (old_it != entries_.end()) {
                    entries_.erase(old_it);
                    out.removed.push_back(t);
                }
                continue;
            }
            if (old_it == entries_.end()) {
                entries_[t] = Entry{std::make_shared<const T>(std::move(new_it->second)), version};
                out.added.push_back({t, version});
            } else if (!same(*old_it->second.data, new_it->second)) {
                old_it->second = Entry{std::make_shared<const T>(std::move(new_it->second)), version};
                out.changed.push_back({t, version});
            }
        }
    }
    // Replaces everything: candidates = the old tiles plus the new ones.
    template <class Same>
    void commit_all(std::map<TileKey, T>& next, std::uint64_t version, LayerChanges& out, Same same) {
        std::set<TileKey> c;
        for (const auto& [k, e] : entries_) c.insert(k);
        for (const auto& [k, v] : next) c.insert(k);
        commit(c, next, version, out, same);
    }

private:
    std::map<TileKey, Entry> entries_;
};

// ------------------------------------------------------------------ built-in layer outputs

// Marking triangles of one tile, one buffer per paint colour. Positions are relative to `origin` (the tile's
// south-west corner at z = 0), z already lifted by WorldCacheParams::marking_lift_m. Normals are all +z,
// uv0 = (x, y) metres relative to origin. Triangles are counter-clockwise seen from above in the SIM frame
// (the same rule as MeshBuffers), so the renderer's single frame conversion applies to every layer alike.
// Marks are ordered edges, then junctions, then roundabout splitters, each by id (deterministic).
struct MarkingBuffers {
    std::array<float, 3> rgb = {1, 1, 1};  // sRGB 0..1 from MarkingRules::colours
    std::vector<Float3> positions;
    std::vector<Float2> uv0;
    std::vector<float> wear;               // per vertex, Marking::wear
    std::vector<std::uint32_t> indices;
};
struct MarkingChunk {
    TileKey tile;
    Vec3 origin;
    std::map<std::string, MarkingBuffers> colours;  // by colour id
};

// Street lights whose column base lies in the tile, in StreetLightResult::merged() order. World doubles.
struct StreetLightGroup {
    TileKey tile;
    Vec3 origin;
    std::vector<StreetLight> lights;
};

// Exact (bitwise for floats) content equality; what "unchanged tile" means for change reporting and what the
// invariant test compares.
bool same_content(const MeshChunk& a, const MeshChunk& b);
bool same_content(const MarkingChunk& a, const MarkingChunk& b);
bool same_content(const StreetLightGroup& a, const StreetLightGroup& b);

// ------------------------------------------------------------------ parameters

struct WorldCacheParams {
    double tile_size = 256.0;      // m, the global grid every tile layer uses
    RenderMeshParams mesh;         // chord tolerance etc. (one LOD per cache; a second LOD = a second cache)
    bool merge_surfaces = false;   // ChunkParams::merge_surfaces
    // Mesh dirty tiles are found from object footprints: the centreline (edges), polygons (junctions, islands)
    // or the node (joints) grown by this margin, matching build_mesh_chunks' own filter (30 m). Must cover the
    // widest cross-section half width; values under 26 m are raised to 26 (the centreline sample spacing).
    double mesh_footprint_margin = 30.0;

    bool generate_markings = true;       // also needs MarkingRules (no rules = no markings layer)
    // chord_tolerance 0.01 by default (as CPRoadMeshBuild did). Use MarkingParams::for_mesh_tolerance(mesh.
    // chord_tolerance) for marks that follow coarse mesh LODs. `roundabouts` is set by the cache (any value
    // given is ignored). surface_height, if set, must only read the edge / junction it is asked about.
    MarkingParams markings;
    double marking_lift_m = 0.003;       // z lift of marks above the surface

    bool generate_street_lights = true;
    StreetLightParams street_lights;

    JunctionOverrides overrides;         // see set_junction_overrides()
};

// ------------------------------------------------------------------ layer interface

class WorldCache;

// What a layer can see while it runs. Everything is read-only.
class LayerContext {
public:
    const RoadGraph& graph() const;
    const RegionPack& region() const;
    const MarkingRules* rules() const;  // null when the cache was built without marking rules
    const WorldCacheParams& params() const;
    std::uint64_t world_version() const;  // the version to stamp on outputs produced in this run

    // What an EARLIER layer reported in this run (null if it has not run in this run, e.g. it is later in the
    // order). A layer should list the layers it reads in reads(), which guarantees they ran first.
    const ObjectChanges* changes_of(std::string_view layer) const;
    // The other layer's state() pointer, cast to the type that layer documents. Null if unknown.
    template <class T>
    const T* state(std::string_view layer) const {
        return static_cast<const T*>(state_of(layer));
    }

private:
    friend class WorldCache;
    explicit LayerContext(const WorldCache& c) : cache_(&c) {}
    const void* state_of(std::string_view layer) const;
    const WorldCache* cache_;
};

// One derived-data system. Implement, register with WorldCache::add_layer(). A layer owns its derived data
// and its TileStore(s); the cache owns ordering, dirty-set consumption and the change list.
class WorldLayer {
public:
    virtual ~WorldLayer() = default;
    virtual std::string_view name() const = 0;
    // Names of the layers whose output this layer reads. They must already be registered (add_layer checks).
    virtual std::vector<std::string> reads() const { return {}; }

    // Rebuild everything from scratch, then commit against the previous snapshots so `out` reports only real
    // differences. The cache marks out.objects.everything for downstream layers.
    virtual void rebuild_all(const LayerContext& ctx, LayerChanges& out) = 0;
    // Incremental step. `graph` is the dirty set the cache consumed (removed ids included); earlier layers'
    // reports are in ctx.changes_of(). Must leave the layer exactly as rebuild_all() would.
    virtual void update(const LayerContext& ctx, const ObjectChanges& graph, LayerChanges& out) = 0;

    // Output access (type-erased). `tiles()` lists tiles that currently have a snapshot; tile_data() is the
    // snapshot as shared_ptr<const T> for the layer's documented T (null if none); tile_version() 0 if none.
    virtual std::vector<TileKey> tiles() const { return {}; }
    virtual std::shared_ptr<const void> tile_data(TileKey) const { return nullptr; }
    virtual std::uint64_t tile_version(TileKey) const { return 0; }
    // Pointer to the layer's shared in-memory state for downstream layers (documented per layer; valid until
    // the next update, cache thread only), or null.
    virtual const void* state() const { return nullptr; }
};

// Built-in layer names. Registered by the constructor, in this order.
inline constexpr std::string_view kLayerJunctions = "junctions";          // state(): const JunctionState*; no tiles
inline constexpr std::string_view kLayerRoadMesh = "road_mesh";           // tile_data(): MeshChunk
inline constexpr std::string_view kLayerMarkings = "markings";            // tile_data(): MarkingChunk
inline constexpr std::string_view kLayerStreetLights = "street_lights";   // tile_data(): StreetLightGroup

// State of the junction layer (what the other built-in layers read).
struct JunctionState {
    JunctionSet junctions;          // 3+ arm nodes, roundabout ring nodes (kind = Roundabout) and ring Joints
    RoundaboutResult roundabouts;   // layouts sorted by group.nodes.front(); problems are from the last time each ring was built
};

// ------------------------------------------------------------------ the cache

class WorldCache {
public:
    // `graph` is non-const only because update() clears its dirty sets (the cache is their sole consumer);
    // the cache never edits the graph's content. The graph, which outlives the cache, should have been given
    // the same region pack (RoadGraph::set_region). `rules` may be null (no markings). Nothing is built until
    // the first update() / rebuild_all().
    WorldCache(RoadGraph& graph, std::shared_ptr<const RegionPack> region, std::shared_ptr<const MarkingRules> rules,
               WorldCacheParams params = {});
    ~WorldCache();
    WorldCache(const WorldCache&) = delete;
    WorldCache& operator=(const WorldCache&) = delete;

    // Registers a layer after the ones already present. Returns false (layer dropped, `error` filled) if the
    // name is taken or a layer it reads is not registered yet. A layer added after the first build is built
    // from scratch by the next update().
    bool add_layer(std::unique_ptr<WorldLayer> layer, std::string* error = nullptr);

    // Consumes RoadGraph::dirty_nodes()/dirty_edges() (and clears them) and brings every layer up to date.
    // Cheap when nothing changed (returns an empty list, world_version unchanged). The first call builds
    // everything.
    ChangeList update();
    // From scratch for every layer; the change list is the difference to the previous outputs. Also clears
    // the graph's dirty sets.
    ChangeList rebuild_all();

    std::uint64_t world_version() const { return version_; }

    // Overrides are cache state: replacing them schedules a full rebuild at the next update().
    void set_junction_overrides(JunctionOverrides overrides);

    // ---- outputs
    std::vector<std::string> layer_names() const;
    std::vector<TileKey> tiles(std::string_view layer) const;
    std::uint64_t tile_version(std::string_view layer, TileKey tile) const;
    template <class T>
    std::shared_ptr<const T> tile(std::string_view layer, TileKey t) const {
        return std::static_pointer_cast<const T>(tile_data(layer, t));
    }
    std::shared_ptr<const MeshChunk> road_mesh(TileKey t) const { return tile<MeshChunk>(kLayerRoadMesh, t); }
    std::shared_ptr<const MarkingChunk> markings(TileKey t) const { return tile<MarkingChunk>(kLayerMarkings, t); }
    std::shared_ptr<const StreetLightGroup> street_lights(TileKey t) const {
        return tile<StreetLightGroup>(kLayerStreetLights, t);
    }

    // ---- in-memory state, cache thread only, valid until the next update()
    const JunctionSet& junctions() const;
    const std::vector<RoundaboutLayout>& roundabouts() const;

    const WorldCacheParams& params() const { return params_; }
    const RoadGraph& graph() const { return graph_; }

private:
    friend class LayerContext;
    struct Slot {
        std::unique_ptr<WorldLayer> layer;
        bool initialised = false;
        std::uint64_t ran_version = 0;  // world version of the run that filled `last`
        ObjectChanges last;
    };
    std::shared_ptr<const void> tile_data(std::string_view layer, TileKey t) const;
    const Slot* slot(std::string_view name) const;
    ChangeList run(bool force);

    RoadGraph& graph_;
    std::shared_ptr<const RegionPack> region_;
    std::shared_ptr<const MarkingRules> rules_;
    WorldCacheParams params_;
    std::vector<Slot> slots_;
    std::uint64_t version_ = 0;
    std::uint64_t running_version_ = 0;
    bool force_rebuild_ = false;
};

}  // namespace cp
