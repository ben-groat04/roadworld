#pragma once

// Shared types of the playtest: the context every step receives and the step itself. No logic here.

#include "CoreMinimal.h"

#include "CPPlaytestInput.h"
#include "CPPlaytestResults.h"

class ACPCameraPawn;
class ACPPlayerController;
class UCPWorldSubsystem;

struct FCPPlaytestContext
{
	ACPPlayerController* PC = nullptr;
	ACPCameraPawn* Pawn = nullptr;
	UCPWorldSubsystem* Sim = nullptr;
	FCPPlaytestInput* Input = nullptr;
	FCPPlaytestResults* Results = nullptr;

	/** Wall-clock milliseconds of every frame since StartFrameSampling (for frame-time statistics). */
	TArray<double> FrameMs;
	bool bSampleFrames = false;
	int32 FrameIndex = 0;
};

struct FCPPlaytestStep
{
	FString Label;
	int32 Frames = 1;                                            // run for this many frames (a timeout when Until is set)
	TFunction<void(FCPPlaytestContext&, int32)> OnFrame;         // every frame, argument = frame within the step
	TFunction<void(FCPPlaytestContext&)> OnEnd;                  // once, after the last frame
	TFunction<bool(FCPPlaytestContext&)> Until;                  // optional: the step also ends when this returns true
};
