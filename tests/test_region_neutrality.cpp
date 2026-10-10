// Region neutrality: runs the pipeline on a deliberately different SYNTHETIC region pack (data/regions/testland:
// right-hand traffic, km/h, other widths / radii / marking rules, no road type id shared with the UK pack) and checks
// that outputs reflect TESTLAND's numbers. A UK value hard-coded in sim_core shows up here as a mismatch.
//
// Test cases marked should_fail() document a KNOWN leak of a UK assumption into the core; the comment above each
// names the file:line. Fix the core, then delete the decorator.
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <ostream>
#include <set>
#include <string>
#include <vector>

#include "doctest/doctest.h"
#include "nlohmann/json.hpp"
#include "cp/junction.h"
#include "cp/junction_roundabout.h"
#include "cp/lanes.h"
#include "cp/layout_io.h"
#include "cp/markings.h"
#include "cp/render_mesh.h"
#include "cp/road_commands.h"
#include "cp/street_lights.h"

using namespace cp;
using json = nlohmann::json;

namespace {

constexpr double kDeg = kPi / 180;
constexpr double kKph = 1.0 / 3.6;

// absolute-tolerance comparison: `x == Near{value, tol}`
struct Near {
    double v, m;
};
bool operator==(double a, const Near& n) { return std::fabs(a - n.v) <= n.m; }
std::ostream& operator<<(std::ostream& o, const Near& n) { return o << n.v << " +-" << n.m; }

// ------------------------------------------------------------------ data access

json read_json(const std::string& file, const std::string& region = "testland") {
    const std::filesystem::path p = std::filesystem::path(CP_DATA_DIR) / "regions" / region / file;
    std::ifstream in(p, std::ios::binary);
    REQUIRE_MESSAGE(in.good(), "cannot open " << p.string());
    json j = json::parse(in, nullptr, /*allow_exceptions=*/false);
    REQUIRE_MESSAGE(!j.is_discarded(), p.string() << " is not valid JSON");
    return j;
}

const RegionPack& base_pack() {
    static const RegionPack pack = [] {
        auto p = load_region_pack_by_id(CP_DATA_DIR, "testland");
        REQUIRE_MESSAGE(p, p.error);
        return *p;
    }();
    return pack;
}

// testland with the driving side forced (the pack itself says right; Left gives the otherwise identical mirror copy).
std::shared_ptr<const RegionPack> tl(DrivingSide side = DrivingSide::Right) {
    static std::shared_ptr<const RegionPack> cache[2];
    auto& slot = cache[side == DrivingSide::Left ? 0 : 1];
    if (!slot) {
        RegionPack p = base_pack();
        p.driving_side = side;
        slot = std::make_shared<const RegionPack>(std::move(p));
    }
    return slot;
}

const MarkingRules& rules() {
    static const MarkingRules r = [] {
        auto m = load_marking_rules_by_id(CP_DATA_DIR, "testland");
        REQUIRE_MESSAGE(m, m.error);
        return *m;
    }();
    return r;
}

// ------------------------------------------------------------------ network builder

struct Net {
    std::shared_ptr<const RegionPack> region;
    RoadGraph g;
    CommandStack<RoadGraph> st{g};
    NodeId centre;

    explicit Net(DrivingSide side = DrivingSide::Right) : region(tl(side)) { g.set_region(region); }

    EdgeId last_edge() const { return g.edges().rbegin()->first; }

    EdgeId road(Vec3 a, Vec3 b, const std::string& type, bool one_way = false) {
        DrawRoadSpec s;
        s.road_type = type;
        s.one_way = one_way;
        s.start.pos = a;
        s.end.pos = b;
        REQUIRE(st.execute(make_draw_road(s)));
        return last_edge();
    }

    // A road between the centre node (created at the origin on first use) and a point `len` m away at bearing `deg`.
    // inbound (default): it ENDS at the centre.
    EdgeId spoke(double deg, double len, const std::string& type, bool one_way = false, bool inbound = true) {
        DrawRoadSpec s;
        s.road_type = type;
        s.one_way = one_way;
        RoadEnd far, c;
        far.pos = {len * std::cos(deg * kDeg), len * std::sin(deg * kDeg), 0};
        c.pos = {0, 0, 0};
        c.existing_node = centre;
        s.start = inbound ? far : c;
        s.end = inbound ? c : far;
        REQUIRE(st.execute(make_draw_road(s)));
        if (!centre.valid()) centre = inbound ? g.edge(last_edge())->b : g.edge(last_edge())->a;
        return last_edge();
    }

    bool place(double radius, const std::string& ring = "circus_ring") {
        return st.execute(make_place_roundabout({0, 0, 0}, radius, ring, *region));
    }

    JunctionSet junctions(RoundaboutResult* rr = nullptr) const {
        JunctionSet js = build_plain_junctions(g, *region, {});
        RoundaboutResult r = apply_roundabouts(js, g, *region, {});
        if (rr) *rr = std::move(r);
        return js;
    }
};

// ------------------------------------------------------------------ geometry helpers

double area(const std::vector<Vec2>& r) {
    double a = 0;
    for (std::size_t i = 0; i < r.size(); ++i) a += r[i].cross(r[(i + 1) % r.size()]);
    return a / 2;
}

double set_area(const PolygonSet& s) {
    double a = 0;
    for (const auto& r : s) a += area(r);  // outers CCW (+), holes CW (-)
    return a;
}

struct Box {
    double x0 = 1e18, x1 = -1e18, y0 = 1e18, y1 = -1e18;
    double dx() const { return x1 - x0; }
    double dy() const { return y1 - y0; }
    double cx() const { return (x0 + x1) / 2; }
    double cy() const { return (y0 + y1) / 2; }
};

Box box_of(const MarkingSet& s, const Marking& m) {
    Box b;
    for (const Vec3& p : s.points(m)) {
        b.x0 = std::min(b.x0, p.x), b.x1 = std::max(b.x1, p.x);
        b.y0 = std::min(b.y0, p.y), b.y1 = std::max(b.y1, p.y);
    }
    return b;
}

Vec2 centroid(const MarkingSet& s, const Marking& m) {
    Vec2 c{0, 0};
    for (const Vec3& p : s.points(m)) c = c + p.xy();
    return c * (1.0 / m.count);
}

double mark_area(const MarkingSet& s, const Marking& m) {
    const auto p = s.points(m);
    double a = 0;
    for (std::size_t i = 0; i < p.size(); ++i) a += p[i].xy().cross(p[(i + 1) % p.size()].xy());
    return std::fabs(a / 2);
}

std::vector<const Marking*> pick(const MarkingSet& s, MarkingCategory c, const std::string& style = "") {
    std::vector<const Marking*> out;
    for (const Marking& m : s.marks)
        if (m.category == c && (style.empty() || s.style_id(m) == style)) out.push_back(&m);
    return out;
}

// Marks of one straight line along +x, sorted by x: every interior (full) mark has length `mark`, consecutive full marks
// are `gap` apart, and every mark is `width` wide. At least `min_full` full marks must exist.
void check_dashes(const MarkingSet& s, std::vector<const Marking*> marks, double mark, double gap, double width,
                  std::size_t min_full = 8) {
    REQUIRE(!marks.empty());
    std::sort(marks.begin(), marks.end(), [&](const Marking* a, const Marking* b) { return box_of(s, *a).x0 < box_of(s, *b).x0; });
    std::size_t full = 0;
    for (std::size_t i = 0; i < marks.size(); ++i) {
        const Box b = box_of(s, *marks[i]);
        CHECK(b.dy() == doctest::Approx(width).epsilon(1e-6));
        if (std::fabs(b.dx() - mark) < 1e-6) ++full;
        else CHECK(b.dx() < mark + 1e-6);  // end partials are shorter, never longer
        if (i > 0) {
            const Box p = box_of(s, *marks[i - 1]);
            if (std::fabs(b.dx() - mark) < 1e-6 && std::fabs(p.dx() - mark) < 1e-6) CHECK(b.x0 - p.x1 == doctest::Approx(gap).epsilon(1e-6));
        }
    }
    CHECK(full >= min_full);
}

void check_continuous(const MarkingSet& s, const std::vector<const Marking*>& marks, double width, double y, double tol = 1e-6) {
    REQUIRE(!marks.empty());
    for (const Marking* m : marks) {
        const Box b = box_of(s, *m);
        CHECK(b.dy() == doctest::Approx(width).epsilon(1e-6));
        CHECK(b.cy() == doctest::Approx(y).epsilon(tol));
    }
}

double lane_total(const RoadType& t, bool one_way) {
    const auto& slots = t.dual ? t.carriageway : (t.two_way && !one_way ? t.half : t.one_way_variant);
    double w = 0;
    for (const CrossSectionSlot& s : slots) w += s.width;
    return t.two_way && !one_way && !t.dual ? 2 * w : w;
}

}  // namespace

// ============================================================================================ 1. the pack itself

