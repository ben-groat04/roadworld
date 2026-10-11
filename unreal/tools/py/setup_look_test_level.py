"""Create (or reopen) /Game/Maps/RoadTest and make sure it contains the look-test environment.

Idempotent: if the map exists it is loaded; every actor is "ensured" (reused when one of that class already
exists, e.g. from the Open World template) rather than duplicated. Re-running changes nothing.

Contents: Directional Light (movable, atmosphere sun 0), Sky Atmosphere, Sky Light (real-time capture),
Exponential Height Fog (volumetric), Volumetric Cloud, unbound Post Process Volume with fixed-ish exposure
limits, a 2 km floor plane at z=-10 cm to catch shadows, a Player Start, and one road actor at the origin.

Script argument (Run-EditorPython.ps1 -ScriptArgs ...): default places CPRoadMeshActor (the sim_core interchange with
real meshes + markings, built in the editor viewport by OnConstruction). the former `old` / `both` arguments
(CPRoadNetworkActor, removed) now fail with a clear message. After placing, the script assigns
/Game/Materials/Surfaces/<slot>/MI_<slot> (made by import_textures.py) to the actor's SurfaceMaterials when those
assets exist, and creates simple marking materials /Game/Materials/Markings/MI_marking_<colour> with a pixel depth
offset (the actor falls back to the same paths and to engine materials by itself, so this is belt and braces).

Run:  Run-EditorPython.ps1 -Script py\\setup_look_test_level.py        (nullrhi is fine)
Every API call marked VERIFY is one I could not check against UE 5.8; a fallback or a loud failure follows it.
"""
import os
import sys

sys.path.insert(0, os.environ.get("CP_PY_DIR", os.path.dirname(os.path.abspath(__file__))))
import cp_common as cp  # noqa: E402
import mat_defs  # noqa: E402
from look_env import *  # noqa: E402,F401,F403  (shared helpers and look constants)
import look_env  # noqa: E402

try:
    import unreal
except ImportError:
    unreal = None

LEVEL = "/Game/Maps/RoadTest"
OPEN_WORLD_TEMPLATE = "/Engine/Maps/Templates/OpenWorld"  # VERIFY path (the template behind File > New Level > Open World)
PLANE_MESH = "/Engine/BasicShapes/Plane.Plane"
# Exposure limits in EV100 (with r.DefaultFeature.AutoExposure.ExtendDefaultLuminanceRange=1, set in DefaultEngine.ini).
EXPOSURE_MIN_EV100 = 8.0   # tunable: bright sunny street ~ 13-15, deep shade ~ 9-10
EXPOSURE_MAX_EV100 = 15.0

CP_MAT_ROOT = "/Game/CityPainter/Materials"
SURFACE_ROOT = "/Game/Materials/Surfaces"
MARKING_ROOT = "/Game/Materials/Markings"
# mesh surface class (data/lane_types.json "surface" + the mesh builder's island_surface) -> texture slot
# (import_textures.py / PolyHaven manifest slots). Keep in step with SlotForSurface() in CPRoadMeshActor.cpp.
SURFACE_SLOTS = {"asphalt": "asphalt_new", "paving": "pavement", "kerb": "kerb_concrete", "grass": "grass", "island": "grass"}
# marking colour id -> sRGB (data/regions/uk/marking_rules.json "colours"); read from the file when found.
DEFAULT_MARKING_COLOURS = {"white": (0.88, 0.88, 0.86), "yellow": (0.93, 0.78, 0.15)}
MARKING_DEPTH_OFFSET_CM = -3.0  # VERIFY sign: PixelDepthOffset > 0 moves the pixel AWAY from the camera, so negative pulls it forward



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




def strip_template_terrain(eas):
    """The Open World template ships a rolling Landscape (+ 64 streaming proxies) and an SM_SkySphere. The landscape
    buries the generated roads (found at the first screenshots) and the sky sphere fights SkyAtmosphere: delete them."""
    n = 0
    for a in list(eas.get_all_level_actors()):
        cname = a.get_class().get_name()
        if cname.startswith("Landscape") or (cname == "StaticMeshActor" and a.get_actor_label() == "SM_SkySphere"):
            if eas.destroy_actor(a):
                n += 1
    if n:
        cp.log("removed %d template terrain/sky actors (Landscape*, SM_SkySphere)" % n)


