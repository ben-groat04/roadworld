// [C] Merge / diverge nodes. Stub.
#include "cp/junction.h"

namespace cp {

void generate_merge_diverge(Junction& junction, const RoadGraph&, const RegionPack&, const JunctionOverrides&) {
    // TODO(C): trims (gore point), connectors incl. lane add/drop, corners, polygons, crossings.
    junction.trims.assign(junction.arms.size(), 0.0);
}

}  // namespace cp
