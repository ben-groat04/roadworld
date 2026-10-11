// The scripted playtest of the City Painter camera, picking and HUD. Every control is driven through Slate input events
// (FCPPlaytestInput) into the real controller, Enhanced Input actions and pawn; the checks read the pawn, the controller,
// the HUD text and the sim. The expectations come from the prototype (prototypes/road-draw/src/scene.js: OrbitControls
// with a 50 degree vertical FOV; key pan 0.9 * distance per second, x3 with Shift; key rotate 1.3 rad/s; limits 15..3800 m,
// polar 0.05..1.42, camera >= 3 m above ground, zoom factor 0.95^1.1 per notch, zoom to the screen centre).

#include "CPPlaytestScript.h"
#include "CPPlaytestRoads.h"

#include "../CPCameraPawn.h"
#include "../CPPlayerController.h"
#include "CPWorldSubsystem.h"

#include "AssetCompilingManager.h"
#include "Camera/CameraComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "Internationalization/Regex.h"
#include "Widgets/SWindow.h"
#include "ShaderCompiler.h"

namespace
{
using FC = FCPPlaytestContext;
constexpr double TwoPi = 6.283185307179586;
constexpr double ProtoVerticalFovDeg = 50.0;

double WrapPi(double A)
{
	A = FMath::Fmod(A, TwoPi);
	return A > TwoPi / 2 ? A - TwoPi : (A < -TwoPi / 2 ? A + TwoPi : A);
}

FVector2D ViewSize(FC& C)
{
	int32 X = 0, Y = 0;
	C.PC->GetViewportSize(X, Y);
	return FVector2D(X, Y);
}

FVector2D Px(FC& C, double Fx, double Fy)
{
	const FVector2D S = ViewSize(C);
	return FVector2D(S.X * Fx, S.Y * Fy);
}

/** A snapshot of the camera rig plus where the target currently is on screen. */
struct FSnap
{
	double Az = 0, Polar = 0, Dist = 0;
	FVector TargetCm = FVector::ZeroVector, CameraCm = FVector::ZeroVector;
	FVector2D TargetScreen = FVector2D::ZeroVector;
	FVector2D Mouse = FVector2D::ZeroVector;
};
using FSnapRef = TSharedRef<FSnap>;

void Take(FC& C, FSnap& S)
{
	S.Az = C.Pawn->GetAzimuth();
	S.Polar = C.Pawn->GetPolar();
	S.Dist = C.Pawn->GetDistanceM();
	S.TargetCm = C.Pawn->GetTargetCm();
	S.CameraCm = C.Pawn->GetActorLocation();
	C.PC->ProjectWorldLocationToScreen(S.TargetCm, S.TargetScreen, false);
	S.Mouse = C.Input->Pointer();
}

double ClearanceM(FC& C)
{
	const FVector P = C.Pawn->GetActorLocation();
	return (P.Z - C.Sim->GroundZCm(P.X, P.Y)) / 100.0;
}

double GroundMoveM(const FSnap& A, const FSnap& E)
{
	return FVector2D(E.TargetCm.X - A.TargetCm.X, E.TargetCm.Y - A.TargetCm.Y).Size() / 100.0;
}

FVector2D GroundDir(const FSnap& A, const FSnap& E)
{
	return FVector2D(E.TargetCm.X - A.TargetCm.X, E.TargetCm.Y - A.TargetCm.Y).GetSafeNormal();
}

void Report(FC& C, const FString& Control, const FString& Expected, const FString& Observed, bool bPass, const FString& Image = FString())
{
	C.Results->AddCheck(Control, Expected, Observed, bPass, Image);
}

struct FBuilder
{
	TArray<FCPPlaytestStep>& Steps;

