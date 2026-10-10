"""Create (or reopen) /Game/Maps/RoadTest and make sure it contains the look-test environment.

Idempotent: if the map exists it is loaded; every actor is "ensured" (reused when one of that class already
exists, e.g. from the Open World template) rather than duplicated. Re-running changes nothing.

Contents: Directional Light (movable, atmosphere sun 0), Sky Atmosphere, Sky Light (real-time capture),
Exponential Height Fog (volumetric), Volumetric Cloud, unbound Post Process Volume with fixed-ish exposure
limits, a 2 km floor plane at z=-10 cm to catch shadows, a Player Start, and one CPRoadNetworkActor at the origin.

NOTE: CPRoadNetworkActor builds its smoke-test roads in BeginPlay only, so in the editor viewport the actor is
empty until Play. See KNOWN_RISKS.md ("road not visible in the editor viewport").

Run:  Run-EditorPython.ps1 -Script py\\setup_look_test_level.py        (nullrhi is fine)
Every API call marked VERIFY is one I could not check against UE 5.8; a fallback or a loud failure follows it.
"""
import os
import sys

sys.path.insert(0, os.environ.get("CP_PY_DIR", os.path.dirname(os.path.abspath(__file__))))
import cp_common as cp  # noqa: E402

try:
    import unreal
except ImportError:
    unreal = None

LEVEL = "/Game/Maps/RoadTest"
OPEN_WORLD_TEMPLATE = "/Engine/Maps/Templates/OpenWorld"  # VERIFY path (the template behind File > New Level > Open World)
CLOUD_MATERIAL = "/Engine/EngineSky/VolumetricClouds/m_SimpleVolumetricCloud_Inst"  # VERIFY path
PLANE_MESH = "/Engine/BasicShapes/Plane.Plane"
# Exposure limits in EV100 (with r.DefaultFeature.AutoExposure.ExtendDefaultLuminanceRange=1, set in DefaultEngine.ini).
EXPOSURE_MIN_EV100 = 8.0   # tunable: bright sunny street ~ 13-15, deep shade ~ 9-10
EXPOSURE_MAX_EV100 = 15.0


def subsystem(cls_name):
    cls = getattr(unreal, cls_name, None)
    if cls is None:
        return None
    try:
        return unreal.get_editor_subsystem(cls)
    except Exception:
        return None


def asset_exists(path):
    eas = subsystem("EditorAssetSubsystem")
    if eas is not None:
        return eas.does_asset_exist(path)
    return unreal.EditorAssetLibrary.does_asset_exist(path)  # fallback (needs EditorScriptingUtilities)


def open_or_create_level():
    les = subsystem("LevelEditorSubsystem")
    if les is None:
        raise cp.ScriptError("LevelEditorSubsystem missing: is the editor scripting plugin loaded?")  # VERIFY name
    if asset_exists(LEVEL):
        cp.log("level exists, loading %s" % LEVEL)
        if not les.load_level(LEVEL):
            raise cp.ScriptError("load_level(%s) returned False" % LEVEL)
        return les, False
    created = False
    if asset_exists(OPEN_WORLD_TEMPLATE):
        cp.log("creating %s from template %s (World Partition)" % (LEVEL, OPEN_WORLD_TEMPLATE))
        try:
            created = bool(les.new_level_from_template(LEVEL, OPEN_WORLD_TEMPLATE))  # VERIFY: signature (asset_path, template_asset_path)
        except Exception as e:  # noqa
            cp.warn("new_level_from_template failed: %s" % e)
    else:
        cp.warn("template %s not found; cannot make a World Partition level from Python" % OPEN_WORLD_TEMPLATE)
    if not created:
        cp.warn("FALLING BACK to a basic (non-World-Partition) level. Convert later via the editor: World Settings > Enable World Partition.")
        created = bool(les.new_level(LEVEL))
    if not created:
        raise cp.ScriptError("could not create level %s" % LEVEL)
    return les, True


