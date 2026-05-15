#include "ui.h"
#include "ble.h"
#include "sprite.h"
#include "util.h"
#include <Arduino.h>
#include <Face.h>
#include <Eye.h>
#include <Mouth.h>
#include <Eyeblow.h>
#include <BoundingRect.h>

using namespace m5avatar;
Avatar avatar;
bool avatarActive = false;

// ── Chrome ──────────────────────────────────────────────────────────────────
static void drawStatusBar() {
  canvas.fillRect(0, 0, 135, 22, C_HEADER);
  drawLogo(4, 4);
  canvas.setFont(&fonts::Font0);
  canvas.setTextColor(C_GRAY2);
  canvas.setCursor(22, 7);
  canvas.print("M5");
  canvas.fillCircle(65, 11, 3, connected ? C_GREEN : C_RED);
  canvas.setTextColor(connected ? C_GREEN : C_GRAY2);
  canvas.setCursor(72, 7);
  canvas.print(connected ? "BLE" : "---");
  drawBatteryIcon(102, 6, m5Bat, m5Charging);
  canvas.setTextColor(C_WHITE);
  canvas.setCursor(102, 14);
  // canvas.printf("%d%%", m5Bat); // Too cramped? Let's skip % for now or make
  // it tiny
  canvas.drawFastHLine(0, 22, 135, C_ACCENT);
}

static void drawTabBar(int active) {
  canvas.fillRect(0, 215, 135, 25, C_TAB_BG);
  canvas.drawFastHLine(0, 215, 135, C_DIV);
  for (int i = 0; i < TAB_COUNT; i++) {
    int x = i * 135 / TAB_COUNT;
    int nextX = (i + 1) * 135 / TAB_COUNT;
    int cellW = nextX - x;
    int centerX = x + cellW / 2;
    if (i == active) {
      canvas.fillRect(x, 216, cellW, 24, C_TAB_HI);
      canvas.fillRect(x, 215, cellW, 2, C_ACCENT);
    }
    uint16_t col = (i == active) ? C_WHITE : C_GRAY2;
    drawTabIcon(i, centerX, 227, col);
  }
}



static void renderScreenInbox() {
  drawStatusBar();
  drawTabBar(SCR_INBOX);
  canvas.setFont(&fonts::Font0);
  if (inboxCount == 0) {
    canvas.setTextColor(C_GRAY2);
    canvas.setCursor(30, 100);
    canvas.print("Inbox empty");
    return;
  }
  uint8_t sel = ui[SCR_INBOX].selIdx;
  uint8_t off = ui[SCR_INBOX].scrollOff;
  if (sel < off)
    off = sel;
  if (sel >= off + 8)
    off = sel - 7;
  ui[SCR_INBOX].scrollOff = off;
  for (int i = 0; i < 8 && off + i < inboxCount; i++) {
    const InboxEntry *e = inboxAt(off + i);
    if (!e)
      continue;
    int y = 26 + i * 23;
    bool isSel = (off + i == sel);
    if (isSel)
      canvas.fillRoundRect(2, y - 2, 128, 22, 3, C_TAB_HI);
    canvas.fillCircle(8, y + 7, 3, e->headerColor);
    canvas.setTextColor(C_WHITE);
    canvas.setCursor(16, y);
    char tb[16];
    strncpy(tb, e->title, 15);
    tb[15] = 0;
    canvas.print(tb);
    canvas.setTextColor(C_GRAY2);
    canvas.setCursor(16, y + 10);
    char pb[21];
    strncpy(pb, e->body, 20);
    pb[20] = 0;
    canvas.print(pb);
    canvas.setTextColor(C_GRAY2);
    char age[8];
    formatAge(e->timestamp, age, sizeof(age));
    canvas.setCursor(105, y);
    canvas.print(age);
  }
  if (inboxCount > 8) {
    int barH = max(8, 180 * 8 / inboxCount);
    int barY = 26 + (180 - barH) * off / max(1, (int)(inboxCount - 8));
    canvas.fillRect(131, 26, 2, 180, C_TRACK);
    canvas.fillRect(131, barY, 2, barH, C_GRAY2);
  }
}

