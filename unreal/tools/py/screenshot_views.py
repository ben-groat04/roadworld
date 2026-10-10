"""Take high-resolution screenshots from fixed camera transforms into <project>/Saved/Screenshots/look/.

Files (default, interchange layout of CPRoadMeshActor; deterministic names, overwritten each run):
  look_01_aerial, look_02_roundabout_oblique, look_03_roundabout_approach, look_04_tee_minor_arm,
  look_07_underside (camera below the road looking up),
  look_05_slip_merge, look_06_kerb_closeup.
The former "old" argument (views of the removed CPRoadNetworkActor) now fails with a clear message.

Run (needs a rendering editor, NOT nullrhi):
    Run-EditorPython.ps1 -Script py\\screenshot_views.py -Render
    Run-EditorPython.ps1 -Script py\\screenshot_views.py -Render -ScriptArgs pie,2     # PIE mode, resolution multiplier 2

Args (space separated, via CP_SCRIPT_ARGS): "pie" = Play-In-Editor mode (not needed for CPRoadMeshActor, which builds in
the editor viewport); a bare integer = HighResShot resolution multiplier (default 1 =
current viewport size; the runner's -ResX/-ResY set the viewport size, default 1920x1080).

How it works: a Slate post-tick state machine. Per view: place the camera, wait WARMUP frames (shader compiles,
streaming, exposure adaptation), call AutomationLibrary.take_high_res_screenshot with an absolute path, wait
until the file appears, move to the next view, finally finish and quit. Fully headless (-nullrhi) is NOT possible:
there are no pixels. If nothing is produced, the fallback by hand is: open the map, run console command
`HighResShot 2` (writes to Saved/Screenshots/WindowsEditor/).

Untested against 5.8: every VERIFY line. The script fails loudly (CP_PY_RESULT: FAIL) on timeout.
"""
import math
import os
import sys
import time

sys.path.insert(0, os.environ.get("CP_PY_DIR", os.path.dirname(os.path.abspath(__file__))))
import cp_common as cp  # noqa: E402

try:
    import unreal
except ImportError:
    unreal = None

LEVEL = "/Game/Maps/RoadTest"


def look_at(eye, target):
    """Return (roll, pitch, yaw) in degrees for a camera at eye looking at target (UE: X fwd, Y right, Z up)."""
    dx, dy, dz = target[0] - eye[0], target[1] - eye[1], target[2] - eye[2]
    yaw = math.degrees(math.atan2(dy, dx))
    pitch = math.degrees(math.atan2(dz, math.hypot(dx, dy)))
    return (0.0, pitch, yaw)


def sim(x, y, z):
    """Sim metres (x east, y north, z up) -> UE cm (X north, Y east, Z up). Same swap as CPRoadMeshBuild.h."""
    return (y * 100.0, x * 100.0, z * 100.0)


# Interchange layout (sim_core/include/cp/test_layouts.h; node positions printed by the standalone check):
#   dual carriageway along y = +-5.5 (x -400..400), minor road along x = 0 (y -250..350) on a bridge over the dual
#   (deck z = 7 m at y = +-30), slip merge/diverge nodes at (+-260, +-5.5), roundabout ring radius 25 m centred
#   (0, 80) at z ~ 6.2 m (entry nodes (0, 55) south and (0, 105) north), residential T-junction at (0, 230) with a
#   side road running east. Left-hand traffic. Heights below the bridge/ring are the layout's nominal levels
#   (kerb view z is approximate: VERIFY by looking at the shot).
# name -> (eye location, target). Edit freely.
INTERCHANGE_VIEWS = [
    ("look_01_aerial", sim(0, 100, 750), sim(1, 100, 0)),                     # whole layout from 750 m, straight down
    ("look_02_roundabout_oblique", sim(-70, 10, 40), sim(0, 80, 6.2)),       # ~100 m out, ~20 degrees down
    ("look_03_roundabout_approach", sim(-1.8, 28, 8.5), sim(0, 54, 7.5)),    # driver height (1.5 m) on the bridge approach, northbound, left lane
    ("look_04_tee_minor_arm", sim(50, 228.6, 1.8), sim(0, 230, 1.6)),        # on the side road looking west at the T, 1.6 m up
    ("look_05_slip_merge", sim(215, 60, 25), sim(255, 8, 0.5)),              # oblique over the east on-slip merging into the dual
    ("look_06_kerb_closeup", sim(-2.4, 148, 4.3), sim(-3.3, 153, 3.7)),      # 0.5 m above the road, 1 m from the west kerb of the minor road
    ("look_07_underside", sim(150, -5.5, -600), sim(150, 5.5, 0)),             # 6 m BELOW the dual carriageway looking up: the underside must be culled (sky/floor show through)
]

VIEWS = INTERCHANGE_VIEWS

WARMUP_FIRST = 600   # frames before the first shot (shader compile / Lumen settling)
WARMUP_NEXT = 120
SHOT_TIMEOUT = 900   # frames to wait for a file after requesting
PROBE_MIN_MEAN = 6.0          # mean 8-bit luminance of a 160x90 probe shot that counts as "lit"
PROBE_INTERVAL = 300          # frames between probes
PROBE_MAX_SECONDS = float(os.environ.get("CP_PROBE_MAX_SECONDS", "2400"))


