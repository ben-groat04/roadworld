// splitmix64 mapped to doubles by hand: identical sequences on every standard library
// (std::uniform_real_distribution and friends are implementation-defined).
#pragma once
#include <cstdint>

struct PortableRng {
    std::uint64_t s;
    std::uint64_t next() {
        std::uint64_t z = (s += 0x9E3779B97F4A7C15ull);
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
        return z ^ (z >> 31);
    }
    double uniform(double lo, double hi) {
        return lo + (hi - lo) * (static_cast<double>(next() >> 11) * (1.0 / 9007199254740992.0));
    }
};
