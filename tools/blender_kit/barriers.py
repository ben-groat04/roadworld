"""Pedestrian guardrail, W-beam safety barrier, concrete step barrier, bridge parapet and parapet coping.

Local frame: +X along the run (origin at the first post / start face), +Y toward the traffic/carriageway face,
+Z up from ground (or deck) level. Modules tile at their nominal pitch (2 m, 4 m, 3 m, 2 m, 1 m); the modelled
length leaves a 5 mm joint where parts butt.
"""
import math
import common as C
from common import Builder

JOINT = 0.005


def ped(P, infill, end):
    L = P["ped_len"]
    h = P["ped_h"]
    pr = P["ped_post_d"] / 2
    rr = P["ped_rail_d"] / 2
    nm = "SM_Guardrail_Ped" + ("_Infill" if infill else "_Rail") + ("_End" if end else "")
    b = Builder(nm)
    posts = [0.0] + ([L] if end else [])
    for x in posts:
        b.tube((x, 0, 0), (x, 0, h), pr, pr, "galvanised_steel", 12)
        b.tube((x, 0, h), (x, 0, h + 0.01), pr * 0.7, pr * 0.5, "galvanised_steel", 12)
    xe = L if end else L - JOINT
    z_top, z_low = h - 0.06, 0.45
    for z in (z_top, z_low):
        b.tube((0, 0, z), (xe, 0, z), rr, rr, "galvanised_steel", 12)
    if infill:
        n = int((L - 0.1) / 0.11)
        for k in range(1, n + 1):
            x = k * (L / (n + 1))
            b.tube((x, 0, z_low), (x, 0, z_top), 0.008, 0.008, "galvanised_steel", 8)
    return b


def wbeam(P):
    L = P["wbeam_len"] - JOINT
    t = P["wbeam_t"]
    top = P["wbeam_top"]
    b = Builder("SM_Barrier_WBeam_4m")
    zb = top - 0.31
    ctr = [(0, 0), (0, 0.04), (0.075, 0.075), (0.075, 0.10), (0, 0.135), (0, 0.175), (0.075, 0.21), (0.075, 0.235), (0, 0.27), (0, 0.31)]
    # offset the centreline +-t/2 using mitred normals (y toward traffic is +)
    front, back = [], []
    for i, (y, z) in enumerate(ctr):
        def seg(a, c):
            dy, dz = c[0] - a[0], c[1] - a[1]
            l = math.hypot(dy, dz)
            return (dz / l, -dy / l)
        n0 = seg(ctr[i - 1], ctr[i]) if i > 0 else None
        n1 = seg(ctr[i], ctr[i + 1]) if i < len(ctr) - 1 else None
        if n0 is None: n0 = n1
        if n1 is None: n1 = n0
        nx, nz = n0[0] + n1[0], n0[1] + n1[1]
        l = math.hypot(nx, nz)
        nx, nz = nx / l, nz / l
        k = (nx * n0[0] + nz * n0[1])
        o = t / 2 / max(k, 0.3)
        front.append((y + nx * o, zb + z + nz * o))
        back.append((y - nx * o, zb + z - nz * o))
    outline = front + back[::-1]
    yb = 0.20
    sec = lambda x: [(x, yb + y, z) for (y, z) in outline]
    b.loft([sec(0.0), sec(L)], "galvanised_steel", bevel=0.0)
    # posts: simple C-section block + spacer block, two per module
    for x in (0.0, P["wbeam_post_spacing"]):
        xc = x + 0.0
        b.box(xc - 0.05, xc + 0.05, -0.03, 0.03, -0.35, top - 0.04, "galvanised_steel", 0.004, 1)
        b.box(xc - 0.05, xc + 0.05, 0.03, yb, zb + 0.03, top - 0.02, "galvanised_steel", 0.004, 1)
        for zz in (zb + 0.0875, zb + 0.2225):
            b.tube((xc, yb + 0.075, zz), (xc, yb + 0.087, zz), 0.014, 0.011, "galvanised_steel", 8)
    return b


