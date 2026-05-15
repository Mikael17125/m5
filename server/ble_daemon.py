#!/usr/bin/env python3
"""M5 Hub — BLE bridge daemon + HTTP control surface.

Hosts the Operator Console UI (static files in server/ui/) and forwards
notification/permission requests to an M5StickC Plus2 over BLE.
"""
import asyncio
import json
import logging
import time
import uuid
from collections import deque
from pathlib import Path
from typing import Any, Optional

from aiohttp import web
from bleak import BleakClient, BleakScanner
from bleak.exc import BleakError

try:
    import psutil
    HAS_PSUTIL = True
except ImportError:
    HAS_PSUTIL = False

# ── Paths & logging ──────────────────────────────────────────────────────────
HERE      = Path(__file__).resolve().parent
UI_DIR    = HERE / "ui"
LOG_DIR   = HERE / "logs"
LOG_FILE  = LOG_DIR / "ble_daemon.log"

# Create logs directory if it doesn't exist
LOG_DIR.mkdir(exist_ok=True)

logging.basicConfig(
    level=logging.INFO,
    format="%(asctime)s [%(levelname)s] %(message)s",
    handlers=[logging.FileHandler(LOG_FILE, mode='a', encoding='utf-8')],
)
log = logging.getLogger("ble_daemon")

# ── Constants ────────────────────────────────────────────────────────────────
DEVICE_NAME    = "M5StickMonitor"
SERVICE_UUID   = "12345678-1234-1234-1234-123456789abc"
CHAR_CMD_UUID  = "aaaabbbb-1234-1234-1234-abcdef123456"
CHAR_SPR_UUID  = "abcd1234-ab12-ab12-ab12-abcdef123456"
CHAR_EVT_UUID  = "ccccdddd-1234-1234-1234-abcdef123456"
CONFIG_PATH    = HERE / "config" / "config.json"
PORT           = 7355
RECONNECT_WAIT = 5
CHUNK_SIZE     = 176
ACTIVITY_CAP   = 200
GATE_CAP       = 100

# ── State ────────────────────────────────────────────────────────────────────
_client: Optional[BleakClient] = None
_connected = False
_loop: Optional[asyncio.AbstractEventLoop] = None
_notify_queue: Optional[asyncio.Queue] = None
_pending_future: Optional[asyncio.Future] = None
_pending_id: Optional[str] = None
_notify_buf = ""

_stats: dict = {"bat": 0, "cpu": 0, "ram": 0, "plug": 0}
_activity: "deque[dict]" = deque(maxlen=ACTIVITY_CAP)
_notifications: "deque[dict]" = deque(maxlen=100)
_gates: "deque[dict]"    = deque(maxlen=GATE_CAP)
_gates_total = 0
_pending_gate: Optional[dict] = None

# OpenCode instance registry — populated via /api/register from each plugin.
# instance_id → { server_url, token, pid, cwd, last_seen }
_instances: dict = {}
INSTANCE_STALE_S = 30  # prune after no heartbeat for this many seconds

# ── Config ────────────────────────────────────────────────────────────────────
DEFAULT_CONFIG = {
    "brightness": 200,
    "idle_ms": 30000,
    "volume": 12,
    "sprites": {},
}

def load_config() -> dict:
    try:
        d = json.loads(CONFIG_PATH.read_text())
        for k, v in DEFAULT_CONFIG.items():
            d.setdefault(k, v)
        return d
    except Exception:
        return dict(DEFAULT_CONFIG)


def save_config(c: dict) -> None:
    CONFIG_PATH.parent.mkdir(parents=True, exist_ok=True)
    CONFIG_PATH.write_text(json.dumps(c, indent=2))


# ── Activity log helpers ─────────────────────────────────────────────────────
def push_activity(kind: str, **fields: Any) -> None:
    entry = {
        "id": uuid.uuid4().hex[:10],
        "ts": time.time(),
        "kind": kind,
        **fields,
    }
    _activity.appendleft(entry)


def push_notification(**fields: Any) -> None:
    entry = {
        "id": uuid.uuid4().hex[:10],
        "ts": time.time(),
        **fields,
    }
    _notifications.appendleft(entry)


