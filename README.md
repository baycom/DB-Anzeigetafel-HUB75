# DB-Anzeigetafel für 64x64 HUB75 + ESP32 Trinity

Zeigt Abfahrten bzw. Ankünfte der Deutschen Bahn (über [DBAPI](https://github.com/ArduinoHannover/DBAPI)) auf einem HUB75-LED-Panel an. Konfiguriert wird alles über ein Web-Frontend.

## Funktionen

- Bis zu 8 Profile (Haltestelle + Abfahrten/Ankünfte), jeweils mit
  - Wochentagen und Zeitfenster (z.B. Abfahrten 06:00–12:00, Ankünfte 12:00–20:00; Fenster über Mitternacht möglich, gleiche Von/Bis-Zeit = ganztägig)
  - Verkehrsmittel-Filter (ICE, IC/EC, RE/RB, S, U, Bus, Tram …)
  - Fußweg/Vorlauf: Abfahrten, die man nicht mehr erreicht, werden ausgeblendet
- Mehrere gleichzeitig aktive Profile wechseln sich seitenweise ab
- Ist kein Profil aktiv, wird eine Uhr mit Datum angezeigt
- Layout *kompakt* (8 Fahrten, 1 Zeile je Fahrt) oder *ausführlich* (4 Fahrten mit Verspätung und Gleis)
- Laufschrift für lange Ziele, Farbcodierung der Linien, Verspätungen gelb/rot, Ausfälle rot durchgestrichen
- Nachtmodus mit eigener Helligkeit
- Web-Frontend: Stationssuche, Profile, Anzeige, WLAN (mit Scan), NTP-Server/Zeitzone, alle HUB75-Parameter inkl. Pinbelegung, Web-Passwort, Firmware-Update (OTA)
- Setup-Access-Point mit Captive Portal, wenn kein WLAN verfügbar ist

## Build & Flashen

1. Ordner in VS Code mit der PlatformIO-Erweiterung öffnen.
2. **Build** / **Upload** (Env `trinity`). Der erste Build lädt die pioarduino-Plattform und den angepassten arduino-esp32-Fork (`ArduinoHannover/arduino-esp32#add-setciphers`). Den braucht DBAPI auf dem ESP32, weil die DB-Schnittstelle sonst die TLS-Verbindung blockiert.
3. Serieller Monitor mit 115200 Baud.

Spätere Updates können auch über das Web-Frontend erfolgen (System → Firmware-Update, Datei `.pio/build/trinity/firmware.bin`).

## Erste Einrichtung

1. Nach dem ersten Start öffnet die Tafel den Access-Point **DB-Tafel-XXXX** (Passwort `bahnhof123`). SSID und Passwort stehen auch auf dem Panel.
2. Mit dem Handy verbinden, die Konfigurationsseite öffnet sich automatisch (sonst http://192.168.4.1).
3. WLAN eintragen und **Speichern**, danach startet das Gerät neu.
4. Im Heimnetz unter `http://db-tafel.local` (oder der IP, die auf der Uhr-Anzeige steht) die Haltestellen anlegen: Namen suchen, Treffer anklicken, Zeitfenster setzen, speichern.

Kommt die WLAN-Verbindung nicht zustande, startet der Setup-AP nach 60 s zusätzlich (bei einem späteren Ausfall nach 2 Minuten). Die Tafel versucht weiter, sich mit dem WLAN zu verbinden.

## Anzeige

| Element | Bedeutung |
| --- | --- |
| Kopfzeile | Anzeigename bzw. „Ab/An + Station“, rechts die Uhrzeit (rot = letzter Abruf fehlgeschlagen) |
| Zeit weiß / gelb / rot | pünktlich / 1–4 min / ≥ 5 min verspätet |
| Zeit rot durchgestrichen | Fahrt fällt aus |
| Badge-Farben | Fernverkehr weiß/rot, Regio rot, S grün, U blau, Tram orange, Bus lila |
| Punkte in der untersten Pixelzeile | Seitenanzeige bei mehreren aktiven Profilen |
| Gleis orange (ausführliches Layout) | Gleiswechsel |

## HUB75 / Trinity

Die Standard-Pinbelegung entspricht der ESP32 Trinity:

| R1 | G1 | B1 | R2 | G2 | B2 | A | B | C | D | E | LAT | OE | CLK |
|----|----|----|----|----|----|---|---|---|---|---|-----|----|-----|
| 25 | 26 | 27 | 14 | 12 | 13 | 23 | 19 | 5 | 17 | **18** | 4 | 15 | 16 |

64x64-Panels brauchen die E-Leitung. Auf der Trinity muss dafür die E-Lötbrücke gesetzt sein (Standard: GPIO18). Bleibt das Panel dunkel oder zeigt es Streifen, im Web-Frontend unter *HUB75-Panel* den Treiber-IC (z.B. FM6126A), den I2S-Takt oder die Clock-Phase ändern.

Die Farbtiefe wird in `platformio.ini` über `PIXEL_COLOR_DEPTH_BITS` eingestellt (Standard 4). Mehr Farbtiefe kostet RAM, der für die TLS-Verbindung zur DB fehlt.

## Projektstruktur

```
src/main.cpp      Setup/Loop
src/config.*      Konfiguration (LittleFS /config.json)
src/net.*         WLAN, Setup-AP, Captive Portal, mDNS, NTP
src/board.*       FreeRTOS-Task: zyklischer Abruf über DBAPI
src/display.*     Darstellung (Offscreen-Canvas → HUB75-DMA)
src/web.*         Webserver, JSON-API, OTA
src/webpage.h     Web-Frontend (HTML/JS, eingebettet)
```

### JSON-API

| Methode | Pfad | Zweck |
| --- | --- | --- |
| GET | `/api/config` | Konfiguration (ohne Passwörter) |
| POST | `/api/config` | Konfiguration speichern; nur enthaltene Felder werden geändert. Antwort `reboot:true`, wenn WLAN- oder Panel-Daten geändert wurden |
| GET | `/api/status` | Zeit, WLAN, Heap, aktuelle Fahrten der aktiven Profile |
| GET | `/api/stations?q=Name` | Stationssuche |
| GET | `/api/scan` | WLAN-Scan |
| POST | `/api/refresh` | Sofortiger Neuabruf |
| POST | `/api/reboot` | Neustart |
| POST | `/update` | Firmware-Upload (multipart) |

## Hinweise

- Die DB-Schnittstelle ist inoffiziell und kann sich jederzeit ändern. Das Abfrageintervall sollte nicht unter 60 s liegen (Minimum 30 s).
- Meldet die Tafel dauerhaft „Antwort ungueltig“ (im seriellen Log `HTTP status 452 OPS_BLOCKED`), sperrt der Akamai-Schutz der DB die eigene Adresse. Dann im Web-Frontend unter *WLAN* „IPv6 verwenden“ abschalten (oder umgekehrt) und ggf. eine neue öffentliche IP holen. Eine neue IPv4 ändert die IPv6-Adresse in der Regel nicht.
- Ein Zeitfenster über Mitternacht (z.B. 22:00–02:00) wird am jeweils aktuellen Wochentag geprüft.
- Ein Richtungsfilter pro Profil zeigt nur Fahrten mit passendem Ziel (bei Ankünften: Herkunft). Gefiltert wird nach dem Endziel, nicht nach Zwischenhalten. `GET /api/targets?id=<Stations-ID>&type=dep|arr&products=<Maske>` liefert die aktuell angefahrenen Ziele zur Auswahl.

## Lizenz

GPL-3.0, siehe [LICENSE](LICENSE). Verwendet wird der Fork [baycom/DBAPI](https://github.com/baycom/DBAPI/tree/fix-arrival-origin) von [ArduinoHannover/DBAPI](https://github.com/ArduinoHannover/DBAPI) (ebenfalls GPL-3.0) mit einer Korrektur: Bei Ankünften wird aus dem Objekt `abgangsOrt` nur der Name übernommen.
