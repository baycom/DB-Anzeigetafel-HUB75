#include "board.h"
#include <WiFi.h>
#include "util.h"

DBAPI db;
Board boards[MAX_PROFILES];
SemaphoreHandle_t dataMutex = nullptr;
SemaphoreHandle_t apiMutex = nullptr;
volatile uint32_t cfgGen = 1;
static volatile bool refreshReq = false;

// ---------- Zeit ----------

bool timeValid() { return time(nullptr) > 1700000000; }

bool localTm(struct tm& out) {
  time_t t = time(nullptr);
  if (t < 1700000000) return false;
  localtime_r(&t, &out);
  return true;
}

time_t localNow() {
  struct tm lt;
  if (!localTm(lt)) return 0;
  // Lokale Wandzeit als UTC interpretieren -> gleiches Format wie DBAPI::parseTime()
  tmElements_t te;
  te.Year = lt.tm_year + 1900 - 1970;
  te.Month = lt.tm_mon + 1;
  te.Day = lt.tm_mday;
  te.Hour = lt.tm_hour;
  te.Minute = lt.tm_min;
  te.Second = lt.tm_sec;
  return makeTime(te);
}

// TimeLib (von DBAPI intern genutzt) mit lokaler Zeit versorgen
static time_t timeLibSync() { return localNow(); }

bool profileActive(const Profile& p, const struct tm& lt) {
  if (!p.enabled || !p.stationId[0]) return false;
  uint8_t bit = 1 << ((lt.tm_wday + 6) % 7);  // tm_wday: 0 = Sonntag
  if (!(p.days & bit)) return false;
  return inWindow(p.fromMin, p.toMin, lt.tm_hour * 60 + lt.tm_min);
}

const char* dbErrorText(uint8_t err) {
  switch (err) {
    case DBERR_NONE: return "OK";
    case DBERR_REQ_FAILED: return "Keine Verbindung";
    case DBERR_RESP_INVALID: return "Antwort ungueltig";
    case DBERR_NOSTATIONID: return "Keine Station";
    case DBERR_DESERIALIZATION_FAILED: return "JSON-Fehler";
    case DBERR_NO_JSON_FOUND: return "Keine Daten";
    default: return "Fehler";
  }
}

// ---------- Verkehrsmittel / Linienbezeichnung ----------

static bool startsWithI(const char* s, const char* p) { return strncasecmp(s, p, strlen(p)) == 0; }

static uint8_t productClass(const char* prod) {
  static const char* fern[] = {"ICE", "IC", "EC", "ECE", "RJ", "RJX", "NJ", "EN", "TGV", "FLX", "WB", "ES", "D"};
  for (auto f : fern)
    if (strcasecmp(prod, f) == 0) return PC_FERN;
  if (startsWithI(prod, "RE") || startsWithI(prod, "RB") || startsWithI(prod, "IRE") || startsWithI(prod, "IR") ||
      startsWithI(prod, "MEX") || startsWithI(prod, "ME") || startsWithI(prod, "ALX") || startsWithI(prod, "BRB"))
    return PC_REGIO;
  if (strcasecmp(prod, "S") == 0) return PC_S;
  if (strcasecmp(prod, "U") == 0) return PC_U;
  if (startsWithI(prod, "STR") || startsWithI(prod, "Tram") || strcasecmp(prod, "T") == 0) return PC_TRAM;
  if (startsWithI(prod, "Bus") || startsWithI(prod, "SEV") || strcasecmp(prod, "B") == 0) return PC_BUS;
  if (startsWithI(prod, "F") || startsWithI(prod, "Schiff")) return PC_SHIP;
  if (startsWithI(prod, "AST") || startsWithI(prod, "RUF") || startsWithI(prod, "ALT")) return PC_RUF;
  return PC_OTHER;
}

