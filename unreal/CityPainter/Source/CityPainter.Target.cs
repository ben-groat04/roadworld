using UnrealBuildTool;
using System.Collections.Generic;

public class CityPainterTarget : TargetRules
{
	public CityPainterTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Game;
		DefaultBuildSettings = BuildSettingsVersion.Latest;
		IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
		ExtraModuleNames.Add("CityPainter");
	}
}
