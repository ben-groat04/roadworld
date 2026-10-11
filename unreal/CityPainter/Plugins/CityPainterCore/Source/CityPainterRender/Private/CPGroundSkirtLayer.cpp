#include "CPGroundSkirtLayer.h"

#include "CPFrame.h"
#include "CPRenderSettings.h"
#include "CPSimWorld.h"
#include "CityPainterRender.h"

#include "Materials/MaterialInterface.h"

#include <algorithm>
#include <cmath>

namespace
{
using CPFrame::FD3;

// Accumulates quads given in the sim frame, counter-clockwise seen from above (A bottom-left, B bottom-right, C top-right,
// D top-left, east right and north up), relative to an origin; the same winding rule as the terrain mesher.
class FQuadBuilder
{
public:
	FQuadBuilder(FCPSimWorld& InSim, const FD3& InOriginSim) : Sim(InSim), OriginSim(InOriginSim)
	{
		const FD3 O = CPFrame::SimToUE(OriginSim);
		Out.OriginUeCm[0] = O.X;
		Out.OriginUeCm[1] = O.Y;
		Out.OriginUeCm[2] = O.Z;
	}

	void Quad(double Ax, double Ay, double Bx, double By, double Cx, double Cy, double Dx, double Dy, const FD3& HeightFrom, const FD3& HeightTo)
	{
		const std::int32_t Base = static_cast<std::int32_t>(Out.Position.size());
		// Heights: every corner takes the edge height of its own nearest edge sample; HeightFrom / HeightTo pick it.
		const double Xs[4] = {Ax, Bx, Cx, Dx}, Ys[4] = {Ay, By, Cy, Dy};
		for (int I = 0; I < 4; ++I)
		{
			const double Hx = std::clamp(Xs[I], HeightFrom.X, HeightTo.X), Hy = std::clamp(Ys[I], HeightFrom.Y, HeightTo.Y);
			Push(Xs[I], Ys[I], Sim.HeightM(Hx, Hy), Hx, Hy);
		}
		Out.Indices.insert(Out.Indices.end(), {Base, Base + 1, Base + 2, Base, Base + 2, Base + 3});
	}

	FCPMeshSection& Section() { return Out; }

private:
	void Push(double X, double Y, double Z, double Hx, double Hy)
	{
		const FD3 P = CPFrame::SimToUE({X - OriginSim.X, Y - OriginSim.Y, Z - OriginSim.Z});
		Out.Position.push_back({static_cast<float>(P.X), static_cast<float>(P.Y), static_cast<float>(P.Z)});
		// Normal from the terrain gradient at the edge sample (the extension itself is flat outward).
		const double E = 1.0, Gx = (Sim.HeightM(Hx + E, Hy) - Sim.HeightM(Hx - E, Hy)) / (2 * E), Gy = (Sim.HeightM(Hx, Hy + E) - Sim.HeightM(Hx, Hy - E)) / (2 * E);
		const double L = std::sqrt(Gx * Gx + Gy * Gy + 1.0);
		const FD3 N = CPFrame::SimDirToUE({-Gx / L, -Gy / L, 1.0 / L});
		Out.Normal.push_back({static_cast<float>(N.X), static_cast<float>(N.Y), static_cast<float>(N.Z)});
		const FD3 T = CPFrame::SimDirToUE({1.0, 0.0, 0.0});
		Out.TangentX.push_back({static_cast<float>(T.X), static_cast<float>(T.Y), static_cast<float>(T.Z)});
		Out.UV0.push_back({static_cast<float>(X), static_cast<float>(Y)});
	}

	FCPSimWorld& Sim;
	FD3 OriginSim;
	FCPMeshSection Out;
};
}  // namespace

void FCPGroundSkirtLayer::Build(const FCPRenderContext& Context)
{
	Teardown();
	const UCPRenderSettings* Settings = GetDefault<UCPRenderSettings>();
	if (!Context.Sim || !Context.Root || Settings->SkirtDistanceM <= 0.0)
	{
		return;
	}
	const FCPTerrainGrid& G = Context.Sim->TerrainGrid();
	const double R = Settings->SkirtDistanceM;
	const double Z0 = Context.Sim->HeightM(G.MinX, G.MinY);
	FQuadBuilder B(*Context.Sim, FD3{G.MinX, G.MinY, Z0});
	// The clamp box of the heights is the terrain rectangle: an outer vertex takes the height of the closest edge point.
	const FD3 Lo{G.MinX, G.MinY, 0.0}, Hi{G.MaxX, G.MaxY, 0.0};
	for (int I = 0; I < G.Nx - 1; ++I)
	{
		const double X0 = G.MinX + I * G.CellM, X1 = G.MinX + (I + 1) * G.CellM;
		B.Quad(X0, G.MinY - R, X1, G.MinY - R, X1, G.MinY, X0, G.MinY, Lo, Hi);   // south
		B.Quad(X0, G.MaxY, X1, G.MaxY, X1, G.MaxY + R, X0, G.MaxY + R, Lo, Hi);   // north
	}
	for (int J = 0; J < G.Ny - 1; ++J)
	{
		const double Y0 = G.MinY + J * G.CellM, Y1 = G.MinY + (J + 1) * G.CellM;
		B.Quad(G.MinX - R, Y0, G.MinX, Y0, G.MinX, Y1, G.MinX - R, Y1, Lo, Hi);   // west
		B.Quad(G.MaxX, Y0, G.MaxX + R, Y0, G.MaxX + R, Y1, G.MaxX, Y1, Lo, Hi);   // east
	}
	B.Quad(G.MinX - R, G.MinY - R, G.MinX, G.MinY - R, G.MinX, G.MinY, G.MinX - R, G.MinY, Lo, Hi);   // south-west corner
	B.Quad(G.MaxX, G.MinY - R, G.MaxX + R, G.MinY - R, G.MaxX + R, G.MinY, G.MaxX, G.MinY, Lo, Hi);   // south-east
	B.Quad(G.MaxX, G.MaxY, G.MaxX + R, G.MaxY, G.MaxX + R, G.MaxY + R, G.MaxX, G.MaxY + R, Lo, Hi);   // north-east
	B.Quad(G.MinX - R, G.MaxY, G.MinX, G.MaxY, G.MinX, G.MaxY + R, G.MinX - R, G.MaxY + R, Lo, Hi);   // north-west

	FCPMeshSection& Section = B.Section();
	Section.bTangentFlipY = true;
	UMaterialInterface* Material = Settings->GroundMaterial.LoadSynchronous();
	Mesh = ICPTileMesh::Create(*Context.Root, FName(TEXT("GroundSkirt")));
	Mesh->SetSection(0, Section, Material);
	UE_LOG(LogCPRender, Log, TEXT("Ground skirt: %.0f m beyond the world edge, %llu triangles"), R, static_cast<unsigned long long>(Section.TriangleCount()));
}

void FCPGroundSkirtLayer::Teardown()
{
	if (Mesh)
	{
		Mesh->Clear();
	}
	Mesh.Reset();
}