# ── BLE send helpers ──────────────────────────────────────────────────────────
async def send_cmd(payload: dict) -> bool:
    if not _connected or _client is None:
        return False
    data = json.dumps(payload, separators=(",", ":")).encode()
    chunks = [data[i:i + CHUNK_SIZE] for i in range(0, len(data), CHUNK_SIZE)]
    total = len(chunks)
    try:
        for idx, chunk in enumerate(chunks):
            frame = bytes([total, idx]) + chunk
            await _client.write_gatt_char(CHAR_CMD_UUID, frame, response=False)
            if total > 1:
                await asyncio.sleep(0.02)
        return True
    except BleakError as e:
        log.error(f"send_cmd error: {e}")
        return False


async def send_spr_frame(frame_bytes: bytes) -> bool:
    if not _connected or _client is None:
        return False
    chunks = [frame_bytes[i:i + CHUNK_SIZE] for i in range(0, len(frame_bytes), CHUNK_SIZE)]
    total = len(chunks)
    try:
        for idx, chunk in enumerate(chunks):
            pkt = bytes([total, idx]) + chunk
            await _client.write_gatt_char(CHAR_SPR_UUID, pkt, response=False)
            await asyncio.sleep(0.02)
        return True
    except BleakError as e:
        log.error(f"send_spr_frame error: {e}")
        return False


async def send_sprite(name: str, frames: list) -> bool:
    if not frames:
        return False
    w = frames[0].get("w", 32)
    h = frames[0].get("h", 32)
    ok = await send_cmd({"t": "spb", "name": name, "frames": len(frames), "w": w, "h": h})
    if not ok:
        return False
    for fi, frame in enumerate(frames):
        try:
            pal_bytes = bytes.fromhex(frame.get("pal", ""))
            px_bytes  = bytes.fromhex(frame.get("px", ""))
        except ValueError as e:
            log.error(f"Sprite frame {fi} hex decode error: {e}")
            return False
        frame_bytes = pal_bytes[:32] + px_bytes[:512]
        if len(frame_bytes) < 544:
            frame_bytes += bytes(544 - len(frame_bytes))
        await send_spr_frame(frame_bytes)
        await send_cmd({"t": "spf", "frame": fi})
        await asyncio.sleep(0.05)
    log.info(f"Sprite '{name}' sent ({len(frames)} frames)")
    return True


# ── On-connect push ───────────────────────────────────────────────────────────
async def on_connect_push():
    """Push config + all server-stored sprites to device on BLE connect."""
    await asyncio.sleep(0.5)
    cfg = load_config()
    await send_cmd({
        "t": "cfg",
        "bright": cfg["brightness"],
        "idle_ms": cfg["idle_ms"],
        "vol": cfg.get("volume", 12),
    })
    # Push all known sprites so notifications with sprite IDs render correctly.
    for name, sprite_data in cfg.get("sprites", {}).items():
        frames = sprite_data.get("frames", [])
        if frames:
            await send_sprite(name, frames)


# ── Quick Actions: BLE event → macOS shell command ──────────────────────────
# Triggered when the M5 sends {"act":"<code>"} from its Quick Actions tab.
ACTION_HANDLERS: dict[str, list[str]] = {
    # Lock the screen (Ctrl+Cmd+Q is the macOS shortcut).
    "lock":      ["osascript", "-e",
                  'tell application "System Events" to keystroke "q" using {control down, command down}'],
    # Put the Mac to sleep.
    "sleep":     ["pmset", "sleepnow"],
    # Toggle output mute.
    "mute":      ["osascript", "-e",
                  "set volume output muted not (output muted of (get volume settings))"],
    # Media play/pause (F8 key code = 100 on macOS).
    "playpause": ["osascript", "-e",
                  'tell application "System Events" to key code 100'],
    # Media next-track (F9 = key code 101).
    "next":      ["osascript", "-e",
                  'tell application "System Events" to key code 101'],
    # Show Desktop (F11 = key code 103).
    "desktop":   ["osascript", "-e",
                  'tell application "System Events" to key code 103'],
}

