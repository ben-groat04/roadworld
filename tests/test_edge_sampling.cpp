#include <cmath>
#include <memory>

#include "doctest/doctest.h"
#include "cp/edge_sampling.h"
#include "cp/road_commands.h"

using namespace cp;

namespace {

// Edge made of explicit clothoid spans starting at the origin heading +x.
Edge make_edge(double length,
               double k0, double k1, double z0 = 0, double z1 = 0) {
    Edge e;
    Clothoid c;
    c.heading0 = 0;
    c.k0 = k0;
    c.dk = (k1 - k0) / length;
    c.length = length;
    e.horizontal.spans.push_back(c);
    e.vertical = VerticalProfile::build({0, length}, {z0, z1}, {});
    return e;
}

Edge arc(double radius, double length) { return make_edge(length, 1 / radius, 1 / radius); }

double dist(const EdgeSample& s, Vec2 c) { return (s.pos.xy() - c).length(); }

}  // namespace

TEST_CASE("straight edge samples to just its ends, with grade") {
    const Edge e = make_edge(100, 0, 0, 0, 5);
    const auto s = sample_centreline(e, {0.01, 0.05, 40.0});
    REQUIRE(s.size() >= 3);
    CHECK(s.front().s == 0);
    CHECK(s.back().s == doctest::Approx(100));
    CHECK(s.back().pos.x == doctest::Approx(100));
    CHECK(s.back().pos.z == doctest::Approx(5));
    CHECK(s[1].grade == doctest::Approx(0.05));
    CHECK(s.size() <= 5);
}

TEST_CASE("arc sampling respects chord error and heading change") {
    const double R = 50;
    const Edge e = arc(R, 100);
    const SamplingParams p{0.01, 0.05, 25.0};
    const auto s = sample_centreline(e, p);
    REQUIRE(s.size() > 5);
    const Vec2 centre{0, R};
    for (std::size_t i = 0; i < s.size(); ++i) {
        CHECK(dist(s[i], centre) == doctest::Approx(R).epsilon(1e-9));
        CHECK(s[i].curvature == doctest::Approx(1 / R));
        if (i == 0) continue;
        CHECK(std::abs(s[i].heading - s[i - 1].heading) <= p.max_heading_change + 1e-9);
        const Vec2 mid = (s[i].pos.xy() + s[i - 1].pos.xy()) * 0.5;
        CHECK(R - (mid - centre).length() <= p.max_chord_error + 1e-9);
    }
    // Tighter tolerance gives more samples.
    CHECK(sample_centreline(e, {0.001, 0.05, 25.0}).size() > s.size());
}

TEST_CASE("offset of a 50 m arc at 10 m gives radius 40 (inside) or 60 (outside)") {
    const double R = 50;
    const Edge e = arc(R, 100);  // turns left
    const Vec2 centre{0, R};

    const auto inner = sample_offset(e, 10.0);  // left = toward the centre
    CHECK_FALSE(inner.has_fold());
    REQUIRE(inner.samples.size() > 3);
    for (const auto& s : inner.samples) {
        CHECK(dist(s, centre) == doctest::Approx(40).epsilon(1e-9));
        CHECK(s.curvature == doctest::Approx(1.0 / 40));
        CHECK_FALSE(s.folded);
    }
    CHECK(inner.length() < 100);  // 100 * 40/50 = 80 (chords slightly shorter)
    CHECK(inner.length() == doctest::Approx(80).epsilon(0.001));

    const auto outer = sample_offset(e, -10.0);
    CHECK_FALSE(outer.has_fold());
    for (const auto& s : outer.samples) {
        CHECK(dist(s, centre) == doctest::Approx(60).epsilon(1e-9));
        CHECK(s.curvature == doctest::Approx(1.0 / 60));
    }
    CHECK(outer.length() == doctest::Approx(120).epsilon(0.001));
    // Samples keep the centreline s so lanes line up.
    CHECK(outer.samples.back().s == doctest::Approx(100));
}

TEST_CASE("offset polyline chord error holds on the offset curve") {
    const Edge e = arc(50, 100);
    const SamplingParams p{0.01, 0.05, 25.0};
    for (double d : {10.0, -10.0, 30.0}) {
        const auto o = sample_offset(e, d, p);
        const double r = 50 - d;
        for (std::size_t i = 1; i < o.samples.size(); ++i) {
            const Vec2 mid = (o.samples[i].pos.xy() + o.samples[i - 1].pos.xy()) * 0.5;
            CHECK(r - (mid - Vec2{0, 50}).length() <= p.max_chord_error + 1e-9);
        }
    }
}

