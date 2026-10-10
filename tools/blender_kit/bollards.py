"""Bollards. Local frame: origin ground level, centre of footprint; +Y toward the carriageway (the wide/face side
of the keep-left island bollard faces +Y). No signs, no graphics: the body is blank."""
import math
import common as C
from common import Builder


def steel(P):
    r = P["steel_d"] / 2
    h = P["steel_h"]
    b = Builder("SM_Bollard_Steel")
    prof = [(0.0, 0.0), (r, 0.0), (r, h - 0.06), (r * 0.97, h - 0.03), (r * 0.82, h - 0.008), (r * 0.5, h + 0.004), (0.0, h + 0.008)]
    b.lathe_z(prof, "painted_steel", 20, bevel=0.0)
    b.lathe_z([(0.0, h - 0.30), (r + 0.003, h - 0.30), (r + 0.003, h - 0.20), (0.0, h - 0.20)], "plastic_white", 20, bevel=0.002, segs=1)
    b.lathe_z([(0.0, 0.0), (r + 0.012, 0.0), (r + 0.012, 0.025), (r + 0.004, 0.04), (0.0, 0.04)], "painted_steel", 20, bevel=0.002, segs=1)
    return b


def keepleft(P):
    w, d, h = P["keepleft"]
    b = Builder("SM_Bollard_IslandKeepLeft")
    base_h = 0.10
    b.box(-w / 2, w / 2, -d / 2, d / 2, 0, base_h, "plastic_black", 0.02, 3)
    # body: slightly tapering rounded-rectangle frustum with a sloped, softened top
    def ring(hw, hd, z, rr=0.045, n=4):
        pts = []
        for cx, cy, a0 in ((hw - rr, hd - rr, 0), (-hw + rr, hd - rr, 90), (-hw + rr, -hd + rr, 180), (hw - rr, -hd + rr, 270)):
            for i in range(n + 1):
                a = math.radians(a0 + 90 * i / n)
                pts.append((cx + rr * math.cos(a), cy + rr * math.sin(a), z))
        return pts
    secs = [ring(w / 2 - 0.005, d / 2 - 0.005, base_h), ring(w / 2 - 0.012, d / 2 - 0.012, h - 0.12),
            ring(w / 2 - 0.045, d / 2 - 0.035, h - 0.03), ring(w / 2 - 0.075, d / 2 - 0.05, h)]
    b.loft(secs, "plastic_white", bevel=0.0)
    secs2 = [ring(w / 2 - 0.0125, d / 2 - 0.0125, h - 0.30), ring(w / 2 - 0.0125, d / 2 - 0.0125, h - 0.20)]
    b.loft(secs2, "plastic_black", bevel=0.0)
    return b


def timber(P):
    w, d, h = P["timber"]
    b = Builder("SM_Bollard_Timber")
    hw, hd = w / 2, d / 2

    def rect(a, c, z):
        return [(-a, -c, z), (a, -c, z), (a, c, z), (-a, c, z)]
    b.loft([rect(hw, hd, -0.2), rect(hw, hd, h - 0.045), rect(hw - 0.025, hd - 0.025, h)], "timber", bevel=0.005, segs=2, angle=35)
    return b


def pieces():
    P = C.PARAMS["bollard"]
    piv = "ground level at the centre of the footprint; +Y toward the carriageway"
    return [
        dict(name="SM_Bollard_Steel", category="Bollards", builder=steel(P), pivot=piv, notes="114 mm steel verge bollard, 1.0 m, white reflective band, domed cap", params=P, proxy_tris=24, collision="box"),
        dict(name="SM_Bollard_IslandKeepLeft", category="Bollards", builder=keepleft(P), pivot=piv, notes="blank island bollard body 320 x 180 x 750 (no sign face); wide face toward +Y", params=P, proxy_tris=24, collision="box"),
        dict(name="SM_Bollard_Timber", category="Bollards", builder=timber(P), pivot=piv, notes="150 x 150 x 1000 timber post, chamfered top, 200 mm embedded below ground", params=P, proxy_tris=12, collision="box"),
    ]
