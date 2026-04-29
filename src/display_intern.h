#pragma once
/**
 * @file display_intern.h
 * @brief Interne Deklarationen für das Display-Modul
 *
 * Dieses Header stellt die gemeinsam genutzten Variablen und Funktionen
 * zwischen den aufgeteilten display-*.cpp-Dateien bereit.
 * Wird nicht von main.cpp oder anderen Modulen inkludiert.
 */
#include "display.h"
#include "webserver.h"
#include "squeezeapi.h"
#include "config.h"
#include <Preferences.h>
#include <SPI.h>
#include <HTTPClient.h>
#include <LittleFS.h>
#include <time.h>

// ─── XPT2046 direkt über SPI ────────────────────────────────────────────────
// LovyanGFX-Touch-Treiber inkompatibel mit Arduino-ESP32 v3.x auf ESP32-S3.
// ILI9488 tristatet MISO nicht → pin_miso=-1 in LGFX_setup.h → MISO frei für Touch.

extern SPIClass touchSPI;
extern bool     touchSPIinited;

// Kalibrierungsdaten (aus NVS geladen oder Standardwerte)
extern int16_t  cal_x0,  cal_x1;   // Rohwert links/rechts
extern int16_t  cal_y0,  cal_y1;   // Rohwert oben/unten
extern uint8_t  cal_swap;          // 1 = XPT-X/Y-Kanäle getauscht
extern bool     cal_valid;

// Letzte Rohwerte (für Diagnoseanzeige)
extern int32_t g_touch_raw_x;
extern int32_t g_touch_raw_y;

// ─── Screen-State ────────────────────────────────────────────────────────────
enum Screen { SCREEN_NONE, SCREEN_STARTUP, SCREEN_PLAYER, SCREEN_SETTINGS, SCREEN_PLAYLIST };
extern Screen g_screen;

// ─── Player-Bildschirm Layout-Konstanten ─────────────────────────────────────
#define PL_HDR_Y      0
#define PL_HDR_H      30
#define PL_COVER_X    4
#define PL_COVER_Y    32
#define PL_COVER_W    128
#define PL_COVER_H    128
#define PL_INFO_X     136
#define PL_INFO_Y     32
#define PL_INFO_W     340
#define PL_PROG_Y     164
#define PL_PROG_H     26
#define PL_TRANS_Y    192
#define PL_TRANS_H    72
#define PL_TRANS_CY   (PL_TRANS_Y + PL_TRANS_H / 2)
#define PL_VOL_Y      264
#define PL_VOL_H      26
#define PL_MODE_Y     290
#define PL_MODE_H     30
#define PL_VOL_BTN_W  40
#define PL_VOL_MINUS_X 4
#define PL_VOL_BAR_X  48
#define PL_VOL_PLUS_X 392
#define PL_VOL_MUTE_X 436
#define PL_VOL_BAR_W  (PL_VOL_PLUS_X - PL_VOL_BAR_X - 4)

// Settings-Screen Layout
#define SET_BOX_X      4
#define SET_BOX_W      336
#define SET_BTN_X      344
#define SET_BTN_W      132
#define SET_SRV_Y      50
#define SET_DISC_Y     90
#define SET_SYNC_Y     130
#define SET_PLR_Y0     194
#define SET_PLR_H      38
#define SET_PLR_GAP    3
#define SET_LISTE_X    340
#define SET_LISTE_W    74

// Farben (RGB888, 24-Bit für LovyanGFX uint32_t-Interface)
#define COL_BG        ((uint32_t)0x000000)
#define COL_HDR       ((uint32_t)0x1E1E1E)
#define COL_DARK      ((uint32_t)0x2E2E2E)
#define COL_MID       ((uint32_t)0x555555)
#define COL_PROG_BG   ((uint32_t)0x1E1E1E)
#define COL_GREEN     ((uint32_t)0x00E000)
#define COL_ACTIVE    ((uint32_t)0x005500)
#define COL_WHITE     ((uint32_t)0xFFFFFF)
#define COL_LGREY     ((uint32_t)0xBBBBBB)
#define COL_MGREY     ((uint32_t)0x888888)
#define COL_DGREY     ((uint32_t)0x606060)
#define COL_YELLOW    ((uint32_t)0xFFCC00)

// Player-Screen Zustand (selektives Neuzeichnen)
extern String  g_dTitle, g_dArtist, g_dAlbum, g_dCoverUrl, g_dCoverKey, g_dPlayer;
extern String  g_dTitleD, g_dArtistD, g_dAlbumD;
extern bool    g_dPlaying, g_dMuted;
extern int     g_dVolume, g_dRepeat, g_dShuffle;
extern int     g_dPosition, g_dDuration, g_dTrackNum;
extern int     g_lmsPos;
extern int     g_localPos;
extern uint32_t g_localPosMs;
extern int     g_lastProgFilled;
extern int     g_lastVolFilled;
extern bool    g_lastVolMuted;
extern int     g_lastClockMin;

// Settings screen state
extern std::vector<SqueeziePlayer>   g_settPlayers;
extern std::vector<SqueeziePlaylist> g_settPlaylists;
extern String   g_settDiscHost;
extern uint16_t g_settDiscPort;
extern bool     g_settDiscFound;
extern bool     g_settSearched;

