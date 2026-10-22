// What the road tool shows, pinned against the prototype's screenshots (docs/progress/2026-10-11-prototype-ui/NN_*.png):
// the HUD words and numbers (cp/tool_hud.h), the colour modes (cp/road_heat.h), the tuning panel (cp/tool_tuning.h),
// the edit tool's road chain, junction-making edits, selection and profile, the finish gestures, snap kinds and
// guides. Each TEST_CASE names the screenshots it pins. Strings are the screenshot's own, with the glyphs the
// prototype printed (middle dot, degree, infinity, check, cross, warning).
#define _CRT_SECURE_NO_WARNINGS
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <functional>
#include <memory>
#include <string>
#include <tuple>
#include <vector>

#include "doctest/doctest.h"
#include "cp/edge_sampling.h"
#include "cp/edit_tool.h"
#include "cp/road_commands.h"
#include "cp/road_heat.h"
#include "cp/road_tool.h"
#include "cp/stroke.h"
#include "cp/tool_hud.h"
#include "cp/tool_tuning.h"

using namespace cp;

namespace {

#define DOT " \xC2\xB7 "
#define DEG "\xC2\xB0"
#define INF_ "\xE2\x88\x9E"
#define CHECK_MARK "\xE2\x9C\x93"
#define CROSS "\xE2\x9C\x95"
#define WARN_SIGN "\xE2\x9A\xA0"

struct Rig {
    std::shared_ptr<const RegionPack> region;
    RoadGraph g;
    CommandStack<RoadGraph> st{g};
    Heightfield hf{{-1000, -1000}, 4.0, 501, 501, 0.0f};

    explicit Rig(std::shared_ptr<const RegionPack> r = nullptr) {
        if (!r) {
            auto p = load_region_pack_by_id(CP_DATA_DIR, "uk");
            REQUIRE_MESSAGE(p, p.error);
            r = std::make_shared<const RegionPack>(*p);
        }
        region = r;
        g.set_region(region);
    }
    EdgeId road(Vec3 a, std::vector<Vec3> interior, Vec3 b, const char* type = "residential", NodeId from = {}, NodeId to = {}) {
        DrawRoadSpec s;
        s.road_type = type;
        s.start.pos = a;
        s.start.existing_node = from;
        s.end.pos = b;
        s.end.existing_node = to;
        s.interior = std::move(interior);
        REQUIRE(st.execute(make_draw_road(s)));
        return g.edges().rbegin()->first;
    }
    // A drawn stroke (junctions made where it meets roads), like the tool commits it.
    void stroke(std::vector<Vec3> pts, const char* type = "residential") {
        StrokeSpec sp;
        sp.points = std::move(pts);
        sp.road_type = type;
        StrokeParams par;
        par.follow_curve = true;
        par.refuse_band = true;
        REQUIRE(st.execute(make_draw_stroke(std::move(sp), par)));
    }
    void set_ground(const std::function<double(double, double)>& f) {
        for (int j = 0; j < hf.ny(); ++j)
            for (int i = 0; i < hf.nx(); ++i)
                hf.set(i, j, static_cast<float>(f(hf.origin().x + i * hf.cell(), hf.origin().y + j * hf.cell())));
    }
};

PointerInput at(double x, double y, double mpp = 1.0) {
    PointerInput in;
    in.ray.origin = {x, y, 800};
    in.ray.dir = {0, 0, -1};
    in.metres_per_pixel = mpp;
    return in;
}
PointerInput through(Vec3 p, double mppm = 0.001) {
    PointerInput in;
    in.ray.origin = {p.x - 300, p.y - 300, p.z + 400};
    in.ray.dir = {300, 300, -400};
    in.metres_per_pixel_per_metre = mppm;
    return in;
}
PointerInput skyward() {
    PointerInput in;
    in.ray.origin = {0, 0, 800};
    in.ray.dir = {0, 0, 1};
    in.metres_per_pixel = 1;
    return in;
}
const Modifiers kFree{true};

std::string f0(double v) {
    char b[48];
    std::snprintf(b, sizeof b, "%.0f", v);
    return b;
}
std::string f1(double v) {
    char b[48];
    std::snprintf(b, sizeof b, "%.1f", v);
    return b;
}
// One decimal, a trailing ".0" dropped (what the advisory prints)
std::string t1(double v) {
    std::string s = f1(v);
    if (s.size() > 2 && s.compare(s.size() - 2, 2, ".0") == 0) s.resize(s.size() - 2);
    return s;
}
bool has_issue(const std::vector<Issue>& v, IssueCode c) {
    return std::any_of(v.begin(), v.end(), [&](const Issue& i) { return i.code == c; });
}
std::vector<std::string> texts(const std::vector<HudLine>& l) {
    std::vector<std::string> o;
    for (const HudLine& x : l) o.push_back(x.text());
    return o;
}

// Writes the engine's output for a screenshot's scenario to <CP_TOOLUI_DUMP>/<name>.txt when that variable is set
// (the hand-in's "render": docs/progress/roadtool3/renders). Does nothing otherwise.
void dump(const std::string& name, const std::vector<std::string>& lines) {
    const char* dir = std::getenv("CP_TOOLUI_DUMP");
    if (!dir || !*dir) return;
    std::ofstream out(std::string(dir) + "/" + name + ".txt", std::ios::binary);
    out << "# sim_core output for the scenario of " << name << ".png (tests/test_tool_ui.cpp; UTF-8, one HUD line per row)\n";
    for (const std::string& l : lines) out << l << "\n";
}
std::vector<std::string> plus(std::vector<std::string> a, const std::vector<std::string>& b) {
    a.insert(a.end(), b.begin(), b.end());
    return a;
}

struct SnapShot {
    std::vector<std::tuple<std::uint64_t, double, double, double>> nodes;
    std::vector<std::tuple<std::uint64_t, std::uint64_t, std::uint64_t, std::string, bool, double, std::size_t>> edges;
    bool operator==(const SnapShot&) const = default;
};
SnapShot snapshot(const RoadGraph& g) {
    SnapShot s;
    for (const auto& [id, n] : g.nodes()) s.nodes.emplace_back(id.value, n.pos.x, n.pos.y, n.pos.z);
    for (const auto& [id, e] : g.edges())
        s.edges.emplace_back(id.value, e.a.value, e.b.value, e.road_type, e.one_way, e.horizontal.length(), e.interior.size());
    return s;
}

}  // namespace

// ---------------------------------------------------------------- HUD lines while drawing

TEST_CASE("HUD (02, 03, 09, 11): header, cursor line, snap off; nothing but the header without a pick") {
    Rig rig;
    rig.set_ground([](double, double) { return 75.9; });
    DrawSession d(rig.g, rig.hf);
    d.set_road_type("residential");
    HudContext ctx;
    // 02 idle: the ray misses the ground, only the header
    DrawPreview none = d.hover(skyward());
    CHECK_FALSE(none.has_pick);
    dump("02_idle", plus(texts(draw_hud(none, ctx)), {std::string("profile panel: ") + profile_empty_text(), std::string("inspect bar: ") + inspect_idle_text()}));
    CHECK(texts(draw_hud(none, ctx)) == std::vector<std::string>{"DRAW" DOT "Residential street" DOT "colour: plain"});
    // 03: free cursor over open ground
    DrawPreview pv = d.hover(at(100, 100));
    const auto lines = draw_hud(pv, ctx);
    dump("03_hover_free", texts(lines));
    CHECK(texts(lines) == std::vector<std::string>{"DRAW" DOT "Residential street" DOT "colour: plain", "cursor z 75.9 m" DOT "snap: free"});
    REQUIRE(lines[0].runs.size() >= 2);
    CHECK(lines[0].runs[0].text == "DRAW");
    CHECK(lines[0].runs[0].bold);
    CHECK_FALSE(lines[0].runs[1].bold);
    // the colour mode names (C key): 29 / 30 headers
    ctx.colour = ColourMode::Curvature;
    CHECK(draw_hud(pv, ctx)[0].text() == "DRAW" DOT "Residential street" DOT "colour: curvature");
    ctx.colour = ColourMode::Gradient;
    CHECK(draw_hud(pv, ctx)[0].text() == "DRAW" DOT "Residential street" DOT "colour: gradient");
    CHECK(next_colour_mode(ColourMode::Plain) == ColourMode::Curvature);
    CHECK(next_colour_mode(ColourMode::Curvature) == ColourMode::Gradient);
    CHECK(next_colour_mode(ColourMode::Gradient) == ColourMode::Plain);
    // 11: Shift held adds an amber "snap off" to the header
    ctx = {};
    ctx.snap_off = true;
    const auto sh = draw_hud(d.hover(at(100, 100), kFree), ctx);
    CHECK(sh[0].text() == "DRAW" DOT "Residential street" DOT "colour: plain" DOT "snap off");
    CHECK(sh[0].runs.back().text == "snap off");
    CHECK(sh[0].runs.back().tone == HudTone::Warning);
}

TEST_CASE("HUD (11): span, bearing, R at cursor, min R, grade, max, G2, total, exactly as the screenshot prints them") {
    Rig rig;
    // 72.6 m at the first point falling 1.9% along a bearing of 101 degrees: "grade -1.9% · max 1.9% · total 46 m"
    const double dx = std::sin(101 * kPi / 180), dy = std::cos(101 * kPi / 180);
    rig.set_ground([&](double x, double y) { return 72.6 - 0.019 * (x * dx + y * dy); });
    DrawSession d(rig.g, rig.hf);
    d.set_road_type("residential");
    REQUIRE(d.click(at(0, 0), kFree).added);
    HudContext ctx;
    ctx.snap_off = true;
    const DrawPreview pv = d.hover(at(45.9 * dx, 45.9 * dy), kFree);
    dump("11_shift_snap_off", texts(draw_hud(pv, ctx)));
    CHECK(texts(draw_hud(pv, ctx)) ==
          std::vector<std::string>{"DRAW" DOT "Residential street" DOT "colour: plain" DOT "snap off", "cursor z 71.7 m" DOT "snap: free",
                                   "span 45.9 m" DOT "bearing 101" DEG DOT "R at cursor " INF_ " m" DOT "min R " INF_ " m",
                                   "grade -1.9%" DOT "max 1.9%" DOT "G2 " CHECK_MARK DOT "total 46 m"});
}

