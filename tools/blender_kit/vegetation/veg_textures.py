"""Procedural leaf / grass atlases (numpy, no downloads). Run inside Blender headless.

Writes RGBA base colour (alpha = mask), tangent normal, roughness PNGs into
unreal/CityPainter/SourceArt/Textures/Vegetation/. Row 0 of the arrays is the bottom of the image (Blender convention).
Colours (sRGB 0-255) are albedo values chosen from the reference-photo patch means in ref_colours.json, brightened
because photo patches include shadow gaps; per-instance tint supplies the variation.
"""
import bpy, os, math, numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, "..", "..", ".."))
TEX_DIR = os.path.join(REPO, "unreal", "CityPainter", "SourceArt", "Textures", "Vegetation")

# atlas layouts: (width, height, cols, rows)
LEAF_ATLAS = (1024, 1024, 2, 2)
GRASS_ATLAS = (1024, 1024, 4, 2)
SS = 2  # supersample

PAL = {
    "leaf_broad": (74, 98, 40), "leaf_hedge": (66, 90, 38), "leaf_hedge_dark": (46, 64, 30),
    "conifer": (38, 64, 38), "gorse": (60, 82, 34), "gorse_flower": (214, 184, 40),
    "grass_mown": (104, 138, 44), "grass_rough": (110, 135, 52), "grass_dry": (150, 128, 74),
    "grass_tussock": (122, 138, 66), "weed": (88, 122, 52), "bracken": (82, 118, 38),
}


def srgb_lin(c):
    c = np.asarray(c, dtype=np.float32) / 255.0
    return np.where(c <= 0.04045, c / 12.92, ((c + 0.055) / 1.055) ** 2.4)


