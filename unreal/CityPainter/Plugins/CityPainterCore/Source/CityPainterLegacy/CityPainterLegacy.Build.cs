using UnrealBuildTool;

// LEGACY module: the old all-in-one road actor (ACPRoadMeshActor) used by the RoadTest look-test level until road render
// layers exist. Nothing depends on this module; it depends only on Sim. Delete it with the actor.
public class CityPainterLegacy : ModuleRules
{
	public CityPainterLegacy(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		CppStandard = CppStandardVersion.Cpp20;
		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "ProceduralMeshComponent", "Projects", "CityPainterSim" });
	}
}