TEST_CASE("HUD (10): angle and length snap lands on 15 degrees and 8 m multiples; span 48.0 m, bearing 105") {
    Rig rig;
    const double c15 = std::cos(15 * kPi / 180), s15 = std::sin(15 * kPi / 180);
    rig.set_ground([&](double x, double y) { return 72.5 - 0.025 * (x * c15 - y * s15); });
    DrawSession d(rig.g, rig.hf);
    d.set_road_type("residential");
    REQUIRE(d.click(at(0, 0), kFree).added);
    const DrawPreview pv = d.hover(at(47, -13));
    CHECK(pv.snap == SnapKind::Angle);
    dump("10_snap_angle_length", texts(draw_hud(pv, {})));
    CHECK(texts(draw_hud(pv, {})) ==
          std::vector<std::string>{"DRAW" DOT "Residential street" DOT "colour: plain", "cursor z 71.3 m" DOT "snap: angle",
                                   "span 48.0 m" DOT "bearing 105" DEG DOT "R at cursor " INF_ " m" DOT "min R " INF_ " m",
                                   "grade -2.5%" DOT "max 2.5%" DOT "G2 " CHECK_MARK DOT "total 48 m"});
    // the same cursor with the tuning panel's snap angle at 45: the grid is coarser, so it snaps to due east
    d.params().snap_angle_deg = 45;
    const DrawPreview wide = d.hover(at(47, -13));
    CHECK(wide.cursor.x == doctest::Approx(48));
    CHECK(wide.cursor.y == doctest::Approx(0).epsilon(1e-9).scale(1));
    // and the snap length at 16 m: 48.8 m rounds to 48 (3 x 16)... and 40 m to 48 as well (round to the nearest multiple)
    d.params().snap_angle_deg = 15;
    d.params().snap_length = 16;
    CHECK(d.hover(at(40, -10.7)).readouts.span_length == doctest::Approx(48).epsilon(1e-6));
}

TEST_CASE("Bearing is a compass angle: 0 north, 90 east, 180 south, 270 west, clockwise (04, 10, 11, 13, 14, 15)") {
    struct C {
        double dx, dy;
        const char* word;
        double deg;
    };
    const C cases[] = {{0, 1, "north", 0},          {1, 0, "east", 90},         {0, -1, "south", 180},     {-1, 0, "west", 270},
                       {1, 1, "NE", 45},            {-1, 1, "NW", 315},         {-0.454, 0.891, "333", 333}, {0.259, 0.966, "15", 15},
                       {-0.259, 0.966, "345", 345}, {0.966, -0.259, "105", 105}};
    for (const C& c : cases) {
        CAPTURE(c.word);
        Rig rig;
        DrawSession d(rig.g, rig.hf);
        d.set_road_type("residential");
        REQUIRE(d.click(at(0, 0), kFree).added);
        const double n = std::hypot(c.dx, c.dy);
        const DrawPreview pv = d.hover(at(60 * c.dx / n, 60 * c.dy / n), kFree);
        REQUIRE(pv.ribbons.size() == 1);
        CHECK(std::abs(std::remainder(pv.readouts.heading_deg - c.deg, 360.0)) < 0.2);
        const std::string line = draw_hud(pv, {})[2].text();
        CHECK(line.find("bearing " + f0(pv.readouts.heading_deg) + DEG) != std::string::npos);
    }
}

TEST_CASE("HUD (04, 13): R at cursor is infinity on a natural end; min R is the tightest bend; both print as whole metres") {
    Rig rig;
    DrawSession d(rig.g, rig.hf);
    d.set_road_type("b_road");
    d.click(at(0, 0), kFree);
    d.click(at(80, 0), kFree);
    d.click(at(150, 50), kFree);
    const DrawPreview pv = d.hover(at(220, 50), kFree);
    REQUIRE(pv.ribbons.size() == 1);
    CHECK(std::isinf(pv.readouts.end_radius));   // a free end has zero curvature: R at cursor stays infinite
    CHECK(std::isfinite(pv.readouts.min_radius));
    CHECK(pv.readouts.min_radius > 20);
    const std::string l = draw_hud(pv, {})[2].text();
    dump("04_midstroke", plus(texts(draw_hud(pv, {})), {"profile: " + std::to_string(pv.profile.grades.size()) + " grade labels, title: " + profile_title(pv.profile)}));
    CHECK(l.find("R at cursor " INF_ " m") != std::string::npos);
    CHECK(l.find("min R " + f0(pv.readouts.min_radius) + " m") != std::string::npos);
    // snapped onto a free road end the heading is pinned, the end curvature is not forced to zero
    Rig rig2;
    rig2.road({0, 0, 0}, {}, {100, 0, 0});
    DrawSession d2(rig2.g, rig2.hf);
    d2.set_road_type("residential");
    d2.click(at(140, 60), kFree);
    const DrawPreview e = d2.hover(at(100, 0, 1));
    CHECK(e.snap == SnapKind::End);
    CHECK(draw_hud(e, {})[1].text() == "cursor z 0.0 m" DOT "snap: end");
    CHECK(draw_hud(e, {})[2].text().find("R at cursor ") != std::string::npos);
}

// ---------------------------------------------------------------- errors and advisories

TEST_CASE("Min span (12): no ghost, no profile, no readouts, one red line; the click is refused; the span follows the tuning value") {
    Rig rig;
    rig.set_ground([](double, double) { return 71.6; });
    DrawSession d(rig.g, rig.hf);
    d.set_road_type("residential");
    REQUIRE(d.click(at(0, 0), kFree).added);
    HudContext ctx;
    ctx.snap_off = true;
    const DrawPreview pv = d.hover(at(2.0, 1.0), kFree);
    CHECK_FALSE(pv.ok());
    CHECK(pv.ribbons.empty());          // no ghost
    CHECK_FALSE(pv.profile.valid);      // no profile ("Profile: draw a road ..." text stays)
    CHECK(pv.profile.stations.empty());
    CHECK(has_issue(pv.errors, IssueCode::MinSpan));
    CHECK(texts(draw_hud(pv, ctx)) ==
          std::vector<std::string>{"DRAW" DOT "Residential street" DOT "colour: plain" DOT "snap off", "cursor z 71.6 m" DOT "snap: free",
                                   CROSS " segment shorter than 4 m"});
    CHECK(draw_hud(pv, ctx).back().tone == HudTone::Error);
    dump("12_error_min_span", plus(texts(draw_hud(pv, ctx)), {"ghost ribbons: 0, profile valid: false"}));
    // the click is refused and nothing is added (finish_pick_px is off: a click near the last point is not a finish)
    const ClickResult c = d.click(at(2.0, 1.0), kFree);
    CHECK_FALSE(c.added);
    CHECK_FALSE(c.finished);
    CHECK(has_issue(c.errors, IssueCode::MinSpan));
    CHECK(d.points().size() == 1);
    // the minimum is the tuning panel's "Min segment m"
    ToolTuning t = default_tuning(rig.region.get());
    t.min_span = 6;
    apply_tuning(t, d.params());
    const DrawPreview six = d.hover(at(5.0, 0.0), kFree);
    REQUIRE(!six.errors.empty());
    CHECK(six.errors[0].text == "segment shorter than 6 m");
}

TEST_CASE("Finish gestures (26, 27): the finish-on-last-point click is opt-in; by default a click there is another point or the min-span error") {
    Rig rig;
    DrawSession d(rig.g, rig.hf);
    d.set_road_type("residential");
    CHECK(d.params().finish_pick_px == 0);  // the prototype has no finish-on-last-point cue
    d.click(at(0, 0), kFree);
    d.click(at(80, 0), kFree);
    // 6 m from the last point at 1 m per pixel: inside 10 px, but the prototype just adds a point
    const DrawPreview near_last = d.hover(at(86, 0), kFree);
    CHECK_FALSE(near_last.would_finish);
    dump("26_stroke_3pts_before_finish", plus(texts(draw_hud(near_last, {})), {"finish: Enter / right-click / double-click / snap onto a road; a click near the last point adds a point"}));
    CHECK(near_last.ribbons.size() == 1);
    const ClickResult c = d.click(at(86, 0), kFree);
    CHECK(c.added);
    CHECK_FALSE(c.finished);
    CHECK(d.points().size() == 3);
    // 1.4 m from the last point: the min-span error (screenshot 12), still not a finish
    CHECK(has_issue(d.hover(at(87, 1), kFree).errors, IssueCode::MinSpan));
    // the stroke still ends with Enter / right-click: finish() ends at the last CLICKED point (26 -> 27)
    auto f = d.finish();
    REQUIRE(f.ok());
    REQUIRE(rig.st.execute(std::move(f.command)));
    CHECK(rig.g.edges().size() == 1);
    {
        DrawSession dd(rig.g, rig.hf);
        dd.set_road_type("residential");
        dump("27_finished_road_idle", plus(texts(draw_hud(dd.hover(at(86, 0, 1.0)), {})), {"ring: green, 11 px (end of the finished road)"}));
    }
    // opt in: the click on the last point finishes
    DrawSession d2(rig.g, rig.hf);
    d2.set_road_type("residential");
    d2.params().finish_pick_px = 10;
    d2.click(at(0, 200), kFree);
    d2.click(at(80, 200), kFree);
    CHECK(d2.hover(at(86, 200), kFree).would_finish);
    CHECK(d2.click(at(86, 200), kFree).finished);
}

TEST_CASE("Fold error and radius advisory together (13): red cross line first, amber warning after, the exact words") {
    Rig rig;
    rig.set_ground([](double, double) { return 73.0; });
    DrawSession d(rig.g, rig.hf);
    d.set_road_type("residential");
    d.click(at(0, 0), kFree);
    d.click(at(24, 0), kFree);
    const DrawPreview pv = d.hover(at(19.2, 9.4), kFree);
    REQUIRE(pv.ribbons.size() == 1);
    REQUIRE(has_issue(pv.errors, IssueCode::Fold));
    REQUIRE(has_issue(pv.advisories, IssueCode::RadiusBelowMinimum));
    const auto lines = draw_hud(pv, {});
    REQUIRE(lines.size() >= 6);
    dump("13_error_too_tight", plus(texts(lines), {"ghost colour: red (error)"}));
    const std::string r = f1(pv.readouts.min_radius);
    // residential total width 14.3 m: half-width 7.2 as the screenshot prints it
    CHECK(lines[4].text() == CROSS " too tight: radius " + r + " m < half-width 7.2 m (edge folds)");
    CHECK(lines[4].tone == HudTone::Error);
    CHECK(lines[5].text() == WARN_SIGN " radius " + f0(pv.readouts.min_radius) + " m below comfortable 20 m for Residential street");
    CHECK(lines[5].tone == HudTone::Warning);
    // the ghost goes red on the error, not on an advisory
    CHECK(ghost_colour(pv) == ui_theme().ghost_error);
    // an advisory alone leaves the ghost normal
    Rig rig2;
    DrawSession d2(rig2.g, rig2.hf);
    d2.set_road_type("b_road");
    d2.click(at(0, 0), kFree);
    d2.click(at(40, 0), kFree);
    const DrawPreview adv = d2.hover(at(60, 30), kFree);
    CHECK(adv.errors.empty());
    REQUIRE(has_issue(adv.advisories, IssueCode::RadiusBelowMinimum));
    CHECK(ghost_colour(adv) == ui_theme().ghost);
    dump("14_advisory_radius", plus(texts(draw_hud(adv, {})), {"ghost colour: normal (advisories do not tint)"}));
    CHECK(adv.advisories[0].text == "radius " + f0(adv.readouts.min_radius) + " m below comfortable 180 m for B road / main road");
    // the click is refused on a fold, allowed on an advisory
    CHECK_FALSE(d.click(at(19.2, 9.4), kFree).added);
    CHECK(d2.click(at(60, 30), kFree).added);
}

