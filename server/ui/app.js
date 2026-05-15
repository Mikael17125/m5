/* ───────────────────────────────────────────────────────────────────────────
   M5 HUB — Operator Console (Minimal)
   ─────────────────────────────────────────────────────────────────────────── */

const POLL_MS = 1500;
const CLOCK_MS = 1000;

const $ = (sel) => document.querySelector(sel);
const $$ = (sel) => document.querySelectorAll(sel);

let lastActivityIds = new Set();
let lastNotifyIds = new Set();
let connectedSince = null;
let configFormInitialized = false;

const COLOR_MAP = {
  cyan: '#56b6c2', red: '#e06c75', green: '#98c379',
  yellow: '#e5c07b', orange: '#d19a66', purple: '#c678dd'
};
let composerMode = 'notify';
let lastSprites = [];

/* ─── BOOT ─────────────────────────────────────────────────────────────────── */
function boot() {
  initComposer();
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
  const [status, activity, notifications, gates, config] = await Promise.all([
    fetch("/api/status").then(safe).catch(() => null),
    fetch("/api/activity").then(safe).catch(() => null),
    fetch("/api/notifications").then(safe).catch(() => null),
    fetch("/api/gates").then(safe).catch(() => null),
    fetch("/api/config").then(safe).catch(() => null),
  ]);
  if (status)        renderStatus(status);
  if (activity)      renderActivity(activity);
  if (notifications) renderNotifyLog(notifications);
  if (gates)         renderGates(gates);
  if (config && !configFormInitialized) {
    populateConfigForm(config);
    configFormInitialized = true;
  }
  if (config) populateSpritePicker(config.sprites || {});
}

async function safe(r) {
  if (!r.ok) throw new Error(r.status);
  return r.json();
}

