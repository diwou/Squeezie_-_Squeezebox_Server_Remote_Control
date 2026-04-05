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
enum Screen { SCREEN_NONE, SCREEN_STARTUP, SCREEN_PLAYER, SCREEN_SETTINGS };
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

// Settings-Screen Layout
#define SET_BOX_X    4
#define SET_BOX_W    336
#define SET_BTN_X    344
#define SET_BTN_W    132
#define SET_SRV_Y    50
#define SET_DISC_Y   90
#define SET_PLR_Y0   154
#define SET_PLR_H    38
#define SET_PLR_GAP  3

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
static String  g_dTitle, g_dArtist, g_dAlbum, g_dCoverUrl, g_dPlayer;
static String  g_dTitleD, g_dArtistD, g_dAlbumD; // displaybereit (ASCII-transliteriert)
static bool    g_dPlaying  = false;
static int     g_dVolume   = -1, g_dRepeat  = -1, g_dShuffle = -1;
static int     g_dPosition = -1, g_dDuration = 0;
static int     g_lmsPos   = -1;   // zuletzt von LMS empfangene Position
static int     g_localPos  = 0;   // Interpolationsbasis
static uint32_t g_localPosMs = 0; // millis() beim letzten LMS-Sync
static int     g_lastProgFilled = -1; // für Fortschrittsbalken selektives Neuzeichnen
static int     g_lastVolFilled  = -1; // für Lautstärkebalken selektives Neuzeichnen
static int     g_lastClockMin   = -2; // für Uhrzeit im Header (-2 = noch nie gezeichnet)

// Settings screen state
static std::vector<SqueeziePlayer> g_settPlayers;
static String   g_settDiscHost;
static uint16_t g_settDiscPort  = 0;
static bool     g_settDiscFound = false;
static bool     g_settSearched  = false;

// Scroll-State für Titel/Artist/Album (Index 0/1/2)
static int32_t g_scrollOff[3]   = {0, 0, 0}; // aktueller Pixel-Offset
static uint32_t g_scrollLast    = 0;          // letzter Scroll-Zeitstempel

// Cover-Puffer (PSRAM) und Download-State
static uint8_t*          g_coverBuf      = nullptr;
static volatile bool     g_coverReady    = false;
static volatile size_t   g_coverLen      = 0;
static volatile uint32_t g_coverGen      = 0;
static volatile uint32_t g_dlGen         = 0;
static String            g_pendingCoverUrl;
static String            g_dispatchedCoverUrl;
static TaskHandle_t      g_coverTask     = nullptr;

// Prefetch-Slots: Cover der nächsten Tracks (PSRAM, sequenziell geladen)
struct PrefetchSlot {
    uint8_t* buf   = nullptr;
    size_t   len   = 0;
    bool     ready = false;
    String   url;
};
static PrefetchSlot      g_pf[SQUEEZE_PREFETCH];
static String            g_pfQueue[SQUEEZE_PREFETCH];
static int               g_pfQueueCount  = 0;
static SemaphoreHandle_t g_pfMux         = nullptr;
static TaskHandle_t      g_pfTask        = nullptr;

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

