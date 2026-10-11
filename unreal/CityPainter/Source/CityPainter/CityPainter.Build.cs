using UnrealBuildTool;

// Game module: wiring only (game mode, controller, camera pawn, input, HUD). Depends downwards on World. It never
// includes sim_core headers (the Sim facade is the only door) and never touches mesh components.
public class CityPainter : ModuleRules
{
	public CityPainter(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		CppStandard = CppStandardVersion.Cpp20;
		PrivateIncludePaths.Add(ModuleDirectory);   // lets files in Tools/ and Playtest/ include each other as "Tools/CPTool.h"
		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "InputCore", "EnhancedInput", "UMG", "Slate", "SlateCore", "CityPainterSim", "CityPainterWorld", "ApplicationCore", "Json" });
		// Loaded (not linked against) so the Render subsystem exists in every City Painter world.
		PrivateDependencyModuleNames.AddRange(new string[] { "CityPainterRender" });
	}
}
