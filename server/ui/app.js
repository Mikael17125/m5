/* ───────────────────────────────────────────────────────────────────────────
   M5 HUB — Operator Console
   Vanilla JS controller. Polls /api/status, /api/activity, /api/gates.
   ─────────────────────────────────────────────────────────────────────────── */

const POLL_MS = 1500;
const CLOCK_MS = 1000;

const $ = (sel) => document.querySelector(sel);
const $$ = (sel) => document.querySelectorAll(sel);

// IDE theme: preview header uses a single accent regardless of selected color.
// The selected color is transmitted to the device for real BLE notification.
const HEADER_COLOR_MAP = null;

let lastActivityIds = new Set();
let connectedSince = null;

/* ─── BOOT ─────────────────────────────────────────────────────────────────── */
function boot() {
  bindForms();
  tickClock();
  setInterval(tickClock, CLOCK_MS);
  poll();
  setInterval(poll, POLL_MS);
}

/* ─── CLOCK ────────────────────────────────────────────────────────────────── */
function tickClock() {
  const d = new Date();
  const h = String(d.getUTCHours()).padStart(2, "0");
  const m = String(d.getUTCMinutes()).padStart(2, "0");
  const s = String(d.getUTCSeconds()).padStart(2, "0");
  $("#clock").textContent = `${h}:${m}:${s}`;
}

/* ─── POLLING ──────────────────────────────────────────────────────────────── */
async function poll() {
  const [status, activity, gates, config] = await Promise.all([
    fetch("/api/status").then(safe).catch(() => null),
    fetch("/api/activity").then(safe).catch(() => null),
    fetch("/api/gates").then(safe).catch(() => null),
    fetch("/api/config").then(safe).catch(() => null),
  ]);
  if (status)   renderStatus(status);
  if (activity) renderActivity(activity);
  if (gates)    renderGates(gates);
  if (config && !configFormInitialized) {
    populateConfigForm(config);
    configFormInitialized = true;
  }
  if (config) renderSprites(config);
}

async function safe(r) {
  if (!r.ok) throw new Error(r.status);
  return r.json();
}

/* ─── STATUS RENDER ────────────────────────────────────────────────────────── */
function renderStatus(s) {
  const connected = !!s.connected;

  // top bar dot + text
  const dot = $("#conn-dot");
  dot.classList.toggle("on", connected);
  $("#conn-text").textContent = connected ? "ACTIVE" : "OFFLINE";

  $("#queue-text").textContent = String(s.queue_depth ?? 0);
  $("#gates-count").textContent = String(s.gates_total ?? "—");

  // hero
  const hero = $("#hero-status");
  hero.textContent = connected ? "CONNECTED" : "OFFLINE";
  hero.classList.toggle("live", connected);
  hero.classList.toggle("off", !connected);

  if (connected && !connectedSince) connectedSince = Date.now();
  if (!connected) connectedSince = null;
  $("#hero-uptime").textContent = connected
    ? `Linked · uptime ${formatUptime(Date.now() - connectedSince)}`
    : "Awaiting BLE beacon";

  $("#device-addr").textContent = s.device || "—";

  // stats
  const bat = s.stats?.bat ?? 0;
  const cpu = s.stats?.cpu ?? 0;
  const ram = s.stats?.ram ?? 0;
  $("#stat-bat").textContent = bat;
  $("#stat-cpu").textContent = cpu;
  $("#stat-ram").textContent = ram;
  $("#bar-bat").style.width = `${clamp(bat)}%`;
  $("#bar-cpu").style.width = `${clamp(cpu)}%`;
  $("#bar-ram").style.width = `${clamp(ram)}%`;

  // pending gate (if any in status payload)
  renderPending(s.pending_gate);
}

function clamp(n) { return Math.max(0, Math.min(100, Number(n) || 0)); }

function formatUptime(ms) {
  const s = Math.floor(ms / 1000);
  if (s < 60) return `${s}s`;
  const m = Math.floor(s / 60);
  if (m < 60) return `${m}m ${s % 60}s`;
  const h = Math.floor(m / 60);
  return `${h}h ${m % 60}m`;
}