def png_mean(path):
    """Mean 8-bit luminance of a small non-interlaced 8-bit PNG (stdlib only: PIL is not in the editor's Python)."""
    import struct
    import zlib
    data = open(path, "rb").read()
    pos, idat, w, h, ctype, depth = 8, b"", 0, 0, 2, 8
    while pos + 8 <= len(data):
        n, typ = struct.unpack(">I4s", data[pos:pos + 8])
        body = data[pos + 8:pos + 8 + n]
        if typ == b"IHDR":
            w, h, depth, ctype = struct.unpack(">IIBB", body[:10])
        elif typ == b"IDAT":
            idat += body
        pos += 12 + n
    if depth != 8 or ctype not in (2, 6):
        return 255.0  # unknown format: do not block the run
    bpp = 3 if ctype == 2 else 4
    raw = zlib.decompress(idat)
    stride = w * bpp
    prev = bytearray(stride)
    total = 0
    for y in range(h):
        f = raw[y * (stride + 1)]
        line = bytearray(raw[y * (stride + 1) + 1:(y + 1) * (stride + 1)])
        for i in range(stride):
            a = line[i - bpp] if i >= bpp else 0
            b = prev[i]
            c = prev[i - bpp] if i >= bpp else 0
            if f == 1:
                line[i] = (line[i] + a) & 255
            elif f == 2:
                line[i] = (line[i] + b) & 255
            elif f == 3:
                line[i] = (line[i] + ((a + b) >> 1)) & 255
            elif f == 4:
                p = a + b - c
                pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                pr = a if (pa <= pb and pa <= pc) else (b if pb <= pc else c)
                line[i] = (line[i] + pr) & 255
        total += sum(line[0::bpp]) + sum(line[1::bpp]) + sum(line[2::bpp])
        prev = line
    return total / float(w * h * 3)
OUT_DIR = None
STATE = {}


def subsystem(name):
    cls = getattr(unreal, name, None)
    return unreal.get_editor_subsystem(cls) if cls is not None else None


def set_camera(eye, target, pie):
    roll, pitch, yaw = look_at(eye, target)
    loc = unreal.Vector(eye[0], eye[1], eye[2])
    rot = unreal.Rotator(roll=roll, pitch=pitch, yaw=yaw)
    if pie:
        ues = subsystem("UnrealEditorSubsystem")
        world = ues.get_game_world()  # VERIFY
        pc = unreal.GameplayStatics.get_player_controller(world, 0)
        pawn = pc.get_controlled_pawn() if pc else None
        if pawn is None:
            raise cp.ScriptError("PIE has no controlled pawn to use as a camera (set a GameMode with a DefaultPawn)")
        pawn.set_actor_location_and_rotation(loc, rot, False, False)
        pc.set_control_rotation(rot)
        return
    ues = subsystem("UnrealEditorSubsystem")
    try:
        ues.set_level_viewport_camera_info(loc, rot)  # VERIFY: UnrealEditorSubsystem.set_level_viewport_camera_info
    except Exception:
        unreal.EditorLevelLibrary.set_level_viewport_camera_info(loc, rot)  # deprecated fallback


def request_shot(path, mult):
    # VERIFY: AutomationLibrary.take_high_res_screenshot(res_x, res_y, filename, camera=None, mask_enabled=False,
    #         capture_hdr=False, comparison_tolerance=ComparisonTolerance.LOW, comparison_notes='', delay=0.2, ...)
    # res_x/res_y of 0 means "viewport size x multiplier" is not guaranteed; pass explicit sizes.
    w = int(os.environ.get("CP_RES_X", "1920")) * mult
    h = int(os.environ.get("CP_RES_Y", "1080")) * mult
    unreal.AutomationLibrary.take_high_res_screenshot(w, h, path)


def tick(delta):
    s = STATE
    s["frame"] += 1
    try:
        step(s)
    except Exception as e:  # noqa
        unreal.unregister_slate_post_tick_callback(s["handle"])
        cp.err("screenshot state machine failed: %s" % e)
        cp.finish(False, str(e))