/* ─── STATUS RENDER ────────────────────────────────────────────────────────── */
function renderStatus(s) {
  const connected = !!s.connected;
  const dot = $("#conn-dot");
  dot.classList.toggle("on", connected);
  $("#conn-text").textContent = connected ? "ACTIVE" : "OFFLINE";
  $("#queue-text").textContent = String(s.queue_depth ?? 0);
  $("#gates-count").textContent = String(s.gates_total ?? "—");
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
  const bat = s.stats?.bat ?? 0;
  const cpu = s.stats?.cpu ?? 0;
  const ram = s.stats?.ram ?? 0;
  $("#stat-bat").textContent = bat;
  $("#stat-cpu").textContent = cpu;
  $("#stat-ram").textContent = ram;
  $("#bar-bat").style.width = `${clamp(bat)}%`;
  $("#bar-cpu").style.width = `${clamp(cpu)}%`;
  $("#bar-ram").style.width = `${clamp(ram)}%`;
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
let pendingGateId = null;
let gateResponding = false;

function renderPending(p) {
  const el = $("#gate-pending");
  if (!p) {
    el.hidden = true;
    pendingGateId = null;
    return;
  }
  el.hidden = false;
  pendingGateId = p.id;
  $("#gate-tool").textContent = p.tool || "Unknown";
  $("#gate-detail").textContent = p.detail || "";
  const remaining = Math.max(0, Math.round((p.deadline_ms - Date.now()) / 1000));
  $("#gate-timer").textContent = `${remaining}s`;
  $$(".gate-btn").forEach(btn => btn.disabled = gateResponding);
}

async function respondGate(decision) {
  if (!pendingGateId || gateResponding) return;
  gateResponding = true;
  $$(".gate-btn").forEach(btn => btn.disabled = true);
  try {
    await fetch("/api/gate/respond", {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({ decision }),
    });
  } catch (e) {
    console.error("gate respond failed:", e);
  }
  gateResponding = false;
}

document.addEventListener("click", (e) => {
  const btn = e.target.closest(".gate-btn");
  if (btn && !btn.disabled) respondGate(btn.dataset.decision);
});

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
          <span class="gate-row-tool">${escapeHtml(it.tool || "")}</span>
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

/* ─── NOTIFICATION LOG (SIDEBAR) ─────────────────────────────────────────── */
function renderNotifyLog(n) {
  const log = $("#notify-log");
  const items = n.items || [];
  $("#notify-count").textContent = `${items.length} entries`;
  if (!items.length) {
    log.innerHTML = '<li class="activity-empty mono">awaiting notifications…</li>';
    lastNotifyIds = new Set();
    return;
  }
  const newIds = new Set();
  const html = items.map((it) => {
    newIds.add(it.id);
    const fresh = lastNotifyIds.size && !lastNotifyIds.has(it.id);
    const ts = formatTimestamp(it.ts, true);
    const statusCls = it.status === "alert" ? " kind-alert" :
                      it.status === "sprite_test" ? " kind-sprite" : "";
    return `
      <li class="activity-row${fresh ? " fresh" : ""}${statusCls}" data-id="${escapeAttr(it.id)}">
        <span class="activity-ts">${escapeHtml(ts)}</span>
        <div class="activity-content">
          <span class="activity-kind kind-notify">NOTIFY</span>
          <span class="activity-msg"><strong>${escapeHtml(it.title || "")}</strong> · ${escapeHtml(it.body || "")}</span>
        </div>
      </li>
    `;
  }).join("");
  log.innerHTML = html;
  lastNotifyIds = newIds;
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

/* ─── TEST NOTIFICATION (COMPOSER) ─────────────────────────────────────────── */
function bindForms() {
  bindSlider("brightness", "brightness-val");
  bindSlider("volume", "volume-val");
  $("#btn-save-config").addEventListener("click", saveConfig);

}

function initComposer() {
  const titleEl = $("#t_title");
  const bodyEl  = $("#t_body");
  const optsEl  = $("#t_opts");

  /* initial counters */
  updateCharCounter("title-counter", titleEl.value.length, 20);
  updateCharCounter("body-counter", bodyEl.value.length, 120);
  updateOptsCounter();

  /* live counters */
  titleEl.addEventListener("input", () => {
    updateCharCounter("title-counter", titleEl.value.length, 20);
    updatePreview();
  });
  bodyEl.addEventListener("input", () => {
    updateCharCounter("body-counter", bodyEl.value.length, 120);
    updatePreview();
  });
  optsEl.addEventListener("input", () => {
    updateOptsCounter();
    updatePreview();
  });

  /* color swatches */
  $$(".swatch").forEach(sw => {
    sw.addEventListener("click", () => {
      $$(".swatch").forEach(s => s.classList.remove("active"));
      sw.classList.add("active");
      $("#t_color").value = sw.dataset.color;
      updatePreview();
    });
  });

  /* mode toggle */
  $$(".mode-btn").forEach(btn => {
    btn.addEventListener("click", () => {
      $$(".mode-btn").forEach(b => b.classList.remove("active"));
      btn.classList.add("active");
      composerMode = btn.dataset.mode;
      $("#mode-hint").textContent = composerMode === "notify"
        ? "waits for response" : "fire-and-forget";
      const optsField = $("#opts-field");
      const timeout = $("#t_timeout");
      if (composerMode === "alert") {
        optsField.style.display = "none";
        timeout.value = 10;
      } else {
        optsField.style.display = "";
        timeout.value = 60;
      }
      $("#timeout-val").textContent = timeout.value;
      updatePreview();
    });
  });

  /* LED toggle label */
  $("#t_led").addEventListener("change", () => {
    $("#led-label").textContent = $("#t_led").checked ? "on" : "off";
  });

  /* timeout slider */
  const timeoutSlider = $("#t_timeout");
  timeoutSlider.addEventListener("input", () => {
    $("#timeout-val").textContent = timeoutSlider.value;
    updatePreview();
  });

  /* sprite / vib / beep changes */
  $("#t_sprite").addEventListener("change", updatePreview);
  $("#t_vib").addEventListener("change", updatePreview);
  $("#t_beep").addEventListener("change", updatePreview);

  /* buttons */
  $("#btn-send").addEventListener("click", sendTestNotify);
  $("#test-form").addEventListener("submit", (e) => { e.preventDefault(); sendTestNotify(); });
  $("#btn-clear").addEventListener("click", clearComposer);

  /* Ctrl+Enter shortcut */
  document.addEventListener("keydown", (e) => {
    if ((e.ctrlKey || e.metaKey) && e.key === "Enter") {
      e.preventDefault();
      sendTestNotify();
    }
  });

  updatePreview();
}

function bindSlider(inputId, displayId) {
  const i = $("#" + inputId);
  const d = $("#" + displayId);
  i.addEventListener("input", () => { d.textContent = i.value; });
}

function updateCharCounter(id, current, max) {
  const el = $("#" + id);
  el.textContent = `${current}/${max}`;
  el.classList.toggle("at-max", current >= max);
}

function updateOptsCounter() {
  const opts = $("#t_opts").value.split(",").map(s => s.trim()).filter(Boolean);
  const el = $("#opts-counter");
  el.textContent = `${opts.length}/3 options`;
  el.classList.toggle("at-max", opts.length >= 3);
}

function updatePreview() {
  const title   = $("#t_title").value || " ";
  const body    = $("#t_body").value || " ";
  const color   = $("#t_color").value;
  const sprite  = $("#t_sprite").value;
  const timeout = $("#t_timeout").value;

  $("#preview-title").textContent = title;
  $("#preview-body").textContent  = body;

  /* header color */
  const hdr = $("#preview-header");
  hdr.setAttribute("data-color", color);
  hdr.style.background = COLOR_MAP[color] || "#2c313a";

  /* sprite badge */
  const spriteBadge = $("#preview-sprite");
  spriteBadge.textContent = sprite || "";
  spriteBadge.style.display = sprite ? "inline" : "none";

  /* timeout */
  $("#preview-timeout").textContent = timeout + "s";

  /* options */
  const optsContainer = $("#preview-options");
  if (composerMode === "notify") {
    optsContainer.style.display = "";
    const opts = $("#t_opts").value.split(",").map(s => s.trim()).filter(Boolean);
    for (let i = 0; i < 3; i++) {
      const el = $(`#preview-opt${i}`);
      if (opts[i]) { el.textContent = opts[i]; el.style.display = ""; }
      else         { el.style.display = "none"; }
    }
  } else {
    optsContainer.style.display = "none";
  }
}

function clearComposer() {
  composerMode = "notify";
  $$(".mode-btn").forEach(b => b.classList.toggle("active", b.dataset.mode === "notify"));
  $("#mode-hint").textContent = "waits for response";
  $("#opts-field").style.display = "";

  $("#t_title").value = "";
  $("#t_body").value  = "";
  $("#t_opts").value  = "";
  $("#t_sprite").value = "";
  $("#t_vib").value   = "double";
  $("#t_beep").value  = "alert";
  $("#t_led").checked = true;
  $("#led-label").textContent = "on";
  $("#t_timeout").value = 60;
  $("#timeout-val").textContent = "60";

  $$(".swatch").forEach(s => s.classList.toggle("active", s.dataset.color === "cyan"));
  $("#t_color").value = "cyan";

  updateCharCounter("title-counter", 0, 20);
  updateCharCounter("body-counter", 0, 120);
  updateOptsCounter();
  $("#form-result").textContent = "";
  updatePreview();
}

async function sendTestNotify() {
  const result = $("#form-result");
  const opts = $("#t_opts").value.split(",").map(s => s.trim()).filter(Boolean).slice(0, 3);
  const body = {
    title:   $("#t_title").value,
    body:    $("#t_body").value,
    color:   $("#t_color").value,
    vibrate: $("#t_vib").value,
    beep:    $("#t_beep").value,
    options: opts,
    led:     $("#t_led").checked,
    sprite:  $("#t_sprite").value || undefined,
    timeout: parseInt($("#t_timeout").value, 10),
  };
  const endpoint = composerMode === "alert" ? "/alert" : "/notify";
  setResult(result, "Transmitting…", "");
  try {
    const r = await fetch(endpoint, {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify(body),
    });
    const d = await r.json();
    const msg = d.ok
      ? "OK" + (d.choice ? " → " + d.choice : "")
      : "Error: " + JSON.stringify(d);
    setResult(result, msg, d.ok ? "ok" : "err");
  } catch (e) {
    setResult(result, "Failed: " + e.message, "err");
  }
}

function populateSpritePicker(sprites) {
  const sel = $("#t_sprite");
  const names = Object.keys(sprites || {});
  if (names.join(",") === lastSprites.join(",")) return;
  lastSprites = names;
  const current = sel.value;
  sel.innerHTML = '<option value="">None</option>' +
    names.map(n => `<option value="${escapeAttr(n)}">${escapeHtml(n)}</option>`).join("");
  sel.value = names.includes(current) ? current : "";
}

function setResult(el, text, cls) {
  el.textContent = text;
  el.classList.remove("ok", "err");
  if (cls) el.classList.add(cls);
}

/* ─── CONFIG ───────────────────────────────────────────────────────────────── */
function populateConfigForm(c) {
  $("#brightness").value     = c.brightness ?? 200;
  $("#brightness-val").textContent = c.brightness ?? 200;
  $("#volume").value         = c.volume ?? 12;
  $("#volume-val").textContent = c.volume ?? 12;
  $("#idle_ms").value        = c.idle_ms ?? 30000;

}

async function saveConfig() {
  const result = $("#config-result");
  const body = {
    brightness:  parseInt($("#brightness").value, 10),
    volume:      parseInt($("#volume").value, 10),
    idle_ms:     parseInt($("#idle_ms").value, 10),
  };
  setResult(result, "Saving…", "");
  try {
    const r = await fetch("/api/config", {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify(body),
    });
    const d = await r.json();
    setResult(result, d.ok ? "Saved" : "Error: " + JSON.stringify(d), d.ok ? "ok" : "err");
  } catch (e) {
    setResult(result, "Failed: " + e.message, "err");
  }
}


/* ─── ESCAPING ───────────────────────────────────────────────────────────────── */
function escapeHtml(s) {
  return String(s ?? "")
    .replace(/&/g, "&amp;")
    .replace(/</g, "&lt;")
    .replace(/>/g, "&gt;")
    .replace(/"/g, "&quot;")
    .replace(/'/g, "&#39;");
}
function escapeAttr(s) { return escapeHtml(s).replace(/\n/g, " "); }

/* ─── START ─────────────────────────────────────────────────────────────────── */
document.addEventListener("DOMContentLoaded", boot);