def world_partition_state():
    try:
        ues = subsystem("UnrealEditorSubsystem")
        world = ues.get_editor_world()
        fn = getattr(world, "is_partitioned_world", None)  # VERIFY
        if fn is not None:
            return "yes" if fn() else "no (basic level)"
        return "unknown (no Python query found); world class = %s" % world.get_class().get_name()
    except Exception as e:  # noqa
        return "unknown (%s)" % e


def find_actors(eas, cls):
    return list(eas.get_all_level_actors_of_class(cls)) if hasattr(eas, "get_all_level_actors_of_class") else [
        a for a in eas.get_all_level_actors() if isinstance(a, cls)
    ]


def ensure(eas, cls, label, location=(0.0, 0.0, 0.0), rotation=(0.0, 0.0, 0.0)):
    """Return an existing actor of cls (first), or spawn one. rotation = (roll, pitch, yaw)."""
    found = find_actors(eas, cls)
    if found:
        a = found[0]
        if len(found) > 1:
            cp.warn("%d actors of %s exist; using the first (%s)" % (len(found), cls.__name__, a.get_actor_label()))
        cp.log("reuse %s '%s'" % (cls.__name__, a.get_actor_label()))
        return a
    loc = unreal.Vector(location[0], location[1], location[2])
    rot = unreal.Rotator(roll=rotation[0], pitch=rotation[1], yaw=rotation[2])
    a = eas.spawn_actor_from_class(cls, loc, rot)
    if a is None:
        raise cp.ScriptError("spawn_actor_from_class(%s) returned None" % cls.__name__)
    a.set_actor_label(label)
    cp.log("spawned %s '%s'" % (cls.__name__, label))
    return a


def setp(obj, name, value, what=""):
    """set_editor_property with a loud-but-nonfatal warning (property names marked VERIFY below may differ in 5.8)."""
    try:
        obj.set_editor_property(name, value)
        return True
    except Exception as e:  # noqa
        cp.warn("could not set %s%s: %s" % (what + "." if what else "", name, e))
        return False


def configure_sun(sun):
    comp = sun.get_editor_property("light_component")
    comp.set_mobility(unreal.ComponentMobility.MOVABLE)
    setp(comp, "atmosphere_sun_light", True, "sun")
    setp(comp, "atmosphere_sun_light_index", 0, "sun")
    setp(comp, "intensity", 10.0, "sun")            # lux, physical-ish daylight with auto exposure
    setp(comp, "cast_shadows", True, "sun")
    setp(comp, "use_temperature", True, "sun")
    setp(comp, "temperature", 6500.0, "sun")
    setp(comp, "cast_cloud_shadows", True, "sun")  # VERIFY name
    sun.set_actor_rotation(unreal.Rotator(roll=0.0, pitch=-45.0, yaw=-30.0), False)


def configure_sky_light(sky):
    comp = sky.get_editor_property("light_component")
    comp.set_mobility(unreal.ComponentMobility.MOVABLE)
    setp(comp, "real_time_capture", True, "skylight")
    src = getattr(unreal, "SkyLightSourceType", None)
    if src is not None:
        setp(comp, "source_type", src.SLS_CAPTURED_SCENE, "skylight")
    setp(comp, "cast_shadows", True, "skylight")


def configure_fog(fog):
    comp = fog.get_editor_property("component")
    setp(comp, "volumetric_fog", True, "fog")
    setp(comp, "fog_density", 0.01, "fog")


def configure_cloud(cloud):
    comp = cloud.get_editor_property("cloud_component")  # VERIFY property name; the actor may expose 'component'
    if comp is None:
        comp = cloud.get_editor_property("component")
    mat = unreal.load_asset(CLOUD_MATERIAL)
    if mat is None:
        cp.warn("cloud material %s not found; clouds will be invisible until a material is assigned" % CLOUD_MATERIAL)
    else:
        setp(comp, "material", mat, "cloud")


