#include "doctest/doctest.h"
#include "cp/ids.h"

#include <unordered_set>

namespace { struct NodeTag; struct SegTag; }
using NodeId = cp::Id<NodeTag>;

TEST_CASE("ids are monotonic, non-null and hashable") {
    cp::IdAllocator<NodeTag> alloc;
    NodeId a = alloc.next(), b = alloc.next();
    CHECK(a.valid());
    CHECK(a < b);
    CHECK_FALSE(NodeId{}.valid());
    std::unordered_set<NodeId> s{a, b, a};
    CHECK(s.size() == 2);
}

TEST_CASE("reserve_through skips past loaded ids") {
    cp::IdAllocator<NodeTag> alloc;
    alloc.reserve_through(41);
    CHECK(alloc.next().value == 42);
    alloc.reserve_through(10);
    CHECK(alloc.next().value == 43);
}