	void Add(const TCHAR* Label, int32 Frames, TFunction<void(FC&, int32)> OnFrame, TFunction<void(FC&)> OnEnd = nullptr, TFunction<bool(FC&)> Until = nullptr)
	{
		FCPPlaytestStep S;
		S.Label = Label;
		S.Frames = Frames;
		S.OnFrame = MoveTemp(OnFrame);
		S.OnEnd = MoveTemp(OnEnd);
		S.Until = MoveTemp(Until);
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
	/** Setup only (not a control): places the rig. Target is an offset in sim metres from the world centre. */
	void View(double OffsetEastM, double OffsetNorthM, double DistM, double Az, double Polar)
	{
		Do(TEXT("set view"), [=](FC& C)
		{
			const FVector2D Centre = C.Sim->WorldBoundsCm().GetCenter();
			C.Pawn->SetView(FVector(Centre.X + OffsetNorthM * 100.0, Centre.Y + OffsetEastM * 100.0, 0.0), DistM, Az, Polar);
		});
		Wait(2);
	}
	/** Press a mouse button at (Fx, Fy) (viewport fractions) and drag by DeltaPx over Frames frames, then release. */
	void Drag(const FKey& Button, double Fx, double Fy, FVector2D DeltaPx, int32 Frames, FSnapRef Start, FSnapRef End)
	{
		Do(TEXT("pointer to start"), [=](FC& C) { C.Input->MoveMouse(Px(C, Fx, Fy)); });
		Wait(2);
		Do(TEXT("button down"), [=](FC& C) { C.Input->MouseButton(Button, true); Take(C, *Start); });
		Wait(2);
		Add(TEXT("drag"), Frames, [=](FC& C, int32 Frame)
		{
			const double T = double(Frame + 1) / Frames;
			C.Input->MoveMouse(Px(C, Fx, Fy) + DeltaPx * T);
		});
		Wait(2);
		Do(TEXT("button up"), [=](FC& C) { Take(C, *End); C.Input->MouseButton(Button, false); });
		Wait(2);
	}
	/** Hold keys for Frames frames. */
	void Hold(const TArray<FKey>& Keys, int32 Frames, FSnapRef Start, FSnapRef End)
	{
		Do(TEXT("keys down"), [=](FC& C) { Take(C, *Start); for (const FKey& K : Keys) { C.Input->KeyDown(K); } });
		Wait(Frames);
		Do(TEXT("keys up"), [=](FC& C) { Take(C, *End); for (const FKey& K : Keys) { C.Input->KeyUp(K); } });
		Wait(3);
	}
};

// ------------------------------------------------------------------------------------------------ scenarios
void ScenarioStartup(FBuilder& B)
{
	// Wait for shaders and assets so screenshots show real materials, not the default grey.
	B.Add(TEXT("warm up (shaders)"), 6000, nullptr, nullptr, [](FC& C)
	{
		const bool bIdle = !(GShaderCompilingManager && GShaderCompilingManager->IsCompiling()) && FAssetCompilingManager::Get().GetNumRemainingAssets() == 0;
		return C.FrameIndex > 120 && bIdle;
	});
	B.Wait(30);
	B.View(0, 0, 600, 0.6, 1.0);
	B.Do(TEXT("check cursor"), [](FC& C)
	{
		UGameViewportClient* VC = C.PC->GetWorld()->GetGameViewport();
		const FVector2D S = ViewSize(C);
		C.Results->SetNote(TEXT("viewport"), FString::Printf(TEXT("%dx%d"), int32(S.X), int32(S.Y)));
		Report(C, TEXT("cursor: shown, not locked"), TEXT("bShowMouseCursor, lock mode DoNotLock"),
			FString::Printf(TEXT("show=%d lock=%d capture=%d"), C.PC->bShowMouseCursor, int32(VC->GetMouseLockMode()), int32(VC->GetMouseCaptureMode())),
			C.PC->bShowMouseCursor && VC->GetMouseLockMode() == EMouseLockMode::DoNotLock);
	});
	B.Shot(TEXT("00_start"));
}

void ScenarioRotate(FBuilder& B)
{
	FSnapRef A = MakeShared<FSnap>(), E = MakeShared<FSnap>();
	B.Do(TEXT("sample frames on"), [](FC& C) { C.FrameMs.Reset(); C.bSampleFrames = true; });
	B.View(0, 0, 600, 0.6, 1.0);
	B.Shot(TEXT("01_rotate_before"));
	B.Drag(EKeys::RightMouseButton, 0.5, 0.5, FVector2D(200, 0), 20, A, E);
	B.Do(TEXT("check rotate right"), [A, E](FC& C)
	{
		const double Exp = -TwoPi * 200.0 / ViewSize(C).Y, Obs = WrapPi(E->Az - A->Az);
		Report(C, TEXT("rotate: right-drag +200 px"), FString::Printf(TEXT("azimuth %+.3f rad (OrbitControls: one viewport height = one turn, dragging right decreases it)"), Exp),
			FString::Printf(TEXT("azimuth %+.3f rad; polar change %+.3f"), Obs, E->Polar - A->Polar), FMath::Abs(Obs - Exp) < 0.02 && FMath::Abs(E->Polar - A->Polar) < 0.01, TEXT("01_rotate_after.png"));
	});
	B.Shot(TEXT("01_rotate_after"));
	B.Drag(EKeys::RightMouseButton, 0.5, 0.5, FVector2D(-200, 0), 20, A, E);
	B.Do(TEXT("check rotate left"), [A, E](FC& C)
	{
		const double Exp = TwoPi * 200.0 / ViewSize(C).Y, Obs = WrapPi(E->Az - A->Az);
		Report(C, TEXT("rotate: left-drag -200 px"), FString::Printf(TEXT("azimuth %+.3f rad"), Exp), FString::Printf(TEXT("azimuth %+.3f rad"), Obs), FMath::Abs(Obs - Exp) < 0.02);
	});
	B.Drag(EKeys::RightMouseButton, 0.5, 0.3, FVector2D(0, 100), 20, A, E);
	B.Do(TEXT("check pitch down-drag"), [A, E](FC& C)
	{
		const double Exp = -TwoPi * 100.0 / ViewSize(C).Y, Obs = E->Polar - A->Polar;
		Report(C, TEXT("orbit pitch: drag down +100 px"), FString::Printf(TEXT("polar %+.3f rad (camera rises toward overhead)"), Exp), FString::Printf(TEXT("polar %+.3f rad"), Obs), FMath::Abs(Obs - Exp) < 0.02);
	});
	// Pitch limits: several long drags each way.
	B.View(0, 0, 200, 0.6, 1.0);
	for (int32 i = 0; i < 4; ++i)
	{
		B.Drag(EKeys::RightMouseButton, 0.5, 0.1, FVector2D(0, 700), 10, A, E);
	}
	B.Do(TEXT("check pitch limit overhead"), [](FC& C)
	{
		const FVector Fwd = C.Pawn->GetActorForwardVector();
		const FVector ToTarget = (C.Pawn->GetTargetCm() - C.Pawn->GetActorLocation()).GetSafeNormal();
		Report(C, TEXT("pitch limit: drag down far (overhead)"), TEXT("polar clamps at 0.05 rad, view looks down at the target, no flip"),
			FString::Printf(TEXT("polar %.3f, forward.z %.3f, forward.target dot %.4f, roll %.2f deg"), C.Pawn->GetPolar(), Fwd.Z, FVector::DotProduct(Fwd, ToTarget), C.Pawn->GetActorRotation().Roll),
			FMath::IsNearlyEqual(C.Pawn->GetPolar(), 0.05, 1e-4) && Fwd.Z < -0.99 && FVector::DotProduct(Fwd, ToTarget) > 0.999 && FMath::Abs(C.Pawn->GetActorRotation().Roll) < 0.5, TEXT("02_pitch_overhead.png"));
	});
	B.Shot(TEXT("02_pitch_overhead"));
	for (int32 i = 0; i < 4; ++i)
	{
		B.Drag(EKeys::RightMouseButton, 0.5, 0.9, FVector2D(0, -700), 10, A, E);
	}
	B.Do(TEXT("check pitch limit horizon"), [](FC& C)
	{
		const FVector Fwd = C.Pawn->GetActorForwardVector();
		const double Clear = ClearanceM(C);
		Report(C, TEXT("pitch limit: drag up far (horizon)"), TEXT("polar clamps at 1.42 rad; camera at least 3 m above the ground; looking down, not flipped"),
			FString::Printf(TEXT("polar %.3f, clearance %.2f m, forward.z %.3f, roll %.2f deg"), C.Pawn->GetPolar(), Clear, Fwd.Z, C.Pawn->GetActorRotation().Roll),
			FMath::IsNearlyEqual(C.Pawn->GetPolar(), 1.42, 1e-4) && Clear > 2.99 && Fwd.Z < 0.0 && FMath::Abs(C.Pawn->GetActorRotation().Roll) < 0.5, TEXT("02_pitch_horizon.png"));
	});
	B.Shot(TEXT("02_pitch_horizon"));
	// Close to a slope at the lowest pitch: never under the terrain.
	B.View(-300, 200, 15, 1.0, 1.42);
	B.Do(TEXT("check clearance near ground"), [](FC& C)
	{
		const double Clear = ClearanceM(C);
		Report(C, TEXT("camera never under the terrain (15 m, polar 1.42)"), TEXT("clearance >= 3 m"), FString::Printf(TEXT("clearance %.2f m"), Clear), Clear > 2.99, TEXT("02_near_ground.png"));
	});
	B.Shot(TEXT("02_near_ground"));
	B.Do(TEXT("sample frames off"), [](FC& C)
	{
		TArray<double> Ms = C.FrameMs;
		Ms.Sort();
		if (Ms.Num() > 10)
		{
			double Sum = 0;
			for (double V : Ms) { Sum += V; }
			const double P95 = Ms[int32(Ms.Num() * 0.95)];
			C.Results->SetMetric(TEXT("frame_ms_mean_while_orbiting"), Sum / Ms.Num());
			C.Results->SetMetric(TEXT("frame_ms_p95_while_orbiting"), P95);
			C.Results->SetMetric(TEXT("frame_ms_max_while_orbiting"), Ms.Last());
			Report(C, TEXT("frame time while orbiting (wall clock)"), TEXT("p95 under 120 ms: only a guard against gross regressions, the number itself is informational (this machine also runs a CFD job; p95 varied 27..80 ms between identical runs; max includes a first-frame hitch)"), FString::Printf(TEXT("mean %.1f, p95 %.1f, max %.1f ms over %d frames"), Sum / Ms.Num(), P95, Ms.Last(), Ms.Num()), P95 < 120.0);
		}
		C.bSampleFrames = false;
	});
}

void ScenarioPan(FBuilder& B)
{
	FSnapRef A = MakeShared<FSnap>(), E = MakeShared<FSnap>();
	const double Distances[] = {20.0, 400.0, 3000.0};
	for (const double D : Distances)
	{
		B.View(0, 0, D, 0.6, 1.0);
		B.Drag(EKeys::MiddleMouseButton, 0.5, 0.5, FVector2D(100, 0), 20, A, E);
		B.Do(TEXT("check pan"), [A, E, D](FC& C)
		{
			// The old target must have moved exactly 100 px right on screen (the ground follows the cursor); the new target is on the ground.
			FVector2D Now;
			C.PC->ProjectWorldLocationToScreen(A->TargetCm, Now, false);
			const double Moved = Now.X - A->TargetScreen.X;
			const double MetresMoved = GroundMoveM(*A, *E);
			const double ExpM = 100.0 * 2.0 * FMath::Tan(FMath::DegreesToRadians(ProtoVerticalFovDeg) * 0.5) * D / ViewSize(C).Y;
			const double DzCm = E->TargetCm.Z - C.Sim->GroundZCm(E->TargetCm.X, E->TargetCm.Y);
			Report(C, FString::Printf(TEXT("pan: middle-drag +100 px at %.0f m"), D), FString::Printf(TEXT("ground follows the cursor: old target moves +100 px on screen; target moves %.2f m; target z on the ground"), ExpM),
				FString::Printf(TEXT("old target moved %+.1f px; target moved %.2f m (%.0f%% of expected); target z error %.2f cm"), Moved, MetresMoved, 100.0 * MetresMoved / FMath::Max(ExpM, 1e-6), DzCm),
				FMath::Abs(Moved - 100.0) < 3.0 && FMath::Abs(MetresMoved / ExpM - 1.0) < 0.03 && FMath::Abs(DzCm) < 1.0, D == 400.0 ? TEXT("03_pan_after.png") : TEXT(""));
		});
		if (D == 400.0)
		{
			B.Shot(TEXT("03_pan_after"));
		}
	}
}

void ScenarioZoom(FBuilder& B)
{
	FSnapRef A = MakeShared<FSnap>(), E = MakeShared<FSnap>();
	B.View(0, 0, 600, 0.6, 1.0);
	B.Shot(TEXT("04_zoom_before"));
	B.Do(TEXT("pointer off centre"), [](FC& C) { C.Input->MoveMouse(Px(C, 0.8, 0.3)); });
	B.Wait(2);
	B.Do(TEXT("snap"), [A](FC& C) { Take(C, *A); });
	for (int32 i = 0; i < 5; ++i)
	{
		B.Do(TEXT("wheel in"), [](FC& C) { C.Input->Wheel(1.f); });
		B.Wait(2);
	}
	B.Do(TEXT("check zoom in"), [A, E](FC& C)
	{
		Take(C, *E);
		const double Exp = A->Dist * FMath::Pow(FMath::Pow(0.95, 1.1), 5.0);
		const double Drift = (E->TargetCm - A->TargetCm).Size();
		Report(C, TEXT("zoom: wheel +5 notches (pointer off centre)"), FString::Printf(TEXT("distance %.1f m (x0.95^1.1 per notch); zooms toward the screen centre like the prototype: target unchanged"), Exp),
			FString::Printf(TEXT("distance %.1f m; target moved %.2f cm"), E->Dist, Drift), FMath::Abs(E->Dist - Exp) < 0.5 && Drift < 1.0, TEXT("04_zoom_after.png"));
	});
	B.Shot(TEXT("04_zoom_after"));
	for (int32 i = 0; i < 80; ++i)
	{
		B.Do(TEXT("wheel in"), [](FC& C) { C.Input->Wheel(1.f); });
	}
	B.Wait(3);
	B.Do(TEXT("check zoom min"), [](FC& C)
	{
		Report(C, TEXT("zoom: minimum distance"), TEXT("15 m, camera 3 m above ground"), FString::Printf(TEXT("%.2f m, clearance %.2f m"), C.Pawn->GetDistanceM(), ClearanceM(C)), FMath::IsNearlyEqual(C.Pawn->GetDistanceM(), 15.0, 1e-3) && ClearanceM(C) > 2.99, TEXT("04_zoom_min.png"));
	});
	B.Shot(TEXT("04_zoom_min"));
	for (int32 i = 0; i < 150; ++i)
	{
		B.Do(TEXT("wheel out"), [](FC& C) { C.Input->Wheel(-1.f); });
	}
	B.Wait(3);
	B.Do(TEXT("check zoom max"), [](FC& C)
	{
		Report(C, TEXT("zoom: maximum distance"), TEXT("3800 m"), FString::Printf(TEXT("%.1f m"), C.Pawn->GetDistanceM()), FMath::IsNearlyEqual(C.Pawn->GetDistanceM(), 3800.0, 0.1), TEXT("04_zoom_max.png"));
	});
	B.Shot(TEXT("04_zoom_max"));
}

void ScenarioKeys(FBuilder& B)
{
	FSnapRef A = MakeShared<FSnap>(), E = MakeShared<FSnap>(), A2 = MakeShared<FSnap>(), E2 = MakeShared<FSnap>();
	const int32 Frames = 30;   // 1 s at the fixed 30 fps of the playtest
	B.View(0, 0, 400, 0.6, 1.0);
	B.Hold({EKeys::W}, Frames, A, E);
	B.Do(TEXT("check W"), [A, E, Frames](FC& C)
	{
		const double Moved = GroundMoveM(*A, *E);
		const FVector2D Fwd(FMath::Cos(A->Az), -FMath::Sin(A->Az));   // (north, east)
		const double Exp = 400.0 * 0.9 * (Frames / 30.0);
		Report(C, TEXT("keyboard pan: W for 1 s at 400 m"), FString::Printf(TEXT("moves %.0f m forward (0.9 x distance per second)"), Exp),
			FString::Printf(TEXT("moved %.1f m, forward dot %.3f"), Moved, FVector2D::DotProduct(GroundDir(*A, *E), Fwd)), FMath::Abs(Moved / Exp - 1.0) < 0.1 && FVector2D::DotProduct(GroundDir(*A, *E), Fwd) > 0.999);
	});
	B.View(0, 0, 400, 0.6, 1.0);
	B.Hold({EKeys::LeftShift, EKeys::W}, Frames, A2, E2);
	B.Do(TEXT("check Shift+W"), [A, E, A2, E2](FC& C)
	{
		const double Fast = GroundMoveM(*A2, *E2), Slow = GroundMoveM(*A, *E);
		Report(C, TEXT("keyboard pan: Shift+W"), TEXT("3x the plain pan"), FString::Printf(TEXT("%.1f m vs %.1f m, ratio %.2f"), Fast, Slow, Fast / FMath::Max(Slow, 1e-6)), FMath::Abs(Fast / FMath::Max(Slow, 1e-6) - 3.0) < 0.15);
	});
	const TCHAR* Names[] = {TEXT("A (left)"), TEXT("S (back)"), TEXT("D (right)"), TEXT("Up arrow"), TEXT("Down arrow"), TEXT("Left arrow"), TEXT("Right arrow")};
	const FKey KeyList[] = {EKeys::A, EKeys::S, EKeys::D, EKeys::Up, EKeys::Down, EKeys::Left, EKeys::Right};
	const FVector2D Local[] = {FVector2D(-1, 0), FVector2D(0, -1), FVector2D(1, 0), FVector2D(0, 1), FVector2D(0, -1), FVector2D(-1, 0), FVector2D(1, 0)};   // (right, forward)
	for (int32 i = 0; i < 7; ++i)
	{
		const FString Name = Names[i];
		const FVector2D L = Local[i];
		B.View(0, 0, 400, 0.6, 1.0);
		B.Hold({KeyList[i]}, 15, A, E);
		B.Do(TEXT("check key"), [A, E, Name, L](FC& C)
		{
			const FVector2D Fwd(FMath::Cos(A->Az), -FMath::Sin(A->Az)), Right(FMath::Sin(A->Az), FMath::Cos(A->Az));
			const FVector2D Want = (Fwd * L.Y + Right * L.X).GetSafeNormal();
			const double Dot = FVector2D::DotProduct(GroundDir(*A, *E), Want);
			Report(C, FString::Printf(TEXT("keyboard pan: %s"), *Name), TEXT("moves in the matching ground direction"), FString::Printf(TEXT("direction dot expected %.3f, moved %.1f m"), Dot, GroundMoveM(*A, *E)), Dot > 0.999);
		});
	}
	// Rotate keys: in the prototype E (rot +1) turns the camera offset clockwise seen from above, i.e. theta decreases.
	B.View(0, 0, 400, 0.6, 1.0);
	B.Hold({EKeys::E}, Frames, A, E);
	B.Do(TEXT("check E"), [A, E](FC& C)
	{
		const double D = WrapPi(E->Az - A->Az);
		Report(C, TEXT("keyboard rotate: E for 1 s"), TEXT("azimuth -1.3 rad (prototype: camera orbits clockwise seen from above), target fixed"), FString::Printf(TEXT("azimuth %+.3f rad, target moved %.2f cm"), D, (E->TargetCm - A->TargetCm).Size()), FMath::Abs(D + 1.3) < 0.1 && (E->TargetCm - A->TargetCm).Size() < 1.0);
	});
	B.View(0, 0, 400, 0.6, 1.0);
	B.Hold({EKeys::Q}, Frames, A, E);
	B.Do(TEXT("check Q"), [A, E](FC& C)
	{
		const double D = WrapPi(E->Az - A->Az);
		Report(C, TEXT("keyboard rotate: Q for 1 s"), TEXT("azimuth +1.3 rad"), FString::Printf(TEXT("azimuth %+.3f rad"), D), FMath::Abs(D - 1.3) < 0.1);
	});
}

void ScenarioEdge(FBuilder& B)
{
	FSnapRef A = MakeShared<FSnap>(), E = MakeShared<FSnap>();
	B.View(0, 0, 150, 0.0, 1.2);
	B.Do(TEXT("hold Shift+W (north)"), [A](FC& C) { Take(C, *A); C.Input->KeyDown(EKeys::LeftShift); C.Input->KeyDown(EKeys::W); });
	B.Add(TEXT("run to the edge"), 900, nullptr, nullptr, [](FC& C) { return C.Pawn->GetTargetCm().X >= C.Sim->WorldBoundsCm().Max.X - 0.5; });
	B.Wait(30);
	B.Do(TEXT("release"), [E](FC& C) { Take(C, *E); C.Input->KeyUp(EKeys::W); C.Input->KeyUp(EKeys::LeftShift); });
	B.Wait(5);
	B.Do(TEXT("check edge"), [E](FC& C)
	{
		const FBox2D Bounds = C.Sim->WorldBoundsCm();
		const bool bAtEdge = FMath::IsNearlyEqual(E->TargetCm.X, Bounds.Max.X, 0.5);
		Report(C, TEXT("world edge: Shift+W north until clamped"), TEXT("target stops at the world edge; camera stays 3 m above the ground"),
			FString::Printf(TEXT("target x %.1f of max %.1f cm; camera x %.1f; clearance %.2f m"), E->TargetCm.X, Bounds.Max.X, E->CameraCm.X, ClearanceM(C)), bAtEdge && ClearanceM(C) > 2.99, TEXT("05_edge.png"));
	});
	B.Shot(TEXT("05_edge"));
	B.View(0, 1000, 400, 0.0, 1.35);   // at the north edge looking north over the world boundary
	B.Shot(TEXT("05_edge_horizon"));
}

bool ParseHud(const FString& Hud, double& X, double& Y, double& H)
{
	const FRegexPattern Pattern(TEXT("x\\s+(-?[0-9.]+)\\s+m\\s+y\\s+(-?[0-9.]+)\\s+m\\s+height\\s+(-?[0-9.]+)\\s+m"));
	FRegexMatcher M(Pattern, Hud);
	if (!M.FindNext())
	{
		return false;
	}
	X = FCString::Atod(*M.GetCaptureGroup(1));
	Y = FCString::Atod(*M.GetCaptureGroup(2));
	H = FCString::Atod(*M.GetCaptureGroup(3));
	return true;
}

void CheckPick(FC& C, const FString& Name, double Fx, double Fy, const FString& Image)
{
	FCPUEGroundPick P;
	if (!C.PC->GetCursorPick(P))
	{
		Report(C, FString::Printf(TEXT("pick %s (%.2f, %.2f)"), *Name, Fx, Fy), TEXT("hits the ground"), TEXT("no hit"), false, Image);
		return;
	}
	const double RefH = UCPWorldSubsystem::GetTerrainHeightM(C.Pawn, P.SimX, P.SimY);
	const double HErrCm = (P.HeightM - RefH) * 100.0;
	const double ZErrCm = P.LocationCm.Z - C.Sim->GroundZCm(P.LocationCm.X, P.LocationCm.Y);
	FVector2D Back;
	C.PC->ProjectWorldLocationToScreen(P.LocationCm, Back, false);
	const double PxErr = (Back - C.Input->Pointer()).Size();
	double Hx = 0, Hy = 0, Hh = 0;
	const bool bHud = ParseHud(C.PC->GetHudText(), Hx, Hy, Hh);
	const double HudErrCm = bHud ? FMath::Max3(FMath::Abs(Hx - P.SimX), FMath::Abs(Hy - P.SimY), FMath::Abs(Hh - P.HeightM)) * 100.0 : 1e9;
	Report(C, FString::Printf(TEXT("pick %s (%.2f, %.2f)"), *Name, Fx, Fy), TEXT("pick height equals sim height at the picked x,y (cm); point reprojects to the pointer (px); HUD text equals the pick (cm)"),
		FString::Printf(TEXT("sim x %.2f y %.2f h %.3f m; height error %.2f cm; surface z error %.2f cm; reprojection %.2f px; HUD vs pick %.1f cm"), P.SimX, P.SimY, P.HeightM, HErrCm, ZErrCm, PxErr, HudErrCm),
		FMath::Abs(HErrCm) < 2.0 && FMath::Abs(ZErrCm) < 2.0 && PxErr < 1.5 && HudErrCm < 2.0, Image);
}

void ScenarioPicking(FBuilder& B)
{
	B.View(0, 0, 500, 0.6, 0.9);
	const double Pts[5][2] = {{0.5, 0.5}, {0.25, 0.3}, {0.75, 0.3}, {0.25, 0.8}, {0.75, 0.8}};
	for (int32 i = 0; i < 5; ++i)
	{
		const double Fx = Pts[i][0], Fy = Pts[i][1];
		B.Do(TEXT("pointer"), [Fx, Fy](FC& C) { C.Input->MoveMouse(Px(C, Fx, Fy)); });
		B.Wait(3);
		B.Do(TEXT("check pick"), [Fx, Fy, i](FC& C) { CheckPick(C, FString::Printf(TEXT("%d"), i + 1), Fx, Fy, i == 0 ? TEXT("06_pick_centre.png") : TEXT("")); });
		if (i == 0)
		{
			B.Shot(TEXT("06_pick_centre"));
		}
	}
	// HUD refresh: move the pointer a few pixels per frame and read the shown text each frame.
	TSharedRef<TArray<FString>> Seen = MakeShared<TArray<FString>>();
	B.Add(TEXT("hud moving"), 12, [Seen](FC& C, int32 F)
	{
		C.Input->MoveMouse(Px(C, 0.3, 0.6) + FVector2D(F * 9.0, F * 3.0));
		Seen->Add(C.PC->GetHudText());
	});
	B.Wait(2);
	B.Do(TEXT("check hud refresh"), [Seen](FC& C)
	{
		int32 Changes = 0;
		for (int32 i = 1; i < Seen->Num(); ++i)
		{
			Changes += ((*Seen)[i] != (*Seen)[i - 1]) ? 1 : 0;
		}
		Report(C, TEXT("HUD updates every frame"), TEXT("text changes on at least 9 of 11 frame steps while the pointer moves"), FString::Printf(TEXT("%d of %d changed; last: %s"), Changes, Seen->Num() - 1, *(*Seen).Last().Replace(TEXT("\n"), TEXT(" | "))), Changes >= 9, TEXT("06_hud.png"));
	});
	B.Shot(TEXT("06_hud"));
	// Pointer over the sky: the HUD must say so instead of showing stale numbers.
	B.View(0, 0, 400, 0.6, 1.4);
	B.Do(TEXT("pointer to sky"), [](FC& C) { C.Input->MoveMouse(Px(C, 0.5, 0.03)); });
	B.Wait(3);
	B.Do(TEXT("check sky pick"), [](FC& C)
	{
		FCPUEGroundPick P;
		const bool bHit = C.PC->GetCursorPick(P);
		const FString Hud = C.PC->GetHudText();
		Report(C, TEXT("pick over the sky"), TEXT("no pick, HUD says the cursor is not over the ground"), FString::Printf(TEXT("pick=%d hud: %s"), bHit, *Hud.Replace(TEXT("\n"), TEXT(" | "))), !bHit && Hud.Contains(TEXT("not over")));
	});
}

void ScenarioHudClickThrough(FBuilder& B)
{
	FSnapRef A = MakeShared<FSnap>(), E = MakeShared<FSnap>();
	B.View(0, 0, 600, 0.6, 1.0);
	B.Drag(EKeys::RightMouseButton, 0.04, 0.05, FVector2D(100, 0), 15, A, E);   // starts on top of the HUD panel
	B.Do(TEXT("check hud click-through"), [A, E](FC& C)
	{
		const double Exp = -TwoPi * 100.0 / ViewSize(C).Y, Obs = WrapPi(E->Az - A->Az);
		Report(C, TEXT("HUD does not swallow clicks: right-drag starting on the HUD panel"), FString::Printf(TEXT("azimuth %+.3f rad"), Exp), FString::Printf(TEXT("azimuth %+.3f rad"), Obs), FMath::Abs(Obs - Exp) < 0.02);
	});
}

void ScenarioResize(FBuilder& B)
{
	FSnapRef A = MakeShared<FSnap>(), E = MakeShared<FSnap>();
	B.Do(TEXT("resize window"), [](FC& C) { GEngine->Exec(C.PC->GetWorld(), TEXT("r.SetRes 1000x600w")); });
	B.Wait(60);
	// A resize can pull the window back onto the screen where the operator's real mouse could disturb the synthetic events: park it again.
	B.Do(TEXT("park window"), [](FC& C) { if (const TSharedPtr<SWindow> W = C.PC->GetWorld()->GetGameViewport()->GetWindow()) { W->MoveWindowTo(FVector2f(20000.f, 20000.f)); } });
	B.Wait(30);
	B.View(0, 0, 500, 0.6, 0.9);
	B.Do(TEXT("pointer"), [](FC& C) { C.Input->MoveMouse(Px(C, 0.6, 0.7)); });
	B.Wait(3);
	B.Do(TEXT("check resize"), [](FC& C)
	{
		const FVector2D S = ViewSize(C);
		Report(C, TEXT("window resize"), TEXT("viewport becomes about 1000x600"), FString::Printf(TEXT("%dx%d"), int32(S.X), int32(S.Y)), FMath::Abs(S.X - 1000) < 30 && FMath::Abs(S.Y - 600) < 60);
		CheckPick(C, TEXT("after resize"), 0.6, 0.7, TEXT("07_resized.png"));
	});
	B.Shot(TEXT("07_resized"));
	B.Drag(EKeys::RightMouseButton, 0.5, 0.5, FVector2D(150, 0), 15, A, E);
	B.Do(TEXT("check rotate after resize"), [A, E](FC& C)
	{
		const double Exp = -TwoPi * 150.0 / ViewSize(C).Y, Obs = WrapPi(E->Az - A->Az);
		Report(C, TEXT("rotate after resize: right-drag +150 px"), FString::Printf(TEXT("azimuth %+.3f rad (uses the new height)"), Exp), FString::Printf(TEXT("azimuth %+.3f rad"), Obs), FMath::Abs(Obs - Exp) < 0.02);
	});
}
}  // namespace

void BuildCPPlaytestScript(TArray<FCPPlaytestStep>& Out)
{
	FBuilder B{Out};
	ScenarioStartup(B);
	ScenarioRotate(B);
	ScenarioPan(B);
	ScenarioZoom(B);
	ScenarioKeys(B);
	ScenarioEdge(B);
	ScenarioPicking(B);
	ScenarioHudClickThrough(B);
	ScenarioResize(B);
	AppendCPPlaytestRoadScenarios(Out);   // M2: roads, undo / redo (Playtest/CPPlaytestRoads.cpp)
}
