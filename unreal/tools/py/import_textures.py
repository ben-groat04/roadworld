"""Import the Poly Haven starter texture library into the UE project and build one material per slot.

Run inside the UE 5.8 editor's Python (see Run-EditorPython.ps1), e.g.:
    CP_SCRIPT_ARGS="--force"   (optional: re-import even if the asset already exists)

Reads  <repo>/unreal/CityPainter/SourceArt/Textures/PolyHaven/manifest.json
Writes /Game/Materials/Surfaces/<slot>/T_<asset>_<map>   (textures)
       /Game/Materials/Surfaces/<slot>/M_<slot>          (master material, opaque)
       /Game/Materials/Surfaces/<slot>/MI_<slot>         (instance; TilingMetres defaults to manifest size_m)

Idempotent: re-running re-imports (replace_existing) and rebuilds the material from scratch.
Nothing here has been run yet (Unreal was not installed when this was written); calls that
may differ in 5.8 are marked "# VERIFY".
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

DEST_ROOT = "/Game/Materials/Surfaces"
MANIFEST_REL = os.path.join("CityPainter", "SourceArt", "Textures", "PolyHaven", "manifest.json")

# map type key (in manifest) -> (texture settings kind)
# "color" = sRGB colour; "normal" = DirectX normal map; "mask" = linear data
MAP_KIND = {"diff": "color", "nor_dx": "normal", "rough": "mask", "ao": "mask", "disp": "mask", "arm": "mask"}

# Slots whose surfaces are mostly vertical: world-aligned XY projection would stretch, so default to mesh UVs.
VERTICAL_SLOTS = {"wall_brick", "structural_concrete", "kerb_concrete", "rock"}


class ImportError_(Exception):
    pass


def _log(m):
    (cp.log if cp else print)(m)


def _warn(m):
    (cp.warn if cp else print)(m)


def find_manifest():
    """Locate manifest.json: CP_PROJECT_DIR env, else relative to this file (unreal/tools/py -> unreal/)."""
    cands = []
    pd = os.environ.get("CP_PROJECT_DIR")
    if pd:
        cands.append(os.path.join(pd, "..", MANIFEST_REL))   # project dir = unreal/CityPainter
        cands.append(os.path.join(pd, "SourceArt", "Textures", "PolyHaven", "manifest.json"))
    here = os.path.dirname(os.path.abspath(__file__))
    cands.append(os.path.join(here, "..", "..", MANIFEST_REL))
    for c in cands:
        c = os.path.normpath(c)
        if os.path.isfile(c):
            return c
    raise ImportError_("manifest.json not found; looked in: " + "; ".join(os.path.normpath(c) for c in cands))


def _enum(cls, name):
    try:
        return getattr(cls, name)
    except AttributeError:
        raise ImportError_("unreal enum %s.%s not found (API changed in this engine version?)" % (cls.__name__, name))


def import_one(slot, asset_id, map_key, abs_path, force):
    """Import one image file; returns the loaded Texture2D."""
    dest_dir = "%s/%s" % (DEST_ROOT, slot)
    name = "T_%s_%s" % (asset_id, map_key)
    asset_path = "%s/%s" % (dest_dir, name)
    if not os.path.isfile(abs_path):
        raise ImportError_("source image missing: %s" % abs_path)

    existing = unreal.EditorAssetLibrary.does_asset_exist(asset_path)
    if existing and not force:
        tex = unreal.EditorAssetLibrary.load_asset(asset_path)
    else:
        task = unreal.AssetImportTask()
        task.set_editor_property("filename", abs_path)
        task.set_editor_property("destination_path", dest_dir)
        task.set_editor_property("destination_name", name)
        task.set_editor_property("replace_existing", True)
        task.set_editor_property("automated", True)   # no dialogs
        task.set_editor_property("save", False)
        unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
        tex = unreal.EditorAssetLibrary.load_asset(asset_path)
    if tex is None:
        raise ImportError_("import failed for %s -> %s" % (abs_path, asset_path))

    kind = MAP_KIND.get(map_key, "mask")
    TCS = unreal.TextureCompressionSettings
    if kind == "color":
        tex.set_editor_property("compression_settings", _enum(TCS, "TC_DEFAULT"))
        tex.set_editor_property("srgb", True)
    elif kind == "normal":
        tex.set_editor_property("compression_settings", _enum(TCS, "TC_NORMALMAP"))
        tex.set_editor_property("srgb", False)
        # Poly Haven nor_dx is already DirectX (Y-down), which is what Unreal expects: do NOT flip green.
        tex.set_editor_property("flip_green_channel", False)
    else:
        tex.set_editor_property("compression_settings", _enum(TCS, "TC_MASKS"))
        tex.set_editor_property("srgb", False)
    # Virtual texture streaming is deliberately left at the project default.
    unreal.EditorAssetLibrary.save_loaded_asset(tex)
    return tex


def _expr(mel, mat, cls, x, y):
    e = mel.create_material_expression(mat, cls, x, y)
    if e is None:
        raise ImportError_("create_material_expression failed for %s" % cls)
    return e


def _tex_param(mel, mat, name, tex, sampler, x, y):
    e = _expr(mel, mat, unreal.MaterialExpressionTextureSampleParameter2D, x, y)
    e.set_editor_property("parameter_name", name)
    e.set_editor_property("texture", tex)
    e.set_editor_property("sampler_type", _enum(unreal.MaterialSamplerType, sampler))  # VERIFY enum prefix SAMPLERTYPE_*
    return e


def build_material(slot, tex, size_m):
    """Create/rebuild M_<slot> and MI_<slot>. tex = dict map_key -> Texture2D."""
    mel = unreal.MaterialEditingLibrary
    dest_dir = "%s/%s" % (DEST_ROOT, slot)
    mname = "M_%s" % slot
    mpath = "%s/%s" % (dest_dir, mname)
    for key in ("diff", "nor_dx", "rough", "ao"):
        if key not in tex:
            raise ImportError_("slot %s lacks required map '%s'" % (slot, key))

    if unreal.EditorAssetLibrary.does_asset_exist(mpath):
        unreal.EditorAssetLibrary.delete_asset(mpath)  # rebuild cleanly; MI is recreated below
    mi_path = "%s/MI_%s" % (dest_dir, slot)
    if unreal.EditorAssetLibrary.does_asset_exist(mi_path):
        unreal.EditorAssetLibrary.delete_asset(mi_path)

    tools = unreal.AssetToolsHelpers.get_asset_tools()
    mat = tools.create_asset(mname, dest_dir, unreal.Material, unreal.MaterialFactoryNew())
    if mat is None:
        raise ImportError_("could not create material " + mpath)
    mat.set_editor_property("blend_mode", unreal.BlendMode.BLEND_OPAQUE)
    # Texture-sampled maps can exceed the default 16 sampler limit only with more maps; we use 4. Shared wrap is fine.

    # --- UV computation -------------------------------------------------------------
    # uv_mesh  = TexCoord0 / TilingMetres           (assumes 1 UV unit = 1 m on the mesh; VERIFY against generated meshes)
    # uv_world = WorldPosition.xy / (TilingMetres * 100)   (Unreal units are cm)
    # UV = Lerp(uv_mesh, uv_world, UseWorldAligned)
    tiling = _expr(mel, mat, unreal.MaterialExpressionScalarParameter, -1400, 0)
    tiling.set_editor_property("parameter_name", "TilingMetres")
    tiling.set_editor_property("default_value", float(size_m))
    use_world = _expr(mel, mat, unreal.MaterialExpressionScalarParameter, -1400, 150)
    use_world.set_editor_property("parameter_name", "UseWorldAligned")
    use_world.set_editor_property("default_value", 0.0 if slot in VERTICAL_SLOTS else 1.0)

    hundred = _expr(mel, mat, unreal.MaterialExpressionConstant, -1400, 300)
    hundred.set_editor_property("r", 100.0)
    tiling_cm = _expr(mel, mat, unreal.MaterialExpressionMultiply, -1200, 250)
    mel.connect_material_expressions(tiling, "", tiling_cm, "A")
    mel.connect_material_expressions(hundred, "", tiling_cm, "B")

    texcoord = _expr(mel, mat, unreal.MaterialExpressionTextureCoordinate, -1400, -150)
    uv_mesh = _expr(mel, mat, unreal.MaterialExpressionDivide, -1000, -100)
    mel.connect_material_expressions(texcoord, "", uv_mesh, "A")
    mel.connect_material_expressions(tiling, "", uv_mesh, "B")

    wpos = _expr(mel, mat, unreal.MaterialExpressionWorldPosition, -1400, 450)  # VERIFY: absolute world position output (camera-relative issues with LWC)
    mask_xy = _expr(mel, mat, unreal.MaterialExpressionComponentMask, -1200, 450)
    mask_xy.set_editor_property("r", True)
    mask_xy.set_editor_property("g", True)
    mask_xy.set_editor_property("b", False)
    mask_xy.set_editor_property("a", False)
    mel.connect_material_expressions(wpos, "", mask_xy, "")
    uv_world = _expr(mel, mat, unreal.MaterialExpressionDivide, -1000, 400)
    mel.connect_material_expressions(mask_xy, "", uv_world, "A")
    mel.connect_material_expressions(tiling_cm, "", uv_world, "B")

    uv = _expr(mel, mat, unreal.MaterialExpressionLinearInterpolate, -800, 100)
    mel.connect_material_expressions(uv_mesh, "", uv, "A")
    mel.connect_material_expressions(uv_world, "", uv, "B")
    mel.connect_material_expressions(use_world, "", uv, "Alpha")

    # --- texture samples ----------------------------------------------------------------
    base = _tex_param(mel, mat, "BaseColor", tex["diff"], "SAMPLERTYPE_COLOR", -500, -250)
    nrm = _tex_param(mel, mat, "Normal", tex["nor_dx"], "SAMPLERTYPE_NORMAL", -500, 0)
    rgh = _tex_param(mel, mat, "Roughness", tex["rough"], "SAMPLERTYPE_MASKS", -500, 250)  # VERIFY: MASKS vs LINEAR_COLOR naming
    amo = _tex_param(mel, mat, "AO", tex["ao"], "SAMPLERTYPE_MASKS", -500, 500)
    for s in (base, nrm, rgh, amo):
        mel.connect_material_expressions(uv, "", s, "UVs")

    mel.connect_material_property(base, "RGB", unreal.MaterialProperty.MP_BASE_COLOR)
    mel.connect_material_property(nrm, "RGB", unreal.MaterialProperty.MP_NORMAL)
    mel.connect_material_property(rgh, "R", unreal.MaterialProperty.MP_ROUGHNESS)
    mel.connect_material_property(amo, "R", unreal.MaterialProperty.MP_AMBIENT_OCCLUSION)
    # Height (disp) and ARM are imported but intentionally not wired: use them later for height-blend / Nanite displacement.

    mel.recompile_material(mat)
    unreal.EditorAssetLibrary.save_loaded_asset(mat)

    # --- instance -----------------------------------------------------------------------
    mi = tools.create_asset("MI_%s" % slot, dest_dir, unreal.MaterialInstanceConstant,
                            unreal.MaterialInstanceConstantFactoryNew())
    if mi is None:
        raise ImportError_("could not create material instance for " + slot)
    mel.set_material_instance_parent(mi, mat)  # VERIFY: name/signature in 5.8
    mel.set_material_instance_scalar_parameter_value(mi, "TilingMetres", float(size_m))  # VERIFY
    mel.update_material_instance(mi)  # VERIFY
    unreal.EditorAssetLibrary.save_loaded_asset(mi)
    return mat, mi


def main():
    if unreal is None:
        raise SystemExit("import_textures.py must run inside the Unreal Editor (module 'unreal' unavailable).")
    force = "--force" in (cp.script_args() if cp else sys.argv[1:])
    mpath = find_manifest()
    with open(mpath, "r", encoding="utf-8") as f:
        manifest = json.load(f)
    base_dir = os.path.dirname(mpath)
    slots = manifest.get("slots")
    if not slots:
        raise ImportError_("manifest.json has no 'slots'")

    _log("importing %d slots from %s (force=%s)" % (len(slots), mpath, force))
    for slot, entry in slots.items():
        asset_id = entry["asset_id"]
        size_m = entry.get("size_m")
        if not size_m:
            _warn("slot %s: no size_m in manifest, defaulting TilingMetres to 2.0" % slot)
            size_m = 2.0
        tex = {}
        for map_key, rel in entry["files"].items():
            tex[map_key] = import_one(slot, asset_id, map_key, os.path.normpath(os.path.join(base_dir, rel)), force)
        build_material(slot, tex, size_m)
        _log("slot %s ready: %s/MI_%s (TilingMetres=%s)" % (slot, "%s/%s" % (DEST_ROOT, slot), slot, size_m))
    _log("done")


if unreal is not None or __name__ == "__main__":
    if cp is not None and unreal is not None:
        cp.run(main)
    else:
        main()
