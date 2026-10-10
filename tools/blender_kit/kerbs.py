"""Precast concrete kerbs (UK, 915 mm units, BS 7263 / EN 1340 style).

Local frame: origin at the ROAD-SIDE BOTTOM EDGE of the unit's START face. +X along the run, +Y toward the
carriageway (the road face is the plane y = 0 and the unit body lies at y < 0, i.e. away from the road),
+Z up from the bed. Laid with the road surface at z = kerb.buried (0.130 m), so HB2 shows 125 mm.
"""
import math, random
from mathutils import Vector
import common as C
from common import Builder


def _profile(w, h, batter_h, batter, rnd=None):
    """Road-face profile points (y, z), CCW-ish; y=0 road face, body at y<0."""
    if rnd is None:
        hv = h - batter_h if h - batter_h > 0.04 else h * 0.4
        bat = batter * (h - hv) / batter_h
    if rnd:   # bullnose: rounded top road edge
        pts = [(0, 0), (0, h - rnd)]
        for i in range(1, 7):
            a = math.pi / 2 * i / 6
            pts.append((-rnd + rnd * math.cos(a), h - rnd + rnd * math.sin(a)))
        pts += [(-w, h), (-w, 0)]
        return pts
    return [(0, 0), (0, hv), (-bat, h), (-w, h), (-w, 0)]


def _straight_sections(prof_a, prof_b, length):
    return [[(0.0, y, z) for (y, z) in prof_a], [(length, y, z) for (y, z) in prof_b]]


def _arc_sections(prof, R, centre_road_side, angle_len, nsec):
    """Sweep profile along an arc whose road-face radius is R. centre_road_side: centre of curvature on +Y."""
    secs = []
    for i in range(nsec + 1):
        th = angle_len * i / nsec
        ring = []
        for (y, z) in prof:
            if centre_road_side:
                r = R - y
                ring.append((r * math.sin(th), R - r * math.cos(th), z))
            else:
                r = R + y
                ring.append((r * math.sin(th), -R + r * math.cos(th), z))
        secs.append(ring)
    return secs


def _finish_chips(ob, seed, chip_mm, jitter_mm, L, h):
    """Variant wear: deterministic vertex jitter on the upper zone plus one corner chip."""
    rng = random.Random(seed)
    me = ob.data
    for v in me.vertices:
        if v.co.z > h * 0.6:
            s = jitter_mm / 1000.0
            v.co.x += rng.uniform(-s, s) * 0.5
            v.co.y += rng.uniform(-s, s)
            v.co.z += rng.uniform(-s, s) * 0.5
    cx = rng.choice([0.0, L]); cz = h
    rad = 0.035
    for v in me.vertices:
        if v.co.z > h - rad and abs(v.co.x - cx) < rad and v.co.y > -0.06:
            fall = max(0.0, 1.0 - math.hypot(v.co.x - cx, v.co.z - cz) / rad)
            d = chip_mm / 1000.0 * fall
            v.co.z -= d
            v.co.x += d * (1 if cx == 0.0 else -1) * 0.6
    me.update()


def _unit(name, sections, K, caps=True):
    b = Builder(name)
    b.loft(sections, "kerb_concrete", bevel=K["bevel"], segs=K["bevel_segs"], caps=caps)
    return b


def pieces():
    K = C.PARAMS["kerb"]
    L = K["length"] - K["joint"]       # modelled length leaves the 5 mm joint
    out = []

    def add(name, builder, pivot, notes, params, variant_of=None, post=None, tris_proxy=12):
        out.append(dict(name=name, category="Kerbs", builder=builder, pivot=pivot, notes=notes,
                        params=params, post=post, proxy_tris=tris_proxy))

    hb = _profile(K["hb_w"], K["hb_h"], K["hb_batter_h"], K["hb_batter"])
    piv = "road-side bottom edge of the start face; +X along run, +Y toward road, body at -Y"
    for i in range(4):
        nm = "SM_Kerb_HB2" + ("" if i == 0 else "_v%d" % i)
        post = None
        if i > 0:
            post = (lambda s: (lambda ob: _finish_chips(ob, s, K["variant_chip_mm"], K["variant_jitter_mm"], L, K["hb_h"])))(C.PARAMS["seed"] + i)
        secs = _straight_sections(hb, hb, L)
        # intermediate loops so variants have density to jitter
        mids = [[(L * t / 6, y, z) for (y, z) in hb] for t in range(1, 6)]
        add(nm, _unit(nm, [secs[0]] + mids + [secs[1]], K), piv,
            "125 x 255 half-battered, 125 mm upstand when laid" + ("" if i == 0 else "; chip/jitter variant %d" % i),
            {"w": K["hb_w"], "h": K["hb_h"], "len": L}, post=post)

    bn = _profile(K["bn_w"], K["bn_h"], 0, 0, rnd=K["bn_r"])
    add("SM_Kerb_Bullnose", _unit("SM_Kerb_Bullnose", _straight_sections(bn, bn, L), K), piv,
        "150 x 255 bullnosed", {"w": K["bn_w"], "h": K["bn_h"], "r": K["bn_r"]})

    lo = _profile(K["hb_w"], K["drop_h"], K["hb_batter_h"], K["hb_batter"])
    for nm, a, bb, note in (("SM_Kerb_TransitionL", hb, lo, "full height at start -> dropped at end (+X)"),
                            ("SM_Kerb_TransitionR", lo, hb, "dropped at start -> full height at end (+X)")):
        add(nm, _unit(nm, _straight_sections(a, bb, L), K), piv, note, {"low_h": K["drop_h"]})

    dr = _profile(K["hb_w"], K["dropper_h"], K["hb_batter_h"], K["hb_batter"])
    add("SM_Kerb_FlushDropper", _unit("SM_Kerb_FlushDropper", _straight_sections(dr, dr, L), K), piv,
        "dropped kerb for crossings, ~10 mm upstand", {"h": K["dropper_h"]})

    ed = [(0, 0), (0, K["edge_h"] - 0.01), (-0.01, K["edge_h"]), (-K["edge_w"], K["edge_h"]), (-K["edge_w"], 0)]
    add("SM_Kerb_Edging", _unit("SM_Kerb_Edging", _straight_sections(ed, ed, L), K), piv,
        "50 x 150 edging kerb (footway back / bed edge)", {"w": K["edge_w"], "h": K["edge_h"]})

    for R in K["radii"]:
        ang = L / R
        n = max(3, int(math.ceil(ang / (math.pi / 20))))
        for tag, centre_road in (("In", True), ("Out", False)):
            nm = "SM_Kerb_Curve_R%d_%s" % (int(R), tag)
            add(nm, _unit(nm, _arc_sections(hb, R, centre_road, ang, n), K), piv,
                "HB2 section swept on road-face radius %g m; %s: centre of curvature on the %s side"
                % (R, tag, "road (+Y; concave face, e.g. footway corner)" if centre_road else "footway (-Y; convex face, island)"),
                {"radius": R, "arc_len": L, "centre_side": "+Y" if centre_road else "-Y"})

    Rn = K["nose_radius"]
    n = 12
    nm = "SM_Kerb_IslandNose"
    add(nm, _unit(nm, _arc_sections(hb, Rn, False, math.pi, n), K), "centre of the nose arc at the road-face start; arc sweeps 180 degrees about -Y centre",
        "traffic-island nosing: HB2 swept round a %.2f m radius semicircle; pair with two straight units" % Rn,
        {"radius": Rn, "sweep_deg": 180})
    return out
