// Vegetation placement (include/cp/vegetation.h). No exceptions: sim_core builds with -fno-exceptions.
//
// Structure of this file
//   1. small helpers (hashing, geometry, polyline cutting and tile splitting)
//   2. region data parser (VegetationRules)
//   3. cross-section reading and the CorridorModel (the clearing mask shared by both layers)
//   4. the default WorldCoverSource (jittered / warped field grid)
//   5. output assembly (VegetationTile, equality)
//   6. the three WorldLayers: veg_corridor, world_cover, vegetation (road edge)
#include "cp/vegetation.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <fstream>
#include <optional>
#include <set>
#include <sstream>

#include "cp/edge_sampling.h"
#include "cp/geom.h"
#include "cp/lanes.h"
#include "cp/street_lights.h"
#include "json_util.h"

namespace cp {

namespace {

using json = jsonu::json;
using jsonu::Err;
using jsonu::member;
using u64 = std::uint64_t;
using i64 = std::int64_t;

// ================================================================== 1. helpers

constexpr double kBig = 1e300;

double lerp(double a, double b, double t) { return a + (b - a) * t; }
double smooth01(double x) {
    x = std::clamp(x, 0.0, 1.0);
    return x * x * (3 - 2 * x);
}

u64 key3(u64 seed, u64 a, u64 b = 0, u64 c = 0, u64 d = 0) { return mix64(mix64(mix64(mix64(seed ^ mix64(a)) ^ b) ^ c) ^ d); }
double h01(u64 seed, u64 a, u64 b = 0, u64 c = 0) { return hash01(key3(seed, a, b, c)); }
u64 ui(i64 v) { return static_cast<u64>(v); }

// Smooth 1-D value noise in [-1, 1].
double vnoise1(u64 seed, double x) {
    const double fl = std::floor(x);
    const i64 i = static_cast<i64>(fl);
    const double t = smooth01(x - fl);
    const double a = h01(seed, ui(i)) * 2 - 1, b = h01(seed, ui(i + 1)) * 2 - 1;
    return lerp(a, b, t);
}
// Smooth 2-D value noise in [0, 1).
double vnoise2(u64 seed, double x, double y) {
    const double fx = std::floor(x), fy = std::floor(y);
    const i64 i = static_cast<i64>(fx), j = static_cast<i64>(fy);
    const double tx = smooth01(x - fx), ty = smooth01(y - fy);
    const double v00 = h01(seed, ui(i), ui(j)), v10 = h01(seed, ui(i + 1), ui(j));
    const double v01 = h01(seed, ui(i), ui(j + 1)), v11 = h01(seed, ui(i + 1), ui(j + 1));
    return lerp(lerp(v00, v10, tx), lerp(v01, v11, tx), ty);
}

double dist_seg(Vec2 p, Vec2 a, Vec2 b) {
    const Vec2 ab = b - a;
    const double l2 = ab.dot(ab);
    const double t = l2 > 1e-18 ? std::clamp((p - a).dot(ab) / l2, 0.0, 1.0) : 0.0;
    return (p - (a + ab * t)).length();
}

bool in_ring(Vec2 p, const std::vector<Vec2>& ring) {
    bool in = false;
    for (std::size_t i = 0, j = ring.size() - 1; i < ring.size(); j = i++) {
        const Vec2 a = ring[i], b = ring[j];
        if ((a.y > p.y) != (b.y > p.y) && p.x < (b.x - a.x) * (p.y - a.y) / (b.y - a.y) + a.x) in = !in;
    }
    return in;
}

double ring_dist(const std::vector<Vec2>& ring, Vec2 p) {
    double d = kBig;
    if (ring.size() < 2) return d;
    for (std::size_t i = 0, j = ring.size() - 1; i < ring.size(); j = i++) d = std::min(d, dist_seg(p, ring[j], ring[i]));
    return d;
}

// Signed distance to an even-odd polygon set: negative inside.
double polyset_sd(const PolygonSet& ps, Vec2 p) {
    double d = kBig;
    bool in = false;
    for (const auto& r : ps) {
        if (r.size() < 3) continue;
        d = std::min(d, ring_dist(r, p));
        if (in_ring(p, r)) in = !in;
    }
    return in ? -d : d;
}

bool in_tri(Vec2 p, const std::array<Vec2, 3>& t) {
    const auto side = [](Vec2 a, Vec2 b, Vec2 c) { return (b - a).cross(c - a); };
    const double d1 = side(t[0], t[1], p), d2 = side(t[1], t[2], p), d3 = side(t[2], t[0], p);
    const bool neg = d1 < 0 || d2 < 0 || d3 < 0, pos = d1 > 0 || d2 > 0 || d3 > 0;
    return !(neg && pos);
}

void box_add(Aabb2& b, Vec2 p) {
    b.lo = {std::min(b.lo.x, p.x), std::min(b.lo.y, p.y)};
    b.hi = {std::max(b.hi.x, p.x), std::max(b.hi.y, p.y)};
}
void box_add(Aabb2& b, const PolygonSet& ps) {
    for (const auto& r : ps)
        for (const Vec2& p : r) box_add(b, p);
}
Aabb2 box_grow(Aabb2 b, double m) {
    if (!b.valid()) return b;
    b.lo = {b.lo.x - m, b.lo.y - m};
    b.hi = {b.hi.x + m, b.hi.y + m};
    return b;
}
bool box_hit(const Aabb2& a, const Aabb2& b) {
    return a.valid() && b.valid() && a.lo.x <= b.hi.x && b.lo.x <= a.hi.x && a.lo.y <= b.hi.y && b.lo.y <= a.hi.y;
}
bool same_box(const Aabb2& a, const Aabb2& b) { return a.lo.x == b.lo.x && a.lo.y == b.lo.y && a.hi.x == b.hi.x && a.hi.y == b.hi.y; }
bool same_pts(const std::vector<Vec2>& a, const std::vector<Vec2>& b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i)
        if (a[i].x != b[i].x || a[i].y != b[i].y) return false;
    return true;
}
bool same_polys(const PolygonSet& a, const PolygonSet& b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i)
        if (!same_pts(a[i], b[i])) return false;
    return true;
}

// Splits [u0..un] into runs where ok(u) holds. Run ends are refined by bisection to the ok side, so a run
// never reaches into a blocked area. Each run lists its parameters (refined start, interior samples, refined end).
template <class F>
std::vector<std::vector<double>> cut_param_runs(const std::vector<double>& u, F ok) {
    std::vector<std::vector<double>> runs;
    std::vector<double> cur;
    const std::size_t n = u.size();
    std::vector<char> v(n);
    for (std::size_t i = 0; i < n; ++i) v[i] = ok(u[i]) ? 1 : 0;
    const auto bisect = [&](double good, double bad) {
        for (int it = 0; it < 14; ++it) {
            const double mid = 0.5 * (good + bad);
            if (ok(mid)) good = mid;
            else bad = mid;
        }
        return good;
    };
    const auto push = [&](double x) {
        if (cur.empty() || x > cur.back() + 1e-9) cur.push_back(x);
    };
    for (std::size_t i = 0; i < n; ++i) {
        if (!v[i]) continue;
        if (cur.empty() && i > 0 && !v[i - 1]) push(bisect(u[i], u[i - 1]));
        push(u[i]);
        if (i + 1 < n && !v[i + 1]) {
            push(bisect(u[i], u[i + 1]));
            runs.push_back(std::move(cur));
            cur.clear();
        }
    }
    if (!cur.empty()) runs.push_back(std::move(cur));
    return runs;
}

// Vertex index ranges of a polyline whose segments all belong (by midpoint) to one tile.
struct TilePiece {
    TileKey tile;
    std::size_t first = 0, last = 0;  // vertices first..last inclusive
};
std::vector<TilePiece> split_by_tile(const std::vector<Vec3>& p, double ts) {
    std::vector<TilePiece> out;
    for (std::size_t k = 0; k + 1 < p.size(); ++k) {
        const Vec2 m{(p[k].x + p[k + 1].x) * 0.5, (p[k].y + p[k + 1].y) * 0.5};
        const TileKey t = tile_of(m, ts);
        if (!out.empty() && out.back().tile == t && out.back().last == k) out.back().last = k + 1;
        else out.push_back({t, k, k + 1});
    }
    return out;
}

Float3 rel(Vec3 w, Vec3 origin) {
    return {static_cast<float>(w.x - origin.x), static_cast<float>(w.y - origin.y), static_cast<float>(w.z - origin.z)};
}

using PieceMap = std::map<TileKey, VegetationTile>;

VegetationTile& tile_in(PieceMap& m, TileKey t, double ts) {
    auto it = m.find(t);
    if (it == m.end()) {
        it = m.emplace(t, VegetationTile{}).first;
        it->second.tile = t;
        it->second.origin = tile_origin(t, ts);
    }
    return it->second;
}

VegInstance make_instance(const VegTypeDef* def, const PieceMap&, Vec3 world, Vec3 origin, double yaw, double scale, u64 seed,
                          VegLayer layer, LeafState leaf) {
    VegInstance v;
    v.pos = rel(world, origin);
    v.yaw = static_cast<float>(yaw);
    v.scale = static_cast<float>(scale);
    v.seed = seed;
    v.tilt = static_cast<float>((h01(seed, 11) - 0.5) * 0.1);
    v.tint = static_cast<float>(h01(seed, 12));
    const int variants = def ? std::max(1, def->variants) : 1;
    v.variant = static_cast<std::uint8_t>(std::min(variants - 1, static_cast<int>(h01(seed, 13) * variants)));
    v.size_class = static_cast<std::uint8_t>(def ? std::clamp(def->size_class, 0, 255) : 0);
    v.layer = layer;
    v.leaf_state = (def && def->kind == VegKind::Tree) ? leaf : LeafState::Leaf;
    return v;
}

void add_instance(PieceMap& m, double ts, const std::string& type, const VegTypeDef* def, Vec3 world, double yaw, double scale, u64 seed,
                  VegLayer layer, LeafState leaf) {
    const TileKey t = tile_of(world.xy(), ts);
    VegetationTile& vt = tile_in(m, t, ts);
    VegInstanceGroup* g = nullptr;
    for (auto& x : vt.groups)
        if (x.type_id == type) g = &x;
    if (!g) {
        vt.groups.push_back({type, {}});
        g = &vt.groups.back();
    }
    g->instances.push_back(make_instance(def, m, world, vt.origin, yaw, scale, seed, layer, leaf));
}

const TreeClass* pick_class(const std::vector<TreeClass>& cl, double u) {
    double tot = 0;
    for (const TreeClass& c : cl) tot += std::max(0.0, c.weight);
    if (cl.empty() || tot <= 0) return nullptr;
    double acc = 0;
    for (const TreeClass& c : cl) {
        acc += std::max(0.0, c.weight);
        if (u * tot < acc) return &c;
    }
    return &cl.back();
}

// ================================================================== 2. region data

double num(const json& j, const char* key, double def, const std::string& ctx, Err& err) {
    const json* v = member(j, key);
    if (!v) return def;
    if (!v->is_number()) {
        err.set(ctx + ": \"" + key + "\" must be a number");
        return def;
    }
    return v->get<double>();
}
std::string str(const json& j, const char* key, const std::string& def, const std::string& ctx, Err& err) {
    const json* v = member(j, key);
    if (!v) return def;
    if (!v->is_string()) {
        err.set(ctx + ": \"" + key + "\" must be a string");
        return def;
    }
    return v->get<std::string>();
}
bool boolean(const json& j, const char* key, bool def, const std::string& ctx, Err& err) {
    const json* v = member(j, key);
    if (!v) return def;
    if (!v->is_boolean()) {
        err.set(ctx + ": \"" + key + "\" must be a boolean");
        return def;
    }
    return v->get<bool>();
}
// "key": [lo, hi] or a single number.
void range(const json& j, const char* key, double& lo, double& hi, const std::string& ctx, Err& err) {
    const json* v = member(j, key);
    if (!v) return;
    if (v->is_number()) {
        lo = hi = v->get<double>();
    } else if (v->is_array() && v->size() == 2 && (*v)[0].is_number() && (*v)[1].is_number()) {
        lo = (*v)[0].get<double>();
        hi = (*v)[1].get<double>();
    } else {
        err.set(ctx + ": \"" + key + "\" must be a number or [lo, hi]");
    }
}
void classes(const json& j, const char* key, std::vector<TreeClass>& out, const std::string& ctx, Err& err) {
    const json* v = member(j, key);
    if (!v) return;
    if (!v->is_array()) {
        err.set(ctx + ": \"" + key + "\" must be an array");
        return;
    }
    out.clear();
    for (const json& e : *v) {
        TreeClass c;
        c.type = str(e, "type", "", ctx, err);
        c.weight = num(e, "weight", 1, ctx, err);
        out.push_back(std::move(c));
    }
}

