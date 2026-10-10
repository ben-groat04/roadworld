# Predicted first-build failures and their mechanical fixes

Written before UE 5.8 was installed, from reading the sources only. Nothing here has been compiled. When
`Build-Editor.ps1` prints an error, find its message below and apply the listed edit. Check a name against the
engine before editing: `Engine\Source\Programs\UnrealBuildTool\Configuration\*.cs` (ModuleRules.cs, TargetRules.cs) is the truth.
Quick lookup: `findstr /s /n /c:"CppCompileWarningSettings" <UE>\Engine\Source\Programs\UnrealBuildTool\Configuration\*.cs`

## A. Build.cs / Target.cs (C# rules compile, runs before any C++)

| Message | Edit |
|---|---|
| `CS0117 'ModuleRules' does not contain a definition for 'ShadowVariableWarningLevel'` (or `UnsafeTypeCastWarningLevel`, `bEnableUndefinedIdentifierWarnings`) at `CityPainterSim.Build.cs` ~22-24 | Property moved into the warning-settings object. Replace the three lines with `CppCompileWarningSettings.ShadowVariableWarningLevel = WarningLevel.Off;` `CppCompileWarningSettings.UnsafeTypeCastWarningLevel = WarningLevel.Off;` `CppCompileWarningSettings.UndefinedIdentifierWarningLevel = WarningLevel.Off;` (5.6+ names as I remember them; confirm with the findstr above. If `UndefinedIdentifierWarningLevel` does not exist look for the property whose doc comment mentions C4668 / `-Wundef`). |
| `CS0618 ... is obsolete` / warning on the same three lines, but the build continues | Same replacement; warning only. |
| `CS0117 'BuildSettingsVersion' does not contain a definition for 'Latest'` in `CityPainter.Target.cs` / `CityPainterEditor.Target.cs` | Use the newest enum member listed in TargetRules.cs (`BuildSettingsVersion.V5` or `V6`); same for `EngineIncludeOrderVersion.Latest` -> the newest `Unreal5_x` member (`Unreal5_8` if present, else the highest). Edit both files. |
| `Expecting to find a type to be declared in a target rules named 'CityPainterEditorTarget'` | Follow-on of a Target.cs compile error above. Fix that first. |
| `CS0246 The type or namespace name 'WarningLevel' could not be found` | `using UnrealBuildTool;` already present; the enum may be nested: use `ModuleRules.WarningLevel.Off`. |
| `Plugin 'EditorScriptingUtilities' ... not found` or editor prompt about it | Delete that line from `CityPainter.uproject` (the Python scripts only use it as a fallback). Same for `PythonScriptPlugin` if the name is wrong (then use Edit > Plugins > search "Python" to read the real name). |
| `Could not find engine for project` / `EngineAssociation "5.8"` unresolved | Register the launcher install: run `<UE>\Engine\Binaries\Win64\UnrealVersionSelector-Win64-Shipping.exe /register` (admin), or right-click the .uproject -> Switch Unreal Engine version. |
| `No suitable Visual C++ installation / toolchain found`, or a version mismatch warning for MSVC 14.51 | Compare with `<UE>\Engine\Config\Windows\Windows_SDK.json`. Pin the compiler in `%APPDATA%\Unreal Engine\UnrealBuildTool\BuildConfiguration.xml`: `<?xml version="1.0"?><Configuration xmlns="https://www.unrealengine.com/BuildConfiguration"><WindowsPlatform><CompilerVersion>14.51.xxxxx</CompilerVersion></WindowsPlatform></Configuration>` (use the real folder name under `...\VC\Tools\MSVC\`). Open VS Installer and add the MSVC version the json prefers if 14.51 is rejected. |
| `Unable to find .NET` / dotnet errors in UBT | UBT uses the engine's bundled dotnet; check `<UE>\Engine\Binaries\ThirdParty\DotNet` exists (install incomplete?). |

## B. C++ exceptions / `<filesystem>` (most likely first C++ errors)

`CityPainterSim.Build.cs` sets `bEnableExceptions = false`. `region_pack.h` includes `<filesystem>`, which is pulled into most
`Wrap_*.cpp` and `CPRoadNetworkActor.cpp`. Only `Wrap_region_pack.cpp` disables the warnings.

| Message | Edit |
|---|---|
| `C4577: 'noexcept' used with no exception handling mode specified` (in `filesystem`, `xlocale`, ...) | Preferred, one-line, covers every file: in `CityPainterSim.Build.cs` set `bEnableExceptions = true;` (and update the comment above it). sim_core compiles fine with exceptions on. |
| `C4530: C++ exception handler used, but unwind semantics are not enabled` | Same fix (`bEnableExceptions = true`). |
| Either of the two above, but you want exceptions off | Add `AdditionalCompilerArguments = "/wd4577 /wd4530";` to Build.cs (property exists on ModuleRules in 5.x; if not, add `#pragma warning(disable: 4577 4530)` at the top of every `Wrap_*.cpp` and `CPRoadNetworkActor.cpp`, before any include). |
| `error C2... 'throw'/'try' used with /EHs-c-` or STL `_HAS_EXCEPTIONS` complaints from `<filesystem>` | `bEnableExceptions = true`. |
| `C4541: 'typeid'/'dynamic_cast' used on polymorphic type with /GR-` (nlohmann/json, Clipper2) | Add `bUseRTTI = true;` to Build.cs. |

## C. Warnings promoted to errors in a non-engine module

| Message | Edit |
|---|---|
| `C4456` / `C4457` / `C4458` / `C4459` `declaration of 'x' hides ...` | Shadow warnings. The Off setting from section A must be in effect (the `CppCompileWarningSettings` form). |
| `C4244` / `C4267` / `C4838` conversion, narrowing | `UnsafeTypeCastWarningLevel = Off` (section A). |
| `C4668 'X' is not defined as a preprocessor macro, replacing with '0'` (Clipper2, nlohmann) | Undefined-identifier setting Off (section A). If it is still an error: `PrivateDefinitions`/pragma are no use; wrap the include in `THIRD_PARTY_INCLUDES_START` / `THIRD_PARTY_INCLUDES_END` (from `Misc/CoreMiscDefines.h`) inside the specific `Wrap_*.cpp`. |
| `C4996` deprecated | Fix or add `#pragma warning(disable: 4996)` in the wrapper. |
| `C5105 macro expansion producing 'defined'` in Windows SDK headers | New SDK + conforming preprocessor. Add `/wd5105` through `AdditionalCompilerArguments`. |
| `C4702 unreachable code`, `C4100 unreferenced parameter`, `C4189` | Warning, not error, unless `bWarningsAsErrors`; disable via the wrapper pragma if they are errors. |

## D. UE macros colliding with sim_core / third-party names (only in TUs that include UE headers: `CPRoadNetworkActor.cpp`)

| Message | Edit |
|---|---|
| `C2059`/`C2143` near an identifier named `check`, `verify`, `ensure`, `TEXT`, `PI`, `INDEX_NONE`, `min`, `max` | A UE macro is eating a sim_core or Clipper2 identifier. Include the sim_core header BEFORE the UE headers in that .cpp, or `#undef` the macro after the UE includes. Do not rename sim_core symbols. |
| `C2872 'X': ambiguous symbol` | A name exists in both `cp::` code and UE (e.g. `FVector`-like types). Qualify with `cp::`. |
| Duplicate-symbol / `C2084 function already has a body` between `Wrap_*.cpp` files | Unity is off already; two sim_core .cpp files define the same anonymous-namespace-free symbol, or a file is wrapped twice. Check `Private/SimCore/` for a duplicate wrapper. |

## E. UHT (UnrealHeaderTool)

| Message | Edit |
|---|---|
| `Unrecognized type 'X'` / `Missing '*' in ...` in `CPRoadNetworkActor.h` | A non-UObject type in a UPROPERTY/UFUNCTION. `FCPSimState` is intentionally NOT exposed; check no UPROPERTY mentions it. |
| `... must include "CPRoadNetworkActor.generated.h" as the last include` | Move the `.generated.h` include to the last line of includes. |
| `Expected a ... forward declaration` for `TObjectPtr<UProceduralMeshComponent>` | Include `ProceduralMeshComponent.h` in the cpp; header keeps the forward declaration `class UProceduralMeshComponent;`. |
| `GENERATED_BODY ... destructor / FVTableHelper` errors, or `C2027 use of undefined type 'FCPSimState'` in generated code | The header declares `ACPRoadNetworkActor(FVTableHelper&)` and an out-of-line destructor so `TUniquePtr<FCPSimState>` is never instantiated against the incomplete type. If the generated code still instantiates it, change `Sim` to a raw `FCPSimState*` with manual `delete` in the destructor (plugin edit; ask the coordinator). |

## F. Linker

| Message | Edit |
|---|---|
| `LNK2019 unresolved external symbol cp::...` | A sim_core `.cpp` has no `Wrap_<name>.cpp` in `Private/SimCore/`. Add one that `#include`s it (copy an existing wrapper). New: `render_mesh.cpp`, `markings.cpp` have wrappers being added by another agent. |
| `LNK2005 already defined` | A sim_core .cpp is wrapped twice, or a header defines a non-inline function. |
| `LNK1104 cannot open file ...` | Antivirus/Live editor lock; close the editor (hot reload is off) and rebuild. |
| `LNK2001 ... clipper2` | Clipper2 .cpp wrappers `Wrap_clipper2_*.cpp` missing for a file used. |

## G. Running the editor (Run-EditorPython.ps1)

| Symptom | Edit / action |
|---|---|
| No `LogPython` lines, `CP_PY_RESULT` missing | Python plugin not enabled (check `.uproject`), or `-ExecutePythonScript` path contains spaces and was mis-parsed (repo path is space-free by design). |
| `Missing modules ... rebuild?` then exit | Binaries are stale or absent: run `Build-Editor.ps1`; the unattended editor cannot rebuild. |
| `AttributeError: module 'unreal' has no attribute X` | The 5.8 name differs; the line is marked `# VERIFY` in the script. Find the class with `dir(unreal)` in the editor's Python console (Output Log -> Cmd -> Python) and edit that one line. |
| `new_level_from_template` missing or template not found | Script falls back to a basic level and warns. To get World Partition: in the editor, File > New Level > Open World, save as `/Game/Maps/RoadTest`, then re-run the setup script (it reuses the level). |
| `CPRoadNetworkActor not found` in setup/report | Module did not load: look at the "Module / plugin / load problems" section of the runner output. Usually the build failed or `Plugins/CityPainterSim` binaries are stale. |
| Screenshot run: no file appears | Not a rendering run (`-Render` forgotten), `-RenderOffscreen` unsupported (remove from `Run-EditorPython.ps1`, a window will show), or `take_high_res_screenshot` ignores absolute paths (look in `Saved\Screenshots\WindowsEditor\`). |
| Road is not visible in the editor viewport screenshot (only floor/sky) | **Needs a C++ change** (plugin sources were off limits): `ACPRoadNetworkActor` builds its smoke-test roads only in `BeginPlay`. Either run `screenshot_views.py` with arg `pie` (Play-In-Editor), or call `RunSmokeTest()` + `RebuildVisuals()` from `OnConstruction`/`PostActorCreated` when `GetWorld()->IsEditorWorld()` (and keep `bRunSmokeTest` as the gate). |
| Editor hangs in a modal dialog under `-unattended` | Read the last lines of the log; usual causes are "unsaved/untitled level" and "Perforce/source control". `-nosourcecontrol` is already passed. |
| Very long first start | Normal: thousands of shaders compile on first launch with the DX12 SM6 + Lumen + RT baseline (10-30+ min). Run `Run-EditorPython.ps1 -Script py\report_environment.py -TimeoutMinutes 90` once to warm the DDC. |
