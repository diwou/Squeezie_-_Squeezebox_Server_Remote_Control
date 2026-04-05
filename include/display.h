#pragma once
#include "LGFX_setup.h"

// Globale Display-Instanz (definiert in display.cpp)
extern LGFX tft;

// Initialisierung: Display-Init, Helligkeit, Touch-Kalibrierung laden/starten
void displayInit();

// Startup-Screen: Logo, Version, IP (leerer String = "Verbinde...")
void displayShowStartup(const char* version, const char* ip);

// Player-Screen: einmalig zeichnen (nach WiFi-Verbindung aufrufen)
void displayDrawPlayerScreen();

// Player-Screen: geänderte Felder aus gecachtem Status neu zeichnen (in loop() aufrufen)
void displayUpdatePlayer();

// Touch-Ereignisse auswerten (in loop() aufrufen)
void displayHandleTouch();

// OTA-Fortschritt anzeigen (0–100); ruft bei 100 "Neustart..." an
void displayShowOTA(int pct);

// Factory-Reset-Warnung: secsLeft = verbleibende Sekunden (10..1), 0 = wird ausgeführt
void displayShowFactoryReset(uint8_t secsLeft);

// Cover-Puffer (PSRAM) für /api/cover — nullptr wenn noch kein Cover geladen
const uint8_t* displayGetCoverBuf();
size_t         displayGetCoverLen();

// Cover-Download aus beliebigem Task starten (z.B. lmsPoll auf Core 0)
void displayPreloadCover(const String& url);

// Prefetch-Queue befüllen: Cover der nächsten Tracks vorladen (Core 0, sequenziell)
void displayQueuePrefetch(const String* urls, int count);
