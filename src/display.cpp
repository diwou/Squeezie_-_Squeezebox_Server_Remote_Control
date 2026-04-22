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

static SPIClass touchSPI(FSPI);
static bool     touchSPIinited = false;

// Kalibrierungsdaten (aus NVS geladen oder Standardwerte)
static int16_t  cal_x0   = 300,  cal_x1   = 3800; // Rohwert links/rechts
static int16_t  cal_y0   = 300,  cal_y1   = 3800; // Rohwert oben/unten
static uint8_t  cal_swap = 0;                       // 1 = XPT-X/Y-Kanäle getauscht
static bool     cal_valid = false;

// Letzte Rohwerte (für Diagnoseanzeige)
int32_t g_touch_raw_x = 0;
int32_t g_touch_raw_y = 0;

// ─── Screen-State ────────────────────────────────────────────────────────────
enum Screen { SCREEN_NONE, SCREEN_STARTUP, SCREEN_PLAYER, SCREEN_SETTINGS, SCREEN_PLAYLIST };
static Screen g_screen = SCREEN_NONE;

// ─── Player-Bildschirm Layout-Konstanten ─────────────────────────────────────
// Gesamt 480×320. Cover 128×128 neben Info. Unten: Progress/Transport/Vol/Mode.
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
#define SET_LISTE_X    340   // "LISTE"-Button im Settings-Header
#define SET_LISTE_W    74

// Farben (RGB888, 24-Bit für LovyanGFX uint32_t-Interface)
// WICHTIG: (uint32_t)-Cast erforderlich — LovyanGFX behandelt int-Literale anders als uint32_t
#define COL_BG        ((uint32_t)0x000000)  // Schwarz
#define COL_HDR       ((uint32_t)0x1E1E1E)  // Sehr dunkelgrau Header
#define COL_DARK      ((uint32_t)0x2E2E2E)  // Dunkelgrau Button-Hintergrund
#define COL_MID       ((uint32_t)0x555555)  // Mittleres Grau (Border)
#define COL_PROG_BG   ((uint32_t)0x1E1E1E)  // Dunkelgrau Fortschrittsbalken
#define COL_GREEN     ((uint32_t)0x00E000)  // Grün
#define COL_ACTIVE    ((uint32_t)0x005500)  // Dunkelgrün aktiver Modus
#define COL_WHITE     ((uint32_t)0xFFFFFF)  // Weiß
#define COL_LGREY     ((uint32_t)0xBBBBBB)  // Hellgrau
#define COL_MGREY     ((uint32_t)0x888888)  // Mittleres Grau
#define COL_DGREY     ((uint32_t)0x606060)  // Dunkelgrau
#define COL_YELLOW    ((uint32_t)0xFFCC00)  // Gelb (WLAN-Verbindungsaufbau)

// Player-Screen Zustand (selektives Neuzeichnen)
static String  g_dTitle, g_dArtist, g_dAlbum, g_dCoverUrl, g_dCoverKey, g_dPlayer;
static String  g_dTitleD, g_dArtistD, g_dAlbumD; // displaybereit (UTF-8-faehig)
static bool    g_dPlaying  = false, g_dMuted = false;
static int     g_dVolume   = -1, g_dRepeat  = -1, g_dShuffle = -1;
static int     g_dPosition = -1, g_dDuration = 0, g_dTrackNum = -1;
static int     g_lmsPos   = -1;   // zuletzt von LMS empfangene Position
static int     g_localPos  = 0;   // Interpolationsbasis
static uint32_t g_localPosMs = 0; // millis() beim letzten LMS-Sync
static int     g_lastProgFilled = -1; // für Fortschrittsbalken selektives Neuzeichnen
static int     g_lastVolFilled  = -1; // für Lautstärkebalken selektives Neuzeichnen
static bool    g_lastVolMuted   = false;
static int     g_lastClockMin   = -2; // für Uhrzeit im Header (-2 = noch nie gezeichnet)

// Settings screen state
static std::vector<SqueeziePlayer>   g_settPlayers;
static std::vector<SqueeziePlaylist> g_settPlaylists;
static String   g_settDiscHost;
static uint16_t g_settDiscPort  = 0;
static bool     g_settDiscFound = false;
static bool     g_settSearched  = false;

// Scroll-State für Titel/Artist/Album (Index 0/1/2)
static int32_t g_scrollOff[3]   = {0, 0, 0}; // aktueller Pixel-Offset
static uint32_t g_scrollLast    = 0;          // letzter Scroll-Zeitstempel
static LGFX_Sprite g_infoLineSprite(&tft);

// Cover-Puffer (PSRAM) und Download-State
static uint8_t*          g_coverBuf      = nullptr;
static uint8_t*          g_coverDlBuf    = nullptr;
static volatile bool     g_coverReady    = false;
static volatile size_t   g_coverLen      = 0;
static volatile uint32_t g_coverGen      = 0;
static volatile uint32_t g_dlGen         = 0;
static volatile uint32_t g_coverSerial   = 0;
static String            g_pendingCoverUrl;
static String            g_pendingCoverKey;
static String            g_dispatchedCoverUrl;
static String            g_dispatchedCoverKey;
static String            g_loadedCoverKey;
static TaskHandle_t      g_coverTask     = nullptr;
static SemaphoreHandle_t g_coverMux      = nullptr;

// Prefetch-Slots: Cover der nächsten Tracks (PSRAM, sequenziell geladen)
struct PrefetchSlot {
    uint8_t* buf   = nullptr;
    size_t   len   = 0;
    bool     ready = false;
    String   url;
    String   key;
};
static PrefetchSlot      g_pf[SQUEEZE_PREFETCH];
static String            g_pfQueue[SQUEEZE_PREFETCH];
static String            g_pfQueueKeys[SQUEEZE_PREFETCH];
static int               g_pfQueueCount  = 0;
static SemaphoreHandle_t g_pfMux         = nullptr;
static TaskHandle_t      g_pfTask        = nullptr;

static bool isCompleteJpeg(const uint8_t* buf, size_t len) {
    return buf && len >= 4
           && buf[0] == 0xFF && buf[1] == 0xD8
           && buf[len - 2] == 0xFF && buf[len - 1] == 0xD9;
}

static size_t readHttpBody(HTTPClient& http, uint8_t* buf, size_t maxLen, uint32_t timeoutMs) {
    WiFiClient* s = http.getStreamPtr();
    int contentLen = http.getSize();
    size_t expected = (contentLen > 0) ? min((size_t)contentLen, maxLen) : 0;
    size_t total = 0;
    uint32_t t0 = millis();
    while ((contentLen > 0 ? total < expected : (http.connected() || s->available()))
           && total < maxLen
           && millis() - t0 < timeoutMs) {
        size_t av = s->available();
        if (av) {
            total += s->readBytes(buf + total, min(av, maxLen - total));
            if (contentLen > 0 && total >= expected) break;
        } else {
            vTaskDelay(1);
        }
    }
    return total;
}

// Rohwert-Lesung (ohne Kalibrierung)
static bool xpt2046ReadRaw(int32_t* rx, int32_t* ry) {
    if (!touchSPIinited) return false;
    touchSPI.beginTransaction(SPISettings(2000000, MSBFIRST, SPI_MODE0));
    digitalWrite(PIN_TOUCH_CS, LOW);
    // Druckmessung Z1: ohne Berührung nahe 0, bei Berührung > 100
    touchSPI.transfer(0xB1);
    int16_t z1 = touchSPI.transfer16(0) >> 3;
    touchSPI.transfer(0xC1);
    touchSPI.transfer16(0); // Z2 verwerfen
    if (z1 < 50) {
        touchSPI.transfer(0x80);
        touchSPI.transfer16(0);
        digitalWrite(PIN_TOUCH_CS, HIGH);
        touchSPI.endTransaction();
        return false;
    }
    int32_t xs = 0, ys = 0;
    const int samples = 8;
    for (int i = 0; i < samples; i++) {
        touchSPI.transfer(0xD1);
        xs += (touchSPI.transfer16(0) >> 3);
        touchSPI.transfer(0x91);
        ys += (touchSPI.transfer16(0) >> 3);
    }
    touchSPI.transfer(0x80);
    touchSPI.transfer16(0);
    digitalWrite(PIN_TOUCH_CS, HIGH);
    touchSPI.endTransaction();
    xs /= samples;
    ys /= samples;
    g_touch_raw_x = xs;
    g_touch_raw_y = ys;
    if (xs < 100 || xs > 4090 || ys < 100 || ys > 4090) return false;
    *rx = xs;
    *ry = ys;
    return true;
}

// Kalibrierte Lesung → Bildschirmkoordinaten
static bool xpt2046Read(int32_t* x, int32_t* y) {
    int32_t rx, ry;
    if (!xpt2046ReadRaw(&rx, &ry)) return false;
    // Achsentausch anwenden
    int32_t sx = cal_swap ? ry : rx;
    int32_t sy = cal_swap ? rx : ry;
    *x = map(sx, cal_x0, cal_x1, CAL_X_LEFT,   CAL_X_RIGHT);
    *y = map(sy, cal_y0, cal_y1, CAL_Y_TOP,    CAL_Y_BOTTOM);
    *x = constrain(*x, 0, DISPLAY_WIDTH  - 1);
    *y = constrain(*y, 0, DISPLAY_HEIGHT - 1);
    return true;
}

LGFX tft;

// UTF-8 → druckbares ASCII: Fallback für Bereiche, die weiterhin den Standardfont nutzen.
// Fuer Titel/Artist/Album verwenden wir weiter unten einen UTF-8-faehigen Font.
static String utf8ToAscii(const String& src) {
    String out;
    out.reserve(src.length());
    const uint8_t* p = (const uint8_t*)src.c_str();
    while (*p) {
        uint8_t b = *p;
        if (b < 0x80) { out += (char)b; p++; continue; }

        if ((b & 0xE0) == 0xC0 && p[1]) {                          // 2-Byte (U+0080–U+07FF)
            uint16_t cp = ((b & 0x1F) << 6) | (p[1] & 0x3F);
            p += 2;
            const char* s = "?";
            if      (cp >= 0xC0 && cp <= 0xC6) s = "A";
            else if (cp == 0xC7)               s = "C";
            else if (cp >= 0xC8 && cp <= 0xCB) s = "E";
            else if (cp >= 0xCC && cp <= 0xCF) s = "I";
            else if (cp == 0xD0)               s = "D";
            else if (cp == 0xD1)               s = "N";
            else if (cp >= 0xD2 && cp <= 0xD8) s = "O";
            else if (cp >= 0xD9 && cp <= 0xDC) s = "U";
            else if (cp >= 0xDD && cp <= 0xDE) s = "Y";
            else if (cp == 0xDF)               s = "ss";   // ß
            else if (cp >= 0xE0 && cp <= 0xE6) s = "a";
            else if (cp == 0xE7)               s = "c";
            else if (cp >= 0xE8 && cp <= 0xEB) s = "e";
            else if (cp >= 0xEC && cp <= 0xEF) s = "i";
            else if (cp == 0xF0)               s = "d";
            else if (cp == 0xF1)               s = "n";
            else if (cp >= 0xF2 && cp <= 0xF8) s = "o";
            else if (cp >= 0xF9 && cp <= 0xFC) s = "u";
            else if (cp >= 0xFD && cp <= 0xFF) s = "y";
            out += s; continue;
        }
        if ((b & 0xF0) == 0xE0 && p[1] && p[2]) {                  // 3-Byte (U+0800–U+FFFF)
            uint32_t cp = ((b & 0x0F) << 12) | ((p[1] & 0x3F) << 6) | (p[2] & 0x3F);
            p += 3;
            if      (cp == 0x2013 || cp == 0x2014) out += '-';      // – —
            else if (cp == 0x2018 || cp == 0x2019) out += '\'';     // ' '
            else if (cp == 0x201C || cp == 0x201D) out += '"';      // " "
            else if (cp == 0x2026)                 out += "...";    // …
            else if (cp == 0x2122)                 out += "TM";     // ™
            else                                   out += '?';
            continue;
        }
        if ((b & 0xF8) == 0xF0 && p[1] && p[2] && p[3]) {          // 4-Byte (Emoji etc.)
            p += 4; out += '?'; continue;
        }
        p++; // ungültiges Byte überspringen
    }
    return out;
}

