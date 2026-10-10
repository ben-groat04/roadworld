"""Build every kit piece, export FBX + proxy FBX and write kit_manifest.json.

  "C:/Program Files/Blender Foundation/Blender 5.2/blender.exe" --background --factory-startup \
      --python tools/blender_kit/build_all.py -- [--only Kerbs,Lighting] [--params my.json] [--out DIR]

Deterministic (seeded jitter only in the kerb variants), idempotent (overwrites outputs, rewrites the manifest;
with --only the other categories' manifest entries are kept).
"""
import sys, os, json, time, importlib
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import bpy
import common as C

argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
opts = {"only": None, "params": None, "out": C.KIT_DIR}
i = 0
while i < len(argv):
    if argv[i] == "--only": opts["only"] = argv[i + 1].split(","); i += 2
    elif argv[i] == "--params": opts["params"] = argv[i + 1]; i += 2
    elif argv[i] == "--out": opts["out"] = argv[i + 1]; i += 2
    else: i += 1
if opts["params"]:
    with open(opts["params"]) as fh:
        C.deep_merge(C.PARAMS, json.load(fh))

MODULES = {"kerbs": "Kerbs", "covers": "Drainage", "lights": "Lighting", "bollards": "Bollards", "barriers": "Barriers", "masonry": "Masonry"}
CAT_DIR = {"Kerbs": "Kerbs", "Drainage": "Drainage", "Lighting": "Lighting", "Bollards": "Bollards",
           "Barriers": "Barriers", "Masonry": "Masonry"}


def clear():
    for o in list(bpy.data.objects):
        bpy.data.objects.remove(o)
    for m in list(bpy.data.meshes):
        bpy.data.meshes.remove(m)


def main():
    t0 = time.time()
    out_root = opts["out"]
    mpath = os.path.join(out_root, "kit_manifest.json")
    old = {}
    if os.path.isfile(mpath):
        try:
            old = {m["name"]: m for m in json.load(open(mpath))["meshes"]}
        except Exception:
            old = {}
    entries = {}
    for modname, mcat in MODULES.items():
        if opts["only"] and mcat not in opts["only"]:
            continue
        mod = importlib.import_module(modname)
        for p in mod.pieces():
            cat = p["category"]
            if opts["only"] and cat not in opts["only"]:
                continue
            clear()
            ob = p["builder"].to_object()
            ob.name = p["name"]
            if p.get("post"):
                p["post"](ob)
            st = C.stats(ob)
            pname = p["name"] + "_proxy"
            if p.get("proxy") is not None:
                pr = p["proxy"].to_object()
                pr.name = pname
            else:
                pr = C.make_proxy(ob, pname, p.get("proxy_tris", 24))
            pst = C.stats(pr)
            d = os.path.join(out_root, CAT_DIR[cat])
            C.export_fbx(ob, os.path.join(d, p["name"] + ".fbx"))
            C.export_fbx(pr, os.path.join(d, pname + ".fbx"))
            e = {
                "name": p["name"], "category": cat, "fbx": "%s/%s.fbx" % (CAT_DIR[cat], p["name"]),
                "dimensions_m": st["dims"], "bbox_min_m": st["bbox_min"], "bbox_max_m": st["bbox_max"],
                "pivot": p["pivot"], "forward_axis": "+X along the run/road; +Y toward carriageway/traffic; +Z up",
                "material_slots": [m.name for m in ob.data.materials], "triangles": st["tris"],
                "open_edges": C.open_edges(ob), "proxy": pname, "proxy_fbx": "%s/%s.fbx" % (CAT_DIR[cat], pname),
                "proxy_triangles": pst["tris"], "collision": p.get("collision", "none"), "notes": p["notes"],
                "parameters": p["params"] if isinstance(p["params"], dict) else {},
            }
            if p.get("light"):
                e["light"] = p["light"]
            entries[p["name"]] = e
            print("BUILT %-34s tris=%6d dims=%s open_edges=%d proxy=%d" % (p["name"], st["tris"], st["dims"], e["open_edges"], pst["tris"]))
    merged = dict(old)
    merged.update(entries)
    meshes = [merged[k] for k in sorted(merged)]
    L = C.PARAMS["light"]
    styles = {}
    for sid, (H, O, tw) in L["styles"].items():
        base = "SM_LC_H%02d_O%02d_%s" % (H, round(O * 10), "T" if tw else "S")
        styles[sid] = {"mesh": base, "mesh_on_structure": base + "_PM", "mounting_height_m": H, "outreach_m": O, "twin_arm": tw}
    manifest = {
        "kit_version": 1, "generator": "tools/blender_kit/build_all.py (Blender %s)" % bpy.app.version_string,
        "units": "FBX in cm: 1 m in Blender == 100 uu; scale 1.0 on import",
        "axes": {"along": "+X", "toward_carriageway": "+Y", "up": "+Z",
                 "fbx_export": "axis_forward=-Z axis_up=Y (Blender defaults; verified round trip via verify_fbx.py)"},
        "uv": "UV 1.0 == 1 m (real-world scale); tile materials as UV / size_m",
        "uv_note": "box projection per face in metres; round parts cylindrical (u = arc length in m, v = axial metres)",
        "street_light": {
            "pivot": "ground level on the column axis (parapet-mount: underside of base plate)",
            "bracket_axis": "+Y in mesh space; rotate by StreetLight.heading (rad CCW from +x, direction the bracket points) so +Y maps to heading",
            "heading_to_yaw": "mesh +Y == bracket direction; yaw_about_Z = heading - pi/2 (mathematical CCW; mirror for Unreal's left-handed Y as needed)",
            "mounting_height_m": "luminaire centre height above the base == StreetLight.mounting_height",
            "outreach_m": "horizontal axis-to-bracket-tip distance == StreetLight.outreach",
            "twin_arm": "second identical arm along -Y (heading + pi)",
            "on_structure": "use mesh_on_structure (_PM: flat bolted plate, no door/root) when StreetLight.on_structure",
            "styles": styles,
            "generic_mesh_name": "SM_LC_H{height:02d}_O{outreach_dm:02d}_{S|T}[_PM]; heights 6,8,10,12; outreach dm 06,10,15,18",
        },
        "meshes": meshes,
    }
    os.makedirs(out_root, exist_ok=True)
    with open(mpath, "w") as fh:
        json.dump(manifest, fh, indent=1)
    print("DONE %d meshes in %.1fs -> %s" % (len(entries), time.time() - t0, mpath))


main()
