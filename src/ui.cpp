#include "ui.h"
#include "util.h"
#include "sprite.h"
#include "ble.h"
#include <Arduino.h>

// ── Chrome ──────────────────────────────────────────────────────────────────
static void drawStatusBar() {
  canvas.fillRect(0, 0, 240, 22, C_HEADER);
  drawLogo(6, 4);
  canvas.setFont(&fonts::Font0);
  canvas.setTextColor(C_GRAY2);
  canvas.setCursor(26, 7);
  canvas.print("M5 DOCK");
  canvas.fillCircle(154, 11, 4, connected ? C_GREEN : C_RED);
  canvas.setTextColor(connected ? C_GREEN : C_GRAY2);
  canvas.setCursor(162, 7);
  canvas.print(connected ? "BLE" : "---");
  drawBatteryIcon(186, 6, m5Bat, m5Charging);
  char pb[6]; snprintf(pb, sizeof(pb), "%d%%", m5Bat);
  canvas.setTextColor(C_WHITE);
  canvas.setCursor(212, 7);
  canvas.print(pb);
  canvas.drawFastHLine(0, 22, 240, C_ACCENT);
}

static void drawTabBar(int active) {
  canvas.fillRect(0, 110, 240, 25, C_TAB_BG);
  canvas.drawFastHLine(0, 110, 240, C_DIV);
  const char *labels[TAB_COUNT] = { "Mon", "Inbox", "Act", "Set" };
  int cellW = 60;
  for (int i = 0; i < TAB_COUNT; i++) {
    int x = i * cellW;
    if (i == active) {
      canvas.fillRect(x, 111, cellW, 24, C_TAB_HI);
      canvas.fillRect(x, 110, cellW, 2, C_ACCENT);
    }
    uint16_t col = (i == active) ? C_WHITE : C_GRAY2;
    drawTabIcon(i, x + 14, 121, col);
    canvas.setFont(&fonts::Font0);
    canvas.setTextColor(col);
    canvas.setCursor(x + 24, 118);
    canvas.print(labels[i]);
  }
}

// ── Screens ──────────────────────────────────────────────────────────────────
static void renderScreenMonitor() {
  drawStatusBar();
  drawTabBar(SCR_MONITOR);
  canvas.setFont(&fonts::Font0);
  canvas.setTextColor(C_GRAY2);
  canvas.setCursor(8, 28);
  canvas.print("HOST STATS");
  canvas.setTextColor(C_GRAY);
  canvas.setCursor(150, 28);
  canvas.print(connected ? "(via Mac)" : "(offline)");

  if (!connected || !hasData || !cfg.show_stats) {
    canvas.setFont(&fonts::Font2);
    canvas.setTextColor(C_GRAY2);
    canvas.setCursor(36, 58);
    canvas.print(!connected ? "Waiting BLE..." : "No data yet");
    canvas.setFont(&fonts::Font0);
    canvas.setTextColor(C_GRAY);
    canvas.setCursor(36, 84);
    canvas.print(!connected ? "Pair on Mac side" : "Send stats from host");
    return;
  }

  char b[8];
  int y = 50;
  canvas.setTextColor(C_GRAY2);
  canvas.setCursor(8, y); canvas.print("CPU");
  canvas.setTextColor(C_WHITE);
  snprintf(b, sizeof(b), "%3d%%", cpuPct);
  canvas.setCursor(208, y); canvas.print(b);
  drawBar(8, y + 10, 224, 7, cpuPct, C_CYAN);

  y = 82;
  canvas.setTextColor(C_GRAY2);
  canvas.setCursor(8, y); canvas.print("RAM");
  canvas.setTextColor(C_WHITE);
  snprintf(b, sizeof(b), "%3d%%", ramPct);
  canvas.setCursor(208, y); canvas.print(b);
  drawBar(8, y + 10, 224, 7, ramPct, C_PURPLE);
}