static void setPlayerInfoFont(uint8_t size) {
    tft.setTextSize(1);
    if (size >= 3) tft.setFont(&fonts::lgfxJapanGothicP_28);
    else           tft.setFont(&fonts::lgfxJapanGothicP_24);
}

static int getPlayerInfoLineHeight(uint8_t size) {
    setPlayerInfoFont(size);
    return tft.fontHeight() + 4;
}

static bool ensureInfoLineSprite(int height) {
    if (height <= 0) return false;
    if (g_infoLineSprite.width() == PL_INFO_W && g_infoLineSprite.height() == height) return true;
    g_infoLineSprite.deleteSprite();
    g_infoLineSprite.setColorDepth(16);
    return g_infoLineSprite.createSprite(PL_INFO_W, height);
}

// ─── Hilfsfunktionen ────────────────────────────────────────────────────────

static void drawButton(int16_t x, int16_t y, int16_t w, int16_t h,
                       uint32_t bgColor, uint32_t txtColor, const char* label,
                       bool highlighted = false) {
    uint32_t borderColor = highlighted ? TFT_YELLOW : TFT_WHITE;
    tft.fillRoundRect(x, y, w, h, 6, bgColor);
    tft.drawRoundRect(x, y, w, h, 6, borderColor);
    if (highlighted) tft.drawRoundRect(x+1, y+1, w-2, h-2, 5, borderColor);
    tft.setTextSize(2);
    int16_t tw = tft.textWidth(label);
    tft.setTextColor(txtColor, bgColor);
    tft.setCursor(x + (w - tw) / 2, y + (h - 16) / 2);
    tft.print(label);
}

static void drawCrosshair(int16_t x, int16_t y) {
    tft.drawFastHLine(x - 20, y,      40, TFT_RED);
    tft.drawFastVLine(x,      y - 20, 40, TFT_RED);
    tft.drawCircle(x, y, 6, TFT_RED);
}

// Blockierend: wartet bis Touch losgelassen, dann bis gedrückt, sammelt Mittelwert
static bool calWaitTouch(int32_t* rx, int32_t* ry) {
    // Zuerst warten bis kein Touch mehr (vorherige Berührung los)
    uint32_t t0 = millis();
    while (millis() - t0 < 2000) {
        int32_t dummy1, dummy2;
        if (!xpt2046ReadRaw(&dummy1, &dummy2)) break;
        delay(30);
    }
    // Warten auf Berührung
    while (true) {
        int32_t tx, ty;
        if (xpt2046ReadRaw(&tx, &ty)) {
            // Stabile Messung: 600 ms mitteln
            int32_t sx = 0, sy = 0;
            int cnt  = 0;
            uint32_t t1 = millis();
            while (millis() - t1 < 600) {
                int32_t a, b;
                if (xpt2046ReadRaw(&a, &b)) { sx += a; sy += b; cnt++; }
                delay(20);
            }
            if (cnt >= 5) { *rx = sx / cnt; *ry = sy / cnt; return true; }
        }
        delay(30);
    }
}

// ─── Kalibrierung ───────────────────────────────────────────────────────────

static void runCalibration() {
    tft.fillScreen(TFT_BLACK);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.setTextDatum(MC_DATUM);
    tft.setTextSize(2);
    tft.drawString("Touch-Kalibrierung", DISPLAY_WIDTH / 2, 60);
    tft.setTextSize(1);
    tft.drawString("Bitte nacheinander die 3 Kreuzmarkierungen beruehren.", DISPLAY_WIDTH / 2, 100);
    delay(2000);

    int32_t r[3][2]; // r[punkt][x/y]

    // Punkt 0: oben-links
    tft.fillScreen(TFT_BLACK);
    tft.setTextColor(TFT_YELLOW, TFT_BLACK);
    tft.setTextDatum(MC_DATUM);
    tft.setTextSize(1);
    tft.drawString("1 von 3 - oben links beruehren", DISPLAY_WIDTH / 2, DISPLAY_HEIGHT / 2);
    drawCrosshair(CAL_X_LEFT, CAL_Y_TOP);
    calWaitTouch(&r[0][0], &r[0][1]);
    srvLog("[Cal] Punkt 0 (oben-links):  roh X=%d Y=%d", r[0][0], r[0][1]);

    // Punkt 1: oben-rechts
    tft.fillScreen(TFT_BLACK);
    tft.drawString("2 von 3 - oben rechts beruehren", DISPLAY_WIDTH / 2, DISPLAY_HEIGHT / 2);
    drawCrosshair(CAL_X_RIGHT, CAL_Y_TOP);
    calWaitTouch(&r[1][0], &r[1][1]);
    srvLog("[Cal] Punkt 1 (oben-rechts): roh X=%d Y=%d", r[1][0], r[1][1]);

    // Punkt 2: unten-links
    tft.fillScreen(TFT_BLACK);
    tft.drawString("3 von 3 - unten links beruehren", DISPLAY_WIDTH / 2, DISPLAY_HEIGHT / 2);
    drawCrosshair(CAL_X_LEFT, CAL_Y_BOTTOM);
    calWaitTouch(&r[2][0], &r[2][1]);
    srvLog("[Cal] Punkt 2 (unten-links): roh X=%d Y=%d", r[2][0], r[2][1]);

    // Achsentausch bestimmen:
    // Punkt 0→1: Bildschirm-X ändert sich stark, Bildschirm-Y bleibt gleich
    // Wenn der Raw-X-Kanal sich stärker ändert → kein Tausch
    int32_t dxRawX = abs(r[1][0] - r[0][0]); // Änderung Raw-X bei horizontaler Bewegung
    int32_t dxRawY = abs(r[1][1] - r[0][1]); // Änderung Raw-Y bei horizontaler Bewegung
    cal_swap = (dxRawY > dxRawX) ? 1 : 0;
    srvLog("[Cal] Achsentausch: %s", cal_swap ? "ja" : "nein");

    if (!cal_swap) {
        // Raw-X → Bildschirm-X, Raw-Y → Bildschirm-Y
        cal_x0 = r[0][0]; cal_x1 = r[1][0]; // oben-links und oben-rechts, Raw-X
        cal_y0 = r[0][1]; cal_y1 = r[2][1]; // oben-links und unten-links, Raw-Y
    } else {
        // Raw-Y → Bildschirm-X, Raw-X → Bildschirm-Y
        cal_x0 = r[0][1]; cal_x1 = r[1][1]; // Raw-Y Werte für Bildschirm-X
        cal_y0 = r[0][0]; cal_y1 = r[2][0]; // Raw-X Werte für Bildschirm-Y
    }
    cal_valid = true;
    srvLog("[Cal] cal_x0=%d cal_x1=%d cal_y0=%d cal_y1=%d",
           cal_x0, cal_x1, cal_y0, cal_y1);

    Preferences prefs;
    prefs.begin(NVS_NAMESPACE, false);
    prefs.putBool(NVS_KEY_CAL_OK,   true);
    prefs.putShort(NVS_KEY_CAL_X0,  cal_x0);
    prefs.putShort(NVS_KEY_CAL_X1,  cal_x1);
    prefs.putShort(NVS_KEY_CAL_Y0,  cal_y0);
    prefs.putShort(NVS_KEY_CAL_Y1,  cal_y1);
    prefs.putUChar(NVS_KEY_CAL_SWAP, cal_swap);
    prefs.end();

    tft.fillScreen(TFT_BLACK);
    tft.setTextColor(TFT_GREEN, TFT_BLACK);
    tft.setTextDatum(MC_DATUM);
    tft.setTextSize(2);
    tft.drawString("Kalibrierung gespeichert!", DISPLAY_WIDTH / 2, DISPLAY_HEIGHT / 2);
    srvLog("[Cal] Kalibrierung abgeschlossen und gespeichert.");
    delay(1500);
}

// ─── Initialisierung ────────────────────────────────────────────────────────

void displayInit() {
    tft.init();
    tft.setRotation(DISPLAY_ROTATION);
    tft.setBrightness(255);

    // Touch-SPI initialisieren (separate Instanz, GPIO 13 frei wegen pin_miso=-1 im LGFX)
    pinMode(PIN_TOUCH_CS, OUTPUT);
    digitalWrite(PIN_TOUCH_CS, HIGH);
    touchSPI.begin(12, 13, 11, PIN_TOUCH_CS); // SCK, MISO, MOSI, CS
    touchSPIinited = true;

    // Gespeicherte Kalibrierung laden
    Preferences prefs;
    prefs.begin(NVS_NAMESPACE, true);
    bool ok = prefs.getBool(NVS_KEY_CAL_OK, false);
    if (ok) {
        cal_x0   = prefs.getShort(NVS_KEY_CAL_X0,  300);
        cal_x1   = prefs.getShort(NVS_KEY_CAL_X1,  3800);
        cal_y0   = prefs.getShort(NVS_KEY_CAL_Y0,  300);
        cal_y1   = prefs.getShort(NVS_KEY_CAL_Y1,  3800);
        cal_swap = prefs.getUChar(NVS_KEY_CAL_SWAP, 0);
        cal_valid = true;
        srvLog("[Touch] Kalibrierung geladen: x0=%d x1=%d y0=%d y1=%d swap=%d",
               cal_x0, cal_x1, cal_y0, cal_y1, cal_swap);
    } else {
        srvLog("[Touch] Keine Kalibrierung — starte Kalibrierung automatisch.");
    }
    prefs.end();

    srvLog("[Display] Initialisiert: %dx%d, Rotation %d",
           DISPLAY_WIDTH, DISPLAY_HEIGHT, DISPLAY_ROTATION);

    g_coverMux = xSemaphoreCreateMutex();
    g_coverBuf = (uint8_t*)ps_malloc(131072);
    g_coverDlBuf = (uint8_t*)ps_malloc(131072);
    if (!g_coverBuf || !g_coverDlBuf) {
        srvLog("[cover] ps_malloc FAIL");
    } else {
        // Cover-Cache aus LittleFS laden — läuft vor WiFi, sofort beim Player-Screen verfügbar
        if (LittleFS.begin(true) && LittleFS.exists("/cover.jpg")) {
            File f = LittleFS.open("/cover.jpg", "r");
            if (f) {
                size_t n = f.read(g_coverBuf, 131070);
                f.close();
                bool valid = isCompleteJpeg(g_coverBuf, n);
                if (valid) {
                    g_coverLen = n;
                    g_coverReady = true;
                    g_coverSerial = 1;
                }
                srvLog("[cover] Cache: %u B, valid=%d", (unsigned)n, valid);
            }
        } else {
            srvLog("[cover] kein Cache");
        }
    }

    // Prefetch-PSRAM-Slots allozieren
    g_pfMux = xSemaphoreCreateMutex();
    for (int i = 0; i < SQUEEZE_PREFETCH; i++) {
        g_pf[i].buf = (uint8_t*)ps_malloc(131072);
        if (!g_pf[i].buf) srvLog("[pf] PSRAM Slot %d FAIL", i);
    }
    srvLog("[pf] %d Prefetch-Slots bereit", SQUEEZE_PREFETCH);

    if (!cal_valid) {
        runCalibration();
    }
}

// ─── Startup-Screen ──────────────────────────────────────────────────────────