void parse_road(const json& j, RoadVegetationRule& r, const std::string& ctx, Err& err) {
    if (const json* v = member(j, "verge")) {
        const std::string c = ctx + " verge";
        VergeRule& g = r.verge;
        g.width = num(*v, "width_m", g.width, c, err);
        g.mown_width = num(*v, "mown_width_m", g.mown_width, c, err);
        g.mown_type = str(*v, "mown_type", g.mown_type, c, err);
        g.rough_type = str(*v, "rough_type", g.rough_type, c, err);
        g.mown_height_class = static_cast<int>(num(*v, "mown_height_class", g.mown_height_class, c, err));
        g.rough_height_class = static_cast<int>(num(*v, "rough_height_class", g.rough_height_class, c, err));
        g.density = num(*v, "density", g.density, c, err);
        g.creep = num(*v, "creep_m", g.creep, c, err);
        g.creep_overrun = num(*v, "creep_overrun_m", g.creep_overrun, c, err);
        g.creep_density = num(*v, "creep_density", g.creep_density, c, err);
        g.creep_type = str(*v, "creep_type", g.creep_type, c, err);
        g.median_grass = boolean(*v, "median_grass", g.median_grass, c, err);
        g.median_type = str(*v, "median_type", g.median_type, c, err);
        g.median_height_class = static_cast<int>(num(*v, "median_height_class", g.median_height_class, c, err));
    }
    if (const json* v = member(j, "scrub")) {
        const std::string c = ctx + " scrub";
        ScrubRule& s = r.scrub;
        s.enabled = boolean(*v, "enabled", s.enabled, c, err);
        s.type = str(*v, "type", s.type, c, err);
        s.spacing = num(*v, "spacing_m", s.spacing, c, err);
        s.probability = num(*v, "probability", s.probability, c, err);
        s.band_lo = num(*v, "band_lo", s.band_lo, c, err);
        s.band_hi = num(*v, "band_hi", s.band_hi, c, err);
        range(*v, "scale", s.scale_min, s.scale_max, c, err);
    }
    if (const json* v = member(j, "street_trees")) {
        const std::string c = ctx + " street_trees";
        StreetTreeRule& t = r.street_trees;
        t.enabled = boolean(*v, "enabled", t.enabled, c, err);
        t.spacing = num(*v, "spacing_m", t.spacing, c, err);
        t.spacing_jitter = num(*v, "spacing_jitter", t.spacing_jitter, c, err);
        t.setback = num(*v, "setback_m", t.setback, c, err);
        t.offset_from_back = num(*v, "offset_from_back_m", t.offset_from_back, c, err);
        t.lateral_jitter = num(*v, "lateral_jitter_m", t.lateral_jitter, c, err);
        t.rows = static_cast<int>(num(*v, "rows", t.rows, c, err));
        t.row_spacing = num(*v, "row_spacing_m", t.row_spacing, c, err);
        t.probability = num(*v, "probability", t.probability, c, err);
        t.paved_clearance = num(*v, "paved_clearance_m", t.paved_clearance, c, err);
        t.light_clearance = num(*v, "light_clearance_m", t.light_clearance, c, err);
        t.structure_clearance = num(*v, "structure_clearance_m", t.structure_clearance, c, err);
        range(*v, "scale", t.scale_min, t.scale_max, c, err);
        classes(*v, "classes", t.classes, c, err);
    }
}

void parse_island(const json& j, IslandRule& r, const std::string& ctx, Err& err) {
    r.enabled = boolean(j, "enabled", r.enabled, ctx, err);
    r.grass_type = str(j, "grass_type", r.grass_type, ctx, err);
    r.grass_density = num(j, "grass_density", r.grass_density, ctx, err);
    r.grass_height_class = static_cast<int>(num(j, "grass_height_class", r.grass_height_class, ctx, err));
    r.inset = num(j, "inset_m", r.inset, ctx, err);
    r.shrub_type = str(j, "shrub_type", r.shrub_type, ctx, err);
    r.shrub_spacing = num(j, "shrub_spacing_m", r.shrub_spacing, ctx, err);
    r.shrub_probability = num(j, "shrub_probability", r.shrub_probability, ctx, err);
    r.min_area = num(j, "min_area_m2", r.min_area, ctx, err);
    r.tree_type = str(j, "tree_type", r.tree_type, ctx, err);
    r.tree_min_radius = num(j, "tree_min_radius_m", r.tree_min_radius, ctx, err);
    r.tree_probability = num(j, "tree_probability", r.tree_probability, ctx, err);
}

void parse_cover(const json& j, WorldCoverRules& w, const std::string& ctx, Err& err) {
    w.field_size_x = num(j, "field_size_x_m", w.field_size_x, ctx, err);
    w.field_size_y = num(j, "field_size_y_m", w.field_size_y, ctx, err);
    w.field_jitter = num(j, "field_jitter", w.field_jitter, ctx, err);
    w.warp_m = num(j, "warp_m", w.warp_m, ctx, err);
    w.warp_wavelength_m = num(j, "warp_wavelength_m", w.warp_wavelength_m, ctx, err);
    w.woodland_share = num(j, "woodland_share", w.woodland_share, ctx, err);
    w.woodland_edge_ragged_m = num(j, "woodland_edge_ragged_m", w.woodland_edge_ragged_m, ctx, err);
    w.arable_share = num(j, "arable_share", w.arable_share, ctx, err);
    w.rough_share = num(j, "rough_share", w.rough_share, ctx, err);
    w.hedge_share = num(j, "hedge_share", w.hedge_share, ctx, err);
    w.hedge_overgrown_share = num(j, "hedge_overgrown_share", w.hedge_overgrown_share, ctx, err);
    w.hedge_trimmed_type = str(j, "hedge_trimmed_type", w.hedge_trimmed_type, ctx, err);
    w.hedge_overgrown_type = str(j, "hedge_overgrown_type", w.hedge_overgrown_type, ctx, err);
    w.hedge_tree_share = num(j, "hedge_tree_share", w.hedge_tree_share, ctx, err);
    w.hedge_tree_spacing = num(j, "hedge_tree_spacing_m", w.hedge_tree_spacing, ctx, err);
    w.hedge_tree_jitter = num(j, "hedge_tree_jitter", w.hedge_tree_jitter, ctx, err);
    w.hedge_gap_per_km = num(j, "hedge_gap_per_km", w.hedge_gap_per_km, ctx, err);
    range(j, "hedge_gap_m", w.hedge_gap_min, w.hedge_gap_max, ctx, err);
    w.scatter_trees_per_ha = num(j, "scatter_trees_per_ha", w.scatter_trees_per_ha, ctx, err);
    w.woodland_trees_per_ha = num(j, "woodland_trees_per_ha", w.woodland_trees_per_ha, ctx, err);
    classes(j, "tree_classes", w.tree_classes, ctx, err);
    classes(j, "woodland_classes", w.woodland_classes, ctx, err);
    range(j, "tree_scale", w.tree_scale_min, w.tree_scale_max, ctx, err);
    w.open_grass_type = str(j, "open_grass_type", w.open_grass_type, ctx, err);
    w.rough_grass_type = str(j, "rough_grass_type", w.rough_grass_type, ctx, err);
    w.woodland_floor_type = str(j, "woodland_floor_type", w.woodland_floor_type, ctx, err);
    w.open_density = num(j, "open_density", w.open_density, ctx, err);
    w.rough_density = num(j, "rough_density", w.rough_density, ctx, err);
    w.woodland_density = num(j, "woodland_density", w.woodland_density, ctx, err);
    w.open_height_class = static_cast<int>(num(j, "open_height_class", w.open_height_class, ctx, err));
    w.rough_height_class = static_cast<int>(num(j, "rough_height_class", w.rough_height_class, ctx, err));
    w.woodland_height_class = static_cast<int>(num(j, "woodland_height_class", w.woodland_height_class, ctx, err));
    w.ground_cell = num(j, "ground_cell_m", w.ground_cell, ctx, err);
    w.min_cell = num(j, "min_cell_m", w.min_cell, ctx, err);
}

Loaded<VegetationRules> finish_rules(VegetationRules&& r, const Err& err) {
    Loaded<VegetationRules> out;
    if (err.failed()) out.error = err.msg;
    else out.value = std::move(r);
    return out;
}

bool read_text(const std::filesystem::path& p, std::string& out) {
    std::ifstream in(p, std::ios::binary);
    if (!in) return false;
    std::ostringstream ss;
    ss << in.rdbuf();
    out = ss.str();
    return true;
}

}  // namespace

// ------------------------------------------------------------------ rules: accessors and parser

double SightRules::distance_for(double speed) const {
    for (const SightRow& r : by_speed)
        if (speed <= r.max_speed) return r.distance;
    return by_speed.empty() ? 25.0 : by_speed.back().distance;
}

const VegTypeDef* VegetationRules::type(const std::string& id) const {
    for (const VegTypeDef& t : types)
        if (t.id == id) return &t;
    return nullptr;
}

const RoadVegetationRule& VegetationRules::for_road(const std::string& road_type) const {
    const auto it = road_types.find(road_type);
    return it == road_types.end() ? default_road : it->second;
}

double VegetationRules::reach() const {
    double r = 0;
    const auto upd = [&](const RoadVegetationRule& rr) {
        r = std::max(r, rr.verge.width);
        if (rr.scrub.enabled) r = std::max(r, rr.verge.width);
        if (rr.street_trees.enabled) {
            const StreetTreeRule& t = rr.street_trees;
            r = std::max(r, t.setback + t.offset_from_back + t.lateral_jitter + std::max(1, t.rows) * t.row_spacing + 12.0);
        }
    };
    upd(default_road);
    for (const auto& [id, rr] : road_types) upd(rr);
    return r + 25.0;  // paved clearances, street-light clearance and slack
}

double VegetationRules::density_at(double sd) const {
    return smooth01((sd - clear_margin) / std::max(1e-6, blend_band));
}

