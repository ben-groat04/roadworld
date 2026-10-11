#include "CPSettings.h"

#include "HAL/FileManager.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/Paths.h"

UCPSettings::UCPSettings()
{
	CategoryName = TEXT("Plugins");
	SectionName = TEXT("City Painter");
}

FString UCPSettings::ResolveDataDir() const
{
	auto Valid = [](const FString& Candidate, FString& Out)
	{
		FString Full = FPaths::ConvertRelativePathToFull(Candidate);
		FPaths::CollapseRelativeDirectories(Full);
		FPaths::NormalizeDirectoryName(Full);
		if (IFileManager::Get().FileExists(*FPaths::Combine(Full, TEXT("lane_types.json"))))
		{
			Out = Full;
			return true;
		}
		return false;
	};

	FString Result;
	if (!DataDir.Path.IsEmpty() && Valid(DataDir.Path, Result))
	{
		return Result;
	}
	// <repo>/unreal/CityPainter/Plugins/CityPainterCore -> up four levels = <repo>.
	const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("CityPainterCore"));
	if (Plugin.IsValid() && Valid(FPaths::Combine(Plugin->GetBaseDir(), TEXT("../../../../data")), Result))
	{
		return Result;
	}
	// <repo>/unreal/CityPainter (project dir) -> up two levels.
	if (Valid(FPaths::Combine(FPaths::ProjectDir(), TEXT("../../data")), Result))
	{
		return Result;
	}
	return FString();
}
