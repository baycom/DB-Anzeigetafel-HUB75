#include "display.h"
#include <WiFi.h>
#include <ESP32-HUB75-MatrixPanel-I2S-DMA.h>
#include <Fonts/TomThumb.h>
#include "board.h"
#include "config.h"
#include "net.h"
#include "util.h"

// ---------------------------------------------------------------------------
// Grundgeruest: Es wird in ein 16-Bit-Offscreen-Canvas gezeichnet, danach werden
// nur die geaenderten Pixel ins DMA-Panel uebertragen (flackerfrei ohne Double-Buffer).
// ---------------------------------------------------------------------------

static MatrixPanel_I2S_DMA* dma = nullptr;
static GFXcanvas16* canvas = nullptr;
static int W = 64, H = 64;
static uint8_t curBrightness = 0xFF;

static constexpr uint16_t rgb(uint8_t r, uint8_t g, uint8_t b) {
  return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
}
static constexpr uint16_t C_BLACK = rgb(0, 0, 0);
static constexpr uint16_t C_WHITE = rgb(230, 230, 230);
static constexpr uint16_t C_GRAY = rgb(110, 110, 110);
static constexpr uint16_t C_DIM = rgb(50, 50, 50);
static constexpr uint16_t C_YELLOW = rgb(255, 190, 0);
static constexpr uint16_t C_ORANGE = rgb(255, 120, 0);
static constexpr uint16_t C_RED = rgb(255, 30, 0);
static constexpr uint16_t C_GREEN = rgb(0, 200, 60);
static constexpr uint16_t C_HEADER = rgb(0, 0, 110);

// Badge-Farben je Verkehrsmittel-Klasse: Hintergrund, Text
static const uint16_t BADGE_BG[] = {rgb(220, 220, 220), rgb(190, 0, 0), rgb(0, 140, 60), rgb(0, 70, 210),
                                    rgb(210, 90, 0),    rgb(130, 0, 160), rgb(0, 130, 170), rgb(170, 150, 0),
                                    rgb(80, 80, 80)};
static const uint16_t BADGE_FG[] = {rgb(200, 0, 0), C_WHITE, C_WHITE, C_WHITE, C_WHITE,
                                    C_WHITE,        C_WHITE, C_BLACK, C_WHITE};

// Adafruit-GFX-Adapter mit Clipping-Rechteck (fuer Laufschrift in Spalten)
class ClipGFX : public Adafruit_GFX {
 public:
  ClipGFX(int16_t w, int16_t h, GFXcanvas16* t) : Adafruit_GFX(w, h), tgt(t) { setClip(0, 0, w, h); }
  void setClip(int16_t x, int16_t y, int16_t w, int16_t h) {
    x0 = max<int16_t>(0, x);
    y0 = max<int16_t>(0, y);
    x1 = min<int16_t>(width(), x + w);
    y1 = min<int16_t>(height(), y + h);
  }
  void drawPixel(int16_t x, int16_t y, uint16_t c) override {
    if (x < x0 || x >= x1 || y < y0 || y >= y1) return;
    tgt->drawPixel(x, y, c);
  }

 private:
  GFXcanvas16* tgt;
  int16_t x0, y0, x1, y1;
};
static ClipGFX* gfx = nullptr;

// Schrift-Metrik (beide Fonts sind monospaced)
struct Fm {
  const GFXfont* font;
  uint8_t cw;        // Zeichenbreite
  uint8_t rowH;      // Zeilenhoehe
  uint8_t baseline;  // Offset vom Zeilenanfang zum Cursor-y
  uint8_t badgeMax;  // max. Zeichen im Linien-Badge
};
static const Fm FM_SMALL = {&TomThumb, 4, 7, 6, 4};
static const Fm FM_NORMAL = {nullptr, 6, 8, 0, 3};
static const Fm* fm = &FM_SMALL;

// Textbreite in Pixeln (TomThumb ist nicht streng monospaced -> Glyphentabelle nutzen)
static int textW(const char* s) {
  if (!fm->font) return strlen(s) * fm->cw;
  int w = 0;
  for (; *s; s++) {
    uint8_t c = *s;
    if (c >= fm->font->first && c <= fm->font->last) w += fm->font->glyph[c - fm->font->first].xAdvance;
  }
  return w;
}

