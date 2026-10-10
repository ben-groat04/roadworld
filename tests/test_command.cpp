#include "doctest/doctest.h"
#include "cp/command.h"

#include <vector>

namespace {

struct World { std::vector<int> items; };

struct Push : cp::Command<World> {
    int v;
    explicit Push(int v) : v(v) {}
    bool apply(World& w) override { if (v < 0) return false; w.items.push_back(v); return true; }
    void revert(World& w) override { w.items.pop_back(); }
    std::string name() const override { return "push"; }
};

}  // namespace

TEST_CASE("execute, undo, redo") {
    World w;
    cp::CommandStack<World> st(w);
    CHECK(st.execute(std::make_unique<Push>(1)));
    CHECK(st.execute(std::make_unique<Push>(2)));
    CHECK(w.items == std::vector<int>{1, 2});
    CHECK(st.undo());
    CHECK(w.items == std::vector<int>{1});
    CHECK(st.redo());
    CHECK(w.items == std::vector<int>{1, 2});
    CHECK_FALSE(st.redo());
}

TEST_CASE("new command clears redo; failed command changes nothing") {
    World w;
    cp::CommandStack<World> st(w);
    st.execute(std::make_unique<Push>(1));
    st.undo();
    CHECK(st.can_redo());
    CHECK_FALSE(st.execute(std::make_unique<Push>(-1)));
    CHECK(st.can_redo());
    st.execute(std::make_unique<Push>(3));
    CHECK_FALSE(st.can_redo());
    CHECK(w.items == std::vector<int>{3});
}

TEST_CASE("composite rolls back on partial failure and undoes as one step") {
    World w;
    cp::CommandStack<World> st(w);
    auto bad = std::make_unique<cp::CompositeCommand<World>>("bad");
    bad->add(std::make_unique<Push>(1));
    bad->add(std::make_unique<Push>(-1));
    CHECK_FALSE(st.execute(std::move(bad)));
    CHECK(w.items.empty());

    auto good = std::make_unique<cp::CompositeCommand<World>>("good");
    good->add(std::make_unique<Push>(1));
    good->add(std::make_unique<Push>(2));
    CHECK(st.execute(std::move(good)));
    CHECK(st.undo_depth() == 1);
    st.undo();
    CHECK(w.items.empty());
}