void displayShowStartup(const char* version, const char* ip) {
    bool fresh = (g_screen != SCREEN_STARTUP);
    g_screen = SCREEN_STARTUP;
    if (fresh) {
        tft.fillScreen(COL_BG);
        tft.setTextDatum(MC_DATUM);
        tft.setTextColor(COL_GREEN, COL_BG);
        tft.setTextSize(4);
        tft.drawString("Squeezie", DISPLAY_WIDTH / 2, DISPLAY_HEIGHT / 2 - 38);
        tft.setTextColor(COL_LGREY, COL_BG);
        tft.setTextSize(2);
        tft.drawString(version, DISPLAY_WIDTH / 2, DISPLAY_HEIGHT / 2 + 6);
    }
    // Nur IP-Zeile aktualisieren — kein fillScreen beim zweiten Aufruf
    int lineY = DISPLAY_HEIGHT / 2 + 34;
    tft.fillRect(0, lineY - 16, DISPLAY_WIDTH, 32, COL_BG);
    tft.setTextDatum(MC_DATUM);
    tft.setTextSize(2);
    if (ip && ip[0]) {
        tft.setTextColor(COL_WHITE, COL_BG);
        tft.drawString(ip, DISPLAY_WIDTH / 2, lineY);
    } else {
        tft.setTextColor(COL_YELLOW, COL_BG);
        tft.drawString("Verbinde mit WLAN...", DISPLAY_WIDTH / 2, lineY);
    }
}

// ─── Player-Screen Zeichenhelfer ─────────────────────────────────────────────

static void drawCircleBtn(int16_t cx, int16_t cy, int16_t r,
                           uint32_t bg, uint32_t fg, const char* icon) {
    tft.fillCircle(cx, cy + 2, r, (uint32_t)0x101010);
    tft.fillCircle(cx, cy, r, bg);
    tft.drawCircle(cx, cy, r, COL_WHITE);
    tft.drawCircle(cx, cy, r - 2, (bg == COL_GREEN) ? (uint32_t)0xE8FBEF : COL_MID);

    if (!icon) return;
    if (!strcmp(icon, "play")) {
        tft.fillTriangle(cx - 8, cy - 12, cx - 8, cy + 12, cx + 11, cy, fg);
    } else if (!strcmp(icon, "pause")) {
        tft.fillRoundRect(cx - 11, cy - 12, 7, 24, 2, fg);
        tft.fillRoundRect(cx + 4, cy - 12, 7, 24, 2, fg);
    } else if (!strcmp(icon, "prev")) {
        tft.fillRect(cx - 13, cy - 12, 4, 24, fg);
        tft.fillTriangle(cx + 9, cy - 13, cx + 9, cy + 13, cx - 8, cy, fg);
    } else if (!strcmp(icon, "next")) {
        tft.fillRect(cx + 9, cy - 12, 4, 24, fg);
        tft.fillTriangle(cx - 9, cy - 13, cx - 9, cy + 13, cx + 8, cy, fg);
    } else if (!strcmp(icon, "voldown") || !strcmp(icon, "volup")) {
        tft.fillTriangle(cx - 11, cy - 5, cx - 4, cy - 11, cx - 4, cy + 11, fg);
        tft.fillRect(cx - 15, cy - 5, 5, 10, fg);
        tft.drawArc(cx + 1, cy, 7, 7, -45, 45, fg);
        if (!strcmp(icon, "volup")) {
            tft.drawArc(cx + 1, cy, 12, 12, -45, 45, fg);
            tft.fillRect(cx + 11, cy - 1, 8, 3, fg);
            tft.fillRect(cx + 14, cy - 4, 3, 9, fg);
        } else {
            tft.fillRect(cx + 10, cy - 1, 8, 3, fg);
        }
    }
}

static void drawVolumeIcon(int16_t x, int16_t y, int16_t w, int16_t h,
                           uint32_t bg, uint32_t fg, bool louder,
                           uint32_t border = COL_WHITE) {
    tft.fillRoundRect(x, y, w, h, 6, bg);
    tft.drawRoundRect(x, y, w, h, 6, border);
    tft.setFont(nullptr);
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(fg, bg);
    tft.setTextSize(3);
    if (louder) {
        tft.drawString("+", x + w / 2, y + h / 2 + 1);
    } else {
        tft.drawString("-", x + w / 2, y + h / 2 + 1);
    }
}

static void drawMuteIcon(int16_t x, int16_t y, int16_t w, int16_t h, bool muted) {
    uint32_t bg = muted ? (uint32_t)0x5A2020 : (uint32_t)0x3A3A3A;
    uint32_t fg = muted ? (uint32_t)0xFFB0B0 : (uint32_t)COL_WHITE;
    uint32_t border = muted ? (uint32_t)0xFFB0B0 : (uint32_t)COL_WHITE;
    tft.fillRoundRect(x, y, w, h, 6, bg);
    tft.drawRoundRect(x, y, w, h, 6, border);

    int cx = x + w / 2;
    int cy = y + h / 2;
    tft.fillRect(cx - 14, cy - 4, 5, 8, fg);
    tft.fillTriangle(cx - 9, cy - 4, cx - 2, cy - 10, cx - 2, cy + 10, fg);
    tft.drawLine(cx + 4, cy - 8, cx + 14, cy + 8, fg);
    tft.drawLine(cx + 14, cy - 8, cx + 4, cy + 8, fg);
}

static void drawRepeatGlyph(int16_t x, int16_t y, uint32_t fg) {
    tft.drawFastHLine(x, y, 14, fg);
    tft.drawFastVLine(x, y, 5, fg);
    tft.drawFastHLine(x + 5, y + 10, 14, fg);
    tft.drawFastVLine(x + 19, y + 5, 6, fg);
    tft.fillTriangle(x + 14, y - 3, x + 14, y + 3, x + 19, y, fg);
    tft.fillTriangle(x + 5, y + 7, x + 5, y + 13, x, y + 10, fg);
}

static void drawShuffleGlyph(int16_t x, int16_t y, uint32_t fg) {
    tft.drawLine(x, y + 1, x + 6, y + 1, fg);
    tft.drawLine(x + 6, y + 1, x + 12, y + 6, fg);
    tft.drawLine(x + 12, y + 6, x + 18, y + 6, fg);
    tft.drawLine(x, y + 11, x + 6, y + 11, fg);
    tft.drawLine(x + 6, y + 11, x + 12, y + 6, fg);
    tft.drawLine(x + 12, y + 6, x + 18, y + 6, fg);
    tft.fillTriangle(x + 18, y + 3, x + 18, y + 9, x + 23, y + 6, fg);
}

// Textzeile ohne Flackern: erst Text mit Hintergrundfarbe zeichnen,
// dann verbleibende Fläche rechts löschen (kein vorangehendes fillRect).
static void drawInfoLine(const char* txt, int16_t x, int16_t y, int16_t w,
                          uint32_t color, uint8_t size) {
    tft.setFont(nullptr);
    tft.setTextSize(size);
    int lh = 8 * size + 4;
    tft.setTextDatum(TL_DATUM);
    tft.setTextColor(color, COL_BG);
    tft.setClipRect(x, y, w, lh);
    tft.fillRect(x, y, w, lh, COL_BG);
    if (!txt || !txt[0]) {
        tft.clearClipRect();
        return;
    }
    tft.drawString(txt, x + 2, y + 2);
    tft.clearClipRect();
}

// Scrollende Textzeile: passt Text → wie drawInfoLine; sonst Marquee-Offset anwenden.
// offset: aktueller Scroll-Pixel-Offset (wird bei Reset auf 0 gesetzt).
static void drawScrollLine(const char* txt, int16_t x, int16_t y, int16_t w,
                            uint32_t color, uint8_t size, int32_t offset) {
    tft.setFont(nullptr);
    tft.setTextSize(size);
    int lh = 8 * size + 4;
    tft.setTextDatum(TL_DATUM);
    tft.setTextColor(color, COL_BG);
    tft.setClipRect(x, y, w, lh);
    tft.fillRect(x, y, w, lh, COL_BG);
    if (!txt || !txt[0]) {
        tft.clearClipRect();
        return;
    }
    int tw = tft.textWidth(txt);
    if (tw + 4 <= w || offset == 0) {
        // Text passt oder Offset = 0: normal zeichnen
        tft.drawString(txt, x + 2, y + 2);
        tft.clearClipRect();
    } else {
        // Scrolling: Text mit negativem Offset zeichnen, Clip auf Feldbreite
        tft.drawString(txt, x + 2 - offset, y + 2);
        tft.clearClipRect();
    }
}

static void drawPlayerScrollLine(const char* txt, int16_t x, int16_t y, int16_t w,
                                 uint32_t color, uint8_t size, int32_t offset, bool bold = false) {
    setPlayerInfoFont(size);
    int lh = tft.fontHeight() + 4;
    if (!ensureInfoLineSprite(lh)) {
        tft.setTextDatum(TL_DATUM);
        tft.setTextColor(color, COL_BG);
        tft.setClipRect(x, y, w, lh);
        tft.fillRect(x, y, w, lh, COL_BG);
        if (!txt || !txt[0]) {
            tft.clearClipRect();
            return;
        }
        int tw = tft.textWidth(txt);
        if (tw + 4 <= w || offset == 0) {
            tft.drawString(txt, x + 2, y + 2);
            if (bold) tft.drawString(txt, x + 3, y + 2);
        } else {
            tft.drawString(txt, x + 2 - offset, y + 2);
            if (bold) tft.drawString(txt, x + 3 - offset, y + 2);
        }
        tft.clearClipRect();
        return;
    }
    g_infoLineSprite.fillSprite(COL_BG);
    g_infoLineSprite.setFont(tft.getFont());
    g_infoLineSprite.setTextSize(1);
    g_infoLineSprite.setTextDatum(TL_DATUM);
    g_infoLineSprite.setTextColor(color, COL_BG);
    if (!txt || !txt[0]) {
        g_infoLineSprite.pushSprite(x, y);
        return;
    }
    int tw = g_infoLineSprite.textWidth(txt);
    if (tw + 4 <= w || offset == 0) {
        g_infoLineSprite.drawString(txt, 2, 2);
        if (bold) g_infoLineSprite.drawString(txt, 3, 2);
    } else {
        g_infoLineSprite.drawString(txt, 2 - offset, 2);
        if (bold) g_infoLineSprite.drawString(txt, 3 - offset, 2);
    }
    g_infoLineSprite.pushSprite(x, y);
}

static void drawPlayerHeader(const char* playerName, bool playing) {
    tft.fillRect(0, PL_HDR_Y, DISPLAY_WIDTH, PL_HDR_H, COL_HDR);
    tft.setFont(nullptr); tft.setTextSize(2);
    // Spielername links
    tft.setTextDatum(ML_DATUM);
    tft.setTextColor(COL_LGREY, COL_HDR);
    tft.drawString(playerName && *playerName ? playerName : "Squeezie", 8, PL_HDR_H / 2);
    // EINST.-Button rechts — touch-Ziel tx > DISPLAY_WIDTH-64
    tft.fillRoundRect(DISPLAY_WIDTH - 62, 3, 58, PL_HDR_H - 6, 4, COL_DARK);
    tft.drawRoundRect(DISPLAY_WIDTH - 62, 3, 58, PL_HDR_H - 6, 4, COL_MID);
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(COL_WHITE, COL_DARK);
    tft.drawString("EINST.", DISPLAY_WIDTH - 33, PL_HDR_H / 2);
    // Status-Dot (links neben EINST.-Button)
    tft.fillCircle(DISPLAY_WIDTH - 85, PL_HDR_H / 2, 5,
                    playing ? (uint32_t)COL_GREEN : (uint32_t)COL_MID);
    // Uhrzeit zentriert im Header
    {
        time_t tnow = time(nullptr);
        struct tm ti; localtime_r(&tnow, &ti);
        if (ti.tm_year > 100) { // NTP synchronisiert (Jahr > 2000)
            char tbuf[6];
            snprintf(tbuf, sizeof(tbuf), "%02d:%02d", ti.tm_hour, ti.tm_min);
            tft.setFont(nullptr); tft.setTextSize(2);
            tft.setTextDatum(MC_DATUM);
            tft.setTextColor(COL_MGREY, COL_HDR);
            tft.drawString(tbuf, DISPLAY_WIDTH / 2, PL_HDR_H / 2);
        }
    }
}

