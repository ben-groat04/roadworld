#pragma once

// The ONE place where simulation coordinates are mapped to Unreal coordinates and back. It knows only plain doubles:
// no Unreal types, no sim_core types. Every other piece of code (World adapter, mesh builders, the legacy road
// build) calls these functions; nobody writes a coordinate swap, a unit scale or a handedness fix by hand.
//
//   sim  (metres, x east, y north, z up, right-handed)  <->  UE (centimetres, X north, Y east, Z up, left-handed)
//   UE.X = sim.y * 100,  UE.Y = sim.x * 100,  UE.Z = sim.z * 100
//   Directions use the same swap WITHOUT the scale: (dx, dy, dz) -> (dy, dx, dz).
//   Triangle winding is NOT changed (the swap and the left-handed frame cancel; see Legacy/CPRoadMeshBuild.h).
//
// It must not know about meshes, tiles, worlds or actors.

namespace CPFrame
{

struct FD3
{
	double X = 0, Y = 0, Z = 0;
};

constexpr double MetresToCm = 100.0;

constexpr FD3 SimToUE(FD3 SimMetres) { return {SimMetres.Y * MetresToCm, SimMetres.X * MetresToCm, SimMetres.Z * MetresToCm}; }
constexpr FD3 UEToSim(FD3 UeCm) { return {UeCm.Y / MetresToCm, UeCm.X / MetresToCm, UeCm.Z / MetresToCm}; }
constexpr FD3 SimDirToUE(FD3 D) { return {D.Y, D.X, D.Z}; }
constexpr FD3 UEDirToSim(FD3 D) { return {D.Y, D.X, D.Z}; }

}  // namespace CPFrame
