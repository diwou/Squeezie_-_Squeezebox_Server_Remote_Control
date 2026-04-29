#include "display_intern.h"
#include <esp_task_wdt.h>

// ─── Cover-Download (FreeRTOS-Task auf Core 0) ─────────────────────────────

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
 * @brief FreeRTOS-Task: Lädt ein Cover-JPEG von einer HTTP-URL herunter.
 *
 * Läuft auf Core 0. Nutzt g_coverMux für thread-sicheren Zugriff auf
 * g_pendingCoverUrl/g_pendingCoverKey. Schreibt das Ergebnis in g_coverDlBuf
 * und signalisiert Fertigstellung über g_coverReady/g_dlGen.
 *
 * Unterstützt LittleFS-Caching: Wenn ein cacheKey gesetzt ist, wird vor dem
 * HTTP-Download im LittleFS nachgesehen, ob die Datei bereits gecached ist.
 * Nach erfolgreichem Download wird der Cache aktualisiert.
 *
 * @param p Unused (FreeRTOS-Parameter).
 */
void coverDlTask(void* p) {
    (void)p;
    esp_task_wdt_add(nullptr);

    for (;;) {
        // Mit 2s-Timeout warten, damit Watchdog regelmässig gefüttert wird
        // und Retry- oder Timeout-Prüfungen möglich sind.
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(2000));
        esp_task_wdt_reset();

        // ── Auftrag aus der Queue holen oder Retry auslösen ─────────────
        xSemaphoreTake(g_coverMux, portMAX_DELAY);

        bool hasNew  = !g_pendingCoverUrl.isEmpty();
        bool doRetry = (!hasNew && g_coverRetryAt > 0
                        && millis() >= g_coverRetryAt
                        && !g_dispatchedCoverUrl.isEmpty());

        String url, key;
        if (hasNew) {
            // Neuer Auftrag von displayPreloadCover()
            url = g_pendingCoverUrl;
            key = g_pendingCoverKey;
            g_dispatchedCoverUrl  = url;
            g_dispatchedCoverKey  = key;
            g_coverTaskStartedAt  = millis();
            g_pendingCoverUrl     = "";
            g_pendingCoverKey     = "";
            g_coverRetryAt        = 0;  // Retry-Zähler zurücksetzen
        } else if (doRetry) {
            // Fehlgeschlagener Download wiederholen
            url = g_dispatchedCoverUrl;
            key = g_dispatchedCoverKey;
            g_coverTaskStartedAt  = millis();
            g_coverRetryAt        = 0;
        }

        xSemaphoreGive(g_coverMux);

        if (!hasNew && !doRetry) continue;  // Keine Arbeit → warten
        if (url.isEmpty()) continue;

        // ── Puffer bereitstellen ─────────────────────────────────────────
        // g_coverDlBuf wird in displayInit() allokiert (PSRAM, 64 kB).
        if (!g_coverDlBuf) continue;
        size_t maxLen = 64000;

        uint8_t* buf = g_coverDlBuf;
        size_t   len = 0;
        bool     ok  = false;

        // ── Cache prüfen ─────────────────────────────────────────────────
        if (key.length() > 0) {
            String cachePath = "/cover/" + key + ".jpg";
            if (LittleFS.exists(cachePath)) {
                File f = LittleFS.open(cachePath, "r");
                if (f) {
                    len = f.read(buf, maxLen);
                    if (len > 0 && isCompleteJpeg(buf, len)) {
                        ok = true;
                    }
                    f.close();
                }
            }
        }

        // ── HTTP-Download (falls nicht gecached) ─────────────────────────
        if (!ok) {
            HTTPClient http;
            http.setTimeout(8000);
            http.setConnectTimeout(5000);
            // Watchdog vor HTTP-Operationen feeden (Verbindungsaufbau kann blockieren)
            esp_task_wdt_reset();
            http.begin(url);
            esp_task_wdt_reset();
            int code = http.GET();
            esp_task_wdt_reset();
            if (code == 200) {
                len = readHttpBody(http, buf, maxLen, 8000);
                if (len > 0 && isCompleteJpeg(buf, len)) {
                    ok = true;
                    // Cache schreiben
                    if (key.length() > 0) {
                        if (!LittleFS.exists("/cover")) LittleFS.mkdir("/cover");
                        String cachePath = "/cover/" + key + ".jpg";
                        File f = LittleFS.open(cachePath, "w");
                        if (f) {
                            f.write(buf, len);
                            f.close();
                        }
                    }
                }
            }
            http.end();
            // Watchdog nach HTTP-Operationen feeden
            esp_task_wdt_reset();
        }

        // ── Ergebnis signalisieren ───────────────────────────────────────
        xSemaphoreTake(g_coverMux, portMAX_DELAY);
        if (ok) {
            g_coverReady = true;
            g_coverLen   = len;
            g_dlGen++;
        } else {
            // Retry später
            g_coverRetryAt = millis() + COVER_RETRY_DELAY_MS;
        }
        // Wichtig: g_coverTask NICHT auf nullptr setzen! Die Task läuft in
        // einer Endlosschleife und das Handle wird für xTaskNotifyGive()
        // in displayPreloadCover() benötigt.
        xSemaphoreGive(g_coverMux);

        esp_task_wdt_reset();
    }
}

/**
 * @brief Kopiert das aktuelle Cover thread-sicher in einen Vektor.
 *
 * @param[out] out Zielvektor für die JPEG-Rohdaten.
 * @return true wenn ein Cover verfügbar war.
 */