/* ─── PENDING GATE ─────────────────────────────────────────────────────────── */
function renderPending(p) {
  const el = $("#gate-pending");
  if (!p) { el.hidden = true; return; }
  el.hidden = false;
  $("#gate-tool").textContent = p.tool || "Unknown";
  $("#gate-detail").textContent = p.detail || "";
  const remaining = Math.max(0, Math.round((p.deadline_ms - Date.now()) / 1000));
  $("#gate-timer").textContent = `${remaining}s`;
}

/* ─── GATES LOG ────────────────────────────────────────────────────────────── */
function renderGates(g) {
  const log = $("#gate-log");
  const items = g.items || [];
  if (!items.length) {
    log.innerHTML = '<li class="gate-log-empty">No gates recorded yet.</li>';
    return;
  }
  log.innerHTML = items.map((it) => {
    const ts = formatTimestamp(it.ts);
    const cls = "decision-" + (it.decision || "error");
    const dec = (it.decision || "error").toUpperCase();
    return `
      <li class="gate-row">
        <span class="gate-row-ts">${escapeHtml(ts)}</span>
        <div class="gate-row-body">
          <span class="gate-row-tool">${escapeHtml(it.tool)}</span>
          <span class="gate-row-detail" title="${escapeAttr(it.detail || "")}">${escapeHtml(it.detail || "")}</span>
        </div>
        <span class="gate-row-decision ${cls}">${dec}</span>
      </li>
    `;
  }).join("");
}

/* ─── ACTIVITY LOG ─────────────────────────────────────────────────────────── */
function renderActivity(a) {
  const log = $("#activity-log");
  const items = a.items || [];
  if (!items.length) {
    log.innerHTML = '<li class="activity-empty mono">awaiting events…</li>';
    lastActivityIds = new Set();
    return;
  }
  const newIds = new Set();
  const html = items.map((it) => {
    newIds.add(it.id);
    const fresh = lastActivityIds.size && !lastActivityIds.has(it.id);
    const ts = formatTimestamp(it.ts, true);
    return `
      <li class="activity-row${fresh ? " fresh" : ""}" data-id="${escapeAttr(it.id)}">
        <span class="activity-ts">${escapeHtml(ts)}</span>
        <div class="activity-content">
          <span class="activity-kind kind-${escapeAttr(it.kind || "system")}">${escapeHtml((it.kind || "system").toUpperCase())}</span>
          <span class="activity-msg">${formatActivityMsg(it)}</span>
        </div>
      </li>
    `;
  }).join("");
  log.innerHTML = html;
  lastActivityIds = newIds;
}

function formatActivityMsg(it) {
  if (it.kind === "gate") {
    const dec = it.decision ? ` → <strong>${escapeHtml(it.decision.toUpperCase())}</strong>` : "";
    return `<strong>${escapeHtml(it.tool || "Tool")}</strong>${dec} <span class="mono dim">${escapeHtml(it.detail || "")}</span>`;
  }
  if (it.kind === "notify") {
    return `<strong>${escapeHtml(it.title || "")}</strong> · ${escapeHtml(it.body || "")}`;
  }
  return escapeHtml(it.msg || JSON.stringify(it));
}

function formatTimestamp(ts, withSeconds = false) {
  if (!ts) return "--:--";
  const d = new Date(ts * 1000);
  const h = String(d.getHours()).padStart(2, "0");
  const m = String(d.getMinutes()).padStart(2, "0");
  if (!withSeconds) return `${h}:${m}`;
  const s = String(d.getSeconds()).padStart(2, "0");
  return `${h}:${m}:${s}`;
}

/* ─── TEST NOTIFICATION ────────────────────────────────────────────────────── */
function bindForms() {
  // live preview
  const fields = ["t_title", "t_body", "t_color", "t_opts"];
  fields.forEach((id) => $("#" + id).addEventListener("input", updatePreview));
  updatePreview();

  $("#btn-send").addEventListener("click", sendTestNotify);
  $("#test-form").addEventListener("submit", (e) => { e.preventDefault(); sendTestNotify(); });

  // sliders show live value
  bindSlider("brightness", "brightness-val");
  bindSlider("volume", "volume-val");

  $("#btn-save-config").addEventListener("click", saveConfig);
  $("#btn-upload-sprite").addEventListener("click", uploadSprite);
}