def step(P):
    L = P["step_len"] - JOINT
    H = P["step_h"]
    half = [(0.285, 0.0), (0.285, 0.075), (0.20, 0.28), (0.20, 0.34), (0.085, H - 0.02), (0.075, H)]
    ring = [(y, z) for (y, z) in half] + [(-y, z) for (y, z) in half[::-1]]
    b = Builder("SM_Barrier_Step_3m")
    b.loft([[(0.0, y, z) for (y, z) in ring], [(L, y, z) for (y, z) in ring]], "structural_concrete", bevel=0.01, segs=2)
    return b


def parapet_steel(P):
    L = P["parapet_bay"] - JOINT
    H = P["parapet_h"]
    b = Builder("SM_Parapet_Steel_2m")
    b.box(-0.10, 0.10, -0.10, 0.10, 0.0, 0.02, "painted_steel", 0.004, 1)
    for sx in (-1, 1):
        for sy in (-1, 1):
            b.tube((sx * 0.07, sy * 0.07, 0.02), (sx * 0.07, sy * 0.07, 0.036), 0.012, 0.01, "painted_steel", 6)
    b.box(-0.05, 0.05, -0.05, 0.05, 0.02, H - 0.08, "painted_steel", 0.008, 2)
    for z0, z1, w in ((H - 0.09, H, 0.12), (0.58, 0.66, 0.08), (0.28, 0.36, 0.08)):
        b.box(0.0, L, 0.05, 0.05 + w, z0, z1, "painted_steel", 0.012, 2)
    return b


def coping(P):
    Lc = P["cope_len"] - JOINT
    w = P["cope_w"]
    prof = [(-0.35, 0), (-0.33, 0), (-0.33, 0.012), (-0.31, 0.012), (-0.31, 0), (0.01, 0), (0.01, 0.012), (0.03, 0.012), (0.03, 0), (0.05, 0),
            (0.05, 0.09), (-0.15, 0.12), (-0.35, 0.09)]
    b = Builder("SM_Parapet_Coping_1m")
    b.loft([[(0.0, y, z) for (y, z) in prof], [(Lc, y, z) for (y, z) in prof]], "structural_concrete", bevel=0.006, segs=2)
    return b


def pieces():
    P = C.PARAMS["barrier"]
    out = []
    pv = "ground level at the first post axis; +X along run, +Y toward traffic"
    out.append(dict(name="SM_Guardrail_Ped_Rail", category="Barriers", builder=ped(P, False, False), pivot=pv, notes="2 m pedestrian guardrail bay, 1.1 m, two rails; post at x=0 only (next bay supplies the far post)", params=P, proxy_tris=24, collision="box"))
    out.append(dict(name="SM_Guardrail_Ped_Infill", category="Barriers", builder=ped(P, True, False), pivot=pv, notes="as Rail with vertical infill bars", params=P, proxy_tris=24, collision="box"))
    out.append(dict(name="SM_Guardrail_Ped_Rail_End", category="Barriers", builder=ped(P, False, True), pivot=pv, notes="2 m bay including the terminal post at x=2.0", params=P, proxy_tris=24, collision="box"))
    out.append(dict(name="SM_Barrier_WBeam_4m", category="Barriers", builder=wbeam(P), pivot="ground at post 1 axis (x=0); beam face toward +Y; posts at x=0 and x=2.0", notes="W-beam steel safety barrier, 4 m module, top 0.75 m, posts every 2 m with spacer blocks", params=P, proxy_tris=40, collision="box"))
    out.append(dict(name="SM_Barrier_Step_3m", category="Barriers", builder=step(P), pivot="ground, centre of base (y=0), start face; symmetric (double-sided)", notes="precast concrete step barrier 0.8 m for central reservations, 3 m unit", params=P, proxy_tris=24, collision="box"))
    out.append(dict(name="SM_Parapet_Steel_2m", category="Barriers", builder=parapet_steel(P), pivot="deck level at post axis (x=0); traffic side +Y; rails to x=2.0", notes="bridge parapet post-and-rail bay, 2 m, 1.0 m high, base plate with bolts", params=P, proxy_tris=24, collision="box"))
    out.append(dict(name="SM_Parapet_Coping_1m", category="Barriers", builder=coping(P), pivot="bottom of the wall traffic face (y=0) at the start; coping overhangs to y=+0.05 and y=-0.35 (0.30 m wall)", notes="precast concrete parapet coping, 1 m, throated", params=P, proxy_tris=12))
    return out
