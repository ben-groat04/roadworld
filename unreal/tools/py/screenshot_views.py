"""Take high-resolution screenshots from fixed camera transforms into <project>/Saved/Screenshots/look/.

Files: look_01_street.png, look_02_mid45.png, look_03_aerial.png (deterministic names; overwritten each run).

Run (needs a rendering editor, NOT nullrhi):
    Run-EditorPython.ps1 -Script py\\screenshot_views.py -Render
    Run-EditorPython.ps1 -Script py\\screenshot_views.py -Render -ScriptArgs pie,2     # PIE mode, resolution multiplier 2

Args (space separated, via CP_SCRIPT_ARGS): "pie" = Play-In-Editor mode (needed to see the CPRoadNetworkActor
roads, which are only built in BeginPlay); a bare integer = HighResShot resolution multiplier (default 1 =
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

sys.path.insert(0, os.environ.get("CP_PY_DIR", os.path.dirname(os.path.abspath(__file__))))
import cp_common as cp  # noqa: E402

try:
    import unreal
except ImportError:
    unreal = None

LEVEL = "/Game/Maps/RoadTest"
CENTER = (10000.0, 12500.0, 300.0)  # UE cm: middle of the 250 m x 290 m smoke-test network (X north, Y east)


def look_at(eye, target):
    """Return (roll, pitch, yaw) in degrees for a camera at eye looking at target (UE: X fwd, Y right, Z up)."""
    dx, dy, dz = target[0] - eye[0], target[1] - eye[1], target[2] - eye[2]
    yaw = math.degrees(math.atan2(dy, dx))
    pitch = math.degrees(math.atan2(dz, math.hypot(dx, dy)))
    return (0.0, pitch, yaw)


# name -> (eye location, target). Edit freely.
VIEWS = [
    ("look_01_street", (574.0, -1916.0, 170.0), (3000.0, 6000.0, 150.0)),      # 1.7 m eye height at the start of road 1
    ("look_02_mid45", (-8000.0, 2000.0, 9000.0), CENTER),                       # about 45 degrees down, ~120 m out
    ("look_03_aerial", (10000.0, 12500.0, 60000.0), (10000.0, 12600.0, 0.0)),   # 600 m straight down (slight yaw offset avoids gimbal)
]

WARMUP_FIRST = 600   # frames before the first shot (shader compile / Lumen settling)
WARMUP_NEXT = 120
SHOT_TIMEOUT = 900   # frames to wait for a file after requesting
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
    if phase == "warm":
        if s["wait"] > 0:
            s["wait"] -= 1
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
    mult = 1
    for a in args:
        if a.isdigit():
            mult = max(1, min(4, int(a)))
    OUT_DIR = os.path.join(cp.project_dir(), "Saved", "Screenshots", "look")
    os.makedirs(OUT_DIR, exist_ok=True)

    les = subsystem("LevelEditorSubsystem")
    if not les.load_level(LEVEL):
        raise cp.ScriptError("could not load %s; run setup_look_test_level.py first" % LEVEL)

    STATE.update(frame=0, i=0, wait=WARMUP_FIRST, phase="place", pie=pie, mult=mult)
    if pie:
        les.editor_request_begin_play()  # VERIFY: LevelEditorSubsystem.editor_request_begin_play
        STATE["phase"] = "wait_pie"
    STATE["handle"] = unreal.register_slate_post_tick_callback(tick)
    cp.log("screenshot run started: pie=%s mult=%d out=%s" % (pie, mult, OUT_DIR))
    return "KEEP_RUNNING"  # the tick callback calls cp.finish() when done


cp.run(main)
