"""Log engine version, RHI/GPU, Lumen/VSM/Nanite state, plugins of interest, and whether CityPainterSim loaded.

Read-only and idempotent. Run:  Run-EditorPython.ps1 -Script py\\report_environment.py
(also works with -Render; with the default -nullrhi the RHI will report NullRHI, which is expected).
"""
import os
import re
import sys

sys.path.insert(0, os.environ.get("CP_PY_DIR", os.path.dirname(os.path.abspath(__file__))))
import cp_common as cp  # noqa: E402

try:
    import unreal
except ImportError:
    unreal = None

# console variable -> meaning of the value
CVARS = [
    ("r.DynamicGlobalIlluminationMethod", "GI method (0 none, 1 Lumen, 2 SSGI, 3 plugin)"),
    ("r.ReflectionMethod", "reflection method (0 none, 1 Lumen, 2 SSR, 3 plugin)"),
    ("r.Lumen.HardwareRayTracing", "Lumen uses hardware RT when available"),
    ("r.Lumen.DiffuseIndirect.Allow", "Lumen diffuse allowed"),
    ("r.Shadow.Virtual.Enable", "Virtual Shadow Maps"),
    ("r.Nanite", "Nanite rendering"),
    ("r.GenerateMeshDistanceFields", "mesh distance fields generated (read at editor start)"),
    ("r.AntiAliasingMethod", "AA method (0 off, 1 FXAA, 2 TAA, 3 MSAA, 4 TSR)"),
    ("r.VirtualTextures", "virtual texture support"),
    ("r.RayTracing", "ray tracing support"),
    ("r.DefaultFeature.AutoExposure.Method", "auto exposure method (0 histogram, 1 basic)"),
    ("r.Substrate", "Substrate materials"),
    ("r.AllowStaticLighting", "static lighting allowed"),
]


def cvar(name):
    """Return the cvar value as a string, or '<unavailable>'. # VERIFY: get_console_variable_*_value exist on SystemLibrary."""
    for fn_name in ("get_console_variable_int_value", "get_console_variable_float_value"):
        fn = getattr(unreal.SystemLibrary, fn_name, None)
        if fn is None:
            continue
        try:
            return str(fn(name))
        except Exception:
            continue
    return "<unavailable>"


def scan_editor_log(patterns):
    """Fallback for RHI / GPU adapter: grep the editor log written by -abslog (path in CP_EDITOR_LOG)."""
    path = os.environ.get("CP_EDITOR_LOG", "")
    found = []
    if not path or not os.path.exists(path):
        return found
    try:
        with open(path, "r", encoding="utf-8", errors="replace") as f:
            for line in f:
                if any(re.search(p, line) for p in patterns):
                    found.append(line.strip())
    except Exception as e:  # noqa
        cp.warn("could not read editor log: %s" % e)
    return found[:12]


def main():
    if unreal is None:
        raise cp.ScriptError("not running inside the Unreal editor")

    cp.log("engine version: %s" % unreal.SystemLibrary.get_engine_version())
    cp.log("project dir: %s" % unreal.SystemLibrary.get_project_directory())

    # RHI and GPU: no stable Python API that I know of, so read the log. # VERIFY any of:
    #   unreal.RenderingLibrary / unreal.RHIBlueprintLibrary / GenericPlatformMisc.get_primary_gpu_brand
    for name in ("get_primary_gpu_brand",):
        for holder in (getattr(unreal, "GenericPlatformMisc", None), getattr(unreal, "PlatformMisc", None)):
            fn = getattr(holder, name, None) if holder else None
            if fn:
                try:
                    cp.log("GPU brand (%s): %s" % (name, fn()))
                except Exception:
                    pass
    for line in scan_editor_log([r"D3D12RHI", r"LogRHI: .*(Using|Adapter|RHI)", r"Adapter Name", r"NullRHI", r"Selected Device Profile", r"GPU: "]):
        cp.log("log> " + line)

    cp.log("--- rendering features (cvar values) ---")
    for name, meaning in CVARS:
        cp.log("%-42s = %-14s %s" % (name, cvar(name), meaning))
    cp.log("(Lumen active if GI=1 and reflections=1; VSM if r.Shadow.Virtual.Enable=1; Nanite if r.Nanite=1.)")

    cp.log("--- plugins / features of interest (loaded = its Python classes exist) ---")
    probes = [
        ("PythonScriptPlugin", "log"),  # we are running, so it is on
        ("EditorScriptingUtilities", "EditorLevelLibrary"),
        ("ProceduralMeshComponent", "ProceduralMeshComponent"),
        ("GeometryScripting", "GeometryScriptLibrary_MeshPrimitiveFunctions"),
        ("MovieRenderPipeline (Render Graph)", "MoviePipelineQueueSubsystem"),
        ("PCG", "PCGComponent"),
        ("Water", "WaterBodyActor"),
        ("DLSS (NVIDIA plugin)", "DLSSLibrary"),  # VERIFY class name
        ("Unreal MCP / toolsets", None),  # exact class unknown; see note below
    ]
    for label, attr in probes:
        if attr is None:
            cp.log("%-36s : (no known probe; check Edit > Plugins or the MCP port 8000)" % label)
            continue
        ok = hasattr(unreal, attr)
        cp.log("%-36s : %s" % (label, "LOADED" if ok else "not loaded"))

    cp.log("--- City Painter ---")
    cls = cp.load_class("/Script/CityPainterSim.CPRoadMeshActor")
    module_ok = cls is not None
    cp.log("CityPainterSim module loaded / CPRoadMeshActor class: %s" % ("YES" if module_ok else "NO"))
    cp.log("python attribute unreal.CPRoadMeshActor: %s" % ("present" if hasattr(unreal, "CPRoadMeshActor") else "absent"))
    if not module_ok:
        raise cp.ScriptError(
            "CPRoadMeshActor not found: CityPainterSim did not load. Check the editor log for LogPluginManager / "
            "LogModuleManager errors, and that Build-Editor.ps1 succeeded."
        )


cp.run(main)
