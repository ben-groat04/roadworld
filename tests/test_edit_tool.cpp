// Edit tool: EditSession (hover picks, selection, handle drag with live preview, point height, delete,
// change type / one-way) by scripted pointer input, with undo / redo exactness and "hover never mutates".
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cmath>
#include <memory>
#include <string>
#include <tuple>
#include <vector>

#include "doctest/doctest.h"
#include "cp/edge_sampling.h"
#include "cp/edit_tool.h"
#include "cp/road_commands.h"
#include "cp/road_tool.h"
#include "cp/stroke.h"

using namespace cp;

namespace {

struct Rig {
    std::shared_ptr<const RegionPack> region;
    RoadGraph g;
    CommandStack<RoadGraph> st{g};
    Heightfield hf{{-1000, -1000}, 4.0, 501, 501, 0.0f};

    Rig() {
        auto p = load_region_pack_by_id(CP_DATA_DIR, "uk");
        REQUIRE_MESSAGE(p, p.error);
        region = std::make_shared<const RegionPack>(*p);
        g.set_region(region);
    }
    // A road through interior points; `from` / `to` attach to existing nodes.
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

// A ray from above and to the side that passes through the world point p (a perspective-style pick).
PointerInput through(Vec3 p, double mppm = 0.001) {
    PointerInput in;
    in.ray.origin = {p.x - 300, p.y - 300, p.z + 400};
    in.ray.dir = {300, 300, -400};
    in.metres_per_pixel_per_metre = mppm;
    return in;
}

struct Snap {
    std::vector<std::tuple<std::uint64_t, double, double, double>> nodes;
    std::vector<std::tuple<std::uint64_t, std::uint64_t, std::uint64_t, std::string, bool, double, double, std::size_t, double, double>> edges;
    bool operator==(const Snap&) const = default;
};
Snap snap(const RoadGraph& g) {
    Snap s;
    for (const auto& [id, n] : g.nodes()) s.nodes.emplace_back(id.value, n.pos.x, n.pos.y, n.pos.z);
    for (const auto& [id, e] : g.edges()) {
        const Vec3 mid = edge_point(e, e.horizontal.length() * 0.37);
        s.edges.emplace_back(id.value, e.a.value, e.b.value, e.road_type, e.one_way, e.horizontal.length(), e.vertical.z(e.horizontal.length() * 0.5),
                             e.interior.size(), mid.x, mid.y);
    }
    return s;
}

bool has_issue(const std::vector<Issue>& v, IssueCode c) {
    return std::any_of(v.begin(), v.end(), [&](const Issue& i) { return i.code == c; });
}

double dist_to_polyline_xy(const Vec3& p, const std::vector<Vec3>& poly) {
    double best = 1e18;
    for (std::size_t i = 0; i + 1 < poly.size(); ++i) {
        const Vec2 a = poly[i].xy(), b = poly[i + 1].xy(), ab = b - a;
        const double l2 = ab.dot(ab);
        const double t = l2 > 0 ? std::clamp((p.xy() - a).dot(ab) / l2, 0.0, 1.0) : 0.0;
        best = std::min(best, (p.xy() - (a + ab * t)).length());
    }
    return best;
}
double max_dev(const std::vector<Vec3>& line, const std::vector<Vec3>& poly) {
    double m = 0;
    for (const Vec3& p : line) m = std::max(m, dist_to_polyline_xy(p, poly));
    return m;
}
std::vector<Vec3> edge_line(const Edge& e) {
    std::vector<Vec3> v;
    for (const EdgeSample& s : sample_centreline(e)) v.push_back(s.pos);
    return v;
}

}  // namespace

TEST_CASE("EditSession: hover priorities and pixel radii; hover never mutates") {
    Rig rig;
    const EdgeId straight = rig.road({0, -200, 0}, {}, {200, -200, 0});
    const EdgeId bent = rig.road({0, 0, 0}, {{80, 30, 0}}, {200, -20, 0});
    rig.g.clear_dirty();
    const Snap before = snap(rig.g);
    EditSession es(rig.g, rig.hf);

    // centreline of a plain road: Road within 12 px (mpp 1 m/px), nothing beyond
    {
        const EditPreview on = es.hover(at(100, -200));
        REQUIRE(on.hover.kind == EditHoverKind::Road);
        CHECK(on.hover.edge == straight);
        CHECK(on.hover.s == doctest::Approx(100).epsilon(1e-2));
        CHECK(on.hover.point.x == doctest::Approx(100).epsilon(1e-6));
        CHECK(es.hover(at(100, -200 + 11.5)).hover.kind == EditHoverKind::Road);
        CHECK(es.hover(at(100, -200 + 12.5)).hover.kind == EditHoverKind::None);
        CHECK(es.hover(at(100, -200 + 12.5, 2.0)).hover.kind == EditHoverKind::Road);  // the radius is in pixels: 2 m/px reaches 24 m
    }
    // a junction / end node of any road: within 11 px
    {
        const EditPreview n = es.hover(at(0, -200));
        REQUIRE(n.hover.kind == EditHoverKind::Node);
        CHECK(n.hover.edge == straight);
        CHECK(n.hover.node == rig.g.edge(straight)->a);
        CHECK(n.hover.handle == 0);
        CHECK(es.hover(at(10, -200)).hover.kind == EditHoverKind::Node);
        CHECK(es.hover(at(11.5, -200)).hover.kind == EditHoverKind::Road);  // beyond the node radius, still on the road
    }
    // an interior control point of a road that is not selected
    {
        const EditPreview ip = es.hover(at(80, 30));
        REQUIRE(ip.hover.kind == EditHoverKind::InteriorPoint);
        CHECK(ip.hover.edge == bent);
        CHECK(ip.hover.handle == 1);
        CHECK(ip.hover.point.y == doctest::Approx(30));
    }
    // the selected road's handles win over everything, with the 3D ray distance to the lifted point
    es.select(bent);
    CHECK(es.selected() == bent);
    const std::vector<Handle> hs = es.handles();
    REQUIRE(hs.size() == 3);
    CHECK(hs[0].is_node());
    CHECK_FALSE(hs[1].is_node());
    CHECK(hs[2].node == rig.g.edge(bent)->b);
    CHECK(hs[1].pos.x == doctest::Approx(80));
    {
        const EditPreview h = es.hover(through({80, 30, 0.5}));
        REQUIRE(h.hover.kind == EditHoverKind::Handle);
        CHECK(h.hover.edge == bent);
        CHECK(h.hover.handle == 1);
        CHECK(h.hover.point.x == doctest::Approx(80));
        // a ray 0.2 m past it at ~560 m range is well inside the 11 px reach
        CHECK(es.hover(through({80.2, 30, 0.5})).hover.kind == EditHoverKind::Handle);
        // 20 m away is not
        CHECK(es.hover(through({100, 30, 0.5})).hover.kind != EditHoverKind::Handle);
        const EditPreview e = es.hover(at(200, -20));
        CHECK(e.hover.kind == EditHoverKind::Handle);
        CHECK(e.hover.handle == 2);
        CHECK(e.hover.node == rig.g.edge(bent)->b);
    }
    // hover never touches the graph, the selection or the stack
    for (double x = -100; x < 320; x += 13)
        for (double y = -260; y < 120; y += 17) es.hover(at(x, y));
    CHECK(es.selected() == bent);
    CHECK(snap(rig.g) == before);
    CHECK(rig.g.dirty_nodes().empty());
    CHECK(rig.g.dirty_edges().empty());
    CHECK(rig.st.undo_depth() == 2);
    // no pick
    PointerInput sky = at(0, 0);
    sky.ray.dir = {0, 0, 1};
    CHECK_FALSE(es.hover(sky).has_pick);
}

TEST_CASE("EditSession: click selects, clears, keeps on a handle") {
    Rig rig;
    const EdgeId a = rig.road({0, 0, 0}, {{80, 30, 0}}, {200, -20, 0});
    const EdgeId b = rig.road({0, 200, 0}, {}, {200, 200, 0});
    EditSession es(rig.g, rig.hf);
    CHECK_FALSE(es.selected().valid());
    es.click(at(100, 200));
    CHECK(es.selected() == b);
    es.click(at(80, 30));  // an interior point of the other road selects that road
    CHECK(es.selected() == a);
    es.click(at(80, 30));  // now a handle of the selected road: nothing changes
    CHECK(es.selected() == a);
    es.click(at(100, 100));  // empty ground clears
    CHECK_FALSE(es.selected().valid());
    es.click(at(0, 0));  // a node selects the road at it
    CHECK(es.selected() == a);
    es.clear_selection();
    CHECK_FALSE(es.selected().valid());
    es.select(EdgeId{});
    CHECK_FALSE(es.selected().valid());
    // a selected road that is deleted is no longer selected
    es.select(a);
    REQUIRE(rig.st.execute(make_delete_edge(a)));
    CHECK_FALSE(es.selected().valid());
    CHECK(es.handles().empty());
    CHECK(es.centreline(a).empty());
    CHECK(es.centreline(b).size() > 2);
}

TEST_CASE("EditSession: dragging an interior handle previews live and commits one undoable move") {
    Rig rig;
    rig.set_ground([](double x, double y) { return 20 + 0.02 * x + 0.01 * y; });
    const EdgeId id = rig.road({0, 0, 20}, {{80, 30, 22.4}}, {200, -20, 24}, "b_road");
    EditSession es(rig.g, rig.hf);
    es.select(id);
    const Snap before = snap(rig.g);
    const std::size_t depth0 = rig.st.undo_depth();

    CHECK_FALSE(es.begin_drag(at(150, 150)));          // not on a handle
    CHECK_FALSE(es.dragging());
    REQUIRE(es.begin_drag(through({80, 30, 22.9})));  // the lifted handle
    CHECK(es.dragging());
    EditPreview pv = es.drag_to(at(90, 45));
    REQUIRE(pv.dragging);
    CHECK(pv.ok());
    CHECK(pv.cursor.x == doctest::Approx(90));
    CHECK(pv.cursor.y == doctest::Approx(45));
    // the handle keeps its height above the terrain (JS dzT): 22.4 - ground(80,30)
    CHECK(pv.cursor.z - rig.hf.height(90, 45) == doctest::Approx(22.4 - rig.hf.height(80, 30)).epsilon(1e-9));
    REQUIRE(pv.ghosts.size() == 1);
    CHECK(pv.ghosts[0].edge == id);
    CHECK(pv.ghosts[0].fits);
    REQUIRE(pv.ribbons.size() == 1);
    CHECK(pv.ribbons[0].total_width > 5);
    CHECK(pv.profile_edge == id);
    REQUIRE(pv.profile.valid);
    CHECK(pv.profile.curves.size() == 3);
    CHECK(pv.readouts.length > 200);
    CHECK(pv.readouts.g2);
    // the graph has not moved
    CHECK(snap(rig.g) == before);
    pv = es.drag_to(at(100, 60));
    const std::vector<Vec3> ghost = pv.ghosts[0].centreline;
    const double ghost_len = pv.readouts.length;
    CHECK(es.hover(at(100, 60)).dragging);  // hover() while dragging IS the preview
    EditResult r = es.end_drag(at(100, 60));
    CHECK_FALSE(es.dragging());
    REQUIRE(r.ok());
    CHECK(snap(rig.g) == before);  // a command, not a mutation
    REQUIRE(rig.st.execute(std::move(r.command)));
    es.invalidate();
    CHECK(rig.st.undo_depth() == depth0 + 1);  // the whole drag is one undo step
    const Edge& e = *rig.g.edge(id);
    CHECK(e.interior[0].x == doctest::Approx(100));
    CHECK(e.interior[0].y == doctest::Approx(60));
    CHECK(e.interior[0].z - rig.hf.height(100, 60) == doctest::Approx(22.4 - rig.hf.height(80, 30)).epsilon(1e-9));
    // what the ghost showed is what the road became
    CHECK(max_dev(ghost, edge_line(e)) < 0.02);
    CHECK(e.horizontal.length() == doctest::Approx(ghost_len).epsilon(1e-9));
    const Snap after = snap(rig.g);
    CHECK_FALSE(after == before);
    rig.st.undo();
    CHECK(snap(rig.g) == before);
    rig.st.redo();
    CHECK(snap(rig.g) == after);
    rig.st.undo();
    CHECK(snap(rig.g) == before);
    // cancel leaves everything alone; a release without travel is not an edit
    es.invalidate();
    REQUIRE(es.begin_drag(through({80, 30, 22.9})));
    es.cancel_drag();
    CHECK_FALSE(es.dragging());
    REQUIRE(es.begin_drag(through({80, 30, 22.9})));
    CHECK_FALSE(es.end_drag(at(80, 30)).ok());
    CHECK(es.end_drag(at(80, 30)).error == "no drag in progress");
    CHECK(snap(rig.g) == before);
}

TEST_CASE("EditSession: dragging a junction node moves every road at it") {
    Rig rig;
    const EdgeId e1 = rig.road({0, 0, 0}, {}, {100, 0, 0});
    const NodeId shared = rig.g.edge(e1)->b;
    const EdgeId e2 = rig.road({100, 0, 0}, {}, {200, 50, 0}, "residential", shared);
    const EdgeId e3 = rig.road({100, 0, 0}, {}, {150, -80, 0}, "residential", shared);
    REQUIRE(rig.g.edges_at(shared).size() == 3);
    EditSession es(rig.g, rig.hf);
    es.select(e1);
    const Snap before = snap(rig.g);
    REQUIRE(es.begin_drag(through({100, 0, 0.5})));
    const EditPreview pv = es.drag_to(at(110, 20));
    CHECK(pv.ok());
    CHECK(pv.ghosts.size() == 3);  // all three roads follow the node
    for (const EditGhost& gh : pv.ghosts) {
        CHECK(gh.fits);
        CHECK((gh.edge == e1 || gh.edge == e2 || gh.edge == e3));
    }
    EditResult r = es.end_drag(at(110, 20));
    REQUIRE(r.ok());
    REQUIRE(rig.st.execute(std::move(r.command)));
    es.invalidate();
    CHECK(rig.g.node(shared)->pos.x == doctest::Approx(110));
    CHECK(rig.g.node(shared)->pos.y == doctest::Approx(20));
    CHECK(rig.st.undo_depth() == 4);  // three roads + one move
    for (const EdgeId id : {e1, e2, e3}) CHECK(edge_line(*rig.g.edge(id)).size() > 2);
    rig.st.undo();
    CHECK(snap(rig.g) == before);
}

TEST_CASE("EditSession: refused moves return the reason and change nothing") {
    Rig rig;
    const EdgeId id = rig.road({0, 0, 0}, {{100, 40, 0}}, {200, 0, 0});
    EditSession es(rig.g, rig.hf);
    es.select(id);
    const Snap before = snap(rig.g);
    auto drag = [&](Vec3 handle, double x, double y) {
        REQUIRE(es.begin_drag(through({handle.x, handle.y, handle.z + 0.5})));
        const EditPreview pv = es.drag_to(at(x, y));
        EditResult r = es.end_drag(at(x, y));
        return std::make_pair(pv, std::move(r));
    };
    {  // onto its neighbour: segment shorter than 4 m
        auto [pv, r] = drag({100, 40, 0}, 2, 1);
        CHECK_FALSE(pv.ok());
        CHECK(has_issue(pv.errors, IssueCode::MinSpan));
        CHECK_FALSE(r.ok());
        CHECK(r.error_code == IssueCode::MinSpan);
        CHECK(r.error == "segment shorter than 4 m");
    }
    {  // doubling back exactly onto the road's own end: no sane curve through it
        auto [pv, r] = drag({100, 40, 0}, 200.0, 0.0);
        CHECK_FALSE(pv.ok());
        CHECK_FALSE(r.ok());
    }
    CHECK(snap(rig.g) == before);
    CHECK_FALSE(es.dragging());
    CHECK(rig.st.undo_depth() == 1);
}

TEST_CASE("EditSession: a drag over another road is refused in the height band, advised at grade, free when clear") {
    Rig rig;
    const EdgeId other = rig.road({0, 100, 0}, {}, {200, 100, 0});
    const EdgeId mine = rig.road({0, 0, 3}, {{100, 40, 3}}, {200, 0, 3});
    EditSession es(rig.g, rig.hf);
    es.select(mine);
    const Snap before = snap(rig.g);
    {  // 3 m above the other road
        REQUIRE(es.begin_drag(through({100, 40, 3.5})));
        const EditPreview pv = es.drag_to(at(100, 170));
        CHECK_FALSE(pv.ok());
        REQUIRE(has_issue(pv.errors, IssueCode::Crossing));
        REQUIRE_FALSE(pv.crossings.empty());
        CHECK(pv.crossings[0].verdict == CrossingVerdict::Refused);
        CHECK(pv.crossings[0].edge == other);
        CHECK(pv.crossings[0].level_difference == doctest::Approx(3.0).epsilon(1e-6));
        const EditResult r = es.end_drag(at(100, 170));
        CHECK_FALSE(r.ok());
        CHECK(r.error_code == IssueCode::Crossing);
        CHECK(r.error.find("crossing too close vertically (3.0 m)") != std::string::npos);
        CHECK(snap(rig.g) == before);
    }
    {  // raise the handle to 8 m first: the same drag is clear
        EditResult up = es.set_point_height(mine, 1, 8);
        REQUIRE(up.ok());
        REQUIRE(rig.st.execute(std::move(up.command)));
        es.invalidate();
        REQUIRE(es.begin_drag(through({100, 40, 8.5})));
        const EditPreview pv = es.drag_to(at(100, 170));
        CHECK(pv.ok());
        REQUIRE_FALSE(pv.crossings.empty());
        CHECK(pv.crossings[0].verdict == CrossingVerdict::Clear);
        CHECK_FALSE(has_issue(pv.advisories, IssueCode::CrossingAtGrade));
        CHECK(es.end_drag(at(100, 170)).ok());
    }
}

TEST_CASE("EditSession: a drag across another road at the same level is allowed and advised: an edit makes no junction") {
    Rig rig;
    rig.road({0, 100, 0}, {}, {200, 100, 0});
    const EdgeId mine = rig.road({0, 0, 0}, {{100, 40, 0}}, {200, 0, 0});
    EditSession es(rig.g, rig.hf);
    es.select(mine);
    REQUIRE(es.begin_drag(through({100, 40, 0.5})));
    const EditPreview pv = es.drag_to(at(100, 170));
    CHECK(pv.ok());
    REQUIRE_FALSE(pv.crossings.empty());
    CHECK(pv.crossings[0].verdict == CrossingVerdict::Join);
    CHECK(has_issue(pv.advisories, IssueCode::CrossingAtGrade));
    EditResult r = es.end_drag(at(100, 170));
    REQUIRE(r.ok());
    CHECK_FALSE(r.advisories.empty());
    REQUIRE(rig.st.execute(std::move(r.command)));
    CHECK(rig.g.edges().size() == 2);  // no junction was made
    CHECK(rig.g.nodes().size() == 4);
}

TEST_CASE("EditSession: point height, node and interior, as one undoable command each") {
    Rig rig;
    const EdgeId id = rig.road({0, 0, 0}, {{100, 40, 1}}, {200, 0, 2});
    EditSession es(rig.g, rig.hf);
    const Snap before = snap(rig.g);
    // interior: PgUp / PgDn step
    EditResult up = es.adjust_point_height(id, 1, es.params().height_step);
    REQUIRE(up.ok());
    CHECK(snap(rig.g) == before);
    REQUIRE(rig.st.execute(std::move(up.command)));
    CHECK(rig.g.edge(id)->interior[0].z == doctest::Approx(2));
    CHECK(rig.g.edge(id)->interior[0].x == doctest::Approx(100));
    // a node: the road end moves with its height
    EditResult n = es.set_point_height(id, 2, 7.5);
    REQUIRE(n.ok());
    REQUIRE(rig.st.execute(std::move(n.command)));
    CHECK(rig.g.node(rig.g.edge(id)->b)->pos.z == doctest::Approx(7.5));
    CHECK(rig.g.edge(id)->vertical.z(rig.g.edge(id)->horizontal.length()) == doctest::Approx(7.5));
    CHECK(rig.st.undo_depth() == 3);
    // steps
    EditResult down = es.adjust_point_height(id, 0, -es.params().height_step_large);
    REQUIRE(down.ok());
    REQUIRE(rig.st.execute(std::move(down.command)));
    CHECK(rig.g.node(rig.g.edge(id)->a)->pos.z == doctest::Approx(-5));
    // refusals
    CHECK_FALSE(es.set_point_height(id, 1, 2).ok());  // already there
    CHECK_FALSE(es.set_point_height(id, 9, 2).ok());  // no such handle
    CHECK_FALSE(es.set_point_height(EdgeId{}, 0, 2).ok());
    CHECK_FALSE(es.adjust_point_height(EdgeId{}, 0, 1).ok());
    // exact undo and redo
    const Snap after = snap(rig.g);
    for (int i = 0; i < 3; ++i) rig.st.undo();
    CHECK(snap(rig.g) == before);
    for (int i = 0; i < 3; ++i) rig.st.redo();
    CHECK(snap(rig.g) == after);
}

TEST_CASE("EditSession: delete, change type and one-way act on the selected road") {
    Rig rig;
    const EdgeId a = rig.road({0, 0, 0}, {{100, 40, 0}}, {200, 0, 0});
    const EdgeId b = rig.road({0, 300, 0}, {}, {200, 300, 0}, "b_road");
    EditSession es(rig.g, rig.hf);
    const Snap before = snap(rig.g);
    // nothing selected
    CHECK_FALSE(es.delete_selected().ok());
    CHECK_FALSE(es.change_type("b_road").ok());
    CHECK_FALSE(es.set_one_way(true).ok());
    es.select(a);
    // type
    CHECK_FALSE(es.change_type("residential").ok());
    CHECK_FALSE(es.change_type("hovercraft").ok());
    CHECK(es.change_type("hovercraft").error_code == IssueCode::NoRegion);
    EditResult t = es.change_type("b_road");
    REQUIRE(t.ok());
    CHECK(rig.g.edge(a)->road_type == "residential");
    REQUIRE(rig.st.execute(std::move(t.command)));
    CHECK(rig.g.edge(a)->road_type == "b_road");
    // one-way
    CHECK_FALSE(es.set_one_way(false).ok());
    EditResult ow = es.set_one_way(true);
    REQUIRE(ow.ok());
    REQUIRE(rig.st.execute(std::move(ow.command)));
    CHECK(rig.g.edge(a)->one_way);
    CHECK(es.set_one_way(true).error == "the road is already one-way");
    // delete
    EditResult del = es.delete_selected();
    REQUIRE(del.ok());
    REQUIRE(rig.st.execute(std::move(del.command)));
    CHECK(rig.g.edge(a) == nullptr);
    CHECK(rig.g.edge(b) != nullptr);
    CHECK_FALSE(es.selected().valid());
    CHECK(rig.st.undo_depth() == 5);
    const Snap after = snap(rig.g);
    for (int i = 0; i < 3; ++i) rig.st.undo();
    CHECK(snap(rig.g) == before);
    CHECK_FALSE(rig.g.edge(a)->one_way);
    CHECK(rig.g.edge(a)->road_type == "residential");
    for (int i = 0; i < 3; ++i) rig.st.redo();
    CHECK(snap(rig.g) == after);
}

TEST_CASE("EditSession: changing the type is refused where the wider road would fold") {
    // U-turns of shrinking radius: find one a residential street (14.3 m wide) takes and a motorway carriageway (17.45 m) does not
    bool found = false;
    for (double r = 16; r >= 5.5 && !found; r -= 0.25) {
        Rig rig;
        const EdgeId id = rig.road({0, 0, 0}, {{30, 0, 0}, {30 + 0.7071 * r, 0.2929 * r, 0}, {30 + r, r, 0}, {30 + 0.7071 * r, 1.7071 * r, 0}, {30, 2 * r, 0}},
                                   {0, 2 * r, 0});
        AnalysisOptions opt;
        opt.road_type = "residential";
        opt.want_profile = false;
        const bool residential_ok = analyse_edge(rig.g, rig.hf, *rig.g.edge(id), opt).errors.empty();
        EditSession es(rig.g, rig.hf);
        es.select(id);
        const EditResult mw = es.change_type("motorway");
        if (!residential_ok || mw.ok()) continue;
        found = true;
        CHECK(mw.error_code == IssueCode::Fold);
        CHECK(rig.g.edge(id)->road_type == "residential");
        CHECK(es.change_type("b_road").ok() == (analyse_edge(rig.g, rig.hf, [&] { Edge e = *rig.g.edge(id); e.road_type = "b_road"; return e; }(), [&] {
                                                   AnalysisOptions o;
                                                   o.road_type = "b_road";
                                                   o.want_profile = false;
                                                   return o;
                                               }()).errors.empty()));
    }
    CHECK(found);
}

TEST_CASE("EditSession: profile series of the selected or hovered road, cached, tracking edits") {
    Rig rig;
    rig.set_ground([](double x, double y) { return 10 + 0.03 * x + 2 * std::sin(y / 30); });
    const EdgeId a = rig.road({0, 0, 11}, {{100, 30, 13}}, {200, 0, 15}, "b_road");
    const EdgeId b = rig.road({0, 300, 12}, {}, {200, 300, 16}, "b_road");
    EditSession es(rig.g, rig.hf);
    CHECK_FALSE(es.hover(at(100, 150)).profile.valid);  // nothing selected, nothing hovered
    // hovered road, nothing selected
    const EditPreview hv = es.hover(at(100, 300));
    REQUIRE(hv.profile.valid);
    CHECK(hv.profile_edge == b);
    CHECK(hv.profile.length == doctest::Approx(rig.g.edge(b)->horizontal.length()));
    // the selected road wins over a hovered one
    es.select(a);
    const EditPreview sel = es.hover(at(100, 300));
    CHECK(sel.profile_edge == a);
    const ProfileSeries direct = profile_of_edge(rig.g, rig.hf, a, es.params().profile_step);
    REQUIRE(sel.profile.valid);
    REQUIRE(sel.profile.stations.size() == direct.stations.size());
    CHECK(sel.profile.curves.size() == 3);
    CHECK(sel.profile.grades.size() == 2);
    for (std::size_t i = 0; i < direct.stations.size(); ++i) CHECK(sel.profile.stations[i].road_z == doctest::Approx(direct.stations[i].road_z));
    CHECK(sel.profile.stations.front().road_z == doctest::Approx(11));
    CHECK(sel.profile.stations.back().road_z == doctest::Approx(15));
    CHECK(sel.profile.stations.front().ground_z == doctest::Approx(rig.hf.height(0, 0)));
    // after an edit the series follows without any call to invalidate (the cache is keyed on the road)
    EditResult up = es.adjust_point_height(a, 1, 4);
    REQUIRE(up.ok());
    REQUIRE(rig.st.execute(std::move(up.command)));
    const EditPreview after = es.hover(at(100, 300));
    REQUIRE(after.profile.valid);
    CHECK(after.profile.curves[1].z == doctest::Approx(17));
    // off
    es.params().profile_series = false;
    CHECK_FALSE(es.hover(at(100, 300)).profile.valid);
}

TEST_CASE("EditSession: hover cost with and without the profile series (printed)") {
    Rig rig;
    rig.set_ground([](double x, double y) { return 6 * std::sin(x / 90) + 4 * std::cos(y / 70); });
    EdgeId sel;
    for (int i = 0; i < 5; ++i) {  // a busy graph: a 5 x 5 grid of roads, 200 m apart, some with interior points
        rig.road({-400.0 + i * 200, -400, 0}, {{-400.0 + i * 200 + 20, 0, 0}}, {-400.0 + i * 200, 400, 0});
        sel = rig.road({-400, -400.0 + i * 200, 0}, {{0, -400.0 + i * 200 + 15, 0}}, {400, -400.0 + i * 200, 0});
    }
    EditSession es(rig.g, rig.hf);
    es.select(sel);
    auto time_ms = [&](bool profile) {
        es.params().profile_series = profile;
        es.hover(at(10, 10));
        const int n = 200;
        const auto t0 = std::chrono::steady_clock::now();
        for (int i = 0; i < n; ++i) es.hover(at(-300 + i * 2.5, -100 + i * 0.7));
        return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count() / n;
    };
    const double without = time_ms(false), with = time_ms(true);
    // a drag: preview of the selected road, 200 moves
    es.params().profile_series = true;
    REQUIRE(es.begin_drag(through({0, 415, 0.5})));
    const auto t0 = std::chrono::steady_clock::now();
    for (int i = 0; i < 100; ++i) es.drag_to(at(i * 0.3, 415 + i * 0.2));
    const double drag_ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count() / 100;
    std::fprintf(stderr, "[edit hover cost, 10-road grid] no profile %.3f ms, with profile %.3f ms; drag preview with profile %.3f ms\n", without, with, drag_ms);
    CHECK(with < 50.0);
    CHECK(drag_ms < 100.0);
}
