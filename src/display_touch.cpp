#include "display_intern.h"

// ─── XPT2046 Touch ──────────────────────────────────────────────────────────

bool xpt2046ReadRaw(int32_t* rx, int32_t* ry) {
    if (!touchSPIinited) return false;
    touchSPI.beginTransaction(SPISettings(2000000, MSBFIRST, SPI_MODE0));
    digitalWrite(PIN_TOUCH_CS, LOW);
    touchSPI.transfer(0xB1);
    int16_t z1 = touchSPI.transfer16(0) >> 3;
    touchSPI.transfer(0xC1);
    touchSPI.transfer16(0);
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

bool xpt2046Read(int32_t* x, int32_t* y) {
    int32_t rx, ry;
    if (!xpt2046ReadRaw(&rx, &ry)) return false;
    int32_t sx = cal_swap ? ry : rx;
    int32_t sy = cal_swap ? rx : ry;
    *x = map(sx, cal_x0, cal_x1, CAL_X_LEFT,   CAL_X_RIGHT);
    *y = map(sy, cal_y0, cal_y1, CAL_Y_TOP,    CAL_Y_BOTTOM);
    *x = constrain(*x, 0, DISPLAY_WIDTH  - 1);
    *y = constrain(*y, 0, DISPLAY_HEIGHT - 1);
    return true;
}

// ─── Kalibrierung ────────────────────────────────────────────────────────────

bool calWaitTouch(int32_t* rx, int32_t* ry) {
    uint32_t t0 = millis();
    while (millis() - t0 < 2000) {
        int32_t dummy1, dummy2;
        if (!xpt2046ReadRaw(&dummy1, &dummy2)) break;
        delay(30);
    }
    while (true) {
        int32_t tx, ty;
        if (xpt2046ReadRaw(&tx, &ty)) {
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

void runCalibration() {
    tft.fillScreen(TFT_BLACK);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.setTextDatum(MC_DATUM);
    tft.setTextSize(2);
    tft.drawString("Touch-Kalibrierung", DISPLAY_WIDTH / 2, 60);
    tft.setTextSize(1);
    tft.drawString("Bitte nacheinander die 3 Kreuzmarkierungen beruehren.", DISPLAY_WIDTH / 2, 100);
    delay(2000);

    int32_t r[3][2];

    tft.fillScreen(TFT_BLACK);
    tft.setTextColor(TFT_YELLOW, TFT_BLACK);
    tft.setTextDatum(MC_DATUM);
    tft.setTextSize(1);
    tft.drawString("1 von 3 - oben links beruehren", DISPLAY_WIDTH / 2, DISPLAY_HEIGHT / 2);
    drawCrosshair(CAL_X_LEFT, CAL_Y_TOP);
    calWaitTouch(&r[0][0], &r[0][1]);
    srvLog("[Cal] Punkt 0 (oben-links):  roh X=%d Y=%d", r[0][0], r[0][1]);

    tft.fillScreen(TFT_BLACK);
    tft.drawString("2 von 3 - oben rechts beruehren", DISPLAY_WIDTH / 2, DISPLAY_HEIGHT / 2);
    drawCrosshair(CAL_X_RIGHT, CAL_Y_TOP);
    calWaitTouch(&r[1][0], &r[1][1]);
    srvLog("[Cal] Punkt 1 (oben-rechts): roh X=%d Y=%d", r[1][0], r[1][1]);

    tft.fillScreen(TFT_BLACK);
    tft.drawString("3 von 3 - unten links beruehren", DISPLAY_WIDTH / 2, DISPLAY_HEIGHT / 2);
    drawCrosshair(CAL_X_LEFT, CAL_Y_BOTTOM);
    calWaitTouch(&r[2][0], &r[2][1]);
    srvLog("[Cal] Punkt 2 (unten-links): roh X=%d Y=%d", r[2][0], r[2][1]);

    int32_t dxRawX = abs(r[1][0] - r[0][0]);
    int32_t dxRawY = abs(r[1][1] - r[0][1]);
    cal_swap = (dxRawY > dxRawX) ? 1 : 0;
    srvLog("[Cal] Achsentausch: %s", cal_swap ? "ja" : "nein");

    if (!cal_swap) {
        cal_x0 = r[0][0]; cal_x1 = r[1][0];
        cal_y0 = r[0][1]; cal_y1 = r[2][1];
    } else {
        cal_x0 = r[0][1]; cal_x1 = r[1][1];
        cal_y0 = r[0][0]; cal_y1 = r[2][0];
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

// ─── Touch-Behandlung (Haupt-Logik) ──────────────────────────────────────────

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

    // Kalibrierung: obere linke Ecke 3 Sek. halten
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

    // Einzelklick-Aktionen
    if (pressed && !lastPressed) {
        String mac = wifiGetPlayerMac();

        if (ty < PL_HDR_H && tx > DISPLAY_WIDTH - 62) {
            drawSettingsScreen();
            lastPressed = true;
            return;
        }

        int cy = PL_TRANS_CY;
        if ((long)(tx - 90) * (tx - 90) + (long)(ty - cy) * (ty - cy) <= 28L * 28L) {
            wifiBeginUserCommand();
            bool ok = squeezePrev(mac);
            wifiEndUserCommand(ok);
        } else if ((long)(tx - 390) * (tx - 390) + (long)(ty - cy) * (ty - cy) <= 28L * 28L) {
            wifiBeginUserCommand();
            bool ok = squeezeNext(mac);
            wifiEndUserCommand(ok);
        } else if ((long)(tx - 240) * (tx - 240) + (long)(ty - cy) * (ty - cy) <= 33L * 33L) {
            wifiBeginUserCommand();
            bool ok = g_dPlaying ? squeezePause(mac) : squeezePlay(mac);
            if (ok) {
                SqueezieStatus cached = squeezieGetCachedStatus();
                cached.playing = !cached.playing;
                wifiUpdateCachedStatus(cached);
            }
            wifiEndUserCommand(ok);
        }
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

    // Lautstärke
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
