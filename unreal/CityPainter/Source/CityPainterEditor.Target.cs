using UnrealBuildTool;
using System.Collections.Generic;

public class CityPainterEditorTarget : TargetRules
{
	public CityPainterEditorTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Editor;
		DefaultBuildSettings = BuildSettingsVersion.Latest;
		IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
		ExtraModuleNames.Add("CityPainter");
	}
}