TEST_CASE("Grade advisory prints the limit as a whole number: 'grade 38.2% above advisory 10%' (14, 15)") {
    {  // residential: limit 10%
        Rig rig;
        rig.set_ground([](double, double y) { return 17.0 + 0.35 * y; });
        DrawSession d(rig.g, rig.hf);
        d.set_road_type("residential");
        d.click(at(0, 0), kFree);
        d.click(at(0, 40), kFree);
        const DrawPreview pv = d.hover(at(0, 80), kFree);
        REQUIRE(has_issue(pv.advisories, IssueCode::GradeAboveAdvisory));
        const Issue& g = *std::find_if(pv.advisories.begin(), pv.advisories.end(), [](const Issue& i) { return i.code == IssueCode::GradeAboveAdvisory; });
        CHECK(g.text == "grade " + t1(pv.readouts.max_grade_pct) + "% above advisory 10%");
        CHECK(draw_hud(pv, {}).back().text() == WARN_SIGN " " + pv.advisories.back().text);
        dump("15_advisory_grade", texts(draw_hud(pv, {})));
    }
    {  // B road: limit 8%
        Rig rig;
        rig.set_ground([](double, double y) { return 70.0 + 0.106 * y; });
        DrawSession d(rig.g, rig.hf);
        d.set_road_type("b_road");
        d.click(at(0, 0), kFree);
        const DrawPreview pv = d.hover(at(0, 99), kFree);
        REQUIRE(has_issue(pv.advisories, IssueCode::GradeAboveAdvisory));
        CHECK(pv.advisories.back().text == "grade 10.6% above advisory 8%");
    }
    {  // motorway 4% and the A road 6%: the same shape of words
        Rig rig;
        rig.set_ground([](double x, double) { return 75.0 + 0.075 * x; });
        DrawSession d(rig.g, rig.hf);
        d.set_road_type("motorway");
        d.click(at(0, 0), kFree);
        const DrawPreview m = d.hover(at(144, 0), kFree);
        CHECK(m.advisories.back().text == "grade 7.5% above advisory 4%");
        d.set_road_type("a_road_dual");
        const DrawPreview a = d.hover(at(144, 0), kFree);
        CHECK(a.advisories.back().text == "grade 7.5% above advisory 6%");
    }
}

TEST_CASE("Bridge and tunnel advisories with the height offset in the cursor line (16, 17)") {
    {  // PgUp 25 m over flat ground: fill beyond the 12 m wall cap becomes a bridge
        Rig rig;
        rig.set_ground([](double, double) { return 33.4; });
        DrawSession d(rig.g, rig.hf);
        d.set_road_type("residential");
        d.set_height_offset(25);
        d.click(at(0, 0), kFree);
        d.set_height_offset(25);
        const DrawPreview pv = d.hover(at(120, 0));
        HudContext ctx;
        ctx.height_offset = d.height_offset();
        const auto lines = draw_hud(pv, ctx);
        CHECK(lines[1].text() == "cursor z 58.4 m (+25 m)" DOT "snap: angle");
        REQUIRE(has_issue(pv.advisories, IssueCode::BridgeNeeded));
        CHECK(lines.back().text() == WARN_SIGN " fill over 12 m wall cap: becomes a bridge");
        dump("16_advisory_bridge", plus(texts(lines), {"profile structure ranges: " + std::to_string(pv.profile.structures.size()) + " (bridge)"}));
        CHECK(lines.back().tone == HudTone::Warning);
        REQUIRE(pv.profile.structures.size() >= 1);
        CHECK(pv.profile.structures[0].kind == StructureKind::Bridge);
    }
    {  // PgDn 22 m: cut beyond the wall cap becomes a tunnel
        Rig rig;
        rig.set_ground([](double, double) { return 65.5; });
        DrawSession d(rig.g, rig.hf);
        d.set_road_type("residential");
        d.set_height_offset(-22);
        d.click(at(0, 0), kFree);
        d.set_height_offset(-22);
        const DrawPreview pv = d.hover(at(0, 144));
        HudContext ctx;
        ctx.height_offset = d.height_offset();
        const auto lines = draw_hud(pv, ctx);
        CHECK(lines[1].text() == "cursor z 43.5 m (-22 m)" DOT "snap: angle");
        REQUIRE(has_issue(pv.advisories, IssueCode::TunnelNeeded));
        CHECK(lines.back().text() == WARN_SIGN " cut over 12 m wall cap: becomes a tunnel");
        dump("17_advisory_tunnel", plus(texts(lines), {"profile structure ranges: " + std::to_string(pv.profile.structures.size()) + " (tunnel)"}));
        REQUIRE(pv.profile.structures.size() >= 1);
        CHECK(pv.profile.structures[0].kind == StructureKind::Tunnel);
    }
    {  // the wall cap is the tuning panel's number, in the words and in the profile title
        Rig base;
        ToolTuning t = default_tuning(base.region.get());
        t.wall_cap = 8;
        Rig rig(tuned_region(*base.region, t));
        rig.set_ground([](double, double) { return 0.0; });
        DrawSession d(rig.g, rig.hf);
        d.set_road_type("residential");
        d.set_height_offset(10);
        d.click(at(0, 0), kFree);
        d.set_height_offset(10);
        const DrawPreview pv = d.hover(at(120, 0), kFree);
        CHECK(pv.advisories.back().text == "fill over 8 m wall cap: becomes a bridge");
        CHECK(profile_title(pv.profile).find("red = over 8 m wall cap (bridge/tunnel)") != std::string::npos);
    }
}

TEST_CASE("Crossing too close vertically: the refusal text is on the HUD and the ghost is red (28)") {
    Rig rig;
    rig.road({-100, 0, 0}, {}, {100, 0, 0});
    rig.set_ground([](double, double) { return 0.0; });
    DrawSession d(rig.g, rig.hf);
    d.set_road_type("residential");
    d.set_height_offset(2.6);
    d.click(at(0, -80), kFree);
    d.set_height_offset(2.6);
    const DrawPreview pv = d.hover(at(0, 80), kFree);
    REQUIRE(has_issue(pv.errors, IssueCode::Crossing));
    CHECK(pv.ribbons.size() == 1);  // the ghost is shown, red
    CHECK(ghost_colour(pv) == ui_theme().ghost_error);
    const auto lines = draw_hud(pv, {});
    bool found = false;
    for (const HudLine& l : lines)
        if (l.text() == CROSS " crossing too close vertically (2.6 m): raise/lower to junction or clear 5.3 m") found = true;
    CHECK(found);
    dump("28_error_crossing_too_close", plus(texts(lines), {"ghost colour: red (error), ghost ribbons: 1"}));
    CHECK_FALSE(d.click(at(0, 80), kFree).added);
    // the refusal is still on the HUD after the next mouse move (it vanished with the next move in the prototype)
    const DrawPreview later = d.hover(at(0, 79.5), kFree);
    CHECK(has_issue(later.errors, IssueCode::Crossing));
}

// ---------------------------------------------------------------- road types: ghost widths and headers (18-22)

TEST_CASE("Ghost widths and HUD headers per road type (18, 19, 20, 21, 22)") {
    struct T {
        const char* id;
        const char* name;
        double total;   // the prototype's ROAD_TYPES[].total
        std::size_t ribbons;
    };
    const T types[] = {{"residential", "Residential street", 14.3, 1},
                       {"b_road", "B road / main road", 10.8, 1},
                       {"a_road_dual", "A road / dual c'way", 10.8, 2},
                       {"motorway", "Motorway", 17.45, 2}};
    for (const T& t : types) {
        CAPTURE(t.id);
        Rig rig;
        DrawSession d(rig.g, rig.hf);
        d.set_road_type(t.id);
        d.click(at(0, 0), kFree);
        d.click(at(70, 30), kFree);
        const DrawPreview pv = d.hover(at(144, 40), kFree);
        REQUIRE(pv.ribbons.size() == t.ribbons);
        for (const Ribbon& r : pv.ribbons) {
            CHECK(r.width == doctest::Approx(t.total).epsilon(0.005));  // the TOTAL width, shoulders included (not the carriageway)
            CHECK(r.total_width == doctest::Approx(t.total).epsilon(0.005));
            CHECK(r.carriageway_width < r.total_width);
        }
        {
            std::vector<std::string> d_ = texts(draw_hud(pv, {}));
            d_.push_back("ghost ribbons: " + std::to_string(pv.ribbons.size()) + ", width each " + f1(pv.ribbons[0].width) + " m (carriageway " +
                         f1(pv.ribbons[0].carriageway_width) + " m)");
            const std::string id(t.id);
            if (id == "a_road_dual") dump("18_type3_dual_ghost", d_);
            else if (id == "motorway") dump("19_type4_motorway_ghost", d_);
            else if (id == "b_road") dump("22_type2_b_road_ghost", d_);
        }
        CHECK(pv.road_type == t.id);
        CHECK(pv.road_type_name == t.name);   // the HUD name is in the preview: the host looks nothing up
        CHECK(draw_hud(pv, {})[0].text() == std::string("DRAW" DOT) + t.name + DOT "colour: plain");
        const RoadWidths w = road_widths(rig.g, t.id, false);
        CHECK(w.known);
        CHECK(w.total == doctest::Approx(t.total).epsilon(0.005));
    }
    {  // 20: O on a dual type draws ONE carriageway and the header says so, in bold
        Rig rig;
        DrawSession d(rig.g, rig.hf);
        d.set_road_type("a_road_dual");
        d.set_one_way(true);
        d.click(at(0, 0), kFree);
        d.click(at(70, 30), kFree);
        const DrawPreview pv = d.hover(at(144, 40), kFree);
        CHECK(pv.ribbons.size() == 1);
        const HudLine h = draw_hud(pv, {})[0];
        CHECK(h.text() == "DRAW" DOT "A road / dual c'way" DOT "single carriageway" DOT "colour: plain");
        dump("20_type3_single_carriageway_O", plus(texts(draw_hud(pv, {})), {"ghost ribbons: 1"}));
        const auto bold = std::find_if(h.runs.begin(), h.runs.end(), [](const HudRun& r) { return r.bold && r.text == "single carriageway"; });
        CHECK(bold != h.runs.end());
    }
    {  // 21: O on a single type: "one-way" (plain)
        Rig rig;
        DrawSession d(rig.g, rig.hf);
        d.set_road_type("residential");
        d.set_one_way(true);
        d.click(at(0, 0), kFree);
        const DrawPreview pv = d.hover(at(70, 30), kFree);
        CHECK(draw_hud(pv, {})[0].text() == "DRAW" DOT "Residential street" DOT "one-way" DOT "colour: plain");
        dump("21_type1_oneway", texts(draw_hud(pv, {})));
    }
}

