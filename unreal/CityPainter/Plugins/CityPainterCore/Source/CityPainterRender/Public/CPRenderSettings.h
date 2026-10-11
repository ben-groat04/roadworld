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

	/** Material of the terrain (created by unreal/tools/py/mat_defs.py). MI_Ground_Floor has VertexDirtWeight 0: the terrain mesh carries no vertex colours (they would read as white = all soil). */
	UPROPERTY(Config, EditAnywhere, Category = "Ground")
	TSoftObjectPtr<UMaterialInterface> GroundMaterial = TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(TEXT("/Game/CityPainter/Materials/MI_Ground_Floor.MI_Ground_Floor")));

	/** Translucent unlit material of the road tool's ghost ribbon; it needs a vector parameter named Color whose alpha is the opacity (the engine's M_SimpleUnlitTranslucent has a Color parameter; check the alpha wiring on first run). */
	UPROPERTY(Config, EditAnywhere, Category = "Tools")
	TSoftObjectPtr<UMaterialInterface> GhostMaterial = TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(TEXT("/Game/CityPainter/Materials/M_Ghost.M_Ghost")));

	/** Heightfield cells per terrain mesh tile side (cell size comes from the world settings: 64 x 4 m = 256 m tiles). */
	UPROPERTY(Config, EditAnywhere, Category = "Ground", meta = (ClampMin = "8", ClampMax = "255"))
	int32 TerrainTileCells = 64;

	/** The ground continues this far (metres) beyond the edge of the world at the edge height, so the world does not end in a void. 0 = off. */
	UPROPERTY(Config, EditAnywhere, Category = "Ground", meta = (ClampMin = "0"))
	double SkirtDistanceM = 20000.0;
};
