"""Shared helpers for the vegetation kit (Blender 5.x, headless).

Conventions (same as tools/blender_kit): metres in Blender, Z up, FBX exported so 1 m = 100 uu (axis forward -Z, up Y),
pivot at ground contact. Naming SM_Veg_<TypeId>_<Variant>_LOD<n>.
Per-loop data written on every mesh:
  UV0            atlas coordinates (leaf atlas 2x2 cells, grass atlas 4x2 cells, see veg_textures.py)
  colour attr "Wind"  R = wind weight (0 at base, 1 at tips), G = per-card phase 0..1, B = ambient occlusion hint 0..1
  custom normals on foliage (outward / upward blends so cards shade as a volume, not as flat planes)
Material slots: veg_leaf (two-sided masked, leaf atlas), veg_grass (two-sided masked, grass atlas), veg_bark (opaque),
veg_core (opaque dark hedge core), veg_canopy_far (opaque far-LOD canopy), veg_timber (stakes).
"""
import bpy, math, os, sys, random, json
import numpy as np
from mathutils import Vector, Matrix

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, "..", "..", ".."))
MESH_DIR = os.path.join(REPO, "unreal", "CityPainter", "SourceArt", "Meshes", "Vegetation")
TEX_DIR = os.path.join(REPO, "unreal", "CityPainter", "SourceArt", "Textures", "Vegetation")
Z = Vector((0, 0, 1))