# STAND-IN GROUND. The sim's test layout has no terrain yet (grading output is not rendered), so a flat plane sits just
# below the road formation. Road z is ~0 on the dual carriageway and the minor road's ends; the layout's verge lanes
# fall 5% over 2.5 m and a one-way carriageway falls ~2.5% over 7.3 m (about 18 cm), so -45 cm leaves a margin and
# nothing z-fights. The minor road's bridge/approach (z up to 7 m) floats over it with no embankment until grading is
# meshed. Replace by real terrain; then this actor, MI_Ground_Floor and FLOOR_* go away.
FLOOR_Z_CM = -45.0
FLOOR_SCALE = 40000.0  # plane is 100 cm: 40000x => 40 km square (horizon at ~20 km; fog has taken over long before)
FLOOR_CENTRE_CM = (10000.0, 0.0)  # layout centre (sim 0, 100 m north), UE X north / Y east


def ensure_floor(eas):
    mat = None
    for p in ("%s/MI_Ground_Floor" % CP_MAT_ROOT, "%s/grass/MI_grass" % SURFACE_ROOT):
        if asset_exists(p):
            mat = unreal.load_asset(p)
            break
    for a in find_actors(eas, unreal.StaticMeshActor):
        if a.get_actor_label() == "CP_Floor":
            cur = a.get_editor_property("static_mesh_component").get_material(0)
            same_mat = (cur is not None and mat is not None and cur.get_path_name() == mat.get_path_name())
            if abs(a.get_actor_location().z - FLOOR_Z_CM) < 1.0 and abs(a.get_actor_scale3d().x - FLOOR_SCALE) < 1.0 and same_mat:
                cp.log("reuse floor CP_Floor")
                return a
            # Moving an external (World Partition) actor does not mark it dirty, so the move was silently not saved:
            # replace it instead (spawning does dirty the package).
            old_z = a.get_actor_location().z
            eas.destroy_actor(a)
            cp.log("replacing floor CP_Floor (was at z=%g cm)" % old_z)
            break
    mesh = unreal.load_asset(PLANE_MESH)
    if mesh is None:
        raise cp.ScriptError("engine plane mesh %s not found" % PLANE_MESH)
    a = eas.spawn_actor_from_class(unreal.StaticMeshActor, unreal.Vector(FLOOR_CENTRE_CM[0], FLOOR_CENTRE_CM[1], FLOOR_Z_CM), unreal.Rotator(roll=0.0, pitch=0.0, yaw=0.0))
    a.set_actor_label("CP_Floor")
    a.get_editor_property("static_mesh_component").set_static_mesh(mesh)
    if mat is not None:
        a.get_editor_property("static_mesh_component").set_material(0, mat)
    a.set_actor_scale3d(unreal.Vector(FLOOR_SCALE, FLOOR_SCALE, 1.0))
    setp(a.get_editor_property("static_mesh_component"), "cast_shadow", False, "floor")
    cp.log("spawned floor CP_Floor (40 km)")
    return a


def marking_colours():
    """Colour id -> (r, g, b) sRGB 0..1 from the region pack's marking_rules.json, else the defaults."""
    import json
    here = os.path.dirname(os.path.abspath(__file__))
    cand = os.path.normpath(os.path.join(here, "..", "..", "..", "data", "regions", "uk", "marking_rules.json"))
    try:
        with open(cand, "r", encoding="utf-8") as f:
            cols = json.load(f).get("colours", {})
        out = {k: tuple(float(c) for c in v["rgb"]) for k, v in cols.items()}
        if out:
            return out
    except Exception as e:  # noqa
        cp.warn("could not read marking colours from %s (%s); using defaults" % (cand, e))
    return dict(DEFAULT_MARKING_COLOURS)


