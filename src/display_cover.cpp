#include "display_intern.h"
#include <esp_task_wdt.h>

// ─── Cover-Download (Single-Shot FreeRTOS-Task auf Core 0) ───────────────────

/**
 * @brief Prüft, ob ein Puffer einen vollständigen JPEG enthält.
 *
 * Ein vollständiges JPEG endet mit dem EOI-Marker 0xFF 0xD9.
 * @param buf  Puffer mit JPEG-Daten.
 * @param len  Anzahl Bytes im Puffer.
 * @return true wenn EOI-Marker gefunden wurde.
 */
bool isCompleteJpeg(const uint8_t* buf, size_t len) {
    if (!buf || len < 2) return false;
    return buf[len - 2] == 0xFF && buf[len - 1] == 0xD9;
}

/**
 * @brief Liest den HTTP-Response-Body in einen Puffer.
 *
 * @param http      Referenz auf die HTTPClient-Instanz (bereits verbunden).
 * @param buf       Zielpuffer (PSRAM).
 * @param maxLen    Maximale Anzahl Bytes.
 * @param timeoutMs Timeout pro read()-Aufruf.
 * @return size_t   Tatsächlich gelesene Bytes.
 */
size_t readHttpBody(HTTPClient& http, uint8_t* buf, size_t maxLen, uint32_t timeoutMs) {
    if (!buf || maxLen == 0) return 0;
    WiFiClient* stream = http.getStreamPtr();
    if (!stream) return 0;

    size_t total = 0;
    uint32_t last = millis();
    while (total < maxLen) {
        uint32_t now = millis();
        if (now - last > timeoutMs) break;
        if (!stream->connected()) break;

        // Watchdog feeden während langer HTTP-Reads
        esp_task_wdt_reset();

        int avail = stream->available();
        if (avail <= 0) {
            delay(1);
            continue;
        }
        last = now;

        int room = maxLen - total;
        int rd   = (avail < room) ? avail : room;
        int got  = stream->readBytes(buf + total, rd);
        total   += got;
        if (got < rd) break;
    }
    return total;
}

/**
 * @brief Prüft, ob der Cover-Download-Task timeout-überschritten hat.
 *
 * @param now       Aktuelle millis()-Zeit.
 * @param startedAt Zeitstempel des Task-Starts.
 * @return true wenn (now - startedAt) > COVER_TASK_TIMEOUT_MS.
 */
bool coverTaskTimedOut(uint32_t now, uint32_t startedAt) {
    return (now - startedAt) > COVER_TASK_TIMEOUT_MS;
}

/**
 * @brief Single-Shot FreeRTOS-Task: Lädt ein Cover-JPEG von einer HTTP-URL.
 *
 * Wird von displayPreloadCover() erzeugt und läuft auf Core 0.
 * Kopiert die Download-Parameter unter Mutex, führt den Download
 * (mit LittleFS-Cache) durch und signalisiert das Ergebnis
 * über g_coverReady/g_dlGen. Der Task terminiert sich selbst.
 *
 * @param p Unused (FreeRTOS-Parameter).
 */
void coverDlTask(void* p) {
    (void)p;
    // Kein esp_task_wdt_add() – der Task läuft kurz (HTTP mit Timeout) und
    // terminiert sich selbst. Der Watchdog wird über die main-loop gefeedet
    // und während des HTTP-Reads in readHttpBody().
    esp_task_wdt_reset();

    // ── Parameter unter Mutex kopieren ────────────────────────────────────
    xSemaphoreTake(g_coverMux, portMAX_DELAY);
    String url = g_pendingCoverUrl;
    String key = g_pendingCoverKey;
    g_pendingCoverUrl     = "";
    g_pendingCoverKey     = "";
    g_coverTaskStartedAt  = millis();
    xSemaphoreGive(g_coverMux);

    size_t len = 0;
    bool   ok  = false;

    if (!url.isEmpty() && g_coverDlBuf) {
        // ── LittleFS-Cache prüfen ─────────────────────────────────────────
        if (key.length() > 0) {
            String cachePath = "/cover/" + key + ".jpg";
            if (LittleFS.exists(cachePath)) {
                File f = LittleFS.open(cachePath, "r");
                if (f) {
                    len = f.read(g_coverDlBuf, 64000);
                    if (len > 0 && isCompleteJpeg(g_coverDlBuf, len)) ok = true;
                    f.close();
                }
            }
        }

        // ── HTTP-Download (falls nicht gecached) ──────────────────────────
        if (!ok) {
            HTTPClient http;
            http.setTimeout(3000);
            esp_task_wdt_reset();
            if (http.begin(url)) {
                esp_task_wdt_reset();
                int code = http.GET();
                esp_task_wdt_reset();
                if (code == 200) {
                    len = readHttpBody(http, g_coverDlBuf, 64000, 1200);
                    if (len > 0 && isCompleteJpeg(g_coverDlBuf, len)) {
                        ok = true;
                        // In LittleFS-Cache schreiben
                        if (key.length() > 0) {
                            if (!LittleFS.exists("/cover")) LittleFS.mkdir("/cover");
                            String cachePath = "/cover/" + key + ".jpg";
                            File f = LittleFS.open(cachePath, "w");
                            if (f) { f.write(g_coverDlBuf, len); f.close(); }
                        }
                    }
                }
                http.end();
                esp_task_wdt_reset();
            } else {
                esp_task_wdt_reset();
            }
        }
    }

    // ── Ergebnis veröffentlichen ──────────────────────────────────────────
    xSemaphoreTake(g_coverMux, portMAX_DELAY);
    if (ok) {
        g_coverLen        = len;
        g_loadedCoverKey  = key;
        g_coverReady      = true;
        g_dlGen++;
        g_coverRetryAt    = 0;
    } else {
        g_coverLen        = 0;
        g_loadedCoverKey  = "";
        g_dispatchedCoverUrl  = "";
        g_dispatchedCoverKey  = "";
        g_coverRetryAt    = millis() + COVER_RETRY_DELAY_MS;
    }
    g_coverTask          = nullptr;
    g_coverTaskStartedAt = 0;
    xSemaphoreGive(g_coverMux);

    esp_task_wdt_reset();
    vTaskDelete(nullptr);
}

