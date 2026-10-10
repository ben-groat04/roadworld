using System.IO;
using UnrealBuildTool;

// Compiles sim_core straight from the repo. UBT only globs the module folder, so
// each sim_core .cpp is pulled in by a one-line wrapper in Private/SimCore/ that
// #includes it by relative path. sim_core is C++20. The geometry/graph/command files use no
// exceptions or RTTI. sim_core is built exception-free (bEnableExceptions = false); RTTI stays off
// (nlohmann/json does not need it).
// If sim_core gains a new .cpp, add a Private/SimCore/Wrap_<name>.cpp for it.
public class CityPainterSim : ModuleRules
{
	public CityPainterSim(ReadOnlyTargetRules Target) : base(Target)
	{
		// No PCH and no unity: keeps UE headers/macros out of sim_core translation units
		// (and avoids anonymous-namespace collisions between sim_core .cpp files).
		PCHUsage = PCHUsageMode.NoPCHs;
		bUseUnity = false;
		bEnableExceptions = false; // sim_core is built exception-free
		CppStandard = CppStandardVersion.Cpp20;

		// Third-party-style warning relaxation. UE promotes some warnings to errors.
		ShadowVariableWarningLevel = WarningLevel.Off;
		UnsafeTypeCastWarningLevel = WarningLevel.Off;
		bEnableUndefinedIdentifierWarnings = false;

		// module dir = <repo>/unreal/CityPainter/Plugins/CityPainterSim/Source/CityPainterSim
		string RepoRoot = Path.GetFullPath(Path.Combine(ModuleDirectory, "..", "..", "..", "..", "..", ".."));
		string SimCore = Path.Combine(RepoRoot, "sim_core");
		string ThirdParty = Path.Combine(RepoRoot, "third_party");

		PublicIncludePaths.Add(Path.Combine(SimCore, "include"));
		// third_party/ holds nlohmann/json.hpp (included as "nlohmann/json.hpp"); harmless if absent.
		foreach (string Dir in new string[] { "." })
		{
			string Full = Path.Combine(ThirdParty, Dir);
			if (Directory.Exists(Full)) { PublicIncludePaths.Add(Full); }
		}

		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "ProceduralMeshComponent" });
	}
}