// UTF-8 → druckbares ASCII: ersetzt gängige Nicht-ASCII-Zeichen durch Alternativen.
// LovyanGFX-Standardfont zeigt nur ASCII 0x20–0x7E — alles andere wird als Quadrat dargestellt.
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

    g_coverBuf = (uint8_t*)ps_malloc(131072);
    if (!g_coverBuf) {
        srvLog("[cover] ps_malloc FAIL");
    } else {
        // Cover-Cache aus LittleFS laden — läuft vor WiFi, sofort beim Player-Screen verfügbar
        if (LittleFS.begin(true) && LittleFS.exists("/cover.jpg")) {
            File f = LittleFS.open("/cover.jpg", "r");
            if (f) {
                size_t n = f.read(g_coverBuf, 131070);
                f.close();
                bool valid = (n >= 2 && g_coverBuf[0] == 0xFF && g_coverBuf[1] == 0xD8);
                if (valid) { g_coverLen = n; g_coverReady = true; }
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
                           uint32_t bg, uint32_t fg, const char* label) {
    tft.fillCircle(cx, cy, r, bg);
    tft.drawCircle(cx, cy, r, COL_MID);
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(fg, bg);
    tft.setTextSize(2);
    tft.drawString(label, cx, cy);
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
    if (!txt || !txt[0]) {
        tft.fillRect(x, y, w, lh, COL_BG);
        tft.clearClipRect();
        return;
    }
    tft.drawString(txt, x + 2, y + 2);
    tft.clearClipRect();
    // Bereich rechts vom Text löschen (falls neuer Text kürzer als alter)
    int tw = tft.textWidth(txt) + 4;
    if (tw < w) tft.fillRect(x + tw, y, w - tw, lh, COL_BG);
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
    if (!txt || !txt[0]) {
        tft.fillRect(x, y, w, lh, COL_BG);
        return;
    }
    int tw = tft.textWidth(txt);
    if (tw + 4 <= w || offset == 0) {
        // Text passt oder Offset = 0: normal zeichnen (kein Clip nötig für kurze Texte)
        tft.setClipRect(x, y, w, lh);
        tft.drawString(txt, x + 2, y + 2);
        tft.clearClipRect();
        if (tw + 4 < w) tft.fillRect(x + tw + 4, y, w - tw - 4, lh, COL_BG);
    } else {
        // Scrolling: Text mit negativem Offset zeichnen, Clip auf Feldbreite
        tft.setClipRect(x, y, w, lh);
        tft.drawString(txt, x + 2 - offset, y + 2);
        tft.clearClipRect();
        // Kanten-Fill nach drawString: verhindert Ghost-Text
        int textLeft = x + 2 - offset;
        if (textLeft > x)
            tft.fillRect(x, y, textLeft - x, lh, COL_BG);
        int textRight = textLeft + tw;
        if (textRight < x + w)
            tft.fillRect(textRight, y, (x + w) - textRight, lh, COL_BG);
    }
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
    drawCircleBtn( 90, cy, 28, COL_DARK, COL_WHITE, "|<");
    uint32_t playBg = playing ? (uint32_t)COL_DARK  : (uint32_t)COL_GREEN;
    uint32_t playFg = playing ? (uint32_t)COL_GREEN : (uint32_t)COL_BG;
    drawCircleBtn(240, cy, 33, playBg, playFg, playing ? "||" : ">");
    drawCircleBtn(390, cy, 28, COL_DARK, COL_WHITE, ">|");
}

// Einmalig beim Screen-Aufbau: nur Buttons zeichnen
static void drawVolumeButtons() {
    int y = PL_VOL_Y, h = PL_VOL_H;
    tft.setFont(nullptr); tft.setTextSize(2); tft.setTextDatum(MC_DATUM);
    tft.fillRoundRect(4, y + 2, 40, h - 4, 4, COL_DARK);
    tft.drawRoundRect(4, y + 2, 40, h - 4, 4, COL_MID);
    tft.setTextColor(COL_WHITE, COL_DARK);
    tft.drawString("-", 24, y + h / 2);
    tft.fillRoundRect(DISPLAY_WIDTH - 44, y + 2, 40, h - 4, 4, COL_DARK);
    tft.drawRoundRect(DISPLAY_WIDTH - 44, y + 2, 40, h - 4, 4, COL_MID);
    tft.drawString("+", DISPLAY_WIDTH - 24, y + h / 2);
}

static void drawVolumeRow(int volume) {
    int y = PL_VOL_Y, h = PL_VOL_H;
    int real_pct = (volume <= 6) ? 0 : constrain((volume - 6) * 100 / 94, 0, 100);
    int bx = 48, bw = DISPLAY_WIDTH - 96;
    int filled = constrain(real_pct * bw / 100, 0, bw);

    // Selektiv: nur geänderte Pixel neu zeichnen → kein Flackern
    if (g_lastVolFilled < 0) {
        // Erstzeichnung: gesamten Balken zeichnen
        if (filled > 0)  tft.fillRect(bx,          y + 5, filled,      h - 10, COL_GREEN);
        if (filled < bw) tft.fillRect(bx + filled,  y + 5, bw - filled, h - 10, COL_PROG_BG);
    } else if (filled > g_lastVolFilled) {
        tft.fillRect(bx + g_lastVolFilled, y + 5, filled - g_lastVolFilled, h - 10, COL_GREEN);
    } else if (filled < g_lastVolFilled) {
        tft.fillRect(bx + filled, y + 5, g_lastVolFilled - filled, h - 10, COL_PROG_BG);
    }
    g_lastVolFilled = filled;

    // Prozentzahl: Bar unter dem Text auffrischen, dann transparenter weißer Text
    char buf[6]; snprintf(buf, sizeof(buf), "%d%%", real_pct);
    tft.setFont(nullptr); tft.setTextSize(2);
    int tw  = tft.textWidth(buf);
    int tcx = bx + bw / 2;                // Textmitte (MC_DATUM)
    int rx0 = max(bx, tcx - tw / 2 - 2);
    int rx1 = min(bx + bw, tcx + tw / 2 + tw % 2 + 2);
    int gx1 = bx + filled;                // Grün endet hier
    if (gx1 > rx0)
        tft.fillRect(rx0, y + 5, min(gx1, rx1) - rx0, h - 10, COL_GREEN);
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
    uint32_t rBg = rAct ? (uint32_t)COL_ACTIVE : (uint32_t)COL_HDR;
    uint32_t sBg = sAct ? (uint32_t)COL_ACTIVE : (uint32_t)COL_HDR;
    tft.fillRoundRect(2,   y + 1, 236, h - 2, 3, rBg);
    tft.setFont(nullptr); tft.setTextSize(2); tft.setTextDatum(MC_DATUM);
    tft.setTextColor(COL_WHITE, rBg);
    tft.drawString(rL, 120, y + h / 2);
    tft.fillRoundRect(242, y + 1, 236, h - 2, 3, sBg);
    tft.setTextColor(COL_WHITE, sBg);
    tft.drawString(sL, 360, y + h / 2);
}

// ── Async Cover-Download (FreeRTOS-Task auf Core 0) ─────────────────────────

static void coverDlTask(void*) {
    uint32_t myGen = g_dlGen;
    size_t total = 0;
    if (g_coverBuf) {
        HTTPClient http;
        srvLog("[cover] URL: %s", g_pendingCoverUrl.c_str());
        http.setTimeout(3000);
        if (http.begin(g_pendingCoverUrl)) {
            int httpCode = http.GET();
            srvLog("[cover] HTTP %d", httpCode);
            if (httpCode == 200) {
                WiFiClient* s = http.getStreamPtr();
                uint32_t t0 = millis();
                while ((http.connected() || s->available()) &&
                       total < 131070 && millis() - t0 < 4000) {
                    size_t av = s->available();
                    if (av) total += s->readBytes(g_coverBuf + total,
                                                   min(av, (size_t)(131070 - total)));
                    else vTaskDelay(1);
                }
                bool jpegOk = (total >= 2 && g_coverBuf[0] == 0xFF && g_coverBuf[1] == 0xD8);
                srvLog("[cover] %d bytes, JPEG=%s", (int)total, jpegOk ? "ok" : "NEIN");
            }
            http.end();
        } else {
            srvLog("[cover] http.begin FAIL");
        }
    }
    // Nur veröffentlichen wenn diese Generation noch aktuell ist
    if (myGen == g_coverGen) {
        g_coverLen   = total;
        g_coverReady = true;
        // Für nächsten Boot cachen (beide Cores lesen nur g_coverBuf → kein Konflikt)
        File f = LittleFS.open("/cover.jpg", "w");
        if (f) { f.write(g_coverBuf, total); f.close(); }
    }
    g_coverTask = nullptr;
    vTaskDelete(nullptr);
}

const uint8_t* displayGetCoverBuf() { return g_coverBuf; }
size_t         displayGetCoverLen() { return (size_t)g_coverLen; }

static String g_coverDrawnForTitle; // Titel des zuletzt auf dem Display gezeichneten Covers
static bool   g_coverPredrawn = false; // Cover in displayDrawPlayerScreen() vorgezeichnet

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
        for (int i = 1; i < g_pfQueueCount; i++) g_pfQueue[i - 1] = g_pfQueue[i];
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
                WiFiClient* s = http.getStreamPtr();
                uint32_t t0 = millis();
                while ((http.connected() || s->available()) &&
                       total < 131070 && millis() - t0 < 4000 &&
                       g_coverTask == nullptr) { // Abbruch wenn Haupt-Download startet
                    size_t av = s->available();
                    if (av) total += s->readBytes(g_pf[slot].buf + total,
                                                   min(av, (size_t)(131070 - total)));
                    else vTaskDelay(1);
                }
            }
            http.end();
        }

        bool jpegOk = (total >= 2 && g_pf[slot].buf[0] == 0xFF && g_pf[slot].buf[1] == 0xD8);
        xSemaphoreTake(g_pfMux, portMAX_DELAY);
        if (jpegOk) {
            g_pf[slot].len   = total;
            g_pf[slot].ready = true;
            g_pf[slot].url   = url;
            srvLog("[pf] Slot %d: %d B", slot, (int)total);
        }
        xSemaphoreGive(g_pfMux);
    }
    g_pfTask = nullptr;
    vTaskDelete(nullptr);
}

