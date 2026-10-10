// Sim-side performance benchmark (docs/sim-performance-baseline.md).
// Builds deterministic synthetic networks THROUGH the real commands (CommandStack, as a player would),
// then times every derived-data stage, the edit loop (dirty rebuild vs full), and scaling.
//
// Usage: bench [--gen grid|organic|motorway|all] [--size small|medium|large|all] [--edges N]
//              [--seed S] [--repeats R] [--edit-iters K] [--no-edit] [--quick] [--json <file>] [--data <dir>]
// Tool code: exception-free, like the library. Heap use is measured by a counting global operator new.
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <psapi.h>
#ifdef _MSC_VER
#include <intrin.h>
#pragma comment(lib, "psapi.lib")
#endif
#endif

#include "cp/command.h"
#include "cp/edge_sampling.h"
#include "cp/junction.h"
#include "cp/junction_roundabout.h"
#include "cp/lanes.h"
#include "cp/markings.h"
#include "cp/region_pack.h"
#include "cp/render_mesh.h"
#include "cp/road_commands.h"
#include "cp/road_graph.h"
#include "cp/triangulate.h"

// Optional stages: compile whether or not the headers exist (and whether or not their .cpp is linked).
#define CPB_LIGHTS 0
#define CPB_GRADING 0
#if defined(__has_include)
#if __has_include("cp/street_lights.h")
#include "cp/street_lights.h"
#undef CPB_LIGHTS
#define CPB_LIGHTS 1
#endif
#if __has_include("cp/grading.h")
// grading.h declares its own cp::TileKey, which collides with render_mesh.h's: rename it for this TU.
#define TileKey GradingTileKey
#include "cp/grading.h"
#undef TileKey
#undef CPB_GRADING
#define CPB_GRADING 1
#endif
#endif
#ifndef CP_BENCH_STRUCTURES
#define CP_BENCH_STRUCTURES 0  // set by CMake when sim_core builds structure_mesh.cpp (not measured yet)
#endif

// ---------------------------------------------------------------------------------------------------
// Counting allocator: live bytes, peak since last reset, allocation count. Replaces the global new/delete
// for the whole process (sim_core included). 16-byte header keeps alignment.
// ---------------------------------------------------------------------------------------------------
namespace heap {
struct alignas(16) Hdr {
    std::size_t size;
    std::size_t pad;
};
std::atomic<std::size_t> g_live{0}, g_peak{0}, g_allocs{0};

void* alloc(std::size_t n) {
    void* raw = std::malloc(n + sizeof(Hdr));
    if (!raw) std::abort();
    static_cast<Hdr*>(raw)->size = n;
    const std::size_t l = g_live.fetch_add(n, std::memory_order_relaxed) + n;
    std::size_t p = g_peak.load(std::memory_order_relaxed);
    while (l > p && !g_peak.compare_exchange_weak(p, l, std::memory_order_relaxed)) {
    }
    g_allocs.fetch_add(1, std::memory_order_relaxed);
    return static_cast<Hdr*>(raw) + 1;
}
void release(void* p) {
    if (!p) return;
    Hdr* h = static_cast<Hdr*>(p) - 1;
    g_live.fetch_sub(h->size, std::memory_order_relaxed);
    std::free(h);
}
std::size_t live() { return g_live.load(std::memory_order_relaxed); }
std::size_t peak() { return g_peak.load(std::memory_order_relaxed); }
std::size_t allocs() { return g_allocs.load(std::memory_order_relaxed); }
void reset_peak() { g_peak.store(g_live.load(std::memory_order_relaxed), std::memory_order_relaxed); }
}  // namespace heap

void* operator new(std::size_t n) { return heap::alloc(n); }
void* operator new[](std::size_t n) { return heap::alloc(n); }
void* operator new(std::size_t n, const std::nothrow_t&) noexcept { return heap::alloc(n); }
void* operator new[](std::size_t n, const std::nothrow_t&) noexcept { return heap::alloc(n); }
void operator delete(void* p) noexcept { heap::release(p); }
void operator delete[](void* p) noexcept { heap::release(p); }
void operator delete(void* p, std::size_t) noexcept { heap::release(p); }
void operator delete[](void* p, std::size_t) noexcept { heap::release(p); }
void operator delete(void* p, const std::nothrow_t&) noexcept { heap::release(p); }
void operator delete[](void* p, const std::nothrow_t&) noexcept { heap::release(p); }

namespace {

using Clock = std::chrono::steady_clock;
double now_ms() {
    return std::chrono::duration<double, std::milli>(Clock::now().time_since_epoch()).count();
}
constexpr double kMB = 1024.0 * 1024.0;
constexpr int kSchema = 1;
bool g_trace = false;
int g_dirty_ring = 0;
std::vector<cp::NodeId> g_last_dn;  // diagnostics: the dirty nodes of the last update

// ------------------------------------------------------------------------------------------ OS memory
struct OsMem {
    bool ok = false;
    double peak_ws_mb = 0, private_mb = 0;
};
OsMem os_mem() {
    OsMem m;
#ifdef _WIN32
    PROCESS_MEMORY_COUNTERS_EX pmc{};
    pmc.cb = sizeof(pmc);
    if (GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&pmc), sizeof(pmc))) {
        m.ok = true;
        m.peak_ws_mb = static_cast<double>(pmc.PeakWorkingSetSize) / kMB;
        m.private_mb = static_cast<double>(pmc.PrivateUsage) / kMB;
    }
#endif
    return m;
}

std::string cpu_name() {
#if defined(_WIN32) && defined(_MSC_VER)
    int r[4];
    char b[49] = {};
    __cpuid(r, static_cast<int>(0x80000000));
    if (static_cast<unsigned>(r[0]) < 0x80000004u) return "unknown";
    for (int i = 0; i < 3; ++i) {
        __cpuid(r, static_cast<int>(0x80000002 + static_cast<unsigned>(i)));
        std::memcpy(b + 16 * i, r, 16);
    }
    std::string s = b;
    while (!s.empty() && s.front() == ' ') s.erase(s.begin());
    return s;
#else
    return "unknown";
#endif
}

// ------------------------------------------------------------------------------------------ rng, hash
struct Rng {  // splitmix64, same approach as tests/portable_rng.h
    std::uint64_t s;
    std::uint64_t next() {
        std::uint64_t z = (s += 0x9E3779B97F4A7C15ull);
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
        return z ^ (z >> 31);
    }
    double uniform(double lo, double hi) {
        return lo + (hi - lo) * (static_cast<double>(next() >> 11) * (1.0 / 9007199254740992.0));
    }
    int below(int n) { return n <= 0 ? 0 : static_cast<int>(next() % static_cast<std::uint64_t>(n)); }
    bool chance(double p) { return uniform(0, 1) < p; }
};

struct Hasher {
    std::uint64_t h = 1469598103934665603ull;
    void bytes(const void* p, std::size_t n) {
        const auto* c = static_cast<const unsigned char*>(p);
        for (std::size_t i = 0; i < n; ++i) {
            h ^= c[i];
            h *= 1099511628211ull;
        }
    }
    template <class T>
    void pod(const T& v) {
        bytes(&v, sizeof v);
    }
    void d(double v) {
        if (v == 0) v = 0;  // fold -0
        pod(v);
    }
    void u(std::uint64_t v) { pod(v); }
    void v3(const cp::Vec3& p) {
        d(p.x);
        d(p.y);
        d(p.z);
    }
    void v2(const cp::Vec2& p) {
        d(p.x);
        d(p.y);
    }
    template <class T>
    void vec_pod(const std::vector<T>& v) {
        u(v.size());
        if (!v.empty()) bytes(v.data(), v.size() * sizeof(T));
    }
};

// Analytic terrain: gentle hills (max slope about 3 percent).
double ground(double x, double y) {
    return 8.0 * std::sin(x / 900.0 + 0.7) * std::cos(y / 700.0) + 3.5 * std::sin(x / 260.0 + y / 310.0) +
           1.2 * std::cos(y / 150.0 - x / 430.0);
}
cp::Vec3 at_ground(cp::Vec2 p, double lift = 0) { return {p.x, p.y, ground(p.x, p.y) + lift}; }

struct Stat {
    double min = 0, med = 0, max = 0;
};
Stat stat_of(std::vector<double> v) {
    Stat s;
    if (v.empty()) return s;
    std::sort(v.begin(), v.end());
    s.min = v.front();
    s.max = v.back();
    const std::size_t n = v.size();
    s.med = (n % 2) ? v[n / 2] : 0.5 * (v[n / 2 - 1] + v[n / 2]);
    return s;
}

// ------------------------------------------------------------------------------------------ hashes of outputs
// TileKey changed shape (x, y) -> (i, j) while this tool was written: accept either.
template <class K>
std::int64_t tk_a(const K& k) {
    if constexpr (requires { k.i; }) return static_cast<std::int64_t>(k.i);
    else return static_cast<std::int64_t>(k.x);
}
template <class K>
std::int64_t tk_b(const K& k) {
    if constexpr (requires { k.j; }) return static_cast<std::int64_t>(k.j);
    else return static_cast<std::int64_t>(k.y);
}
template <class K = cp::TileKey>
K tk_make(std::int64_t a, std::int64_t b) {
    K k{};
    if constexpr (requires { k.i; }) {
        k.i = a;
        k.j = b;
    } else {
        k.x = static_cast<decltype(k.x)>(a);
        k.y = static_cast<decltype(k.y)>(b);
    }
    return k;
}

std::uint64_t hash_junctions(const cp::JunctionSet& js) {
    Hasher h;
    for (const auto& [id, j] : js.junctions) {
        h.u(id.value);
        h.u(static_cast<std::uint64_t>(j.cls));
        h.u(static_cast<std::uint64_t>(j.kind));
        h.u(j.degraded ? 1 : 0);
        h.v3(j.pos);
        h.vec_pod(j.trims);
        h.vec_pod(j.corner_radii);
        h.u(j.arms.size());
        for (const auto& a : j.arms) {
            h.u(a.edge.value);
            h.u(a.at_a ? 1 : 0);
            h.d(a.heading_out);
            h.u(a.lanes.size());
        }
        for (const auto& c : j.corners) {
            h.d(c.radius);
            h.u(c.kerb.size());
            for (const auto& p : c.kerb) h.v3(p);
        }
        for (const auto* ps : {&j.carriageway, &j.pavements, &j.gore})
            for (const auto& ring : *ps) {
                h.u(ring.size());
                for (const auto& p : ring) h.v2(p);
            }
        for (const auto& c : j.crossings) {
            h.u(static_cast<std::uint64_t>(c.arm));
            h.d(c.s);
        }
        for (const auto& c : j.connectors) {
            h.u(static_cast<std::uint64_t>(c.arm_from * 1000 + c.arm_to));
            h.u(c.enabled ? 1 : 0);
            h.u(c.path.size());
            for (const auto& p : c.path) h.v3(p);
            h.vec_pod(c.conflicts);
        }
    }
    return h.h;
}

std::uint64_t hash_chunks(const std::map<cp::TileKey, cp::MeshChunk>& chunks) {
    Hasher h;
    for (const auto& [k, c] : chunks) {
        h.u(static_cast<std::uint64_t>(tk_a(k)) * 1000003ull ^ static_cast<std::uint64_t>(tk_b(k)));
        for (const auto& [name, b] : c.surfaces) {
            h.bytes(name.data(), name.size());
            h.vec_pod(b.positions);
            h.vec_pod(b.normals);
            h.vec_pod(b.tangents);
            h.vec_pod(b.uv0);
            h.vec_pod(b.uv1);
            h.vec_pod(b.colours);
            h.vec_pod(b.indices);
            for (const auto& s : b.sections) {
                h.u(s.edge.value);
                h.u(s.node.value);
                h.d(s.s0);
                h.d(s.s1);
                h.u(s.first_index);
                h.u(s.index_count);
            }
        }
    }
    return h.h;
}

std::uint64_t hash_markset(const cp::MarkingSet& ms) {
    Hasher h;
    h.u(ms.vertices.size());
    for (const auto& v : ms.vertices) h.v3(v);
    for (const auto& m : ms.marks) {
        h.u(static_cast<std::uint64_t>(m.category));
        h.u(m.style);
        h.u(m.colour);
        h.u(m.first);
        h.u(m.count);
        h.u(m.strip ? 1 : 0);
        h.u(m.edge.value);
        h.u(m.node.value);
        h.u(static_cast<std::uint64_t>(m.lane + 1));
    }
    return h.h;
}

#if CPB_LIGHTS
std::uint64_t hash_lights(const std::vector<cp::StreetLight>& v) {
    Hasher h;
    for (const auto& l : v) {
        h.v3(l.position);
        h.d(l.heading);
        h.u(l.edge.value);
        h.u(l.node.value);
        h.d(l.s);
        h.u(static_cast<std::uint64_t>(l.side));
        h.u(static_cast<std::uint64_t>(l.origin));
    }
    return h.h;
}
#endif

std::uint64_t hash_graph(const cp::RoadGraph& g) {
    Hasher h;
    for (const auto& [id, n] : g.nodes()) {
        h.u(id.value);
        h.v3(n.pos);
    }
    for (const auto& [id, e] : g.edges()) {
        h.u(id.value);
        h.u(e.a.value);
        h.u(e.b.value);
        h.bytes(e.road_type.data(), e.road_type.size());
        h.u(e.one_way ? 1 : 0);
        h.u(e.interior.size());
        for (const auto& p : e.interior) h.v3(p);
        const double L = e.horizontal.length();
        h.d(L);
        h.d(e.vertical.z(L * 0.5));
        h.u(e.structure.size());
    }
    return h.h;
}