TEST_CASE("testland: the pack loads without errors and every field round-trips to the JSON") {
    const RegionPack& p = base_pack();
    const json rj = read_json("region.json"), tj = read_json("road_types.json");

    CHECK(p.id == rj["id"].get<std::string>());
    CHECK(p.id == "testland");
    CHECK(p.version == rj["version"].get<int>());
    CHECK(p.driving_side == DrivingSide::Right);
    CHECK(p.headroom_m == doctest::Approx(rj["headroom_m"].get<double>()));
    CHECK(p.plan_crossing_junction_dz_m == doctest::Approx(rj["plan_crossing_junction_dz_m"].get<double>()));

    const json& gr = rj["grading"];
    CHECK(p.grading.formation_drop == doctest::Approx(gr["formation_drop_m"].get<double>()));
    CHECK(p.grading.batter == doctest::Approx(gr["batter"].get<double>()));
    CHECK(p.grading.verge == doctest::Approx(gr["verge_m"].get<double>()));
    CHECK(p.grading.wall_thickness == doctest::Approx(gr["wall_thickness_m"].get<double>()));
    CHECK(p.grading.wall_start == doctest::Approx(gr["wall_start_m"].get<double>()));
    CHECK(p.grading.wall_cap == doctest::Approx(gr["wall_cap_m"].get<double>()));
    CHECK(p.grading.wall_slack == doctest::Approx(gr["wall_slack_m"].get<double>()));
    CHECK(p.grading.tunnel_clear_height == doctest::Approx(gr["tunnel_clear_height_m"].get<double>()));

    const json& sr = rj["structures"];
    CHECK(p.structures.parapet_height == doctest::Approx(sr["parapet_height_m"].get<double>()));
    CHECK(p.structures.max_pier_spacing == doctest::Approx(sr["max_pier_spacing_m"].get<double>()));
    CHECK(p.structures.min_bridge_length == doctest::Approx(sr["min_bridge_length_m"].get<double>()));
    CHECK(p.structures.pier_road_clearance == doctest::Approx(sr["pier_road_clearance_m"].get<double>()));
    CHECK(p.structures.wall_panel == doctest::Approx(sr["wall_panel_m"].get<double>()));
    CHECK(p.structures.tunnel_rise_k == doctest::Approx(sr["tunnel_rise_k"].get<double>()));
    CHECK(p.structures.coping_thickness == doctest::Approx(sr["coping_thickness_m"].get<double>()));
    CHECK(p.structures.coping_overhang == doctest::Approx(sr["coping_overhang_m"].get<double>()));

    const json& rb = rj["roundabout"];
    const RoundaboutRules& r = p.roundabout;
    CHECK(r.entry_corner_radius == doctest::Approx(rb["entry_corner_radius_m"].get<double>()));
    CHECK(r.exit_corner_radius == doctest::Approx(rb["exit_corner_radius_m"].get<double>()));
    CHECK(r.ring_corner_radius_factor == doctest::Approx(rb["ring_corner_radius_factor"].get<double>()));
    CHECK(r.max_arc == doctest::Approx(rb["max_arc_rad"].get<double>()));
    CHECK(r.max_ring_edges == rb["max_ring_edges"].get<int>());
    CHECK(r.min_island_radius == doctest::Approx(rb["min_island_radius_m"].get<double>()));
    CHECK(r.min_edge_length == doctest::Approx(rb["min_edge_length_m"].get<double>()));
    CHECK(r.min_tool_radius == doctest::Approx(rb["min_tool_radius_m"].get<double>()));
    CHECK(r.max_tool_radius == doctest::Approx(rb["max_tool_radius_m"].get<double>()));
    CHECK(r.splitter_length == doctest::Approx(rb["splitter_length_m"].get<double>()));
    CHECK(r.splitter_width == doctest::Approx(rb["splitter_width_m"].get<double>()));
    CHECK(r.central_island_surface == rb["central_island_surface"].get<std::string>());
    CHECK(r.splitter_surface == rb["splitter_surface"].get<std::string>());
    CHECK(r.island_kerb_upstand == doctest::Approx(rb["island_kerb_upstand_m"].get<double>()));
    CHECK(r.apron_island_radius_max == doctest::Approx(rb["apron_island_radius_max_m"].get<double>()));
    CHECK(r.apron_surface == rb["apron_surface"].get<std::string>());
    const json& sc = rb["size_classes"];
    REQUIRE(r.size_classes.size() == sc.size());
    for (std::size_t i = 0; i < sc.size(); ++i) {
        const RoundaboutSizeClass& k = r.size_classes[i];
        CHECK(k.entry_corner_radius == doctest::Approx(sc[i]["entry_corner_radius_m"].get<double>()));
        CHECK(k.exit_corner_radius == doctest::Approx(sc[i]["exit_corner_radius_m"].get<double>()));
        CHECK(k.splitter_length == doctest::Approx(sc[i]["splitter_length_m"].get<double>()));
        CHECK(k.splitter_width == doctest::Approx(sc[i]["splitter_width_m"].get<double>()));
        CHECK(k.apron_width == doctest::Approx(sc[i].value("apron_width_m", 0.0)));
        if (sc[i].contains("max_radius_m")) CHECK(k.max_radius == doctest::Approx(sc[i]["max_radius_m"].get<double>()));
        else CHECK(k.max_radius > 1e9);
    }
    const json& sf = rb["splitter_speed_factors"];
    REQUIRE(r.splitter_speed_factors.size() == sf.size());
    for (std::size_t i = 0; i < sf.size(); ++i) {
        CHECK(r.splitter_speed_factors[i].factor == doctest::Approx(sf[i]["factor"].get<double>()));
        if (sf[i].contains("max_speed_kph")) CHECK(r.splitter_speed_factors[i].max_speed == doctest::Approx(sf[i]["max_speed_kph"].get<double>() * kKph));
        else CHECK(r.splitter_speed_factors[i].max_speed > 1e9);
    }

    const json& jn = rj["junction"];
    const json& cr = jn["corner_radius_by_class"];
    REQUIRE(p.junction.corner_radius.size() == cr.size());
    for (std::size_t i = 0; i < cr.size(); ++i) {
        const CornerRadiusRule& k = p.junction.corner_radius[i];
        CHECK(k.radius == doctest::Approx(cr[i]["radius_m"].get<double>()));
        CHECK(k.minor_max == cr[i].value("minor_max", 1 << 30));
        CHECK(k.major_max == cr[i].value("major_max", 1 << 30));
        CHECK(k.minor_min == cr[i].value("minor_min", 0));
        CHECK(k.major_min == cr[i].value("major_min", 0));
    }
    CHECK(p.junction.skew_min == doctest::Approx(jn["skew_factor"]["min"].get<double>()));
    CHECK(p.junction.skew_max == doctest::Approx(jn["skew_factor"]["max"].get<double>()));
    const json& tp = jn["turn_pockets"];
    const TurnPocketRules& t = p.junction.turn_pockets;
    CHECK(t.enabled == tp["enabled"].get<bool>());
    CHECK(t.major_class_min == tp["major_class_min"].get<int>());
    CHECK(t.minor_class_max == tp["minor_class_max"].get<int>());
    CHECK(t.lane_width == doctest::Approx(tp["lane_width_m"].get<double>()));
    CHECK(t.max_bend == doctest::Approx(tp["max_bend_rad"].get<double>()));
    CHECK(t.min_minor_angle == doctest::Approx(tp["min_minor_angle_rad"].get<double>()));
    CHECK(t.max_minor_angle == doctest::Approx(tp["max_minor_angle_rad"].get<double>()));
    CHECK(t.min_taper_ratio == doctest::Approx(tp["min_taper_ratio"].get<double>()));
    CHECK(t.min_storage == doctest::Approx(tp["min_storage_m"].get<double>()));
    REQUIRE(t.taper_ratio.size() == tp["taper_ratio"].size());
    REQUIRE(t.storage.size() == tp["storage"].size());
    for (std::size_t i = 0; i < t.taper_ratio.size(); ++i) CHECK(t.taper_ratio[i].factor == doctest::Approx(tp["taper_ratio"][i]["ratio"].get<double>()));
    for (std::size_t i = 0; i < t.storage.size(); ++i) CHECK(t.storage[i].factor == doctest::Approx(tp["storage"][i]["length_m"].get<double>()));

    // lane types are shared by all regions
    CHECK(p.lane_types.size() == read_json("lane_types.json", "..")["lane_types"].size());

    // road types
    const json& types = tj["road_types"];
    REQUIRE(p.road_types.size() == types.size());
    const auto check_slots = [](const json& arr, const std::vector<CrossSectionSlot>& slots, const std::string& what) {
        INFO(what);
        REQUIRE(slots.size() == arr.size());
        for (std::size_t i = 0; i < arr.size(); ++i) {
            CHECK(slots[i].lane_type == arr[i]["lane"].get<std::string>());
            CHECK(slots[i].width == doctest::Approx(arr[i]["width_m"].get<double>()));
        }
    };
    for (const json& e : types) {
        const std::string id = e["id"].get<std::string>();
        INFO("road type " << id);
        const RoadType* rt = p.road_type(id);
        REQUIRE(rt);
        CHECK(rt->name == e["name"].get<std::string>());
        CHECK(rt->two_way == e.value("two_way", false));
        CHECK(rt->dual == e.value("dual", false));
        CHECK(rt->roundabout_ring == e.value("roundabout_ring", false));
        CHECK(rt->speed_limit == doctest::Approx(e["speed_limit"].get<double>() * kKph));  // km/h, not mph
        CHECK(rt->design_speed == doctest::Approx(e["design_speed_kph"].get<double>() * kKph));
        CHECK(rt->road_class == e["road_class"].get<int>());
        CHECK(rt->corner_radius == doctest::Approx(e["corner_radius_m"].get<double>()));
        CHECK(rt->median_gap == doctest::Approx(e.value("median_gap_m", 0.0)));
        if (e.contains("half")) check_slots(e["half"], rt->half, "half");
        else CHECK(rt->half.empty());
        if (e.contains("one_way_variant")) check_slots(e["one_way_variant"], rt->one_way_variant, "one_way_variant");
        if (e.contains("carriageway")) check_slots(e["carriageway"], rt->carriageway, "carriageway");
        CHECK(rt->vertical_k_crest == doctest::Approx(e["vertical_k"]["crest"].get<double>()));
        CHECK(rt->vertical_k_sag == doctest::Approx(e["vertical_k"]["sag"].get<double>()));
        CHECK(rt->min_radius == doctest::Approx(e["min_radius_m"].get<double>()));
        CHECK(rt->advisory_max_grade == doctest::Approx(e["advisory_max_grade_pct"].get<double>() / 100));
        CHECK(rt->markings == e["markings"].get<std::string>());
        CHECK(rt->crossfall.has_value() == e.contains("crossfall"));
        if (e.contains("crossfall") && rt->crossfall) CHECK(*rt->crossfall == doctest::Approx(e["crossfall"].get<double>()));
        CHECK(rt->kerb_upstand.has_value() == e.contains("kerb_upstand_m"));
        if (e.contains("kerb_upstand_m") && rt->kerb_upstand) CHECK(*rt->kerb_upstand == doctest::Approx(e["kerb_upstand_m"].get<double>()));
        CHECK(rt->verge_crossfall.has_value() == e.contains("verge_crossfall"));
        if (e.contains("verge_crossfall") && rt->verge_crossfall) CHECK(*rt->verge_crossfall == doctest::Approx(e["verge_crossfall"].get<double>()));
        const json& sl = e["street_lights"];
        const StreetLightRules& L = rt->street_lights;
        CHECK(L.lit == sl.value("lit", true));
        if (sl.value("lit", true)) {
            CHECK(L.spacing == doctest::Approx(sl["spacing_m"].get<double>()));
            CHECK(L.setback == doctest::Approx(sl["setback_m"].get<double>()));
            CHECK(L.footway_inset == doctest::Approx(sl.value("footway_inset_m", 0.3)));
            CHECK(L.mounting_height == doctest::Approx(sl["mounting_height_m"].get<double>()));
            CHECK(L.outreach == doctest::Approx(sl["outreach_m"].get<double>()));
            CHECK(L.style == sl["style"].get<std::string>());
            CHECK(L.island_min_radius == doctest::Approx(sl.value("island_min_radius_m", 0.0)));
            const std::string a = sl["arrangement"].get<std::string>();
            const LightArrangement want = a == "single_side" ? LightArrangement::SingleSide
                                          : a == "staggered" ? LightArrangement::Staggered
                                          : a == "opposite"  ? LightArrangement::Opposite
                                                             : LightArrangement::CentralReservation;
            CHECK(L.arrangement == want);
        }
    }
    // exactly one ring type, and it is the flagged one
    CHECK(default_ring_road_type(p) == "circus_ring");
}