Loaded<VegetationRules> parse_vegetation_rules(const std::string& json_text, const RegionPack*) {
    Err err;
    VegetationRules r;
    const json root = json::parse(json_text, nullptr, false);
    if (root.is_discarded()) {
        err.set("vegetation: invalid JSON");
        return finish_rules(std::move(r), err);
    }
    const json* v = member(root, "vegetation");
    if (!v || !v->is_object()) {
        err.set("vegetation: missing \"vegetation\" object");
        return finish_rules(std::move(r), err);
    }
    const json& j = *v;
    if (const json* ts = member(j, "types")) {
        if (!ts->is_array()) err.set("vegetation: \"types\" must be an array");
        else
            for (const json& e : *ts) {
                VegTypeDef t;
                t.id = str(e, "id", "", "vegetation type", err);
                const std::string ctx = "vegetation type \"" + t.id + "\"";
                const std::string kind = str(e, "kind", "grass", ctx, err);
                if (kind == "grass") t.kind = VegKind::Grass;
                else if (kind == "hedge") t.kind = VegKind::Hedge;
                else if (kind == "tree") t.kind = VegKind::Tree;
                else if (kind == "shrub") t.kind = VegKind::Shrub;
                else err.set(ctx + ": kind must be grass, hedge, tree or shrub");
                range(e, "height_m", t.height_min, t.height_max, ctx, err);
                range(e, "width_m", t.width_min, t.width_max, ctx, err);
                t.crown_radius = num(e, "crown_radius_m", t.crown_radius, ctx, err);
                t.size_class = static_cast<int>(num(e, "size_class", 0, ctx, err));
                t.variants = static_cast<int>(num(e, "variants", t.variants, ctx, err));
                if (t.id.empty()) err.set("vegetation type: empty id");
                r.types.push_back(std::move(t));
            }
    }
    if (const json* s = member(j, "sight")) {
        r.sight.height = num(*s, "height_m", r.sight.height, "vegetation sight", err);
        if (const json* rows = member(*s, "by_speed"); rows && rows->is_array())
            for (const json& e : *rows) {
                SightRow row;
                if (member(e, "max_speed_kph")) row.max_speed = num(e, "max_speed_kph", 0, "vegetation sight", err) / 3.6;
                row.distance = num(e, "distance_m", row.distance, "vegetation sight", err);
                r.sight.by_speed.push_back(row);
            }
    }
    if (const json* c = member(j, "corridor")) {
        r.blend_band = num(*c, "blend_band_m", r.blend_band, "vegetation corridor", err);
        r.clear_margin = num(*c, "clear_margin_m", r.clear_margin, "vegetation corridor", err);
        r.cut_edge = num(*c, "cut_edge_m", r.cut_edge, "vegetation corridor", err);
        r.junction_clearance = num(*c, "junction_clearance_m", r.junction_clearance, "vegetation corridor", err);
    }
    if (const json* d = member(j, "default_road")) parse_road(*d, r.default_road, "vegetation default_road", err);
    if (const json* rts = member(j, "road_types")) {
        if (!rts->is_object()) err.set("vegetation: \"road_types\" must be an object");
        else
            for (auto it = rts->begin(); it != rts->end(); ++it) {
                RoadVegetationRule rr = r.default_road;
                parse_road(it.value(), rr, "vegetation road type \"" + it.key() + "\"", err);
                r.road_types[it.key()] = std::move(rr);
            }
    }
    if (const json* i = member(j, "island")) parse_island(*i, r.island, "vegetation island", err);
    if (const json* i = member(j, "splitter")) parse_island(*i, r.splitter, "vegetation splitter", err);
    if (const json* w = member(j, "world_cover")) parse_cover(*w, r.world_cover, "vegetation world_cover", err);
    if (err.failed()) return finish_rules(std::move(r), err);

    // Cross references and sanity.
    const auto check = [&](const std::string& id, const std::string& where) {
        if (!id.empty() && !r.type(id)) err.set("vegetation " + where + ": unknown type id \"" + id + "\"");
    };
    const auto check_road = [&](const RoadVegetationRule& rr, const std::string& w) {
        check(rr.verge.mown_type, w + " verge mown_type");
        check(rr.verge.rough_type, w + " verge rough_type");
        check(rr.verge.creep_type, w + " verge creep_type");
        check(rr.verge.median_type, w + " verge median_type");
        check(rr.scrub.type, w + " scrub type");
        for (const TreeClass& c : rr.street_trees.classes) check(c.type, w + " street_trees class");
    };
    check_road(r.default_road, "default_road");
    for (const auto& [id, rr] : r.road_types) check_road(rr, "road type " + id);
    for (const IslandRule* ir : {&r.island, &r.splitter}) {
        check(ir->grass_type, "island grass_type");
        check(ir->shrub_type, "island shrub_type");
        check(ir->tree_type, "island tree_type");
    }
    const WorldCoverRules& w = r.world_cover;
    check(w.hedge_trimmed_type, "world_cover hedge_trimmed_type");
    check(w.hedge_overgrown_type, "world_cover hedge_overgrown_type");
    check(w.open_grass_type, "world_cover open_grass_type");
    check(w.rough_grass_type, "world_cover rough_grass_type");
    check(w.woodland_floor_type, "world_cover woodland_floor_type");
    for (const TreeClass& c : w.tree_classes) check(c.type, "world_cover tree_classes");
    for (const TreeClass& c : w.woodland_classes) check(c.type, "world_cover woodland_classes");
    if (r.blend_band <= 0 || r.clear_margin < 0 || r.clear_margin + r.blend_band > 40) err.set("vegetation corridor: need 0 < blend_band and clear_margin + blend_band <= 40 m");
    if (w.field_jitter < 0 || w.field_jitter >= 0.5) err.set("vegetation world_cover: field_jitter must be in [0, 0.5)");
    if (w.field_size_x <= 0 || w.field_size_y <= 0 || w.ground_cell <= 0 || w.min_cell <= 0) err.set("vegetation world_cover: sizes must be positive");
    return finish_rules(std::move(r), err);
}

Loaded<VegetationRules> load_vegetation_rules(const std::filesystem::path& region_dir, const RegionPack* region) {
    std::string text;
    if (!read_text(region_dir / "vegetation.json", text) && !read_text(region_dir / "region.json", text)) {
        Loaded<VegetationRules> out;
        out.error = "cannot open " + (region_dir / "region.json").string();
        return out;
    }
    return parse_vegetation_rules(text, region);
}

Loaded<VegetationRules> load_vegetation_rules_by_id(const std::filesystem::path& data_dir, const std::string& region_id,
                                                    const RegionPack* region) {
    return load_vegetation_rules(data_dir / "regions" / region_id, region);
}

namespace {

// ================================================================== 3. cross-sections and the corridor model

struct SideX {
    double cw = 0;    // outermost carriageway-role lane edge (distance from the centreline)
    double back = 0;  // outermost non-verge lane edge (back of footway / carriageway edge)
    double ext = 0;   // whole cross-section
    bool creep_ok = false;
    double hard_room = 0;  // m the creep may take over a hard strip (0 = none)
};
struct Section {
    bool ok = false;
    SideX L, R;
};

Section compute_section(const Edge& e, const RoadType& rt, const RegionPack& region) {
    Section s;
    const std::vector<Lane> lanes = instantiate_lanes(e, rt, region.driving_side);
    if (lanes.empty()) return s;
    struct Best {
        double cw_w = 0;
        const LaneType* cw_lt = nullptr;
    } bl, br;
    for (const Lane& l : lanes) {
        const LaneType* lt = region.lane_type(l.lane_type);
        const std::string prof = lt ? lt->profile : std::string("carriageway");
        const bool verge = prof == "verge", carriage = prof == "carriageway";
        const double lo = l.offset - l.width / 2, hi = l.offset + l.width / 2;
        if (hi > 0) {
            s.L.ext = std::max(s.L.ext, hi);
            if (!verge) s.L.back = std::max(s.L.back, hi);
            if (carriage && hi > s.L.cw) {
                s.L.cw = hi;
                bl = {l.width, lt};
            }
        }
        if (lo < 0) {
            s.R.ext = std::max(s.R.ext, -lo);
            if (!verge) s.R.back = std::max(s.R.back, -lo);
            if (carriage && -lo > s.R.cw) {
                s.R.cw = -lo;
                br = {l.width, lt};
            }
        }
    }
    const auto fin = [](SideX& x, const Best& b) {
        x.creep_ok = x.cw > 0 && std::abs(x.back - x.cw) < 1e-9;
        x.hard_room = (x.creep_ok && b.cw_lt && b.cw_lt->users.empty()) ? 0.4 * b.cw_w : 0.0;
    };
    fin(s.L, bl);
    fin(s.R, br);
    s.ok = s.L.ext > 0 || s.R.ext > 0;
    return s;
}

bool median_is_left(const RegionPack& region) { return region.driving_side == DrivingSide::Right; }

// Corridor half extents (from the centreline) of an edge: cross-section plus the verge, or half the median gap.
void corridor_extents(const RoadType& rt, const RoadVegetationRule& rule, const Section& sec, const RegionPack& region, double& cl, double& cr) {
    const bool ml = median_is_left(region);
    const auto side = [&](const SideX& x, bool is_left) {
        const bool median = rt.dual && (is_left == ml);
        const double w = median ? (rule.verge.median_grass ? rt.median_gap * 0.5 : 0.0) : rule.verge.width;
        return std::max(x.ext, x.back + w);
    };
    cl = side(sec.L, true);
    cr = side(sec.R, false);
}

struct EdgeRec {
    EdgeId id;
    std::string road_type;
    std::vector<Vec2> line;
    double pl = 0, pr = 0, cl = 0, cr = 0;
    Aabb2 box;
};
bool same_rec(const EdgeRec& a, const EdgeRec& b) {
    return a.id == b.id && a.road_type == b.road_type && a.pl == b.pl && a.pr == b.pr && a.cl == b.cl && a.cr == b.cr &&
           same_pts(a.line, b.line) && same_box(a.box, b.box);
}

struct PolyRec {
    std::vector<PolygonSet> sets;
    double verge = 0;  // corridor extension beyond the polygons
    double tall = 0;   // radius within which tall world cover is forbidden (ramps, roundabouts); 0 = none
    std::vector<std::array<Vec2, 3>> tris;  // junction visibility triangles
    Aabb2 box;
};
bool same_prec(const PolyRec& a, const PolyRec& b) {
    if (a.verge != b.verge || a.tall != b.tall || a.sets.size() != b.sets.size() || a.tris.size() != b.tris.size() || !same_box(a.box, b.box))
        return false;
    for (std::size_t i = 0; i < a.sets.size(); ++i)
        if (!same_polys(a.sets[i], b.sets[i])) return false;
    for (std::size_t i = 0; i < a.tris.size(); ++i)
        for (int k = 0; k < 3; ++k)
            if (a.tris[i][static_cast<std::size_t>(k)].x != b.tris[i][static_cast<std::size_t>(k)].x ||
                a.tris[i][static_cast<std::size_t>(k)].y != b.tris[i][static_cast<std::size_t>(k)].y)
                return false;
    return true;
}

double edge_speed(const RoadGraph& g, const RegionPack& region, EdgeId id) {
    const Edge* e = g.edge(id);
    const RoadType* rt = e ? region.road_type(e->road_type) : nullptr;
    return rt ? rt->speed_limit : 0.0;
}

}  // namespace

struct CorridorModel::Impl {
    VegetationRules rules;  // copy: the model must not dangle
    std::function<double(Vec2)> hook;
    std::map<EdgeId, EdgeRec> edges;
    std::map<NodeId, PolyRec> juncs;
    std::map<NodeId, PolyRec> islands;  // keyed by the first ring node
    std::vector<Aabb2> dirty;
    bool all_dirty = false;

    struct Item {
        int kind = 0;  // 0 edge segment, 1 polygon record
        Vec2 a, b;
        double pl = 0, pr = 0, cl = 0, cr = 0;
        EdgeId edge;
        const PolyRec* rec = nullptr;
    };
    static constexpr double kCell = 64;
    std::map<std::pair<i64, i64>, std::vector<Item>> grid;

    static i64 cell(double v) { return static_cast<i64>(std::floor(v / kCell)); }
    void add_item(const Item& it, const Aabb2& b) {
        for (i64 i = cell(b.lo.x); i <= cell(b.hi.x); ++i)
            for (i64 j = cell(b.lo.y); j <= cell(b.hi.y); ++j) grid[{i, j}].push_back(it);
    }
    void index() {
        grid.clear();
        for (const auto& [id, r] : edges) {
            const double g = 64 + std::max(r.cl, r.cr);
            for (std::size_t k = 0; k + 1 < r.line.size(); ++k) {
                Item it;
                it.a = r.line[k];
                it.b = r.line[k + 1];
                it.pl = r.pl;
                it.pr = r.pr;
                it.cl = r.cl;
                it.cr = r.cr;
                it.edge = id;
                Aabb2 b;
                box_add(b, it.a);
                box_add(b, it.b);
                add_item(it, box_grow(b, g));
            }
        }
        const auto add_polys = [&](const std::map<NodeId, PolyRec>& m) {
            for (const auto& [id, r] : m) {
                Item it;
                it.kind = 1;
                it.rec = &r;
                add_item(it, box_grow(r.box, 64 + std::max(r.verge, r.tall)));
            }
        };
        add_polys(juncs);
        add_polys(islands);
    }
    const std::vector<Item>* cell_items(Vec2 p) const {
        const auto it = grid.find({cell(p.x), cell(p.y)});
        return it == grid.end() ? nullptr : &it->second;
    }

    double field(Vec2 p, bool corridor, EdgeId exclude, double cap) const {
        double d = std::min(cap, 64.0);
        const std::vector<Item>* items = cell_items(p);
        if (items) {
            std::vector<const PolyRec*> seen;
            for (const Item& it : *items) {
                if (it.kind == 0) {
                    if (it.edge == exclude && exclude.valid()) continue;
                    const double ds = dist_seg(p, it.a, it.b);
                    const bool left = (it.b - it.a).cross(p - it.a) >= 0;
                    d = std::min(d, ds - (corridor ? (left ? it.cl : it.cr) : (left ? it.pl : it.pr)));
                } else {
                    if (std::find(seen.begin(), seen.end(), it.rec) != seen.end()) continue;
                    seen.push_back(it.rec);
                    double sd = kBig;
                    for (const PolygonSet& s : it.rec->sets) sd = std::min(sd, polyset_sd(s, p));
                    d = std::min(d, sd - (corridor ? it.rec->verge : 0.0));
                }
            }
        }
        if (corridor && hook) d = std::min(d, hook(p));
        return d;
    }

    bool sight(Vec2 p) const {
        const std::vector<Item>* items = cell_items(p);
        if (!items) return false;
        for (const Item& it : *items)
            if (it.kind == 1)
                for (const auto& t : it.rec->tris)
                    if (in_tri(p, t)) return true;
        return false;
    }
    bool tall_blocked(Vec2 p) const {
        const std::vector<Item>* items = cell_items(p);
        if (!items) return false;
        std::vector<const PolyRec*> seen;
        for (const Item& it : *items) {
            if (it.kind != 1) continue;
            if (std::find(seen.begin(), seen.end(), it.rec) != seen.end()) continue;
            seen.push_back(it.rec);
            for (const auto& t : it.rec->tris)
                if (in_tri(p, t)) return true;
            if (it.rec->tall > 0) {
                double sd = kBig;
                for (const PolygonSet& s : it.rec->sets) sd = std::min(sd, polyset_sd(s, p));
                if (sd < it.rec->tall) return true;
            }
        }
        return false;
    }
};

