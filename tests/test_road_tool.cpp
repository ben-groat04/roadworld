// Road tool: ground pick, road index, stroke resolver, DrawSession scripted click sequences.
#include <algorithm>
#include <cmath>
#include <memory>
#include <string>
#include <tuple>
#include <vector>

#include "doctest/doctest.h"
#include "cp/ground_pick.h"
#include "cp/junction.h"
#include "cp/lanes.h"
#include "cp/road_commands.h"
#include "cp/road_index.h"
#include "cp/road_tool.h"
#include "cp/stroke.h"
#include "cp/test_layouts.h"
#include "portable_rng.h"

using namespace cp;

namespace {

struct Rig {
    std::shared_ptr<const RegionPack> region;
    RoadGraph g;
    CommandStack<RoadGraph> st{g};
    Heightfield hf{{-1000, -1000}, 4.0, 501, 501, 0.0f};

    explicit Rig(bool mirrored = false) {
        auto p = load_region_pack_by_id(CP_DATA_DIR, "uk");
        REQUIRE_MESSAGE(p, p.error);
        RegionPack pack = *p;
        if (mirrored) pack.driving_side = DrivingSide::Right;
        region = std::make_shared<const RegionPack>(pack);
        g.set_region(region);
    }

    EdgeId road(Vec3 a, Vec3 b, const char* type = "residential") {
        DrawRoadSpec s;
        s.road_type = type;
        s.start.pos = a;
        s.end.pos = b;
        REQUIRE(st.execute(make_draw_road(s)));
        return g.edges().rbegin()->first;
    }
    void set_ground(double (*f)(double, double)) {
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
const Modifiers kFree{true};

struct Snap {
    std::vector<std::tuple<std::uint64_t, double, double, double>> nodes;
    std::vector<std::tuple<std::uint64_t, std::uint64_t, std::uint64_t, std::string, bool, double, double, std::size_t>> edges;
    bool operator==(const Snap&) const = default;
};
Snap snap(const RoadGraph& g) {
    Snap s;
    for (const auto& [id, n] : g.nodes()) s.nodes.emplace_back(id.value, n.pos.x, n.pos.y, n.pos.z);
    for (const auto& [id, e] : g.edges())
        s.edges.emplace_back(id.value, e.a.value, e.b.value, e.road_type, e.one_way, e.horizontal.length(), e.vertical.z(e.horizontal.length() * 0.5),
                             e.interior.size());
    return s;
}

// Click a list of free points (exact positions), then finish.
FinishResult draw_free(DrawSession& d, const std::vector<std::pair<double, double>>& pts) {
    for (auto [x, y] : pts) {
        auto r = d.click(at(x, y), kFree);
        REQUIRE(r.added);
    }
    return d.finish();
}

bool has_issue(const std::vector<Issue>& v, IssueCode c) {
    return std::any_of(v.begin(), v.end(), [&](const Issue& i) { return i.code == c; });
}

}  // namespace

// ---------------------------------------------------------------- ground pick

TEST_CASE("pick_ground: flat ground") {
    Heightfield hf({-100, -100}, 5.0, 41, 41, 10.0f);
    auto h = pick_ground(hf, {{5, 5, 100}, {1, 0, -1}});
    REQUIRE(h);
    CHECK(h->point.x == doctest::Approx(95).epsilon(1e-6));
    CHECK(h->point.z == doctest::Approx(10).epsilon(1e-6));
    CHECK(h->distance == doctest::Approx(90 * std::sqrt(2.0)).epsilon(1e-6));
    auto v = pick_ground(hf, {{20, -30, 500}, {0, 0, -3}});  // non-unit direction
    REQUIRE(v);
    CHECK(v->point.x == doctest::Approx(20));
    CHECK(v->point.y == doctest::Approx(-30));
    CHECK(v->point.z == doctest::Approx(10));
}

TEST_CASE("pick_ground: slope, miss, under the ground, grazing, cliff") {
    Heightfield hf({-100, -100}, 4.0, 51, 51, 0.0f);
    for (int j = 0; j < hf.ny(); ++j)
        for (int i = 0; i < hf.nx(); ++i) hf.set(i, j, static_cast<float>(0.2 * (-100 + 4.0 * i)));  // z = 0.2 x
    auto s = pick_ground(hf, {{0, 0, 200}, {0, 0, -1}});
    REQUIRE(s);
    CHECK(s->point.z == doctest::Approx(0).epsilon(1e-6));
    auto s2 = pick_ground(hf, {{50, 0, 200}, {0, 0, -1}});
    REQUIRE(s2);
    CHECK(s2->point.z == doctest::Approx(10).epsilon(1e-5));
    // inclined ray meeting the slope: z = 100 - t, x = t * 0.5 -> 100 - t = 0.1 t
    auto s3 = pick_ground(hf, {{0, 0, 100}, {0.5, 0, -1}});
    REQUIRE(s3);
    CHECK(s3->point.z == doctest::Approx(0.2 * s3->point.x).epsilon(1e-6));

    CHECK_FALSE(pick_ground(hf, {{0, 0, 200}, {0, 0, 1}}));          // pointing away
    CHECK_FALSE(pick_ground(hf, {{5000, 0, 100}, {0, 0, -1}}));      // outside the footprint
    CHECK_FALSE(pick_ground(hf, {{-90, 0, 500}, {1, 0, 0.01}}));     // stays above the terrain
    CHECK_FALSE(pick_ground(hf, {{0, 0, -50}, {0, 0, -1}}));         // starts under the ground
    CHECK_FALSE(pick_ground(Heightfield{}, {{0, 0, 1}, {0, 0, -1}}));

    Heightfield flat({-1000, -1000}, 4.0, 501, 501, 0.0f);
    auto gz = pick_ground(flat, {{-900, 0, 0.5}, {1, 0, -0.001}});   // grazing: ~500 m of travel
    REQUIRE(gz);
    CHECK(gz->point.x == doctest::Approx(-400).epsilon(1e-4));
    CHECK(gz->point.z == doctest::Approx(0).epsilon(1e-6));

    Heightfield cliff({-100, -100}, 4.0, 51, 51, 0.0f);   // 0 for x <= 0, 100 for x >= 4: a 25:1 ramp
    for (int j = 0; j < cliff.ny(); ++j)
        for (int i = 0; i < cliff.nx(); ++i)
            if (-100 + 4.0 * i > 0.5) cliff.set(i, j, 100.0f);
    auto c = pick_ground(cliff, {{-90, 0, 50}, {1, 0, 0}});
    REQUIRE(c);
    CHECK(c->point.z == doctest::Approx(cliff.height(c->point.x, c->point.y)).epsilon(1e-6));
    CHECK(c->point.x > -5);
    CHECK(c->point.x < 5);
}

// ---------------------------------------------------------------- road index

TEST_CASE("RoadIndex: nearest point / node / end against brute force on random graphs") {
    for (std::uint64_t seed = 1; seed <= 4; ++seed) {
        Rig rig;
        PortableRng rng{seed * 7919};
        for (int i = 0; i < 12; ++i) {
            DrawRoadSpec s;
            s.road_type = "residential";
            s.start.pos = {rng.uniform(-300, 300), rng.uniform(-300, 300), rng.uniform(0, 3)};
            s.end.pos = {s.start.pos.x + rng.uniform(40, 150), s.start.pos.y + rng.uniform(-150, 150), rng.uniform(0, 3)};
            s.interior.push_back({(s.start.pos.x + s.end.pos.x) / 2 + rng.uniform(-20, 20), (s.start.pos.y + s.end.pos.y) / 2 + rng.uniform(-20, 20), 1});
            rig.st.execute(make_draw_road(s));
        }
        RoadIndex idx(rig.g);
        CHECK(idx.edge_count() == rig.g.edges().size());
        for (int q = 0; q < 200; ++q) {
            const Vec2 p{rng.uniform(-400, 400), rng.uniform(-400, 400)};
            const double radius = rng.uniform(5, 120);
            double best = 1e18;
            for (const auto& [id, e] : rig.g.edges())
                for (double s = 0; s <= e.horizontal.length(); s += 0.02) best = std::min(best, (edge_point(e, s).xy() - p).length());
            auto hit = idx.nearest_point(p, radius);
            if (best < radius - 0.2) {
                REQUIRE(hit);
                CHECK(hit->distance == doctest::Approx(best).epsilon(0.02).epsilon(0.1));
                CHECK(hit->distance - best < 0.1);
                CHECK((edge_point(*idx.edge(hit->edge), hit->s).xy() - hit->point.xy()).length() < 1e-9);
            } else if (best > radius + 0.2) {
                CHECK_FALSE(hit);
            }
            double nbest = 1e18;
            std::size_t deg1 = 0;
            for (const auto& [id, n] : rig.g.nodes()) nbest = std::min(nbest, (n.pos.xy() - p).length());
            auto nh = idx.nearest_node(p, 1e9);
            REQUIRE(nh);
            CHECK(nh->distance == doctest::Approx(nbest));
            for (const auto& [id, n] : rig.g.nodes()) deg1 += rig.g.edges_at(id).size() == 1;
            CHECK(idx.free_ends().size() == deg1);
            auto fe = idx.nearest_free_end(p, 1e9);
            if (deg1 > 0) {
                REQUIRE(fe);
                double fb = 1e18;
                for (const EndHit& e : idx.free_ends()) fb = std::min(fb, (e.pos.xy() - p).length());
                CHECK(fe->distance == doctest::Approx(fb));
            }
        }
        // level filter: a query far above every road finds nothing
        CHECK_FALSE(idx.nearest_point({0, 0}, 1e6, 500.0, 15));
    }
}

TEST_CASE("RoadIndex: signed lateral distance and free end heading") {
    Rig rig;
    rig.road({0, 0, 0}, {100, 0, 0});
    RoadIndex idx(rig.g);
    auto left = idx.nearest_point({50, 4}, 10);
    auto right = idx.nearest_point({50, -4}, 10);
    REQUIRE(left);
    REQUIRE(right);
    CHECK(left->lateral == doctest::Approx(4).epsilon(1e-6));
    CHECK(right->lateral == doctest::Approx(-4).epsilon(1e-6));
    CHECK(left->s == doctest::Approx(50).epsilon(1e-3));
    CHECK(left->heading == doctest::Approx(0).epsilon(1e-6));
    REQUIRE(idx.free_ends().size() == 2);
    auto end = idx.nearest_free_end({103, 0}, 10);
    REQUIRE(end);
    CHECK(end->out_heading == doctest::Approx(0).epsilon(1e-6));
    auto start = idx.nearest_free_end({-3, 0}, 10);
    REQUIRE(start);
    CHECK(std::abs(std::abs(start->out_heading) - kPi) < 1e-6);
}

// ---------------------------------------------------------------- stroke resolver

TEST_CASE("stroke: crossing verdicts follow the region thresholds") {
    Rig rig;
    rig.road({0, 0, 0}, {200, 0, 0});
    StrokeSpec sp;
    sp.road_type = "residential";
    StrokeParams P;
    P.refuse_band = true;
    P.follow_curve = true;
    auto cross = [&](double z) {
        sp.points = {{100, -60, z}, {100, 60, z}};
        return stroke_crossings(rig.g, sp, P);
    };
    sp.points = {{100, -60, 0.4}, {100, 60, 0.4}};
    CHECK(check_stroke(rig.g, sp, P) == "");
    auto a = cross(0.4);
    REQUIRE(a.size() == 1);
    CHECK(a[0].verdict == CrossingVerdict::Join);
    CHECK(a[0].pos.x == doctest::Approx(100).epsilon(1e-6));
    auto b = cross(3.0);
    REQUIRE(b.size() == 1);
    CHECK(b[0].verdict == CrossingVerdict::Refused);
    CHECK(b[0].dz == doctest::Approx(3.0).epsilon(1e-6));
    auto c = cross(rig.region->headroom_m + 0.1);
    REQUIRE(c.size() == 1);
    CHECK(c[0].verdict == CrossingVerdict::Clear);
    // explicit thresholds override the region
    P.join_dz = 5;
    CHECK(cross(3.0)[0].verdict == CrossingVerdict::Join);
    P.join_dz = -1;
    P.refuse_band = false;
    CHECK(cross(3.0)[0].verdict == CrossingVerdict::Clear);
    // degenerate stroke
    sp.points = {{0, 0, 0}, {0, 0, 0}};
    P.refuse_band = true;
    CHECK(check_stroke(rig.g, sp, P) == "cannot fit curve");
}

TEST_CASE("stroke: one undo step, redo reproduces ids") {
    Rig rig;
    rig.road({0, 0, 0}, {200, 0, 0});
    const Snap before = snap(rig.g);
    StrokeSpec sp;
    sp.road_type = "residential";
    sp.points = {{100, -60, 0}, {100, 60, 0}};
    REQUIRE(rig.st.execute(make_draw_stroke(sp)));
    const Snap after = snap(rig.g);
    CHECK(rig.g.edges().size() == 4);
    CHECK(rig.g.nodes().size() == 5);
    CHECK(rig.st.undo_depth() == 2);
    rig.st.undo();
    CHECK(snap(rig.g) == before);
    rig.st.redo();
    CHECK(snap(rig.g) == after);
}

// ---------------------------------------------------------------- DrawSession

TEST_CASE("DrawSession: straight road") {
    Rig rig;
    DrawSession d(rig.g, rig.hf);
    d.set_road_type("residential");
    auto r = draw_free(d, {{0, 0}, {100, 0}});
    REQUIRE(r.ok());
    CHECK_FALSE(d.active());
    REQUIRE(rig.st.execute(std::move(r.command)));
    REQUIRE(rig.g.edges().size() == 1);
    const Edge& e = rig.g.edges().begin()->second;
    CHECK(e.horizontal.length() == doctest::Approx(100).epsilon(1e-6));
    CHECK(rig.g.nodes().size() == 2);
}

TEST_CASE("DrawSession: S-curve is one fitted edge with an interior point") {
    Rig rig;
    DrawSession d(rig.g, rig.hf);
    d.set_road_type("residential");
    d.click(at(0, 0), kFree);
    d.click(at(60, 30), kFree);
    auto pv = d.hover(at(120, 0), kFree);
    REQUIRE(pv.ok());
    CHECK(pv.ribbons.size() == 1);
    CHECK(pv.ribbons[0].committed.size() >= 2);
    CHECK(pv.ribbons[0].hover.size() >= 2);
    CHECK(pv.ribbons[0].committed.back().x == doctest::Approx(pv.ribbons[0].hover.front().x));
    CHECK(pv.ribbons[0].hover.back().x == doctest::Approx(120).epsilon(1e-6));
    CHECK(pv.readouts.length > 125);
    CHECK(std::isfinite(pv.readouts.min_radius));
    CHECK(pv.readouts.height == doctest::Approx(0));
    CHECK(pv.ribbons[0].width > 5);
    d.click(at(120, 0), kFree);
    auto r = d.finish();
    REQUIRE(r.ok());
    REQUIRE(rig.st.execute(std::move(r.command)));
    const Edge& e = rig.g.edges().begin()->second;
    CHECK(e.interior.size() == 1);
    CHECK(e.horizontal.g2);
    CHECK(e.horizontal.spans.size() == 2);
}

TEST_CASE("DrawSession: snap to a free end auto-finishes tangentially") {
    Rig rig;
    rig.road({0, 0, 0}, {100, 0, 0});
    DrawSession d(rig.g, rig.hf);
    d.set_road_type("residential");
    REQUIRE(d.click(at(200, 60), kFree).added);
    auto pv = d.hover(at(104, 3));
    CHECK(pv.snap == SnapKind::End);
    CHECK(pv.cursor.x == doctest::Approx(100));
    CHECK(pv.cursor.y == doctest::Approx(0));
    auto c = d.click(at(104, 3));
    REQUIRE(c.added);
    REQUIRE(c.auto_finished);
    REQUIRE(c.finish.ok());
    REQUIRE(rig.st.execute(std::move(c.finish.command)));
    CHECK(rig.g.nodes().size() == 3);
    CHECK(rig.g.edges().size() == 2);
    const Edge& ne = rig.g.edges().rbegin()->second;
    REQUIRE(ne.heading_b);
    CHECK(std::abs(wrap_angle(*ne.heading_b - kPi)) < 1e-9);
    CHECK_FALSE(d.active());
}

TEST_CASE("DrawSession: T onto an existing road splits it") {
    Rig rig;
    rig.road({0, 0, 0}, {200, 0, 0});
    DrawSession d(rig.g, rig.hf);
    d.set_road_type("residential");
    d.click(at(100, -80), kFree);
    auto pv = d.hover(at(101, 3));
    CHECK(pv.snap == SnapKind::Road);
    CHECK(pv.snap_edge == rig.g.edges().begin()->first);
    auto c = d.click(at(101, 3));
    REQUIRE(c.auto_finished);
    REQUIRE(rig.st.execute(std::move(c.finish.command)));
    CHECK(rig.g.edges().size() == 3);
    CHECK(rig.g.nodes().size() == 4);
    bool junction = false;
    for (const auto& [id, n] : rig.g.nodes()) junction = junction || rig.g.edges_at(id).size() == 3;
    CHECK(junction);
}

TEST_CASE("DrawSession: crossing at grade joins, level separation does not, the band is refused") {
    {  // at grade
        Rig rig;
        rig.road({0, 0, 0}, {200, 0, 0});
        DrawSession d(rig.g, rig.hf);
        d.set_road_type("residential");
        auto r = draw_free(d, {{100, -60}, {100, 60}});
        REQUIRE(r.ok());
        REQUIRE(rig.st.execute(std::move(r.command)));
        CHECK(rig.g.edges().size() == 4);
        CHECK(rig.g.nodes().size() == 5);
        for (const auto& [id, n] : rig.g.nodes())
            if (rig.g.edges_at(id).size() == 4) {
                CHECK(n.pos.x == doctest::Approx(100).epsilon(1e-3));
                CHECK(n.pos.y == doctest::Approx(0).epsilon(1e-3));
            }
    }
    {  // bridge: 8 m above the existing road clears it
        Rig rig;
        rig.road({0, 0, 0}, {200, 0, 0});
        DrawSession d(rig.g, rig.hf);
        d.set_road_type("residential");
        d.adjust_height(8);
        REQUIRE(d.click(at(100, -60), kFree).added);
        d.adjust_height(8);
        REQUIRE(d.click(at(100, 60), kFree).added);
        auto r = d.finish();
        REQUIRE(r.ok());
        REQUIRE(rig.st.execute(std::move(r.command)));
        CHECK(rig.g.edges().size() == 2);
        CHECK(rig.g.nodes().size() == 4);
    }
    {  // 3 m above: refused with a reason, stroke stays open
        Rig rig;
        rig.road({0, 0, 0}, {200, 0, 0});
        const Snap before = snap(rig.g);
        DrawSession d(rig.g, rig.hf);
        d.set_road_type("residential");
        d.adjust_height(3);
        d.click(at(100, -60), kFree);
        d.adjust_height(3);
        d.click(at(100, 60), kFree);
        auto r = d.finish();
        CHECK_FALSE(r.ok());
        CHECK(r.error.find("crossing too close vertically (3.0 m)") != std::string::npos);
        CHECK(r.error.find("5.3") != std::string::npos);
        CHECK(d.active());
        CHECK(snap(rig.g) == before);
        // lowering the second point to near ground level resolves it
        REQUIRE(d.undo_point());
        d.set_height_offset(0.3);
        d.click(at(100, 60), kFree);
        // first point is still 3 m up: the average crossing level is ~1.65 m, still in the band
        CHECK_FALSE(d.finish().ok());
    }
}

TEST_CASE("DrawSession: modifier turns snapping off") {
    Rig rig;
    rig.road({0, 0, 0}, {100, 0, 0});
    DrawSession d(rig.g, rig.hf);
    d.set_road_type("residential");
    CHECK(d.hover(at(103, 2)).snap == SnapKind::End);
    auto pv = d.hover(at(103, 2), kFree);
    CHECK(pv.snap == SnapKind::Free);
    CHECK(pv.cursor.x == doctest::Approx(103));
    CHECK(pv.cursor.y == doctest::Approx(2));
    // angle / length grid from the first point
    d.click(at(0, 100), kFree);
    auto a = d.hover(at(50, 103));
    CHECK(a.snap == SnapKind::Angle);
    CHECK(a.cursor.y == doctest::Approx(100).epsilon(1e-9));
    CHECK(a.cursor.x == doctest::Approx(48));  // 50 m rounded to the 8 m step
    CHECK(a.guide.type == SnapGuide::Type::Line);
    CHECK(a.guide.faint);
    auto f = d.hover(at(50, 103), kFree);
    CHECK(f.snap == SnapKind::Free);
    CHECK(f.cursor.x == doctest::Approx(50));
    CHECK(f.cursor.y == doctest::Approx(103));
}

TEST_CASE("DrawSession: end-extension and parallel-offset guides") {
    Rig rig;
    rig.road({0, 0, 0}, {100, 0, 0}, "b_road");
    DrawSession d(rig.g, rig.hf);
    d.set_road_type("b_road");
    // beyond the east end, 3 m off the extension line, but further than the end snap radius (14 m)
    auto e = d.hover(at(140, 3));
    CHECK(e.snap == SnapKind::Guide);
    CHECK(e.guide.type == SnapGuide::Type::Line);
    CHECK(e.cursor.y == doctest::Approx(0).epsilon(1e-9));
    CHECK(e.cursor.x == doctest::Approx(140));
}

TEST_CASE("DrawSession: parallel-offset guide lands side by side") {
    Rig rig;
    rig.road({0, 0, 0}, {200, 0, 0}, "b_road");
    const RoadType& rt = *rig.region->road_type("b_road");
    Edge probe;
    probe.road_type = "b_road";
    const double total = cross_section_width(instantiate_lanes(probe, rt, rig.region->driving_side));
    const double base = total;  // A.total / 2 + B.total / 2 for the same type
    DrawSession d(rig.g, rig.hf);
    d.set_road_type("b_road");
    // cursor roughly one road width north of the road, mid-way along: no end within 14 m, no extension
    auto pv = d.hover(at(100, base + 2.0, 0.5));
    CHECK(pv.snap == SnapKind::Guide);
    CHECK(pv.guide.type == SnapGuide::Type::Offset);
    CHECK(pv.guide.offset == doctest::Approx(base).epsilon(1e-9));
    CHECK(pv.cursor.y == doctest::Approx(base).epsilon(1e-9));
    CHECK(pv.guide.polyline.size() > 10);
    auto south = d.hover(at(100, -base - 2.0, 0.5));
    CHECK(south.guide.offset == doctest::Approx(-base).epsilon(1e-9));
    // far from any multiple: free
    CHECK(d.hover(at(100, base * 1.5, 0.5)).snap == SnapKind::Free);
}

TEST_CASE("DrawSession: undo_point and cancel") {
    Rig rig;
    DrawSession d(rig.g, rig.hf);
    d.set_road_type("residential");
    CHECK_FALSE(d.undo_point());
    d.click(at(0, 0), kFree);
    d.click(at(50, 0), kFree);
    d.click(at(100, 20), kFree);
    CHECK(d.points().size() == 3);
    CHECK(d.undo_point());
    CHECK(d.points().size() == 2);
    CHECK(d.points().back().x == doctest::Approx(50));
    CHECK(d.undo_point());
    CHECK(d.undo_point());
    CHECK_FALSE(d.active());
    CHECK_FALSE(d.undo_point());
    d.click(at(0, 0), kFree);
    d.cancel();
    CHECK_FALSE(d.active());
    CHECK_FALSE(d.finish().ok());
}

TEST_CASE("DrawSession: every error and advisory fires once") {
    Rig rig;
    DrawSession d(rig.g, rig.hf);
    d.set_road_type("residential");
    d.click(at(0, 0), kFree);
    // minimum span
    auto ms = d.hover(at(2, 0, 0.1), kFree);
    CHECK_FALSE(ms.ok());
    CHECK(has_issue(ms.errors, IssueCode::MinSpan));
    CHECK(ms.errors[0].text == "segment shorter than 4 m");
    auto c = d.click(at(2, 0, 0.1), kFree);
    CHECK_FALSE(c.added);
    CHECK(has_issue(c.errors, IssueCode::MinSpan));
    // fold: a hairpin tighter than the half-width of the road
    d.click(at(20, 0), kFree);
    auto fold = d.hover(at(0, 5), kFree);
    CHECK(has_issue(fold.errors, IssueCode::Fold));
    // radius below the type minimum with a fold-free bend
    d.cancel();
    d.set_road_type("b_road");
    d.click(at(0, 0), kFree);
    d.click(at(60, 0), kFree);
    auto rad = d.hover(at(60, 60), kFree);
    CHECK(rad.ok());
    CHECK(has_issue(rad.advisories, IssueCode::RadiusBelowMinimum));
    CHECK(rad.readouts.min_radius < 180);
    // grade above the advisory (b_road 8 %)
    d.cancel();
    d.click(at(0, 0), kFree);
    d.adjust_height(12);
    auto gr = d.hover(at(60, 0), kFree);
    CHECK(has_issue(gr.advisories, IssueCode::GradeAboveAdvisory));
    CHECK(gr.readouts.grade_pct == doctest::Approx(20).epsilon(1e-6));
    CHECK(gr.cursor.z == doctest::Approx(12));
    d.cancel();
}

TEST_CASE("DrawSession: bridge and tunnel advisories from the terrain") {
    Rig rig;
    rig.set_ground([](double x, double y) {  // a 40 m deep pit around (0, 100) and a 40 m hill around (0, -100)
        const double d1 = std::hypot(x, y - 100), d2 = std::hypot(x, y + 100);
        return (d1 < 60 ? -40.0 * std::min(1.0, (60 - d1) / 20) : 0.0) + (d2 < 60 ? 40.0 * std::min(1.0, (60 - d2) / 20) : 0.0);
    });
    DrawSession d(rig.g, rig.hf);
    d.set_road_type("b_road");
    d.click(at(0, 0), kFree);
    d.adjust_height(40);
    d.click(at(0, 100), kFree);  // pit centre, held at z = 0 by the height offset
    auto bridge = d.hover(at(0, 200), kFree);
    CHECK(has_issue(bridge.advisories, IssueCode::BridgeNeeded));
    d.cancel();
    d.click(at(0, 0), kFree);
    d.set_height_offset(-40);
    d.click(at(0, -100), kFree);  // hill top (ground +40) held at z = 0
    auto tunnel = d.hover(at(0, -200), kFree);
    CHECK(has_issue(tunnel.advisories, IssueCode::TunnelNeeded));
}

TEST_CASE("DrawSession: dual carriageway becomes two one-way edges at the region offset, mirrored by driving side") {
    for (bool mirrored : {false, true}) {
        Rig rig(mirrored);
        DrawSession d(rig.g, rig.hf);
        d.set_road_type("a_road_dual");
        d.set_one_way(false);
        d.click(at(0, 0), kFree);
        auto pv = d.hover(at(200, 0), kFree);
        CHECK(pv.ribbons.size() == 2);
        auto r = draw_free(d, {{200, 0}});  // add the last point (the first is already placed) and finish
        REQUIRE(r.ok());
        REQUIRE(rig.st.execute(std::move(r.command)));
        REQUIRE(rig.g.edges().size() == 2);
        const double o = interchange_dual_offset(*rig.region);
        CHECK(o > 0);
        for (const auto& [id, e] : rig.g.edges()) {
            CHECK(e.one_way);
            const Vec3 a = rig.g.node(e.a)->pos, b = rig.g.node(e.b)->pos;
            CHECK(std::abs(a.y) == doctest::Approx(o).epsilon(1e-6));
            CHECK(std::abs(b.y) == doctest::Approx(o).epsilon(1e-6));
            // left-hand traffic going east is on the north side; mirrored for right-hand traffic
            const bool east = b.x > a.x;
            const bool north = a.y > 0;
            CHECK((east == north) == !mirrored);
        }
        // single carriageway of a dual type: one edge, not offset
        Rig one(mirrored);
        DrawSession d1(one.g, one.hf);
        d1.set_road_type("a_road_dual");
        d1.set_one_way(true);
        auto r1 = draw_free(d1, {{0, 0}, {200, 0}});
        REQUIRE(r1.ok());
        REQUIRE(one.st.execute(std::move(r1.command)));
        CHECK(one.g.edges().size() == 1);
        CHECK(one.g.node(one.g.edges().begin()->second.a)->pos.y == doctest::Approx(0));
    }
}

TEST_CASE("DrawSession: hover and click never touch the graph; finish executes, undoes and redoes exactly") {
    Rig rig;
    rig.road({0, 0, 0}, {200, 0, 0});
    rig.road({100, -150, 0}, {100, -40, 0});
    rig.g.clear_dirty();
    const Snap before = snap(rig.g);
    DrawSession d(rig.g, rig.hf);
    d.set_road_type("a_road_dual");
    for (double x = -50; x < 250; x += 17)
        for (double y = -100; y < 100; y += 23) d.hover(at(x, y));
    d.click(at(10, 80), kFree);
    d.hover(at(150, 60));
    d.click(at(150, 60), kFree);
    d.hover(at(300, -60));
    CHECK(snap(rig.g) == before);
    CHECK(rig.g.dirty_nodes().empty());
    CHECK(rig.g.dirty_edges().empty());
    d.click(at(160, -60), kFree);
    auto r = d.finish();
    REQUIRE(r.ok());
    CHECK(snap(rig.g) == before);  // finish() builds a command, it does not run it
    REQUIRE(rig.st.execute(std::move(r.command)));
    d.invalidate();
    const Snap after = snap(rig.g);
    CHECK(after.edges.size() > before.edges.size() + 2);
    rig.st.undo();
    CHECK(snap(rig.g) == before);
    rig.st.redo();
    CHECK(snap(rig.g) == after);
    rig.st.undo();
    CHECK(snap(rig.g) == before);
}

TEST_CASE("DrawSession: no pick, no region") {
    Rig rig;
    DrawSession d(rig.g, rig.hf);
    PointerInput up = at(0, 0);
    up.ray.dir = {0, 0, 1};
    auto pv = d.hover(up);
    CHECK_FALSE(pv.has_pick);
    CHECK_FALSE(pv.ok());
    CHECK_FALSE(d.click(up).added);
    RoadGraph bare;
    DrawSession d2(bare, rig.hf);
    d2.click(at(0, 0), kFree);
    auto e = d2.hover(at(50, 0), kFree);
    CHECK(has_issue(e.errors, IssueCode::NoRegion));
}

TEST_CASE("DrawSession: height offset and surface hits") {
    Rig rig;
    DrawSession d(rig.g, rig.hf);
    d.set_road_type("residential");
    d.adjust_height(2);
    d.adjust_height(-0.5);
    CHECK(d.height_offset() == doctest::Approx(1.5));
    CHECK(d.hover(at(10, 10), kFree).cursor.z == doctest::Approx(1.5));
    d.click(at(10, 10), kFree);
    CHECK(d.height_offset() == 0);  // reset by the click
    CHECK(d.points()[0].z == doctest::Approx(1.5));
    // a host-supplied road surface hit nearer than the ground gives the height (bridge deck)
    PointerInput in = at(40, 10);
    in.ray.origin = {40, 10, 100};
    in.surface_hit = Vec3{40, 10, 7.5};
    auto pv = d.hover(in, kFree);
    CHECK(pv.cursor.z == doctest::Approx(7.5));
}

TEST_CASE("DrawSession: clicking the last placed point finishes the stroke; finish and cancel keep the session usable") {
    Rig rig;
    DrawSession d(rig.g, rig.hf);
    d.set_road_type("residential");
    // one point: finishing is refused with a reason and nothing is lost
    d.click(at(0, 0), kFree);
    auto f1 = d.finish();
    CHECK_FALSE(f1.ok());
    CHECK_FALSE(f1.error.empty());
    CHECK(d.points().size() == 1);
    // a click on the only point cannot finish either, and keeps the point
    auto c1 = d.click(at(3, 0), kFree);
    CHECK_FALSE(c1.finished);
    CHECK_FALSE(c1.finish.error.empty());
    CHECK(d.points().size() == 1);
    d.click(at(80, 0), kFree);
    CHECK(d.hover(at(82, 1), kFree).would_finish);
    CHECK_FALSE(d.hover(at(120, 1), kFree).would_finish);
    auto c2 = d.click(at(82, 1), kFree);   // within the pick radius of the last point
    REQUIRE(c2.finished);
    CHECK_FALSE(c2.auto_finished);
    CHECK_FALSE(d.active());
    REQUIRE(rig.st.execute(std::move(c2.finish.command)));
    REQUIRE(rig.g.edges().size() == 1);
    CHECK(rig.g.edges().begin()->second.horizontal.length() == doctest::Approx(80).epsilon(1e-6));
    // cancel clears the stroke and the session can start again
    d.click(at(0, 50), kFree);
    d.click(at(60, 50), kFree);
    d.cancel();
    CHECK_FALSE(d.active());
    CHECK(d.click(at(0, 90), kFree).added);
    CHECK(d.points().size() == 1);
}

namespace {
const Junction* junction_at(const JunctionSet& js, const RoadGraph& g, std::size_t arms) {
    for (const auto& [id, j] : js.junctions)
        if (j.arms.size() == arms && g.edges_at(id).size() == arms) return &j;
    return nullptr;
}
}  // namespace

TEST_CASE("DrawSession: strokes that meet roads make real junctions (T = 3 arms, crossing = 4 arms)") {
    {  // T: start on the road
        Rig rig;
        rig.road({0, 0, 0}, {200, 0, 0});
        DrawSession d(rig.g, rig.hf);
        d.set_road_type("residential");
        auto c0 = d.click(at(101, 2));   // snaps onto the road: the first point
        REQUIRE(c0.added);
        d.click(at(100, 80), kFree);
        auto r = d.finish();
        REQUIRE(r.ok());
        REQUIRE(rig.st.execute(std::move(r.command)));
        const JunctionSet js = build_plain_junctions(rig.g, *rig.region, {});
        const Junction* j = junction_at(js, rig.g, 3);
        REQUIRE(j);
        CHECK(j->pos.xy().x == doctest::Approx(101).epsilon(0.02));
        CHECK(js.junctions.size() == 1);
    }
    {  // T: end on the road
        Rig rig;
        rig.road({0, 0, 0}, {200, 0, 0});
        DrawSession d(rig.g, rig.hf);
        d.set_road_type("residential");
        d.click(at(100, -80), kFree);
        auto c = d.click(at(101, -2));
        REQUIRE(c.auto_finished);
        REQUIRE(rig.st.execute(std::move(c.finish.command)));
        const JunctionSet js = build_plain_junctions(rig.g, *rig.region, {});
        CHECK(junction_at(js, rig.g, 3));
    }
    {  // crossing at similar level: 4 arms
        Rig rig;
        rig.road({0, 0, 0}, {200, 0, 0});
        DrawSession d(rig.g, rig.hf);
        d.set_road_type("residential");
        d.set_height_offset(0.5);
        d.click(at(100, -80), kFree);
        d.set_height_offset(0.5);
        d.click(at(100, 80), kFree);
        auto r = d.finish();
        REQUIRE(r.ok());
        REQUIRE(rig.st.execute(std::move(r.command)));
        const JunctionSet js = build_plain_junctions(rig.g, *rig.region, {});
        const Junction* j = junction_at(js, rig.g, 4);
        REQUIRE(j);
        CHECK(j->pos.xy().x == doctest::Approx(100).epsilon(1e-3));
        CHECK(js.junctions.size() == 1);
    }
    {  // bridged crossing: no junction
        Rig rig;
        rig.road({0, 0, 0}, {200, 0, 0});
        DrawSession d(rig.g, rig.hf);
        d.set_road_type("residential");
        d.set_height_offset(8);
        d.click(at(100, -80), kFree);
        d.set_height_offset(8);
        d.click(at(100, 80), kFree);
        auto r = d.finish();
        REQUIRE(r.ok());
        REQUIRE(rig.st.execute(std::move(r.command)));
        CHECK(build_plain_junctions(rig.g, *rig.region, {}).junctions.empty());
    }
}
