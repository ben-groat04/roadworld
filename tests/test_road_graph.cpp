#include "doctest/doctest.h"
#include "cp/road_commands.h"

using namespace cp;

namespace {

DrawRoadSpec straight(Vec3 a, Vec3 b) {
    DrawRoadSpec s;
    s.start.pos = a;
    s.end.pos = b;
    s.road_type = "residential";
    return s;
}

EdgeId only_edge(const RoadGraph& g) {
    REQUIRE(g.edges().size() == 1);
    return g.edges().begin()->first;
}

}  // namespace

TEST_CASE("draw, undo and redo a road keeps the same ids") {
    RoadGraph g;
    CommandStack<RoadGraph> st(g);
    auto spec = straight({0, 0, 10}, {200, 0, 20});
    spec.interior = {{100, 30, 12}};
    REQUIRE(st.execute(make_draw_road(spec)));
    CHECK(g.nodes().size() == 2);
    const EdgeId e = only_edge(g);
    CHECK(g.edge(e)->horizontal.g2);
    CHECK(edge_point(*g.edge(e), 0).z == doctest::Approx(10));

    st.undo();
    CHECK(g.nodes().empty());
    CHECK(g.edges().empty());
    st.redo();
    CHECK(only_edge(g) == e);
}

TEST_CASE("t-junction splits the existing edge into two, undo restores it") {
    RoadGraph g;
    CommandStack<RoadGraph> st(g);
    REQUIRE(st.execute(make_draw_road(straight({0, 0, 0}, {200, 0, 0}))));
    const EdgeId main = only_edge(g);

    auto side = straight({}, {100, 150, 0});
    side.start.split_edge = main;
    side.start.split_s = 100;
    REQUIRE(st.execute(make_draw_road(side)));
    CHECK(g.edges().size() == 3);
    CHECK(g.nodes().size() == 4);
    CHECK(g.edge(main) == nullptr);  // retired, never reused

    // Find the junction node: three edges meet there.
    int junctions = 0;
    for (auto& [id, n] : g.nodes())
        if (g.edges_at(id).size() == 3) {
            ++junctions;
            CHECK(n.pos.x == doctest::Approx(100));
        }
    CHECK(junctions == 1);

    st.undo();
    CHECK(only_edge(g) == main);
    CHECK(g.nodes().size() == 2);
    st.redo();
    CHECK(g.edges().size() == 3);
}

TEST_CASE("attaching to an existing node shares it") {
    RoadGraph g;
    CommandStack<RoadGraph> st(g);
    st.execute(make_draw_road(straight({0, 0, 5}, {100, 0, 5})));
    const NodeId end = g.edge(only_edge(g))->b;
    auto next = straight({}, {200, 50, 9});
    next.start.existing_node = end;
    REQUIRE(st.execute(make_draw_road(next)));
    CHECK(g.nodes().size() == 3);
    CHECK(g.edges_at(end).size() == 2);
}

TEST_CASE("delete removes orphaned nodes and undo brings everything back") {
    RoadGraph g;
    CommandStack<RoadGraph> st(g);
    st.execute(make_draw_road(straight({0, 0, 0}, {100, 0, 0})));
    const EdgeId e = only_edge(g);
    REQUIRE(st.execute(make_delete_edge(e)));
    CHECK(g.nodes().empty());
    st.undo();
    CHECK(only_edge(g) == e);
    CHECK(g.nodes().size() == 2);
}

TEST_CASE("moving a handle refits and undoes") {
    RoadGraph g;
    CommandStack<RoadGraph> st(g);
    auto spec = straight({0, 0, 0}, {200, 0, 0});
    spec.interior = {{100, 0, 0}};
    st.execute(make_draw_road(spec));
    const EdgeId e = only_edge(g);
    const double L0 = g.edge(e)->horizontal.length();
    REQUIRE(st.execute(make_move_interior_point(e, 0, {100, 40, 0})));
    CHECK(g.edge(e)->horizontal.length() > L0);
    st.undo();
    CHECK(g.edge(e)->horizontal.length() == doctest::Approx(L0));
}

