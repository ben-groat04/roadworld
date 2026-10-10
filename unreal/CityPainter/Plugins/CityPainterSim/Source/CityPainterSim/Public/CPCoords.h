#pragma once

#include "CoreMinimal.h"

// Coordinate conversion between sim_core and Unreal. Single source of truth.
//
// sim_core: metres, x east, y north, z up, RIGHT-handed. Heading is radians CCW from +x (east).
// Unreal:   centimetres, LEFT-handed, X forward, Y right, Z up.
//
// Mapping (an X/Y swap, which is the reflection needed to change handedness):
//     UE.X = sim.y * 100   (north)
//     UE.Y = sim.x * 100   (east)
//     UE.Z = sim.z * 100   (up)
// So the map is the right way round seen from above: +X(UE) = north, +Y(UE) = east, matching
// the usual GIS/compass layout. Nothing is mirrored, only the handedness convention changes.
//
// Heading: sim heading h (CCW from east) -> UE yaw degrees (clockwise from +X/north toward +Y/east)
//     YawDeg = 90 - RadiansToDegrees(h)
//
// Keep every conversion in this file. All values are double (UE5 FVector is double).
namespace CPCoords
{
	constexpr double MetresToCm = 100.0;

	inline FVector SimToUE(double X, double Y, double Z)
	{
		return FVector(Y * MetresToCm, X * MetresToCm, Z * MetresToCm);
	}

	inline void UEToSim(const FVector& P, double& X, double& Y, double& Z)
	{
		X = P.Y / MetresToCm;
		Y = P.X / MetresToCm;
		Z = P.Z / MetresToCm;
	}

	inline double SimHeadingToYawDeg(double HeadingRad)
	{
		return 90.0 - FMath::RadiansToDegrees(HeadingRad);
	}
}
