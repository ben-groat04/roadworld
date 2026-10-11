#pragma once

// Step-building helpers shared by the milestone 2 road scenarios (CPPlaytestRoads.cpp, CPPlaytestRoadStates.cpp):
// project a sim-metre position to the screen, click it, press keys, check the graph counts, take a screenshot.
// World coordinates here are sim metres east / north of the world centre (the sim origin).

#include "CoreMinimal.h"

#include "../CPCameraPawn.h"
#include "../CPPlayerController.h"
#include "../CityPainterLog.h"
#include "CPPlaytestTypes.h"
#include "CPSimWorld.h"
#include "CPWorldSubsystem.h"

#include "Engine/World.h"

namespace CPPlaytestRoadSteps
{
using FC = FCPPlaytestContext;

inline FVector2D ScreenOf(FC& C, double EastM, double NorthM)
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

inline FStats Stats(FC& C)
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

	/** Camera over the world centre. Defaults are the milestone 2 overview; the states use a prototype-like oblique framing. */
	void View(double DistM = 380.0, double Azimuth = 0.0, double Polar = 0.25, double EastM = 0.0, double NorthM = 0.0)
	{
		Do(TEXT("set view"), [=](FC& C)
		{
			const FVector2D Centre = C.Sim->WorldBoundsCm().GetCenter();
			C.Pawn->SetView(FVector(Centre.X + NorthM * 100.0, Centre.Y + EastM * 100.0, 0.0), DistM, Azimuth, Polar);
		});
		Wait(3);
	}
	void Pointer(double East, double North)
	{
		Do(TEXT("pointer"), [=](FC& C) { C.Input->MoveMouse(ScreenOf(C, East, North)); });
		Wait(3);   // the tool recomputes its hover on the next player tick
	}
	/** Pointer at a viewport fraction (for the sky and the HUD). */
	void PointerFraction(double Fx, double Fy)
	{
		Do(TEXT("pointer (fraction)"), [=](FC& C)
		{
			int32 X = 0, Y = 0;
			C.PC->GetViewportSize(X, Y);
			C.Input->MoveMouse(FVector2D(X * Fx, Y * Fy));
		});
		Wait(3);
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

	/** Logs the counts and checks them. Image names the screenshot taken right after (may be empty: no shot). */
	void CheckGraph(const FString& What, int32 ExpectNodes, int32 ExpectEdges, const FString& Image)
	{
		Do(TEXT("check graph"), [=](FC& C)
		{
			const FStats S = Stats(C);
			C.Results->AddCheck(What, FString::Printf(TEXT("%d nodes, %d edges"), ExpectNodes, ExpectEdges), FString::Printf(TEXT("%d nodes, %d edges"), S.Nodes, S.Edges),
				S.Nodes == ExpectNodes && S.Edges == ExpectEdges, Image);
			UE_LOG(LogCPGame, Display, TEXT("CPPlaytest M2: %s -> %d nodes, %d edges"), *What, S.Nodes, S.Edges);
		});
		if (!Image.IsEmpty())
		{
			Shot(Image.Replace(TEXT(".png"), TEXT("")));
		}
	}
	/** Logs the tool HUD text and the ghost state with a label (what the player reads on screen). */
	void LogHud(const FString& Label)
	{
		Do(TEXT("log hud"), [Label](FC& C)
		{
			UE_LOG(LogCPGame, Display, TEXT("CPPlaytest M2 HUD [%s]: %s"), *Label, *C.PC->GetToolHudText().Replace(TEXT("\n"), TEXT(" | ")));
		});
	}
};
}  // namespace CPPlaytestRoadSteps
