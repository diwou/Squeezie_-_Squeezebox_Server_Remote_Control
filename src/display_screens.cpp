#include "display_intern.h"

// ─── Startup-Screen ────────────────────────────────────────────────────────

void displayShowStartup(const char* version, const char* ip) {
    g_screen = SCREEN_STARTUP;
    tft.fillScreen(COL_BG);

    // Rahmen
    tft.drawRoundRect(10, 10, DISPLAY_WIDTH - 20, DISPLAY_HEIGHT - 20, 12, COL_MID);
    tft.drawRoundRect(12, 12, DISPLAY_WIDTH - 24, DISPLAY_HEIGHT - 24, 10, COL_DGREY);

    // Titel
    tft.setTextSize(4);
    tft.setTextColor(COL_WHITE, COL_BG);
    int16_t tw = tft.textWidth("Squeezie");
    tft.setCursor((DISPLAY_WIDTH - tw) / 2, 80);
    tft.print("Squeezie");

    // Version
    tft.setTextSize(2);
    tft.setTextColor(COL_LGREY, COL_BG);
    tw = tft.textWidth(version);
    tft.setCursor((DISPLAY_WIDTH - tw) / 2, 130);
    tft.print(version);

    // Statuszeile
    tft.setTextSize(2);
    tft.setTextColor(COL_YELLOW, COL_BG);
    if (ip && strlen(ip) > 0) {
        tw = tft.textWidth(ip);
        tft.setCursor((DISPLAY_WIDTH - tw) / 2, 200);
        tft.print(ip);
    } else {
        const char* msg = "Verbinde...";
        tw = tft.textWidth(msg);
        tft.setCursor((DISPLAY_WIDTH - tw) / 2, 200);
        tft.print(msg);
    }
}

// ─── Player-Screen ─────────────────────────────────────────────────────────

void displayDrawPlayerScreen() {
    g_screen = SCREEN_PLAYER;
    tft.fillScreen(COL_BG);

    // Header
    drawPlayerHeader(g_dPlayer.c_str(), g_dPlaying);

    // Cover-Bereich
    drawCoverPlaceholder();

    // Progress-Bar
    drawProgressBar(g_dPosition, g_dDuration);

    // Transport-Buttons
    drawTransportButtons(g_dPlaying);

    // Volume
    drawVolumeRow(g_dVolume, g_dMuted);

    // Mode
    drawModeRow(g_dRepeat, g_dShuffle);

    // Initialwerte für selektives Redraw
    g_lastProgFilled = -1;
    g_lastVolFilled  = -1;
    g_lastVolMuted   = false;
    g_lastClockMin   = -2;
    g_dTitleD        = "";
    g_dArtistD       = "";
    g_dAlbumD        = "";
}

