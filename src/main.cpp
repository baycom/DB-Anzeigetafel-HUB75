/*
 * DB Abfahrts-/Ankunftstafel
 * 64x64 HUB75 LED-Panel + ESP32 Trinity, Daten ueber soundstorm/DBAPI
 *
 * Module:
 *   config  - Konfiguration (LittleFS /config.json)
 *   net     - WLAN, Setup-AP mit Captive Portal, mDNS, NTP/Zeitzone
 *   board   - FreeRTOS-Task, der die aktiven Profile zyklisch bei der DB abfragt
 *   display - Darstellung auf dem HUB75-Panel
 *   web     - Web-Frontend + JSON-API + OTA-Update
 */
#include <Arduino.h>
#include <LittleFS.h>
#include "board.h"
#include "config.h"
#include "display.h"
#include "net.h"
#include "web.h"
#include <esp_system.h>

const char* resetReasonText() {
  switch (esp_reset_reason()) {
    case ESP_RST_POWERON: return "Einschalten";
    case ESP_RST_SW: return "Software (ESP.restart)";
    case ESP_RST_PANIC: return "Absturz (Panic)";
    case ESP_RST_INT_WDT: return "Interrupt-Watchdog";
    case ESP_RST_TASK_WDT: return "Task-Watchdog";
    case ESP_RST_WDT: return "Watchdog";
    case ESP_RST_BROWNOUT: return "Unterspannung (Brownout)";
    case ESP_RST_EXT: return "Reset-Taste/extern";
    case ESP_RST_DEEPSLEEP: return "Deep-Sleep";
    default: return "unbekannt";
  }
}

void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.printf("\n[main] DB-Tafel %s, Neustart-Ursache: %s\n", FW_VERSION, resetReasonText());

  if (!LittleFS.begin(true)) Serial.println(F("[main] LittleFS konnte nicht eingebunden werden"));
  configLoad();

  boardInit();
  displayInit();
  displayStart();
  displayMessage("DB-Tafel", "Start ...", FW_VERSION, 1500);

  netStart();
  timeStart();
  webStart();
  boardStart();
}

void loop() {
  netLoop();
  webLoop();
  delay(2);
}
