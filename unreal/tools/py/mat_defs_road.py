"""Road-surface masters for mat_defs.py (asphalt, kerb, footway, markings). Imported by mat_defs; split out for size.

Vertex data contract: see mat_defs.py. All world-space coordinates are metres (WorldPosition / 100).
"""
import unreal

import mat_lib as ml

FOLDER = "/Game/CityPainter/Materials"
SURF = "/Game/Materials/Surfaces"


def T(slot, asset, kind):
    return "%s/%s/T_%s_%s" % (SURF, slot, asset, kind)


def nz(mb, uv, period, offset=(0.0, 0.0), levels=1, fn="GRADIENT_ALU"):
    """Noise 0..1 over world metres, feature period in metres (number or scalar-parameter expression)."""
    p = mb.div(mb.add(uv, offset) if offset != (0.0, 0.0) else uv, period)
    return mb.noise(mb.xyz(p), 1.0, fn=fn, levels=levels)


# ======================================================================================================== asphalt
def build_asphalt(report):
    """M_RoadAsphalt: see docs/progress/2026-10-11-unreal-second/REVIEW.md section 4 (revised). Parameters by group:
    Textures: TileBaseM, TileWornM, Tile2Scale, BlendPeriodM.   Colour: Desaturate, CoolTint, AlbedoScale, MacroStrength,
    MacroPeriodM, VariationStrength.   Tracks: TrackCentreM, TrackWidthM, TrackDarken, TrackRoughDelta, TrackFlatten,
    CentreBrighten, CentreRoughAdd.   Wear: WearStrength, WearBias, WornBlendStrength, CrackSealAmount, CrackDensity,
    CrackThreshold, PatchAmount, PatchCellM, PatchAlbedoDelta, PatchRoughDelta.   Edge: EdgeDirtDarken, EdgeDirtRoughAdd,
    EdgeRagged, FringeAmount, FringeThreshold, FringeGravelScale, FringeTint.   Surface: RoughMin, RoughMax,
    NormalStrength, OilAmount (0: no oil in the reference photos).
    Vertex data: R wear amplitude, G edge dirt, B variation; UV1.x metres from lane centre, UV1.y lane width (0 = not a lane).
    NOTE (differs from the review text): UV1.x is METRES from the lane centre (not 0..1): the wheel tracks sit at |x| = 0.9."""
    mb = ml.MB("M_RoadAsphalt", FOLDER)
    uv = mb.world_m_xy()
    uv1 = mb.texcoord(1)
    u1x, u1y = mb.mask(uv1, "r"), mb.mask(uv1, "g")
    vr, vg, vb = mb.vcol("r"), mb.vcol("g"), mb.vcol("b")
    sp = mb.sp

    tile_base = sp("TileBaseM", 2.08, "Textures")
    tile_worn = sp("TileWornM", 3.0, "Textures")
    tile2 = sp("Tile2Scale", 0.73, "Textures")
    blend = mb.smooth(0.3, 0.7, nz(mb, uv, sp("BlendPeriodM", 27.0, "Textures")))

    # base layer (asphalt_01), two scales / rotations
    base = mb.antitile("BaseDiffuse", T("asphalt_new", "asphalt_01", "diff"), uv, tile_base, 37.0, tile2, blend, "color")
    nor1 = mb.antitile("BaseNormal", T("asphalt_new", "asphalt_01", "nor_dx"), uv, tile_base, 37.0, tile2, blend, "normal")
    arm1 = mb.antitile("BaseARM", T("asphalt_new", "asphalt_01", "arm"), uv, tile_base, 37.0, tile2, blend, "mask")
    # worn / crack layer (asphalt_02), one sample each
    uv_w = mb.div(uv, tile_worn)
    worn = mb.tex("WornDiffuse", T("asphalt_worn", "asphalt_02", "diff"), uv_w, "color")
    worn_n = mb.tex("WornNormal", T("asphalt_worn", "asphalt_02", "nor_dx"), uv_w, "normal")
    worn_h = mb.tex("WornHeight", T("asphalt_worn", "asphalt_02", "disp"), uv_w, "mask")
    gravel = mb.tex("GravelDiffuse", T("gravel", "gravel_floor_02", "diff"), mb.div(uv, 2.0), "color")

    # colour correction: desaturate the brownish Poly Haven tint, push slightly cool
    desat_f = sp("Desaturate", 0.8, "Colour")
    cool = mb.vp("CoolTint", (0.98, 1.0, 1.025), "Colour")
    base_c = mb.mul(mb.desat(base, desat_f), cool)
    worn_c = mb.mul(mb.desat((worn, "RGB"), desat_f), cool)

    # lane signals
    ax = mb.absx(u1x)
    valid = mb.smooth(0.01, 0.2, u1y)
    tc = sp("TrackCentreM", 0.9, "Tracks")
    tw = sp("TrackWidthM", 0.45, "Tracks")
    track = mb.mul(mb.one_minus(mb.smooth(0.0, tw, mb.absx(mb.sub(ax, tc)))), valid)
    centre = mb.mul(mb.one_minus(mb.smooth(0.05, 0.5, ax)), valid)

    # wear signal: R amplitude (x wheel-path pattern on lanes) + bias
    wear_lane = mb.lerp(1.0, mb.add(0.35, mb.mul(track, 0.65)), valid)
    wear = mb.sat(mb.add(mb.mul(mb.mul(vr, sp("WearStrength", 1.0, "Wear")), wear_lane), sp("WearBias", 0.0, "Wear")))
    macro_n = nz(mb, uv, sp("MacroPeriodM", 55.0, "Colour"), (11.0, 5.0), levels=2)
    worn_blend = mb.sat(mb.mul(mb.mul(mb.smooth(0.15, 0.65, wear), sp("WornBlendStrength", 0.4, "Wear")),
                               mb.lerp(0.6, 1.0, macro_n)))
    alb = mb.lerp(base_c, worn_c, worn_blend)

    # tone: macro noise + vertex B, then tracks (darker) / centre strip (paler)
    macro_s = sp("MacroStrength", 0.15, "Colour")
    macro_tone = mb.lerp(mb.sub(1.0, macro_s), mb.add(1.0, macro_s), macro_n)
    var_tone = mb.add(1.0, mb.mul(mb.mul(mb.sub(vb, 0.5), 2.0), sp("VariationStrength", 0.30, "Colour")))
    alb = mb.mul(mb.mul(mb.mul(alb, sp("AlbedoScale", 0.75, "Colour")), macro_tone), var_tone)
    alb = mb.mul(alb, mb.sub(1.0, mb.mul(track, sp("TrackDarken", 0.22, "Tracks"))))
    alb = mb.mul(alb, mb.add(1.0, mb.mul(centre, sp("CentreBrighten", 0.04, "Tracks"))))

    # patches: sparse round repair patches (sharp edge), per-cell hash
    cell = sp("PatchCellM", 6.0, "Wear")
    h_exist = mb.hash_cell(uv, cell, 0.0)
    h_a = mb.hash_cell(uv, cell, 1.0)
    h_b = mb.hash_cell(uv, cell, 2.0)
    p_cell = mb.div(sp("PatchAmount", 0.07, "Wear"), 0.28)           # a circle of radius ~0.3 cell covers ~28% of its cell
    exist = mb.op("Step", [("Y", mb.sub(1.0, p_cell)), ("X", h_exist)])
    cf = mb.sub(mb.frac(mb.div(uv, cell)), (0.5, 0.5))
    jitter = mb.mul(mb.vec2(mb.sub(h_a, 0.5), mb.sub(h_b, 0.5)), 0.3)
    dist = mb.op("Distance", [("A", cf), ("B", jitter)])
    radius = mb.add(mb.add(0.14, mb.mul(h_b, 0.16)), mb.mul(mb.sub(nz(mb, uv, 0.9, (3.0, 9.0)), 0.5), 0.06))
    patch = mb.mul(exist, mb.op("Step", [("Y", dist), ("X", radius)]))
    tone = mb.sub(mb.mul(h_a, 2.0), 1.0)
    alb = mb.mul(alb, mb.add(1.0, mb.mul(mb.mul(patch, tone), sp("PatchAlbedoDelta", 0.10, "Wear"))))

    # crack seals: dark thin lines from the asphalt_02 height map, only in sparse regions
    cth = sp("CrackThreshold", 0.3, "Wear")
    crack = mb.one_minus(mb.smooth(mb.sub(cth, 0.07), cth, mb.mask((worn_h, "RGB"), "r")))
    cd = sp("CrackDensity", 0.62, "Wear")
    gate = mb.smooth(cd, mb.add(cd, 0.1), nz(mb, uv, 21.0, (90.0, 40.0), levels=2))
    crack_m = mb.mul(mb.mul(crack, gate), sp("CrackSealAmount", 0.25, "Wear"))
    alb = mb.mul(alb, mb.sub(1.0, mb.mul(crack_m, 0.55)))

    # edge dirt (G): ragged darkening, then a gravel / soil fringe
    n_hi = nz(mb, uv, 0.35, (7.0, 3.0), levels=3)
    rag = sp("EdgeRagged", 0.4, "Edge")
    dirt = mb.smooth(0.08, 0.55, mb.add(vg, mb.mul(mb.sub(n_hi, 0.5), rag)))
    alb = mb.mul(alb, mb.sub(1.0, mb.mul(dirt, sp("EdgeDirtDarken", 0.28, "Edge"))))
    n_mid = nz(mb, uv, 1.6, (40.0, 12.0), levels=2)
    fth = sp("FringeThreshold", 0.88, "Edge")
    fringe = mb.mul(mb.smooth(mb.sub(fth, 0.04), mb.add(fth, 0.06), mb.add(vg, mb.mul(mb.sub(n_mid, 0.5), 0.3))),
                    sp("FringeAmount", 0.25, "Edge"))
    fringe_c = mb.mul(mb.mul(mb.desat((gravel, "RGB"), 0.6), sp("FringeGravelScale", 0.25, "Edge")),
                      mb.vp("FringeTint", (0.9, 0.88, 0.82), "Edge"))
    alb = mb.lerp(alb, fringe_c, fringe)

    # oil (default 0)
    oil = mb.mul(mb.mul(mb.smooth(0.62, 0.7, nz(mb, uv, 1.7, (60.0, 20.0), levels=2)),
                        mb.one_minus(mb.smooth(0.1, 0.4, ax))), mb.mul(valid, sp("OilAmount", 0.0, "Surface")))
    alb = mb.mul(alb, mb.lerp(1.0, 0.6, oil))

    # roughness
    r_tex = mb.sat(mb.mask(arm1, "g"))
    rough = mb.lerp(sp("RoughMin", 0.66, "Surface"), sp("RoughMax", 0.9, "Surface"), r_tex)
    rough = mb.add(rough, mb.mul(centre, sp("CentreRoughAdd", 0.05, "Tracks")))
    rough = mb.add(rough, mb.mul(track, sp("TrackRoughDelta", -0.15, "Tracks")))
    rough = mb.add(rough, mb.mul(dirt, sp("EdgeDirtRoughAdd", 0.12, "Edge")))
    rough = mb.add(rough, mb.mul(patch, sp("PatchRoughDelta", 0.08, "Wear")))
    rough = mb.add(rough, mb.mul(worn_blend, 0.05))
    rough = mb.lerp(rough, 0.95, fringe)
    rough = mb.clamp(mb.lerp(rough, 0.25, oil), 0.25, 1.0)

    # normal: base (flatter in tracks) blended toward the worn normal
    n_base = mb.flatten(nor1, mb.mul(track, sp("TrackFlatten", 0.3, "Tracks")))
    n_mix = mb.normalize(mb.lerp(n_base, (worn_n, "RGB"), mb.mul(worn_blend, 0.7)))
    normal = mb.flatten(n_mix, mb.sub(1.0, sp("NormalStrength", 1.0, "Surface")))
    ao = mb.lerp(1.0, mb.mask(arm1, "r"), 0.8)

    mb.out("MP_BASE_COLOR", alb)
    mb.out("MP_NORMAL", normal)
    mb.out("MP_ROUGHNESS", rough)
    mb.out("MP_AMBIENT_OCCLUSION", ao)
    mb.finish()
    report(mb)
    m = mb.m
    return {
        "default": ml.make_instance("MI_RoadAsphalt", FOLDER, m),
        "new": ml.make_instance("MI_RoadAsphalt_New", FOLDER, m, {
            "AlbedoScale": 0.65, "PatchAmount": 0.02, "CrackSealAmount": 0.15, "WornBlendStrength": 0.3, "VariationStrength": 0.2}),
        "worn": ml.make_instance("MI_RoadAsphalt_Worn", FOLDER, m, {
            "AlbedoScale": 0.95, "WearBias": 0.25, "PatchAmount": 0.10, "CrackSealAmount": 0.7, "WornBlendStrength": 0.9}),
        "patched": ml.make_instance("MI_RoadAsphalt_Patched", FOLDER, m, {
            "AlbedoScale": 0.85, "WearBias": 0.25, "PatchAmount": 0.35, "CrackSealAmount": 0.9}),
    }