// Aus beliebigem Task aufrufbar (z.B. lmsPoll) — befüllt Prefetch-Queue
void displayQueuePrefetch(const String* urls, int count) {
    if (!g_pfMux || count <= 0) return;
    xSemaphoreTake(g_pfMux, portMAX_DELAY);
    g_pfQueueCount = 0;
    for (int i = 0; i < count && i < SQUEEZE_PREFETCH; i++) {
        if (urls[i].isEmpty()) continue;
        // Schon vorhanden oder bereits fertig im Slot → überspringen
        bool skip = false;
        for (int j = 0; j < SQUEEZE_PREFETCH; j++) {
            if (g_pf[j].ready && g_pf[j].url == urls[i]) { skip = true; break; }
        }
        for (int j = 0; j < g_pfQueueCount && !skip; j++) {
            if (g_pfQueue[j] == urls[i]) skip = true;
        }
        if (!skip) g_pfQueue[g_pfQueueCount++] = urls[i];
    }
    bool needTask = (g_pfQueueCount > 0 && g_pfTask == nullptr);
    xSemaphoreGive(g_pfMux);
    if (needTask) {
        xTaskCreatePinnedToCore(prefetchTask, "pfetch", 8192,
                                nullptr, 1, (TaskHandle_t*)&g_pfTask, 0);
        srvLog("[pf] Queue: %d URLs", g_pfQueueCount);
    }
}

