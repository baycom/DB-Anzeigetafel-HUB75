#include "util.h"

void foldAscii(const char* in, char* out, size_t outSize) {
  if (!outSize) return;
  size_t o = 0;
  const uint8_t* p = (const uint8_t*)in;
  auto put = [&](const char* s) {
    while (*s && o + 1 < outSize) out[o++] = *s++;
  };
  while (p && *p && o + 1 < outSize) {
    uint8_t c = *p;
    if (c < 0x80) {
      out[o++] = (char)c;
      p++;
    } else if (c == 0xC3 && p[1]) {
      switch (p[1]) {
        case 0xA4: put("ae"); break;
        case 0xB6: put("oe"); break;
        case 0xBC: put("ue"); break;
        case 0x84: put("Ae"); break;
        case 0x96: put("Oe"); break;
        case 0x9C: put("Ue"); break;
        case 0x9F: put("ss"); break;
        case 0xA9: case 0xA8: put("e"); break;
        case 0xA0: case 0xA1: put("a"); break;
        default: break;
      }
      p += 2;
    } else {
      // Rest einer unbekannten UTF-8-Sequenz ueberspringen
      p++;
      while (*p && (*p & 0xC0) == 0x80) p++;
    }
  }
  out[o] = 0;
}

int parseHHMM(const char* s) {
  if (!s) return -1;
  int h, m;
  if (sscanf(s, "%d:%d", &h, &m) != 2) return -1;
  if (h < 0 || h > 24 || m < 0 || m > 59) return -1;
  if (h == 24) return 0;
  return h * 60 + m;
}

void fmtHHMM(uint16_t minutes, char* out) {
  snprintf(out, 6, "%02u:%02u", (unsigned)((minutes / 60) % 24), (unsigned)(minutes % 60));
}

bool inWindow(uint16_t from, uint16_t to, uint16_t m) {
  if (from == to) return true;
  if (from < to) return m >= from && m < to;
  return m >= from || m < to;  // ueber Mitternacht
}
