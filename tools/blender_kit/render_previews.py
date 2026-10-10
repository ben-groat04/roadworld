"""Headless preview renders: one contact sheet per category plus a street assembly.

  blender --background --factory-startup --python tools/blender_kit/render_previews.py -- [--cats Kerbs,Lighting] [--assembly] [--samples 48] [--res 1500x940] [--proxy]

Re-imports the exported FBX files (so it also exercises the Unreal-bound files), applies preview materials by slot name
(PolyHaven sets for concrete/pavement, simple principled for metals) and renders with Cycles (GPU if available).
"""
import sys, os, json, math
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import bpy
from mathutils import Vector, Matrix
import common as C

argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
cats = None; assembly = False; samples = 48; res = (1500, 940); proxies = False; only_names = None
i = 0
while i < len(argv):
    a = argv[i]
    if a == "--cats": cats = [c for c in argv[i + 1].split(",") if c in ("Kerbs","Drainage","Lighting","Bollards","Barriers","Masonry")]; i += 2
    elif a == "--assembly": assembly = True; i += 1
    elif a == "--samples": samples = int(argv[i + 1]); i += 2
    elif a == "--res": res = tuple(int(x) for x in argv[i + 1].split("x")); i += 2
    elif a == "--proxy": proxies = True; i += 1
    elif a == "--names": only_names = argv[i + 1].split(","); i += 2
    else: i += 1

PREVIEW_DIR = os.path.join(C.HERE, "preview")
PH = os.path.join(C.REPO, "unreal", "CityPainter", "SourceArt", "Textures", "PolyHaven")
PH_SETS = {"kerb_concrete": ("concrete_wall_009", 1.805), "structural_concrete": ("concrete", 4.0), "pavement": ("concrete_pavement", 1.8),
           "ground": ("asphalt_02", 3.0)}


def ph_material(slot, key=None):
    name = "pv_" + (key or slot)
    if name in bpy.data.materials:
        return bpy.data.materials[name]
    aid, size = PH_SETS[key or slot]
    m = bpy.data.materials.new(name); m.use_nodes = True
    nt = m.node_tree; bsdf = next(n for n in nt.nodes if n.type == 'BSDF_PRINCIPLED')
    uv = nt.nodes.new('ShaderNodeTexCoord'); mp = nt.nodes.new('ShaderNodeMapping')
    mp.inputs['Scale'].default_value = (1 / size, 1 / size, 1)
    nt.links.new(uv.outputs['UV'], mp.inputs['Vector'])

    def tex(kind, cs):
        p = os.path.join(PH, aid, "%s_%s_2k.jpg" % (aid, kind))
        n = nt.nodes.new('ShaderNodeTexImage'); n.image = bpy.data.images.load(p)
        n.image.colorspace_settings.name = cs
        nt.links.new(mp.outputs['Vector'], n.inputs['Vector'])
        return n
    d = tex("diff", "sRGB"); r = tex("rough", "Non-Color"); nn = tex("nor_dx", "Non-Color")
    nt.links.new(d.outputs['Color'], bsdf.inputs['Base Color'])
    nt.links.new(r.outputs['Color'], bsdf.inputs['Roughness'])
    sep = nt.nodes.new('ShaderNodeSeparateColor'); com = nt.nodes.new('ShaderNodeCombineColor'); inv = nt.nodes.new('ShaderNodeMath')
    inv.operation = 'SUBTRACT'; inv.inputs[0].default_value = 1.0
    nt.links.new(nn.outputs['Color'], sep.inputs['Color'])
    nt.links.new(sep.outputs['Green'], inv.inputs[1])
    nt.links.new(sep.outputs['Red'], com.inputs['Red']); nt.links.new(inv.outputs['Value'], com.inputs['Green']); nt.links.new(sep.outputs['Blue'], com.inputs['Blue'])
    nm = nt.nodes.new('ShaderNodeNormalMap'); nm.inputs['Strength'].default_value = 1.0
    nt.links.new(com.outputs['Color'], nm.inputs['Color']); nt.links.new(nm.outputs['Normal'], bsdf.inputs['Normal'])
    return m


def preview_material(slot):
    if slot in ("kerb_concrete", "structural_concrete", "pavement"):
        return ph_material(slot)
    return C.get_material(slot)


