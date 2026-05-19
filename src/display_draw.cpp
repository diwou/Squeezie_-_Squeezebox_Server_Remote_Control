#include "display_intern.h"

// ─── Hilfsfunktionen ────────────────────────────────────────────────────────

void drawButton(int16_t x, int16_t y, int16_t w, int16_t h,
                uint32_t bgColor, uint32_t txtColor, const char* label,
                bool highlighted) {
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

void drawCrosshair(int16_t x, int16_t y) {
    tft.drawFastHLine(x - 20, y,      40, TFT_RED);
    tft.drawFastVLine(x,      y - 20, 40, TFT_RED);
    tft.drawCircle(x, y, 6, TFT_RED);
}

// ─── Player-Screen Zeichenhelfer ─────────────────────────────────────────────

void drawCircleBtn(int16_t cx, int16_t cy, int16_t r,
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

void drawVolumeIcon(int16_t x, int16_t y, int16_t w, int16_t h,
                    uint32_t bg, uint32_t fg, bool louder,
                    uint32_t border) {
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

void drawMuteIcon(int16_t x, int16_t y, int16_t w, int16_t h, bool muted) {
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

void drawRepeatGlyph(int16_t x, int16_t y, uint32_t fg) {
    tft.drawFastHLine(x, y, 14, fg);
    tft.drawFastVLine(x, y, 5, fg);
    tft.drawFastHLine(x + 5, y + 10, 14, fg);
    tft.drawFastVLine(x + 19, y + 5, 6, fg);
    tft.fillTriangle(x + 14, y - 3, x + 14, y + 3, x + 19, y, fg);
    tft.fillTriangle(x + 5, y + 7, x + 5, y + 13, x, y + 10, fg);
}

void drawShuffleGlyph(int16_t x, int16_t y, uint32_t fg) {
    tft.drawLine(x, y + 1, x + 6, y + 1, fg);
    tft.drawLine(x + 6, y + 1, x + 12, y + 6, fg);
    tft.drawLine(x + 12, y + 6, x + 18, y + 6, fg);
    tft.drawLine(x, y + 11, x + 6, y + 11, fg);
    tft.drawLine(x + 6, y + 11, x + 12, y + 6, fg);
    tft.drawLine(x + 12, y + 6, x + 18, y + 6, fg);
    tft.fillTriangle(x + 18, y + 3, x + 18, y + 9, x + 23, y + 6, fg);
}

// ─── Textausgabe ─────────────────────────────────────────────────────────────

void drawInfoLine(const char* txt, int16_t x, int16_t y, int16_t w,
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

void drawScrollLine(const char* txt, int16_t x, int16_t y, int16_t w,
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
        tft.drawString(txt, x + 2, y + 2);
        tft.clearClipRect();
    } else {
        tft.drawString(txt, x + 2 - offset, y + 2);
        tft.clearClipRect();
    }
}

void drawPlayerScrollLine(const char* txt, int16_t x, int16_t y, int16_t w,
                          uint32_t color, uint8_t size, int32_t offset, bool bold) {
    setPlayerInfoFont(size);
    int lh = tft.fontHeight() + 4;
    LGFX_Sprite& infoSprite = (size >= 3) ? g_infoLineSpriteBig : g_infoLineSpriteSmall;
    if (!ensureInfoLineSprite(infoSprite, lh)) {
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
    infoSprite.fillSprite(COL_BG);
    infoSprite.setFont(tft.getFont());
    infoSprite.setTextSize(1);
    infoSprite.setTextDatum(TL_DATUM);
    infoSprite.setTextColor(color, COL_BG);
    if (!txt || !txt[0]) {
        infoSprite.pushSprite(x, y);
        return;
    }
    int tw = infoSprite.textWidth(txt);
    if (tw + 4 <= w || offset == 0) {
        infoSprite.drawString(txt, 2, 2);
        if (bold) infoSprite.drawString(txt, 3, 2);
    } else {
        infoSprite.drawString(txt, 2 - offset, 2);
        if (bold) infoSprite.drawString(txt, 3 - offset, 2);
    }
    infoSprite.pushSprite(x, y);
}

// ─── UTF-8 → ASCII ──────────────────────────────────────────────────────────

String utf8ToAscii(const String& src) {
    String out;
    out.reserve(src.length());
    const uint8_t* p = (const uint8_t*)src.c_str();
    while (*p) {
        uint8_t b = *p;
        if (b < 0x80) { out += (char)b; p++; continue; }

        if ((b & 0xE0) == 0xC0 && p[1]) {
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
            else if (cp == 0xDF)               s = "ss";
            else if (cp >= 0xE0 && cp <= 0xE6) s = "a";
            else if (cp == 0xE7)               s = "c";
            else if (cp >= 0xE8 && cp <= 0xEB) s = "e";
            else if (cp >= 0xEC && cp <= 0xEF) s = "i";
            else if (cp == 0xF0)               s = "d";
            else if (cp == 0xF1)               s = "n";
            else if (cp >= 0xF2 && cp <= 0xF8) s = "o";
            else if (cp >= 0xF9 && cp <= 0xFC) s = "u";
            else if (cp >= 0xFD && cp <= 0xFF) s = "y";
            else if (cp == 0x130)              s = "I";
            else if (cp == 0x131)              s = "i";
            out += s; continue;
        }
        if ((b & 0xF0) == 0xE0 && p[1] && p[2]) {
            uint32_t cp = ((b & 0x0F) << 12) | ((p[1] & 0x3F) << 6) | (p[2] & 0x3F);
            p += 3;
            if      (cp == 0x2013 || cp == 0x2014) out += '-';
            else if (cp == 0x2018 || cp == 0x2019) out += '\'';
            else if (cp == 0x201C || cp == 0x201D) out += '"';
            else if (cp == 0x2026)                 out += "...";
            else if (cp == 0x2122)                 out += "TM";
            else                                   out += '?';
            continue;
        }
        if ((b & 0xF8) == 0xF0 && p[1] && p[2] && p[3]) {
            p += 4; out += '?'; continue;
        }
        p++;
    }
    return out;
}

// ─── Font-Hilfen ─────────────────────────────────────────────────────────────

void setPlayerInfoFont(uint8_t size) {
    tft.setTextSize(1);
    if (size >= 3) tft.setFont(&fonts::lgfxJapanGothicP_28);
    else           tft.setFont(&fonts::lgfxJapanGothicP_24);
}

int getPlayerInfoLineHeight(uint8_t size) {
    setPlayerInfoFont(size);
    return tft.fontHeight() + 4;
}

bool ensureInfoLineSprite(LGFX_Sprite& sprite, int height) {
    if (height <= 0) return false;
    if (sprite.width() == PL_INFO_W && sprite.height() == height) return true;
    if (sprite.width() > 0 && sprite.height() > 0) sprite.deleteSprite();
    sprite.setColorDepth(16);
    return sprite.createSprite(PL_INFO_W, height);
}

// ─── Player-Screen Komponenten ───────────────────────────────────────────────

void drawPlayerHeader(const char* playerName, bool playing) {
    tft.fillRect(0, PL_HDR_Y, DISPLAY_WIDTH, PL_HDR_H, COL_HDR);
    tft.setFont(nullptr); tft.setTextSize(2);
    tft.setTextDatum(ML_DATUM);
    tft.setTextColor(COL_LGREY, COL_HDR);
    tft.drawString(playerName && *playerName ? playerName : "Squeezie", 8, PL_HDR_H / 2);
    tft.fillRoundRect(DISPLAY_WIDTH - 62, 3, 58, PL_HDR_H - 6, 4, COL_DARK);
    tft.drawRoundRect(DISPLAY_WIDTH - 62, 3, 58, PL_HDR_H - 6, 4, COL_MID);
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(COL_WHITE, COL_DARK);
    tft.drawString("EINST.", DISPLAY_WIDTH - 33, PL_HDR_H / 2);
    tft.fillCircle(DISPLAY_WIDTH - 85, PL_HDR_H / 2, 5,
                    playing ? (uint32_t)COL_GREEN : (uint32_t)COL_MID);
    {
        time_t tnow = time(nullptr);
        struct tm ti; localtime_r(&tnow, &ti);
        if (ti.tm_year > 100) {
            char tbuf[6];
            snprintf(tbuf, sizeof(tbuf), "%02d:%02d", ti.tm_hour, ti.tm_min);
            tft.setFont(nullptr); tft.setTextSize(2);
            tft.setTextDatum(MC_DATUM);
            tft.setTextColor(COL_MGREY, COL_HDR);
            tft.drawString(tbuf, DISPLAY_WIDTH / 2, PL_HDR_H / 2);
        }
    }
}

void drawCoverPlaceholder() {
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

void drawCoverImage(const uint8_t* buf, size_t len) {
    tft.fillRect(PL_COVER_X, PL_COVER_Y, PL_COVER_W, PL_COVER_H, COL_BG);
    if (buf && len > 0) {
        tft.drawJpg(buf, (uint32_t)len, PL_COVER_X, PL_COVER_Y, PL_COVER_W, PL_COVER_H);
    }
}

void drawProgressBar(int position, int duration) {
    int y  = PL_PROG_Y + 2;
    int h  = PL_PROG_H - 4;
    int bw = DISPLAY_WIDTH - 4;
    int filled = (duration > 0) ?
        constrain((int)((long)position * bw / duration), 0, bw) : 0;

    if (g_lastProgFilled < 0) {
        tft.fillRect(2, y, bw, h, COL_PROG_BG);
        if (filled > 0) tft.fillRect(2, y, filled, h, COL_GREEN);
    } else if (filled > g_lastProgFilled) {
        tft.fillRect(2 + g_lastProgFilled, y, filled - g_lastProgFilled, h, COL_GREEN);
    } else if (filled < g_lastProgFilled) {
        tft.fillRect(2 + filled, y, g_lastProgFilled - filled, h, COL_PROG_BG);
    }
    g_lastProgFilled = filled;

    char buf[20];
    snprintf(buf, sizeof(buf), "%d:%02d / %d:%02d",
             position / 60, position % 60, duration / 60, duration % 60);
    tft.setFont(nullptr); tft.setTextSize(2);
    int tw  = tft.textWidth(buf);
    int trx = DISPLAY_WIDTH - 3;
    int rx0 = max(2, trx - tw - 2);
    int rx1 = min(2 + bw, trx + 2);
    int gx1 = 2 + filled;
    if (gx1 > rx0)
        tft.fillRect(rx0, y, min(gx1, rx1) - rx0, h, COL_GREEN);
    if (gx1 < rx1)
        tft.fillRect(max(rx0, gx1), y, rx1 - max(rx0, gx1), h, COL_PROG_BG);
    tft.setTextDatum(MR_DATUM);
    tft.setTextColor(COL_WHITE);
    tft.drawString(buf, trx, PL_PROG_Y + PL_PROG_H / 2);
}

void drawTransportButtons(bool playing) {
    tft.fillRect(0, PL_TRANS_Y, DISPLAY_WIDTH, PL_TRANS_H, COL_BG);
    int cy = PL_TRANS_CY;
    drawCircleBtn( 90, cy, 29, (uint32_t)0x3A3A3A, COL_WHITE, "prev");
    uint32_t playBg = playing ? (uint32_t)0x3A3A3A : (uint32_t)COL_GREEN;
    uint32_t playFg = playing ? (uint32_t)COL_GREEN : (uint32_t)COL_BG;
    drawCircleBtn(240, cy, 35, playBg, playFg, playing ? "pause" : "play");
    drawCircleBtn(390, cy, 29, (uint32_t)0x3A3A3A, COL_WHITE, "next");
}

void drawVolumeButtons(bool muted) {
    int y = PL_VOL_Y, h = PL_VOL_H;
    uint32_t volBg = muted ? (uint32_t)0x242424 : (uint32_t)0x3A3A3A;
    uint32_t volFg = muted ? (uint32_t)COL_DGREY : (uint32_t)COL_WHITE;
    uint32_t volBorder = muted ? (uint32_t)COL_DGREY : (uint32_t)COL_WHITE;
    drawVolumeIcon(PL_VOL_MINUS_X, y + 2, PL_VOL_BTN_W, h - 4, volBg, volFg, false, volBorder);
    drawVolumeIcon(PL_VOL_PLUS_X,  y + 2, PL_VOL_BTN_W, h - 4, volBg, volFg, true,  volBorder);
    drawMuteIcon(PL_VOL_MUTE_X, y + 2, PL_VOL_BTN_W, h - 4, muted);
}

void drawVolumeRow(int volume, bool muted) {
    int y = PL_VOL_Y, h = PL_VOL_H;
    int real_pct = constrain(volume, 0, 100);
    int bx = PL_VOL_BAR_X, bw = PL_VOL_BAR_W;
    int filled = constrain(real_pct * bw / 100, 0, bw);
    uint32_t fillCol = muted ? (uint32_t)COL_MGREY : (uint32_t)COL_GREEN;

    if (g_lastVolFilled < 0 || muted != g_lastVolMuted) {
        if (filled > 0)  tft.fillRect(bx,          y + 5, filled,      h - 10, fillCol);
        if (filled < bw) tft.fillRect(bx + filled,  y + 5, bw - filled, h - 10, COL_PROG_BG);
    } else if (filled > g_lastVolFilled) {
        tft.fillRect(bx + g_lastVolFilled, y + 5, filled - g_lastVolFilled, h - 10, fillCol);
    } else if (filled < g_lastVolFilled) {
        tft.fillRect(bx + filled, y + 5, g_lastVolFilled - filled, h - 10, COL_PROG_BG);
    }
    g_lastVolFilled = filled;
    g_lastVolMuted = muted;

    char buf[6]; snprintf(buf, sizeof(buf), "%d%%", real_pct);
    tft.setFont(nullptr); tft.setTextSize(2);
    int tw  = tft.textWidth(buf);
    int tcx = bx + bw / 2;
    int rx0 = max(bx, tcx - tw / 2 - 2);
    int rx1 = min(bx + bw, tcx + tw / 2 + tw % 2 + 2);
    int gx1 = bx + filled;
    if (gx1 > rx0)
        tft.fillRect(rx0, y + 5, min(gx1, rx1) - rx0, h - 10, fillCol);
    if (gx1 < rx1)
        tft.fillRect(max(rx0, gx1), y + 5, rx1 - max(rx0, gx1), h - 10, COL_PROG_BG);
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(COL_WHITE);
    tft.drawString(buf, tcx, y + h / 2);
}

void drawModeRow(int repeat, int shuffle) {
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