TEST_CASE("fold detection on an arc") {
    const Edge e = arc(50, 100);
    CHECK(find_folds(e, 10).empty());
    CHECK(find_folds(e, -80).empty());      // outside never folds
    CHECK(find_folds(e, 49.9).empty());
    auto exact = find_folds(e, 50);          // offset * curvature == 1
    REQUIRE(exact.size() == 1);
    CHECK(exact[0].s0 == doctest::Approx(0));
    CHECK(exact[0].s1 == doctest::Approx(100));
    CHECK(sample_offset(e, 60).has_fold());
    for (const auto& s : sample_offset(e, 60).samples) CHECK(s.folded);
    CHECK(find_folds(e, 0).empty());

    // Right-turning arc folds for negative (right) offsets only.
    const Edge r = arc(-50, 100);
    CHECK(find_folds(r, 60).empty());
    CHECK(find_folds(r, -60).size() == 1);
}

TEST_CASE("fold detection on a clothoid reports the exact range") {
    // Curvature 0 -> 0.04 over 100 m. An offset of 40 m folds where k >= 1/40, i.e. s >= 62.5.
    const Edge e = make_edge(100, 0, 0.04);
    const auto folds = find_folds(e, 40);
    REQUIRE(folds.size() == 1);
    CHECK(folds[0].s0 == doctest::Approx(62.5));
    CHECK(folds[0].s1 == doctest::Approx(100));
    CHECK(find_folds(e, 20).empty());
    CHECK(find_folds(e, -40).empty());

    const auto o = sample_offset(e, 40);
    CHECK(o.has_fold());
    bool saw_start = false;
    for (const auto& s : o.samples) {
        CHECK(s.folded == (s.s >= 62.5 - 1e-9));
        saw_start = saw_start || std::abs(s.s - 62.5) < 1e-9;
    }
    CHECK(saw_start);  // a sample lands exactly where the fold begins
}

TEST_CASE("step refines as the offset approaches a fold") {
    const Edge e = arc(50, 100);
    const SamplingParams p{1.0, 1.0, 25.0};  // loose: only the fold refinement bites
    const auto far = sample_offset(e, 5.0, p);
    const auto near = sample_offset(e, 45.0, p);
    CHECK(near.samples.size() > far.samples.size());
}

TEST_CASE("fold ranges merge across spans") {
    Edge e = arc(50, 50);
    Clothoid c2 = e.horizontal.spans[0];
    c2.length = 50;
    e.horizontal.spans.push_back(c2);
    e.vertical = VerticalProfile::build({0, 100}, {0, 0}, {});
    const auto folds = find_folds(e, 60);
    REQUIRE(folds.size() == 1);
    CHECK(folds[0].s1 == doctest::Approx(100));
}

TEST_CASE("vertical curve K comes from the edge's road type") {
    auto load = load_region_pack_by_id(CP_DATA_DIR, "uk");
    REQUIRE(load);
    auto pack = std::make_shared<const RegionPack>(*load);

    auto build = [&](const char* type, bool with_region) {
        RoadGraph g;
        if (with_region) g.set_region(pack);
        const NodeId a = g.insert_node({{}, {0, 0, 0}});
        const NodeId b = g.insert_node({{}, {400, 0, 0}});
        Edge e;
        e.a = a;
        e.b = b;
        e.interior = {{200, 0, 2}};  // +1% then -1%: a crest with delta grade 2%
        e.road_type = type;
        const EdgeId id = g.insert_edge(e);
        REQUIRE(id);
        return g.edge(id)->vertical.breaks.at(1).length;
    };
    CHECK(build("residential", true) == doctest::Approx(10 * 2.0));   // K crest 10
    CHECK(build("motorway", true) == doctest::Approx(182 * 2.0).epsilon(0.5));  // clipped by spans
    CHECK(build("residential", false) == doctest::Approx(30 * 2.0));  // fallback K 30
    CHECK(build("no_such_type", true) == doctest::Approx(30 * 2.0));  // unknown type: fallback

    // set_region refits existing edges.
    RoadGraph g;
    const NodeId a = g.insert_node({{}, {0, 0, 0}});
    const NodeId b = g.insert_node({{}, {400, 0, 0}});
    Edge e;
    e.a = a;
    e.b = b;
    e.interior = {{200, 0, 2}};
    e.road_type = "residential";
    const EdgeId id = g.insert_edge(e);
    CHECK(g.edge(id)->vertical.breaks.at(1).length == doctest::Approx(60));
    g.set_region(pack);
    CHECK(g.edge(id)->vertical.breaks.at(1).length == doctest::Approx(20));
}
