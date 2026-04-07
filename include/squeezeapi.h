#pragma once
#include <Arduino.h>
#include <vector>

#define SQUEEZE_PREFETCH 4   // Anzahl vorauszuladender Cover-Slots
#define SQUEEZE_VOL_MIN  6   // LMS-internes Minimum, das 0% in der UI entspricht

struct SqueeziePlayer {
    String mac;
    String name;
    bool   connected;
};

struct SqueezieStatus {
    bool   playing     = false;
    String title;
    String artist;
    String album;
    int    trackNum    = 0;
    String coverUrl;           // Cover-URL des aktuellen Titels
    String coverKey;           // Prefetch-/Cache-Key auf Basis der Track-Metadaten
    int    duration    = 0;
    int    position    = 0;
    int    volume      = 0;
    int    repeatMode  = 0;   // 0=aus, 1=Titel, 2=Playlist
    int    shuffleMode = 0;   // 0=aus, 1=Titel, 2=Album
    String playerName;
    String nextCoverUrls[SQUEEZE_PREFETCH]; // Cover-URLs der nächsten Tracks
    String nextCoverKeys[SQUEEZE_PREFETCH]; // Metadaten-Key der nächsten Tracks
    int    nextCount   = 0;
};

void     squeezeSetServer(const String& host, uint16_t port);
String   squeezeGetHost();
uint16_t squeezeGetPort();

bool squeezeGetPlayers(std::vector<SqueeziePlayer>& out);
bool squeezeGetStatus(const String& playerMac, SqueezieStatus& out);

bool squeezePlay(const String& playerMac);
bool squeezePause(const String& playerMac);
bool squeezeNext(const String& playerMac);
bool squeezePrev(const String& playerMac);
bool squeezeSeek(const String& playerMac, int seconds);
bool squeezeSetVolume(const String& playerMac, int volume);       // absolut 0–100
bool squeezeAdjustVolume(const String& playerMac, int delta);     // relativ (+1, -5, …)
bool squeezeSetRepeat(const String& playerMac, int mode);
bool squeezeSetShuffle(const String& playerMac, int mode);

int  squeezeVolumeFromLms(int lmsVolume); // LMS [6..100] -> UI [0..100]
int  squeezeVolumeToLms(int uiVolume);    // UI [0..100] -> LMS [6..100]

// UDP-Discovery: sucht Lyrion-Server im Netz (blockierend bis timeoutMs)
bool squeezeDiscover(String& foundHost, uint16_t& foundPort, uint32_t timeoutMs = 3000);
