"""City Painter master materials, built from Python with mat_lib.MB (UE 5.8 editor).

Content layout (docs/unreal-architecture.md conventions: one master per surface family, parameters, instances):
  /Game/CityPainter/Materials/M_Ground, MI_Ground, MI_Ground_Floor
  /Game/CityPainter/Materials/M_RoadAsphalt, MI_RoadAsphalt, _New, _Worn, _Patched
  /Game/CityPainter/Materials/M_Kerb, MI_Kerb;  M_Footway, MI_Footway;  M_Marking, MI_Marking_<colour>
Textures are the Poly Haven sets already imported by import_textures.py (/Game/Materials/Surfaces/<slot>/T_*).

Vertex data contract (sim_core/include/cp/render_mesh.h; the plugin passes colour and UV1 through since 11 Oct 2026):
  colour R = wear amplitude, G = edge dirt, B = low-frequency variation, A reserved.
  UV0 metres; UV1.x = metres from the lane centre (+ left of travel), UV1.y = lane width (0 where not a driving lane).
All world-space texture coordinates are in metres (WorldPosition / 100). Rebuilt from scratch on every run (idempotent).
"""
import unreal

import mat_lib as ml

FOLDER = "/Game/CityPainter/Materials"
SURF = "/Game/Materials/Surfaces"


def T(slot, asset, kind):
    return "%s/%s/T_%s_%s" % (SURF, slot, asset, kind)


REPORT = []


def _report(mb):
    REPORT.append("%s: %d nodes, %d texture samples, %d noise nodes, %s" % (
        mb.name, mb.stats["nodes"], mb.stats["tex"], mb.stats["noise"], mb.stats.get("instr", "")))