# leaf atlas cells -> uv rect ; grass atlas cells -> uv rect (u0, v0, u1, v1)
LEAF_CELLS = {i: (0.5 * (i % 2), 0.5 * (i // 2), 0.5 * (i % 2) + 0.5, 0.5 * (i // 2) + 0.5) for i in range(4)}
GRASS_CELLS = {i: (0.25 * (i % 4), 0.5 * (i // 4), 0.25 * (i % 4) + 0.25, 0.5 * (i // 4) + 0.5) for i in range(8)}
LEAF_BROAD, LEAF_HEDGE, LEAF_CONIFER, LEAF_GORSE = 0, 1, 2, 3
G_MOWN, G_ROUGH, G_TUSSOCK, G_WEED, G_BRACKEN, G_DRY, G_CREEP = 0, 1, 2, 3, 4, 5, 6


def crop(rect, fu=1.0, fv=1.0):
    """Sub-rectangle anchored at the cell bottom-centre: keep fraction fu of the width and fv of the height."""
    u0, v0, u1, v1 = rect
    cu = 0.5 * (u0 + u1)
    hw = 0.5 * (u1 - u0) * fu
    return (cu - hw, v0, cu + hw, v0 + (v1 - v0) * fv)


def norm(v):
    v = Vector(v)
    return v.normalized() if v.length > 1e-9 else Vector((0, 0, 1))


def reset_scene():
    bpy.ops.wm.read_factory_settings(use_empty=True)
    u = bpy.context.scene.unit_settings
    u.system = 'METRIC'
    u.scale_length = 1.0
    u.length_unit = 'METERS'


# ---- preview/export materials ------------------------------------------------------------------------------------
_IMG = {}


def _img(name):
    if name not in _IMG:
        p = os.path.join(TEX_DIR, name)
        img = bpy.data.images.load(p)
        _IMG[name] = img
    return _IMG[name]


def _tint_node(nt, links, colour_out):
    at = nt.nodes.new("ShaderNodeAttribute")
    at.attribute_type = 'OBJECT'
    at.attribute_name = '["tint"]'
    mx = nt.nodes.new("ShaderNodeMix")
    mx.data_type = 'RGBA'
    mx.blend_type = 'MULTIPLY'
    mx.inputs[0].default_value = 1.0
    links.new(colour_out, mx.inputs[6])
    links.new(at.outputs["Color"], mx.inputs[7])
    return mx.outputs[2]


def get_material(slot):
    if slot in bpy.data.materials:
        return bpy.data.materials[slot]
    m = bpy.data.materials.new(slot)
    m.use_nodes = True
    nt = m.node_tree
    nodes, links = nt.nodes, nt.links
    for n in list(nodes):
        nodes.remove(n)
    out = nodes.new("ShaderNodeOutputMaterial")
    bsdf = nodes.new("ShaderNodeBsdfPrincipled")
    if slot in ("veg_leaf", "veg_grass"):
        atlas = "Leaf" if slot == "veg_leaf" else "Grass"
        tex = nodes.new("ShaderNodeTexImage")
        tex.image = _img("T_Veg_%s_BC.png" % atlas)
        tex.interpolation = 'Linear'
        nmap_t = nodes.new("ShaderNodeTexImage")
        nmap_t.image = _img("T_Veg_%s_N.png" % atlas)
        nmap_t.image.colorspace_settings.name = 'Non-Color'
        nmap = nodes.new("ShaderNodeNormalMap")
        links.new(nmap_t.outputs["Color"], nmap.inputs["Color"])
        links.new(nmap.outputs["Normal"], bsdf.inputs["Normal"])
        col = _tint_node(nt, links, tex.outputs["Color"])
        links.new(col, bsdf.inputs["Base Color"])
        bsdf.inputs["Roughness"].default_value = 0.65
        try:
            bsdf.inputs["Specular IOR Level"].default_value = 0.25
        except KeyError:
            pass
        tr = nodes.new("ShaderNodeBsdfTranslucent")
        links.new(col, tr.inputs["Color"])
        mix_t = nodes.new("ShaderNodeMixShader")
        mix_t.inputs[0].default_value = 0.30 if slot == "veg_leaf" else 0.15
        links.new(bsdf.outputs["BSDF"], mix_t.inputs[1])
        links.new(tr.outputs["BSDF"], mix_t.inputs[2])
        tp = nodes.new("ShaderNodeBsdfTransparent")
        gt = nodes.new("ShaderNodeMath")
        gt.operation = 'GREATER_THAN'
        gt.inputs[1].default_value = 0.5
        links.new(tex.outputs["Alpha"], gt.inputs[0])
        mix_a = nodes.new("ShaderNodeMixShader")
        links.new(gt.outputs[0], mix_a.inputs[0])
        links.new(tp.outputs["BSDF"], mix_a.inputs[1])
        links.new(mix_t.outputs["Shader"], mix_a.inputs[2])
        links.new(mix_a.outputs["Shader"], out.inputs["Surface"])
        try:
            m.surface_render_method = 'DITHERED'
        except Exception:
            pass
        m.use_backface_culling = False
    else:
        spec = {"veg_bark": ((0.075, 0.06, 0.045), 0.9), "veg_core": ((0.020, 0.032, 0.014), 0.9),
                "veg_canopy_far": ((0.045, 0.075, 0.025), 0.85), "veg_timber": ((0.20, 0.14, 0.08), 0.8)}[slot]
        at = nodes.new("ShaderNodeAttribute")
        at.attribute_type = 'OBJECT'
        at.attribute_name = '["tint"]'
        mx = nodes.new("ShaderNodeMix")
        mx.data_type = 'RGBA'
        mx.blend_type = 'MULTIPLY'
        mx.inputs[0].default_value = 1.0
        mx.inputs[6].default_value = (*spec[0], 1)
        links.new(at.outputs["Color"], mx.inputs[7])
        links.new(mx.outputs[2], bsdf.inputs["Base Color"])
        bsdf.inputs["Roughness"].default_value = spec[1]
        links.new(bsdf.outputs["BSDF"], out.inputs["Surface"])
    return m


# ---- mesh builder ------------------------------------------------------------------------------------------------
class VMesh:
    def __init__(self, name):
        self.name = name
        self.verts = []
        self.faces = []
        self.fmat = []
        self.uv = []     # per loop
        self.nrm = []    # per loop (None -> auto)
        self.col = []    # per loop (r, g, b)
        self.slots = []

    def slot(self, s):
        if s not in self.slots:
            self.slots.append(s)
        return self.slots.index(s)

    def vert(self, p):
        self.verts.append(tuple(p))
        return len(self.verts) - 1

    def face(self, idx, mat, uvs, nrms, cols):
        self.faces.append(tuple(idx))
        self.fmat.append(self.slot(mat))
        self.uv.extend(uvs)
        self.nrm.extend(nrms)
        self.col.extend(cols)

    # a flat quad centred on c with face normal n, "up" axis hint, size w x h, rolled about n
    def card(self, c, n, up_hint, w, h, rect, mat, shade_n, wind_r, phase, ao, roll=0.0, flip_u=False):
        c, n = Vector(c), norm(n)
        right = norm(up_hint.cross(n)) if abs(up_hint.normalized().dot(n)) < 0.97 else norm(Vector((1, 0, 0)).cross(n))
        up = n.cross(right)
        if roll:
            cs, sn = math.cos(roll), math.sin(roll)
            right, up = right * cs + up * sn, up * cs - right * sn
        u0, v0, u1, v1 = rect
        if flip_u:
            u0, u1 = u1, u0
        hw, hh = right * (w / 2), up * (h / 2)
        pts = [c - hw - hh, c + hw - hh, c + hw + hh, c - hw + hh]
        idx = [self.vert(p) for p in pts]
        wr = wind_r if callable(wind_r) is False else None
        cols = []
        for p in pts:
            r = wind_r(p) if callable(wind_r) else wind_r
            cols.append((r, phase, ao))
        s = shade_n
        self.face(idx, mat, [(u0, v0), (u1, v0), (u1, v1), (u0, v1)], [tuple(s)] * 4, cols)

    # bent strip rising from base point: width w, length h, `rows` segments, leaning along `lean` (unit horizontal)
    def blade_card(self, base, up, right, w, h, rows, bend, rect, mat, phase, nrm_out, wind_scale=1.0, taper=1.0):
        base, up, right = Vector(base), norm(up), norm(right)
        lean = norm(right.cross(up))
        prev = None
        L = []
        for k in range(rows + 1):
            t = k / rows
            cen = base + up * (h * t) + lean * (bend * h * t * t)
            ww = w * (1 - (1 - taper) * t) / 2
            a, b = cen - right * ww, cen + right * ww
            L.append((self.vert(a), self.vert(b), a, b, t))
        u0, v0, u1, v1 = rect
        s = tuple(norm(nrm_out * 0.45 + Z * 0.55))
        for k in range(rows):
            ia, ib, a0, b0, t0 = L[k]
            ja, jb, a1, b1, t1 = L[k + 1]
            self.face([ia, ib, jb, ja], mat,
                      [(u0, v0 + (v1 - v0) * t0), (u1, v0 + (v1 - v0) * t0), (u1, v0 + (v1 - v0) * t1), (u0, v0 + (v1 - v0) * t1)],
                      [s] * 4,
                      [(t0 * wind_scale, phase, 0.5 + 0.5 * t0), (t0 * wind_scale, phase, 0.5 + 0.5 * t0),
                       (t1 * wind_scale, phase, 0.5 + 0.5 * t1), (t1 * wind_scale, phase, 0.5 + 0.5 * t1)])

    # tapered tube along a polyline (parallel-transport frames); radii per point; open ends
    def tube(self, pts, radii, sides, mat="veg_bark", wind_fn=None, uv_scale=1.0):
        pts = [Vector(p) for p in pts]
        if len(pts) < 2:
            return
        t0 = norm(pts[1] - pts[0])
        ref = Vector((1, 0, 0)) if abs(t0.x) < 0.9 else Vector((0, 1, 0))
        nrm0 = norm(t0.cross(ref))
        rings = []
        acc = 0.0
        for i, p in enumerate(pts):
            if i == 0:
                t = t0
            elif i == len(pts) - 1:
                t = norm(pts[i] - pts[i - 1])
            else:
                t = norm(pts[i + 1] - pts[i - 1])
            nrm0 = norm(nrm0 - t * nrm0.dot(t))
            bn = t.cross(nrm0)
            if i:
                acc += (pts[i] - pts[i - 1]).length
            r = max(radii[i], 0.002)
            ids = []
            dirs = []
            for s in range(sides):
                a = 2 * math.pi * s / sides
                d = nrm0 * math.cos(a) + bn * math.sin(a)
                dirs.append(d)
                ids.append(self.vert(p + d * r))
            rings.append((ids, dirs, acc, p))
        for i in range(len(rings) - 1):
            ia, da, va, pa = rings[i]
            ib, db, vb, pb = rings[i + 1]
            for s in range(sides):
                s2 = (s + 1) % sides
                ca = max(1, 2 * math.pi * radii[i])
                uu0, uu1 = s / sides * ca * uv_scale, (s + 1) / sides * ca * uv_scale
                wa = wind_fn(pa) if wind_fn else 0.0
                wb = wind_fn(pb) if wind_fn else 0.0
                self.face([ia[s], ia[s2], ib[s2], ib[s]], mat,
                          [(uu0, va), (uu1, va), (uu1, vb), (uu0, vb)],
                          [tuple(da[s]), tuple(da[s2]), tuple(db[s2]), tuple(db[s])],
                          [(wa, 0, 0.7), (wa, 0, 0.7), (wb, 0, 0.7), (wb, 0, 0.7)])

    def prism_cap(self, pts_ring):  # unused helper kept simple
        pass

    def to_object(self, link=True):
        me = bpy.data.meshes.new(self.name)
        me.from_pydata(self.verts, [], self.faces)
        me.update()
        for s in self.slots:
            me.materials.append(get_material(s))
        me.polygons.foreach_set("material_index", self.fmat)
        uvl = me.uv_layers.new(name="UVMap")
        uvl.data.foreach_set("uv", np.array(self.uv, np.float32).ravel())
        ca = me.color_attributes.new("Wind", 'FLOAT_COLOR', 'CORNER')
        c = np.zeros((len(self.col), 4), np.float32)
        c[:, :3] = np.clip(np.array(self.col, np.float32), 0, 1)
        c[:, 3] = 1
        ca.data.foreach_set("color", c.ravel())
        if all(n is not None for n in self.nrm) and self.nrm:
            try:
                me.normals_split_custom_set([tuple(n) for n in self.nrm])
            except Exception as e:  # pragma: no cover
                print("custom normals failed", e)
        for p in me.polygons:
            p.use_smooth = True
        ob = bpy.data.objects.new(self.name, me)
        ob["tint"] = (1.0, 1.0, 1.0)
        if link:
            bpy.context.scene.collection.objects.link(ob)
        return ob


def stats(ob):
    me = ob.data
    me.calc_loop_triangles()
    vs = np.array([tuple(v.co) for v in me.vertices]) if len(me.vertices) else np.zeros((1, 3))
    mn, mx = vs.min(0), vs.max(0)
    return {"tris": len(me.loop_triangles), "bbox_min": [round(float(x), 3) for x in mn],
            "bbox_max": [round(float(x), 3) for x in mx], "dims": [round(float(mx[i] - mn[i]), 3) for i in range(3)]}


def export_fbx(ob, path):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    for o in bpy.context.scene.objects:
        o.select_set(False)
    ob.select_set(True)
    bpy.context.view_layer.objects.active = ob
    bpy.ops.export_scene.fbx(
        filepath=path, use_selection=True, global_scale=1.0, apply_unit_scale=True,
        apply_scale_options='FBX_SCALE_NONE', axis_forward='-Z', axis_up='Y', object_types={'MESH'},
        use_mesh_modifiers=True, mesh_smooth_type='OFF', colors_type='LINEAR', add_leaf_bones=False, bake_anim=False,
        use_tspace=False, path_mode='STRIP')
    ob.select_set(False)


# ---- noise -------------------------------------------------------------------------------------------------------
def periodic_noise(rng, period, n_terms=4, amp=1.0, max_cycles=6):
    """Sum of sinusoids with integer cycles over `period` so the result tiles exactly. Returns f(x)."""
    terms = []
    for i in range(n_terms):
        k = rng.randint(1, max_cycles)
        terms.append((k, rng.uniform(0, 2 * math.pi), amp * rng.uniform(0.4, 1.0) / (0.6 + 0.4 * k)))
    tot = sum(a for _, _, a in terms)

    def f(x):
        return sum(a * math.sin(2 * math.pi * k * x / period + ph) for k, ph, a in terms) / tot
    return f