namespace {

EdgeRec make_edge_rec(const Edge& e, const RoadType& rt, const VegetationRules& rules, const RegionPack& region) {
    EdgeRec r;
    r.id = e.id;
    r.road_type = e.road_type;
    const Section sec = compute_section(e, rt, region);
    if (!sec.ok) return r;
    r.pl = sec.L.back;  // paved: verge-role lanes are grass, not paved
    r.pr = sec.R.back;
    corridor_extents(rt, rules.for_road(e.road_type), sec, region, r.cl, r.cr);
    for (const EdgeSample& s : sample_centreline(e, SamplingParams{0.02, 0.1, 10.0})) r.line.push_back(s.pos.xy());
    for (const Vec2& p : r.line) box_add(r.box, p);
    r.box = box_grow(r.box, std::max(r.cl, r.cr));
    return r;
}

void add_poly_box(PolyRec& r) {
    for (const PolygonSet& s : r.sets) box_add(r.box, s);
    for (const auto& t : r.tris)
        for (const Vec2& p : t) box_add(r.box, p);
    r.box = box_grow(r.box, std::max(r.verge, r.tall));
}

PolyRec make_junction_rec(const Junction& j, const RoadGraph& g, const RegionPack& region, const VegetationRules& rules) {
    PolyRec r;
    r.sets = {j.carriageway, j.pavements, j.gore};
    for (const JunctionArm& a : j.arms) {
        const Edge* e = g.edge(a.edge);
        if (e) r.verge = std::max(r.verge, rules.for_road(e->road_type).verge.width);
    }
    if (j.cls == NodeClass::Merge || j.cls == NodeClass::Diverge) r.tall = rules.junction_clearance;
    if (j.cls == NodeClass::Junction && j.trims.size() == j.arms.size()) {
        const std::size_t n = j.arms.size();
        for (std::size_t k = 0; k < n && n >= 3; ++k) {
            if (k >= j.corners.size()) break;
            const double ang = j.corners[k].angle;
            if (ang >= kPi - 0.05 || ang < 0.2) continue;
            const JunctionArm& A = j.arms[k];
            const JunctionArm& B = j.arms[(k + 1) % n];
            const double dA = rules.sight.distance_for(edge_speed(g, region, A.edge));
            const double dB = rules.sight.distance_for(edge_speed(g, region, B.edge));
            const Vec2 pa = arm_point(A, j.trims[k] + dA, A.kerb_left).pos.xy();
            const Vec2 pb = arm_point(B, j.trims[(k + 1) % n] + dB, -B.kerb_right).pos.xy();
            r.tris.push_back({j.pos.xy(), pa, pb});
        }
    }
    add_poly_box(r);
    return r;
}

PolyRec make_island_rec(const RoundaboutLayout& l, const VegetationRules& rules) {
    PolyRec r;
    r.sets.push_back(l.island);
    r.sets.push_back(l.apron);
    for (const SplitterIsland& s : l.splitters) r.sets.push_back(PolygonSet{s.polygon});
    r.tall = rules.junction_clearance;
    add_poly_box(r);
    return r;
}

}  // namespace

CorridorModel::CorridorModel() : impl_(std::make_unique<Impl>()) {}
CorridorModel::~CorridorModel() = default;
CorridorModel::CorridorModel(CorridorModel&&) noexcept = default;
CorridorModel& CorridorModel::operator=(CorridorModel&&) noexcept = default;

double CorridorModel::band() const { return impl_->rules.blend_band; }
double CorridorModel::signed_distance(Vec2 p, double cap) const { return impl_->field(p, true, {}, cap); }
double CorridorModel::paved_distance(Vec2 p, EdgeId exclude, double cap) const { return impl_->field(p, false, exclude, cap); }
bool CorridorModel::in_sight_triangle(Vec2 p) const { return impl_->sight(p); }
bool CorridorModel::tall_blocked(Vec2 p) const { return impl_->tall_blocked(p); }
double CorridorModel::clear(Vec2 p, bool tall) const {
    const double sd = impl_->field(p, true, {}, impl_->rules.clear_margin + impl_->rules.blend_band + 1);
    const double m = impl_->rules.density_at(sd);
    if (m <= 0) return 0;
    if (tall && impl_->tall_blocked(p)) return 0;
    return m;
}
bool CorridorModel::all_dirty() const { return impl_->all_dirty; }
const std::vector<Aabb2>& CorridorModel::dirty() const { return impl_->dirty; }
std::vector<EdgeId> CorridorModel::edge_ids() const {
    std::vector<EdgeId> out;
    for (const auto& [id, r] : impl_->edges)
        if (r.line.size() >= 2) out.push_back(id);
    return out;
}
Aabb2 CorridorModel::edge_box(EdgeId e) const {
    const auto it = impl_->edges.find(e);
    return it == impl_->edges.end() ? Aabb2{} : it->second.box;
}
std::vector<NodeId> CorridorModel::island_ids() const {
    std::vector<NodeId> out;
    for (const auto& [id, r] : impl_->islands) out.push_back(id);
    return out;
}
Aabb2 CorridorModel::island_box(NodeId n) const {
    const auto it = impl_->islands.find(n);
    return it == impl_->islands.end() ? Aabb2{} : it->second.box;
}

void CorridorModel::rebuild(const Inputs& in, std::function<double(Vec2)> hook) {
    Impl& m = *impl_;
    m.rules = *in.rules;
    m.hook = std::move(hook);
    m.edges.clear();
    m.juncs.clear();
    m.islands.clear();
    m.dirty.clear();
    m.all_dirty = true;
    for (const auto& [id, e] : in.graph->edges()) {
        const RoadType* rt = in.region->road_type(e.road_type);
        if (rt) m.edges[id] = make_edge_rec(e, *rt, m.rules, *in.region);
    }
    for (const auto& [id, j] : in.junctions->junctions) m.juncs[id] = make_junction_rec(j, *in.graph, *in.region, m.rules);
    for (const RoundaboutLayout& l : *in.roundabouts)
        if (l.ok && !l.group.nodes.empty()) m.islands[l.group.nodes.front()] = make_island_rec(l, m.rules);
    m.index();
}

void CorridorModel::update(const Inputs& in, const std::vector<EdgeId>& candidates) {
    Impl& m = *impl_;
    m.dirty.clear();
    m.all_dirty = false;
    bool changed = false;
    const auto note = [&](const Aabb2& b) {
        if (b.valid()) m.dirty.push_back(b);
        changed = true;
    };
    std::set<EdgeId> ids(candidates.begin(), candidates.end());
    for (const auto& [id, r] : m.edges)
        if (!in.graph->edge(id)) ids.insert(id);
    for (EdgeId id : ids) {
        const auto old = m.edges.find(id);
        const Edge* e = in.graph->edge(id);
        const RoadType* rt = e ? in.region->road_type(e->road_type) : nullptr;
        if (!rt) {
            if (old != m.edges.end()) {
                note(old->second.box);
                m.edges.erase(old);
            }
            continue;
        }
        EdgeRec nr = make_edge_rec(*e, *rt, m.rules, *in.region);
        if (old == m.edges.end()) {
            note(nr.box);
            m.edges[id] = std::move(nr);
        } else if (!same_rec(old->second, nr)) {
            note(old->second.box);
            note(nr.box);
            old->second = std::move(nr);
        }
    }
    const auto diff = [&](std::map<NodeId, PolyRec>& cur, std::map<NodeId, PolyRec>& next) {
        for (auto it = cur.begin(); it != cur.end();) {
            const auto n = next.find(it->first);
            if (n == next.end()) {
                note(it->second.box);
                it = cur.erase(it);
            } else {
                if (!same_prec(it->second, n->second)) {
                    note(it->second.box);
                    note(n->second.box);
                    it->second = n->second;
                }
                ++it;
            }
        }
        for (auto& [id, r] : next)
            if (!cur.count(id)) {
                note(r.box);
                cur[id] = r;
            }
    };
    std::map<NodeId, PolyRec> nj, ni;
    for (const auto& [id, j] : in.junctions->junctions) nj[id] = make_junction_rec(j, *in.graph, *in.region, m.rules);
    for (const RoundaboutLayout& l : *in.roundabouts)
        if (l.ok && !l.group.nodes.empty()) ni[l.group.nodes.front()] = make_island_rec(l, m.rules);
    diff(m.juncs, nj);
    diff(m.islands, ni);
    if (changed) m.index();
}

CorridorModel CorridorModel::build(const RoadGraph& graph, const RegionPack& region, const VegetationRules& rules,
                                   const JunctionSet& junctions, const std::vector<RoundaboutLayout>& roundabouts,
                                   std::function<double(Vec2)> hook) {
    CorridorModel m;
    Inputs in;
    in.graph = &graph;
    in.region = &region;
    in.rules = &rules;
    in.junctions = &junctions;
    in.roundabouts = &roundabouts;
    m.rebuild(in, std::move(hook));
    return m;
}

namespace {

// ================================================================== 4. the default world cover source

enum class Land { Wood, Arable, Rough, Pasture };

class DefaultCover final : public WorldCoverSource {
public:
    DefaultCover(std::shared_ptr<const VegetationRules> rules, u64 seed) : r_(std::move(rules)), w_(r_->world_cover), seed_(seed) {}

    CoverGround ground_at(Vec2 p) const override {
        CoverGround g;
        switch (land_at(p)) {
            case Land::Wood:
                g = {w_.woodland_floor_type, w_.woodland_density, static_cast<std::uint8_t>(w_.woodland_height_class)};
                break;
            case Land::Rough:
                g = {w_.rough_grass_type, w_.rough_density, static_cast<std::uint8_t>(w_.rough_height_class)};
                break;
            case Land::Pasture:
                g = {w_.open_grass_type, w_.open_density, static_cast<std::uint8_t>(w_.open_height_class)};
                break;
            case Land::Arable:
                g = {"", 0, 0};  // no instances: the renderer's ground material shows soil / crop
                break;
        }
        return g;
    }

    void points_in(Vec2 lo, Vec2 hi, std::vector<CoverPoint>& out) const override {
        // Hedge trees.
        for_segments(lo, hi, [&](const Seg& s) {
            if (!s.present || ha(18, s.axis, s.i, s.j) >= w_.hedge_tree_share) return;
            const double len = s.s.back();
            const double sp = std::max(1.0, w_.hedge_tree_spacing);
            for (int k = 0; (k + 0.5) * sp < len; ++k) {
                const double sk = (k + 0.5) * sp + (ha(19, s.axis, s.i, static_cast<i64>(k) + 1000 * s.j) - 0.5) * 2 * w_.hedge_tree_jitter * sp;
                if (sk <= 0 || sk >= len) continue;
                bool in_gap = false;
                for (const auto& g : s.gaps) in_gap = in_gap || (sk > g.first - 1.5 && sk < g.second + 1.5);
                if (in_gap) continue;
                const Vec2 pos = point_at(s.pts, s.s, sk);
                if (pos.x < lo.x || pos.x >= hi.x || pos.y < lo.y || pos.y >= hi.y) continue;
                const TreeClass* c = pick_class(w_.tree_classes, ha(20, s.axis, s.i, static_cast<i64>(k) + 1000 * s.j));
                if (c) out.push_back(make_point(*c, pos, key3(seed_, 21, ui(s.axis * 2 + 1) ^ ui(s.i), ui(s.j), ui(k))));
            }
        });
        // Scattered trees in open and rough fields: coarse lattice.
        {
            const double C = 20;
            const double prob = w_.scatter_trees_per_ha * C * C / 1e4;
            for (i64 cy = static_cast<i64>(std::floor(lo.y / C)); cy <= static_cast<i64>(std::floor(hi.y / C)); ++cy)
                for (i64 cx = static_cast<i64>(std::floor(lo.x / C)); cx <= static_cast<i64>(std::floor(hi.x / C)); ++cx) {
                    if (h(30, ui(cx), ui(cy)) >= prob) continue;
                    const Vec2 pos{(static_cast<double>(cx) + h(31, ui(cx), ui(cy))) * C, (static_cast<double>(cy) + h(32, ui(cx), ui(cy))) * C};
                    if (pos.x < lo.x || pos.x >= hi.x || pos.y < lo.y || pos.y >= hi.y) continue;
                    if (land_at(pos) == Land::Wood) continue;
                    const TreeClass* c = pick_class(w_.tree_classes, h(33, ui(cx), ui(cy)));
                    if (c) out.push_back(make_point(*c, pos, key3(seed_, 34, ui(cx), ui(cy))));
                }
        }
        // Woodland trees: fine lattice.
        {
            const double F = 5;
            const double prob = std::min(1.0, w_.woodland_trees_per_ha * F * F / 1e4);
            const std::vector<TreeClass>& cl = w_.woodland_classes.empty() ? w_.tree_classes : w_.woodland_classes;
            if (prob > 0)
                for (i64 cy = static_cast<i64>(std::floor(lo.y / F)); cy <= static_cast<i64>(std::floor(hi.y / F)); ++cy)
                    for (i64 cx = static_cast<i64>(std::floor(lo.x / F)); cx <= static_cast<i64>(std::floor(hi.x / F)); ++cx) {
                        if (h(40, ui(cx), ui(cy)) >= prob) continue;
                        const Vec2 pos{(static_cast<double>(cx) + h(41, ui(cx), ui(cy))) * F, (static_cast<double>(cy) + h(42, ui(cx), ui(cy))) * F};
                        if (pos.x < lo.x || pos.x >= hi.x || pos.y < lo.y || pos.y >= hi.y) continue;
                        if (land_at(pos) != Land::Wood) continue;
                        const TreeClass* c = pick_class(cl, h(43, ui(cx), ui(cy)));
                        if (c) out.push_back(make_point(*c, pos, key3(seed_, 44, ui(cx), ui(cy))));
                    }
        }
    }

