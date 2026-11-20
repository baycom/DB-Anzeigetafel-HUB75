#pragma once
#include <Arduino.h>
#include <DBAPI.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include "config.h"

// Verkehrsmittel-Klasse fuer Farbe/Beschriftung
enum ProdClass : uint8_t { PC_FERN, PC_REGIO, PC_S, PC_U, PC_TRAM, PC_BUS, PC_SHIP, PC_RUF, PC_OTHER };

struct Entry {
  time_t   time;            // Planzeit (lokale Zeit als "naiver" Timestamp, wie von DBAPI geliefert)
  time_t   realTime;        // Prognose
  int16_t  delay;           // Minuten
  bool     cancelled;
  uint8_t  pclass;
  char     label[10];       // z.B. "S5", "RE2", "ICE", "100"
  char     platform[8];
  char     newPlatform[8];
  char     target[50];      // Ziel (Abfahrt) bzw. Herkunft (Ankunft)
};

struct Board {
  bool     valid;           // es liegen Daten vor
  uint8_t  err;             // DBerror des letzten Abrufs
  uint32_t fetchedMs;       // millis() des letzten Abrufs, 0 = nie
  uint32_t gen;             // Konfigurations-Generation
  uint8_t  count;
  Entry    e[MAX_ENTRIES];
};

extern DBAPI db;
extern Board boards[MAX_PROFILES];
extern SemaphoreHandle_t dataMutex;  // schuetzt cfg.profiles + boards
extern SemaphoreHandle_t apiMutex;   // schuetzt die DBAPI-Instanz
extern volatile uint32_t cfgGen;

void boardInit();    // Mutexe anlegen (vor allem anderen aufrufen)
void boardStart();   // Abruf-Task starten
void boardInvalidate();   // nach Konfigurationsaenderung (unter dataMutex aufrufen)
void boardRequestRefresh();

// Asynchrone Stationssuche (laeuft im Abruf-Task).
// Liefert true + JSON-Ergebnis, wenn die Suche nach q fertig ist; sonst false (Suche gestartet/laeuft).
bool stationSearch(const char* q, String& json);
// Asynchroner Abruf der aktuell angefahrenen Ziele (bzw. Herkunftsorte) einer Station,
// fuer die Auswahl des Richtungsfilters im Web-Frontend. Verhalten wie stationSearch().
bool targetLookup(const char* stationId, bool arrivals, uint16_t products, String& json);

bool   timeValid();
time_t localNow();   // lokale Zeit als naiver Timestamp (passend zu Entry::time)
bool   localTm(struct tm& out);
bool   profileActive(const Profile& p, const struct tm& lt);
const char* dbErrorText(uint8_t err);
