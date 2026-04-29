#pragma once
/**
 * @file webserver.h
 * @brief WiFi-Management, Webserver und LMS-Polling
 *
 * Dieses Modul verwaltet die WiFi-Verbindung (Client + AP-Fallback), stellt
 * einen Async-Webserver mit REST-API und Webinterface bereit und führt das
 * periodische LMS-Polling auf Core 0 durch.
 *
 * Kernaufgaben:
 * - WiFi-State-Machine: DISCONNECTED → CONNECTING → CONNECTED / AP
 * - Captive Portal im AP-Mode (DNS-Umleitung)
 * - REST-API-Endpunkte für Status, Cover, Steuerung, Konfiguration
 * - OTA-Update (ArduinoOTA + Web-Upload)
 * - LMS-Status-Polling (alle 50 ms auf Core 0)
 * - Sync-Volume-Reconciliation für Multiroom-Gruppen
 * - Factory-Reset (NVS löschen + Neustart)
 */
#include "squeezeapi.h"

/// WiFi-Zustandsautomat
enum WifiState {
    WIFI_STATE_CONNECTING,   ///< Verbindungsaufbau läuft
    WIFI_STATE_CONNECTED,    ///< Mit WLAN verbunden
    WIFI_STATE_AP,           ///< Access-Point-Modus (Squeezie-Setup)
    WIFI_STATE_DISCONNECTED, ///< Keine Verbindung
};

/**
 * @brief Initialisiert WiFi und startet den Webserver.
 *
 * Versucht, mit gespeicherten WLAN-Credentials zu verbinden. Falls keine
 * Credentials vorhanden oder Verbindung fehlschlägt, wird der AP-Modus
 * aktiviert ("Squeezie-Setup").
 */
void webserverInit();

/**
 * @brief Webserver-Loop (in loop() aufrufen).
 *
 * Überwacht den WiFi-Zustand, bedient DNS im AP-Modus, führt ggf.
 * LMS-Polling durch und aktualisiert die Sync-Volume-Reconciliation.
 */
void webserverLoop();

/// @return Aktueller WiFi-Zustand.
WifiState wifiGetState();

/// @return Aktuelle IP-Adresse (leerer String bei AP/Disconnected).
String    wifiGetIP();

/**
 * @brief Gibt eine Kopie des gecachten Player-Status zurück (thread-safe).
 * @return SqueezieStatus-Snapshot.
 */
SqueezieStatus squeezieGetCachedStatus();

/**
 * @brief Aktualisiert den gecachten Status (wird nach LMS-Poll aufgerufen).
 * @param status Neuer Status aus squeezeGetStatus().
 */
void           wifiUpdateCachedStatus(const SqueezieStatus& status);

/// @return MAC-Adresse des aktuell ausgewählten Players.
String         wifiGetPlayerMac();

/// @return Scroll-Geschwindigkeit in ms/Pixel (aus NVS).
int            wifiGetScrollSpeed();

/// @return true wenn Sync-Volume aktiviert ist.
bool           wifiGetSyncVolume();

/**
 * @brief Setzt und speichert die LMS-Server-Adresse in NVS.
 * @param host Hostname oder IP.
 * @param port Port (Standard 9000).
 */
void wifiSetServer(const String& host, uint16_t port);

/**
 * @brief Setzt und speichert den aktiven Player in NVS.
 * @param mac MAC-Adresse des Players.
 */
void wifiSetPlayer(const String& mac);

/**
 * @brief Aktiviert/deaktiviert Sync-Volume für Multiroom-Gruppen.
 * @param enabled true = Sync-Volume aktiv.
 */
void wifiSetSyncVolume(bool enabled);

/// @return Gespeicherte Playlist-ID.
String wifiGetPlaylistId();

/// @return Gespeicherter Playlist-Name.
String wifiGetPlaylistName();

/**
 * @brief Setzt und speichert die aktive Playlist.
 * @param playlistId   LMS-interne ID.
 * @param playlistName Anzeigename.
 * @param loadNow      true = Playlist sofort auf Player laden.
 * @return true bei Erfolg.
 */
bool wifiSetPlaylist(const String& playlistId, const String& playlistName,
                     bool loadNow = true);

/// @brief Erzwingt eine sofortige LMS-Statusabfrage.
void wifiForceRepoll();

/// @brief Markiert den Beginn eines Nutzerbefehls und pausiert Hintergrund-Polls kurz.
void wifiBeginUserCommand();

/// @brief Beendet einen Nutzerbefehl und startet optional einen schnellen Statusabgleich.
void wifiEndUserCommand(bool forceRepoll = true);

/// @brief Führt eine sofortige LMS-Statusabfrage durch.
bool wifiPollNow();

/**
 * @brief Konsumiert den aktuellen OTA-Fortschritt.
 * @param[out] pct Fortschritt in Prozent (0–100).
 * @return true wenn sich der Fortschritt geändert hat.
 */
bool wifiConsumeOtaProgress(int& pct);

/// @return true wenn ein OTA-Update aktiv ist.
bool wifiIsOtaActive();

/**
 * @brief Schreibt eine formatierte Nachricht in den Webserver-Log-Ringbuffer.
 *
 * Der Ringbuffer hat 80 Einträge à 128 Zeichen und ist thread-safe
 * (portMUX_TYPE). Der Log ist unter /log abrufbar.
 *
 * @param fmt printf-ähnlicher Format-String.
 * @param ... Format-Argumente.
 */
void srvLog(const char* fmt, ...);

/**
 * @brief Führt einen Factory-Reset durch.
 *
 * Löscht alle NVS-Daten (WiFi, Server, Player, Kalibrierung) und startet
 * das Gerät neu. Sollte nur nach Bestätigung oder bei BOOT-Button-Hold
 * aufgerufen werden.
 */
void wifiFactoryReset();