    void lines_touching(Vec2 lo, Vec2 hi, std::vector<CoverLine>& out) const override {
        for_segments(lo, hi, [&](const Seg& s) {
            if (!s.present) return;
            std::vector<std::pair<double, double>> keep;  // arc intervals between gaps
            double a = 0;
            const double len = s.s.back();
            for (const auto& g : s.gaps) {
                if (g.first - a >= 2.0) keep.push_back({a, g.first});
                a = g.second;
            }
            if (len - a >= 2.0) keep.push_back({a, len});
            const bool over = ha(12, s.axis, s.i, s.j) < w_.hedge_overgrown_share;
            const std::string& tid = over ? w_.hedge_overgrown_type : w_.hedge_trimmed_type;
            const VegTypeDef* def = r_->type(tid);
            const u64 base = key3(seed_, 20, ui(s.axis), ui(s.i), ui(s.j));
            for (std::size_t k = 0; k < keep.size(); ++k) {
                CoverLine l;
                l.type_id = tid;
                l.style = over ? HedgeStyle::Overgrown : HedgeStyle::Trimmed;
                const double u1 = ha(14, s.axis, s.i, s.j), u2 = ha(15, s.axis, s.i, s.j);
                l.height = static_cast<float>(def ? lerp(def->height_min, def->height_max, u1) : 1.5);
                l.width = static_cast<float>(def ? lerp(def->width_min, def->width_max, u2) : 1.0);
                l.seed = mix64(base + k);
                const double sa = keep[k].first, sb = keep[k].second;
                l.points.push_back(point_at(s.pts, s.s, sa));
                l.s.push_back(sa);
                for (std::size_t q = 0; q < s.s.size(); ++q)
                    if (s.s[q] > sa + 1e-9 && s.s[q] < sb - 1e-9) {
                        l.points.push_back(s.pts[q]);
                        l.s.push_back(s.s[q]);
                    }
                l.points.push_back(point_at(s.pts, s.s, sb));
                l.s.push_back(sb);
                Aabb2 b;
                for (const Vec2& p : l.points) box_add(b, p);
                if (box_hit(b, Aabb2{lo, hi})) out.push_back(std::move(l));
            }
        });
    }

private:
    struct Seg {
        bool present = false;
        int axis = 0;
        i64 i = 0, j = 0;
        std::vector<Vec2> pts;
        std::vector<double> s;
        std::vector<std::pair<double, double>> gaps;
    };

    double h(u64 salt, u64 a, u64 b = 0, u64 c = 0) const { return h01(seed_ ^ mix64(salt + 77), a, b, c); }
    double ha(u64 salt, int axis, i64 i, i64 j) const { return h(salt, ui(axis), ui(i), ui(j)); }

    double line_x(i64 i) const { return static_cast<double>(i) * w_.field_size_x + (h(1, ui(i)) - 0.5) * 2 * w_.field_jitter * w_.field_size_x; }
    double line_y(i64 j) const { return static_cast<double>(j) * w_.field_size_y + (h(2, ui(j)) - 0.5) * 2 * w_.field_jitter * w_.field_size_y; }
    double wx(double y) const { return w_.warp_m * vnoise1(seed_ ^ 0x3, y / w_.warp_wavelength_m); }
    double wy(double x) const { return w_.warp_m * vnoise1(seed_ ^ 0x4, x / w_.warp_wavelength_m); }

    Land kind_of(i64 i, i64 j) const {
        if (h(5, ui(i), ui(j)) < w_.woodland_share) return Land::Wood;
        const double r = h(6, ui(i), ui(j));
        if (r < w_.arable_share) return Land::Arable;
        if (r < w_.arable_share + w_.rough_share) return Land::Rough;
        return Land::Pasture;
    }

    Land land_at(Vec2 p) const {
        const double u = p.x - wx(p.y), v = p.y - wy(p.x);
        i64 i = static_cast<i64>(std::floor(u / w_.field_size_x)), j = static_cast<i64>(std::floor(v / w_.field_size_y));
        while (line_x(i + 1) <= u) ++i;
        while (line_x(i) > u) --i;
        while (line_y(j + 1) <= v) ++j;
        while (line_y(j) > v) --j;
        const double dxl = u - line_x(i), dxh = line_x(i + 1) - u, dyl = v - line_y(j), dyh = line_y(j + 1) - v;
        const Land own = kind_of(i, j);
        const double rg = w_.woodland_edge_ragged_m;
        if (own == Land::Wood) {
            const double t = rg * (2 * vnoise2(seed_ ^ 0x7, p.x / 15, p.y / 15) - 1);
            if (std::min({dxl, dxh, dyl, dyh}) > t) return Land::Wood;
            return Land::Rough;  // eroded fringe
        }
        const double t2 = rg * vnoise2(seed_ ^ 0x8, p.x / 15, p.y / 15);
        if ((dxl < t2 && kind_of(i - 1, j) == Land::Wood) || (dxh < t2 && kind_of(i + 1, j) == Land::Wood) ||
            (dyl < t2 && kind_of(i, j - 1) == Land::Wood) || (dyh < t2 && kind_of(i, j + 1) == Land::Wood))
            return Land::Wood;
        return own;
    }

    static Vec2 point_at(const std::vector<Vec2>& pts, const std::vector<double>& s, double sv) {
        if (sv <= s.front()) return pts.front();
        if (sv >= s.back()) return pts.back();
        std::size_t k = static_cast<std::size_t>(std::upper_bound(s.begin(), s.end(), sv) - s.begin());
        k = std::clamp<std::size_t>(k, 1, s.size() - 1);
        const double t = (sv - s[k - 1]) / std::max(1e-12, s[k] - s[k - 1]);
        return pts[k - 1] + (pts[k] - pts[k - 1]) * t;
    }

    Seg make_seg(int axis, i64 i, i64 j) const {
        Seg s;
        s.axis = axis;
        s.i = i;
        s.j = j;
        if (ha(10, axis, i, j) >= w_.hedge_share) return s;
        Land a, b;
        if (axis == 0) {
            a = kind_of(i - 1, j);
            b = kind_of(i, j);
        } else {
            a = kind_of(i, j - 1);
            b = kind_of(i, j);
        }
        if (a == Land::Wood && b == Land::Wood) return s;
        const double t0 = axis == 0 ? line_y(j) : line_x(i), t1 = axis == 0 ? line_y(j + 1) : line_x(i + 1);
        const int n = std::max(1, static_cast<int>(std::ceil((t1 - t0) / 2.0)));
        for (int k = 0; k <= n; ++k) {
            const double t = lerp(t0, t1, static_cast<double>(k) / n);
            s.pts.push_back(axis == 0 ? Vec2{line_x(i) + wx(t), t} : Vec2{t, line_y(j) + wy(t)});
        }
        s.s.push_back(0);
        for (std::size_t k = 1; k < s.pts.size(); ++k) s.s.push_back(s.s.back() + (s.pts[k] - s.pts[k - 1]).length());
        const double len = s.s.back();
        const int ng = std::min(3, static_cast<int>(std::floor(len / 1000.0 * w_.hedge_gap_per_km + ha(16, axis, i, j))));
        for (int g = 0; g < ng; ++g) {
            const double c = len * (g + 0.15 + 0.7 * ha(17, axis, i, j * 7 + g)) / ng;
            const double gw = lerp(w_.hedge_gap_min, w_.hedge_gap_max, ha(9, axis, i, j * 7 + g));
            s.gaps.push_back({std::max(0.0, c - gw / 2), std::min(len, c + gw / 2)});
        }
        s.present = true;
        return s;
    }

    template <class F>
    void for_segments(Vec2 lo, Vec2 hi, F f) const {
        const double mx = w_.warp_m + w_.field_jitter * std::max(w_.field_size_x, w_.field_size_y) + 4;
        const i64 i0 = static_cast<i64>(std::floor((lo.x - mx) / w_.field_size_x)) - 1, i1 = static_cast<i64>(std::floor((hi.x + mx) / w_.field_size_x)) + 1;
        const i64 j0 = static_cast<i64>(std::floor((lo.y - mx) / w_.field_size_y)) - 1, j1 = static_cast<i64>(std::floor((hi.y + mx) / w_.field_size_y)) + 1;
        for (i64 i = i0; i <= i1; ++i)
            for (i64 j = j0; j <= j1; ++j) {
                for (int axis = 0; axis < 2; ++axis) {
                    // Cheap reject by the segment's box.
                    const double xa = axis == 0 ? line_x(i) - w_.warp_m : line_x(i), xb = axis == 0 ? line_x(i) + w_.warp_m : line_x(i + 1);
                    const double ya = axis == 0 ? line_y(j) : line_y(j) - w_.warp_m, yb = axis == 0 ? line_y(j + 1) : line_y(j) + w_.warp_m;
                    if (xb < lo.x - 4 || xa > hi.x + 4 || yb < lo.y - 4 || ya > hi.y + 4) continue;
                    const Seg s = make_seg(axis, i, j);
                    if (s.present) f(s);
                }
            }
    }

    CoverPoint make_point(const TreeClass& c, Vec2 pos, u64 seed) const {
        CoverPoint p;
        p.type_id = c.type;
        p.pos = pos;
        p.seed = seed;
        p.yaw = h01(seed, 1) * 2 * kPi - kPi;
        p.scale = lerp(w_.tree_scale_min, w_.tree_scale_max, h01(seed, 2));
        const VegTypeDef* d = r_->type(c.type);
        p.size_class = static_cast<std::uint8_t>(d ? std::clamp(d->size_class, 0, 255) : 0);
        p.tall = d ? d->height_max > r_->sight.height : true;
        return p;
    }

    std::shared_ptr<const VegetationRules> r_;
    const WorldCoverRules& w_;
    u64 seed_;
};

// ================================================================== 5. tile assembly and equality

void append_piece(VegetationTile& dst, const VegetationTile& src) {
    for (const VegInstanceGroup& g : src.groups) {
        VegInstanceGroup* d = nullptr;
        for (auto& x : dst.groups)
            if (x.type_id == g.type_id) d = &x;
        if (!d) {
            dst.groups.push_back({g.type_id, {}});
            d = &dst.groups.back();
        }
        d->instances.insert(d->instances.end(), g.instances.begin(), g.instances.end());
    }
    for (const GroundPatchGroup& g : src.patches) {
        GroundPatchGroup* d = nullptr;
        for (auto& x : dst.patches)
            if (x.type_id == g.type_id) d = &x;
        if (!d) {
            dst.patches.push_back({g.type_id, {}});
            d = &dst.patches.back();
        }
        d->patches.insert(d->patches.end(), g.patches.begin(), g.patches.end());
    }
    dst.hedges.insert(dst.hedges.end(), src.hedges.begin(), src.hedges.end());
    dst.strips.insert(dst.strips.end(), src.strips.begin(), src.strips.end());
    dst.areas.insert(dst.areas.end(), src.areas.begin(), src.areas.end());
}
void finish_tile(VegetationTile& t) {
    std::sort(t.groups.begin(), t.groups.end(), [](const auto& a, const auto& b) { return a.type_id < b.type_id; });
    std::sort(t.patches.begin(), t.patches.end(), [](const auto& a, const auto& b) { return a.type_id < b.type_id; });
}

bool same_f3(const Float3& a, const Float3& b) { return a.x == b.x && a.y == b.y && a.z == b.z; }
bool same_f3s(const std::vector<Float3>& a, const std::vector<Float3>& b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i)
        if (!same_f3(a[i], b[i])) return false;
    return true;
}

}  // namespace

