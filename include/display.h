#pragma once
/**
 * @file display.h
 * @brief Display-Modul – Touch-Controller, Cover-Download, Screen-Management
 *
 * Zentrale Schnittstelle für das ILI9488-TFT-Display (480×320) mit
 * XPT2046-Touch-Controller. Verwaltet mehrere Screens (Startup, Player,
 * Settings, Playlist), asynchrone Cover-Downloads via FreeRTOS-Task auf
 * Core 0, ein Prefetch-System für die nächsten Tracks sowie die
 * Touch-Kalibrierung.
 *
 * Die Display-Instanz (@ref tft) ist global und wird in display.cpp definiert.
 */
#include "LGFX_setup.h"
#include <vector>

/// Globale LovyanGFX-Display-Instanz (definiert in display.cpp)
extern LGFX tft;

/**
 * @brief Initialisiert Display, Hintergrundbeleuchtung und Touch-Kalibrierung.
 *
 * Lädt Kalibrierungsdaten aus NVS; falls keine gültigen Daten vorhanden,
 * wird die interaktive 3-Punkt-Kalibrierung gestartet.
 */
void displayInit();

/**
 * @brief Zeigt den Startup-Screen mit Logo, Version und IP-Adresse.
 * @param version Versionsstring (z. B. "v0.4.5")
 * @param ip      IP-Adresse oder leerer String ("") für "Verbinde..."
 */
void displayShowStartup(const char* version, const char* ip);

/**
 * @brief Zeichnet den Player-Screen einmalig vollständig.
 *
 * Nach erfolgreicher WiFi-Verbindung aufrufen. Enthält Cover, Info-Text,
 * Progress-Bar, Transport-Buttons, Volume-Regler und Mode-Toggles.
 */
void displayDrawPlayerScreen();

/**
 * @brief Aktualisiert geänderte Felde des Player-Screens (selektives Redraw).
 *
 * Soll in loop() aufgerufen werden. Vergleicht den aktuellen Status mit
 * dem letzten gezeichneten Stand und zeichnet nur geänderte Bereiche neu.
 */
void displayUpdatePlayer();

/**
 * @brief Wertet Touch-Ereignisse aus und führt die entsprechende Aktion aus.
 *
 * Soll in loop() nach displayUpdatePlayer() aufgerufen werden, damit
 * der SPI-DMA des Displays abgeschlossen ist. Unterstützt Transport-Buttons,
 * Volume-Slider, Mode-Toggles, Seek, Settings-Navigation und Playlist.
 */
void displayHandleTouch();

/**
 * @brief Zeigt den OTA-Fortschrittsbalken an.
 * @param pct Fortschritt in Prozent (0–100); bei 100 wird "Neustart..." angezeigt.
 */
void displayShowOTA(int pct);

/**
 * @brief Zeigt die Factory-Reset-Warnung mit Countdown.
 * @param secsLeft Verbleibende Sekunden (10..1); 0 = Reset wird ausgeführt.
 */
void displayShowFactoryReset(uint8_t secsLeft);

/**
 * @brief Kopiert das aktuelle Cover thread-sicher in einen Puffer.
 * @param[out] out Zielvektor für die JPEG-Rohdaten.
 * @return true wenn ein Cover verfügbar war, false sonst.
 */
bool           displayCopyCover(std::vector<uint8_t>& out);

/**
 * @brief Gibt eine fortlaufende Seriennummer des aktuellen Covers zurück.
 * @return uint32_t Seriennummer (ändert sich bei jedem neuen Cover).
 */
uint32_t       displayGetCoverSerial();

/**
 * @brief Startet einen asynchronen Cover-Download (aus beliebigem Task).
 * @param url     Vollständige HTTP-URL zum Cover-JPEG.
 * @param cacheKey Optionaler Cache-Key für LittleFS-Persistenz.
 *
 * Kann aus dem lmsPoll-Task auf Core 0 aufgerufen werden.
 */
void displayPreloadCover(const String& url, const String& cacheKey = "");

/**
 * @brief Befüllt die Prefetch-Queue mit Cover-URLs der nächsten Tracks.
 * @param urls  Array von Cover-URLs (Länge count).
 * @param keys  Array von Cache-Keys (Länge count).
 * @param count Anzahl der Einträge (max SQUEEZE_PREFETCH).
 *
 * Die Cover werden sequenziell auf Core 0 vorgeladen.
 */
void displayQueuePrefetch(const String* urls, const String* keys, int count);