// Scroll-State
extern int32_t g_scrollOff[3];
extern uint32_t g_scrollLast;
extern LGFX_Sprite g_infoLineSprite;

// Cover-Puffer (PSRAM) und Download-State
extern uint8_t*          g_coverBuf;
extern uint8_t*          g_coverDlBuf;
extern volatile bool     g_coverReady;
extern volatile size_t   g_coverLen;
extern volatile uint32_t g_coverGen;
extern volatile uint32_t g_dlGen;
extern volatile uint32_t g_coverSerial;
extern String            g_pendingCoverUrl;
extern String            g_pendingCoverKey;
extern String            g_dispatchedCoverUrl;
extern String            g_dispatchedCoverKey;
extern String            g_loadedCoverKey;
extern volatile uint32_t g_coverRetryAt;
extern volatile uint32_t g_coverTaskStartedAt;
extern TaskHandle_t      g_coverTask;
extern SemaphoreHandle_t g_coverMux;
extern const uint32_t COVER_RETRY_DELAY_MS;
extern const uint32_t COVER_TASK_TIMEOUT_MS;

// Prefetch-Slots
struct PrefetchSlot {
    uint8_t* buf   = nullptr;
    size_t   len   = 0;
    bool     ready = false;
    String   url;
    String   key;
};
extern PrefetchSlot      g_pf[SQUEEZE_PREFETCH];
extern String            g_pfQueue[SQUEEZE_PREFETCH];
extern String            g_pfQueueKeys[SQUEEZE_PREFETCH];
extern int               g_pfQueueCount;
extern SemaphoreHandle_t g_pfMux;
extern TaskHandle_t      g_pfTask;

// Cover-Candidate (Stabilisierung über 2 Polls)
extern String g_coverDrawnForTitle;
extern bool   g_coverPredrawn;
extern String g_coverCandidateUrl;
extern String g_coverCandidateKey;
extern uint8_t g_coverCandidateCount;

// ─── Interne Funktionsdeklarationen ──────────────────────────────────────────

// display_draw.cpp
void     drawButton(int16_t x, int16_t y, int16_t w, int16_t h,
                    uint32_t bgColor, uint32_t txtColor, const char* label,
                    bool highlighted = false);
void     drawCrosshair(int16_t x, int16_t y);
void     drawCircleBtn(int16_t cx, int16_t cy, int16_t r,
                       uint32_t bg, uint32_t fg, const char* icon);
void     drawVolumeIcon(int16_t x, int16_t y, int16_t w, int16_t h,
                        uint32_t bg, uint32_t fg, bool louder,
                        uint32_t border = COL_WHITE);
void     drawMuteIcon(int16_t x, int16_t y, int16_t w, int16_t h, bool muted);
void     drawRepeatGlyph(int16_t x, int16_t y, uint32_t fg);
void     drawShuffleGlyph(int16_t x, int16_t y, uint32_t fg);
void     drawInfoLine(const char* txt, int16_t x, int16_t y, int16_t w,
                      uint32_t color, uint8_t size);
void     drawScrollLine(const char* txt, int16_t x, int16_t y, int16_t w,
                        uint32_t color, uint8_t size, int32_t offset);
void     drawPlayerScrollLine(const char* txt, int16_t x, int16_t y, int16_t w,
                              uint32_t color, uint8_t size, int32_t offset, bool bold = false);
void     drawPlayerHeader(const char* playerName, bool playing);
void     drawCoverPlaceholder();
void     drawCoverImage(const uint8_t* buf, size_t len);
void     drawProgressBar(int position, int duration);
void     drawTransportButtons(bool playing);
void     drawVolumeButtons(bool muted);
void     drawVolumeRow(int volume, bool muted);
void     drawModeRow(int repeat, int shuffle);
String   utf8ToAscii(const String& src);
void     setPlayerInfoFont(uint8_t size);
int      getPlayerInfoLineHeight(uint8_t size);
bool     ensureInfoLineSprite(int height);

// display_touch.cpp
bool     xpt2046ReadRaw(int32_t* rx, int32_t* ry);
bool     xpt2046Read(int32_t* x, int32_t* y);
bool     calWaitTouch(int32_t* rx, int32_t* ry);
void     runCalibration();

// display_cover.cpp
bool     isCompleteJpeg(const uint8_t* buf, size_t len);
size_t   readHttpBody(HTTPClient& http, uint8_t* buf, size_t maxLen, uint32_t timeoutMs);
bool     coverTaskTimedOut(uint32_t now, uint32_t startedAt);
void     coverDlTask(void*);
void     prefetchTask(void*);

// display_screens.cpp
void     drawPlaylistList();
void     drawPlaylistScreen();
void     handlePlaylistTouch(int32_t tx, int32_t ty);
void     drawSettingsSrvRow();
void     drawSettingsDiscRow();
void     drawSettingsSyncRow();
void     drawSettingsPlayerList();
void     drawSettingsScreen();
void     handleSettingsTouch(int32_t tx, int32_t ty);
