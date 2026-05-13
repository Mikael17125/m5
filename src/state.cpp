#include "state.h"
#include <BLEDevice.h>

LGFX_Sprite canvas(&StickCP2.Display);
Preferences prefs;
Config      cfg = { 200, 80, 30000, true, true };

Notification notif = {};
bool         notifReady = false;

InboxEntry inbox[INBOX_CAP];
uint8_t    inboxHead  = 0;
uint8_t    inboxCount = 0;

Sprite sprites[MAX_SPRITES];
int    spriteCount = 0;
char   idleSpriteName[16] = "";

bool connected = false;
bool hasData   = false;
bool isCharg   = false;
int  batPct = 0, cpuPct = 0, ramPct = 0;

Screen  current = SCR_MONITOR;
Screen  prevTab = SCR_MONITOR;
UIState ui[TAB_COUNT] = {};

uint32_t idleTimer   = 0;
uint32_t notifStart  = 0;
uint32_t blinkTimer  = 0;
uint32_t ledTimer    = 0;
bool     blinkOn     = false;
bool     ledState    = false;
uint32_t btnA_pressTime = 0;
uint32_t btnA_firstTapT = 0;
uint8_t  btnA_taps      = 0;
bool     btnA_held      = false;
uint32_t actionFlashUntil = 0;

bool     dirty           = true;
uint32_t lastRenderMs    = 0;
uint32_t lastStatusBarMs = 0;
int      m5Bat           = 0;
bool     m5Charging      = false;

BLECharacteristic *evtChar = nullptr;

const ActionItem ACTIONS[] = {
  { "Lock Screen",  "lock"      },
  { "Sleep Mac",    "sleep"     },
  { "Mute Toggle",  "mute"      },
  { "Play / Pause", "playpause" },
  { "Next Track",   "next"      },
  { "Show Desktop", "desktop"   },
};
const int ACTION_COUNT = sizeof(ACTIONS) / sizeof(ACTIONS[0]);

const char *SETTING_LABELS[SET_COUNT] = { "Brightness", "Volume", "Idle (s)", "Vibration" };

void loadSettings() {
  prefs.begin("m5dock", true);
  cfg.brightness = prefs.getUChar("bright", cfg.brightness);
  cfg.volume     = prefs.getUChar("vol",    cfg.volume);
  cfg.idle_ms    = prefs.getUInt ("idle",   cfg.idle_ms);
  cfg.vibEnabled = prefs.getBool ("vib",    cfg.vibEnabled);
  prefs.end();
}

void saveSettings() {
  prefs.begin("m5dock", false);
  prefs.putUChar("bright", cfg.brightness);
  prefs.putUChar("vol",    cfg.volume);
  prefs.putUInt ("idle",   cfg.idle_ms);
  prefs.putBool ("vib",    cfg.vibEnabled);
  prefs.end();
}
