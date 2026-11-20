#pragma once
#include <Arduino.h>

void displayInit();
void displayLoop();
void displayStart();         // Display-Task starten (danach displayLoop() nicht mehr selbst aufrufen)
int  displayLdrRaw();        // geglaetteter ADC-Wert des Lichtsensors, -1 = aus
uint8_t displayBrightness(); // aktuell eingestellte Panel-Helligkeit
// Meldung (z.B. "Firmware-Update") anzeigen; ms = 0 -> bis zum naechsten Aufruf mit title = nullptr
void displayMessage(const char* title, const char* line1 = nullptr, const char* line2 = nullptr, uint32_t ms = 0);
