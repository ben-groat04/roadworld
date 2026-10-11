#include "CPPlaytestDriver.h"

#include "CPPlaytestScript.h"
#include "../CPCameraPawn.h"
#include "../CPPlayerController.h"
#include "CPWorldSubsystem.h"
#include "../CityPainterLog.h"

#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "HAL/PlatformTime.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"

void UCPPlaytestDriver::MaybeStart(ACPPlayerController* Controller)
{
#if !UE_BUILD_SHIPPING
	if (Controller && FParse::Param(FCommandLine::Get(), TEXT("CPPlaytest")))
	{
		UCPPlaytestDriver* Driver = NewObject<UCPPlaytestDriver>(Controller);
		Driver->AddToRoot();   // lives until Finish(); the controller does not own it
		Driver->Begin(Controller);
	}
#endif
}

void UCPPlaytestDriver::Begin(ACPPlayerController* Controller)
{
	FString OutDir;
	if (!FParse::Value(FCommandLine::Get(), TEXT("CPPlaytestOut="), OutDir))
	{
		OutDir = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Playtest"), TEXT("latest"));
	}
	OutDir = FPaths::ConvertRelativePathToFull(OutDir);
	IFileManager::Get().MakeDirectory(*OutDir, true);
	Results.SetOutputDir(OutDir);

	UWorld* World = Controller->GetWorld();
	Input = MakeUnique<FCPPlaytestInput>(World ? World->GetGameViewport() : nullptr);
	Context.PC = Controller;
	Context.Pawn = Cast<ACPCameraPawn>(Controller->GetPawn());
	Context.Sim = World ? World->GetSubsystem<UCPWorldSubsystem>() : nullptr;
	Context.Input = Input.Get();
	Context.Results = &Results;
	BuildCPPlaytestScript(Steps);
	LastWallSeconds = FPlatformTime::Seconds();
	bRunning = true;
	UE_LOG(LogCPGame, Display, TEXT("CPPlaytest: %d steps, output %s"), Steps.Num(), *OutDir);
}

TStatId UCPPlaytestDriver::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UCPPlaytestDriver, STATGROUP_Tickables);
}

UWorld* UCPPlaytestDriver::GetTickableGameObjectWorld() const
{
	return Context.PC ? Context.PC->GetWorld() : nullptr;
}

void UCPPlaytestDriver::Tick(float DeltaTime)
{
	const double Now = FPlatformTime::Seconds();
	if (Context.bSampleFrames)
	{
		Context.FrameMs.Add((Now - LastWallSeconds) * 1000.0);
	}
	LastWallSeconds = Now;
	++Context.FrameIndex;

	if (StepIndex >= Steps.Num())
	{
		Finish();
		return;
	}
	if (!Context.Pawn)
	{
		Context.Pawn = Cast<ACPCameraPawn>(Context.PC->GetPawn());
	}
	FCPPlaytestStep& Step = Steps[StepIndex];
	if (FrameInStep == 0)
	{
		UE_LOG(LogCPGame, Display, TEXT("CPPlaytest step %d/%d: %s"), StepIndex + 1, Steps.Num(), *Step.Label);
	}
	if (Step.OnFrame)
	{
		Step.OnFrame(Context, FrameInStep);
	}
	++FrameInStep;
	const bool bDone = FrameInStep >= Step.Frames || (Step.Until && Step.Until(Context));
	if (bDone)
	{
		if (Step.OnEnd)
		{
			Step.OnEnd(Context);
		}
		++StepIndex;
		FrameInStep = 0;
	}
}

void UCPPlaytestDriver::Finish()
{
	bRunning = false;
	Input->ReleaseAll();
	const bool bWritten = Results.Write();
	UE_LOG(LogCPGame, Display, TEXT("CPPlaytest: finished, %d checks failed, results %s (%s)"), Results.NumFailed(), *Results.GetOutputDir(), bWritten ? TEXT("written") : TEXT("WRITE FAILED"));
	FFileHelper::SaveStringToFile(FString::Printf(TEXT("CP_PLAYTEST_DONE failed=%d\n"), Results.NumFailed()), *FPaths::Combine(Results.GetOutputDir(), TEXT("done.txt")));
	FPlatformMisc::RequestExit(false);
}