bool displayCopyCover(std::vector<uint8_t>& out) {
    xSemaphoreTake(g_coverMux, portMAX_DELAY);
    bool ok = false;
    if (g_coverReady && g_coverBuf && g_coverLen > 0) {
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

// ─── Prefetch-Task ─────────────────────────────────────────────────────────

/**
 * @brief FreeRTOS-Task: Lädt Cover im Voraus (Prefetch) auf Core 0.
 *
 * Verarbeitet die g_pfQueue sequenziell. Jeder Slot wird mit einem JPEG
 * aus PSRAM befüllt und als ready markiert.
 *
 * @param p Unused.
 */
void prefetchTask(void* p) {
    (void)p;
    esp_task_wdt_add(nullptr);

    for (;;) {
        // Mit 2s-Timeout warten, damit Watchdog regelmässig gefüttert wird
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(2000));
        esp_task_wdt_reset();

        // Queue-Inhalt lokal sichern (unter Mutex)
        xSemaphoreTake(g_pfMux, portMAX_DELAY);
        int count = g_pfQueueCount;
        // Kopie anlegen, damit Queue während Verarbeitung freigegeben werden kann
        String urls[SQUEEZE_PREFETCH];
        String keys[SQUEEZE_PREFETCH];
        for (int i = 0; i < count; i++) {
            urls[i] = g_pfQueue[i];
            keys[i] = g_pfQueueKeys[i];
        }
        g_pfQueueCount = 0;
        xSemaphoreGive(g_pfMux);

        if (count == 0) continue;

        for (int i = 0; i < count; i++) {
            const String& url = urls[i];
            const String& key = keys[i];
            if (url.isEmpty()) continue;

            // Slot suchen
            int slot = -1;
            for (int s = 0; s < SQUEEZE_PREFETCH; s++) {
                if (!g_pf[s].ready && g_pf[s].buf) {
                    slot = s;
                    break;
                }
            }
            if (slot < 0) break;

            // Cache prüfen
            size_t   len = 0;
            uint8_t* buf = g_pf[slot].buf;
            size_t maxLen = 64000;
            bool ok = false;

            if (key.length() > 0) {
                String cachePath = "/cover/" + key + ".jpg";
                if (LittleFS.exists(cachePath)) {
                    File f = LittleFS.open(cachePath, "r");
                    if (f) {
                        len = f.read(buf, maxLen);
                        if (len > 0 && isCompleteJpeg(buf, len)) ok = true;
                        f.close();
                    }
                }
            }

            if (!ok) {
                HTTPClient http;
                http.setTimeout(8000);
                http.setConnectTimeout(5000);
                esp_task_wdt_reset();
                http.begin(url);
                esp_task_wdt_reset();
                int code = http.GET();
                esp_task_wdt_reset();
                if (code == 200) {
                    len = readHttpBody(http, buf, maxLen, 8000);
                    if (len > 0 && isCompleteJpeg(buf, len)) {
                        ok = true;
                        if (key.length() > 0) {
                            if (!LittleFS.exists("/cover")) LittleFS.mkdir("/cover");
                            String cachePath = "/cover/" + key + ".jpg";
                            File f = LittleFS.open(cachePath, "w");
                            if (f) {
                                f.write(buf, len);
                                f.close();
                            }
                        }
                    }
                }
                http.end();
                esp_task_wdt_reset();
            }

            if (ok) {
                g_pf[slot].len   = len;
                g_pf[slot].ready = true;
                g_pf[slot].url   = url;
                g_pf[slot].key   = key;
            }

            esp_task_wdt_reset();
        }
    }
}

/**
 * @brief Startet einen asynchronen Cover-Download.
 *
 * Kann aus jedem Task (z. B. lmsPoll auf Core 0) aufgerufen werden.
 * Der Download läuft im coverDlTask auf Core 0.
 *
 * @param url      Vollständige HTTP-URL zum Cover-JPEG.
 * @param cacheKey Optionaler Cache-Key für LittleFS.
 */
void displayPreloadCover(const String& url, const String& cacheKey) {
    if (url.isEmpty()) return;

    xSemaphoreTake(g_coverMux, portMAX_DELAY);

    // Nicht dispatchen, wenn bereits in Arbeit
    if (url == g_dispatchedCoverUrl || url == g_pendingCoverUrl) {
        xSemaphoreGive(g_coverMux);
        return;
    }

    g_pendingCoverUrl  = url;
    g_pendingCoverKey  = cacheKey;
    g_coverRetryAt     = 0;

    TaskHandle_t task = g_coverTask;
    if (task) {
        xTaskNotifyGive(task);
    }
    xSemaphoreGive(g_coverMux);
}

/**
 * @brief Befüllt die Prefetch-Queue mit Cover-URLs.
 *
 * @param urls  Array von Cover-URLs.
 * @param keys  Array von Cache-Keys.
 * @param count Anzahl der Einträge.
 */
void displayQueuePrefetch(const String* urls, const String* keys, int count) {
    if (count <= 0 || count > SQUEEZE_PREFETCH) return;

    xSemaphoreTake(g_pfMux, portMAX_DELAY);
    g_pfQueueCount = count;
    for (int i = 0; i < count; i++) {
        g_pfQueue[i]     = urls[i];
        g_pfQueueKeys[i] = keys[i];
    }
    TaskHandle_t task = g_pfTask;
    if (task) {
        xTaskNotifyGive(task);
    }
    xSemaphoreGive(g_pfMux);
}
