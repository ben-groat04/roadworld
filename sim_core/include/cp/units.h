#pragma once
// Unit helpers shared by the data loaders. Internal units are metres, m/s, radians.
#include <string>

namespace cp {

// Metres per second in one unit of a pack's speed_unit. Accepted spellings (the one table every loader uses):
// "mph", "km/h", "kph", "kmh", "m/s", "mps", "ms". Returns false (and leaves `out` unchanged) for anything else.
inline bool speed_unit_to_mps(const std::string& unit, double& out) {
    if (unit == "mph") out = 0.44704;
    else if (unit == "km/h" || unit == "kph" || unit == "kmh") out = 1.0 / 3.6;
    else if (unit == "m/s" || unit == "mps" || unit == "ms") out = 1.0;
    else return false;
    return true;
}

inline constexpr const char* kSpeedUnitSpellings = "mph, km/h, kph, kmh, m/s, mps or ms";

}  // namespace cp
