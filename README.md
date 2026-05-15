# M5 Hub

A physical notification and permission terminal for your desk. An M5StickC Plus2 displays alerts, quick actions, and Claude Code permission gates over BLE — controlled from a Python daemon with a built-in web console.

## What It Does

- **Notifications** — Push alerts to the device with custom colors, vibration patterns, beep sounds, and LED blink. Up to 3 choice buttons per notification.
- **Permission Gates** — Integrates with Claude Code's pre-tool hooks. Tool execution pauses until you Allow, Always-allow, or Deny on the device.
- **Quick Actions** — One-tap macOS controls: screen lock, sleep, mute, play/pause, next track, show desktop.
- **Inbox** — Notification history browsable on-device.
- **Custom Sprites** — Upload 4-bit paletted pixel art (32×32) for notification headers and the idle screen.
- **Operator Console** — Web UI at `http://127.0.0.1:7355` for monitoring, testing notifications, managing sprites, and device config.

## Hardware

- [M5StickC Plus2](https://docs.m5stack.com/en/core/M5StickC%20Plus2) (ESP32, 135×240 LCD, IMU, mic, buzzer, IR, buttons)
- USB-C connection for flashing

## Setup

### Firmware

Requires [PlatformIO](https://docs.platformio.org/en/latest/core/installation.html) CLI or the VS Code extension.

```bash
pio run                        # compile
pio run --target upload        # flash (USB, 1.5 MBaud)
pio device monitor             # serial at 115200
```

### Server

```bash
pip install aiohttp bleak      # required
pip install psutil             # optional — host battery/CPU/RAM stats
python server/ble_daemon.py
```

The daemon scans for the BLE device named `M5StickMonitor`, connects, and auto-reconnects every 5 seconds. HTTP API starts on `127.0.0.1:7355`.

Config stored at `~/.config/m5hub/config.json`. Logs at `~/ble_daemon.log`.

## OpenCode Integration

The M5 hub integrates with [OpenCode](https://opencode.ai) as a global plugin. When OpenCode asks for permission to run a tool, the request appears simultaneously on the M5 device and in the native OpenCode TUI dialog — first responder wins. No custom UI overlay; the native dialog remains the primary interface.

### How It Works

1. OpenCode fires a `permission.asked` event via its plugin system.
2. The plugin (`~/.config/opencode/plugins/ble-gate.ts`) receives the event, forwards it to the daemon's `/gate` endpoint.
3. The daemon pushes a notification to the M5 device (tool name + file pattern).
4. You tap **Allow**, **Always**, or **Deny** on the M5.
5. The plugin receives the decision and replies to OpenCode's permission API.
6. If you tap **Always**, all other pending permissions in the same session are auto-approved — no further taps needed.

### Setup

1. Copy the plugin to your OpenCode global plugins directory:

```bash
cp server/ble_gate_plugin.ts ~/.config/opencode/plugins/ble-gate.ts
```

2. Register it in `~/.config/opencode/opencode.json`:

```json
{
  "plugin": ["~/.config/opencode/plugins/ble-gate.ts"]
}
```

3. Start the daemon before opening OpenCode in the project directory.

The plugin self-activates only when OpenCode is opened in a directory containing `m5` in its path. Logs are written to `/tmp/ble-gate.log`.

### Plugin Architecture

```
OpenCode TUI
  └─ plugin event hook (permission.asked)
       └─ POST /gate → ble_daemon.py
            └─ BLE notify → M5StickC Plus2
                 └─ user taps Allow/Always/Deny
            └─ decision returned to plugin
       └─ POST /session/:id/permissions/:id → OpenCode API
```

The plugin registers itself with the daemon on startup (POST `/api/register`) and sends heartbeats every 10 seconds. When `decision=always` is received, the plugin immediately resolves all other queued permissions for that session and calls `/api/dismiss_session` to drain the M5 notification queue.

## Claude Code Integration

`ble_gate.py` is a [pre-tool hook](https://docs.anthropic.com/en/docs/claude-code/hooks) for Claude Code (the CLI):

1. Claude Code invokes `ble_gate.py` before running a tool.
2. The script sends the tool name + detail to the daemon's `/gate` endpoint.
3. The daemon pushes a notification to the M5 device.
4. You tap **Allow**, **Always**, or **Deny** on the device.
5. The script exits 0 (allow) or 2 (block).

Add to your Claude Code settings (`~/.claude/settings.json`):

```json
{
  "hooks": {
    "preToolUse": [
      {
        "matcher": "",
        "hooks": [
          { "type": "command", "command": "python /path/to/m5/server/ble_gate.py" }
        ]
      }
    ]
  }
}
```

Selecting "Always" patches `~/.claude/settings.json` to auto-allow that tool going forward.

## HTTP API

| Method | Endpoint              | Description                              |
|--------|-----------------------|------------------------------------------|
| GET    | `/api/status`         | Connection state, stats, queue depth     |
| GET    | `/api/activity`       | Recent event log                         |
| GET    | `/api/gates`          | Permission gate history                  |
| GET    | `/api/config`         | Current device config                    |
| POST   | `/api/config`         | Update + push config to device           |
| POST   | `/notify`             | Send notification (waits for response)   |
| POST   | `/alert`              | Send fire-and-forget alert               |
| POST   | `/gate`               | Permission gate (used by ble_gate.py)    |
| POST   | `/api/sprite`         | Upload sprite                            |
| DELETE | `/api/sprite/{name}`  | Delete sprite                            |
| POST   | `/api/sprite/{name}/idle`   | Set sprite as idle screen           |
| POST   | `/api/sprite/{name}/test`   | Test-fire sprite as notification     |
| POST   | `/api/register`       | Register an OpenCode plugin instance     |
| POST   | `/api/heartbeat`      | Plugin keepalive                         |
| POST   | `/api/unregister`     | Deregister plugin instance               |
| GET    | `/api/instances`      | List active plugin instances             |
| POST   | `/api/dismiss_session`| Cancel queued M5 gates for a session     |

### Notify / Alert Body

```json
{
  "title": "Alert",
  "body": "Something happened",
  "color": "cyan",
  "sprite": "",
  "vibrate": "single",
  "beep": "single",
  "led": true,
  "options": ["OK", "Dismiss"],
  "timeout": 60
}
```

Fields: `title` (max 20), `body` (max 120), `color` (`cyan|red|green|yellow|orange|purple`), `vibrate` (`off|single|double|triple|long`), `beep` (`off|single|double|success|error|alert`), `options` (max 3).

## Architecture

```
src/              ESP32 firmware (Arduino / PlatformIO)
  main.cpp          setup/loop, screen state machine
  ble.h/cpp         BLE GATT server (3 characteristics)
  state.h/cpp       globals, config persistence (NVS)
  ui.h/cpp          screen rendering + button input
  sprite.h/cpp      sprite storage, drawing, frame assembly
  util.h/cpp        JSON parsers, vib/beep, helpers

server/           Python host
  ble_daemon.py     BLE bridge + HTTP API (aiohttp + bleak)
  ble_gate.py       Claude Code pre-tool permission hook
  ui/               Operator Console (static HTML/JS/CSS)
```

### BLE Protocol

Single GATT service with three characteristics:

| UUID suffix | Purpose     | Direction        |
|-------------|-------------|------------------|
| `aaaabbbb`  | Commands    | Host → Device    |
| `abcd1234`  | Sprite data | Host → Device    |
| `ccccdddd`  | Events      | Device → Host    |

Commands are JSON, chunked at 176 bytes with a `[total, idx]` header. Sprite frames are fixed 544 bytes (32-byte palette + 512-byte pixel data). Events flow back as JSON notifications (choices, action triggers).

## Project Structure Conventions

- `state.h` declares all globals; `state.cpp` defines them. New globals go in both files.
- Screen enum: tabs first (`SCR_MONITOR` through `SCR_SETTINGS`), `TAB_COUNT` separator, then sub-screens.
- Colors are RGB565 constants in `state.h`.
- `util.cpp` JSON parsers assume well-formed input — no validation.
- Canvas renders to a 135×240 `LGFX_Sprite`, not directly to the display.
- `lib_ignore = DFRobot_GP8XXX` in `platformio.ini` prevents a build conflict — don't remove.
