// The milestone 2 part of the scripted playtest: drawing roads with the real mouse and keyboard events into the real
// tool manager, then undo and redo, checking the road graph through the sim facade after each step. It was written
// without being run (the editor was not available to its author); the expected counts are derived from the sim, not
// measured, so a failing check is a lead to read the log, not necessarily a bug in the tool.
//
// World used (sim metres, east / north of the world centre, which is the sim origin), camera high over the origin:
//   A  straight          (-80, 0) -> (80, 0)
//   S  S-curve, 4 points (-80, 50) (-30, 80) (30, 30) (80, 60), finished with Enter
//   T  (0, -70) then a click ON road A at (0, 0): snaps to the road and finishes by itself, splitting A
// Clicks are made with Shift held (snapping off) so the points land exactly where aimed, except T's last click.

#include "CPPlaytestRoads.h"

#include "../CPCameraPawn.h"
#include "../CPPlayerController.h"
#include "../CityPainterLog.h"
#include "CPWorldSubsystem.h"

#include "Engine/World.h"

namespace
{
using FC = FCPPlaytestContext;
constexpr double ViewDistanceM = 380.0;

FVector2D ScreenOf(FC& C, double EastM, double NorthM)
{
	const double XCm = NorthM * 100.0, YCm = EastM * 100.0;   // CPFrame: UE.X = sim north, UE.Y = sim east (centimetres)
	FVector2D Screen = FVector2D::ZeroVector;
	C.PC->ProjectWorldLocationToScreen(FVector(XCm, YCm, C.Sim->GroundZCm(XCm, YCm)), Screen, false);
	return Screen;
}

struct FStats
{
	int32 Nodes = -1, Edges = -1;
};

FStats Stats(FC& C)
{
	FStats S;
	if (const FCPSimWorld* Sim = C.Sim ? C.Sim->GetSim() : nullptr)
	{
		const FCPGraphStats G = Sim->GraphStats();
		S.Nodes = static_cast<int32>(G.Nodes);
		S.Edges = static_cast<int32>(G.Edges);
	}
	return S;
}

struct FBuilder
{
	TArray<FCPPlaytestStep>& Steps;

	void Add(const TCHAR* Label, int32 Frames, TFunction<void(FC&, int32)> OnFrame)
	{
		FCPPlaytestStep S;
		S.Label = Label;
		S.Frames = Frames;
		S.OnFrame = MoveTemp(OnFrame);
		Steps.Add(MoveTemp(S));
	}
	void Do(const TCHAR* Label, TFunction<void(FC&)> Fn)
	{
		Add(Label, 1, [Fn](FC& C, int32) { Fn(C); });
	}
	void Wait(int32 Frames = 3) { Add(TEXT("wait"), Frames, nullptr); }
	void Shot(const FString& Name)
	{
		Do(TEXT("screenshot"), [Name](FC& C) { C.Results->RequestShot(Name); });
		Wait(5);
	}

	void View()
	{
		Do(TEXT("set view over the origin"), [](FC& C)
		{
			const FVector2D Centre = C.Sim->WorldBoundsCm().GetCenter();
			C.Pawn->SetView(FVector(Centre.X, Centre.Y, 0.0), ViewDistanceM, 0.0, 0.25);
		});
		Wait(3);
	}
	void Pointer(double East, double North)
	{
		Do(TEXT("pointer"), [=](FC& C) { C.Input->MoveMouse(ScreenOf(C, East, North)); });
		Wait(3);   // the tool recomputes its hover on the next player tick
	}
	void Click(double East, double North)
	{
		Pointer(East, North);
		Do(TEXT("left button down"), [](FC& C) { C.Input->MouseButton(EKeys::LeftMouseButton, true); });
		Wait(1);
		Do(TEXT("left button up"), [](FC& C) { C.Input->MouseButton(EKeys::LeftMouseButton, false); });
		Wait(3);
	}
	void Press(const FKey& Key)
	{
		Do(TEXT("key down"), [Key](FC& C) { C.Input->KeyDown(Key); });
		Wait(1);
		Do(TEXT("key up"), [Key](FC& C) { C.Input->KeyUp(Key); });
		Wait(3);
	}
	void Chord(const FKey& Modifier, const FKey& Key)
	{
		Do(TEXT("modifier down"), [Modifier](FC& C) { C.Input->KeyDown(Modifier); });
		Wait(1);
		Press(Key);
		Do(TEXT("modifier up"), [Modifier](FC& C) { C.Input->KeyUp(Modifier); });
		Wait(2);
	}
	void ShiftDown() { Do(TEXT("Shift down (snap off)"), [](FC& C) { C.Input->KeyDown(EKeys::LeftShift); }); Wait(1); }
	void ShiftUp() { Do(TEXT("Shift up (snap on)"), [](FC& C) { C.Input->KeyUp(EKeys::LeftShift); }); Wait(2); }

