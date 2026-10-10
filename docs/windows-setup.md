# Windows setup for Unreal 5.8 C++ (do this while the engine downloads)

VS Code can be your editor, but Unreal C++ needs Microsoft's **MSVC compiler**, which only comes from the Visual Studio Installer. Checked against Epic's UE 5.8 release notes and the Visual Studio setup page (10 Oct 2026).

## 1. Install Visual Studio 2026 Community (recommended) or 2022 17.14+

Download the Visual Studio Installer from visualstudio.microsoft.com. Community edition is free. Install these:

**Workloads**
- [ ] Game development with C++
- [ ] Desktop development with C++
- [ ] .NET desktop development

**Individual components** (Installation details panel, under Game development with C++)
- [ ] Unreal Engine installer (and the "IDE support for Unreal Engine" / Unreal integration if offered)
- [ ] Windows 11 SDK **10.0.26100** (minimum 10.0.22621)
- [ ] MSVC v14.4x or v14.50 build tools (x64/x86). UE 5.8 minimum is 14.38, recommended 14.50, and Epic's build farm uses 14.44.
- [ ] C++ profiling tools
- [ ] .NET 10 SDK (VS 2026) or .NET 8 (VS 2022) for UnrealBuildTool

Expect about 30–50 GB. After installing, open `Engine/Config/Windows/Windows_SDK.json` in the UE 5.8 install and check that the preferred MSVC/SDK versions match what you installed.

## 2. Other tools
- [ ] **Git for Windows**, plus **Git LFS** (`git lfs install` once). Unreal binary assets in `unreal/` are tracked via LFS.
- [ ] **CMake 3.20+** (or use the copy bundled with Visual Studio) to build and test `sim_core` on Windows: `cmake -S . -B build && cmake --build build --config Release && ctest --test-dir build -C Release`.
- [ ] Optional: in VS Code, the **C/C++** and **CMake Tools** extensions. Once the Unreal project exists, set Editor Preferences → Source Code → Visual Studio Code and run "Refresh Visual Studio Code Project".

## 3. Sanity check once Unreal finishes
1. Clone this repo and `git lfs pull`.
2. Build sim_core with CMake (step 2) and confirm the tests pass.
3. Create a blank **C++** project from the UE 5.8 launcher and confirm it compiles and opens. That proves the toolchain before we add our plugin.
