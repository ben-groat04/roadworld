"""Renders contact sheets and composed test scenes from the exported FBX kit only (imports SM_Veg_*.fbx).

    blender --background --factory-startup --python tools/blender_kit/vegetation/render_vegetation.py -- \
        [--sheets hedges,grass,shrubs,trees,canopy,lods] [--scenes a,b,c] [--samples 24] [--out DIR]
Output default: docs/progress/2026-10-11-vegetation-kit/
"""
import sys, os, math, random, json
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from veg_common import *
import veg_render as R

args = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []


def opt(name, default):
    return args[args.index(name) + 1] if name in args else default


OUT = os.path.abspath(opt("--out", os.path.join(REPO, "docs", "progress", "2026-10-11-vegetation-kit")))
SAMPLES = int(opt("--samples", 24))
SHEETS = opt("--sheets", "hedges,grass,shrubs,trees,canopy,lods").split(",")
SCENES = opt("--scenes", "a,b,c").split(",")
SHEETS = [s for s in SHEETS if s]
SCENES = [s for s in SCENES if s]
os.makedirs(OUT, exist_ok=True)
MAN = json.load(open(os.path.join(MESH_DIR, "vegetation_manifest.json")))
ASSETS = {(a["type_id"], a["variant"]): a for a in MAN["assets"]}

_base = {}


def base_obj(tid, var, lod):
    key = (tid, var, lod)
    if key not in _base:
        a = ASSETS[(tid, var)]
        lod = min(lod, len(a["lods"]) - 1)
        ob = R.import_fbx(os.path.join(MESH_DIR, a["lods"][lod]["file"]))
        ob.hide_render = True
        ob.hide_viewport = True
        _base[key] = ob
        _base[(tid, var, lod)] = ob
    return _base[key]


def inst(tid, var, lod, loc, rot=0.0, scale=1.0, tint=None):
    return R.instance(base_obj(tid, var, lod), loc, rot, scale, tint)


def rtint(rng, spread=0.12, warm=0.06):
    k = 1 + rng.uniform(-spread, spread)
    return (k * (1 + rng.uniform(-warm, warm)), k, k * (1 + rng.uniform(-warm, warm) - 0.02))


def clear_scene_objects(keep_base=True):
    for o in list(bpy.data.objects):
        if o not in _base.values() and o.type in ('MESH', 'FONT'):
            bpy.data.objects.remove(o)


def label(text, loc, size=0.3, rot=(math.pi / 2, 0, 0)):
    cu = bpy.data.curves.new("t", 'FONT')
    cu.body = text
    cu.size = size
    cu.align_x = 'CENTER'
    ob = bpy.data.objects.new("lbl", cu)
    ob.location = loc
    ob.rotation_euler = rot
    ob.data.materials.append(R.flat_material("lblm", (0.9, 0.9, 0.9)))
    bpy.context.scene.collection.objects.link(ob)
    return ob


def ruler(x, y, length=1.0):
    n = 10
    for i in range(n):
        R.plane("r", x + i * length / n, x + (i + 1) * length / n, y, y + 0.03, 0.002,
                R.flat_material("rl%d" % (i % 2), (0.8, 0.1, 0.1) if i % 2 else (0.9, 0.9, 0.9)))


# ---- sheets -------------------------------------------------------------------------------------------------------
def render_tile(items, path, lods=(0,), size=400):
    """items: list of (tid, var). Renders one tile with the asset(s) (LODs side by side), figure or ruler for scale."""
    clear_scene_objects()
    sc = bpy.context.scene
    sc.render.resolution_x = sc.render.resolution_y = size
    R.plane("ground", -200, 200, -200, 200, 0, GROUND)
    x = 0.0
    xs = []
    maxd = 0.0
    maxh = 0.0
    for (tid, var) in items:
        a = ASSETS[(tid, var)]
        for lod in lods:
            if lod >= len(a["lods"]):
                continue
            d = a["lods"][lod]["dims_m"]
            w = max(d[0], d[1])
            ob = inst(tid, var, lod, (x + w / 2, 0, 0))
            ob.location.x = x + w * 0.5
            xs.append(ob.location.x)
            x += w * 1.15 + 0.1
            maxd = max(maxd, d[2], w)
            maxh = max(maxh, d[2])
    total_w = x
    if maxh > 1.3:
        R.figure(-0.6, -1.0 - 0.0)
        total_w += 1.0
    else:
        ruler(-0.1, -maxd * 0.5, min(1.0, max(0.5, maxd)))
    cx = total_w / 2 - (0.5 if maxh > 1.3 else 0)
    dist = max(maxd * 1.55, total_w * 1.0 / 0.9 * 0.95, 0.45)
    R.look_at(CAM, (cx, -dist * 1.1, max(0.8, maxh * 0.55) + dist * 0.12), (cx, 0, maxh * 0.45), 40)
    R.render(path)


