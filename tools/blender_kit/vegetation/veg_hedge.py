"""Hedge modules. Design: a lumpy opaque core (periodic noise so modules tile) plus leaf-cluster cards scattered on the
surface with randomised orientation, plus (overgrown) emergent shoots. Cards on a real mesh surface read better from 5-50 m
than a displaced shell (no flat plates, silhouette comes from cards poking out) and stay cheap; the core stops light and
sight-lines leaking through and gives the dark interior seen in the reference photos."""
import math, random
from mathutils import Vector
from veg_common import *

# module specs. kind: trimmed | overgrown. end: None | taper | cut (closed end at +X)
HEDGES = {
    "hedge_trimmed": {
        "V00": dict(L=4.0, H=1.6, W=1.1, seed=11, end=None), "V01": dict(L=4.0, H=2.3, W=1.5, seed=12, end=None),
        "V02": dict(L=2.0, H=1.9, W=1.3, seed=13, end=None), "V03": dict(L=4.0, H=3.0, W=1.9, seed=14, end=None),
        "E00": dict(L=2.0, H=1.9, W=1.3, seed=15, end="taper"), "E01": dict(L=2.0, H=1.9, W=1.3, seed=16, end="cut"),
    },
    "hedge_overgrown": {
        "V00": dict(L=4.0, H=2.2, W=1.8, seed=21, end=None), "V01": dict(L=4.0, H=3.0, W=2.4, seed=22, end=None),
        "V02": dict(L=4.0, H=3.6, W=2.8, seed=23, end=None), "V03": dict(L=2.0, H=2.0, W=1.8, seed=24, end=None),
        "E00": dict(L=4.0, H=2.6, W=2.2, seed=25, end="taper"), "E01": dict(L=4.0, H=2.6, W=2.2, seed=26, end="cut"),
    },
}
ROLE = {None: "module", "taper": "end_taper", "cut": "end_cut"}


def _superellipse(theta, n):
    c, s = math.cos(theta), math.sin(theta)
    return (math.copysign(abs(c) ** (2 / n), c), abs(s) ** (2 / n))


