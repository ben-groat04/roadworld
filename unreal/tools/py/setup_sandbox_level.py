"""Create (or update) /Game/CityPainter/Maps/L_Sandbox: the map for the City Painter world.

The map holds ONLY the environment: sky atmosphere, sun, sky light, height fog, clouds and an unbound post process volume,
with the look values shared with the legacy level (look_env.py: the latest sky-fill and fog-cutoff fixes). No terrain, no
roads, no floor: the world is spawned at run time by the City Painter subsystems (UCPWorldSubsystem draws nothing itself,
UCPRenderSubsystem builds the ground layer), in the editor viewport and in play-in-editor. The map's world settings name
ACPGameMode so pressing Play gives the orbit camera.

Idempotent: re-running reuses existing actors and re-applies the constants.
Run:  Run-EditorPython.ps1 -Script py\\setup_sandbox_level.py        (nullrhi is fine)
"""
import os
import sys

sys.path.insert(0, os.environ.get("CP_PY_DIR", os.path.dirname(os.path.abspath(__file__))))
import cp_common as cp  # noqa: E402
from look_env import (  # noqa: E402
    asset_exists, subsystem, ensure, setp, find_actors,
    configure_sun, configure_sky_light, configure_fog, configure_cloud, configure_post_process,
)

try:
    import unreal
except ImportError:
    unreal = None

LEVEL = "/Game/CityPainter/Maps/L_Sandbox"
GAME_MODE_CLASS = "/Script/CityPainter.CPGameMode"
# Actor classes the map may contain besides the environment; anything else found in a fresh level is removed.
KEEP_CLASS_PREFIXES = ("DirectionalLight", "SkyAtmosphere", "SkyLight", "ExponentialHeightFog", "VolumetricCloud", "PostProcessVolume",
                       "WorldSettings", "Brush", "LevelBounds", "AbstractNavData", "WorldDataLayers", "WorldPartitionMiniMap")


def open_or_create_level():
    les = subsystem("LevelEditorSubsystem")
    if les is None:
        raise cp.ScriptError("LevelEditorSubsystem missing: is the editor scripting plugin loaded?")
    if asset_exists(LEVEL):
        cp.log("level exists, loading %s" % LEVEL)
        if not les.load_level(LEVEL):
            raise cp.ScriptError("load_level(%s) returned False" % LEVEL)
        return les, False
    # A basic (non World Partition) level: the world is small and spawned by code, so external actors would only add noise.
    if not les.new_level(LEVEL):
        raise cp.ScriptError("could not create level %s" % LEVEL)
    return les, True


def strip_non_environment(eas):
    """A new level can carry template actors (floor, player start...). The sandbox must contain only the environment."""
    n = 0
    for a in list(eas.get_all_level_actors()):
        cname = a.get_class().get_name()
        if not cname.startswith(KEEP_CLASS_PREFIXES):
            if eas.destroy_actor(a):
                n += 1
                cp.log("removed non-environment actor %s (%s)" % (a.get_actor_label(), cname))
    return n


def set_game_mode():
    cls = cp.require_class(GAME_MODE_CLASS)
    world = subsystem("UnrealEditorSubsystem").get_editor_world()
    ws = world.get_world_settings()
    ws.modify()
    if not setp(ws, "default_game_mode", cls, "world settings"):
        raise cp.ScriptError("could not set the default game mode on the map")
    got = ws.get_editor_property("default_game_mode")
    cp.log("WORLD SETTINGS default game mode = %s" % (got.get_name() if got else "None"))


def main():
    if unreal is None:
        raise cp.ScriptError("not running inside the Unreal editor")
    cp.log("engine %s" % unreal.SystemLibrary.get_engine_version())
    les, created = open_or_create_level()
    eas = subsystem("EditorActorSubsystem")
    if eas is None:
        raise cp.ScriptError("EditorActorSubsystem missing")
    cp.log("level %s" % ("CREATED" if created else "opened"))

    configure_sun(ensure(eas, unreal.DirectionalLight, "CP_Sun", (0, 0, 5000), (0, -45, -30)))
    sky_atm = ensure(eas, unreal.SkyAtmosphere, "CP_SkyAtmosphere")
    try:  # same grey-green ground albedo as the look-test level: no dark-blue band at the horizon
        sky_atm.get_editor_property("sky_atmosphere_component").set_editor_property("ground_albedo", unreal.Color(r=90, g=105, b=80, a=255))
    except Exception as e:  # noqa
        cp.warn("sky atmosphere ground albedo not set: %s" % e)
    configure_sky_light(ensure(eas, unreal.SkyLight, "CP_SkyLight", (0, 0, 3000)))
    configure_fog(ensure(eas, unreal.ExponentialHeightFog, "CP_HeightFog", (0, 0, 0)))
    configure_cloud(ensure(eas, unreal.VolumetricCloud, "CP_Clouds"))
    configure_post_process(ensure(eas, unreal.PostProcessVolume, "CP_PostProcess"))
    strip_non_environment(eas)
    set_game_mode()

    if not les.save_current_level():
        cp.warn("save_current_level returned False")
    try:
        unreal.EditorLoadingAndSavingUtils.save_dirty_packages(True, True)
    except Exception as e:  # noqa
        cp.warn("save_dirty_packages failed: %s" % e)
    if not asset_exists(LEVEL):
        raise cp.ScriptError("%s does not exist after save" % LEVEL)
    names = sorted(a.get_actor_label() for a in eas.get_all_level_actors())
    cp.log("saved %s with %d actors: %s" % (LEVEL, len(names), ", ".join(names)))


cp.run(main)
