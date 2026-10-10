using UnrealBuildTool;

public class CityPainter : ModuleRules
{
	public CityPainter(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "InputCore", "CityPainterSim" });
	}
}
