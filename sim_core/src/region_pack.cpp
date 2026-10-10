// No exceptions: sim_core builds with -fno-exceptions (Unreal modules disable them).
// Errors travel as a message in Err; the first error wins and parsing stops.
#include "cp/region_pack.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <sstream>

#include "cp/units.h"
#include "json_util.h"
#include "nlohmann/json.hpp"

namespace cp {

namespace {

using json = nlohmann::json;

using jsonu::Err;

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

using jsonu::member;

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
    return member(j, key) ? get_number(j, key, ctx, err) : def;
}

bool get_bool(const json& j, const char* key, bool def, const std::string& ctx, Err& err) {
    const json* v = member(j, key);
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
        if (const json* users = member(e, "users")) {
            if (!users->is_array()) err.set(ctx + ": \"users\" must be an array");
            else
                for (const json& u : *users) {
                    if (!u.is_string()) err.set(ctx + ": \"users\" entries must be strings");
                    else lt.users.push_back(u.get<std::string>());
                }
        }
        lt.may_park = get_bool(e, "may_park", false, ctx, err);
        lt.connects_at_junctions = get_bool(e, "connects_at_junctions", false, ctx, err);
        if (member(e, "surface")) lt.surface = get_string(e, "surface", ctx, err);
        if (member(e, "profile")) {
            lt.profile = get_string(e, "profile", ctx, err);
            if (lt.profile != "carriageway" && lt.profile != "kerb" && lt.profile != "raised" &&
                lt.profile != "verge")
                err.set(ctx + ": profile must be carriageway, kerb, raised or verge");
        }
        if (err.failed()) return out;
        out.emplace(lt.id, std::move(lt));
    }
    return out;
}