def step(s):
    phase = s["phase"]
    if phase == "wait_pie":
        ues = subsystem("UnrealEditorSubsystem")
        if ues.get_game_world() is not None:  # VERIFY
            s["phase"] = "place"
            s["wait"] = WARMUP_FIRST
            cp.log("PIE world is up")
        elif s["frame"] > 3000:
            raise cp.ScriptError("PIE never started (rendering mode? editor_request_begin_play unsupported?)")
        return
    if phase == "place":
        if s["i"] >= len(VIEWS):
            unreal.unregister_slate_post_tick_callback(s["handle"])
            done = [v[0] for v in VIEWS]
            cp.log("screenshots written: %s" % ", ".join(os.path.join(OUT_DIR, n + ".png") for n in done))
            cp.finish(True)
            return
        name, eye, target = VIEWS[s["i"]]
        set_camera(eye, target, s["pie"])
        s["phase"] = "warm"
        return
    if phase == "probe":
        # Shaders compile in the background and unfinished materials/lights render black. Take tiny probe shots until the
        # picture is no longer black (or PROBE_MAX_SECONDS passes), so the real shots do not come out empty.
        if "probe_t0" not in s:
            s["probe_t0"] = time.time()
        probe = os.path.join(OUT_DIR, "_probe.png")
        if os.path.exists(probe):
            os.remove(probe)
        unreal.AutomationLibrary.take_high_res_screenshot(160, 90, probe)
        s["probe_path"] = probe
        s["probe_frame"] = s["frame"]
        s["phase"] = "probe_wait"
        return
    if phase == "probe_wait":
        p = s["probe_path"]
        if os.path.exists(p) and os.path.getsize(p) > 0:
            mean = png_mean(p)
            elapsed = time.time() - s["probe_t0"]
            if mean >= PROBE_MIN_MEAN or elapsed > PROBE_MAX_SECONDS:
                cp.log("probe mean %.2f/255 after %.0f s (%s)" % (mean, elapsed, "ok" if mean >= PROBE_MIN_MEAN else "TIMEOUT, shooting anyway"))
                s["probed"] = True
                s["wait"] = WARMUP_NEXT   # let exposure adapt once the scene lit up
                s["phase"] = "warm"
            else:
                if s["frame"] % 10 == 0:
                    pass
                cp.log("probe mean %.2f/255 after %.0f s: still dark, waiting" % (mean, elapsed))
                s["wait"] = PROBE_INTERVAL
                s["phase"] = "probe_idle"
        elif s["frame"] - s["probe_frame"] > SHOT_TIMEOUT:
            raise cp.ScriptError("probe screenshot never appeared: %s" % p)
        return
    if phase == "probe_idle":
        s["wait"] -= 1
        if s["wait"] <= 0:
            s["phase"] = "probe"
        return
    if phase == "warm":
        if s["wait"] > 0:
            s["wait"] -= 1
            return
        if not s.get("probed"):
            s["phase"] = "probe"
            return
        name = VIEWS[s["i"]][0]
        path = os.path.join(OUT_DIR, name + ".png")
        if os.path.exists(path):
            os.remove(path)  # deterministic: overwrite
        request_shot(path, s["mult"])
        s["path"] = path
        s["shot_frame"] = s["frame"]
        s["phase"] = "wait_file"
        cp.log("requested %s" % path)
        return
    if phase == "wait_file":
        if os.path.exists(s["path"]) and os.path.getsize(s["path"]) > 0:
            cp.log("got %s (%d bytes)" % (s["path"], os.path.getsize(s["path"])))
            s["i"] += 1
            s["wait"] = WARMUP_NEXT
            s["phase"] = "place"
        elif s["frame"] - s["shot_frame"] > SHOT_TIMEOUT:
            raise cp.ScriptError(
                "no screenshot file after %d frames: %s. Is this a rendering run (-Render), and does the editor "
                "write absolute HighResShot paths? Check Saved/Screenshots/WindowsEditor/ for stray output." % (SHOT_TIMEOUT, s["path"])
            )


def main():
    global OUT_DIR
    if unreal is None:
        raise cp.ScriptError("not running inside the Unreal editor")
    if os.environ.get("CP_RENDER", "0") != "1":
        raise cp.ScriptError("screenshots need a rendering editor: run Run-EditorPython.ps1 with -Render (nullrhi has no pixels)")
    args = cp.script_args()
    pie = "pie" in args
    global VIEWS
    if "old" in args:
        raise cp.ScriptError("the 'old' argument was removed together with CPRoadNetworkActor; run without it for the interchange views")
    VIEWS = INTERCHANGE_VIEWS
    mult = 1
    for a in args:
        if a.isdigit():
            mult = max(1, min(4, int(a)))
    OUT_DIR = os.path.join(cp.project_dir(), "Saved", "Screenshots", "look")
    os.makedirs(OUT_DIR, exist_ok=True)

    les = subsystem("LevelEditorSubsystem")
    if not les.load_level(LEVEL):
        raise cp.ScriptError("could not load %s; run setup_look_test_level.py first" % LEVEL)

    # Verified (PythonScriptPlugin EditorPythonScriptingLibrary.h, ScriptName "EditorPythonScripting"): without this the
    # plugin requests QUIT_EDITOR on the tick after the script returns, killing the state machine below.
    unreal.EditorPythonScripting.set_keep_python_script_alive(True)

    STATE.update(frame=0, i=0, wait=WARMUP_FIRST, phase="place", pie=pie, mult=mult)
    if pie:
        les.editor_request_begin_play()  # VERIFY: LevelEditorSubsystem.editor_request_begin_play
        STATE["phase"] = "wait_pie"
    STATE["handle"] = unreal.register_slate_post_tick_callback(tick)
    cp.log("screenshot run started: pie=%s mult=%d out=%s" % (pie, mult, OUT_DIR))
    return "KEEP_RUNNING"  # the tick callback calls cp.finish() when done


cp.run(main)