static void drawCoverPlaceholder() {
    int cx = PL_COVER_X + PL_COVER_W / 2;
    int cy = PL_COVER_Y + PL_COVER_H / 2;
    tft.fillRect(PL_COVER_X, PL_COVER_Y, PL_COVER_W, PL_COVER_H, COL_BG);
    tft.drawRoundRect(PL_COVER_X + 2, PL_COVER_Y + 2, PL_COVER_W - 4, PL_COVER_H - 4, 6, COL_MID);
    tft.drawCircle(cx - 16, cy + 10, 16, COL_MID);
    tft.drawCircle(cx + 16, cy + 10, 16, COL_MID);
    tft.drawFastVLine(cx - 32, cy - 30, 40, COL_MID);
    tft.drawFastVLine(cx,      cy - 30, 40, COL_MID);
    tft.drawFastHLine(cx - 32, cy - 30, 32, COL_MID);
}

static void drawCoverImage(const uint8_t* buf, size_t len) {
    tft.fillRect(PL_COVER_X, PL_COVER_Y, PL_COVER_W, PL_COVER_H, COL_BG);
    if (buf && len > 0) {
        tft.drawJpg(buf, (uint32_t)len, PL_COVER_X, PL_COVER_Y, PL_COVER_W, PL_COVER_H);
    }
}

static void drawProgressBar(int position, int duration) {
    int y  = PL_PROG_Y + 2;
    int h  = PL_PROG_H - 4;
    int bw = DISPLAY_WIDTH - 4;
    int filled = (duration > 0) ?
        constrain((int)((long)position * bw / duration), 0, bw) : 0;

    // Selektiv: nur geänderte Pixel neu zeichnen → kein Flackern
    if (g_lastProgFilled < 0) {
        tft.fillRect(2, y, bw, h, COL_PROG_BG);
        if (filled > 0) tft.fillRect(2, y, filled, h, COL_GREEN);
    } else if (filled > g_lastProgFilled) {
        tft.fillRect(2 + g_lastProgFilled, y, filled - g_lastProgFilled, h, COL_GREEN);
    } else if (filled < g_lastProgFilled) {
        tft.fillRect(2 + filled, y, g_lastProgFilled - filled, h, COL_PROG_BG);
    }
    g_lastProgFilled = filled;

    // Zeitanzeige: Bar unter dem Text auffrischen, dann transparenter weißer Text
    char buf[20];
    snprintf(buf, sizeof(buf), "%d:%02d / %d:%02d",
             position / 60, position % 60, duration / 60, duration % 60);
    tft.setFont(nullptr); tft.setTextSize(2);
    int tw  = tft.textWidth(buf);
    int trx = DISPLAY_WIDTH - 3;           // rechte Textkante (MR_DATUM)
    int rx0 = max(2, trx - tw - 2);        // Auffrisch-Bereich links
    int rx1 = min(2 + bw, trx + 2);        // Auffrisch-Bereich rechts
    int gx1 = 2 + filled;                  // Grün endet hier
    if (gx1 > rx0)
        tft.fillRect(rx0, y, min(gx1, rx1) - rx0, h, COL_GREEN);
    if (gx1 < rx1)
        tft.fillRect(max(rx0, gx1), y, rx1 - max(rx0, gx1), h, COL_PROG_BG);
    tft.setTextDatum(MR_DATUM);
    tft.setTextColor(COL_WHITE);           // kein Hintergrund = transparent
    tft.drawString(buf, trx, PL_PROG_Y + PL_PROG_H / 2);
}

static void drawTransportButtons(bool playing) {
    tft.fillRect(0, PL_TRANS_Y, DISPLAY_WIDTH, PL_TRANS_H, COL_BG);
    int cy = PL_TRANS_CY;                          // = 210 + 36 = 246
    drawCircleBtn( 90, cy, 29, (uint32_t)0x3A3A3A, COL_WHITE, "prev");
    uint32_t playBg = playing ? (uint32_t)0x3A3A3A : (uint32_t)COL_GREEN;
    uint32_t playFg = playing ? (uint32_t)COL_GREEN : (uint32_t)COL_BG;
    drawCircleBtn(240, cy, 35, playBg, playFg, playing ? "pause" : "play");
    drawCircleBtn(390, cy, 29, (uint32_t)0x3A3A3A, COL_WHITE, "next");
}

static void drawVolumeButtons(bool muted) {
    int y = PL_VOL_Y, h = PL_VOL_H;
    uint32_t volBg = muted ? (uint32_t)0x242424 : (uint32_t)0x3A3A3A;
    uint32_t volFg = muted ? (uint32_t)COL_DGREY : (uint32_t)COL_WHITE;
    uint32_t volBorder = muted ? (uint32_t)COL_DGREY : (uint32_t)COL_WHITE;
    drawVolumeIcon(PL_VOL_MINUS_X, y + 2, PL_VOL_BTN_W, h - 4, volBg, volFg, false, volBorder);
    drawVolumeIcon(PL_VOL_PLUS_X,  y + 2, PL_VOL_BTN_W, h - 4, volBg, volFg, true,  volBorder);
    drawMuteIcon(PL_VOL_MUTE_X, y + 2, PL_VOL_BTN_W, h - 4, muted);
}

static void drawVolumeRow(int volume, bool muted) {
    int y = PL_VOL_Y, h = PL_VOL_H;
    int real_pct = constrain(volume, 0, 100);
    int bx = PL_VOL_BAR_X, bw = PL_VOL_BAR_W;
    int filled = constrain(real_pct * bw / 100, 0, bw);
    uint32_t fillCol = muted ? (uint32_t)COL_MGREY : (uint32_t)COL_GREEN;

    // Selektiv: nur geänderte Pixel neu zeichnen → kein Flackern
    if (g_lastVolFilled < 0 || muted != g_lastVolMuted) {
        // Erstzeichnung: gesamten Balken zeichnen
        if (filled > 0)  tft.fillRect(bx,          y + 5, filled,      h - 10, fillCol);
        if (filled < bw) tft.fillRect(bx + filled,  y + 5, bw - filled, h - 10, COL_PROG_BG);
    } else if (filled > g_lastVolFilled) {
        tft.fillRect(bx + g_lastVolFilled, y + 5, filled - g_lastVolFilled, h - 10, fillCol);
    } else if (filled < g_lastVolFilled) {
        tft.fillRect(bx + filled, y + 5, g_lastVolFilled - filled, h - 10, COL_PROG_BG);
    }
    g_lastVolFilled = filled;
    g_lastVolMuted = muted;

    // Prozentzahl: Bar unter dem Text auffrischen, dann transparenter weißer Text
    char buf[6]; snprintf(buf, sizeof(buf), "%d%%", real_pct);
    tft.setFont(nullptr); tft.setTextSize(2);
    int tw  = tft.textWidth(buf);
    int tcx = bx + bw / 2;                // Textmitte (MC_DATUM)
    int rx0 = max(bx, tcx - tw / 2 - 2);
    int rx1 = min(bx + bw, tcx + tw / 2 + tw % 2 + 2);
    int gx1 = bx + filled;                // Grün endet hier
    if (gx1 > rx0)
        tft.fillRect(rx0, y + 5, min(gx1, rx1) - rx0, h - 10, fillCol);
    if (gx1 < rx1)
        tft.fillRect(max(rx0, gx1), y + 5, rx1 - max(rx0, gx1), h - 10, COL_PROG_BG);
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(COL_WHITE);           // kein Hintergrund = transparent
    tft.drawString(buf, tcx, y + h / 2);
}

static void drawModeRow(int repeat, int shuffle) {
    int y = PL_MODE_Y, h = PL_MODE_H;
    const char* rptTxt[] = {"Wdh: aus", "Wdh: Titel", "Wdh: Liste"};
    const char* shfTxt[] = {"Zuf: aus", "Zuf: Titel", "Zuf: Album"};
    const char* rL = (repeat  >= 0 && repeat  <= 2) ? rptTxt[repeat]  : "Wdh";
    const char* sL = (shuffle >= 0 && shuffle <= 2) ? shfTxt[shuffle] : "Zuf";
    bool rAct = repeat  > 0;
    bool sAct = shuffle > 0;
    uint32_t rBg = rAct ? (uint32_t)0x0F4D22 : (uint32_t)0x363636;
    uint32_t sBg = sAct ? (uint32_t)0x0F4D22 : (uint32_t)0x363636;
    uint32_t rBorder = rAct ? COL_GREEN : COL_LGREY;
    uint32_t sBorder = sAct ? COL_GREEN : COL_LGREY;
    uint32_t rIconBg = rAct ? (uint32_t)0x19A347 : (uint32_t)0x555555;
    uint32_t sIconBg = sAct ? (uint32_t)0x19A347 : (uint32_t)0x555555;

    tft.fillRoundRect(4,   y + 3, 232, h - 4, 7, (uint32_t)0x101010);
    tft.fillRoundRect(2,   y + 1, 236, h - 2, 7, rBg);
    tft.drawRoundRect(2,   y + 1, 236, h - 2, 7, rBorder);
    tft.fillCircle(28, y + h / 2, 12, rIconBg);
    tft.drawCircle(28, y + h / 2, 12, COL_WHITE);
    drawRepeatGlyph(17, y + h / 2 - 5, COL_WHITE);
    tft.setFont(nullptr); tft.setTextSize(2); tft.setTextDatum(ML_DATUM);
    tft.setTextColor(COL_WHITE, rBg);
    tft.drawString(rL, 48, y + h / 2);

    tft.fillRoundRect(244, y + 3, 232, h - 4, 7, (uint32_t)0x101010);
    tft.fillRoundRect(242, y + 1, 236, h - 2, 7, sBg);
    tft.drawRoundRect(242, y + 1, 236, h - 2, 7, sBorder);
    tft.fillCircle(268, y + h / 2, 12, sIconBg);
    tft.drawCircle(268, y + h / 2, 12, COL_WHITE);
    drawShuffleGlyph(257, y + h / 2 - 6, COL_WHITE);
    tft.setTextColor(COL_WHITE, sBg);
    tft.drawString(sL, 288, y + h / 2);
}

// ── Async Cover-Download (FreeRTOS-Task auf Core 0) ─────────────────────────

static void coverDlTask(void*) {
    uint32_t myGen;
    String pendingUrl;
    String pendingKey;
    xSemaphoreTake(g_coverMux, portMAX_DELAY);
    myGen = g_dlGen;
    pendingUrl = g_pendingCoverUrl;
    pendingKey = g_pendingCoverKey;
    xSemaphoreGive(g_coverMux);
    size_t total = 0;
    bool jpegOk = false;
    if (g_coverDlBuf) {
        HTTPClient http;
        srvLog("[cover] URL: %s", pendingUrl.c_str());
        http.setTimeout(3000);
        if (http.begin(pendingUrl)) {
            int httpCode = http.GET();
            srvLog("[cover] HTTP %d", httpCode);
            if (httpCode == 200) {
                total = readHttpBody(http, g_coverDlBuf, 131070, 1200);
                jpegOk = isCompleteJpeg(g_coverDlBuf, total);
                srvLog("[cover] %d bytes, JPEG=%s", (int)total, jpegOk ? "ok" : "NEIN");
            }
            http.end();
        } else {
            srvLog("[cover] http.begin FAIL");
        }
    }
    // Nur veröffentlichen wenn diese Generation noch aktuell ist
    xSemaphoreTake(g_coverMux, portMAX_DELAY);
    if (myGen == g_coverGen) {
        if (jpegOk && total > 0) {
            memcpy(g_coverBuf, g_coverDlBuf, total);
            g_coverLen = total;
            g_loadedCoverKey = pendingKey;
        } else {
            g_coverLen = 0;
            g_loadedCoverKey = "";
        }
        g_coverReady = true;
        g_coverSerial++;
        // Für nächsten Boot cachen (beide Cores lesen nur g_coverBuf → kein Konflikt)
        if (jpegOk && total > 0) {
            File f = LittleFS.open("/cover.jpg", "w");
            if (f) { f.write(g_coverBuf, total); f.close(); }
        }
    }
    g_coverTask = nullptr;
    xSemaphoreGive(g_coverMux);
    vTaskDelete(nullptr);
}

