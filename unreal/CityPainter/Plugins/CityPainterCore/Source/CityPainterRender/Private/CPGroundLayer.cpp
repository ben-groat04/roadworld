#include "CPGroundLayer.h"

#include "CPRenderSettings.h"
#include "CPSimWorld.h"
#include "CityPainterRender.h"

#include "HAL/PlatformTime.h"
#include "Materials/MaterialInterface.h"

void FCPGroundLayer::Build(const FCPRenderContext& Context)
{
	Teardown();
	if (!Context.Sim || !Context.Root)
	{
		return;
	}
	const UCPRenderSettings* Settings = GetDefault<UCPRenderSettings>();
	UMaterialInterface* Material = Settings->GroundMaterial.LoadSynchronous();
	if (!Material)
	{
		UE_LOG(LogCPRender, Warning, TEXT("Ground material %s not found; tiles use the default material"), *Settings->GroundMaterial.ToString());
	}

	const FCPTerrainGrid& Grid = Context.Sim->TerrainGrid();
	const int32 Cells = Settings->TerrainTileCells;
	const int32 TilesX = (Grid.Nx - 1 + Cells - 1) / Cells;
	const int32 TilesY = (Grid.Ny - 1 + Cells - 1) / Cells;

	const double T0 = FPlatformTime::Seconds();
	double SimSeconds = 0.0;
	int64 Triangles = 0, Vertices = 0;
	FCPMeshSection Section;
	for (int32 J = 0; J < TilesY; ++J)
	{
		for (int32 I = 0; I < TilesX; ++I)
		{
			const double S0 = FPlatformTime::Seconds();
			Context.Sim->BuildTerrainTile(I * Cells, J * Cells, Cells, Section);
			SimSeconds += FPlatformTime::Seconds() - S0;
			if (Section.IsEmpty())
			{
				continue;
			}
			TUniquePtr<ICPTileMesh> Tile = ICPTileMesh::Create(*Context.Root, FName(*FString::Printf(TEXT("Ground_%d_%d"), I, J)));
			Tile->SetSection(0, Section, Material);
			Triangles += static_cast<int64>(Section.TriangleCount());
			Vertices += static_cast<int64>(Section.Position.size());
			Tiles.Add(MoveTemp(Tile));
		}
	}
	UE_LOG(LogCPRender, Log, TEXT("Ground layer: %d tiles (%d x %d, %d cells = %.0f m each), %lld triangles, %lld vertices; sim meshing %.0f ms, total build %.0f ms"),
		Tiles.Num(), TilesX, TilesY, Cells, Cells * Grid.CellM, Triangles, Vertices, SimSeconds * 1000.0, (FPlatformTime::Seconds() - T0) * 1000.0);
}

void FCPGroundLayer::Teardown()
{
	for (TUniquePtr<ICPTileMesh>& Tile : Tiles)
	{
		Tile->Clear();
	}
	Tiles.Reset();
}
