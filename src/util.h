#pragma once
#include <stdint.h>

// JSON tiny parsers (no validation — assume well-formed input from daemon)
void jsonStr(const char *src, const char *key, char *out, int maxLen);
int  jsonInt(const char *src, const char *key);

// Audio + haptic
void vib (int ms, int str = 128);
void doVib (uint8_t pat);
void doBeep(uint8_t pat);

// Token → enum parsers
uint16_t parseColor(const char *name);
uint8_t  parseVib  (const char *s);
uint8_t  parseBeep (const char *s);

// Formatting + helpers
void     formatAge(uint32_t ts, char *out, int n);
uint16_t batColor (int pct);
