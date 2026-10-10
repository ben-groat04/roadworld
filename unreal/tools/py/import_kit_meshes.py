"""Import the Blender-built kit meshes (tools/blender_kit) into the UE 5.8 project.

Run inside the editor's Python (see Run-EditorPython.ps1). Optional CP_SCRIPT_ARGS (space separated):
    --only Kerbs,Lighting   only these categories      --no-proxy   skip the *_proxy fallbacks
    --force                 (kept for symmetry; imports always replace existing assets, so re-runs are idempotent)

Reads  <project>/SourceArt/Meshes/Kit/kit_manifest.json (+ the FBX files it lists)
Writes /Game/Meshes/Kit/<Category>/SM_<Name>            Nanite enabled, no auto collision (simple box where the manifest asks)
       /Game/Meshes/Kit/<Category>/Proxy/SM_<Name>_proxy  non-Nanite fallback for far distance / non-Nanite use
Materials: slots keep the FBX slot names (kerb_concrete, galvanised_steel, ...). A slot is assigned
/Game/Materials/Surfaces/<slot>/MI_<slot> when that asset exists, otherwise left as the default material.

Nothing here has been run (Unreal was not installed when this was written); calls that may differ in 5.8 are marked
"# VERIFY". Axis note: meshes are authored +X along the run, +Y toward the carriageway, +Z up, in metres
(FBX cm). Unreal's importer mirrors Y when converting the right-handed FBX to its left-handed space; if the street
light bracket points the wrong way in the editor, flip the sign in the instancing code rather than re-exporting.
"""
import json
import os
import sys

try:
    import unreal
except ImportError:  # allows syntax checking outside the editor
    unreal = None

sys.path.insert(0, os.environ.get("CP_PY_DIR", os.path.dirname(os.path.abspath(__file__))))
try:
    import cp_common as cp
except ImportError:
    cp = None

DEST_ROOT = "/Game/Meshes/Kit"
SURFACE_ROOT = "/Game/Materials/Surfaces"


class KitImportError(Exception):
    pass


def _find_manifest():
    cands = []
    env = os.environ.get("CP_KIT_DIR")
    if env:
        cands.append(os.path.join(env, "kit_manifest.json"))
    try:
        cands.append(os.path.join(cp.project_dir(), "SourceArt", "Meshes", "Kit", "kit_manifest.json"))
    except Exception:
        pass
    here = os.path.dirname(os.path.abspath(__file__))
    cands.append(os.path.normpath(os.path.join(here, "..", "..", "CityPainter", "SourceArt", "Meshes", "Kit", "kit_manifest.json")))
    for c in cands:
        if os.path.isfile(c):
            return c
    raise KitImportError("kit_manifest.json not found; looked in: " + "; ".join(os.path.normpath(c) for c in cands)
                         + ". Build it with tools/blender_kit/README.md first.")


def _import_fbx(abs_fbx, dest_dir, asset_name):
    if not os.path.isfile(abs_fbx):
        raise KitImportError("FBX missing (is it checked out from Git LFS?): %s" % abs_fbx)
    opts = unreal.FbxImportUI()
    opts.set_editor_property("import_mesh", True)
    opts.set_editor_property("import_as_skeletal", False)
    opts.set_editor_property("import_animations", False)
    opts.set_editor_property("import_materials", False)
    opts.set_editor_property("import_textures", False)
    opts.set_editor_property("automated_import_should_detect_type", False)
    opts.set_editor_property("mesh_type_to_import", unreal.FBXImportType.FBXIT_STATIC_MESH)  # VERIFY enum name
    sm = opts.static_mesh_import_data
    sm.set_editor_property("combine_meshes", True)
    sm.set_editor_property("auto_generate_collision", False)
    sm.set_editor_property("generate_lightmap_u_vs", False)
    sm.set_editor_property("convert_scene", True)           # VERIFY: default true; keeps the exporter's axis metadata honoured
    sm.set_editor_property("force_front_x_axis", False)
    sm.set_editor_property("convert_scene_unit", False)     # file is already in cm
    # Normals come from the file (sharp edges set in Blender)
    try:
        sm.set_editor_property("normal_import_method", unreal.FBXNormalImportMethod.FBXNIM_IMPORT_NORMALS)  # VERIFY
    except Exception:
        pass

    task = unreal.AssetImportTask()
    task.set_editor_property("filename", abs_fbx)
    task.set_editor_property("destination_path", dest_dir)
    task.set_editor_property("destination_name", asset_name)
    task.set_editor_property("replace_existing", True)
    task.set_editor_property("replace_existing_settings", True)
    task.set_editor_property("automated", True)
    task.set_editor_property("save", False)
    task.set_editor_property("options", opts)
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
    path = "%s/%s" % (dest_dir, asset_name)
    mesh = unreal.EditorAssetLibrary.load_asset(path)
    if mesh is None:
        raise KitImportError("import failed: %s -> %s" % (abs_fbx, path))
    return mesh