	/** Logs the counts and checks them. Image names the screenshot taken right after. */
	void CheckGraph(const FString& What, int32 ExpectNodes, int32 ExpectEdges, const FString& Image)
	{
		Do(TEXT("check graph"), [=](FC& C)
		{
			const FStats S = Stats(C);
			C.Results->AddCheck(What, FString::Printf(TEXT("%d nodes, %d edges"), ExpectNodes, ExpectEdges), FString::Printf(TEXT("%d nodes, %d edges"), S.Nodes, S.Edges),
				S.Nodes == ExpectNodes && S.Edges == ExpectEdges, Image);
			UE_LOG(LogCPGame, Display, TEXT("CPPlaytest M2: %s -> %d nodes, %d edges"), *What, S.Nodes, S.Edges);
		});
		Shot(Image.Replace(TEXT(".png"), TEXT("")));
	}
};

void ScenarioRoads(FBuilder& B)
{
	B.View();
	B.Shot(TEXT("10_m2_00_empty"));

	// A: straight, 2 points, finished with Enter.
	B.ShiftDown();
	B.Click(-80, 0);
	B.Shot(TEXT("10_m2_01_a_first_point"));
	B.Click(80, 0);
	B.Shot(TEXT("10_m2_02_a_ghost"));
	B.Press(EKeys::Enter);
	B.CheckGraph(TEXT("A: straight road, 2 clicks + Enter"), 2, 1, TEXT("10_m2_03_a_done.png"));

	// S: S-curve, 4 points, finished with Enter.
	B.Click(-80, 50);
	B.Click(-30, 80);
	B.Click(30, 30);
	B.Click(80, 60);
	B.Shot(TEXT("10_m2_04_s_ghost"));
	B.Press(EKeys::Enter);
	B.CheckGraph(TEXT("S: S-curve, 4 clicks + Enter"), 4, 2, TEXT("10_m2_05_s_done.png"));

	// T: start south of A, then click ON A (snap on): the road ends by itself and A is split at the junction.
	B.Click(0, -70);
	B.ShiftUp();
	B.Pointer(0, 0);
	B.Shot(TEXT("10_m2_06_t_snap_to_road"));
	B.Click(0, 0);
	B.CheckGraph(TEXT("T: click on road A finishes the road and splits A (2 new nodes, A becomes 2 edges + T)"), 6, 4, TEXT("10_m2_07_t_done.png"));

	// Undo three times (Ctrl+Z), checking each step.
	B.Chord(EKeys::LeftControl, EKeys::Z);
	B.CheckGraph(TEXT("undo 1 (T)"), 4, 2, TEXT("10_m2_08_undo1.png"));
	B.Chord(EKeys::LeftControl, EKeys::Z);
	B.CheckGraph(TEXT("undo 2 (S)"), 2, 1, TEXT("10_m2_09_undo2.png"));
	B.Chord(EKeys::LeftControl, EKeys::Z);
	B.CheckGraph(TEXT("undo 3 (A)"), 0, 0, TEXT("10_m2_10_undo3.png"));

	// Redo twice (Ctrl+Y).
	B.Chord(EKeys::LeftControl, EKeys::Y);
	B.CheckGraph(TEXT("redo 1 (A)"), 2, 1, TEXT("10_m2_11_redo1.png"));
	B.Chord(EKeys::LeftControl, EKeys::Y);
	B.CheckGraph(TEXT("redo 2 (S)"), 4, 2, TEXT("10_m2_12_redo2.png"));
}

void ScenarioRightClick(FBuilder& B)
{
	// Right CLICK (no drag) finishes a road with two points; a right DRAG rotates the camera and does not.
	B.ShiftDown();
	B.Click(-60, -40);
	B.Click(60, -40);
	B.Do(TEXT("right button down"), [](FC& C) { C.Input->MouseButton(EKeys::RightMouseButton, true); });
	B.Wait(1);
	B.Do(TEXT("right button up"), [](FC& C) { C.Input->MouseButton(EKeys::RightMouseButton, false); });
	B.Wait(3);
	B.CheckGraph(TEXT("right-click without a drag finishes the road"), 6, 3, TEXT("10_m2_13_right_click_finish.png"));

	TSharedRef<double> AzimuthBefore = MakeShared<double>(0.0);
	B.Click(-60, -90);
	B.Click(60, -90);
	B.Do(TEXT("remember azimuth"), [AzimuthBefore](FC& C) { *AzimuthBefore = C.Pawn->GetAzimuth(); C.Input->MoveMouse(C.Input->Pointer()); });
	B.Do(TEXT("right button down"), [](FC& C) { C.Input->MouseButton(EKeys::RightMouseButton, true); });
	B.Wait(2);
	B.Add(TEXT("right drag 120 px"), 12, [](FC& C, int32) { C.Input->MoveMouse(C.Input->Pointer() + FVector2D(10.0, 0.0)); });
	B.Do(TEXT("right button up"), [](FC& C) { C.Input->MouseButton(EKeys::RightMouseButton, false); });
	B.Wait(3);
	B.Do(TEXT("check right drag"), [AzimuthBefore](FC& C)
	{
		const FStats S = Stats(C);
		const double Moved = FMath::Abs(C.Pawn->GetAzimuth() - *AzimuthBefore);
		C.Results->AddCheck(TEXT("right-drag rotates the camera and does not finish the road"), TEXT("6 nodes, 3 edges and the azimuth changed"),
			FString::Printf(TEXT("%d nodes, %d edges, azimuth moved %.3f rad"), S.Nodes, S.Edges, Moved), S.Nodes == 6 && S.Edges == 3 && Moved > 0.05, TEXT("10_m2_14_right_drag.png"));
	});
	B.Shot(TEXT("10_m2_14_right_drag"));
	B.Press(EKeys::Escape);
	B.ShiftUp();
	B.Do(TEXT("check cancel"), [](FC& C)
	{
		const FStats S = Stats(C);
		C.Results->AddCheck(TEXT("Esc cancels the open stroke"), TEXT("6 nodes, 3 edges"), FString::Printf(TEXT("%d nodes, %d edges"), S.Nodes, S.Edges), S.Nodes == 6 && S.Edges == 3, FString());
	});
}
}  // namespace

void AppendCPPlaytestRoadScenarios(TArray<FCPPlaytestStep>& Out)
{
	FBuilder B{Out};
	ScenarioRoads(B);
	ScenarioRightClick(B);
}