// ------------------------------------------------------------------------------------------ network
struct CmdStats {
    std::map<std::string, std::vector<double>> ms;
    std::map<std::string, int> failed;
};

struct Net {
    std::shared_ptr<const cp::RegionPack> region;
    cp::RoadGraph graph;
    cp::CommandStack<cp::RoadGraph> stack{graph};
    cp::JunctionOverrides overrides;
    CmdStats cmds;
    std::vector<std::string> notes;
    std::string ring_type;
    Net() = default;
    Net(const Net&) = delete;
    Net& operator=(const Net&) = delete;

    bool exec(const std::string& cat, cp::RoadCommandPtr c) {
        const double t0 = now_ms();
        const bool ok = stack.execute(std::move(c));
        const double t1 = now_ms();
        cmds.ms[cat].push_back(t1 - t0);
        if (!ok) ++cmds.failed[cat];
        return ok;
    }

    cp::EdgeId draw(cp::DrawRoadSpec spec) {
        const int splits = (spec.start.split_edge ? 1 : 0) + (spec.end.split_edge ? 1 : 0);
        const char* cat = splits == 0 ? "draw" : splits == 1 ? "draw+1split" : "draw+2split";
        if (!exec(cat, cp::make_draw_road(std::move(spec)))) return {};
        return graph.edges().rbegin()->first;  // ids are monotonic: the drawn edge is the newest
    }

    bool place_roundabout(cp::NodeId node, double radius, std::string* why = nullptr) {
        const cp::Node* n = graph.node(node);
        if (!n) return false;
        const std::string err = cp::check_place_roundabout(graph, n->pos, radius, ring_type, *region);
        if (!err.empty()) {
            if (why) *why = err;
            return false;
        }
        return exec("place_roundabout", cp::make_place_roundabout(n->pos, radius, ring_type, *region));
    }
};

cp::RoadEnd end_new(cp::Vec3 p, std::optional<double> h = {}) {
    cp::RoadEnd r;
    r.pos = p;
    r.heading = h;
    return r;
}
cp::RoadEnd end_node(cp::NodeId n, std::optional<double> h = {}) {
    cp::RoadEnd r;
    r.existing_node = n;
    r.heading = h;
    return r;
}
cp::RoadEnd end_split(cp::EdgeId e, double s, std::optional<double> h = {}) {
    cp::RoadEnd r;
    r.split_edge = e;
    r.split_s = s;
    r.heading = h;
    return r;
}

double edge_heading_at(const cp::Edge& e, double s) {
    double acc = 0;
    for (std::size_t i = 0; i < e.horizontal.spans.size(); ++i) {
        const cp::Clothoid& c = e.horizontal.spans[i];
        if (s <= acc + c.length || i + 1 == e.horizontal.spans.size())
            return c.heading(std::clamp(s - acc, 0.0, c.length));
        acc += c.length;
    }
    return 0;
}

// ------------------------------------------------------------------------------------------ generators
struct GenInfo {
    int target = 0;
    int draw_failed = 0;
    int roundabouts_placed = 0, roundabouts_tried = 0;
    std::string detail;
};

// ---- grid -------------------------------------------------------------------------------------
bool grid_alley(int i, int j) {
    return (j % 2 == 0) && (((static_cast<unsigned>(i) * 73856093u) ^ (static_cast<unsigned>(j) * 19349663u)) % 11u == 0);
}
int grid_edge_estimate(int C, int R) {
    int alleys = 0;
    for (int j = 0; j + 1 < R; ++j)
        for (int i = 0; i + 1 < C; ++i)
            if (grid_alley(i, j)) ++alleys;
    return R * (C - 1) + C * (R - 1) + 3 * alleys;
}

void gen_grid(Net& n, Rng& rng, int target, double bx, double by, GenInfo& info) {
    int R = 3, C = 3;
    for (R = 3; R < 2000; ++R) {
        C = std::max(3, static_cast<int>(std::lround(R * by / bx)));
        if (grid_edge_estimate(C, R) >= target) break;
    }
    info.detail = std::to_string(C) + " x " + std::to_string(R) + " lattice";
    std::vector<cp::NodeId> nid(static_cast<std::size_t>(C) * static_cast<std::size_t>(R));
    auto idx = [&](int i, int j) { return static_cast<std::size_t>(j) * static_cast<std::size_t>(C) + static_cast<std::size_t>(i); };
    std::vector<cp::Vec2> pos(nid.size());
    for (int j = 0; j < R; ++j)
        for (int i = 0; i < C; ++i)
            pos[idx(i, j)] = {i * bx + rng.uniform(-3, 3), j * by + rng.uniform(-3, 3)};
    std::vector<cp::EdgeId> hedge(static_cast<std::size_t>(C) * static_cast<std::size_t>(R));

    auto end_for = [&](int i, int j) {
        const cp::NodeId id = nid[idx(i, j)];
        return id ? end_node(id) : end_new(at_ground(pos[idx(i, j)]));
    };
    auto remember = [&](cp::EdgeId e, int i0, int j0, int i1, int j1) {
        const cp::Edge* ed = e ? n.graph.edge(e) : nullptr;
        if (!ed) {
            ++info.draw_failed;
            return;
        }
        nid[idx(i0, j0)] = ed->a;
        nid[idx(i1, j1)] = ed->b;
    };
    // Horizontal streets (rows), every 5th a B road, every 9th curved.
    for (int j = 0; j < R; ++j) {
        for (int i = 0; i + 1 < C; ++i) {
            cp::DrawRoadSpec s;
            s.road_type = (j % 5 == 0) ? "b_road" : "residential";
            s.start = end_for(i, j);
            s.end = end_for(i + 1, j);
            if (j % 9 == 4) {
                const cp::Vec2 m = (pos[idx(i, j)] + pos[idx(i + 1, j)]) * 0.5;
                s.interior.push_back(at_ground({m.x, m.y + ((i % 2) ? 12.0 : -12.0)}));
            }
            const cp::EdgeId e = n.draw(std::move(s));
            hedge[idx(i, j)] = e;
            remember(e, i, j, i + 1, j);
        }
    }
    // Vertical streets (columns), every 6th a B road, every 7th curved.
    for (int i = 0; i < C; ++i) {
        for (int j = 0; j + 1 < R; ++j) {
            cp::DrawRoadSpec s;
            s.road_type = (i % 6 == 0) ? "b_road" : "residential";
            s.start = end_for(i, j);
            s.end = end_for(i, j + 1);
            if (i % 7 == 3) {
                const cp::Vec2 m = (pos[idx(i, j)] + pos[idx(i, j + 1)]) * 0.5;
                s.interior.push_back(at_ground({m.x + ((j % 2) ? 10.0 : -10.0), m.y}));
            }
            const cp::EdgeId e = n.draw(std::move(s));
            remember(e, i, j, i, j + 1);
        }
    }
    // Mid-block alleys: both ends split a block edge (exercises the split path twice per command).
    for (int j = 0; j + 1 < R; ++j) {
        for (int i = 0; i + 1 < C; ++i) {
            if (!grid_alley(i, j)) continue;
            const cp::EdgeId e0 = hedge[idx(i, j)], e1 = hedge[idx(i, j + 1)];
            const cp::Edge* a = e0 ? n.graph.edge(e0) : nullptr;
            const cp::Edge* b = e1 ? n.graph.edge(e1) : nullptr;
            if (!a || !b) continue;
            cp::DrawRoadSpec s;
            s.road_type = "residential";
            s.start = end_split(e0, a->horizontal.length() * 0.5);
            s.end = end_split(e1, b->horizontal.length() * 0.5);
            n.draw(std::move(s));
        }
    }
}

// ---- spatial hash of road sample points (organic growth clearance test) --------------------------
struct PointGrid {
    double cell = 16;
    std::unordered_map<std::int64_t, std::vector<cp::Vec2>> m;
    static std::int64_t key(std::int64_t ix, std::int64_t iy) { return (ix << 32) ^ (iy & 0xffffffffll); }
    void add(cp::Vec2 p) {
        m[key(static_cast<std::int64_t>(std::floor(p.x / cell)), static_cast<std::int64_t>(std::floor(p.y / cell)))].push_back(p);
    }
    bool close_to(cp::Vec2 p, double r) const {
        const auto cx = static_cast<std::int64_t>(std::floor(p.x / cell)), cy = static_cast<std::int64_t>(std::floor(p.y / cell));
        for (std::int64_t dx = -1; dx <= 1; ++dx)
            for (std::int64_t dy = -1; dy <= 1; ++dy) {
                auto it = m.find(key(cx + dx, cy + dy));
                if (it == m.end()) continue;
                for (const auto& q : it->second)
                    if ((q - p).length() < r) return true;
            }
        return false;
    }
    void add_edge(const cp::Edge& e, double step = 8) {
        const double L = e.horizontal.length();
        for (double s = 0; s <= L; s += step) add(cp::edge_point(e, s).xy());
        add(cp::edge_point(e, L).xy());
    }
};

// ---- corridors (dual carriageway + interchanges) ---------------------------------------------------
struct Corridor {
    virtual ~Corridor() = default;
    virtual cp::Vec2 at(double u, double y) const = 0;  // y: lateral, + = left of the travel direction u
    virtual double heading(double u) const = 0;
    virtual double period() const { return 0; }  // > 0: closed loop of this length
};
struct LineCorridor final : Corridor {
    double y0 = 0, a1 = 0, l1 = 1, a2 = 0, l2 = 1;
    double slope(double u) const { return a1 / l1 * std::cos(u / l1) + a2 / l2 * std::cos(u / l2); }
    double heading(double u) const override { return std::atan2(slope(u), 1.0); }
    cp::Vec2 at(double u, double y) const override {
        const double h = heading(u);
        const cp::Vec2 p{u, y0 + a1 * std::sin(u / l1) + a2 * std::sin(u / l2)};
        return {p.x - y * std::sin(h), p.y + y * std::cos(h)};
    }
};
struct CircleCorridor final : Corridor {  // counter-clockwise, left = inward
    double R = 1000;
    double heading(double u) const override { return u / R + cp::kPi / 2; }
    cp::Vec2 at(double u, double y) const override {
        const double phi = u / R;
        return {(R - y) * std::cos(phi), (R - y) * std::sin(phi)};
    }
    double period() const override { return 2 * cp::kPi * R; }
};

double lift_at(double ay) {  // minor road height above ground at lateral distance |y| from the corridor
    if (ay <= 30) return 7.0;
    if (ay <= 80) return 6.25;
    if (ay <= 130) return 6.25 - (ay - 80) / 50 * 1.75;
    if (ay <= 200) return 4.5 - (ay - 130) / 70 * 3.0;
    if (ay <= 300) return 1.5 - (ay - 200) / 100 * 1.5;
    return 0;
}

struct Interchange {
    double u0 = 0;
    cp::NodeId inner_end;  // optional existing node the inner arm of the minor road attaches to
};

struct CorridorSpec {
    const Corridor* c = nullptr;
    double u_begin = 0, u_end = 0;  // open corridors only
    std::string dual_type;
    double off = 5.5;  // carriageway centre offset from the corridor line
    double seg = 300;  // regular chain spacing
    std::vector<Interchange> ix;
    bool ring_inner = true, ring_outer = false;
    double ring_radius = 25;
};

void draw_chain(Net& n, const Corridor& c, const std::vector<double>& us, bool closed, double lateral, bool reverse,
                const std::string& type, std::vector<cp::NodeId>& nodes) {
    const std::size_t m = us.size();
    nodes.assign(m, cp::NodeId{});
    auto vertex = [&](std::size_t i) {
        double u = us[i % m];
        if (i >= m) u += c.period();
        return at_ground(c.at(u, lateral));
    };
    auto u_of = [&](std::size_t i) { return us[i % m] + (i >= m ? c.period() : 0.0); };
    const std::size_t edges = closed ? m : m - 1;
    for (std::size_t k = 0; k < edges; ++k) {
        const std::size_t i0 = reverse ? (k + 1) : k;   // travel i0 -> i1
        const std::size_t i1 = reverse ? k : (k + 1);
        // For the closed reverse chain the last edge runs vertex 0 -> vertex m-1 (one period earlier).
        std::size_t a = i0, b = i1;
        double ua = u_of(a), ub = u_of(b);
        if (closed && reverse && k + 1 == m) {
            a = 0;
            b = m - 1;
            ua = us[0] + c.period();
            ub = us[m - 1];
        }
        cp::DrawRoadSpec s;
        s.road_type = type;
        s.one_way = true;
        const double hdg_add = reverse ? cp::kPi : 0.0;
        const std::size_t na = a % m, nb = b % m;
        s.start = nodes[na] ? end_node(nodes[na], cp::wrap_angle(c.heading(ua) + hdg_add)) : end_new(vertex(a), cp::wrap_angle(c.heading(ua) + hdg_add));
        s.end = nodes[nb] ? end_node(nodes[nb], cp::wrap_angle(c.heading(ub) + hdg_add)) : end_new(vertex(b), cp::wrap_angle(c.heading(ub) + hdg_add));
        if (closed && reverse && k + 1 == m) {
            s.start.pos = at_ground(c.at(ua, lateral));
            s.end.pos = at_ground(c.at(ub, lateral));
        }
        const double span = std::abs(ub - ua);
        const int extra = span > 140 ? static_cast<int>(span / 120) : 0;
        for (int q = 1; q <= extra; ++q) {
            const double t = static_cast<double>(q) / (extra + 1);
            const double uq = reverse ? ua + (ub - ua) * t : ua + (ub - ua) * t;
            s.interior.push_back(at_ground(c.at(uq, lateral)));
        }
        const cp::EdgeId e = n.draw(std::move(s));
        const cp::Edge* ed = e ? n.graph.edge(e) : nullptr;
        if (!ed) continue;
        nodes[na] = ed->a;
        nodes[nb] = ed->b;
    }
}