TEST_CASE("testland: no id, set or style name is shared with the UK pack") {
    const RegionPack& tp = base_pack();
    auto uk = load_region_pack_by_id(CP_DATA_DIR, "uk");
    REQUIRE_MESSAGE(uk, uk.error);
    for (const auto& [id, rt] : tp.road_types) CHECK_MESSAGE(uk->road_type(id) == nullptr, "shared road type id " << id);
    auto ukr = load_marking_rules_by_id(CP_DATA_DIR, "uk");
    REQUIRE_MESSAGE(ukr, ukr.error);
    for (const auto& [id, s] : rules().styles) CHECK_MESSAGE(!ukr->styles.count(id), "shared style id " << id);
    for (const auto& [id, s] : rules().sets) CHECK_MESSAGE(!ukr->sets.count(id), "shared marking set id " << id);
    for (const auto& [id, c] : rules().colours) CHECK_MESSAGE(!ukr->colours.count(id), "shared colour id " << id);
    for (const auto& [id, rt] : tp.road_types)
        if (rt.markings.size()) CHECK_MESSAGE(rules().set(rt.markings), "road type " << id << " names a missing marking set");
    CHECK(uk->driving_side != tp.driving_side);
}

TEST_CASE("testland: marking rules load and every style, colour and rule number round-trips to the JSON") {
    const MarkingRules& m = rules();
    const json j = read_json("marking_rules.json");
    CHECK(m.version == j["version"].get<int>());
    CHECK(m.speed_unit == "kph");
    CHECK(m.min_mark_m == doctest::Approx(j["min_mark_m"].get<double>()));
    CHECK(m.shoulder_lane_types == std::vector<std::string>{"hard_strip"});
    CHECK(m.wear.strength == doctest::Approx(j["wear"]["strength"].get<double>()));
    CHECK(m.wear.patch_period_m == doctest::Approx(j["wear"]["patch_period_m"].get<double>()));
    CHECK(m.wear.patch_weight == doctest::Approx(j["wear"]["patch_weight"].get<double>()));
    CHECK(m.wear.wheel_boost == doctest::Approx(j["wear"]["wheel_boost"].get<double>()));
    CHECK(m.wear.wheel_offset_m == doctest::Approx(j["wear"]["wheel_offset_m"].get<double>()));
    CHECK(m.wear.wheel_half_width_m == doctest::Approx(j["wear"]["wheel_half_width_m"].get<double>()));
    for (auto it = j["colours"].begin(); it != j["colours"].end(); ++it) {
        REQUIRE(m.colours.count(it.key()));
        for (int i = 0; i < 3; ++i) CHECK(m.colours.at(it.key()).rgb[i] == doctest::Approx(it.value()["rgb"][static_cast<std::size_t>(i)].get<double>()));
    }
    std::size_t nstyles = 0;
    for (auto it = j["styles"].begin(); it != j["styles"].end(); ++it, ++nstyles) {
        INFO("style " << it.key());
        REQUIRE(m.styles.count(it.key()));
        const LineStyle& s = m.styles.at(it.key());
        CHECK(s.colour == it.value()["colour"].get<std::string>());
        CHECK(s.fill == it.value().value("fill", false));
        if (!s.fill) {
            const json& st = it.value()["strands"];
            REQUIRE(s.strands.size() == st.size());
            for (std::size_t i = 0; i < st.size(); ++i) {
                CHECK(s.strands[i].width == doctest::Approx(st[i]["width_m"].get<double>()));
                CHECK(s.strands[i].mark == doctest::Approx(st[i]["mark_m"].get<double>()));
                CHECK(s.strands[i].gap == doctest::Approx(st[i]["gap_m"].get<double>()));
                CHECK(s.strands[i].offset == doctest::Approx(st[i].value("offset_m", 0.0)));
            }
        }
    }
    CHECK(m.styles.size() == nstyles);
    CHECK(m.sets.size() == j["sets"].size());
    for (auto it = j["sets"].begin(); it != j["sets"].end(); ++it) {
        REQUIRE(m.set(it.key()));
        CHECK(m.set(it.key())->boundaries.size() == it.value()["boundaries"].size());
        CHECK(m.set(it.key())->bays.size() == it.value()["bays"].size());
    }
    // the bay rule
    REQUIRE(m.set("town")->bays.size() == 1);
    const BayRule& bay = m.set("town")->bays[0];
    CHECK(bay.keepout_m == doctest::Approx(8.0));
    CHECK(bay.edge_clear_m == doctest::Approx(0.08));
    // junction rules
    const json& jn = j["junction"];
    CHECK(m.give_way.setback_m == doctest::Approx(jn["give_way"]["setback_m"].get<double>()));
    CHECK(m.give_way.line_gap_m == doctest::Approx(jn["give_way"]["line_gap_m"].get<double>()));
    CHECK(m.give_way.equal_class_max_deviation_deg == doctest::Approx(jn["give_way"]["equal_class_max_deviation_deg"].get<double>()));
    CHECK_FALSE(m.give_way.triangle);  // testland paints no give-way triangle
    CHECK(m.stop_line.setback_m == doctest::Approx(jn["stop_line"]["setback_m"].get<double>()));
    CHECK(m.zebra.stripe_width_m == doctest::Approx(jn["zebra"]["stripe_width_m"].get<double>()));
    CHECK(m.zebra.gap_m == doctest::Approx(jn["zebra"]["gap_m"].get<double>()));
    CHECK(m.zebra.band_width_m == doctest::Approx(jn["zebra"]["band_width_m"].get<double>()));
    CHECK(m.zebra.clear_m == doctest::Approx(jn["zebra"]["clear_m"].get<double>()));
    CHECK(m.gore.same_direction == "diagonal");
    CHECK(m.gore.opposing == "chevron");
    CHECK(m.gore.chevron.angle_deg == doctest::Approx(40));
    CHECK(m.arrows.distances_m == std::vector<double>{7.0, 19.0, 33.0});
    CHECK(m.arrows.width_m == doctest::Approx(0.9));
    CHECK(m.arrows.glyphs.count("ahead"));
    CHECK(m.arrows.glyphs.count("fork"));
    CHECK(m.splitter.hatch_length_m == doctest::Approx(14));
    CHECK(m.splitter.outline_gap_m == doctest::Approx(0.2));
}

TEST_CASE("testland: JunctionRules and RoundaboutRules lookups use testland's tables") {
    const RegionPack& p = base_pack();
    const JunctionRules& jr = p.junction;
    // classes: farm_track 25, local 45, collector 65, ring 70, arterial 80, rural_trunk 95, expressway_dual 110
    CHECK(jr.corner_radius_for(25, 25) == doctest::Approx(3.5));
    CHECK(jr.corner_radius_for(25, 45) == doctest::Approx(3.5));
    CHECK(jr.corner_radius_for(25, 80) == doctest::Approx(5.5));
    CHECK(jr.corner_radius_for(25, 95) == doctest::Approx(9));
    CHECK(jr.corner_radius_for(45, 45) == doctest::Approx(5));
    CHECK(jr.corner_radius_for(45, 65) == doctest::Approx(8));
    CHECK(jr.corner_radius_for(65, 45) == doctest::Approx(8));  // order does not matter
    CHECK(jr.corner_radius_for(45, 95) == doctest::Approx(11.5));
    CHECK(jr.corner_radius_for(65, 65) == doctest::Approx(9.5));
    CHECK(jr.corner_radius_for(65, 80) == doctest::Approx(9.5));
    CHECK(jr.corner_radius_for(65, 95) == doctest::Approx(13));
    CHECK(jr.corner_radius_for(80, 110) == doctest::Approx(13));
    CHECK(jr.corner_radius_for(110, 110) == doctest::Approx(17));
    CHECK(jr.skew_factor(kPi / 4) == doctest::Approx(0.7));       // 0.5 clamps to the testland minimum
    CHECK(jr.skew_factor(kPi / 2) == doctest::Approx(1.0));
    CHECK(jr.skew_factor(kPi * 0.9) == doctest::Approx(1.35));    // 1.8 clamps to the testland maximum

    const RoundaboutRules& rb = p.roundabout;
    CHECK(rb.size_class(10).entry_corner_radius == doctest::Approx(5));
    CHECK(rb.size_class(14).apron_width == doctest::Approx(1.2));  // max_radius is inclusive
    CHECK(rb.size_class(20).exit_corner_radius == doctest::Approx(18));
    CHECK(rb.size_class(26).splitter_width == doctest::Approx(2.7));
    CHECK(rb.size_class(40).splitter_length == doctest::Approx(40));
    CHECK(rb.size_class(500).splitter_length == doctest::Approx(50));
    // speed factors are keyed in km/h: 50 km/h falls in the "up to 60" row
    CHECK(rb.splitter_speed_factor(30 * kKph) == doctest::Approx(0.8));
    CHECK(rb.splitter_speed_factor(50 * kKph) == doctest::Approx(1.05));
    CHECK(rb.splitter_speed_factor(70 * kKph) == doctest::Approx(1.3));
    CHECK(rb.splitter_speed_factor(100 * kKph) == doctest::Approx(1.5));
    CHECK(p.road_type("collector")->speed_limit == doctest::Approx(50 * kKph));
    CHECK(rb.splitter_speed_factor(p.road_type("collector")->speed_limit) == doctest::Approx(1.05));
    CHECK(rb.splitter_speed_factor(p.road_type("arterial")->speed_limit) == doctest::Approx(1.05));  // 60 km/h, row boundary
    CHECK(rb.splitter_speed_factor(p.road_type("rural_trunk")->speed_limit) == doctest::Approx(1.5));
}

// ============================================================================================ 2. straight roads