double speed_unit_to_mps(const std::string& unit, Err& err) {
    double f = 1.0;
    if (!cp::speed_unit_to_mps(unit, f))
        err.set("region: unknown speed_unit \"" + unit + "\" (expected " + kSpeedUnitSpellings + ")");
    return f;
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
        if (member(e, "dir")) {
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
        if (member(e, "side")) {
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

void parse_street_lights(const json& j, RoadType& rt, const std::string& ctx, Err& err) {
    if (!j.is_object()) {
        err.set(ctx + ": expected an object");
        return;
    }
    StreetLightRules& r = rt.street_lights;
    r.lit = get_bool(j, "lit", true, ctx, err);
    r.spacing = opt_number(j, "spacing_m", r.spacing, ctx, err);
    r.setback = opt_number(j, "setback_m", r.setback, ctx, err);
    r.footway_inset = opt_number(j, "footway_inset_m", r.footway_inset, ctx, err);
    r.mounting_height = opt_number(j, "mounting_height_m", r.mounting_height, ctx, err);
    r.outreach = opt_number(j, "outreach_m", r.outreach, ctx, err);
    r.island_min_radius = opt_number(j, "island_min_radius_m", r.island_min_radius, ctx, err);
    if (member(j, "style")) r.style = get_string(j, "style", ctx, err);
    if (member(j, "arrangement")) {
        const std::string a = get_string(j, "arrangement", ctx, err);
        if (a == "single_side") r.arrangement = LightArrangement::SingleSide;
        else if (a == "staggered") r.arrangement = LightArrangement::Staggered;
        else if (a == "opposite") r.arrangement = LightArrangement::Opposite;
        else if (a == "central_reservation") r.arrangement = LightArrangement::CentralReservation;
        else if (!err.failed())
            err.set(ctx + ": arrangement must be single_side, staggered, opposite or central_reservation");
    }
    if (err.failed()) return;
    if (r.arrangement == LightArrangement::CentralReservation && !rt.dual)
        err.set(ctx + ": central_reservation needs a dual road type");
    if (r.lit && !(r.spacing >= 5 && r.spacing <= 500)) err.set(ctx + ": spacing_m must be between 5 and 500");
    if (!(r.setback >= 0 && r.setback <= 10)) err.set(ctx + ": setback_m must be between 0 and 10");
    if (!(r.footway_inset >= 0 && r.footway_inset <= 2)) err.set(ctx + ": footway_inset_m must be between 0 and 2");
    if (!(r.mounting_height > 0 && r.mounting_height <= 30)) err.set(ctx + ": mounting_height_m must be between 0 and 30");
    if (!(r.outreach >= 0 && r.outreach <= 6)) err.set(ctx + ": outreach_m must be between 0 and 6");
    if (!(r.island_min_radius >= 0)) err.set(ctx + ": island_min_radius_m must not be negative");
}

RoadType parse_road_type(const json& e, double speed_to_mps, const LaneTypeTable& lane_types, Err& err) {
    RoadType rt;
    rt.id = get_string(e, "id", "road type", err);
    if (err.failed()) return rt;
    const std::string ctx = "road type \"" + rt.id + "\"";
    if (rt.id.empty()) err.set("road type: empty id");
    if (member(e, "name")) rt.name = get_string(e, "name", ctx, err);
    rt.two_way = get_bool(e, "two_way", false, ctx, err);
    rt.dual = get_bool(e, "dual", false, ctx, err);
    if (rt.two_way && rt.dual) err.set(ctx + ": cannot be both two_way and dual");
    rt.speed_limit = get_number(e, "speed_limit", ctx, err) * speed_to_mps;
    rt.design_speed = get_number(e, "design_speed_kph", ctx, err) / 3.6;
    rt.road_class = static_cast<int>(std::lround(opt_number(e, "road_class", 0, ctx, err)));
    rt.corner_radius = opt_number(e, "corner_radius_m", 0, ctx, err);
    if (rt.corner_radius < 0) err.set(ctx + ": corner_radius_m must not be negative");
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
    if (member(e, "markings")) rt.markings = get_string(e, "markings", ctx, err);
    const auto opt_shape = [&](const char* key, std::optional<double>& out, double hi) {
        if (!member(e, key)) return;
        const double v = get_number(e, key, ctx, err);
        if (!err.failed() && !(v >= 0 && v <= hi))
            err.set(ctx + ": " + key + " must be between 0 and " + std::to_string(hi));
        out = v;
    };
    opt_shape("crossfall", rt.crossfall, 0.2);
    opt_shape("kerb_upstand_m", rt.kerb_upstand, 0.5);
    opt_shape("footway_crossfall", rt.footway_crossfall, 0.2);
    opt_shape("verge_crossfall", rt.verge_crossfall, 0.5);
    rt.roundabout_ring = get_bool(e, "roundabout_ring", false, ctx, err);
    if (rt.roundabout_ring && (rt.two_way || rt.dual))
        err.set(ctx + ": roundabout_ring types must be one-way (neither two_way nor dual)");
    if (const json* sl = member(e, "street_lights")) parse_street_lights(*sl, rt, ctx + ".street_lights", err);
    return rt;
}

// A table of {max_speed_kph (optional: open end), <value_key>} rows, in speed order.
std::vector<SpeedFactor> parse_speed_table(const json* arr, const char* value_key, const std::string& ctx, Err& err) {
    std::vector<SpeedFactor> out;
    if (!arr) return out;
    if (!arr->is_array()) {
        err.set(ctx + ": expected an array");
        return out;
    }
    int index = 0;
    for (const json& row : *arr) {
        const std::string rctx = ctx + "[" + std::to_string(index++) + "]";
        SpeedFactor f;
        if (member(row, "max_speed_kph")) f.max_speed = get_number(row, "max_speed_kph", rctx, err) / 3.6;
        f.factor = get_number(row, value_key, rctx, err);
        if (err.failed()) return out;
        if (!(f.factor > 0)) {
            err.set(rctx + ": " + value_key + " must be positive");
            return out;
        }
        if (!out.empty() && !(f.max_speed > out.back().max_speed)) {
            err.set(rctx + ": rows must be in increasing max_speed_kph order");
            return out;
        }
        out.push_back(f);
    }
    return out;
}

void parse_roundabout_extras(const json& rb, RoundaboutRules& r, Err& err) {
    const std::string c = "region.roundabout";
    r.apron_island_radius_max = opt_number(rb, "apron_island_radius_max_m", r.apron_island_radius_max, c, err);
    if (member(rb, "apron_surface")) r.apron_surface = get_string(rb, "apron_surface", c, err);
    r.splitter_speed_factors = parse_speed_table(member(rb, "splitter_speed_factors"), "factor",
                                                 c + ".splitter_speed_factors", err);
    if (const json* sc = member(rb, "size_classes")) {
        if (!sc->is_array()) {
            err.set(c + ".size_classes: expected an array");
            return;
        }
        int index = 0;
        for (const json& row : *sc) {
            const std::string rctx = c + ".size_classes[" + std::to_string(index++) + "]";
            RoundaboutSizeClass k;
            k.entry_corner_radius = r.entry_corner_radius;
            k.exit_corner_radius = r.exit_corner_radius;
            k.splitter_length = r.splitter_length;
            k.splitter_width = r.splitter_width;
            if (member(row, "max_radius_m")) k.max_radius = get_number(row, "max_radius_m", rctx, err);
            k.entry_corner_radius = opt_number(row, "entry_corner_radius_m", k.entry_corner_radius, rctx, err);
            k.exit_corner_radius = opt_number(row, "exit_corner_radius_m", k.exit_corner_radius, rctx, err);
            k.splitter_length = opt_number(row, "splitter_length_m", k.splitter_length, rctx, err);
            k.splitter_width = opt_number(row, "splitter_width_m", k.splitter_width, rctx, err);
            k.apron_width = opt_number(row, "apron_width_m", k.apron_width, rctx, err);
            if (err.failed()) return;
            if (!(k.max_radius > 0) || k.entry_corner_radius < 0 || k.exit_corner_radius < 0 || k.splitter_length < 0 ||
                k.splitter_width < 0 || k.apron_width < 0) {
                err.set(rctx + ": a value is out of range");
                return;
            }
            if (!r.size_classes.empty() && !(k.max_radius > r.size_classes.back().max_radius)) {
                err.set(rctx + ": classes must be in increasing max_radius_m order");
                return;
            }
            r.size_classes.push_back(k);
        }
    }
    r.splitter_min_taper = opt_number(rb, "splitter_min_taper_m", r.splitter_min_taper, c, err);
    if (!err.failed() && !(r.splitter_min_taper >= 0)) err.set(c + ": splitter_min_taper_m must not be negative");
    if (r.apron_island_radius_max < 0) err.set(c + ": apron_island_radius_max_m must not be negative");
}

void parse_junction_rules(const json& jr, JunctionRules& r, Err& err) {
    const std::string c = "region.junction";
    if (const json* tab = member(jr, "corner_radius_by_class")) {
        if (!tab->is_array()) {
            err.set(c + ".corner_radius_by_class: expected an array");
            return;
        }
        int index = 0;
        for (const json& row : *tab) {
            const std::string rctx = c + ".corner_radius_by_class[" + std::to_string(index++) + "]";
            CornerRadiusRule k;
            k.minor_min = static_cast<int>(opt_number(row, "minor_min", k.minor_min, rctx, err));
            k.minor_max = static_cast<int>(opt_number(row, "minor_max", k.minor_max, rctx, err));
            k.major_min = static_cast<int>(opt_number(row, "major_min", k.major_min, rctx, err));
            k.major_max = static_cast<int>(opt_number(row, "major_max", k.major_max, rctx, err));
            k.radius = get_number(row, "radius_m", rctx, err);
            if (err.failed()) return;
            if (k.radius < 0) {
                err.set(rctx + ": radius_m must not be negative");
                return;
            }
            r.corner_radius.push_back(k);
        }
    }
    r.default_corner_radius = opt_number(jr, "default_corner_radius_m", r.default_corner_radius, c, err);
    if (!err.failed() && !(r.default_corner_radius > 0)) err.set(c + ": default_corner_radius_m must be positive");
    if (const json* mg = member(jr, "merge")) {
        const std::string mc = c + ".merge";
        MergeRules& m = r.merge;
        m.taper_length = parse_speed_table(member(*mg, "taper_length"), "length_m", mc + ".taper_length", err);
        m.default_taper_length = opt_number(*mg, "default_taper_length_m", m.default_taper_length, mc, err);
        m.gore_length = opt_number(*mg, "gore_length_m", m.gore_length, mc, err);
        if (!err.failed() && (!(m.default_taper_length > 0) || !(m.gore_length >= 0))) err.set(mc + ": a value is out of range");
    }
    if (const json* sk = member(jr, "skew_factor")) {
        r.skew_min = opt_number(*sk, "min", r.skew_min, c + ".skew_factor", err);
        r.skew_max = opt_number(*sk, "max", r.skew_max, c + ".skew_factor", err);
        if (!err.failed() && (!(r.skew_min > 0.1) || r.skew_max < r.skew_min || r.skew_max > 4))
            err.set(c + ".skew_factor: need 0.1 < min <= max <= 4");
    }
    if (const json* tp = member(jr, "turn_pockets")) {
        TurnPocketRules& t = r.turn_pockets;
        const std::string tc = c + ".turn_pockets";
        t.enabled = get_bool(*tp, "enabled", t.enabled, tc, err);
        t.major_class_min = static_cast<int>(opt_number(*tp, "major_class_min", t.major_class_min, tc, err));
        t.minor_class_max = static_cast<int>(opt_number(*tp, "minor_class_max", t.minor_class_max, tc, err));
        t.lane_width = opt_number(*tp, "lane_width_m", t.lane_width, tc, err);
        t.max_bend = opt_number(*tp, "max_bend_rad", t.max_bend, tc, err);
        t.min_minor_angle = opt_number(*tp, "min_minor_angle_rad", t.min_minor_angle, tc, err);
        t.max_minor_angle = opt_number(*tp, "max_minor_angle_rad", t.max_minor_angle, tc, err);
        t.min_taper_ratio = opt_number(*tp, "min_taper_ratio", t.min_taper_ratio, tc, err);
        t.min_storage = opt_number(*tp, "min_storage_m", t.min_storage, tc, err);
        t.taper_ratio = parse_speed_table(member(*tp, "taper_ratio"), "ratio", tc + ".taper_ratio", err);
        t.storage = parse_speed_table(member(*tp, "storage"), "length_m", tc + ".storage", err);
        t.default_storage = opt_number(*tp, "default_storage_m", t.default_storage, tc, err);
        t.default_taper_ratio = opt_number(*tp, "default_taper_ratio", t.default_taper_ratio, tc, err);
        if (!err.failed() && (!(t.default_storage > 0) || !(t.default_taper_ratio >= 1))) err.set(tc + ": default_storage_m / default_taper_ratio out of range");
        if (!err.failed() && (!(t.lane_width > 1.5 && t.lane_width < 6) || t.max_bend < 0 || t.min_minor_angle < 0 ||
                              t.max_minor_angle < t.min_minor_angle || !(t.min_taper_ratio >= 1) || t.min_storage < 0))
            err.set(tc + ": a value is out of range");
    }
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
    if (const json* rb = member(rj, "roundabout")) {
        RoundaboutRules& r = pack.roundabout;
        const std::string c = "region.roundabout";
        r.entry_corner_radius = opt_number(*rb, "entry_corner_radius_m", r.entry_corner_radius, c, err);
        r.exit_corner_radius = opt_number(*rb, "exit_corner_radius_m", r.exit_corner_radius, c, err);
        r.ring_corner_radius_factor = opt_number(*rb, "ring_corner_radius_factor", r.ring_corner_radius_factor, c, err);
        r.max_arc = opt_number(*rb, "max_arc_rad", r.max_arc, c, err);
        r.max_ring_edges = static_cast<int>(opt_number(*rb, "max_ring_edges", r.max_ring_edges, c, err));
        r.min_island_radius = opt_number(*rb, "min_island_radius_m", r.min_island_radius, c, err);
        r.min_edge_length = opt_number(*rb, "min_edge_length_m", r.min_edge_length, c, err);
        r.min_tool_radius = opt_number(*rb, "min_tool_radius_m", r.min_tool_radius, c, err);
        r.max_tool_radius = opt_number(*rb, "max_tool_radius_m", r.max_tool_radius, c, err);
        r.splitter_length = opt_number(*rb, "splitter_length_m", r.splitter_length, c, err);
        r.splitter_width = opt_number(*rb, "splitter_width_m", r.splitter_width, c, err);
        if (member(*rb, "central_island_surface")) r.central_island_surface = get_string(*rb, "central_island_surface", c, err);
        if (member(*rb, "splitter_surface")) r.splitter_surface = get_string(*rb, "splitter_surface", c, err);
        r.island_kerb_upstand = opt_number(*rb, "island_kerb_upstand_m", r.island_kerb_upstand, c, err);
        if (r.island_kerb_upstand < 0) err.set(c + ": island_kerb_upstand_m must not be negative");
        if (!err.failed() && (!(r.max_arc > 0.1) || !(r.min_tool_radius > 0) || r.max_tool_radius < r.min_tool_radius ||
                              r.max_ring_edges < 4 || r.min_edge_length < 0 || r.min_island_radius < 0 ||
                              r.entry_corner_radius < 0 || r.exit_corner_radius < 0 || r.ring_corner_radius_factor < 0))
            err.set(c + ": a value is out of range");
        if (!err.failed()) parse_roundabout_extras(*rb, r, err);
    }
    if (const json* rr = member(rj, "render")) {
        RenderRules& r = pack.render;
        const std::string c = "region.render";
        r.crossfall = opt_number(*rr, "crossfall", r.crossfall, c, err);
        r.kerb_upstand = opt_number(*rr, "kerb_upstand_m", r.kerb_upstand, c, err);
        r.footway_crossfall = opt_number(*rr, "footway_crossfall", r.footway_crossfall, c, err);
        r.verge_crossfall = opt_number(*rr, "verge_crossfall", r.verge_crossfall, c, err);
        if (!err.failed() && !(r.crossfall >= 0 && r.crossfall <= 0.2 && r.kerb_upstand >= 0 && r.kerb_upstand <= 0.5 &&
                               r.footway_crossfall >= 0 && r.footway_crossfall <= 0.2 && r.verge_crossfall >= 0 &&
                               r.verge_crossfall <= 0.5))
            err.set(c + ": a value is out of range");
    }
    if (const json* jr = member(rj, "junction")) parse_junction_rules(*jr, pack.junction, err);
    if (const json* gr = member(rj, "grading")) {
        GradingRules& g = pack.grading;
        const std::string c = "region.grading";
        g.formation_drop = opt_number(*gr, "formation_drop_m", g.formation_drop, c, err);
        g.batter = opt_number(*gr, "batter", g.batter, c, err);
        g.verge = opt_number(*gr, "verge_m", g.verge, c, err);
        g.wall_thickness = opt_number(*gr, "wall_thickness_m", g.wall_thickness, c, err);
        g.wall_start = opt_number(*gr, "wall_start_m", g.wall_start, c, err);
        g.wall_cap = opt_number(*gr, "wall_cap_m", g.wall_cap, c, err);
        g.wall_slack = opt_number(*gr, "wall_slack_m", g.wall_slack, c, err);
        g.tunnel_clear_height = opt_number(*gr, "tunnel_clear_height_m", g.tunnel_clear_height, c, err);
        g.wall_max_height = opt_number(*gr, "wall_max_height_m", g.wall_max_height, c, err);
        g.bench_min_width = opt_number(*gr, "bench_min_width_m", g.bench_min_width, c, err);
        g.wall_min_length = opt_number(*gr, "wall_min_length_m", g.wall_min_length, c, err);
        g.toe_ditch_depth = opt_number(*gr, "toe_ditch_depth_m", g.toe_ditch_depth, c, err);
        g.toe_ditch_width = opt_number(*gr, "toe_ditch_width_m", g.toe_ditch_width, c, err);
        g.end_taper_batter = opt_number(*gr, "end_taper_batter", g.end_taper_batter, c, err);
        g.wing_max = opt_number(*gr, "wing_max_m", g.wing_max, c, err);
        g.wing_ratio = opt_number(*gr, "wing_ratio", g.wing_ratio, c, err);
        g.wing_min_height = opt_number(*gr, "wing_min_height_m", g.wing_min_height, c, err);
        g.round_daylight = opt_number(*gr, "round_daylight_m", g.round_daylight, c, err);
        g.round_hinge = opt_number(*gr, "round_hinge_m", g.round_hinge, c, err);
        if (!err.failed() && (g.wall_max_height < 0.3 || g.bench_min_width < 0 || g.wall_min_length < 0 || g.toe_ditch_depth < 0 ||
                              g.toe_ditch_width < 0 || g.end_taper_batter < 0.1 || g.wing_max < 0 || g.wing_ratio < 0 ||
                              g.wing_min_height < 0 || g.round_daylight < 0 || g.round_hinge < 0))
            err.set(c + ": a value is out of range");
        if (!err.failed() && (!(g.batter > 0.1) || g.formation_drop < 0 || g.verge < 0 || !(g.wall_thickness > 0) ||
                              !(g.wall_start > 0) || !(g.wall_cap > 0) || g.wall_slack < 0 || !(g.tunnel_clear_height > 0)))
            err.set(c + ": a value is out of range");
    }
    if (const json* sr = member(rj, "structures")) {
        StructureRules& s = pack.structures;
        const std::string c = "region.structures";
        s.parapet_height = opt_number(*sr, "parapet_height_m", s.parapet_height, c, err);
        s.max_pier_spacing = opt_number(*sr, "max_pier_spacing_m", s.max_pier_spacing, c, err);
        s.min_bridge_length = opt_number(*sr, "min_bridge_length_m", s.min_bridge_length, c, err);
        s.pier_road_clearance = opt_number(*sr, "pier_road_clearance_m", s.pier_road_clearance, c, err);
        s.wall_panel = opt_number(*sr, "wall_panel_m", s.wall_panel, c, err);
        s.tunnel_rise_k = opt_number(*sr, "tunnel_rise_k", s.tunnel_rise_k, c, err);
        s.coping_thickness = opt_number(*sr, "coping_thickness_m", s.coping_thickness, c, err);
        s.coping_overhang = opt_number(*sr, "coping_overhang_m", s.coping_overhang, c, err);
        s.wall_face_thickness = opt_number(*sr, "wall_face_thickness_m", s.wall_face_thickness, c, err);
        s.wall_joint_width = opt_number(*sr, "wall_joint_width_m", s.wall_joint_width, c, err);
        s.wall_joint_proud = opt_number(*sr, "wall_joint_proud_m", s.wall_joint_proud, c, err);
        if (const json* v = member(*sr, "wall_allow_short_panels")) { if (v->is_boolean()) s.wall_allow_short_panels = v->get<bool>(); else err.set(c + ".wall_allow_short_panels: expected a boolean"); }
        if (!err.failed() && (s.wall_face_thickness <= 0 || s.wall_joint_width < 0 || s.wall_joint_proud < 0)) err.set(c + ": a value is out of range");
        if (!err.failed() && (!(s.parapet_height > 0) || !(s.max_pier_spacing >= 4) || s.min_bridge_length < 0 || s.pier_road_clearance < 0 ||
                              !(s.wall_panel >= 1) || !(s.tunnel_rise_k > 0.02 && s.tunnel_rise_k < 2) || !(s.coping_thickness > 0) ||
                              s.coping_overhang < 0))
            err.set(c + ": a value is out of range");
    }

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

const LaneType* RegionPack::lane_type(const std::string& key) const {
    auto it = lane_types.find(key);
    return it == lane_types.end() ? nullptr : &it->second;
}

const RoadType* RegionPack::road_type(const std::string& key) const {
    auto it = road_types.find(key);
    return it == road_types.end() ? nullptr : &it->second;
}

RoundaboutSizeClass RoundaboutRules::size_class(double ring_radius) const {
    for (const RoundaboutSizeClass& k : size_classes)
        if (ring_radius <= k.max_radius) return k;
    if (!size_classes.empty()) return size_classes.back();
    RoundaboutSizeClass k;
    k.entry_corner_radius = entry_corner_radius;
    k.exit_corner_radius = exit_corner_radius;
    k.splitter_length = splitter_length;
    k.splitter_width = splitter_width;
    return k;
}

double RoundaboutRules::splitter_speed_factor(double speed_limit) const {
    for (const SpeedFactor& f : splitter_speed_factors)
        if (speed_limit <= f.max_speed + 1e-9) return f.factor;
    return splitter_speed_factors.empty() ? 1.0 : splitter_speed_factors.back().factor;
}

double TurnPocketRules::lookup(const std::vector<SpeedFactor>& table, double speed_limit, double fallback) const {
    for (const SpeedFactor& f : table)
        if (speed_limit <= f.max_speed + 1e-9) return f.factor;
    return table.empty() ? fallback : table.back().factor;
}

double MergeRules::taper_length_for(double design_speed) const {
    for (const SpeedFactor& f : taper_length)
        if (design_speed <= f.max_speed + 1e-9) return f.factor;
    return taper_length.empty() ? default_taper_length : taper_length.back().factor;
}

double JunctionRules::corner_radius_for(int class_a, int class_b) const {
    const int minor = std::min(class_a, class_b), major = std::max(class_a, class_b);
    for (const CornerRadiusRule& r : corner_radius)
        if (minor >= r.minor_min && minor <= r.minor_max && major >= r.major_min && major <= r.major_max) return r.radius;
    return 0;
}

double JunctionRules::skew_factor(double angle) const {
    return std::clamp(angle / (3.14159265358979323846 / 2), skew_min, skew_max);
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