TEST_CASE("Dual ghost: refitted through the offset points, as committed; how far it is from the offset of the drawn centreline (18, 19)") {
    // The prototype offset the centreline; sim_core commits two roads fitted through the offset points and draws exactly those.
    // On a gentle curve the two agree to centimetres; on a tight one they differ, and this pins how much.
    struct C {
        const char* name;
        double radius;
        double limit;
    };
    const C cases[] = {{"gentle R~400", 400, 0.15}, {"tight R~60", 60, 0.15}, {"very tight R~25", 25, 0.5}};
    for (const C& c : cases) {
        CAPTURE(c.name);
        Rig rig;
        DrawSession d(rig.g, rig.hf);
        d.set_road_type("a_road_dual");
        const double arc = 60 * kPi / 180;  // points on a circle of the radius, 60 degrees of arc
        std::vector<Vec3> pts;
        for (int i = 0; i < 4; ++i) {
            const double a = arc * i / 3.0;
            pts.push_back({c.radius * std::sin(a), c.radius * (1 - std::cos(a)), 0});
        }
        d.click(at(pts[0].x, pts[0].y), kFree);
        d.click(at(pts[1].x, pts[1].y), kFree);
        d.click(at(pts[2].x, pts[2].y), kFree);
        const DrawPreview pv = d.hover(at(pts[3].x, pts[3].y), kFree);
        REQUIRE(pv.ribbons.size() == 2);
        // the prototype's ghost: the fitted centreline offset by +/- (gap / 2 + total / 2)
        const RoadWidths w = road_widths(rig.g, "a_road_dual", false);
        const double off = rig.region->road_type("a_road_dual")->median_gap / 2 + w.total / 2;
        std::vector<Vec2> xy;
        for (const Vec3& p : pts) xy.push_back(p.xy());
        auto chain = fit_sane_chain(xy);
        REQUIRE(chain.has_value());
        Edge centre;
        centre.road_type = "a_road_dual";
        centre.horizontal = *chain;
        std::vector<double> ss{0}, zs{0};
        for (const Clothoid& sp : chain->spans) {
            ss.push_back(ss.back() + sp.length);
            zs.push_back(0);
        }
        centre.vertical = VerticalProfile::build(ss, zs, rig.g.curve_params_for(centre));
        std::vector<Vec3> left, right;
        for (const EdgeSample& s : sample_offset(centre, off).samples) left.push_back(s.pos);
        for (const EdgeSample& s : sample_offset(centre, -off).samples) right.push_back(s.pos);
        auto dist = [](const Vec3& p, const std::vector<Vec3>& poly) {
            double best = 1e18;
            for (std::size_t i = 0; i + 1 < poly.size(); ++i) {
                const Vec2 a = poly[i].xy(), b = poly[i + 1].xy(), ab = b - a;
                const double l2 = ab.dot(ab);
                const double t = l2 > 0 ? std::clamp((p.xy() - a).dot(ab) / l2, 0.0, 1.0) : 0.0;
                best = std::min(best, (p.xy() - (a + ab * t)).length());
            }
            return best;
        };
        double worst = 0;
        for (const Ribbon& r : pv.ribbons) {
            std::vector<Vec3> line = r.committed;
            line.insert(line.end(), r.hover.begin(), r.hover.end());
            REQUIRE(line.size() > 10);
            for (const Vec3& p : line) worst = std::max(worst, std::min(dist(p, left), dist(p, right)));
        }
        std::printf("[dual ghost vs exact offset of the centreline] %s: worst %.3f m\n", c.name, worst);
        CHECK(worst < c.limit);
    }
}

TEST_CASE("Offset guide at 1x, 2x and 3x the lateral spacing, both sides, and not at 4x (08, 08b)") {
    Rig rig;
    rig.road({-100, 0, 0}, {}, {100, 0, 0});  // residential, total 14.3 -> centres 14.3 apart
    DrawSession d(rig.g, rig.hf);
    d.set_road_type("residential");
    d.click(at(-150, 300), kFree);  // a stroke in progress, far from the road
    const double base = road_widths(rig.g, "residential", false).total;  // two residential roads side by side: half + half
    for (const double sgn : {1.0, -1.0})
        for (int k = 1; k <= 3; ++k) {
            CAPTURE(k);
            CAPTURE(sgn);
            const DrawPreview pv = d.hover(at(0, sgn * base * k + 1.5, 1.0));  // 1.5 m off the exact spacing, inside the 7 px pick
            REQUIRE(pv.snap == SnapKind::Guide);
            CHECK(pv.guide.type == SnapGuide::Type::Offset);
            CHECK(std::abs(pv.guide.offset) == doctest::Approx(base * k).epsilon(1e-9));
            CHECK(pv.cursor.y == doctest::Approx(sgn * base * k).epsilon(1e-6));  // snapped exactly
            REQUIRE(pv.guide.polyline.size() > 10);
            for (const Vec3& p : pv.guide.polyline) CHECK(std::abs(p.y - sgn * base * k) < 1e-3);  // the dashed copy of the road
            CHECK(draw_hud(pv, {})[1].text().find(DOT "snap: guide") != std::string::npos);
            CHECK(ring_style(pv.snap).colour == 0x7ee0ff);
            if (sgn > 0 && (k == 1 || k == 3))
                dump(k == 1 ? "08_snap_parallel_offset_guide" : "08b_snap_offset_guide_3x",
                     plus(texts(draw_hud(pv, {})), {"guide: dashed copy of the road at lateral offset " + f1(pv.guide.offset) + " m (" + std::to_string(k) + " x " + f1(base) + " m), ring cyan"}));
        }
    const DrawPreview four = d.hover(at(0, base * 4 + 1.5, 1.0));
    CHECK(four.snap != SnapKind::Guide);
    // a single carriageway of a dual type is median-aware (20): the spacing uses the median gap
    d.set_road_type("a_road_dual");
    d.set_one_way(true);
    const double a_total = road_widths(rig.g, "a_road_dual", true).total;
    const double gap = rig.region->road_type("a_road_dual")->median_gap;
    const double lat = base / 2 + a_total / 2 + gap / 2;  // road half width + carriageway half width + half the median gap
    const DrawPreview one = d.hover(at(0, lat + 1.0, 1.0));
    CHECK(one.snap == SnapKind::Guide);
}

TEST_CASE("Snap kinds: end, road, extension guide; ring colours and sizes (05, 06, 07)") {
    Rig rig;
    rig.set_ground([](double, double) { return 79.2; });
    rig.road({0, 0, 79.2}, {}, {200, 0, 79.2});
    DrawSession d(rig.g, rig.hf);
    d.set_road_type("residential");
    {  // 05: a free end (14 px pick)
        const DrawPreview pv = d.hover(at(205, 4, 1.0));
        CHECK(pv.snap == SnapKind::End);
        CHECK(draw_hud(pv, {})[1].text() == "cursor z 79.2 m" DOT "snap: end");
        CHECK(ring_style(pv.snap).colour == 0x3fb950);
        CHECK(ring_style(pv.snap).radius_px == 11);
        dump("05_snap_end", plus(texts(draw_hud(pv, {})), {"ring: green, 11 px"}));
    }
    {  // 06: on the road (10 px pick), away from the ends
        const DrawPreview pv = d.hover(at(100, 3, 1.0));
        CHECK(pv.snap == SnapKind::Road);
        CHECK(draw_hud(pv, {})[1].text() == "cursor z 79.2 m" DOT "snap: road");
        dump("06_snap_road", plus(texts(draw_hud(pv, {})), {"ring: green, 11 px"}));
        CHECK(ring_style(pv.snap).colour == 0x3fb950);
    }
    {  // 07: on the straight extension beyond the end: a cyan dashed line from the end to the cursor
        const DrawPreview pv = d.hover(at(260, 3, 1.0));
        CHECK(pv.snap == SnapKind::Guide);
        CHECK(pv.guide.type == SnapGuide::Type::Line);
        CHECK_FALSE(pv.guide.faint);
        CHECK(pv.guide.polyline.front().x == doctest::Approx(200));
        CHECK(pv.guide.polyline.back().x == doctest::Approx(260));
        CHECK(draw_hud(pv, {})[1].text() == "cursor z 79.2 m" DOT "snap: guide");
        dump("07_snap_extension_guide", plus(texts(draw_hud(pv, {})), {"guide: cyan dashed line from the road end (x 200) to the cursor (x 260), ring cyan 11 px"}));
    }
    {  // free ring is white and small
        const DrawPreview pv = d.hover(at(100, 400, 1.0));
        CHECK(pv.snap == SnapKind::Free);
        dump("09_hover_free_second", plus(texts(draw_hud(pv, {})), {"ring: white, 7 px"}));
        CHECK(ring_style(pv.snap).colour == 0xffffff);
        CHECK(ring_style(pv.snap).radius_px == 7);
    }
}

TEST_CASE("Snap kind Node: a junction node (not a free end) is its own kind, with a word, a ring and an auto-finish") {
    Rig rig;
    rig.road({0, 0, 0}, {}, {200, 0, 0});
    rig.stroke({{100, -100, 0}, {100, 0, 0}});  // a T: the node at (100, 0) has three edges
    NodeId t;
    for (const auto& [id, n] : rig.g.nodes())
        if (rig.g.edges_at(id).size() == 3) t = id;
    REQUIRE(t.valid());
    DrawSession d(rig.g, rig.hf);
    d.set_road_type("residential");
    d.click(at(100, 150), kFree);
    const DrawPreview pv = d.hover(at(103, 2, 1.0));
    CHECK(pv.snap == SnapKind::Node);
    CHECK(pv.snap_node == t);
    CHECK(std::string(snap_kind_word(pv.snap)) == "node");
    CHECK(draw_hud(pv, {})[1].text() == "cursor z 0.0 m" DOT "snap: node");
    CHECK(ring_style(pv.snap).colour == 0x3fb950);
    ClickResult c = d.click(at(103, 2, 1.0));
    REQUIRE(c.finished);
    CHECK(c.auto_finished);  // like a road end: the stroke ends on the node
    REQUIRE(rig.st.execute(std::move(c.finish.command)));
    CHECK(rig.g.edges_at(t).size() == 4);
}

