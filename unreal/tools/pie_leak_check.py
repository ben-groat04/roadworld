#!/usr/bin/env python3
"""Starts and stops play-in-editor N times through the live editor's MCP server and reports actor counts of the editor
world and the 'World counts' log lines of UCPRenderSubsystem (see unreal/tools/README.md, 'Driving the live editor over MCP').
Usage: python pie_leak_check.py [N] [outdir]   (editor must be running with the City Painter map open)"""
import json, os, re, sys, time
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from mcp_call import Mcp, call_toolset, capture_viewport, EDITOR_APP

SCENE = "editor_toolset.toolsets.scene.SceneTools"
LOGS = "EditorToolset.LogsToolset"


def count_roots(m):
    r = call_toolset(m, SCENE, "find_actors", {"name": "CP_RenderRoot", "tag": "", "collision_channels": []})
    return len(r) if isinstance(r, list) else r


def main():
    n = int(sys.argv[1]) if len(sys.argv) > 1 else 2
    out = sys.argv[2] if len(sys.argv) > 2 else "."
    m = Mcp()
    res = {"editor_roots_before": count_roots(m)}
    capture_viewport(m, os.path.join(out, "pie_editor_before.png"))
    for i in range(n):
        call_toolset(m, EDITOR_APP, "StartPIE", {"options": {"bSimulate": False, "playMode": "PlayMode_InViewPort", "warmupSeconds": 8}})
        res["pie_running_%d" % i] = call_toolset(m, EDITOR_APP, "IsPIERunning")
        capture_viewport(m, os.path.join(out, "pie_running_%d.png" % i))
        call_toolset(m, EDITOR_APP, "StopPIE")
        time.sleep(4)
        res["pie_running_after_stop_%d" % i] = call_toolset(m, EDITOR_APP, "IsPIERunning")
        res["editor_roots_after_%d" % i] = count_roots(m)
    entries = call_toolset(m, LOGS, "GetLogEntries", {"category": "LogCPRender", "pattern": "World counts|Ground layer|skirt", "maxEntries": 60})
    res["log_counts"] = [e for e in (entries if isinstance(entries, list) else [entries]) if "World counts" in json.dumps(e)]
    print(json.dumps(res, indent=1))
    open(os.path.join(out, "pie_leak_check.json"), "w").write(json.dumps(res, indent=1))


main()
