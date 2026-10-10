using System.IO;
using UnrealBuildTool;

// Compiles sim_core straight from the repo. UBT only globs the module folder, so
// each sim_core .cpp is pulled in by a one-line wrapper in Private/SimCore/ that
// #includes it by relative path. sim_core is C++20 and exception-free; nlohmann/json (parse with
// allow_exceptions=false) and Clipper2 (DoError is a no-op) compile throw-free with exceptions off. RTTI stays off.
// If sim_core gains a new .cpp, add a Private/SimCore/Wrap_<name>.cpp for it.
public class CityPainterSim : ModuleRules
{
	public CityPainterSim(ReadOnlyTargetRules Target) : base(Target)
	{
		// No PCH and no unity: keeps UE headers/macros out of sim_core translation units
		// (and avoids anonymous-namespace collisions between sim_core .cpp files).
		PCHUsage = PCHUsageMode.NoPCHs;
		bUseUnity = false;
		bEnableExceptions = false; // sim_core is built exception-free: no throw/try/catch, nlohmann parse(allow_exceptions=false), no throwing filesystem calls
		CppStandard = CppStandardVersion.Cpp20;

		// Third-party-style warning relaxation. UE promotes some warnings to errors.
		ShadowVariableWarningLevel = WarningLevel.Off;
		UnsafeTypeCastWarningLevel = WarningLevel.Off;
		bEnableUndefinedIdentifierWarnings = false;

		// module dir = <repo>/unreal/CityPainter/Plugins/CityPainterSim/Source/CityPainterSim
		string RepoRoot = Path.GetFullPath(Path.Combine(ModuleDirectory, "..", "..", "..", "..", "..", ".."));
		string SimCore = Path.Combine(RepoRoot, "sim_core");
		string ThirdParty = Path.Combine(RepoRoot, "third_party");

		// Guard: every sim_core/src/*.cpp needs a Private/SimCore/Wrap_<name>.cpp that includes it, otherwise the
		// build fails much later with an unresolved external (LNK2019) that does not name the cause.
		string WrapperDir = Path.Combine(ModuleDirectory, "Private", "SimCore");
		foreach (string Src in Directory.GetFiles(Path.Combine(SimCore, "src"), "*.cpp"))
		{
			string Name = Path.GetFileName(Src);
			string Wrapper = Path.Combine(WrapperDir, "Wrap_" + Name);
			if (!File.Exists(Wrapper))
			{
				throw new BuildException("sim_core/src/" + Name + " has no wrapper. Add " + Wrapper
					+ " (copy Wrap_region_pack.cpp, including its full #pragma warning list, and change the #include to sim_core/src/" + Name + ").");
			}
			if (!File.ReadAllText(Wrapper).Contains("sim_core/src/" + Name))
			{
				throw new BuildException(Wrapper + " does not include sim_core/src/" + Name + ".");
			}
		}

		PublicIncludePaths.Add(Path.Combine(SimCore, "include"));
		// third_party/ holds nlohmann/json.hpp (included as "nlohmann/json.hpp"); Clipper2 headers are included as
		// "clipper2/clipper.h" and live under third_party/clipper2/include. Clipper2's .cpp files are compiled via
		// Private/SimCore/Wrap_clipper2_*.cpp.
		PrivateIncludePaths.Add(ThirdParty);
		PrivateIncludePaths.Add(Path.Combine(ThirdParty, "clipper2", "include"));

		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "ProceduralMeshComponent", "Projects" });
	}
}
