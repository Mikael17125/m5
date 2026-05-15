#include "state.h"
#include "ble.h"
#include "ui.h"
#include "sprite.h"
#include <Arduino.h>

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println("[boot] start");

  auto cfg_m5 = M5.config();
  cfg_m5.fallback_board = m5::board_t::board_M5StickCPlus2;
  StickCP2.begin(cfg_m5);
  Serial.printf("[boot] M5 board=%d (PLUS2=%d)\n", (int)M5.getBoard(), (int)m5::board_t::board_M5StickCPlus2);

  StickCP2.Display.setRotation(0);
  loadSettings();
  StickCP2.Display.setBrightness(cfg.brightness);
  StickCP2.Speaker.setVolume(cfg.volume);
  canvas.createSprite(135, 240);
  StickCP2.Power.setLed(0);
  idleTimer = millis();
  Serial.println("[boot] m5 ok, BLE next");

  setupBLE();
  Serial.println("[boot] BLE ok");

  // Prime button state before any wasPressed read
  M5.update();
  Serial.println("[boot] update primed");

  renderCurrent();
  Serial.println("[boot] render ok, entering loop");
}

void loop() {
  StickCP2.update();
  const uint32_t now = millis();

  // LED blink during notify
  if (current == SCR_NOTIFY && notif.ledBlink && now - ledTimer > 500) {
    ledState = !ledState;
    StickCP2.Power.setLed(ledState ? 1 : 0);
    ledTimer = now;
  }

  // Header blink tick (only matters on notify screen)
  if (now - blinkTimer > 500) {
    blinkOn = !blinkOn;
    blinkTimer = now;
    if (current == SCR_NOTIFY) dirty = true;
  }

  // Periodically refresh local M5 battery
  if (now - lastStatusBarMs > 2000) {
    lastStatusBarMs = now;
    int b  = StickCP2.Power.getBatteryLevel();
    bool c = StickCP2.Power.isCharging();
    if (b != m5Bat || c != m5Charging) {
      m5Bat = b;
      m5Charging = c;
      dirty = true;
    }
  }



  handleInput();

  // Idle transition from tab-root screens
  if (current < TAB_COUNT && cfg.idle_ms > 0 && (now - idleTimer) > cfg.idle_ms) {
    prevTab = current;
    current = SCR_IDLE;
    dirty = true;
  }

  // Avatar lifecycle: start/stop avatar background tasks
  if (avatarActive && current != SCR_IDLE) {
    stopIdleAvatar();
    dirty = true;
  }
  if (current == SCR_IDLE && !avatarActive) {
    startIdleAvatar();
  }

  // Render only when dirty OR screen animates (frame-capped)
  const bool animates = screenAnimates();
  if (dirty || (animates && (now - lastRenderMs) >= MIN_FRAME_MS)) {
    renderCurrent();
    lastRenderMs = now;
    dirty = false;
  }

  // Adaptive delay: snappy when active, calmer when idle (saves CPU + heat)
  delay(animates ? 10 : 30);
}