// Aus beliebigem Task aufrufbar (z.B. lmsPoll auf Core 0) — startet Cover-Download sofort
void displayPreloadCover(const String& url) {
    if (url.isEmpty() || !g_coverBuf) return;
    if (g_coverTask != nullptr) return;           // Download läuft bereits
    if (g_dispatchedCoverUrl == url && (g_coverReady || g_coverLen > 0)) return; // schon geladen

    // Prefetch-Cache prüfen — Cover bereits vorhanden?
    if (g_pfMux) {
        xSemaphoreTake(g_pfMux, portMAX_DELAY);
        for (int i = 0; i < SQUEEZE_PREFETCH; i++) {
            if (g_pf[i].ready && g_pf[i].url == url && g_pf[i].len > 0) {
                memcpy(g_coverBuf, g_pf[i].buf, g_pf[i].len);
                g_coverLen           = g_pf[i].len;
                g_dCoverUrl          = url;
                g_dispatchedCoverUrl = url;
                g_coverReady         = true;
                g_pf[i].ready        = false; // Slot freigeben
                xSemaphoreGive(g_pfMux);
                srvLog("[cover] Prefetch-Hit Slot %d: %d B", i, (int)g_coverLen);
                return;
            }
        }
        xSemaphoreGive(g_pfMux);
    }

    // Kein Cache-Hit → normaler Download
    g_dCoverUrl          = url;
    g_pendingCoverUrl    = url;
    g_dlGen              = ++g_coverGen;
    g_dispatchedCoverUrl = url;
    g_coverReady         = false;
    xTaskCreatePinnedToCore(coverDlTask, "cover", 8192,
                            nullptr, 1, (TaskHandle_t*)&g_coverTask, 0);
    srvLog("[cover] Preload gestartet (früh)");
}