def make_sheet(name, tiles, cols, tile=400, lods=(0,), caption=None):
    imgs = []
    tmp = os.path.join(OUT, "_tmp")
    os.makedirs(tmp, exist_ok=True)
    for i, items in enumerate(tiles):
        p = os.path.join(tmp, "%s_%02d.png" % (name, i))
        render_tile(items, p, lods, tile)
        im = bpy.data.images.load(p)
        px = np.array(im.pixels[:], np.float32).reshape(tile, tile, 4)
        imgs.append(px)
        bpy.data.images.remove(im)
    rows = (len(imgs) + cols - 1) // cols
    sheet = np.zeros((rows * tile, cols * tile, 4), np.float32)
    sheet[..., 3] = 1
    for i, px in enumerate(imgs):
        r, c = divmod(i, cols)
        rr = rows - 1 - r  # image rows are bottom-up
        sheet[rr * tile:(rr + 1) * tile, c * tile:(c + 1) * tile] = px
        # thin separator
        sheet[rr * tile:(rr + 1) * tile, c * tile:c * tile + 1] = 0.1
        sheet[rr * tile:rr * tile + 1, c * tile:(c + 1) * tile] = 0.1
    im = bpy.data.images.new("sheet", cols * tile, rows * tile, alpha=True)
    im.pixels.foreach_set(sheet.ravel())
    im.filepath_raw = os.path.join(OUT, name + ".png")
    im.file_format = 'PNG'
    im.save()
    bpy.data.images.remove(im)
    for i in range(len(imgs)):
        try:
            os.remove(os.path.join(tmp, "%s_%02d.png" % (name, i)))
        except OSError:
            pass


def variants(tid):
    return [(t, v) for (t, v) in ASSETS if t == tid]


# ---- scenes ---------------------------------------------------------------------------------------------------------
def pick_lod(dist, cuts=(18, 50)):
    return 0 if dist < cuts[0] else (1 if dist < cuts[1] else 2)


class Scene:
    def __init__(self, cam_pos):
        self.cam = Vector(cam_pos)
        self.n = 0

    def put(self, tid, var, loc, rot=0.0, scale=1.0, tint=None, cuts=(18, 50), max_lod=2, cull=None):
        d = (Vector(loc) - self.cam).length
        if cull is not None and d > cull:
            return None
        lod = min(pick_lod(d, cuts), max_lod)
        self.n += 1
        return inst(tid, var, lod, loc, rot, scale, tint)


def ground_and_road(half_len, road_w=7.3, x0=-5, ground_rgb=(0.07, 0.115, 0.03), width=120):
    R.plane("field", x0, x0 + half_len, -width, width, -0.02, R.flat_material("field", (0.07, 0.115, 0.03)))
    R.plane("road", x0, x0 + half_len, -road_w / 2, road_w / 2, 0.0, R.flat_material("asphalt", (0.075, 0.075, 0.08), 0.85))
    for sgn in (-1, 1):  # gravel / soil edge strip 0.4 m
        R.plane("edge", x0, x0 + half_len, sgn * road_w / 2, sgn * (road_w / 2 + 0.4) if sgn > 0 else sgn * (road_w / 2) - 0.4, 0.001,
                R.flat_material("gravel", (0.13, 0.1, 0.07), 0.95))