async def run_action(code: str) -> None:
    cmd = ACTION_HANDLERS.get(code)
    if not cmd:
        log.warning(f"Unknown action: {code}")
        push_activity("system", msg=f"Unknown action: {code}")
        return
    try:
        proc = await asyncio.create_subprocess_exec(
            *cmd,
            stdout=asyncio.subprocess.DEVNULL,
            stderr=asyncio.subprocess.PIPE,
        )
        _, err = await proc.communicate()
        if proc.returncode == 0:
            push_activity("action", code=code, msg=f"Action <strong>{code}</strong>")
            log.info(f"Action OK: {code}")
        else:
            msg = err.decode(errors="replace").strip()
            push_activity("system", msg=f"Action {code} failed: {msg[:80]}")
            log.error(f"Action {code} failed (exit {proc.returncode}): {msg}")
    except Exception as e:
        log.error(f"run_action({code}) exception: {e}")
        push_activity("system", msg=f"Action {code} error: {e}")


# ── EVT notification handler ──────────────────────────────────────────────────
def on_notify(sender, data):
    global _pending_future, _pending_id, _notify_buf
    try:
        _notify_buf += data.decode("utf-8")
        start = _notify_buf.find("{")
        end   = _notify_buf.rfind("}")
        if start != -1 and end > start:
            payload = json.loads(_notify_buf[start:end + 1])
            log.info(f"EVT: {payload}")
            recv_id = str(payload.get("id", "")).strip()
            # Quick Action event from M5 Actions tab
            if "act" in payload:
                act_code = str(payload["act"])
                _loop.call_soon_threadsafe(
                    lambda c=act_code: asyncio.ensure_future(run_action(c))
                )
            # Resolve pending notify/gate future
            elif _pending_future and not _pending_future.done():
                if recv_id == str(_pending_id).strip() or recv_id == "":
                    _loop.call_soon_threadsafe(_pending_future.set_result, payload)
            _notify_buf = ""
        if len(_notify_buf) > 1024:
            _notify_buf = ""
    except Exception as e:
        log.error(f"on_notify error: {e}")
        _notify_buf = ""


def on_disconnect(c):
    global _connected, _client
    _connected = False
    _client    = None
    push_activity("system", msg="M5 disconnected")
    log.warning("M5 disconnected")


# ── BLE connection ────────────────────────────────────────────────────────────
async def connect():
    global _client, _connected
    log.info("Scanning for M5 devices...")
    
    # METHOD 1: Scan for any BLE device, then connect first one with our UUID
    device = None
    
    # Scan for 10 seconds to find ANY device
    devices = await BleakScanner.discover(timeout=10)
    log.info(f"Found {len(devices)} BLE devices")
    
    # Filter for devices that advertise our SERVICE_UUID
    for d in devices:
        try:
            uuids = d.metadata.get('uuids', [])
            if SERVICE_UUID.lower() in [str(u).lower() for u in uuids]:
                device = d
                log.info(f"Found device by UUID: {d.name or '<unknown>'}")
                break
        except:
            pass
    
    # METHOD 2: Scan for device by name
    if device is None:
        log.info("Trying name scan...")
        device = await BleakScanner.find_device_by_name(DEVICE_NAME, timeout=10)
    
    # METHOD 3: Try connecting to ANY device that seems to be M5
    if device is None:
        log.info("Device not found, trying MAC address connect...")
        # Store first unknown UUID as potential device
        for d in devices:
            if d.address and not d.name:
                log.info(f"Trying first unknown device: {d.address}")
                device = d
                break
    
    if device is None:
        log.warning("No device found after exhaustive scan")
        return

    try:
        log.info(f"Connecting to {device.name} ({device.address})")
        _client = BleakClient(device, disconnected_callback=on_disconnect)
        await _client.connect()
        await _client.start_notify(CHAR_EVT_UUID, on_notify)
        print("\n" + "="*60)
        print("✓ BLE CONNECTED!")
        print(f"  Device: {device.name or device.address}")
        print("="*60 + "\n")
        
        _connected = True
        push_activity("system", msg=f"Connected to {device.name}")
        log.info("Connected!")
        asyncio.create_task(on_connect_push())
    except Exception as e:
        log.error(f"connect error: {e}")
        _client    = None
        print("\n" + "=" * 60)
        print("x BLE DISCONNECTED")
        print("=" * 60 + "\n")
        _connected = False


async def reconnect_loop():
    while True:
        if not _connected:
            await connect()
        await asyncio.sleep(RECONNECT_WAIT)