function bindSlider(inputId, displayId) {
  const i = $("#" + inputId);
  const d = $("#" + displayId);
  i.addEventListener("input", () => { d.textContent = i.value; });
}

function updatePreview() {
  const title = $("#t_title").value || " ";
  const body  = $("#t_body").value || " ";
  const color = $("#t_color").value;
  const opts  = $("#t_opts").value.split(",").map(s => s.trim()).filter(Boolean).slice(0, 3);

  $("#preview-title").textContent = title;
  $("#preview-body").textContent = body;
  // header color is a device-side concern; show its name as a tag instead.
  const hdr = $("#preview-header");
  hdr.setAttribute("data-color", color);

  const [o0, o1, o2] = opts;
  setOpt("#preview-opt0", o0 || "OK");
  setOpt("#preview-opt1", o1, !o1);
  setOpt("#preview-opt2", o2, !o2);
}

function setOpt(sel, text, hide) {
  const el = $(sel);
  el.textContent = text || "";
  el.style.display = hide ? "none" : "block";
}

async function sendTestNotify() {
  const result = $("#form-result");
  const opts = $("#t_opts").value.split(",").map(s => s.trim()).filter(Boolean);
  const body = {
    title: $("#t_title").value,
    body:  $("#t_body").value,
    color: $("#t_color").value,
    vibrate: $("#t_vib").value,
    beep:    $("#t_beep").value,
    options: opts,
    led: true,
  };
  setResult(result, "Transmitting…", "");
  try {
    const r = await fetch("/notify", {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify(body),
    });
    const d = await r.json();
    const lbl = d.label || "—";
    setResult(result, `choice ${d.choice ?? "?"} · ${lbl}`, "ok");
  } catch (e) {
    setResult(result, "Transmit failed: " + e.message, "err");
  }
}

function setResult(el, text, cls) {
  el.textContent = text;
  el.classList.remove("ok", "err");
  if (cls) el.classList.add(cls);
}

/* ─── CONFIG ───────────────────────────────────────────────────────────────── */
let configFormInitialized = false;

function populateConfigForm(c) {
  $("#brightness").value     = c.brightness ?? 200;
  $("#brightness-val").textContent = c.brightness ?? 200;
  $("#volume").value         = c.volume ?? 12;
  $("#volume-val").textContent = c.volume ?? 12;
  $("#idle_ms").value        = c.idle_ms ?? 30000;
  $("#idle_sprite").value    = c.idle_sprite ?? "";
  $("#show_stats").value     = c.show_stats ? "1" : "0";
}

async function saveConfig() {
  const result = $("#config-result");
  const body = {
    brightness:  parseInt($("#brightness").value, 10),
    volume:      parseInt($("#volume").value, 10),
    idle_ms:     parseInt($("#idle_ms").value, 10),
    idle_sprite: $("#idle_sprite").value,
    show_stats:  $("#show_stats").value === "1",
  };
  setResult(result, "Saving…", "");
  try {
    const r = await fetch("/api/config", {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify(body),
    });
    const d = await r.json();
    setResult(result, d.ok ? "Saved & pushed to device" : "Error: " + JSON.stringify(d), d.ok ? "ok" : "err");
  } catch (e) {
    setResult(result, "Failed: " + e.message, "err");
  }
}

/* ─── SPRITE UPLOAD ────────────────────────────────────────────────────────── */
async function uploadSprite() {
  const result = $("#sprite-result");
  const text = $("#sprite_json").value.trim();
  if (!text) { setResult(result, "Paste JSON first", "err"); return; }
  let parsed;
  try { parsed = JSON.parse(text); }
  catch (e) { setResult(result, "Invalid JSON: " + e.message, "err"); return; }
  setResult(result, "Uploading…", "");
  try {
    const r = await fetch("/api/sprite", {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify(parsed),
    });
    const d = await r.json();
    setResult(result, d.ok ? `Sprite '${parsed.name}' uploaded` : "Error: " + JSON.stringify(d), d.ok ? "ok" : "err");
  } catch (e) {
    setResult(result, "Failed: " + e.message, "err");
  }
}