struct IxVerts {
    std::size_t minus = 0, plus = 0;  // indices into the vertex list of u0 - slip_len and u0 + slip_len
};

void build_dual_corridor(Net& n, const CorridorSpec& cs, GenInfo& info) {
    const Corridor& c = *cs.c;
    const double slip = 260;
    const bool closed = c.period() > 0;
    const double L = closed ? c.period() : cs.u_end - cs.u_begin;
    const double u_lo = closed ? 0.0 : cs.u_begin;
    // Vertex list: interchange vertices (protected) plus a regular grid clear of them.
    std::vector<double> prot;
    auto wrapu = [&](double u) {
        if (!closed) return u;
        u = std::fmod(u, L);
        return u < 0 ? u + L : u;
    };
    for (const auto& x : cs.ix) {
        prot.push_back(wrapu(x.u0 - slip));
        prot.push_back(wrapu(x.u0 + slip));
    }
    std::sort(prot.begin(), prot.end());
    std::vector<double> us = prot;
    const int count = std::max(1, static_cast<int>(L / cs.seg));
    for (int k = 0; k <= count; ++k) {
        const double u = u_lo + L * k / count;
        if (closed && k == count) break;
        bool clear = true;
        for (double p : prot) {
            double d = std::abs(u - p);
            if (closed) d = std::min(d, L - d);
            if (d < 0.8 * cs.seg) {
                clear = false;
                break;
            }
        }
        if (clear) us.push_back(u);
    }
    std::sort(us.begin(), us.end());
    auto index_of = [&](double u) {
        const double w = wrapu(u);
        std::size_t best = 0;
        double bd = 1e300;
        for (std::size_t i = 0; i < us.size(); ++i)
            if (std::abs(us[i] - w) < bd) {
                bd = std::abs(us[i] - w);
                best = i;
            }
        return best;
    };
    std::vector<cp::NodeId> fwd, rev;
    draw_chain(n, c, us, closed, +cs.off, false, cs.dual_type, fwd);
    draw_chain(n, c, us, closed, -cs.off, true, cs.dual_type, rev);

    for (const auto& x : cs.ix) {
        // Minor road across the corridor with a bridge, terminals at +-80 m.
        const double u0 = x.u0;
        auto P = [&](double y, double lift) { return at_ground(c.at(u0, y), lift); };
        cp::DrawRoadSpec p1;
        p1.road_type = "b_road";
        p1.start = end_new(P(-300, 0));
        p1.end = end_new(P(-80, 6.25));
        for (double y : {-200.0, -130.0}) p1.interior.push_back(P(y, lift_at(std::abs(y))));
        const cp::EdgeId e1 = n.draw(std::move(p1));
        const cp::Edge* a1 = e1 ? n.graph.edge(e1) : nullptr;
        if (!a1) continue;
        const cp::NodeId tminus = a1->b;
        cp::DrawRoadSpec p2;
        p2.road_type = "b_road";
        p2.start = end_node(tminus);
        p2.end = end_new(P(80, 6.25));
        for (double y : {-45.0, -30.0, 30.0, 45.0}) p2.interior.push_back(P(y, lift_at(std::abs(y))));
        const cp::EdgeId e2 = n.draw(std::move(p2));
        const cp::Edge* a2 = e2 ? n.graph.edge(e2) : nullptr;
        if (!a2) continue;
        const cp::NodeId tplus = a2->b;
        const double mid = a2->horizontal.length() * 0.5;
        const double half = cs.off > 8 ? 30.0 : 20.0;
        n.exec("set_structure", cp::make_set_structure_span(e2, mid - half, mid + half, cp::StructureKind::Bridge));
        cp::DrawRoadSpec p3;
        p3.road_type = "b_road";
        p3.start = end_node(tplus);
        p3.end = x.inner_end ? end_node(x.inner_end) : end_new(P(300, 0));
        for (double y : {130.0, 200.0}) p3.interior.push_back(P(y, lift_at(std::abs(y))));
        n.draw(std::move(p3));

        // Slips (fixture shapes, in the corridor's curved frame): local x along the corridor, y lateral.
        const double lifts[5] = {0, 1.5, 3.7, 5.4, 6.25};
        const double ys[5] = {cs.off, 23, 52, 72, 80}, xs[5] = {-slip, -190, -120, -50, 0};
        auto slip_road = [&](bool mirror_x, bool rot180, cp::NodeId from, cp::NodeId to) {
            std::array<cp::Vec3, 5> pts;
            std::array<double, 5> ux{};
            for (int k = 0; k < 5; ++k) {
                double x_ = xs[k], y_ = ys[k];
                if (mirror_x) x_ = -x_;
                if (rot180) {
                    x_ = -x_;
                    y_ = -y_;
                }
                ux[static_cast<std::size_t>(k)] = u0 + x_;
                pts[static_cast<std::size_t>(k)] = at_ground(c.at(u0 + x_, y_), lifts[k]);
            }
            if (mirror_x) {
                std::reverse(pts.begin(), pts.end());
                std::reverse(ux.begin(), ux.end());
            }
            double h0 = 10 * cp::kPi / 180, h1 = 0;
            if (mirror_x) {
                h0 = 0;
                h1 = -10 * cp::kPi / 180;
            }
            if (rot180) {
                h0 = cp::wrap_angle(h0 + cp::kPi);
                h1 = cp::wrap_angle(h1 + cp::kPi);
            }
            cp::DrawRoadSpec s;
            s.road_type = "b_road";
            s.one_way = true;
            s.start = end_node(from, cp::wrap_angle(c.heading(ux.front()) + h0));
            s.end = end_node(to, cp::wrap_angle(c.heading(ux.back()) + h1));
            s.interior.assign(pts.begin() + 1, pts.end() - 1);
            n.draw(std::move(s));
        };
        const std::size_t im = index_of(u0 - slip), ip = index_of(u0 + slip);
        if (fwd[im] && fwd[ip] && rev[im] && rev[ip]) {
            slip_road(false, false, fwd[im], tplus);   // forward off-slip
            slip_road(true, false, tplus, fwd[ip]);    // forward on-slip
            slip_road(false, true, rev[ip], tminus);   // reverse off-slip
            slip_road(true, true, tminus, rev[im]);    // reverse on-slip
        } else {
            ++info.draw_failed;
        }
        if (cs.ring_inner) {
            ++info.roundabouts_tried;
            std::string why;
            if (n.place_roundabout(tplus, cs.ring_radius, &why)) ++info.roundabouts_placed;
            else n.notes.push_back("interchange roundabout (inner) refused: " + why);
        }
        if (cs.ring_outer) {
            ++info.roundabouts_tried;
            std::string why;
            if (n.place_roundabout(tminus, cs.ring_radius, &why)) ++info.roundabouts_placed;
            else n.notes.push_back("interchange roundabout (outer) refused: " + why);
        }
    }
}

// ---- motorway ---------------------------------------------------------------------------------
void gen_motorway(Net& n, Rng& rng, int target, GenInfo& info) {
    const double length = 20000;
    const int K = std::max(1, (target + 429) / 430);  // about 430 edges per 20 km corridor at the 150 m minimum chain spacing
    const int per = target / K;
    const int ix_count = 7;
    const double budget = std::max(40.0, per - ix_count * 23.0);
    const double seg = std::clamp(2 * length / budget, 150.0, 600.0);
    info.detail = std::to_string(K) + " corridor(s) of 20 km, chain spacing " + std::to_string(static_cast<int>(seg)) + " m";
    for (int k = 0; k < K; ++k) {
        LineCorridor lc;
        lc.y0 = k * 2500.0 + rng.uniform(-100, 100);
        lc.a1 = rng.uniform(250, 500);
        lc.l1 = rng.uniform(2800, 4200);
        lc.a2 = rng.uniform(60, 150);
        lc.l2 = rng.uniform(700, 1100);
        CorridorSpec cs;
        cs.c = &lc;
        cs.u_begin = 0;
        cs.u_end = length;
        cs.dual_type = "motorway";
        cs.off = 10.2;
        cs.seg = seg;
        cs.ring_inner = cs.ring_outer = true;
        for (int q = 0; q < ix_count; ++q) cs.ix.push_back({1200.0 + q * 2750.0 + rng.uniform(-150, 150), {}});
        build_dual_corridor(n, cs, info);
    }
}

// ---- organic ----------------------------------------------------------------------------------
void gen_organic(Net& n, Rng& rng, int target, GenInfo& info) {
    const int growth_target = static_cast<int>(target * 0.86);
    const double R_town = std::max(450.0, std::sqrt(growth_target / (cp::kPi * 170e-6)));
    const int arms = std::clamp(6 + target / 4000, 6, 12);
    info.detail = "town radius " + std::to_string(static_cast<int>(R_town)) + " m, " + std::to_string(arms) + " arterials";
    PointGrid grid;
    std::vector<cp::EdgeId> frontier;
    std::vector<double> arm_angle(static_cast<std::size_t>(arms));
    std::vector<cp::NodeId> arm_tip(static_cast<std::size_t>(arms));
    cp::NodeId hub;
    // Arterials: wobbling radial chains of B road from the hub.
    for (int a = 0; a < arms; ++a) {
        const double th = 2 * cp::kPi * a / arms + rng.uniform(-0.12, 0.12);
        arm_angle[static_cast<std::size_t>(a)] = th;
        const double amp = rng.uniform(20, 45), ph = rng.uniform(0, 6.28);
        auto pt = [&](double r) {
            const double w = amp * std::sin(r / 450.0 + ph);
            return cp::Vec2{r * std::cos(th) - w * std::sin(th), r * std::sin(th) + w * std::cos(th)};
        };
        cp::NodeId prev = hub;
        double r = 0;
        while (r < R_town - 50) {
            const double r1 = std::min(R_town, r + rng.uniform(220, 360));
            cp::DrawRoadSpec s;
            s.road_type = "b_road";
            s.start = prev ? end_node(prev) : end_new(at_ground(pt(r)));
            s.end = end_new(at_ground(pt(r1)));
            for (int q = 1; q <= 2; ++q) s.interior.push_back(at_ground(pt(r + (r1 - r) * q / 3.0)));
            const cp::EdgeId e = n.draw(std::move(s));
            const cp::Edge* ed = e ? n.graph.edge(e) : nullptr;
            if (!ed) {
                ++info.draw_failed;
                break;
            }
            if (!hub) hub = ed->a;
            prev = ed->b;
            grid.add_edge(*ed);
            frontier.push_back(e);
            r = r1;
        }
        arm_tip[static_cast<std::size_t>(a)] = prev;
    }
    // Growth: side streets split an existing street, with crossroads, curves and cul-de-sacs.
    auto try_branch = [&](cp::EdgeId parent) -> bool {
        const cp::Edge* pe = n.graph.edge(parent);
        if (!pe || pe->road_type == n.ring_type) return false;
        const double L = pe->horizontal.length();
        if (L < 40) return false;
        const double s = rng.uniform(15, L - 15);
        const cp::Vec2 p = cp::edge_point(*pe, s).xy();
        const double h = edge_heading_at(*pe, s);
        const double side = rng.chance(0.5) ? 1.0 : -1.0;
        const double dir = h + side * (cp::kPi / 2 + rng.uniform(-0.2, 0.2));
        const double len = rng.uniform(55, 140), bend = rng.uniform(-0.35, 0.35);
        auto leg = [&](cp::Vec2 from, double d, double length, double bnd, cp::DrawRoadSpec& spec) {
            const cp::Vec2 end = from + cp::Vec2{std::cos(d + bnd), std::sin(d + bnd)} * length;
            if (end.length() > R_town) return false;
            const cp::Vec2 mid = from + cp::Vec2{std::cos(d + bnd * 0.5), std::sin(d + bnd * 0.5)} * (length * 0.5);
            for (double t = 20; t < length; t += 8) {
                const double f = t / length;
                const cp::Vec2 q = from + cp::Vec2{std::cos(d + bnd * f), std::sin(d + bnd * f)} * t;
                if (grid.close_to(q, 14)) return false;
            }
            if (grid.close_to(end, 14)) return false;
            spec.interior = {at_ground(mid)};
            spec.end = end_new(at_ground(end));
            spec.road_type = rng.chance(0.08) ? "b_road" : "residential";
            return true;
        };
        cp::DrawRoadSpec spec;
        if (!leg(p, dir, len, bend, spec)) return false;
        spec.start = end_split(parent, s);
        const cp::EdgeId ne = n.draw(std::move(spec));
        const cp::Edge* nd = ne ? n.graph.edge(ne) : nullptr;
        if (!nd) {
            ++info.draw_failed;
            return false;
        }
        grid.add_edge(*nd);
        frontier.push_back(ne);
        const cp::NodeId hubn = nd->a;
        for (cp::EdgeId e : n.graph.edges_at(hubn)) frontier.push_back(e);
        if (rng.chance(0.3)) {  // opposite leg: crossroads
            cp::DrawRoadSpec back;
            if (leg(p, dir + cp::kPi, rng.uniform(55, 120), -bend, back)) {
                back.start = end_node(hubn);
                const cp::EdgeId e2 = n.draw(std::move(back));
                if (const cp::Edge* e2d = e2 ? n.graph.edge(e2) : nullptr) {
                    grid.add_edge(*e2d);
                    frontier.push_back(e2);
                }
            }
        }
        return true;
    };
    std::int64_t attempts = 0;
    const std::int64_t max_attempts = static_cast<std::int64_t>(growth_target) * 60;
    while (static_cast<int>(n.graph.edges().size()) < growth_target && attempts < max_attempts && !frontier.empty()) {
        ++attempts;
        const std::size_t k = static_cast<std::size_t>(rng.below(static_cast<int>(frontier.size())));
        const cp::EdgeId e = frontier[k];
        if (!n.graph.edge(e)) {
            frontier[k] = frontier.back();
            frontier.pop_back();
            continue;
        }
        try_branch(e);
    }
    info.detail += ", growth attempts " + std::to_string(attempts);
    if (static_cast<int>(n.graph.edges().size()) < growth_target) info.detail += " (saturated at " + std::to_string(n.graph.edges().size()) + " edges)";

    // Bypass: dual carriageway ring with slip-road interchanges at every arterial.
    double r_max = 0;
    for (const auto& [id, nd] : n.graph.nodes()) r_max = std::max(r_max, nd.pos.xy().length());
    CircleCorridor cc;
    cc.R = r_max + 380;
    CorridorSpec cs;
    cs.c = &cc;
    cs.dual_type = "a_road_dual";
    cs.off = 5.5;
    cs.seg = 420;
    cs.ring_inner = true;
    cs.ring_radius = 25;
    for (int a = 0; a < arms; ++a) {
        double th = arm_angle[static_cast<std::size_t>(a)];
        if (th < 0) th += 2 * cp::kPi;
        cs.ix.push_back({th * cc.R, arm_tip[static_cast<std::size_t>(a)]});
    }
    std::sort(cs.ix.begin(), cs.ix.end(), [](const Interchange& x, const Interchange& y) { return x.u0 < y.u0; });
    build_dual_corridor(n, cs, info);

    // Roundabouts via the place tool: the hub first, then at junctions of the grown town.
    auto try_ring = [&](cp::NodeId node, double radius) {
        ++info.roundabouts_tried;
        std::string why;
        if (n.place_roundabout(node, radius, &why)) {
            ++info.roundabouts_placed;
            return true;
        }
        n.notes.push_back("organic roundabout refused: " + why);
        return false;
    };
    if (hub) try_ring(hub, 30);
    const int want = std::max(2, target / 1500);
    std::vector<cp::NodeId> cand;
    for (const auto& [id, nd] : n.graph.nodes()) {
        if (n.graph.edges_at(id).size() < 3 || nd.pos.xy().length() < 150) continue;
        cand.push_back(id);
    }
    int placed = 0;
    for (int tries = 0; tries < 400 && placed < want && !cand.empty(); ++tries) {
        const std::size_t k = static_cast<std::size_t>(rng.below(static_cast<int>(cand.size())));
        const cp::NodeId id = cand[k];
        if (!n.graph.node(id)) continue;
        if (try_ring(id, 20)) ++placed;
    }
}