static void makeLabel(const DBdeparr* d, uint8_t pc, char* out, size_t n) {
  bool numeric = d->textline[0] >= '0' && d->textline[0] <= '9';
  switch (pc) {
    case PC_FERN:
      strlcpy(out, d->product, n);  // Zugnummern sind auf 64 px wenig hilfreich
      break;
    case PC_REGIO:
    case PC_S:
    case PC_U:
      if (numeric) snprintf(out, n, "%s%s", d->product, d->textline);
      else strlcpy(out, d->textline[0] ? d->textline : d->product, n);
      break;
    default:
      strlcpy(out, (numeric || !d->product[0]) ? d->textline : d->product, n);
      if (!out[0]) strlcpy(out, d->textline, n);
      break;
  }
  // Leerzeichen entfernen ("S 5" -> "S5")
  char* w = out;
  for (char* r = out; *r; r++)
    if (*r != ' ') *w++ = *r;
  *w = 0;
}

// ---------- Abruf ----------

void boardInvalidate() {
  cfgGen = cfgGen + 1;
  for (auto& b : boards) {
    b.valid = false;
    b.err = DBERR_NONE;
    b.fetchedMs = 0;
    b.count = 0;
  }
}

void boardRequestRefresh() { refreshReq = true; }

// ---------- Stationssuche ----------

static char searchQ[64] = "";
static volatile uint8_t searchState = 0;  // 0 = frei, 1 = angefordert, 2 = fertig
static String searchResult;

bool stationSearch(const char* q, String& json) {
  bool done = false;
  xSemaphoreTake(dataMutex, portMAX_DELAY);
  if (strcmp(q, searchQ) == 0 && searchState == 2) {
    json = searchResult;
    searchState = 0;
    searchQ[0] = 0;
    done = true;
  } else if (strcmp(q, searchQ) != 0 || searchState == 0) {
    strlcpy(searchQ, q, sizeof(searchQ));
    searchState = 1;
  }
  xSemaphoreGive(dataMutex);
  return done;
}

static void runStationSearch() {
  char q[64];
  xSemaphoreTake(dataMutex, portMAX_DELAY);
  strlcpy(q, searchQ, sizeof(q));
  xSemaphoreGive(dataMutex);

  JsonDocument doc;
  JsonArray arr = doc["stations"].to<JsonArray>();
  xSemaphoreTake(apiMutex, portMAX_DELAY);
  Serial.printf("[db] Heap frei %u, groesster Block %u\n", ESP.getFreeHeap(), ESP.getMaxAllocHeap());
  uint32_t t0 = millis();
  DBstation* s = db.getStation(q, nullptr, 10);
  for (; s; s = s->next) {
    JsonObject o = arr.add<JsonObject>();
    o["name"] = s->name;
    o["id"] = s->stationId;
  }
  uint8_t err = db.getError();
  xSemaphoreGive(apiMutex);
  doc["error"] = err ? dbErrorText(err) : "";
  Serial.printf("[db] Stationssuche '%s': %u Treffer, %s, %lu ms\n", q, arr.size(), dbErrorText(err),
                (unsigned long)(millis() - t0));

  xSemaphoreTake(dataMutex, portMAX_DELAY);
  if (strcmp(q, searchQ) == 0) {  // Suchbegriff inzwischen nicht geaendert
    searchResult = "";
    serializeJson(doc, searchResult);
    searchState = 2;
  }
  xSemaphoreGive(dataMutex);
}

// ---------- Workaround: DBAPI-Anfragepuffer ----------
// DBAPI::getStationBoard() serialisiert die Anfrage in einen festen 350-Byte-Puffer.
// Bei langen Stations-IDs und vielen Verkehrsmitteln wird das JSON abgeschnitten und die
// DB lehnt ab. Daher die Laenge vorab berechnen und die ID notfalls um entbehrliche Felder
// kuerzen (die Bahnhofsnummer L= bleibt immer erhalten).

