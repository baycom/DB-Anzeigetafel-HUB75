#pragma once
#include <Arduino.h>

// Wandelt UTF-8-Umlaute in ASCII um (ä -> ae ...), andere Nicht-ASCII-Zeichen werden verworfen.
void foldAscii(const char* in, char* out, size_t outSize);

// "HH:MM" <-> Minuten seit Mitternacht
int  parseHHMM(const char* s);               // -1 bei Fehler
void fmtHHMM(uint16_t minutes, char* out);   // out >= 6 Byte

// Liegt m (Minuten seit 0:00) im Fenster [from, to)? Ueber Mitternacht moeglich, from == to = immer.
bool inWindow(uint16_t from, uint16_t to, uint16_t m);
