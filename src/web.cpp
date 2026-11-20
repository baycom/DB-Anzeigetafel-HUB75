#include "web.h"
#include <WebServer.h>
#include <WiFi.h>
#include <Update.h>
#include "board.h"
#include "config.h"
#include "display.h"
#include "net.h"
#include "webpage.h"
#include <NetworkClientSecure.h>

const char* resetReasonText();

static WebServer server(80);
static uint32_t rebootAt = 0;
static bool otaAuthOk = false;

static bool auth() {
  if (!cfg.webPass[0]) return true;
  if (server.authenticate("admin", cfg.webPass)) return true;
  server.requestAuthentication(BASIC_AUTH, "DB-Tafel");
  return false;
}

static void sendJson(JsonDocument& doc, int code = 200) {
  String out;
  serializeJson(doc, out);
  server.sendHeader("Cache-Control", "no-store");
  server.send(code, "application/json", out);
}

static void sendError(int code, const char* msg) {
  JsonDocument d;
  d["ok"] = false;
  d["error"] = msg;
  sendJson(d, code);
}

// ---------- Handler ----------

static void handleRoot() {
  if (!auth()) return;
  server.sendHeader("Cache-Control", "no-store");
  server.send_P(200, "text/html; charset=utf-8", INDEX_HTML);
}

static void handleGetConfig() {
  if (!auth()) return;
  JsonDocument doc;
  xSemaphoreTake(dataMutex, portMAX_DELAY);
  configToJson(cfg, doc, false);
  xSemaphoreGive(dataMutex);
  doc["info"]["maxProfiles"] = MAX_PROFILES;
  doc["info"]["maxEntries"] = MAX_ENTRIES;
  doc["info"]["version"] = FW_VERSION;
  sendJson(doc);
}

static void handlePostConfig() {
  if (!auth()) return;
  JsonDocument in;
  DeserializationError e = deserializeJson(in, server.arg("plain"));
  if (e) return sendError(400, e.c_str());

  Config* n = new Config(cfg);  // ~3 kB, nicht auf den Stack
  configFromJson(in.as<JsonVariantConst>(), *n);

  bool reboot = !panelEqual(n->panel, cfg.panel) || strcmp(n->wifiSsid, cfg.wifiSsid) ||
                strcmp(n->wifiPass, cfg.wifiPass) || strcmp(n->hostname, cfg.hostname) ||
                strcmp(n->apPass, cfg.apPass) || n->ipv6 != cfg.ipv6;
  bool timeChanged = strcmp(n->ntp1, cfg.ntp1) || strcmp(n->ntp2, cfg.ntp2) || strcmp(n->tz, cfg.tz);

  xSemaphoreTake(dataMutex, portMAX_DELAY);
  cfg = *n;
  boardInvalidate();
  xSemaphoreGive(dataMutex);
  delete n;

  bool ok = configSave();
  if (timeChanged) timeStart();

  JsonDocument out;
  out["ok"] = ok;
  out["reboot"] = reboot;
  sendJson(out, ok ? 200 : 500);
  if (reboot) rebootAt = millis() + 1500;
}

static void handleStatus() {
  if (!auth()) return;
  JsonDocument doc;
  struct tm lt;
  bool tv = localTm(lt);
  doc["timeValid"] = tv;
  if (tv) {
    char b[24];
    strftime(b, sizeof(b), "%d.%m.%Y %H:%M:%S", &lt);
    doc["time"] = b;
  }
  doc["version"] = FW_VERSION;
  doc["uptime"] = millis() / 1000;
  doc["heap"] = ESP.getFreeHeap();
  doc["heapMin"] = ESP.getMinFreeHeap();
  doc["heapMaxBlock"] = ESP.getMaxAllocHeap();
  doc["resetReason"] = resetReasonText();
  doc["ldr"] = displayLdrRaw();
  doc["brightness"] = displayBrightness();
  JsonObject w = doc["wifi"].to<JsonObject>();
  w["connected"] = netStaConnected();
  w["ssid"] = WiFi.SSID();
  w["ip"] = WiFi.localIP().toString();
  w["rssi"] = WiFi.RSSI();
  w["ap"] = netApActive();
  w["apSsid"] = netApSsid();
  w["hostname"] = cfg.hostname;

  time_t nowL = localNow();
  JsonArray bs = doc["boards"].to<JsonArray>();
  xSemaphoreTake(dataMutex, portMAX_DELAY);
  for (int i = 0; i < cfg.profileCount; i++) {
    const Board& b = boards[i];
    JsonObject o = bs.add<JsonObject>();
    o["idx"] = i;
    o["active"] = tv && profileActive(cfg.profiles[i], lt);
    o["valid"] = b.valid;
    o["error"] = b.err ? dbErrorText(b.err) : "";
    o["age"] = b.fetchedMs ? (long)((millis() - b.fetchedMs) / 1000) : -1;
    o["count"] = b.count;
    JsonArray es = o["entries"].to<JsonArray>();
    for (int k = 0; k < b.count && k < 8; k++) {
      const Entry& e = b.e[k];
      if (e.realTime + 59 < nowL) continue;
      JsonObject eo = es.add<JsonObject>();
      char t[6];
      struct tm tt;
      gmtime_r(&e.time, &tt);
      snprintf(t, sizeof(t), "%02d:%02d", tt.tm_hour, tt.tm_min);
      eo["time"] = t;
      eo["delay"] = e.delay;
      eo["cancelled"] = e.cancelled;
      eo["line"] = e.label;
      eo["target"] = e.target;
      eo["platform"] = e.newPlatform[0] ? e.newPlatform : e.platform;
    }
  }
  xSemaphoreGive(dataMutex);
  sendJson(doc);
}

