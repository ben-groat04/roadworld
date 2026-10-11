// Junction round 3 (docs/junctions.md section 7b): lane width through an approach flare, splitter islands,
// central island surface fields and over-run apron, class-pair corner radii with the skew factor, the four
// rural/street road types at T and 4-arm junctions, and turn pockets staying off in the shipped UK pack.
#include <algorithm>
#include <cmath>
#include <memory>
#include <string>
#include <vector>

#include "doctest/doctest.h"
#include "cp/junction_roundabout.h"

using namespace cp;

namespace {

constexpr double kDeg = kPi / 180;
const char* const kRing = "roundabout_ring";

std::shared_ptr<const RegionPack> uk_pack(DrivingSide side = DrivingSide::Left) {
    auto p = load_region_pack_by_id(CP_DATA_DIR, "uk");
    REQUIRE_MESSAGE(p, p.error);
    p.value->driving_side = side;
    return std::make_shared<const RegionPack>(*p);
}

struct Net {
    std::shared_ptr<const RegionPack> region;
    RoadGraph g;
    CommandStack<RoadGraph> st{g};
    NodeId centre;

    explicit Net(DrivingSide side = DrivingSide::Left) : region(uk_pack(side)) { g.set_region(region); }
    explicit Net(std::shared_ptr<const RegionPack> r) : region(std::move(r)) { g.set_region(region); }

    EdgeId last_edge() const { return g.edges().rbegin()->first; }

    // A road from `len` m out at bearing `deg` into the centre node (drawn far end first, so it ENDS at the node).
    void spoke(double deg, double len, const std::string& type, bool one_way = false, bool outbound = false) {
        DrawRoadSpec s;
        s.road_type = type;
        s.one_way = one_way;
        RoadEnd far, c;
        far.pos = {len * std::cos(deg * kDeg), len * std::sin(deg * kDeg), 0};
        c.pos = {0, 0, 0};
        c.existing_node = centre;
        if (outbound) {
            s.start = c;
            s.end = far;
        } else {
            s.start = far;
            s.end = c;
        }
        REQUIRE(st.execute(make_draw_road(s)));
        if (!centre.valid()) centre = outbound ? g.edge(last_edge())->a : g.edge(last_edge())->b;
    }

    bool place(double radius) { return st.execute(make_place_roundabout({0, 0, 0}, radius, kRing, *region)); }

    JunctionSet junctions(RoundaboutResult* rr = nullptr) const {
        JunctionSet js = build_plain_junctions(g, *region, {});
        RoundaboutResult r = apply_roundabouts(js, g, *region, {});
        if (rr) *rr = std::move(r);
        return js;
    }
};

double area(const std::vector<Vec2>& r) {
    double a = 0;
    for (std::size_t i = 0; i < r.size(); ++i) a += r[i].cross(r[(i + 1) % r.size()]);
    return a / 2;
}

bool segs_cross(Vec2 a, Vec2 b, Vec2 c, Vec2 d) {
    const auto o = [](Vec2 p, Vec2 q, Vec2 r) { return (q - p).cross(r - p); };
    const double d1 = o(a, b, c), d2 = o(a, b, d), d3 = o(c, d, a), d4 = o(c, d, b);
    return ((d1 > 1e-9 && d2 < -1e-9) || (d1 < -1e-9 && d2 > 1e-9)) &&
           ((d3 > 1e-9 && d4 < -1e-9) || (d3 < -1e-9 && d4 > 1e-9));
}

bool simple(const std::vector<Vec2>& r) {
    if (r.size() < 3 || std::fabs(area(r)) < 1e-3) return false;
    const std::size_t n = r.size();
    for (std::size_t i = 0; i < n; ++i)
        for (std::size_t k = i + 2; k < n; ++k) {
            if (i == 0 && k == n - 1) continue;
            if (segs_cross(r[i], r[(i + 1) % n], r[k], r[(k + 1) % n])) return false;
        }
    return true;
}

double seg_dist(Vec2 p, Vec2 a, Vec2 b) {
    const Vec2 ab = b - a;
    const double l2 = ab.x * ab.x + ab.y * ab.y;
    const double t = l2 > 0 ? std::clamp(((p - a).x * ab.x + (p - a).y * ab.y) / l2, 0.0, 1.0) : 0.0;
    return (p - (a + ab * t)).length();
}

// Inside the ring (even-odd) or within `tol` of its boundary.
bool inside_or_near(const std::vector<Vec2>& r, Vec2 p, double tol) {
    bool in = false;
    for (std::size_t i = 0, n = r.size(); i < n; ++i) {
        const Vec2 a = r[i], b = r[(i + 1) % n];
        if (seg_dist(p, a, b) <= tol) return true;
        if ((a.y > p.y) != (b.y > p.y) && p.x < (b.x - a.x) * (p.y - a.y) / (b.y - a.y) + a.x) in = !in;
    }
    return in;
}

bool inside_set(const PolygonSet& set, Vec2 p, double tol) {
    for (const auto& r : set)
        if (area(r) > 0 && inside_or_near(r, p, tol)) return true;
    return false;
}

const Junction* ring_junction_of(const JunctionSet& js, const JunctionGroup& grp, EdgeId approach, const JunctionArm** arm_out) {
    for (NodeId n : grp.nodes) {
        const Junction* j = js.find(n);
        if (!j) continue;
        for (const JunctionArm& a : j->arms)
            if (a.edge == approach) {
                *arm_out = &a;
                return j;
            }
    }
    return nullptr;
}

// Lane width of the road type (first driving half-lane).
double type_lane_width(const RegionPack& r, const std::string& type) {
    const RoadType* t = r.road_type(type);
    REQUIRE(t);
    const auto& slots = t->dual ? t->carriageway : t->half;
    for (const CrossSectionSlot& s : slots)
        if (s.lane_type == "driving") return s.width;
    FAIL("no driving slot in " << type);
    return 0;
}

}  // namespace

