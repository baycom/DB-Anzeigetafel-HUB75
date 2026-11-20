#include "net.h"
#include <WiFi.h>
#include <DNSServer.h>
#include <ESPmDNS.h>
#include <esp_sntp.h>
#include "config.h"

static DNSServer dns;
static bool apActive = false;
static bool mdnsStarted = false;
static uint32_t staStartMs = 0;
static uint32_t lastRetryMs = 0;
static uint32_t connectedSinceMs = 0;
static uint32_t lastConnectedMs = 0;
static bool everConnected = false;
static char apSsid[32];

static void startAP() {
  if (apActive) return;
  WiFi.mode(WIFI_AP_STA);
  const char* pw = strlen(cfg.apPass) >= 8 ? cfg.apPass : nullptr;
  WiFi.softAP(apSsid, pw);
  dns.setErrorReplyCode(DNSReplyCode::NoError);
  dns.start(53, "*", WiFi.softAPIP());
  apActive = true;
  Serial.printf("[net] Setup-AP '%s' aktiv, IP %s\n", apSsid, WiFi.softAPIP().toString().c_str());
}

static void stopAP() {
  if (!apActive) return;
  dns.stop();
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_STA);
  apActive = false;
  Serial.println(F("[net] Setup-AP beendet"));
}

void netStart() {
  // MAC aus dem eFuse lesen (WiFi.macAddress() liefert vor WiFi.mode() nur Nullen)
  uint64_t mac = ESP.getEfuseMac();
  snprintf(apSsid, sizeof(apSsid), "DB-Tafel-%02X%02X", (uint8_t)(mac >> 32), (uint8_t)(mac >> 40));

  WiFi.persistent(false);
  WiFi.setHostname(cfg.hostname);
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  // Die DB-Schnittstelle ist zeitweise nur per IPv6 zuverlaessig erreichbar,
  // DBAPI faellt bei Bedarf automatisch auf IPv4 zurueck.
  if (cfg.ipv6) WiFi.enableIPv6();

  if (cfg.wifiSsid[0]) {
    Serial.printf("[net] Verbinde mit '%s' ...\n", cfg.wifiSsid);
    WiFi.begin(cfg.wifiSsid, cfg.wifiPass);
    staStartMs = lastRetryMs = millis();
  } else {
    startAP();
  }
}

void netLoop() {
  if (apActive) dns.processNextRequest();

  if (WiFi.status() == WL_CONNECTED) {
    if (!connectedSinceMs) {
      connectedSinceMs = millis() | 1;
      Serial.printf("[net] Verbunden, IP %s, RSSI %d dBm\n", WiFi.localIP().toString().c_str(), WiFi.RSSI());
      if (!mdnsStarted && MDNS.begin(cfg.hostname)) {
        MDNS.addService("http", "tcp", 80);
        mdnsStarted = true;
      }
    }
    everConnected = true;
    lastConnectedMs = millis();
    // Setup-AP abschalten, sobald das WLAN laeuft und niemand mehr am AP haengt
    // (der AP kostet ~10 kB RAM, die fuer TLS zur DB-API gebraucht werden)
    if (apActive && millis() - connectedSinceMs > 15000 && WiFi.softAPgetStationNum() == 0) stopAP();
    return;
  }

  connectedSinceMs = 0;
  if (!cfg.wifiSsid[0]) return;
  // Setup-AP zusaetzlich starten: beim Start nach 60 s ohne Verbindung,
  // bei einem spaeteren WLAN-Ausfall erst nach 2 Minuten
  uint32_t waitMs = everConnected ? 120000 : 60000;
  uint32_t sinceMs = everConnected ? lastConnectedMs : staStartMs;
  if (!apActive && millis() - sinceMs > waitMs) startAP();
  // Regelmaessig neu verbinden (im AP-Modus nur ohne AP-Client, sonst stoert der Kanalwechsel)
  if (millis() - lastRetryMs > 30000 && (!apActive || WiFi.softAPgetStationNum() == 0)) {
    lastRetryMs = millis();
    WiFi.disconnect();  // laufenden Versuch abbrechen, sonst "sta is connecting, cannot set config"
    WiFi.begin(cfg.wifiSsid, cfg.wifiPass);
  }
}

void timeStart() {
  sntp_set_sync_interval(3600UL * 1000UL);
  configTzTime(cfg.tz, cfg.ntp1, cfg.ntp2[0] ? cfg.ntp2 : nullptr);
}

bool netApActive() { return apActive; }
bool netStaConnected() { return WiFi.status() == WL_CONNECTED; }
const char* netApSsid() { return apSsid; }