def scene_a(rng, quality_name="scene_a_rural_single_carriageway"):
    """Rural single carriageway cross-section: mown strip 2-4 m, ragged edge, hedges 3-8 m from edge, hedge trees 15-80 m."""
    clear_scene_objects()
    cam = (0.0, 0.9, 1.65)
    S = Scene(cam)
    LEN = 190
    ground_and_road(LEN, 6.4)
    half = 3.2
    # ragged edge strips both sides (1 m modules) over first 70 m, sparse after
    for sgn in (-1, 1):
        for i in range(0, 120):
            if rng.random() < 0.75:
                v = "V%02d" % rng.randint(0, 2)
                o = S.put("grass_edge_creep_strip", v, (i + 0.5, sgn * (half + 0.2), 0.0), 0 if sgn > 0 else math.pi, 1.0, rtint(rng), cuts=(12, 40), max_lod=1, cull=70)
    # mown strip with width varying 2.2-4 m, then rough fringe 1 m
    for sgn in (-1, 1):
        for xi in np.arange(0.0, 70.0, 0.14):
            wmown = 3.0 + 0.8 * math.sin(xi * 0.13 + sgn) + rng.uniform(-0.1, 0.1)
            for k in range(int(wmown * 7)):
                y = half + 0.5 + rng.uniform(0, wmown)
                if rng.random() < 0.8:
                    S.put("grass_mown_clump", "V%02d" % rng.randint(0, 3), (xi + rng.uniform(0, 0.14), sgn * y, 0), rng.uniform(0, 6.28),
                          rng.uniform(0.9, 1.4), rtint(rng, 0.12), cuts=(10, 26), max_lod=1, cull=40)
            for k in range(int(1.0 * 1.2)):
                y = half + 0.5 + wmown + rng.uniform(0, 1.3)
                S.put("grass_rough_tuft", "V%02d" % rng.randint(0, 3), (xi + rng.uniform(0, 0.14), sgn * y, 0), rng.uniform(0, 6.28),
                      rng.uniform(0.8, 1.3), rtint(rng, 0.15), cuts=(10, 28), max_lod=1, cull=45)
    # hedges
    for sgn, kind, off, vars_ in ((-1, "hedge_trimmed", 4.6, ("V00", "V01", "V02", "V03")), (1, "hedge_overgrown", 6.2, ("V00", "V01", "V02", "V03"))):
        x = 0.0
        gate = rng.uniform(45, 70) if sgn > 0 else 31.0
        gated = False
        tree_next = rng.uniform(12, 30)
        while x < LEN:
            if not gated and x > gate:
                gated = True
                if x > 0:
                    S.put(kind, "E01", (x - 2.0 + 0.0, sgn * (half + off), 0), 0.0 if True else 0, 1.0, rtint(rng, 0.1), cuts=(18, 50))
                x += 5.5
                S.put(kind, "E01", (x + 2.0, sgn * (half + off), 0), math.pi, 1.0, rtint(rng, 0.1))
                x += 4.0
                continue
            v = rng.choice(vars_)
            L = ASSETS[(kind, v)]["module_length_m"]
            hs = rng.uniform(0.85, 1.2)
            S.put(kind, v, (x + L / 2, sgn * (half + off + rng.uniform(-0.35, 0.35)), 0), 0.0 if rng.random() < 0.5 else math.pi, (1, 1, hs), rtint(rng, 0.14, 0.09), cuts=(20, 55))
            if x > tree_next:
                tid = rng.choice(["tree_broadleaf_round", "tree_broadleaf_open", "tree_broadleaf_open"])
                sz = rng.choice(["M", "L", "L"])
                v2 = "%s%d" % (sz, rng.randint(0, 1))
                S.put(tid, v2, (x + rng.uniform(0, L), sgn * (half + off + rng.uniform(0.2, 1.0)), 0), rng.uniform(0, 6.28), rng.uniform(0.85, 1.2), rtint(rng, 0.12, 0.1), cuts=(30, 90))
                tree_next = x + rng.uniform(15, 80)
            x += L
    # far wood
    for k in range(3):
        S.put("canopy_impostor_block", "V%02d" % k, (LEN - 30 + k * 18, rng.choice([-1, 1]) * rng.uniform(40, 60), 0), rng.uniform(0, 6.28), 1.0, rtint(rng), cuts=(999, 999), max_lod=0)
    cam_ob = CAM
    R.look_at(cam_ob, cam, (60.0, 0.4, 1.6), 30)
    sc = bpy.context.scene
    sc.render.resolution_x, sc.render.resolution_y = 1280, 720
    R.render(os.path.join(OUT, quality_name + "_eye.png"))
    R.look_at(cam_ob, (-14, 22, 9), (22, -2, 1.5), 32)
    R.render(os.path.join(OUT, quality_name + "_elevated.png"))
    R.look_at(cam_ob, (6, -1.5, 1.2), (14, 4.5, 1.1), 36)
    R.render(os.path.join(OUT, quality_name + "_closeup.png"))
    print("scene a instances", S.n)