static void renderScreenInboxDetail() {
  uint8_t idx = ui[SCR_INBOX].selIdx;
  const InboxEntry *e = inboxAt(idx);
  if (!e) {
    current = SCR_INBOX;
    return;
  }
  canvas.fillScreen(C_BG);
  canvas.fillRect(0, 0, 135, 24, e->headerColor);
  canvas.setFont(&fonts::Font0);
  canvas.setTextColor(C_WHITE);
  canvas.setCursor(6, 7);
  char tb[16];
  strncpy(tb, e->title, 15);
  tb[15] = 0;
  canvas.print(tb);
  Sprite *sp = strlen(e->spriteName) > 0 ? findSprite(e->spriteName) : nullptr;
  if (sp && sp->ready) {
    int frame = (millis() / 200) % sp->frameCount;
    drawSprite(sp, frame, 115 - sp->w, (24 - sp->h) / 2);
  }
  canvas.setFont(&fonts::Font0);
  canvas.setTextColor(C_WHITE);
  int dlen = strlen(e->body);
  for (int line = 0; line < 12 && line * 22 < dlen; line++) {
    char lbuf[23];
    strncpy(lbuf, e->body + line * 22, 22);
    lbuf[22] = 0;
    canvas.setCursor(6, 30 + line * 11);
    canvas.print(lbuf);
  }
  canvas.drawFastHLine(0, 215, 135, C_DIV);
  canvas.setTextColor(C_GRAY2);
  char age[8];
  formatAge(e->timestamp, age, sizeof(age));
  canvas.setCursor(6, 222);
  canvas.printf("%s ago", age);
  canvas.setTextColor(C_ACCENT);
  canvas.setCursor(80, 222);
  canvas.print("B=Next");
}

static void renderScreenActions() {
  drawStatusBar();
  drawTabBar(SCR_ACTIONS);
  canvas.setFont(&fonts::Font0);
  uint8_t sel = ui[SCR_ACTIONS].selIdx;
  uint8_t off = ui[SCR_ACTIONS].scrollOff;
  if (sel < off)
    off = sel;
  if (sel >= off + 8)
    off = sel - 7;
  ui[SCR_ACTIONS].scrollOff = off;
  for (int i = 0; i < 8 && off + i < ACTION_COUNT; i++) {
    int y = 26 + i * 23;
    bool isSel = (off + i == sel);
    if (isSel) {
      bool flash = millis() < actionFlashUntil;
      canvas.fillRoundRect(2, y - 2, 128, 22, 3, flash ? C_GREEN : C_TAB_HI);
    }
    canvas.setTextColor(isSel ? C_WHITE : C_GRAY2);
    canvas.setCursor(10, y + 3);
    canvas.print(ACTIONS[off + i].label);
    if (isSel) {
      canvas.setTextColor(C_ACCENT);
      canvas.setCursor(118, y + 3);
      canvas.print(">");
    }
  }
  if (ACTION_COUNT > 8) {
    int barH = max(8, 180 * 8 / ACTION_COUNT);
    int barY = 26 + (180 - barH) * off / max(1, ACTION_COUNT - 8);
    canvas.fillRect(131, 26, 2, 180, C_TRACK);
    canvas.fillRect(131, barY, 2, barH, C_GRAY2);
  }
}

static int settingValue(int row) {
  switch (row) {
  case SET_BRIGHT:
    return cfg.brightness;
  case SET_VOLUME:
    return cfg.volume;
  case SET_IDLE:
    return cfg.idle_ms / 1000;
  case SET_VIB:
    return cfg.vibEnabled ? 1 : 0;
  }
  return 0;
}

static void renderScreenSettings() {
  drawStatusBar();
  drawTabBar(SCR_SETTINGS);
  canvas.setFont(&fonts::Font0);
  uint8_t sel = ui[SCR_SETTINGS].selIdx;
  for (int i = 0; i < SET_COUNT; i++) {
    int y = 26 + i * 23;
    bool isSel = (i == sel);
    if (isSel)
      canvas.fillRoundRect(2, y - 2, 128, 22, 3, C_TAB_HI);
    canvas.setTextColor(isSel ? C_WHITE : C_GRAY2);
    canvas.setCursor(10, y + 3);
    canvas.print(SETTING_LABELS[i]);
    int val = settingValue(i);
    char vb[12];
    if (i == SET_VIB)
      snprintf(vb, sizeof(vb), "%s", val ? "ON" : "OFF");
    else
      snprintf(vb, sizeof(vb), "%d", val);
    canvas.setTextColor(isSel ? C_ACCENT : C_GRAY2);
    canvas.setCursor(100, y + 3);
    canvas.print(vb);
  }
}

static void renderScreenSettingsEdit() {
  drawStatusBar();
  uint8_t row = ui[SCR_SETTINGS].selIdx;
  canvas.setFont(&fonts::Font0);
  canvas.setTextColor(C_WHITE);
  canvas.setCursor(8, 32);
  canvas.print("Edit: ");
  canvas.print(SETTING_LABELS[row]);

  int val = settingValue(row);
  canvas.setTextColor(C_ACCENT);
  canvas.setCursor(8, 60);
  char vb[16];
  if (row == SET_VIB)
    snprintf(vb, sizeof(vb), "%s", val ? "ON" : "OFF");
  else
    snprintf(vb, sizeof(vb), "%d", val);
  canvas.print(vb);

  int barMax = 255;
  if (row == SET_IDLE)
    barMax = 300;
  if (row == SET_VIB)
    barMax = 1;
  if (row != SET_VIB) {
    int pct = val * 100 / barMax;
    drawBar(8, 78, 119, 8, pct, C_ACCENT);
  }
  canvas.drawFastHLine(0, 215, 135, C_DIV);
  canvas.fillRect(0, 215, 135, 25, C_TAB_BG);
  canvas.setTextColor(C_GRAY2);
  canvas.setCursor(8, 225);
  canvas.print("B=+  PWR=can  A=sav");
}