def build_hedge(kind, spec, lod):
    rng = random.Random(spec["seed"] * 7 + 1)
    trimmed = kind == "hedge_trimmed"
    L, H, W = spec["L"], spec["H"], spec["W"]
    end = spec["end"]
    vm = VMesh("hedge")
    # LOD parameters
    seg_len = (0.3, 0.55, 1.0)[lod]
    R = ((16 if trimmed else 20), 12, 8)[lod]
    nseg = max(2, round(L / seg_len))
    core_mat = "veg_core" if lod < 2 else "veg_canopy_far"
    amp = 0.035 if trimmed else 0.16
    nexp = 5.0 if trimmed else 2.8
    fH = periodic_noise(rng, L, 3, 0.0 if trimmed else 0.16)
    fW = periodic_noise(rng, L, 3, 0.03 if trimmed else 0.14)
    fa, fb, fc = [periodic_noise(rng, L, 4, 1.0, 7) for _ in range(3)]
    jit_amp = 0.015 if trimmed else 0.05
    d = min(1.1, L * 0.45)
    cols = []
    for i in range(nseg + 1):
        x = -L / 2 + L * i / nseg
        h = H * (1 + fH(x))
        w = W * (1 + fW(x)) / 2
        row = []
        for j in range(R):
            th = math.pi - math.pi * j / (R - 1)
            y0, z0 = _superellipse(th, nexp)
            y, z = y0 * w, z0 * h
            bump = amp * (fa(x + L * 0.5 * th / math.pi) * 0.6 + fb(x * 1.0 + L * 0.31 * th) * 0.4) * (0.2 + z0)
            if lod == 0 and 0 < i < nseg:
                bump += rng.uniform(-jit_amp, jit_amp) * z0
            y *= 1 + bump
            z *= 1 + bump
            if end == "taper" and x > L / 2 - d:
                u = (x - (L / 2 - d)) / d
                y *= max(0.05, math.sqrt(max(0.0, 1 - u * u)))
                z *= max(0.05, (1 - u ** 4) ** 0.5)
            row.append((x, y, max(z, 0.0)))
        cols.append(row)
    ids = [[vm.vert(p) for p in row] for row in cols]
    tris = []  # for sampling cards: (p0, p1, p2, normal, area)
    for i in range(nseg):
        for j in range(R - 1):
            q = [ids[i][j], ids[i + 1][j], ids[i + 1][j + 1], ids[i][j + 1]]
            P = [Vector(vm.verts[k]) for k in q]
            n = (P[1] - P[0]).cross(P[3] - P[0])
            if n.length < 1e-9:
                n = (P[2] - P[1]).cross(P[3] - P[1])
            if n.length < 1e-9:
                continue
            nn = n.normalized()
            # outward test: normal should point away from the axis (y sign) or up
            cen = (P[0] + P[1] + P[2] + P[3]) / 4
            if nn.dot(Vector((0, cen.y, cen.z - 0.4 * H))) < 0:
                nn = -nn
                q = q[::-1]
            vm.face(q, core_mat, [(vm.verts[k][0], vm.verts[k][2]) for k in q], [None] * 4, [(min(1, vm.verts[k][2] / H), 0, 0.25) for k in q])
            tris.append((P[0], P[1], P[2], nn, (P[1] - P[0]).cross(P[2] - P[0]).length / 2))
            tris.append((P[0], P[2], P[3], nn, (P[2] - P[0]).cross(P[3] - P[0]).length / 2))
    if end == "cut":
        ring = ids[nseg]
        cen = Vector((L / 2, 0, H * 0.45))
        face = ring[::-1]
        # outward is +X: winding as seen from +X
        vm.face(face, core_mat, [(vm.verts[k][1], vm.verts[k][2]) for k in face], [None] * len(face), [(0.3, 0, 0.25)] * len(face))
        P = [Vector(vm.verts[k]) for k in ring]
        # fan triangles for card sampling
        c0 = sum(P, Vector()) / len(P)
        for a in range(len(P) - 1):
            area = (P[a] - c0).cross(P[a + 1] - c0).length / 2
            tris.append((c0, P[a], P[a + 1], Vector((1, 0, 0)), area))
    # core faces with n=None need normals: compute smooth-ish from geometry later (use face normal) -> set per-face
    _fix_core_normals(vm)

    # ---- leaf cards ---------------------------------------------------------------------------------------------
    if lod == 0:
        dens, csz = (62 if trimmed else 52), (0.19 if trimmed else 0.26)
    elif lod == 1:
        dens, csz = (14 if trimmed else 12), (0.4 if trimmed else 0.52)
    else:
        dens, csz = (2.2 if trimmed else 2.0), (0.95 if trimmed else 1.1)
    cum = []
    tot = 0
    for t in tris:
        tot += t[4]
        cum.append(tot)
    ncards = int(tot * dens)
    import bisect
    spread = 0.5 if trimmed else 0.95
    for _ in range(ncards):
        r = rng.random() * tot
        t = tris[min(bisect.bisect_left(cum, r), len(tris) - 1)]
        a, b = rng.random(), rng.random()
        if a + b > 1:
            a, b = 1 - a, 1 - b
        p = t[0] + (t[1] - t[0]) * a + (t[2] - t[0]) * b
        n = t[3]
        off = rng.uniform(0.0, 0.05 if trimmed else 0.14)
        c = p + n * off
        cn = norm(n + Vector((rng.uniform(-1, 1), rng.uniform(-1, 1), rng.uniform(-1, 1))) * spread)
        if lod == 0 and rng.random() < 0.22 and not trimmed:
            rect, sz = LEAF_CELLS[LEAF_BROAD], csz * 1.15
        else:
            rect, sz = LEAF_CELLS[LEAF_HEDGE], csz * rng.uniform(0.8, 1.25)
        shade_n = norm(n * 0.7 + Z * 0.3)
        vm.card(c, cn, Z, sz, sz, rect, "veg_leaf", shade_n, min(1.0, c.z / H) * 0.6 + 0.0, rng.random(),
                0.55 + 0.45 * min(1, off / 0.1 + 0.2), roll=rng.uniform(0, 6.28), flip_u=rng.random() < 0.5)
    # ---- emergent shoots -----------------------------------------------------------------------------------------
    if not trimmed and lod < 2:
        nshoot = int(L * (3.2 if lod == 0 else 1.2))
        for _ in range(nshoot):
            x = rng.uniform(-L / 2 + 0.2, L / 2 - 0.2)
            y = rng.uniform(-W * 0.3, W * 0.3)
            z0 = H * (1 + fH(x)) * 0.92
            ln = rng.uniform(0.5, 1.5)
            dirv = norm(Vector((rng.uniform(-0.3, 0.3), rng.uniform(-0.7, 0.7), 1.0)))
            pts, rad = [], []
            p = Vector((x, y, z0 * 0.9))
            segs = 4
            for k in range(segs + 1):
                pts.append(p.copy())
                rad.append(0.016 * (1 - k / segs) + 0.004)
                dirv = norm(dirv + Vector((rng.uniform(-0.2, 0.2), rng.uniform(-0.2, 0.2), -0.05)))
                p = p + dirv * ln / segs
            vm.tube(pts, rad, 3, "veg_bark", wind_fn=lambda q, H=H: min(1.0, max(0.0, (q.z - H * 0.8) / 1.2)))
            nl = 7 if lod == 0 else 3
            for k in range(nl):
                tt = 0.35 + 0.65 * k / max(1, nl - 1)
                pi = pts[min(segs, int(tt * segs))]
                cn = norm(Vector((rng.uniform(-1, 1), rng.uniform(-1, 1), rng.uniform(-0.2, 1))))
                s = (0.2 if lod == 0 else 0.35) * rng.uniform(0.8, 1.3)
                vm.card(pi + cn * 0.04, cn, Z, s, s, LEAF_CELLS[LEAF_BROAD if rng.random() < 0.5 else LEAF_HEDGE], "veg_leaf",
                        norm(cn * 0.5 + Z * 0.5), min(1.0, 0.5 + tt * 0.5), rng.random(), 0.9, roll=rng.uniform(0, 6.28))
    return vm


def _fix_core_normals(vm):
    """Per-loop normals for the core: smooth by averaging adjacent face normals via shared verts."""
    acc = {}
    off = 0
    fn = []
    for f in vm.faces:
        if vm.fmat[len(fn)] is not None:
            pass
        P = [Vector(vm.verts[k]) for k in f]
        n = Vector()
        for a in range(1, len(P) - 1):
            n += (P[a] - P[0]).cross(P[a + 1] - P[0])
        fn.append(n.normalized() if n.length > 1e-12 else Vector((0, 0, 1)))
    for fi, f in enumerate(vm.faces):
        if vm.slots[vm.fmat[fi]] in ("veg_core", "veg_canopy_far") and len(f) == 4:
            for k in f:
                acc[k] = acc.get(k, Vector()) + fn[fi]
    loop = 0
    for fi, f in enumerate(vm.faces):
        for k in f:
            if vm.nrm[loop] is None:
                if len(f) == 4 and k in acc:
                    vm.nrm[loop] = tuple(norm(acc[k]))
                else:
                    vm.nrm[loop] = tuple(fn[fi])
            loop += 1
