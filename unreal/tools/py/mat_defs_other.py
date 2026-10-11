"""Kerb, footway and marking masters for mat_defs.py (stage 3). See mat_defs.py for the vertex data contract."""
import unreal

import mat_lib as ml
from mat_defs_road import FOLDER, T, nz


# ======================================================================================================== kerb
def build_kerb(report):
    """M_Kerb: precast concrete kerb, neutral grey (reference photos: grey, ~100 mm upstand), joints every 0.915 m,
    per-unit tone variation, dark grime band at the base of the face. Uses UV0 (kerb faces: u metres along the kerb,
    v metres up the face then across the top). Parameters: TileMetres, Desaturate, AlbedoScale, UnitVariation,
    MacroVariation, JointDarken, BaseGrimeHeightM, BaseGrimeDarken, RoughBase, NormalStrength."""
    mb = ml.MB("M_Kerb", FOLDER)
    sp = mb.sp
    uv0 = mb.texcoord(0)
    uv = mb.world_m_xy()
    tile = sp("TileMetres", 1.805, "Kerb")
    uvt = mb.div(uv0, tile)
    diff = mb.tex("KerbDiffuse", T("kerb_concrete", "concrete_wall_009", "diff"), uvt, "color")
    nor = mb.tex("KerbNormal", T("kerb_concrete", "concrete_wall_009", "nor_dx"), uvt, "normal")
    arm = mb.tex("KerbARM", T("kerb_concrete", "concrete_wall_009", "arm"), uvt, "mask")

    col = mb.mul(mb.desat((diff, "RGB"), sp("Desaturate", 0.6, "Kerb")), sp("AlbedoScale", 0.62, "Kerb"))
    u = mb.mask(uv0, "r")
    v = mb.mask(uv0, "g")
    unit = mb.div(u, 0.915)
    # per-unit tone: lattice noise on (unit index, coarse world cell)
    wx = mb.floor(mb.div(mb.mask(uv, "r"), 37.0))
    wy = mb.floor(mb.div(mb.mask(uv, "g"), 41.0))
    h = mb.noise(mb.vec3(mb.floor(unit), mb.add(wx, mb.mul(wy, 7.0)), 0.0), 1.0, fn="VALUE_ALU")
    col = mb.mul(col, mb.add(1.0, mb.mul(mb.sub(h, 0.5), mb.mul(sp("UnitVariation", 0.10, "Kerb"), 2.0))))
    macro = nz(mb, uv, 19.0, (3.0, 11.0), levels=2)   # long wavelength: 7 m blobs read as leopard print on the kerb tops
    col = mb.mul(col, mb.add(1.0, mb.mul(mb.sub(macro, 0.5), mb.mul(sp("MacroVariation", 0.08, "Kerb"), 2.0))))
    # joints
    f = mb.frac(unit)
    jd = mb.minx(f, mb.sub(1.0, f))
    joint = mb.one_minus(mb.smooth(0.002, 0.006, jd))
    col = mb.mul(col, mb.sub(1.0, mb.mul(joint, sp("JointDarken", 0.45, "Kerb"))))
    # grime band at the base of the face (v = metres up the face), ragged
    rag = nz(mb, uv0, 0.25, (5.0, 2.0), levels=2)
    grime = mb.one_minus(mb.smooth(0.0, mb.mul(sp("BaseGrimeHeightM", 0.045, "Kerb"), mb.add(0.6, rag)), v))
    col = mb.mul(col, mb.sub(1.0, mb.mul(grime, sp("BaseGrimeDarken", 0.55, "Kerb"))))

    rough = mb.clamp(mb.add(mb.lerp(0.7, 0.95, mb.mask(arm, "g")), mb.mul(grime, 0.05)), 0.6, 1.0)
    normal = mb.flatten((nor, ""), mb.sub(1.0, sp("NormalStrength", 1.0, "Kerb")))
    mb.out("MP_BASE_COLOR", col)
    mb.out("MP_NORMAL", normal)
    mb.out("MP_ROUGHNESS", rough)
    mb.out("MP_AMBIENT_OCCLUSION", mb.lerp(1.0, mb.mask(arm, "r"), 0.7))
    mb.finish()
    report(mb)
    return {"kerb": ml.make_instance("MI_Kerb", FOLDER, mb.m)}