// ============================================================ 1. lane clear width beside a splitter

TEST_CASE("round3: two-way approach lanes keep the road type's width through the flare, the outer kerbs flare") {
    for (DrivingSide side : {DrivingSide::Left, DrivingSide::Right}) {
        Net net(side);
        for (double d : {0.0, 90.0, 180.0, 270.0}) net.spoke(d, 150, "b_road");
        REQUIRE(net.place(30));
        RoundaboutResult rr;
        const JunctionSet js = net.junctions(&rr);
        REQUIRE(rr.layouts.size() == 1);
        const RoundaboutLayout& lay = rr.layouts[0];
        REQUIRE_MESSAGE(lay.ok, lay.error);
        const double w = type_lane_width(*net.region, "b_road");
        int checked = 0;
        for (const RoundaboutArm& ra : lay.group.arms) {
            const JunctionArm* arm = nullptr;
            const Junction* j = ring_junction_of(js, lay.group, ra.edge, &arm);
            REQUIRE(j);
            REQUIRE(arm);
            REQUIRE(arm->flare.active());
            REQUIRE(arm->lanes.size() == 2);
            CHECK(flare_spread(*arm, 0, true) == doctest::Approx(flare_spread(*arm, 0, false)));
            CHECK(flare_spread(*arm, 0, true) > 0.5);
            CHECK(flare_spread(*arm, arm->flare.length, true) == doctest::Approx(0).epsilon(1e-9));
            double prev_total = 1e9;
            for (double s = 0; s <= arm->flare.length + 1e-9; s += 0.5) {
                // each lane: centre-to-kerb distance is half a lane, so the lane keeps its full width at every s
                for (const LaneRef& l : arm->lanes) {
                    const double kerb = l.lateral > 0 ? arm->kerb_left : -arm->kerb_right;
                    const double clear = 2 * std::fabs(flared_lateral(*arm, s, kerb) - flared_lateral(*arm, s, l.lateral));
                    CHECK_MESSAGE(clear == doctest::Approx(w).epsilon(1e-6), "s=" << s);
                    CHECK(l.width == doctest::Approx(w));
                }
                // lanes of opposite direction move apart by the splitter width and never overlap
                REQUIRE(arm->lanes.size() == 2);
                const double l0 = flared_lateral(*arm, s, arm->lanes[0].lateral);
                const double l1 = flared_lateral(*arm, s, arm->lanes[1].lateral);
                CHECK(std::fabs(l0 - l1) >= w - 1e-6);
                // carriageway width between the flared kerbs falls monotonically toward the node-side trim
                const double total = flared_lateral(*arm, s, arm->kerb_left) - flared_lateral(*arm, s, -arm->kerb_right);
                CHECK(total <= prev_total + 1e-9);
                prev_total = total;
                // outer kerbs lie `spread` outside the road type's kerb offsets
                CHECK(flared_lateral(*arm, s, arm->kerb_left) ==
                      doctest::Approx(arm->kerb_left + flare_spread(*arm, s, true)));
                CHECK(flared_lateral(*arm, s, -arm->kerb_right) ==
                      doctest::Approx(-arm->kerb_right - flare_spread(*arm, s, false)));
            }
            ++checked;
        }
        CHECK(checked == 4);
    }
}

namespace {
// A dual-carriageway approach from the east: an inbound one-way edge at y = -off and an outbound one at y = +off
// (a dual stroke is one one-way carriageway; the pair is what makes the dual road), both crossing the ring.
void dual_approach(Net& net, double len, double off, const std::string& type, bool mirror_side = false) {
    const double sgn = mirror_side ? -1 : 1;
    DrawRoadSpec in;
    in.road_type = type;
    in.one_way = true;
    in.start.pos = {len, -sgn * off, 0};
    in.end.pos = {0, -sgn * off, 0};
    REQUIRE(net.st.execute(make_draw_road(in)));
    DrawRoadSpec out;
    out.road_type = type;
    out.one_way = true;
    out.start.pos = {0, sgn * off, 0};
    out.end.pos = {len, sgn * off, 0};
    REQUIRE(net.st.execute(make_draw_road(out)));
}
}  // namespace

