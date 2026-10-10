"""Shared bpy helpers for the City Painter kit. Run only inside Blender (headless).

Conventions
  * Blender units are metres (scale_length 1.0). FBX is exported so 1 m == 100 uu in Unreal.
  * Local frame of every piece: +X along the road/run, +Y toward the carriageway/traffic side, +Z up.
  * UVs are real-world: UV 1.0 == 1 m (box projection per face; cylindrical for round parts). Materials tile
    in Unreal with UV * (1 / TilingMetres) (the PolyHaven manifest size_m).
  * Material slot names equal the project's surface slots (see SLOT_PREVIEW keys).
"""
import bpy, bmesh, math, os, sys, json, copy
from mathutils import Vector, Matrix

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, "..", ".."))
KIT_DIR = os.path.join(REPO, "unreal", "CityPainter", "SourceArt", "Meshes", "Kit")
if HERE not in sys.path:
    sys.path.insert(0, HERE)
from params import PARAMS as _DEFAULT

PARAMS = copy.deepcopy(_DEFAULT)


def deep_merge(a, b):
    for k, v in b.items():
        if isinstance(v, dict) and isinstance(a.get(k), dict):
            deep_merge(a[k], v)
        else:
            a[k] = v


# Material slot name -> preview (base colour, metallic, roughness); None = PolyHaven texture set in previews
SLOT_PREVIEW = {
    "kerb_concrete": None, "structural_concrete": None, "pavement": None,
    "galvanised_steel": ((0.55, 0.57, 0.58), 1.0, 0.35), "painted_steel": ((0.07, 0.08, 0.09), 0.6, 0.45),
    "cast_iron": ((0.08, 0.075, 0.07), 0.7, 0.6), "lantern_glass": ((0.7, 0.8, 0.85), 0.0, 0.05),
    "plastic_black": ((0.02, 0.02, 0.02), 0.0, 0.5), "plastic_white": ((0.8, 0.8, 0.78), 0.0, 0.4),
    "timber": ((0.28, 0.17, 0.09), 0.0, 0.8),
}


def reset_scene():
    bpy.ops.wm.read_factory_settings(use_empty=True)
    u = bpy.context.scene.unit_settings
    u.system = 'METRIC'
    u.scale_length = 1.0
    u.length_unit = 'METERS'


def get_material(name):
    if name not in bpy.data.materials:
        m = bpy.data.materials.new(name)
        m.use_nodes = True
        spec = SLOT_PREVIEW.get(name)
        bsdf = next(n for n in m.node_tree.nodes if n.type == 'BSDF_PRINCIPLED')
        if spec:
            bsdf.inputs['Base Color'].default_value = (*spec[0], 1)
            bsdf.inputs['Metallic'].default_value = spec[1]
            bsdf.inputs['Roughness'].default_value = spec[2]
        m.diffuse_color = (*(spec[0] if spec else (0.6, 0.6, 0.6)), 1)
    return bpy.data.materials[name]