def scene_b(rng):
    """Dual carriageway edge: mown strip, tussocky bank, 15-40 m tree/scrub belt."""
    clear_scene_objects()
    cam = (0.0, -0.5, 1.7)
    S = Scene(cam)
    LEN = 190
    R.plane("field", -5, LEN, -150, 150, -0.02, R.flat_material("field", (0.07, 0.115, 0.03)))
    R.plane("road", -5, LEN, -7.5, 3.0, 0.0, R.flat_material("asphalt", (0.07, 0.07, 0.075), 0.85))
    R.plane("line", -5, LEN, 2.75, 2.85, 0.002, R.flat_material("wline", (0.6, 0.6, 0.6), 0.7))
    # bank: rises from y=3.0 +4 m flat mown strip to 2 m height over 7 m (plane quad)
    me = bpy.data.meshes.new("bank")
    me.from_pydata([(-5, 3, 0), (LEN, 3, 0), (LEN, 8.5, 0.05), (-5, 8.5, 0.05), (-5, 16, 2.2), (LEN, 16, 2.2), (-5, 40, 2.4), (LEN, 40, 2.4)],
                   [], [(0, 1, 2, 3), (3, 2, 5, 4), (4, 5, 7, 6)])
    me.materials.append(R.flat_material("bank", (0.05, 0.07, 0.022)))
    ob = bpy.data.objects.new("bank", me)
    bpy.context.scene.collection.objects.link(ob)

    def bank_z(y):
        if y < 8.5:
            return 0.05 * max(0, (y - 3) / 5.5)
        if y < 16:
            return 0.05 + (y - 8.5) / 7.5 * 2.15
        return 2.2 + (min(y, 40) - 16) / 24 * 0.2
    for xi in np.arange(0.0, 70.0, 0.14):
        for k in range(16):
            y = 3.3 + rng.uniform(0, 4.5)
            S.put("grass_mown_clump", "V%02d" % rng.randint(0, 3), (xi + rng.uniform(0, 0.14), y, bank_z(y)), rng.uniform(0, 6.28), rng.uniform(0.9, 1.4), rtint(rng, 0.12),
                  cuts=(10, 26), max_lod=1, cull=40)
        for k in range(4):
            y = rng.uniform(8.0, 16.5)
            vv = rng.random()
            tid = "grass_tussock" if vv < 0.3 else "grass_rough_tuft"
            S.put(tid, "V%02d" % rng.randint(0, 3), (xi + rng.uniform(0, 0.14), y, bank_z(y)), rng.uniform(0, 6.28), rng.uniform(0.8, 1.4), rtint(rng, 0.15, 0.12),
                  cuts=(10, 28), max_lod=1, cull=50)
    for i in range(0, 120):
        if rng.random() < 0.6:
            S.put("grass_edge_creep_strip", "V%02d" % rng.randint(0, 2), (i + 0.5, 3.2, 0.0), 0.0, 1.0, rtint(rng), cuts=(12, 40), max_lod=1, cull=70)
    # belt 15-40 m wide beginning 12 m back: trees M/L + scrub underneath, canopy blocks behind
    belt_start, belt_w = 17.0, 28.0
    for xi in np.arange(-4, LEN, 4.2):
        for k in range(3):
            y = belt_start + rng.uniform(0, belt_w) * (0.4 + 0.6 * rng.random())
            if rng.random() < 0.18:
                continue
            tid = rng.choice(["tree_broadleaf_round", "tree_broadleaf_open", "tree_broadleaf_open"])
            sz = rng.choice(["M", "L", "S"])
            S.put(tid, "%s%d" % (sz, rng.randint(0, 1)), (xi + rng.uniform(-2, 2), y, 2.3), rng.uniform(0, 6.28), rng.uniform(0.85, 1.3), rtint(rng, 0.2, 0.15), cuts=(30, 90))
    for xi in np.arange(0, 90, 2.6):
        y = rng.uniform(15.5, 20)
        tid = rng.choice(["shrub_scrub", "shrub_gorse", "shrub_scrub"])
        S.put(tid, "V%02d" % rng.randint(0, 2), (xi + rng.uniform(-1, 1), y, 2.0), rng.uniform(0, 6.28), rng.uniform(0.8, 1.3), rtint(rng, 0.2, 0.12), cuts=(25, 70), cull=120)
    for k in range(6):
        S.put("canopy_impostor_block", "V%02d" % (k % 3), (30 + k * 26, rng.uniform(45, 60), 2.3), rng.uniform(0, 6.28), 1.0, rtint(rng), cuts=(999, 999), max_lod=0)
    sc = bpy.context.scene
    sc.render.resolution_x, sc.render.resolution_y = 1280, 720
    R.look_at(CAM, cam, (60.0, 8.0, 3.0), 30)
    R.render(os.path.join(OUT, "scene_b_dual_carriageway_edge_eye.png"))
    R.look_at(CAM, (-12, -22, 12), (26, 14, 2.5), 32)
    R.render(os.path.join(OUT, "scene_b_dual_carriageway_edge_elevated.png"))
    print("scene b instances", S.n)


