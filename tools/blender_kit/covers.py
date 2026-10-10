"""Gully gratings, kerb inlet unit, manhole and inspection covers.

Local frame: origin at the CENTRE of the frame, TOP of the frame at z = +0.003 (gully grating: +0.006) above the road mesh so it
never z-fights; bodies extend downward (z < 0). +X along the road, +Y toward the carriageway. Bars of gully
gratings run along X (flow direction is across, toward the kerb = -Y).
"""
import math
import common as C
from common import Builder

TOP = 0.003


def gully(P):
    o, i, d = P["gully_outer"], P["gully_inner"], P["gully_depth"]
    bar, gap = P["gully_bar"], P["gully_gap"]
    b = Builder("SM_Gully_Grating")
    ho, hi = o / 2, i / 2
    zt = 0.006     # grate sits 6 mm proud so the dark slot floor (z=+1.5 mm) stays above a closed road mesh
    # frame: four bevelled rails forming a square ring with inward lip
    b.box(-ho, ho, hi, ho, zt - d, zt, "cast_iron", 0.004)
    b.box(-ho, ho, -ho, -hi, zt - d, zt, "cast_iron", 0.004)
    b.box(-ho, -hi, -hi, hi, zt - d, zt, "cast_iron", 0.004)
    b.box(hi, ho, -hi, hi, zt - d, zt, "cast_iron", 0.004)
    # seating ledge under the grate and a dark sump tray below
    zg = zt - 0.001
    n = int((i + gap) // (bar + gap))
    span = n * bar + (n - 1) * gap
    y0 = -span / 2
    for k in range(n):
        ya = y0 + k * (bar + gap)
        b.box(-hi, hi, ya, ya + bar, zg - 0.035, zg, "cast_iron", 0.0025, 1)
    # cross ties and hinge lugs
    for x in (-hi, -hi / 3, hi / 3, hi - 0.02):
        b.box(x, x + 0.02, -hi, hi, zg - 0.03, zg - 0.0, "cast_iron", 0.002, 1)
    b.box(-hi, hi, -hi - 0.0, -hi + 0.02, zt - d, zt - d + 0.012, "cast_iron")  # bottom ledge strip (keeps frame closed)
    # sump: open-topped pit seen through the slots (inside the frame, closed shell of a thick slab)
    b.box(-hi, hi, -hi, hi, -0.03, 0.0015, "plastic_black")
    return b


def kerb_inlet(P):
    L = P["inlet_len"] - 0.005
    w, h = P["inlet_w"], P["inlet_h"]
    ow = P["inlet_open_w"]
    z0, z1 = P["inlet_open_z"]
    b = Builder("SM_Gully_KerbInlet")
    x0 = (L - ow) / 2
    x1 = x0 + ow
    # pivot: road-side bottom edge of the start face, same as kerbs (+Y toward road, body at -Y)
    bev = 0.004
    b.box(0, x0, -w, 0, 0, h, "kerb_concrete", bev)
    b.box(x1, L, -w, 0, 0, h, "kerb_concrete", bev)
    b.box(x0, x1, -w, 0, z1, h, "kerb_concrete", bev)          # lintel
    b.box(x0, x1, -w, 0, 0, z0, "kerb_concrete", bev)          # sill
    b.box(x0, x1, -w, -w + 0.04, z0, z1, "plastic_black")      # dark back of the inlet recess
    return b


def manhole_round(P):
    fd, cd, d = P["mh_frame_d"], P["mh_cover_d"], P["mh_depth"]
    rh = P["rib_h"]
    b = Builder("SM_Manhole_Round600")
    zt = TOP
    b.ring_tube((0, 0), zt - d, zt, fd / 2, cd / 2 + 0.006, "cast_iron", 40, bevel=0.004)
    b.ring_tube((0, 0), zt - d, zt - 0.045, cd / 2 + 0.006, cd / 2 - 0.08, "cast_iron", 40)   # seating ledge
    # cover disc slightly recessed, with raised cross-hatch ribs
    zc = zt - 0.004
    b.lathe_z([(0.0, zc - 0.05), (cd / 2, zc - 0.05), (cd / 2, zc), (0.0, zc)], "cast_iron", 40, bevel=0.003)
    pitch, rw = P["rib_pitch"], P["rib_w"]
    r_in = cd / 2 - 0.02
    k = int(r_in // pitch)
    for ax in (0, 1):
        for j in range(-k, k + 1):
            c = j * pitch
            hl = math.sqrt(max(r_in ** 2 - c ** 2, 0)) - 0.005
            if hl < 0.03:
                continue
            if ax == 0:
                b.box(-hl, hl, c - rw / 2, c + rw / 2, zc, zc + rh, "cast_iron", 0.0015, 1)
            else:
                b.box(c - rw / 2, c + rw / 2, -hl, hl, zc, zc + rh, "cast_iron", 0.0015, 1)
    return b


def inspection_rect(P):
    fw, fl = P["rect_frame"]
    cw, cl = P["rect_cover"]
    d = P["mh_depth"]
    rh = P["rib_h"]
    b = Builder("SM_Inspection_Rect")
    zt = TOP
    hx, hy, cx_, cy_ = fw / 2, fl / 2, cw / 2, cl / 2
    gap = 0.006
    b.box(-hx, hx, cy_ + gap, hy, zt - d, zt, "cast_iron", 0.004)
    b.box(-hx, hx, -hy, -cy_ - gap, zt - d, zt, "cast_iron", 0.004)
    b.box(-hx, -cx_ - gap, -cy_ - gap, cy_ + gap, zt - d, zt, "cast_iron", 0.004)
    b.box(cx_ + gap, hx, -cy_ - gap, cy_ + gap, zt - d, zt, "cast_iron", 0.004)
    zc = zt - 0.004
    b.box(-cx_, cx_, -cy_, cy_, zc - 0.05, zc, "cast_iron", 0.003)
    b.box(-cx_ - 0.02, cx_ + 0.02, -cy_ - 0.02, cy_ + 0.02, zt - d, zt - d + 0.03, "cast_iron")  # ledge tray
    n = int((cl - 0.04) // 0.04)
    for j in range(n):
        y = -cy_ + 0.03 + j * 0.04
        b.box(-cx_ + 0.03, cx_ - 0.03, y, y + 0.014, zc, zc + rh, "cast_iron", 0.0015, 1)
    return b


def pieces():
    P = C.PARAMS["cover"]
    piv = "centre of frame in plan; frame top at z=+3 mm, body below; +X along road, +Y toward road"
    return [
        dict(name="SM_Gully_Grating", category="Drainage", builder=gully(P), pivot=piv,
             notes="450 x 450 hinged cast-iron road gully grating; slots run along X, flow arrives from +Y", params=P, proxy_tris=12),
        dict(name="SM_Gully_KerbInlet", category="Drainage", builder=kerb_inlet(P),
             pivot="road-side bottom edge of the start face (same as kerbs); body at -Y", notes="precast kerb-inlet unit, 450 mm opening; lay beside a channel", params=P, proxy_tris=12),
        dict(name="SM_Manhole_Round600", category="Drainage", builder=manhole_round(P), pivot=piv,
             notes="600 mm clear-opening round cast-iron cover and frame with cross-hatch tread", params=P, proxy_tris=24),
        dict(name="SM_Inspection_Rect", category="Drainage", builder=inspection_rect(P), pivot=piv,
             notes="rectangular 600 x 450 inspection cover; ribs along X", params=P, proxy_tris=12),
    ]