void displayUpdatePlayer() {
    if (g_screen != SCREEN_PLAYER) return;

    SqueezieStatus st = squeezieGetCachedStatus();

    // ── Player-Name / Header ─────────────────────────────────────────────
    if (st.playerName != g_dPlayer) {
        g_dPlayer = st.playerName;
        drawPlayerHeader(g_dPlayer.c_str(), st.playing);
    }

    // ── Uhrzeit im Header minütlich aktualisieren ────────────────────────
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

    // ── Titel / Artist / Album (mit Scroll-Logik) ────────────────────────
    bool metaChanged = (st.title != g_dTitle || st.artist != g_dArtist || st.album != g_dAlbum);
    if (metaChanged) {
        // Info-Bereich löschen (116px Höhe für Titel/Artist/Album + Track-Zeile)
        tft.fillRect(PL_INFO_X, PL_INFO_Y, PL_INFO_W, 116, COL_BG);
        // Cover-Platzhalter (falls kein Cover geladen)
        drawCoverPlaceholder();
        // Scroll-Offsets bei Titelwechsel zurücksetzen
        g_scrollOff[0] = g_scrollOff[1] = g_scrollOff[2] = 0;
        g_scrollLast = millis();
        g_dTitle  = st.title;
        g_dArtist = st.artist;
        g_dAlbum  = st.album;
        g_dTitleD  = utf8ToAscii(st.title);
        g_dArtistD = utf8ToAscii(st.artist);
        g_dAlbumD  = utf8ToAscii(st.album);
    }

    // Textbreiten cachen — nur bei Titelwechsel neu messen
    static int s_tw0 = 0, s_tw1 = 0, s_tw2 = 0;
    if (metaChanged) {
        setPlayerInfoFont(3); s_tw0 = tft.textWidth(g_dTitleD.c_str());
        setPlayerInfoFont(2); s_tw1 = tft.textWidth(g_dArtistD.c_str());
                             s_tw2 = tft.textWidth(g_dAlbumD.c_str());
    }

    // Scroll-Tick: Fraktionaler Akkumulator
    uint32_t now = millis();
    int scrollMs = wifiGetScrollSpeed();
    if (scrollMs < 1) scrollMs = 1;
    static uint32_t s_scrollAccum = 0;
    s_scrollAccum += (now - g_scrollLast);
    g_scrollLast = now;
    int tickPx = (int)(s_scrollAccum / (uint32_t)scrollMs);
    if (tickPx > 0) s_scrollAccum -= (uint32_t)tickPx * (uint32_t)scrollMs;
    if (metaChanged) s_scrollAccum = 0;
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
    if (metaChanged || (doScroll && l0)) drawPlayerScrollLine(g_dTitleD.c_str(),  PL_INFO_X, PL_INFO_Y + 2,  PL_INFO_W, COL_WHITE, 3, g_scrollOff[0], true);
    if (metaChanged || (doScroll && l1)) drawPlayerScrollLine(g_dArtistD.c_str(), PL_INFO_X, PL_INFO_Y + 34, PL_INFO_W, COL_GREEN, 2, g_scrollOff[1]);
    if (metaChanged || (doScroll && l2)) drawPlayerScrollLine(g_dAlbumD.c_str(),  PL_INFO_X, PL_INFO_Y + 58, PL_INFO_W, COL_LGREY, 2, g_scrollOff[2]);

    // Track-Info-Zeile (Track-Nummer + Dauer)
    if (metaChanged || st.duration != g_dDuration || st.trackNum != g_dTrackNum) {
        char buf[32];
        snprintf(buf, sizeof(buf), "Track %d  |  %d:%02d",
                 st.trackNum, st.duration / 60, st.duration % 60);
        drawInfoLine(buf, PL_INFO_X, PL_INFO_Y + 92, PL_INFO_W, COL_MGREY, 2);
        g_dDuration = st.duration;
        g_dTrackNum = st.trackNum;
    }

    // ── Cover ────────────────────────────────────────────────────────────
    // Nutze die vom LMS gelieferte Cover-URL und den Cover-Key aus dem Status
    String coverUrl  = st.coverUrl;
    String coverKey  = st.coverKey;
    if (coverUrl != g_dCoverUrl || coverKey != g_dCoverKey) {
        g_dCoverUrl = coverUrl;
        g_dCoverKey = coverKey;
        // Neues Cover anfordern
        displayPreloadCover(coverUrl, coverKey);
    }

    // Gecoverte Cover-Downloads abholen
    xSemaphoreTake(g_coverMux, portMAX_DELAY);
    bool newCover = g_coverReady && (g_dlGen != g_coverGen);
    if (newCover) {
        g_coverGen    = g_dlGen;
        g_coverReady  = false;
        g_coverSerial++;
        // Puffer tauschen
        uint8_t* tmp  = g_coverBuf;
        g_coverBuf    = g_coverDlBuf;
        g_coverDlBuf  = tmp;
        g_coverLen    = g_coverLen; // bleibt gleich
        g_loadedCoverKey = g_dispatchedCoverKey;
    }
    xSemaphoreGive(g_coverMux);

    if (newCover) {
        drawCoverImage(g_coverBuf, g_coverLen);
    }

    // ── Playing-State ────────────────────────────────────────────────────
    if (st.playing != g_dPlaying) {
        g_dPlaying = st.playing;
        drawTransportButtons(g_dPlaying);
    }

    // ── Progress ─────────────────────────────────────────────────────────
    if (st.position != g_lmsPos) {
        g_lmsPos     = st.position;
        g_localPos   = st.position;
        g_localPosMs = millis();
    }
    // Interpolierte Position für die Anzeige
    int elapsed = (int)((millis() - g_localPosMs) / 1000);
    int displayPos = g_localPos + elapsed;
    if (displayPos < 0) displayPos = 0;
    if (displayPos != g_dPosition || st.duration != g_dDuration) {
        g_dPosition = displayPos;
        g_dDuration = st.duration;
        drawProgressBar(g_dPosition, g_dDuration);
    }

    // ── Volume ───────────────────────────────────────────────────────────
    if (st.volume != g_dVolume || st.muted != g_dMuted) {
        g_dVolume = st.volume;
        g_dMuted  = st.muted;
        drawVolumeRow(g_dVolume, g_dMuted);
    }

    // ── Repeat / Shuffle ─────────────────────────────────────────────────
    if (st.repeatMode  != g_dRepeat)  { g_dRepeat  = st.repeatMode;  drawModeRow(g_dRepeat, g_dShuffle); }
    if (st.shuffleMode != g_dShuffle) { g_dShuffle = st.shuffleMode; drawModeRow(g_dRepeat, g_dShuffle); }
}

