#pragma once
#include <Arduino.h>

void netStart();
void netLoop();
void timeStart();          // NTP + Zeitzone (erneut aufrufbar nach Konfigurationsaenderung)
bool netApActive();
bool netStaConnected();
const char* netApSsid();
