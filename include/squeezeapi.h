#pragma once
/**
 * @file squeezeapi.h
 * @brief Lyrion/Squeezebox JSON-RPC-API – Kommunikation mit dem LMS-Server
 *
 * Stellt Funktionen zur Steuerung eines Lyrion (ehemals Logitech Media Server)
 * bereit. Die Kommunikation erfolgt per HTTP-POST an die JSON-RPC-Schnittstelle
 * des Servers. Zusätzlich wird eine UDP-basierte Server-Discovery unterstützt.
 *
 * @note Alle Netzwerkfunktionen sind blockierend; Timeouts sind in den
 *       Implementierungen konfiguriert (Standard 1200 ms, Status 800 ms).
 */

#include <Arduino.h>
#include <vector>
// cspell:ignore slimproto

/**
 * @struct SqueeziePlayer
 * @brief Repräsentiert einen Squeezebox-Player im Netzwerk.
 */
struct SqueeziePlayer {
    String mac;        ///< MAC-Adresse des Players (z. B. "aa:bb:cc:dd:ee:ff")
    String name;       ///< Anzeigename des Players
    bool   connected;  ///< true wenn der Player mit dem LMS verbunden ist
};

/**
 * @struct SqueeziePlaylist
 * @brief Repräsentiert eine Playlist auf dem LMS-Server.
 */
struct SqueeziePlaylist {
    String id;           ///< LMS-interne ID der Playlist
    String name;         ///< Anzeigename der Playlist
    int    trackCount;   ///< Anzahl der Titel in der Playlist
};

/**
 * @struct SqueezieStatus
 * @brief Gecachter Status eines Players (wird periodisch vom LMS abgefragt).
 */
struct SqueezieStatus {
    bool   playing     = false;  ///< true wenn Wiedergabe läuft
    String title;                 ///< Aktueller Titelname
    String artist;                ///< Interpret
    String album;                 ///< Albumname
    int    trackNum    = 0;       ///< Titelnummer im aktuellen Album
    String coverUrl;              ///< Vollständige HTTP-URL zum Cover-JPEG
    String coverKey;              ///< Stabiler Track-Key für Cover-Download (verhindert unnötige Neuladung)
    int    duration    = 0;       ///< Gesamtdauer in Sekunden
    int    position    = 0;       ///< Aktuelle Position in Sekunden
    int    volume      = 0;       ///< Lautstärke 0–100
    bool   muted       = false;   ///< true wenn stummgeschaltet
    int    repeatMode  = 0;       ///< 0=aus, 1=Titel, 2=Playlist
    int    shuffleMode = 0;       ///< 0=aus, 1=Titel, 2=Album
    String playerName;            ///< Anzeigename des aktuellen Players
};

/**
 * @brief Setzt die Server-Adresse und speichert sie (NVS).
 * @param host Hostname oder IP-Adresse des LMS.
 * @param port Port des LMS (Standard 9000).
 */
void     squeezeSetServer(const String& host, uint16_t port);

/// @brief Gibt die aktuell konfigurierte LMS-Hostadresse zurück.
String   squeezeGetHost();

/// @brief Gibt den aktuell konfigurierten LMS-Port zurück.
uint16_t squeezeGetPort();

/**
 * @brief Ruft die Liste aller bekannten Player vom LMS ab.
 * @param[out] out Vektor mit SqueeziePlayer-Strukturen.
 * @return true bei Erfolg, false bei Fehler.
 */
bool squeezeGetPlayers(std::vector<SqueeziePlayer>& out);

/**
 * @brief Ruft die Playlist-Liste vom LMS ab.
 * @param[out] out Vektor mit SqueeziePlaylist-Strukturen.
 * @return true bei Erfolg, false bei Fehler.
 */
bool squeezeGetPlaylists(std::vector<SqueeziePlaylist>& out);

/**
 * @brief Ruft den vollständigen Player-Status inkl. Cover-URLs ab.
 * @param playerMac MAC-Adresse des Players.
 * @param[out] out  Ausführlicher Status (Titel, Cover, Position, Volume, Modi).
 * @return true bei Erfolg, false bei Fehler.
 */