def scene_c(rng):
    """Roundabout island with mown grass and one small tree."""
    clear_scene_objects()
    cam = (-13.0, -22.0, 5.2)
    S = Scene(cam)
    Rad = 11.0
    R.plane("field", -90, 90, -90, 90, -0.02, R.flat_material("field", (0.07, 0.115, 0.03)))
    # asphalt: big disk with island on top (disk of radius 20 built as polygon fan)
    n = 72
    verts = [(0, 0, 0.0)] + [(21 * math.cos(2 * math.pi * i / n), 21 * math.sin(2 * math.pi * i / n), 0.0) for i in range(n)]
    faces = [(0, 1 + i, 1 + (i + 1) % n) for i in range(n)]
    me = bpy.data.meshes.new("rb")
    me.from_pydata(verts, [], faces)
    me.materials.append(R.flat_material("asphalt", (0.07, 0.07, 0.075), 0.85))
    bpy.context.scene.collection.objects.link(bpy.data.objects.new("rb", me))
    # raised island 0.12 m with kerb wall
    verts2 = [(0, 0, 0.12)] + [(Rad * math.cos(2 * math.pi * i / n), Rad * math.sin(2 * math.pi * i / n), 0.12) for i in range(n)]
    me2 = bpy.data.meshes.new("island")
    me2.from_pydata(verts2, [], faces)
    me2.materials.append(R.flat_material("islandg", (0.045, 0.08, 0.02)))
    bpy.context.scene.collection.objects.link(bpy.data.objects.new("island", me2))
    kv, kf = [], []
    for i in range(n):
        a = 2 * math.pi * i / n
        kv += [(Rad * math.cos(a), Rad * math.sin(a), 0.0), (Rad * math.cos(a), Rad * math.sin(a), 0.12)]
    for i in range(n):
        j = (i + 1) % n
        kf.append((2 * i, 2 * j, 2 * j + 1, 2 * i + 1))
    me3 = bpy.data.meshes.new("kerb")
    me3.from_pydata(kv, [], kf)
    me3.materials.append(R.flat_material("kerbm", (0.35, 0.35, 0.33), 0.9))
    bpy.context.scene.collection.objects.link(bpy.data.objects.new("kerb", me3))
    # grass on island
    area = math.pi * (Rad - 0.3) ** 2
    for k in range(int(area * 45)):
        r = (Rad - 0.35) * math.sqrt(rng.random())
        a = rng.uniform(0, 6.28)
        S.put("grass_mown_clump", "V%02d" % rng.randint(0, 3), (r * math.cos(a), r * math.sin(a), 0.12), rng.uniform(0, 6.28), rng.uniform(0.9, 1.4), rtint(rng, 0.1),
              cuts=(10, 26), max_lod=1, cull=45)
    # ragged creep around the kerb (strips are along +X, +Y toward carriageway -> rotate so +Y points outward)
    for i in range(0, 64):
        a = 2 * math.pi * i / 64
        if rng.random() < 0.5:
            x, y = (Rad + 0.0) * math.cos(a), (Rad + 0.0) * math.sin(a)
            S.put("grass_edge_creep_strip", "V%02d" % rng.randint(0, 2), (x, y, 0.0), a - math.pi / 2, 1.1, rtint(rng), cuts=(12, 40), max_lod=1, cull=60)
    # weeds at kerb joints
    for i in range(36):
        a = rng.uniform(0, 6.28)
        S.put("weed_creep", "V%02d" % rng.randint(0, 3), ((Rad - 0.15) * math.cos(a), (Rad - 0.15) * math.sin(a), 0.12), rng.uniform(0, 6.28), rng.uniform(0.8, 1.4), rtint(rng, 0.12), cuts=(10, 26), max_lod=1, cull=45)
    # one small tree
    S.put("tree_broadleaf_round", "S1", (2.5, 1.0, 0.12), 0.8, 1.0, (1, 1, 1))
    # surrounding verge rough + hedge + trees on the horizon
    for k in range(40):
        a = rng.uniform(0, 6.28)
        r = rng.uniform(23, 40)
        S.put(rng.choice(["tree_broadleaf_round", "tree_broadleaf_open"]), "%s%d" % (rng.choice("ML"), rng.randint(0, 1)), (r * math.cos(a), r * math.sin(a), 0),
              rng.uniform(0, 6.28), rng.uniform(0.9, 1.2), rtint(rng, 0.2, 0.12), cuts=(30, 90))
    for k in range(5):
        S.put("canopy_impostor_block", "V%02d" % (k % 3), (math.cos(k * 1.3 + 1) * 60, math.sin(k * 1.3 + 1) * 60, 0), k, 1.0, rtint(rng), cuts=(999, 999), max_lod=0)
    sc = bpy.context.scene
    sc.render.resolution_x, sc.render.resolution_y = 1280, 720
    R.look_at(CAM, cam, (2, 2, 0.8), 28)
    R.render(os.path.join(OUT, "scene_c_roundabout_island_eye.png"))
    R.look_at(CAM, (-20, -30, 22), (0, 0, 0), 30)
    R.render(os.path.join(OUT, "scene_c_roundabout_island_elevated.png"))
    print("scene c instances", S.n)


