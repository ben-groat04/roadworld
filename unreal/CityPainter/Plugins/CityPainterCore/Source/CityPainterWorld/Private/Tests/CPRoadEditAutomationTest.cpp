// Automation test "CityPainter.World.RoadEdit": a stroke drawn through the tool session becomes ONE command; submitting it
// changes exactly the tiles near the road (road surface and markings), undo removes exactly those tiles again and returns
// the graph to empty, redo restores them, and a road far away touches none of the first road's tiles. Headless: no
// world, no renderer, only the Sim facade (the same calls UCPWorldSubsystem makes).
// Run: UnrealEditor-Cmd.exe CityPainter.uproject -ExecCmds="Automation RunTests CityPainter.World.RoadEdit; Quit" -nullrhi -unattended

#include "Misc/AutomationTest.h"

#include "CPSettings.h"
#include "CPSimWorld.h"

#include <set>
#include <tuple>

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
using FTileId = std::tuple<std::string, std::int64_t, std::int64_t>;   // layer, tile x, tile y

std::set<FTileId> Tiles(const FCPChangeSet& Set, ECPTileChange Change)
{
	std::set<FTileId> Out;
	for (const FCPLayerTile& T : Set.Tiles)
	{
		if (T.Change == Change)
		{
			Out.insert({T.Layer, T.TileX, T.TileY});
		}
	}
	return Out;
}

/** A pointer ray straight down onto sim position (X, Y), 60 degree field of view at 900 pixels high. */
FCPPointerInput DownRay(FCPSimWorld& Sim, double X, double Y)
{
	FCPPointerInput P;
	P.OriginSim = {X, Y, Sim.HeightM(X, Y) + 300.0};
	P.DirSim = {0, 0, -1};
	P.MetresPerPixelPerMetre = 2.0 * FMath::Tan(FMath::DegreesToRadians(25.0)) / 900.0;
	return P;
}

/** Draws a straight road from (X0, Y0) to (X1, Y1) with the tool session, snapping off so the points are exactly where clicked. */
FCPCommandHandle DrawStraight(FCPSimWorld& Sim, double X0, double Y0, double X1, double Y1, FString& Error)
{
	FCPToolSession* Tool = Sim.ToolSession();
	FCPToolModifiers Free;
	Free.bSnapOff = true;
	const FCPToolClickResult C0 = Tool->Click(DownRay(Sim, X0, Y0), Free);
	const FCPToolClickResult C1 = Tool->Click(DownRay(Sim, X1, Y1), Free);
	if (!C0.bAdded || !C1.bAdded)
	{
		Error = FString::Printf(TEXT("clicks were not added (first %d second %d)"), C0.bAdded, C1.bAdded);
		return nullptr;
	}
	FCPToolFinishResult Fin = Tool->Finish();
	if (!Fin.bOk)
	{
		Error = UTF8_TO_TCHAR(Fin.Error.c_str());
		return nullptr;
	}
	return MoveTemp(Fin.Command);
}
}  // namespace

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCPRoadEditTest, "CityPainter.World.RoadEdit",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

