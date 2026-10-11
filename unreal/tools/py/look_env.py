"""Shared level-environment helpers and look constants (sun, sky light, fog, clouds, post process) for the editor
scripts that build levels: setup_look_test_level.py (legacy RoadTest) and setup_sandbox_level.py (L_Sandbox). One place
for the values both levels use. Importing this module has no side effects.
"""
import os
import sys

sys.path.insert(0, os.environ.get("CP_PY_DIR", os.path.dirname(os.path.abspath(__file__))))
import cp_common as cp  # noqa: E402

try:
    import unreal
except ImportError:
    unreal = None

CLOUD_MATERIAL = "/Engine/EngineSky/VolumetricClouds/m_SimpleVolumetricCloud_Inst"  # VERIFY path


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


def cls_name(cls):
    """Name of either a Python wrapper type (unreal.SkyLight) or a UClass object (from load_class)."""
    try:
        return cls.__name__
    except AttributeError:
        return cls.get_name()


def find_actors(eas, cls):
    """Level actors that are instances of cls (a Python wrapper type or a UClass). EditorActorSubsystem has no
    *_of_class query, and isinstance() needs a Python type, so UClass objects go through is_a()."""
    if isinstance(cls, type):
        return [a for a in eas.get_all_level_actors() if isinstance(a, cls)]
    return [a for a in eas.get_all_level_actors() if a.get_class().is_child_of(cls)] if hasattr(unreal.Class, "is_child_of") else [
        a for a in eas.get_all_level_actors() if a.get_class() == cls]


def ensure(eas, cls, label, location=(0.0, 0.0, 0.0), rotation=(0.0, 0.0, 0.0)):
    """Return an existing actor of cls (first), or spawn one. rotation = (roll, pitch, yaw)."""
    found = find_actors(eas, cls)
    if found:
        a = found[0]
        if len(found) > 1:
            cp.warn("%d actors of %s exist; using the first (%s)" % (len(found), cls_name(cls), a.get_actor_label()))
        cp.log("reuse %s '%s'" % (cls_name(cls), a.get_actor_label()))
        return a
    loc = unreal.Vector(location[0], location[1], location[2])
    rot = unreal.Rotator(roll=rotation[0], pitch=rotation[1], yaw=rotation[2])
    a = eas.spawn_actor_from_class(cls, loc, rot)
    if a is None:
        raise cp.ScriptError("spawn_actor_from_class(%s) returned None" % cls_name(cls))
    a.set_actor_label(label)
    cp.log("spawned %s '%s'" % (cls_name(cls), label))
    return a


def setp(obj, name, value, what=""):
    """set_editor_property with a loud-but-nonfatal warning (property names marked VERIFY below may differ in 5.8)."""
    try:
        obj.set_editor_property(name, value)
        return True
    except Exception as e:  # noqa
        cp.warn("could not set %s%s: %s" % (what + "." if what else "", name, e))
        return False


SUN_ELEVATION_DEG = 45.0   # tunable. Light travels toward yaw SUN_TRAVEL_YAW (UE yaw 0 = +X = sim north, 90 = east): 40 = toward the
SUN_TRAVEL_YAW = 40.0      # north-east, i.e. the sun stands south-west (review: GSV/aerial shadows), so shadows fall across both axes of the layout
SUN_LUX = 100000.0
SUN_SOURCE_ANGLE = 1.2     # degrees: soft shadow edges (real sun 0.53; a little more reads like thin high cloud)
SKY_INTENSITY = 7.0  # skylight with real-time capture. Fourth run (docs/progress/2026-10-11-unreal-fourth/LUMEN_FINDINGS.md): the black
# shadows are correct DIRECT sun shadows (ShowFlag.DynamicShadows 0 removes them) lit only by sky light and then crushed by the tone
# curve toe (pre-tonemap shade is ~36% of sunlit). The sky light multiplier does not change the visible sky, only the fill:
# 3.0 -> 7.0 lifts shade about 3x; the exposure bias below pulls the sunlit road back.
EXPOSURE_BIAS_EV = -0.35   # post-process auto_exposure_bias (negative = darker); compensates the stronger fill
FOG_DENSITY = 0.006
FOG_HEIGHT_FALLOFF = 0.2
FOG_START_DISTANCE_CM = 3000.0
FOG_CUTOFF_CM = 3000000.0  # 30 km. The dark navy horizon band was the height fog applied to near-horizon SKY pixels; the cutoff exempts them (the floor ends at 20 km)
FOG_MAX_OPACITY = 0.92
FOG_INSCATTER = (0.50, 0.58, 0.68)  # linear; pale grey-blue haze (the atmosphere contribution is added on top)


def configure_sun(sun):
    comp = sun.get_editor_property("light_component")
    comp.set_mobility(unreal.ComponentMobility.MOVABLE)
    setp(comp, "atmosphere_sun_light", True, "sun")
    setp(comp, "atmosphere_sun_light_index", 0, "sun")
    setp(comp, "intensity", SUN_LUX, "sun")        # lux: real daylight; 10 (the engine default) is for non-extended exposure and rendered black here
    setp(comp, "cast_shadows", True, "sun")
    setp(comp, "use_temperature", True, "sun")
    setp(comp, "temperature", 6200.0, "sun")
    setp(comp, "cast_cloud_shadows", True, "sun")  # VERIFY name
    setp(comp, "light_source_angle", SUN_SOURCE_ANGLE, "sun")
    sun.set_actor_rotation(unreal.Rotator(roll=0.0, pitch=-SUN_ELEVATION_DEG, yaw=SUN_TRAVEL_YAW), False)


