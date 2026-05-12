#pragma once

// =============================================================================
// Squeezie — Zentrale Konfiguration
// =============================================================================

// --- Anwendung ---
#define APP_VERSION_BASE "0.5.0"
#if __has_include("version_generated.h")
#include "version_generated.h"
#endif
#ifndef APP_VERSION
#define APP_VERSION APP_VERSION_BASE
#endif

// --- Pins ---
#define PIN_TFT_BL      9   // Hintergrundbeleuchtung (HIGH = an)
#define PIN_TFT_CS      17  // Display Chip-Select
#define PIN_TFT_DC      8   // Display Data/Command
#define PIN_TFT_RST     18  // Display Reset
#define PIN_TFT_MOSI    3   // Display SDI / MOSI (SPI)
#define PIN_TFT_MISO    10  // Display SDO / MISO (SPI, optional read-back)
#define PIN_TFT_SCLK    46  // Display SCK (SPI)
#define PIN_TOUCH_CS    12  // Touch Chip-Select (TCS)
#define PIN_TOUCH_IRQ   21  // Touch-Interrupt (PEN, LOW = berührt)
#define PIN_TOUCH_MOSI  13  // Touch T_DIN / MOSI (SPI)
#define PIN_TOUCH_MISO  14  // Touch T_DOUT / MISO (SPI)
#define PIN_TOUCH_SCLK  11  // Touch T_CLK / SCK (SPI)
#define PIN_BOOT_BTN    0   // Eingebauter BOOT-Button (LOW = gedrückt)
#define PIN_PLAY_BTN    15  // Externer Play/Pause-Taster (LOW = gedrückt)
#define PIN_PREV_BTN    16  // Externer Track-zurueck-Taster (LOW = gedrückt)
#define PIN_NEXT_BTN    7   // Externer Track-vor-Taster (LOW = gedrückt)
#define PIN_VOL_UP_BTN  4   // Externer Volume-Plus-Taster (LOW = gedrückt)
#define PIN_VOL_DN_BTN  6   // Externer Volume-Minus-Taster (LOW = gedrückt)
#define PIN_MUTE_BTN    5   // Externer Mute-Taster (LOW = gedrückt)

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

// --- OTA / Sicherheit ---
#define OTA_HOSTNAME             "squeezie"
#define OTA_PASSWORD             "squeezie"