// ── Avatar lifecycle ──────────────────────────────────────────────────────
void startIdleAvatar() {
  if (avatarActive) return;
  static bool faceConfigured = false;
  if (!faceConfigured) {
    // Custom Face designed for 135×240 portrait display
    BoundingRect *faceRect = new BoundingRect(0, 0, 135, 240);

    // Eyes: big (r=10), centered vertically in screen
    BoundingRect *eyeLPos = new BoundingRect(110, 46);
    BoundingRect *eyeRPos = new BoundingRect(110, 88);
    Eye *eyeL = new Eye(10, true);   // left eye
    Eye *eyeR = new Eye(10, false);  // right eye

    // Mouth: centered below eyes
    BoundingRect *mouthPos = new BoundingRect(145, 67);
    Mouth *mouth = new Mouth(18, 36, 4, 20);

    // Eyebrows: above each eye
    BoundingRect *eyebrowLPos = new BoundingRect(88, 46);
    BoundingRect *eyebrowRPos = new BoundingRect(88, 88);
    Eyeblow *eyebrowL = new Eyeblow(24, 3, true);
    Eyeblow *eyebrowR = new Eyeblow(24, 3, false);

    M5Canvas *spr = new M5Canvas(&M5.Lcd);
    M5Canvas *tmpSpr = new M5Canvas(&M5.Lcd);

    Face *portraitFace = new Face(mouth, mouthPos, eyeR, eyeRPos,
                                  eyeL, eyeLPos, eyebrowR, eyebrowRPos,
                                  eyebrowL, eyebrowLPos,
                                  faceRect, spr, tmpSpr);
    avatar.setFace(portraitFace);
    faceConfigured = true;
  }
  avatar.setScale(1.0f);        // native size for 135×240
  avatar.setPosition(0, 0);     // full screen
  // Match palette to our dark theme
  {
    ColorPalette cp;
    cp.set(COLOR_PRIMARY, C_WHITE);
    cp.set(COLOR_BACKGROUND, C_BG);
    cp.set(COLOR_SECONDARY, C_GRAY2);
    avatar.setColorPalette(cp);
  }
  avatar.start(1);              // 1-bit color depth (saves RAM)
  avatarActive = true;
}

void stopIdleAvatar() {
  if (!avatarActive) return;
  avatar.stop();
  avatarActive = false;
}

static void renderScreenIdle() {
  // Avatar library draws anime face directly to M5.Display via bg tasks
  // Nothing to draw on our canvas
}

static void renderScreenNotify() {
  canvas.fillScreen(C_BG);
  canvas.fillRect(0, 0, 135, 26, notif.headerColor);
  canvas.setFont(&fonts::Font0);
  canvas.setTextColor(C_WHITE);
  canvas.setCursor(6, 8);
  char tbuf[16];
  strncpy(tbuf, notif.title, 15);
  tbuf[15] = 0;
  canvas.print(tbuf);

  Sprite *sp =
      strlen(notif.spriteName) > 0 ? findSprite(notif.spriteName) : nullptr;
  if (sp && sp->ready) {
    int frame = (millis() / 200) % sp->frameCount;
    drawSprite(sp, frame, 110 - sp->w, (26 - sp->h) / 2);
  } else if (blinkOn) {
    canvas.fillCircle(125, 13, 4, C_WHITE);
  }
  canvas.drawFastHLine(0, 26, 135, C_RED);

  canvas.setFont(&fonts::Font0);
  canvas.setTextColor(C_WHITE);
  int dlen = strlen(notif.body);
  for (int line = 0; line < 10 && line * 22 < dlen; line++) {
    char lbuf[23];
    strncpy(lbuf, notif.body + line * 22, 22);
    lbuf[22] = 0;
    canvas.setCursor(6, 30 + line * 11);
    canvas.print(lbuf);
  }

  canvas.drawFastHLine(4, 150, 127, C_DIV);

  canvas.setFont(&fonts::Font0);
  if (notif.optCount == 1) {
    canvas.fillRoundRect(4, 160, 127, 28, 4, C_GREEN);
    canvas.setTextColor(0x0000);
    canvas.setCursor(20, 170);
    canvas.print(notif.options[0]);
  } else if (notif.optCount >= 2) {
    for (int i = 0; i < notif.optCount && i < 3; i++) {
      int y = 160 + i * 22;
      canvas.setTextColor(i == 0 ? C_GREEN : (i == 1 ? C_CYAN : C_DARK_RED));
      canvas.setCursor(6, y);
      canvas.printf("%s: %s", i == 0 ? "Tap" : (i == 1 ? "x2" : "Hld"),
                    notif.options[i]);
    }
  }
}