static void drawText(int x, int y, int w, int h, const char* s, uint16_t col, int scrollPx = 0) {
  gfx->setClip(x, y, w, h);
  gfx->setTextColor(col);
  gfx->setCursor(x - scrollPx, y + fm->baseline);
  gfx->print(s);
}

// Laufschrift: Pause - nach links scrollen - Pause - zurueckspringen
static int scrollOffset(int tw, int fw, uint32_t t) {
  if (tw <= fw || !cfg.disp.scroll) return 0;
  const uint32_t pause = 1500, speed = 14;  // px/s
  int over = tw - fw;
  uint32_t travel = over * 1000 / speed;
  uint32_t ph = t % (2 * pause + travel);
  if (ph < pause) return 0;
  if (ph < pause + travel) return (ph - pause) * speed / 1000;
  return over;
}

static void drawCentered(int y, const char* s, uint16_t col, uint32_t t) {
  int tw = textW(s);
  if (tw <= W) drawText((W - tw) / 2, y, tw, fm->rowH, s, col);
  else drawText(0, y, W, fm->rowH, s, col, scrollOffset(tw, W, t));
}

static void present() {
  if (!dma) return;
  // Komplettes Bild uebertragen (64x64 = 4096 Pixel, ~3 ms). Gleiche Werte erneut zu
  // schreiben flackert nicht; spart den 8-kB-Vergleichspuffer fuer TLS.
  uint16_t* buf = canvas->getBuffer();
  for (int y = 0, i = 0; y < H; y++)
    for (int x = 0; x < W; x++, i++) dma->drawPixel(x, y, buf[i]);
  if (cfg.panel.doubleBuffer) dma->flipDMABuffer();
}

// ---------------------------------------------------------------------------
// Meldungen
// ---------------------------------------------------------------------------

static char msgTitle[32], msgL1[40], msgL2[40];
static bool msgActive = false;
static uint32_t msgUntil = 0;

static void drawStatus(const char* title, const char* l1, const char* l2, const char* l3, uint32_t t);
static void renderNow();

void displayMessage(const char* title, const char* line1, const char* line2, uint32_t ms) {
  if (!title) {
    msgActive = false;
    return;
  }
  strlcpy(msgTitle, title, sizeof(msgTitle));
  strlcpy(msgL1, line1 ? line1 : "", sizeof(msgL1));
  strlcpy(msgL2, line2 ? line2 : "", sizeof(msgL2));
  msgActive = true;
  msgUntil = ms ? millis() + ms : 0;
  // gezeichnet wird im Display-Task (spaetestens nach 40 ms)
}

static void drawStatus(const char* title, const char* l1, const char* l2, const char* l3, uint32_t t) {
  const char* lines[] = {l1, l2, l3};
  int n = 1;
  for (auto l : lines)
    if (l && l[0]) n++;
  int rh = fm->rowH + 2;
  int y = (H - n * rh) / 2;
  drawCentered(y, title, C_YELLOW, t);
  y += rh;
  for (auto l : lines)
    if (l && l[0]) {
      drawCentered(y, l, C_WHITE, t);
      y += rh;
    }
}

// ---------------------------------------------------------------------------
// Uhr-Bildschirm (kein Profil aktiv)
// ---------------------------------------------------------------------------

static void drawClock(const struct tm& lt, uint32_t t) {
  static const char* WD[] = {"So", "Mo", "Di", "Mi", "Do", "Fr", "Sa"};
  char buf[24];
  snprintf(buf, sizeof(buf), "%02d:%02d", lt.tm_hour, lt.tm_min);
  gfx->setClip(0, 0, W, H);
  gfx->setFont(nullptr);
  gfx->setTextSize(W >= 64 ? 2 : 1);
  int s = W >= 64 ? 2 : 1;
  int tw = 5 * 6 * s - s;  // klassischer Font: 6 px je Zeichen
  gfx->setTextColor(C_WHITE);
  gfx->setCursor((W - tw) / 2, H / 2 - 7 * s);
  gfx->print(buf);
  gfx->setTextSize(1);
  gfx->setFont(fm->font);

  snprintf(buf, sizeof(buf), "%s %02d.%02d.%04d", WD[lt.tm_wday], lt.tm_mday, lt.tm_mon + 1, lt.tm_year + 1900);
  drawCentered(H / 2 + 4, buf, C_GRAY, t);

  // Sekunden-Fortschrittsbalken
  canvas->drawFastHLine(0, H / 2 - 7 * s - 4, (W * lt.tm_sec) / 59, C_DIM);

  if (cfg.profileCount == 0) {
    String ip = WiFi.localIP().toString();
    drawCentered(H - fm->rowH - 1, ip.c_str(), C_GREEN, t);
  } else {
    drawCentered(H - fm->rowH - 1, "kein Plan aktiv", C_DIM, t);
  }
}