bool displayCopyCover(std::vector<uint8_t>& out) {
    out.clear();
    if (!g_coverMux || !g_coverBuf) return false;
    xSemaphoreTake(g_coverMux, portMAX_DELAY);
    size_t len = (size_t)g_coverLen;
    bool valid = isCompleteJpeg(g_coverBuf, len);
    if (valid) {
        out.resize(len);
        memcpy(out.data(), g_coverBuf, len);
    }
    xSemaphoreGive(g_coverMux);
    return valid;
}

uint32_t displayGetCoverSerial() {
    if (!g_coverMux) return 0;
    xSemaphoreTake(g_coverMux, portMAX_DELAY);
    uint32_t serial = g_coverSerial;
    xSemaphoreGive(g_coverMux);
    return serial;
}

static String g_coverDrawnForTitle; // Titel des zuletzt auf dem Display gezeichneten Covers
static bool   g_coverPredrawn = false; // Cover in displayDrawPlayerScreen() vorgezeichnet
static String g_coverCandidateUrl;
static String g_coverCandidateKey;
static uint8_t g_coverCandidateCount = 0;

// ── Prefetch-Task: lädt Cover der Folgetracks sequenziell ───────────────────

static void prefetchTask(void*) {
    for (;;) {
        // Nächste URL aus Queue holen
        xSemaphoreTake(g_pfMux, portMAX_DELAY);
        if (g_pfQueueCount == 0) {
            xSemaphoreGive(g_pfMux);
            break;
        }
        String url = g_pfQueue[0];
        String key = g_pfQueueKeys[0];
        for (int i = 1; i < g_pfQueueCount; i++) {
            g_pfQueue[i - 1] = g_pfQueue[i];
            g_pfQueueKeys[i - 1] = g_pfQueueKeys[i];
        }
        g_pfQueueCount--;
        xSemaphoreGive(g_pfMux);

        // Haupt-Download hat Vorrang — warten bis er fertig ist
        while (g_coverTask != nullptr) vTaskDelay(pdMS_TO_TICKS(200));

        // Freien Slot finden (oder ältesten belegen)
        int slot = -1;
        xSemaphoreTake(g_pfMux, portMAX_DELAY);
        for (int i = 0; i < SQUEEZE_PREFETCH; i++) {
            if (g_pf[i].buf && !g_pf[i].ready) { slot = i; break; }
        }
        if (slot < 0) {
            // Alle Slots belegt — Slot überschreiben, der nicht in der Queue ist
            for (int i = 0; i < SQUEEZE_PREFETCH; i++) {
                if (g_pf[i].buf && g_pf[i].url != url) { slot = i; break; }
            }
        }
        xSemaphoreGive(g_pfMux);

        if (slot < 0 || !g_pf[slot].buf) continue;

        // Download in Slot
        size_t total = 0;
        HTTPClient http;
        http.setTimeout(3000);
        if (http.begin(url)) {
            int code = http.GET();
            if (code == 200) {
                total = readHttpBody(http, g_pf[slot].buf, 131070, 1200);
            }
            http.end();
        }

        bool jpegOk = isCompleteJpeg(g_pf[slot].buf, total);
        xSemaphoreTake(g_pfMux, portMAX_DELAY);
        if (jpegOk) {
            g_pf[slot].len   = total;
            g_pf[slot].ready = true;
            g_pf[slot].url   = url;
            g_pf[slot].key   = key;
            srvLog("[pf] Slot %d: %d B", slot, (int)total);
        }
        xSemaphoreGive(g_pfMux);
    }
    g_pfTask = nullptr;
    vTaskDelete(nullptr);
}

// Aus beliebigem Task aufrufbar (z.B. lmsPoll) — befüllt Prefetch-Queue
void displayQueuePrefetch(const String* urls, const String* keys, int count) {
    if (!g_pfMux) return;
    int limit = max(0, min(count, SQUEEZE_PREFETCH));
    xSemaphoreTake(g_pfMux, portMAX_DELAY);
    g_pfQueueCount = 0;
    for (int i = 0; i < SQUEEZE_PREFETCH; i++) {
        g_pf[i].ready = false;
        g_pf[i].len = 0;
        g_pf[i].url = "";
        g_pf[i].key = "";
    }
    for (int i = 0; i < limit; i++) {
        if (!urls || urls[i].isEmpty()) continue;
        g_pfQueue[g_pfQueueCount] = urls[i];
        g_pfQueueKeys[g_pfQueueCount] = (keys && !keys[i].isEmpty()) ? keys[i] : urls[i];
        g_pfQueueCount++;
    }
    bool startTask = (g_pfQueueCount > 0 && g_pfTask == nullptr);
    xSemaphoreGive(g_pfMux);
    if (startTask) {
        xSemaphoreTake(g_pfMux, portMAX_DELAY);
        if (g_pfTask == nullptr && g_pfQueueCount > 0) {
            xTaskCreatePinnedToCore(prefetchTask, "prefetch", 8192,
                                    nullptr, 1, (TaskHandle_t*)&g_pfTask, 0);
        }
        xSemaphoreGive(g_pfMux);
    }
}

// Aus beliebigem Task aufrufbar (z.B. lmsPoll auf Core 0) — startet Cover-Download sofort
void displayPreloadCover(const String& url, const String& cacheKey) {
    if (url.isEmpty() || !g_coverBuf || !g_coverDlBuf) return;
    String key = cacheKey.isEmpty() ? url : cacheKey;
    xSemaphoreTake(g_coverMux, portMAX_DELAY);
    bool coverTaskRunning = (g_coverTask != nullptr);
    bool alreadyLoaded = (g_coverLen > 0 && g_loadedCoverKey == key);
    xSemaphoreGive(g_coverMux);
    if (coverTaskRunning || alreadyLoaded) return; // Download läuft bereits / schon geladen

    xSemaphoreTake(g_coverMux, portMAX_DELAY);
    g_pendingCoverUrl    = url;
    g_pendingCoverKey    = key;
    g_dlGen              = ++g_coverGen;
    g_coverReady         = false;
    g_coverLen           = 0;
    g_loadedCoverKey     = "";
    g_coverSerial++;
    xTaskCreatePinnedToCore(coverDlTask, "cover", 8192,
                            nullptr, 1, (TaskHandle_t*)&g_coverTask, 0);
    xSemaphoreGive(g_coverMux);
    srvLog("[cover] Preload gestartet (früh)");
}

// ─── Einstellungs-Screen ─────────────────────────────────────────────────────

// ─── Playlist-Screen ─────────────────────────────────────────────────────────

static void drawSettingsScreen(); // Vorwärtsdeklaration für Rücknavigation

static void drawPlaylistList() {
    int listY0 = PL_HDR_H + 4;
    tft.fillRect(0, listY0, DISPLAY_WIDTH, DISPLAY_HEIGHT - listY0, COL_BG);
    String curId = wifiGetPlaylistId();
    int n = (int)g_settPlaylists.size();
    if (n == 0) {
        tft.setFont(nullptr); tft.setTextSize(2);
        tft.setTextDatum(ML_DATUM);
        tft.setTextColor(COL_MGREY, COL_BG);
        tft.drawString(squeezeGetHost().isEmpty()
            ? "Kein Server konfiguriert"
            : "Keine Playlists gefunden",
            12, listY0 + 16);
        return;
    }
    int maxVisible = (DISPLAY_HEIGHT - listY0) / (SET_PLR_H + SET_PLR_GAP);
    int count = min(n, maxVisible);
    for (int i = 0; i < count; i++) {
        int y = listY0 + i * (SET_PLR_H + SET_PLR_GAP);
        bool active = (g_settPlaylists[i].id == curId);
        uint32_t bg     = active ? (uint32_t)COL_ACTIVE : (uint32_t)COL_DARK;
        uint32_t border = active ? (uint32_t)COL_GREEN  : (uint32_t)COL_MID;
        tft.fillRoundRect(4, y, DISPLAY_WIDTH - 8, SET_PLR_H, 5, bg);
        tft.drawRoundRect(4, y, DISPLAY_WIDTH - 8, SET_PLR_H, 5, border);
        tft.setFont(nullptr); tft.setTextSize(2);
        tft.setTextDatum(ML_DATUM);
        tft.setTextColor(COL_WHITE, bg);
        tft.setClipRect(12, y, DISPLAY_WIDTH - 24, SET_PLR_H);
        tft.drawString(utf8ToAscii(g_settPlaylists[i].name).c_str(), 12, y + SET_PLR_H / 2);
        tft.clearClipRect();
    }
}

static void drawPlaylistScreen() {
    g_screen = SCREEN_PLAYLIST;
    tft.fillScreen(COL_BG);
    tft.fillRect(0, 0, DISPLAY_WIDTH, PL_HDR_H, COL_HDR);
    tft.setFont(nullptr); tft.setTextSize(2);
    tft.setTextDatum(ML_DATUM);
    tft.setTextColor(COL_WHITE, COL_HDR);
    tft.drawString("PLAYLIST", 8, PL_HDR_H / 2);
    tft.fillRoundRect(DISPLAY_WIDTH - 62, 3, 58, PL_HDR_H - 6, 4, COL_DARK);
    tft.drawRoundRect(DISPLAY_WIDTH - 62, 3, 58, PL_HDR_H - 6, 4, COL_MID);
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(COL_WHITE, COL_DARK);
    tft.drawString("ZUR.", DISPLAY_WIDTH - 33, PL_HDR_H / 2);
    // "Lade..."-Hinweis während Netzwerkzugriff
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(COL_YELLOW, COL_BG);
    tft.drawString("Lade...", DISPLAY_WIDTH / 2, DISPLAY_HEIGHT / 2);
    g_settPlaylists.clear();
    if (!squeezeGetHost().isEmpty()) squeezeGetPlaylists(g_settPlaylists);
    drawPlaylistList();
}

static void handlePlaylistTouch(int32_t tx, int32_t ty) {
    if (ty < PL_HDR_H && tx > DISPLAY_WIDTH - 62) {
        drawSettingsScreen();
        return;
    }
    int listY0 = PL_HDR_H + 4;
    if (ty >= listY0 && !g_settPlaylists.empty()) {
        int stride = SET_PLR_H + SET_PLR_GAP;
        int idx = (ty - listY0) / stride;
        if ((ty - listY0) % stride < SET_PLR_H && idx < (int)g_settPlaylists.size()) {
            wifiSetPlaylist(g_settPlaylists[idx].id, g_settPlaylists[idx].name, true);
            displayDrawPlayerScreen();
        }
    }
}

static void drawSettingsSrvRow() {
    String srvTxt = squeezeGetHost().isEmpty()
        ? "Kein Server"
        : (squeezeGetHost() + ":" + String(squeezeGetPort()));
    tft.fillRoundRect(SET_BOX_X, SET_SRV_Y, SET_BOX_W, 34, 4, COL_DARK);
    tft.drawRoundRect(SET_BOX_X, SET_SRV_Y, SET_BOX_W, 34, 4, COL_MID);
    tft.setFont(nullptr); tft.setTextSize(2);
    tft.setTextDatum(ML_DATUM);
    tft.setTextColor(COL_LGREY, COL_DARK);
    tft.setClipRect(SET_BOX_X + 4, SET_SRV_Y, SET_BOX_W - 8, 34);
    tft.drawString(srvTxt.c_str(), SET_BOX_X + 8, SET_SRV_Y + 17);
    tft.clearClipRect();
    tft.fillRoundRect(SET_BTN_X, SET_SRV_Y, SET_BTN_W, 34, 4, COL_DARK);
    tft.drawRoundRect(SET_BTN_X, SET_SRV_Y, SET_BTN_W, 34, 4, COL_MID);
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(COL_WHITE, COL_DARK);
    tft.drawString("SUCHEN", SET_BTN_X + SET_BTN_W / 2, SET_SRV_Y + 17);
}