# ======================================================================================================== footway
def build_footway(report):
    """M_Footway: concrete flag paving (the Poly Haven concrete_pavement set has flags 600 x 300 mm per 1.8 m tile; the
    default TileMetres 1.5 gives 500 x 250 mm), neutral grey, no rotation anti-tiling (it would shear the slab joints);
    tone variation comes from two noise octaves (4 m, 15 m) and vertex B; dirt blotches. Parameters: TileMetres,
    Desaturate, AlbedoScale, MacroVariation, DirtAmount, RoughBase, NormalStrength.
    The 'paving' surface class is also what the mesh builder assigns to splitter islands, so those read as small
    flags too (real UK splitters are mostly tarmac: a sim-side surface-class change, not done here)."""
    mb = ml.MB("M_Footway", FOLDER)
    sp = mb.sp
    uv = mb.world_m_xy()
    uvt = mb.div(uv, sp("TileMetres", 1.5, "Footway"))
    diff = mb.tex("FlagDiffuse", T("pavement", "concrete_pavement", "diff"), uvt, "color")
    nor = mb.tex("FlagNormal", T("pavement", "concrete_pavement", "nor_dx"), uvt, "normal")
    arm = mb.tex("FlagARM", T("pavement", "concrete_pavement", "arm"), uvt, "mask")
    col = mb.mul(mb.desat((diff, "RGB"), sp("Desaturate", 0.65, "Footway")), mb.mul(sp("AlbedoScale", 0.85, "Footway"), mb.vp("CoolTint", (0.97, 1.0, 1.03), "Footway")))
    m1 = nz(mb, uv, 4.0, (21.0, 8.0), levels=2)
    m2 = nz(mb, uv, 15.0, (5.0, 33.0))
    mv = sp("MacroVariation", 0.07, "Footway")
    col = mb.mul(col, mb.add(1.0, mb.mul(mb.add(mb.sub(m1, 0.5), mb.mul(mb.sub(m2, 0.5), 0.8)), mb.mul(mv, 2.0))))
    col = mb.mul(col, mb.add(1.0, mb.mul(mb.sub(mb.vcol("b"), 0.5), 0.2)))
    dirt = mb.smooth(0.5, 0.95, nz(mb, uv, 2.5, (13.0, 2.0), levels=3))   # softer, larger, weaker than before: no blotches
    col = mb.mul(col, mb.sub(1.0, mb.mul(dirt, sp("DirtAmount", 0.08, "Footway"))))
    rough = mb.clamp(mb.add(mb.lerp(0.75, 0.95, mb.mask(arm, "g")), mb.mul(dirt, 0.04)), 0.6, 1.0)
    normal = mb.flatten((nor, ""), mb.sub(1.0, sp("NormalStrength", 1.0, "Footway")))
    mb.out("MP_BASE_COLOR", col)
    mb.out("MP_NORMAL", normal)
    mb.out("MP_ROUGHNESS", rough)
    mb.out("MP_AMBIENT_OCCLUSION", mb.lerp(1.0, mb.mask(arm, "r"), 0.7))
    mb.finish()
    report(mb)
    return {"footway": ml.make_instance("MI_Footway", FOLDER, mb.m)}


# ======================================================================================================== markings
def build_marking(report, colours):
    """M_Marking (masked, with pixel depth offset through MakeMaterialAttributes: the MP_PixelDepthOffset enum is hidden
    from Python but the MakeMaterialAttributes pin is reachable). Parameters: Color (linear, from the region pack),
    PaintBrightness (0.92: slightly off-white), Erosion (fraction of paint chipped away: default 0.10 = 5-15% main
    roads; ~0.4 for old minor roads), WearInfluence (how much vertex R raises erosion: the sim has no marking wear yet,
    R = 0), ChipScaleM (chip feature size), Dirt (tyre grime darkening), PdoCm (pixel depth offset in cm, negative =
    toward the camera), NormalStrength.
    colours: {colour id: (r, g, b) sRGB 0..1}. Instances MI_Marking_<id>; 'old' variants MI_Marking_<id>_Old (Erosion 0.4)."""
    mb = ml.MB("M_Marking", FOLDER, blend=unreal.BlendMode.BLEND_MASKED)
    sp = mb.sp
    uv = mb.world_m_xy()
    color = mb.vp("Color", (0.79, 0.79, 0.77), "Paint")
    bright = sp("PaintBrightness", 0.85, "Paint")
    # chips: 3-octave noise at ~0.25 m plus a coarser 2 m modulation so wear comes in clusters
    chip_scale = sp("ChipScaleM", 0.22, "Paint")
    n_chip = nz(mb, uv, chip_scale, (17.0, 4.0), levels=3)
    n_clump = nz(mb, uv, 2.2, (50.0, 9.0), levels=2)
    wear = mb.sat(mb.add(mb.mul(mb.vcol("r"), sp("WearInfluence", 0.5, "Paint")), 0.0))
    ero = mb.mul(sp("Erosion", 0.10, "Paint"), mb.add(mb.lerp(0.5, 1.5, n_clump), wear))
    # ero is a target coverage; the noise is ~N(0.5, 0.13) so threshold = 0.76 - 0.52 * coverage (see docstring)
    thr = mb.sub(0.76, mb.mul(mb.clamp(ero, 0.0, 0.8), 0.52))
    chipped = mb.op("Step", [("Y", thr), ("X", n_chip)])
    mask = mb.one_minus(chipped)
    # tyre grime
    dirt = mb.mul(sp("Dirt", 0.18, "Paint"), mb.smooth(0.4, 0.75, nz(mb, uv, 0.6, (5.0, 77.0), levels=2)))
    albedo = mb.mul(mb.mul(color, bright), mb.sub(1.0, dirt))
    # a little asphalt-like bump so paint does not look like a decal
    nor = mb.tex("PaintNormal", T("asphalt_new", "asphalt_01", "nor_dx"), mb.div(uv, 2.08), "normal")
    normal = mb.flatten((nor, ""), mb.sub(1.0, sp("NormalStrength", 0.4, "Paint")))
    rough = mb.add(0.55, mb.mul(mb.sub(n_chip, 0.5), 0.2))
    pdo = sp("PdoCm", -2.0, "Paint")

    mma = mb.node("MakeMaterialAttributes")
    mb.wire(albedo, mma, "BaseColor")
    mb.wire(rough, mma, "Roughness")
    mb.wire(normal, mma, "Normal")
    mb.wire(mask, mma, "OpacityMask")
    mb.wire(pdo, mma, "PixelDepthOffset")
    mb.m.set_editor_property("use_material_attributes", True)
    mb.out("MP_MATERIAL_ATTRIBUTES", mma)
    mb.finish()
    report(mb)
    out = {}
    for cid, rgb in colours.items():
        lin = tuple(c / 12.92 if c <= 0.04045 else ((c + 0.055) / 1.055) ** 2.4 for c in rgb)
        out[cid] = ml.make_instance("MI_Marking_%s" % cid, FOLDER, mb.m, None, {"Color": lin})
        out[cid + "_old"] = ml.make_instance("MI_Marking_%s_Old" % cid, FOLDER, mb.m, {"Erosion": 0.40}, {"Color": lin})
    return out