def configure_post_process(ppv):
    setp(ppv, "unbound", True, "ppv")
    settings = ppv.get_editor_property("settings")
    ok = True
    ok &= setp(settings, "override_auto_exposure_min_brightness", True, "ppv.settings")
    ok &= setp(settings, "override_auto_exposure_max_brightness", True, "ppv.settings")
    ok &= setp(settings, "auto_exposure_min_brightness", EXPOSURE_MIN_EV100, "ppv.settings")
    ok &= setp(settings, "auto_exposure_max_brightness", EXPOSURE_MAX_EV100, "ppv.settings")
    setp(settings, "override_auto_exposure_method", True, "ppv.settings")
    setp(settings, "auto_exposure_method", unreal.AutoExposureMethod.AEM_HISTOGRAM, "ppv.settings")
    setp(settings, "override_auto_exposure_bias", True, "ppv.settings")
    setp(settings, "auto_exposure_bias", 0.0, "ppv.settings")
    ppv.set_editor_property("settings", settings)  # structs are copies in Python: assign back
    if not ok:
        cp.warn("exposure limits may not have been applied (property names differ?); set them in the volume's Details panel")


def ensure_floor(eas):
    for a in find_actors(eas, unreal.StaticMeshActor):
        if a.get_actor_label() == "CP_Floor":
            cp.log("reuse floor CP_Floor")
            return a
    mesh = unreal.load_asset(PLANE_MESH)
    if mesh is None:
        raise cp.ScriptError("engine plane mesh %s not found" % PLANE_MESH)
    a = eas.spawn_actor_from_class(unreal.StaticMeshActor, unreal.Vector(10000.0, 12500.0, -10.0), unreal.Rotator(roll=0.0, pitch=0.0, yaw=0.0))
    a.set_actor_label("CP_Floor")
    a.get_editor_property("static_mesh_component").set_static_mesh(mesh)
    a.set_actor_scale3d(unreal.Vector(2000.0, 2000.0, 1.0))  # plane is 100 cm: 2000x => 2 km square
    cp.log("spawned floor CP_Floor (2 km)")
    return a


def main():
    if unreal is None:
        raise cp.ScriptError("not running inside the Unreal editor")
    cp.log("engine %s" % unreal.SystemLibrary.get_engine_version())

    road_cls = cp.require_class("/Script/CityPainterSim.CPRoadNetworkActor")

    les, created = open_or_create_level()
    cp.log("level %s; World Partition: %s" % ("CREATED" if created else "opened", world_partition_state()))
    eas = subsystem("EditorActorSubsystem")
    if eas is None:
        raise cp.ScriptError("EditorActorSubsystem missing")  # VERIFY name

    sun = ensure(eas, unreal.DirectionalLight, "CP_Sun", (0, 0, 5000), (0, -45, -30))
    configure_sun(sun)
    ensure(eas, unreal.SkyAtmosphere, "CP_SkyAtmosphere")
    configure_sky_light(ensure(eas, unreal.SkyLight, "CP_SkyLight", (0, 0, 3000)))
    configure_fog(ensure(eas, unreal.ExponentialHeightFog, "CP_HeightFog", (0, 0, 0)))
    configure_cloud(ensure(eas, unreal.VolumetricCloud, "CP_Clouds"))
    configure_post_process(ensure(eas, unreal.PostProcessVolume, "CP_PostProcess"))
    ensure_floor(eas)
    ensure(eas, unreal.PlayerStart, "CP_PlayerStart", (-3000.0, 0.0, 200.0), (0, 0, 0))
    road = ensure(eas, road_cls, "CP_RoadNetwork", (0.0, 0.0, 0.0))
    road.set_actor_location(unreal.Vector(0.0, 0.0, 0.0), False, False)

    # Save. With World Partition the actors are external files; save_current_level + dirty packages covers both.
    if not les.save_current_level():
        cp.warn("save_current_level returned False")
    try:
        unreal.EditorLoadingAndSavingUtils.save_dirty_packages(True, True)
    except Exception as e:  # noqa
        cp.warn("save_dirty_packages failed: %s" % e)
    if not asset_exists(LEVEL):
        raise cp.ScriptError("%s does not exist after save" % LEVEL)
    cp.log("saved %s with %d actors" % (LEVEL, len(eas.get_all_level_actors())))


cp.run(main)