def setup_render():
    sc = bpy.context.scene
    sc.render.engine = 'CYCLES'
    sc.cycles.samples = samples
    sc.cycles.use_denoising = True
    sc.render.resolution_x, sc.render.resolution_y = res
    sc.render.film_transparent = False
    try:
        pref = bpy.context.preferences.addons['cycles'].preferences
        for t in (os.environ.get('KIT_GPU', 'CUDA'),):
            try:
                pref.compute_device_type = t
                pref.get_devices()
                if any(d.type == t for d in pref.devices):
                    for d in pref.devices: d.use = (d.type == t)
                    sc.cycles.device = 'GPU'
                    print("PREVIEW device", t)
                    break
            except Exception:
                continue
    except Exception as e:
        print("PREVIEW gpu setup failed", e)
    w = bpy.data.worlds.new("w"); sc.world = w; w.use_nodes = True
    bg = next(n for n in w.node_tree.nodes if n.type == 'BACKGROUND')
    bg.inputs['Color'].default_value = (0.62, 0.68, 0.76, 1); bg.inputs['Strength'].default_value = 0.9
    sun = bpy.data.lights.new("sun", 'SUN'); sun.energy = 3.2; sun.angle = math.radians(3)
    so = bpy.data.objects.new("sun", sun); bpy.context.scene.collection.objects.link(so)
    so.rotation_euler = (math.radians(52), math.radians(8), math.radians(-35))
    sc.view_settings.view_transform = 'Standard' if 'Standard' in [i.identifier for i in bpy.types.ColorManagedViewSettings.bl_rna.properties['view_transform'].enum_items] else sc.view_settings.view_transform
    return sc


def add_ground(size, x0, x1, y0, y1, z=0.0, key="ground", tile=None):
    me = bpy.data.meshes.new("ground")
    vs = [(x0, y0, z), (x1, y0, z), (x1, y1, z), (x0, y1, z)]
    me.from_pydata(vs, [], [(0, 1, 2, 3)]); me.update()
    me.uv_layers.new(name="UVMap")
    for l in me.loops:
        v = me.vertices[l.vertex_index].co
        me.uv_layers[0].data[l.index].uv = (v.x, v.y)
    ob = bpy.data.objects.new("ground", me); bpy.context.scene.collection.objects.link(ob)
    me.materials.append(ph_material("g", key))
    return ob


def import_piece(entry, proxy=False):
    path = os.path.join(C.KIT_DIR, entry["proxy_fbx" if proxy else "fbx"])
    before = set(bpy.data.objects)
    bpy.ops.import_scene.fbx(filepath=path)
    new = [o for o in bpy.data.objects if o not in before and o.type == 'MESH']
    ob = new[0]
    for s in ob.material_slots:
        base = s.material.name.split(".")[0] if s.material else None
        if base:
            s.material = preview_material(base)
    for o in set(bpy.data.objects) - before - set(new):
        bpy.data.objects.remove(o)
    return ob


def place(ob, loc, rot_z=0.0):
    m0 = ob.matrix_world.copy()
    ob.matrix_world = Matrix.Translation(Vector(loc)) @ Matrix.Rotation(rot_z, 4, 'Z') @ m0


def scale_bar(x, y, z=0.0, length=1.0):
    n = 4
    for k in range(n):
        me = bpy.data.meshes.new("sb")
        x0 = x + k * length / n
        vs = [(x0, y, z), (x0 + length / n, y, z), (x0 + length / n, y + 0.04, z), (x0, y + 0.04, z)]
        top = [(a, b, c + 0.02) for (a, b, c) in vs]
        me.from_pydata(vs + top, [], [(0, 3, 2, 1), (4, 5, 6, 7), (0, 1, 5, 4), (1, 2, 6, 5), (2, 3, 7, 6), (3, 0, 4, 7)]); me.update()
        ob = bpy.data.objects.new("sb", me); bpy.context.scene.collection.objects.link(ob)
        mat = bpy.data.materials.new("sb%d" % k); mat.use_nodes = True
        b = next(nd for nd in mat.node_tree.nodes if nd.type == 'BSDF_PRINCIPLED')
        b.inputs['Base Color'].default_value = (0.9, 0.1, 0.1, 1) if k % 2 == 0 else (0.95, 0.95, 0.95, 1)
        me.materials.append(mat)


def human(x, y, z=0.0, h=1.8):
    mat = bpy.data.materials.new("hum"); mat.use_nodes = True
    b = next(nd for nd in mat.node_tree.nodes if nd.type == 'BSDF_PRINCIPLED')
    b.inputs['Base Color'].default_value = (0.2, 0.45, 0.8, 1)
    bpy.ops.mesh.primitive_cylinder_add(radius=0.15, depth=h - 0.3, location=(x, y, z + (h - 0.3) / 2))
    a = bpy.context.object; a.data.materials.append(mat)
    bpy.ops.mesh.primitive_uv_sphere_add(radius=0.11, location=(x, y, z + h - 0.12))
    bpy.context.object.data.materials.append(mat)