def srgb_to_linear(c):
    return c / 12.92 if c <= 0.04045 else ((c + 0.055) / 1.055) ** 2.4


def ensure_marking_materials(colours):
    """Create M_Marking (vector parameter Color, roughness, pixel depth offset) and MI_marking_<id> per colour.
    Idempotent. Returns {colour id: MaterialInstanceConstant or None}."""
    mel = getattr(unreal, "MaterialEditingLibrary", None)
    tools = unreal.AssetToolsHelpers.get_asset_tools()
    parent_path = MARKING_ROOT + "/M_Marking"
    parent = unreal.load_asset(parent_path) if asset_exists(parent_path) else None
    if parent is None and mel is not None:
        try:
            parent = tools.create_asset("M_Marking", MARKING_ROOT, unreal.Material, unreal.MaterialFactoryNew())
            colour = mel.create_material_expression(parent, unreal.MaterialExpressionVectorParameter, -600, 0)
            colour.set_editor_property("parameter_name", "Color")
            colour.set_editor_property("default_value", unreal.LinearColor(0.8, 0.8, 0.8, 1.0))
            rough = mel.create_material_expression(parent, unreal.MaterialExpressionConstant, -600, 200)
            rough.set_editor_property("r", 0.55)
            mel.connect_material_property(colour, "", unreal.MaterialProperty.MP_BASE_COLOR)
            mel.connect_material_property(rough, "", unreal.MaterialProperty.MP_ROUGHNESS)
            # MP_PixelDepthOffset is UMETA(Hidden) in Engine/Public/SceneTypes.h:177, so Python has no
            # unreal.MaterialProperty.MP_PIXEL_DEPTH_OFFSET: no pixel depth offset from script. Markings rely on the 3 mm mesh lift.
            cp.warn("M_Marking has no pixel depth offset (enum hidden from Python); relying on MarkingLiftMillimetres")
            mel.recompile_material(parent)
            unreal.EditorAssetLibrary.save_loaded_asset(parent)
            cp.log("created %s" % parent_path)
        except Exception as e:  # noqa
            cp.warn("could not create the marking master material (%s); the actor will tint an engine material instead" % e)
            parent = None
    out = {}
    for cid, rgb in colours.items():
        path = "%s/MI_marking_%s" % (MARKING_ROOT, cid)
        mi = unreal.load_asset(path) if asset_exists(path) else None
        if mi is None and parent is not None and mel is not None:
            try:
                mi = tools.create_asset("MI_marking_%s" % cid, MARKING_ROOT, unreal.MaterialInstanceConstant,
                                        unreal.MaterialInstanceConstantFactoryNew())
                mel.set_material_instance_parent(mi, parent)  # VERIFY
                lin = unreal.LinearColor(srgb_to_linear(rgb[0]), srgb_to_linear(rgb[1]), srgb_to_linear(rgb[2]), 1.0)
                mel.set_material_instance_vector_parameter_value(mi, "Color", lin)  # VERIFY
                mel.update_material_instance(mi)
                unreal.EditorAssetLibrary.save_loaded_asset(mi)
                cp.log("created %s" % path)
            except Exception as e:  # noqa
                cp.warn("could not create %s (%s)" % (path, e))
                mi = None
        out[cid] = mi
    return out


# surface class -> City Painter master-material instance under CP_MAT_ROOT (first that exists wins; else the Poly Haven
# slot instance, else the engine default inside the actor).
CP_CLASS_MATERIALS = {"asphalt": "MI_RoadAsphalt", "paving": "MI_Footway", "kerb": "MI_Kerb", "grass": "MI_Ground", "island": "MI_Ground"}


