#pragma once

// FCPPlaytestResults: collects what a playtest observed (checks with expected / observed / pass, numeric metrics, the
// screenshots taken) and writes results.json. It knows nothing about the camera or the sim: callers hand it values.

#include "CoreMinimal.h"

struct FCPPlaytestCheck
{
	FString Control;     // which control or feature, e.g. "rotate: drag right 200 px"
	FString Expected;
	FString Observed;
	bool bPass = false;
	FString Image;       // file name of the screenshot that goes with it, may be empty
};

class FCPPlaytestResults
{
public:
	void SetOutputDir(const FString& Dir) { OutputDir = Dir; }
	const FString& GetOutputDir() const { return OutputDir; }

	void AddCheck(const FString& Control, const FString& Expected, const FString& Observed, bool bPass, const FString& Image = FString());
	void SetMetric(const FString& Name, double Value) { Metrics.Add(Name, Value); }
	void SetNote(const FString& Name, const FString& Value) { Notes.Add(Name, Value); }
	/** Asks the engine for a screenshot (viewport including the HUD) named <Name>.png in the output directory. */
	FString RequestShot(const FString& Name);

	int32 NumFailed() const;
	/** Writes results.json; returns false on I/O failure. */
	bool Write() const;

private:
	FString OutputDir;
	TArray<FCPPlaytestCheck> Checks;
	TMap<FString, double> Metrics;
	TMap<FString, FString> Notes;
	TArray<FString> Shots;
};