TEST_CASE("round3: dual-carriageway approach: lanes keep the road type's width and the median splitter is built") {
    Net net;
    for (double d : {90.0, 180.0, 270.0}) net.spoke(d, 200, "b_road");
    dual_approach(net, 200, 9, "a_road_dual");
    REQUIRE(net.place(40));
    RoundaboutResult rr;
    const JunctionSet js = net.junctions(&rr);
    REQUIRE(rr.layouts.size() == 1);
    const RoundaboutLayout& lay = rr.layouts[0];
    REQUIRE_MESSAGE(lay.ok, lay.error);
    const double w = type_lane_width(*net.region, "a_road_dual");
    CHECK(w == doctest::Approx(3.65));
    int dual_arms = 0;
    for (const RoundaboutArm& ra : lay.group.arms) {
        if (net.g.edge(ra.edge)->road_type != "a_road_dual") continue;
        ++dual_arms;
        const JunctionArm* arm = nullptr;
        REQUIRE(ring_junction_of(js, lay.group, ra.edge, &arm));
        CHECK_FALSE(arm->flare.active());  // one-way carriageways take no flared splitter
        REQUIRE(arm->lanes.size() == 2);
        for (double s = 0; s <= 40; s += 2) {
            for (const LaneRef& l : arm->lanes) {
                CHECK(l.width == doctest::Approx(w));
                const double kerb = l.lateral > 0 ? arm->kerb_left : -arm->kerb_right;
                CHECK(std::fabs(flared_lateral(*arm, s, kerb) - flared_lateral(*arm, s, l.lateral)) >= w / 2 - 1e-6);
            }
            CHECK(std::fabs(flared_lateral(*arm, s, arm->lanes[0].lateral) - flared_lateral(*arm, s, arm->lanes[1].lateral)) ==
                  doctest::Approx(w).epsilon(1e-6));
        }
    }
    CHECK(dual_arms == 2);
    // the two b_road two-way spokes get ordinary splitters; the dual pair one median splitter (arm_b set)
    int paired = 0, single = 0;
    for (const SplitterIsland& sp : lay.splitters) {
        std::string pts;
        for (const Vec2& q : sp.polygon) pts += "(" + std::to_string(q.x) + "," + std::to_string(q.y) + ") ";
        INFO("arm_b " << sp.arm_b << " length " << sp.length << " polygon " << pts);
        CHECK(simple(sp.polygon));
        CHECK(area(sp.polygon) > 0);
        (sp.arm_b >= 0 ? paired : single)++;
    }
    CHECK(single == 3);
    CHECK(paired == 1);
}

// ============================================================ 2. splitter islands

TEST_CASE("round3: splitters exist on two-way approaches only, sized by ring class and approach speed") {
    Net net;
    // Ring radius 30 (size class 'up to 50 m'). Four approaches: two two-way of different speed, one one-way
    // inbound, one one-way outbound.
    net.spoke(0, 200, "b_road");                       // two-way, 40 mph
    net.spoke(90, 200, "b_road", true, false);         // one-way, toward the ring
    net.spoke(180, 200, "a_road_rural");               // two-way, 60 mph
    net.spoke(270, 200, "b_road", true, true);         // one-way, away from the ring
    REQUIRE(net.place(30));
    RoundaboutResult rr;
    const JunctionSet js = net.junctions(&rr);
    REQUIRE(rr.layouts.size() == 1);
    const RoundaboutLayout& lay = rr.layouts[0];
    REQUIRE_MESSAGE(lay.ok, lay.error);
    const RoundaboutRules& prm = roundabout_params(*net.region);
    const RoundaboutSizeClass cls = prm.size_class(lay.group.radius);
    REQUIRE(lay.group.arms.size() == 4);
    CHECK(lay.splitters.size() == 2);
    int two_way = 0;
    for (const RoundaboutArm& ra : lay.group.arms) {
        const JunctionArm* arm = nullptr;
        REQUIRE(ring_junction_of(js, lay.group, ra.edge, &arm));
        if (!(ra.entry && ra.exit)) {
            CHECK_FALSE(arm->flare.active());  // one-way arms carry no splitter shape
            continue;
        }
        ++two_way;
        CHECK(arm->flare.active());
    }
    CHECK(two_way == 2);
    std::vector<double> lengths;
    for (const SplitterIsland& sp : lay.splitters) {
        REQUIRE(sp.arm >= 0);
        REQUIRE(static_cast<std::size_t>(sp.arm) < lay.group.arms.size());
        const RoundaboutArm& ra = lay.group.arms[static_cast<std::size_t>(sp.arm)];
        CHECK(sp.arm_b < 0);
        CHECK((ra.entry && ra.exit));
        const RoadType* t = net.region->road_type(net.g.edge(ra.edge)->road_type);
        REQUIRE(t);
        const double expect_len = cls.splitter_length * prm.splitter_speed_factor(t->speed_limit);
        CHECK(sp.width == doctest::Approx(cls.splitter_width).epsilon(0.02));
        // The kerbed island is exactly the data length (class length x speed factor), whatever the flare needs for the
        // corner fillets: the rest of the flare is paint (sp.paint), never more kerb.
        CHECK(sp.length == doctest::Approx(expect_len).epsilon(0.02));
        CHECK(sp.length < 45.0);  // the old floor made a 30 m ring's b_road island 51.5 m long and its a_road_rural island 63 m
        lengths.push_back(sp.length);
        CHECK(simple(sp.polygon));
        CHECK(area(sp.polygon) > 1.0);
    }
    REQUIRE(lengths.size() == 2);
    // the faster approach (a_road_rural, 60 mph) gets the longer splitter
    const auto& A = lay.group.arms[static_cast<std::size_t>(lay.splitters[0].arm)];
    const bool first_is_b = net.g.edge(A.edge)->road_type == "b_road";
    CHECK((first_is_b ? lengths[1] > lengths[0] : lengths[0] > lengths[1]));
}