class Cell:
    """One atlas cell drawn at SS x resolution."""

    def __init__(self, w, h):
        self.w, self.h = w * SS, h * SS
        self.rgb = np.zeros((self.h, self.w, 3), np.float32)
        self.a = np.zeros((self.h, self.w), np.float32)
        self.hgt = np.zeros((self.h, self.w), np.float32)
        yy, xx = np.mgrid[0:self.h, 0:self.w]
        self.xx = xx.astype(np.float32)
        self.yy = yy.astype(np.float32)

    def shape(self, base, direction, length, width_fn, colour, curve=0.0, hgt=0.5, tent=0.25, tip_light=0.25,
              base_dark=0.35, midrib=0.0):
        """Draw one elongated shape. base (x,y) in cell fractions; direction radians (0 = +x); length in px (unsupersampled);
        width_fn(t) -> half width as fraction of length; curve bends it sideways."""
        L = length * SS
        if L < 2:
            return
        bx, by = base[0] * self.w, base[1] * self.h
        ca, sa = math.cos(direction), math.sin(direction)
        reach = L * 1.6 + 6
        x0, x1 = int(max(0, bx - reach)), int(min(self.w, bx + reach))
        y0, y1 = int(max(0, by - reach)), int(min(self.h, by + reach))
        if x1 <= x0 or y1 <= y0:
            return
        sl = (slice(y0, y1), slice(x0, x1))
        px = self.xx[sl] - bx
        py = self.yy[sl] - by
        u = px * ca + py * sa
        v = -px * sa + py * ca
        t = u / L
        inside = (t > 0) & (t < 1)
        tt = np.clip(t, 0, 1)
        v2 = v - curve * L * tt * tt
        wv = np.maximum(width_fn(tt) * L, 1.0)
        m = inside & (np.abs(v2) < wv)
        if not m.any():
            return
        rel = v2 / wv
        shade = (1 - base_dark * (1 - tt)) * (1 + tip_light * tt) * (1 - 0.18 * np.abs(rel))
        if midrib > 0:
            shade = shade * (1 + midrib * np.exp(-(v2 / (1.2 * SS)) ** 2))
        col = srgb_lin(colour)
        h = hgt + tent * (1 - np.abs(rel)) * 0.4 + 0.02 * tt
        w = m
        sub = self.rgb[sl]
        sub[w] = col[None, :] * shade[w][:, None]
        self.a[sl][w] = 1.0
        self.hgt[sl][w] = h[w]

    def dot(self, c, r, colour, hgt=0.5):
        bx, by = c[0] * self.w, c[1] * self.h
        rr = r * SS
        x0, x1 = int(max(0, bx - rr - 1)), int(min(self.w, bx + rr + 2))
        y0, y1 = int(max(0, by - rr - 1)), int(min(self.h, by + rr + 2))
        sl = (slice(y0, y1), slice(x0, x1))
        m = ((self.xx[sl] - bx) ** 2 + (self.yy[sl] - by) ** 2) < rr * rr
        self.rgb[sl][m] = srgb_lin(colour)
        self.a[sl][m] = 1.0
        self.hgt[sl][m] = hgt

    def finish(self):
        k = SS

        def ds(x):
            return x.reshape(self.h // k, k, self.w // k, k, *x.shape[2:]).mean(axis=(1, 3))
        a = ds(self.a)
        rgb = ds(self.rgb * self.a[..., None]) / np.maximum(a[..., None], 1e-4)
        hgt = ds(self.hgt)
        msk = (a > 0.5)
        mean = (rgb * msk[..., None]).sum(axis=(0, 1)) / max(1.0, float(msk.sum()))
        rgb = np.where(a[..., None] > 0.02, rgb, mean[None, None, :])
        return rgb, a, hgt


def w_leaf(width):
    return lambda t: width * np.sin(np.pi * np.clip(t, 0, 1) ** 0.75) ** 0.9


def w_blade(width):
    return lambda t: width * (1 - np.clip(t, 0, 1)) ** 0.8 * (0.5 + 0.5 * np.minimum(1, t * 12))


def w_needle(width):
    return lambda t: width * (1 - np.clip(t, 0, 1) * 0.6)


def w_const(width):
    return lambda t: width + 0 * t


def jit(rng, c, amt):
    f = 1 + rng.uniform(-amt, amt)
    return tuple(int(np.clip(v * f * (1 + rng.uniform(-0.04, 0.04)), 0, 255)) for v in c)


# ---- leaf atlas cells ------------------------------------------------------------------------------------------
def cell_broadleaf(w, h, rng, pal="leaf_broad", n=30, lsize=0.30, spread=1.0):
    c = Cell(w, h)
    cx, cy = 0.5, 0.5
    leaves = []
    for i in range(n):
        ang = rng.uniform(0, 2 * math.pi)
        rad = math.sqrt(rng.uniform(0.02, 1.0)) * 0.30 * spread
        px, py = cx + rad * math.cos(ang), cy + rad * math.sin(ang)
        leaves.append((px, py, ang + rng.uniform(-0.6, 0.6), lsize * rng.uniform(0.7, 1.15)))
    leaves.sort(key=lambda l: l[1])
    for (px, py, ang, ls) in leaves[::3]:
        c.shape((cx, cy), math.atan2(py - cy, px - cx), math.hypot(px - cx, py - cy) * w, w_const(0.012), (60, 50, 32),
                hgt=0.3, tent=0)
    for (px, py, ang, ls) in leaves:
        col = jit(rng, PAL[pal], 0.18)
        c.shape((px, py), ang, ls * h * 0.5, w_leaf(0.36), col, curve=rng.uniform(-0.12, 0.12),
                hgt=rng.uniform(0.35, 0.8), tent=0.4, midrib=0.25)
    return c


def cell_hedge_leaf(w, h, rng):
    c = Cell(w, h)
    leaves = []
    for i in range(190):
        ang = rng.uniform(0, 2 * math.pi)
        rad = math.sqrt(rng.uniform(0.0, 1.0)) * 0.40
        leaves.append((0.5 + rad * math.cos(ang), 0.5 + rad * math.sin(ang), rng.uniform(0, 2 * math.pi), rng.uniform(0.7, 1.2)))
    leaves.sort(key=lambda l: l[1])
    for (px, py, ang, s) in leaves:
        dark = rng.random() < 0.35
        col = jit(rng, PAL["leaf_hedge_dark" if dark else "leaf_hedge"], 0.2)
        c.shape((px, py), ang, 0.115 * h * s, w_leaf(0.42), col, curve=rng.uniform(-0.15, 0.15),
                hgt=rng.uniform(0.3, 0.9), tent=0.4, midrib=0.2)
    return c


def cell_conifer(w, h, rng):
    c = Cell(w, h)
    c.shape((0.04, 0.5), 0.0, 0.92 * w, w_const(0.008), (55, 40, 28), curve=0.04, hgt=0.4, tent=0)
    for k in range(34):
        t = 0.05 + 0.9 * k / 34
        tl = (1 - t) * 0.42 + 0.05
        for side in (-1, 1):
            ang = side * rng.uniform(0.9, 1.25)
            bx, by = 0.04 + t * 0.92, 0.5
            c.shape((bx, by), ang, tl * h * 0.9, w_leaf(0.09), jit(rng, PAL["conifer"], 0.15), hgt=rng.uniform(0.4, 0.8), tent=0.2)
            for j in range(4):
                tt = 0.3 + 0.22 * j
                c.shape((bx + math.cos(ang) * tl * 0.9 * tt * h / w, by + math.sin(ang) * tl * 0.9 * tt),
                        ang + side * rng.uniform(-0.5, 0.3), tl * h * 0.25, w_needle(0.06), jit(rng, PAL["conifer"], 0.2),
                        hgt=rng.uniform(0.5, 0.9), tent=0.1)
    return c


def cell_gorse(w, h, rng):
    c = Cell(w, h)
    for k in range(95):
        ang = rng.uniform(0, 2 * math.pi)
        r = math.sqrt(rng.uniform(0.02, 1)) * 0.28
        bx, by = 0.5 + r * math.cos(ang) * 0.6, 0.5 + r * math.sin(ang) * 0.6
        c.shape((bx, by), ang, rng.uniform(0.14, 0.26) * h, w_needle(0.035), jit(rng, PAL["gorse"], 0.2),
                curve=rng.uniform(-0.1, 0.1), hgt=rng.uniform(0.3, 0.9), tent=0.1)
    for k in range(7):
        ang = rng.uniform(0, 2 * math.pi)
        r = rng.uniform(0.05, 0.33)
        c.dot((0.5 + r * math.cos(ang), 0.5 + r * math.sin(ang)), 0.017 * h, jit(rng, PAL["gorse_flower"], 0.1), hgt=0.95)
    return c


# ---- grass atlas cells (cell aspect 1:2) -----------------------------------------------------------------------
def cell_blades(w, h, rng, n, pal, lmin, lmax, spread, width, curve, dry_mix=0.0):
    c = Cell(w, h)
    bl = []
    for i in range(n):
        x = 0.5 + rng.uniform(-spread, spread) * 0.5
        ang = math.pi / 2 - (x - 0.5) * rng.uniform(0.2, 1.1) + rng.uniform(-0.15, 0.15)
        bl.append((x, rng.uniform(0.0, 0.03), ang, rng.uniform(lmin, lmax), rng.uniform(-curve, curve)))
    bl.sort(key=lambda b: b[3])
    for (x, y, ang, ln, cv) in bl:
        pcol = PAL["grass_dry"] if rng.random() < dry_mix else PAL[pal]
        c.shape((x, y), ang, ln * h, w_blade(width), jit(rng, pcol, 0.2), curve=cv, hgt=rng.uniform(0.3, 0.9), tent=0.3,
                base_dark=0.55, tip_light=0.3)
    return c


def cell_weed(w, h, rng):
    c = Cell(w, h)
    for i in range(11):
        ang = math.pi / 2 + rng.uniform(-1.25, 1.25)
        ln = rng.uniform(0.18, 0.42) * h
        col = jit(rng, PAL["weed"], 0.2)
        c.shape((0.5, 0.03), ang, ln, w_leaf(0.3), col, curve=rng.uniform(-0.2, 0.2), hgt=rng.uniform(0.3, 0.9), tent=0.4, midrib=0.3)
        for s in (-1, 1):
            for tt in (0.35, 0.6):
                bx = 0.5 + math.cos(ang) * ln * tt / w
                by = 0.03 + math.sin(ang) * ln * tt / h
                c.shape((bx, by), ang + s * 1.0, ln * 0.35, w_leaf(0.4), col, hgt=0.5, tent=0.3)
    return c


def cell_bracken(w, h, rng):
    c = Cell(w, h)
    N = 18
    c.shape((0.5, 0.02), math.pi / 2 - 0.04, 0.95 * h, w_const(0.006), (70, 62, 30), curve=-0.09, hgt=0.4, tent=0)
    for k in range(3, N):
        t = k / N
        px, py = 0.5 + 0.18 * t * t * 0.9, 0.02 + 0.93 * t
        ln = (0.42 * (1 - t) ** 0.7 + 0.04) * w
        for side in (-1, 1):
            ang = math.pi / 2 + side * rng.uniform(1.0, 1.25)
            c.shape((px, py), ang, ln, w_leaf(0.17), jit(rng, PAL["bracken"], 0.15), curve=side * 0.08,
                    hgt=rng.uniform(0.4, 0.9), tent=0.3, midrib=0.2)
    return c


def cell_creep(w, h, rng):
    """Top-down patch of short grass and weed leaves for flat ground strips (ragged edge baked in)."""
    c = Cell(w, h)
    for i in range(900):
        x, y = rng.uniform(0.04, 0.96), rng.uniform(0.02, 0.98)
        edge = min(x, 1 - x, y, 1 - y)
        if edge < 0.16 and rng.random() < (0.16 - edge) / 0.16 * 1.05:
            continue
        ang = rng.uniform(0, 2 * math.pi)
        weed = rng.random() < 0.3
        c.shape((x, y), ang, rng.uniform(0.08, 0.17) * w * (1.5 if weed else 1.0), w_leaf(0.3) if weed else w_blade(0.1),
                jit(rng, PAL["weed" if weed else "grass_mown"], 0.2), hgt=rng.uniform(0.3, 0.9), tent=0.2)
    return c


def assemble(size, cells, normal_strength=3.0, rough=0.62):
    W, H, cols, rows = size
    cw, ch = W // cols, H // rows
    rgb = np.zeros((H, W, 3), np.float32)
    a = np.zeros((H, W), np.float32)
    hg = np.zeros((H, W), np.float32)
    for idx, cell in cells.items():
        r, cc = divmod(idx, cols)
        cr, ca, chg = cell.finish()
        rgb[r * ch:(r + 1) * ch, cc * cw:(cc + 1) * cw] = cr
        a[r * ch:(r + 1) * ch, cc * cw:(cc + 1) * cw] = ca
        hg[r * ch:(r + 1) * ch, cc * cw:(cc + 1) * cw] = chg
    gx = (np.roll(hg, -1, 1) - np.roll(hg, 1, 1)) * normal_strength
    gy = (np.roll(hg, -1, 0) - np.roll(hg, 1, 0)) * normal_strength
    n = np.stack([-gx, -gy, np.ones_like(gx)], -1)
    n /= np.linalg.norm(n, axis=-1, keepdims=True)
    nrm = n * 0.5 + 0.5
    r_ = np.clip(rough - 0.12 * (hg - 0.5), 0.3, 0.9)
    return rgb, a, nrm, r_


def save_png(path, arr, name, is_data):
    h, w = arr.shape[:2]
    img = bpy.data.images.new(name, w, h, alpha=True, float_buffer=False)
    img.colorspace_settings.name = 'Non-Color' if is_data else 'sRGB'
    px = np.ones((h, w, 4), np.float32)
    px[..., :arr.shape[2]] = arr
    if not is_data:
        c = px[..., :3]
        px[..., :3] = np.where(c <= 0.0031308, 12.92 * c, 1.055 * np.power(np.maximum(c, 0), 1 / 2.4) - 0.055)
    img.pixels.foreach_set(px.ravel())
    img.filepath_raw = path
    img.file_format = 'PNG'
    img.save()
    return img


def bake_all(seed=7):
    os.makedirs(TEX_DIR, exist_ok=True)
    rng = np.random.default_rng(seed)
    W, H, cols, rows = LEAF_ATLAS
    cw, ch = W // cols, H // rows
    cells = {0: cell_broadleaf(cw, ch, rng), 1: cell_hedge_leaf(cw, ch, rng), 2: cell_conifer(cw, ch, rng), 3: cell_gorse(cw, ch, rng)}
    rgb, a, nrm, rg = assemble(LEAF_ATLAS, cells, 3.0, 0.6)
    save_png(os.path.join(TEX_DIR, "T_Veg_Leaf_BC.png"), np.concatenate([rgb, a[..., None]], -1), "T_Veg_Leaf_BC", False)
    save_png(os.path.join(TEX_DIR, "T_Veg_Leaf_N.png"), nrm, "T_Veg_Leaf_N", True)
    save_png(os.path.join(TEX_DIR, "T_Veg_Leaf_R.png"), np.repeat(rg[..., None], 3, -1), "T_Veg_Leaf_R", True)
    W, H, cols, rows = GRASS_ATLAS
    cw, ch = W // cols, H // rows
    cells = {
        0: cell_blades(cw, ch, rng, 46, "grass_mown", 0.22, 0.42, 1.0, 0.045, 0.06),
        1: cell_blades(cw, ch, rng, 30, "grass_rough", 0.55, 0.95, 0.9, 0.032, 0.18, 0.15),
        2: cell_blades(cw, ch, rng, 26, "grass_tussock", 0.7, 0.99, 0.55, 0.028, 0.32, 0.3),
        3: cell_weed(cw, ch, rng),
        4: cell_bracken(cw, ch, rng),
        5: cell_blades(cw, ch, rng, 30, "grass_rough", 0.5, 0.95, 0.9, 0.032, 0.2, 0.9),
        6: cell_creep(cw, ch, rng),
    }
    rgb, a, nrm, rg = assemble(GRASS_ATLAS, cells, 2.5, 0.68)
    save_png(os.path.join(TEX_DIR, "T_Veg_Grass_BC.png"), np.concatenate([rgb, a[..., None]], -1), "T_Veg_Grass_BC", False)
    save_png(os.path.join(TEX_DIR, "T_Veg_Grass_N.png"), nrm, "T_Veg_Grass_N", True)
    save_png(os.path.join(TEX_DIR, "T_Veg_Grass_R.png"), np.repeat(rg[..., None], 3, -1), "T_Veg_Grass_R", True)
    return {"leaf": {"size": LEAF_ATLAS, "cells": {"0": "broadleaf cluster", "1": "hedge small leaves", "2": "conifer spray", "3": "gorse/scrub spray"}},
            "grass": {"size": GRASS_ATLAS, "cells": {"0": "mown blades", "1": "rough tuft", "2": "tussock", "3": "weed rosette",
                                                     "4": "bracken frond", "5": "dry blades", "6": "creep ground patch"}}}


if __name__ == "__main__":
    print(bake_all())