// ─── OTA-Screen ───────────────────────────────────────────────────────────

void displayShowOTA(int pct) {
    g_screen = SCREEN_STARTUP; // OTA überschreibt den Screen
    tft.fillScreen(COL_BG);

    tft.setTextSize(3);
    tft.setTextColor(COL_WHITE, COL_BG);
    int16_t tw = tft.textWidth("OTA-Update");
    tft.setCursor((DISPLAY_WIDTH - tw) / 2, 80);
    tft.print("OTA-Update");

    // Fortschrittsbalken
    int barW = DISPLAY_WIDTH - 40;
    int barH = 24;
    int barX = 20;
    int barY = 140;

    tft.drawRoundRect(barX, barY, barW, barH, 4, COL_WHITE);
    if (pct > 0) {
        int fillW = ((barW - 4) * pct) / 100;
        tft.fillRoundRect(barX + 2, barY + 2, fillW, barH - 4, 3, COL_GREEN);
    }

    // Prozentzahl
    tft.setTextSize(2);
    tft.setTextColor(COL_LGREY, COL_BG);
    char pctStr[8];
    snprintf(pctStr, sizeof(pctStr), "%d%%", pct);
    tw = tft.textWidth(pctStr);
    tft.setCursor((DISPLAY_WIDTH - tw) / 2, barY + barH + 10);
    tft.print(pctStr);

    if (pct >= 100) {
        tft.setTextSize(2);
        tft.setTextColor(COL_YELLOW, COL_BG);
        const char* msg = "Neustart...";
        tw = tft.textWidth(msg);
        tft.setCursor((DISPLAY_WIDTH - tw) / 2, barY + barH + 40);
        tft.print(msg);
    }
}

// ─── Factory-Reset-Screen ─────────────────────────────────────────────────

void displayShowFactoryReset(uint8_t secsLeft) {
    g_screen = SCREEN_STARTUP;
    tft.fillScreen(COL_BG);

    tft.setTextSize(3);
    tft.setTextColor(TFT_RED, COL_BG);
    const char* msg = "Factory Reset";
    int16_t tw = tft.textWidth(msg);
    tft.setCursor((DISPLAY_WIDTH - tw) / 2, 80);
    tft.print(msg);

    tft.setTextSize(2);
    tft.setTextColor(COL_WHITE, COL_BG);
    msg = "Alle Daten werden geloescht!";
    tw = tft.textWidth(msg);
    tft.setCursor((DISPLAY_WIDTH - tw) / 2, 140);
    tft.print(msg);

    char buf[32];
    snprintf(buf, sizeof(buf), "Abbruch in %d s", secsLeft);
    tft.setTextColor(COL_YELLOW, COL_BG);
    tw = tft.textWidth(buf);
    tft.setCursor((DISPLAY_WIDTH - tw) / 2, 180);
    tft.print(buf);

    if (secsLeft == 0) {
        tft.setTextSize(2);
        tft.setTextColor(TFT_RED, COL_BG);
        msg = "RESET WIRD AUSGEFUEHRT";
        tw = tft.textWidth(msg);
        tft.setCursor((DISPLAY_WIDTH - tw) / 2, 220);
        tft.print(msg);
    }
}