// ---------------------------------------------------------------------------
// Abfahrts-/Ankunftstafel
// ---------------------------------------------------------------------------

static Board snap;  // Kopie der Daten fuer das Zeichnen (kurze Sperrzeit)

static uint16_t delayColor(const Entry& e) {
  if (e.cancelled) return C_RED;
  if (e.delay >= 5) return C_RED;
  if (e.delay >= 1) return C_YELLOW;
  return C_WHITE;
}

static void fmtTime(const Entry& e, time_t nowL, char* out, size_t n) {
  if (cfg.disp.timeMode == 1) {
    long m = (long)(e.realTime - nowL + 59) / 60;
    if (m < 0) m = 0;
    if (m < 60) {
      snprintf(out, n, "%ld'", m);
      return;
    }
  }
  struct tm tt;
  time_t t = e.time;
  gmtime_r(&t, &tt);  // naiver Timestamp = lokale Wandzeit
  snprintf(out, n, "%02d:%02d", tt.tm_hour, tt.tm_min);
}

static void badgeText(const Entry& e, char* out) {
  strlcpy(out, e.label, fm->badgeMax + 1);
}

static void drawBadge(int x, int y, int w, const Entry& e) {
  char lbl[8];
  badgeText(e, lbl);
  uint8_t pc = e.pclass < sizeof(BADGE_BG) / 2 ? e.pclass : PC_OTHER;
  canvas->fillRect(x, y, w, fm->rowH - 1, BADGE_BG[pc]);
  int tw = textW(lbl);
  drawText(x + (w - tw + 1) / 2, y, w, fm->rowH, lbl, BADGE_FG[pc]);
}

static void drawHeader(const Profile& p, bool err, const struct tm& lt, uint32_t t) {
  int rh = fm->rowH;
  canvas->fillRect(0, 0, W, rh, C_HEADER);
  char clk[6];
  snprintf(clk, sizeof(clk), "%02d:%02d", lt.tm_hour, lt.tm_min);
  int cwid = textW(clk);
  int cx = W - cwid;
  // Uhrzeit rot = letzter Abruf fehlgeschlagen
  drawText(cx, 0, cwid, rh, clk, err ? C_RED : C_WHITE);

  char raw[64], lbl[64];
  if (p.label[0]) strlcpy(raw, p.label, sizeof(raw));
  else snprintf(raw, sizeof(raw), "%s %s", p.arrivals ? "An" : "Ab", p.stationName);
  foldAscii(raw, lbl, sizeof(lbl));
  int lw = cx - 2;
  drawText(0, 0, lw, rh, lbl, C_WHITE, scrollOffset(textW(lbl), lw, t));
}

