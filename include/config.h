#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>

#ifndef FW_VERSION
#define FW_VERSION "dev"
#endif

#define MAX_PROFILES 8   // max. Anzahl konfigurierbarer Haltestellen-Profile
#define MAX_ENTRIES 20   // max. Fahrten pro Profil

// Reihenfolge der HUB75-Pins in PanelConfig::pins
enum PinIdx { P_R1, P_G1, P_B1, P_R2, P_G2, P_B2, P_A, P_B, P_C, P_D, P_E, P_LAT, P_OE, P_CLK, P_COUNT };
extern const char* const PIN_NAMES[P_COUNT];

// Ein Profil = eine Haltestelle + Abfahrt/Ankunft + Zeitfenster, in dem es angezeigt wird
struct Profile {
  bool     enabled     = true;
  char     label[24]   = "";      // Anzeigename in der Kopfzeile (leer = Stationsname)
  char     stationName[48] = "";
  char     stationId[160]  = "";  // HAFAS-ID aus der Stationssuche
  bool     arrivals    = false;   // false = Abfahrten, true = Ankuenfte
  uint8_t  days        = 0x7F;    // Bit0 = Montag ... Bit6 = Sonntag
  uint16_t fromMin     = 0;       // Beginn Zeitfenster (Minuten seit 0:00)
  uint16_t toMin       = 0;       // Ende Zeitfenster; from == to -> ganztaegig
  uint16_t products    = 1023;    // DBproduct-Bitmaske
  uint8_t  leadMin     = 0;       // Vorlaufzeit/Fussweg: Fahrten frueher als jetzt+X ausblenden
  uint8_t  maxCount    = 10;      // max. Anzahl abgerufener Fahrten
  uint8_t  hours       = 1;       // abgefragter Zeitraum in Stunden (1..3)
  // Richtungsfilter: nur Fahrten, deren Ziel (bei Ankuenften: Herkunft) einen der
  // durch ';' getrennten Begriffe enthaelt (Gross/Klein egal, ASCII). Leer = alle.
  char     dirs[120]   = "";
};

// HUB75-Parameter (Aenderung erfordert Neustart)
struct PanelConfig {
  uint16_t width        = 64;
  uint16_t height       = 64;
  uint8_t  chain        = 1;
  // Standardbelegung ESP32 Trinity (E = GPIO18 fuer 64x64-Panels)
  int8_t   pins[P_COUNT] = {25, 26, 27, 14, 12, 13, 23, 19, 5, 17, 18, 4, 15, 16};
  uint8_t  driver       = 0;      // 0=SHIFTREG 1=FM6124 2=FM6126A 3=ICN2038S 4=MBI5124 5=DP3246
  uint8_t  lineDriver   = 0;      // 0=TYPE138 (Standard) 1=TYPE595 2=DIRECT 3=SM5266P
  uint8_t  i2sMHz       = 8;      // 8, 10, 15, 20
  bool     clkPhase     = true;
  uint8_t  latchBlanking = 1;
  bool     doubleBuffer = false;
  uint16_t minRefresh   = 60;
};

// Anzeige-Parameter (wirken sofort)
struct DisplayConfig {
  uint8_t  layout        = 0;     // 0 = kompakt (1 Zeile/Fahrt), 1 = ausfuehrlich (2 Zeilen/Fahrt)
  uint8_t  font          = 0;     // 0 = klein (TomThumb 3x5), 1 = normal (5x7)
  uint8_t  timeMode      = 0;     // 0 = Uhrzeit, 1 = Minuten bis Abfahrt
  bool     scroll        = true;  // lange Texte scrollen
  bool     hideCancelled = false;
  uint16_t refreshSec    = 60;    // Abfrageintervall der DB-API
  uint8_t  pageSec       = 10;    // Wechselintervall bei mehreren aktiven Profilen
  uint8_t  brightness    = 80;    // 0..255
  bool     nightEnabled  = false;
  uint8_t  nightBrightness = 10;
  uint16_t nightFrom     = 22 * 60;
  uint16_t nightTo       = 6 * 60;
  // Lichtsensor (LDR-Spannungsteiler an einem ADC1-Pin)
  bool     ldrEnabled    = false;
  int8_t   ldrPin        = 35;
  bool     ldrInvert     = false; // true: heller = kleinerer ADC-Wert
  uint16_t ldrDark       = 100;   // ADC-Rohwert (0..4095) bei Dunkelheit
  uint16_t ldrBright     = 3000;  // ADC-Rohwert bei heller Umgebung
  uint8_t  ldrMinBright  = 8;     // Panel-Helligkeit bei Dunkelheit (Maximum = brightness)
};

struct Config {
  char wifiSsid[33]  = "";
  char wifiPass[65]  = "";
  char hostname[32]  = "db-tafel";
  char apPass[65]    = "bahnhof123";  // Passwort des Setup-Access-Points (<8 Zeichen = offen)
  bool ipv6         = true;          // IPv6 nutzen (DBAPI bevorzugt dann IPv6)
  char webPass[33]   = "";            // Passwort fuer das Web-Frontend (Benutzer "admin"), leer = aus
  char ntp1[64]      = "pool.ntp.org";
  char ntp2[64]      = "ptbtime1.ptb.de";
  char tz[64]        = "CET-1CEST,M3.5.0,M10.5.0/3";
  PanelConfig   panel;
  DisplayConfig disp;
  uint8_t profileCount = 0;
  Profile profiles[MAX_PROFILES];
};

extern Config cfg;

bool configLoad();
bool configSave();
// secrets = false: Passwoerter werden nicht ausgegeben (fuer das Web-Frontend)
void configToJson(const Config& c, JsonDocument& doc, bool secrets);
// Uebernimmt alle im JSON vorhandenen Felder nach c (fehlende Felder bleiben unveraendert)
void configFromJson(JsonVariantConst src, Config& c);
bool panelEqual(const PanelConfig& a, const PanelConfig& b);