def _set_nanite(mesh, enabled):
    # VERIFY: StaticMesh.nanite_settings (MeshNaniteSettings struct with 'enabled') is the UE5 property; the
    # import option FbxStaticMeshImportData.build_nanite does the same on import in 5.2+.
    try:
        ns = mesh.get_editor_property("nanite_settings")
        ns.set_editor_property("enabled", bool(enabled))
        mesh.set_editor_property("nanite_settings", ns)
    except Exception as e:
        cp.warn("could not set Nanite on %s: %s (set 'Enable Nanite Support' by hand)" % (mesh.get_name(), e))


def _set_collision(mesh, kind):
    lib = unreal.EditorStaticMeshLibrary
    try:
        lib.remove_collisions(mesh)
        if kind and kind != "none":
            # columns, barriers, bollards: one simple box; the engine-side instancing may replace this later
            lib.add_simple_collisions(mesh, unreal.ScriptingCollisionShapeType.BOX)  # VERIFY enum name
    except Exception as e:
        cp.warn("collision setup failed on %s: %s" % (mesh.get_name(), e))


def _assign_materials(mesh):
    try:
        mats = mesh.get_editor_property("static_materials")
    except Exception:
        return
    for i, sm in enumerate(mats):
        slot = str(sm.get_editor_property("material_slot_name"))
        mi = "%s/%s/MI_%s" % (SURFACE_ROOT, slot, slot)
        if unreal.EditorAssetLibrary.does_asset_exist(mi):
            mat = unreal.EditorAssetLibrary.load_asset(mi)
            if mat is not None:
                mesh.set_material(i, mat)  # VERIFY: StaticMesh.set_material(index, material) in 5.8
        else:
            cp.log("no %s yet: slot '%s' left unassigned on %s" % (mi, slot, mesh.get_name()))


def main():
    if unreal is None or cp is None:
        raise RuntimeError("must run inside the Unreal editor")
    args = cp.script_args()
    only = None
    if "--only" in args:
        only = set(args[args.index("--only") + 1].split(","))
    with_proxy = "--no-proxy" not in args
    mpath = _find_manifest()
    root = os.path.dirname(mpath)
    with open(mpath) as fh:
        man = json.load(fh)
    n_ok = 0
    try:
        for m in man["meshes"]:
            if only and m["category"] not in only:
                continue
            dest = "%s/%s" % (DEST_ROOT, m["category"])
            mesh = _import_fbx(os.path.join(root, m["fbx"]), dest, m["name"])
            _set_nanite(mesh, True)
            _set_collision(mesh, m.get("collision", "none"))
            _assign_materials(mesh)
            unreal.EditorAssetLibrary.save_loaded_asset(mesh)
            if with_proxy and m.get("proxy_fbx"):
                pm = _import_fbx(os.path.join(root, m["proxy_fbx"]), dest + "/Proxy", m["proxy"])
                _set_nanite(pm, False)
                _set_collision(pm, "none")
                _assign_materials(pm)
                unreal.EditorAssetLibrary.save_loaded_asset(pm)
            n_ok += 1
            cp.log("imported %s (%s tris)" % (m["name"], m.get("triangles")))
    except KitImportError as e:
        raise cp.ScriptError(str(e))
    cp.log("kit import complete: %d meshes" % n_ok)


if cp is not None:
    cp.run(main)