// ─── Einstellungs-Screen ─────────────────────────────────────────────────────

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
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(COL_WHITE, COL_DARK);
    tft.drawString("ZUR.", DISPLAY_WIDTH - 33, PL_HDR_H / 2);
    // Abschnitt: Server
    tft.setFont(nullptr); tft.setTextSize(1);
    tft.setTextDatum(ML_DATUM);
    tft.setTextColor(COL_MGREY, COL_BG);
    tft.drawString("SERVER", 6, 39);
    drawSettingsSrvRow();
    drawSettingsDiscRow();
    // Trennlinie
    tft.fillRect(0, 130, DISPLAY_WIDTH, 1, COL_MID);
    // Abschnitt: Player
    tft.setFont(nullptr); tft.setTextSize(1);
    tft.setTextDatum(ML_DATUM);
    tft.setTextColor(COL_MGREY, COL_BG);
    tft.drawString("PLAYER", 6, 141);
    // Player-Liste laden
    g_settPlayers.clear();
    if (!squeezeGetHost().isEmpty()) squeezeGetPlayers(g_settPlayers);
    drawSettingsPlayerList();
}

static void handleSettingsTouch(int32_t tx, int32_t ty) {
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
    bool hadCover = g_coverLen > 0; // Cache oder frisch vorgeladenes Cover

    g_dTitle = g_dArtist = g_dAlbum = g_dCoverUrl = g_dPlayer = "";
    g_dPlaying = false; g_dVolume = g_dRepeat = g_dShuffle = -1;
    g_dPosition = -1;   g_dDuration = 0;
    g_lastProgFilled = -1;
    g_lastVolFilled  = -1;
    g_scrollOff[0] = g_scrollOff[1] = g_scrollOff[2] = 0;
    g_scrollLast = 0;
    g_coverDrawnForTitle = "";
    g_coverPredrawn = false;
    if (!hadCover) {
        g_coverReady = false;
        g_coverLen   = 0;
    }

    tft.fillScreen(COL_BG);
    drawPlayerHeader("Squeezie", false);
    if (hadCover) {
        g_coverReady    = false;
        g_coverPredrawn = true;
        tft.drawJpg(g_coverBuf, (uint32_t)g_coverLen,
                    PL_COVER_X, PL_COVER_Y, PL_COVER_W, PL_COVER_H);
        srvLog("[cover] Preload sofort gezeichnet (%d bytes)", (int)g_coverLen);
    } else {
        drawCoverPlaceholder();
    }
    tft.fillRect(PL_INFO_X, PL_INFO_Y, PL_INFO_W, PL_COVER_H, COL_BG);
    drawProgressBar(0, 0);
    drawTransportButtons(false);
    drawVolumeButtons();
    drawVolumeRow(0);
    drawModeRow(0, 0);
}