static void drawSettingsDiscRow() {
    tft.fillRect(0, SET_DISC_Y, DISPLAY_WIDTH, 36, COL_BG);
    if (!g_settSearched) return;
    if (g_settDiscFound) {
        String discTxt = g_settDiscHost + ":" + String(g_settDiscPort);
        tft.fillRoundRect(SET_BOX_X, SET_DISC_Y, SET_BOX_W, 34, 4, (uint32_t)0x003300);
        tft.drawRoundRect(SET_BOX_X, SET_DISC_Y, SET_BOX_W, 34, 4, COL_GREEN);
        tft.setFont(nullptr); tft.setTextSize(2);
        tft.setTextDatum(ML_DATUM);
        tft.setTextColor(COL_GREEN, (uint32_t)0x003300);
        tft.drawString(discTxt.c_str(), SET_BOX_X + 8, SET_DISC_Y + 17);
        tft.fillRoundRect(SET_BTN_X, SET_DISC_Y, SET_BTN_W, 34, 4, COL_GREEN);
        tft.drawRoundRect(SET_BTN_X, SET_DISC_Y, SET_BTN_W, 34, 4, (uint32_t)0x00AA00);
        tft.setTextDatum(MC_DATUM);
        tft.setTextColor(COL_BG, COL_GREEN);
        tft.drawString("UEBERN.", SET_BTN_X + SET_BTN_W / 2, SET_DISC_Y + 17);
    } else {
        tft.setFont(nullptr); tft.setTextSize(2);
        tft.setTextDatum(ML_DATUM);
        tft.setTextColor(COL_MGREY, COL_BG);
        tft.drawString("Kein Server gefunden", SET_BOX_X + 8, SET_DISC_Y + 17);
    }
}

static void drawSettingsSyncRow() {
    bool enabled = wifiGetSyncVolume();
    tft.fillRect(0, SET_SYNC_Y, DISPLAY_WIDTH, 36, COL_BG);
    tft.fillRoundRect(SET_BOX_X, SET_SYNC_Y, SET_BOX_W, 34, 4,
                      enabled ? (uint32_t)0x003300 : COL_DARK);
    tft.drawRoundRect(SET_BOX_X, SET_SYNC_Y, SET_BOX_W, 34, 4,
                      enabled ? COL_GREEN : COL_MID);
    tft.setFont(nullptr); tft.setTextSize(2);
    tft.setTextDatum(ML_DATUM);
    tft.setTextColor(enabled ? COL_GREEN : COL_LGREY,
                     enabled ? (uint32_t)0x003300 : COL_DARK);
    tft.drawString("SYNC-LAUTSTAERKE", SET_BOX_X + 8, SET_SYNC_Y + 17);
    tft.fillRoundRect(SET_BTN_X, SET_SYNC_Y, SET_BTN_W, 34, 4,
                      enabled ? COL_GREEN : COL_DARK);
    tft.drawRoundRect(SET_BTN_X, SET_SYNC_Y, SET_BTN_W, 34, 4,
                      enabled ? (uint32_t)0x00AA00 : COL_MID);
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(enabled ? COL_BG : COL_WHITE,
                     enabled ? COL_GREEN : COL_DARK);
    tft.drawString(enabled ? "AN" : "AUS",
                   SET_BTN_X + SET_BTN_W / 2, SET_SYNC_Y + 17);
}

static void drawSettingsPlayerList() {
    tft.fillRect(0, SET_PLR_Y0, DISPLAY_WIDTH, DISPLAY_HEIGHT - SET_PLR_Y0, COL_BG);
    String curMac = wifiGetPlayerMac();
    int n = (int)g_settPlayers.size();
    if (n == 0) {
        tft.setFont(nullptr); tft.setTextSize(2);
        tft.setTextDatum(ML_DATUM);
        tft.setTextColor(COL_MGREY, COL_BG);
        tft.drawString(squeezeGetHost().isEmpty()
            ? "Kein Server konfiguriert"
            : "Kein Player gefunden",
            12, SET_PLR_Y0 + 16);
        return;
    }
    int maxVisible = (DISPLAY_HEIGHT - SET_PLR_Y0) / (SET_PLR_H + SET_PLR_GAP);
    int count = min(n, maxVisible);
    for (int i = 0; i < count; i++) {
        int y = SET_PLR_Y0 + i * (SET_PLR_H + SET_PLR_GAP);
        bool active    = (g_settPlayers[i].mac == curMac);
        bool connected = g_settPlayers[i].connected;
        uint32_t bg     = active ? (uint32_t)COL_ACTIVE : (uint32_t)COL_DARK;
        uint32_t border = active ? (uint32_t)COL_GREEN  : (uint32_t)COL_MID;
        uint32_t fg     = connected ? (uint32_t)COL_WHITE : (uint32_t)COL_MGREY;
        tft.fillRoundRect(4, y, DISPLAY_WIDTH - 8, SET_PLR_H, 5, bg);
        tft.drawRoundRect(4, y, DISPLAY_WIDTH - 8, SET_PLR_H, 5, border);
        tft.fillCircle(22, y + SET_PLR_H / 2, 5,
                       connected ? (uint32_t)COL_GREEN : (uint32_t)COL_MGREY);
        tft.setFont(nullptr); tft.setTextSize(2);
        tft.setTextDatum(ML_DATUM);
        tft.setTextColor(fg, bg);
        tft.setClipRect(36, y, DISPLAY_WIDTH - 52, SET_PLR_H);
        tft.drawString(utf8ToAscii(g_settPlayers[i].name).c_str(), 36, y + SET_PLR_H / 2);
        tft.clearClipRect();
    }
}

static void drawSettingsScreen() {
    g_screen        = SCREEN_SETTINGS;
    g_settDiscFound = false;
    g_settSearched  = false;
    tft.fillScreen(COL_BG);
    // Header
    tft.fillRect(0, 0, DISPLAY_WIDTH, PL_HDR_H, COL_HDR);
    tft.setFont(nullptr); tft.setTextSize(2);
    tft.setTextDatum(ML_DATUM);
    tft.setTextColor(COL_WHITE, COL_HDR);
    tft.drawString("EINSTELLUNGEN", 8, PL_HDR_H / 2);
    tft.fillRoundRect(DISPLAY_WIDTH - 62, 3, 58, PL_HDR_H - 6, 4, COL_DARK);
    tft.drawRoundRect(DISPLAY_WIDTH - 62, 3, 58, PL_HDR_H - 6, 4, COL_MID);
    tft.fillRoundRect(SET_LISTE_X, 3, SET_LISTE_W, PL_HDR_H - 6, 4, COL_DARK);
    tft.drawRoundRect(SET_LISTE_X, 3, SET_LISTE_W, PL_HDR_H - 6, 4, COL_MID);
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(COL_WHITE, COL_DARK);
    tft.drawString("ZUR.", DISPLAY_WIDTH - 33, PL_HDR_H / 2);
    tft.drawString("LISTE", SET_LISTE_X + SET_LISTE_W / 2, PL_HDR_H / 2);
    // Abschnitt: Server
    tft.setFont(nullptr); tft.setTextSize(1);
    tft.setTextDatum(ML_DATUM);
    tft.setTextColor(COL_MGREY, COL_BG);
    tft.drawString("SERVER", 6, 39);
    drawSettingsSrvRow();
    drawSettingsDiscRow();
    drawSettingsSyncRow();
    // Trennlinie
    tft.fillRect(0, 170, DISPLAY_WIDTH, 1, COL_MID);
    // Abschnitt: Player
    tft.setFont(nullptr); tft.setTextSize(1);
    tft.setTextDatum(ML_DATUM);
    tft.setTextColor(COL_MGREY, COL_BG);
    tft.drawString("PLAYER", 6, 181);
    // Player-Liste laden
    g_settPlayers.clear();
    if (!squeezeGetHost().isEmpty()) squeezeGetPlayers(g_settPlayers);
    drawSettingsPlayerList();
}

static void handleSettingsTouch(int32_t tx, int32_t ty) {
    // LISTE-Button → Playlist-Screen
    if (ty < PL_HDR_H && tx >= SET_LISTE_X && tx < SET_LISTE_X + SET_LISTE_W) {
        drawPlaylistScreen();
        return;
    }
    // ZURÜCK-Button
    if (ty < PL_HDR_H && tx > DISPLAY_WIDTH - 62) {
        displayDrawPlayerScreen();
        return;
    }
    // SUCHEN-Button
    if (ty >= SET_SRV_Y && ty < SET_SRV_Y + 34 && tx >= SET_BTN_X) {
        tft.fillRoundRect(SET_BTN_X, SET_SRV_Y, SET_BTN_W, 34, 4, COL_DARK);
        tft.drawRoundRect(SET_BTN_X, SET_SRV_Y, SET_BTN_W, 34, 4, COL_MID);
        tft.setFont(nullptr); tft.setTextSize(2); tft.setTextDatum(MC_DATUM);
        tft.setTextColor(COL_YELLOW, COL_DARK);
        tft.drawString("Suche...", SET_BTN_X + SET_BTN_W / 2, SET_SRV_Y + 17);
        g_settDiscFound = squeezeDiscover(g_settDiscHost, g_settDiscPort, 3000);
        g_settSearched  = true;
        tft.fillRoundRect(SET_BTN_X, SET_SRV_Y, SET_BTN_W, 34, 4, COL_DARK);
        tft.drawRoundRect(SET_BTN_X, SET_SRV_Y, SET_BTN_W, 34, 4, COL_MID);
        tft.setTextDatum(MC_DATUM);
        tft.setTextColor(COL_WHITE, COL_DARK);
        tft.drawString("SUCHEN", SET_BTN_X + SET_BTN_W / 2, SET_SRV_Y + 17);
        drawSettingsDiscRow();
        return;
    }
    // ÜBERNEHMEN-Button (nur nach erfolgter Suche mit Ergebnis)
    if (g_settDiscFound && ty >= SET_DISC_Y && ty < SET_DISC_Y + 34 && tx >= SET_BTN_X) {
        wifiSetServer(g_settDiscHost, g_settDiscPort);
        drawSettingsSrvRow();
        g_settPlayers.clear();
        squeezeGetPlayers(g_settPlayers);
        drawSettingsPlayerList();
        return;
    }
    // Gruppenlautstärke ein/aus
    if (ty >= SET_SYNC_Y && ty < SET_SYNC_Y + 34 && tx >= SET_BOX_X) {
        wifiSetSyncVolume(!wifiGetSyncVolume());
        drawSettingsSyncRow();
        return;
    }
    // Player-Auswahl
    if (ty >= SET_PLR_Y0 && !g_settPlayers.empty()) {
        int stride = SET_PLR_H + SET_PLR_GAP;
        int plrIdx = (ty - SET_PLR_Y0) / stride;
        if ((ty - SET_PLR_Y0) % stride < SET_PLR_H &&
                plrIdx < (int)g_settPlayers.size()) {
            wifiSetPlayer(g_settPlayers[plrIdx].mac);
            drawSettingsPlayerList();
        }
    }
}

// ─── Player-Screen ────────────────────────────────────────────────────────────

void displayDrawPlayerScreen() {
    g_screen = SCREEN_PLAYER;
    xSemaphoreTake(g_coverMux, portMAX_DELAY);

    g_dTitle = g_dArtist = g_dAlbum = g_dCoverUrl = g_dPlayer = "";
    g_dCoverKey = "";
    g_dPlaying = false; g_dMuted = false; g_dVolume = g_dRepeat = g_dShuffle = -1;
    g_dPosition = -1;   g_dDuration = 0; g_dTrackNum = -1;
    g_lastProgFilled = -1;
    g_lastVolFilled  = -1;
    g_lastVolMuted   = false;
    g_scrollOff[0] = g_scrollOff[1] = g_scrollOff[2] = 0;
    g_scrollLast = 0;
    g_coverDrawnForTitle = "";
    g_coverPredrawn = false;
    g_coverCandidateUrl = "";
    g_coverCandidateKey = "";
    g_coverCandidateCount = 0;
    g_coverReady = false;
    g_coverLen   = 0;
    g_loadedCoverKey = "";
    xSemaphoreGive(g_coverMux);

    tft.fillScreen(COL_BG);
    drawPlayerHeader("Squeezie", false);
    drawCoverPlaceholder();
    tft.fillRect(PL_INFO_X, PL_INFO_Y, PL_INFO_W, PL_COVER_H, COL_BG);
    drawProgressBar(0, 0);
    drawTransportButtons(false);
    drawVolumeButtons(false);
    drawVolumeRow(0, false);
    drawModeRow(0, 0);
}

