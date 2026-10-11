"""Procedural trees: own recursive branching skeleton (no add-on; the Sapling add-on is reported broken on Blender 5.x and
Blender 5.2.2 ships no tree generator) + leaf-cluster cards. One skeleton per (species, size, seed); each LOD is a different
rendering of the same skeleton so silhouettes stay consistent. Also staked sapling and plantation conifer."""
import math, random
from mathutils import Vector, Matrix
from veg_common import *
from veg_ground import blob, _blob_normals

# species parameters. index = branch level (0 trunk). envelope(z_rel) scales child length by height up the tree.
SPECIES = {
    "round": dict(
        trunk_frac=0.32, r0f=0.030, taper=0.55, levels=3, segs=[6, 5, 4, 3, 3], sides=[8, 6, 5, 4, 3],
        nchild=[5, 5, 4, 3], t0=[0.78, 0.30, 0.25, 0.3], angle=[0.95, 0.85, 0.8, 0.8], lenratio=[0.95, 0.62, 0.55, 0.5],
        rratio=[0.62, 0.62, 0.6, 0.6], gnarl=[0.12, 0.28, 0.35, 0.4], tropism=[0.0, 0.5, 0.15, -0.1], upbias=[0.1, 0.25, 0.1, 0.0],
        clump=0.115, cards=5, leaf_cell=(LEAF_BROAD, LEAF_HEDGE), crown_flat=1.0, keep=1.0),
    "open": dict(
        trunk_frac=0.52, r0f=0.022, taper=0.4, levels=3, segs=[7, 5, 4, 3, 3], sides=[8, 6, 5, 4, 3],
        nchild=[8, 4, 3, 2], t0=[0.3, 0.25, 0.25, 0.3], angle=[0.8, 0.75, 0.8, 0.8], lenratio=[0.5, 0.6, 0.55, 0.5],
        rratio=[0.5, 0.55, 0.55, 0.6], gnarl=[0.06, 0.22, 0.3, 0.35], tropism=[0.2, 0.3, 0.1, -0.1], upbias=[0.35, 0.3, 0.15, 0.0],
        clump=0.08, cards=3, leaf_cell=(LEAF_BROAD,), crown_flat=0.8, keep=0.65),
}
SIZES = {"S": 5.0, "M": 10.0, "L": 16.0}


def _perp(d, phi):
    ref = Vector((0, 0, 1)) if abs(d.z) < 0.9 else Vector((1, 0, 0))
    u = norm(d.cross(ref))
    v = norm(d.cross(u))
    return u * math.cos(phi) + v * math.sin(phi)