static void drawBoard(const Profile& p, const struct tm& lt, uint32_t t) {
  time_t nowL = localNow();
  int rh = fm->rowH, cw = fm->cw;

  // sichtbare Fahrten filtern
  const Entry* vis[MAX_ENTRIES];
  int n = 0;
  for (int i = 0; i < snap.count; i++) {
    const Entry& e = snap.e[i];
    if (cfg.disp.hideCancelled && e.cancelled) continue;
    time_t ref = e.cancelled ? e.time : e.realTime;
    if (ref + 59 < nowL) continue;                                    // schon weg
    if (!p.arrivals && p.leadMin && ref < nowL + p.leadMin * 60) continue;  // nicht mehr erreichbar
    vis[n++] = &e;
  }

  drawHeader(p, snap.err != DBERR_NONE, lt, t);

  if (!snap.valid) {
    drawCentered(H / 2 - rh, snap.err ? "Fehler:" : "Lade ...", snap.err ? C_RED : C_GRAY, t);
    if (snap.err) drawCentered(H / 2, dbErrorText(snap.err), C_RED, t);
    return;
  }
  if (n == 0) {
    drawCentered(H / 2 - rh / 2, "Keine Fahrten", C_GRAY, t);
    return;
  }

  bool detailed = cfg.disp.layout == 1;
  int perEntry = detailed ? 2 : 1;
  int rows = (H - rh) / (rh * perEntry);
  if (n > rows) n = rows;

  // Spaltenbreiten an die tatsaechlichen Inhalte anpassen
  char ts[MAX_ENTRIES][8];
  int timeW = 0, badgeW = 2 * cw;
  for (int i = 0; i < n; i++) {
    fmtTime(*vis[i], nowL, ts[i], sizeof(ts[i]));
    timeW = max(timeW, textW(ts[i]));
    char lbl[8];
    badgeText(*vis[i], lbl);
    badgeW = max(badgeW, textW(lbl));
  }
  badgeW += 1;

  for (int i = 0; i < n; i++) {
    const Entry& e = *vis[i];
    int y = rh + i * rh * perEntry;
    uint16_t tc = delayColor(e);

    if (!detailed) {
      // [Zeit] [Badge] [Ziel .....]
      int tx = timeW - textW(ts[i]);
      drawText(tx, y, timeW, rh, ts[i], tc);
      if (e.cancelled) canvas->drawFastHLine(tx, y + rh / 2, textW(ts[i]) - 1, C_RED);
      int bx = timeW + 1;
      drawBadge(bx, y, badgeW, e);
      int dx = bx + badgeW + 1, dw = W - dx;
      drawText(dx, y, dw, rh, e.target, e.cancelled ? C_RED : C_WHITE, scrollOffset(textW(e.target), dw, t));
    } else {
      // Zeile 1: [Zeit] [+Verspaetung | faellt aus]      [Gleis]
      drawText(0, y, timeW, rh, ts[i], e.cancelled ? C_RED : C_WHITE);
      char pl[12] = "";
      const char* plat = e.newPlatform[0] ? e.newPlatform : e.platform;
      bool changed = e.newPlatform[0] && strcmp(e.newPlatform, e.platform) != 0;
      if (plat[0]) snprintf(pl, sizeof(pl), "Gl%s", plat);
      int pw = textW(pl);
      if (pw) drawText(W - pw, y, pw, rh, pl, changed ? C_ORANGE : C_GRAY);
      int ix = timeW + cw, iw = W - pw - ix - 1;
      if (e.cancelled) {
        drawText(ix, y, iw, rh, "faellt aus", C_RED, scrollOffset(textW("faellt aus"), iw, t));
      } else if (e.delay > 0) {
        char d[8];
        snprintf(d, sizeof(d), "+%d", e.delay);
        drawText(ix, y, iw, rh, d, tc);
      } else if (cfg.disp.timeMode == 0) {
        drawText(ix, y, iw, rh, "+0", C_GREEN);
      }
      // Zeile 2: [Badge] [Ziel .....]
      int y2 = y + rh;
      drawBadge(0, y2, badgeW, e);
      int dx = badgeW + 1, dw = W - dx;
      drawText(dx, y2, dw, rh, e.target, e.cancelled ? C_RED : C_WHITE, scrollOffset(textW(e.target), dw, t));
    }
  }
}

static void drawPageDots(int pages, int cur) {
  int used = (fm->rowH * ((H - fm->rowH) / fm->rowH)) + fm->rowH;
  if (pages < 2 || used >= H) return;  // nur wenn unten eine Pixelzeile frei ist
  int x = (W - (pages * 3 - 1)) / 2;
  for (int i = 0; i < pages; i++) canvas->drawFastHLine(x + i * 3, H - 1, 2, i == cur ? C_YELLOW : C_DIM);
}

// ---------------------------------------------------------------------------
// Hauptschleife
// ---------------------------------------------------------------------------

static int pageIdx = 0;
static int lastProfile = -1;
static uint32_t pageStart = 0;

// ---------- Lichtsensor ----------

static float ldrAvg = -1;      // gleitender Mittelwert des ADC-Rohwerts
static int8_t ldrPinActive = -1;

static void sampleLdr() {
  const DisplayConfig& d = cfg.disp;
  if (!d.ldrEnabled) {
    ldrAvg = -1;
    return;
  }
  if (ldrPinActive != d.ldrPin) {
    ldrPinActive = d.ldrPin;
    analogRead(d.ldrPin);                          // Pin als ADC-Kanal einrichten
    analogSetPinAttenuation(d.ldrPin, ADC_11db);  // Messbereich ~0..3,1 V
    ldrAvg = -1;
  }
  int raw = analogRead(d.ldrPin);
  // Exponentielle Glaettung (~2 s bei 10 Messungen/s), damit die Helligkeit nicht flackert
  ldrAvg = ldrAvg < 0 ? raw : ldrAvg + (raw - ldrAvg) * 0.05f;
}

