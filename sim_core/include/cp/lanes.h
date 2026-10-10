#pragma once
// Lane instantiation: turns an edge's road type cross-section into lane records.
// Lateral offsets are of the lane centre from the edge centreline, + = left of a -> b.
// Lanes are numbered left to right across the whole cross-section (slot 0 is the
// leftmost strip when looking from a toward b).
#include <string>
#include <vector>

#include "cp/region_pack.h"
#include "cp/road_graph.h"

namespace cp {

struct Lane {
    int slot = 0;         // index across the cross-section, left to right
    int source_slot = 0;  // index into the road type list it came from (half, variant or carriageway)
    std::string lane_type;
    double offset = 0;  // m, centre of the lane from the centreline, + = left of a -> b
    double width = 0;   // m
    TravelDir dir = TravelDir::None;
};

// Two-way types: the half cross-section is placed on the driving side for the forward
// direction and mirrored for the other; the edge centreline is the middle line.
// One-way edges of a two-way type use one_way_variant; dual types use carriageway.
// Both are centred on the edge centreline (the dual carriageway edge is placed that way)
// and list strips offside -> nearside, where nearside is left for Left driving.
std::vector<Lane> instantiate_lanes(const Edge& e, const RoadType& type, DrivingSide side);

// Total width of the lanes' cross-section.
double cross_section_width(const std::vector<Lane>& lanes);

}  // namespace cp