class Builder:
    """Accumulates closed, bevelled parts into one bmesh with per-part material slot and metric UVs."""

    def __init__(self, name):
        self.name = name
        self.bm = bmesh.new()
        self.uv = self.bm.loops.layers.uv.new("UVMap")
        self.slots = []

    def slot(self, m):
        if m not in self.slots:
            self.slots.append(m)
        return self.slots.index(m)

    def part(self, verts, faces, mat, bevel=0.0, segs=2, cyl=None, xf=None, angle=35.0):
        """verts: (x,y,z) list; faces: index tuples. cyl=(axis, centre Vector) selects cylindrical UVs."""
        bm = self.bm
        vs = []
        for v in verts:
            p = Vector(v)
            if xf is not None:
                p = xf @ p
            vs.append(bm.verts.new(p))
        fs = [bm.faces.new([vs[i] for i in f]) for f in faces]
        bmesh.ops.recalc_face_normals(bm, faces=fs)
        allf = list(fs)
        if bevel > 0:
            thr = math.radians(angle)
            edges = set()
            for f in fs:
                for e in f.edges:
                    if len(e.link_faces) == 2:
                        try:
                            if e.calc_face_angle() > thr:
                                edges.add(e)
                        except ValueError:
                            pass
            if edges:
                r = bmesh.ops.bevel(bm, geom=list(edges), offset=bevel, offset_type='OFFSET', segments=segs,
                                    profile=0.5, affect='EDGES', clamp_overlap=True)
                allf += r['faces']
        mi = self.slot(mat)
        mpp = 1.0 / PARAMS["uv_metres_per_unit"]
        for f in allf:
            if not f.is_valid:
                continue
            f.material_index = mi
            n = f.normal
            use_cyl = False
            if cyl is not None:
                ax, cen = cyl
                idx = {'x': 0, 'y': 1, 'z': 2}[ax]
                use_cyl = abs(n[idx]) < 0.8
                if use_cyl:
                    o = [i for i in range(3) if i != idx]       # the two radial axes (cyclic order)
                    fc = f.calc_center_median()
                    a0 = math.atan2(fc[o[1]] - cen[o[1]], fc[o[0]] - cen[o[0]])
                    r_ref = max(math.hypot(fc[o[0]] - cen[o[0]], fc[o[1]] - cen[o[1]]), 1e-3)
            for l in f.loops:
                c = l.vert.co
                if use_cyl:
                    a = math.atan2(c[o[1]] - cen[o[1]], c[o[0]] - cen[o[0]])
                    da = (a - a0 + math.pi) % (2 * math.pi) - math.pi
                    l[self.uv].uv = ((a0 + da) * r_ref * mpp, c[idx] * mpp)
                else:
                    ax_ = max(range(3), key=lambda i: abs(n[i]))
                    u, v = ((c.y, c.z), (c.x, c.z), (c.x, c.y))[ax_]
                    l[self.uv].uv = (u * mpp, v * mpp)
        return allf

    # ---- primitives -------------------------------------------------------------------------------
    def box(self, x0, x1, y0, y1, z0, z1, mat, bevel=0.0, segs=2, xf=None):
        v = [(x0, y0, z0), (x1, y0, z0), (x1, y1, z0), (x0, y1, z0), (x0, y0, z1), (x1, y0, z1), (x1, y1, z1), (x0, y1, z1)]
        f = [(0, 3, 2, 1), (4, 5, 6, 7), (0, 1, 5, 4), (1, 2, 6, 5), (2, 3, 7, 6), (3, 0, 4, 7)]
        return self.part(v, f, mat, bevel, segs, xf=xf)

    def loft(self, sections, mat, bevel=0.0, segs=2, caps=True, cyl=None, xf=None, angle=35.0):
        """sections: list of equal-length rings of (x,y,z); consecutive rings are joined by quads."""
        n = len(sections[0])
        verts = [p for s in sections for p in s]
        faces = []
        for i in range(len(sections) - 1):
            for j in range(n):
                k = (j + 1) % n
                faces.append((i * n + j, i * n + k, (i + 1) * n + k, (i + 1) * n + j))
        if caps:
            faces.append(tuple(range(n))[::-1])
            faces.append(tuple((len(sections) - 1) * n + j for j in range(n)))
        return self.part(verts, faces, mat, bevel, segs, cyl=cyl, xf=xf, angle=angle)

    def tube(self, p0, p1, r0, r1, mat, n=16, bevel=0.0, segs=2):
        """Solid (tapered) cylinder between two points."""
        p0 = Vector(p0)
        p1 = Vector(p1)
        d = (p1 - p0).normalized()
        h = Vector((0, 0, 1))
        if abs(d.dot(h)) > 0.95:
            h = Vector((1, 0, 0))
        a = d.cross(h).normalized()
        b = d.cross(a).normalized()

        def ring(p, r):
            return [tuple(p + r * (math.cos(2 * math.pi * i / n) * a + math.sin(2 * math.pi * i / n) * b)) for i in range(n)]
        cyl = None
        for nm, vec in (('x', (1, 0, 0)), ('y', (0, 1, 0)), ('z', (0, 0, 1))):
            if abs(abs(d.dot(Vector(vec))) - 1) < 1e-6:
                cyl = (nm, p0)
        return self.loft([ring(p0, r0), ring(p1, r1)], mat, bevel, segs, cyl=cyl)

    def lathe_z(self, profile, mat, n=24, center=(0, 0), bevel=0.0, segs=2):
        """Solid of revolution about Z. profile = [(r, z)...] bottom to top; r == 0 allowed at the ends."""
        cx, cy = center
        v = []
        rings = []
        for (r, z) in profile:
            if r < 1e-9:
                rings.append(('pt', len(v)))
                v.append((cx, cy, z))
            else:
                rings.append(('ring', len(v)))
                for i in range(n):
                    a = 2 * math.pi * i / n
                    v.append((cx + r * math.cos(a), cy + r * math.sin(a), z))
        f = []
        for k in range(len(rings) - 1):
            (ta, ia), (tb, ib) = rings[k], rings[k + 1]
            for i in range(n):
                j = (i + 1) % n
                if ta == 'ring' and tb == 'ring':
                    f.append((ia + i, ia + j, ib + j, ib + i))
                elif ta == 'pt':
                    f.append((ia, ib + j, ib + i))
                else:
                    f.append((ia + i, ia + j, ib))
        if rings[0][0] == 'ring':
            f.append(tuple(rings[0][1] + i for i in range(n))[::-1])
        if rings[-1][0] == 'ring':
            f.append(tuple(rings[-1][1] + i for i in range(n)))
        return self.part(v, f, mat, bevel, segs, cyl=('z', Vector((cx, cy, 0))))

    def ring_tube(self, centre, z0, z1, r_out, r_in, mat, n=24, bevel=0.0, segs=2):
        """Annulus (hollow cylinder) along Z, closed shell."""
        cx, cy = centre
        v = []
        for z in (z0, z1):
            for r in (r_out, r_in):
                for i in range(n):
                    a = 2 * math.pi * i / n
                    v.append((cx + r * math.cos(a), cy + r * math.sin(a), z))

        def ix(zi, ri, i):
            return (zi * 2 + ri) * n + (i % n)
        f = []
        for i in range(n):
            f.append((ix(0, 0, i), ix(0, 0, i + 1), ix(1, 0, i + 1), ix(1, 0, i)))
            f.append((ix(0, 1, i), ix(1, 1, i), ix(1, 1, i + 1), ix(0, 1, i + 1)))
            f.append((ix(1, 0, i), ix(1, 0, i + 1), ix(1, 1, i + 1), ix(1, 1, i)))
            f.append((ix(0, 0, i), ix(0, 1, i), ix(0, 1, i + 1), ix(0, 0, i + 1)))
        return self.part(v, f, mat, bevel, segs, cyl=('z', Vector((cx, cy, 0))))

    # ---- finish -----------------------------------------------------------------------------------
    def to_object(self, sharp_angle=35.0):
        bm = self.bm
        bmesh.ops.triangulate(bm, faces=list(bm.faces), quad_method='BEAUTY', ngon_method='BEAUTY')
        thr = math.radians(sharp_angle)
        for f in bm.faces:
            f.smooth = True
        for e in bm.edges:
            if len(e.link_faces) == 2:
                try:
                    e.smooth = e.calc_face_angle() <= thr
                except ValueError:
                    pass
        me = bpy.data.meshes.new(self.name)
        bm.to_mesh(me)
        bm.free()
        for s in self.slots:
            me.materials.append(get_material(s))
        ob = bpy.data.objects.new(self.name, me)
        bpy.context.scene.collection.objects.link(ob)
        return ob