def label(text, loc, size):
    bpy.ops.object.text_add(location=(loc[0], loc[1], 0.003), rotation=(0, 0, math.pi))
    t = bpy.context.object; t.data.body = text; t.data.size = size; t.data.align_x = 'LEFT'
    mat = bpy.data.materials.new("lbl"); mat.use_nodes = True
    b = next(nd for nd in mat.node_tree.nodes if nd.type == 'BSDF_PRINCIPLED')
    b.inputs['Base Color'].default_value = (1, 1, 1, 1); b.inputs['Emission Color'].default_value = (1, 1, 1, 1); b.inputs['Emission Strength'].default_value = 0.6
    t.data.materials.append(mat)


def camera(target, direction, dist, lens=50):
    cam = bpy.data.cameras.new("cam"); cam.lens = lens; cam.clip_end = 500
    co = bpy.data.objects.new("cam", cam); bpy.context.scene.collection.objects.link(co)
    d = Vector(direction).normalized()
    co.location = Vector(target) + d * dist
    co.rotation_euler = (-d).to_track_quat('-Z', 'Y').to_euler()
    bpy.context.scene.camera = co
    return co


def clear_scene():
    for o in list(bpy.data.objects): bpy.data.objects.remove(o)


def render(path):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    bpy.context.scene.render.filepath = path
    bpy.ops.render.render(write_still=True)
    print("PREVIEW wrote", path)


# category -> (columns, cell margin, cam dir, fit factor, label size, ground half-extent pad)
LAYOUT = {
    "Kerbs": (4, 0.30, (0.35, 1.0, 0.75), 1.0, 0.045),
    "Drainage": (4, 0.30, (0.25, 1.0, 1.1), 1.0, 0.05),
    "Lighting": (6, 1.6, (1.0, 0.5, 0.15), 1.0, 0.22),
    "Bollards": (3, 0.45, (0.3, 1.0, 0.35), 1.0, 0.04),
    "Barriers": (3, 1.4, (0.5, 1.0, 0.5), 1.0, 0.1),
    "Masonry": (3, 0.4, (0.35, 1.0, 0.7), 1.0, 0.04),
}


def contact_sheet(cat, meshes):
    clear_scene()
    sc = setup_render()
    cols, margin, cdir, fit, lsize = LAYOUT[cat]
    cols = min(cols, len(meshes))
    cw = max(m["dimensions_m"][0] for m in meshes) + margin
    ch = max(m["dimensions_m"][1] for m in meshes) + margin
    zmax = max(m["dimensions_m"][2] for m in meshes)
    rows = math.ceil(len(meshes) / cols)
    for k, m in enumerate(meshes):
        r, c = divmod(k, cols)
        ob = import_piece(m, proxies)
        bb0, bb1 = m["bbox_min_m"], m["bbox_max_m"]
        place(ob, (c * cw - bb0[0], -r * ch - bb0[1] - ((bb1[1] - bb0[1]) / 2 if cat in ("Lighting",) else 0), 0))
        label(m["name"].replace("SM_", ""), (c * cw, -r * ch - ch * 0.5 - 0.05 - (0.35 if cat == "Lighting" else 0), 0), lsize)
    x1 = cols * cw
    add_ground(0, -2, x1 + 2, -(rows) * ch - 2, 3, 0.0)
    scale_bar(-0.0, 0.6 if cat != "Lighting" else 3.0, 0.0)
    if cat in ("Lighting", "Barriers"):
        human(-0.9 if cat != "Lighting" else -1.2, -0.3, 0.0)
    cx = (x1 - cw) / 2 + 0.3
    cy = -(rows - 1) * ch / 2
    d = Vector(cdir).normalized()
    el = math.asin(d.z)
    halfw = max(cols if len(meshes) >= cols else len(meshes), 1) * cw / 2 * (math.cos(math.atan2(d.x, d.y)) if cat == "Lighting" else 1)
    if cat == "Lighting":
        halfw = max(halfw, len(meshes) * cw / 2 * abs(math.sin(math.atan2(d.x, d.y))) + 1.5)
    halfh = (zmax * math.cos(el) + rows * ch * math.sin(el) + (0.4 if cat != "Lighting" else 1.0)) / 2
    dist = max(halfw / math.tan(math.radians(19.8)), halfh / math.tan(math.radians(12.7))) * 1.3
    target = (cx, cy, zmax * (0.5 if cat == "Lighting" else 0.12))
    camera(target, cdir, dist)
    render(os.path.join(PREVIEW_DIR, "sheet_%s%s.png" % (cat, "_proxy" if proxies else "")))