def configure_sky_light(sky):
    comp = sky.get_editor_property("light_component")
    comp.set_mobility(unreal.ComponentMobility.MOVABLE)
    setp(comp, "real_time_capture", True, "skylight")
    src = getattr(unreal, "SkyLightSourceType", None)
    if src is not None:
        setp(comp, "source_type", src.SLS_CAPTURED_SCENE, "skylight")
    setp(comp, "cast_shadows", False, "skylight")  # experiment 1: runtime road meshes have no distance field, so skylight shadowing left the median black
    setp(comp, "intensity", SKY_INTENSITY, "skylight")
    setp(comp, "lower_hemisphere_is_black", False, "skylight")  # the ground bounces light up; black here makes shadows too dark


def configure_fog(fog):
    comp = fog.get_editor_property("component")
    # Volumetric fog off: froxel cost for no visible gain in a daytime look test; height fog alone gives the haze.
    setp(comp, "enable_volumetric_fog", False, "fog")  # verified: bEnableVolumetricFog, ExponentialHeightFogComponent.h:136
    setp(comp, "fog_density", FOG_DENSITY, "fog")
    setp(comp, "fog_height_falloff", FOG_HEIGHT_FALLOFF, "fog")
    setp(comp, "start_distance", FOG_START_DISTANCE_CM, "fog")
    setp(comp, "fog_cutoff_distance", FOG_CUTOFF_CM, "fog")
    setp(comp, "fog_max_opacity", FOG_MAX_OPACITY, "fog")
    setp(comp, "fog_inscattering_luminance", unreal.LinearColor(FOG_INSCATTER[0], FOG_INSCATTER[1], FOG_INSCATTER[2], 1.0), "fog")
    setp(comp, "sky_atmosphere_ambient_contribution_color_scale", unreal.LinearColor(1.0, 1.0, 1.0, 1.0), "fog")
    cp.log("FOG readback: density=%s falloff=%s start=%s maxop=%s inscatter=%s volumetric=%s actors=%d" % (comp.get_editor_property("fog_density"), comp.get_editor_property("fog_height_falloff"), comp.get_editor_property("start_distance"), comp.get_editor_property("fog_max_opacity"), comp.get_editor_property("fog_inscattering_luminance"), comp.get_editor_property("enable_volumetric_fog"), len(unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors())))


def configure_cloud(cloud):
    comp = cloud.get_editor_property("volumetric_cloud_component")  # verified: AVolumetricCloud::VolumetricCloudComponent, Components/VolumetricCloudComponent.h:264
    mat = unreal.load_asset(CLOUD_MATERIAL)
    if mat is None:
        cp.warn("cloud material %s not found; clouds will be invisible until a material is assigned" % CLOUD_MATERIAL)
    else:
        setp(comp, "material", mat, "cloud")


FIXED_EV100 = 14.0   # tunable: sunny day 13-14.5 (overcast 11-12). min == max pins auto exposure to this EV100.


def configure_post_process(ppv):
    setp(ppv, "unbound", True, "ppv")
    settings = ppv.get_editor_property("settings")
    ok = True
    ok &= setp(settings, "override_auto_exposure_min_brightness", True, "ppv.settings")
    ok &= setp(settings, "override_auto_exposure_max_brightness", True, "ppv.settings")
    ok &= setp(settings, "auto_exposure_min_brightness", FIXED_EV100, "ppv.settings")
    ok &= setp(settings, "auto_exposure_max_brightness", FIXED_EV100, "ppv.settings")
    setp(settings, "override_auto_exposure_method", True, "ppv.settings")
    setp(settings, "auto_exposure_method", unreal.AutoExposureMethod.AEM_BASIC, "ppv.settings")
    setp(settings, "override_auto_exposure_bias", True, "ppv.settings")
    setp(settings, "auto_exposure_bias", EXPOSURE_BIAS_EV, "ppv.settings")
    # Mild post: no heavy bloom / vignette / fringing, a trace of film grain, no motion blur in stills.
    for flag, prop, value in (("override_bloom_intensity", "bloom_intensity", 0.2),
                              ("override_vignette_intensity", "vignette_intensity", 0.15),
                              ("override_scene_fringe_intensity", "scene_fringe_intensity", 0.0),
                              ("override_film_grain_intensity", "film_grain_intensity", 0.06),
                              ("override_motion_blur_amount", "motion_blur_amount", 0.0)):
        setp(settings, flag, True, "ppv.settings")
        setp(settings, prop, value, "ppv.settings")
    # Make sure Lumen GI / reflections are the active methods for this volume (shadowed areas were pure black: no skylight GI).
    for flag, prop, enum_name, member in (("override_dynamic_global_illumination_method", "dynamic_global_illumination_method", "DynamicGlobalIlluminationMethod", "LUMEN"),
                                          ("override_reflection_method", "reflection_method", "ReflectionMethod", "LUMEN")):
        en = getattr(unreal, enum_name, None)
        if en is not None and hasattr(en, member):
            setp(settings, flag, True, "ppv.settings")
            setp(settings, prop, getattr(en, member), "ppv.settings")
        else:
            cp.warn("enum unreal.%s.%s not available" % (enum_name, member))
    ppv.set_editor_property("settings", settings)  # structs are copies in Python: assign back
    if not ok:
        cp.warn("exposure limits may not have been applied (property names differ?); set them in the volume's Details panel")

