# M5 Hub — Agent Instructions

Dual-language project: ESP32 C++ firmware + Python BLE bridge daemon.

## Architecture

```
src/           C++ Arduino firmware (M5StickC Plus2)
  main.cpp       setup/loop, idle avatar lifecycle, render loop
  ble.h/cpp      BLE GATT server (3 characteristics), inbox ring buffer, cmd dispatch
  state.h/cpp    all globals, config struct, persistence (NVS Preferences)
  ui.h/cpp       screen rendering, button input, avatar face (M5Stack-Avatar)
  sprite.h/cpp   sprite storage/drawing, icon helpers (logo, battery, tab icons)
  util.h/cpp     JSON parsers (no validation), vib/beep, color/vib/beep parsers
server/        Python host-side
  ble_daemon.py  BLE bridge + HTTP API (aiohttp, port 7355)
  ble_gate.py    Claude Code pre-tool permission hook
  run.sh         Daemon launcher (tees to log)
  preview_avatar.py  Avatar face preview renderer (PIL, no BLE dependency)
  config/        Runtime config (config.json, gitignored)
  logs/          Daemon logs (gitignored)
  ui/            Operator Console web UI (static HTML/JS/CSS)
```

No CI pipeline. No automated tests. No linter config.

## Build & Flash (PlatformIO)

```bash
pio run                          # compile firmware
pio run --target upload          # flash to device (USB, 1.5 MBaud)
pio device monitor               # serial monitor at 115200
```

- Board ID: `m5stick-c` (PlatformIO env name is `m5stick-c-plus2`). Framework: `arduino`, Platform: `espressif32`.
- Partition: `huge_app.csv` (8 MB flash).
- `lib_ignore = DFRobot_GP8XXX` — do not remove; prevents build conflict with M5StickCPlus2 lib.
- `lib_deps`: `m5stack/M5StickCPlus2@^1.0.2`, `meganetaaan/M5Stack-Avatar@^0.10.0`.
- Build artifacts live in `.pio/` (gitignored).
- `pio run` requires PlatformIO CLI (`pip install platformio` or VS Code extension). Not a CMake project.

## Server

```bash
pip install aiohttp bleak        # required
pip install psutil               # optional, for host battery/CPU stats
pip install Pillow               # optional, for preview_avatar.py
python server/ble_daemon.py      # starts HTTP on 127.0.0.1:7355
bash server/run.sh               # alternative: launches + tees to log
```

- Config: `server/config/config.json` (relative to script, auto-created). Stores brightness, idle_ms, volume, sprites.
- Logs: `server/logs/ble_daemon.log` (relative to script, auto-created).
- Port hardcoded as `PORT = 7355` in `ble_daemon.py`. Not configurable via env or CLI.
- Both `config/` and `logs/` are gitignored.

## BLE Protocol

Firmware and server share these UUIDs (must stay in sync):
- Service: `12345678-1234-1234-1234-123456789abc`
- CMD char: `aaaabbbb-1234-1234-1234-abcdef123456`
- SPR char: `abcd1234-ab12-ab12-ab12-abcdef123456`
- EVT char: `ccccdddd-1234-1234-1234-abcdef123456`

Commands are JSON, chunked at 176 bytes (`CHUNK_SIZE`) with `[total, idx]` header.
Sprite frames: 32-byte palette (16× RGB565 BE) + up to 512-byte pixel data (4bpp indexed) = 544 bytes fixed.
MTU set to 512 on firmware side.

## HTTP API

Daemon exposes these routes on `127.0.0.1:7355`:
- `GET  /` — Operator Console UI
- `GET  /api/status` — connection state, queue depth, host stats, gate info
- `GET  /api/activity` — activity log (capped at 200)
- `GET  /api/notifications` — notification history
- `GET  /api/gates` — permission gate history
- `GET  /api/config` — read config JSON
- `POST /api/config` — update config + push to device
- `POST /api/sprite` — upload sprite (name + frames), stores in config
- `DELETE /api/sprite/{name}` — remove sprite from config
- `POST /api/sprite/{name}/test` — test-fire sprite as notification
- `POST /notify` — send notification, awaits response (choice)
- `POST /alert` — send one-way alert (no response awaited)
- `POST /gate` — Claude Code permission gate (await allow/deny/always)