static void renderScreenInbox() {
  drawStatusBar();
  drawTabBar(SCR_INBOX);
  canvas.setFont(&fonts::Font0);
  if (inboxCount == 0) {
    canvas.setTextColor(C_GRAY2);
    canvas.setCursor(56, 62);
    canvas.print("Inbox empty");
    return;
  }
  uint8_t sel = ui[SCR_INBOX].selIdx;
  uint8_t off = ui[SCR_INBOX].scrollOff;
  if (sel < off) off = sel;
  if (sel >= off + 4) off = sel - 3;
  ui[SCR_INBOX].scrollOff = off;
  for (int i = 0; i < 4 && off + i < inboxCount; i++) {
    const InboxEntry *e = inboxAt(off + i);
    if (!e) continue;
    int y = 26 + i * 20;
    bool isSel = (off + i == sel);
    if (isSel) canvas.fillRoundRect(2, y - 2, 236, 19, 3, C_TAB_HI);
    canvas.fillCircle(10, y + 7, 4, e->headerColor);
    canvas.setTextColor(C_WHITE);
    canvas.setCursor(22, y);
    char tb[20]; strncpy(tb, e->title, 19); tb[19] = 0;
    canvas.print(tb);
    canvas.setTextColor(C_GRAY2);
    canvas.setCursor(22, y + 9);
    char pb[26]; strncpy(pb, e->body, 25); pb[25] = 0;
    canvas.print(pb);
    canvas.setTextColor(C_GRAY2);
    char age[8]; formatAge(e->timestamp, age, sizeof(age));
    canvas.setCursor(212, y);
    canvas.print(age);
  }
  if (inboxCount > 4) {
    int barH = max(8, 80 * 4 / inboxCount);
    int barY = 26 + (80 - barH) * off / max(1, (int)(inboxCount - 4));
    canvas.fillRect(236, 26, 2, 80, C_TRACK);
    canvas.fillRect(236, barY, 2, barH, C_GRAY2);
  }
}

static void renderScreenInboxDetail() {
  uint8_t idx = ui[SCR_INBOX].selIdx;
  const InboxEntry *e = inboxAt(idx);
  if (!e) { current = SCR_INBOX; return; }
  canvas.fillScreen(C_BG);
  canvas.fillRect(0, 0, 240, 24, e->headerColor);
  canvas.setFont(&fonts::Font2);
  canvas.setTextColor(C_WHITE);
  canvas.setCursor(6, 5);
  char tb[20]; strncpy(tb, e->title, 19); tb[19] = 0;
  canvas.print(tb);
  Sprite *sp = strlen(e->spriteName) > 0 ? findSprite(e->spriteName) : nullptr;
  if (sp && sp->ready) {
    int frame = (millis() / 200) % sp->frameCount;
    drawSprite(sp, frame, 220 - sp->w, (24 - sp->h) / 2);
  }
  canvas.setFont(&fonts::Font0);
  canvas.setTextColor(C_WHITE);
  int dlen = strlen(e->body);
  for (int line = 0; line < 7 && line * 38 < dlen; line++) {
    char lbuf[39];
    strncpy(lbuf, e->body + line * 38, 38);
    lbuf[38] = 0;
    canvas.setCursor(6, 30 + line * 11);
    canvas.print(lbuf);
  }
  canvas.drawFastHLine(0, 116, 240, C_DIV);
  canvas.setTextColor(C_GRAY2);
  char age[8]; formatAge(e->timestamp, age, sizeof(age));
  canvas.setCursor(6, 122);
  canvas.printf("%s ago", age);
  canvas.setTextColor(C_GRAY2);
  canvas.setCursor(140, 122);
  canvas.print("PWR=Back  B=Next");
}

static void renderScreenActions() {
  drawStatusBar();
  drawTabBar(SCR_ACTIONS);
  canvas.setFont(&fonts::Font0);
  uint8_t sel = ui[SCR_ACTIONS].selIdx;
  uint8_t off = ui[SCR_ACTIONS].scrollOff;
  if (sel < off) off = sel;
  if (sel >= off + 4) off = sel - 3;
  ui[SCR_ACTIONS].scrollOff = off;
  for (int i = 0; i < 4 && off + i < ACTION_COUNT; i++) {
    int y = 26 + i * 20;
    bool isSel = (off + i == sel);
    if (isSel) {
      bool flash = millis() < actionFlashUntil;
      canvas.fillRoundRect(2, y - 2, 236, 19, 3, flash ? C_GREEN : C_TAB_HI);
    }
    canvas.setTextColor(isSel ? C_WHITE : C_GRAY2);
    canvas.setCursor(12, y + 3);
    canvas.print(ACTIONS[off + i].label);
    if (isSel) {
      canvas.setTextColor(C_ACCENT);
      canvas.setCursor(220, y + 3);
      canvas.print(">");
    }
  }
  if (ACTION_COUNT > 4) {
    int barH = max(8, 80 * 4 / ACTION_COUNT);
    int barY = 26 + (80 - barH) * off / max(1, ACTION_COUNT - 4);
    canvas.fillRect(236, 26, 2, 80, C_TRACK);
    canvas.fillRect(236, barY, 2, barH, C_GRAY2);
  }
}