// ------------------------------------------------------------------------------------------ records
struct StageRec {
    std::string name;
    std::vector<double> ms;
    double peak_mb = 0, retained_mb = 0, allocs = 0;
    std::vector<std::pair<std::string, double>> out;
    std::uint64_t hash = 0;
    bool has_hash = false, stable = true;
    std::string note;
    void set(const std::string& k, double v) {
        for (auto& o : out)
            if (o.first == k) {
                o.second = v;
                return;
            }
        out.emplace_back(k, v);
    }
};

struct EditRec {
    std::string name;
    int iters = 0;
    std::map<std::string, std::vector<double>> phase_ms;  // by phase
    std::vector<double> dirty_nodes, dirty_edges, dirty_tiles, junctions_rebuilt;
    bool verified = false, verify_ok = true;
    std::string verify_detail;
    int failed = 0;
};

struct NetRun {
    std::string gen, size;
    std::uint64_t seed = 0;
    int requested = 0;
    std::string detail;
    std::size_t nodes = 0, edges = 0, junctions = 0, layouts = 0, degraded = 0, roundabout_problems = 0;
    std::map<std::string, int> classes;
    std::map<std::string, int> degraded_reasons;
    double length_km = 0;
    int draw_failed = 0, roundabouts_tried = 0, roundabouts_placed = 0;
    std::map<std::string, std::vector<double>> cmd_ms;
    std::map<std::string, int> cmd_failed;
    std::uint64_t graph_hash = 0;
    bool graph_hash_stable = true;
    double graph_retained_mb = 0;  // heap held by graph + undo stack
    double os_peak_ws_mb = 0, os_private_mb = 0;
    bool os_ok = false;
    std::vector<StageRec> stages;
    std::vector<EditRec> edits;
    std::vector<std::string> notes;
    std::map<std::string, double> edit_full_ms;  // the full-rebuild counterpart, medians
};

const StageRec* find_stage(const NetRun& r, const std::string& name) {
    for (const auto& s : r.stages)
        if (s.name == name) return &s;
    return nullptr;
}
double med_of(const NetRun& r, const std::string& name) {
    const StageRec* s = find_stage(r, name);
    return s ? stat_of(s->ms).med : 0.0;
}

template <class Clear, class Run>
void run_stage(NetRun& nr, const std::string& name, int reps, Clear clear, Run run) {
    StageRec rec;
    rec.name = name;
    for (int r = 0; r < reps; ++r) {
        clear();
        const std::size_t live0 = heap::live(), a0 = heap::allocs();
        heap::reset_peak();
        const double t0 = now_ms();
        const std::uint64_t h = run(rec);
        const double t1 = now_ms();
        rec.ms.push_back(t1 - t0);
        rec.peak_mb = static_cast<double>(heap::peak() > live0 ? heap::peak() - live0 : 0) / kMB;
        rec.retained_mb = static_cast<double>(heap::live() > live0 ? heap::live() - live0 : 0) / kMB;
        rec.allocs = static_cast<double>(heap::allocs() - a0);
        if (h != 0) {
            if (r == 0) rec.hash = h;
            else if (h != rec.hash) rec.stable = false;
            rec.has_hash = true;
        }
    }
    std::fprintf(stderr, "  %-26s med %9.2f ms\n", name.c_str(), stat_of(rec.ms).med);
    nr.stages.push_back(std::move(rec));
}

// Several sub-timers from one pass (the pass returns its parts in ms, in `names` order).
void run_parts(NetRun& nr, const std::vector<std::string>& names, int reps,
               const std::function<std::vector<double>()>& pass) {
    std::vector<StageRec> recs(names.size());
    for (std::size_t i = 0; i < names.size(); ++i) recs[i].name = names[i];
    for (int r = 0; r < reps; ++r) {
        const std::vector<double> parts = pass();
        for (std::size_t i = 0; i < names.size() && i < parts.size(); ++i) recs[i].ms.push_back(parts[i]);
    }
    for (auto& r : recs) {
        std::fprintf(stderr, "  %-26s med %9.2f ms\n", r.name.c_str(), stat_of(r.ms).med);
        nr.stages.push_back(std::move(r));
    }
}

// ------------------------------------------------------------------------------------------ options
struct Opts {
    std::string gen = "all", size = "all", json, data;
    long edges = 0;
    std::uint64_t seed = 12345;
    int reps = 0;       // 0 = per size default
    int edit_iters = 5;
    bool edit = true;
    bool quick = false;
    int undo_k = 100;
    double edit_tol = 0.1;
};

std::string tol_name(const char* prefix, double t) {
    char b[64];
    std::snprintf(b, sizeof b, "%s_%g", prefix, t);
    return b;
}

// ------------------------------------------------------------------------------------------ derived-data caches
struct Caches {
    cp::JunctionSet js;
    std::vector<cp::RoundaboutLayout> layouts;
    std::map<cp::TileKey, cp::MeshChunk> chunks;  // at the edit tolerance
    cp::MarkingResult marks;
#if CPB_LIGHTS
    cp::StreetLightResult lights;
#endif
};

struct Tools {  // everything the stages need besides the graph
    const cp::RegionPack* region = nullptr;
    const cp::JunctionOverrides* ov = nullptr;
    const cp::MarkingRules* rules = nullptr;
    cp::MarkingParams mp;
};

cp::RenderMeshParams mesh_params(double tol) {
    cp::RenderMeshParams p;
    p.chord_tolerance = tol;
    return p;
}

void full_junctions(const cp::RoadGraph& g, const Tools& t, cp::JunctionSet& js, std::vector<cp::RoundaboutLayout>& layouts) {
    js = cp::build_plain_junctions(g, *t.region, *t.ov);
    cp::RoundaboutResult rr = cp::apply_roundabouts(js, g, *t.region, *t.ov);
    layouts = std::move(rr.layouts);
}

std::size_t mesh_bytes(const std::map<cp::TileKey, cp::MeshChunk>& chunks, std::size_t* tris, std::size_t* verts) {
    std::size_t bytes = 0, tr = 0, vt = 0;
    for (const auto& [k, c] : chunks)
        for (const auto& [name, b] : c.surfaces) {
            bytes += b.positions.size() * sizeof(cp::Float3) + b.normals.size() * sizeof(cp::Float3) + b.tangents.size() * sizeof(cp::Float4) +
                     b.uv0.size() * sizeof(cp::Float2) + b.uv1.size() * sizeof(cp::Float2) + b.colours.size() * 4 + b.indices.size() * 4;
            tr += b.indices.size() / 3;
            vt += b.positions.size();
        }
    if (tris) *tris = tr;
    if (verts) *verts = vt;
    return bytes;
}

#if CPB_GRADING
cp::GradingParams grading_params(const cp::RegionPack& region) { return cp::GradingParams::from_rules(region.grading); }
#endif

// ------------------------------------------------------------------------------------------ edit loop
struct SetRoadType final : cp::RoadCommand {  // the library has no "change road type" command: same primitives as the others
    SetRoadType(cp::EdgeId id, std::string type) : id_(id), type_(std::move(type)) {}
    bool apply(cp::RoadGraph& g) override {
        const cp::Edge* e = g.edge(id_);
        if (!e) return false;
        saved_ = *e;
        cp::Edge n = saved_;
        n.road_type = type_;
        g.erase_edge(id_);
        if (!g.insert_edge(n).valid()) {
            g.insert_edge(saved_);
            return false;
        }
        return true;
    }
    void revert(cp::RoadGraph& g) override {
        g.erase_edge(id_);
        g.insert_edge(saved_);
    }
    std::string name() const override { return "set road type"; }
    cp::EdgeId id_;
    std::string type_;
    cp::Edge saved_;
};

constexpr double kTile = 256.0;

void add_tiles_around(std::set<cp::TileKey>& out, cp::Vec2 p, double margin) {
    const cp::TileKey lo = cp::tile_of({p.x - margin, p.y - margin}, kTile);
    const cp::TileKey hi = cp::tile_of({p.x + margin, p.y + margin}, kTile);
    for (std::int64_t x = tk_a(lo); x <= tk_a(hi); ++x)
        for (std::int64_t y = tk_b(lo); y <= tk_b(hi); ++y) out.insert(tk_make(x, y));
}
void add_edge_tiles(std::set<cp::TileKey>& out, const cp::Edge& e) {
    const double L = e.horizontal.length();
    for (double s = 0; s < L; s += 24.0) add_tiles_around(out, cp::edge_point(e, s).xy(), 30);
    add_tiles_around(out, cp::edge_point(e, L).xy(), 30);
}

struct UpdateTimes {
    double dirty = 0, junc = 0, marks = 0, lights = 0, grading = 0;
    std::map<double, double> mesh;
    std::size_t dn = 0, de = 0, dt = 0, dj = 0;
};

const std::vector<double> kTols = {0.02, 0.1, 0.5};