bool same_content(const VegetationTile& a, const VegetationTile& b) {
    if (a.tile != b.tile || a.origin.x != b.origin.x || a.origin.y != b.origin.y || a.origin.z != b.origin.z) return false;
    if (a.groups.size() != b.groups.size() || a.hedges.size() != b.hedges.size() || a.strips.size() != b.strips.size() ||
        a.patches.size() != b.patches.size() || a.areas.size() != b.areas.size())
        return false;
    for (std::size_t g = 0; g < a.groups.size(); ++g) {
        const auto &x = a.groups[g], &y = b.groups[g];
        if (x.type_id != y.type_id || x.instances.size() != y.instances.size()) return false;
        for (std::size_t i = 0; i < x.instances.size(); ++i) {
            const VegInstance &p = x.instances[i], &q = y.instances[i];
            if (!same_f3(p.pos, q.pos) || p.yaw != q.yaw || p.scale != q.scale || p.tilt != q.tilt || p.tint != q.tint ||
                p.size_class != q.size_class || p.variant != q.variant || p.leaf_state != q.leaf_state || p.layer != q.layer || p.seed != q.seed)
                return false;
        }
    }
    for (std::size_t g = 0; g < a.patches.size(); ++g) {
        const auto &x = a.patches[g], &y = b.patches[g];
        if (x.type_id != y.type_id || x.patches.size() != y.patches.size()) return false;
        for (std::size_t i = 0; i < x.patches.size(); ++i) {
            const GroundPatch &p = x.patches[i], &q = y.patches[i];
            if (!same_f3(p.centre, q.centre) || p.half_size != q.half_size || p.density != q.density || p.height_class != q.height_class) return false;
        }
    }
    for (std::size_t i = 0; i < a.hedges.size(); ++i) {
        const HedgeRun &p = a.hedges[i], &q = b.hedges[i];
        if (p.type_id != q.type_id || p.style != q.style || p.height != q.height || p.width != q.width || p.seed != q.seed || p.s0 != q.s0 ||
            !same_f3s(p.points, q.points))
            return false;
    }
    for (std::size_t i = 0; i < a.strips.size(); ++i) {
        const GrassStrip &p = a.strips[i], &q = b.strips[i];
        if (p.type_id != q.type_id || p.density != q.density || p.height_class != q.height_class || p.seed != q.seed || p.s0 != q.s0 ||
            p.edge != q.edge || p.side != q.side || !same_f3s(p.inner, q.inner) || !same_f3s(p.outer, q.outer))
            return false;
    }
    for (std::size_t i = 0; i < a.areas.size(); ++i) {
        const AreaPlanting &p = a.areas[i], &q = b.areas[i];
        if (p.type_id != q.type_id || p.density != q.density || p.height_class != q.height_class || p.seed != q.seed || !same_f3s(p.ring, q.ring)) return false;
    }
    return true;
}

std::shared_ptr<const WorldCoverSource> make_default_cover_source(std::shared_ptr<const VegetationRules> rules, std::uint64_t world_seed) {
    return std::make_shared<DefaultCover>(std::move(rules), world_seed);
}

namespace {

// ================================================================== 6. layers

struct Shared {
    std::shared_ptr<const VegetationRules> rules;
    VegetationParams params;
    std::shared_ptr<const WorldCoverSource> cover;
    double ground_z(double x, double y) const { return params.ground_height ? params.ground_height(x, y) : 0.0; }
};

std::vector<EdgeId> edges_at_nodes(const RoadGraph& g, const std::set<NodeId>& nodes) {
    std::vector<EdgeId> out;
    for (NodeId n : nodes)
        for (EdgeId e : g.edges_at(n)) out.push_back(e);
    return out;
}

// ---------------------------------------------------------------- veg_corridor

class CorridorLayer final : public WorldLayer {
public:
    explicit CorridorLayer(std::shared_ptr<Shared> sh) : sh_(std::move(sh)) {}
    std::string_view name() const override { return kLayerVegCorridor; }
    std::vector<std::string> reads() const override { return {std::string(kLayerJunctions)}; }
    const void* state() const override { return &model_; }

    void rebuild_all(const LayerContext& ctx, LayerChanges&) override {
        const JunctionState& js = *ctx.state<JunctionState>(kLayerJunctions);
        model_.rebuild(inputs(ctx, js), sh_->params.grading_footprint);
    }
    void update(const LayerContext& ctx, const ObjectChanges& gr, LayerChanges& out) override {
        const ObjectChanges& rj = *ctx.changes_of(kLayerJunctions);
        if (rj.everything || gr.everything) return rebuild_all(ctx, out);
        const JunctionState& js = *ctx.state<JunctionState>(kLayerJunctions);
        std::set<NodeId> nodes(gr.nodes.begin(), gr.nodes.end());
        nodes.insert(rj.nodes.begin(), rj.nodes.end());
        std::vector<EdgeId> cand = edges_at_nodes(ctx.graph(), nodes);
        cand.insert(cand.end(), gr.edges.begin(), gr.edges.end());
        model_.update(inputs(ctx, js), cand);
    }

private:
    CorridorModel::Inputs inputs(const LayerContext& ctx, const JunctionState& js) const {
        CorridorModel::Inputs in;
        in.graph = &ctx.graph();
        in.region = &ctx.region();
        in.rules = sh_->rules.get();
        in.junctions = &js.junctions;
        in.roundabouts = &js.roundabouts.layouts;
        return in;
    }
    std::shared_ptr<Shared> sh_;
    CorridorModel model_;
};

// ---------------------------------------------------------------- world_cover

class WorldCoverLayer final : public WorldLayer {
public:
    explicit WorldCoverLayer(std::shared_ptr<Shared> sh) : sh_(std::move(sh)) {}
    std::string_view name() const override { return kLayerWorldCover; }
    std::vector<std::string> reads() const override { return {std::string(kLayerJunctions), std::string(kLayerVegCorridor)}; }
    std::vector<TileKey> tiles() const override {
        std::vector<TileKey> t;
        for (const auto& [k, e] : store_.entries()) t.push_back(k);
        return t;
    }
    std::shared_ptr<const void> tile_data(TileKey t) const override { return store_.get(t); }
    std::uint64_t tile_version(TileKey t) const override { return store_.version(t); }

    void rebuild_all(const LayerContext& ctx, LayerChanges& out) override {
        const CorridorModel& cm = *ctx.state<CorridorModel>(kLayerVegCorridor);
        std::map<TileKey, VegetationTile> next;
        for (const TileKey& t : world_tiles(ctx.params().tile_size)) build(ctx, cm, t, next);
        store_.commit_all(next, ctx.world_version(), out, [](const VegetationTile& a, const VegetationTile& b) { return same_content(a, b); });
    }
    void update(const LayerContext& ctx, const ObjectChanges& gr, LayerChanges& out) override {
        const CorridorModel& cm = *ctx.state<CorridorModel>(kLayerVegCorridor);
        const ObjectChanges* cc = ctx.changes_of(kLayerVegCorridor);
        if (gr.everything || cm.all_dirty() || (cc && cc->everything)) return rebuild_all(ctx, out);
        const double ts = ctx.params().tile_size;
        const VegetationRules& r = *sh_->rules;
        const double grow = r.clear_margin + r.blend_band + r.world_cover.ground_cell + 2.0;
        std::set<TileKey> cand;
        const std::vector<TileKey> all = world_tiles(ts);
        for (const Aabb2& b0 : cm.dirty()) {
            const Aabb2 b = box_grow(b0, grow);
            const TileKey lo = tile_of(b.lo, ts), hi = tile_of(b.hi, ts);
            for (const TileKey& t : all)
                if (t.i >= lo.i && t.i <= hi.i && t.j >= lo.j && t.j <= hi.j) cand.insert(t);
        }
        std::map<TileKey, VegetationTile> next;
        for (const TileKey& t : cand) build(ctx, cm, t, next);
        store_.commit(cand, next, ctx.world_version(), out, [](const VegetationTile& a, const VegetationTile& b) { return same_content(a, b); });
    }

private:
    std::vector<TileKey> world_tiles(double ts) const {
        std::vector<TileKey> out;
        const TileKey lo = tile_of(sh_->params.world_min, ts);
        const TileKey hi = tile_of({sh_->params.world_max.x - 1e-6, sh_->params.world_max.y - 1e-6}, ts);
        for (i64 j = lo.j; j <= hi.j; ++j)
            for (i64 i = lo.i; i <= hi.i; ++i) out.push_back({i, j});
        return out;
    }

    void build(const LayerContext& ctx, const CorridorModel& cm, TileKey t, std::map<TileKey, VegetationTile>& next) const {
        const double ts = ctx.params().tile_size;
        const VegetationRules& R = *sh_->rules;
        const WorldCoverSource& src = *sh_->cover;
        const Vec3 org = tile_origin(t, ts);
        const Vec2 lo{org.x, org.y}, hi{org.x + ts, org.y + ts};
        PieceMap pm;
        VegetationTile& vt = tile_in(pm, t, ts);

        // Ground patches: adaptive quadtree against the corridor distance field.
        {
            const int n = std::max(1, static_cast<int>(std::lround(ts / R.world_cover.ground_cell)));
            const double base = ts / n;
            const double margin = R.clear_margin, band = R.blend_band;
            std::map<std::string, GroundPatchGroup> groups;
            const auto emit = [&](Vec2 c, double size, double dens) {
                const CoverGround g = src.ground_at(c);
                if (g.type_id.empty() || g.density <= 0) return;
                GroundPatchGroup& grp = groups[g.type_id];
                grp.type_id = g.type_id;
                GroundPatch p;
                p.centre = rel({c.x, c.y, sh_->ground_z(c.x, c.y)}, org);
                p.half_size = static_cast<float>(size * 0.5);
                p.density = static_cast<float>(g.density * dens);
                p.height_class = g.height_class;
                grp.patches.push_back(p);
            };
            const auto rec = [&](auto&& self, Vec2 cl, double size) -> void {
                const Vec2 c{cl.x + size / 2, cl.y + size / 2};
                const double h = size * 0.70710678;
                const double cap = std::min(64.0, margin + band + h + 2);
                const double sd = cm.signed_distance(c, cap);
                if (sd + h <= margin) return;
                if (sd - h >= margin + band) return emit(c, size, 1.0);
                if (size > R.world_cover.min_cell * 1.0001) {
                    const double s2 = size / 2;
                    self(self, cl, s2);
                    self(self, {cl.x + s2, cl.y}, s2);
                    self(self, {cl.x, cl.y + s2}, s2);
                    self(self, {cl.x + s2, cl.y + s2}, s2);
                    return;
                }
                const double d = R.density_at(sd);
                if (d > 0.01) emit(c, size, d);
            };
            for (int j = 0; j < n; ++j)
                for (int i = 0; i < n; ++i) rec(rec, {lo.x + i * base, lo.y + j * base}, base);
            for (auto& [id, g] : groups) vt.patches.push_back(std::move(g));
        }

        // Point entities (trees): thinned by the corridor mask with a per-entity threshold.
        {
            std::vector<CoverPoint> pts;
            src.points_in(lo, hi, pts);
            for (const CoverPoint& p : pts) {
                if (tile_of(p.pos, ts) != t) continue;
                const double c = cm.clear(p.pos, p.tall);
                if (c <= h01(p.seed, 0xC0FFEE)) continue;
                const double sd = cm.signed_distance(p.pos, R.clear_margin + R.cut_edge + 1);
                const VegLayer layer = sd < R.clear_margin + R.cut_edge ? VegLayer::CutEdge : VegLayer::WorldCover;
                const VegTypeDef* def = R.type(p.type_id);
                add_instance(pm, ts, p.type_id, def, {p.pos.x, p.pos.y, sh_->ground_z(p.pos.x, p.pos.y)}, p.yaw, p.scale, p.seed, layer,
                             sh_->params.leaf_state);
            }
        }

        // Hedge lines: cut hard at clear_margin (a cut face), pieces owned by segment midpoint.
        {
            std::vector<CoverLine> lines;
            src.lines_touching({lo.x - 2, lo.y - 2}, {hi.x + 2, hi.y + 2}, lines);
            for (const CoverLine& l : lines) {
                const auto at = [&](double s) {
                    std::size_t k = static_cast<std::size_t>(std::upper_bound(l.s.begin(), l.s.end(), s) - l.s.begin());
                    k = std::clamp<std::size_t>(k, 1, l.s.size() - 1);
                    const double u = (s - l.s[k - 1]) / std::max(1e-12, l.s[k] - l.s[k - 1]);
                    return l.points[k - 1] + (l.points[k] - l.points[k - 1]) * u;
                };
                if (l.points.size() < 2) continue;
                const auto ok = [&](double s) {
                    const Vec2 p = at(s);
                    return cm.signed_distance(p, R.clear_margin + 1) >= R.clear_margin && !cm.tall_blocked(p);
                };
                for (const std::vector<double>& run : cut_param_runs(l.s, ok)) {
                    if (run.back() - run.front() < 1.0) continue;
                    std::vector<Vec3> w;
                    for (double s : run) {
                        const Vec2 p = at(s);
                        w.push_back({p.x, p.y, sh_->ground_z(p.x, p.y)});
                    }
                    for (const TilePiece& pc : split_by_tile(w, ts)) {
                        if (pc.tile != t) continue;
                        HedgeRun h;
                        h.type_id = l.type_id;
                        h.style = l.style;
                        h.height = l.height;
                        h.width = l.width;
                        h.seed = l.seed;
                        h.s0 = run[pc.first];
                        for (std::size_t k = pc.first; k <= pc.last; ++k) h.points.push_back(rel(w[k], org));
                        vt.hedges.push_back(std::move(h));
                    }
                }
            }
        }
        finish_tile(vt);
        if (!vt.empty()) next[t] = std::move(vt);
    }

