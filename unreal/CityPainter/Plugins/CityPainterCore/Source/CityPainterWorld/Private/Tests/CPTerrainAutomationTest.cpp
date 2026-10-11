// Automation test "CityPainter.World.Terrain": the seeded test world builds, its terrain tiles cover the grid with the
// expected triangle count, every triangle faces up in the picture seen from above (north up, east right), neighbouring
// tiles share their border vertices exactly, and the analytic ground pick agrees with the height query.
// Run: UnrealEditor-Cmd.exe CityPainter.uproject -ExecCmds="Automation RunTests CityPainter.World; Quit" -nullrhi -unattended

#include "Misc/AutomationTest.h"

#include "CPSimWorld.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCPTerrainTest, "CityPainter.World.Terrain",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

bool FCPTerrainTest::RunTest(const FString& Parameters)
{
	std::string Error, Warning;
	std::unique_ptr<FCPSimWorld> Sim = FCPSimWorld::Create(FCPSimWorldConfig(), Error, Warning);
	if (!TestNotNull(TEXT("sim world created"), Sim.get()))
	{
		AddError(UTF8_TO_TCHAR(Error.c_str()));
		return false;
	}
	const FCPTerrainGrid& G = Sim->TerrainGrid();
	TestEqual(TEXT("samples per side"), G.Nx, 513);

	const int32 Cells = 64;
	const int32 TilesPerSide = (G.Nx - 1) / Cells;
	int64 Triangles = 0;
	int32 DownFacing = 0;
	TMap<FIntPoint, FCPMeshSection> Tiles;
	for (int32 J = 0; J < TilesPerSide; ++J)
	{
		for (int32 I = 0; I < TilesPerSide; ++I)
		{
			FCPMeshSection& S = Tiles.Add(FIntPoint(I, J));
			Sim->BuildTerrainTile(I * Cells, J * Cells, Cells, S);
			Triangles += static_cast<int64>(S.TriangleCount());
			// Picture seen from above: east = UE Y (right), north = UE X (up). Counter-clockwise = positive area.
			for (size_t T = 0; T + 2 < S.Indices.size(); T += 3)
			{
				const auto& A = S.Position[S.Indices[T]];
				const auto& B = S.Position[S.Indices[T + 1]];
				const auto& C = S.Position[S.Indices[T + 2]];
				const double Area = (B[1] - A[1]) * (C[0] - A[0]) - (B[0] - A[0]) * (C[1] - A[1]);
				DownFacing += Area <= 0.0 ? 1 : 0;
			}
		}
	}
	TestEqual(TEXT("tile count"), Tiles.Num(), 64);
	TestEqual(TEXT("total triangles = 2 per cell"), Triangles, static_cast<int64>(512) * 512 * 2);
	TestEqual(TEXT("triangles facing down or degenerate"), DownFacing, 0);

	// Border vertices of neighbours coincide in absolute UE position (tile origin + relative float, within 0.01 cm).
	const FCPMeshSection& W = Tiles[FIntPoint(0, 0)];
	const FCPMeshSection& E = Tiles[FIntPoint(1, 0)];
	const int32 NxTile = Cells + 1;
	double MaxGap = 0.0;
	for (int32 J = 0; J < NxTile; ++J)
	{
		const auto& A = W.Position[J * NxTile + Cells];  // east edge of the west tile
		const auto& B = E.Position[J * NxTile];          // west edge of the east tile
		for (int32 Axis = 0; Axis < 3; ++Axis)
		{
			MaxGap = FMath::Max(MaxGap, FMath::Abs((W.OriginUeCm[Axis] + A[Axis]) - (E.OriginUeCm[Axis] + B[Axis])));
		}
	}
	TestTrue(FString::Printf(TEXT("tile borders coincide (max gap %.5f cm)"), MaxGap), MaxGap < 0.01);

	// Straight-down pick agrees with the height query; an oblique pick lands on the ground along its ray.
	FCPGroundPick Hit;
	const double X = 123.4, Y = -56.7, H = Sim->HeightM(X, Y);
	if (TestTrue(TEXT("down ray hits"), Sim->PickGround({X, Y, H + 500.0}, {0, 0, -1}, Hit)))
	{
		TestTrue(FString::Printf(TEXT("pick height %.4f vs %.4f"), Hit.HeightM, H), FMath::Abs(Hit.HeightM - H) < 0.001);
	}
	if (TestTrue(TEXT("oblique ray hits"), Sim->PickGround({X, Y - 300.0, H + 200.0}, {0, 1, -0.5}, Hit)))
	{
		TestTrue(TEXT("oblique pick is on the terrain"), FMath::Abs(Hit.PointSim.Z - Sim->HeightM(Hit.PointSim.X, Hit.PointSim.Y)) < 0.01);
	}
	TestFalse(TEXT("ray pointing up misses"), Sim->PickGround({X, Y, H + 10.0}, {0, 0.1, 1}, Hit));
	return true;
}

#endif