def street_assembly(by):
    clear_scene()
    sc = setup_render()
    zfw = 0.125
    L = 20.0
    add_ground(0, -4, 26, 0.0, 9, 0.0, "ground")               # carriageway (z = 0)
    # footway (pavement) block y in [-4, 0] at z = zfw: a box so the kerb face reads
    me = bpy.data.meshes.new("fw")
    x0, x1, y0, y1 = -4, 26, -4.5, -0.125
    vs = [(x0, y0, 0), (x1, y0, 0), (x1, y1, 0), (x0, y1, 0), (x0, y0, zfw), (x1, y0, zfw), (x1, y1, zfw), (x0, y1, zfw)]
    me.from_pydata(vs, [], [(4, 5, 6, 7), (1, 2, 6, 5)]); me.update()
    me.uv_layers.new(name="UVMap")
    for l in me.loops:
        v = me.vertices[l.vertex_index].co
        me.uv_layers[0].data[l.index].uv = (v.x, v.y)
    fw = bpy.data.objects.new("fw", me); bpy.context.scene.collection.objects.link(fw)
    me.materials.append(ph_material("p", "pavement"))
    # kerbs
    names = ["SM_Kerb_HB2", "SM_Kerb_HB2_v1", "SM_Kerb_HB2", "SM_Kerb_HB2_v2", "SM_Kerb_HB2", "SM_Kerb_HB2_v3"]
    pitch = 0.915
    n = int(L / pitch)
    for k in range(n):
        nm = names[k % len(names)]
        if k >= n - 2: nm = "SM_Kerb_TransitionL" if k == n - 2 else "SM_Kerb_FlushDropper"
        ob = import_piece(by[nm])
        place(ob, (k * pitch, 0, -0.130 + 0.0))
    # edging along footway back
    for k in range(int(L / 0.915) + 1):
        ob = import_piece(by["SM_Kerb_Edging"])
        place(ob, (k * pitch, -4.5 + 0.0, zfw - 0.15 + 0.01), 0)
    place(import_piece(by["SM_Gully_Grating"]), (4.0, 0.30, 0.0))
    place(import_piece(by["SM_Gully_KerbInlet"]), (8.5, 0, -0.130 + 0.0))
    place(import_piece(by["SM_Manhole_Round600"]), (11.0, 2.6, 0.0))
    place(import_piece(by["SM_LC_H08_O10_S"]), (15.0, -1.6, zfw))
    for k in range(3):
        place(import_piece(by["SM_Guardrail_Ped_Rail"]), (1.0 + 2.0 * k, -0.9, zfw))
    place(import_piece(by["SM_Guardrail_Ped_Rail_End"]), (7.0, -0.9, zfw))
    place(import_piece(by["SM_Bollard_Steel"]), (18.0, -1.2, zfw))
    place(import_piece(by["SM_Tactile_Blister_400"]), (18.5, -0.25, zfw), 0)
    human(12.5, -1.4, zfw)
    scale_bar(0.0, 1.5, 0.0)
    camera((10, -0.5, 2.6), (0.30, 0.95, 0.30), 25, lens=32)
    render(os.path.join(PREVIEW_DIR, "street_assembly.png"))
    for nm, tgt, d, dist in (("closeup_kerb_gully", (6.0, 0.1, 0.1), (0.25, 0.9, 0.45), 5.0),
                             ("closeup_column_base", (15.0, -1.6, 1.0), (0.8, 0.6, 0.25), 3.6),
                             ("closeup_column_head", (15.0, -0.6, 8.0), (0.7, 0.7, 0.1), 5.0),
                             ("closeup_guardrail_end", (5.0, -0.6, 0.6), (0.4, 0.9, 0.35), 6.0)):
        bpy.data.objects.remove(bpy.context.scene.camera)
        camera(tgt, d, dist, lens=40)
        render(os.path.join(PREVIEW_DIR, nm + ".png"))


def main():
    man = json.load(open(os.path.join(C.KIT_DIR, "kit_manifest.json")))
    by = {m["name"]: m for m in man["meshes"]}
    for cat in (cats if cats is not None else ([] if assembly else list(LAYOUT))):
        meshes = [m for m in man["meshes"] if m["category"] == cat]
        if cat == "Lighting":
            keep = ["SM_LC_H06_O06_S", "SM_LC_H08_O10_S", "SM_LC_H10_O15_S", "SM_LC_H12_O18_T", "SM_LC_H08_O10_S_PM", "SM_LC_H12_O18_S"]
            meshes = [by[k] for k in keep if k in by]
        if only_names:
            meshes = [m for m in meshes if m["name"] in only_names]
        if cats is None and not assembly or cats:
            contact_sheet(cat, meshes)
    if assembly:
        street_assembly(by)


main()
