// [A] Lane connectors. Stub: classify_turn is real (it defines the nearside/offside
// semantics); the generators return empty results until A implements them.
#include <cmath>

#include "cp/junction.h"

namespace cp {

TurnKind classify_turn(double turn, DrivingSide side) {
    const double t = wrap_angle(turn);
    const double a = std::fabs(t);
    if (a < kPi / 6) return TurnKind::Straight;
    if (a > kPi * 5 / 6) return TurnKind::UTurn;
    const bool left = t > 0;
    const bool nearside = left == (side == DrivingSide::Left);
    return nearside ? TurnKind::Nearside : TurnKind::Offside;
}

bool turn_is_left(TurnKind kind, DrivingSide side) {
    return (kind == TurnKind::Nearside) == (side == DrivingSide::Left);
}

void generate_connectors(Junction& junction, DrivingSide, const JunctionOverrides&) {
    junction.connectors.clear();  // TODO(A)
}

std::vector<Vec3> make_connector_path(const JunctionArm&, const LaneRef&, double, const JunctionArm&, const LaneRef&,
                                      double, const std::vector<Vec3>*) {
    return {};  // TODO(A)
}

}  // namespace cp