## Screen State Machine

```
Tabs (cycle with BtnA):
  SCR_INBOX (0)     → scroll with BtnB, open detail with BtnPWR
  SCR_ACTIONS (1)   → scroll with BtnB, execute with BtnPWR
  SCR_SETTINGS (2)  → scroll with BtnB, enter edit with BtnPWR

Sub-screens:
  SCR_INBOX_DETAIL  → BtnB cycles items, BtnA/BtnPWR back to inbox
  SCR_SETTINGS_EDIT → BtnB steps value, BtnA saves, BtnPWR cancels/reloads
  SCR_NOTIFY        → BtnA tap=opt0, double-tap=opt1, long-press=last opt; auto-timeout
  SCR_IDLE          → anime avatar face (M5Stack-Avatar), any button wakes to prevTab
```

## Key Conventions

- `state.h` owns all globals, structs, enums, color constants; `state.cpp` defines them. Add new globals to both.
- Screen enum order matters: `SCR_INBOX..SCR_SETTINGS` are tabs (0–2), `TAB_COUNT=3` separator, then sub-screens.
- 3 tabs total: Inbox, Actions, Settings. No Monitor tab.
- Color constants are RGB565 (`C_BG`, `C_HEADER`, `C_ACCENT`, `C_WHITE`, `C_GRAY2`, etc.), defined in `state.h`.
- `util.cpp` JSON parsers (`jsonStr`, `jsonInt`) assume well-formed input from daemon — no validation.
- Quick Actions (`lock`, `sleep`, `mute`, `playpause`, `next`, `desktop`) map to macOS shell commands via `osascript`/`pmset` in `ble_daemon.py`.
- BLE events from device: `{"id":"...", "choice":N}` for notification responses, `{"act":"<code>"}` for quick actions.
- `ble_gate.py` reads Claude Code hook JSON from stdin, calls `/gate` on daemon, exits 0 (allow) or 2 (block).
- Gate **fails open**: if daemon unreachable, tool execution is allowed automatically.
- "Always-allow" writes to `server/claude_settings.json` (local to repo), not `~/.claude/settings.json`.
- On BLE connect, daemon auto-pushes config + all stored sprites to device (`on_connect_push`).
- Sprites persisted in daemon config, uploaded to device on connect. Device stores them in RAM only.

## Notification Flow

1. Client POSTs `/notify` with title, body, color, options, timeout, optional sprite/vibrate/beep.
2. Daemon queues payload → `notify_worker` sends CMD over BLE chunks.
3. Firmware parses → pushes to inbox ring buffer → switches to `SCR_NOTIFY` → vibration + beep.
4. User taps/double-taps/long-presses → firmware sends EVT `{"id":"...", "choice":N}` back.
5. Daemon resolves future → HTTP response with choice + label.

Field sizes (truncated server-side): title 20, body 120, id 8 chars. Options: up to 3, each ≤12 chars.

## Gotchas

- Upload requires USB connection. `pio run --target upload` auto-detects port.
- BLE device advertises as `M5StickMonitor`. Server auto-reconnects every 5 s. Scanner tries UUID match → name match → first unknown device.
- ESP32 Preferences (NVS key `"m5dock"`) stores config; `loadSettings()` / `saveSettings()` in `state.cpp`.
- Canvas is a 135×240 sprite (`LGFX_Sprite`), not direct display writes. Pushed via `canvas.pushSprite(0,0)`.
- Idle screen uses M5Stack-Avatar library — renders directly to `M5.Display` via background tasks, NOT to canvas.
- `ble.cpp` has a **duplicate code bug** in `handleConfig()` (lines 127–144): `idle_ms` and `vol` parsing + `saveSettings()` appears twice. Remove the duplicate block.
- Sprite limit: `MAX_SPRITES = 8`, inbox limit: `INBOX_CAP = 20`. Sprite frames max 8 per sprite.
- Sprite frame buffer: `sprBuffer[16384]` — supports up to 64×64 frames.
- Daemon uses `aiohttp` (async) + `bleak` (async BLE). All BLE operations are coroutines — don't call synchronously.
- `preview_avatar.py` is standalone — uses PIL to render avatar face preview, no BLE or M5 hardware needed.
- Activity log capped at 200 entries, notification history at 100, gate history at 100.