# ── Stats loop ────────────────────────────────────────────────────────────────
async def stats_loop():
    """Collect host stats for web UI only — no longer sent to device."""
    while True:
        await asyncio.sleep(5)
        try:
            if HAS_PSUTIL:
                bat   = psutil.sensors_battery()
                bat_p = int(bat.percent) if bat else 0
                plug  = 1 if (bat and bat.power_plugged) else 0
                cpu_p = int(psutil.cpu_percent(interval=None))
                ram_p = int(psutil.virtual_memory().percent)
            else:
                bat_p, plug, cpu_p, ram_p = 0, 0, 0, 0
            _stats.update({"bat": bat_p, "cpu": cpu_p, "ram": ram_p, "plug": plug})
        except Exception as e:
            log.error(f"stats_loop error: {e}")


# ── Notify worker ─────────────────────────────────────────────────────────────
async def notify_worker():
    global _pending_future, _pending_id
    while True:
        item = await _notify_queue.get()
        payload = item["payload"]
        future  = item.get("future")
        options = item.get("options", ["OK"])
        req_id  = payload["id"]

        if not _connected or _client is None:
            log.warning("Not connected, skipping notify")
            if future and not future.done():
                future.set_result({"choice": -1, "label": "error"})
            _notify_queue.task_done()
            continue

        _pending_id     = req_id
        _pending_future = _loop.create_future()

        ok = await send_cmd(payload)
        if not ok:
            if future and not future.done():
                future.set_result({"choice": -1, "label": "error"})
            _notify_queue.task_done()
            continue

        try:
            result = await asyncio.wait_for(asyncio.shield(_pending_future), timeout=payload.get("tout", 60) + 5)
            choice = result.get("choice", -1)
            label  = options[choice] if 0 <= choice < len(options) else "timeout"
            result["label"] = label
            if future and not future.done():
                future.set_result(result)
        except asyncio.TimeoutError:
            log.warning(f"Notify timeout for {req_id}")
            if future and not future.done():
                future.set_result({"choice": -1, "label": "timeout"})
        finally:
            _pending_future = None
            _pending_id     = None
        _notify_queue.task_done()


# ── HTTP handlers ─────────────────────────────────────────────────────────────
async def handle_index(request):
    return web.FileResponse(UI_DIR / "index.html")


async def handle_status(request):
    return web.json_response({
        "connected":    _connected,
        "device":       DEVICE_NAME,
        "queue_depth":  _notify_queue.qsize() if _notify_queue else 0,
        "stats":        _stats,
        "gates_total":  _gates_total,
        "pending_gate": _pending_gate,
    })


async def handle_activity(request):
    return web.json_response({"items": list(_activity)})


async def handle_notifications(request):
    return web.json_response({"items": list(_notifications)})


async def handle_gates(request):
    return web.json_response({"items": list(_gates), "total": _gates_total})


async def handle_get_config(request):
    cfg = load_config()
    return web.json_response(cfg)


async def handle_post_config(request):
    try:
        body = await request.json()
        cfg  = load_config()
        for k in ("brightness", "idle_ms", "volume"):
            if k in body:
                cfg[k] = body[k]
        save_config(cfg)
        await send_cmd({
            "t": "cfg",
            "bright": cfg["brightness"],
            "idle_ms": cfg["idle_ms"],
            "vol": cfg.get("volume", 12),
        })
        push_activity("system", msg=f"Config pushed · bright={cfg['brightness']} vol={cfg['volume']}")
        return web.json_response({"ok": True})
    except Exception as e:
        return web.json_response({"ok": False, "error": str(e)}, status=400)


async def handle_sprite(request):
    try:
        body   = await request.json()
        name   = body.get("name", "")
        frames = body.get("frames", [])
        if not name or not frames:
            return web.json_response({"ok": False, "error": "name and frames required"}, status=400)
        cfg = load_config()
        cfg.setdefault("sprites", {})[name] = {"frames": frames}
        save_config(cfg)
        ok = await send_sprite(name, frames)
        push_activity("system", msg=f"Sprite '{name}' uploaded ({len(frames)} frames)")
        return web.json_response({"ok": ok})
    except Exception as e:
        return web.json_response({"ok": False, "error": str(e)}, status=400)


async def handle_sprite_delete(request):
    name = request.match_info["name"]
    cfg  = load_config()
    sprites = cfg.get("sprites", {})
    if name not in sprites:
        return web.json_response({"ok": False, "error": "not found"}, status=404)
    sprites.pop(name)
    save_config(cfg)
    push_activity("system", msg=f"Sprite '{name}' deleted")
    return web.json_response({"ok": True})


