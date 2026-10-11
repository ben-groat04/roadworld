"""Tiny node-graph builder on top of unreal.MaterialEditingLibrary (UE 5.8 editor Python).

Why: a master material is ~100 nodes; writing them as create/connect pairs is unreadable. `MB` wraps one material and
offers expression-style helpers (`mul`, `lerp`, `smooth`, `tex`, `noise`, ...). A "value" is an expression, an
(expression, output_name) tuple, or a python number / 3-tuple (auto constant). Every failed connection raises with
the node class and pin name, so a wrong pin name is found on the first run instead of producing a silent dead graph.

Verified in 5.8 (probe 11 Oct 2026): enums such as NoiseFunction / TextureMipValueMode are exposed to Python; the
Custom (HLSL) expression's inputs are NOT (FCustomInput is a plain USTRUCT), so no Custom nodes here; pixel depth
offset is reachable through MakeMaterialAttributes (pin "PixelDepthOffset") + MP_MATERIAL_ATTRIBUTES.
"""
import math

import unreal


class MatError(Exception):
    pass


def tex_asset(path):
    t = unreal.load_asset(path)
    if t is None:
        raise MatError("texture not found: " + path)
    return t


class MB:
    def __init__(self, name, folder, blend=None, two_sided=False):
        self.mel = unreal.MaterialEditingLibrary
        self.name = name
        self.folder = folder
        full = "%s/%s" % (folder, name)
        eal = unreal.EditorAssetLibrary
        if eal.does_asset_exist(full):
            eal.delete_asset(full)
        tools = unreal.AssetToolsHelpers.get_asset_tools()
        self.m = tools.create_asset(name, folder, unreal.Material, unreal.MaterialFactoryNew())
        if self.m is None:
            raise MatError("could not create material " + full)
        if blend is not None:
            self.m.set_editor_property("blend_mode", blend)
        if two_sided:
            self.m.set_editor_property("two_sided", True)
        self.i = 0
        self._const = {}
        self.stats = {"tex": 0, "noise": 0, "nodes": 0}

    # ---- node plumbing -------------------------------------------------
    def node(self, cls, **props):
        x = -3200 + (self.i % 16) * 200
        y = -1200 + (self.i // 16) * 190
        self.i += 1
        self.stats["nodes"] += 1
        e = self.mel.create_material_expression(self.m, getattr(unreal, "MaterialExpression" + cls), x, y)
        if e is None:
            raise MatError("create_material_expression failed: " + cls)
        for k, v in props.items():
            e.set_editor_property(k, v)
        return e

    def _src(self, v):
        if isinstance(v, (int, float)):
            key = float(v)
            if key not in self._const:
                self._const[key] = self.node("Constant", r=key)
            return self._const[key], ""
        if isinstance(v, tuple) and len(v) == 3 and all(isinstance(a, (int, float)) for a in v):
            return self.node("Constant3Vector", constant=unreal.LinearColor(v[0], v[1], v[2], 1.0)), ""
        if isinstance(v, tuple) and len(v) == 2 and all(isinstance(a, (int, float)) for a in v):
            return self.node("Constant2Vector", r=float(v[0]), g=float(v[1])), ""
        if isinstance(v, tuple):
            return v
        return v, ""

    def wire(self, src, dst, pin):
        e, out = self._src(src)
        if not self.mel.connect_material_expressions(e, out, dst, pin):
            raise MatError("connect failed: %s(%s) -> %s.%s" % (type(e).__name__, out, type(dst).__name__, pin))

    def op(self, cls, pins, **props):
        n = self.node(cls, **props)
        for pin, v in pins:
            self.wire(v, n, pin)
        return n

    # ---- parameters ----------------------------------------------------
    def sp(self, name, default, group=""):
        n = self.node("ScalarParameter", parameter_name=name, default_value=float(default))
        if group:
            n.set_editor_property("group", group)
        return n

    def vp(self, name, rgb, group=""):
        n = self.node("VectorParameter", parameter_name=name, default_value=unreal.LinearColor(rgb[0], rgb[1], rgb[2], 1.0))
        if group:
            n.set_editor_property("group", group)
        return n

    def tex(self, name, path, uv, kind="color", group="Textures", mip_level=None):
        """TextureSampleParameter2D. kind: color | normal | mask. Returns the expression (output "RGB" = colour)."""
        st = {"color": "SAMPLERTYPE_COLOR", "normal": "SAMPLERTYPE_NORMAL", "mask": "SAMPLERTYPE_MASKS"}[kind]
        n = self.node("TextureSampleParameter2D", parameter_name=name, texture=tex_asset(path),
                      sampler_type=getattr(unreal.MaterialSamplerType, st))
        n.set_editor_property("group", group)
        self.wire(uv, n, "UVs")
        self.stats["tex"] += 1
        return n

    # ---- math ----------------------------------------------------------
    def add(self, a, b): return self.op("Add", [("A", a), ("B", b)])
    def sub(self, a, b): return self.op("Subtract", [("A", a), ("B", b)])
    def mul(self, a, b): return self.op("Multiply", [("A", a), ("B", b)])
    def div(self, a, b): return self.op("Divide", [("A", a), ("B", b)])
    def lerp(self, a, b, t): return self.op("LinearInterpolate", [("A", a), ("B", b), ("Alpha", t)])
    def sat(self, a): return self.op("Saturate", [("", a)])
    def one_minus(self, a): return self.op("OneMinus", [("", a)])
    def absx(self, a): return self.op("Abs", [("", a)])
    def frac(self, a): return self.op("Frac", [("", a)])
    def floor(self, a): return self.op("Floor", [("", a)])
    def sine(self, a): return self.op("Sine", [("", a)], period=1.0)
    def cosine(self, a): return self.op("Cosine", [("", a)], period=1.0)
    def pw(self, a, e): return self.op("Power", [("Base", a), ("Exponent", e)])
    def maxx(self, a, b): return self.op("Max", [("A", a), ("B", b)])
    def minx(self, a, b): return self.op("Min", [("A", a), ("B", b)])
    def clamp(self, a, lo, hi): return self.op("Clamp", [("", a), ("Min", lo), ("Max", hi)])
    def smooth(self, lo, hi, v): return self.op("SmoothStep", [("Min", lo), ("Max", hi), ("Value", v)])
    def normalize(self, a): return self.op("Normalize", [("VectorInput", a)])
    def desat(self, rgb, frac): return self.op("Desaturation", [("", rgb), ("Fraction", frac)])
    def dot(self, a, b): return self.op("DotProduct", [("A", a), ("B", b)])
    def vec2(self, a, b): return self.op("AppendVector", [("A", a), ("B", b)])
    def vec3(self, a, b, c): return self.op("AppendVector", [("A", self.vec2(a, b)), ("B", c)])

    def mask(self, v, ch):
        return self.op("ComponentMask", [("", v)], r="r" in ch, g="g" in ch, b="b" in ch, a="a" in ch)

    def vcol(self, ch):
        return self.mask(self.node("VertexColor"), ch)

    def texcoord(self, index):
        return self.node("TextureCoordinate", coordinate_index=index)

    def world_m_xy(self):
        """Absolute world XY in metres (float2)."""
        wp = self.node("WorldPosition")
        return self.div(self.mask(wp, "rg"), 100.0)

    def rot_uv(self, uv, deg):
        c, s = math.cos(math.radians(deg)), math.sin(math.radians(deg))
        u, v = self.mask(uv, "r"), self.mask(uv, "g")
        return self.vec2(self.sub(self.mul(u, c), self.mul(v, s)), self.add(self.mul(u, s), self.mul(v, c)))

    def noise(self, pos3, scale, fn="GRADIENT_ALU", levels=1, level_scale=2.0, out_min=0.0, out_max=1.0, turbulence=False):
        """Noise node; pos3 is a float3 (world metres); scale = cycles per unit of pos3. Output remapped to [out_min,out_max]."""
        f = getattr(unreal.NoiseFunction, "NOISEFUNCTION_" + fn)
        self.stats["noise"] += 1
        return self.op("Noise", [("", pos3)], scale=float(scale), noise_function=f, levels=int(levels),
                       level_scale=float(level_scale), output_min=float(out_min), output_max=float(out_max),
                       turbulence=turbulence, quality=1)

    def xyz(self, xy, z=0.0):
        return self.vec3(self.mask(xy, "r"), self.mask(xy, "g"), z)

    def noise_m(self, xy_m, period_m, fn="GRADIENT_ALU", levels=1, offset=0.0, **kw):
        """Noise over world metres with a feature period in metres."""
        p = self.xyz(xy_m if not offset else self.add(xy_m, (offset, offset * 0.7)))
        return self.noise(p, 1.0 / period_m, fn=fn, levels=levels, **kw)

    def hash_cell(self, xy_m, cell_m, salt=0.0):
        """Per-cell pseudo-random 0..1: value noise evaluated on the integer lattice (cell index)."""
        cell = self.floor(self.div(xy_m, cell_m))
        p = self.xyz(self.add(cell, (salt * 13.7, salt * 7.3)))
        return self.noise(p, 1.0, fn="VALUE_ALU", levels=1)

    def antitile(self, name, path, uv_m, size_m, rot_deg, scale_b, blend, kind="color", group="Textures"):
        """Two samples of one texture (A: metres/size; B: rotated, different scale) blended by `blend` (0..1).
        Returns (mixed_rgb_expr, blendedtuple). Both samples share the texture parameter `name`."""
        uvA = self.div(uv_m, size_m)
        uvB = self.mul(self.rot_uv(uv_m, rot_deg), self.div(scale_b, size_m))
        a = self.tex(name, path, uvA, kind, group)
        b = self.tex(name, path, uvB, kind, group)
        return self.lerp((a, "RGB"), (b, "RGB"), blend)

    def flatten(self, n, amount):
        return self.normalize(self.lerp(n, (0.0, 0.0, 1.0), amount))

    # ---- outputs -------------------------------------------------------
    def out(self, prop, src):
        e, o = self._src(src)
        if not self.mel.connect_material_property(e, o, getattr(unreal.MaterialProperty, prop)):
            raise MatError("connect_material_property failed: %s" % prop)

    def finish(self):
        self.mel.recompile_material(self.m)
        unreal.EditorAssetLibrary.save_loaded_asset(self.m)
        try:
            ni = self.mel.get_statistics(self.m)
            self.stats["instr"] = "vs=%s ps=%s samplers=%s" % (ni.num_vertex_shader_instructions, ni.num_pixel_shader_instructions, ni.num_samplers)
        except Exception as e:  # noqa
            self.stats["instr"] = "n/a (%s)" % str(e)[:60]
        return self.m


def make_instance(name, folder, parent, scalars=None, vectors=None):
    mel = unreal.MaterialEditingLibrary
    eal = unreal.EditorAssetLibrary
    full = "%s/%s" % (folder, name)
    if eal.does_asset_exist(full):
        eal.delete_asset(full)
    tools = unreal.AssetToolsHelpers.get_asset_tools()
    mi = tools.create_asset(name, folder, unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
    mel.set_material_instance_parent(mi, parent)
    known_s = set(str(n) for n in mel.get_scalar_parameter_names(parent))
    known_v = set(str(n) for n in mel.get_vector_parameter_names(parent))
    for k, v in (scalars or {}).items():
        if k not in known_s:
            raise MatError("%s: parent has no scalar parameter '%s'" % (name, k))
        mel.set_material_instance_scalar_parameter_value(mi, k, float(v))  # always returns False in 5.8 (engine quirk): ignore
    for k, v in (vectors or {}).items():
        if k not in known_v:
            raise MatError("%s: parent has no vector parameter '%s'" % (name, k))
        mel.set_material_instance_vector_parameter_value(mi, k, unreal.LinearColor(v[0], v[1], v[2], 1.0))
    mel.update_material_instance(mi)
    eal.save_loaded_asset(mi)
    return mi
