#include "cp/lanes.h"

#include <algorithm>

namespace cp {

namespace {

TravelDir flip(TravelDir d) {
    if (d == TravelDir::Forward) return TravelDir::Backward;
    if (d == TravelDir::Backward) return TravelDir::Forward;
    return d;
}

Lane make_lane(const CrossSectionSlot& s, int source, double offset, TravelDir dir) {
    Lane l;
    l.source_slot = source;
    l.lane_type = s.lane_type;
    l.offset = offset;
    l.width = s.width;
    l.dir = dir;
    return l;
}

// +1 when the nearside of travel a -> b is on the left.
double nearside_sign(DrivingSide side) { return side == DrivingSide::Left ? 1.0 : -1.0; }

// Whole cross-section listed offside -> nearside, centred on the centreline.
std::vector<Lane> centred(const std::vector<CrossSectionSlot>& slots, DrivingSide side) {
    double total = 0;
    for (const auto& s : slots) total += s.width;
    std::vector<Lane> out;
    double u = 0;  // distance from the offside edge
    for (std::size_t i = 0; i < slots.size(); ++i) {
        const double centre = u + slots[i].width / 2 - total / 2;  // toward nearside
        out.push_back(make_lane(slots[i], static_cast<int>(i), nearside_sign(side) * centre, slots[i].dir));
        u += slots[i].width;
    }
    return out;
}

// Half cross-section (centre -> nearside) for forward travel, mirrored for backward.
std::vector<Lane> two_way(const std::vector<CrossSectionSlot>& half, DrivingSide side) {
    std::vector<Lane> out;
    double d = 0;
    for (std::size_t i = 0; i < half.size(); ++i) {
        const double centre = d + half[i].width / 2;
        const int src = static_cast<int>(i);
        const double sgn = nearside_sign(side);
        out.push_back(make_lane(half[i], src, sgn * centre, half[i].dir));
        out.push_back(make_lane(half[i], src, -sgn * centre, flip(half[i].dir)));
        d += half[i].width;
    }
    return out;
}

}  // namespace

std::vector<Lane> instantiate_lanes(const Edge& e, const RoadType& type, DrivingSide side) {
    std::vector<Lane> lanes;
    if (type.dual) lanes = centred(type.carriageway, side);
    else if (type.two_way && !e.one_way) lanes = two_way(type.half, side);
    else lanes = centred(type.one_way_variant, side);
    std::stable_sort(lanes.begin(), lanes.end(),
                     [](const Lane& a, const Lane& b) { return a.offset > b.offset; });
    for (std::size_t i = 0; i < lanes.size(); ++i) lanes[i].slot = static_cast<int>(i);
    return lanes;
}

double cross_section_width(const std::vector<Lane>& lanes) {
    double w = 0;
    for (const Lane& l : lanes) w += l.width;
    return w;
}

}  // namespace cp
