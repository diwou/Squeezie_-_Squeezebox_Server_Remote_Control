#pragma once
#include "squeezeapi.h"

enum WifiState {
    WIFI_STATE_CONNECTING,
    WIFI_STATE_CONNECTED,
    WIFI_STATE_AP,
    WIFI_STATE_DISCONNECTED,
};

void            webserverInit();
void            webserverLoop();
WifiState       wifiGetState();
String          wifiGetIP();

// Letzter gecachter Player-Status (für Display-Modul)
SqueezieStatus        squeezieGetCachedStatus(); // gibt Snapshot zurück (thread-safe)
String                wifiGetPlayerMac();
int                   wifiGetScrollSpeed(); // Scroll-Geschwindigkeit in ms/Pixel (aus NVS)
void                  wifiSetServer(const String& host, uint16_t port); // setzt + speichert in NVS
void                  wifiSetPlayer(const String& mac);                 // setzt + speichert in NVS

// Webserver-Log (Ring-Buffer, abrufbar unter /log)
void                  srvLog(const char* fmt, ...);

// NVS komplett löschen und neu starten (Factory Reset)
void                  wifiFactoryReset();