void displayUpdatePlayer() {
    if (g_screen != SCREEN_PLAYER) return;
    tft.startWrite(); // SPI-Bus acquiren; laufende DMA-Transfers abwarten
    const SqueezieStatus& s = squeezieGetCachedStatus();

    // Fertiges Cover zeichnen (asynchron heruntergeladen)
    xSemaphoreTake(g_coverMux, portMAX_DELAY);
    bool coverReady = g_coverReady;
    bool coverTaskRunning = (g_coverTask != nullptr);
    bool alreadyDispatched = (g_dispatchedCoverUrl == g_dCoverUrl && g_dispatchedCoverKey == g_dCoverKey);
    size_t coverLen = g_coverLen;
    String loadedCoverKey = g_loadedCoverKey;
    xSemaphoreGive(g_coverMux);
    if (coverReady) {
        xSemaphoreTake(g_coverMux, portMAX_DELAY);
        g_coverReady = false;
        bool keyMatches = (!s.coverKey.isEmpty() && g_loadedCoverKey == s.coverKey);
        if (g_coverLen > 0 && keyMatches) {
            srvLog("[cover] drawJpg %d bytes", (int)g_coverLen);
            drawCoverImage(g_coverBuf, g_coverLen);
            g_coverDrawnForTitle = s.title;
        } else if (g_coverLen > 0) {
            srvLog("[cover] verwerfe Key-Mismatch");
            g_coverLen = 0;
            g_loadedCoverKey = "";
            drawCoverPlaceholder();
        } else {
            srvLog("[cover] kein Bild, Placeholder");
            drawCoverPlaceholder();
        }
        coverTaskRunning = (g_coverTask != nullptr);
        alreadyDispatched = (g_dispatchedCoverUrl == g_dCoverUrl && g_dispatchedCoverKey == g_dCoverKey);
        coverLen = g_coverLen;
        loadedCoverKey = g_loadedCoverKey;
        xSemaphoreGive(g_coverMux);
    }

    // Preloaded-Cover: beim ersten bekannten Titel als gezeichnet markieren → kein Re-Download
    if (g_coverPredrawn && !s.title.isEmpty()) {
        g_coverDrawnForTitle = s.title;
        g_coverPredrawn = false;
    }

    // Cover neu laden wenn der LMS denselben Cover-Key in zwei Polls bestätigt.
    bool coverUrlChanged = (s.coverUrl != g_dCoverUrl);
    bool coverKeyChanged = (s.coverKey != g_dCoverKey);
    bool coverChanged    = (coverUrlChanged || coverKeyChanged);
    bool coverStable     = false;
    if (coverChanged) {
        if (s.coverUrl == g_coverCandidateUrl && s.coverKey == g_coverCandidateKey) {
            if (g_coverCandidateCount < 2) g_coverCandidateCount++;
        } else {
            g_coverCandidateUrl = s.coverUrl;
            g_coverCandidateKey = s.coverKey;
            g_coverCandidateCount = 1;
        }
        coverStable = (g_coverCandidateCount >= 2);
    } else {
        g_coverCandidateUrl = "";
        g_coverCandidateKey = "";
        g_coverCandidateCount = 0;
        if (!s.title.isEmpty()) g_coverDrawnForTitle = s.title;
    }
    if (coverChanged && coverStable) {
        g_dCoverUrl = s.coverUrl;
        g_dCoverKey = s.coverKey;
        g_coverCandidateUrl = "";
        g_coverCandidateKey = "";
        g_coverCandidateCount = 0;
        xSemaphoreTake(g_coverMux, portMAX_DELAY);
        g_coverGen++;
        g_dispatchedCoverUrl = "";
        g_dispatchedCoverKey = "";
        if (s.coverUrl.isEmpty()) {
            g_coverReady = false;
            g_coverLen   = 0;
            g_loadedCoverKey = "";
            g_coverSerial++;
            xSemaphoreGive(g_coverMux);
            drawCoverPlaceholder();
            g_coverDrawnForTitle = s.title;
        } else {
            g_coverReady = false;
            g_coverLen   = 0;
            g_loadedCoverKey = "";
            xSemaphoreGive(g_coverMux);
            drawCoverPlaceholder();
        }
    }
    // Download starten wenn URL gesetzt, Puffer da, kein Task läuft und noch nicht dispatcht
    xSemaphoreTake(g_coverMux, portMAX_DELAY);
    coverTaskRunning = (g_coverTask != nullptr);
    alreadyDispatched = (g_dispatchedCoverUrl == g_dCoverUrl && g_dispatchedCoverKey == g_dCoverKey);
    coverReady = g_coverReady;
    coverLen = g_coverLen;
    loadedCoverKey = g_loadedCoverKey;
    xSemaphoreGive(g_coverMux);
    if (!g_dCoverUrl.isEmpty() && g_coverBuf && !coverTaskRunning && !alreadyDispatched) {
        // Bereits bereit? (Prefetch-Hit via displayPreloadCover oder Preload)
        if (coverReady && coverLen > 0 && !g_dCoverKey.isEmpty() && loadedCoverKey == g_dCoverKey) {
            xSemaphoreTake(g_coverMux, portMAX_DELAY);
            g_dispatchedCoverUrl = g_dCoverUrl; // kein Download nötig
            g_dispatchedCoverKey = g_dCoverKey;
            xSemaphoreGive(g_coverMux);
        } else {
            xSemaphoreTake(g_coverMux, portMAX_DELAY);
            g_coverReady         = false;
            g_coverLen           = 0;
            g_loadedCoverKey     = "";
            g_pendingCoverUrl    = g_dCoverUrl;
            g_pendingCoverKey    = g_dCoverKey;
            g_dlGen              = g_coverGen;
            g_dispatchedCoverUrl = g_dCoverUrl;
            g_dispatchedCoverKey = g_dCoverKey;
            xTaskCreatePinnedToCore(coverDlTask, "cover", 8192,
                                    nullptr, 1, (TaskHandle_t*)&g_coverTask, 0);
            xSemaphoreGive(g_coverMux);
        }
    }

    // Header: nur neu zeichnen wenn Spielername wechselt
    if (s.playerName != g_dPlayer) {
        drawPlayerHeader(s.playerName.c_str(), s.playing);
        g_dPlayer = s.playerName;
    }

    // Uhrzeit: minütlich aktualisieren
    {
        time_t tnow = time(nullptr);
        struct tm ti; localtime_r(&tnow, &ti);
        if (ti.tm_year > 100 && ti.tm_min != g_lastClockMin) {
            g_lastClockMin = ti.tm_min;
            char tbuf[6];
            snprintf(tbuf, sizeof(tbuf), "%02d:%02d", ti.tm_hour, ti.tm_min);
            tft.setFont(nullptr); tft.setTextSize(2);
            tft.setTextDatum(MC_DATUM);
            tft.setTextColor(COL_MGREY, COL_HDR);
            tft.drawString(tbuf, DISPLAY_WIDTH / 2, PL_HDR_H / 2);
        }
    }

    // Play-Status: nur Play-Button + Dot selektiv → kein fillRect auf Transport-Zeile
    if (s.playing != g_dPlaying) {
        int cy = PL_TRANS_CY;
        uint32_t bg = s.playing ? (uint32_t)COL_DARK  : (uint32_t)COL_GREEN;
        uint32_t fg = s.playing ? (uint32_t)COL_GREEN : (uint32_t)COL_BG;
        drawCircleBtn(240, cy, 35, bg, fg, s.playing ? "pause" : "play");
        tft.fillCircle(DISPLAY_WIDTH - 85, PL_HDR_H / 2, 5,
                        s.playing ? (uint32_t)COL_GREEN : (uint32_t)COL_MID);
        g_dPlaying = s.playing;
    }

    // Textinfo: Titel / Künstler / Album scrollen, Track-Zeile sitzt bewusst tiefer,
    // damit sie von den größeren Unicode-Fonts nicht überzeichnet wird.
    bool infoChanged = (s.title != g_dTitle || s.artist != g_dArtist || s.album != g_dAlbum);
    if (infoChanged) {
        tft.fillRect(PL_INFO_X, PL_INFO_Y, PL_INFO_W, 116, COL_BG);
        // Scroll-Offsets bei Titelwechsel zurücksetzen
        g_scrollOff[0] = g_scrollOff[1] = g_scrollOff[2] = 0;
        g_scrollLast = millis();
        g_dTitle  = s.title;  g_dArtist  = s.artist;  g_dAlbum  = s.album;
        g_dTitleD = s.title;
        g_dArtistD = s.artist;
        g_dAlbumD  = s.album;
    }

    // Textbreiten cachen — nur bei Titelwechsel neu messen
    static int s_tw0 = 0, s_tw1 = 0, s_tw2 = 0;
    if (infoChanged) {
        setPlayerInfoFont(3); s_tw0 = tft.textWidth(g_dTitleD.c_str());
        setPlayerInfoFont(2); s_tw1 = tft.textWidth(g_dArtistD.c_str());
                             s_tw2 = tft.textWidth(g_dAlbumD.c_str());
    }

    // Scroll-Tick: Fraktionaler Akkumulator → funktioniert für alle scrollMs-Werte
    uint32_t now = millis();
    int scrollMs = wifiGetScrollSpeed();
    if (scrollMs < 1) scrollMs = 1;
    static uint32_t s_scrollAccum = 0;
    s_scrollAccum += (now - g_scrollLast);
    g_scrollLast = now;
    int tickPx = (int)(s_scrollAccum / (uint32_t)scrollMs);
    if (tickPx > 0) s_scrollAccum -= (uint32_t)tickPx * (uint32_t)scrollMs;
    if (infoChanged) s_scrollAccum = 0;
    bool doScroll = (tickPx > 0);

    // Welche Zeilen scrollen tatsächlich?
    bool l0 = (s_tw0 + 4 > PL_INFO_W);
    bool l1 = (s_tw1 + 4 > PL_INFO_W);
    bool l2 = (s_tw2 + 4 > PL_INFO_W);

    if (doScroll) {
        if (l0) { g_scrollOff[0] += tickPx; if (g_scrollOff[0] > s_tw0 + 30) g_scrollOff[0] = 0; }
        if (l1) { g_scrollOff[1] += tickPx; if (g_scrollOff[1] > s_tw1 + 30) g_scrollOff[1] = 0; }
        if (l2) { g_scrollOff[2] += tickPx; if (g_scrollOff[2] > s_tw2 + 30) g_scrollOff[2] = 0; }
    }

    // Nicht-scrollende Zeilen nur bei Inhaltswechsel zeichnen → kein Flackern
    if (infoChanged || (doScroll && l0)) drawPlayerScrollLine(g_dTitleD.c_str(),  PL_INFO_X, PL_INFO_Y + 2,  PL_INFO_W, COL_WHITE, 3, g_scrollOff[0], true);
    if (infoChanged || (doScroll && l1)) drawPlayerScrollLine(g_dArtistD.c_str(), PL_INFO_X, PL_INFO_Y + 34, PL_INFO_W, COL_GREEN, 2, g_scrollOff[1]);
    if (infoChanged || (doScroll && l2)) drawPlayerScrollLine(g_dAlbumD.c_str(),  PL_INFO_X, PL_INFO_Y + 58, PL_INFO_W, COL_LGREY, 2, g_scrollOff[2]);
    if (infoChanged || s.duration != g_dDuration || s.trackNum != g_dTrackNum) {
        char buf[32];
        snprintf(buf, sizeof(buf), "Track %d  |  %d:%02d",
                 s.trackNum, s.duration / 60, s.duration % 60);
        drawInfoLine(buf, PL_INFO_X, PL_INFO_Y + 92, PL_INFO_W, COL_MGREY, 2);
        g_dDuration = s.duration;
        g_dTrackNum = s.trackNum;
    }

    // Neue LMS-Position → Interpolationsbasis resetten
    if (s.position != g_lmsPos) {
        g_lmsPos     = s.position;
        g_localPos   = s.position;
        g_localPosMs = millis();
    }
    // Zwischen Polls: lokale Zeit-Interpolation (flüssig, keine Sprünge)
    int dispPos = g_localPos;
    if (s.playing && s.duration > 0) {
        dispPos = constrain(g_localPos + (int)((millis() - g_localPosMs) / 1000),
                            0, s.duration);
    }
    if (dispPos != g_dPosition || s.duration != g_dDuration) {
        drawProgressBar(dispPos, s.duration);
        g_dPosition = dispPos;
    }
    if (s.muted != g_dMuted) {
        drawVolumeButtons(s.muted);
        g_dMuted = s.muted;
    }
    if (s.volume != g_dVolume || s.muted != g_lastVolMuted) {
        drawVolumeRow(s.volume, s.muted);
        g_dVolume = s.volume;
    }
    if (s.repeatMode != g_dRepeat || s.shuffleMode != g_dShuffle) {
        drawModeRow(s.repeatMode, s.shuffleMode);
        g_dRepeat  = s.repeatMode;
        g_dShuffle = s.shuffleMode;
    }
    tft.endWrite(); // DMA abgeschlossen, Bus für Touch freigeben
}