TEST_CASE("redo after a newer allocation does not collide") {
    RoadGraph g;
    CommandStack<RoadGraph> st(g);
    st.execute(make_draw_road(straight({0, 0, 0}, {100, 0, 0})));
    const EdgeId first = only_edge(g);
    st.undo();
    st.execute(make_draw_road(straight({0, 50, 0}, {100, 50, 0})));
    CHECK(only_edge(g) != first);  // the undone id is never handed out again
}

TEST_CASE("structure spans: set, overwrite, clear, undo") {
    RoadGraph g;
    CommandStack<RoadGraph> st(g);
    REQUIRE(st.execute(make_draw_road(straight({0, 0, 0}, {200, 0, 0}))));
    const EdgeId e = only_edge(g);
    CHECK_FALSE(st.execute(make_set_structure_span(e, 150, 250, StructureKind::Bridge)));  // beyond the edge
    REQUIRE(st.execute(make_set_structure_span(e, 20, 120, StructureKind::Bridge)));
    REQUIRE(st.execute(make_set_structure_span(e, 100, 160, StructureKind::Tunnel)));
    const auto& sp = g.edge(e)->structure;
    REQUIRE(sp.size() == 2);
    CHECK(sp[0] == StructureSpan{20, 100, StructureKind::Bridge});
    CHECK(sp[1] == StructureSpan{100, 160, StructureKind::Tunnel});
    REQUIRE(st.execute(make_set_structure_span(e, 0, 60, StructureKind::Ground)));
    REQUIRE(g.edge(e)->structure.size() == 2);
    CHECK(g.edge(e)->structure[0] == StructureSpan{60, 100, StructureKind::Bridge});
    st.undo();
    CHECK(g.edge(e)->structure[0] == StructureSpan{20, 100, StructureKind::Bridge});
    st.undo();
    st.undo();
    CHECK(g.edge(e)->structure.empty());
}

TEST_CASE("splitting an edge carries structure spans to both halves, undo restores them") {
    RoadGraph g;
    CommandStack<RoadGraph> st(g);
    REQUIRE(st.execute(make_draw_road(straight({0, 0, 0}, {200, 0, 0}))));
    const EdgeId main = only_edge(g);
    REQUIRE(st.execute(make_set_structure_span(main, 20, 60, StructureKind::Bridge)));
    REQUIRE(st.execute(make_set_structure_span(main, 80, 160, StructureKind::Tunnel)));

    auto side = straight({}, {100, 150, 0});
    side.start.split_edge = main;
    side.start.split_s = 100;
    REQUIRE(st.execute(make_draw_road(side)));
    int halves = 0;
    for (auto& [id, e] : g.edges()) {
        if (e.interior.size() || e.horizontal.length() < 60 || e.horizontal.length() > 140) continue;
        const bool left = edge_point(e, 0).x < 1;
        if (left) {
            REQUIRE(e.structure.size() == 2);
            CHECK(e.structure[0] == StructureSpan{20, 60, StructureKind::Bridge});
            CHECK(e.structure[1].s0 == doctest::Approx(80));
            CHECK(e.structure[1].s1 == doctest::Approx(e.horizontal.length()));
            CHECK(e.structure[1].kind == StructureKind::Tunnel);
        } else {
            REQUIRE(e.structure.size() == 1);
            CHECK(e.structure[0].s0 == doctest::Approx(0));
            CHECK(e.structure[0].s1 == doctest::Approx(60));
            CHECK(e.structure[0].kind == StructureKind::Tunnel);
        }
        ++halves;
    }
    CHECK(halves == 2);

    st.undo();
    REQUIRE(only_edge(g) == main);
    REQUIRE(g.edge(main)->structure.size() == 2);
    CHECK(g.edge(main)->structure[1] == StructureSpan{80, 160, StructureKind::Tunnel});
}
