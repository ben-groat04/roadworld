"""Coping stones and tactile paving.

Coping pieces: origin at the bottom of the FRONT face (y=0) at the start (x=0); body extends to -Y; +X along the wall.
Tactile slab: origin at the slab centre on the bed (z=0); top face at z=0.065 (flush with a 65 mm paving build-up);
blister rows align to X/Y.
"""
import math
import common as C
from common import Builder

J = 0.005


def retaining(P):
    L, W, H = P["retain_cope"]
    b = Builder("SM_Coping_Retaining")
    prof = [(0, 0), (0, H - 0.02), (-0.015, H), (-W + 0.05, H + 0.015), (-W, H - 0.01), (-W, 0), (-W + 0.05, 0), (-W + 0.05, 0.012), (-W + 0.08, 0.012), (-W + 0.08, 0)]
    b.loft([[(0.0, y, z) for (y, z) in prof], [(L - J, y, z) for (y, z) in prof]], "structural_concrete", bevel=0.007, segs=2)
    return b


def headwall(P):
    L, W, H = P["head_cope"]
    b = Builder("SM_Coping_Headwall")
    r = 0.06
    prof = [(0, 0), (0, H - r)]
    for i in range(1, 7):
        a = math.pi / 2 * i / 6
        prof.append((-r + r * math.cos(a), H - r + r * math.sin(a)))
    prof += [(-W + 0.03, H), (-W, H - 0.03), (-W, 0), (-0.06, 0), (-0.06, 0.015), (-0.03, 0.015), (-0.03, 0)]
    b.loft([[(0.0, y, z) for (y, z) in prof], [(L - J, y, z) for (y, z) in prof]], "structural_concrete", bevel=0.008, segs=2)
    return b


def tactile(P):
    s = P["tactile"]
    t = 0.065
    b = Builder("SM_Tactile_Blister_400")
    b.box(-s / 2, s / 2, -s / 2, s / 2, 0, t, "pavement", 0.004, 1)
    pitch = 0.0665
    for i in range(6):
        for j in range(6):
            x = (i - 2.5) * pitch
            y = (j - 2.5) * pitch
            b.tube((x, y, t), (x, y, t + 0.006), 0.0125, 0.0085, "pavement", 12)
    return b


def pieces():
    P = C.PARAMS["masonry"]
    pv = "bottom of the front face at the start; +X along wall, body at -Y"
    return [
        dict(name="SM_Coping_Retaining", category="Masonry", builder=retaining(P), pivot=pv, notes="retaining-wall coping stone 500 x 400 x 150, weathered top, drip groove", params=P, proxy_tris=12),
        dict(name="SM_Coping_Headwall", category="Masonry", builder=headwall(P), pivot=pv, notes="precast headwall coping 1000 x 450 x 200 with rounded front arris", params=P, proxy_tris=12),
        dict(name="SM_Tactile_Blister_400", category="Masonry", builder=tactile(P), pivot="slab centre on the bed; top at z=0.065; 6 x 6 blisters at 66.5 mm", notes="400 x 400 blister tactile paving slab", params=P, proxy_tris=12),
    ]