def skeleton(species, H, seed):
    sp = SPECIES[species]
    rng = random.Random(seed * 101 + (7 if species == "round" else 13))
    maxlev = sp["levels"] + (1 if H > 12 else 0)
    branches, clumps = [], []
    trunk_len = sp["trunk_frac"] * H
    r0 = sp["r0f"] * H
    crown_centre = Vector((0, 0, trunk_len + (H - trunk_len) * 0.45))

    def env(z):
        zr = max(0.0, min(1.0, z / H))
        if species == "round":
            return 0.35 + 0.65 * math.sin(math.pi * min(1.0, (zr - 0.15) / 0.8 + 0.1)) ** 0.7
        return 0.25 + 0.75 * math.sin(math.pi * min(1.0, max(0.0, (zr - 0.2) / 0.85 + 0.05))) ** 0.8

    def grow(level, start, d, length, rad0, parent):
        li = min(level, len(sp["segs"]) - 1)
        nseg = sp["segs"][li]
        pts, rad = [Vector(start)], [rad0]
        p = Vector(start)
        for k in range(nseg):
            g = sp["gnarl"][min(level, 3)]
            d = norm(d + Vector((rng.uniform(-g, g), rng.uniform(-g, g), rng.uniform(-g, g))) + Z * sp["tropism"][min(level, 3)] / nseg)
            if level == 0 and species == "open":
                d = norm(d + Z * 0.4)  # strong leader
            p = p + d * length / nseg
            pts.append(p.copy())
            rad.append(max(0.004, rad0 * (1 - (1 - sp["taper"]) * (k + 1) / nseg)))
        br = dict(level=level, pts=pts, rad=rad, parent=parent)
        branches.append(br)
        if level < maxlev:
            lc = min(level, 3)
            n = sp["nchild"][lc] + (rng.randint(-1, 1) if level > 0 else 0)
            if level == 0 and species == "round":
                n = sp["nchild"][0] + rng.randint(0, 1)
            phi0 = rng.uniform(0, 6.28)
            for c in range(n):
                t = sp["t0"][lc] + (1.0 - sp["t0"][lc]) * (c + rng.uniform(0.0, 0.9)) / max(1, n)
                if level == 0 and species == "round":
                    t = min(1.0, 0.82 + 0.18 * c / max(1, n - 1)) if n > 1 else 1.0
                ip = min(len(pts) - 1, max(1, int(round(t * (len(pts) - 1)))))
                dp = norm(pts[ip] - pts[ip - 1])
                phi = phi0 + c * 2.399 + rng.uniform(-0.4, 0.4)
                ang = sp["angle"][lc] * rng.uniform(0.8, 1.2)
                cd = norm(dp * math.cos(ang) + _perp(dp, phi) * math.sin(ang) + Z * sp["upbias"][lc])
                zr = pts[ip].z
                ln = length * sp["lenratio"][lc] * rng.uniform(0.75, 1.2)
                if level == 0:
                    ln = (H - trunk_len) * 0.5 * rng.uniform(0.85, 1.12) * env(zr) if species == "round" else H * 0.4 * env(zr) * rng.uniform(0.7, 1.1)
                    ln = max(ln, H * 0.12)
                else:
                    ln *= env(zr) ** 0.5
                grow(level + 1, pts[ip], cd, ln, rad[ip] * sp["rratio"][lc], br)
            if level >= 1:
                # also the tip continues as a short extension (keeps branch ends leafy)
                pass
        if level >= maxlev - 1:
            clumps.append((pts[-1].copy(), d.copy(), level))
            if level == maxlev - 1 and len(pts) > 3 and rng.random() < 0.6:
                clumps.append((pts[len(pts) // 2].copy() + _perp(d, rng.uniform(0, 6.28)) * 0.05 * H, d.copy(), level))
        return br

    lean = Vector((rng.uniform(-0.05, 0.05), rng.uniform(-0.05, 0.05), 1.0))
    if species == "round":
        grow(0, Vector((0, 0, 0)), norm(lean), trunk_len, r0, None)
    else:
        grow(0, Vector((0, 0, 0)), norm(lean), H * 0.8, r0, None)
    return dict(branches=branches, clumps=clumps, crown_centre=crown_centre, H=H, sp=sp, rng_seed=seed)


def build_broadleaf(species, size_key, seed_idx, lod, leaf_state="leaf"):
    H = SIZES[size_key] * (0.84 if species == "open" else 1.0)
    seed = {"S": 1, "M": 2, "L": 3}[size_key] * 10 + seed_idx
    sk = skeleton(species, H, seed)
    sp = sk["sp"]
    rng = random.Random(seed * 7 + lod)
    vm = VMesh("tree")
    cc = sk["crown_centre"]
    sides_scale = (1.0, 0.65, 0.0)[lod]
    maxlev = max(b["level"] for b in sk["branches"])
    if lod < 2:
        for b in sk["branches"]:
            if lod == 1 and b["level"] >= maxlev:
                continue
            ns = sp["sides"][min(b["level"], 4)]
            if lod == 1:
                ns = max(3, ns - 2)
            pts, rad = b["pts"], b["rad"]
            if lod == 1 and len(pts) > 3:
                pts = pts[::2] if len(pts) % 2 == 1 else pts[::2] + [pts[-1]]
                rad = rad[::2] if len(rad) % 2 == 1 else rad[::2] + [rad[-1]]
            vm.tube(pts, rad, ns, "veg_bark", wind_fn=lambda q, H=H: min(1.0, max(0.0, q.z / H)) ** 1.5 * 0.5)
    else:
        for b in sk["branches"]:
            if b["level"] > 1:
                continue
            vm.tube([b["pts"][0], b["pts"][-1]] if b["level"] else b["pts"][::3] + [b["pts"][-1]], [b["rad"][0], b["rad"][-1]] if b["level"] else b["rad"][::3] + [b["rad"][-1]],
                    4 if b["level"] == 0 else 3, "veg_bark")
    # foliage
    if leaf_state == "leaf" and lod < 2:
        base_sz = sp["clump"] * H
        clumps = sk["clumps"]
        ncard = sp["cards"] if lod == 0 else 2
        szm = 1.0 if lod == 0 else 1.7
        if lod == 1:
            clumps = clumps[::2]
        for (pos, d, lev) in clumps:
            if rng.random() > sp["keep"]:
                continue
            for k in range(ncard):
                off = Vector((rng.uniform(-1, 1), rng.uniform(-1, 1), rng.uniform(-0.7, 1.0))) * base_sz * 0.55
                c = pos + off
                rel = c - cc
                out = norm(rel.normalized() * 0.8 + Vector((rng.uniform(-1, 1), rng.uniform(-1, 1), rng.uniform(-0.6, 1))) * 0.8)
                shade = norm(rel.normalized() * 0.7 + Z * 0.35)
                dist = rel.length / (0.55 * H + 1e-3)
                ao = max(0.25, min(1.0, 0.3 + 0.75 * dist))
                cell = rng.choice(sp["leaf_cell"])
                s = base_sz * rng.uniform(0.9, 1.5) * szm
                vm.card(c, out, Z, s, s, LEAF_CELLS[cell], "veg_leaf", shade, min(1.0, c.z / H), rng.random(), ao,
                        roll=rng.uniform(0, 6.28), flip_u=rng.random() < 0.5)
    elif lod == 2:
        # far LOD: opaque lumps clustered from the clump positions
        cell = max(1.2, 0.3 * H)
        grid = {}
        for (pos, d, lev) in sk["clumps"]:
            key = (int(pos.x // cell), int(pos.y // cell), int(pos.z // cell))
            grid.setdefault(key, []).append(pos)
        for key, ps in grid.items():
            c = sum(ps, Vector()) / len(ps)
            r = cell * 0.62
            blob(vm, c, Vector((r, r, r * 0.8)), "veg_canopy_far", rng, 0.07, 0, 0.18)
        _blob_normals(vm)
    return vm


# ---- staked sapling -----------------------------------------------------------------------------------------------
def build_sapling(seed_idx, lod):
    rng = random.Random(500 + seed_idx)
    H = (2.3, 2.8, 3.3)[seed_idx]
    vm = VMesh("sapling")
    lean = Vector((rng.uniform(-0.04, 0.04), rng.uniform(-0.04, 0.04), 1))
    if lod < 2:
        pts = [Vector((0, 0, 0)) + norm(lean) * (H * k / 5) + Vector((rng.uniform(-0.02, 0.02), rng.uniform(-0.02, 0.02), 0)) * (k > 0) for k in range(6)]
        vm.tube(pts, [0.022 - 0.0035 * k for k in range(6)], 6 if lod == 0 else 4, "veg_bark", wind_fn=lambda q: min(1.0, q.z / H) * 0.6)
        tips = []
        for k in range(7 if lod == 0 else 4):
            z = H * rng.uniform(0.5, 0.92)
            a = rng.uniform(0, 6.28)
            d = norm(Vector((math.cos(a) * 0.9, math.sin(a) * 0.9, 0.7)))
            p0 = Vector((0, 0, z))
            p1 = p0 + d * rng.uniform(0.3, 0.55)
            if lod == 0:
                vm.tube([p0, p1], [0.008, 0.003], 3, "veg_bark")
            tips.append(p1)
        tips.append(pts[-1])
        cc = Vector((0, 0, H * 0.72))
        for tp in tips:
            for k in range(3 if lod == 0 else 2):
                c = tp + Vector((rng.uniform(-1, 1), rng.uniform(-1, 1), rng.uniform(-0.5, 1))) * 0.14
                rel = c - cc
                s = 0.3 * rng.uniform(0.8, 1.3) * (1 if lod == 0 else 1.5)
                vm.card(c, norm(rel.normalized() + Vector((rng.uniform(-1, 1), rng.uniform(-1, 1), rng.uniform(-1, 1))) * 0.6), Z, s, s,
                        LEAF_CELLS[LEAF_BROAD], "veg_leaf", norm(rel.normalized() * 0.7 + Z * 0.3), min(1.0, c.z / H), rng.random(),
                        0.6 + 0.4 * min(1.0, rel.length), roll=rng.uniform(0, 6.28))
        # stake (square section) beside the trunk, tie strip
        sx = 0.11
        for (x0, x1, y0, y1) in [(sx, sx + 0.04, -0.02, 0.02)]:
            z1 = H * 0.78
            v = [vm.vert((x, y, z)) for z in (0, z1) for (x, y) in ((x0, y0), (x1, y0), (x1, y1), (x0, y1))]
            for a in range(4):
                b = (a + 1) % 4
                vm.face([v[a], v[b], v[4 + b], v[4 + a]], "veg_timber", [(0, 0), (0.04, 0), (0.04, z1), (0, z1)], [None] * 4,
                        [(0, 0, 0.8)] * 4)
            vm.face([v[4], v[5], v[6], v[7]], "veg_timber", [(0, 0), (1, 0), (1, 1), (0, 1)], [None] * 4, [(0, 0, 0.8)] * 4)
        _blob_normals(vm)
    else:
        blob(vm, Vector((0, 0, H * 0.72)), Vector((0.55, 0.55, 0.9)), "veg_canopy_far", rng, 0.15, 0)
        vm.tube([Vector((0, 0, 0)), Vector((0, 0, H * 0.4))], [0.02, 0.015], 3, "veg_bark")
        _blob_normals(vm)
    return vm


# ---- plantation conifer ---------------------------------------------------------------------------------------------
def build_conifer(seed_idx, lod):
    rng = random.Random(800 + seed_idx)
    H = (11.0, 14.0, 17.0)[seed_idx]
    vm = VMesh("conifer")
    Rmax = H * 0.16
    nwh = {0: 17, 1: 8, 2: 0}[lod]
    if lod < 2:
        vm.tube([Vector((0, 0, 0)), Vector((0, 0, H))], [0.016 * H, 0.002 * H], 8 if lod == 0 else 4, "veg_bark",
                wind_fn=lambda q: (q.z / H) ** 1.5 * 0.4)
        # dead lower trunk: bare to 12 percent
        for w in range(nwh):
            t = 0.14 + 0.86 * (w + 0.5) / nwh
            z = t * H
            nb = 7 if lod == 0 else 5
            a0 = rng.uniform(0, 6.28)
            for b in range(nb):
                a = a0 + 2 * math.pi * b / nb + rng.uniform(-0.25, 0.25)
                ln = Rmax * (1 - t) ** 0.85 * rng.uniform(0.8, 1.15) + 0.3
                pitch = -0.38 + 0.55 * t + rng.uniform(-0.1, 0.1)
                dirv = norm(Vector((math.cos(a) * math.cos(pitch), math.sin(a) * math.cos(pitch), math.sin(pitch))))
                base = Vector((0, 0, z))
                if lod == 0:
                    vm.tube([base, base + dirv * ln * 0.5 + Vector((0, 0, 0.0)), base + dirv * ln + Vector((0, 0, -0.1 * ln))], [0.03 * ln * 0.5 + 0.01, 0.012, 0.004], 3,
                            "veg_bark")
                hor = norm(dirv.cross(Z)) if abs(dirv.z) < 0.99 else Vector((1, 0, 0))
                nrolls = (-0.9, 0.0, 0.9) if lod == 0 else (-0.5, 0.5)
                for roll in nrolls:
                    vaxis = norm(hor * math.cos(roll) + norm(dirv.cross(hor)) * math.sin(roll))
                    wlen = ln * 1.05
                    wid = ln * 0.85
                    c = base + dirv * (wlen * 0.46) + Vector((0, 0, -0.05 * ln))
                    nrm = norm(dirv.cross(vaxis))
                    if nrm.z < 0:
                        nrm = -nrm
                    # explicit axes: u along the branch, v across
                    _card_axes(vm, c, dirv, vaxis, wlen, wid * (1.0 if lod == 0 else 1.5), LEAF_CELLS[LEAF_CONIFER],
                               norm(Vector((math.cos(a), math.sin(a), 0.5 * (1 - t) + 0.5)) ), t, rng.random(), 0.35 + 0.65 * min(1, 0.4 + (1 - (1 - t)) * 0.6))
        # leader top
        for k in range(2):
            a = k * math.pi / 2
            ax = Vector((math.cos(a), math.sin(a), 0))
            _card_axes(vm, Vector((0, 0, H * 0.96)), Z, ax, H * 0.1, H * 0.05, LEAF_CELLS[LEAF_CONIFER], ax, 1.0, rng.random(), 0.9)
        _blob_normals(vm)
    else:
        # three stacked cones, 8 sides, opaque
        tiers = [(0.14, 0.55, 1.0), (0.42, 0.8, 0.75), (0.68, 1.0, 0.5)]
        n = 8
        for (z0, z1, rf) in tiers:
            rb = Rmax * rf * (1 + 0.1 * rng.random())
            ids_b = [vm.vert((rb * math.cos(2 * math.pi * i / n), rb * math.sin(2 * math.pi * i / n), H * z0)) for i in range(n)]
            apex = vm.vert((0, 0, H * (z1 + 0.18)))
            for i in range(n):
                j = (i + 1) % n
                vm.face([ids_b[i], ids_b[j], apex], "veg_canopy_far", [(0, 0), (1, 0), (0.5, 1)], [None] * 3, [(1, 0, 0.4 + 0.2 * rng.random())] * 3)
        vm.tube([Vector((0, 0, 0)), Vector((0, 0, H * 0.2))], [0.012 * H, 0.01 * H], 4, "veg_bark")
        _blob_normals(vm)
    return vm


def _card_axes(vm, c, u_axis, v_axis, w, h, rect, shade_n, wind, phase, ao):
    u_axis, v_axis = norm(u_axis), norm(v_axis)
    pts = [c - u_axis * (w / 2) - v_axis * (h / 2), c + u_axis * (w / 2) - v_axis * (h / 2),
           c + u_axis * (w / 2) + v_axis * (h / 2), c - u_axis * (w / 2) + v_axis * (h / 2)]
    ids = [vm.vert(p) for p in pts]
    u0, v0, u1, v1 = rect
    vm.face(ids, "veg_leaf", [(u0, v0), (u1, v0), (u1, v1), (u0, v1)], [tuple(norm(shade_n))] * 4, [(wind, phase, ao)] * 4)