// ─── Playlist-Screen ──────────────────────────────────────────────────────

void drawPlaylistList() {
    int y = 40;
    tft.fillRect(0, y, DISPLAY_WIDTH, DISPLAY_HEIGHT - y, COL_BG);
    tft.setTextSize(2);
    for (size_t i = 0; i < g_settPlaylists.size(); i++) {
        uint32_t bg = (i % 2 == 0) ? COL_DARK : COL_BG;
        tft.fillRect(0, y, DISPLAY_WIDTH, 30, bg);
        tft.setTextColor(COL_WHITE, bg);
        tft.setCursor(10, y + 6);
        tft.print(g_settPlaylists[i].name);
        y += 30;
    }
}

void drawPlaylistScreen() {
    g_screen = SCREEN_PLAYLIST;
    tft.fillScreen(COL_BG);

    // Header
    tft.fillRect(0, 0, DISPLAY_WIDTH, 30, COL_HDR);
    tft.setTextSize(2);
    tft.setTextColor(COL_WHITE, COL_HDR);
    tft.setCursor(10, 6);
    tft.print("Playlist auswaehlen");

    // Zurück-Button
    drawButton(DISPLAY_WIDTH - 80, 2, 76, 26, COL_DARK, COL_WHITE, "Zurueck");

    drawPlaylistList();
}

void handlePlaylistTouch(int32_t tx, int32_t ty) {
    // Zurück-Button
    if (tx >= DISPLAY_WIDTH - 80 && tx <= DISPLAY_WIDTH - 4 && ty >= 2 && ty <= 28) {
        displayDrawPlayerScreen();
        return;
    }

    // Playlist antippen
    if (ty >= 40) {
        int idx = (ty - 40) / 30;
        if (idx >= 0 && (size_t)idx < g_settPlaylists.size()) {
            wifiSetPlaylist(g_settPlaylists[idx].id, g_settPlaylists[idx].name, true);
            displayDrawPlayerScreen();
        }
    }
}

// ─── Settings-Screen ──────────────────────────────────────────────────────

void drawSettingsSrvRow() {
    int y = SET_SRV_Y;
    tft.fillRect(SET_BOX_X, y, SET_BOX_W, 30, COL_DARK);
    tft.drawRoundRect(SET_BOX_X, y, SET_BOX_W, 30, 4, COL_MID);
    tft.setTextSize(2);
    tft.setTextColor(COL_WHITE, COL_DARK);
    tft.setCursor(SET_BOX_X + 8, y + 6);
    tft.print("Server: ");
    String host = squeezeGetHost();
    if (host.length() > 0) {
        tft.print(host);
    } else {
        tft.setTextColor(COL_MGREY, COL_DARK);
        tft.print("(keiner)");
    }
}

void drawSettingsDiscRow() {
    int y = SET_DISC_Y;
    tft.fillRect(SET_BOX_X, y, SET_BOX_W, 30, COL_DARK);
    tft.drawRoundRect(SET_BOX_X, y, SET_BOX_W, 30, 4, COL_MID);
    tft.setTextSize(2);
    tft.setTextColor(COL_WHITE, COL_DARK);
    tft.setCursor(SET_BOX_X + 8, y + 6);
    if (g_settSearched) {
        if (g_settDiscFound) {
            tft.print("Gefunden: ");
            tft.print(g_settDiscHost);
        } else {
            tft.print("Kein Server gefunden");
        }
    } else {
        tft.print("Server suchen...");
    }
}

