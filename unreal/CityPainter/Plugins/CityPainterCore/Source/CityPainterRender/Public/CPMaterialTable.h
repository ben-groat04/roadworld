#pragma once

// FCPMaterialTable: the ONE mapping from what the sim says a surface or a marking is (surface class "asphalt", paint
// colour "white") to a material asset, with the fallback when an asset is missing (logged once per key, never silent).
// Layers ask it for a material by sim key; they do not know asset paths. It must not know about tiles or meshes.

#include "CoreMinimal.h"
#include "UObject/StrongObjectPtr.h"

class UMaterialInterface;

class CITYPAINTERRENDER_API FCPMaterialTable
{
public:
	/** Material of a road surface class ("asphalt", "paving", "kerb", "grass", "island"). Never null. */
	UMaterialInterface* ForSurface(const FString& SurfaceClass);
	/** Material of a paint colour id ("white", "yellow"). Never null. */
	UMaterialInterface* ForMarking(const FString& ColourId);

private:
	UMaterialInterface* Resolve(const FString& CacheKey, const FString& ObjectPath);
	TMap<FString, TStrongObjectPtr<UMaterialInterface>> Cache;
};