static const char* const SERVICE_NAMES[10] = {
    "HOCHGESCHWINDIGKEITSZUEGE", "INTERCITYUNDEUROCITYZUEGE", "INTERREGIOUNDSCHNELLZUEGE",
    "NAHVERKEHRSONSTIGEZUEGE",   "SBAHNEN",                   "BUSSE",
    "SCHIFFE",                   "UBAHN",                     "STRASSENBAHN",
    "ANRUFPFLICHTIGEVERKEHRE"};
static const size_t DBAPI_REQ_BUF = 350;

static size_t requestLen(const char* id, uint16_t products) {
  size_t n = strlen("{\"verkehrsmittel\":[],\"ursprungsBahnhofId\":\"\",\"anfragezeit\":\"00:00\","
                    "\"datum\":\"0000-00-00\"}") + strlen(id);
  int cnt = 0;
  for (int i = 0; i < 10; i++)
    if (products & (1 << (9 - i))) {
      n += strlen(SERVICE_NAMES[i]) + 2;  // Anfuehrungszeichen
      cnt++;
    }
  if (cnt > 1) n += cnt - 1;  // Kommas
  return n;
}

// Entfernt ein Feld "key=...@" aus einer HAFAS-ID ("A=1@O=...@L=...@")
static bool removeIdField(char* id, const char* key) {
  size_t kl = strlen(key);
  for (char* f = id; *f;) {
    char* end = strchr(f, '@');
    if (strncmp(f, key, kl) == 0 && f[kl] == '=') {
      if (end) memmove(f, end + 1, strlen(end + 1) + 1);
      else *f = 0;
      return true;
    }
    if (!end) break;
    f = end + 1;
  }
  return false;
}

static void fitStationId(char* id, uint16_t products) {
  static const char* const drop[] = {"p", "i", "X", "Y", "U", "O"};
  for (auto k : drop) {
    if (requestLen(id, products) < DBAPI_REQ_BUF) return;
    removeIdField(id, k);
  }
}

// ---------- Richtungsfilter ----------

static bool containsI(const char* hay, const char* needle, size_t n) {
  for (; *hay; hay++)
    if (strncasecmp(hay, needle, n) == 0) return true;
  return false;
}

// dirs = "Hamburg; Altona" -> true, wenn target einen der Begriffe enthaelt (leer = alles)
static bool matchDirs(const char* target, const char* dirs) {
  bool any = false;
  for (const char* p = dirs; *p;) {
    while (*p == ';' || *p == ' ') p++;
    const char* e = strchr(p, ';');
    size_t n = e ? (size_t)(e - p) : strlen(p);
    while (n && p[n - 1] == ' ') n--;
    if (n) {
      any = true;
      if (containsI(target, p, n)) return true;
    }
    if (!e) break;
    p = e + 1;
  }
  return !any;
}

// Mit Filter mehr Fahrten abrufen, damit nach dem Aussortieren genug uebrig bleiben
static uint8_t requestCount(const Profile& p) {
  if (!p.dirs[0]) return p.maxCount;
  return (uint8_t)min(40, p.maxCount * 3);
}

// ---------- Ziele einer Station (fuer die Filterauswahl) ----------

static char tgtKey[180] = "";  // "d|1023|<id>"
static volatile uint8_t tgtState = 0;  // 0 = frei, 1 = angefordert, 2 = fertig
static String tgtResult;

bool targetLookup(const char* stationId, bool arrivals, uint16_t products, String& json) {
  char key[sizeof(tgtKey)];
  snprintf(key, sizeof(key), "%c|%u|%s", arrivals ? 'a' : 'd', products, stationId);
  bool done = false;
  xSemaphoreTake(dataMutex, portMAX_DELAY);
  if (strcmp(key, tgtKey) == 0 && tgtState == 2) {
    json = tgtResult;
    tgtResult = "";
    tgtState = 0;
    tgtKey[0] = 0;
    done = true;
  } else if (strcmp(key, tgtKey) != 0 || tgtState == 0) {
    strlcpy(tgtKey, key, sizeof(tgtKey));
    tgtState = 1;
  }
  xSemaphoreGive(dataMutex);
  return done;
}

