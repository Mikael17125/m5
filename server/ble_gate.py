#!/usr/bin/env python3
import json, sys, urllib.request, urllib.error
from pathlib import Path

HERE = Path(__file__).resolve().parent

DAEMON_URL   = "http://127.0.0.1:7355"
USER_TIMEOUT = 75

def parse_hook():
    try:
        return json.loads(sys.stdin.read())
    except Exception:
        return {}

def make_detail(hook):
    inp = hook.get("tool_input", {})
    if "command"   in inp: return inp["command"][:120]
    if "file_path" in inp: return f"{hook.get('tool_name','')}: {inp['file_path']}"[:120]
    return json.dumps(inp)[:120]

def call_daemon(tool, detail):
    payload = json.dumps({
        "tool": tool, "detail": detail,
        "title": tool, "body": detail,
        "color": "red", "vibrate": "double", "beep": "alert",
        "options": ["Allow", "Always", "Deny"],
        "led": True, "timeout": 60
    }).encode()
    req = urllib.request.Request(
        f"{DAEMON_URL}/gate", data=payload,
        headers={"Content-Type": "application/json"}, method="POST"
    )
    try:
        with urllib.request.urlopen(req, timeout=USER_TIMEOUT) as resp:
            return json.loads(resp.read()).get("decision", "error")
    except urllib.error.URLError:
        print("[ble_gate] Daemon not reachable, failing open", file=sys.stderr)
        return "error"

SETTINGS_PATH = HERE / "claude_settings.json"

def is_always_allowed(tool):
    try:
        settings = json.loads(SETTINGS_PATH.read_text())
        return tool in settings.get("permissions", {}).get("allow", [])
    except Exception:
        return False

def patch_always_allow(tool):
    try:
        settings = json.loads(SETTINGS_PATH.read_text())
    except Exception:
        settings = {}
    perms = settings.setdefault("permissions", {})
    allow = perms.setdefault("allow", [])
    if tool not in allow:
        allow.append(tool)
        SETTINGS_PATH.write_text(json.dumps(settings, indent=2))
        print(f"[ble_gate] Added '{tool}' to permissions.allow", file=sys.stderr)

def main():
    hook   = parse_hook()
    tool   = hook.get("tool_name", "UnknownTool")
    detail = make_detail(hook)
    print(f"[ble_gate] {tool}: {detail[:80]}", file=sys.stderr)

    if is_always_allowed(tool):
        print(f"[ble_gate] always-allowed, skip", file=sys.stderr)
        sys.exit(0)

    decision = call_daemon(tool, detail)
    print(f"[ble_gate] decision={decision}", file=sys.stderr)

    if decision == "always":
        patch_always_allow(tool)
        sys.exit(0)
    elif decision in ("once", "error"):
        sys.exit(0)
    elif decision == "deny":
        print(json.dumps({"decision": "block", "reason": f"User denied '{tool}' on M5."}))
        sys.exit(2)
    elif decision == "timeout":
        print(json.dumps({"decision": "block", "reason": f"Permission for '{tool}' timed out."}))
        sys.exit(2)
    else:
        sys.exit(0)

if __name__ == "__main__":
    main()
