// No exceptions: sim_core builds with -fno-exceptions (Unreal modules disable them).
// Errors travel as a message in Err; the first error wins and parsing stops.
#include "cp/region_pack.h"

#include <cmath>
#include <fstream>
#include <sstream>

#include "nlohmann/json.hpp"

namespace cp {

namespace {

using json = nlohmann::json;

struct Err {
    std::string msg;
    bool failed() const { return !msg.empty(); }
    void set(const std::string& m) {
        if (msg.empty()) msg = m;
    }
};

// Returns the member, or nullptr after recording "missing".
const json* require(const json& j, const char* key, const std::string& ctx, Err& err) {
    if (err.failed()) return nullptr;
    if (!j.is_object()) {
        err.set(ctx + ": expected an object");
        return nullptr;
    }
    auto it = j.find(key);
    if (it == j.end()) {
        err.set(ctx + ": missing \"" + key + "\"");
        return nullptr;
    }
    return &*it;
}

const json* optional_member(const json& j, const char* key) {
    if (!j.is_object()) return nullptr;
    auto it = j.find(key);
    return it == j.end() ? nullptr : &*it;
}

std::string get_string(const json& j, const char* key, const std::string& ctx, Err& err) {
    const json* v = require(j, key, ctx, err);
    if (!v) return {};
    if (!v->is_string()) {
        err.set(ctx + ": \"" + key + "\" must be a string");
        return {};
    }
    return v->get<std::string>();
}

double get_number(const json& j, const char* key, const std::string& ctx, Err& err) {
    const json* v = require(j, key, ctx, err);
    if (!v) return 0;
    if (!v->is_number()) {
        err.set(ctx + ": \"" + key + "\" must be a number");
        return 0;
    }
    return v->get<double>();
}

double opt_number(const json& j, const char* key, double def, const std::string& ctx, Err& err) {
    return optional_member(j, key) ? get_number(j, key, ctx, err) : def;
}

bool get_bool(const json& j, const char* key, bool def, const std::string& ctx, Err& err) {
    const json* v = optional_member(j, key);
    if (!v) return def;
    if (!v->is_boolean()) {
        err.set(ctx + ": \"" + key + "\" must be a boolean");
        return def;
    }
    return v->get<bool>();
}

json parse_json(const std::string& text, const std::string& what, Err& err) {
    if (err.failed()) return {};
    json j = json::parse(text, nullptr, /*allow_exceptions=*/false);
    if (j.is_discarded()) {
        err.set(what + ": invalid JSON");
        return {};
    }
    return j;
}

bool read_file(const std::filesystem::path& p, std::string& out, Err& err) {
    std::ifstream in(p, std::ios::binary);
    if (!in) {
        err.set("cannot open " + p.string());
        return false;
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    out = ss.str();
    return true;
}

LaneTypeTable do_parse_lane_types(const std::string& text, Err& err) {
    LaneTypeTable out;
    const json j = parse_json(text, "lane_types", err);
    const json* arr = require(j, "lane_types", "lane_types", err);
    if (!arr) return out;
    if (!arr->is_array()) {
        err.set("lane_types: \"lane_types\" must be an array");
        return out;
    }
    for (const json& e : *arr) {
        LaneType lt;
        lt.id = get_string(e, "id", "lane type", err);
        if (err.failed()) return out;
        const std::string ctx = "lane type \"" + lt.id + "\"";
        if (lt.id.empty()) err.set("lane type: empty id");
        if (out.count(lt.id)) err.set("duplicate lane type id \"" + lt.id + "\"");
        if (const json* users = optional_member(e, "users")) {
            if (!users->is_array()) err.set(ctx + ": \"users\" must be an array");
            else
                for (const json& u : *users) {
                    if (!u.is_string()) err.set(ctx + ": \"users\" entries must be strings");
                    else lt.users.push_back(u.get<std::string>());
                }
        }
        lt.may_park = get_bool(e, "may_park", false, ctx, err);
        lt.connects_at_junctions = get_bool(e, "connects_at_junctions", false, ctx, err);
        if (optional_member(e, "surface")) lt.surface = get_string(e, "surface", ctx, err);
        if (err.failed()) return out;
        out.emplace(lt.id, std::move(lt));
    }
    return out;
}

double speed_unit_to_mps(const std::string& unit, Err& err) {
    if (unit == "mph") return 0.44704;
    if (unit == "kph" || unit == "km/h" || unit == "kmh") return 1.0 / 3.6;
    if (unit == "mps" || unit == "m/s") return 1.0;
    err.set("region: unknown speed_unit \"" + unit + "\" (expected mph, kph or mps)");
    return 1.0;
}

std::vector<CrossSectionSlot> parse_slots(const json* arr, const std::string& ctx,
                                          const LaneTypeTable& lane_types, Err& err) {
    std::vector<CrossSectionSlot> out;
    if (!arr || err.failed()) return out;
    if (!arr->is_array()) {
        err.set(ctx + " must be an array");
        return out;
    }
    if (arr->empty()) {
        err.set(ctx + " is empty");
        return out;
    }
    int index = 0;
    for (const json& e : *arr) {
        const std::string sctx = ctx + "[" + std::to_string(index++) + "]";
        CrossSectionSlot s;
        s.lane_type = get_string(e, "lane", sctx, err);
        s.width = get_number(e, "width_m", sctx, err);
        if (err.failed()) return out;
        auto lt = lane_types.find(s.lane_type);
        if (lt == lane_types.end()) {
            err.set(sctx + ": unknown lane type id \"" + s.lane_type + "\"");
            return out;
        }
        if (!(s.width > 0) || !std::isfinite(s.width)) {
            err.set(sctx + ": width_m must be positive, got " + std::to_string(s.width));
            return out;
        }
        if (optional_member(e, "dir")) {
            const std::string d = get_string(e, "dir", sctx, err);
            if (d == "forward") s.dir = TravelDir::Forward;
            else if (d == "backward") s.dir = TravelDir::Backward;
            else if (d == "none") s.dir = TravelDir::None;
            else err.set(sctx + ": dir must be forward, backward or none, got \"" + d + "\"");
        } else {
            bool car = false;
            for (const auto& u : lt->second.users) car = car || u == "car";
            s.dir = (car && !lt->second.may_park) ? TravelDir::Forward : TravelDir::None;
        }
        if (optional_member(e, "side")) {
            const std::string d = get_string(e, "side", sctx, err);
            if (d == "nearside") s.side = SlotSide::Nearside;
            else if (d == "offside") s.side = SlotSide::Offside;
            else err.set(sctx + ": side must be nearside or offside, got \"" + d + "\"");
        }
        if (err.failed()) return out;
        out.push_back(std::move(s));
    }
    return out;
}

RoadType parse_road_type(const json& e, double speed_to_mps, const LaneTypeTable& lane_types, Err& err) {
    RoadType rt;
    rt.id = get_string(e, "id", "road type", err);
    if (err.failed()) return rt;
    const std::string ctx = "road type \"" + rt.id + "\"";
    if (rt.id.empty()) err.set("road type: empty id");
    if (optional_member(e, "name")) rt.name = get_string(e, "name", ctx, err);
    rt.two_way = get_bool(e, "two_way", false, ctx, err);
    rt.dual = get_bool(e, "dual", false, ctx, err);
    if (rt.two_way && rt.dual) err.set(ctx + ": cannot be both two_way and dual");
    rt.speed_limit = get_number(e, "speed_limit", ctx, err) * speed_to_mps;
    rt.design_speed = get_number(e, "design_speed_kph", ctx, err) / 3.6;
    if (err.failed()) return rt;
    if (!(rt.speed_limit > 0)) err.set(ctx + ": speed_limit must be positive");
    if (!(rt.design_speed > 0)) err.set(ctx + ": design_speed_kph must be positive");

    if (rt.dual) {
        rt.median_gap = get_number(e, "median_gap_m", ctx, err);
        if (rt.median_gap < 0) err.set(ctx + ": median_gap_m must not be negative");
        rt.carriageway =
            parse_slots(require(e, "carriageway", ctx, err), ctx + ".carriageway", lane_types, err);
    } else {
        if (rt.two_way)
            rt.half = parse_slots(require(e, "half", ctx, err), ctx + ".half", lane_types, err);
        rt.one_way_variant = parse_slots(require(e, "one_way_variant", ctx, err),
                                         ctx + ".one_way_variant", lane_types, err);
    }

    if (const json* vk = require(e, "vertical_k", ctx, err)) {
        rt.vertical_k_crest = get_number(*vk, "crest", ctx + ".vertical_k", err);
        rt.vertical_k_sag = get_number(*vk, "sag", ctx + ".vertical_k", err);
        if (!err.failed() && (!(rt.vertical_k_crest > 0) || !(rt.vertical_k_sag > 0)))
            err.set(ctx + ": vertical_k crest and sag must be positive");
    }
    rt.min_radius = opt_number(e, "min_radius_m", 0, ctx, err);
    if (rt.min_radius < 0) err.set(ctx + ": min_radius_m must not be negative");
    rt.advisory_max_grade = opt_number(e, "advisory_max_grade_pct", 0, ctx, err) / 100.0;
    if (rt.advisory_max_grade < 0) err.set(ctx + ": advisory_max_grade_pct must not be negative");
    if (optional_member(e, "markings")) rt.markings = get_string(e, "markings", ctx, err);
    return rt;
}

RegionPack do_parse_region(const LaneTypeTable& lane_types, const std::string& region_text,
                           const std::string& road_types_text, Err& err) {
    RegionPack pack;
    const json rj = parse_json(region_text, "region.json", err);
    pack.lane_types = lane_types;
    pack.id = get_string(rj, "id", "region", err);
    pack.version = static_cast<int>(opt_number(rj, "version", 0, "region", err));
    const std::string side = get_string(rj, "driving_side", "region", err);
    if (!err.failed()) {
        if (side == "left") pack.driving_side = DrivingSide::Left;
        else if (side == "right") pack.driving_side = DrivingSide::Right;
        else err.set("region: driving_side must be \"left\" or \"right\", got \"" + side + "\"");
    }
    const std::string unit = get_string(rj, "speed_unit", "region", err);
    const double speed_to_mps = err.failed() ? 1.0 : speed_unit_to_mps(unit, err);
    pack.headroom_m = opt_number(rj, "headroom_m", 0, "region", err);
    pack.plan_crossing_junction_dz_m = opt_number(rj, "plan_crossing_junction_dz_m", 0, "region", err);

    const json tj = parse_json(road_types_text, "road_types.json", err);
    const json* arr = require(tj, "road_types", "road_types.json", err);
    if (!arr) return pack;
    if (!arr->is_array()) {
        err.set("road_types.json: \"road_types\" must be an array");
        return pack;
    }
    for (const json& e : *arr) {
        RoadType rt = parse_road_type(e, speed_to_mps, lane_types, err);
        if (!err.failed() && pack.road_types.count(rt.id))
            err.set("duplicate road type id \"" + rt.id + "\"");
        if (err.failed()) return pack;
        pack.road_types.emplace(rt.id, std::move(rt));
    }
    return pack;
}

template <class T>
Loaded<T> finish(T&& value, const Err& err) {
    Loaded<T> r;
    if (err.failed()) r.error = err.msg;
    else r.value = std::move(value);
    return r;
}

}  // namespace

const LaneType* RegionPack::lane_type(const std::string& id) const {
    auto it = lane_types.find(id);
    return it == lane_types.end() ? nullptr : &it->second;
}

const RoadType* RegionPack::road_type(const std::string& id) const {
    auto it = road_types.find(id);
    return it == road_types.end() ? nullptr : &it->second;
}

Loaded<LaneTypeTable> parse_lane_types(const std::string& json_text) {
    Err err;
    return finish(do_parse_lane_types(json_text, err), err);
}

Loaded<RegionPack> parse_region_pack(const LaneTypeTable& lane_types, const std::string& region_json,
                                     const std::string& road_types_json) {
    Err err;
    return finish(do_parse_region(lane_types, region_json, road_types_json, err), err);
}

Loaded<LaneTypeTable> load_lane_types(const std::filesystem::path& file) {
    Err err;
    std::string text;
    if (!read_file(file, text, err)) return finish(LaneTypeTable{}, err);
    return finish(do_parse_lane_types(text, err), err);
}

Loaded<RegionPack> load_region_pack(const std::filesystem::path& lane_types_file,
                                    const std::filesystem::path& region_dir) {
    Err err;
    std::string lt_text, region_text, types_text;
    if (!read_file(lane_types_file, lt_text, err) || !read_file(region_dir / "region.json", region_text, err) ||
        !read_file(region_dir / "road_types.json", types_text, err))
        return finish(RegionPack{}, err);
    const LaneTypeTable lt = do_parse_lane_types(lt_text, err);
    if (err.failed()) return finish(RegionPack{}, err);
    return finish(do_parse_region(lt, region_text, types_text, err), err);
}

Loaded<RegionPack> load_region_pack_by_id(const std::filesystem::path& data_dir, const std::string& region_id) {
    return load_region_pack(data_dir / "lane_types.json", data_dir / "regions" / region_id);
}

}  // namespace cp
