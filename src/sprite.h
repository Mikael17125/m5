#pragma once
#include "state.h"

Sprite *findSprite(const char *name);
void    drawSprite(Sprite *sp, int frame, int x, int y);

// Small icon drawers
void drawBar        (int x, int y, int w, int h, int pct, uint16_t color);
void drawLogo       (int x, int y);
void drawBatteryIcon(int x, int y, int pct, bool charging);
void drawTabIcon    (int tab, int cx, int cy, uint16_t col);

// Sprite upload handlers (called from BLE cmd dispatch)
void handleSprBegin    (const char *s);
void handleSprFrameDone(const char *s);
void handleSetIdle     (const char *s);

// Sprite frame chunk-receive buffer (shared with SprCB)
extern uint8_t sprBuffer[16384];
extern int     sprBufLen, sprTotal, sprRecv;