# ======================================================================================================== ground
def build_ground():
    """M_Ground: grass/soil with two-scale anti-tiling, field-scale tint patches, soil patches and vertex-G soil.
    STAND-IN: a flat ground plane (CP_Floor) and the sim's verge meshes use this until terrain from grading is rendered."""
    mb = ml.MB("M_Ground", FOLDER)
    uv = mb.world_m_xy()
    G = "Ground"
    tile_g = mb.sp("TileMetres", 2.0, G)
    rot = 37.0
    scale_b = mb.sp("Tile2Scale", 0.55, G)
    blend_period = 27.0
    nb = mb.noise_m(uv, blend_period)
    blend = mb.smooth(0.3, 0.7, nb)

    diff = mb.antitile("GrassDiffuse", T("grass", "sparse_grass", "diff"), uv, tile_g, rot, scale_b, blend, "color")
    nor = mb.antitile("GrassNormal", T("grass", "sparse_grass", "nor_dx"), uv, tile_g, rot, scale_b, blend, "normal")
    arm = mb.antitile("GrassARM", T("grass", "sparse_grass", "arm"), uv, tile_g, rot, scale_b, blend, "mask")
    soil = mb.tex("SoilDiffuse", T("soil", "forest_ground_06", "diff"), mb.div(uv, 2.11), "color")

    # field-scale patchwork (a stand-in for fields: sharp contours of a very low frequency noise, 3 tone levels)
    field_period = mb.sp("FieldPeriodM", 190.0, G)
    f1 = mb.op("Noise", [("", mb.xyz(mb.div(uv, field_period)))], scale=1.0,
               noise_function=unreal.NoiseFunction.NOISEFUNCTION_GRADIENT_ALU, levels=2, level_scale=2.0,
               output_min=0.0, output_max=1.0, quality=1)
    mb.stats["noise"] += 1
    f2 = mb.op("Noise", [("", mb.xyz(mb.add(mb.div(uv, field_period), (31.0, 17.0))))], scale=1.0,
               noise_function=unreal.NoiseFunction.NOISEFUNCTION_GRADIENT_ALU, levels=2, level_scale=2.0,
               output_min=0.0, output_max=1.0, quality=1)
    mb.stats["noise"] += 1
    level1 = mb.div(mb.floor(mb.mul(f1, 3.0)), 2.0)   # 0, .5, 1 (a little above 1 rarely: clamp)
    level1 = mb.sat(level1)
    level2 = mb.sat(mb.div(mb.floor(mb.mul(f2, 3.0)), 2.0))
    macro = mb.smooth(0.35, 0.65, mb.noise_m(uv, 43.0, levels=2))

    dry_amount = mb.sp("DryAmount", 0.55, G)
    dry_mix = mb.sat(mb.mul(dry_amount, mb.add(mb.mul(level1, 0.6), mb.mul(macro, 0.4))))
    tint = mb.lerp(mb.vp("GrassTint", (0.85, 1.25, 0.72), G), mb.vp("DryTint", (1.7, 1.65, 1.2), G), dry_mix)
    contrast = mb.sp("FieldContrast", 0.22, G)
    bright = mb.lerp(mb.sub(1.0, contrast), mb.add(1.0, contrast), level2)
    grass_col = mb.mul(mb.mul(mb.desat((diff, ""), mb.sp("GrassDesaturate", 0.85, G)), tint), bright)  # texture mean (0.082,0.050,0.009) is brown: neutralise first, then tint green-grey

    soil_amount = mb.sp("SoilAmount", 0.45, G)
    soil_noise = mb.smooth(0.6, 0.78, mb.noise_m(uv, 7.0, levels=2, offset=5.0))
    vdirt = mb.sp("VertexDirtWeight", 1.0, G)
    g_vc = mb.vcol("g")
    soil_mask = mb.sat(mb.add(mb.mul(soil_noise, soil_amount), mb.mul(mb.smooth(0.0, 0.6, g_vc), vdirt)))
    albedo = mb.lerp(grass_col, mb.mul(mb.desat((soil, "RGB"), mb.sp("SoilDesaturate", 0.75, G)), 0.85), soil_mask)  # grey-brown dirt, not orange

    nstr = mb.sp("NormalStrength", 1.0, G)
    normal = mb.flatten((nor, ""), mb.sub(1.0, nstr))
    rough = mb.clamp(mb.mask(arm, "g"), 0.75, 1.0)
    mb.out("MP_BASE_COLOR", albedo)
    mb.out("MP_NORMAL", normal)
    mb.out("MP_ROUGHNESS", rough)
    mb.out("MP_AMBIENT_OCCLUSION", mb.lerp(1.0, mb.mask(arm, "r"), 0.7))
    mb.finish()
    _report(mb)
    m = mb.m
    inst = {
        "ground": ml.make_instance("MI_Ground", FOLDER, m, {"VertexDirtWeight": 1.0}),
        "floor": ml.make_instance("MI_Ground_Floor", FOLDER, m, {"VertexDirtWeight": 0.0}),
    }
    return inst


# ======================================================================================================== driver
def _asphalt():
    import mat_defs_road
    return mat_defs_road.build_asphalt(_report)


def _kerb():
    import mat_defs_other
    return mat_defs_other.build_kerb(_report)


def _footway():
    import mat_defs_other
    return mat_defs_other.build_footway(_report)


BUILDERS = {"ground": build_ground, "asphalt": _asphalt, "kerb": _kerb, "footway": _footway}
DEFAULT_MARKING_COLOURS = {"white": (0.90, 0.90, 0.88), "yellow": (0.93, 0.78, 0.15)}
MARKING = "marking"   # built separately: needs the colour table (see build())


def build(names=None, marking_colours=None):
    """Build the named masters (default: all defined). Returns the report lines (nodes / samples / instructions)."""
    del REPORT[:]
    names = list(names or (list(BUILDERS) + [MARKING]))
    for n in names:
        if n == MARKING:
            import mat_defs_other
            mat_defs_other.build_marking(_report, marking_colours or DEFAULT_MARKING_COLOURS)
        elif n in BUILDERS:
            BUILDERS[n]()
        else:
            raise ml.MatError("unknown material group '%s' (have: %s, %s)" % (n, ", ".join(BUILDERS), MARKING))
    return list(REPORT)
