#include "config.h"
#include <LittleFS.h>
#include "util.h"

Config cfg;

const char* const PIN_NAMES[P_COUNT] = {"r1", "g1", "b1", "r2", "g2", "b2", "a",
                                        "b",  "c",  "d",  "e",  "lat", "oe", "clk"};

static const char* CONFIG_FILE = "/config.json";

// ---------- Hilfsfunktionen zum Uebernehmen einzelner Felder ----------

static void getStr(JsonVariantConst v, char* dst, size_t n) {
  if (v.is<const char*>()) strlcpy(dst, v.as<const char*>(), n);
}

template <typename T>
static void getNum(JsonVariantConst v, T& dst, long lo, long hi) {
  if (v.is<long>() || v.is<double>()) dst = (T)constrain(v.as<long>(), lo, hi);
}

static void getBool(JsonVariantConst v, bool& dst) {
  if (v.is<bool>()) dst = v.as<bool>();
}

static void getTime(JsonVariantConst v, uint16_t& dst) {
  if (v.is<const char*>()) {
    int m = parseHHMM(v.as<const char*>());
    if (m >= 0) dst = m;
  }
}

static String hhmm(uint16_t m) {
  char b[6];
  fmtHHMM(m, b);
  return String(b);
}

// ---------- Serialisierung ----------

void configToJson(const Config& c, JsonDocument& doc, bool secrets) {
  JsonObject wifi = doc["wifi"].to<JsonObject>();
  wifi["ssid"] = c.wifiSsid;
  wifi["hostname"] = c.hostname;
  wifi["ipv6"] = c.ipv6;
  if (secrets) {
    wifi["pass"] = c.wifiPass;
    wifi["apPass"] = c.apPass;
  } else {
    wifi["passSet"] = c.wifiPass[0] != 0;
    wifi["apPassSet"] = c.apPass[0] != 0;
  }

  JsonObject web = doc["web"].to<JsonObject>();
  if (secrets) web["pass"] = c.webPass;
  else web["passSet"] = c.webPass[0] != 0;

  JsonObject t = doc["time"].to<JsonObject>();
  t["ntp1"] = c.ntp1;
  t["ntp2"] = c.ntp2;
  t["tz"] = c.tz;

  const PanelConfig& p = c.panel;
  JsonObject pn = doc["panel"].to<JsonObject>();
  pn["width"] = p.width;
  pn["height"] = p.height;
  pn["chain"] = p.chain;
  pn["driver"] = p.driver;
  pn["lineDriver"] = p.lineDriver;
  pn["i2sMHz"] = p.i2sMHz;
  pn["clkPhase"] = p.clkPhase;
  pn["latchBlanking"] = p.latchBlanking;
  pn["doubleBuffer"] = p.doubleBuffer;
  pn["minRefresh"] = p.minRefresh;
  JsonObject pins = pn["pins"].to<JsonObject>();
  for (int i = 0; i < P_COUNT; i++) pins[PIN_NAMES[i]] = p.pins[i];

  const DisplayConfig& d = c.disp;
  JsonObject ds = doc["display"].to<JsonObject>();
  ds["layout"] = d.layout;
  ds["font"] = d.font;
  ds["timeMode"] = d.timeMode;
  ds["scroll"] = d.scroll;
  ds["hideCancelled"] = d.hideCancelled;
  ds["refreshSec"] = d.refreshSec;
  ds["pageSec"] = d.pageSec;
  ds["brightness"] = d.brightness;
  ds["nightEnabled"] = d.nightEnabled;
  ds["nightBrightness"] = d.nightBrightness;
  ds["nightFrom"] = hhmm(d.nightFrom);
  ds["nightTo"] = hhmm(d.nightTo);
  ds["ldrEnabled"] = d.ldrEnabled;
  ds["ldrPin"] = d.ldrPin;
  ds["ldrInvert"] = d.ldrInvert;
  ds["ldrDark"] = d.ldrDark;
  ds["ldrBright"] = d.ldrBright;
  ds["ldrMinBright"] = d.ldrMinBright;

  JsonArray pr = doc["profiles"].to<JsonArray>();
  for (uint8_t i = 0; i < c.profileCount; i++) {
    const Profile& f = c.profiles[i];
    JsonObject o = pr.add<JsonObject>();
    o["enabled"] = f.enabled;
    o["label"] = f.label;
    o["stationName"] = f.stationName;
    o["stationId"] = f.stationId;
    o["type"] = f.arrivals ? "arr" : "dep";
    o["days"] = f.days;
    o["from"] = hhmm(f.fromMin);
    o["to"] = hhmm(f.toMin);
    o["products"] = f.products;
    o["leadMin"] = f.leadMin;
    o["maxCount"] = f.maxCount;
    o["hours"] = f.hours;
    o["dirs"] = f.dirs;
  }
}