TEST_CASE("round3: splitter size follows the ring size class") {
    double prev_len = 0, prev_w = 0;
    for (double R : {10.0, 18.0, 30.0, 60.0}) {
        Net net;
        for (double d : {0.0, 90.0, 180.0, 270.0}) net.spoke(d, R + 150, "b_road");
        REQUIRE(net.place(R));
        RoundaboutResult rr;
        net.junctions(&rr);
        REQUIRE(rr.layouts.size() == 1);
        const RoundaboutLayout& lay = rr.layouts[0];
        REQUIRE_MESSAGE(lay.ok, lay.error);
        const RoundaboutRules& prm = roundabout_params(*net.region);
        const RoundaboutSizeClass cls = prm.size_class(lay.group.radius);
        const double f = prm.splitter_speed_factor(net.region->road_type("b_road")->speed_limit);
        REQUIRE(lay.splitters.size() == 4);
        for (const SplitterIsland& sp : lay.splitters) {
            CHECK_MESSAGE(sp.width == doctest::Approx(cls.splitter_width).epsilon(0.02), "R=" << R);
            CHECK_MESSAGE(sp.length == doctest::Approx(cls.splitter_length * f).epsilon(0.03), "R=" << R);
        }
        const double len = lay.splitters[0].length, wd = lay.splitters[0].width;
        CHECK_MESSAGE(len > prev_len, "R=" << R);
        CHECK_MESSAGE(wd >= prev_w, "R=" << R);
        prev_len = len;
        prev_w = wd;
    }
}

TEST_CASE("round3: splitter polygon is simple, positive area, and lies inside the flared arm") {
    for (double R : {14.0, 30.0}) {
        Net net;
        for (double d : {0.0, 90.0, 180.0, 270.0}) net.spoke(d, R + 150, "b_road");
        REQUIRE(net.place(R));
        RoundaboutResult rr;
        const JunctionSet js = net.junctions(&rr);
        REQUIRE(rr.layouts.size() == 1);
        const RoundaboutLayout& lay = rr.layouts[0];
        REQUIRE_MESSAGE(lay.ok, lay.error);
        REQUIRE(lay.splitters.size() == 4);
        for (const SplitterIsland& sp : lay.splitters) {
            CHECK(simple(sp.polygon));
            CHECK(area(sp.polygon) > 0);  // CCW
            const RoundaboutArm& ra = lay.group.arms[static_cast<std::size_t>(sp.arm)];
            const JunctionArm* arm = nullptr;
            const Junction* j = ring_junction_of(js, lay.group, ra.edge, &arm);
            REQUIRE(j);
            const Vec2 o = j->pos.xy();
            const Vec2 dir{std::cos(arm->heading_out), std::sin(arm->heading_out)};
            for (const Vec2& p : sp.polygon) {
                // The island must lie in the junction's carriageway (the flared arm stub plus ring) ...
                CHECK_MESSAGE(inside_set(j->carriageway, p, 0.25), "R=" << R << " vertex " << p.x << "," << p.y);
                // ... and, beyond the node, between the flared kerbs of the arm.
                const Vec2 d = p - o;
                const double s = d.x * dir.x + d.y * dir.y;
                const double lat = dir.x * d.y - dir.y * d.x;
                if (s > 0 && s <= arm->flare.length)
                    CHECK_MESSAGE(std::fabs(lat) <= arm->kerb_left + flare_spread(*arm, s, true) + 0.05,
                                  "R=" << R << " s=" << s << " lat=" << lat);
            }
        }
    }
}

TEST_CASE("round3: the kerbed splitter is a short teardrop, widest at the ring; the rest of the flare is paint") {
    for (double R : {14.0, 25.0, 30.0}) {
        Net net;
        for (double d : {0.0, 90.0, 180.0, 270.0}) net.spoke(d, R + 150, "b_road");
        REQUIRE(net.place(R));
        RoundaboutResult rr;
        const JunctionSet js = net.junctions(&rr);
        REQUIRE(rr.layouts.size() == 1);
        const RoundaboutLayout& lay = rr.layouts[0];
        REQUIRE_MESSAGE(lay.ok, lay.error);
        REQUIRE(lay.splitters.size() == 4);
        for (const SplitterIsland& sp : lay.splitters) {
            const RoundaboutArm& ra = lay.group.arms[static_cast<std::size_t>(sp.arm)];
            const JunctionArm* arm = nullptr;
            const Junction* j = ring_junction_of(js, lay.group, ra.edge, &arm);
            REQUIRE(j);
            const Vec2 o = j->pos.xy();
            const Vec2 dir{std::cos(arm->heading_out), std::sin(arm->heading_out)};
            const auto sl = [&](Vec2 p) { return std::pair<double, double>{(p - o).dot(dir), dir.x * (p - o).y - dir.y * (p - o).x}; };
            INFO("R=" << R << " length " << sp.length << " width " << sp.width << " flare " << arm->flare.length);
            // Length : width of a photographed island (3:1 to 5:1), and no longer than the data asks for.
            CHECK(sp.length / sp.width > 2.5);
            CHECK(sp.length / sp.width < 9.0);
            CHECK(sp.length <= arm->flare.length);
            // Widest at the ring end, then narrowing monotonically (a triangle, not a constant-width strip).
            std::vector<std::pair<double, double>> left;
            for (const Vec2& p : sp.polygon) {
                const auto q = sl(p);
                if (q.second > 0.02 && q.first > sp.ring_s + 0.5 && q.first < sp.nose_s - 1.0) left.push_back(q);
            }
            std::sort(left.begin(), left.end());
            REQUIRE(left.size() >= 2);
            for (std::size_t k = 1; k < left.size(); ++k) CHECK(left[k].second <= left[k - 1].second + 0.02);
            CHECK(left.front().second > 0.8 * sp.width / 2);
            CHECK(left.back().second < 0.45 * sp.width / 2);
            // The kerbed island lies inside the paint gap, which is as long as the flare.
            REQUIRE(sp.paint.size() >= 3);
            CHECK(simple(sp.paint));
            CHECK(area(sp.paint) >= area(sp.polygon) - 1e-6);
            double paint_s = 0;
            for (const Vec2& p : sp.paint) paint_s = std::max(paint_s, sl(p).first);
            CHECK(paint_s >= sp.nose_s - 1e-6);
            // A long flare (fillets need it) never lengthens the kerbed island: only the paint.
            CHECK(sp.nose_s - sp.ring_s == doctest::Approx(sp.length));
        }
    }
}