// Helligkeit aus Lichtsensor: linear zwischen ldrMinBright (dunkel) und brightness (hell)
static uint8_t ldrBrightness() {
  const DisplayConfig& d = cfg.disp;
  float lo = d.ldrDark, hi = d.ldrBright, v = ldrAvg;
  if (d.ldrInvert) {
    lo = 4095 - lo;
    hi = 4095 - hi;
    v = 4095 - v;
  }
  if (hi <= lo) hi = lo + 1;
  float f = constrain((v - lo) / (hi - lo), 0.0f, 1.0f);
  return (uint8_t)(d.ldrMinBright + f * ((int)d.brightness - (int)d.ldrMinBright) + 0.5f);
}

static void updateBrightness() {
  const DisplayConfig& d = cfg.disp;
  uint8_t b = d.brightness;
  if (d.ldrEnabled && ldrAvg >= 0) b = ldrBrightness();
  // Nachtmodus hat Vorrang (z.B. Panel nachts ganz aus)
  struct tm lt;
  if (d.nightEnabled && localTm(lt) && inWindow(d.nightFrom, d.nightTo, lt.tm_hour * 60 + lt.tm_min))
    b = d.nightBrightness;
  // kleine Hysterese gegen Pendeln zwischen zwei Stufen
  if (dma && (curBrightness == 0xFF || abs((int)b - (int)curBrightness) >= 2 || b == 0 || b == d.brightness)) {
    if (b != curBrightness) {
      dma->setBrightness8(b);
      curBrightness = b;
    }
  }
}

int displayLdrRaw() { return ldrAvg < 0 ? -1 : (int)(ldrAvg + 0.5f); }
uint8_t displayBrightness() { return curBrightness == 0xFF ? cfg.disp.brightness : curBrightness; }

static void render() {
  uint32_t now = millis();
  fm = cfg.disp.font == 1 ? &FM_NORMAL : &FM_SMALL;
  canvas->fillScreen(C_BLACK);
  gfx->setFont(fm->font);
  gfx->setTextSize(1);
  gfx->setTextWrap(false);

  if (msgActive && msgUntil && (int32_t)(now - msgUntil) > 0) msgActive = false;
  if (msgActive) {
    drawStatus(msgTitle, msgL1, msgL2, nullptr, now);
    return;
  }

  if (!netStaConnected()) {
    if (netApActive()) {
      char pw[48];
      snprintf(pw, sizeof(pw), "PW %s", strlen(cfg.apPass) >= 8 ? cfg.apPass : "(offen)");
      drawStatus("WLAN-Setup", netApSsid(), pw, WiFi.softAPIP().toString().c_str(), now);
    } else {
      drawStatus("WLAN", "verbinde ...", cfg.wifiSsid, nullptr, now);
    }
    return;
  }

  struct tm lt;
  if (!localTm(lt)) {
    drawStatus("Zeit", "NTP-Sync ...", WiFi.localIP().toString().c_str(), nullptr, now);
    return;
  }

  // aktive Profile ermitteln, Seitenwechsel
  int active[MAX_PROFILES];
  int na = 0;
  xSemaphoreTake(dataMutex, portMAX_DELAY);
  for (int i = 0; i < cfg.profileCount; i++)
    if (profileActive(cfg.profiles[i], lt)) active[na++] = i;

  if (na == 0) {
    xSemaphoreGive(dataMutex);
    lastProfile = -1;
    drawClock(lt, now);
    return;
  }
  if (na > 1 && now - pageStart > cfg.disp.pageSec * 1000UL) {
    pageIdx++;
    pageStart = now;
  }
  pageIdx %= na;
  int pi = active[pageIdx];
  if (pi != lastProfile) {
    lastProfile = pi;
    pageStart = now;
  }
  Profile p = cfg.profiles[pi];
  memcpy(&snap, &boards[pi], sizeof(Board));
  xSemaphoreGive(dataMutex);

  drawBoard(p, lt, now - pageStart);
  drawPageDots(na, pageIdx);
}