void configFromJson(JsonVariantConst src, Config& c) {
  JsonVariantConst wifi = src["wifi"];
  getStr(wifi["ssid"], c.wifiSsid, sizeof(c.wifiSsid));
  getStr(wifi["pass"], c.wifiPass, sizeof(c.wifiPass));
  getStr(wifi["hostname"], c.hostname, sizeof(c.hostname));
  getStr(wifi["apPass"], c.apPass, sizeof(c.apPass));
  getBool(wifi["ipv6"], c.ipv6);
  if (!c.hostname[0]) strlcpy(c.hostname, "db-tafel", sizeof(c.hostname));

  getStr(src["web"]["pass"], c.webPass, sizeof(c.webPass));

  JsonVariantConst t = src["time"];
  getStr(t["ntp1"], c.ntp1, sizeof(c.ntp1));
  getStr(t["ntp2"], c.ntp2, sizeof(c.ntp2));
  getStr(t["tz"], c.tz, sizeof(c.tz));
  if (!c.ntp1[0]) strlcpy(c.ntp1, "pool.ntp.org", sizeof(c.ntp1));
  if (!c.tz[0]) strlcpy(c.tz, "CET-1CEST,M3.5.0,M10.5.0/3", sizeof(c.tz));

  JsonVariantConst pn = src["panel"];
  PanelConfig& p = c.panel;
  getNum(pn["width"], p.width, 16, 256);
  getNum(pn["height"], p.height, 16, 128);
  getNum(pn["chain"], p.chain, 1, 8);
  getNum(pn["driver"], p.driver, 0, 5);
  getNum(pn["lineDriver"], p.lineDriver, 0, 3);
  getNum(pn["i2sMHz"], p.i2sMHz, 8, 20);
  if (p.i2sMHz != 8 && p.i2sMHz != 10 && p.i2sMHz != 15 && p.i2sMHz != 20) p.i2sMHz = 8;
  getBool(pn["clkPhase"], p.clkPhase);
  getNum(pn["latchBlanking"], p.latchBlanking, 1, 4);
  getBool(pn["doubleBuffer"], p.doubleBuffer);
  getNum(pn["minRefresh"], p.minRefresh, 30, 240);
  for (int i = 0; i < P_COUNT; i++) getNum(pn["pins"][PIN_NAMES[i]], p.pins[i], -1, 39);

  JsonVariantConst ds = src["display"];
  DisplayConfig& d = c.disp;
  getNum(ds["layout"], d.layout, 0, 1);
  getNum(ds["font"], d.font, 0, 1);
  getNum(ds["timeMode"], d.timeMode, 0, 1);
  getBool(ds["scroll"], d.scroll);
  getBool(ds["hideCancelled"], d.hideCancelled);
  getNum(ds["refreshSec"], d.refreshSec, 30, 3600);
  getNum(ds["pageSec"], d.pageSec, 2, 120);
  getNum(ds["brightness"], d.brightness, 0, 255);
  getBool(ds["nightEnabled"], d.nightEnabled);
  getNum(ds["nightBrightness"], d.nightBrightness, 0, 255);
  getTime(ds["nightFrom"], d.nightFrom);
  getTime(ds["nightTo"], d.nightTo);
  getBool(ds["ldrEnabled"], d.ldrEnabled);
  getNum(ds["ldrPin"], d.ldrPin, 32, 39);  // nur ADC1-Pins (ADC2 ist bei aktivem WLAN belegt)
  getBool(ds["ldrInvert"], d.ldrInvert);
  getNum(ds["ldrDark"], d.ldrDark, 0, 4095);
  getNum(ds["ldrBright"], d.ldrBright, 0, 4095);
  getNum(ds["ldrMinBright"], d.ldrMinBright, 0, 255);

  JsonArrayConst pr = src["profiles"];
  if (!pr.isNull()) {
    c.profileCount = 0;
    for (JsonVariantConst o : pr) {
      if (c.profileCount >= MAX_PROFILES) break;
      Profile f;  // Standardwerte
      getBool(o["enabled"], f.enabled);
      getStr(o["label"], f.label, sizeof(f.label));
      getStr(o["stationName"], f.stationName, sizeof(f.stationName));
      getStr(o["stationId"], f.stationId, sizeof(f.stationId));
      f.arrivals = o["type"].is<const char*>() && strcmp(o["type"].as<const char*>(), "arr") == 0;
      getNum(o["days"], f.days, 0, 0x7F);
      getTime(o["from"], f.fromMin);
      getTime(o["to"], f.toMin);
      getNum(o["products"], f.products, 0, 1023);
      getNum(o["leadMin"], f.leadMin, 0, 120);
      getNum(o["maxCount"], f.maxCount, 1, MAX_ENTRIES);
      getNum(o["hours"], f.hours, 1, 3);
      if (o["dirs"].is<const char*>())  // Umlaute falten, Ziele kommen als "ae" usw.
        foldAscii(o["dirs"].as<const char*>(), f.dirs, sizeof(f.dirs));
      c.profiles[c.profileCount++] = f;
    }
  }
}