void drawSettingsSyncRow() {
    int y = SET_SYNC_Y;
    tft.fillRect(SET_BOX_X, y, SET_BOX_W, 30, COL_DARK);
    tft.drawRoundRect(SET_BOX_X, y, SET_BOX_W, 30, 4, COL_MID);
    tft.setTextSize(2);
    tft.setTextColor(COL_WHITE, COL_DARK);
    tft.setCursor(SET_BOX_X + 8, y + 6);
    bool sync = wifiGetSyncVolume();
    tft.print("Sync Volume: ");
    tft.print(sync ? "AN" : "AUS");
}

void drawSettingsPlayerList() {
    int y0 = SET_PLR_Y0;
    tft.fillRect(0, y0, DISPLAY_WIDTH, DISPLAY_HEIGHT - y0, COL_BG);
    tft.setTextSize(2);
    for (size_t i = 0; i < g_settPlayers.size(); i++) {
        int y = y0 + i * (SET_PLR_H + SET_PLR_GAP);
        if (y + SET_PLR_H > DISPLAY_HEIGHT) break;

        uint32_t bg = (g_settPlayers[i].mac == wifiGetPlayerMac()) ? COL_ACTIVE : COL_DARK;
        tft.fillRect(SET_BOX_X, y, SET_BOX_W, SET_PLR_H, bg);
        tft.drawRoundRect(SET_BOX_X, y, SET_BOX_W, SET_PLR_H, 4, COL_MID);

        tft.setTextColor(COL_WHITE, bg);
        tft.setCursor(SET_BOX_X + 8, y + 4);
        tft.print(g_settPlayers[i].name);

        tft.setTextSize(1);
        tft.setTextColor(COL_LGREY, bg);
        tft.setCursor(SET_BOX_X + 8, y + 22);
        tft.print(g_settPlayers[i].mac);
        tft.setTextSize(2);

        // Sync-Checkbox
        if (g_settPlayers[i].mac != wifiGetPlayerMac()) {
            bool inSync = false;
            // Vereinfacht: Prüfe ob MAC in Sync-Gruppe
            String mac = g_settPlayers[i].mac;
            // (Die echte Sync-Prüfung erfolgt über squeezeGetSyncGroupMembers)
            tft.fillRect(SET_LISTE_X, y + 4, SET_LISTE_W, SET_PLR_H - 8, COL_BG);
            tft.drawRoundRect(SET_LISTE_X, y + 4, SET_LISTE_W, SET_PLR_H - 8, 4, COL_MID);
            tft.setTextColor(COL_LGREY, COL_BG);
            tft.setCursor(SET_LISTE_X + 6, y + 8);
            tft.print("Sync");
        }
    }
}

void drawSettingsScreen() {
    g_screen = SCREEN_SETTINGS;
    tft.fillScreen(COL_BG);

    // Header
    tft.fillRect(0, 0, DISPLAY_WIDTH, 30, COL_HDR);
    tft.setTextSize(2);
    tft.setTextColor(COL_WHITE, COL_HDR);
    tft.setCursor(10, 6);
    tft.print("Einstellungen");

    // Schliessen-Button
    drawButton(DISPLAY_WIDTH - 80, 2, 76, 26, COL_DARK, COL_WHITE, "Schliessen");

    // Server-Reihe
    drawSettingsSrvRow();

    // Discovery-Reihe
    drawSettingsDiscRow();

    // Sync-Volume-Reihe
    drawSettingsSyncRow();

    // Player-Liste
    drawSettingsPlayerList();
}

