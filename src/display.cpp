#include "display_intern.h"

// ─── Globale Display-Instanz ─────────────────────────────────────────────────
LGFX tft;

// ─── XPT2046 direkt über SPI ────────────────────────────────────────────────
SPIClass touchSPI(FSPI);
bool     touchSPIinited = false;

// Kalibrierungsdaten (aus NVS geladen oder Standardwerte)
int16_t  cal_x0   = 300,  cal_x1   = 3800;
int16_t  cal_y0   = 300,  cal_y1   = 3800;
uint8_t  cal_swap = 0;
bool     cal_valid = false;

// Letzte Rohwerte (für Diagnoseanzeige)
int32_t g_touch_raw_x = 0;
int32_t g_touch_raw_y = 0;

// ─── Screen-State ────────────────────────────────────────────────────────────
Screen g_screen = SCREEN_NONE;

// ─── Player-Screen Zustand (selektives Neuzeichnen) ─────────────────────────
String  g_dTitle, g_dArtist, g_dAlbum, g_dCoverUrl, g_dCoverKey, g_dPlayer;
String  g_dTitleD, g_dArtistD, g_dAlbumD;
bool    g_dPlaying  = false, g_dMuted = false;
int     g_dVolume   = -1, g_dRepeat  = -1, g_dShuffle = -1;
int     g_dPosition = -1, g_dDuration = 0, g_dTrackNum = -1;
int     g_lmsPos   = -1;
int     g_localPos  = 0;
uint32_t g_localPosMs = 0;
int     g_lastProgFilled = -1;
int     g_lastVolFilled  = -1;
bool    g_lastVolMuted   = false;
int     g_lastClockMin   = -2;

// ─── Settings screen state ───────────────────────────────────────────────────
std::vector<SqueeziePlayer>   g_settPlayers;
std::vector<SqueeziePlaylist> g_settPlaylists;
String   g_settDiscHost;
uint16_t g_settDiscPort  = 0;
bool     g_settDiscFound = false;
bool     g_settSearched  = false;

// ─── Scroll-State ────────────────────────────────────────────────────────────
int32_t g_scrollOff[3]   = {0, 0, 0};
uint32_t g_scrollLast    = 0;
LGFX_Sprite g_infoLineSprite(&tft);

// ─── Cover-Puffer (PSRAM) und Download-State ─────────────────────────────────
uint8_t*          g_coverBuf      = nullptr;
uint8_t*          g_coverDlBuf    = nullptr;
volatile bool     g_coverReady    = false;
volatile size_t   g_coverLen      = 0;
volatile uint32_t g_coverGen      = 0;
volatile uint32_t g_dlGen         = 0;
volatile uint32_t g_coverSerial   = 0;
String            g_pendingCoverUrl;
String            g_pendingCoverKey;
String            g_dispatchedCoverUrl;
String            g_dispatchedCoverKey;
String            g_loadedCoverKey;
volatile uint32_t g_coverRetryAt   = 0;
volatile uint32_t g_coverTaskStartedAt = 0;
TaskHandle_t      g_coverTask     = nullptr;
SemaphoreHandle_t g_coverMux      = nullptr;
const uint32_t COVER_RETRY_DELAY_MS = 15000;
const uint32_t COVER_TASK_TIMEOUT_MS = 12000;

// ─── Prefetch-Slots ──────────────────────────────────────────────────────────
PrefetchSlot      g_pf[SQUEEZE_PREFETCH];
String            g_pfQueue[SQUEEZE_PREFETCH];
String            g_pfQueueKeys[SQUEEZE_PREFETCH];
int               g_pfQueueCount  = 0;
SemaphoreHandle_t g_pfMux         = nullptr;
TaskHandle_t      g_pfTask        = nullptr;

// ─── Cover-Candidate (Stabilisierung über 2 Polls) ──────────────────────────
String g_coverDrawnForTitle;
bool   g_coverPredrawn = false;
String g_coverCandidateUrl;
String g_coverCandidateKey;
uint8_t g_coverCandidateCount = 0;