/* ─── SPRITE GALLERY ───────────────────────────────────────────────────────── */
// Track active animation intervals by sprite name so we can clean them up
// when sprites are re-rendered.
const spriteTimers = new Map();
// Cache last-rendered config to skip work when nothing changed.
let lastSpriteConfigHash = "";

function renderSprites(config) {
  const sprites = config.sprites || {};
  const idleName = config.idle_sprite || "";
  const names = Object.keys(sprites);

  // Cheap diff so we don't rebuild gallery on every 1.5s poll
  const hash = JSON.stringify({ k: names, i: idleName });
  if (hash === lastSpriteConfigHash) return;
  lastSpriteConfigHash = hash;

  // Stop existing animations
  for (const [, id] of spriteTimers) clearInterval(id);
  spriteTimers.clear();

  const hint = $("#sprite-hint");
  const gallery = $("#sprite-gallery");

  if (!names.length) {
    hint.textContent = "// vault empty · upload a sprite below to see it here";
    gallery.innerHTML = "";
    return;
  }
  hint.textContent = idleName
    ? `// ${names.length} sprite${names.length > 1 ? "s" : ""} · idle = "${idleName}"`
    : `// ${names.length} sprite${names.length > 1 ? "s" : ""} · idle = breathing dots (none assigned)`;

  gallery.innerHTML = names.map((n) => spriteCardHtml(n, idleName === n, sprites[n].frames || [])).join("");

  // Paint canvases + start animations
  for (const name of names) {
    const frames = sprites[name].frames || [];
    const cv = gallery.querySelector(`canvas[data-name="${cssAttr(name)}"]`);
    if (!cv || !frames.length) continue;
    const decoded = frames.map((f) => decodeSpriteFrame(f.pal || "", f.px || "", f.w || 32, f.h || 32)).filter(Boolean);
    if (!decoded.length) continue;
    cv.width = decoded[0].w;
    cv.height = decoded[0].h;
    const ctx = cv.getContext("2d");
    ctx.imageSmoothingEnabled = false;
    let idx = 0;
    const draw = () => {
      const f = decoded[idx % decoded.length];
      ctx.putImageData(f.image, 0, 0);
      idx++;
    };
    draw();
    if (decoded.length > 1) {
      const id = setInterval(draw, 200);
      spriteTimers.set(name, id);
    }
  }

  // Wire actions
  gallery.querySelectorAll(".sprite-action").forEach((el) => {
    el.addEventListener("click", () => {
      const op = el.dataset.op;
      const name = el.dataset.name;
      if (op === "idle")   setIdleSprite(name);
      if (op === "test")   testSprite(name);
      if (op === "delete") deleteSprite(name);
    });
  });
}

function spriteCardHtml(name, isIdle, frames) {
  const fc = frames.length;
  const w = frames[0]?.w || 32;
  const h = frames[0]?.h || 32;
  const badge = isIdle ? '<span class="sprite-badge mono">IDLE</span>' : "";
  return `
    <article class="sprite-card${isIdle ? " is-idle" : ""}">
      <div class="sprite-preview">
        <canvas data-name="${escapeAttr(name)}"></canvas>
      </div>
      <div class="sprite-meta">
        <div class="sprite-name">${escapeHtml(name)}${badge}</div>
        <div class="sprite-stats mono dim">${fc} frame${fc !== 1 ? "s" : ""} · ${w}×${h}</div>
      </div>
      <div class="sprite-actions">
        <button class="btn sprite-action" data-op="idle"   data-name="${escapeAttr(name)}">Set idle</button>
        <button class="btn sprite-action" data-op="test"   data-name="${escapeAttr(name)}">Test</button>
        <button class="btn sprite-action sprite-action-danger" data-op="delete" data-name="${escapeAttr(name)}">×</button>
      </div>
    </article>
  `;
}