static void runTargetLookup() {
  char key[sizeof(tgtKey)];
  xSemaphoreTake(dataMutex, portMAX_DELAY);
  strlcpy(key, tgtKey, sizeof(key));
  xSemaphoreGive(dataMutex);

  bool arrivals = key[0] == 'a';
  char* sep = strchr(key + 2, '|');
  if (!sep) return;
  uint16_t products = (uint16_t)atoi(key + 2);
  char id[160];
  strlcpy(id, sep + 1, sizeof(id));
  fitStationId(id, products);

  JsonDocument doc;
  JsonArray arr = doc["targets"].to<JsonArray>();
  xSemaphoreTake(apiMutex, portMAX_DELAY);
  uint32_t t0 = millis();
  // 2 Stunden, damit auch seltener bediente Richtungen auftauchen
  DBdeparr* r = arrivals ? db.getArrivals(id, nullptr, localNow(), 30, 2, products)
                         : db.getDepartures(id, nullptr, localNow(), 30, 2, products);
  uint8_t err = db.getError();
  for (DBdeparr* d = r; d; d = d->next) {
    char t[50], lbl[10];
    foldAscii(d->target, t, sizeof(t));
    if (!t[0] || strcmp(t, "null") == 0) continue;  // Fahrt ohne Zielangabe
    uint8_t pc = productClass(d->product);
    makeLabel(d, pc, lbl, sizeof(lbl));
    JsonObject o;
    for (JsonObject x : arr)
      if (strcmp(x["name"].as<const char*>(), t) == 0) o = x;
    if (o.isNull()) {
      o = arr.add<JsonObject>();
      o["name"] = t;
      o["lines"] = lbl;
    } else if (!containsI(o["lines"].as<const char*>(), lbl, strlen(lbl))) {
      String l = o["lines"].as<String>();
      o["lines"] = l + ", " + lbl;
    }
  }
  xSemaphoreGive(apiMutex);
  doc["error"] = err ? dbErrorText(err) : "";
  Serial.printf("[db] Ziele abgerufen: %u verschiedene, %s, %lu ms\n", arr.size(), dbErrorText(err),
                (unsigned long)(millis() - t0));

  xSemaphoreTake(dataMutex, portMAX_DELAY);
  if (strcmp(key, tgtKey) == 0) {
    tgtResult = "";
    serializeJson(doc, tgtResult);
    tgtState = 2;
  }
  xSemaphoreGive(dataMutex);
}

static Board tmp;  // gross -> nicht auf dem Stack