bool squeezeGetStatus(const String& playerMac, SqueezieStatus& out);

/**
 * @brief Ermittelt alle Mitglieder einer Sync-Gruppe.
 * @param playerMac MAC-Adresse eines Gruppenmitglieds.
 * @param[out] out  Vektor mit MAC-Adressen aller Gruppenmitglieder.
 * @return true wenn mindestens ein weiteres Mitglied gefunden wurde.
 */
bool squeezeGetSyncGroupMembers(const String& playerMac, std::vector<String>& out);

// ─── Transport-Kommandos ─────────────────────────────────────────────────────

bool squeezePlay(const String& playerMac);       ///< Wiedergabe starten
bool squeezePause(const String& playerMac);      ///< Wiedergabe pausieren
bool squeezeNext(const String& playerMac);       ///< Nächster Titel
bool squeezePrev(const String& playerMac);       ///< Vorheriger Titel
bool squeezeSeek(const String& playerMac, int seconds); ///< An Position springen

// ─── Lautstärke ──────────────────────────────────────────────────────────────

bool squeezeSetVolume(const String& playerMac, int volume);       ///< Absolut 0–100
bool squeezeAdjustVolume(const String& playerMac, int delta);     ///< Relativ (+1, -5, …)
bool squeezeToggleMute(const String& playerMac);                  ///< Stummschaltung umschalten
bool squeezeSetMute(const String& playerMac, bool mute);          ///< Stummschaltung setzen
bool squeezeGetVolumeState(const String& playerMac, int& volume, bool& muted); ///< Aktuelle Lautstärke abfragen

// ─── Wiedergabemodi ──────────────────────────────────────────────────────────

bool squeezeSetRepeat(const String& playerMac, int mode);  ///< 0=aus, 1=Titel, 2=Playlist
bool squeezeSetShuffle(const String& playerMac, int mode); ///< 0=aus, 1=Titel, 2=Album

// ─── Playlists ───────────────────────────────────────────────────────────────

/**
 * @brief Lädt eine Playlist auf dem Player.
 * @param playerMac   MAC-Adresse des Players.
 * @param playlistId  LMS-interne ID der Playlist.
 * @param playlistName Optionaler Anzeigename (wird nicht an LMS gesendet).
 * @return true bei Erfolg.
 */
bool squeezeLoadPlaylist(const String& playerMac, const String& playlistId,
                         const String& playlistName = "");

// ─── Volume-Konvertierung ────────────────────────────────────────────────────

/**
 * @brief Konvertiert LMS-Volume ins UI-Format (0–100).
 * @param lmsVolume Rohwert vom LMS (kann negativ sein bei Mute).
 * @return Positiver UI-Volume-Wert (abs() für negative Werte).
 */
int  squeezeVolumeFromLms(int lmsVolume);

/**
 * @brief Konvertiert UI-Volume ins LMS-Format (1:1).
 * @param uiVolume UI-Volume 0–100.
 * @return Gleicher Wert für LMS.
 */
int  squeezeVolumeToLms(int uiVolume);

// ─── Server-Discovery ────────────────────────────────────────────────────────

/**
 * @brief Sucht einen Lyrion-Server im lokalen Netzwerk.
 *
 * Führt primär eine UDP-Discovery auf Port 3483 (Slim-Discovery-Protokoll)
 * per Broadcast aus. Falls dabei kein Treffer kommt, wird als Fallback
 * mDNS (`_slimproto._tcp`) abgefragt.
 *
 * @param[out] foundHost Gefundene IP-Adresse des Servers.
 * @param[out] foundPort Gefundener Port (Standard 9000).
 * @param timeoutMs Maximale Suchdauer in ms (Standard 3000).
 * @return true wenn ein Server gefunden wurde.
 */
bool squeezeDiscover(String& foundHost, uint16_t& foundPort, uint32_t timeoutMs = 3000);
