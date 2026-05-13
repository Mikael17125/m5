#pragma once
#include "M5StickCPlus2.h"
#include <Preferences.h>
#include <stdint.h>

// ── Palette ──────────────────────────────────────────────────────────────────
#define C_BG       0x0922
#define C_HEADER   0x0C14
#define C_ACCENT   0x0389
#define C_WHITE    0xEF7B
#define C_GRAY     0x2B4B
#define C_GRAY2    0x5ACB
#define C_TRACK    0x1124
#define C_DIV      0x1134
#define C_GREEN    0x0652
#define C_CYAN     0x059B
#define C_PURPLE   0x4C5E
#define C_YELLOW   0xFD20
#define C_ORANGE   0xFC80
#define C_RED      0xF800
#define C_DARK_RED 0x8000
#define C_TAB_BG   0x08C2
#define C_TAB_HI   0x14B6

// ── Timing constants ────────────────────────────────────────────────────────
#define DOUBLE_TAP_MS  400
#define LONG_PRESS_MS  2000
#define MIN_FRAME_MS   80
#define BAR_REFRESH_MS 2000

// ── Sizes ───────────────────────────────────────────────────────────────────
#define MAX_SPRITES 8
#define INBOX_CAP   20

// ── Screen state ────────────────────────────────────────────────────────────
enum Screen {
  SCR_MONITOR = 0,
  SCR_INBOX,
  SCR_ACTIONS,
  SCR_SETTINGS,
  TAB_COUNT,
  SCR_INBOX_DETAIL,
  SCR_SETTINGS_EDIT,
  SCR_NOTIFY,
  SCR_IDLE
};

struct UIState {
  uint8_t selIdx;
  uint8_t scrollOff;
};

// ── Config ──────────────────────────────────────────────────────────────────
struct Config {
  uint8_t  volume;
  uint8_t  brightness;
  uint32_t idle_ms;
  bool     show_stats;
  bool     vibEnabled;
};

// ── Notification ────────────────────────────────────────────────────────────
struct Notification {
  char     id[9];
  char     title[21];
  char     body[121];
  char     spriteName[16];
  uint16_t headerColor;
  uint8_t  vibPat;
  uint8_t  beepPat;
  bool     ledBlink;
  char     options[3][13];
  uint8_t  optCount;
  uint16_t timeout;
  bool     pending;
};

struct InboxEntry {
  char     title[21];
  char     body[121];
  char     spriteName[16];
  uint16_t headerColor;
  uint32_t timestamp;
};

// ── Sprite ──────────────────────────────────────────────────────────────────
struct Sprite {
  char    name[16];
  uint8_t frameCount, w, h;
  uint8_t *frames[8];
  bool    ready;
};

// ── Quick Actions ───────────────────────────────────────────────────────────
struct ActionItem { const char *label; const char *code; };
extern const ActionItem ACTIONS[];
extern const int        ACTION_COUNT;

// ── Settings rows ───────────────────────────────────────────────────────────
enum SettingRow { SET_BRIGHT = 0, SET_VOLUME, SET_IDLE, SET_VIB, SET_COUNT };
extern const char *SETTING_LABELS[SET_COUNT];

// ── Globals (defined in state.cpp) ──────────────────────────────────────────
extern LGFX_Sprite canvas;
extern Preferences prefs;
extern Config      cfg;
extern Notification notif;
extern bool        notifReady;
extern InboxEntry  inbox[INBOX_CAP];
extern uint8_t     inboxHead, inboxCount;
extern Sprite      sprites[MAX_SPRITES];
extern int         spriteCount;
extern char        idleSpriteName[16];

extern bool connected, hasData, isCharg;
extern int  batPct, cpuPct, ramPct;

extern Screen   current, prevTab;
extern UIState  ui[TAB_COUNT];

extern uint32_t idleTimer, notifStart, blinkTimer, ledTimer;
extern bool     blinkOn, ledState;
extern uint32_t btnA_pressTime, btnA_firstTapT;
extern uint8_t  btnA_taps;
extern bool     btnA_held;
extern uint32_t actionFlashUntil;

extern bool     dirty;
extern uint32_t lastRenderMs, lastStatusBarMs;
extern int      m5Bat;
extern bool     m5Charging;

extern class BLECharacteristic *evtChar;

// ── Persistence ─────────────────────────────────────────────────────────────
void loadSettings();
void saveSettings();