void renderCurrent() {
  if (current == SCR_IDLE) {
    renderScreenIdle();
    return; // avatar bg tasks draw directly to M5.Display
  }
  canvas.fillScreen(C_BG);
  switch (current) {
  case SCR_INBOX:
    renderScreenInbox();
    break;
  case SCR_ACTIONS:
    renderScreenActions();
    break;
  case SCR_SETTINGS:
    renderScreenSettings();
    break;
  case SCR_INBOX_DETAIL:
    renderScreenInboxDetail();
    break;
  case SCR_SETTINGS_EDIT:
    renderScreenSettingsEdit();
    break;
  case SCR_NOTIFY:
    renderScreenNotify();
    break;
  default:
    break;
  }
  canvas.pushSprite(0, 0);
}

// ── Setting edit step ───────────────────────────────────────────────────────
static void settingStep(int row) {
  switch (row) {
  case SET_BRIGHT:
    cfg.brightness += 25;
    if (cfg.brightness > 250)
      cfg.brightness = 25;
    StickCP2.Display.setBrightness(cfg.brightness);
    break;
  case SET_VOLUME:
    cfg.volume += 32;
    if (cfg.volume > 240)
      cfg.volume = 0;
    StickCP2.Speaker.setVolume(cfg.volume);
    break;
  case SET_IDLE: {
    uint32_t s = cfg.idle_ms / 1000;
    s += 15;
    if (s > 300)
      s = 15;
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
  if (current == SCR_NOTIFY)
    return true;
  if (current == SCR_IDLE) {
    return false;  // avatar bg tasks animate themselves
  }
  if (current == SCR_INBOX_DETAIL) {
    const InboxEntry *e = inboxAt(ui[SCR_INBOX].selIdx);
    if (e && strlen(e->spriteName) > 0 && findSprite(e->spriteName))
      return true;
  }
  if (current == SCR_ACTIONS && millis() < actionFlashUntil)
    return true;
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
      if (btnA_taps == 0)
        btnA_firstTapT = now;
      if (btnA_taps < 2)
        btnA_taps++;
    }
    if (M5.BtnA.wasReleased())
      btnA_held = false;
    if (btnA_held && btnA_taps == 1 &&
        (now - btnA_pressTime) >= LONG_PRESS_MS) {
      sendChoice(notif.optCount > 1 ? notif.optCount - 1 : 0);
      vib(400, 200);
      btnA_taps = 0;
      btnA_held = false;
    }
    if (btnA_taps == 2 && !btnA_held) {
      sendChoice(notif.optCount >= 2 ? 1 : 0);
      vib(120);
      delay(80);
      vib(120);
      btnA_taps = 0;
    }
    if (btnA_taps == 1 && !btnA_held &&
        (now - btnA_firstTapT) >= DOUBLE_TAP_MS) {
      vib(150);
      sendChoice(0);
      btnA_taps = 0;
    }
    if (notif.pending && (now - notifStart) >= (uint32_t)notif.timeout * 1000) {
      sendChoice(-1);
    }
    return;
  }

  btnA_taps = 0;
  btnA_held = false;

  if (current == SCR_IDLE) {
    if (a || b || p) {
      current = prevTab;
      idleTimer = now;
      dirty = true;
    }
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
        current = SCR_INBOX_DETAIL;
        dirty = true;
      } else if (current == SCR_ACTIONS) {
        sendAction(ACTIONS[ui[SCR_ACTIONS].selIdx].code);
        dirty = true;
      } else if (current == SCR_SETTINGS) {
        current = SCR_SETTINGS_EDIT;
        dirty = true;
      }
    }
    return;
  }

  if (current == SCR_INBOX_DETAIL) {
    if (b && inboxCount > 0) {
      ui[SCR_INBOX].selIdx = (ui[SCR_INBOX].selIdx + 1) % inboxCount;
      idleTimer = now;
      dirty = true;
    }
    if (a || p) {
      current = SCR_INBOX;
      idleTimer = now;
      dirty = true;
    }
    return;
  }

  if (current == SCR_SETTINGS_EDIT) {
    if (b) {
      settingStep(ui[SCR_SETTINGS].selIdx);
      idleTimer = now;
      vib(40);
      dirty = true;
    }
    if (a) {
      saveSettings();
      current = SCR_SETTINGS;
      idleTimer = now;
      vib(80);
      dirty = true;
    }
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
