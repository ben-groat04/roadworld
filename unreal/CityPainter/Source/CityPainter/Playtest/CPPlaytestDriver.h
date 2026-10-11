#pragma once

// UCPPlaytestDriver: runs the scripted playtest (CPPlaytestScript.cpp) frame by frame inside a real game session, then
// writes results.json and quits. Started by the player controller when the command line has -CPPlaytest. It only
// sequences steps; the steps send input through FCPPlaytestInput and read the real pawn/controller/sim.

#include "CoreMinimal.h"
#include "Tickable.h"
#include "UObject/Object.h"

#include "CPPlaytestTypes.h"

#include "CPPlaytestDriver.generated.h"

UCLASS()
class UCPPlaytestDriver : public UObject, public FTickableGameObject
{
	GENERATED_BODY()

public:
	/** Creates and starts a driver when -CPPlaytest is on the command line (non-shipping builds only). */
	static void MaybeStart(ACPPlayerController* Controller);

	// FTickableGameObject
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;
	virtual bool IsTickable() const override { return bRunning; }
	virtual UWorld* GetTickableGameObjectWorld() const override;

private:
	void Begin(ACPPlayerController* Controller);
	void Finish();

	FCPPlaytestContext Context;
	TUniquePtr<FCPPlaytestInput> Input;
	FCPPlaytestResults Results;
	TArray<FCPPlaytestStep> Steps;
	int32 StepIndex = 0;
	int32 FrameInStep = 0;
	double LastWallSeconds = 0;
	bool bRunning = false;
};
