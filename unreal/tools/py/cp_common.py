"""Shared helpers for City Painter editor scripts (UE 5.x Python, `unreal` module).

Every script follows the same pattern:

    import os, sys
    sys.path.insert(0, os.environ.get("CP_PY_DIR", os.path.dirname(os.path.abspath(__file__))))
    import cp_common as cp

    def main():
        ...                      # raise cp.ScriptError("clear message") to fail loudly

    cp.run(main)                 # logs CP_PY_RESULT: OK / FAIL, then quits the editor

Run-EditorPython.ps1 greps the log for the "CP_PY_RESULT:" marker to decide the exit code.
"""
import os
import sys
import traceback

try:
    import unreal
except ImportError:  # pragma: no cover - lets the file be syntax-checked outside the editor
    unreal = None


class ScriptError(Exception):
    """Raise for expected, user-facing failures (missing class, missing plugin...)."""


def log(msg):
    # LogPython lines are what the runner summarises.
    if unreal is not None:
        unreal.log("CP: " + str(msg))
    else:
        print("CP: " + str(msg))


def warn(msg):
    if unreal is not None:
        unreal.log_warning("CP: " + str(msg))
    else:
        print("CP WARNING: " + str(msg))


def err(msg):
    if unreal is not None:
        unreal.log_error("CP: " + str(msg))
    else:
        print("CP ERROR: " + str(msg))


def script_args():
    """Arguments passed by Run-EditorPython.ps1 -ScriptArgs (environment CP_SCRIPT_ARGS, space separated)."""
    raw = os.environ.get("CP_SCRIPT_ARGS", "").strip()
    return raw.split() if raw else []


def project_dir():
    d = os.environ.get("CP_PROJECT_DIR")
    if d:
        return d
    if unreal is not None:
        return unreal.SystemLibrary.get_project_directory()
    raise ScriptError("CP_PROJECT_DIR not set and unreal module unavailable")


def load_class(path):
    """Load a UClass by script path, e.g. '/Script/CityPainterSim.CPRoadMeshActor'. None if missing."""
    try:
        return unreal.load_class(None, path)  # VERIFY: unreal.load_class exists in 5.8 (it does in 5.0-5.5).
    except Exception:
        pass
    try:
        return unreal.find_object(None, path)  # fallback
    except Exception:
        return None


def require_class(path):
    cls = load_class(path)
    if cls is None:
        raise ScriptError(
            "Class %s did not load. Is the CityPainterSim plugin built and enabled? "
            "Run unreal\\tools\\Build-Editor.ps1 and check the module-load lines in the editor log." % path
        )
    return cls


def quit_editor():
    if unreal is None:
        return
    try:
        unreal.SystemLibrary.quit_editor()  # VERIFY: exists in 5.8; fallback below.
    except Exception:
        try:
            unreal.SystemLibrary.execute_console_command(None, "QUIT_EDITOR")
        except Exception:
            pass


def run(main, quit_when_done=True):
    """Run main(); always print the result marker; quit the editor unless main() returns the string 'KEEP_RUNNING'
    (used by scripts that finish later from a tick callback and call finish() themselves)."""
    try:
        rv = main()
    except ScriptError as e:
        err("FAILED: %s" % e)
        finish(False, str(e), quit_when_done)
        return
    except Exception as e:  # noqa
        err("UNEXPECTED EXCEPTION: %s" % e)
        for line in traceback.format_exc().splitlines():
            err(line)
        finish(False, "exception: %s" % e, quit_when_done)
        return
    if rv == "KEEP_RUNNING":
        return
    finish(True, "", quit_when_done)


def finish(ok, reason="", quit_when_done=True):
    if ok:
        log("CP_PY_RESULT: OK")
    else:
        log("CP_PY_RESULT: FAIL " + reason)
    if quit_when_done:
        quit_editor()