// Brings every cache back up to date after the graph changed from `before` to net.graph.
void update_caches(Net& net, Caches& c, const Tools& t, const cp::RoadGraph& before, UpdateTimes* ut, double edit_tol,
                   bool all_tols, bool time_grading) {
    const cp::RoadGraph& g = net.graph;
    double t0 = now_ms();
    std::vector<cp::NodeId> dn;
    for (cp::NodeId id : g.dirty_nodes())
        if (g.node(id)) dn.push_back(id);
    std::set<cp::EdgeId> de_set;
    std::set<cp::NodeId> stale_candidates;
    std::set<cp::TileKey> tiles;
    for (cp::NodeId id : g.dirty_nodes()) {
        for (cp::EdgeId e : g.edges_at(id)) {
            de_set.insert(e);
            if (const cp::Edge* ed = g.edge(e)) add_edge_tiles(tiles, *ed);
        }
        if (const cp::Node* nd = g.node(id)) add_tiles_around(tiles, nd->pos.xy(), 60);
        if (const cp::Node* nd = before.node(id)) add_tiles_around(tiles, nd->pos.xy(), 60);
        for (cp::EdgeId e : before.edges_at(id)) {
            de_set.insert(e);  // old edges too: the cache entries of deleted edges must be dropped
            const cp::Edge* ed = before.edge(e);
            if (!ed) continue;
            add_edge_tiles(tiles, *ed);
            stale_candidates.insert(ed->a);
            stale_candidates.insert(ed->b);
        }
    }
    // erase_node() drops a node from dirty_nodes(), so edges whose BOTH ends were removed (a new street between two
    // split nodes, undone) are not reachable from any dirty node: follow removed nodes through the old adjacency.
    for (std::vector<cp::NodeId> work(stale_candidates.begin(), stale_candidates.end()); !work.empty();) {
        const cp::NodeId id = work.back();
        work.pop_back();
        if (g.node(id)) continue;
        for (cp::EdgeId e : before.edges_at(id)) {
            if (!de_set.insert(e).second) continue;
            if (const cp::Edge* ed = before.edge(e)) {
                add_edge_tiles(tiles, *ed);
                if (stale_candidates.insert(ed->a).second) work.push_back(ed->a);
                if (stale_candidates.insert(ed->b).second) work.push_back(ed->b);
            }
        }
    }
    for (int ring = 0; ring < g_dirty_ring; ++ring) {  // experiment: widen the dirty set by whole hops
        std::set<cp::NodeId> have(dn.begin(), dn.end()), add;
        for (cp::NodeId id : dn)
            for (cp::EdgeId e : g.edges_at(id))
                if (const cp::Edge* ed = g.edge(e)) {
                    if (!have.count(ed->a)) add.insert(ed->a);
                    if (!have.count(ed->b)) add.insert(ed->b);
                }
        for (cp::NodeId id : add) {
            dn.push_back(id);
            for (cp::EdgeId e : g.edges_at(id)) {
                de_set.insert(e);
                if (const cp::Edge* ed = g.edge(e)) add_edge_tiles(tiles, *ed);
            }
        }
    }
    std::vector<cp::EdgeId> de(de_set.begin(), de_set.end());
    net.graph.clear_dirty();
    g_last_dn.assign(dn.begin(), dn.end());
    double t1 = now_ms();
    if (ut) ut->dirty = t1 - t0;

    // Junctions (+ roundabout post-pass), merged into the cache.
    t0 = now_ms();
    cp::JunctionSet part = cp::build_plain_junctions(g, *t.region, *t.ov, dn);
    cp::RoundaboutResult rr = cp::apply_roundabouts(part, g, *t.region, *t.ov, dn);
    for (cp::NodeId id : dn) c.js.junctions.erase(id);
    for (cp::NodeId id : stale_candidates)
        if (!g.node(id)) c.js.junctions.erase(id);
    std::set<cp::NodeId> touched(dn.begin(), dn.end());
    for (const auto& l : rr.layouts)
        for (cp::NodeId id : l.group.nodes) touched.insert(id);
    for (auto it = c.layouts.begin(); it != c.layouts.end();) {
        bool hit = false;
        for (cp::NodeId id : it->group.nodes)
            if (touched.count(id) || !g.node(id)) hit = true;
        it = hit ? c.layouts.erase(it) : it + 1;
    }
    const std::size_t rebuilt = part.junctions.size();
    for (auto& [id, j] : part.junctions) c.js.junctions[id] = std::move(j);
    for (auto& l : rr.layouts) c.layouts.push_back(std::move(l));
    t1 = now_ms();
    if (ut) {
        ut->junc = t1 - t0;
        ut->dn = dn.size();
        ut->de = de.size();
        ut->dt = tiles.size();
        ut->dj = rebuilt;
    }

    // Mesh tiles.
    for (double tol : kTols) {
        if (!all_tols && tol != edit_tol) continue;
        t0 = now_ms();
        cp::ChunkParams cpm;
        cpm.tile_size = kTile;
        auto part_chunks = cp::build_mesh_chunks(g, *t.region, c.js, &c.layouts, mesh_params(tol), cpm, &tiles);
        if (tol == edit_tol) {
            for (const auto& k : tiles) c.chunks.erase(k);
            for (auto& [k, v] : part_chunks) c.chunks[k] = std::move(v);
        }
        t1 = now_ms();
        if (ut) ut->mesh[tol] = t1 - t0;
    }

    std::vector<cp::NodeId> dn_all = dn;  // plus the removed nodes, so cache entries keyed by them are dropped
    for (cp::NodeId id : stale_candidates)
        if (!g.node(id)) dn_all.push_back(id);
    // Markings.
    t0 = now_ms();
    cp::rebuild_dirty_markings(c.marks, g, *t.region, *t.rules, c.js, dn_all, de, t.mp);
    t1 = now_ms();
    if (ut) ut->marks = t1 - t0;

#if CPB_LIGHTS
    t0 = now_ms();
    cp::rebuild_dirty_street_lights(c.lights, g, *t.region, c.js, &c.layouts, dn_all, de);
    t1 = now_ms();
    if (ut) ut->lights = t1 - t0;
#endif

#if CPB_GRADING
    if (time_grading) {  // no dirty path: the road list is rebuilt for the whole network
        t0 = now_ms();
        const cp::GradingParams gp = grading_params(*t.region);
        std::vector<cp::SampledRoad> roads = cp::sample_graph_for_grading(g, gp);
        const cp::GroundFn gf = [](double x, double y) { return ground(x, y); };
        auto prepared = cp::prepare_grading_roads(roads, gf, gp);
        t1 = now_ms();
        if (ut) ut->grading = t1 - t0;
    }
#else
    (void)time_grading;
#endif
}

struct FullHashes {
    std::uint64_t junc = 0, chunks = 0, marks = 0, lights = 0;
};

FullHashes hash_caches(const Caches& c) {
    FullHashes h;
    h.junc = hash_junctions(c.js);
    h.chunks = hash_chunks(c.chunks);
    h.marks = hash_markset(c.marks.merged());
#if CPB_LIGHTS
    h.lights = hash_lights(c.lights.merged());
#endif
    return h;
}

void build_caches(Net& net, Caches& c, const Tools& t, double edit_tol) {
    full_junctions(net.graph, t, c.js, c.layouts);
    cp::ChunkParams cpm;
    cpm.tile_size = kTile;
    c.chunks = cp::build_mesh_chunks(net.graph, *t.region, c.js, &c.layouts, mesh_params(edit_tol), cpm);
    c.marks = cp::generate_markings(net.graph, *t.region, *t.rules, c.js, t.mp);
#if CPB_LIGHTS
    c.lights = cp::generate_street_lights(net.graph, *t.region, c.js, &c.layouts);
#endif
}

// Picks the graph elements for the five edit types, near the middle of the network.
struct Middle {
    cp::Vec2 lo, hi;
    bool inside(cp::Vec2 p) const { return p.x >= lo.x && p.x <= hi.x && p.y >= lo.y && p.y <= hi.y; }
};
Middle middle_of(const cp::RoadGraph& g) {
    double x0 = 1e300, y0 = 1e300, x1 = -1e300, y1 = -1e300;
    for (const auto& [id, n] : g.nodes()) {
        x0 = std::min(x0, n.pos.x);
        x1 = std::max(x1, n.pos.x);
        y0 = std::min(y0, n.pos.y);
        y1 = std::max(y1, n.pos.y);
    }
    const double cx = 0.5 * (x0 + x1), cy = 0.5 * (y0 + y1), hx = 0.2 * (x1 - x0), hy = 0.2 * (y1 - y0);
    return {{cx - hx, cy - hy}, {cx + hx, cy + hy}};
}

void run_edit_loop(Net& net, NetRun& nr, const Tools& t, const Opts& o, Rng rng) {
    Caches c;
    std::fprintf(stderr, "  [edit loop] building caches...\n");
    build_caches(net, c, t, o.edit_tol);
    net.graph.clear_dirty();
    const Middle mid = middle_of(net.graph);
    std::vector<cp::EdgeId> mid_edges;
    std::vector<cp::NodeId> mid_junction_nodes;
    for (const auto& [id, e] : net.graph.edges()) {
        const double L = e.horizontal.length();
        if (L < 40 || e.road_type == net.ring_type) continue;
        if (mid.inside(cp::edge_point(e, L * 0.5).xy())) mid_edges.push_back(id);
    }
    for (const auto& [id, nd] : net.graph.nodes())
        if (mid.inside(nd.pos.xy()) && net.graph.edges_at(id).size() >= 3) mid_junction_nodes.push_back(id);
    if (mid_edges.empty()) {
        nr.notes.push_back("edit loop skipped: no suitable edges in the middle of the network");
        return;
    }
    std::map<std::string, int> refusals;  // why the place tool refused candidate sites
    const char* names[5] = {"new street splits two edges", "move one control point", "delete one edge", "change one road type",
                            "place one roundabout"};
    for (int type = 0; type < 5; ++type) {
        EditRec er;
        er.name = names[type];
        int attempts = 0;
        while (er.iters < o.edit_iters && attempts < o.edit_iters * 12) {
            ++attempts;
            auto before = std::make_unique<cp::RoadGraph>(net.graph);
            // Choose and apply the edit.
            double apply_ms = 0;
            bool ok = false;
            const cp::EdgeId pick = mid_edges[static_cast<std::size_t>(rng.below(static_cast<int>(mid_edges.size())))];
            const cp::Edge* pe = net.graph.edge(pick);
            if (!pe) continue;
            const std::size_t depth0 = net.stack.undo_depth();
            const double ta = now_ms();
            switch (type) {
            case 0: {
                const double L1 = pe->horizontal.length();
                const cp::Vec2 m1 = cp::edge_point(*pe, L1 * 0.5).xy();
                cp::EdgeId other;
                for (int tries = 0; tries < 400 && !other; ++tries) {
                    const cp::EdgeId cand = mid_edges[static_cast<std::size_t>(rng.below(static_cast<int>(mid_edges.size())))];
                    const cp::Edge* ce = net.graph.edge(cand);
                    if (!ce || cand == pick || ce->a == pe->a || ce->a == pe->b || ce->b == pe->a || ce->b == pe->b) continue;
                    const double d = (cp::edge_point(*ce, ce->horizontal.length() * 0.5).xy() - m1).length();
                    if (d > 50 && d < 140) other = cand;
                }
                if (!other) break;
                const cp::Edge* oe = net.graph.edge(other);
                cp::DrawRoadSpec s;
                s.road_type = "residential";
                s.start = end_split(pick, L1 * 0.5);
                s.end = end_split(other, oe->horizontal.length() * 0.5);
                ok = net.stack.execute(cp::make_draw_road(std::move(s)));
                break;
            }
            case 1: {
                if (!pe->interior.empty()) {
                    const std::size_t i = static_cast<std::size_t>(rng.below(static_cast<int>(pe->interior.size())));
                    cp::Vec3 to = pe->interior[i];
                    to.x += rng.uniform(-6, 6);
                    to.y += rng.uniform(-6, 6);
                    ok = net.stack.execute(cp::make_move_interior_point(pick, i, to));
                } else {
                    cp::Vec3 to = net.graph.node(pe->a)->pos;
                    to.x += rng.uniform(-4, 4);
                    to.y += rng.uniform(-4, 4);
                    ok = net.stack.execute(cp::make_move_node(pe->a, to));
                }
                break;
            }
            case 2:
                ok = net.stack.execute(cp::make_delete_edge(pick));
                break;
            case 3:
                ok = net.stack.execute(std::make_unique<SetRoadType>(pick, pe->road_type == "residential" ? "b_road" : "residential"));
                break;
            default: {
                if (mid_junction_nodes.empty()) break;
                const cp::NodeId nd = mid_junction_nodes[static_cast<std::size_t>(rng.below(static_cast<int>(mid_junction_nodes.size())))];
                const cp::Node* nn = net.graph.node(nd);
                if (!nn) break;
                if (const std::string why = cp::check_place_roundabout(net.graph, nn->pos, 20, net.ring_type, *t.region); !why.empty()) {
                    ++refusals[why.substr(0, 90)];
                    break;
                }
                ok = net.stack.execute(cp::make_place_roundabout(nn->pos, 20, net.ring_type, *t.region));
                break;
            }
            }
            apply_ms = now_ms() - ta;
            if (!ok) {
                ++er.failed;
                continue;
            }
            // Bring everything up to date and time each phase.
            UpdateTimes ut;
            update_caches(net, c, t, *before, &ut, o.edit_tol, true, true);
            er.phase_ms["apply"].push_back(apply_ms);
            er.phase_ms["dirty_sets"].push_back(ut.dirty);
            er.phase_ms["junctions"].push_back(ut.junc);
            for (const auto& [tol, ms] : ut.mesh) er.phase_ms[tol_name("mesh", tol)].push_back(ms);
            er.phase_ms["markings"].push_back(ut.marks);
            er.phase_ms["lights"].push_back(ut.lights);
            er.phase_ms["grading_whole_net"].push_back(ut.grading);
            er.dirty_nodes.push_back(static_cast<double>(ut.dn));
            er.dirty_edges.push_back(static_cast<double>(ut.de));
            er.dirty_tiles.push_back(static_cast<double>(ut.dt));
            er.junctions_rebuilt.push_back(static_cast<double>(ut.dj));
            if (er.iters == 0) {  // check the incremental result against a from-scratch rebuild
                Caches fresh;
                const double tf = now_ms();
                build_caches(net, fresh, t, o.edit_tol);
                const double full_ms = now_ms() - tf;
                const FullHashes a = hash_caches(c), b = hash_caches(fresh);
                er.verified = true;
                er.verify_ok = a.junc == b.junc && a.chunks == b.chunks && a.marks == b.marks && a.lights == b.lights;
                char buf[160];
                std::snprintf(buf, sizeof buf, "junctions %s, mesh %s, markings %s, lights %s; full rebuild (junctions+mesh %g+markings+lights) %.0f ms",
                              a.junc == b.junc ? "match" : "DIFFER", a.chunks == b.chunks ? "match" : "DIFFER", a.marks == b.marks ? "match" : "DIFFER",
                              a.lights == b.lights ? "match" : "DIFFER", o.edit_tol, full_ms);
                er.verify_detail = buf;
                if (a.marks != b.marks) {  // which entries differ
                    std::size_t de_ = 0, dj_ = 0;
                    std::string ex;
                    for (const auto* side : {&c.marks.edges, &fresh.marks.edges})
                        for (const auto& [id, ms] : *side) {
                            const auto& other = side == &c.marks.edges ? fresh.marks.edges : c.marks.edges;
                            auto it = other.find(id);
                            const std::uint64_t hb = it == other.end() ? 0 : hash_markset(it->second);
                            if (hash_markset(ms) != hb && (side == &c.marks.edges || it == other.end())) {
                                ++de_;
                                if (ex.size() < 200) {
                                    const cp::Edge* ee = net.graph.edge(id);
                                    ex += " e" + std::to_string(id.value);
                                    if (!ee) ex += "(gone)";
                                    else {
                                        auto dirty = [&](cp::NodeId n_) { return std::find(g_last_dn.begin(), g_last_dn.end(), n_) != g_last_dn.end(); };
                                        ex += "(" + ee->road_type + (dirty(ee->a) || dirty(ee->b) ? ", at dirty node" : ", NOT at a dirty node") +
                                              ", arms " + std::to_string(net.graph.edges_at(ee->a).size()) + "/" + std::to_string(net.graph.edges_at(ee->b).size()) + ")";
                                    }
                                }
                            }
                        }
                    for (const auto* side : {&c.marks.junctions, &fresh.marks.junctions})
                        for (const auto& [id, ms] : *side) {
                            const auto& other = side == &c.marks.junctions ? fresh.marks.junctions : c.marks.junctions;
                            auto it = other.find(id);
                            const std::uint64_t hb = it == other.end() ? 0 : hash_markset(it->second);
                            if (hash_markset(ms) != hb && (side == &c.marks.junctions || it == other.end())) ++dj_;
                        }
                    er.verify_detail += " | markings differ in " + std::to_string(de_) + " edge entries, " + std::to_string(dj_) + " junction entries;" + ex;
                }
                er.phase_ms["full_rebuild_post_edit"].push_back(full_ms);
                // Resynchronise from the fresh state so a mismatch does not poison later iterations.
                c = std::move(fresh);
            }
            ++er.iters;
            // Undo and restore the caches (not timed).
            auto mid_graph = std::make_unique<cp::RoadGraph>(net.graph);
            while (net.stack.undo_depth() > depth0) net.stack.undo();
            update_caches(net, c, t, *mid_graph, nullptr, o.edit_tol, false, false);
        }
        std::fprintf(stderr, "  [edit] %-30s %d iterations%s\n", names[type], er.iters, er.verified ? (er.verify_ok ? " (verified)" : " (MISMATCH)") : "");
        nr.edits.push_back(std::move(er));
    }
    for (const auto& [why, n] : refusals) nr.notes.push_back("place-roundabout edit: site refused " + std::to_string(n) + "x: " + why);
}

