# unreal/tools: command-line build and editor automation

**One command, engine installed to first pictures (re-runnable, resumes where it stopped):**
`powershell -NoProfile -ExecutionPolicy Bypass -File unreal\tools\First-Run.ps1`
(`-DryRun` previews, `-From <step>` / `-Only <step>` / `-Force` override; steps: find-engine, build, environment, textures, level, screenshots; state in `unreal\CityPainter\Saved\first-run-state.json`. On failure it prints the log path and the exact command to re-run that step.)

Only these still need a human:
1. Accept the prerequisites prompt if one is shown.
2. Wait out the first shader compile (10-30+ minutes).
3. Enable the Unreal MCP + All Toolsets plugins and tick Auto Start Server.


Windows PowerShell 5.1. Run from the repo root as `powershell -NoProfile -ExecutionPolicy Bypass -File unreal\tools\<script>`.

| Script | What it does |
|---|---|
| `Find-Unreal.ps1` | Locates UE 5.8 (-EngineRoot, `UE_ROOT`, `LauncherInstalled.dat`, registry, then `<drive>:\UE_5.8` scan) and verifies `Build.bat`, `UnrealEditor-Cmd.exe`, `RunUAT.bat`. Errors clearly on absent or half-installed engines. |
| `Build-Editor.ps1` | Builds `CityPainterEditor Win64 Development` via `Build.bat`, logs to `unreal\CityPainter\Saved\Logs\build-<time>.log`, prints a deduplicated error summary (compiler, linker, UHT, UBT, Build.cs/Target.cs C#). Exit 0 ok, 1 build failed, 2 setup problem. Flags: `-Stream` (echo the log live), `-MaxErrors`, `-SkipGenerate`, `-Clean`. |
| `First-Run.ps1` | Orchestrates all steps below with a resumable state file; see the top of this file. |
| `Run-EditorPython.ps1` | Runs `py\<script>.py` in an unattended editor and summarises the log (`LogPython` lines, tracebacks, module-load failures, crash markers, the script's `CP_PY_RESULT` marker). `-Render` for screenshot runs, default is `-nullrhi`. `-ScriptArgs a,b` reaches the script via `cp.script_args()`. |
| `BuildLogParser.ps1` | Parser used by Build-Editor (dot-source it). |
| `py\setup_look_test_level.py` | Creates/updates `/Game/Maps/RoadTest` (Open World template if available) with sky, sun, fog, clouds, post-process, floor, player start and one `CPRoadNetworkActor`. Idempotent. |
| `py\screenshot_views.py` | Street / 45 degree / aerial screenshots into `Saved\Screenshots\look\`. Needs `-Render`. |
| `py\report_environment.py` | Logs engine version, RHI/GPU (from the log), Lumen/VSM/Nanite cvars, plugin probes, and whether `CPRoadNetworkActor` loaded. |
| `KNOWN_RISKS.md` | Predicted first-build errors mapped to exact edits. Read it when a build fails. |

## Tests (no engine needed)
```
powershell -NoProfile -ExecutionPolicy Bypass -File unreal\tools\tests\Test-FindUnreal.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File unreal\tools\tests\Test-BuildLogParser.ps1
```
`tests\sample-build.log` is a synthetic log (MSVC, linker, UHT, UBT, C# rules errors); the formats are from memory of
real UE output and should be checked against the first real failing build.

## Writing Python scripts
Copy the pattern in `py\cp_common.py`: `cp.run(main)` prints `CP_PY_RESULT: OK` or `FAIL <reason>` and quits the editor.
Mark any API call not confirmed on 5.8 with `# VERIFY`. Raise `cp.ScriptError("clear message")` to fail loudly.