// ---------------------------------------------------------------- colour modes (29, 30)

TEST_CASE("Heat map classes: curvature against the road type's radius, gradient against its limit, thresholds from region data (29, 30)") {
    Rig rig;
    const HeatMapRules& h = rig.region->road_tool.heat;
    // B road: comfortable radius 180 m, limit 8%
    const double R = 180;
    CHECK(heat_class_curvature(1 / 89.0, R, h) == HeatClass::Red);       // < 0.5 R
    CHECK(heat_class_curvature(1 / 91.0, R, h) == HeatClass::Amber);     // < R
    CHECK(heat_class_curvature(1 / 179.0, R, h) == HeatClass::Amber);
    CHECK(heat_class_curvature(-1 / 181.0, R, h) == HeatClass::Caution); // < 2 R, sign ignored
    CHECK(heat_class_curvature(1 / 359.0, R, h) == HeatClass::Caution);
    CHECK(heat_class_curvature(1 / 361.0, R, h) == HeatClass::Ok);
    CHECK(heat_class_curvature(0, R, h) == HeatClass::Ok);                // a straight
    CHECK(heat_class_curvature(1 / 5.0, 0, h) == HeatClass::Ok);          // no comfortable radius set
    CHECK(heat_class_gradient(0.121, 0.08, h) == HeatClass::Red);        // > 1.5 x limit
    CHECK(heat_class_gradient(0.119, 0.08, h) == HeatClass::Amber);      // > limit
    CHECK(heat_class_gradient(-0.081, 0.08, h) == HeatClass::Amber);     // sign ignored
    CHECK(heat_class_gradient(0.041, 0.08, h) == HeatClass::Caution);    // > 0.5 x limit
    CHECK(heat_class_gradient(0.039, 0.08, h) == HeatClass::Ok);
    CHECK(heat_class_gradient(0.5, 0, h) == HeatClass::Ok);
    // the four colours of the prototype, as data
    CHECK(heat_colour(HeatClass::Red, h) == 0xe5484d);
    CHECK(heat_colour(HeatClass::Amber, h) == 0xf5a623);
    CHECK(heat_colour(HeatClass::Caution, h) == 0xe8d44d);
    CHECK(heat_colour(HeatClass::Ok, h) == 0x3fb950);
    const auto rgb = heat_rgb(HeatClass::Ok, h);
    CHECK(rgb[0] == doctest::Approx(0x3f / 255.0).epsilon(1e-6));
    CHECK(rgb[1] == doctest::Approx(0xb9 / 255.0).epsilon(1e-6));
}

TEST_CASE("Heat map per vertex on a drawn S-curve road: one value and class per centreline vertex, plain gives none (29, 30)") {
    Rig rig;
    // an S through four clicks on hilly ground: tight bends and steep stretches
    rig.set_ground([](double x, double y) { return 20.0 + 0.09 * x + 6.0 * std::sin(y / 25.0); });
    rig.stroke({{0, 0, 20}, {120, 60, 31}, {220, -20, 25}, {320, 70, 40}}, "b_road");
    REQUIRE(rig.g.edges().size() == 1);
    const Edge& e = rig.g.edges().begin()->second;
    CHECK(heat_vertices(rig.g, e, ColourMode::Plain).empty());
    const auto cv = heat_vertices(rig.g, e, ColourMode::Curvature);
    const auto gv = heat_vertices(rig.g, e, ColourMode::Gradient);
    REQUIRE(cv.size() > 20);
    CHECK(cv.size() == gv.size());
    CHECK(cv.front().s == doctest::Approx(0));
    CHECK(cv.back().s == doctest::Approx(e.horizontal.length()));
    for (std::size_t i = 1; i < cv.size(); ++i) CHECK(cv[i].s > cv[i - 1].s);
    const RoadType* rt = rig.region->road_type("b_road");
    const HeatMapRules& h = rig.region->road_tool.heat;
    bool any_red = false;
    for (const HeatVertex& v : cv) {
        CHECK(v.cls == heat_class_curvature(v.value, rt->min_radius, h));
        CHECK(v.colour == heat_colour(v.cls, h));
        any_red = any_red || v.cls == HeatClass::Red;
    }
    for (const HeatVertex& v : gv) CHECK(v.cls == heat_class_gradient(v.value, rt->advisory_max_grade, h));
    CHECK(any_red);   // the bends are far tighter than 90 m
    {
        auto summary = [&](const std::vector<HeatVertex>& v, ColourMode m) {
            std::size_t n[4] = {0, 0, 0, 0};
            for (const HeatVertex& x : v) ++n[static_cast<int>(x.cls)];
            DrawPreview hp;
            hp.road_type_name = "B road / main road";
            HudContext hc;
            hc.colour = m;
            return plus(texts(draw_hud(hp, hc)), {std::to_string(v.size()) + " vertices: ok " + std::to_string(n[0]) + ", caution " + std::to_string(n[1]) + ", amber " +
                                                      std::to_string(n[2]) + ", red " + std::to_string(n[3]) + " (colours 3fb950 e8d44d f5a623 e5484d)"});
        };
        dump("29_colour_curvature", summary(cv, ColourMode::Curvature));
        dump("30_colour_gradient", summary(gv, ColourMode::Gradient));
    }
    // thresholds are data: loosening them turns the red away
    RegionPack loose = *rig.region;
    loose.road_tool.heat.curvature_red = 0.01;
    loose.road_tool.heat.curvature_amber = 0.02;
    loose.road_tool.heat.curvature_caution = 0.03;
    Rig rig2(std::make_shared<const RegionPack>(loose));
    rig2.set_ground([](double x, double y) { return 20.0 + 0.09 * x + 6.0 * std::sin(y / 25.0); });
    rig2.stroke({{0, 0, 20}, {120, 60, 31}, {220, -20, 25}, {320, 70, 40}}, "b_road");
    const auto cv2 = heat_vertices(rig2.g, rig2.g.edges().begin()->second, ColourMode::Curvature);
    for (const HeatVertex& v : cv2) CHECK(v.cls == HeatClass::Ok);
}

TEST_CASE("Road tool rules parse from region data") {
    auto base = load_region_pack_by_id(CP_DATA_DIR, "uk");
    REQUIRE(base);
    CHECK(base->road_tool.snap_angle_deg == 15);
    CHECK(base->road_tool.snap_length == 8);
    CHECK(base->road_tool.min_span == 4);
    CHECK(base->road_tool.water_level == 14);
    CHECK(base->road_tool.k_multiplier == 1);
    CHECK(base->road_tool.chain_max_turn_deg == 25);
    CHECK(base->road_tool.heat.colour_red == 0xe5484d);
    CHECK(base->road_tool.heat.gradient_red == 1.5);
    CHECK(base->road_tool.heat.curvature_caution == 2.0);
    CHECK(base->road_type("a_road_dual")->hud_name() == "A road / dual c'way");
    CHECK(base->road_type("a_road_dual")->name == "A road / dual carriageway");
    CHECK(base->road_type("b_road")->hud_name() == "B road / main road");  // no short name: the name itself
    // a pack without the section keeps the prototype's numbers
    RegionPack bare;
    CHECK(bare.road_tool.snap_angle_deg == 15);
    CHECK(bare.road_tool.heat.colour_ok == 0x3fb950);
}

// ---------------------------------------------------------------- tuning panel (23)

TEST_CASE("Tuning panel (23): eight fields, labels, ranges and steps as the screenshot; defaults from the region; each value lands where it acts") {
    const auto& f = tuning_fields();
    const char* labels[] = {"Snap angle \xC2\xB0", "Snap length m", "Wall cap m (bridge/tunnel)", "Wall start m",
                            "Batter (H per V)",    "Vert. curve K \xC3\x97", "Min segment m", "Water level m"};
    for (std::size_t i = 0; i < 8; ++i) CHECK(std::string(f[i].label) == labels[i]);
    CHECK(f[0].min == 5);    CHECK(f[0].max == 45);  CHECK(f[0].step == 5);
    CHECK(f[1].min == 1);    CHECK(f[1].max == 50);  CHECK(f[1].step == 1);
    CHECK(f[2].min == 3);    CHECK(f[2].max == 30);
    CHECK(f[3].min == 2);    CHECK(f[3].max == 12);
    CHECK(f[4].min == 1);    CHECK(f[4].max == 4);   CHECK(f[4].step == doctest::Approx(0.1));
    CHECK(f[5].min == 0.25); CHECK(f[5].max == 2);   CHECK(f[5].step == doctest::Approx(0.05));
    CHECK(f[6].min == 1);    CHECK(f[6].max == 20);
    CHECK(f[7].min == 0);    CHECK(f[7].max == 60);  CHECK(f[7].step == 0.5);

    Rig rig;
    const ToolTuning t0 = default_tuning(rig.region.get());
    {
        std::vector<std::string> rows;
        for (const TuningField& fd : f) rows.push_back(std::string(fd.label) + "  " + tuning_value_text(fd, t0.*fd.member) + "   [" + tuning_value_text(fd, fd.min) + " .. " + tuning_value_text(fd, fd.max) + ", step " + tuning_value_text(fd, fd.step) + "]");
        dump("23_tuning_panel_open", rows);
    }
    // the screenshot's slider values: 15, 8, 12, 12, 2, 1, 4, 14
    CHECK(t0.snap_angle == 15);
    CHECK(t0.snap_length == 8);
    CHECK(t0.wall_cap == 12);
    CHECK(t0.wall_start == 12);   // "Wall start" sits at the end of its range in the prototype
    CHECK(t0.batter == 2);
    CHECK(t0.k_multiplier == 1);
    CHECK(t0.min_span == 4);
    CHECK(t0.water_level == 14);
    CHECK(tuning_value_text(f[4], t0.batter) == "2");
    CHECK(tuning_value_text(f[5], 0.55) == "0.55");
    CHECK(default_tuning(nullptr) == t0);   // no region: the prototype's numbers
    // clamped to the slider's range and grid
    ToolTuning wild = t0;
    wild.snap_angle = 17;       // grid of 5 from 5: 15
    wild.wall_start = 40;       // max 12
    wild.k_multiplier = 0.01;   // min 0.25
    wild.water_level = 14.3;    // grid 0.5: 14.5 (nearest)
    const ToolTuning c = clamped(wild);
    CHECK(c.snap_angle == 15);
    CHECK(c.wall_start == 12);
    CHECK(c.k_multiplier == 0.25);
    CHECK(c.water_level == 14.5);

    // tool-side values go to the sessions
    ToolTuning t = t0;
    t.snap_angle = 30;
    t.snap_length = 16;
    t.min_span = 9;
    t.water_level = 22;
    DrawParams dp;
    apply_tuning(t, dp);
    CHECK(dp.snap_angle_deg == 30);
    CHECK(dp.snap_length == 16);
    CHECK(dp.min_span == 9);
    REQUIRE(dp.water_level);
    CHECK(*dp.water_level == 22);
    EditParams ep;
    apply_tuning(t, ep);
    CHECK(ep.min_span == 9);
    REQUIRE(ep.water_level);
    CHECK(*ep.water_level == 22);

    // region-side values go into a tuned copy; the base is untouched
    t = t0;
    t.wall_cap = 8;
    t.wall_start = 6;
    t.batter = 3;
    t.k_multiplier = 2;
    const auto tuned = tuned_region(*rig.region, t);
    CHECK(tuned->grading.wall_cap == 8);
    CHECK(tuned->grading.wall_start == 6);
    CHECK(tuned->grading.batter == 3);
    CHECK(tuned->road_type("b_road")->vertical_k_crest == doctest::Approx(2 * rig.region->road_type("b_road")->vertical_k_crest));
    CHECK(tuned->road_type("b_road")->vertical_k_sag == doctest::Approx(2 * rig.region->road_type("b_road")->vertical_k_sag));
    CHECK(rig.region->grading.wall_cap == 12);
    CHECK(rig.region->road_type("b_road")->vertical_k_crest == 30);
    // applying the default again restores the K values (it scales the BASE pack's values)
    const auto back = tuned_region(*rig.region, t0);
    CHECK(back->road_type("b_road")->vertical_k_crest == 30);
}