static int settingValue(int row) {
  switch (row) {
    case SET_BRIGHT: return cfg.brightness;
    case SET_VOLUME: return cfg.volume;
    case SET_IDLE:   return cfg.idle_ms / 1000;
    case SET_VIB:    return cfg.vibEnabled ? 1 : 0;
  }
  return 0;
}

static void renderScreenSettings() {
  drawStatusBar();
  drawTabBar(SCR_SETTINGS);
  canvas.setFont(&fonts::Font0);
  uint8_t sel = ui[SCR_SETTINGS].selIdx;
  for (int i = 0; i < SET_COUNT; i++) {
    int y = 26 + i * 20;
    bool isSel = (i == sel);
    if (isSel) canvas.fillRoundRect(2, y - 2, 236, 19, 3, C_TAB_HI);
    canvas.setTextColor(isSel ? C_WHITE : C_GRAY2);
    canvas.setCursor(12, y + 3);
    canvas.print(SETTING_LABELS[i]);
    int val = settingValue(i);
    char vb[12];
    if (i == SET_VIB) snprintf(vb, sizeof(vb), "%s", val ? "ON" : "OFF");
    else              snprintf(vb, sizeof(vb), "%d", val);
    canvas.setTextColor(isSel ? C_ACCENT : C_GRAY2);
    canvas.setCursor(196, y + 3);
    canvas.print(vb);
  }
}

static void renderScreenSettingsEdit() {
  drawStatusBar();
  uint8_t row = ui[SCR_SETTINGS].selIdx;
  canvas.setFont(&fonts::Font2);
  canvas.setTextColor(C_WHITE);
  canvas.setCursor(8, 32);
  canvas.print("Edit: ");
  canvas.print(SETTING_LABELS[row]);

  int val = settingValue(row);
  canvas.setFont(&fonts::Font0);
  canvas.setTextColor(C_ACCENT);
  canvas.setCursor(8, 60);
  char vb[16];
  if (row == SET_VIB) snprintf(vb, sizeof(vb), "%s", val ? "ON" : "OFF");
  else                snprintf(vb, sizeof(vb), "%d", val);
  canvas.print(vb);

  int barMax = 255;
  if (row == SET_IDLE) barMax = 300;
  if (row == SET_VIB)  barMax = 1;
  if (row != SET_VIB) {
    int pct = val * 100 / barMax;
    drawBar(8, 78, 224, 8, pct, C_ACCENT);
  }
  canvas.drawFastHLine(0, 110, 240, C_DIV);
  canvas.fillRect(0, 110, 240, 25, C_TAB_BG);
  canvas.setTextColor(C_GRAY2);
  canvas.setCursor(8, 120);
  canvas.print("B=+  PWR=cancel  A=save");
}

static void renderScreenIdle() {
  canvas.fillScreen(C_BG);
  Sprite *sp = strlen(idleSpriteName) > 0 ? findSprite(idleSpriteName) : nullptr;
  if (sp && sp->ready) {
    int frame = (millis() / 200) % sp->frameCount;
    int x = (240 - sp->w) / 2;
    int y = (135 - sp->h) / 2;
    drawSprite(sp, frame, x, y);
  } else {
    uint32_t phase = (millis() / 400) % 3;
    for (int i = 0; i < 3; i++) {
      uint16_t c = (i == (int)phase) ? C_WHITE : C_GRAY;
      canvas.fillCircle(108 + i * 12, 67, 4, c);
    }
  }
}

