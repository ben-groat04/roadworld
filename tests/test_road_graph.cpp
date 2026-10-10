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