void displayUpdatePlayer() {
    if (g_screen != SCREEN_PLAYER) return;
    tft.startWrite(); // SPI-Bus acquiren; laufende DMA-Transfers abwarten
    const SqueezieStatus& s = squeezieGetCachedStatus();

    // Fertiges Cover zeichnen (asynchron heruntergeladen)
    if (g_coverReady) {
        g_coverReady = false;
        if (g_coverLen > 0) {
            srvLog("[cover] drawJpg %d bytes", (int)g_coverLen);
            tft.drawJpg(g_coverBuf, (uint32_t)g_coverLen,
                        PL_COVER_X, PL_COVER_Y, PL_COVER_W, PL_COVER_H);
            g_coverDrawnForTitle = s.title;
        } else {
            srvLog("[cover] kein Bild, Placeholder");
            drawCoverPlaceholder();
        }
    }

    // Preloaded-Cover: beim ersten bekannten Titel als gezeichnet markieren → kein Re-Download
    if (g_coverPredrawn && !s.title.isEmpty()) {
        g_coverDrawnForTitle = s.title;
        g_coverPredrawn = false;
    }

    // Cover neu laden wenn URL oder Titel wechselt.
    bool coverUrlChanged = (s.coverUrl != g_dCoverUrl && !s.coverUrl.isEmpty());
    bool titleChanged    = (!s.title.isEmpty() && s.title != g_coverDrawnForTitle);
    if (coverUrlChanged || (titleChanged && g_coverTask == nullptr)) {
        if (coverUrlChanged) g_dCoverUrl = s.coverUrl;
        g_coverGen++;
        g_dispatchedCoverUrl = ""; // nur Dispatch-State zurücksetzen — g_coverReady/Len noch behalten
        if (s.coverUrl.isEmpty()) {
            g_coverReady = false;
            g_coverLen   = 0;
            drawCoverPlaceholder();
        }
    }
    // Download starten wenn URL gesetzt, Puffer da, kein Task läuft und noch nicht dispatcht
    if (!g_dCoverUrl.isEmpty() && g_coverBuf && g_coverTask == nullptr
            && g_dispatchedCoverUrl != g_dCoverUrl) {
        // Bereits bereit? (Prefetch-Hit via displayPreloadCover oder Preload)
        if (g_coverReady && g_coverLen > 0) {
            g_dispatchedCoverUrl = g_dCoverUrl; // kein Download nötig
        } else {
            // Prefetch-Cache direkt prüfen
            bool pfHit = false;
            if (g_pfMux) {
                xSemaphoreTake(g_pfMux, portMAX_DELAY);
                for (int i = 0; i < SQUEEZE_PREFETCH; i++) {
                    if (g_pf[i].ready && g_pf[i].url == g_dCoverUrl && g_pf[i].len > 0) {
                        memcpy(g_coverBuf, g_pf[i].buf, g_pf[i].len);
                        g_coverLen           = g_pf[i].len;
                        g_dispatchedCoverUrl = g_dCoverUrl;
                        g_coverReady         = true;
                        g_pf[i].ready        = false;
                        pfHit                = true;
                        srvLog("[cover] UpdatePlayer Prefetch-Hit %d B", (int)g_coverLen);
                        break;
                    }
                }
                xSemaphoreGive(g_pfMux);
            }
            if (!pfHit) {
                g_coverReady         = false;
                g_coverLen           = 0;
                g_pendingCoverUrl    = g_dCoverUrl;
                g_dlGen              = g_coverGen;
                g_dispatchedCoverUrl = g_dCoverUrl;
                xTaskCreatePinnedToCore(coverDlTask, "cover", 8192,
                                        nullptr, 1, (TaskHandle_t*)&g_coverTask, 0);
            }
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
        drawCircleBtn(240, cy, 33, bg, fg, s.playing ? "||" : ">");
        tft.fillCircle(DISPLAY_WIDTH - 85, PL_HDR_H / 2, 5,
                        s.playing ? (uint32_t)COL_GREEN : (uint32_t)COL_MID);
        g_dPlaying = s.playing;
    }

    // Textinfo: Layout (y relativ zu PL_INFO_Y=32): Titel size3(28px), Künstler size2(20px),
    // Album size2(20px), Track size2(20px). Passt in 128px Cover-Höhe.
    bool infoChanged = (s.title != g_dTitle || s.artist != g_dArtist || s.album != g_dAlbum);
    if (infoChanged) {
        // Scroll-Offsets bei Titelwechsel zurücksetzen
        g_scrollOff[0] = g_scrollOff[1] = g_scrollOff[2] = 0;
        g_scrollLast = millis();
        g_dTitle  = s.title;  g_dArtist  = s.artist;  g_dAlbum  = s.album;
        g_dTitleD = utf8ToAscii(s.title);
        g_dArtistD = utf8ToAscii(s.artist);
        g_dAlbumD  = utf8ToAscii(s.album);
    }

    // Textbreiten cachen — nur bei Titelwechsel neu messen
    static int s_tw0 = 0, s_tw1 = 0, s_tw2 = 0;
    if (infoChanged) {
        tft.setFont(nullptr);
        tft.setTextSize(3); s_tw0 = tft.textWidth(g_dTitleD.c_str());
        tft.setTextSize(2); s_tw1 = tft.textWidth(g_dArtistD.c_str());
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
    if (infoChanged || (doScroll && l0)) drawScrollLine(g_dTitleD.c_str(),  PL_INFO_X, PL_INFO_Y + 2,  PL_INFO_W, COL_WHITE, 3, g_scrollOff[0]);
    if (infoChanged || (doScroll && l1)) drawScrollLine(g_dArtistD.c_str(), PL_INFO_X, PL_INFO_Y + 34, PL_INFO_W, COL_GREEN, 2, g_scrollOff[1]);
    if (infoChanged || (doScroll && l2)) drawScrollLine(g_dAlbumD.c_str(),  PL_INFO_X, PL_INFO_Y + 58, PL_INFO_W, COL_LGREY, 2, g_scrollOff[2]);
    if (s.duration != g_dDuration) {
        char buf[32];
        snprintf(buf, sizeof(buf), "Track %d  |  %d:%02d",
                 s.trackNum, s.duration / 60, s.duration % 60);
        drawInfoLine(buf, PL_INFO_X, PL_INFO_Y + 82, PL_INFO_W, COL_MGREY, 2);
        g_dDuration = s.duration;
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
    if (s.volume != g_dVolume) {
        drawVolumeRow(s.volume);
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
            squeezePrev(mac);
        } else if ((long)(tx - 390) * (tx - 390) + (long)(ty - cy) * (ty - cy) <= 28L * 28L) {
            squeezeNext(mac);
        } else if ((long)(tx - 240) * (tx - 240) + (long)(ty - cy) * (ty - cy) <= 33L * 33L) {
            if (g_dPlaying) squeezePause(mac); else squeezePlay(mac);
        }
        // Fortschrittsbalken: Seek
        else if (ty >= PL_PROG_Y && ty < PL_TRANS_Y && g_dDuration > 0) {
            int seekPos = (int)((long)tx * g_dDuration / DISPLAY_WIDTH);
            squeezeSeek(mac, constrain(seekPos, 0, g_dDuration));
        }
        // Modus-Buttons
        else if (ty >= PL_MODE_Y) {
            if (tx < 240) {
                int next = (max(0, g_dRepeat) + 1) % 3;
                squeezeSetRepeat(mac, next);
                g_dRepeat = next;
                drawModeRow(g_dRepeat, g_dShuffle);
            } else {
                int next = (max(0, g_dShuffle) + 1) % 3;
                squeezeSetShuffle(mac, next);
                g_dShuffle = next;
                drawModeRow(g_dRepeat, g_dShuffle);
            }
        }
    }

    // ── Lautstärke ────────────────────────────────────────────────────────────
    if (pressed && ty >= PL_VOL_Y && ty < PL_MODE_Y) {
        const int bx = 48, bw = DISPLAY_WIDTH - 96;
        bool inVolMinus = (tx >= 4   && tx <= 44);
        bool inVolPlus  = (tx >= DISPLAY_WIDTH - 44 && tx <= DISPLAY_WIDTH - 4);
        bool inBar      = (tx >= bx  && tx <= bx + bw);
        if (inBar) {
            // Direkt nach Touch-Position setzen (stufenlos)
            int pct = constrain((tx - bx) * 100 / bw, 0, 100);
            int vol = (pct == 0) ? 0 : (pct * 94 / 100 + 6);
            if (!volHeld || millis() - volHoldT0 > 80) {
                squeezeSetVolume(wifiGetPlayerMac(), vol);
                volHeld   = true;
                volHoldT0 = millis();
            }
        } else if (inVolMinus || inVolPlus) {
            if (!volHeld || millis() - volHoldT0 > 250) {
                squeezeAdjustVolume(wifiGetPlayerMac(), inVolMinus ? -1 : 1);
                volHeld   = true;
                volHoldT0 = millis();
            }
        } else { volHeld = false; }
    } else { volHeld = false; }

    lastPressed = pressed;
}
