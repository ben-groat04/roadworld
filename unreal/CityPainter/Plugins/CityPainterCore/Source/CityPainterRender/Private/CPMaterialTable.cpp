#include "CPMaterialTable.h"

#include "CityPainterRender.h"

#include "MaterialDomain.h"
#include "Materials/Material.h"
#include "Materials/MaterialInterface.h"
#include "UObject/UObjectGlobals.h"

namespace
{
// Material instances built by unreal/tools/py/mat_defs*.py (same classes as setup_look_test_level.py CP_CLASS_MATERIALS).
struct FSurfaceRow
{
	const TCHAR* SurfaceClass;
	const TCHAR* ObjectPath;
};
constexpr FSurfaceRow SurfaceTable[] = {
	{TEXT("asphalt"), TEXT("/Game/CityPainter/Materials/MI_RoadAsphalt.MI_RoadAsphalt")},
	{TEXT("paving"), TEXT("/Game/CityPainter/Materials/MI_Footway.MI_Footway")},
	{TEXT("kerb"), TEXT("/Game/CityPainter/Materials/MI_Kerb.MI_Kerb")},
	{TEXT("grass"), TEXT("/Game/CityPainter/Materials/MI_Ground.MI_Ground")},
	{TEXT("island"), TEXT("/Game/CityPainter/Materials/MI_Ground.MI_Ground")},
};
// Markings: MI_Marking_<colour id> (white, yellow); a colour without an asset falls back to the default material.
}  // namespace

UMaterialInterface* FCPMaterialTable::Resolve(const FString& CacheKey, const FString& ObjectPath)
{
	if (const TStrongObjectPtr<UMaterialInterface>* Hit = Cache.Find(CacheKey))
	{
		return Hit->Get();
	}
	UMaterialInterface* Material = ObjectPath.IsEmpty() ? nullptr : LoadObject<UMaterialInterface>(nullptr, *ObjectPath, nullptr, LOAD_NoWarn | LOAD_Quiet);
	if (Material)
	{
		UE_LOG(LogCPRender, Log, TEXT("Material '%s' -> %s"), *CacheKey, *ObjectPath);
	}
	else
	{
		Material = UMaterial::GetDefaultMaterial(MD_Surface);
		UE_LOG(LogCPRender, Warning, TEXT("Material '%s': asset '%s' not found, using the engine default material"), *CacheKey, *ObjectPath);
	}
	Cache.Add(CacheKey, TStrongObjectPtr<UMaterialInterface>(Material));
	return Material;
}

UMaterialInterface* FCPMaterialTable::ForSurface(const FString& SurfaceClass)
{
	for (const FSurfaceRow& Row : SurfaceTable)
	{
		if (SurfaceClass == Row.SurfaceClass)
		{
			return Resolve(TEXT("surface:") + SurfaceClass, Row.ObjectPath);
		}
	}
	return Resolve(TEXT("surface:") + SurfaceClass, FString());  // unknown class: default material, the warning names the class
}

UMaterialInterface* FCPMaterialTable::ForMarking(const FString& ColourId)
{
	return Resolve(TEXT("marking:") + ColourId, FString::Printf(TEXT("/Game/CityPainter/Materials/MI_Marking_%s.MI_Marking_%s"), *ColourId, *ColourId));
}
