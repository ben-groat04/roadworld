// [B] Junction geometry. compute_trims is real (it is the contract with A); the rest is a stub.
#include <algorithm>
#include <cmath>

#include "cp/junction.h"

namespace cp {

std::vector<double> compute_trims(const std::vector<JunctionArm>& arms, const std::vector<double>& radii) {
    const std::size_t n = arms.size();
    if (radii.size() != n) return {};
    std::vector<double> trims(n, 0.0);
    for (std::size_t k = 0; k < n; ++k) {
        const std::size_t j = (k + 1) % n;
        double theta = arms[j].heading_out - arms[k].heading_out;
        if (theta <= 0) theta += 2 * kPi;
        if (theta >= kPi - 0.01) continue;  // straight or reflex: no corner
        const double a = arms[k].kerb_left, b = arms[j].kerb_right;
        const double sn = std::sin(theta), cot = std::cos(theta) / sn;
        const double fillet = radii[k] / std::tan(theta / 2);
        trims[k] = std::max(trims[k], a * cot + b / sn + fillet);
        trims[j] = std::max(trims[j], a / sn + b * cot + fillet);
    }
    for (std::size_t i = 0; i < n; ++i)
        trims[i] = std::clamp(trims[i], 0.0, std::min(arms[i].edge_length / 2, kArmSampleLength));
    return trims;
}

void generate_geometry(Junction& junction, const RoadGraph&, const RegionPack&, const JunctionOverrides&) {
    // TODO(B): corners (kerb fillets), carriageway/pavement polygons via Clipper2, crossings.
    junction.corners.clear();
    junction.carriageway.clear();
    junction.pavements.clear();
    junction.crossings.clear();
}

}  // namespace cp
