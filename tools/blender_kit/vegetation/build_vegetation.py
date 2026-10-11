"""Build the whole vegetation kit: textures, meshes (all LODs), FBX, manifest.

    "C:/Program Files/Blender Foundation/Blender 5.2/blender.exe" --background --factory-startup \
        --python tools/blender_kit/vegetation/build_vegetation.py -- [--only hedges,grass,shrubs,trees,canopy] [--no-textures] [--out DIR]

Outputs: unreal/CityPainter/SourceArt/Meshes/Vegetation/SM_Veg_<TypeId>_<Variant>_LOD<n>.fbx,
         vegetation_manifest.json and vegetation_manifest.md in the same folder, textures in SourceArt/Textures/Vegetation/.
"""
import sys, os, json, time
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from veg_common import *
import veg_textures, veg_hedge, veg_ground, veg_tree

args = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
only = None
out_dir = MESH_DIR
if "--only" in args:
    only = set(args[args.index("--only") + 1].split(","))
if "--out" in args:
    out_dir = args[args.index("--out") + 1]
do_tex = "--no-textures" not in args

reset_scene()
if do_tex:
    veg_textures.bake_all()

manifest = []


def want(group):
    return only is None or group in only


def emit(type_id, variant, lod_builders, role="module", notes="", extra=None):
    """lod_builders: list of callables (one per LOD) returning VMesh."""
    entry = {"type_id": type_id, "variant": variant, "role": role, "pivot": "ground contact, centre of footprint", "lods": []}
    slots = []
    for lod, fn in enumerate(lod_builders):
        vm = fn()
        name = "SM_Veg_%s_%s_LOD%d" % (type_id, variant, lod)
        vm.name = name
        ob = vm.to_object()
        ob.name = name
        s = stats(ob)
        fbx = os.path.join(out_dir, name + ".fbx")
        export_fbx(ob, fbx)
        for m in vm.slots:
            if m not in slots:
                slots.append(m)
        entry["lods"].append({"lod": lod, "file": name + ".fbx", "tris": s["tris"], "dims_m": s["dims"],
                              "bbox_min_m": s["bbox_min"], "bbox_max_m": s["bbox_max"]})
        bpy.data.objects.remove(ob)
        bpy.data.meshes.remove(bpy.data.meshes[name]) if name in bpy.data.meshes else None
    entry["dims_m"] = entry["lods"][0]["dims_m"]
    entry["material_slots"] = slots
    if notes:
        entry["notes"] = notes
    if extra:
        entry.update(extra)
    manifest.append(entry)
    print("%-34s %-4s %s" % (type_id, variant, [l["tris"] for l in entry["lods"]]))


t0 = time.time()
if want("hedges"):
    for kind, vs in veg_hedge.HEDGES.items():
        for v, spec in vs.items():
            emit(kind, v, [lambda lod=lod, kind=kind, spec=spec: veg_hedge.build_hedge(kind, spec, lod) for lod in range(3)],
                 role=veg_hedge.ROLE[spec["end"]],
                 notes="Module length %.0f m along +X, pivot at ground centre of the module. %s" % (
                     spec["L"], "Closed end at +X (mirror for the other end)." if spec["end"] else "Tiles end to end (noise is periodic in X)."),
                 extra={"module_length_m": spec["L"], "nominal_core_height_m": spec["H"], "nominal_width_m": spec["W"]})
if want("grass"):
    for kind in ("grass_mown_clump", "grass_rough_tuft", "grass_tussock", "weed_creep", "bracken_patch"):
        for i in range(veg_ground.GRASS_VARIANTS):
            emit(kind, "V%02d" % i, [lambda lod=lod, kind=kind, i=i: veg_ground.build_grass(kind, i, lod) for lod in range(3)],
                 role="cluster", notes="Crossed/bent cards, blade tips wind weight 1. Fade out by distance (grass only under ~30 m camera height).")
    for i in range(veg_ground.STRIP_VARIANTS):
        emit("grass_edge_creep_strip", "V%02d" % i, [lambda lod=lod, i=i: veg_ground.build_edge_strip(i, lod) for lod in range(2)],
             role="strip", notes="1 m long along +X; +Y toward the carriageway: ragged edge over the asphalt, pivot on the edge line.")
if want("shrubs"):
    for kind in ("shrub_scrub", "shrub_gorse"):
        for i in range(3):
            emit(kind, "V%02d" % i, [lambda lod=lod, kind=kind, i=i: veg_ground.build_shrub(kind, i, lod) for lod in range(3)],
                 role="shrub")
if want("trees"):
    for species, tid in (("round", "tree_broadleaf_round"), ("open", "tree_broadleaf_open")):
        for sz in ("S", "M", "L"):
            for si in range(2):
                emit(tid, "%s%d" % (sz, si), [lambda lod=lod, species=species, sz=sz, si=si: veg_tree.build_broadleaf(species, sz, si, lod) for lod in range(3)],
                     role="tree", extra={"size_class": sz, "nominal_height_m": veg_tree.SIZES[sz]},
                     notes="Leaf state: leaf only (a bare variant is not built).")
    for i in range(3):
        emit("tree_sapling_staked", "V%02d" % i, [lambda lod=lod, i=i: veg_tree.build_sapling(i, lod) for lod in range(3)], role="tree")
    for i in range(3):
        emit("tree_conifer_plantation", "V%02d" % i, [lambda lod=lod, i=i: veg_tree.build_conifer(i, lod) for lod in range(3)], role="tree")
if want("canopy"):
    for i in range(3):
        emit("canopy_impostor_block", "V%02d" % i, [lambda lod=lod, i=i: veg_ground.build_canopy_block(i, lod) for lod in range(2)],
             role="block", notes="Distant woodland volume; opaque, vertex colour B gives per-face shade variation.")

man = {"generator": "tools/blender_kit/vegetation/build_vegetation.py", "blender": bpy.app.version_string,
       "units": "metres in this file; FBX is cm (100 uu per m)", "axes": "+X along the run, +Y toward the carriageway, +Z up",
       "wind_vertex_colour": {"attribute": "Wind", "R": "wind weight 0 base .. 1 tips", "G": "per-card phase", "B": "shade/AO hint 0..1"},
       "textures": ["T_Veg_Leaf_BC/N/R.png", "T_Veg_Grass_BC/N/R.png"], "assets": manifest}
mj = os.path.join(out_dir, "vegetation_manifest.json")
if only is not None and os.path.exists(mj):
    old = json.load(open(mj))
    keep = [a for a in old["assets"] if not any(a["type_id"] == m["type_id"] and a["variant"] == m["variant"] for m in manifest)]
    man["assets"] = keep + manifest
os.makedirs(out_dir, exist_ok=True)
json.dump(man, open(mj, "w"), indent=1)
lines = ["# Vegetation kit manifest", "", "Generated by tools/blender_kit/vegetation/build_vegetation.py (Blender %s). Metres; pivot at ground contact; +X along run." % bpy.app.version_string, "",
         "| type_id | variant | role | dims LOD0 (x,y,z m) | tris LOD0/1/2 | slots |", "|---|---|---|---|---|---|"]
for a in man["assets"]:
    lines.append("| %s | %s | %s | %s | %s | %s |" % (a["type_id"], a["variant"], a["role"], " x ".join("%.2f" % d for d in a["dims_m"]),
                                                      "/".join(str(l["tris"]) for l in a["lods"]), ", ".join(a["material_slots"])))
open(os.path.join(out_dir, "vegetation_manifest.md"), "w").write("\n".join(lines) + "\n")
print("done %d assets in %.0f s" % (len(manifest), time.time() - t0))