static void fetchProfile(uint8_t i) {
  Profile p;
  uint32_t gen;
  xSemaphoreTake(dataMutex, portMAX_DELAY);
  p = cfg.profiles[i];
  gen = cfgGen;
  xSemaphoreGive(dataMutex);

  time_t qt = localNow() + p.leadMin * 60;
  memset(&tmp, 0, sizeof(tmp));

  size_t len = requestLen(p.stationId, p.products);
  if (len >= DBAPI_REQ_BUF) {
    fitStationId(p.stationId, p.products);
    Serial.printf("[db] Anfrage waere %u Byte (max %u), Stations-ID gekuerzt: %s\n", (unsigned)len,
                  (unsigned)DBAPI_REQ_BUF - 1, p.stationId);
  }

  xSemaphoreTake(apiMutex, portMAX_DELAY);
  uint32_t t0 = millis();
  uint8_t want = requestCount(p);
  DBdeparr* r = p.arrivals ? db.getArrivals(p.stationId, nullptr, qt, want, p.hours, p.products)
                           : db.getDepartures(p.stationId, nullptr, qt, want, p.hours, p.products);
  tmp.err = db.getError();
  char target[sizeof(Entry::target)];
  uint8_t total = 0;
  for (DBdeparr* d = r; d && tmp.count < p.maxCount && tmp.count < MAX_ENTRIES; d = d->next) {
    total++;
    foldAscii(d->target, target, sizeof(target));
    if (!matchDirs(target, p.dirs)) continue;
    Entry& e = tmp.e[tmp.count++];
    e.time = d->time;
    e.realTime = d->realTime;
    e.delay = d->delay;
    e.cancelled = d->cancelled;
    e.pclass = productClass(d->product);
    makeLabel(d, e.pclass, e.label, sizeof(e.label));
    strlcpy(e.platform, d->platform, sizeof(e.platform));
    strlcpy(e.newPlatform, d->newPlatform, sizeof(e.newPlatform));
    strlcpy(e.target, target, sizeof(e.target));
  }
  xSemaphoreGive(apiMutex);

  if (p.dirs[0]) Serial.printf("[db] Richtungsfilter '%s': %u von %u Fahrten\n", p.dirs, tmp.count, total);
  Serial.printf("[db] Profil %u (%s): %u Fahrten, Status %s, %lu ms, Heap %u\n", i, p.stationName, tmp.count,
                dbErrorText(tmp.err), (unsigned long)(millis() - t0), ESP.getFreeHeap());

  xSemaphoreTake(dataMutex, portMAX_DELAY);
  if (gen == cfgGen) {
    Board& b = boards[i];
    if (tmp.count > 0 || tmp.err == DBERR_NONE) {
      memcpy(b.e, tmp.e, sizeof(Entry) * tmp.count);
      b.count = tmp.count;
      b.valid = true;
    }
    b.err = tmp.err;
    b.fetchedMs = millis() | 1;
    b.gen = gen;
  }
  xSemaphoreGive(dataMutex);
}

static void fetchTask(void*) {
  for (;;) {
    vTaskDelay(pdMS_TO_TICKS(500));
    if (WiFi.status() != WL_CONNECTED) continue;
    if (searchState == 1) runStationSearch();  // Stationssuche hat Vorrang
    if (tgtState == 1) runTargetLookup();
    struct tm lt;
    if (!localTm(lt)) continue;

    bool force = refreshReq;
    refreshReq = false;
    for (uint8_t i = 0; i < MAX_PROFILES; i++) {
      bool due = false;
      xSemaphoreTake(dataMutex, portMAX_DELAY);
      if (i < cfg.profileCount && profileActive(cfg.profiles[i], lt)) {
        const Board& b = boards[i];
        uint32_t interval = (b.err == DBERR_NONE ? cfg.disp.refreshSec : min<uint16_t>(30, cfg.disp.refreshSec)) * 1000UL;
        due = force || b.fetchedMs == 0 || b.gen != cfgGen || millis() - b.fetchedMs >= interval;
      }
      xSemaphoreGive(dataMutex);
      if (due) {
        fetchProfile(i);
        vTaskDelay(pdMS_TO_TICKS(300));  // API nicht im Dauerfeuer abfragen
      }
    }
  }
}

void boardInit() {
  dataMutex = xSemaphoreCreateMutex();
  apiMutex = xSemaphoreCreateMutex();
  memset(boards, 0, sizeof(boards));
  db.setUmlaut(REP_UML);  // ä -> ae (Pixel-Fonts ohne Umlaute)
  setSyncProvider(timeLibSync);
  setSyncInterval(60);
}

void boardStart() {
  // TLS + JSON brauchen reichlich Stack.
  // DBAPI wartet auf Antworten per Busy-Loop (Stream::timedRead, bis 10 s). Auf Core 0
  // verhungert dabei der Idle-Task, den der Task-Watchdog ueberwacht -> Reset.
  // Daher Core 1 (dort ueberwacht der Watchdog den Idle-Task nicht) mit niedrigster
  // Prioritaet: Display (2) und Webserver (1) verdraengen den Abruf jederzeit.
  xTaskCreatePinnedToCore(fetchTask, "dbfetch", 16384, nullptr, tskIDLE_PRIORITY, nullptr, 1);
}