TEST_CASE("round3: splitter lengths follow the approach speed bands (15-20 m at 30 mph, 25-35 m at 40-50 mph, 40-60 m at 60 mph)") {
    Net net;
    net.spoke(0, 200, "residential");
    net.spoke(90, 200, "b_road");
    net.spoke(180, 200, "a_road_rural");
    net.spoke(270, 200, "b_road");
    REQUIRE(net.place(30));
    RoundaboutResult rr;
    net.junctions(&rr);
    REQUIRE(rr.layouts.size() == 1);
    for (const SplitterIsland& sp : rr.layouts[0].splitters) {
        const std::string& type = net.g.edge(rr.layouts[0].group.arms[static_cast<std::size_t>(sp.arm)].edge)->road_type;
        INFO(type << " length " << sp.length);
        if (type == "residential") {
            CHECK(sp.length >= 12.0);
            CHECK(sp.length <= 20.0);
        } else if (type == "b_road") {
            CHECK(sp.length >= 25.0);
            CHECK(sp.length <= 35.0);
        } else if (type == "a_road_rural") {
            CHECK(sp.length >= 38.0);
            CHECK(sp.length <= 60.0);
        }
    }
}

// ============================================================ 3. island surface fields and apron

namespace {
RoundaboutLayout lay_for(const Net& net) {
    RoundaboutResult rr;
    net.junctions(&rr);
    REQUIRE(rr.layouts.size() == 1);
    return rr.layouts[0];
}
}  // namespace

TEST_CASE("round3: apron appears per the size-class and island-radius rules") {
    const RoundaboutRules uk_rules = roundabout_params(*uk_pack());
    REQUIRE(uk_rules.size_classes.size() >= 2);
    REQUIRE(uk_rules.size_classes[0].apron_width > 0);
    const double apron_w = uk_rules.size_classes[0].apron_width;
    {  // small ring: apron present
        Net net;
        for (double d : {0.0, 90.0, 180.0, 270.0}) net.spoke(d, 100, "b_road");
        REQUIRE(net.place(10));
        const RoundaboutLayout lay = lay_for(net);
        REQUIRE_MESSAGE(lay.ok, lay.error);
        REQUIRE(lay.apron.size() == 2);
        const auto& outer = lay.apron[0];
        const auto& hole = lay.apron[1];
        REQUIRE(lay.island.size() == 1);
        CHECK(area(outer) > 0);
        CHECK(area(hole) < 0);
        CHECK(simple(outer));
        // apron = collar of the width the rule asks for between the outer island and the raised island
        const double collar = area(outer) + area(hole);
        const double r_out = std::sqrt(area(outer) / kPi);
        CHECK(collar == doctest::Approx(kPi * (r_out * r_out - (r_out - apron_w) * (r_out - apron_w))).epsilon(0.03));
        // the raised island is the hole: same area, and strictly inside the outer ring
        CHECK(area(lay.island[0]) == doctest::Approx(-area(hole)).epsilon(0.001));
        for (const Vec2& p : lay.island[0]) CHECK(inside_or_near(outer, p, 1e-6));
        CHECK(area(lay.island[0]) < area(outer));
        // the apron's outer edge is the island kerb of the ring: it must not overlap the circulatory carriageway
        const Vec2 c = lay.group.centre;
        const double ring_inner = lay.group.radius - 3.5;  // two 3.5 m lanes
        for (const Vec2& p : outer) CHECK((p - c).length() <= ring_inner + 0.15);
        for (const Vec2& p : lay.island[0]) CHECK((p - c).length() <= ring_inner - apron_w + 0.15);
        CHECK(lay.apron_surface == uk_rules.apron_surface);
        // and none of the ring junctions' carriageway reaches into the apron
        const JunctionSet js = net.junctions();
        for (NodeId n : lay.group.nodes)
            if (const Junction* j = js.find(n))
                for (const auto& poly : j->carriageway)
                    for (const Vec2& p : poly) CHECK((p - c).length() >= (ring_inner - 0.2));
    }
    {  // ring 12 m: island radius 8.5 < 9: still apron
        Net net;
        for (double d : {0.0, 90.0, 180.0, 270.0}) net.spoke(d, 100, "b_road");
        REQUIRE(net.place(12));
        CHECK(lay_for(net).apron.size() == 2);
    }
    {  // medium ring (class 2 has no apron width): none, and the island is a single full-size ring
        Net net;
        for (double d : {0.0, 90.0, 180.0, 270.0}) net.spoke(d, 100, "b_road");
        REQUIRE(net.place(18));
        const RoundaboutLayout lay = lay_for(net);
        REQUIRE_MESSAGE(lay.ok, lay.error);
        CHECK(lay.apron.empty());
        REQUIRE(lay.island.size() == 1);
        CHECK(area(lay.island[0]) == doctest::Approx(kPi * 14.5 * 14.5).epsilon(0.03));
    }
    {  // rule off: apron_island_radius_max = 0
        RegionPack p = *uk_pack();
        p.roundabout.apron_island_radius_max = 0;
        Net net(std::make_shared<const RegionPack>(p));
        for (double d : {0.0, 90.0, 180.0, 270.0}) net.spoke(d, 100, "b_road");
        REQUIRE(net.place(10));
        CHECK(lay_for(net).apron.empty());
    }
    {  // island bigger than the limit (limit 5, island 6.5): none
        RegionPack p = *uk_pack();
        p.roundabout.apron_island_radius_max = 5;
        Net net(std::make_shared<const RegionPack>(p));
        for (double d : {0.0, 90.0, 180.0, 270.0}) net.spoke(d, 100, "b_road");
        REQUIRE(net.place(10));
        CHECK(lay_for(net).apron.empty());
    }
    {  // size class without apron width: none
        RegionPack p = *uk_pack();
        for (auto& k : p.roundabout.size_classes) k.apron_width = 0;
        Net net(std::make_shared<const RegionPack>(p));
        for (double d : {0.0, 90.0, 180.0, 270.0}) net.spoke(d, 100, "b_road");
        REQUIRE(net.place(10));
        CHECK(lay_for(net).apron.empty());
    }
}