static void renderScreenNotify() {
  canvas.fillScreen(C_BG);
  canvas.fillRect(0, 0, 240, 26, notif.headerColor);
  canvas.setFont(&fonts::Font2);
  canvas.setTextColor(C_WHITE);
  canvas.setCursor(6, 6);
  char tbuf[20]; strncpy(tbuf, notif.title, 19); tbuf[19] = 0;
  canvas.print(tbuf);

  Sprite *sp = strlen(notif.spriteName) > 0 ? findSprite(notif.spriteName) : nullptr;
  if (sp && sp->ready) {
    int frame = (millis() / 200) % sp->frameCount;
    drawSprite(sp, frame, 208 - sp->w, (26 - sp->h) / 2);
  } else if (blinkOn) {
    canvas.fillCircle(228, 13, 5, C_WHITE);
  }
  canvas.drawFastHLine(0, 26, 240, C_RED);

  canvas.setFont(&fonts::Font0);
  canvas.setTextColor(C_WHITE);
  int dlen = strlen(notif.body);
  for (int line = 0; line < 4 && line * 38 < dlen; line++) {
    char lbuf[39];
    strncpy(lbuf, notif.body + line * 38, 38);
    lbuf[38] = 0;
    canvas.setCursor(6, 30 + line * 11);
    canvas.print(lbuf);
  }

  canvas.drawFastHLine(4, 76, 232, C_DIV);

  canvas.setFont(&fonts::Font0);
  if (notif.optCount == 1) {
    canvas.fillRoundRect(4, 82, 232, 28, 4, C_GREEN);
    canvas.setTextColor(0x0000);
    canvas.setCursor(80, 93);
    canvas.print(notif.options[0]);
  } else if (notif.optCount == 2) {
    char ha[24], hb[24];
    snprintf(ha, sizeof(ha), "Tap: %s",  notif.options[0]);
    snprintf(hb, sizeof(hb), "Hold: %s", notif.options[1]);
    canvas.setCursor(6, 84);  canvas.setTextColor(C_GREEN);    canvas.print(ha);
    canvas.setCursor(6, 100); canvas.setTextColor(C_DARK_RED); canvas.print(hb);
  } else {
    char ha[24], hb[24], hc[24];
    snprintf(ha, sizeof(ha), "Tap: %s",  notif.options[0]);
    snprintf(hb, sizeof(hb), "x2:  %s",  notif.options[1]);
    snprintf(hc, sizeof(hc), "Hold:%s",  notif.options[2]);
    canvas.setCursor(6, 82);  canvas.setTextColor(C_GREEN);    canvas.print(ha);
    canvas.setCursor(6, 98);  canvas.setTextColor(C_CYAN);     canvas.print(hb);
    canvas.setCursor(6, 114); canvas.setTextColor(C_DARK_RED); canvas.print(hc);
  }
}

void renderCurrent() {
  canvas.fillScreen(C_BG);
  switch (current) {
    case SCR_MONITOR:       renderScreenMonitor();       break;
    case SCR_INBOX:         renderScreenInbox();         break;
    case SCR_ACTIONS:       renderScreenActions();       break;
    case SCR_SETTINGS:      renderScreenSettings();      break;
    case SCR_INBOX_DETAIL:  renderScreenInboxDetail();   break;
    case SCR_SETTINGS_EDIT: renderScreenSettingsEdit();  break;
    case SCR_NOTIFY:        renderScreenNotify();        break;
    case SCR_IDLE:          renderScreenIdle();          break;
    default: break;
  }
  canvas.pushSprite(0, 0);
}

// ── Setting edit step ───────────────────────────────────────────────────────
static void settingStep(int row) {
  switch (row) {
    case SET_BRIGHT:
      cfg.brightness += 25;
      if (cfg.brightness > 250) cfg.brightness = 25;
      StickCP2.Display.setBrightness(cfg.brightness);
      break;
    case SET_VOLUME:
      cfg.volume += 32;
      if (cfg.volume > 240) cfg.volume = 0;
      StickCP2.Speaker.setVolume(cfg.volume);
      break;
    case SET_IDLE: {
      uint32_t s = cfg.idle_ms / 1000;
      s += 15;
      if (s > 300) s = 15;
      cfg.idle_ms = s * 1000;
      break;
    }
    case SET_VIB:
      cfg.vibEnabled = !cfg.vibEnabled;
      break;
  }
}

