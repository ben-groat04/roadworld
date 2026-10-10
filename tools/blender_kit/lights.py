"""Tubular steel street lighting columns with bracket arm(s) and LED lantern.

Local frame: origin on the ground at the column axis (z=0 is the footway/verge surface under the flange); +Z up;
BRACKET POINTS +Y (toward the carriageway); the door of the base compartment faces +X. A twin-arm column
has a second identical arm pointing -Y. `H` is the MOUNTING HEIGHT (luminaire centre above the base), `O` the
OUTREACH (horizontal distance from the column axis to the lantern spigot end of the bracket).
Names: SM_LC_H<height m, 2 digits>_O<outreach in dm, 2 digits>_<S|T>[_PM]; PM = parapet/structure mount
(flat base plate with bolts instead of root flange and compartment door; z=0 at the plate underside).
"""
import math
import common as C
from common import Builder


def _dkey(h):
    return str(int(h))


def build(H, O, twin, pm, L):
    sides = L["sides"]
    rb = L["base_d"][_dkey(H)] / 2
    rt = L["top_d"][_dkey(H)] / 2
    rise = math.radians(L["arm_rise_deg"])
    arm_r = L["arm_d"] / 2
    lw, ll, lh = L["lantern"][1], L["lantern"][0], L["lantern"][2]
    z_end = H - 0.06
    z_a = z_end - O * math.tan(rise)           # where the arm leaves the column
    z_top = z_a + 0.12
    nm = "SM_LC_H%02d_O%02d_%s%s" % (H, round(O * 10), "T" if twin else "S", "_PM" if pm else "")
    b = Builder(nm)

    def r_at(z):
        return rb + (rt - rb) * z / z_top

    if pm:
        pw, pl, pt = L["pm_plate"]
        b.box(-pw / 2, pw / 2, -pl / 2, pl / 2, 0, pt, "galvanised_steel", 0.004)
        for sx in (-1, 1):
            for sy in (-1, 1):
                b.tube((sx * (pw / 2 - 0.035), sy * (pl / 2 - 0.035), pt), (sx * (pw / 2 - 0.035), sy * (pl / 2 - 0.035), pt + 0.018),
                       0.014, 0.012, "galvanised_steel", 6)
        z0 = pt
        prof = [(0.0, z0), (rb + 0.02, z0), (rb + 0.02, z0 + 0.03), (r_at(0.18), z0 + 0.18), (rt, z_top), (rt * 0.9, z_top + 0.02), (0.0, z_top + 0.02)]
        b.lathe_z(prof, "galvanised_steel", sides, bevel=0.003, segs=1)
    else:
        fr = L["flange_d"] / 2
        fh = L["flange_h"]
        prof = [(0.0, 0.0), (fr, 0.0), (fr, fh), (rb + 0.03, fh + 0.01), (rb + 0.012, 0.16), (r_at(0.16), 0.16),
                (rt, z_top), (rt * 0.9, z_top + 0.02), (0.0, z_top + 0.02)]
        b.lathe_z(prof, "galvanised_steel", sides, bevel=0.003, segs=1)
        # base compartment door (raised, faces +X)
        dz0, dh, dw = L["door_z"], L["door_h"], L["door_w"]
        rd = r_at(dz0 + dh / 2) * math.cos(math.pi / sides)
        b.box(rd - 0.012, rd + 0.007, -dw / 2, dw / 2, dz0, dz0 + dh, "galvanised_steel", 0.004)
        for zz in (dz0 + 0.07, dz0 + dh - 0.07):
            b.tube((rd + 0.007, 0, zz), (rd + 0.016, 0, zz), 0.012, 0.010, "galvanised_steel", 6)
        b.box(rd + 0.007, rd + 0.013, -0.012, 0.012, dz0 + dh * 0.5 - 0.03, dz0 + dh * 0.5 + 0.03, "galvanised_steel", 0.002, 1)

    # arm(s) + lantern(s)
    for sgn in ((1, -1) if twin else (1,)):
        y_end = sgn * O
        b.tube((0, 0, z_a - 0.05), (0, 0, z_top + 0.0), 0.047, 0.047, "galvanised_steel", 12)   # sleeve at the shaft top
        b.tube((0, sgn * 0.03, z_a), (0, y_end, z_end), arm_r * 1.15, arm_r, "galvanised_steel", 12)
        yc = y_end + sgn * (ll * 0.5 - 0.18)                    # lantern: spigot end sits 0.18 m behind the arm tip
        y0, y1 = sorted((yc - ll / 2, yc + ll / 2))
        zc = z_end - 0.01 + 0.0
        b.box(-lw / 2, lw / 2, y0, y1, zc - lh * 0.5, zc + lh * 0.5, "painted_steel", 0.045, 4)
        b.box(-lw / 2 + 0.04, lw / 2 - 0.04, y0 + 0.07, y1 - 0.07, zc - lh * 0.5 - 0.006, zc - lh * 0.5 + 0.02, "lantern_glass", 0.003, 1)
        b.box(-0.05, 0.05, min(y_end, y_end - sgn * 0.0) - 0.03, max(y_end, y_end) + 0.03, z_end - 0.04, z_end + 0.045, "painted_steel", 0.01, 2)
    # proxy: 6-sided tapered tube + thin arm box + lantern box
    p = Builder(nm + "_proxy")
    p.tube((0, 0, 0), (0, 0, z_top), rb, rt, "galvanised_steel", 6)
    for sgn in ((1, -1) if twin else (1,)):
        yc = sgn * (O + ll * 0.5 - 0.18)
        y0, y1 = sorted((sgn * 0.0 - 0.04, sgn * O))
        p.box(-0.03, 0.03, y0, y1, z_end - 0.03, z_end + 0.03, "galvanised_steel")
        a, c = sorted((yc - ll / 2, yc + ll / 2))
        p.box(-lw / 2, lw / 2, a, c, z_end - lh / 2, z_end + lh / 2, "painted_steel")
    return b, p, dict(height=H, outreach=O, twin=twin, parapet_mount=pm, shaft_top=round(z_top, 3), arm_z_at_column=round(z_a, 3),
                      arm_z_at_tip=round(z_end, 3))


def pieces():
    L = C.PARAMS["light"]
    out = []
    pivot = "ground level on the column axis; bracket points +Y (toward carriageway), compartment door faces +X"
    combos = [(H, O, tw, False) for H in L["heights"] for O in L["outreaches"] for tw in (False, True)]
    for st, (H, O, tw) in L["styles"].items():
        combos.append((H, O, tw, True))
    for H, O, tw, pm in combos:
        b, p, info = build(H, O, tw, pm, L)
        out.append(dict(name=b.name, category="Lighting", builder=b, proxy=p, pivot=pivot if not pm else
                        "underside of the base plate on the column axis; bracket +Y; no door",
                        notes="tubular steel column H=%gm, outreach %gm, %s%s" % (H, O, "twin-arm" if tw else "single arm", ", parapet/structure mount" if pm else ""),
                        params=info, proxy_tris=0, collision="box_column", light=dict(info)))
    return out