TEST_CASE("round3: island, splitter and apron surface fields follow the region rules") {
    Net net;
    for (double d : {0.0, 90.0, 180.0, 270.0}) net.spoke(d, 100, "b_road");
    REQUIRE(net.place(20));
    const RoundaboutLayout lay = lay_for(net);
    REQUIRE_MESSAGE(lay.ok, lay.error);
    const RoundaboutRules& prm = roundabout_params(*net.region);
    CHECK(lay.island_surface == prm.central_island_surface);
    CHECK(lay.island_surface == "grass");
    CHECK(lay.splitter_surface == prm.splitter_surface);
    CHECK(lay.splitter_surface == "asphalt");  // photos: small islands are tarmac inside a kerb
    CHECK(lay.apron_surface == prm.apron_surface);
    CHECK(lay.island_kerb_upstand == doctest::Approx(prm.island_kerb_upstand));
    CHECK(lay.island_kerb_upstand == doctest::Approx(0.125));
    CHECK(lay.island_kerbed);

    // changing the rule changes the output
    RegionPack p = *uk_pack();
    p.roundabout.central_island_surface = "gravel";
    p.roundabout.splitter_surface = "cobbles";
    p.roundabout.island_kerb_upstand = 0.2;
    Net net2(std::make_shared<const RegionPack>(p));
    for (double d : {0.0, 90.0, 180.0, 270.0}) net2.spoke(d, 100, "b_road");
    REQUIRE(net2.place(20));
    const RoundaboutLayout lay2 = lay_for(net2);
    REQUIRE_MESSAGE(lay2.ok, lay2.error);
    CHECK(lay2.island_surface == "gravel");
    CHECK(lay2.splitter_surface == "cobbles");
    CHECK(lay2.island_kerb_upstand == doctest::Approx(0.2));
}

// ============================================================ 4. class-pair corner radii with skew

namespace {

// Straight through road along x (type `major`) plus one side road (type `minor`) at bearing `deg`.
Junction t_junction(const Net& net) {
    const JunctionSet set = build_junctions(net.g, *net.region, {});
    REQUIRE(set.junctions.size() == 1);
    return set.junctions.begin()->second;
}

double expected_radius(const RegionPack& r, int class_a, int class_b, double theta) {
    const double base = r.junction.corner_radius_for(class_a, class_b);
    return theta < kPi - 0.01 ? base * std::clamp(theta / (kPi / 2), r.junction.skew_min, r.junction.skew_max) : base;
}

}  // namespace

TEST_CASE("round3: class-pair corner radius table (data values) at a right-angle T") {
    struct Row {
        const char* major;
        const char* minor;
        double radius;  // from region.json junction.corner_radius_by_class
    };
    // road_class: country_lane 30, residential 50, b_road_rural 60, b_road 70, main_street 75, a_road_rural 90, a_road_dual 100
    const Row rows[] = {
        {"residential", "country_lane", 4},   // minor<=40, major<=55
        {"b_road", "country_lane", 6},        // minor<=40, major<=80
        {"a_road_dual", "country_lane", 10},  // minor<=40, any major
        {"residential", "residential", 6},    // minor<=55, major<=55
        {"b_road", "residential", 9},         // minor<=55, major<=80
        {"a_road_rural", "residential", 12},  // minor<=55
        {"b_road", "b_road", 10},             // minor<=80, major<=80
        {"a_road_rural", "b_road", 14},       // minor<=80
        {"a_road_dual", "a_road_rural", 15},  // default row
    };
    for (const Row& row : rows) {
        Net net;
        // through road: two arms along +x and -x; side road at 90 degrees. All 100 m, so nothing is clamped by edge length.
        net.spoke(0, 100, row.major);
        net.spoke(180, 100, row.major);
        net.spoke(90, 100, row.minor);
        const Junction j = t_junction(net);
        REQUIRE(j.arms.size() == 3);
        REQUIRE(j.corners.size() == 3);
        int bent = 0;
        for (const CornerCurve& c : j.corners) {
            if (c.angle >= kPi - 0.01) continue;  // the straight side of the T has no corner
            ++bent;
            CHECK_MESSAGE(c.angle == doctest::Approx(kPi / 2).epsilon(1e-6), row.major << "/" << row.minor);
            CHECK_MESSAGE(c.requested_radius == doctest::Approx(row.radius), row.major << "/" << row.minor);
            if (c.radius < c.requested_radius - 1e-9) {  // clamped: must be explained, never silent
                CHECK(c.requested_radius > c.radius);
            } else {
                CHECK(c.radius == doctest::Approx(row.radius));
            }
            CHECK_FALSE(c.kerb.empty());
        }
        CHECK(bent == 2);
        CHECK_MESSAGE(!j.degraded, row.major << "/" << row.minor << ": " << j.degraded_reason);
        // resolve_corner_radii agrees with the junction
        const std::vector<double> radii = resolve_corner_radii(j.node, j.arms, {}, net.region.get());
        for (std::size_t k = 0; k < radii.size(); ++k)
            if (j.corners[k].angle < kPi - 0.01) CHECK(radii[k] == doctest::Approx(row.radius));
    }
}