def assign_materials(actor, marking_mis):
    """Fill SurfaceMaterials / MarkingMaterials on a CPRoadMeshActor from assets that exist. Missing ones are left
    unset (the actor logs and falls back by itself)."""
    surf = {}
    for cls, slot in SURFACE_SLOTS.items():
        cands = []
        if cls in CP_CLASS_MATERIALS:
            cands.append("%s/%s" % (CP_MAT_ROOT, CP_CLASS_MATERIALS[cls]))
        cands.append("%s/%s/MI_%s" % (SURFACE_ROOT, slot, slot))
        path = next((p for p in cands if asset_exists(p)), None)
        if path:
            surf[unreal.Name(cls)] = unreal.load_asset(path)
            cp.log("surface '%s' -> %s" % (cls, path))
        else:
            cp.warn("no surface material for class '%s' (tried %s)" % (cls, ", ".join(cands)))
    if surf:
        setp(actor, "surface_materials", surf, "road")  # VERIFY: Python dict -> TMap<FName, UMaterialInterface*>
    marks = {unreal.Name(k): v for k, v in marking_mis.items() if v is not None}
    if marks:
        setp(actor, "marking_materials", marks, "road")


def main():
    if unreal is None:
        raise cp.ScriptError("not running inside the Unreal editor")
    cp.log("engine %s" % unreal.SystemLibrary.get_engine_version())

    args = cp.script_args()
    if "old" in args or "both" in args:
        raise cp.ScriptError("the 'old' / 'both' arguments were removed together with CPRoadNetworkActor; run without arguments")
    mesh_cls = cp.require_class("/Script/CityPainterLegacy.CPRoadMeshActor")

    les, created = open_or_create_level()
    cp.log("level %s; World Partition: %s" % ("CREATED" if created else "opened", world_partition_state()))
    eas = subsystem("EditorActorSubsystem")
    if eas is None:
        raise cp.ScriptError("EditorActorSubsystem missing")  # VERIFY name

    sun = ensure(eas, unreal.DirectionalLight, "CP_Sun", (0, 0, 5000), (0, -45, -30))
    configure_sun(sun)
    sky_atm = ensure(eas, unreal.SkyAtmosphere, "CP_SkyAtmosphere")
    try:  # ground the atmosphere beyond the 40 km floor in a grey-green so no dark-blue band shows at the horizon
        sky_atm.get_editor_property("sky_atmosphere_component").set_editor_property("ground_albedo", unreal.Color(r=90, g=105, b=80, a=255))
    except Exception as e:  # noqa
        cp.warn("sky atmosphere ground albedo not set: %s" % e)
    configure_sky_light(ensure(eas, unreal.SkyLight, "CP_SkyLight", (0, 0, 3000)))
    configure_fog(ensure(eas, unreal.ExponentialHeightFog, "CP_HeightFog", (0, 0, 0)))
    configure_cloud(ensure(eas, unreal.VolumetricCloud, "CP_Clouds"))
    configure_post_process(ensure(eas, unreal.PostProcessVolume, "CP_PostProcess"))
    mats = [a for a in cp.script_args() if a in mat_defs.BUILDERS or a == mat_defs.MARKING]
    for line in mat_defs.build(mats or None, marking_colours()):
        cp.log("MATERIAL " + line)
    strip_template_terrain(eas)
    ensure_floor(eas)
    ensure(eas, unreal.PlayerStart, "CP_PlayerStart", (-3000.0, 0.0, 200.0), (0, 0, 0))
    if mesh_cls is not None:
        road = ensure(eas, mesh_cls, "CP_RoadMesh", (0.0, 0.0, 0.0))
        road.set_actor_location(unreal.Vector(0.0, 0.0, 0.0), False, False)
        marks = ensure_marking_materials(marking_colours())
        for cid in list(marks):
            p = "%s/MI_Marking_%s" % (CP_MAT_ROOT, cid)   # City Painter master (built by mat_defs "marking") wins over the simple one
            if asset_exists(p):
                marks[cid] = unreal.load_asset(p)
        assign_materials(road, marks)
        # The actor builds in OnConstruction; make sure it has built before saving (RebuildAll is CallInEditor).
        try:
            road.rebuild_all()  # VERIFY: Python name of the UFUNCTION RebuildAll
        except Exception as e:  # noqa
            cp.warn("rebuild_all() failed or missing (%s); the actor builds itself on construction anyway" % e)

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
