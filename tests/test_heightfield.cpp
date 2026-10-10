#include "doctest/doctest.h"
#include "cp/heightfield.h"

using namespace cp;

TEST_CASE("heightfield bilinear is exact on a plane and clamps outside") {
    Heightfield hf({10, 20}, 2.0, 5, 4);
    for (int j = 0; j < 4; ++j)
        for (int i = 0; i < 5; ++i)
            hf.set(i, j, static_cast<float>(0.5 * (10 + 2 * i) + 0.25 * (20 + 2 * j)));
    CHECK(hf.height(13.3, 24.1) == doctest::Approx(0.5 * 13.3 + 0.25 * 24.1).epsilon(1e-6));
    CHECK(hf.height(10, 20) == doctest::Approx(10.0));
    CHECK(hf.height(-100, 20) == doctest::Approx(hf.height(10, 20)));
    CHECK(Heightfield().height(1, 1) == 0.0);
}
