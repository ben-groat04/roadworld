#include "CPTerrainMesher.h"

#include "CPFrame.h"

#include "cp/heightfield.h"

#include <algorithm>
#include <cmath>

namespace CPTerrain
{

namespace
{
// Unit surface normal (sim frame) from central differences over the full field (one-sided at the border).
CPFrame::FD3 SimNormal(const cp::Heightfield& F, int I, int J)
{
	const int I0 = std::max(I - 1, 0), I1 = std::min(I + 1, F.nx() - 1);
	const int J0 = std::max(J - 1, 0), J1 = std::min(J + 1, F.ny() - 1);
	const double Dx = (F.at(I1, J) - F.at(I0, J)) / ((I1 - I0) * F.cell());
	const double Dy = (F.at(I, J1) - F.at(I, J0)) / ((J1 - J0) * F.cell());
	const double L = std::sqrt(Dx * Dx + Dy * Dy + 1.0);
	return {-Dx / L, -Dy / L, 1.0 / L};
}
}  // namespace

void BuildTile(const cp::Heightfield& F, int CellI0, int CellJ0, int Cells, FCPMeshSection& Out)
{
	Out = FCPMeshSection();
	const int I0 = std::clamp(CellI0, 0, F.nx() - 1), J0 = std::clamp(CellJ0, 0, F.ny() - 1);
	const int I1 = std::min(I0 + Cells, F.nx() - 1), J1 = std::min(J0 + Cells, F.ny() - 1);
	if (I1 <= I0 || J1 <= J0)
	{
		return;
	}
	const int Nx = I1 - I0 + 1, Ny = J1 - J0 + 1;
	// Tile origin: the south-west corner at its own height (UE cm, double); vertices are float and relative to it.
	const CPFrame::FD3 OriginSim{F.origin().x + I0 * F.cell(), F.origin().y + J0 * F.cell(), static_cast<double>(F.at(I0, J0))};
	const CPFrame::FD3 OriginUe = CPFrame::SimToUE(OriginSim);
	Out.OriginUeCm[0] = OriginUe.X;
	Out.OriginUeCm[1] = OriginUe.Y;
	Out.OriginUeCm[2] = OriginUe.Z;
	const std::size_t NumVerts = static_cast<std::size_t>(Nx) * Ny;
	Out.Position.reserve(NumVerts);
	Out.Normal.reserve(NumVerts);
	Out.TangentX.reserve(NumVerts);
	Out.UV0.reserve(NumVerts);
	for (int J = J0; J <= J1; ++J)
	{
		for (int I = I0; I <= I1; ++I)
		{
			const double X = F.origin().x + I * F.cell(), Y = F.origin().y + J * F.cell(), Z = F.at(I, J);
			const CPFrame::FD3 P = CPFrame::SimToUE({X - OriginSim.X, Y - OriginSim.Y, Z - OriginSim.Z});
			Out.Position.push_back({static_cast<float>(P.X), static_cast<float>(P.Y), static_cast<float>(P.Z)});
			const CPFrame::FD3 N = SimNormal(F, I, J);
			const CPFrame::FD3 Nu = CPFrame::SimDirToUE(N);
			Out.Normal.push_back({static_cast<float>(Nu.X), static_cast<float>(Nu.Y), static_cast<float>(Nu.Z)});
			// +u = sim east, made perpendicular to the normal.
			CPFrame::FD3 T{1.0 - N.X * N.X, -N.X * N.Y, -N.X * N.Z};
			const double TL = std::sqrt(T.X * T.X + T.Y * T.Y + T.Z * T.Z);
			T = {T.X / TL, T.Y / TL, T.Z / TL};
			const CPFrame::FD3 Tu = CPFrame::SimDirToUE(T);
			Out.TangentX.push_back({static_cast<float>(Tu.X), static_cast<float>(Tu.Y), static_cast<float>(Tu.Z)});
			Out.UV0.push_back({static_cast<float>(X), static_cast<float>(Y)});
		}
	}
	// Two triangles per cell, counter-clockwise seen from above in the sim frame (east right, north up).
	Out.Indices.reserve(static_cast<std::size_t>(Nx - 1) * (Ny - 1) * 6);
	for (int J = 0; J < Ny - 1; ++J)
	{
		for (int I = 0; I < Nx - 1; ++I)
		{
			const std::int32_t A = J * Nx + I, B = A + 1, C = A + Nx + 1, D = A + Nx;
			Out.Indices.insert(Out.Indices.end(), {A, B, C, A, C, D});
		}
	}
	Out.bTangentFlipY = true;  // sim bitangent w = +1 (see Legacy/CPRoadMeshBuild.h)
}

}  // namespace CPTerrain
