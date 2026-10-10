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
`Wrap_*.cpp` and `CPRoadNetworkActor.cpp`. Update: every `Wrap_*.cpp` (and `CPRoadMeshBuild.cpp`) now carries `4530 4577` in its pragma list (checked by compiling each wrapper standalone with `/EHs-c- /GR- /W4`: clean). Still missing it at the time of writing, owned by other agents: `Wrap_grading.cpp`, `Wrap_layout_io.cpp`, `Wrap_street_lights.cpp` (they pull `<chrono>`: `C4530` at `chrono(2387)`). Add `4530 4577` to their pragma lists if the build reports it.

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
| Road is not visible in the editor viewport screenshot (only floor/sky) | FIXED in code for both actors: `ACPRoadNetworkActor` and `ACPRoadMeshActor` now build in `OnConstruction` in editor worlds (see section H for the new actor). Original note: `ACPRoadNetworkActor` built its smoke-test roads only in `BeginPlay`. Either run `screenshot_views.py` with arg `pie` (Play-In-Editor), or call `RunSmokeTest()` + `RebuildVisuals()` from `OnConstruction`/`PostActorCreated` when `GetWorld()->IsEditorWorld()` (and keep `bRunSmokeTest` as the gate). |
| Editor hangs in a modal dialog under `-unattended` | Read the last lines of the log; usual causes are "unsaved/untitled level" and "Perforce/source control". `-nosourcecontrol` is already passed. |
| Very long first start | Normal: thousands of shaders compile on first launch with the DX12 SM6 + Lumen + RT baseline (10-30+ min). Run `Run-EditorPython.ps1 -Script py\report_environment.py -TimeoutMinutes 90` once to warm the DDC. |

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
