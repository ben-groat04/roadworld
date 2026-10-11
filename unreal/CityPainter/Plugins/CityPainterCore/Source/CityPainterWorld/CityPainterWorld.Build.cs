using UnrealBuildTool;

// World module: hosts the simulation inside a UWorld (subsystem, settings). Depends on Sim only (downwards).
// It must not depend on Render, ProceduralMeshComponent, UMG or the game module.
public class CityPainterWorld : ModuleRules
{
	public CityPainterWorld(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		CppStandard = CppStandardVersion.Cpp20;

		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "DeveloperSettings", "CityPainterSim" });
		PrivateDependencyModuleNames.AddRange(new string[] { "Projects" });
	}
}