async def handle_sprite_test(request):
    name = request.match_info["name"]
    cfg  = load_config()
    if name not in cfg.get("sprites", {}):
        return web.json_response({"ok": False, "error": "not found"}, status=404)
    req_id  = uuid.uuid4().hex[:8]
    options = ["OK"]
    payload = {
        "t":    "n",
        "id":   req_id,
        "ti":   f"Sprite: {name}"[:20],
        "bo":   f"Test fire of sprite '{name}' from daemon UI."[:120],
        "hc":   "cyan",
        "sp":   name,
        "vb":   "single",
        "bp":   "single",
        "led":  0,
        "op":   options,
        "tout": 8,
    }
    push_activity("notify", title=payload["ti"], body=payload["bo"],
                  color=payload["hc"], id=req_id)
    push_notification(
        title=payload["ti"], body=payload["bo"],
        color=payload["hc"], id=req_id,
        options=options, status="sprite_test",
    )
    await _notify_queue.put({"payload": payload, "future": None, "options": options})
    return web.json_response({"ok": True, "id": req_id})


def _build_notif_payload(body: dict, default_timeout: int = 60) -> tuple[dict, list[str]]:
    options = body.get("options", ["OK"])[:3]
    req_id  = uuid.uuid4().hex[:8]
    payload = {
        "t":    "n",
        "id":   req_id,
        "ti":   body.get("title", "")[:20],
        "bo":   body.get("body",  "")[:120],
        "hc":   body.get("color", "cyan"),
        "sp":   body.get("sprite", ""),
        "vb":   body.get("vibrate", "single"),
        "bp":   body.get("beep",    "single"),
        "led":  1 if body.get("led", False) else 0,
        "op":   options,
        "tout": body.get("timeout", default_timeout),
    }
    return payload, options


async def handle_notify(request):
    try:
        body = await request.json()
        payload, options = _build_notif_payload(body)
        push_activity(
            "notify",
            title=payload["ti"], body=payload["bo"],
            color=payload["hc"], id=payload["id"],
        )
        push_notification(
            title=payload["ti"], body=payload["bo"],
            color=payload["hc"], id=payload["id"],
            options=options, status="sent",
        )
        fut = _loop.create_future()
        await _notify_queue.put({"payload": payload, "future": fut, "options": options})
        result = await asyncio.wait_for(asyncio.shield(fut), timeout=body.get("timeout", 60) + 10)
        result["ok"] = True
        return web.json_response(result)
    except asyncio.TimeoutError:
        return web.json_response({"choice": -1, "label": "timeout"})
    except Exception as e:
        log.error(f"handle_notify error: {e}")
        return web.json_response({"choice": -1, "label": "error"})


async def handle_alert(request):
    try:
        body = await request.json()
        payload, options = _build_notif_payload(body, default_timeout=10)
        payload["led"] = 0
        push_activity(
            "notify",
            title=payload["ti"], body=payload["bo"],
            color=payload["hc"], id=payload["id"],
        )
        push_notification(
            title=payload["ti"], body=payload["bo"],
            color=payload["hc"], id=payload["id"],
            options=options, status="alert",
        )
        await _notify_queue.put({"payload": payload, "future": None, "options": options})
        return web.json_response({"ok": True, "id": payload["id"]})
    except Exception as e:
        return web.json_response({"ok": False, "error": str(e)}, status=400)


async def handle_gate_respond(request):
    """Resolve a pending gate from the Operator Console web UI (desktop)."""
    global _pending_future, _pending_gate
    try:
        body = await request.json()
        decision = body.get("decision")  # "Allow", "Always", or "Deny"
        if _pending_future is None or _pending_future.done():
            return web.json_response({"ok": False, "error": "No pending gate"}, status=400)
        choice_map = {"Allow": 0, "Always": 1, "Deny": 2}
        choice = choice_map.get(decision)
        if choice is None:
            return web.json_response({"ok": False, "error": f"Invalid decision: {decision}"}, status=400)
        _pending_future.set_result({"choice": choice})
        # Dismiss the notification on M5 device
        nid = (_pending_gate or {}).get("id", "")
        if nid:
            asyncio.create_task(send_cmd({"t": "d", "id": nid}))
        return web.json_response({"ok": True})
    except Exception as e:
        log.error(f"handle_gate_respond error: {e}")
        return web.json_response({"ok": False, "error": str(e)}, status=500)