TEST_CASE("Tuning panel (23): the K multiplier lengthens the vertical curves; the water level reaches the profile") {
    auto build = [](double k, double* curve_len, double* water) {
        Rig base;
        ToolTuning t = default_tuning(base.region.get());
        t.k_multiplier = k;
        Rig rig(tuned_region(*base.region, t));
        rig.set_ground([](double x, double) { return x < 100 ? 40.0 + 0.005 * x : 40.5 - 0.005 * (x - 100); });
        DrawSession d(rig.g, rig.hf);
        d.set_road_type("b_road");
        d.params().water_level = 41.0;
        d.click(at(0, 0), kFree);
        d.click(at(100, 0), kFree);
        const DrawPreview pv = d.hover(at(200, 0), kFree);
        REQUIRE(pv.profile.valid);
        REQUIRE(pv.profile.curves.size() == 3);
        *curve_len = pv.profile.curves[1].length;
        *water = pv.profile.stations.front().water_depth;
        REQUIRE(pv.profile.water_level);
        CHECK(*pv.profile.water_level == 41.0);
    };
    double a = 0, b = 0, wa = 0, wb = 0;
    build(1.0, &a, &wa);
    build(2.0, &b, &wb);
    CHECK(a > 0);
    CHECK(b == doctest::Approx(2 * a).epsilon(0.02));
    CHECK(wa == doctest::Approx(1.0).epsilon(0.01));  // 41 m water over ground at 40 m
}

// ---------------------------------------------------------------- profile panel text and axes

TEST_CASE("Profile panel text and axes (04, 12, 16): title, empty text, tick steps and padding exactly as the panel computed them") {
    CHECK(std::string(profile_empty_text()) == "Profile: draw a road, or select one in edit mode (Tab). P hides this panel.");
    ProfileSeries p;
    p.valid = true;
    p.length = 224;
    p.wall_cap = 12;
    p.z_min = 75.9;
    p.z_max = 87.4;
    CHECK(profile_title(p) ==
          "Profile" DOT "224 m" DOT "vs original ground" DOT "blue = fill, orange = cut, red = over 12 m wall cap (bridge/tunnel)" DOT "yellow = vertical curves");
    const ProfileAxes a = profile_axes(p);
    // 04: x labels 0 50 100 150 200; y labels 75 80 85 90
    CHECK(a.x_step == 50);
    CHECK(a.x_ticks == std::vector<double>{0, 50, 100, 150, 200});
    CHECK(a.z_step == 5);
    CHECK(a.z_ticks == std::vector<double>{75, 80, 85, 90});
    CHECK(profile_z_label(75) == "75 m");
    CHECK(profile_x_label(150) == "150");
    // 16: 432 m long, 3..66 m: labels 0 50 ... 400 and 0 20 40 60
    p.length = 432;
    p.z_min = 3;
    p.z_max = 66;
    const ProfileAxes b = profile_axes(p);
    CHECK(b.x_step == 50);
    CHECK(b.x_ticks.back() == 400);
    CHECK(b.z_step == 20);
    CHECK(b.z_ticks == std::vector<double>{0, 20, 40, 60});
    CHECK(profile_z_label(b.z_ticks[0]) == "0 m");  // not "-0 m"
    // 11 / 12: a 48 m road: x labels 0 10 20 30 40; y labels 68 70 72 74 76
    p.length = 48;
    p.z_min = 71.5;
    p.z_max = 72.6;
    const ProfileAxes c = profile_axes(p);
    CHECK(c.x_step == 10);
    CHECK(c.x_ticks == std::vector<double>{0, 10, 20, 30, 40});
    CHECK(c.z_step == 2);
    CHECK(c.z_ticks == std::vector<double>{68, 70, 72, 74, 76});
    // bands
    CHECK(profile_band(5, 12) == ProfileBand::Fill);
    CHECK(profile_band(-5, 12) == ProfileBand::Cut);
    CHECK(profile_band(12.5, 12) == ProfileBand::OverCap);
    CHECK(profile_band(-30, 12) == ProfileBand::OverCap);
}

TEST_CASE("Profile series of a drawn stroke: grade labels with sign, amber over the limit, vertical curves (04, 14)") {
    Rig rig;
    rig.set_ground([](double x, double) { return 75.0 + 0.09 * x; });
    DrawSession d(rig.g, rig.hf);
    d.set_road_type("b_road");
    d.click(at(0, 0), kFree);
    d.click(at(60, 0), kFree);
    const DrawPreview pv = d.hover(at(120, 20), kFree);
    REQUIRE(pv.profile.valid);
    CHECK(pv.profile.max_grade_pct == doctest::Approx(8.0));
    REQUIRE(pv.profile.grades.size() == 2);
    CHECK(pv.profile.grades[0].label.front() == '+');
    CHECK(pv.profile.grades[0].label.back() == '%');
    CHECK(pv.profile.grades[0].over_limit);   // 9% > 8%: drawn amber
    CHECK(pv.profile.curves.size() == 3);
    CHECK(pv.profile.curves.front().length == 0);   // no vertical curve at an end
    CHECK(pv.profile.curves.back().length == 0);
}

// ---------------------------------------------------------------- help and inspect bar text (01, 02, 43, 44)

TEST_CASE("Help overlay (01) and inspect bar (02, 43, 44) words") {
    const auto rows = help_rows({"Residential", "B road", "Dual carriageway", "Motorway"});
    REQUIRE(rows.size() == 14);
    CHECK(rows[0].keys == "Left click");
    CHECK(rows[0].meaning.find("Add a point (the clothoid refits through all points).") == 0);
    CHECK(rows[1].keys == "Double-click / Enter / right-click (no drag)");
    CHECK(rows[1].meaning == "Finish road");
    CHECK(rows[5].keys == "1 2 3 4");
    CHECK(rows[5].meaning == "Residential" DOT "B road" DOT "Dual carriageway" DOT "Motorway (dual types: one stroke places two one-way carriageways)");
    CHECK(rows[8].keys == "Tab");
    CHECK(rows[13].keys == "H");
    CHECK(rows[13].meaning == "Toggle this help");
    CHECK(std::string(help_title()) == "Road Draw 3D prototype");
    CHECK(std::string(help_paragraph()).find("retaining walls where cut/fill exceeds \"wall start\"") != std::string::npos);
    CHECK(std::string(inspect_idle_text()) == "or drop a file here" DOT "J = connectors");
    CHECK(inspect_status_text("interchange.json", 19, 12, 11, 0, 54, 0, true) ==
          "interchange.json: 19 roads, 12 junctions, 11 bridge / 0 tunnel samples, 54 connectors (0 disabled)" DOT "J connectors on");
    CHECK(inspect_status_text("interchange.json", 19, 12, 11, 0, 54, 0, false).find("J connectors off") != std::string::npos);
    dump("43_inspect_scene_loaded_connectors", {inspect_status_text("interchange.json", 19, 12, 11, 0, 54, 0, true)});
    dump("44_inspect_connectors_off", {inspect_status_text("interchange.json", 19, 12, 11, 0, 54, 0, false)});
    {
        std::vector<std::string> hr;
        for (const HelpRow& r : rows) hr.push_back(r.keys + "  |  " + r.meaning);
        hr.push_back(help_paragraph());
        dump("01_idle_help", hr);
    }
    CHECK(ui_theme().turn_left == 0x4aa3ff);
    CHECK(ui_theme().turn_disabled == 0x8a8f98);
    CHECK(ui_theme().edit_selected_line == 0xffd866);
    CHECK(ui_theme().handle_hover_px == 10);
}

// ---------------------------------------------------------------- edit mode: HUD, selection, drag, delete (31-36)

namespace {
struct EditRig : Rig {
    EdgeId road_id;
    EditRig() {
        set_ground([](double, double) { return 78.0; });
        road_id = road({0, 0, 76.1}, {{103, 0, 77.7}}, {200, 0, 79.2});
    }
};
}  // namespace