    std::shared_ptr<Shared> sh_;
    TileStore<VegetationTile> store_;
};

// ---------------------------------------------------------------- vegetation (road edge)

using ObjKey = std::pair<int, u64>;  // (0 edge | 1 island, id)

struct GenEnv {
    const RoadGraph& graph;
    const RegionPack& region;
    const VegetationRules& rules;
    const CorridorModel& cm;
    const JunctionSet& js;
    const std::vector<RoundaboutLayout>& rb;
    const StreetLightParams& lp;
    const VegetationParams& params;
    double ts;
};

// Miter inset of a CCW ring by d (approximate for sharp corners).
std::vector<Vec2> inset_ring(const std::vector<Vec2>& r, double d) {
    std::vector<Vec2> out;
    const std::size_t n = r.size();
    for (std::size_t i = 0; i < n; ++i) {
        const Vec2 p = r[(i + n - 1) % n], c = r[i], q = r[(i + 1) % n];
        Vec2 e1 = c - p, e2 = q - c;
        const double l1 = std::max(1e-9, e1.length()), l2 = std::max(1e-9, e2.length());
        const Vec2 n1{-e1.y / l1, e1.x / l1}, n2{-e2.y / l2, e2.x / l2};
        const double den = std::max(0.3, 1 + n1.dot(n2));
        out.push_back(c + (n1 + n2) * (d / den));
    }
    return out;
}
double ring_area(const std::vector<Vec2>& r) {
    double a = 0;
    for (std::size_t i = 0, j = r.size() - 1; i < r.size(); j = i++) a += r[j].cross(r[i]);
    return 0.5 * a;
}

void gen_island(const GenEnv& v, const RoundaboutLayout& L, PieceMap& out, LeafState leaf) {
    if (!L.ok || L.group.nodes.empty()) return;
    const NodeId first = L.group.nodes.front();
    const Node* nd = v.graph.node(first);
    const double z = nd ? nd->pos.z : 0.0;
    const u64 oseed = mix64(v.params.world_seed ^ mix64(0x15FA0000ull + first.value));
    const auto plant = [&](const IslandRule& rule, const std::vector<Vec2>& ring, u64 seed, bool allow_tree) {
        if (!rule.enabled || ring.size() < 3 || ring_area(ring) < rule.min_area) return;
        std::vector<Vec2> in = inset_ring(ring, rule.inset);
        if (ring_area(in) < 0.5) return;
        Vec2 cen{0, 0};
        double a2 = 0;
        for (std::size_t i = 0, j = in.size() - 1; i < in.size(); j = i++) {
            const double cr = in[j].cross(in[i]);
            a2 += cr;
            cen = cen + (in[j] + in[i]) * cr;
        }
        cen = cen * (1.0 / (3 * a2));
        const TileKey t = tile_of(cen, v.ts);
        VegetationTile& vt = tile_in(out, t, v.ts);
        if (!rule.grass_type.empty()) {
            AreaPlanting ap;
            ap.type_id = rule.grass_type;
            ap.density = static_cast<float>(rule.grass_density);
            ap.height_class = static_cast<std::uint8_t>(rule.grass_height_class);
            ap.seed = seed;
            for (const Vec2& p : in) ap.ring.push_back(rel({p.x, p.y, z}, vt.origin));
            vt.areas.push_back(std::move(ap));
        }
        const double rad = std::sqrt(std::abs(ring_area(in)) / kPi);
        bool tree = false;
        if (allow_tree && !rule.tree_type.empty() && rule.tree_min_radius > 0 && rad >= rule.tree_min_radius &&
            h01(seed, 5) < rule.tree_probability && in_ring(cen, in) && ring_dist(in, cen) > 1.5) {
            add_instance(out, v.ts, rule.tree_type, v.rules.type(rule.tree_type), {cen.x, cen.y, z}, h01(seed, 6) * 2 * kPi - kPi,
                         lerp(0.9, 1.1, h01(seed, 7)), mix64(seed + 99), VegLayer::RoadEdge, leaf);
            tree = true;
        }
        if (!rule.shrub_type.empty() && rule.shrub_spacing > 0.5) {
            Aabb2 b;
            for (const Vec2& p : in) box_add(b, p);
            const double sp = rule.shrub_spacing;
            for (i64 cy = static_cast<i64>(std::floor(b.lo.y / sp)); cy <= static_cast<i64>(std::floor(b.hi.y / sp)); ++cy)
                for (i64 cx = static_cast<i64>(std::floor(b.lo.x / sp)); cx <= static_cast<i64>(std::floor(b.hi.x / sp)); ++cx) {
                    const u64 k = key3(seed, 0x54, ui(cx), ui(cy));
                    if (h01(k, 1) >= rule.shrub_probability) continue;
                    const Vec2 p{(static_cast<double>(cx) + h01(k, 2)) * sp, (static_cast<double>(cy) + h01(k, 3)) * sp};
                    if (!in_ring(p, in) || ring_dist(in, p) < 0.8) continue;
                    if (tree && (p - cen).length() < 2.5) continue;
                    add_instance(out, v.ts, rule.shrub_type, v.rules.type(rule.shrub_type), {p.x, p.y, z}, h01(k, 4) * 2 * kPi - kPi,
                                 lerp(0.8, 1.2, h01(k, 5)), mix64(k + 7), VegLayer::RoadEdge, leaf);
                }
        }
    };
    if (L.island_surface == "grass")
        for (std::size_t i = 0; i < L.island.size(); ++i)
            if (ring_area(L.island[i]) > 0) plant(v.rules.island, L.island[i], key3(oseed, 1, i), true);
    if (L.splitter_surface == "grass")
        for (std::size_t i = 0; i < L.splitters.size(); ++i)
            if (ring_area(L.splitters[i].polygon) > 0) plant(v.rules.splitter, L.splitters[i].polygon, key3(oseed, 2, i), true);
}

void gen_edge(const GenEnv& v, EdgeId id, PieceMap& out) {
    const Edge* e = v.graph.edge(id);
    if (!e) return;
    const RoadType* rt = v.region.road_type(e->road_type);
    if (!rt) return;
    const RoadVegetationRule& rule = v.rules.for_road(e->road_type);
    const Section sec = compute_section(*e, *rt, v.region);
    if (!sec.ok) return;
    const SurfaceProfile prof = make_surface_profile(v.region, *e);
    const double len = e->horizontal.length();
    if (len <= 0) return;
    const u64 oseed = mix64(v.params.world_seed ^ mix64(0xED6E0000ull + id.value));
    const LeafState leaf = v.params.leaf_state;

    const auto trim_at = [&](NodeId n, bool at_a) {
        const Junction* j = v.js.find(n);
        if (!j) return 0.0;
        for (std::size_t i = 0; i < j->arms.size() && i < j->trims.size(); ++i)
            if (j->arms[i].edge == id && j->arms[i].at_a == at_a) return j->trims[i];
        return 0.0;
    };
    const double s_lo = trim_at(e->a, true), s_hi = len - trim_at(e->b, false);
    if (s_hi - s_lo < 1.0) return;
    const auto on_structure = [&](double s, double margin) {
        for (const StructureSpan& sp : e->structure)
            if (sp.kind != StructureKind::Ground && s >= sp.s0 - margin && s <= sp.s1 + margin) return true;
        return false;
    };
    const auto heading = [&](double s) { return edge_heading(*e, s); };
    const auto curvature = [&](double s) { return wrap_angle(heading(std::min(len, s + 0.25)) - heading(std::max(0.0, s - 0.25))) / 0.5; };
    // Signed lateral d (+ left of a -> b).
    const auto plan = [&](double s, double d) {
        const Vec3 c = edge_point(*e, s);
        const double hd = heading(s);
        return Vec2{c.x - std::sin(hd) * d, c.y + std::cos(hd) * d};
    };
    const auto world = [&](double s, double d) {
        const Vec3 c = edge_point(*e, s);
        const Vec2 p = plan(s, d);
        const double lat = std::clamp(d, prof.valid() ? prof.right() : 0.0, prof.valid() ? prof.left() : 0.0);
        return Vec3{p.x, p.y, c.z + (prof.valid() ? prof.offset(lat, true) : 0.0)};
    };
    const auto fold = [&](double s, double d) { return 1 - d * curvature(s) < 0.15; };
    const auto tall_type = [&](const std::string& t) {
        const VegTypeDef* d = v.rules.type(t);
        return d ? d->height_max > v.rules.sight.height : true;
    };

    // Street lights (for tree clearance).
    std::vector<Vec2> lights;
    if (rule.street_trees.enabled) {
        for (const StreetLight& l : generate_edge_street_lights(v.graph, v.region, v.js, id, &v.rb, v.lp)) lights.push_back(l.position.xy());
        for (NodeId n : {e->a, e->b})
            if (v.js.find(n))
                for (const StreetLight& l : generate_junction_street_lights(v.graph, v.region, v.js, n, &v.rb, v.lp)) lights.push_back(l.position.xy());
    }

    const bool ml = median_is_left(v.region);
    for (int si = 0; si < 2; ++si) {
        const double sg = si == 0 ? 1.0 : -1.0;
        const SideX& sx = si == 0 ? sec.L : sec.R;
        if (sx.ext <= 0) continue;
        const bool median = rt->dual && ((si == 0) == ml);
        const VergeRule& vr = rule.verge;
        const double gap = 0.02;
        const double vw = median ? (vr.median_grass ? rt->median_gap * 0.5 : 0.0) : vr.width;
        // Keyed by nearside / offside (not left / right) so a mirrored layout mirrors the random choices too.
        const u64 sseed = key3(oseed, ui(((si == 0) == (v.region.driving_side == DrivingSide::Left)) ? 0 : 1));

        // ---- ribbons
        const auto ribbon = [&](const std::function<double(double)>& din, const std::function<double(double)>& dout, bool test_inner, const std::string& type,
                                int hc, double density, u64 salt) {
            if (type.empty()) return;
            const int n = std::max(1, static_cast<int>(std::ceil((s_hi - s_lo) / 2.0)));
            std::vector<double> us;
            for (int k = 0; k <= n; ++k) us.push_back(lerp(s_lo, s_hi, static_cast<double>(k) / n));
            const auto ok = [&](double s) {
                if (on_structure(s, 0.5)) return false;
                const double d0 = sg * dout(s), d1 = sg * (test_inner ? din(s) : dout(s));
                if (fold(s, d0) || fold(s, d1)) return false;
                for (const double d : {d0, 0.5 * (d0 + d1), d1})
                    if (v.cm.paved_distance(plan(s, d), id) < 0.0) return false;
                return true;
            };
            for (const std::vector<double>& run : cut_param_runs(us, ok)) {
                if (run.back() - run.front() < 1.0) continue;
                std::vector<Vec3> wi, wo, wm;
                for (double s : run) {
                    wi.push_back(world(s, sg * din(s)));
                    wo.push_back(world(s, sg * dout(s)));
                    wm.push_back({(wi.back().x + wo.back().x) / 2, (wi.back().y + wo.back().y) / 2, 0});
                }
                for (const TilePiece& pc : split_by_tile(wm, v.ts)) {
                    VegetationTile& vt = tile_in(out, pc.tile, v.ts);
                    GrassStrip gs;
                    gs.type_id = type;
                    gs.density = static_cast<float>(density);
                    gs.height_class = static_cast<std::uint8_t>(std::clamp(hc, 0, 255));
                    gs.seed = key3(sseed, salt);
                    gs.s0 = run[pc.first];
                    gs.edge = id;
                    gs.side = static_cast<std::int8_t>(si == 0 ? 1 : -1);
                    for (std::size_t k = pc.first; k <= pc.last; ++k) {
                        gs.inner.push_back(rel(wi[k], vt.origin));
                        gs.outer.push_back(rel(wo[k], vt.origin));
                    }
                    vt.strips.push_back(std::move(gs));
                }
            }
        };

        const double d_in = sx.back + gap;
        if (vw > 0.05) {
            if (median) {
                const std::string& mt = vr.median_type.empty() ? vr.rough_type : vr.median_type;
                ribbon([&](double) { return d_in; }, [&](double) { return d_in + vw; }, true, mt, vr.median_height_class, vr.density, 1);
            } else {
                const double mw = std::clamp(vr.mown_width, 0.0, vw);
                if (mw > 0.05)
                    ribbon([&](double) { return d_in; }, [&](double) { return d_in + mw; }, true, vr.mown_type, vr.mown_height_class, vr.density, 2);
                if (vw - mw > 0.05)
                    ribbon([&](double) { return d_in + mw; }, [&](double) { return d_in + vw; }, true, vr.rough_type, vr.rough_height_class, vr.density, 3);
            }
        }
        // Ragged creep over the carriageway edge.
        if (!median && !vr.creep_type.empty() && sx.creep_ok) {
            const double amp = sx.hard_room > 0 ? std::min(vr.creep, sx.hard_room) : vr.creep_overrun;
            if (amp > 0.01) {
                const u64 nseed = key3(sseed, 0xC4EE9);
                ribbon([&](double s) { return sx.back - amp * (0.25 + 0.75 * (0.5 + 0.5 * vnoise1(nseed, s / 2.5)) * (0.6 + 0.4 * vnoise1(nseed ^ 1, s / 0.9))); },
                       [&](double) { return sx.back + gap; }, false, vr.creep_type, 0, vr.creep_density, 4);
            }
        }

        if (median) continue;

        // ---- scrub on the verge
        if (rule.scrub.enabled && vr.width > 0.5 && rule.scrub.spacing > 0.5) {
            const ScrubRule& sc = rule.scrub;
            const std::int64_t nslots = static_cast<std::int64_t>(std::ceil(len / sc.spacing));
            for (std::int64_t k = 0; k < nslots; ++k) {
                const u64 kk = key3(sseed, 0x5C, ui(k));
                if (h01(kk, 1) >= sc.probability) continue;
                const double s = (static_cast<double>(k) + h01(kk, 2)) * sc.spacing;
                if (s < s_lo || s > s_hi || on_structure(s, 1.0)) continue;
                const double d = d_in + vr.width * (sc.band_lo + (sc.band_hi - sc.band_lo) * h01(kk, 3));
                if (fold(s, sg * d)) continue;
                const Vec2 p = plan(s, sg * d);
                if (v.cm.paved_distance(p, id) < 0.6) continue;
                if (tall_type(sc.type) && v.cm.in_sight_triangle(p)) continue;
                const Vec3 w = world(s, sg * d);
                add_instance(out, v.ts, sc.type, v.rules.type(sc.type), w, h01(kk, 4) * 2 * kPi - kPi, lerp(sc.scale_min, sc.scale_max, h01(kk, 5)),
                             mix64(kk + 5), VegLayer::RoadEdge, leaf);
            }
        }

        // ---- street trees / belts
        if (rule.street_trees.enabled && !rule.street_trees.classes.empty() && rule.street_trees.spacing > 0.5) {
            const StreetTreeRule& st = rule.street_trees;
            for (int r = 0; r < std::max(1, st.rows); ++r) {
                const double base = std::max(sx.cw + st.setback, sx.back + st.offset_from_back) + r * st.row_spacing;
                const std::int64_t nslots = static_cast<std::int64_t>(std::ceil(len / st.spacing));
                for (std::int64_t k = 0; k < nslots; ++k) {
                    const u64 kk = key3(sseed, 0x7E00 + ui(r), ui(k));
                    if (h01(kk, 0) >= st.probability) continue;
                    const double s = (static_cast<double>(k) + 0.5 + (h01(kk, 1) - 0.5) * 2 * st.spacing_jitter) * st.spacing;
                    if (s < s_lo || s > s_hi || on_structure(s, st.structure_clearance)) continue;
                    const double d = base + (h01(kk, 2) - 0.5) * 2 * st.lateral_jitter;
                    if (fold(s, sg * d)) continue;
                    const TreeClass* tc = pick_class(st.classes, h01(kk, 3));
                    if (!tc) continue;
                    const VegTypeDef* def = v.rules.type(tc->type);
                    const double clearance = std::max(st.paved_clearance, 0.3 * (def ? def->crown_radius : 0.0));
                    const Vec2 p = plan(s, sg * d);
                    if (v.cm.paved_distance(p, id) < clearance) continue;
                    if (tall_type(tc->type) && (v.cm.in_sight_triangle(p) || v.cm.tall_blocked(p))) continue;
                    bool near_light = false;
                    for (const Vec2& l : lights) near_light = near_light || (l - p).length() < st.light_clearance;
                    if (near_light) continue;
                    add_instance(out, v.ts, tc->type, def, world(s, sg * d), h01(kk, 4) * 2 * kPi - kPi, lerp(st.scale_min, st.scale_max, h01(kk, 5)),
                                 mix64(kk + 9), VegLayer::RoadEdge, leaf);
                }
            }
        }
    }
    // HOOK (out of scope): scrub / saplings on earthwork slopes. Grading output is not read here; when the grading
    // layer exists, read its slope polygons and add a rule in this function ("slope_scrub"), keyed by edge id.
}

std::vector<EdgeId> run_mates(const RoadGraph& g, EdgeId id) {
    std::vector<EdgeId> out;
    std::set<EdgeId> seen{id};
    std::vector<EdgeId> work{id};
    while (!work.empty()) {
        const EdgeId cur = work.back();
        work.pop_back();
        const Edge* e = g.edge(cur);
        if (!e) continue;
        for (NodeId n : {e->a, e->b}) {
            const std::vector<EdgeId> at = g.edges_at(n);
            if (at.size() != 2) continue;
            for (EdgeId o : at) {
                if (o == cur || seen.count(o)) continue;
                const Edge* oe = g.edge(o);
                if (oe && oe->road_type == e->road_type && oe->one_way == e->one_way) {
                    seen.insert(o);
                    out.push_back(o);
                    work.push_back(o);
                }
            }
        }
    }
    return out;
}

class RoadEdgeLayer final : public WorldLayer {
public:
    explicit RoadEdgeLayer(std::shared_ptr<Shared> sh) : sh_(std::move(sh)) {}
    std::string_view name() const override { return kLayerVegetation; }
    std::vector<std::string> reads() const override { return {std::string(kLayerJunctions), std::string(kLayerVegCorridor)}; }
    std::vector<TileKey> tiles() const override {
        std::vector<TileKey> t;
        for (const auto& [k, e] : store_.entries()) t.push_back(k);
        return t;
    }
    std::shared_ptr<const void> tile_data(TileKey t) const override { return store_.get(t); }
    std::uint64_t tile_version(TileKey t) const override { return store_.version(t); }

