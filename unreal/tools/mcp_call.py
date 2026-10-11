#!/usr/bin/env python3
"""Minimal MCP streamable-HTTP client for the live Unreal editor (Epic ModelContextProtocol plugin, default http://127.0.0.1:8000/mcp).

Usage:
  python mcp_call.py tools                     # tools/list
  python mcp_call.py call <tool> '<json args>' # tools/call
  python mcp_call.py raw <method> '<json params>'
As a module: from mcp_call import Mcp; m = Mcp(); m.call('tool', {...}).
Every call opens (or reuses) a session: initialize -> Mcp-Session-Id header -> notifications/initialized.
Responses are plain JSON or one SSE 'data:' frame; both are handled.
"""
import json, sys, urllib.request, urllib.error

URL = "http://127.0.0.1:8000/mcp"


class Mcp:
    def __init__(self, url=URL, timeout=300):
        self.url, self.timeout, self.sid, self.n = url, timeout, None, 0
        r = self.rpc("initialize", {"protocolVersion": "2025-06-18", "capabilities": {},
                                    "clientInfo": {"name": "cp-playtest", "version": "1"}})
        self.rpc("notifications/initialized", None, notify=True)
        self.init = r

    def rpc(self, method, params, notify=False):
        self.n += 1
        body = {"jsonrpc": "2.0", "method": method}
        if params is not None:
            body["params"] = params
        if not notify:
            body["id"] = self.n
        h = {"Content-Type": "application/json", "Accept": "application/json, text/event-stream"}
        if self.sid:
            h["Mcp-Session-Id"] = self.sid
            h["MCP-Protocol-Version"] = "2025-06-18"
        req = urllib.request.Request(self.url, json.dumps(body).encode(), h)
        try:
            with urllib.request.urlopen(req, timeout=self.timeout) as resp:
                self.sid = resp.headers.get("Mcp-Session-Id") or self.sid
                text = resp.read().decode("utf-8", "replace")
        except urllib.error.HTTPError as e:
            text = e.read().decode("utf-8", "replace")
            raise RuntimeError("HTTP %d: %s" % (e.code, text[:500]))
        if notify or not text.strip():
            return None
        if text.lstrip().startswith(("event:", "data:")):
            text = "".join(l[5:].strip() for l in text.splitlines() if l.startswith("data:"))
        msg = json.loads(text)
        if "error" in msg:
            raise RuntimeError(json.dumps(msg["error"]))
        return msg.get("result")

    def tools(self):
        return self.rpc("tools/list", {})

    def call(self, name, args=None):
        return self.rpc("tools/call", {"name": name, "arguments": args or {}})


def call_toolset(m, toolset, tool, args=None):
    """Call a toolset tool through the call_tool meta-tool (tool search mode is on). Returns the parsed returnValue."""
    r = m.call("call_tool", {"toolset_name": toolset, "tool_name": tool, "arguments": args or {}})
    t = text_of(r)
    if r.get("isError"):
        raise RuntimeError(t[:800])
    try:
        j = json.loads(t)
        return j.get("returnValue", j) if isinstance(j, dict) else j
    except ValueError:
        return t


EDITOR_APP = "EditorToolset.EditorAppToolset"


def capture_viewport(m, out_png, transform=None, show_ui=True):
    """CaptureViewport of the level editor viewport (or PIE viewport) to a PNG file; returns the metadata dict."""
    import base64
    if transform is None:   # the tool wants an explicit pose: use the viewport camera's current one
        transform = call_toolset(m, EDITOR_APP, "GetCameraTransform")
    args = {"captureTransform": transform, "annotations": {"gridSpacing": 0, "gridExtent": 0, "gridHeight": 0, "maxLabels": 0}, "bShowUI": show_ui}
    r = call_toolset(m, EDITOR_APP, "CaptureViewport", args)
    img = r["image"]
    open(out_png, "wb").write(base64.b64decode(img["data"]))
    return {k: v for k, v in r.items() if k != "image"}


def text_of(result):
    return "\n".join(c.get("text", "") for c in (result or {}).get("content", []) if c.get("type") == "text")


if __name__ == "__main__":
    m = Mcp()
    cmd = sys.argv[1] if len(sys.argv) > 1 else "tools"
    if cmd == "tools":
        print(json.dumps(m.tools(), indent=1))
    elif cmd == "call":
        print(json.dumps(m.call(sys.argv[2], json.loads(sys.argv[3]) if len(sys.argv) > 3 else {}), indent=1))
    else:
        print(json.dumps(m.rpc(sys.argv[2], json.loads(sys.argv[3]) if len(sys.argv) > 3 else {}), indent=1))