void handleSettingsTouch(int32_t tx, int32_t ty) {
    // Schliessen-Button
    if (tx >= DISPLAY_WIDTH - 80 && tx <= DISPLAY_WIDTH - 4 && ty >= 2 && ty <= 28) {
        displayDrawPlayerScreen();
        return;
    }

    // Server-Reihe antippen → Discovery starten
    if (tx >= SET_BOX_X && tx <= SET_BOX_X + SET_BOX_W && ty >= SET_SRV_Y && ty <= SET_SRV_Y + 30) {
        if (!g_settSearched) {
            g_settSearched = true;
            String host;
            uint16_t port;
            g_settDiscFound = squeezeDiscover(host, port, 5000);
            if (g_settDiscFound) {
                g_settDiscHost = host;
                g_settDiscPort = port;
                wifiSetServer(host, port);
            }
            drawSettingsScreen();
        }
        return;
    }

    // Sync-Volume toggle
    if (tx >= SET_BOX_X && tx <= SET_BOX_X + SET_BOX_W && ty >= SET_SYNC_Y && ty <= SET_SYNC_Y + 30) {
        wifiSetSyncVolume(!wifiGetSyncVolume());
        drawSettingsScreen();
        return;
    }

    // Player-Liste
    int y0 = SET_PLR_Y0;
    for (size_t i = 0; i < g_settPlayers.size(); i++) {
        int y = y0 + i * (SET_PLR_H + SET_PLR_GAP);
        if (y + SET_PLR_H > DISPLAY_HEIGHT) break;

        if (tx >= SET_BOX_X && tx <= SET_BOX_X + SET_BOX_W && ty >= y && ty <= y + SET_PLR_H) {
            wifiSetPlayer(g_settPlayers[i].mac);
            drawSettingsScreen();
            return;
        }

        // Sync-Button
        if (tx >= SET_LISTE_X && tx <= SET_LISTE_X + SET_LISTE_W && ty >= y + 4 && ty <= y + SET_PLR_H - 4) {
            if (g_settPlayers[i].mac != wifiGetPlayerMac()) {
                // Sync toggle (vereinfacht)
                wifiSetPlayer(g_settPlayers[i].mac);
                drawSettingsScreen();
            }
            return;
        }
    }
}

// ─── Display-Init ─────────────────────────────────────────────────────────

void displayInit() {
    // SPI für Touch initialisieren
    if (!touchSPIinited) {
        touchSPI.begin(PIN_TOUCH_SCLK, PIN_TOUCH_MISO, PIN_TOUCH_MOSI, PIN_TOUCH_CS);
        pinMode(PIN_TOUCH_CS, OUTPUT);
        digitalWrite(PIN_TOUCH_CS, HIGH);
        touchSPIinited = true;
    }

    // Display initialisieren
    tft.init();
    tft.setRotation(DISPLAY_ROTATION);
    tft.fillScreen(COL_BG);

    // Hintergrundbeleuchtung einschalten
    pinMode(PIN_TFT_BL, OUTPUT);
    digitalWrite(PIN_TFT_BL, HIGH);

    // Cover-Puffer in PSRAM allozieren
    if (!g_coverBuf)   g_coverBuf   = (uint8_t*)ps_malloc(64000);
    if (!g_coverDlBuf) g_coverDlBuf = (uint8_t*)ps_malloc(64000);

    // Mutex erstellen
    if (!g_coverMux) g_coverMux = xSemaphoreCreateMutex();

    // Kalibrierungsdaten aus NVS laden
    Preferences prefs;
    prefs.begin(NVS_NAMESPACE, true);
    cal_valid = false;
    if (prefs.isKey(NVS_KEY_CAL_OK)) {
        cal_x0    = prefs.getShort(NVS_KEY_CAL_X0,  300);
        cal_x1    = prefs.getShort(NVS_KEY_CAL_X1,  3800);
        cal_y0    = prefs.getShort(NVS_KEY_CAL_Y0,  300);
        cal_y1    = prefs.getShort(NVS_KEY_CAL_Y1,  3800);
        cal_swap  = prefs.getUChar(NVS_KEY_CAL_SWAP, 0);
        cal_valid = true;
    }
    prefs.end();

    if (!cal_valid) {
        runCalibration();
    }
}