TEST_CASE("testland straight roads: lanes sit on the right-hand side and have testland's widths") {
    const RegionPack& p = base_pack();
    Edge e;
    // local: half = driving 2.8, parking 2.3, kerb 0.2, pavement 1.7; forward traffic keeps RIGHT (negative offset).
    e.road_type = "local";
    auto lanes = instantiate_lanes(e, *p.road_type("local"), DrivingSide::Right);
    REQUIRE(lanes.size() == 8);
    const Lane* fwd = nullptr;
    const Lane* bwd = nullptr;
    for (const Lane& l : lanes)
        if (l.lane_type == "driving") (l.dir == TravelDir::Forward ? fwd : bwd) = &l;
    REQUIRE(fwd);
    REQUIRE(bwd);
    CHECK(fwd->offset == doctest::Approx(-1.4));
    CHECK(fwd->width == doctest::Approx(2.8));
    CHECK(bwd->offset == doctest::Approx(1.4));
    CHECK(cross_section_width(lanes) == doctest::Approx(2 * (2.8 + 2.3 + 0.2 + 1.7)));
    // the same data with a left-driving setting mirrors exactly: same lane type and direction at the negated offset
    auto left = instantiate_lanes(e, *p.road_type("local"), DrivingSide::Left);
    REQUIRE(left.size() == lanes.size());
    for (const Lane& l : lanes) {
        bool found = false;
        for (const Lane& m : left)
            if (m.lane_type == l.lane_type && m.dir == l.dir && std::fabs(m.offset + l.offset) < 1e-9 && m.width == l.width) found = true;
        CHECK(found);
    }
    // forward lane of a left-driving copy is on the left (+)
    for (const Lane& l : left)
        if (l.lane_type == "driving" && l.dir == TravelDir::Forward) CHECK(l.offset == doctest::Approx(1.4));

    // every type: total width = sum of the JSON slot widths, nothing missing
    const json tj = read_json("road_types.json");
    for (const json& t : tj["road_types"]) {
        const std::string id = t["id"].get<std::string>();
        INFO(id);
        const RoadType* rt = p.road_type(id);
        REQUIRE(rt);
        Edge ed;
        ed.road_type = id;
        ed.one_way = true;  // one_way_variant (carriageway for dual types), listed offside -> nearside
        const auto ls = instantiate_lanes(ed, *rt, DrivingSide::Right);
        double want = 0;
        const json& arr = t.contains("carriageway") ? t["carriageway"] : t["one_way_variant"];
        for (const json& s : arr) want += s["width_m"].get<double>();
        CHECK(cross_section_width(ls) == doctest::Approx(want));
        // lane slot widths are exactly the JSON widths in order
        std::vector<double> ws;
        for (const Lane& l : ls) ws.push_back(l.width);
        std::vector<double> js;
        for (const json& s : arr) js.push_back(s["width_m"].get<double>());
        // lanes are listed left to right; the JSON lists offside -> nearside, so for Right-hand traffic the order is the same
        CHECK(ws == js);
    }
    // the expressway carriageway is placed offside -> nearside; with right-hand traffic the offside (hard strip 0.6 m) is on the LEFT (+y)
    Edge ex;
    ex.road_type = "expressway_dual";
    ex.one_way = true;
    const auto el = instantiate_lanes(ex, *p.road_type("expressway_dual"), DrivingSide::Right);
    REQUIRE(el.size() == 5);
    CHECK(el.front().lane_type == "hard_strip");
    CHECK(el.front().width == doctest::Approx(0.6));
    CHECK(el.front().offset == doctest::Approx(11.4 / 2 - 0.3));
    CHECK(el.back().lane_type == "verge");
    CHECK(el.back().offset == doctest::Approx(-11.4 / 2 + 1.3));
}

TEST_CASE("testland straight roads: render mesh cross-sections have testland's widths, kerb upstand and surfaces") {
    struct Row { const char* type; double asphalt_half; const char* outer_surface; double outer_half; };
    const Row rows[] = {
        {"local", 5.1, "paving", 5.1 + 0.2 + 1.7},
        {"collector", 3.1, "paving", 3.1 + 0.2 + 2.3},
        {"arterial", 6.8, "paving", 6.8 + 0.2 + 2.6},
        {"rural_trunk", 3.55 + 1.2, "grass", 3.55 + 1.2 + 3.1},
        {"farm_track", 2.1, "grass", 2.1 + 0.8},
    };
    for (const Row& r : rows) {
        INFO(r.type);
        Net n;
        const EdgeId e = n.road({0, 0, 0}, {200, 0, 0}, r.type);
        const JunctionSet js = n.junctions();
        const WorldMeshSet m = build_edge_mesh(n.g, *n.region, e, js);
        REQUIRE(m.count("asphalt"));
        REQUIRE(m.count(r.outer_surface));
        double ymin = 1e18, ymax = -1e18;
        for (const Vec3& v : m.at("asphalt").positions) ymin = std::min(ymin, v.y), ymax = std::max(ymax, v.y);
        CHECK(ymax == doctest::Approx(r.asphalt_half).epsilon(1e-9));
        CHECK(ymin == doctest::Approx(-r.asphalt_half).epsilon(1e-9));
        double omax = 0;
        for (const Vec3& v : m.at(r.outer_surface).positions) omax = std::max(omax, std::fabs(v.y));
        CHECK(omax == doctest::Approx(r.outer_half).epsilon(1e-9));
        // lane widths: every x-station ring spans the whole cross-section
        const RoadType* rt = n.region->road_type(r.type);
        CHECK(2 * r.outer_half == doctest::Approx(lane_total(*rt, false)));
        // x extent: the edge from end to end (no junction trims on a 2-arm-free straight)
        double xmin = 1e18, xmax = -1e18;
        for (const Vec3& v : m.at("asphalt").positions) xmin = std::min(xmin, v.x), xmax = std::max(xmax, v.x);
        CHECK(xmin == Near{0, 1e-6});
        CHECK(xmax == Near{200, 1e-6});
    }
    // kerb upstand comes from the road type (collector 0.18, not a default): kerb top minus carriageway at the kerb foot
    struct Kerb { const char* type; double up, inner, outer; };
    for (const Kerb& k : {Kerb{"collector", 0.18, 3.1, 3.3}, Kerb{"arterial", 0.18, 6.8, 7.0}}) {
        INFO(k.type);
        Net n;
        const EdgeId e = n.road({0, 0, 0}, {200, 0, 0}, k.type);
        const SurfaceProfile prof = make_surface_profile(*n.region, *n.g.edge(e));
        REQUIRE(prof.valid());
        // nearside is the RIGHT (-y) for this pack
        const double foot = prof.offset(-(k.inner - 0.01), true);
        const double top = prof.offset(-(k.outer + 0.01), false);
        CHECK(top - foot == Near{k.up, 0.012});
    }
}

TEST_CASE("testland straight roads: markings use testland's styles, colours, widths and dash patterns") {
    // local (40 km/h, 5.6 m wide, parking both sides): no centre line (under 5.8 m); bay lines + bay dividers
    {
        Net n;
        n.road({0, 0, 0}, {240, 0, 0}, "local");
        const JunctionSet js = n.junctions();
        const MarkingSet ms = generate_markings(n.g, *n.region, rules(), js).merged();
        CHECK(ms.warnings.empty());
        CHECK(pick(ms, MarkingCategory::CentreLine).empty());
        CHECK(pick(ms, MarkingCategory::LaneLine).empty());
        const auto bay = pick(ms, MarkingCategory::ParkingBay, "bay_edge");
        REQUIRE(bay.size() == 2);
        for (const Marking* m : bay) {
            CHECK(ms.colour_id(*m) == "paint_white");
            CHECK(box_of(ms, *m).dy() == doctest::Approx(0.12).epsilon(1e-6));
            CHECK(std::fabs(box_of(ms, *m).cy()) == doctest::Approx(2.8).epsilon(1e-6));  // the driving | parking boundary
        }
        auto ticks = pick(ms, MarkingCategory::ParkingBay, "bay_tick");
        REQUIRE(ticks.size() > 10);
        std::vector<double> xs;
        for (const Marking* m : ticks) {
            const Box b = box_of(ms, *m);
            CHECK(b.dx() == doctest::Approx(0.13).epsilon(1e-6));
            if (b.cy() < 0) xs.push_back(b.cx());
        }
        std::sort(xs.begin(), xs.end());
        REQUIRE(xs.size() > 5);
        for (std::size_t i = 1; i < xs.size(); ++i) CHECK(xs[i] - xs[i - 1] == doctest::Approx(5.5).epsilon(1e-6));  // the bay length
    }
    // collector (50 km/h, 6.2 m): yellow centre line 3 m / 3 m, 0.12 wide (50 km/h is "speed_max 50", not 50 mph)
    {
        Net n;
        n.road({0, 0, 0}, {240, 0, 0}, "collector");
        const JunctionSet js = n.junctions();
        const MarkingSet ms = generate_markings(n.g, *n.region, rules(), js).merged();
        CHECK(ms.warnings.empty());
        const auto c = pick(ms, MarkingCategory::CentreLine);
        REQUIRE(!c.empty());
        for (const Marking* m : c) {
            CHECK(ms.style_id(*m) == "centre_yel_slow");
            CHECK(ms.colour_id(*m) == "paint_yellow");
            CHECK(box_of(ms, *m).cy() == Near{0, 1e-6});
        }
        check_dashes(ms, c, 3.0, 3.0, 0.12);
        CHECK(pick(ms, MarkingCategory::EdgeLine).empty());  // kerbs on both sides: no edge line
    }
    // arterial (60 km/h, 13.6 m, two lanes each way): double yellow centre; lane lines 3 m / 9 m, 0.12 (60 km/h is "up to 70")
    {
        Net n;
        n.road({0, 0, 0}, {240, 0, 0}, "arterial");
        const JunctionSet js = n.junctions();
        const MarkingSet ms = generate_markings(n.g, *n.region, rules(), js).merged();
        CHECK(ms.warnings.empty());
        const auto c = pick(ms, MarkingCategory::CentreLine);
        REQUIRE(c.size() >= 2);
        std::set<long> ys;
        for (const Marking* m : c) {
            CHECK(ms.style_id(*m) == "centre_yel_dbl");
            CHECK(box_of(ms, *m).dy() == doctest::Approx(0.09).epsilon(1e-6));
            ys.insert(std::lround(box_of(ms, *m).cy() * 1000));
        }
        CHECK(ys == std::set<long>{-110, 110});
        const auto l = pick(ms, MarkingCategory::LaneLine);
        REQUIRE(!l.empty());
        std::set<long> ly;
        for (const Marking* m : l) {
            CHECK(ms.style_id(*m) == "lane_fast");
            ly.insert(std::lround(box_of(ms, *m).cy() * 1000));
        }
        CHECK(ly == std::set<long>{-3400, 3400});
        std::vector<const Marking*> one_side;
        for (const Marking* m : l)
            if (box_of(ms, *m).cy() > 0) one_side.push_back(m);
        check_dashes(ms, one_side, 3.0, 9.0, 0.12, 5);
    }
    // rural trunk (90 km/h, 7.1 m): fast yellow centre 4.5 / 7.5; edge lines 0.22 wide (90 km/h is above 80)
    {
        Net n;
        n.road({0, 0, 0}, {300, 0, 0}, "rural_trunk");
        const JunctionSet js = n.junctions();
        const MarkingSet ms = generate_markings(n.g, *n.region, rules(), js).merged();
        CHECK(ms.warnings.empty());
        const auto c = pick(ms, MarkingCategory::CentreLine);
        for (const Marking* m : c) CHECK(ms.style_id(*m) == "centre_yel_fast");
        check_dashes(ms, c, 4.5, 7.5, 0.12, 8);
        const auto e = pick(ms, MarkingCategory::EdgeLine);
        REQUIRE(e.size() >= 2);
        std::set<long> ys;
        for (const Marking* m : e) {
            CHECK(ms.style_id(*m) == "edge_thick");
            CHECK(box_of(ms, *m).dy() == doctest::Approx(0.22).epsilon(1e-6));
            ys.insert(std::lround(std::fabs(box_of(ms, *m).cy()) * 1000));
        }
        CHECK(ys == std::set<long>{3350});  // lane edge 3.55 m, inset 0.2 m into the lane
    }
    // farm track: marking set "" -> nothing, no warnings
    {
        Net n;
        n.road({0, 0, 0}, {200, 0, 0}, "farm_track");
        const JunctionSet js = n.junctions();
        const MarkingSet ms = generate_markings(n.g, *n.region, rules(), js).merged();
        CHECK(ms.marks.empty());
        CHECK(ms.warnings.empty());
    }
    // expressway carriageway (100 km/h): lane line 0.18 (3 m / 9 m) between the two driving lanes; heavy edge lines 0.27
    {
        Net n;
        n.road({0, 0, 0}, {300, 0, 0}, "expressway_dual", true);
        const JunctionSet js = n.junctions();
        const MarkingSet ms = generate_markings(n.g, *n.region, rules(), js).merged();
        CHECK(ms.warnings.empty());
        const auto l = pick(ms, MarkingCategory::LaneLine);
        for (const Marking* m : l) {
            CHECK(ms.style_id(*m) == "lane_fast_wide");
            CHECK(box_of(ms, *m).cy() == doctest::Approx(1.65).epsilon(1e-6));  // right-hand traffic: offside (hard strip) is at +y
        }
        check_dashes(ms, l, 3.0, 9.0, 0.18);
        const auto e = pick(ms, MarkingCategory::EdgeLine);
        REQUIRE(e.size() >= 2);
        std::set<long> ys;
        for (const Marking* m : e) {
            CHECK(ms.style_id(*m) == "edge_heavy");
            CHECK(box_of(ms, *m).dy() == doctest::Approx(0.27).epsilon(1e-6));
            ys.insert(std::lround(box_of(ms, *m).cy() * 1000));
        }
        // boundaries at y = +5.1 (offside strip | lane) and y = -1.8 (lane | nearside strip), 0.3 m inset into the lane
        CHECK(ys == std::set<long>{-1500, 4800});
    }
}