def stats(ob):
    me = ob.data
    me.calc_loop_triangles()
    tris = len(me.loop_triangles)
    vs = [v.co for v in me.vertices]
    mn = [min(v[i] for v in vs) for i in range(3)]
    mx = [max(v[i] for v in vs) for i in range(3)]
    return {"tris": tris, "bbox_min": [round(x, 4) for x in mn], "bbox_max": [round(x, 4) for x in mx],
            "dims": [round(mx[i] - mn[i], 4) for i in range(3)]}


def open_edges(ob):
    bm = bmesh.new()
    bm.from_mesh(ob.data)
    bad = sum(1 for e in bm.edges if len(e.link_faces) == 1)
    bm.free()
    return bad


def bbox_proxy(name, mn, mx, mats):
    """Fallback proxy: the bounding box (12 tris)."""
    b = Builder(name)
    b.box(mn[0], mx[0], mn[1], mx[1], mn[2], mx[2], mats[0])
    return b.to_object()


def make_proxy(ob, name, target_tris, bbox_tol=0.08):
    """Decimated copy of ob (separate mesh data); bbox box if decimation collapses the bounds."""
    s0 = stats(ob)
    dup = ob.copy()
    dup.data = ob.data.copy()
    dup.name = name
    dup.data.name = name
    bpy.context.scene.collection.objects.link(dup)
    mod = dup.modifiers.new("dec", 'DECIMATE')
    mod.decimate_type = 'COLLAPSE'
    mod.ratio = max(min(target_tris / max(s0["tris"], 1), 1.0), 0.002)
    dg = bpy.context.evaluated_depsgraph_get()
    me = bpy.data.meshes.new_from_object(dup.evaluated_get(dg))
    old = dup.data
    dup.modifiers.remove(mod)
    dup.data = me
    me.name = name
    bpy.data.meshes.remove(old)
    s1 = stats(dup)
    ok = all(abs(s1["dims"][i] - s0["dims"][i]) <= bbox_tol * max(s0["dims"]) for i in range(3)) and s1["tris"] >= 8
    if not ok:
        bpy.data.objects.remove(dup)
        dup = bbox_proxy(name, s0["bbox_min"], s0["bbox_max"], [m.name for m in ob.data.materials])
    return dup


def export_fbx(ob, path):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    for o in bpy.context.scene.objects:
        o.select_set(False)
    ob.select_set(True)
    bpy.context.view_layer.objects.active = ob
    bpy.ops.export_scene.fbx(
        filepath=path, use_selection=True, global_scale=1.0, apply_unit_scale=True,
        apply_scale_options='FBX_SCALE_NONE', axis_forward='-Z', axis_up='Y', object_types={'MESH'},
        use_mesh_modifiers=True, mesh_smooth_type='OFF', add_leaf_bones=False, bake_anim=False,
        use_tspace=False, path_mode='STRIP')
    ob.select_set(False)
