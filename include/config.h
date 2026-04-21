#pragma once

// =============================================================================
// Squeezie — Zentrale Konfiguration
// =============================================================================

// --- Anwendung ---
#define APP_VERSION_BASE "0.4.3"
#if __has_include("version_generated.h")
#include "version_generated.h"
#endif
#ifndef APP_VERSION
#define APP_VERSION APP_VERSION_BASE
#endif

// --- Pins ---
#define PIN_TFT_BL      7   // Hintergrundbeleuchtung (HIGH = an)
#define PIN_TOUCH_CS    4   // Touch Chip-Select (TCS)
#define PIN_TOUCH_IRQ   5   // Touch-Interrupt (PEN, LOW = berührt, nicht angeschlossen)
#define PIN_BOOT_BTN    0   // Eingebauter BOOT-Button (LOW = gedrückt)
#define PIN_PLAY_BTN    1   // Externer Play/Pause-Taster (LOW = gedrückt)

// --- Display ---
#define DISPLAY_ROTATION    1   // 1 = Querformat: 480×320
#define DISPLAY_WIDTH     480
#define DISPLAY_HEIGHT    320

// --- NVS ---
#define NVS_NAMESPACE        "squeezie"
#define NVS_KEY_CAL_OK       "calOK"    // bool: Kalibrierung gültig
#define NVS_KEY_CAL_X0       "calX0"    // int16: Rohwert Bildschirm-X am linken Zielpunkt
#define NVS_KEY_CAL_X1       "calX1"    // int16: Rohwert Bildschirm-X am rechten Zielpunkt
#define NVS_KEY_CAL_Y0       "calY0"    // int16: Rohwert Bildschirm-Y am oberen Zielpunkt
#define NVS_KEY_CAL_Y1       "calY1"    // int16: Rohwert Bildschirm-Y am unteren Zielpunkt
#define NVS_KEY_CAL_SWAP     "calSwap"  // uint8: 1 = X/Y-Kanäle getauscht

// --- Kalibrierungszielpunkte (Bildschirmkoordinaten) ---
#define CAL_X_LEFT    30
#define CAL_X_RIGHT   (DISPLAY_WIDTH  - 30)
#define CAL_Y_TOP     30
#define CAL_Y_BOTTOM  (DISPLAY_HEIGHT - 30)

// --- WiFi / Netzwerk (NVS-Schlüssel) ---
#define NVS_KEY_WIFI_SSID   "wifiSSID"
#define NVS_KEY_WIFI_PASS   "wifiPass"
#define NVS_KEY_NET_DHCP    "netDHCP"
#define NVS_KEY_NET_IP      "netIP"
#define NVS_KEY_NET_MASK    "netMask"
#define NVS_KEY_NET_GW      "netGW"
#define NVS_KEY_NET_DNS     "netDNS"
#define NVS_KEY_NET_NTP     "netNTP"

// --- Squeezebox Server (NVS-Schlüssel) ---
#define NVS_KEY_SRV_HOST    "srvHost"
#define NVS_KEY_SRV_PORT    "srvPort"
#define NVS_KEY_PLAYER_MAC  "playerMAC"
#define NVS_KEY_SYNC_VOL    "syncVol"
#define NVS_KEY_SCROLL_SPD  "scrollSpd"
#define NVS_KEY_PL_ID       "plId"
#define NVS_KEY_PL_NAME     "plName"

// --- WiFi AP ---
#define WIFI_AP_SSID             "Squeezie-Setup"
#define WIFI_CONNECT_TIMEOUT_MS  8000
#define LYRION_DEFAULT_PORT      9000