async def handle_gate(request):
    """Claude Code / OpenCode permission gate. Forwards pre-tool hooks here."""
    global _pending_gate, _gates_total
    try:
        body   = await request.json()
        tool   = body.get("tool", "UnknownTool")
        detail = body.get("detail", "")
        timeout = int(body.get("timeout", 60))
        options = ["Allow", "Always", "Deny"]
        req_id  = uuid.uuid4().hex[:8]
        payload = {
            "t":    "n",
            "id":   req_id,
            "ti":   tool[:20],
            "bo":   detail[:120],
            "hc":   body.get("color", "red"),
            "sp":   "",
            "vb":   body.get("vibrate", "double"),
            "bp":   body.get("beep",    "alert"),
            "led":  1,
            "op":   options,
            "tout": timeout,
        }
        _pending_gate = {
            "tool": tool, "detail": detail, "id": req_id,
            "deadline_ms": int((time.time() + timeout) * 1000),
        }
        push_activity("gate", tool=tool, detail=detail, id=req_id, decision="pending")

        fut = _loop.create_future()
        session_id = body.get("session_id", "")
        await _notify_queue.put({"payload": payload, "future": fut, "options": options, "session_id": session_id})
        try:
            result = await asyncio.wait_for(asyncio.shield(fut), timeout=timeout + 10)
            label  = result.get("label", "error")
        except asyncio.TimeoutError:
            label  = "timeout"

        decision_map = {"Always": "always", "Allow": "once", "Deny": "deny"}
        decision = decision_map.get(label, label)

        record = {
            "ts": time.time(), "tool": tool, "detail": detail,
            "decision": decision, "id": req_id,
        }
        _gates.appendleft(record)
        _gates_total += 1
        push_activity("gate", tool=tool, detail=detail, id=req_id, decision=decision)
        _pending_gate = None

        return web.json_response({"decision": decision})
    except Exception as e:
        log.error(f"handle_gate error: {e}")
        _pending_gate = None
        return web.json_response({"decision": "error"})


async def handle_dismiss_session(request):
    """Auto-resolve all queued gate items for a session (e.g. after 'always' pressed)."""
    try:
        body       = await request.json()
        session_id = str(body.get("session_id", "")).strip()
        if not session_id:
            return web.json_response({"ok": False, "error": "missing session_id"}, status=400)

        dismissed = 0
        # Walk the queue and resolve any matching items immediately
        remaining = []
        while True:
            try:
                item = _notify_queue.get_nowait()
            except asyncio.QueueEmpty:
                break
            if item.get("session_id") == session_id:
                fut = item.get("future")
                if fut and not fut.done():
                    fut.set_result({"choice": 0, "label": "Always"})
                    dismissed += 1
                _notify_queue.task_done()
            else:
                remaining.append(item)

        for item in remaining:
            await _notify_queue.put(item)

        log.info(f"dismiss_session: sess={session_id} dismissed={dismissed}")
        return web.json_response({"ok": True, "dismissed": dismissed})
    except Exception as e:
        log.error(f"handle_dismiss_session error: {e}")
        return web.json_response({"ok": False, "error": str(e)}, status=500)


# ── OpenCode instance registry ───────────────────────────────────────────────
async def handle_register(request):
    """Plugin registers an OpenCode instance with this daemon at startup."""
    try:
        body = await request.json()
        iid  = str(body.get("instance_id") or "").strip()
        if not iid:
            return web.json_response({"ok": False, "error": "missing instance_id"}, status=400)
        _instances[iid] = {
            "server_url": body.get("server_url"),
            "token":      body.get("token"),
            "pid":        body.get("pid"),
            "cwd":        body.get("cwd"),
            "last_seen":  time.time(),
        }
        log.info(f"register instance {iid} pid={body.get('pid')} url={body.get('server_url')}")
        push_activity("system", msg=f"OpenCode instance registered ({len(_instances)} active)")
        return web.json_response({"ok": True, "instance_id": iid, "active": len(_instances)})
    except Exception as e:
        log.error(f"register error: {e}")
        return web.json_response({"ok": False, "error": str(e)}, status=500)