TEST_CASE("testland straight roads: street lights follow testland's arrangement, spacing, offsets and mounting") {
    const auto lit = [](Net& n) {
        const JunctionSet js = n.junctions();
        return generate_street_lights(n.g, *n.region, js).merged();
    };
    // local: opposite, spacing 26, column 0.35 m in from the back of the footway (7.0 m), 5.5 m high, 0.7 m outreach
    {
        Net n;
        n.road({0, 0, 0}, {300, 0, 0}, "local");
        const auto ls = lit(n);
        REQUIRE(!ls.empty());
        std::vector<double> left, right;
        for (const StreetLight& l : ls) {
            CHECK(l.style == "post_top_5m");
            CHECK(l.mounting_height == doctest::Approx(5.5));
            CHECK(l.outreach == doctest::Approx(0.7));
            CHECK(std::fabs(l.position.y) == Near{7.0 - 0.35, 0.02});
            (l.position.y > 0 ? left : right).push_back(l.s);
            CHECK(wrap_angle(l.heading - (l.position.y > 0 ? -kPi / 2 : kPi / 2)) == Near{0, 1e-6});  // brackets point at the road
        }
        CHECK(left.size() == right.size());
        std::sort(left.begin(), left.end());
        REQUIRE(left.size() > 5);
        const double cell = left[1] - left[0];
        CHECK(std::fabs(cell / 26.0 - 1) <= 0.2 + 1e-9);
        for (std::size_t i = 2; i < left.size(); ++i) CHECK(left[i] - left[i - 1] == doctest::Approx(cell).epsilon(1e-6));
    }
    // collector: staggered, nearside (RIGHT, y < 0) first, spacing 37
    {
        Net n;
        n.road({0, 0, 0}, {300, 0, 0}, "collector");
        auto ls = lit(n);
        REQUIRE(ls.size() > 4);
        std::sort(ls.begin(), ls.end(), [](const StreetLight& a, const StreetLight& b) { return a.s < b.s; });
        CHECK(ls.front().side == LightSide::Right);
        for (std::size_t i = 0; i < ls.size(); ++i) {
            CHECK(ls[i].style == "arm_9m");
            CHECK(ls[i].mounting_height == doctest::Approx(9));
            CHECK(ls[i].outreach == doctest::Approx(1.4));
            CHECK(std::fabs(ls[i].position.y) == Near{5.6 - 0.4, 0.02});
            CHECK((ls[i].side == LightSide::Right) == (i % 2 == 0));
            if (i) CHECK(std::fabs((ls[i].s - ls[i - 1].s) / 37.0 - 1) <= 0.2 + 1e-9);
        }
    }
    // arterial: single side (nearside = right), spacing 41, 11 m high
    {
        Net n;
        n.road({0, 0, 0}, {300, 0, 0}, "arterial");
        auto ls = lit(n);
        REQUIRE(ls.size() > 3);
        std::sort(ls.begin(), ls.end(), [](const StreetLight& a, const StreetLight& b) { return a.s < b.s; });
        for (std::size_t i = 0; i < ls.size(); ++i) {
            CHECK(ls[i].side == LightSide::Right);
            CHECK(ls[i].position.y < 0);
            CHECK(std::fabs(ls[i].position.y) == Near{9.6 - 0.45, 0.02});
            CHECK(ls[i].style == "arm_11m");
            CHECK(ls[i].mounting_height == doctest::Approx(11));
            if (i) CHECK(std::fabs((ls[i].s - ls[i - 1].s) / 41.0 - 1) <= 0.2 + 1e-9);
        }
    }
    // unlit types produce nothing
    for (const char* t : {"rural_trunk", "farm_track"}) {
        Net n;
        n.road({0, 0, 0}, {300, 0, 0}, t);
        CHECK(lit(n).empty());
    }
    // expressway: twin-arm columns in the median between two carriageways (inner edges 4.2 m apart: centres +-7.8 m)
    {
        Net n;
        n.road({0, -7.8, 0}, {300, -7.8, 0}, "expressway_dual", true);  // eastbound, keeps right = south
        n.road({300, 7.8, 0}, {0, 7.8, 0}, "expressway_dual", true);    // westbound
        const auto ls = lit(n);
        REQUIRE(ls.size() > 3);
        for (const StreetLight& l : ls) {
            CHECK(l.side == LightSide::Median);
            CHECK(l.twin_arm);
            CHECK(l.style == "twin_13m");
            CHECK(l.mounting_height == doctest::Approx(13));
            CHECK(l.outreach == doctest::Approx(2.0));
            CHECK(l.position.y == Near{0, 0.05});
        }
    }
}

TEST_CASE("testland: render mesh chunks of a small network are valid and use only testland's surfaces") {
    Net n;
    for (double d : {0.0, 90.0, 180.0}) n.spoke(d, 120, d == 90 ? "local" : "collector");
    n.spoke(270, 120, "rural_trunk");
    const JunctionSet js = n.junctions();
    for (const auto& [id, j] : js.junctions) CHECK_MESSAGE(!j.degraded, j.degraded_reason);
    SurfaceTable table;
    const auto chunks = build_mesh_chunks(n.g, *n.region, js, nullptr, {}, {}, nullptr, &table);
    REQUIRE(!chunks.empty());
    const std::set<std::string> allowed{"asphalt", "kerb", "paving", "grass"};
    std::size_t verts = 0;
    for (const auto& [key, chunk] : chunks) {
        for (const auto& [name, mb] : chunk.surfaces) {
            CHECK_MESSAGE(allowed.count(name), "unexpected surface " << name);
            REQUIRE(mb.positions.size() == mb.normals.size());
            verts += mb.positions.size();
            for (const Float3& v : mb.positions) CHECK(std::isfinite(v.x + v.y + v.z));
            for (std::uint32_t i : mb.indices) REQUIRE(i < mb.positions.size());
        }
    }
    CHECK(verts > 100);
    CHECK(table.id("asphalt") >= 0);
    CHECK(table.id("grass") >= 0);
}

// ============================================================================================ 3. junctions

namespace {

struct Pair { const char* minor; const char* major; double radius; };

// Radius per the testland class table (farm_track 25, local 45, collector 65, arterial 80, rural_trunk 95).
const Pair kPairs[] = {
    {"local", "local", 5},          {"local", "collector", 8},     {"collector", "collector", 9.5},
    {"local", "arterial", 8},       {"collector", "arterial", 9.5}, {"local", "rural_trunk", 11.5},
    {"farm_track", "local", 3.5},   {"farm_track", "rural_trunk", 9}, {"collector", "rural_trunk", 13},
    {"arterial", "rural_trunk", 13}, {"farm_track", "farm_track", 3.5}, {"farm_track", "arterial", 5.5},
};

double kerb_of(const RegionPack& r, const std::string& type) {
    const RoadType* t = r.road_type(type);
    REQUIRE(t);
    // inner face of the outermost kerb lane, or the half width when there is none (two-way: half cross-section)
    double u = 0;
    for (const CrossSectionSlot& s : t->half) {
        if (s.lane_type == "kerb") return u;
        u += s.width;
    }
    return u;
}

}  // namespace

TEST_CASE("testland T-junctions: no degraded junctions; corner radii and trims come from testland's class table") {
    for (const Pair& pr : kPairs) {
        INFO(pr.minor << " onto " << pr.major);
        Net n;
        n.spoke(180, 150, pr.major);
        n.spoke(0, 150, pr.major);
        n.spoke(90, 150, pr.minor);
        const JunctionSet js = n.junctions();
        const Junction* j = js.find(n.centre);
        REQUIRE(j);
        CHECK_MESSAGE(!j->degraded, j->degraded_reason);
        CHECK(j->cls == NodeClass::Junction);
        REQUIRE(j->arms.size() == 3);
        REQUIRE(j->corners.size() == 3);
        for (std::size_t k = 0; k < 3; ++k) {
            const CornerCurve& c = j->corners[k];
            if (c.angle > kPi - 1e-6) continue;  // the straight kerb of the through road
            CHECK(j->corner_radii[k] == doctest::Approx(pr.radius));
            CHECK(c.requested_radius == doctest::Approx(pr.radius));
            CHECK(c.radius == doctest::Approx(pr.radius));  // 150 m arms leave room for the whole fillet
            CHECK(c.angle == doctest::Approx(kPi / 2));
        }
        // trim of an arm = the other road's kerb offset + the fillet radius (90 degree corners)
        // (a turn pocket, on for the rural trunk class, replaces the pocket arm's trim with its flare length)
        const double kmaj = kerb_of(*n.region, pr.major), kmin = kerb_of(*n.region, pr.minor);
        if (j->pockets.empty())
            for (std::size_t a = 0; a < 3; ++a) {
                const bool minor_arm = std::fabs(std::fabs(j->arms[a].heading_out) - kPi / 2) < 1e-6;
                CHECK(j->trims[a] == doctest::Approx((minor_arm ? kmaj : kmin) + pr.radius).epsilon(1e-6));
            }
        // the arms carry testland's classes
        for (const JunctionArm& arm : j->arms) {
            const bool minor_arm = std::fabs(std::fabs(arm.heading_out) - kPi / 2) < 1e-6;
            CHECK(arm.road_class == n.region->road_type(minor_arm ? pr.minor : pr.major)->road_class);
        }
    }
}