// 4bpp + 16-color RGB565 palette → ImageData (RGBA)
function decodeSpriteFrame(palHex, pxHex, w, h) {
  if (!palHex || !pxHex) return null;
  const pal = hexToBytes(palHex);
  const px  = hexToBytes(pxHex);
  if (pal.length < 32 || px.length < (w * h) / 2) return null;
  // Decode 16-entry palette from big-endian RGB565 → RGBA888
  const palRgba = new Uint8ClampedArray(16 * 4);
  for (let i = 0; i < 16; i++) {
    const rgb565 = (pal[i * 2] << 8) | pal[i * 2 + 1];
    const r = ((rgb565 >> 11) & 0x1f) * 255 / 31 | 0;
    const g = ((rgb565 >> 5)  & 0x3f) * 255 / 63 | 0;
    const b = ( rgb565        & 0x1f) * 255 / 31 | 0;
    palRgba[i * 4 + 0] = r;
    palRgba[i * 4 + 1] = g;
    palRgba[i * 4 + 2] = b;
    palRgba[i * 4 + 3] = 255;
  }
  const out = new Uint8ClampedArray(w * h * 4);
  for (let i = 0; i < w * h; i++) {
    const byte   = px[i >> 1];
    const nibble = (i & 1) === 0 ? (byte >> 4) : (byte & 0x0f);
    const p = nibble * 4;
    const o = i * 4;
    out[o + 0] = palRgba[p + 0];
    out[o + 1] = palRgba[p + 1];
    out[o + 2] = palRgba[p + 2];
    out[o + 3] = palRgba[p + 3];
  }
  return { w, h, image: new ImageData(out, w, h) };
}

function hexToBytes(hex) {
  const clean = String(hex || "").replace(/\s+/g, "");
  const out = new Uint8Array(clean.length >> 1);
  for (let i = 0; i < out.length; i++) {
    out[i] = parseInt(clean.substr(i * 2, 2), 16);
  }
  return out;
}

function cssAttr(s) {
  // CSS attribute selector escape for sprite names (allow safe chars only)
  return String(s).replace(/[^a-zA-Z0-9_-]/g, "_");
}

async function setIdleSprite(name) {
  try {
    const r = await fetch(`/api/sprite/${encodeURIComponent(name)}/idle`, { method: "POST" });
    const d = await r.json();
    setResult($("#sprite-result"), d.ok ? `'${name}' set as idle` : "Error: " + (d.error || ""), d.ok ? "ok" : "err");
    if (d.ok) lastSpriteConfigHash = ""; // force re-render
  } catch (e) {
    setResult($("#sprite-result"), "Failed: " + e.message, "err");
  }
}

async function testSprite(name) {
  try {
    const r = await fetch(`/api/sprite/${encodeURIComponent(name)}/test`, { method: "POST" });
    const d = await r.json();
    setResult($("#sprite-result"), d.ok ? `'${name}' test-fired` : "Error", d.ok ? "ok" : "err");
  } catch (e) {
    setResult($("#sprite-result"), "Failed: " + e.message, "err");
  }
}

async function deleteSprite(name) {
  if (!confirm(`Delete sprite '${name}' permanently?`)) return;
  try {
    const r = await fetch(`/api/sprite/${encodeURIComponent(name)}`, { method: "DELETE" });
    const d = await r.json();
    setResult($("#sprite-result"), d.ok ? `'${name}' deleted` : "Error", d.ok ? "ok" : "err");
    if (d.ok) lastSpriteConfigHash = "";
  } catch (e) {
    setResult($("#sprite-result"), "Failed: " + e.message, "err");
  }
}

/* ─── ESCAPING ─────────────────────────────────────────────────────────────── */
function escapeHtml(s) {
  return String(s ?? "")
    .replace(/&/g, "&amp;")
    .replace(/</g, "&lt;")
    .replace(/>/g, "&gt;")
    .replace(/"/g, "&quot;")
    .replace(/'/g, "&#39;");
}
function escapeAttr(s) { return escapeHtml(s).replace(/\n/g, " "); }

/* ─── START ────────────────────────────────────────────────────────────────── */
document.addEventListener("DOMContentLoaded", boot);