async def handle_heartbeat(request):
    try:
        body = await request.json()
        iid  = str(body.get("instance_id") or "").strip()
        inst = _instances.get(iid)
        if inst is None:
            return web.json_response({"ok": False, "error": "unknown instance"}, status=404)
        inst["last_seen"] = time.time()
        return web.json_response({"ok": True})
    except Exception as e:
        return web.json_response({"ok": False, "error": str(e)}, status=500)


async def handle_unregister(request):
    try:
        body = await request.json()
        iid  = str(body.get("instance_id") or "").strip()
        if iid in _instances:
            del _instances[iid]
            log.info(f"unregister instance {iid}")
        return web.json_response({"ok": True, "active": len(_instances)})
    except Exception as e:
        return web.json_response({"ok": False, "error": str(e)}, status=500)


async def handle_instances(request):
    """List active OpenCode instances. Useful for debugging."""
    return web.json_response({
        "active": len(_instances),
        "items":  [{"instance_id": k, **v} for k, v in _instances.items()],
    })


async def instance_prune_loop():
    """Remove instances that haven't heartbeat'd recently."""
    while True:
        try:
            now = time.time()
            stale = [k for k, v in _instances.items() if now - v.get("last_seen", 0) > INSTANCE_STALE_S]
            for k in stale:
                log.info(f"prune stale instance {k}")
                del _instances[k]
        except Exception as e:
            log.error(f"prune error: {e}")
        await asyncio.sleep(10)


# ── Static helpers ───────────────────────────────────────────────────────────
async def handle_static(request):
    name = request.match_info["name"]
    safe = UI_DIR / name
    try:
        safe.resolve().relative_to(UI_DIR.resolve())
    except ValueError:
        return web.Response(status=404)
    if not safe.exists() or not safe.is_file():
        return web.Response(status=404)
    return web.FileResponse(safe)


# ── Main ──────────────────────────────────────────────────────────────────────
async def main():
    from datetime import datetime
    print("=" * 40 + "\n")
    print(f"Started: {datetime.now()}\n")
    print("HTTP: 127.0.0.1:7355")
    print("Waiting for BLE device...\n")
    global _loop, _notify_queue
    _loop         = asyncio.get_running_loop()
    _notify_queue = asyncio.Queue()

    asyncio.create_task(reconnect_loop())
    asyncio.create_task(notify_worker())
    asyncio.create_task(stats_loop())
    asyncio.create_task(instance_prune_loop())

    app = web.Application()
    app.router.add_get ("/",              handle_index)
    app.router.add_get ("/{name:[\\w\\-.]+\\.(?:css|js|svg|png|jpg|ico|woff2?)}", handle_static)
    app.router.add_get ("/api/status",    handle_status)
    app.router.add_get ("/api/activity",     handle_activity)
    app.router.add_get ("/api/notifications", handle_notifications)
    app.router.add_get ("/api/gates",     handle_gates)
    app.router.add_get ("/api/config",    handle_get_config)
    app.router.add_post("/api/config",    handle_post_config)
    app.router.add_post  ("/api/sprite",                  handle_sprite)
    app.router.add_delete("/api/sprite/{name}",           handle_sprite_delete)
    app.router.add_post  ("/api/sprite/{name}/test",      handle_sprite_test)
    app.router.add_post("/notify",        handle_notify)
    app.router.add_post("/alert",         handle_alert)
    app.router.add_post("/gate",              handle_gate)
    app.router.add_post("/api/gate/respond",  handle_gate_respond)
    app.router.add_post("/api/dismiss_session", handle_dismiss_session)
    app.router.add_post("/api/register",      handle_register)
    app.router.add_post("/api/heartbeat",     handle_heartbeat)
    app.router.add_post("/api/unregister",    handle_unregister)
    app.router.add_get ("/api/instances",     handle_instances)

    runner = web.AppRunner(app)
    await runner.setup()
    await web.TCPSite(runner, "127.0.0.1", PORT).start()
    log.info(f"Daemon running on http://127.0.0.1:{PORT}")
    push_activity("system", msg=f"Daemon online on port {PORT}")
    await asyncio.Event().wait()


if __name__ == "__main__":
    try:
        asyncio.run(main())
    except KeyboardInterrupt:
        log.info("Daemon stopped")
