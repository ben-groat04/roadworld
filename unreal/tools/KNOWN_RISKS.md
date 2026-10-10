# Predicted first-build failures and their mechanical fixes

## Before first compile: human steps (verified against D:\UE_5.8 5.8.3 source and this machine)

Nothing mandatory must be installed. Findings:
- Source of truth: `Engine\Config\Windows\Windows_SDK.json` and `Platform\Windows\MicrosoftPlatformSDK.cs`. VS 2026 is recognised (`WindowsCompiler.VisualStudio2026` in `UEBuildWindows.cs`, "14.50 = VS2026 18.0" in the json).
- Installed MSVC toolset is `14.51.36231` (VS 18 Community). Preferred ranges are 14.50.35717+ and 14.44.35207+; banned are 14.50.0-14.50.35722, 14.44.0-14.44.35210, 14.40-14.43, 14.39. 14.51 is in neither list, so `AddVisualCppToolChain` gives it no error (it is accepted, just not "preferred" rank; UBT may log a warning). It is the only toolset, so it is selected. Minimum is 14.38.33130.
- Windows SDK: json `MainVersion` 10.0.22621.0, range 10.0.19041.0-10.9.99999.0. Installed 10.0.22621.0 and 10.0.26100.0: fine.
- .NET: UBT uses the engine's bundled `Engine\Binaries\ThirdParty\DotNet\10.0`; the machine has SDK 10.0.401 too. Fine.
- The only risk is an untested 14.51 compiler bug/ICE. Fallback if the compile hits an internal compiler error or odd STL/SDK errors: in VS Installer > Modify > Individual components add "MSVC Build Tools for x64/x86 (v14.50-18.0)" (component id `Microsoft.VisualStudio.Component.VC.14.50.18.0.x86.x64`, the one UBT itself suggests) and then pin it with `%APPDATA%\Unreal Engine\UnrealBuildTool\BuildConfiguration.xml` (do not create unless needed):

