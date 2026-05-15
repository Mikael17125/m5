#include "ble.h"
#include "util.h"
#include "sprite.h"
#include <Arduino.h>
#include <BLE2902.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>

// ── CMD chunk assembly buffer ────────────────────────────────────────────────
static char cmdBuf[2048];
static int  cmdBufLen = 0, cmdTotal = 0, cmdRecv = 0;

// ── Inbox ring buffer ────────────────────────────────────────────────────────
void pushInbox(const Notification &n) {
  InboxEntry &e = inbox[inboxHead];
  strncpy(e.title, n.title, sizeof(e.title) - 1); e.title[sizeof(e.title) - 1] = 0;
  strncpy(e.body, n.body, sizeof(e.body) - 1); e.body[sizeof(e.body) - 1] = 0;
  strncpy(e.spriteName, n.spriteName, sizeof(e.spriteName) - 1); e.spriteName[sizeof(e.spriteName) - 1] = 0;
  e.headerColor = n.headerColor;
  e.timestamp = millis();
  inboxHead = (inboxHead + 1) % INBOX_CAP;
  if (inboxCount < INBOX_CAP) inboxCount++;
}

const InboxEntry *inboxAt(uint8_t idx) {
  if (idx >= inboxCount) return nullptr;
  int real = (int)inboxHead - 1 - (int)idx;
  while (real < 0) real += INBOX_CAP;
  return &inbox[real];
}

// ── TX ──────────────────────────────────────────────────────────────────────
void sendEvent(const char *json) {
  if (!evtChar || !connected) return;
  evtChar->setValue((uint8_t *)json, strlen(json));
  evtChar->notify();
  Serial.printf("TX: %s\n", json);
}

void sendChoice(int idx) {
  char buf[40];
  snprintf(buf, sizeof(buf), "{\"id\":\"%s\",\"choice\":%d}", notif.id, idx);
  sendEvent(buf);
  notif.pending = false;
  notifReady    = false;
  StickCP2.Power.setLed(0);
  current   = prevTab;
  idleTimer = millis();
  dirty = true;
}

void sendAction(const char *code) {
  char buf[48];
  snprintf(buf, sizeof(buf), "{\"act\":\"%s\"}", code);
  sendEvent(buf);
  actionFlashUntil = millis() + 250;
  vib(120);
}

// ── CMD handlers ────────────────────────────────────────────────────────────
static void handleStats(const char *s) {
  // No-op - monitor screen removed, stats still parsed but not displayed
}

static void handleNotify(const char *s) {
  char hcStr[16], vibStr[16], beepStr[16];
  jsonStr(s, "\"id\"", notif.id, sizeof(notif.id));
  jsonStr(s, "\"ti\"", notif.title, sizeof(notif.title));
  jsonStr(s, "\"bo\"", notif.body, sizeof(notif.body));
  jsonStr(s, "\"hc\"", hcStr, sizeof(hcStr));
  jsonStr(s, "\"vb\"", vibStr, sizeof(vibStr));
  jsonStr(s, "\"bp\"", beepStr, sizeof(beepStr));
  jsonStr(s, "\"sp\"", notif.spriteName, sizeof(notif.spriteName));
  notif.headerColor = parseColor(hcStr);
  notif.vibPat      = parseVib(vibStr);
  notif.beepPat     = parseBeep(beepStr);
  notif.ledBlink    = jsonInt(s, "\"led\":") == 1;
  notif.timeout     = jsonInt(s, "\"tout\":");
  if (notif.timeout == 0) notif.timeout = 60;

  notif.optCount = 0;
  const char *op = strstr(s, "\"op\":[");
  if (op) {
    op += 6;
    for (int i = 0; i < 3 && *op && *op != ']'; i++) {
      while (*op && *op != '"') op++;
      if (!*op) break;
      op++;
      int j = 0;
      while (*op && *op != '"' && j < 12) notif.options[i][j++] = *op++;
      notif.options[i][j] = 0;
      if (*op == '"') op++;
      notif.optCount++;
    }
  }
  if (notif.optCount == 0) {
    strcpy(notif.options[0], "OK");
    notif.optCount = 1;
  }

  notif.pending = true;
  notifReady    = true;
  notifStart    = millis();

  pushInbox(notif);

  if (current != SCR_NOTIFY) prevTab = (current < TAB_COUNT) ? current : SCR_INBOX;
  current = SCR_NOTIFY;
  idleTimer = millis();
  doVib(notif.vibPat);
  doBeep(notif.beepPat);
}

static void handleDismiss(const char *s) {
  char did[9];
  jsonStr(s, "\"id\"", did, sizeof(did));
  if (strcmp(did, notif.id) == 0 && current == SCR_NOTIFY) {
    notif.pending = false;
    notifReady    = false;
    StickCP2.Power.setLed(0);
    current   = prevTab;
    idleTimer = millis();
  }
}