TEST_CASE("testland 4-arm junctions: no degraded junctions; every corner takes testland's class-pair radius") {
    const Pair crosses[] = {{"local", "collector", 8}, {"collector", "arterial", 9.5}, {"local", "rural_trunk", 11.5}, {"farm_track", "arterial", 5.5}};
    for (const Pair& pr : crosses) {
        INFO(pr.minor << " across " << pr.major);
        Net n;
        n.spoke(0, 160, pr.major);
        n.spoke(180, 160, pr.major);
        n.spoke(90, 160, pr.minor);
        n.spoke(270, 160, pr.minor);
        const JunctionSet js = n.junctions();
        const Junction* j = js.find(n.centre);
        REQUIRE(j);
        CHECK_MESSAGE(!j->degraded, j->degraded_reason);
        REQUIRE(j->arms.size() == 4);
        REQUIRE(j->corners.size() == 4);
        for (std::size_t k = 0; k < 4; ++k) {
            CHECK(j->corner_radii[k] == doctest::Approx(pr.radius));
            CHECK(j->corners[k].radius == doctest::Approx(pr.radius));
        }
        CHECK(j->connectors.size() > 4);
        CHECK(set_area(j->carriageway) > 50);
        // the major arms are the major-type ones
        for (int mi : j->major_arms) CHECK(j->arms[static_cast<std::size_t>(mi)].road_class == n.region->road_type(pr.major)->road_class);
    }
}

TEST_CASE("testland skewed T: radius scaled by the corner angle within testland's skew limits") {
    Net n;
    n.spoke(180, 150, "local");
    n.spoke(0, 150, "local");
    n.spoke(60, 150, "local");
    const JunctionSet js = n.junctions();
    const Junction* j = js.find(n.centre);
    REQUIRE(j);
    CHECK_MESSAGE(!j->degraded, j->degraded_reason);
    int acute = 0, obtuse = 0;
    for (std::size_t k = 0; k < j->corners.size(); ++k) {
        const CornerCurve& c = j->corners[k];
        if (c.angle > kPi - 1e-6) continue;
        if (c.angle < kPi / 2) {
            ++acute;
            CHECK(c.angle == doctest::Approx(60 * kDeg));
            CHECK(j->corner_radii[k] == doctest::Approx(5 * 0.7));  // 60/90 = 0.667 clamps to the minimum 0.7
        } else {
            ++obtuse;
            CHECK(c.angle == doctest::Approx(120 * kDeg));
            CHECK(j->corner_radii[k] == doctest::Approx(5 * (120.0 / 90.0)));  // 1.333 < 1.35
        }
    }
    CHECK(acute == 1);
    CHECK(obtuse == 1);
}

TEST_CASE("testland T-junction on a rural trunk road gets the data-enabled turn pocket, on the offside of right-hand traffic") {
    for (DrivingSide side : {DrivingSide::Right, DrivingSide::Left}) {
        for (double minor_deg : {90.0, 270.0}) {
            INFO("side " << (side == DrivingSide::Right ? "right" : "left") << " minor at " << minor_deg);
            Net n(side);
            n.spoke(180, 200, "rural_trunk");
            n.spoke(0, 200, "rural_trunk");
            n.spoke(minor_deg, 200, "farm_track");
            const JunctionSet js = n.junctions();
            const Junction* j = js.find(n.centre);
            REQUIRE(j);
            CHECK_MESSAGE(!j->degraded, j->degraded_reason);
            REQUIRE(j->pockets.size() == 1);
            const TurnPocket& tp = j->pockets[0];
            CHECK_FALSE(tp.forced);
            CHECK(tp.lane_width == doctest::Approx(3.05));
            // 90 km/h falls in the "up to 90" row: storage 32 m, taper 1:22 of the lane width (a 90 mph misreading gives 44 / 1:28)
            CHECK(tp.storage_length == doctest::Approx(32));
            CHECK(tp.taper_length == doctest::Approx(22 * 3.05));
            // the pocket serves the offside turn: toward the minor arm, 90 degrees clockwise (right-hand) / ccw (left-hand) of the arm's outward heading
            const JunctionArm& a = j->arms[static_cast<std::size_t>(tp.arm)];
            const JunctionArm& m = j->arms[static_cast<std::size_t>(tp.to_arm)];
            CHECK(m.road_class == 25);
            const double phi = wrap_angle(m.heading_out - a.heading_out);
            CHECK(phi == Near{side == DrivingSide::Right ? -kPi / 2 : kPi / 2, 1e-6});
        }
    }
    // class 65 through road is below major_class_min 90: no pocket
    Net n;
    n.spoke(180, 200, "collector");
    n.spoke(0, 200, "collector");
    n.spoke(90, 200, "local");
    CHECK(n.junctions().find(n.centre)->pockets.empty());
}

TEST_CASE("testland driving side: a junction and its mirror image built with the mirrored pack are mirror images") {
    // Layout A (right-hand pack) at angles theta; layout B (left-hand copy of the same pack) at -theta.
    struct Spoke { double deg; const char* type; };
    const std::vector<Spoke> arms{{0, "arterial"}, {100, "local"}, {190, "arterial"}, {250, "collector"}};
    struct Summary {
        std::size_t connectors = 0, corners = 0, crossings = 0;
        std::map<int, int> kinds;
        std::vector<double> trims, radii;
        double carriageway = 0, pavements = 0;
        std::map<std::string, int> mark_counts;  // "category/style"
        double mark_area = 0;
    };
    const auto build = [&](DrivingSide side, double sign) {
        Net n(side);
        for (const Spoke& s : arms) n.spoke(sign * s.deg, 150, s.type);
        const JunctionSet js = n.junctions();
        const Junction* j = js.find(n.centre);
        REQUIRE(j);
        CHECK_MESSAGE(!j->degraded, j->degraded_reason);
        Summary s;
        s.connectors = j->connectors.size();
        s.corners = j->corners.size();
        s.crossings = j->crossings.size();
        for (const LaneConnector& c : j->connectors) ++s.kinds[static_cast<int>(c.kind)];
        s.trims = j->trims;
        std::sort(s.trims.begin(), s.trims.end());
        s.radii = j->corner_radii;
        std::sort(s.radii.begin(), s.radii.end());
        s.carriageway = set_area(j->carriageway);
        s.pavements = set_area(j->pavements);
        const MarkingSet ms = generate_markings(n.g, *n.region, rules(), js).merged();
        for (const Marking& m : ms.marks) {
            ++s.mark_counts[std::string(marking_category_name(m.category)) + "/" + ms.style_id(m)];
            s.mark_area += mark_area(ms, m);
        }
        return s;
    };
    const Summary r = build(DrivingSide::Right, 1.0);
    const Summary l = build(DrivingSide::Left, -1.0);
    CHECK(r.connectors == l.connectors);
    CHECK(r.corners == l.corners);
    CHECK(r.crossings == l.crossings);
    CHECK(r.kinds == l.kinds);
    REQUIRE(r.trims.size() == l.trims.size());
    for (std::size_t i = 0; i < r.trims.size(); ++i) CHECK(r.trims[i] == doctest::Approx(l.trims[i]).epsilon(1e-6));
    REQUIRE(r.radii.size() == l.radii.size());
    for (std::size_t i = 0; i < r.radii.size(); ++i) CHECK(r.radii[i] == doctest::Approx(l.radii[i]).epsilon(1e-6));
    CHECK(r.carriageway == doctest::Approx(l.carriageway).epsilon(1e-6));
    CHECK(r.pavements == doctest::Approx(l.pavements).epsilon(1e-6));
    CHECK(r.mark_counts == l.mark_counts);
    CHECK(r.mark_area == doctest::Approx(l.mark_area).epsilon(1e-6));
    CHECK(r.connectors > 8);
}

TEST_CASE("testland driving side: lanes, connectors and give-way lines sit on the mirrored side of an identical left-driving copy") {
    for (DrivingSide side : {DrivingSide::Right, DrivingSide::Left}) {
        const double sgn = side == DrivingSide::Right ? 1.0 : -1.0;  // +1: inbound lanes on the left of heading_out
        INFO("side " << (sgn > 0 ? "right" : "left"));
        Net n(side);
        n.spoke(180, 150, "collector");
        n.spoke(0, 150, "collector");
        const EdgeId minor = n.spoke(90, 150, "local");
        const JunctionSet js = n.junctions();
        const Junction* j = js.find(n.centre);
        REQUIRE(j);
        CHECK(j->side == side);
        std::size_t minor_arm = 99;
        for (std::size_t a = 0; a < j->arms.size(); ++a) {
            if (j->arms[a].edge == minor) minor_arm = a;
            for (const LaneRef& l : j->arms[a].lanes) CHECK((l.inbound ? l.lateral * sgn : -l.lateral * sgn) > 0);  // inbound on the left of heading_out for right-hand traffic
        }
        REQUIRE(minor_arm < j->arms.size());
        // a left turn from the through road onto the minor arm (90 degrees ccw): offside for right-hand traffic, nearside for left
        int left_turns = 0, right_turns = 0;
        for (const LaneConnector& c : j->connectors) {
            if (c.kind != TurnKind::Offside && c.kind != TurnKind::Nearside) continue;
            // planar turn from the geometry alone: travel heading in = outward heading + pi, out = outward heading of the target
            const double turn = wrap_angle(j->arms[static_cast<std::size_t>(c.arm_to)].heading_out -
                                           (j->arms[static_cast<std::size_t>(c.arm_from)].heading_out + kPi));
            const bool geom_left = turn > 0;
            CHECK(turn_is_left(c.kind, side) == geom_left);
            CHECK((c.kind == TurnKind::Nearside) == ((side == DrivingSide::Left) == geom_left));  // nearside = turning toward the kerb side
            (geom_left ? left_turns : right_turns)++;
        }
        // T: W->N and N->E turn left, E->N and N->W turn right (a connector per lane pair)
        CHECK(left_turns > 0);
        CHECK(left_turns == right_turns);
        // give-way markings: minor arm yields; the line sits across its inbound lanes (the LEFT half, x < 0, for right-hand traffic)
        const MarkingSet ms = generate_markings(n.g, *n.region, rules(), js).merged();
        const auto gw = pick(ms, MarkingCategory::GiveWayLine);
        REQUIRE(!gw.empty());
        double ymin = 1e18;
        for (const Marking* m : gw) {
            CHECK(ms.style_id(*m) == "giveway_slow");  // the 40 km/h local street: "speed_max 50"
            CHECK(centroid(ms, *m).x * sgn < 0);       // right-hand traffic: x < 0 side (west of a northbound minor arm's inbound = southbound lane)
            ymin = std::min(ymin, box_of(ms, *m).y0);
            const Box b = box_of(ms, *m);
            CHECK(b.dy() == doctest::Approx(0.25).epsilon(1e-6));
        }
        CHECK(ymin == doctest::Approx(j->trims[minor_arm] + 0.7).epsilon(1e-6));  // testland setback
        CHECK(pick(ms, MarkingCategory::GiveWayTriangle).empty());                // testland paints no triangle
        // and the other roads do not give way
        for (const Marking* m : gw) CHECK(box_of(ms, *m).cy() > 1.0);
    }
}

