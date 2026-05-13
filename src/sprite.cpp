#include "sprite.h"
#include "util.h"
#include <Arduino.h>

uint8_t sprBuffer[16384];
int     sprBufLen = 0, sprTotal = 0, sprRecv = 0;
static int sprSlot = -1, sprFrameIdx = 0;

// Scratch buffer for decoded RGB565 sprite (up to 64×64 = 4KB).
// Reused per draw call — only one sprite drawn at a time.
#define SPR_BUF_MAX_PIXELS  (64 * 64)
static uint16_t sprDecodeBuf[SPR_BUF_MAX_PIXELS];

Sprite *findSprite(const char *name) {
  for (int i = 0; i < spriteCount; i++)
    if (strcmp(sprites[i].name, name) == 0) return &sprites[i];
  return nullptr;
}

void drawSprite(Sprite *sp, int frame, int x, int y) {
  if (!sp || !sp->ready || frame >= sp->frameCount) return;
  uint8_t *data = sp->frames[frame];
  if (!data) return;
  int w = sp->w, h = sp->h;
  int total = w * h;
  if (total > SPR_BUF_MAX_PIXELS) return;  // refuse oversized

  // Unpack palette (16 entries × big-endian RGB565)
  uint16_t pal[16];
  for (int i = 0; i < 16; i++) {
    pal[i] = ((uint16_t)data[i * 2] << 8) | data[i * 2 + 1];
  }

  // Decode 4bpp → RGB565 in one pass (no per-pixel SPI calls)
  uint8_t *px = data + 32;
  uint16_t *out = sprDecodeBuf;
  int half = total >> 1;
  for (int i = 0; i < half; i++) {
    uint8_t byte = px[i];
    *out++ = pal[byte >> 4];
    *out++ = pal[byte & 0x0F];
  }
  // If total is odd, last nibble handled separately
  if (total & 1) {
    sprDecodeBuf[total - 1] = pal[(px[half] >> 4)];
  }
  canvas.pushImage(x, y, w, h, sprDecodeBuf);
}

void drawBar(int x, int y, int w, int h, int pct, uint16_t color) {
  // Thin bars look identical with fillRect (no rounded-corner pixel math).
  canvas.fillRect(x, y, w, h, C_TRACK);
  int fill = constrain(pct, 0, 100) * w / 100;
  if (fill > 0) canvas.fillRect(x, y, fill, h, color);
}

void drawLogo(int x, int y) {
  canvas.drawRoundRect(x, y, 14, 14, 3, C_WHITE);
  canvas.fillRoundRect(x + 3, y + 3, 8, 8, 1, C_ACCENT);
  canvas.fillCircle(x + 7, y + 7, 1, C_WHITE);
}

void drawBatteryIcon(int x, int y, int pct, bool charging) {
  canvas.drawRoundRect(x, y, 18, 9, 1, C_WHITE);
  canvas.fillRect(x + 18, y + 2, 2, 5, C_WHITE);
  int fill = constrain(pct, 0, 100) * 14 / 100;
  uint16_t col = batColor(pct);
  if (charging) col = C_YELLOW;
  if (fill > 0) canvas.fillRect(x + 2, y + 2, fill, 5, col);
}

void drawTabIcon(int tab, int cx, int cy, uint16_t col) {
  switch (tab) {
    case SCR_MONITOR:
      canvas.fillRect(cx - 5, cy + 1, 2, 4, col);
      canvas.fillRect(cx - 1, cy - 2, 2, 7, col);
      canvas.fillRect(cx + 3, cy - 4, 2, 9, col);
      break;
    case SCR_INBOX:
      canvas.drawRect(cx - 6, cy - 4, 12, 9, col);
      canvas.drawLine(cx - 6, cy - 4, cx, cy + 1, col);
      canvas.drawLine(cx + 6, cy - 4, cx, cy + 1, col);
      break;
    case SCR_ACTIONS:
      canvas.fillTriangle(cx - 1, cy - 5, cx + 3, cy - 5, cx - 1, cy,     col);
      canvas.fillTriangle(cx - 3, cy,     cx + 1, cy,     cx + 1, cy + 5, col);
      break;
    case SCR_SETTINGS:
      canvas.fillCircle(cx, cy, 4, col);
      canvas.fillCircle(cx, cy, 2, C_TAB_BG);
      canvas.fillRect(cx - 1, cy - 6, 2, 2, col);
      canvas.fillRect(cx - 1, cy + 4, 2, 2, col);
      canvas.fillRect(cx - 6, cy - 1, 2, 2, col);
      canvas.fillRect(cx + 4, cy - 1, 2, 2, col);
      break;
  }
}

void handleSprBegin(const char *s) {
  char name[16];
  jsonStr(s, "\"name\"", name, sizeof(name));
  int fc = jsonInt(s, "\"frames\":");
  int w  = jsonInt(s, "\"w\":");
  int h  = jsonInt(s, "\"h\":");
  if (w == 0) w = 32;
  if (h == 0) h = 32;

  sprSlot = -1;
  for (int i = 0; i < spriteCount; i++) {
    if (strcmp(sprites[i].name, name) == 0) { sprSlot = i; break; }
  }
  if (sprSlot < 0 && spriteCount < MAX_SPRITES) sprSlot = spriteCount++;
  if (sprSlot < 0) return;

  for (int f = 0; f < 8; f++) {
    if (sprites[sprSlot].frames[f]) { free(sprites[sprSlot].frames[f]); sprites[sprSlot].frames[f] = nullptr; }
  }
  strncpy(sprites[sprSlot].name, name, 15);
  sprites[sprSlot].name[15]   = 0;
  sprites[sprSlot].frameCount = fc;
  sprites[sprSlot].w          = w;
  sprites[sprSlot].h          = h;
  sprites[sprSlot].ready      = false;
  sprFrameIdx = 0;
  sprBufLen = sprTotal = sprRecv = 0;
  Serial.printf("SPR begin: %s %d frames %dx%d slot=%d\n", name, fc, w, h, sprSlot);
}

void handleSprFrameDone(const char *s) {
  int fi = jsonInt(s, "\"frame\":");
  if (sprSlot < 0 || fi >= sprites[sprSlot].frameCount) return;
  int fs = sprites[sprSlot].w * sprites[sprSlot].h / 2 + 32;
  if (sprBufLen < fs) { Serial.println("SPR frame: buffer too small"); return; }
  uint8_t *mem = (uint8_t *)malloc(fs);
  if (!mem) { Serial.println("SPR: malloc failed"); return; }
  memcpy(mem, sprBuffer, fs);
  sprites[sprSlot].frames[fi] = mem;
  sprBufLen = sprTotal = sprRecv = 0;
  if (fi == sprites[sprSlot].frameCount - 1) {
    sprites[sprSlot].ready = true;
    Serial.printf("SPR ready: %s\n", sprites[sprSlot].name);
  }
}

void handleSetIdle(const char *s) {
  jsonStr(s, "\"name\"", idleSpriteName, sizeof(idleSpriteName));
}