TEST_CASE("Edit mode (31-36): hover lights the road, selection says 'selected road #N . L m . ...', handles, drag, delete leaves no profile") {
    EditRig rig;
    EditSession es(rig.g, rig.hf);
    HudContext ctx;
    // 31: hover over the road: its profile shows, no selection line
    const EditPreview hv = es.hover(at(60, 1.5));
    CHECK(hv.hover.kind == EditHoverKind::Road);
    CHECK(hv.hover_chain == std::vector<EdgeId>{rig.road_id});
    CHECK(hv.profile.valid);
    CHECK(hv.profile_edge == rig.road_id);
    CHECK_FALSE(hv.selection.valid);
    dump("31_edit_hover_road", plus(texts(edit_hud(hv, ctx, "Residential street")), {"hovered road lit: " + std::to_string(hv.hover_chain.size()) + " edge, profile of the hovered road: valid"}));
    CHECK(texts(edit_hud(hv, ctx, "Residential street")) == std::vector<std::string>{"EDIT" DOT "Residential street" DOT "colour: plain"});
    // 32: select it: the HUD line with the whole road's length; three handles (ends and the interior point)
    const EditHover clicked = es.click(at(60, 1.5));
    CHECK(clicked.kind == EditHoverKind::Road);
    CHECK(es.selected() == rig.road_id);
    const EditPreview sel = es.hover(at(60, 300));
    CHECK(sel.selection.valid);
    CHECK(sel.selection.number == static_cast<int>(rig.road_id.value));
    CHECK(sel.selection.length == doctest::Approx(200).epsilon(0.01));
    CHECK(sel.selection.road_type_name == "Residential street");
    CHECK(texts(edit_hud(sel, ctx, "Residential street")) ==
          std::vector<std::string>{"EDIT" DOT "Residential street" DOT "colour: plain",
                                   "selected road #" + std::to_string(rig.road_id.value) + DOT "200 m" DOT "drag handles, PgUp/PgDn height, Del delete"});
    dump("32_edit_selected_handles", plus(texts(edit_hud(sel, ctx, "Residential street")), {"handles: 3 (two ends, one interior point)"}));
    const auto handles = es.handles();
    REQUIRE(handles.size() == 3);
    CHECK(handles[0].is_node());
    CHECK_FALSE(handles[1].is_node());
    CHECK(handles[2].is_node());
    for (std::size_t i = 0; i < 3; ++i) CHECK(handles[i].position == i);
    // 33: hover a handle
    const EditPreview hh = es.hover(through({103, 0, 77.7 + 0.5}));
    CHECK(hh.hover.kind == EditHoverKind::Handle);
    CHECK(hh.hover.handle == 1);
    dump("33_edit_handle_hover", plus(texts(edit_hud(hh, ctx, "Residential street")), {"hover: handle 1 of the selected road (gold, 10 px)"}));
    // 34: drag it: the road as it will be, a live profile, no commit yet
    REQUIRE(es.begin_drag(through({103, 0, 78.2})));
    const EditPreview dr = es.drag_to(at(103, 40));
    CHECK(dr.dragging);
    CHECK(dr.ok());
    CHECK(dr.ribbons.size() == 1);
    CHECK(dr.profile.valid);
    CHECK(dr.profile.length > 200);   // longer than the straight road
    dump("34_edit_handle_dragging", plus(texts(edit_hud(dr, ctx, "Residential street")), {"drag preview: ghost ribbon 1, live profile length " + f0(dr.profile.length) + " m, graph untouched"}));
    CHECK(rig.g.edges().begin()->second.interior[0].y == 0);   // the graph is untouched
    // 35: release: one command
    EditResult r = es.end_drag(at(103, 40));
    REQUIRE(r.ok());
    CHECK_FALSE(r.made_junction);
    REQUIRE(rig.st.execute(std::move(r.command)));
    es.invalidate();
    const EditPreview after = es.hover(at(60, 300));
    CHECK(after.selection.valid);
    CHECK(after.selection.length > 200);   // the HUD length follows the edit (206 m in the screenshot)
    CHECK(after.profile.valid);
    dump("35_edit_after_drag", texts(edit_hud(after, ctx, "Residential street")));
    // 36: delete: the road goes, and so does its profile (the prototype left a stale one; the tool shows none)
    EditResult del = es.delete_selected();
    REQUIRE(del.ok());
    REQUIRE(rig.st.execute(std::move(del.command)));
    es.invalidate();
    CHECK_FALSE(es.selected().valid());
    const EditPreview gone = es.hover(at(60, 300));
    CHECK_FALSE(gone.profile.valid);
    CHECK_FALSE(gone.profile_edge.valid());
    CHECK(gone.profile.stations.empty());
    CHECK_FALSE(gone.selection.valid);
    dump("36_edit_after_delete", plus(texts(edit_hud(gone, ctx, "Residential street")), {"profile: none (valid = false)"}));
    CHECK(texts(edit_hud(gone, ctx, "Residential street")) == std::vector<std::string>{"EDIT" DOT "Residential street" DOT "colour: plain"});
    // and an undo brings the road and its profile back
    REQUIRE(rig.st.undo());
    es.invalidate();
    es.select(rig.road_id);
    CHECK(es.hover(at(60, 300)).profile.valid);
}

TEST_CASE("Edit: a free end dropped on another road joins it, as a drawn end does; refused edits print as red HUD lines (36)") {
    Rig rig;
    rig.road({0, 100, 0}, {}, {200, 100, 0});
    const EdgeId b = rig.road({100, -100, 0}, {}, {100, 40, 0});
    const SnapShot before = snapshot(rig.g);
    EditSession es(rig.g, rig.hf);
    es.select(b);
    REQUIRE(es.begin_drag(through({100, 40, 0.5})));
    const EditPreview pv = es.drag_to(at(100, 100.4));
    CHECK(pv.ok());
    EditResult r = es.end_drag(at(100, 100.4));
    REQUIRE(r.ok());
    CHECK(r.made_junction);
    CHECK(std::abs(r.anchor.y - 100) > 3);   // the anchor is on the new road, not on the road it joined
    REQUIRE(rig.st.execute(std::move(r.command)));
    es.invalidate();
    REQUIRE(es.select_at(r.anchor));
    {   // the new road is selected (its far end is at y = -100), not a half of the road it joined
        const Edge* sel = rig.g.edge(es.selected());
        REQUIRE(sel != nullptr);
        CHECK((std::abs(rig.g.node(sel->a)->pos.y + 100) < 1 || std::abs(rig.g.node(sel->b)->pos.y + 100) < 1));
    }
    CHECK(rig.g.edges().size() == 3);   // the other road is split, a T of three arms
    std::size_t three = 0;
    for (const auto& [id, n] : rig.g.nodes()) three += rig.g.edges_at(id).size() == 3;
    CHECK(three == 1);
    REQUIRE(rig.st.undo());
    CHECK(snapshot(rig.g) == before);
    // a refused release (onto its own other end: no sane curve) comes back as a reason the host prints as a red line
    es.invalidate();
    es.select(b);
    REQUIRE(es.begin_drag(through({100, 40, 0.5})));
    es.drag_to(at(100, -100));
    const EditResult bad = es.end_drag(at(100, -100));
    CHECK_FALSE(bad.ok());
    const HudLine line = error_hud_line(bad.error);
    CHECK(line.text() == CROSS " " + bad.error);
    CHECK(line.tone == HudTone::Error);
    CHECK(warning_hud_line("x").text() == WARN_SIGN " x");
}

TEST_CASE("Edit hover reports nodes and interior points but names the road to light up (31, 33)") {
    EditRig rig;
    EditSession es(rig.g, rig.hf);
    // over the interior point of an unselected road: kind InteriorPoint, but the host lights the ROAD
    const EditPreview ip = es.hover(at(103, 0.5));
    CHECK(ip.hover.kind == EditHoverKind::InteriorPoint);
    CHECK(ip.hover_chain == std::vector<EdgeId>{rig.road_id});
    const EditPreview nd = es.hover(at(1, 0.5));
    CHECK(nd.hover.kind == EditHoverKind::Node);
    CHECK(nd.hover_chain == std::vector<EdgeId>{rig.road_id});
    const EditPreview away = es.hover(at(100, 300));
    CHECK(away.hover.kind == EditHoverKind::None);
    CHECK(away.hover_chain.empty());
}

TEST_CASE("Edit drag: world_margin keeps a dragged handle inside the heightfield; the margin is a parameter") {
    for (const double margin : {1.0, 12.0}) {
        CAPTURE(margin);
        EditRig rig;
        EditParams p;
        p.world_margin = margin;
        EditSession es(rig.g, rig.hf, p);
        es.select(rig.road_id);
        REQUIRE(es.begin_drag(through({103, 0, 78.2})));
        // the heightfield spans -1000 .. 1000 in x and y
        const EditPreview pv = es.drag_to(at(5000, 300));
        CHECK(pv.cursor.x == doctest::Approx(1000 - margin));
        CHECK(pv.cursor.y == doctest::Approx(300));
        const EditPreview lo = es.drag_to(at(-9000, -7000));
        CHECK(lo.cursor.x == doctest::Approx(-1000 + margin));
        CHECK(lo.cursor.y == doctest::Approx(-1000 + margin));
        es.cancel_drag();
    }
    {  // a committed far drag of the free east end lands on the clamp too
        EditRig rig;
        EditSession es(rig.g, rig.hf);
        es.select(rig.road_id);
        REQUIRE(es.begin_drag(through({200, 0, 79.7})));
        es.drag_to(at(1200, 0));
        EditResult r = es.end_drag(at(1200, 0));
        REQUIRE(r.ok());
        REQUIRE(rig.st.execute(std::move(r.command)));
        double max_x = -1e9;
        for (const auto& [id, n] : rig.g.nodes()) max_x = std::max(max_x, n.pos.x);
        CHECK(max_x == doctest::Approx(999));
    }
}

// ---------------------------------------------------------------- the road chain: a road through a junction is one road (41, 42)

TEST_CASE("Road chain: a road through a junction selects, profiles, measures and deletes as one road (41, 42)") {
    Rig rig;
    rig.set_ground([](double x, double) { return 40.0 + 0.05 * x; });
    rig.stroke({{0, 0, 40}, {300, 0, 55}});   // road A, one stroke
    REQUIRE(rig.g.edges().size() == 1);
    rig.stroke({{150, -120, 47.5}, {150, 120, 47.5}});        // road B crosses it: both are cut in two at the junction
    REQUIRE(rig.g.edges().size() == 4);
    EditSession es(rig.g, rig.hf);
    // click on the west half of A
    es.click(at(40, 0.5));
    REQUIRE(es.selected().valid());
    const EditSelection s = es.selection();
    REQUIRE(s.valid);
    CHECK(s.chain.edges.size() == 2);                       // both halves
    CHECK(s.length == doctest::Approx(300).epsilon(0.02));
    CHECK(s.chain.length == s.length);
    // the profile is ONE series over the whole road
    const EditPreview pv = es.hover(at(500, 500));
    REQUIRE(pv.profile.valid);
    CHECK(pv.profile.length == doctest::Approx(300).epsilon(0.02));
    CHECK(pv.profile.stations.front().s == 0);
    CHECK(pv.profile.stations.back().s == doctest::Approx(pv.profile.length));
    for (std::size_t i = 1; i < pv.profile.stations.size(); ++i) CHECK(pv.profile.stations[i].s > pv.profile.stations[i - 1].s);
    CHECK(pv.profile.curves.size() == 3);                   // two ends and the junction, the junction once
    CHECK(pv.profile.grades.size() == 2);
    for (const ProfileGrade& g : pv.profile.grades) CHECK(g.label == "+5.0%");
    CHECK(pv.profile.curves[1].s == doctest::Approx(150).epsilon(0.02));
    // handles: end, junction (once), end
    const auto hs = es.handles();
    REQUIRE(hs.size() == 3);
    CHECK(hs[1].is_node());
    CHECK(hs[1].pos.x == doctest::Approx(150));
    // the HUD says the whole length
    const auto lines = edit_hud(pv, {}, "Residential street");
    CHECK(lines.back().text() == "selected road #" + std::to_string(s.number) + DOT "300 m" DOT "drag handles, PgUp/PgDn height, Del delete");
    dump("41_edit_select_viaduct_profile", plus(texts(lines), {"one road of " + std::to_string(s.chain.edges.size()) + " edges; profile length " + f0(pv.profile.length) + " m, " + std::to_string(pv.profile.curves.size()) + " control points, grades " + pv.profile.grades[0].label + " " + pv.profile.grades[1].label}));
    // a handle of the OTHER half can be hovered
    const EditPreview hv = es.hover(through({300, 0, 55.5}));
    CHECK(hv.hover.kind == EditHoverKind::Handle);
    // clicking the other half keeps the same road
    es.click(at(250, 0.5));
    CHECK(es.selection().chain.edges == s.chain.edges);
    // road B is a different road
    es.click(at(150.5, 80));
    CHECK(es.selection().chain.edges.size() == 2);
    CHECK(es.selection().chain.edges != s.chain.edges);
    {
        const EditPreview pb = es.hover(at(500, 500));
        dump("42_edit_select_tunnel_profile", plus(texts(edit_hud(pb, {}, "Residential street")), {"the second road: " + std::to_string(pb.selection.chain.edges.size()) + " edges, profile length " + f0(pb.profile.length) + " m"}));
    }
    // Delete takes the whole road in ONE undo step
    es.click(at(40, 0.5));
    const SnapShot before = snapshot(rig.g);
    EditResult del = es.delete_selected();
    REQUIRE(del.ok());
    REQUIRE(rig.st.execute(std::move(del.command)));
    CHECK(rig.g.edges().size() == 2);   // road B's halves
    REQUIRE(rig.st.undo());
    CHECK(snapshot(rig.g) == before);
    // change type applies to the whole road
    es.invalidate();
    es.click(at(40, 0.5));
    EditResult ct = es.change_type("b_road");
    REQUIRE(ct.ok());
    REQUIRE(rig.st.execute(std::move(ct.command)));
    std::size_t b_roads = 0;
    for (const auto& [id, e] : rig.g.edges()) b_roads += e.road_type == "b_road";
    CHECK(b_roads == 2);
}