static void handleStations() {
  if (!auth()) return;
  String q = server.arg("q");
  q.trim();
  if (q.length() < 2) return sendError(400, "Suchbegriff zu kurz");
  if (!netStaConnected()) return sendError(503, "Kein WLAN");
  String json;
  if (stationSearch(q.c_str(), json)) {
    server.sendHeader("Cache-Control", "no-store");
    server.send(200, "application/json", json);
  } else {
    server.send(202, "application/json", "{\"pending\":true}");  // Frontend fragt erneut nach
  }
}

// /api/targets?id=<Stations-ID>&type=dep|arr&products=<Bitmaske>
static void handleTargets() {
  if (!auth()) return;
  String id = server.arg("id");
  if (id.isEmpty()) return sendError(400, "Keine Station");
  if (!netStaConnected()) return sendError(503, "Kein WLAN");
  long prod = server.hasArg("products") ? server.arg("products").toInt() : 1023;
  if (prod <= 0 || prod > 1023) prod = 1023;
  String json;
  if (targetLookup(id.c_str(), server.arg("type") == "arr", (uint16_t)prod, json)) {
    server.sendHeader("Cache-Control", "no-store");
    server.send(200, "application/json", json);
  } else {
    server.send(202, "application/json", "{\"pending\":true}");
  }
}

// Diagnose: TLS-Verbindung zu einem Host aufbauen (Handshake-Timeout 15 s)
// /api/tlstest?host=app.services-bahn.de[&v4=1]
static void handleTlsTest() {
  if (!auth()) return;
  String host = server.arg("host");
  if (host.isEmpty()) host = "app.services-bahn.de";
  JsonDocument doc;
  doc["host"] = host;
  doc["heapBefore"] = ESP.getFreeHeap();
  doc["maxBlockBefore"] = ESP.getMaxAllocHeap();
  NetworkClientSecure c;
  c.setInsecure();
  c.setHandshakeTimeout(15);
  c.setTimeout(10000);
  if (server.hasArg("dbciphers")) {  // Cipher-Liste wie DBAPI (iOS-Emulation)
    static const int list[] = {MBEDTLS_TLS_ECDHE_ECDSA_WITH_AES_256_GCM_SHA384, MBEDTLS_TLS_ECDHE_ECDSA_WITH_AES_128_GCM_SHA256,
                               MBEDTLS_TLS_ECDHE_ECDSA_WITH_CHACHA20_POLY1305_SHA256, MBEDTLS_TLS_ECDHE_RSA_WITH_AES_256_GCM_SHA384,
                               MBEDTLS_TLS_ECDHE_RSA_WITH_AES_128_GCM_SHA256, MBEDTLS_TLS_ECDHE_RSA_WITH_CHACHA20_POLY1305_SHA256, 0};
    c.setCiphers(list);
    doc["dbciphers"] = true;
  }
  uint32_t t0 = millis();
  bool ok;
  if (server.hasArg("v4")) {
    IPAddress ip;
    ok = WiFi.hostByName(host.c_str(), ip) && c.connect(ip, 443, host.c_str(), nullptr, nullptr, nullptr);
    doc["ip"] = ip.toString();
  } else {
    ok = c.connect(host.c_str(), 443);
    if (ok) doc["ip"] = c.remoteIP().toString();
  }
  doc["connectMs"] = millis() - t0;
  doc["connected"] = ok;
  if (ok && server.hasArg("api")) {
    // dieselbe Anfrage wie DBAPI::getStation()
    String json = "{\"locationTypes\":[\"ST\"],\"searchTerm\":\"Hannover Hbf\"}";
    c.print(String("POST /mob/location/search HTTP/1.1\r\nHost: ") + host +
            "\r\nUser-Agent: DBNavigator/iOS/26.12.1\r\nX-Correlation-ID: FOO\r\n"
            "Content-Type: application/x.db.vendo.mob.location.v3+json\r\n"
            "Accept: application/x.db.vendo.mob.location.v3+json\r\nConnection: close\r\n"
            "Content-Length: " + json.length() + "\r\n\r\n" + json);
    doc["status"] = c.readStringUntil('\n');
  } else if (ok) {
    c.print(String("HEAD / HTTP/1.1\r\nHost: ") + host + "\r\nConnection: close\r\n\r\n");
    doc["status"] = c.readStringUntil('\n');
  } else {
    char err[100] = "";
    c.lastError(err, sizeof(err));
    doc["error"] = err;
  }
  c.stop();
  doc["totalMs"] = millis() - t0;
  Serial.printf("[tls] %s: %s in %lu ms\n", host.c_str(), ok ? "OK" : "FEHLER", (unsigned long)(millis() - t0));
  sendJson(doc);
}