// resolve_corner_radii gained an optional region argument while this tool was written: use it when present.
template <class Arms>
std::vector<double> corner_radii_for(cp::NodeId id, const Arms& arms, const cp::JunctionOverrides& ov,
                                     const cp::RegionPack& region) {
    if constexpr (requires { cp::resolve_corner_radii(id, arms, ov, &region); }) return cp::resolve_corner_radii(id, arms, ov, &region);
    else return cp::resolve_corner_radii(id, arms, ov);
}

// ------------------------------------------------------------------------------------------ measuring one network
void measure_network(Net& net, NetRun& nr, const Opts& o, int reps, Rng edit_rng) {
    const cp::RegionPack& region = *net.region;
    const cp::JunctionOverrides& ov = net.overrides;
    Tools t;
    t.region = &region;
    t.ov = &ov;
    cp::MarkingRules rules_storage;
    {
        auto rules = cp::load_marking_rules_by_id(o.data, region.id);
        if (rules) rules_storage = *rules;
        else nr.notes.push_back("marking rules not loaded: " + rules.error);
    }
    t.rules = &rules_storage;
    const cp::RoadGraph& g = net.graph;
    auto noop = [] {};

    // --- lanes
    run_stage(nr, "lanes", reps, noop, [&](StageRec& rec) {
        std::size_t lanes = 0;
        Hasher h;
        for (const auto& [id, e] : g.edges()) {
            const cp::RoadType* rt = region.road_type(e.road_type);
            if (!rt) continue;
            const auto l = cp::instantiate_lanes(e, *rt, region.driving_side);
            lanes += l.size();
            h.u(l.size());
            for (const auto& x : l) h.d(x.offset);
        }
        rec.set("lanes", static_cast<double>(lanes));
        return h.h;
    });

    // --- edge sampling at the mesh LOD tolerances
    for (double tol : kTols) {
        run_stage(nr, tol_name("sampling", tol), reps, noop, [&](StageRec& rec) {
            std::size_t cl = 0, off = 0;
            Hasher h;
            cp::SamplingParams sp;
            sp.max_chord_error = tol;
            for (const auto& [id, e] : g.edges()) {
                const cp::RoadType* rt = region.road_type(e.road_type);
                cl += cp::sample_centreline(e, sp).size();
                if (!rt) continue;
                for (const auto& lane : cp::instantiate_lanes(e, *rt, region.driving_side)) {
                    const auto op = cp::sample_offset(e, lane.offset, sp);
                    off += op.samples.size();
                    if (!op.samples.empty()) h.v3(op.samples.back().pos);
                }
            }
            rec.set("centreline_samples", static_cast<double>(cl));
            rec.set("offset_samples", static_cast<double>(off));
            h.u(cl);
            h.u(off);
            return h.h;
        });
    }

    // --- junction sub-stages (same orchestration as build_plain_junctions, timed per part)
    {
        std::map<std::string, double> last;
        run_parts(nr, {"junc_arms_classify", "junc_trims", "junc_connectors", "junc_geometry", "junc_merge_diverge"}, reps, [&] {
            double tc = 0, tt = 0, tcn = 0, tg = 0, tm = 0;
            for (const auto& [id, nd] : g.nodes()) {
                double a = now_ms();
                if (g_trace) std::fprintf(stderr, "node %llu\n", static_cast<unsigned long long>(id.value));
                cp::Junction j;
                j.node = id;
                j.pos = nd.pos;
                j.side = region.driving_side;
                j.arms = cp::collect_arms(g, id, region);
                j.cls = cp::classify_arms(j.arms);
                if (j.arms.size() < 3) {
                    tc += now_ms() - a;
                    continue;
                }
                j.major_arms = cp::major_arm_indices(j.arms);
                j.corner_radii = corner_radii_for(id, j.arms, ov, region);
                double b = now_ms();
                tc += b - a;
                if (j.cls == cp::NodeClass::Merge || j.cls == cp::NodeClass::Diverge) {
                    cp::generate_merge_diverge(j, g, region, ov);
                    tm += now_ms() - b;
                } else {
                    j.trims = cp::compute_trims(j.arms, j.corner_radii);
                    const double c1 = now_ms();
                    tt += c1 - b;
                    cp::generate_connectors(j, j.side, ov, region);
                    const double c2 = now_ms();
                    tcn += c2 - c1;
                    cp::generate_geometry(j, g, region, ov);
                    tg += now_ms() - c2;
                }
            }
            return std::vector<double>{tc, tt, tcn, tg, tm};
        });
    }

    // --- plain junctions, roundabout scan/apply, full build_junctions
    cp::JunctionSet plain;
    run_stage(nr, "junctions_plain", reps, [&] { plain = cp::JunctionSet{}; }, [&](StageRec& rec) {
        plain = cp::build_plain_junctions(g, region, ov);
        rec.set("junctions", static_cast<double>(plain.junctions.size()));
        return hash_junctions(plain);
    });
    run_stage(nr, "roundabout_find", reps, noop, [&](StageRec& rec) {
        const cp::RoundaboutScan sc = cp::find_roundabouts(g, region);
        rec.set("groups", static_cast<double>(sc.groups.size()));
        rec.set("problems", static_cast<double>(sc.problems.size()));
        return static_cast<std::uint64_t>(sc.groups.size() * 131 + sc.problems.size());
    });
    cp::JunctionSet jscopy;
    cp::RoundaboutResult rr;
    run_stage(nr, "roundabout_apply", reps, [&] { jscopy = plain; rr = cp::RoundaboutResult{}; }, [&](StageRec& rec) {
        rr = cp::apply_roundabouts(jscopy, g, region, ov);
        rec.set("layouts", static_cast<double>(rr.layouts.size()));
        rec.set("problems", static_cast<double>(rr.problems.size()));
        return hash_junctions(jscopy);
    });
    run_stage(nr, "junctions_full", reps, noop, [&](StageRec& rec) {
        const cp::JunctionSet s = cp::build_junctions(g, region, ov);
        rec.set("junctions", static_cast<double>(s.junctions.size()));
        return hash_junctions(s);
    });

    Caches c;
    c.js = std::move(jscopy);
    c.layouts = std::move(rr.layouts);
    nr.roundabout_problems = rr.problems.size();
    nr.junctions = c.js.junctions.size();
    nr.layouts = c.layouts.size();
    for (const auto& [id, j] : c.js.junctions) {
        const char* cn = j.cls == cp::NodeClass::Junction ? "junction" : j.cls == cp::NodeClass::Merge ? "merge" : j.cls == cp::NodeClass::Diverge ? "diverge" : j.cls == cp::NodeClass::Joint ? "joint" : "end";
        ++nr.classes[cn];
        if (j.kind == cp::JunctionKind::Roundabout) ++nr.classes["roundabout_ring_node"];
        if (j.degraded) {
            ++nr.degraded;
            ++nr.degraded_reasons[j.degraded_reason.substr(0, 60)];
        }
    }

    // --- mesh chunks at the four tolerances
    for (double tol : {0.02, 0.1, 0.5, 2.0}) {
        std::map<cp::TileKey, cp::MeshChunk> cur;
        run_stage(nr, tol_name("mesh", tol), reps, [&] { cur.clear(); }, [&](StageRec& rec) {
            cp::ChunkParams cpm;
            cpm.tile_size = kTile;
            cur = cp::build_mesh_chunks(g, region, c.js, &c.layouts, mesh_params(tol), cpm);
            std::size_t tris = 0, verts = 0;
            const std::size_t bytes = mesh_bytes(cur, &tris, &verts);
            rec.set("tiles", static_cast<double>(cur.size()));
            rec.set("triangles", static_cast<double>(tris));
            rec.set("vertices", static_cast<double>(verts));
            rec.set("bytes", static_cast<double>(bytes));
            return hash_chunks(cur);
        });
        if (tol == o.edit_tol) c.chunks = std::move(cur);
    }
    // Where the mesh time goes (single pass per repeat, tolerance 0.1 and 0.02).
    for (double tol : {0.02, 0.1}) {
        run_parts(nr, {tol_name("meshpart_edges", tol), tol_name("meshpart_junctions", tol), tol_name("meshpart_joints", tol), tol_name("meshpart_islands", tol)},
                  std::min(reps, 3), [&] {
                      const cp::RenderMeshParams p = mesh_params(tol);
                      double a = now_ms();
                      std::size_t sink = 0;
                      for (const auto& [id, e] : g.edges()) sink += cp::build_edge_mesh(g, region, id, c.js, p).size();
                      double b = now_ms();
                      for (const auto& [id, j] : c.js.junctions) sink += cp::build_junction_mesh(g, region, j, p).size();
                      double d = now_ms();
                      for (const auto& [id, nd] : g.nodes())
                          if (g.edges_at(id).size() == 2) sink += cp::build_joint_mesh(g, region, id, p).size();
                      double e2 = now_ms();
                      for (const auto& l : c.layouts) sink += cp::build_roundabout_mesh(g, region, l, p).size();
                      double f = now_ms();
                      if (sink == static_cast<std::size_t>(-1)) std::fputc(' ', stderr);
                      return std::vector<double>{b - a, d - b, e2 - d, f - e2};
                  });
    }

    // --- markings
    run_stage(nr, "markings", reps, [&] { c.marks = cp::MarkingResult{}; }, [&](StageRec& rec) {
        c.marks = cp::generate_markings(g, region, *t.rules, c.js, t.mp);
        std::size_t marks = 0, verts = 0, warn = 0;
        Hasher h;
        for (const auto& [id, ms] : c.marks.edges) {
            marks += ms.marks.size();
            verts += ms.vertices.size();
            warn += ms.warnings.size();
            h.u(hash_markset(ms));
        }
        for (const auto& [id, ms] : c.marks.junctions) {
            marks += ms.marks.size();
            verts += ms.vertices.size();
            warn += ms.warnings.size();
            h.u(hash_markset(ms));
        }
        rec.set("marks", static_cast<double>(marks));
        rec.set("vertices", static_cast<double>(verts));
        rec.set("bytes", static_cast<double>(verts * sizeof(cp::Vec3) + marks * sizeof(cp::Marking)));
        rec.set("warnings", static_cast<double>(warn));
        return h.h;
    });
    cp::MarkingSet merged;
    run_stage(nr, "markings_merge", reps, [&] { merged = cp::MarkingSet{}; }, [&](StageRec&) {
        merged = c.marks.merged();
        return hash_markset(merged);
    });
    run_stage(nr, "markings_triangulate", reps, noop, [&](StageRec& rec) {
        std::size_t tris = 0, empty = 0;
        for (const auto& m : merged.marks) {
            const auto idx = cp::triangulate_marking(merged, m);
            tris += idx.size() / 3;
            if (idx.empty()) ++empty;
        }
        rec.set("triangles", static_cast<double>(tris));
        rec.set("marks_without_triangles", static_cast<double>(empty));
        rec.set("index_bytes", static_cast<double>(tris * 12));
        return static_cast<std::uint64_t>(tris);
    });

#if CPB_LIGHTS
    run_stage(nr, "street_lights", reps, [&] { c.lights = cp::StreetLightResult{}; }, [&](StageRec& rec) {
        c.lights = cp::generate_street_lights(g, region, c.js, &c.layouts);
        const auto all = c.lights.merged();
        rec.set("lights", static_cast<double>(all.size()));
        rec.set("bytes", static_cast<double>(all.size() * sizeof(cp::StreetLight)));
        return hash_lights(all);
    });
#else
    nr.notes.push_back("street lights: header not present, not measured");
#endif

#if CPB_GRADING
    {
        const cp::GradingParams gp = grading_params(region);
        const cp::GroundFn gf = [](double x, double y) { return ground(x, y); };
        std::vector<cp::SampledRoad> roads;
        run_stage(nr, "grading_sample_roads", reps, noop, [&](StageRec& rec) {
            roads = cp::sample_graph_for_grading(g, gp);
            std::size_t samples = 0;
            for (const auto& r : roads) samples += r.samples.size();
            rec.set("roads", static_cast<double>(roads.size()));
            rec.set("samples", static_cast<double>(samples));
            return static_cast<std::uint64_t>(samples);
        });
        std::vector<cp::GradingRoad> prepared;
        run_stage(nr, "grading_prepare", reps, noop, [&](StageRec& rec) {
            std::vector<cp::SampledRoad> copy = roads;
            prepared = cp::prepare_grading_roads(copy, gf, gp);
            rec.set("prepared_roads", static_cast<double>(prepared.size()));
            return static_cast<std::uint64_t>(prepared.size());
        });
        // Grade 8 tiles of 128 x 128 texels at 0.5 m around the middle of the network.
        const Middle mid = middle_of(g);
        const cp::Vec2 ctr{0.5 * (mid.lo.x + mid.hi.x), 0.5 * (mid.lo.y + mid.hi.y)};
        run_stage(nr, "grading_8_tiles", reps, noop, [&](StageRec& rec) {
            Hasher h;
            for (int k = 0; k < 8; ++k) {
                cp::TileSpec ts;
                ts.nx = ts.ny = 128;
                ts.res = 0.5;
                ts.x0 = std::floor(ctr.x / 64.0) * 64.0 + (k % 4) * 64.0;
                ts.y0 = std::floor(ctr.y / 64.0) * 64.0 + (k / 4) * 64.0;
                const auto natural = cp::sample_natural_tile(gf, ts);
                const auto gt = cp::grade_tile(prepared, gp, ts, natural);
                h.d(static_cast<double>(gt.height[gt.height.size() / 2]));
            }
            rec.set("tiles", 8);
            return h.h;
        });
    }
#else
    nr.notes.push_back("grading: header not present, not measured");
#endif
    if (!CP_BENCH_STRUCTURES) nr.notes.push_back("structure meshes: structure_mesh.cpp not built in sim_core, not measured");

    // --- command undo/redo of the last K commands
    {
        const std::uint64_t h0 = hash_graph(g);
        const int K = static_cast<int>(std::min<std::size_t>(static_cast<std::size_t>(o.undo_k), net.stack.undo_depth()));
        std::vector<double> undo_ms, redo_ms;
        bool same = true;
        for (int r = 0; r < reps; ++r) {
            const double a = now_ms();
            for (int i = 0; i < K; ++i) net.stack.undo();
            const double b = now_ms();
            for (int i = 0; i < K; ++i) net.stack.redo();
            const double d = now_ms();
            undo_ms.push_back(b - a);
            redo_ms.push_back(d - b);
            if (hash_graph(g) != h0) same = false;
        }
        net.graph.clear_dirty();
        StageRec u, rd;
        u.name = "undo_last_" + std::to_string(K);
        u.ms = undo_ms;
        rd.name = "redo_last_" + std::to_string(K);
        rd.ms = redo_ms;
        rd.has_hash = u.has_hash = true;
        rd.stable = u.stable = same;
        rd.hash = u.hash = h0;
        std::fprintf(stderr, "  %-26s med %9.2f ms (%s)\n", u.name.c_str(), stat_of(undo_ms).med, same ? "state restored" : "STATE DIFFERS");
        std::fprintf(stderr, "  %-26s med %9.2f ms\n", rd.name.c_str(), stat_of(redo_ms).med);
        nr.stages.push_back(std::move(u));
        nr.stages.push_back(std::move(rd));
    }

    // Retained heap of the caches (junctions, chunks at the edit tolerance, markings, lights).
    {
        const OsMem m = os_mem();
        nr.os_ok = m.ok;
        nr.os_peak_ws_mb = m.peak_ws_mb;
        nr.os_private_mb = m.private_mb;
    }

    if (o.edit) run_edit_loop(net, nr, t, o, edit_rng);
}