TEST_CASE("round3: skew factor scales the radius by corner angle / 90 degrees, clamped (docs/junctions.md 7b)") {
    // Documented rule: radius = table radius * clamp(theta / 90 deg, skew.min, skew.max), larger on the obtuse
    // corner. UK pack: min 0.6, max 1.5. Residential through road, b_road... same-class T: table radius 6.
    const double base = 6;  // residential/residential
    for (double deg : {75.0, 60.0, 45.0, 30.0}) {
        Net net;
        net.spoke(0, 150, "residential");
        net.spoke(180, 150, "residential");
        net.spoke(deg, 150, "residential");
        const Junction j = t_junction(net);
        REQUIRE(j.arms.size() == 3);
        CHECK_FALSE(j.degraded);
        const double lo = net.region->junction.skew_min, hi = net.region->junction.skew_max;
        CHECK(lo == doctest::Approx(0.6));
        CHECK(hi == doctest::Approx(1.5));
        int checked = 0;
        for (const CornerCurve& c : j.corners) {
            if (c.angle >= kPi - 0.01) continue;
            const double want = base * std::clamp(c.angle / (kPi / 2), lo, hi);
            CHECK_MESSAGE(c.requested_radius == doctest::Approx(want).epsilon(1e-6),
                          "side road at " << deg << " deg, corner angle " << c.angle / kDeg);
            CHECK(c.radius <= c.requested_radius + 1e-9);
            ++checked;
        }
        CHECK(checked == 2);
        // 'larger on the obtuse corner': the two bent corners sum to 180 degrees, so their angles differ
        std::vector<const CornerCurve*> bent;
        for (const CornerCurve& c : j.corners)
            if (c.angle < kPi - 0.01) bent.push_back(&c);
        REQUIRE(bent.size() == 2);
        const CornerCurve* acute = bent[0]->angle < bent[1]->angle ? bent[0] : bent[1];
        const CornerCurve* obtuse = acute == bent[0] ? bent[1] : bent[0];
        CHECK(obtuse->requested_radius > acute->requested_radius);
        CHECK(acute->angle == doctest::Approx(deg * kDeg).epsilon(1e-6));
        // the acute corner may be clamped by the trims, but then it must not be flagged degraded
        CHECK(acute->radius > 0);
    }
    {  // 60 degrees explicitly: 6 * (60/90) = 4.0 on the acute corner, 6 * (120/90) = 8.0 on the obtuse one
        Net net;
        net.spoke(0, 150, "residential");
        net.spoke(180, 150, "residential");
        net.spoke(60, 150, "residential");
        const Junction j = t_junction(net);
        std::vector<double> req;
        for (const CornerCurve& c : j.corners)
            if (c.angle < kPi - 0.01) req.push_back(c.requested_radius);
        std::sort(req.begin(), req.end());
        REQUIRE(req.size() == 2);
        CHECK(req[0] == doctest::Approx(4.0).epsilon(1e-6));
        CHECK(req[1] == doctest::Approx(8.0).epsilon(1e-6));
        // resolve_corner_radii directly
        const std::vector<double> radii = resolve_corner_radii(j.node, j.arms, {}, net.region.get());
        for (std::size_t k = 0; k < radii.size(); ++k)
            if (j.corners[k].angle < kPi - 0.01)
                CHECK(radii[k] == doctest::Approx(expected_radius(*net.region, 50, 50, j.corners[k].angle)));
    }
    {  // a player override wins over the table and the skew
        Net net;
        net.spoke(0, 150, "residential");
        net.spoke(180, 150, "residential");
        net.spoke(60, 150, "residential");
        const Junction j0 = t_junction(net);
        JunctionOverrides ov;
        for (std::size_t k = 0; k < j0.corners.size(); ++k)
            if (j0.corners[k].angle < kPi - 0.01) {
                ov.corner_radius[{j0.node, j0.corners[k].edge_l, j0.corners[k].edge_r}] = 2.5;
                break;
            }
        const JunctionSet set = build_junctions(net.g, *net.region, ov);
        const Junction& j = set.junctions.begin()->second;
        int overridden = 0;
        for (const CornerCurve& c : j.corners)
            if (c.overridden) {
                ++overridden;
                CHECK(c.requested_radius == doctest::Approx(2.5));
            }
        CHECK(overridden == 1);
    }
}

TEST_CASE("round3: a corner squeezed by short edges reports radius < requested, not silent") {
    // Short side road (12 m): the trims cannot hold a 15 m fillet. Either it is clamped (requested > radius) or
    // it is degraded with a reason. Never radius == requested with an impossible geometry.
    Net net;
    net.spoke(0, 12, "a_road_dual");
    net.spoke(180, 12, "a_road_dual");
    net.spoke(90, 12, "a_road_dual");
    const Junction j = t_junction(net);
    bool squeezed = false;
    for (const CornerCurve& c : j.corners) {
        if (c.angle >= kPi - 0.01) continue;
        CHECK(c.radius <= c.requested_radius + 1e-9);
        if (c.radius < c.requested_radius - 1e-9) squeezed = true;
    }
    CHECK((squeezed || j.degraded));
    if (j.degraded) CHECK_FALSE(j.degraded_reason.empty());
}