bool panelEqual(const PanelConfig& a, const PanelConfig& b) {
  if (a.width != b.width || a.height != b.height || a.chain != b.chain || a.driver != b.driver ||
      a.lineDriver != b.lineDriver ||
      a.i2sMHz != b.i2sMHz || a.clkPhase != b.clkPhase || a.latchBlanking != b.latchBlanking ||
      a.doubleBuffer != b.doubleBuffer || a.minRefresh != b.minRefresh)
    return false;
  for (int i = 0; i < P_COUNT; i++)
    if (a.pins[i] != b.pins[i]) return false;
  return true;
}

// ---------- Datei ----------

bool configLoad() {
  File f = LittleFS.open(CONFIG_FILE, "r");
  if (!f) {
    Serial.println(F("[cfg] Keine Konfiguration gefunden, verwende Standardwerte"));
    return false;
  }
  JsonDocument doc;
  DeserializationError e = deserializeJson(doc, f);
  f.close();
  if (e) {
    Serial.printf("[cfg] Fehler beim Lesen: %s\n", e.c_str());
    return false;
  }
  configFromJson(doc.as<JsonVariantConst>(), cfg);
  Serial.printf("[cfg] Geladen, %u Profil(e)\n", cfg.profileCount);
  return true;
}

bool configSave() {
  JsonDocument doc;
  configToJson(cfg, doc, true);
  File f = LittleFS.open("/config.tmp", "w");
  if (!f) return false;
  size_t n = serializeJson(doc, f);
  f.close();
  if (!n) return false;
  LittleFS.remove(CONFIG_FILE);
  bool ok = LittleFS.rename("/config.tmp", CONFIG_FILE);
  Serial.printf("[cfg] Gespeichert (%u Byte): %s\n", (unsigned)n, ok ? "ok" : "FEHLER");
  return ok;
}