// ─── OTA-Fortschrittsanzeige ─────────────────────────────────────────────────

void displayShowOTA(int pct) {
    g_screen = SCREEN_STARTUP; // verhindert displayUpdatePlayer() im Loop
    static bool drawn = false;
    if (pct < 0) pct = 0;
    if (pct > 100) pct = 100;
    if (!drawn) {
        drawn = true;
        tft.fillScreen(COL_BG);
        tft.setTextDatum(MC_DATUM);
        tft.setTextColor(COL_GREEN, COL_BG);
        tft.setFont(nullptr);
        tft.setTextSize(3);
        tft.drawString("OTA Update", DISPLAY_WIDTH / 2, DISPLAY_HEIGHT / 2 - 50);
        tft.setTextColor(COL_LGREY, COL_BG);
        tft.setTextSize(2);
        tft.drawString("Bitte nicht trennen!", DISPLAY_WIDTH / 2, DISPLAY_HEIGHT / 2 - 10);
    }
    // Fortschrittsbalken
    int bx = 20, by = DISPLAY_HEIGHT / 2 + 20, bw = DISPLAY_WIDTH - 40, bh = 24;
    tft.fillRect(bx, by, bw, bh, COL_PROG_BG);
    int filled = pct * bw / 100;
    if (filled > 0) tft.fillRect(bx, by, filled, bh, COL_GREEN);
    char buf[8]; snprintf(buf, sizeof(buf), "%d%%", pct);
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(COL_WHITE, COL_PROG_BG);
    tft.setFont(nullptr);
    tft.setTextSize(2);
    tft.drawString(buf, DISPLAY_WIDTH / 2, by + bh / 2);
    if (pct >= 100) {
        drawn = false; // Reset für nächstes OTA
        tft.fillRect(bx, by + bh + 10, bw, 30, COL_BG);
        tft.setTextColor(COL_GREEN, COL_BG);
        tft.drawString("Neustart...", DISPLAY_WIDTH / 2, by + bh + 25);
    }
}

// ─── Factory-Reset-Warnung ────────────────────────────────────────────────────

void displayShowFactoryReset(uint8_t secsLeft) {
    g_screen = SCREEN_STARTUP; // verhindert displayUpdatePlayer() im Loop
    static uint8_t lastSecs = 255;
    if (secsLeft == lastSecs) return; // nur neu zeichnen wenn sich der Countdown ändert
    lastSecs = secsLeft;

    tft.fillScreen(COL_BG);
    tft.setTextDatum(MC_DATUM);
    tft.setFont(nullptr);

    tft.setTextColor((uint32_t)0xFF2200, COL_BG);
    tft.setTextSize(3);
    tft.drawString("FACTORY RESET", DISPLAY_WIDTH / 2, DISPLAY_HEIGHT / 2 - 60);

    if (secsLeft == 0) {
        tft.setTextColor(COL_LGREY, COL_BG);
        tft.setTextSize(2);
        tft.drawString("Alle Einstellungen werden", DISPLAY_WIDTH / 2, DISPLAY_HEIGHT / 2 - 10);
        tft.drawString("geloescht...", DISPLAY_WIDTH / 2, DISPLAY_HEIGHT / 2 + 20);
        lastSecs = 255; // nächsten Aufruf wieder frisch zeichnen lassen
    } else {
        char buf[24];
        snprintf(buf, sizeof(buf), "In %d Sekunde%s...", secsLeft, secsLeft == 1 ? "" : "n");
        tft.setTextColor(COL_WHITE, COL_BG);
        tft.setTextSize(2);
        tft.drawString(buf, DISPLAY_WIDTH / 2, DISPLAY_HEIGHT / 2 - 10);
        tft.setTextColor(COL_MGREY, COL_BG);
        tft.setTextSize(1);
        tft.drawString("BOOT loslassen zum Abbrechen", DISPLAY_WIDTH / 2, DISPLAY_HEIGHT / 2 + 30);
    }
}

// ─── Touch-Behandlung ────────────────────────────────────────────────────────

void displayHandleTouch() {
    static bool     lastPressed = false;
    static uint32_t calHoldT0  = 0;
    static bool     calHeld    = false;
    static bool     volHeld    = false;
    static uint32_t volHoldT0  = 0;

    int32_t tx, ty;
    bool pressed = xpt2046Read(&tx, &ty);

    if (g_screen == SCREEN_SETTINGS) {
        if (pressed && !lastPressed) handleSettingsTouch(tx, ty);
        lastPressed = pressed;
        return;
    }
    if (g_screen == SCREEN_PLAYLIST) {
        if (pressed && !lastPressed) handlePlaylistTouch(tx, ty);
        lastPressed = pressed;
        return;
    }
    if (g_screen != SCREEN_PLAYER) { lastPressed = pressed; return; }

    // ── Kalibrierung: obere linke Ecke 3 Sek. halten ────────────────────────
    if (pressed && tx < 80 && ty < PL_HDR_H) {
        if (!calHeld) { calHeld = true; calHoldT0 = millis(); }
        if (millis() - calHoldT0 >= 3000) {
            calHeld = false;
            runCalibration();
            displayDrawPlayerScreen();
            lastPressed = true;
            return;
        }
        lastPressed = pressed;
        return;
    }
    calHeld = false;

    // ── Einzelklick-Aktionen (nur beim ersten Frame des Drückens) ─────────────
    if (pressed && !lastPressed) {
        String mac = wifiGetPlayerMac();

        // EINST.-Button (Header, rechte Seite) → Einstellungs-Screen
        if (ty < PL_HDR_H && tx > DISPLAY_WIDTH - 62) {
            drawSettingsScreen();
            lastPressed = true;
            return;
        }

        // Transport-Buttons (Kreise, Radien angepasst auf neue Größe)
        int cy = PL_TRANS_CY;
        if ((long)(tx - 90) * (tx - 90) + (long)(ty - cy) * (ty - cy) <= 28L * 28L) {
            if (squeezePrev(mac)) wifiForceRepoll();
        } else if ((long)(tx - 390) * (tx - 390) + (long)(ty - cy) * (ty - cy) <= 28L * 28L) {
            if (squeezeNext(mac)) wifiForceRepoll();
        } else if ((long)(tx - 240) * (tx - 240) + (long)(ty - cy) * (ty - cy) <= 33L * 33L) {
            bool ok = g_dPlaying ? squeezePause(mac) : squeezePlay(mac);
            if (ok) {
                SqueezieStatus cached = squeezieGetCachedStatus();
                cached.playing = !cached.playing;
                wifiUpdateCachedStatus(cached);
                wifiForceRepoll();
            }
        }
        // Fortschrittsbalken: Seek
        else if (ty >= PL_PROG_Y && ty < PL_TRANS_Y && g_dDuration > 0) {
            int seekPos = (int)((long)tx * g_dDuration / DISPLAY_WIDTH);
            seekPos = constrain(seekPos, 0, g_dDuration);
            if (squeezeSeek(mac, seekPos)) {
                SqueezieStatus cached = squeezieGetCachedStatus();
                cached.position = seekPos;
                wifiUpdateCachedStatus(cached);
                wifiForceRepoll();
            }
        }
        // Modus-Buttons
        else if (ty >= PL_MODE_Y) {
            if (tx < 240) {
                int next = (max(0, g_dRepeat) + 1) % 3;
                if (squeezeSetRepeat(mac, next)) {
                    SqueezieStatus cached = squeezieGetCachedStatus();
                    cached.repeatMode = next;
                    wifiUpdateCachedStatus(cached);
                    g_dRepeat = next;
                    drawModeRow(g_dRepeat, g_dShuffle);
                    wifiForceRepoll();
                }
            } else {
                int next = (max(0, g_dShuffle) + 1) % 3;
                if (squeezeSetShuffle(mac, next)) {
                    SqueezieStatus cached = squeezieGetCachedStatus();
                    cached.shuffleMode = next;
                    wifiUpdateCachedStatus(cached);
                    g_dShuffle = next;
                    drawModeRow(g_dRepeat, g_dShuffle);
                    wifiForceRepoll();
                }
            }
        }
    }

    // ── Lautstärke ────────────────────────────────────────────────────────────
    if (pressed && ty >= PL_VOL_Y && ty < PL_MODE_Y) {
        const int bx = PL_VOL_BAR_X, bw = PL_VOL_BAR_W;
        bool inVolMinus = (tx >= PL_VOL_MINUS_X && tx <= PL_VOL_MINUS_X + PL_VOL_BTN_W);
        bool inVolPlus  = (tx >= PL_VOL_PLUS_X  && tx <= PL_VOL_PLUS_X  + PL_VOL_BTN_W);
        bool inMute     = (tx >= PL_VOL_MUTE_X  && tx <= PL_VOL_MUTE_X  + PL_VOL_BTN_W);
        bool inBar      = (tx >= bx  && tx <= bx + bw);
        if (inMute) {
            if (!lastPressed && squeezeToggleMute(wifiGetPlayerMac())) {
                SqueezieStatus cached = squeezieGetCachedStatus();
                cached.muted = !cached.muted && cached.volume > 0;
                wifiUpdateCachedStatus(cached);
                wifiForceRepoll();
            }
            volHeld = false;
        } else if (g_dMuted) {
            volHeld = false;
        } else if (inBar) {
            // Direkt nach Touch-Position setzen (stufenlos)
            int pct = constrain((tx - bx) * 100 / bw, 0, 100);
            if (!volHeld || millis() - volHoldT0 > 80) {
                if (squeezeSetVolume(wifiGetPlayerMac(), pct)) {
                    SqueezieStatus cached = squeezieGetCachedStatus();
                    cached.volume = pct;
                    cached.muted = false;
                    wifiUpdateCachedStatus(cached);
                    wifiForceRepoll();
                    volHeld   = true;
                    volHoldT0 = millis();
                }
            }
        } else if (inVolMinus || inVolPlus) {
            if (!volHeld || millis() - volHoldT0 > 250) {
                int nextVolume = constrain(g_dVolume + (inVolMinus ? -1 : 1), 0, 100);
                if (squeezeSetVolume(wifiGetPlayerMac(), nextVolume)) {
                    SqueezieStatus cached = squeezieGetCachedStatus();
                    cached.volume = nextVolume;
                    cached.muted = false;
                    wifiUpdateCachedStatus(cached);
                    wifiForceRepoll();
                    volHeld   = true;
                    volHoldT0 = millis();
                }
            }
        } else { volHeld = false; }
    } else { volHeld = false; }

    lastPressed = pressed;
}