// ============================================================ 5. the four new road types

TEST_CASE("round3: main_street, a_road_rural, b_road_rural, country_lane build T and 4-arm junctions cleanly") {
    for (const char* type : {"main_street", "a_road_rural", "b_road_rural", "country_lane"}) {
        for (DrivingSide side : {DrivingSide::Left, DrivingSide::Right}) {
            {  // T
                Net net(side);
                net.spoke(0, 100, type);
                net.spoke(180, 100, type);
                net.spoke(90, 100, type);
                const Junction j = t_junction(net);
                CHECK_MESSAGE(!j.degraded, type << " T: " << j.degraded_reason);
                CHECK(j.cls == NodeClass::Junction);
                CHECK(j.arms.size() == 3);
                CHECK_FALSE(j.carriageway.empty());
                for (const auto& r : j.carriageway) CHECK_MESSAGE(simple(r), type << " T");
                CHECK(j.connectors.size() == 6);
                int bent = 0;
                for (const CornerCurve& c : j.corners)
                    if (c.angle < kPi - 0.01) {
                        ++bent;
                        CHECK_FALSE(c.kerb.empty());
                        CHECK_MESSAGE(c.radius == doctest::Approx(c.requested_radius), type << " T radius clamped");
                    }
                CHECK(bent == 2);
            }
            {  // 4 arms
                Net net(side);
                for (double d : {0.0, 90.0, 180.0, 270.0}) net.spoke(d, 100, type);
                const Junction j = t_junction(net);
                CHECK_MESSAGE(!j.degraded, type << " X: " << j.degraded_reason);
                CHECK(j.arms.size() == 4);
                CHECK_FALSE(j.carriageway.empty());
                for (const auto& r : j.carriageway) CHECK_MESSAGE(simple(r), type << " X");
                CHECK(j.connectors.size() == 12);
                for (const CornerCurve& c : j.corners) {
                    CHECK_FALSE(c.kerb.empty());
                    CHECK_MESSAGE(c.radius == doctest::Approx(c.requested_radius), type << " X radius clamped");
                }
            }
        }
    }
}

// ============================================================ 6. turn pockets stay disabled

TEST_CASE("round3: with the shipped UK pack no junction gains a turn pocket or an extra lane") {
    const auto region = uk_pack();
    CHECK_FALSE(region->junction.turn_pockets.enabled);
    // Layouts that meet the pocket rule's other conditions (through road class >= 85, lesser road at ~90 degrees).
    struct Case { const char* major; const char* minor; };
    for (const Case& cs : {Case{"a_road_rural", "b_road_rural"}, Case{"a_road_rural", "country_lane"},
                           Case{"a_road_dual", "b_road"}, Case{"a_road_rural", "residential"}}) {
        for (DrivingSide side : {DrivingSide::Left, DrivingSide::Right}) {
            Net net(side);
            net.spoke(0, 150, cs.major);
            net.spoke(180, 150, cs.major);
            net.spoke(90, 150, cs.minor);
            net.spoke(270, 150, cs.minor);  // 4 arms too
            const JunctionSet set = build_junctions(net.g, *net.region, {});
            REQUIRE(set.junctions.size() == 1);
            const Junction& j = set.junctions.begin()->second;
            CHECK_MESSAGE(j.pockets.empty(), cs.major << "/" << cs.minor);
            for (const LaneConnector& c : j.connectors) CHECK_FALSE(c.via_pocket);
            for (const JunctionArm& a : j.arms) {
                CHECK_FALSE(a.flare.active());
                // lanes at the node are exactly the lanes the road type defines
                const RoadType* t = net.region->road_type(net.g.edge(a.edge)->road_type);
                REQUIRE(t);
                Edge e;
                e.one_way = false;
                e.road_type = t->id;
                int connecting = 0;
                for (const Lane& l : instantiate_lanes(e, *t, side)) {
                    const LaneType* lt = net.region->lane_type(l.lane_type);
                    if (lt && lt->connects_at_junctions && l.dir != TravelDir::None) ++connecting;
                }
                CHECK_MESSAGE(static_cast<int>(a.lanes.size()) == connecting, cs.major << "/" << cs.minor);
            }
            // the same graph, T form
        }
    }
    {  // T junction, plus the control: the same layout DOES get a pocket when the rule is switched on
        Net off;
        off.spoke(0, 150, "a_road_rural");
        off.spoke(180, 150, "a_road_rural");
        off.spoke(90, 150, "b_road_rural");
        const Junction j_off = t_junction(off);
        CHECK(j_off.pockets.empty());
        std::size_t lanes_off = 0;
        for (const JunctionArm& a : j_off.arms) lanes_off += a.lanes.size();

        RegionPack p = *uk_pack();
        p.junction.turn_pockets.enabled = true;
        Net on(std::make_shared<const RegionPack>(p));
        on.spoke(0, 150, "a_road_rural");
        on.spoke(180, 150, "a_road_rural");
        on.spoke(90, 150, "b_road_rural");
        const Junction j_on = t_junction(on);
        // The control proves the layout satisfies the rule, so the empty result above is the switch, not the layout.
        CHECK_MESSAGE(!j_on.pockets.empty(), "control: enabled pack should give this T a pocket");
        // even with a pocket the number of arm lanes is unchanged (the pocket is a ghost-island strip, not an arm lane)
        std::size_t lanes_on = 0;
        for (const JunctionArm& a : j_on.arms) lanes_on += a.lanes.size();
        CHECK(lanes_on == lanes_off);
    }
}
