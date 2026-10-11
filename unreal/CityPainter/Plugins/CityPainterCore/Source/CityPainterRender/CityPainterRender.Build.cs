using UnrealBuildTool;

// Render module: draws what the sim says (layer registry, tile meshes). Depends on World and Sim (downwards). It does
// not include sim_core headers (Sim hides them) and the mesh-component plugin is a PRIVATE dependency used by exactly
// one file (Private/CPProcTileMesh.cpp), so switching the mesh technology touches one place.
public class CityPainterRender : ModuleRules
{
	public CityPainterRender(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		CppStandard = CppStandardVersion.Cpp20;

		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "DeveloperSettings", "CityPainterSim", "CityPainterWorld" });
		PrivateDependencyModuleNames.AddRange(new string[] { "ProceduralMeshComponent" });
	}
}