# ---- main -----------------------------------------------------------------------------------------------------------
reset_scene()
CAM = R.setup(400, 400, SAMPLES, sky_strength=0.38, sun_strength=3.6, sun_elev=40, sun_az=35)
GROUND = R.flat_material("tile_ground", (0.05, 0.07, 0.03), 0.95)
rng = random.Random(5)

if "hedges" in SHEETS:
    tiles = [[v] for v in variants("hedge_trimmed") + variants("hedge_overgrown")]
    make_sheet("sheet_hedges_LOD0", tiles, 4, 450)
    make_sheet("sheet_hedges_LOD1_LOD2", [[v] for v in [("hedge_trimmed", "V01"), ("hedge_overgrown", "V01"), ("hedge_trimmed", "E01"), ("hedge_overgrown", "E00")]], 2, 600, lods=(0, 1, 2))
if "grass" in SHEETS:
    tiles = [[v] for t in ("grass_mown_clump", "grass_rough_tuft", "grass_tussock", "weed_creep", "bracken_patch") for v in variants(t)]
    tiles += [[v] for v in variants("grass_edge_creep_strip")]
    make_sheet("sheet_grass_LOD0", tiles, 6, 300)
    make_sheet("sheet_grass_LODs", [[(t, "V00")] for t in ("grass_mown_clump", "grass_rough_tuft", "grass_tussock", "weed_creep", "bracken_patch")], 3, 500, lods=(0, 1, 2))
if "shrubs" in SHEETS:
    tiles = [[v] for t in ("shrub_scrub", "shrub_gorse") for v in variants(t)]
    make_sheet("sheet_shrubs_LOD0", tiles, 3, 420)
    make_sheet("sheet_shrubs_LODs", [[("shrub_scrub", "V01")], [("shrub_gorse", "V01")]], 2, 600, lods=(0, 1, 2))
if "trees" in SHEETS:
    tiles = [[v] for t in ("tree_broadleaf_round", "tree_broadleaf_open") for v in variants(t)]
    tiles += [[v] for v in variants("tree_sapling_staked") + variants("tree_conifer_plantation")]
    make_sheet("sheet_trees_LOD0", tiles, 5, 380)
    make_sheet("sheet_trees_LODs", [[("tree_broadleaf_round", "L0")], [("tree_broadleaf_open", "M0")], [("tree_conifer_plantation", "V01")], [("tree_sapling_staked", "V01")]], 2, 700, lods=(0, 1, 2))
if "canopy" in SHEETS:
    make_sheet("sheet_canopy_blocks", [[v] for v in variants("canopy_impostor_block")], 3, 420, lods=(0, 1))
for s in SCENES:
    {"a": scene_a, "b": scene_b, "c": scene_c}[s](random.Random(11))
tmp = os.path.join(OUT, "_tmp")
if os.path.isdir(tmp):
    try:
        os.rmdir(tmp)
    except OSError:
        pass
print("render done")