// ------------------------------------------------------------------------------------------ one network end to end
void build_net(Net& net, const std::string& gen, std::uint64_t seed, int target, const Opts& o, GenInfo& info) {
    Rng rng{seed};
    for (const auto& [id, rt] : net.region->road_types) (void)id, (void)rt;
    net.ring_type = cp::default_ring_road_type(*net.region);
    info.target = target;
    if (gen == "grid") gen_grid(net, rng, target, 100, 60, info);
    else if (gen == "organic") gen_organic(net, rng, target, info);
    else gen_motorway(net, rng, target, info);
    (void)o;
}

std::unique_ptr<NetRun> bench_one(const std::string& gen, const std::string& size, int target, const Opts& o,
                                  const std::shared_ptr<const cp::RegionPack>& region, int reps) {
    auto nr = std::make_unique<NetRun>();
    nr->gen = gen;
    nr->size = size;
    nr->seed = o.seed;
    nr->requested = target;
    std::fprintf(stderr, "== %s / %s (target %d edges, %d repeats)\n", gen.c_str(), size.c_str(), target, reps);

    // Network construction, repeated; the last build is kept for the other stages.
    std::unique_ptr<Net> net;
    std::vector<double> build_ms;
    GenInfo info;
    const int build_reps = std::max(1, std::min(reps, size == "large" ? 3 : reps));
    for (int r = 0; r < build_reps; ++r) {
        net.reset();
        const std::size_t live0 = heap::live();
        net = std::make_unique<Net>();
        net->region = region;
        net->graph.set_region(region);
        info = GenInfo{};
        const double t0 = now_ms();
        build_net(*net, gen, o.seed, target, o, info);
        build_ms.push_back(now_ms() - t0);
        nr->graph_retained_mb = static_cast<double>(heap::live() - live0) / kMB;
        const std::uint64_t h = hash_graph(net->graph);
        if (r == 0) nr->graph_hash = h;
        else if (h != nr->graph_hash) nr->graph_hash_stable = false;
        if (r + 1 < build_reps) std::fprintf(stderr, "  build %d/%d: %.0f ms, %zu edges\n", r + 1, build_reps, build_ms.back(), net->graph.edges().size());
    }
    nr->nodes = net->graph.nodes().size();
    nr->edges = net->graph.edges().size();
    nr->detail = info.detail;
    nr->draw_failed = info.draw_failed;
    nr->roundabouts_tried = info.roundabouts_tried;
    nr->roundabouts_placed = info.roundabouts_placed;
    nr->cmd_ms = net->cmds.ms;
    nr->cmd_failed = net->cmds.failed;
    nr->notes = net->notes;

    double len = 0;
    for (const auto& [id, e] : net->graph.edges()) len += e.horizontal.length();
    nr->length_km = len / 1000.0;
    {
        StageRec b;
        b.name = "net_build";
        b.ms = build_ms;
        b.retained_mb = nr->graph_retained_mb;
        b.set("edges", static_cast<double>(nr->edges));
        b.set("commands", static_cast<double>(net->stack.undo_depth()));
        b.hash = nr->graph_hash;
        b.has_hash = true;
        b.stable = nr->graph_hash_stable;
        nr->stages.push_back(std::move(b));
    }
    std::fprintf(stderr, "  built: %zu nodes, %zu edges, %.1f km, %zu commands, %.0f ms, %.1f MB graph+undo; %d draw failures; %d/%d roundabouts\n",
                 nr->nodes, nr->edges, nr->length_km, net->stack.undo_depth(), stat_of(build_ms).med, nr->graph_retained_mb, info.draw_failed,
                 info.roundabouts_placed, info.roundabouts_tried);
    net->graph.clear_dirty();
    measure_network(*net, *nr, o, reps, Rng{o.seed + 1000});
    return nr;
}

// ------------------------------------------------------------------------------------------ scaling
// Least-squares slope of log(median ms) against log(edges) over the sizes of one generator.
double exponent(const std::vector<const NetRun*>& runs, const std::string& stage) {
    std::vector<std::pair<double, double>> pts;
    for (const NetRun* r : runs) {
        const double m = med_of(*r, stage);
        if (m > 0.05 && r->edges > 0) pts.emplace_back(std::log(static_cast<double>(r->edges)), std::log(m));
    }
    if (pts.size() < 2) return std::nan("");
    double sx = 0, sy = 0, sxx = 0, sxy = 0;
    for (const auto& [x, y] : pts) {
        sx += x;
        sy += y;
        sxx += x * x;
        sxy += x * y;
    }
    const double n = static_cast<double>(pts.size());
    const double den = n * sxx - sx * sx;
    return den == 0 ? std::nan("") : (n * sxy - sx * sy) / den;
}

// ------------------------------------------------------------------------------------------ output
class JW {
public:
    void obj() { sep(); s_ += '{'; first_.push_back(true); }
    void end_obj() { s_ += '}'; first_.pop_back(); }
    void arr() { sep(); s_ += '['; first_.push_back(true); }
    void end_arr() { s_ += ']'; first_.pop_back(); }
    void key(const std::string& k) {
        sep();
        str_raw(k);
        s_ += ':';
        pending_ = true;
    }
    void num(double v) {
        sep();
        if (!std::isfinite(v)) {
            s_ += "null";
            return;
        }
        char b[48];
        std::snprintf(b, sizeof b, "%.6g", v);
        s_ += b;
    }
    void str(const std::string& v) {
        sep();
        str_raw(v);
    }
    void boolean(bool v) {
        sep();
        s_ += v ? "true" : "false";
    }
    void hex(std::uint64_t v) {
        char b[24];
        std::snprintf(b, sizeof b, "%016llx", static_cast<unsigned long long>(v));
        str(b);
    }
    const std::string& text() const { return s_; }

private:
    void sep() {
        if (pending_) {
            pending_ = false;
            return;
        }
        if (!first_.empty()) {
            if (!first_.back()) s_ += ',';
            first_.back() = false;
        }
    }
    void str_raw(const std::string& v) {
        s_ += '"';
        for (char ch : v) {
            if (ch == '"' || ch == '\\') {
                s_ += '\\';
                s_ += ch;
            } else if (static_cast<unsigned char>(ch) < 0x20) {
                s_ += ' ';
            } else {
                s_ += ch;
            }
        }
        s_ += '"';
    }
    std::string s_;
    std::vector<bool> first_;
    bool pending_ = false;
};

void write_stat(JW& j, const std::vector<double>& ms) {
    const Stat s = stat_of(ms);
    j.obj();
    j.key("median_ms"); j.num(s.med);
    j.key("min_ms"); j.num(s.min);
    j.key("max_ms"); j.num(s.max);
    j.key("repeats"); j.num(static_cast<double>(ms.size()));
    j.end_obj();
}

