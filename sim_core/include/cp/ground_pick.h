#pragma once
// Ray vs terrain: where a pointer ray meets the heightfield. Pure query (doubles throughout, no mutation).
// Port of rayTerrain() in prototypes/road-draw/src/input.js, made robust: the ray is clipped to the heightfield
// footprint, marched in steps of at most half a cell of horizontal travel (so a grazing ray cannot hop over a
// ridge), and the bracket is bisected. The number of steps is bounded (GroundPickParams::max_steps).
#include <optional>

#include "cp/heightfield.h"
#include "cp/vec.h"

namespace cp {

struct Ray {
    Vec3 origin;
    Vec3 dir;  // need not be unit length; must be non-zero
};

struct GroundHit {
    Vec3 point;
    double distance = 0;  // along the (normalised) ray from the origin
};

struct GroundPickParams {
    double max_distance = 14000;  // m; JS: rayTerrain `max` = 14000
    int max_steps = 100000;       // hard bound on marching steps (the step grows if the ray is longer)
    int bisect_iterations = 60;
};

// Nearest hit with the ground along the ray, or nullopt when the ray misses the footprint, stays above the
// terrain, starts underneath it, or the heightfield is empty. A ray entering the footprint already below the
// surface (a cliff-like edge seen from outside) hits at the entry point.
std::optional<GroundHit> pick_ground(const Heightfield& hf, const Ray& ray, const GroundPickParams& p = {});

}  // namespace cp