TEST_CASE("testland junction markings: arrows, zebra, stop line and mouth edge use testland's rules") {
    Net n;
    const EdgeId minor = n.spoke(90, 160, "collector");
    n.spoke(270, 160, "collector");
    n.spoke(0, 160, "arterial");
    n.spoke(180, 160, "arterial");
    const JunctionSet js = n.junctions();
    const Junction* j = js.find(n.centre);
    REQUIRE(j);
    CHECK_MESSAGE(!j->degraded, j->degraded_reason);
    MarkingParams mp;
    for (const JunctionArm& a : j->arms) {
        if (a.edge == minor) {
            mp.stop_arms.insert({n.centre, a.edge});
            mp.crossing_arms.insert({n.centre, a.edge});
        }
    }
    const MarkingSet ms = generate_junction_markings(n.g, *n.region, rules(), *j, mp);
    CHECK(ms.warnings.empty());
    // arrows: arterial arms (2 inbound lanes), 3 distances x 2 lanes x 2 arms; 60 km/h arrow length 6.5 m
    const auto arrows = pick(ms, MarkingCategory::Arrow);
    CHECK(arrows.size() >= 12);  // 3 distances x 2 lanes x 2 arterial arms (a two-part glyph counts twice)
    double longest = 0;
    for (const Marking* m : arrows) {
        const Box b = box_of(ms, *m);
        longest = std::max(longest, std::max(b.dx(), b.dy()));
        CHECK(ms.style_id(*m) == "fill_pale");
    }
    CHECK(longest == Near{6.5, 0.02});  // glyph "ahead" spans x 0..1
    // stop line: the minor arm was asked for one; testland style stop_bar 0.3 wide, setback 0.8
    const auto stop = pick(ms, MarkingCategory::StopLine);
    REQUIRE(!stop.empty());
    double ymin = 1e18;
    for (const Marking* m : stop) {
        CHECK(ms.style_id(*m) == "stop_bar");
        ymin = std::min(ymin, box_of(ms, *m).y0);
        CHECK(box_of(ms, *m).dy() == doctest::Approx(0.3).epsilon(1e-6));
    }
    std::size_t ma = 0;
    for (std::size_t a = 0; a < j->arms.size(); ++a)
        if (j->arms[a].edge == minor) ma = a;
    CHECK(ymin == doctest::Approx(j->trims[ma] + 0.8).epsilon(1e-6));
    // zebra: stripes 0.45 wide across, band 3.4 along the (north-south) arm; give-way lines "zebra_gw" 0.28
    const auto zebra = pick(ms, MarkingCategory::Zebra, "fill_pale");
    REQUIRE(zebra.size() >= 3);
    for (const Marking* m : zebra) {
        const Box b = box_of(ms, *m);
        CHECK(std::min(b.dx(), b.dy()) == doctest::Approx(0.45).epsilon(1e-6));
        CHECK(std::max(b.dx(), b.dy()) == doctest::Approx(3.4).epsilon(1e-6));
    }
    const auto zgw = pick(ms, MarkingCategory::Zebra, "zebra_gw");
    REQUIRE(!zgw.empty());
    for (const Marking* m : zgw) CHECK(box_of(ms, *m).dx() > 0);
}

// ============================================================================================ 4. roundabouts

namespace {

void four_arm_ring(Net& n, double radius, const char* approach) {
    for (double d : {0.0, 90.0, 180.0, 270.0}) n.spoke(d, 160, approach);
    REQUIRE(n.place(radius));
}

}  // namespace

TEST_CASE("testland roundabout: ring type, circulation, size class, splitters, surfaces and fillets come from testland") {
    for (DrivingSide side : {DrivingSide::Right, DrivingSide::Left}) {
        INFO("side " << (side == DrivingSide::Right ? "right" : "left"));
        Net n(side);
        four_arm_ring(n, 25, "collector");
        RoundaboutResult rr;
        const JunctionSet js = n.junctions(&rr);
        const RoundaboutScan scan = find_roundabouts(n.g, *n.region);
        CHECK(scan.problems.empty());
        REQUIRE(scan.groups.size() == 1);
        CHECK(scan.groups[0].clockwise == (side == DrivingSide::Left));  // right-hand traffic circulates anticlockwise
        // every ring edge is the testland ring type, arcs at most testland's max_arc
        for (EdgeId e : scan.groups[0].ring_edges) CHECK(n.g.edge(e)->road_type == "circus_ring");
        CHECK(scan.groups[0].ring_edges.size() >= 5);  // 2*pi / 1.4 rad
        REQUIRE(rr.layouts.size() == 1);
        const RoundaboutLayout& lay = rr.layouts[0];
        REQUIRE_MESSAGE(lay.ok, lay.error);
        for (const auto& [nid, j] : js.junctions) CHECK_MESSAGE(!j.degraded, j.degraded_reason);
        CHECK(lay.island_surface == "paving");
        CHECK(lay.splitter_surface == "grass");
        CHECK(lay.island_kerb_upstand == doctest::Approx(0.16));
        CHECK(lay.apron.empty());  // island radius well above 12 m
        CHECK(lay.group.radius == Near{25, 0.5});
        // splitters: 2.7 m wide at the ring (radius 25 is in the "up to 26" class); length 28 m x 1.05 for the 50 km/h collector
        REQUIRE(lay.splitters.size() == 4);
        for (const SplitterIsland& sp : lay.splitters) {
            CHECK(sp.width == doctest::Approx(2.7).epsilon(0.03));
            CHECK(sp.length == doctest::Approx(28 * 1.05).epsilon(0.03));
        }
        // entry / exit fillets of the ring nodes: 10 m and 18 m
        std::multiset<long> radii;
        for (const auto& [nid, j] : lay.ring_junctions)
            for (std::size_t k = 0; k < j.corners.size(); ++k) {
                const CornerCurve& c = j.corners[k];
                if (c.requested_radius > 0 && j.arms.size() > 2) radii.insert(std::lround(c.requested_radius * 100));
            }
        CHECK(radii.count(1000) == 4);   // entries
        CHECK(radii.count(1800) == 4);   // exits
        // give-way: one entry per approach
        CHECK(lay.give_way.size() == 8);  // 4 entries x 2 ring lanes
        // ring light columns exist and carry testland's style and mounting; island column since the island radius is >= 12 m
        const StreetLightResult sl = generate_street_lights(n.g, *n.region, js, &rr.layouts);
        int island = 0, ring = 0;
        for (const StreetLight& l : sl.merged()) {
            if (l.origin == LightOrigin::RoundaboutIsland) ++island;
            if (l.origin == LightOrigin::RoundaboutRing) {
                ++ring;
                CHECK(l.style == "arm_9m");
                CHECK(l.mounting_height == doctest::Approx(9));
                CHECK(l.outreach == doctest::Approx(1.1));
            }
        }
        CHECK(island == 1);
        CHECK(ring > 4);
        // island mesh: the central island is "paving" (large), the splitters "grass" (small) - the UK pack has them the other way round
        const WorldMeshSet mesh = build_roundabout_mesh(n.g, *n.region, lay);
        const auto top_area = [&](const char* surf) {
            double a = 0;
            if (!mesh.count(surf)) return a;
            const WorldMesh& m = mesh.at(surf);
            for (std::size_t i = 0; i + 2 < m.indices.size(); i += 3) {
                const Vec2 p = m.positions[m.indices[i]].xy(), q = m.positions[m.indices[i + 1]].xy(), r = m.positions[m.indices[i + 2]].xy();
                a += std::fabs((q - p).cross(r - p)) / 2;
            }
            return a;
        };
        double splitter_area = 0;
        for (const SplitterIsland& sp : lay.splitters) splitter_area += std::fabs(area(sp.polygon));
        CHECK(top_area("paving") == doctest::Approx(std::fabs(set_area(lay.island))).epsilon(0.1));
        CHECK(top_area("grass") == doctest::Approx(splitter_area).epsilon(0.15));
        // markings: entry lines with the 50 km/h style (not "speed_max 40"), splitter outline "edge_thin" (50 km/h is not above 50)
        MarkingParams mp;
        mp.roundabouts = &rr;
        const MarkingResult mr = generate_markings(n.g, *n.region, rules(), js, mp);
        std::size_t entries = 0;
        for (const auto& [nid, set] : mr.junctions)
            for (const Marking* m : pick(set, MarkingCategory::RoundaboutEntry)) {
                ++entries;
                CHECK(set.style_id(*m) == "giveway_fast");
            }
        CHECK(entries >= 4);
        std::size_t outlines = 0;
        for (const auto& [nid, set] : mr.splitters)
            for (const Marking* m : pick(set, MarkingCategory::EdgeLine)) {
                ++outlines;
                CHECK(set.style_id(*m) == "edge_thin");
            }
        CHECK(outlines >= 1);
    }
}

TEST_CASE("testland roundabout: tool limits and the small-ring apron come from testland") {
    Net n;
    for (double d : {0.0, 90.0, 180.0, 270.0}) n.spoke(d, 120, "local");
    // tool radius limits 10 .. 90 m (UK: 8 .. 100)
    CHECK_FALSE(check_place_roundabout(n.g, {0, 0, 0}, 9, "circus_ring", *n.region).empty());
    CHECK_FALSE(check_place_roundabout(n.g, {0, 0, 0}, 95, "circus_ring", *n.region).empty());
    CHECK_FALSE(check_place_roundabout(n.g, {0, 0, 0}, 25, "collector", *n.region).empty());  // not a ring type
    CHECK(check_place_roundabout(n.g, {0, 0, 0}, 25, "circus_ring", *n.region).empty());
    // radius 14 (the "up to 14" class): island radius under the 12 m limit gets the 1.2 m apron, class "up to 14" fillets 5 / 9.5
    REQUIRE(n.place(14));
    RoundaboutResult rr;
    const JunctionSet js = n.junctions(&rr);
    REQUIRE(rr.layouts.size() == 1);
    const RoundaboutLayout& lay = rr.layouts[0];
    REQUIRE_MESSAGE(lay.ok, lay.error);
    CHECK_FALSE(lay.apron.empty());
    CHECK(lay.apron_surface == "grass");
    CHECK(lay.group.radius == doctest::Approx(14).epsilon(0.03));
    for (const auto& [id, j] : js.junctions) CHECK_MESSAGE(!j.degraded, j.degraded_reason);
    std::multiset<long> radii;
    for (const auto& [nid, j] : lay.ring_junctions)
        for (const CornerCurve& c : j.corners)
            if (c.requested_radius > 0 && j.arms.size() > 2) radii.insert(std::lround(c.requested_radius * 100));
    CHECK(radii.count(500) == 4);
    CHECK(radii.count(950) == 4);
}

// ============================================================================================ 5. merge / diverge