    void rebuild_all(const LayerContext& ctx, LayerChanges& out) override {
        const CorridorModel& cm = *ctx.state<CorridorModel>(kLayerVegCorridor);
        const JunctionState& js = *ctx.state<JunctionState>(kLayerJunctions);
        objs_.clear();
        by_tile_.clear();
        std::set<TileKey> cand;
        for (EdgeId id : cm.edge_ids()) regen({0, id.value}, ctx, cm, js, cand);
        for (NodeId n : cm.island_ids()) regen({1, n.value}, ctx, cm, js, cand);
        std::map<TileKey, VegetationTile> next;
        compose(ctx, cand, next);
        store_.commit_all(next, ctx.world_version(), out, [](const VegetationTile& a, const VegetationTile& b) { return same_content(a, b); });
    }

    void update(const LayerContext& ctx, const ObjectChanges& gr, LayerChanges& out) override {
        const CorridorModel& cm = *ctx.state<CorridorModel>(kLayerVegCorridor);
        const ObjectChanges& rj = *ctx.changes_of(kLayerJunctions);
        if (gr.everything || rj.everything || cm.all_dirty()) return rebuild_all(ctx, out);
        const JunctionState& js = *ctx.state<JunctionState>(kLayerJunctions);
        const RoadGraph& g = ctx.graph();
        // Seeds: dirty edges, edges at dirty / rebuilt-junction nodes, and their run-mates (street-light phase).
        std::set<NodeId> nodes(gr.nodes.begin(), gr.nodes.end());
        nodes.insert(rj.nodes.begin(), rj.nodes.end());
        std::set<EdgeId> force(gr.edges.begin(), gr.edges.end());
        for (EdgeId e : edges_at_nodes(g, nodes)) force.insert(e);
        const std::set<EdgeId> seeds = force;
        for (EdgeId e : seeds)
            for (EdgeId m : run_mates(g, e)) force.insert(m);
        const double R = sh_->rules->reach();
        std::set<TileKey> cand;
        std::set<ObjKey> todo;
        const std::vector<EdgeId> live = cm.edge_ids();
        std::set<u64> live_ids;
        for (EdgeId id : live) {
            live_ids.insert(id.value);
            bool hit = force.count(id) > 0;
            if (!hit) {
                const Aabb2 b = box_grow(cm.edge_box(id), R);
                for (const Aabb2& d : cm.dirty()) hit = hit || box_hit(b, d);
            }
            if (hit || !objs_.count({0, id.value})) todo.insert({0, id.value});
        }
        std::set<u64> live_islands;
        for (NodeId n : cm.island_ids()) {
            live_islands.insert(n.value);
            bool hit = false;
            const Aabb2 b = box_grow(cm.island_box(n), R);
            for (const Aabb2& d : cm.dirty()) hit = hit || box_hit(b, d);
            if (hit || !objs_.count({1, n.value})) todo.insert({1, n.value});
        }
        for (auto it = objs_.begin(); it != objs_.end();) {
            const bool alive = it->first.first == 0 ? live_ids.count(it->first.second) > 0 : live_islands.count(it->first.second) > 0;
            if (alive) {
                ++it;
                continue;
            }
            drop(it->first, cand);
            it = objs_.erase(it);
        }
        for (const ObjKey& k : todo) regen(k, ctx, cm, js, cand);
        std::map<TileKey, VegetationTile> next;
        compose(ctx, cand, next);
        store_.commit(cand, next, ctx.world_version(), out, [](const VegetationTile& a, const VegetationTile& b) { return same_content(a, b); });
    }

private:
    void drop(const ObjKey& k, std::set<TileKey>& cand) {
        const auto it = objs_.find(k);
        if (it == objs_.end()) return;
        for (const auto& [t, p] : it->second) {
            cand.insert(t);
            by_tile_[t].erase(k);
            if (by_tile_[t].empty()) by_tile_.erase(t);
        }
    }
    void regen(const ObjKey& k, const LayerContext& ctx, const CorridorModel& cm, const JunctionState& js, std::set<TileKey>& cand) {
        drop(k, cand);
        const GenEnv env{ctx.graph(), ctx.region(), *sh_->rules, cm, js.junctions, js.roundabouts.layouts, ctx.params().street_lights, sh_->params,
                         ctx.params().tile_size};
        PieceMap pm;
        if (k.first == 0) {
            gen_edge(env, EdgeId{k.second}, pm);
        } else {
            for (const RoundaboutLayout& l : js.roundabouts.layouts)
                if (l.ok && !l.group.nodes.empty() && l.group.nodes.front().value == k.second) gen_island(env, l, pm, sh_->params.leaf_state);
        }
        for (auto& [t, p] : pm) {
            cand.insert(t);
            by_tile_[t].insert(k);
        }
        objs_[k] = std::move(pm);
    }
    void compose(const LayerContext& ctx, const std::set<TileKey>& cand, std::map<TileKey, VegetationTile>& next) const {
        const double ts = ctx.params().tile_size;
        for (const TileKey& t : cand) {
            const auto bt = by_tile_.find(t);
            if (bt == by_tile_.end()) continue;
            VegetationTile vt;
            vt.tile = t;
            vt.origin = tile_origin(t, ts);
            for (const ObjKey& k : bt->second) append_piece(vt, objs_.at(k).at(t));
            finish_tile(vt);
            if (!vt.empty()) next[t] = std::move(vt);
        }
    }

    std::shared_ptr<Shared> sh_;
    std::map<ObjKey, PieceMap> objs_;
    std::map<TileKey, std::set<ObjKey>> by_tile_;
    TileStore<VegetationTile> store_;
};

}  // namespace

bool add_vegetation_layers(WorldCache& cache, std::shared_ptr<const VegetationRules> rules, VegetationParams params, std::string* error) {
    if (!rules) {
        if (error) *error = "null vegetation rules";
        return false;
    }
    auto sh = std::make_shared<Shared>();
    sh->rules = rules;
    sh->cover = params.cover ? params.cover : make_default_cover_source(rules, params.world_seed);
    sh->params = std::move(params);
    if (!cache.add_layer(std::make_unique<CorridorLayer>(sh), error)) return false;
    if (sh->params.world_cover && !cache.add_layer(std::make_unique<WorldCoverLayer>(sh), error)) return false;
    if (sh->params.road_edge && !cache.add_layer(std::make_unique<RoadEdgeLayer>(sh), error)) return false;
    return true;
}

}  // namespace cp