void write_json(const std::string& path, const std::vector<std::unique_ptr<NetRun>>& runs, const Opts& o,
                const std::map<std::string, std::vector<const NetRun*>>& by_gen) {
    JW j;
    j.obj();
    j.key("schema"); j.num(kSchema);
    j.key("tool"); j.str("bench");
    j.key("machine");
    j.obj();
    j.key("cpu"); j.str(cpu_name());
    j.key("hardware_threads"); j.num(static_cast<double>(std::thread::hardware_concurrency()));
#ifdef NDEBUG
    j.key("build"); j.str("release");
#else
    j.key("build"); j.str("NOT RELEASE (NDEBUG undefined)");
#endif
    j.end_obj();
    j.key("seed"); j.num(static_cast<double>(o.seed));
    j.key("optional_stages");
    j.obj();
    j.key("street_lights"); j.boolean(CPB_LIGHTS != 0);
    j.key("grading"); j.boolean(CPB_GRADING != 0);
    j.key("structures"); j.boolean(CP_BENCH_STRUCTURES != 0);
    j.end_obj();
    j.key("runs");
    j.arr();
    for (const auto& r : runs) {
        j.obj();
        j.key("generator"); j.str(r->gen);
        j.key("size"); j.str(r->size);
        j.key("requested_edges"); j.num(r->requested);
        j.key("detail"); j.str(r->detail);
        j.key("nodes"); j.num(static_cast<double>(r->nodes));
        j.key("edges"); j.num(static_cast<double>(r->edges));
        j.key("length_km"); j.num(r->length_km);
        j.key("junctions"); j.num(static_cast<double>(r->junctions));
        j.key("roundabout_layouts"); j.num(static_cast<double>(r->layouts));
        j.key("roundabout_problems"); j.num(static_cast<double>(r->roundabout_problems));
        j.key("degraded_junctions"); j.num(static_cast<double>(r->degraded));
        j.key("degraded_share"); j.num(r->junctions ? static_cast<double>(r->degraded) / static_cast<double>(r->junctions) : 0.0);
        j.key("classes");
        j.obj();
        for (const auto& [k, v] : r->classes) { j.key(k); j.num(v); }
        j.end_obj();
        j.key("degraded_reasons");
        j.obj();
        for (const auto& [k, v] : r->degraded_reasons) { j.key(k); j.num(v); }
        j.end_obj();
        j.key("draw_failures"); j.num(r->draw_failed);
        j.key("roundabouts_tried"); j.num(r->roundabouts_tried);
        j.key("roundabouts_placed"); j.num(r->roundabouts_placed);
        j.key("graph_hash"); j.hex(r->graph_hash);
        j.key("graph_hash_stable_across_builds"); j.boolean(r->graph_hash_stable);
        j.key("graph_retained_mb"); j.num(r->graph_retained_mb);
        j.key("os_memory_measured"); j.boolean(r->os_ok);
        j.key("os_peak_working_set_mb"); j.num(r->os_ok ? r->os_peak_ws_mb : std::nan(""));
        j.key("os_private_mb"); j.num(r->os_ok ? r->os_private_mb : std::nan(""));
        j.key("commands");
        j.obj();
        for (const auto& [cat, v] : r->cmd_ms) {
            std::vector<double> sorted = v;
            std::sort(sorted.begin(), sorted.end());
            j.key(cat);
            j.obj();
            j.key("count"); j.num(static_cast<double>(v.size()));
            j.key("median_ms"); j.num(stat_of(v).med);
            j.key("p95_ms"); j.num(sorted.empty() ? 0.0 : sorted[std::min(sorted.size() - 1, static_cast<std::size_t>(0.95 * static_cast<double>(sorted.size())))]);
            j.key("max_ms"); j.num(sorted.empty() ? 0.0 : sorted.back());
            double sum = 0;
            for (double x : v) sum += x;
            j.key("total_ms"); j.num(sum);
            auto f = r->cmd_failed.find(cat);
            j.key("failed"); j.num(f == r->cmd_failed.end() ? 0 : f->second);
            j.end_obj();
        }
        j.end_obj();
        j.key("stages");
        j.obj();
        for (const auto& s : r->stages) {
            j.key(s.name);
            j.obj();
            j.key("time"); write_stat(j, s.ms);
            j.key("peak_heap_mb"); j.num(s.peak_mb);
            j.key("retained_heap_mb"); j.num(s.retained_mb);
            j.key("allocations"); j.num(s.allocs);
            if (s.has_hash) {
                j.key("hash"); j.hex(s.hash);
                j.key("hash_stable_across_repeats"); j.boolean(s.stable);
            }
            j.key("outputs");
            j.obj();
            for (const auto& [k, v] : s.out) { j.key(k); j.num(v); }
            j.end_obj();
            j.end_obj();
        }
        j.end_obj();
        j.key("edits");
        j.arr();
        for (const auto& e : r->edits) {
            j.obj();
            j.key("name"); j.str(e.name);
            j.key("iterations"); j.num(e.iters);
            j.key("failed_attempts"); j.num(e.failed);
            j.key("verified_against_full_rebuild"); j.boolean(e.verified);
            j.key("verify_ok"); j.boolean(e.verify_ok);
            j.key("verify_detail"); j.str(e.verify_detail);
            j.key("dirty_nodes_median"); j.num(stat_of(e.dirty_nodes).med);
            j.key("dirty_edges_median"); j.num(stat_of(e.dirty_edges).med);
            j.key("dirty_tiles_median"); j.num(stat_of(e.dirty_tiles).med);
            j.key("junctions_rebuilt_median"); j.num(stat_of(e.junctions_rebuilt).med);
            j.key("phases");
            j.obj();
            for (const auto& [k, v] : e.phase_ms) { j.key(k); write_stat(j, v); }
            j.end_obj();
            j.end_obj();
        }
        j.end_arr();
        j.key("notes");
        j.arr();
        for (const auto& n : r->notes) j.str(n);
        j.end_arr();
        j.end_obj();
    }
    j.end_arr();
    j.key("scaling_exponents");
    j.obj();
    for (const auto& [gen, rs] : by_gen) {
        if (rs.size() < 2) continue;
        j.key(gen);
        j.obj();
        j.key("edges");
        j.arr();
        for (const NetRun* r : rs) j.num(static_cast<double>(r->edges));
        j.end_arr();
        for (const auto& s : rs.back()->stages) {
            const double e = exponent(rs, s.name);
            if (std::isfinite(e)) { j.key(s.name); j.num(e); }
        }
        j.end_obj();
    }
    j.end_obj();
    j.end_obj();
    std::FILE* f = nullptr;
#ifdef _MSC_VER
    if (fopen_s(&f, path.c_str(), "wb") != 0) f = nullptr;
#else
    f = std::fopen(path.c_str(), "wb");
#endif
    if (!f) {
        std::fprintf(stderr, "bench: cannot write %s\n", path.c_str());
        return;
    }
    std::fwrite(j.text().data(), 1, j.text().size(), f);
    std::fputc('\n', f);
    std::fclose(f);
}

std::string fmt_out(const StageRec& s) {
    std::string r;
    for (const auto& [k, v] : s.out) {
        char b[64];
        std::snprintf(b, sizeof b, "%s%s=%.0f", r.empty() ? "" : " ", k.c_str(), v);
        r += b;
    }
    return r;
}

void print_run(const NetRun& r) {
    std::printf("\n=== %s / %s: %zu nodes, %zu edges, %.1f km, %zu junctions (%zu roundabout layouts), %zu degraded (%.1f%%) ===\n", r.gen.c_str(),
                r.size.c_str(), r.nodes, r.edges, r.length_km, r.junctions, r.layouts, r.degraded,
                r.junctions ? 100.0 * static_cast<double>(r.degraded) / static_cast<double>(r.junctions) : 0.0);
    std::printf("%s\n", r.detail.c_str());
    std::printf("classes:");
    for (const auto& [k, v] : r.classes) std::printf(" %s=%d", k.c_str(), v);
    std::printf("\ndegraded reasons:");
    for (const auto& [k, v] : r.degraded_reasons) std::printf(" [%s]=%d", k.c_str(), v);
    std::printf("\ndraw failures %d, roundabouts placed %d/%d, graph+undo heap %.1f MB%s\n", r.draw_failed, r.roundabouts_placed, r.roundabouts_tried,
                r.graph_retained_mb, r.graph_hash_stable ? "" : "  GRAPH HASH UNSTABLE");
    if (r.os_ok) std::printf("OS: peak working set %.0f MB, private %.0f MB\n", r.os_peak_ws_mb, r.os_private_mb);
    std::printf("\ncommands:        n      med ms     p95 ms     max ms    total ms  failed\n");
    for (const auto& [cat, v] : r.cmd_ms) {
        std::vector<double> sorted = v;
        std::sort(sorted.begin(), sorted.end());
        double sum = 0;
        for (double x : v) sum += x;
        auto f = r.cmd_failed.find(cat);
        std::printf("  %-18s %6zu %10.3f %10.3f %10.3f %11.1f %6d\n", cat.c_str(), v.size(), stat_of(v).med,
                    sorted.empty() ? 0.0 : sorted[std::min(sorted.size() - 1, static_cast<std::size_t>(0.95 * static_cast<double>(sorted.size())))],
                    sorted.empty() ? 0.0 : sorted.back(), sum, f == r.cmd_failed.end() ? 0 : f->second);
    }
    std::printf("\n%-26s %10s %10s %10s %9s %9s %10s  %s\n", "stage", "med ms", "min", "max", "peak MB", "kept MB", "allocs", "outputs");
    for (const auto& s : r.stages) {
        const Stat st = stat_of(s.ms);
        std::printf("%-26s %10.2f %10.2f %10.2f %9.1f %9.1f %10.0f  %s%s\n", s.name.c_str(), st.med, st.min, st.max, s.peak_mb, s.retained_mb, s.allocs,
                    fmt_out(s).c_str(), (s.has_hash && !s.stable) ? "  NONDETERMINISTIC" : "");
    }
    if (!r.edits.empty()) {
        std::printf("\nedit loop (median of N, ms): dirty path, per phase\n");
        std::printf("%-30s %3s %6s %6s %6s %6s %8s %8s %8s %8s %8s %8s %8s\n", "edit", "N", "dnodes", "dedges", "tiles", "juncs", "apply", "junc",
                    "mesh.02", "mesh.1", "mesh.5", "marks", "lights");
        for (const auto& e : r.edits) {
            auto m = [&](const std::string& k) {
                auto it = e.phase_ms.find(k);
                return it == e.phase_ms.end() ? 0.0 : stat_of(it->second).med;
            };
            std::printf("%-30s %3d %6.0f %6.0f %6.0f %6.0f %8.2f %8.2f %8.1f %8.1f %8.1f %8.2f %8.2f  grading-whole-net %.0f ms  dirty-sets %.2f ms%s\n",
                        e.name.c_str(), e.iters, stat_of(e.dirty_nodes).med, stat_of(e.dirty_edges).med, stat_of(e.dirty_tiles).med,
                        stat_of(e.junctions_rebuilt).med, m("apply"), m("junctions"), m("mesh_0.02"), m("mesh_0.1"), m("mesh_0.5"), m("markings"), m("lights"),
                        m("grading_whole_net"), m("dirty_sets"), e.verified ? (e.verify_ok ? "  [matches full rebuild]" : "  [MISMATCH vs full rebuild]") : "");
            if (e.verified) std::printf("    verify: %s\n", e.verify_detail.c_str());
        }
    }
    for (const auto& n : r.notes) std::printf("note: %s\n", n.c_str());
}

}  // namespace

int main(int argc, char** argv) {
    Opts o;
#ifdef CP_DATA_DIR
    o.data = CP_DATA_DIR;
#endif
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        auto next = [&]() -> std::string { return i + 1 < argc ? std::string(argv[++i]) : std::string(); };
        if (a == "--gen") o.gen = next();
        else if (a == "--size") o.size = next();
        else if (a == "--edges") o.edges = std::strtol(next().c_str(), nullptr, 10);
        else if (a == "--seed") o.seed = std::strtoull(next().c_str(), nullptr, 10);
        else if (a == "--repeats") o.reps = static_cast<int>(std::strtol(next().c_str(), nullptr, 10));
        else if (a == "--edit-iters") o.edit_iters = static_cast<int>(std::strtol(next().c_str(), nullptr, 10));
        else if (a == "--no-edit") o.edit = false;
        else if (a == "--quick") o.quick = true;
        else if (a == "--trace") g_trace = true;
        else if (a == "--dirty-ring") g_dirty_ring = static_cast<int>(std::strtol(next().c_str(), nullptr, 10));
        else if (a == "--json") o.json = next();
        else if (a == "--data") o.data = next();
        else {
            std::fprintf(stderr, "usage: bench [--gen grid|organic|motorway|all] [--size small|medium|large|all] [--edges N] [--seed S] [--repeats R] "
                                 "[--edit-iters K] [--no-edit] [--quick] [--json file] [--data dir]\n");
            return 2;
        }
    }
    if (o.quick) {
        o.size = "small";
        if (o.reps == 0) o.reps = 2;
        o.edit_iters = std::min(o.edit_iters, 2);
    }
#ifndef NDEBUG
    std::fprintf(stderr, "bench: WARNING: not a Release build, timings are not meaningful\n");
#endif
    auto pack = cp::load_region_pack_by_id(o.data, "uk");
    if (!pack) {
        std::fprintf(stderr, "bench: %s\n", pack.error.c_str());
        return 1;
    }
    auto region = std::make_shared<const cp::RegionPack>(*pack);

    std::vector<std::string> gens;
    if (o.gen == "all") gens = {"grid", "organic", "motorway"};
    else gens = {o.gen};
    struct SizeSpec {
        std::string name;
        int edges;
        int reps;
    };
    std::vector<SizeSpec> sizes;
    if (o.edges > 0) sizes.push_back({"custom", static_cast<int>(o.edges), 5});
    else {
        const std::vector<SizeSpec> all = {{"small", 200, 7}, {"medium", 2000, 5}, {"large", 20000, 5}};
        for (const auto& s : all)
            if (o.size == "all" || o.size == s.name) sizes.push_back(s);
    }
    if (sizes.empty() || gens.empty()) {
        std::fprintf(stderr, "bench: nothing to run\n");
        return 2;
    }
    std::vector<std::unique_ptr<NetRun>> runs;
    std::map<std::string, std::vector<const NetRun*>> by_gen;
    for (const auto& g : gens) {
        if (g != "grid" && g != "organic" && g != "motorway") {
            std::fprintf(stderr, "bench: unknown generator '%s'\n", g.c_str());
            return 2;
        }
        for (const auto& s : sizes) {
            runs.push_back(bench_one(g, s.name, s.edges, o, region, o.reps > 0 ? o.reps : s.reps));
            print_run(*runs.back());
            std::fflush(stdout);
            by_gen[g].push_back(runs.back().get());
        }
    }
    // Scaling exponents.
    bool any = false;
    for (const auto& [gen, rs] : by_gen) {
        if (rs.size() < 2) continue;
        if (!any) std::printf("\n=== scaling exponents (log-log slope of median ms vs edge count) ===\n");
        any = true;
        std::printf("%s (edges:", gen.c_str());
        for (const NetRun* r : rs) std::printf(" %zu", r->edges);
        std::printf(")\n");
        for (const auto& s : rs.back()->stages) {
            const double e = exponent(rs, s.name);
            if (std::isfinite(e)) std::printf("  %-26s %5.2f%s\n", s.name.c_str(), e, e > 1.25 ? "   <-- super-linear" : "");
        }
    }
    if (!o.json.empty()) write_json(o.json, runs, o, by_gen);
    return 0;
}
