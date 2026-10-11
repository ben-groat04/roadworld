#pragma once

// UCPRenderSettings: project settings for drawing (materials by asset path, mesh tile size). Values only. It must not
// know about the simulation or about any particular layer's code.

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "Materials/MaterialInterface.h"

#include "CPRenderSettings.generated.h"

UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "City Painter Rendering"))
class CITYPAINTERRENDER_API UCPRenderSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UCPRenderSettings();

	/** Material of the terrain (created by unreal/tools/py/mat_defs.py). */
	UPROPERTY(Config, EditAnywhere, Category = "Ground")
	TSoftObjectPtr<UMaterialInterface> GroundMaterial = TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(TEXT("/Game/CityPainter/Materials/MI_Ground.MI_Ground")));

	/** Heightfield cells per terrain mesh tile side (cell size comes from the world settings: 64 x 4 m = 256 m tiles). */
	UPROPERTY(Config, EditAnywhere, Category = "Ground", meta = (ClampMin = "8", ClampMax = "255"))
	int32 TerrainTileCells = 64;
};