// ─── displayCopyCover / displayGetCoverSerial ─────────────────────────────────

/**
 * @brief Kopiert das aktuelle Cover thread-sicher in einen Vektor.
 *
 * @param[out] out Zielvektor für die JPEG-Rohdaten.
 * @return true wenn ein Cover verfügbar war.
 */
bool displayCopyCover(std::vector<uint8_t>& out) {
    xSemaphoreTake(g_coverMux, portMAX_DELAY);
    bool ok = false;
    if (g_coverBuf && g_coverLen > 0) {
        out.assign(g_coverBuf, g_coverBuf + g_coverLen);
        ok = true;
    }
    xSemaphoreGive(g_coverMux);
    return ok;
}

/**
 * @brief Gibt die Seriennummer des aktuellen Covers zurück.
 *
 * @return uint32_t Seriennummer (0 = kein Cover).
 */
uint32_t displayGetCoverSerial() {
    return g_coverSerial;
}

// ─── displayPreloadCover ──────────────────────────────────────────────────────

/**
 * @brief Startet einen asynchronen Cover-Download (Single-Shot-Task).
 *
 * Kann aus jedem Task (z. B. lmsPoll auf Core 0) aufgerufen werden.
 * Erzeugt einen neuen coverDlTask auf Core 0, der das JPEG lädt,
 * signalisiert und sich selbst terminiert (wie im Code vor dem Code-Review).
 *
 * @param url      Vollständige HTTP-URL zum Cover-JPEG.
 * @param cacheKey Optionaler Cache-Key für LittleFS.
 */
void displayPreloadCover(const String& url, const String& cacheKey) {
    if (url.isEmpty()) return;

    xSemaphoreTake(g_coverMux, portMAX_DELAY);

    // Bereits geladen? (Cache-Hit)
    if (!cacheKey.isEmpty() && g_coverLen > 0 && g_loadedCoverKey == cacheKey) {
        xSemaphoreGive(g_coverMux);
        return;
    }
    // Task läuft bereits?
    if (g_coverTask != nullptr) {
        // Timeout? Dann alten Task aufgeben
        if (coverTaskTimedOut(millis(), g_coverTaskStartedAt)) {
            g_coverTask = nullptr;
        } else {
            xSemaphoreGive(g_coverMux);
            return;
        }
    }
    // Retry-Blockade?
    if (g_coverRetryAt != 0 && millis() < g_coverRetryAt) {
        xSemaphoreGive(g_coverMux);
        return;
    }

    // Neuen Download anstossen
    g_pendingCoverUrl    = url;
    g_pendingCoverKey    = cacheKey;
    g_dispatchedCoverUrl = url;
    g_dispatchedCoverKey = cacheKey;
    g_coverRetryAt       = 0;
    g_coverTaskStartedAt = millis();
    g_coverReady         = false;
    g_coverLen           = 0;
    g_loadedCoverKey     = "";

    xTaskCreatePinnedToCore(coverDlTask, "coverDl", 8192, nullptr, 1, &g_coverTask, 0);
    xSemaphoreGive(g_coverMux);
}