static void handleScan() {
  if (!auth()) return;
  int n = WiFi.scanNetworks(false, false);
  JsonDocument doc;
  JsonArray arr = doc["networks"].to<JsonArray>();
  for (int i = 0; i < n; i++) {
    bool dup = false;
    for (JsonObject o : arr)
      if (WiFi.SSID(i) == o["ssid"].as<const char*>()) dup = true;
    if (dup || WiFi.SSID(i).isEmpty()) continue;
    JsonObject o = arr.add<JsonObject>();
    o["ssid"] = WiFi.SSID(i);
    o["rssi"] = WiFi.RSSI(i);
    o["open"] = WiFi.encryptionType(i) == WIFI_AUTH_OPEN;
  }
  WiFi.scanDelete();
  sendJson(doc);
}

static void handleRefresh() {
  if (!auth()) return;
  boardRequestRefresh();
  server.send(200, "application/json", "{\"ok\":true}");
}

static void handleReboot() {
  if (!auth()) return;
  server.send(200, "application/json", "{\"ok\":true}");
  rebootAt = millis() + 1000;
}

static void handleUpdateDone() {
  if (!auth()) return;
  bool ok = otaAuthOk && !Update.hasError();
  server.send(ok ? 200 : 500, "application/json", ok ? "{\"ok\":true}" : "{\"ok\":false}");
  if (ok) {
    displayMessage("Update", "erfolgreich", "Neustart ...");
    rebootAt = millis() + 1500;
  } else {
    displayMessage("Update", "fehlgeschlagen", nullptr, 5000);
  }
}

static void handleUpdateUpload() {
  HTTPUpload& up = server.upload();
  if (up.status == UPLOAD_FILE_START) {
    otaAuthOk = !cfg.webPass[0] || server.authenticate("admin", cfg.webPass);
    if (!otaAuthOk) return;
    Serial.printf("[ota] Start: %s\n", up.filename.c_str());
    displayMessage("Update", "laeuft ...");
    if (!Update.begin(UPDATE_SIZE_UNKNOWN)) Update.printError(Serial);
  } else if (!otaAuthOk) {
    return;
  } else if (up.status == UPLOAD_FILE_WRITE) {
    if (Update.write(up.buf, up.currentSize) != up.currentSize) Update.printError(Serial);
  } else if (up.status == UPLOAD_FILE_END) {
    if (Update.end(true)) Serial.printf("[ota] OK, %u Byte\n", up.totalSize);
    else Update.printError(Serial);
  } else if (up.status == UPLOAD_FILE_ABORTED) {
    Update.abort();
  }
}

static void handleNotFound() {
  // Captive Portal: im Setup-AP alle fremden Hosts auf die Konfigurationsseite umleiten
  if (netApActive() && server.hostHeader() != WiFi.softAPIP().toString()) {
    server.sendHeader("Location", String("http://") + WiFi.softAPIP().toString() + "/", true);
    server.send(302, "text/plain", "");
    return;
  }
  server.send(404, "text/plain", "Nicht gefunden");
}

void webStart() {
  server.on("/", HTTP_GET, handleRoot);
  server.on("/api/config", HTTP_GET, handleGetConfig);
  server.on("/api/config", HTTP_POST, handlePostConfig);
  server.on("/api/status", HTTP_GET, handleStatus);
  server.on("/api/stations", HTTP_GET, handleStations);
  server.on("/api/targets", HTTP_GET, handleTargets);
  server.on("/api/scan", HTTP_GET, handleScan);
  server.on("/api/tlstest", HTTP_GET, handleTlsTest);
  server.on("/api/refresh", HTTP_POST, handleRefresh);
  server.on("/api/reboot", HTTP_POST, handleReboot);
  server.on("/update", HTTP_POST, handleUpdateDone, handleUpdateUpload);
  server.onNotFound(handleNotFound);
  server.begin();
  Serial.println(F("[web] Webserver gestartet (Port 80)"));
}

void webLoop() {
  server.handleClient();
  if (rebootAt && (int32_t)(millis() - rebootAt) > 0) {
    displayMessage("Neustart", "...");
    delay(200);
    ESP.restart();
  }
}
