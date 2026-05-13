#include "util.h"
#include "state.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <Arduino.h>

void jsonStr(const char *src, const char *key, char *out, int maxLen) {
  const char *p = strstr(src, key);
  if (!p) { out[0] = 0; return; }
  p += strlen(key);
  while (*p && *p != '"') p++;
  if (!*p) { out[0] = 0; return; }
  p++;
  int i = 0;
  while (*p && *p != '"' && i < maxLen - 1) out[i++] = *p++;
  out[i] = 0;
}

int jsonInt(const char *src, const char *key) {
  const char *p = strstr(src, key);
  if (!p) return 0;
  p += strlen(key);
  while (*p && (*p == ' ' || *p == ':')) p++;
  return atoi(p);
}

void vib(int ms, int str) {
  if (!cfg.vibEnabled) return;
  StickCP2.Power.setVibration(str);
  delay(ms);
  StickCP2.Power.setVibration(0);
}

void doVib(uint8_t pat) {
  if (!cfg.vibEnabled) return;
  switch (pat) {
    case 1: vib(300); break;
    case 2: vib(120); delay(80); vib(120); break;
    case 3: vib(600, 200); break;
    case 4: vib(80); delay(60); vib(80); delay(60); vib(80); break;
  }
}

void doBeep(uint8_t pat) {
  switch (pat) {
    case 1: StickCP2.Speaker.tone(1000, 200); break;
    case 2: StickCP2.Speaker.tone(1000, 150); delay(200); StickCP2.Speaker.tone(1000, 150); break;
    case 3:
      StickCP2.Speaker.tone(880, 120); delay(140);
      StickCP2.Speaker.tone(1100, 120); delay(140);
      StickCP2.Speaker.tone(1320, 200); break;
    case 4:
      StickCP2.Speaker.tone(1320, 120); delay(140);
      StickCP2.Speaker.tone(1100, 120); delay(140);
      StickCP2.Speaker.tone(880, 200); break;
    case 5:
      for (int i = 0; i < 3; i++) { StickCP2.Speaker.tone(1500, 100); delay(180); }
      break;
  }
}

uint16_t parseColor(const char *name) {
  if (strcmp(name, "red")    == 0) return C_RED;
  if (strcmp(name, "green")  == 0) return C_GREEN;
  if (strcmp(name, "cyan")   == 0) return C_CYAN;
  if (strcmp(name, "yellow") == 0) return C_YELLOW;
  if (strcmp(name, "orange") == 0) return C_ORANGE;
  if (strcmp(name, "purple") == 0) return C_PURPLE;
  if (strcmp(name, "white")  == 0) return C_WHITE;
  return C_HEADER;
}

uint8_t parseVib(const char *s) {
  if (strcmp(s, "single") == 0) return 1;
  if (strcmp(s, "double") == 0) return 2;
  if (strcmp(s, "long")   == 0) return 3;
  if (strcmp(s, "triple") == 0) return 4;
  return 0;
}

uint8_t parseBeep(const char *s) {
  if (strcmp(s, "single")  == 0) return 1;
  if (strcmp(s, "double")  == 0) return 2;
  if (strcmp(s, "success") == 0) return 3;
  if (strcmp(s, "error")   == 0) return 4;
  if (strcmp(s, "alert")   == 0) return 5;
  return 0;
}

void formatAge(uint32_t ts, char *out, int n) {
  uint32_t s = (millis() - ts) / 1000;
  if (s < 60)        snprintf(out, n, "%lus",  (unsigned long)s);
  else if (s < 3600) snprintf(out, n, "%lum",  (unsigned long)(s / 60));
  else               snprintf(out, n, "%luh",  (unsigned long)(s / 3600));
}

uint16_t batColor(int p) {
  if (p > 60) return C_GREEN;
  if (p > 30) return C_YELLOW;
  if (p > 15) return C_ORANGE;
  return C_RED;
}