namespace {

int odd_arm(const Junction& j) {
    for (std::size_t i = 0; i < j.arms.size(); ++i)
        if (j.cls == NodeClass::Merge ? j.arms[i].outbound_count() > 0 : j.arms[i].inbound_count() > 0) return static_cast<int>(i);
    return -1;
}

void build_slip(Net& n, const char* main_type, bool merge, double sep, double len) {
    const auto road = [&](double deg, const char* type, bool inbound) {
        DrawRoadSpec s;
        s.road_type = type;
        s.one_way = true;
        RoadEnd c, far;
        far.pos = {len * std::cos(deg * kDeg), len * std::sin(deg * kDeg), 0};
        c.pos = {0, 0, 0};
        c.existing_node = n.centre;
        s.start = inbound ? far : c;
        s.end = inbound ? c : far;
        REQUIRE(n.st.execute(make_draw_road(s)));
        if (!n.centre.valid()) {
            const Edge& e = n.g.edges().rbegin()->second;
            n.centre = inbound ? e.b : e.a;
        }
    };
    const bool north = (n.region->driving_side == DrivingSide::Left) == merge;  // nearside of the travel direction
    const double slip = north ? 180 - sep : 180 + sep;
    road(180, main_type, merge);
    road(slip, "local", merge);
    road(0, main_type, !merge);
}

}  // namespace

TEST_CASE("testland merge/diverge: slip joins and leaves the nearside lane of a right-hand carriageway, with testland's gore paint") {
    for (const char* main_type : {"collector", "expressway_dual"}) {
        for (bool merge : {true, false}) {
            INFO(main_type << (merge ? " merge" : " diverge"));
            Net n;
            build_slip(n, main_type, merge, 25, 300);
            const JunctionSet js = n.junctions();
            const Junction* j = js.find(n.centre);
            REQUIRE(j);
            REQUIRE(j->cls == (merge ? NodeClass::Merge : NodeClass::Diverge));
            CHECK_MESSAGE(!j->degraded, j->degraded_reason);
            const int oi = odd_arm(*j);
            REQUIRE(oi >= 0);
            const JunctionArm& O = j->arms[static_cast<std::size_t>(oi)];
            REQUIRE(O.lanes.size() == 2);
            REQUIRE(j->connectors.size() == 3);
            // right-hand traffic: the nearside lane is planar-right of the travel direction.
            const int near_lane = (n.region->driving_side == DrivingSide::Left) == merge ? 0 : 1;
            int slip_connectors = 0;
            for (const LaneConnector& c : j->connectors) {
                const int pair = merge ? c.arm_from : c.arm_to;
                const int olane = merge ? c.lane_to : c.lane_from;
                CHECK(c.kind == TurnKind::Straight);
                if (j->arms[static_cast<std::size_t>(pair)].lanes.size() == 1) {
                    ++slip_connectors;
                    CHECK(olane == near_lane);
                }
            }
            CHECK(slip_connectors == 1);
            REQUIRE(j->gore.size() == 1);
            CHECK(area(j->gore[0]) > 1.0);
            // gore paint: testland uses diagonal hatching where both flows run the same way (UK: chevrons)
            const MarkingSet ms = generate_junction_markings(n.g, *n.region, rules(), *j);
            CHECK(ms.warnings.empty());
            CHECK(!pick(ms, MarkingCategory::Hatching).empty());
            CHECK(pick(ms, MarkingCategory::Chevron).empty());
            for (const Marking* m : pick(ms, MarkingCategory::Hatching)) CHECK(ms.style_id(*m) == "fill_pale");
        }
    }
}

// ============================================================================================ 6. layout I/O

namespace {

const char* kLayout = R"({
  "format": "cp-test-layout", "version": 1, "name": "testland tee", "region": "testland", "driving_side": "right",
  "roads": [
    { "type": "arterial", "points": [[-150, 0, 0], [0, 0, 0], [150, 0, 0]] },
    { "type": "local", "points": [[0, 0, 0], [0, 60, 0], [0, 140, 0]] },
    { "type": "collector", "points": [[400, -150, 0], [400, 0, 0], [400, 150, 0]] },
    { "type": "collector", "points": [[250, 0, 0], [400, 0, 0], [550, 0, 0]] }
  ],
  "roundabouts": [ { "centre": [400, 0, 0], "radius": 25 } ],
  "cameras": [ { "name": "c1", "pos": [0, 60, 2.5], "target": [0, -10, 0.5], "fov_deg": 70 } ]
})";

}  // namespace

TEST_CASE("testland layout I/O: a testland layout loads, replays and round-trips through JSON") {
    std::string err;
    auto l = load_layout(kLayout, CP_DATA_DIR, err);
    REQUIRE_MESSAGE(l, err);
    CHECK(l->region->id == "testland");
    CHECK(l->doc.driving_side == DrivingSide::Right);
    CHECK(l->region->driving_side == DrivingSide::Right);
    // the roundabout got the region's own ring type ("type" omitted in the layout)
    bool ring = false;
    for (const auto& [id, e] : l->graph.edges()) ring = ring || e.road_type == "circus_ring";
    CHECK(ring);
    RoundaboutResult rr;
    JunctionSet js = build_plain_junctions(l->graph, *l->region, l->overrides);
    rr = apply_roundabouts(js, l->graph, *l->region, l->overrides);
    REQUIRE(rr.layouts.size() == 1);
    CHECK(rr.layouts[0].ok);
    for (const auto& [id, j] : js.junctions) CHECK_MESSAGE(!j.degraded, j.degraded_reason);
    // JSON round trip
    LayoutDoc a, b;
    std::string e2;
    REQUIRE_MESSAGE(parse_layout(kLayout, a, e2), e2);
    REQUIRE_MESSAGE(parse_layout(layout_to_json(a), b, e2), e2);
    CHECK(b.region == "testland");
    CHECK(b.driving_side == DrivingSide::Right);
    REQUIRE(a.roads.size() == b.roads.size());
    for (std::size_t i = 0; i < a.roads.size(); ++i) {
        CHECK(a.roads[i].type == b.roads[i].type);
        REQUIRE(a.roads[i].points.size() == b.roads[i].points.size());
    }
    CHECK(layout_to_json(a) == layout_to_json(b));
}

TEST_CASE("testland layout I/O: a left-driving layout is refused by the right-hand pack (one setting per save)") {
    std::string err;
    auto l = load_layout(R"({ "region": "testland", "driving_side": "left", "roads": [ { "type": "local", "points": [[0, 0], [100, 0]] } ] })", CP_DATA_DIR, err);
    CHECK_FALSE(l);
    CHECK(err.find("driving_side") != std::string::npos);
}

TEST_CASE("testland layout I/O: crossing roads split when their height difference is under testland's plan_crossing_junction_dz_m (1.4 m)") {
    const auto nodes_for = [](double dz) {
        const std::string text = std::string(R"({ "region": "testland", "driving_side": "right", "roads": [
            { "type": "local", "points": [[-100, 0, 0], [100, 0, 0]] },
            { "type": "local", "points": [[0, -100, )") + std::to_string(dz) + R"(], [0, -20, )" + std::to_string(dz) + R"(], [0, 20, )" + std::to_string(dz) + R"(], [0, 100, )" + std::to_string(dz) + R"(]] } ] })";
        std::string err;
        auto l = load_layout(text, CP_DATA_DIR, err);
        REQUIRE_MESSAGE(l, err);
        return l->graph.nodes().size();
    };
    CHECK(nodes_for(1.2) == 5);  // below 1.4: a junction (UK's 1.0 would leave a grade-separated crossing)
    CHECK(nodes_for(1.6) == 4);  // above: no junction
}

// A layout without "region" silently becomes a UK layout (and a left-driving one).
// LEAK: sim_core/include/cp/layout_io.h:89 (`std::string region = "uk"`), sim_core/src/layout_io.cpp:188 (default "uk")
// and :189 (default driving side "left").
TEST_CASE("layout I/O: a layout without a region id does not silently become UK" * doctest::should_fail()) {
    LayoutDoc d;
    std::string err;
    const bool ok = parse_layout(R"({ "roads": [ { "type": "local", "points": [[0, 0], [100, 0]] } ] })", d, err);
    // Either the parse demands an explicit region (ok == false) or the document does not claim to be UK.
    CHECK((!ok || d.region != "uk"));
}

// ============================================================================================ 7. units

namespace {

const char* kMinimalRules(const char* unit, std::string& storage) {
    storage = std::string(R"({ "version": 1, "speed_unit": ")") + unit +
              R"(", "colours": { "w": { "rgb": [1, 1, 1] } }, "styles": { "s": { "colour": "w", "strands": [ { "width_m": 0.1 } ] } }, "sets": {} })";
    return storage.c_str();
}

}  // namespace

// The two parsers spell the speed units differently: region.json takes "km/h" and "mps" but not "ms",
// marking_rules.json takes "ms" but not "km/h" or "mps". A pack author using one spelling in both files hits an error.
// LEAK (duplicated unit table): sim_core/src/region_pack.cpp:134-137 vs sim_core/src/markings.cpp:325-329.
TEST_CASE("speed unit spellings are accepted identically by region.json and marking_rules.json" * doctest::should_fail()) {
    auto lt = parse_lane_types(R"({"lane_types":[{"id":"driving","users":["car"],"may_park":false,"connects_at_junctions":true,"surface":"asphalt"}]})");
    REQUIRE(lt);
    for (const char* unit : {"mph", "kph", "km/h", "kmh", "ms", "m/s", "mps"}) {
        INFO("unit " << unit);
        const std::string region = std::string(R"({"id":"t","driving_side":"right","speed_unit":")") + unit + R"("})";
        const auto pack = parse_region_pack(*lt, region,
            R"({"road_types":[{"id":"r","two_way":true,"speed_limit":50,"design_speed_kph":50,"half":[{"lane":"driving","width_m":3.0}],"one_way_variant":[{"lane":"driving","width_m":3.0}],"vertical_k":{"crest":10,"sag":9}}]})");
        std::string storage;
        const auto mr = parse_marking_rules(kMinimalRules(unit, storage));
        CHECK_MESSAGE(static_cast<bool>(pack) == static_cast<bool>(mr), "region.json " << (pack ? "accepts" : "rejects") << " but marking_rules.json " << (mr ? "accepts" : "rejects"));
    }
}

TEST_CASE("speed conversion: 50 km/h is 13.89 m/s in every consumer of the unit") {
    const RegionPack& p = base_pack();
    CHECK(p.road_type("collector")->speed_limit == doctest::Approx(13.8889).epsilon(1e-4));
    CHECK(p.road_type("collector")->speed_limit < 50 * 0.44704);  // not read as mph (22.35 m/s)
    // the marking selector converts "speed_max": 50 with the file's unit: a 50 km/h road matches, a 51 km/h one does not
    const MarkingRules& m = rules();
    const BoundaryRule& centre = m.set("town")->boundaries[0];
    MarkCtx ctx;
    ctx.width = 6.2;
    ctx.lanes = 2;
    ctx.speed = 50 * kKph;
    const std::string* s = select_style(centre.style, ctx);
    REQUIRE(s);
    CHECK(*s == "centre_yel_slow");
    ctx.speed = 51 * kKph;
    s = select_style(centre.style, ctx);
    REQUIRE(s);
    CHECK(*s == "centre_yel_fast");
    ctx.speed = 50 * 0.44704;  // 50 mph
    s = select_style(centre.style, ctx);
    REQUIRE(s);
    CHECK(*s == "centre_yel_fast");
}