static void renderNow() {
  if (!canvas) return;
  render();
  present();
}

void displayInit() {
  const PanelConfig& p = cfg.panel;
  HUB75_I2S_CFG::i2s_pins pins = {p.pins[P_R1], p.pins[P_G1], p.pins[P_B1], p.pins[P_R2], p.pins[P_G2],
                                  p.pins[P_B2], p.pins[P_A],  p.pins[P_B],  p.pins[P_C],  p.pins[P_D],
                                  p.pins[P_E],  p.pins[P_LAT], p.pins[P_OE], p.pins[P_CLK]};
  HUB75_I2S_CFG mx(p.width, p.height, p.chain, pins);

  static const HUB75_I2S_CFG::shift_driver drivers[] = {HUB75_I2S_CFG::SHIFTREG, HUB75_I2S_CFG::FM6124,
                                                        HUB75_I2S_CFG::FM6126A,  HUB75_I2S_CFG::ICN2038S,
                                                        HUB75_I2S_CFG::MBI5124,  HUB75_I2S_CFG::DP3246};
  static const HUB75_I2S_CFG::line_driver lines[] = {HUB75_I2S_CFG::TYPE138, HUB75_I2S_CFG::TYPE595,
                                                      HUB75_I2S_CFG::TYPE_DIRECT, HUB75_I2S_CFG::SM5266P};
  mx.driver = drivers[min<uint8_t>(p.driver, 5)];
  mx.line_decoder = lines[min<uint8_t>(p.lineDriver, 3)];
  switch (p.i2sMHz) {
    case 10: mx.i2sspeed = HUB75_I2S_CFG::HZ_10M; break;
    case 15: mx.i2sspeed = HUB75_I2S_CFG::HZ_15M; break;
    case 20: mx.i2sspeed = HUB75_I2S_CFG::HZ_20M; break;
    default: mx.i2sspeed = HUB75_I2S_CFG::HZ_8M; break;
  }
  mx.clkphase = p.clkPhase;
  mx.latch_blanking = p.latchBlanking;
  mx.double_buff = p.doubleBuffer;
  mx.min_refresh_rate = p.minRefresh;

  W = p.width * p.chain;
  H = p.height;
  canvas = new GFXcanvas16(W, H);
  gfx = new ClipGFX(W, H, canvas);

  dma = new MatrixPanel_I2S_DMA(mx);
  if (!dma->begin()) {
    Serial.println(F("[disp] HUB75-Initialisierung fehlgeschlagen (Speicher/Pins pruefen)"));
    delete dma;
    dma = nullptr;
    return;
  }
  dma->clearScreen();
  updateBrightness();

  // Testbild (3 s): oben Farbbalken R/G/B/Weiss, unten Weiss/B/G/R, weisser Rahmen, Ecke oben links gelb
  const uint16_t bars[4] = {rgb(255, 0, 0), rgb(0, 255, 0), rgb(0, 0, 255), rgb(255, 255, 255)};
  for (int i = 0; i < 4; i++) {
    canvas->fillRect(i * W / 4, 0, W / 4, H / 2, bars[i]);
    canvas->fillRect(i * W / 4, H / 2, W / 4, H / 2, bars[3 - i]);
  }
  canvas->drawRect(0, 0, W, H, rgb(255, 255, 255));
  canvas->fillRect(0, 0, 4, 4, rgb(255, 255, 0));
  present();
  delay(3000);
  Serial.printf("[disp] Panel %dx%d bereit, Heap %u\n", W, H, ESP.getFreeHeap());
}

// Eigener Task: Uhr und Laufschrift laufen weiter, auch wenn Webserver oder DB-Abruf blockieren
static void displayTask(void*) {
  for (;;) {
    displayLoop();
    vTaskDelay(pdMS_TO_TICKS(5));
  }
}

void displayStart() {
  if (!canvas) return;
  xTaskCreatePinnedToCore(displayTask, "display", 6144, nullptr, 2, nullptr, 1);
}

void displayLoop() {
  static uint32_t lastFrame = 0, lastBright = 0;
  uint32_t now = millis();
  if (now - lastBright >= 100) {  // 10x pro Sekunde messen und nachfuehren
    lastBright = now;
    sampleLdr();
    updateBrightness();
  }
  if (now - lastFrame < 40) return;  // ~25 fps fuer weiche Laufschrift
  lastFrame = now;
  renderNow();
}