static void handleConfig(const char *s) {
  int br = jsonInt(s, "\"bright\":");
  if (br > 0) {
    cfg.brightness = constrain(br, 10, 255);
    StickCP2.Display.setBrightness(cfg.brightness);
  }
  int im = jsonInt(s, "\"idle_ms\":");
  if (im > 0) cfg.idle_ms = im;
  int vol = jsonInt(s, "\"vol\":");
  if (vol > 0) { cfg.volume = constrain(vol, 0, 255); StickCP2.Speaker.setVolume(cfg.volume); }
  saveSettings();
}
  int im = jsonInt(s, "\"idle_ms\":");
  if (im > 0) cfg.idle_ms = im;
  int vol = jsonInt(s, "\"vol\":");
  if (vol > 0) { cfg.volume = constrain(vol, 0, 255); StickCP2.Speaker.setVolume(cfg.volume); }
  saveSettings();
}

void dispatchCmd(const char *s) {
  char t[8]; jsonStr(s, "\"t\"", t, sizeof(t));
  // Stats / config / sprite uploads happen silently while the device is idle.
  if      (strcmp(t, "s")    == 0) handleStats(s);
  else if (strcmp(t, "n")    == 0) { idleTimer = millis(); handleNotify(s); dirty = true; }
  else if (strcmp(t, "d")    == 0) { handleDismiss(s); dirty = true; }
  else if (strcmp(t, "cfg")  == 0) { handleConfig(s); if (current != SCR_IDLE) dirty = true; }
  else if (strcmp(t, "spb")  == 0) handleSprBegin(s);
  else if (strcmp(t, "spf")  == 0) { handleSprFrameDone(s); if (current != SCR_IDLE) dirty = true; }
}

// ── Server / characteristic callbacks ───────────────────────────────────────
class ServerCB : public BLEServerCallbacks {
  void onConnect(BLEServer *) override {
    connected = true;
    dirty = true;
    Serial.println("BLE Connected");
  }
  void onDisconnect(BLEServer *) override {
    connected = false;
    if (current == SCR_NOTIFY) {
      notif.pending = false;
      notifReady    = false;
      StickCP2.Power.setLed(0);
      current = prevTab;
    }
    dirty = true;
    Serial.println("BLE Disconnected");
    BLEDevice::startAdvertising();
  }
};

class CmdCB : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic *c) override {
    std::string v = c->getValue();
    if (v.length() < 2) return;
    const uint8_t *data = (const uint8_t *)v.c_str();
    int len   = v.length();
    int total = data[0];
    int idx   = data[1];
    if (idx == 0) { cmdBufLen = 0; cmdTotal = total; cmdRecv = 0; }
    int payload = len - 2;
    if (cmdBufLen + payload < (int)sizeof(cmdBuf)) {
      memcpy(cmdBuf + cmdBufLen, data + 2, payload);
      cmdBufLen += payload;
    }
    cmdRecv++;
    if (cmdRecv == cmdTotal) {
      cmdBuf[cmdBufLen] = 0;
      dispatchCmd(cmdBuf);
    }
  }
};

class SprCB : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic *c) override {
    std::string v = c->getValue();
    if (v.length() < 2) return;
    const uint8_t *data = (const uint8_t *)v.c_str();
    int len   = v.length();
    int total = data[0];
    int idx   = data[1];
    if (idx == 0) { sprBufLen = 0; sprTotal = total; sprRecv = 0; }
    int payload = len - 2;
    if (sprBufLen + payload < (int)sizeof(sprBuffer)) {
      memcpy(sprBuffer + sprBufLen, data + 2, payload);
      sprBufLen += payload;
    }
    sprRecv++;
  }
};

void setupBLE() {
  BLEDevice::init("M5StickMonitor");
  BLEDevice::setMTU(512);
  BLEServer *srv = BLEDevice::createServer();
  srv->setCallbacks(new ServerCB());
  BLEService *svc = srv->createService(BLEUUID(SERVICE_UUID), 32);

  BLECharacteristic *cmdChar = svc->createCharacteristic(
    CHAR_CMD_UUID,
    BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_WRITE_NR);
  cmdChar->setCallbacks(new CmdCB());

  BLECharacteristic *sprChar = svc->createCharacteristic(
    CHAR_SPR_UUID,
    BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_WRITE_NR);
  sprChar->setCallbacks(new SprCB());

  evtChar = svc->createCharacteristic(
    CHAR_EVT_UUID,
    BLECharacteristic::PROPERTY_NOTIFY | BLECharacteristic::PROPERTY_READ);
  evtChar->addDescriptor(new BLE2902());

  svc->start();
  BLEAdvertising *adv = BLEDevice::getAdvertising();
  adv->addServiceUUID(SERVICE_UUID);
  adv->setScanResponse(true);
  BLEDevice::startAdvertising();
}
