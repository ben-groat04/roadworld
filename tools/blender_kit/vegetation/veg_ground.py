"""Grass / ground cover, shrubs, and the low-poly canopy blocks. Cards use the grass atlas (veg_grass) or leaf atlas."""
import math, random
from mathutils import Vector
from veg_common import *

GRASS_VARIANTS = 4
STRIP_VARIANTS = 3
SHRUB_SIZES = {"shrub_scrub": (1.3, 2.0, 2.8), "shrub_gorse": (0.9, 1.3, 1.7)}


def _yaw_dir(a):
    return Vector((math.cos(a), math.sin(a), 0))


def build_grass(kind, variant_idx, lod):
    rng = random.Random(hash((kind, variant_idx)) % 100000 + 17 * variant_idx + 3)
    vm = VMesh("grass")
    s = 0.85 + 0.3 * rng.random() if lod == 0 else 1.0
    if kind == "grass_mown_clump":
        n, h, w, rows, bend, rad, rect = (8, 0.12 * s, 0.11, 2, 0.35, 0.07, crop(GRASS_CELLS[G_MOWN], 1.0, 0.46))
    elif kind == "grass_rough_tuft":
        n, h, w, rows, bend, rad, rect = (9, 0.38 * s, 0.2, 3, 0.4, 0.09, GRASS_CELLS[G_ROUGH if variant_idx % 2 == 0 else G_DRY])
    elif kind == "grass_tussock":
        n, h, w, rows, bend, rad, rect = (11, 0.85 * s, 0.4, 3, 0.55, 0.14, GRASS_CELLS[G_TUSSOCK if variant_idx % 2 == 0 else G_DRY])
    elif kind == "weed_creep":
        n, h, w, rows, bend, rad, rect = (6, 0.2 * s, 0.26, 2, 0.45, 0.05, crop(GRASS_CELLS[G_WEED], 1.0, 0.5))
    elif kind == "bracken_patch":
        n, h, w, rows, bend, rad, rect = (9, 0.95 * s, 0.5, 4, 0.55, 0.22, GRASS_CELLS[G_BRACKEN])
    else:
        raise ValueError(kind)
    if lod == 1:
        n = max(3, n // 2)
        rows = max(1, rows - 1) if kind != "weed_creep" else 1
        w *= 1.35
    elif lod == 2:
        n, rows = 2, 1
    for i in range(n):
        a = 2 * math.pi * (i + rng.uniform(-0.3, 0.3)) / n if lod == 0 else math.pi * i / n * 2
        out = _yaw_dir(a)
        base = out * rng.uniform(0, rad) if lod == 0 else Vector((0, 0, 0))
        right = Vector((-out.y, out.x, 0))
        lean = right.cross(Z)  # horizontal direction perpendicular to card plane
        # lean outward: choose sign so that bend goes along `out`
        if lean.dot(out) < 0:
            lean = -lean
        up = norm(Z + out * rng.uniform(0.05, 0.25))
        hh = h * rng.uniform(0.8, 1.15)
        # blade_card leans along right.cross(up); build using right such that lean ~ out
        r_axis = norm(up.cross(out))  # so right.cross(up) = out-ish
        vm.blade_card(base, up, r_axis, w * rng.uniform(0.85, 1.15), hh, rows, bend * rng.uniform(0.6, 1.1), rect, "veg_grass",
                      rng.random(), out)
    return vm


def build_edge_strip(variant_idx, lod):
    """1 m long ragged grass/weed mat along the carriageway edge. +Y toward carriageway: the ragged edge is at +Y (over the
    asphalt), the grass side at -Y. Pivot at the edge line centre."""
    rng = random.Random(900 + variant_idx)
    vm = VMesh("strip")
    nx = 8 if lod == 0 else 3
    ny = 3 if lod == 0 else 1
    w0, w1 = -0.30, 0.18
    ids = []
    rect = GRASS_CELLS[G_CREEP]
    u0, v0, u1, v1 = rect
    for i in range(nx + 1):
        x = -0.5 + i / nx
        row = []
        for j in range(ny + 1):
            t = j / ny
            y = w0 + (w1 - w0) * t
            if j == ny:
                # scalloped, irregular outer edge (periodic in x: first/last equal)
                y = 0.05 + 0.13 * (0.5 + 0.5 * math.sin(2 * math.pi * (x + 0.5) * 3 + variant_idx)) * (0.6 + 0.4 * math.sin(2 * math.pi * (x + 0.5) * 5 + 2 * variant_idx))
            row.append(vm.vert((x, y, 0.012 + 0.02 * t)))
        ids.append(row)
    for i in range(nx):
        for j in range(ny):
            f = [ids[i][j], ids[i + 1][j], ids[i + 1][j + 1], ids[i][j + 1]]
            uv = []
            for k in f:
                x, y, z = vm.verts[k]
                uv.append((u0 + (u1 - u0) * (x + 0.5), v0 + (v1 - v0) * ((y - w0) / (w1 - w0))))
            vm.face(f, "veg_grass", uv, [(0, 0, 1)] * 4, [(0.0, 0, 0.7)] * 4)
    # upright tufts along the inner (grass) side and a few weeds creeping out
    nt_ = 9 if lod == 0 else 3
    for i in range(nt_):
        x = -0.5 + (i + rng.uniform(0.1, 0.9)) / nt_
        y = rng.uniform(-0.2, 0.08)
        a = rng.uniform(0, math.pi)
        out = _yaw_dir(a)
        right = Vector((-out.y, out.x, 0))
        weed = rng.random() < 0.35
        rect2 = crop(GRASS_CELLS[G_WEED], 1.0, 0.5) if weed else crop(GRASS_CELLS[G_MOWN], 1.0, 0.46)
        vm.blade_card((x, y, 0.0), Z, norm(Z.cross(out)), 0.17 if weed else 0.12, 0.13 if weed else 0.12, 2 if lod == 0 else 1,
                      0.35, rect2, "veg_grass", rng.random(), out)
        if lod == 0:
            vm.blade_card((x, y, 0.0), Z, norm(Z.cross(right)), 0.17 if weed else 0.12, 0.13 if weed else 0.12, 2, 0.35, rect2,
                          "veg_grass", rng.random(), right)
    return vm


# ---- shrubs ------------------------------------------------------------------------------------------------------
def _stem(vm, rng, base, direction, length, r0, segs, sides, wind_h, curl=0.12, droop=0.0):
    pts, rad = [Vector(base)], [r0]
    d = norm(direction)
    p = Vector(base)
    for k in range(segs):
        d = norm(d + Vector((rng.uniform(-curl, curl), rng.uniform(-curl, curl), rng.uniform(-curl, curl) - droop)))
        p = p + d * length / segs
        pts.append(p.copy())
        rad.append(max(0.004, r0 * (1 - (k + 1) / segs * 0.8)))
    vm.tube(pts, rad, sides, "veg_bark", wind_fn=lambda q: min(1.0, q.z / wind_h) * 0.5)
    return pts, d


def build_shrub(kind, variant_idx, lod):
    seed = (31 if kind == "shrub_scrub" else 57) + variant_idx * 13
    rng = random.Random(seed)
    size = SHRUB_SIZES[kind][variant_idx]
    vm = VMesh("shrub")
    gorse = kind == "shrub_gorse"
    centre = Vector((0, 0, size * (0.45 if not gorse else 0.4)))
    ex = Vector((1.15, 1.15, 1.0)) if not gorse else Vector((1.35, 1.35, 0.9))
    if lod == 2:
        vm.card((0, 0, size * 0.5), Vector((0, 1, 0)), Z, size * 1.5, size, LEAF_CELLS[LEAF_GORSE if gorse else LEAF_HEDGE], "veg_leaf",
                (0, 0, 1), 0.5, 0.5, 0.7)
        vm.card((0, 0, size * 0.5), Vector((1, 0, 0)), Z, size * 1.5, size, LEAF_CELLS[LEAF_GORSE if gorse else LEAF_HEDGE], "veg_leaf",
                (0, 0, 1), 0.5, 0.5, 0.7)
        blob(vm, centre, Vector((size * 0.45 * ex.x, size * 0.45 * ex.y, size * 0.4)), "veg_canopy_far", rng, 0.12, 0)
        _blob_normals(vm)
        return vm
    nstem = (9 if not gorse else 13) + variant_idx * 2
    tips = []
    for i in range(nstem):
        a = 2 * math.pi * (i + rng.uniform(-0.3, 0.3)) / nstem
        incl = rng.uniform(0.3, 0.9) if not gorse else rng.uniform(0.5, 1.15)
        d = Vector((math.cos(a) * math.sin(incl), math.sin(a) * math.sin(incl), math.cos(incl)))
        ln = size * rng.uniform(0.45, 0.75)
        base = Vector((math.cos(a), math.sin(a), 0)) * rng.uniform(0, 0.08 * size)
        if lod == 0:
            pts, d_end = _stem(vm, rng, base, d, ln, 0.012 * size ** 0.6 + 0.008, 3, 3 if size < 2 else 4, size, droop=0.06 if not gorse else 0)
        else:
            pts = [base, base + d * ln]
        tips.append((pts[-1], d))
        for k in range(2 if lod == 0 else 0):
            j = rng.randint(1, len(pts) - 1)
            bd = norm(d + Vector((rng.uniform(-1, 1), rng.uniform(-1, 1), rng.uniform(-0.3, 0.6))))
            bp, bde = _stem(vm, rng, pts[j], bd, ln * rng.uniform(0.3, 0.5), 0.006, 2, 3, size)
            tips.append((bp[-1], bde))
    if gorse:
        blob(vm, centre, Vector((size * 0.42 * ex.x, size * 0.42 * ex.y, size * 0.34)), "veg_canopy_far", rng, 0.1, 0)
    rect = LEAF_CELLS[LEAF_GORSE] if gorse else LEAF_CELLS[LEAF_BROAD]
    rect_b = LEAF_CELLS[LEAF_GORSE] if gorse else LEAF_CELLS[LEAF_HEDGE]
    ncl = {0: 5 if not gorse else 7, 1: 6}[lod]
    csz = (0.42 if not gorse else 0.52) * size ** 0.75 * (1.0 if lod == 0 else 1.45)
    for (tp, d) in tips:
        for k in range(ncl):
            off = Vector((rng.uniform(-1, 1), rng.uniform(-1, 1), rng.uniform(-0.6, 1.0))) * csz * 0.35
            c = tp + off - d * csz * 0.15
            out = norm((c - centre).normalized() * 0.8 + Vector((rng.uniform(-1, 1), rng.uniform(-1, 1), rng.uniform(-1, 1))) * 0.7)
            shade = norm((c - centre).normalized() * 0.75 + Z * 0.25)
            ao = max(0.45, min(1.0, 0.3 + 0.9 * ((c - centre).length / (size * 0.7))))
            vm.card(c, out, Z, csz * rng.uniform(0.8, 1.2), csz * rng.uniform(0.8, 1.2), rect if (gorse or rng.random() < 0.35) else rect_b,
                    "veg_leaf", shade, min(1.0, c.z / size), rng.random(), ao, roll=rng.uniform(0, 6.28), flip_u=rng.random() < 0.5)
    if gorse:
        _blob_normals(vm)
    return vm


# ---- low poly blobs (far LODs, canopy blocks) ---------------------------------------------------------------------
_ICO = None


def _ico():
    global _ICO
    if _ICO is None:
        t = (1 + 5 ** 0.5) / 2
        v = [(-1, t, 0), (1, t, 0), (-1, -t, 0), (1, -t, 0), (0, -1, t), (0, 1, t), (0, -1, -t), (0, 1, -t), (t, 0, -1), (t, 0, 1), (-t, 0, -1), (-t, 0, 1)]
        v = [tuple(Vector(p).normalized()) for p in v]
        f = [(0, 11, 5), (0, 5, 1), (0, 1, 7), (0, 7, 10), (0, 10, 11), (1, 5, 9), (5, 11, 4), (11, 10, 2), (10, 7, 6), (7, 1, 8),
             (3, 9, 4), (3, 4, 2), (3, 2, 6), (3, 6, 8), (3, 8, 9), (4, 9, 5), (2, 4, 11), (6, 2, 10), (8, 6, 7), (9, 8, 1)]
        _ICO = (v, f)
    return _ICO


def blob(vm, centre, radii, mat, rng, jitter=0.15, subdiv=0, colour_var=0.15):
    v, f = _ico()
    if subdiv:
        nv, nf, cache = list(v), [], {}

        def mid(a, b):
            k = (min(a, b), max(a, b))
            if k not in cache:
                nv.append(tuple((Vector(nv[a]) + Vector(nv[b])).normalized()))
                cache[k] = len(nv) - 1
            return cache[k]
        for (a, b, c) in f:
            ab, bc, ca = mid(a, b), mid(b, c), mid(c, a)
            nf += [(a, ab, ca), (b, bc, ab), (c, ca, bc), (ab, bc, ca)]
        v, f = nv, nf
    jit = [1 + rng.uniform(-jitter, jitter) for _ in v]
    ids = [vm.vert(Vector(centre) + Vector((p[0] * radii.x, p[1] * radii.y, p[2] * radii.z)) * jit[i]) for i, p in enumerate(v)]
    shade = 0.75 + rng.uniform(-colour_var, colour_var)
    for (a, b, c) in f:
        vm.face([ids[a], ids[b], ids[c]], mat, [(0, 0), (1, 0), (0, 1)], [None] * 3, [(1.0, 0, shade)] * 3)


def _blob_normals(vm):
    """Fill missing per-loop normals with the smooth outward normal (position relative to the mesh centroid is not used:
    use the face normal averaged per vertex)."""
    acc = {}
    fn = []
    for f in vm.faces:
        P = [Vector(vm.verts[k]) for k in f]
        n = Vector()
        for a in range(1, len(P) - 1):
            n += (P[a] - P[0]).cross(P[a + 1] - P[0])
        fn.append(n.normalized() if n.length > 1e-12 else Vector((0, 0, 1)))
    for fi, f in enumerate(vm.faces):
        for k in f:
            acc[k] = acc.get(k, Vector()) + fn[fi]
    loop = 0
    for fi, f in enumerate(vm.faces):
        for k in f:
            if vm.nrm[loop] is None:
                vm.nrm[loop] = tuple(norm(acc[k]))
            loop += 1


def build_canopy_block(variant_idx, lod):
    """Lumpy low-poly woodland canopy volume, ~24 x 16 m footprint, 10-14 m tall; pivot ground centre."""
    rng = random.Random(700 + variant_idx)
    vm = VMesh("canopy")
    L = (24.0, 20.0, 28.0)[variant_idx]
    Wd = (16.0, 18.0, 14.0)[variant_idx]
    H0 = (11.0, 13.0, 12.0)[variant_idx]
    nx, ny = (12, 7) if lod == 0 else (4, 3)
    f1 = [rng.uniform(0, 6.28) for _ in range(4)]
    ids = []
    for i in range(nx + 1):
        row = []
        for j in range(ny + 1):
            u, v = i / nx, j / ny
            x, y = (u - 0.5) * L, (v - 0.5) * Wd
            # rounded footprint: pull the perimeter in a little with noise
            edge = min(u, 1 - u, v, 1 - v)
            k = min(1.0, edge / 0.18)
            z = H0 * (0.35 + 0.65 * math.sqrt(k)) * (1 + 0.16 * math.sin(7 * u + f1[0]) * math.sin(5 * v + f1[1]) + rng.uniform(-0.07, 0.07)) if edge > 0 else 0.0
            if edge <= 0:
                z = H0 * 0.2
            x += rng.uniform(-0.6, 0.6) * (1 if 0 < i < nx else 0)
            y += rng.uniform(-0.6, 0.6) * (1 if 0 < j < ny else 0)
            row.append(vm.vert((x, y, max(z, 0.5) if edge > 0 else 0.0)))
        ids.append(row)
    shade = lambda: 0.55 + rng.uniform(-0.2, 0.3)
    for i in range(nx):
        for j in range(ny):
            q = [ids[i][j], ids[i + 1][j], ids[i + 1][j + 1], ids[i][j + 1]]
            vm.face(q, "veg_canopy_far", [(0, 0)] * 4, [None] * 4, [(1.0, 0, shade())] * 4)
    # sides: skirt from the border ring down to the ground
    ring = []
    for i in range(nx):
        ring.append(ids[i][0])
    for j in range(ny):
        ring.append(ids[nx][j])
    for i in range(nx, 0, -1):
        ring.append(ids[i][ny])
    for j in range(ny, 0, -1):
        ring.append(ids[0][j])
    for a in range(len(ring)):
        b = (a + 1) % len(ring)
        pa, pb = Vector(vm.verts[ring[a]]), Vector(vm.verts[ring[b]])
        ga, gb = vm.vert((pa.x * 1.0, pa.y, 0.0)), vm.vert((pb.x, pb.y, 0.0))
        vm.face([ring[a], ga, gb, ring[b]], "veg_canopy_far", [(0, 0)] * 4, [None] * 4, [(0.6, 0, 0.4)] * 4)
    _fix_all_normals(vm)
    return vm


def _fix_all_normals(vm):
    _blob_normals(vm)
