#pragma once

// UCPSettings: project settings for the City Painter world (Project Settings > Plugins > City Painter). Holds
// values only: where the data folder is, which region pack and seed to use, how big the test world is, and which
// maps get a City Painter world. It must not know about actors, meshes or the simulation itself.

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "Engine/EngineTypes.h"

#include "CPSettings.generated.h"

UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "City Painter"))
class CITYPAINTERWORLD_API UCPSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UCPSettings();

	/** Absolute path of the repo's data/ folder (lane_types.json, regions/). Empty = found by walking up from the plugin. */
	UPROPERTY(Config, EditAnywhere, Category = "Data")
	FDirectoryPath DataDir;

	/** Region pack under data/regions/. The core is region-agnostic; this is only a setting. */
	UPROPERTY(Config, EditAnywhere, Category = "Data")
	FString RegionId = TEXT("uk");

	/** Seed of the generated test world (a stand-in for a future world generator). */
	UPROPERTY(Config, EditAnywhere, Category = "Test world")
	int64 WorldSeed = 1;

	/** Edge length of the square test world in metres, centred on the origin. */
	UPROPERTY(Config, EditAnywhere, Category = "Test world", meta = (ClampMin = "64"))
	double WorldSizeM = 2048.0;

	/** Terrain heightfield cell size in metres. */
	UPROPERTY(Config, EditAnywhere, Category = "Test world", meta = (ClampMin = "0.5"))
	double CellSizeM = 4.0;

	/** Maps whose package path starts with this get a City Painter world (the subsystems do nothing elsewhere). */
	UPROPERTY(Config, EditAnywhere, Category = "Maps")
	FString MapPathPrefix = TEXT("/Game/CityPainter/Maps/");

	/** The data folder to use: DataDir when it holds lane_types.json, else found next to the plugin; empty if none. */
	FString ResolveDataDir() const;
};