bool FCPRoadEditTest::RunTest(const FString& Parameters)
{
	FCPSimWorldConfig Config;
	const FString DataDir = GetDefault<UCPSettings>()->ResolveDataDir();
	if (!TestFalse(TEXT("data directory found (lane_types.json)"), DataDir.IsEmpty()))
	{
		return false;
	}
	Config.DataDir = std::string(TCHAR_TO_UTF8(*DataDir));
	Config.bVegetationLayers = false;   // not what this test is about; keeps it quick
	std::string Error, Warning;
	std::unique_ptr<FCPSimWorld> Sim = FCPSimWorld::Create(Config, Error, Warning);
	if (!TestNotNull(TEXT("sim world created"), Sim.get()))
	{
		AddError(UTF8_TO_TCHAR(Error.c_str()));
		return false;
	}
	if (!TestTrue(TEXT("roads available"), Sim->HasRoads()))
	{
		AddError(UTF8_TO_TCHAR(Sim->RoadsUnavailableReason().c_str()));
		return false;
	}
	FCPToolSession* Tool = Sim->ToolSession();
	if (!TestNotNull(TEXT("tool session"), Tool))
	{
		return false;
	}
	Tool->SetRoadType("residential");

	// Before any click: the preview has a pick, no ribbon, no stroke.
	FCPToolModifiers Free;
	Free.bSnapOff = true;
	const FCPToolPreview Idle = Tool->Hover(DownRay(*Sim, 0, 0), Free);
	TestTrue(TEXT("idle hover hits the ground"), Idle.bHasPick);
	TestFalse(TEXT("no stroke before the first click"), Idle.bStrokeActive);
	TestEqual(TEXT("no ribbon before the first click"), static_cast<int32>(Idle.Ribbons.size()), 0);

	TestEqual(TEXT("empty graph: nodes"), static_cast<int32>(Sim->GraphStats().Nodes), 0);
	FString Why;

	// ---- road A: east-west, 120 m, around the origin. Tiles (256 m grid) i,j in {-1, 0}.
	const uint64 V0 = Sim->WorldVersion();
	FCPCommandHandle CmdA = DrawStraight(*Sim, -60, 0, 60, 0, Why);
	if (!TestNotNull(*FString::Printf(TEXT("a stroke finishes into a command (%s)"), *Why), CmdA.get()))
	{
		return false;
	}
	TestFalse(TEXT("the stroke ended with the finish"), Tool->IsActive());
	const FCPEditResult A = Sim->Submit(MoveTemp(CmdA));
	if (!TestTrue(*FString::Printf(TEXT("submit road A (%hs)"), A.Error.c_str()), A.bOk))
	{
		return false;
	}
	TestEqual(TEXT("A: nodes"), static_cast<int32>(A.Graph.Nodes), 2);
	TestEqual(TEXT("A: edges"), static_cast<int32>(A.Graph.Edges), 1);
	TestEqual(TEXT("A: world version advanced by one"), static_cast<int64>(Sim->WorldVersion()), static_cast<int64>(V0 + 1));
	const std::set<FTileId> AddedA = Tiles(A.Changes, ECPTileChange::Added);
	TestTrue(TEXT("A: road surface tiles added"), A.Changes.Count(CPLayer::RoadMesh, ECPTileChange::Added) > 0);
	TestTrue(TEXT("A: marking tiles added"), A.Changes.Count(CPLayer::Markings, ECPTileChange::Added) > 0);
	TestEqual(TEXT("A: nothing changed or removed"), static_cast<int32>(A.Changes.Tiles.size()), static_cast<int32>(AddedA.size()));
	for (const FCPLayerTile& T : A.Changes.Tiles)
	{
		TestTrue(FString::Printf(TEXT("A: tile %lld,%lld is near the road (expected -1..0 on both axes)"), T.TileX, T.TileY), T.TileX >= -1 && T.TileX <= 0 && T.TileY >= -1 && T.TileY <= 0);
		int64 Triangles = 0;
		for (const FCPKeyedSection& S : T.Sections)
		{
			Triangles += static_cast<int64>(S.Mesh.TriangleCount());
			TestEqual(TEXT("A: section arrays agree (positions = normals)"), static_cast<int64>(S.Mesh.Position.size()), static_cast<int64>(S.Mesh.Normal.size()));
			TestEqual(TEXT("A: UV0 count"), static_cast<int64>(S.Mesh.Position.size()), static_cast<int64>(S.Mesh.UV0.size()));
		}
		TestTrue(TEXT("A: every changed tile has triangles"), Triangles > 0);
	}

	// ---- road B: far east, 100 m. Its tiles must not overlap road A's.
	FCPCommandHandle CmdB = DrawStraight(*Sim, 700, 0, 800, 0, Why);
	if (!TestNotNull(*FString::Printf(TEXT("road B finishes into a command (%s)"), *Why), CmdB.get()))
	{
		return false;
	}
	const FCPEditResult B = Sim->Submit(MoveTemp(CmdB));
	TestTrue(TEXT("submit road B"), B.bOk);
	TestEqual(TEXT("B: edges"), static_cast<int32>(B.Graph.Edges), 2);
	const std::set<FTileId> AddedB = Tiles(B.Changes, ECPTileChange::Added);
	TestTrue(TEXT("B: tiles added"), AddedB.size() > 0);
	for (const FTileId& Id : AddedB)
	{
		TestTrue(TEXT("B: no tile of road A changes"), AddedA.count(Id) == 0);
	}
	TestEqual(TEXT("B: nothing else changed"), static_cast<int32>(B.Changes.Tiles.size()), static_cast<int32>(AddedB.size()));

	// ---- undo B, undo A: each removes exactly the tiles its draw added; the graph returns to empty.
	const FCPEditResult UndoB = Sim->Undo();
	TestTrue(TEXT("undo B"), UndoB.bOk);
	TestTrue(TEXT("undo B removes exactly the tiles B added"), Tiles(UndoB.Changes, ECPTileChange::Removed) == AddedB);
	TestEqual(TEXT("undo B: edges"), static_cast<int32>(UndoB.Graph.Edges), 1);
	const FCPEditResult UndoA = Sim->Undo();
	TestTrue(TEXT("undo A"), UndoA.bOk);
	TestTrue(TEXT("undo A removes exactly the tiles A added"), Tiles(UndoA.Changes, ECPTileChange::Removed) == AddedA);
	TestEqual(TEXT("undo A: nodes"), static_cast<int32>(UndoA.Graph.Nodes), 0);
	TestEqual(TEXT("undo A: edges"), static_cast<int32>(UndoA.Graph.Edges), 0);
	const FCPEditResult UndoNone = Sim->Undo();
	TestFalse(TEXT("undo with nothing to undo fails and says so"), UndoNone.bOk);
	TestTrue(TEXT("... with a message"), !UndoNone.Error.empty());
	TestEqual(TEXT("... and changes no tile"), static_cast<int32>(UndoNone.Changes.Tiles.size()), 0);

	// ---- redo: the same tiles come back, and the snapshot of everything agrees.
	const FCPEditResult RedoA = Sim->Redo();
	TestTrue(TEXT("redo A"), RedoA.bOk);
	TestTrue(TEXT("redo A adds exactly the tiles A first added"), Tiles(RedoA.Changes, ECPTileChange::Added) == AddedA);
	TestEqual(TEXT("redo A: edges"), static_cast<int32>(RedoA.Graph.Edges), 1);
	const FCPChangeSet All = Sim->SnapshotAll();
	TestTrue(TEXT("snapshot of the world lists exactly road A's tiles"), Tiles(All, ECPTileChange::Added) == AddedA);

	// ---- a new edit after undo clears the redo history.
	FCPCommandHandle CmdC = DrawStraight(*Sim, -60, 100, 60, 100, Why);
	if (TestNotNull(*FString::Printf(TEXT("road C finishes into a command (%s)"), *Why), CmdC.get()))
	{
		TestTrue(TEXT("submit road C"), Sim->Submit(MoveTemp(CmdC)).bOk);
		TestFalse(TEXT("redo history is gone after a new edit"), Sim->GraphStats().bCanRedo);
	}
	return true;
}

#endif