```xml
<?xml version="1.0" encoding="utf-8" ?>
<Configuration xmlns="https://www.unrealengine.com/BuildConfiguration">
  <WindowsPlatform>
    <Compiler>VisualStudio2026</Compiler>
    <CompilerVersion>14.50.35723</CompilerVersion>
  </WindowsPlatform>
</Configuration>
```
(use the exact folder name under `...\VC\Tools\MSVC\` after installing; `CompilerVersion` is a `WindowsPlatform` target property, config key confirmed in `UEBuildWindows.cs` line ~389.)
- Plugins `ProceduralMeshComponent` (Runtime), `EditorScriptingUtilities` (Editor), `PythonScriptPlugin` (Experimental) all exist in 5.8; nothing to install.
- Optional MCP plugins (both `EnabledByDefault: false`, so they must be added to the .uproject to use): "Unreal MCP" = plugin name `ModelContextProtocol` at `Engine\Plugins\Experimental\ModelContextProtocol`; "All Toolsets" = plugin name `AllToolsets` at `Engine\Plugins\Experimental\Toolsets\AllToolsets` (aggregates ToolsetRegistry and about 25 toolsets, including `MCPClientToolset`). Adding them to the .uproject enlarges the first build/startup noticeably on a slow disk: enable after the first good compile.


Written before UE 5.8 was installed, from reading the sources only. Nothing here has been compiled. When
`Build-Editor.ps1` prints an error, find its message below and apply the listed edit. Check a name against the
engine before editing: `Engine\Source\Programs\UnrealBuildTool\Configuration\*.cs` (ModuleRules.cs, TargetRules.cs) is the truth.
Quick lookup: `findstr /s /n /c:"CppCompileWarningSettings" <UE>\Engine\Source\Programs\UnrealBuildTool\Configuration\*.cs`

## A. Build.cs / Target.cs (C# rules compile, runs before any C++)

**Verified against 5.8.3 source (Configuration\Rules\*.cs):** `CppCompileWarningSettings.{ShadowVariableWarningLevel,UnsafeTypeCastWarningLevel,UndefinedIdentifierWarningLevel}` exist (CppCompileWarnings.cs) and the old ModuleRules properties are `[Obsolete]` forwarders (ModuleRules.Obsolete.cs; `bEnableUndefinedIdentifierWarnings` obsolete since 5.5 and no longer forwards): Build.cs now uses the new form (already applied). `bEnableExceptions`, `bUseRTTI`, `CppStandard` (`CppStandardVersion?`, `Cpp20` exists in CppCompileEnvironment.cs), `PCHUsage`, `bUseUnity`, `WarningLevel` enum exist as written. `BuildSettingsVersion.Latest` = V7 and `EngineIncludeOrderVersion.Latest` = Unreal5_8 exist (TargetRules.cs); Latest may drift later but compiles now. `AdditionalCompilerArguments` is NOT in ModuleRules.cs: the row in section B that uses it will not compile; use `#pragma warning` in the wrappers instead. Plugin rows below: the three plugin names are correct.

| Message | Edit |
|---|---|
| `CS0117 'ModuleRules' does not contain a definition for 'ShadowVariableWarningLevel'` (or `UnsafeTypeCastWarningLevel`, `bEnableUndefinedIdentifierWarnings`) at `CityPainterSim.Build.cs` ~22-24 | Property moved into the warning-settings object. Replace the three lines with `CppCompileWarningSettings.ShadowVariableWarningLevel = WarningLevel.Off;` `CppCompileWarningSettings.UnsafeTypeCastWarningLevel = WarningLevel.Off;` `CppCompileWarningSettings.UndefinedIdentifierWarningLevel = WarningLevel.Off;` (5.6+ names as I remember them; confirm with the findstr above. If `UndefinedIdentifierWarningLevel` does not exist look for the property whose doc comment mentions C4668 / `-Wundef`). |
| `CS0618 ... is obsolete` / warning on the same three lines, but the build continues | Same replacement; warning only. |
| `CS0117 'BuildSettingsVersion' does not contain a definition for 'Latest'` in `CityPainter.Target.cs` / `CityPainterEditor.Target.cs` | Use the newest enum member listed in TargetRules.cs (`BuildSettingsVersion.V5` or `V6`); same for `EngineIncludeOrderVersion.Latest` -> the newest `Unreal5_x` member (`Unreal5_8` if present, else the highest). Edit both files. |
| `Expecting to find a type to be declared in a target rules named 'CityPainterEditorTarget'` | Follow-on of a Target.cs compile error above. Fix that first. |
| `CS0246 The type or namespace name 'WarningLevel' could not be found` | `using UnrealBuildTool;` already present; the enum may be nested: use `ModuleRules.WarningLevel.Off`. |
| `Plugin 'EditorScriptingUtilities' ... not found` or editor prompt about it | Delete that line from `CityPainter.uproject` (the Python scripts only use it as a fallback). Same for `PythonScriptPlugin` if the name is wrong (then use Edit > Plugins > search "Python" to read the real name). |
| `Could not find engine for project` / `EngineAssociation "5.8"` unresolved | Register the launcher install (optional human step; `UnrealVersionSelector /register` only matters for double-clicking the .uproject and for resolving `EngineAssociation`, the tools here pass the engine path explicitly): run `<UE>\Engine\Binaries\Win64\UnrealVersionSelector-Win64-Shipping.exe /register` (admin), or right-click the .uproject -> Switch Unreal Engine version. Without launcher registration `EngineAssociation "5.8"` stays unresolved by design; the command-line tools do not need it. |
| `No suitable Visual C++ installation / toolchain found`, or a version mismatch warning for MSVC 14.51 | Compare with `<UE>\Engine\Config\Windows\Windows_SDK.json`. Pin the compiler in `%APPDATA%\Unreal Engine\UnrealBuildTool\BuildConfiguration.xml`: `<?xml version="1.0"?><Configuration xmlns="https://www.unrealengine.com/BuildConfiguration"><WindowsPlatform><CompilerVersion>14.51.xxxxx</CompilerVersion></WindowsPlatform></Configuration>` (use the real folder name under `...\VC\Tools\MSVC\`). Open VS Installer and add the MSVC version the json prefers if 14.51 is rejected. |
| Same, but UBT still picks a compiler you do not want | Next to `<CompilerVersion>` add `<Compiler>VisualStudio2022</Compiler>` (or whichever value the `WindowsCompiler` enum in `<UE>\Engine\Source\Programs\UnrealBuildTool\Platform\Windows\WindowsPlatform.cs` lists; VS 2026 only appears in it if 5.8 knows it). Human step if UBT rejects 14.51 outright: open Visual Studio Installer > Modify > Individual components and add "MSVC v14.44 (or v14.50) - VS 2022/2026 C++ x64/x86 build tools" plus "Windows 11 SDK (10.0.26100)", then set `CompilerVersion` to the folder name now present under `...VCToolsMSVC`. |
| `Unable to find .NET` / dotnet errors in UBT | UBT uses the engine's bundled dotnet; check `<UE>\Engine\Binaries\ThirdParty\DotNet` exists (install incomplete?). |

## B. C++ exceptions / `<filesystem>` (most likely first C++ errors)

`CityPainterSim.Build.cs` sets `bEnableExceptions = false`. `region_pack.h` includes `<filesystem>`, which is pulled into most
`Wrap_*.cpp`. Update: every `Wrap_*.cpp` (and `CPRoadMeshBuild.cpp`) now carries `4530 4577` in its pragma list (checked by compiling each wrapper standalone with `/EHs-c- /GR- /W4`: clean). Still missing it at the time of writing, owned by other agents: `Wrap_grading.cpp`, `Wrap_layout_io.cpp`, `Wrap_street_lights.cpp` (they pull `<chrono>`: `C4530` at `chrono(2387)`). Add `4530 4577` to their pragma lists if the build reports it.

| Message | Edit |
|---|---|
| `C4577: 'noexcept' used with no exception handling mode specified` (in `filesystem`, `xlocale`, ...) | Preferred, one-line, covers every file: in `CityPainterSim.Build.cs` set `bEnableExceptions = true;` (and update the comment above it). sim_core compiles fine with exceptions on. |
| `C4530: C++ exception handler used, but unwind semantics are not enabled` | Same fix (`bEnableExceptions = true`). |
| Either of the two above, but you want exceptions off | Add `AdditionalCompilerArguments = "/wd4577 /wd4530";` to Build.cs (property exists on ModuleRules in 5.x; if not, add `#pragma warning(disable: 4577 4530)` at the top of every `Wrap_*.cpp`, before any include). |
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

## D. UE macros colliding with sim_core / third-party names (only in TUs that include UE headers: `CPRoadMeshActor.cpp`, which includes no sim_core header)

| Message | Edit |
|---|---|
| `C2059`/`C2143` near an identifier named `check`, `verify`, `ensure`, `TEXT`, `PI`, `INDEX_NONE`, `min`, `max` | A UE macro is eating a sim_core or Clipper2 identifier. Include the sim_core header BEFORE the UE headers in that .cpp, or `#undef` the macro after the UE includes. Do not rename sim_core symbols. |
| `C2872 'X': ambiguous symbol` | A name exists in both `cp::` code and UE (e.g. `FVector`-like types). Qualify with `cp::`. |
| Duplicate-symbol / `C2084 function already has a body` between `Wrap_*.cpp` files | Unity is off already; two sim_core .cpp files define the same anonymous-namespace-free symbol, or a file is wrapped twice. Check `Private/SimCore/` for a duplicate wrapper. |

## E. UHT (UnrealHeaderTool)

| Message | Edit |
|---|---|
| `Expected a ... forward declaration` for `TObjectPtr<UProceduralMeshComponent>` | Include `ProceduralMeshComponent.h` in the cpp; header keeps the forward declaration `class UProceduralMeshComponent;`. |

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
| `CPRoadMeshActor not found` in setup/report | Module did not load: look at the "Module / plugin / load problems" section of the runner output. Usually the build failed or `Plugins/CityPainterSim` binaries are stale. |
| Screenshot run: no file appears | Not a rendering run (`-Render` forgotten), `-RenderOffscreen` unsupported (remove from `Run-EditorPython.ps1`, a window will show), or `take_high_res_screenshot` ignores absolute paths (look in `Saved\Screenshots\WindowsEditor\`). |
| Road is not visible in the editor viewport screenshot (only floor/sky) | `ACPRoadMeshActor` builds in `OnConstruction` in editor worlds (section H); if nothing shows press `Rebuild All`, or run `screenshot_views.py` with arg `pie` (Play-In-Editor). The old `ACPRoadNetworkActor` was removed. |
| Editor hangs in a modal dialog under `-unattended` | Read the last lines of the log; usual causes are "unsaved/untitled level" and "Perforce/source control". `-nosourcecontrol` is already passed. |
| Very long first start | Normal: thousands of shaders compile on first launch with the DX12 SM6 + Lumen + RT baseline (10-30+ min, longer on a slow disk). The default `-nullrhi` mode skips most shader compiles, so it does NOT warm the shader cache: the first `-Render` run (screenshots) pays for it, so give that step a long timeout (First-Run uses 120 min). `report_environment.py` under nullrhi still warms engine startup, plugin load and some DDC content. |

## H. `ACPRoadMeshActor` and `CPRoadMeshBuild` (road meshes + markings; written without an engine)

Files: `Public/CPRoadMeshActor.h`, `Private/CPRoadMeshActor.cpp` (Unreal side, unverified) and `Private/CPRoadMeshBuild.{h,cpp}` (UE-free; compiled and run standalone with MSVC against sim_core, numbers in the hand-off report). Ranked by how likely the first build is to hit them. Error texts are from memory of MSVC/UHT output.

| # | Likely error / symptom | Cause and exact fix |
|---|---|---|
| 1 | `C2664: cannot convert argument 5 from 'TArray<FVector2D>' to 'const TArray<FVector2f> &'` (or similar for arg 2/3/4) in `CPRoadMeshActor.cpp` `CreateSection()` | `UProceduralMeshComponent::CreateMeshSection(int32, const TArray<FVector>&, const TArray<int32>&, const TArray<FVector>&, const TArray<FVector2D>&, const TArray<FColor>&, const TArray<FProcMeshTangent>&, bool)` is what the existing actor's `_LinearColor` variant mirrors (UE 5.0-5.5). If 5.8 changed UV0 to `FVector2f`/vertices to `FVector3f`, change the local arrays in `CreateSection()` to the types the header names (`ProceduralMeshComponent.h`, search `CreateMeshSection`). Nothing else uses them. |
| 2 | `C2668: ambiguous call to overloaded function` at `Comp->CreateMeshSection(` | 5.x added overloads with UV1..UV3. Pass the arrays as lvalues (already done) or call `CreateMeshSection_LinearColor(...)` with a `TArray<FLinearColor>` (the old actor does; replace `Colors` by `FLinearColor` and `FColor` by `FLinearColor::FromSRGBColor`). |
| 3 | `error LNK2019: unresolved external symbol "IPluginManager::Get"` or `C1083 Cannot open include file 'Interfaces/IPluginManager.h'` | Needs the `Projects` module: already added to `PublicDependencyModuleNames` in `CityPainterSim.Build.cs`. If the include path differs, `FindPlugin` lives in `Interfaces/IPluginManager.h` (5.x). Fallback if it fails to link: delete the plugin-dir branch in `ResolveDataDir()`; the `FPaths::ProjectDir()/../../data` branch still finds `data/`. |
| 4 | UHT: `Unrecognized type 'FCPMeshSim'`, or `C2027: use of undefined type 'FCPMeshSim'` in `CPRoadMeshActor.gen.cpp` / `~TUniquePtr` | Same pimpl pattern as the old actor (forward `struct FCPMeshSim;`, `FVTableHelper&` ctor and destructor defined out of line). If generated code still instantiates the destructor, replace `TUniquePtr<FCPMeshSim> Sim;` by `FCPMeshSim* Sim = nullptr;`, `new` it in the constructor and `delete` it in `~ACPRoadMeshActor()`; the `FVTableHelper` constructor leaves it null (every use is guarded by `if (!Sim)`). |
| 5 | UHT: `TMap<FName, TObjectPtr<UMaterialInterface>>` rejected | Supported in 5.x. If rejected change both maps (`SurfaceMaterials`, `MarkingMaterials`, plus the two `Resolved*` transient maps) to `TMap<FName, UMaterialInterface*>`; no cpp change needed. |
| 6 | `C2039: 'bUseAsyncCooking': is not a member of 'UProceduralMeshComponent'` | Delete the `Comp->bUseAsyncCooking = false;` line (collision is off, irrelevant). |
| 7 | `C2660`/`C2664` on `FProcMeshTangent(FVector, bool)` | Struct is `FProcMeshTangent { FVector TangentX; bool bFlipTangentY; }` with ctors `()`, `(FVector, bool)` and `(float,float,float)` (the old actor uses the latter). Use `FProcMeshTangent T; T.TangentX = ...; T.bFlipTangentY = ...;` if the ctor changed. |
| 8 | `C2039: 'FTCHARToUTF8' ...` / `Length` not a member | `FTCHARToUTF8` is `TStringConversion`; `.Get()` and `.Length()` exist in 5.x. Fallback in `ToStd()`: `return std::string(TCHAR_TO_UTF8(*S));`. |
| 9 | `C2065: 'FPlatformTime': undeclared identifier` | Add `#include "HAL/PlatformTime.h"` to `CPRoadMeshActor.cpp` (normally pulled by CoreMinimal). Same for `EWorldType` (`Engine/EngineBaseTypes.h`) and `LOAD_NoWarn` (`UObject/ObjectMacros.h`/`UObject/UObjectGlobals.h`). |
| 10 | `C2589`/`C2059` near `check`, `verify`, `TEXT` inside `CPRoadMeshActor.cpp` | The file includes `CPRoadMeshBuild.h` (std headers only) FIRST on purpose, and no sim_core header. If some std header still collides, include `<array> <cstdint> <map> <memory> <string> <vector>` in a block before it and `#undef` the macro afterwards. |
| 11 | `LNK2019 ... CPBuild::BuildRender` / `CPBuild::FWorldDeleter::operator()` | `Private/CPRoadMeshBuild.cpp` not compiled: UBT globs the module folder, so it should be picked up by a plain regenerate (`Build-Editor.ps1` does this). New sim_core file: `Wrap_test_layouts.cpp` for `sim_core/src/test_layouts.cpp` (added). |
| 12 | `C4577`/`C4530` in `CPRoadMeshBuild.cpp` | Its pragma list covers them (`<filesystem>` via `region_pack.h`); if the warnings come from `CPRoadMeshBuild.h` (std headers included before the pragma) move the `#pragma warning(push/disable)` above that include or set `bEnableExceptions = true` (section B). |
| 13 | Behaviour: `OnConstruction` does not build when the level opens, or builds twice | Editor worlds call `OnConstruction` after spawn, on property change, on drag and (usually) when a level loads. `EnsureBuilt()` only rebuilds when the sim hash changed (`Layout`, `RegionId`, `DataDirOverride`); render-only settings re-mesh without redrawing the layout; anything else just reapplies materials and debug lines. If nothing appears after opening the map, press the `Rebuild All` button in the Details panel (`CallInEditor`). PIE/game worlds skip `OnConstruction` and build in `BeginPlay`. The tile components are `RF_Transient`: they are never saved into the level. |
| 14 | Behaviour: roads upside down (invisible from above, visible from below), or lit wrong | Winding. The flip lives in two places in `CPRoadMeshBuild.cpp`: `ConvertBuffers()` (surfaces: indices `i+1` and `i+2` swapped) and the marking loop (`bCcw ? C : B`). To undo, remove the swap in both. Dark/flat lighting only: normals are `(ny, nx, nz)`; do not negate. The standalone check proves all asphalt triangles have geometric normal +Z in this convention (front face = clockwise seen from above, `(b-a)x(c-a)` formula), assuming UE's front face is clockwise. |
| 15 | Behaviour: normal-mapped surfaces light from the wrong side (bumps look inverted along the road) | `bFlipTangentY`: set to `true` for sim bitangent w = +1 (derivation in `CPRoadMeshBuild.h`). Flip the single line `Dst.TangentFlipY[i] = T.w > 0.f ? 1 : 0;` in `ConvertBuffers()`. Materials use world-aligned XY for flat surfaces so this only matters for kerb faces / `UseWorldAligned = 0` slots. |
| 16 | Behaviour: UV scale wrong (textures huge or microscopic on kerbs) | Mesh UVs are metres; `M_<slot>` divides by `TilingMetres`. If textures look 100x off check the material's UV path, not the mesh. |
| 17 | Behaviour: marking material not found / white default | Logged per colour (`LogCPRoadMesh`). The actor tries `/Game/Materials/Markings/MI_marking_<id>` (made by `setup_look_test_level.py`), then a dynamic instance of `/Engine/BasicShapes/BasicShapeMaterial` with vector parameter `Color` (name unverified; if the parameter does not exist markings render with the shape material's default grey). Assign `MarkingMaterials` by hand if needed. |
| 18 | Behaviour: markings z-fight at distance | They lie 3 mm above the surface (`MarkingLiftMillimetres`). `M_Marking` writes a pixel depth offset of -3 cm (`MARKING_DEPTH_OFFSET_CM` in `setup_look_test_level.py`; sign unverified: if markings vanish under the road use `+3.0`). Raise the lift to 5 mm or more if shimmer persists. |
| 19 | Python: `AttributeError` on `rebuild_all`, `surface_materials`, `marking_materials`, `MP_PIXEL_DEPTH_OFFSET`, or `TypeError` assigning a dict to a TMap | All marked `# VERIFY` in `setup_look_test_level.py`; each is wrapped so the script warns and continues (the actor resolves `MI_<slot>` and marking materials itself by path). Python names are the snake_case of the C++ UPROPERTY/UFUNCTION names. |
| 20 | `DrawDebugLine` lines vanish when the actor is dragged or other actors' debug lines disappear | `DrawDebug()` calls `FlushPersistentDebugLines(World)` which clears every persistent debug line in the world, then redraws ours. Expected. |
| 21 | `Layout` enum shows ugly names / `UMETA(ToolTip=...)` rejected | Remove the `UMETA(...)` clauses in `CPRoadMeshActor.h`. |
| 22 | `FDirectoryPath` unknown | `#include "Engine/EngineTypes.h"` (present). In packaged builds `FPaths::ProjectDir()`-relative lookups of `data/` fail: TODO stage `data/` (see comment on `DataDirOverride`). |

Large-world note: all component locations and tile origins are `double` (`FVector` is double precision in UE 5); vertex arrays are `float` relative to the tile origin, so nothing precision-critical is converted to float except tile-local values below 100 m. Assigning `FVector(float,float,float)` to the double vectors compiles silently.

Standalone re-check of the UE-free part (needs a sim_core build; throwaway CMake project outside the repo that adds `Private/CPRoadMeshBuild.cpp` plus a main to a copy of the repo CMake with `/GR- /EHs-c-`; see `unreal/tools/README.md`).

New-wrapper rule: every new `Private/SimCore/Wrap_<name>.cpp` must copy the FULL pragma list from `Wrap_region_pack.cpp` (including `4530 4577`).

## This machine (D:\UE_5.8 on a USB disk, repo on C:)

- Engine on a slow USB spinning disk: a cold editor launch takes 5-15 minutes before shader compilation even starts. Later launches are faster once the OS cache is warm. Build and editor steps carry long timeouts for this reason.
- The derived-data cache lives on C: (default `C:\UEDDC`, via env `UE-LocalDataCachePath`, set by Build-Editor.ps1 and Run-EditorPython.ps1 when unset; override with `-LocalDdcPath`). The DDC is safe to delete: it only costs a rebuild of cooked data and shaders.
- About 34 GB free on C:. The DDC, `Intermediate\`, `Binaries\` and the logs all grow; check `Get-PSDrive C` now and then, and delete `C:\UEDDC` first if space runs short.
- 16 GB RAM: builds use `-MaxParallelActions=4` and the editor config limits background shader compile threads (`NumUnusedShaderCompilingThreads=4`). Raise them only if memory stays free.
- Two GPUs (RTX 4060 + integrated Radeon 780M): if the editor picks the wrong one, pass `-GraphicsAdapter <index>` (try 0 then 1; the chosen adapter is in the log's `LogD3D12RHI` lines).
- Project files (`.sln`) are not generated by default; use `Build-Editor.ps1 -GenerateProjectFiles` only if you want to open the project in Visual Studio.

## I. API spot-check against 5.8.3 headers (ProceduralMeshComponent.h, IPluginManager.h, StringConv.h)

Verified matching our calls: `CreateMeshSection(int32, TArray<FVector>, TArray<int32>, TArray<FVector>, TArray<FVector2D>, TArray<FColor>, TArray<FProcMeshTangent>, bool)` (8-arg inline overload exists, still double `FVector`/`FVector2D`, so rows H1/H2 should not trigger); `FProcMeshTangent(FVector,bool)`, `(float,float,float)` ctors; `bUseAsyncCooking` and `bUseComplexAsSimpleCollision` are public members (H6 will not trigger); `IPluginManager::FindPlugin(FStringView)` exists; `FTCHARToUTF8::Get()/Length()` exist. Not checked: DefaultEngine.ini cvars.