TEST_CASE("Road chain: T junctions, sharp bends, other types and the turn limit are not chained wrongly") {
    {  // a T: the through road is one road, the stem is another
        Rig rig;
        rig.stroke({{0, 0, 0}, {300, 0, 0}});
        rig.stroke({{150, -100, 0}, {150, 0, 0}});
        REQUIRE(rig.g.edges().size() == 3);
        for (const auto& [id, e] : rig.g.edges()) {
            const RoadChain c = road_chain_of(rig.g, id, 25);
            const bool stem = std::abs(rig.g.node(e.a)->pos.y + 100) < 1 || std::abs(rig.g.node(e.b)->pos.y + 100) < 1;
            CHECK(c.edges.size() == (stem ? 1u : 2u));
        }
    }
    {  // a Y at about 120 degrees: nothing continues anything
        Rig rig;
        rig.stroke({{0, 0, 0}, {150, 0, 0}});
        rig.stroke({{150, 0, 0}, {225, 130, 0}});
        rig.stroke({{150, 0, 0}, {225, -130, 0}});
        REQUIRE(rig.g.edges().size() == 3);
        for (const auto& [id, e] : rig.g.edges()) CHECK(road_chain_of(rig.g, id, 25).edges.size() == 1);
    }
    {  // same straight line, different types: two roads
        Rig rig;
        rig.stroke({{0, 0, 0}, {150, 0, 0}}, "residential");
        rig.stroke({{150, 0, 0}, {300, 0, 0}}, "b_road");
        REQUIRE(rig.g.edges().size() == 2);
        CHECK(road_chain_of(rig.g, rig.g.edges().begin()->first, 25).edges.size() == 1);
    }
    {  // the turn limit is region data: a 15 degree kink at a junction chains at 25 and not at 5
        Rig rig;
        rig.stroke({{0, 0, 0}, {150, 0, 0}});
        rig.stroke({{150, 0, 0}, {300, 40, 0}});
        rig.stroke({{150, 0, 0}, {150, -120, 0}});   // a side road makes the node a junction
        REQUIRE(rig.g.edges().size() == 3);
        const EdgeId first = rig.g.edges().begin()->first;
        CHECK(road_chain_of(rig.g, first, 25).edges.size() == 2);
        CHECK(road_chain_of(rig.g, first, 5).edges.size() == 1);
    }
    {  // two roads meeting end to end (one continued from the other's end) are two roads, as in the prototype
        Rig rig;
        rig.stroke({{0, 0, 0}, {150, 0, 0}});
        rig.stroke({{150, 0, 0}, {300, 0, 0}});
        REQUIRE(rig.g.edges().size() == 2);
        for (const auto& [id, e] : rig.g.edges()) CHECK(road_chain_of(rig.g, id, 25).edges.size() == 1);
    }
    {  // chain selection off: a click selects the single edge
        Rig rig;
        rig.stroke({{0, 0, 0}, {300, 0, 0}});
        rig.stroke({{150, -100, 0}, {150, 100, 0}});
        EditParams p;
        p.chain_selection = false;
        EditSession es(rig.g, rig.hf, p);
        es.click(at(40, 0.5));
        CHECK(es.selection().chain.edges.size() == 1);
    }
}

TEST_CASE("Road chain: drawn in opposite directions, the profile still runs one way with consistent grades (41)") {
    Rig rig;
    rig.set_ground([](double x, double) { return 40.0 + 0.05 * x; });
    const EdgeId e1 = rig.road({0, 0, 40}, {}, {150, 0, 47.5});            // west to east
    const NodeId mid = rig.g.edge(e1)->b;
    rig.road({300, 0, 55}, {}, {150, 0, 47.5}, "residential", {}, mid);   // east to west, ends at the same node
    rig.road({150, -120, 47.5}, {}, {150, 0, 47.5}, "residential", {}, mid);   // a side road: the node is a junction
    REQUIRE(rig.g.edges().size() == 3);
    EditSession es(rig.g, rig.hf);
    es.select(e1);
    const RoadChain c = es.road_chain(e1);
    REQUIRE(c.edges.size() == 2);
    REQUIRE(c.reversed.size() == 2);
    CHECK(c.reversed[0] != c.reversed[1]);   // one of them runs against the road's direction
    const EditPreview pv = es.hover(at(500, 500));
    REQUIRE(pv.profile.valid);
    CHECK(pv.profile.length == doctest::Approx(300).epsilon(0.02));
    REQUIRE(pv.profile.grades.size() == 2);
    // both runs climb at 5% in the chain direction (the reversed edge's own grade is -5%)
    CHECK(pv.profile.grades[0].label == pv.profile.grades[1].label);
    CHECK(pv.profile.grades[0].label == "+5.0%");
    for (std::size_t i = 1; i < pv.profile.stations.size(); ++i) CHECK(pv.profile.stations[i].s > pv.profile.stations[i - 1].s);
    CHECK(pv.profile.stations.front().road_z == doctest::Approx(40.0));
    CHECK(pv.profile.stations.back().road_z == doctest::Approx(55.0));
    // join_profiles on its own
    ProfileSeries a = profile_of_edge(rig.g, rig.hf, c.edges[0]), b = profile_of_edge(rig.g, rig.hf, c.edges[1]);
    const ProfileSeries j = join_profiles({a, b}, {c.reversed[0], c.reversed[1]});
    CHECK(j.length == doctest::Approx(a.length + b.length));
    CHECK_FALSE(join_profiles({}, {}).valid);
    ProfileSeries bad;
    CHECK_FALSE(join_profiles({a, bad}, {false, false}).valid);
}

TEST_CASE("Edit drag on a road through a junction: the whole road's profile follows the dragged edge (41, 34)") {
    Rig rig;
    rig.set_ground([](double, double) { return 40.0; });
    rig.stroke({{0, 0, 40}, {300, 0, 40}});
    rig.stroke({{150, -120, 40}, {150, 120, 40}});
    EditSession es(rig.g, rig.hf);
    es.click(at(40, 0.5));
    REQUIRE(es.selection().chain.edges.size() == 2);
    const double before = es.hover(at(500, 500)).profile.length;
    REQUIRE(es.begin_drag(through({0, 0, 40.5})));
    const EditPreview dr = es.drag_to(at(0, 60));
    REQUIRE(dr.profile.valid);
    CHECK(dr.profile.length > before);   // the dragged half is longer, the other is unchanged, the series is the sum
    EditResult r = es.end_drag(at(0, 60));
    REQUIRE(r.ok());
    CHECK_FALSE(r.made_junction);
    REQUIRE(rig.st.execute(std::move(r.command)));
    es.invalidate();
    CHECK(es.hover(at(500, 500)).profile.length == doctest::Approx(dr.profile.length).epsilon(1e-6));
}

// ---------------------------------------------------------------- stroke fit

TEST_CASE("fit_clothoid_chain refuses a stroke that doubles back onto an earlier point (no 1e15 m chain), keeps legitimate hairpins") {
    {  // exactly onto the first point
        auto c = fit_clothoid_chain(std::vector<Vec2>{{0, 0}, {80, 0}, {0, 0}});
        CHECK_FALSE(c.has_value());
    }
    {  // through an extra point: the road closes onto its own start
        auto c = fit_clothoid_chain(std::vector<Vec2>{{0, 0}, {80, 0}, {80, 40}, {0, 0}});
        if (c)
            for (const Clothoid& s : c->spans) CHECK(s.length < 1e4);
    }
    {  // near misses never return an absurd chain
        for (const double off : {0.0, 0.05, 0.5, 2.0}) {
            auto c = fit_clothoid_chain(std::vector<Vec2>{{0, 0}, {80, 0}, {0.0, off}});
            if (c)
                for (const Clothoid& s : c->spans) CHECK(s.length < 1e4);
        }
    }
    {  // a real hairpin: a U-turn with room to turn in
        auto c = fit_clothoid_chain(std::vector<Vec2>{{0, 0}, {60, 0}, {60, 40}, {0, 40}});
        REQUIRE(c.has_value());
        for (const Clothoid& s : c->spans) CHECK(s.length < 400);
    }
    {  // fit_sane_chain is the same fit now
        auto a = fit_sane_chain(std::vector<Vec2>{{0, 0}, {80, 0}, {0, 0}});
        CHECK_FALSE(a.has_value());
        auto b = fit_sane_chain(std::vector<Vec2>{{0, 0}, {60, 20}, {120, -10}});
        REQUIRE(b.has_value());
        auto c = fit_clothoid_chain(std::vector<Vec2>{{0, 0}, {60, 20}, {120, -10}});
        REQUIRE(c.has_value());
        CHECK(b->length() == doctest::Approx(c->length()));
    }
}