// ── Animation hint ──────────────────────────────────────────────────────────
bool screenAnimates() {
  if (current == SCR_NOTIFY) return true;
  if (current == SCR_IDLE)   return true;
  if (current == SCR_INBOX_DETAIL) {
    const InboxEntry *e = inboxAt(ui[SCR_INBOX].selIdx);
    if (e && strlen(e->spriteName) > 0 && findSprite(e->spriteName)) return true;
  }
  if (current == SCR_ACTIONS && millis() < actionFlashUntil) return true;
  return false;
}

// ── Input handling ──────────────────────────────────────────────────────────
void handleInput() {
  uint32_t now = millis();
  bool a = M5.BtnA.wasPressed();
  bool b = M5.BtnB.wasPressed();
  bool p = M5.BtnPWR.wasClicked();

  if (current == SCR_NOTIFY) {
    if (M5.BtnA.wasPressed()) {
      btnA_pressTime = now;
      btnA_held = true;
      if (btnA_taps == 0) btnA_firstTapT = now;
      if (btnA_taps < 2) btnA_taps++;
    }
    if (M5.BtnA.wasReleased()) btnA_held = false;
    if (btnA_held && btnA_taps == 1 && (now - btnA_pressTime) >= LONG_PRESS_MS) {
      vib(400, 200);
      sendChoice(notif.optCount > 1 ? notif.optCount - 1 : 0);
      btnA_taps = 0; btnA_held = false;
    }
    if (btnA_taps == 2 && !btnA_held) {
      vib(120); delay(80); vib(120);
      sendChoice(notif.optCount >= 2 ? 1 : 0);
      btnA_taps = 0;
    }
    if (btnA_taps == 1 && !btnA_held && (now - btnA_firstTapT) >= DOUBLE_TAP_MS) {
      vib(150);
      sendChoice(0);
      btnA_taps = 0;
    }
    if (notif.pending && (now - notifStart) >= (uint32_t)notif.timeout * 1000) {
      sendChoice(-1);
    }
    return;
  }

  btnA_taps = 0; btnA_held = false;

  if (current == SCR_IDLE) {
    if (a || b || p) { current = prevTab; idleTimer = now; dirty = true; }
    return;
  }

  if (current < TAB_COUNT) {
    if (a) {
      current = (Screen)((current + 1) % TAB_COUNT);
      idleTimer = now;
      vib(60);
      dirty = true;
    } else if (b) {
      idleTimer = now;
      if (current == SCR_INBOX && inboxCount > 0) {
        ui[SCR_INBOX].selIdx = (ui[SCR_INBOX].selIdx + 1) % inboxCount;
        dirty = true;
      } else if (current == SCR_ACTIONS) {
        ui[SCR_ACTIONS].selIdx = (ui[SCR_ACTIONS].selIdx + 1) % ACTION_COUNT;
        dirty = true;
      } else if (current == SCR_SETTINGS) {
        ui[SCR_SETTINGS].selIdx = (ui[SCR_SETTINGS].selIdx + 1) % SET_COUNT;
        dirty = true;
      }
    } else if (p) {
      idleTimer = now;
      if (current == SCR_INBOX && inboxCount > 0) {
        current = SCR_INBOX_DETAIL; dirty = true;
      } else if (current == SCR_ACTIONS) {
        sendAction(ACTIONS[ui[SCR_ACTIONS].selIdx].code); dirty = true;
      } else if (current == SCR_SETTINGS) {
        current = SCR_SETTINGS_EDIT; dirty = true;
      }
    }
    return;
  }

  if (current == SCR_INBOX_DETAIL) {
    if (b && inboxCount > 0) {
      ui[SCR_INBOX].selIdx = (ui[SCR_INBOX].selIdx + 1) % inboxCount;
      idleTimer = now; dirty = true;
    }
    if (a || p) { current = SCR_INBOX; idleTimer = now; dirty = true; }
    return;
  }

  if (current == SCR_SETTINGS_EDIT) {
    if (b) { settingStep(ui[SCR_SETTINGS].selIdx); idleTimer = now; vib(40); dirty = true; }
    if (a) { saveSettings(); current = SCR_SETTINGS; idleTimer = now; vib(80); dirty = true; }
    if (p) {
      loadSettings();
      StickCP2.Display.setBrightness(cfg.brightness);
      StickCP2.Speaker.setVolume(cfg.volume);
      current = SCR_SETTINGS;
      idleTimer = now;
      dirty = true;
    }
    return;
  }
}
