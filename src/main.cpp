#include <Arduino.h>
#include "config.h"
#include "display.h"
#include "webserver.h"

static void handlePlayPauseButton() {
    static bool     lastRawState      = HIGH;
    static bool     stableState       = HIGH;
    static uint32_t lastChangeAt      = 0;
    static bool     pressHandled      = false;
    const uint32_t  debounceMs        = 35;

    bool rawState = digitalRead(PIN_PLAY_BTN);
    if (rawState != lastRawState) {
        lastRawState = rawState;
        lastChangeAt = millis();
    }
    if (millis() - lastChangeAt < debounceMs) return;
    if (stableState == rawState) return;

    stableState = rawState;
    if (stableState == HIGH) {
        pressHandled = false;
        return;
    }
    if (pressHandled) return;

    String mac = wifiGetPlayerMac();
    if (mac.isEmpty()) {
        srvLog("[HW] Play/Pause ignoriert: kein Player gesetzt.");
        pressHandled = true;
        return;
    }

    SqueezieStatus status = squeezieGetCachedStatus();
    bool ok = status.playing ? squeezePause(mac) : squeezePlay(mac);
    if (ok) {
        wifiForceRepoll();
        srvLog("[HW] Play/Pause-Taster ausgelöst.");
    } else {
        srvLog("[HW] Play/Pause fehlgeschlagen.");
    }
    pressHandled = true;
}

void setup() {
    pinMode(PIN_BOOT_BTN, INPUT_PULLUP);
    pinMode(PIN_PLAY_BTN, INPUT_PULLUP);

    displayInit();                         // Display + ggf. Touch-Kalibrierung
    displayShowStartup("v" APP_VERSION, ""); // Startup-Screen sofort anzeigen
    webserverInit();                       // WiFi verbinden (blockiert bis zu 15 s)

    String ip = wifiGetIP();
    srvLog("[Squeezie] IP: %s", ip.c_str());
    displayShowStartup("v" APP_VERSION, ip.c_str());
    delay(1500);

    displayDrawPlayerScreen();             // Player-Screen aktivieren
}

void loop() {
    // ── BOOT-Button: 10 Sek. halten → Factory Reset ───────────────────────────
    static uint32_t bootHoldStart = 0;
    static bool     bootShowing   = false;
    static int      lastOtaPct    = -1;

    if (digitalRead(PIN_BOOT_BTN) == LOW) {
        if (bootHoldStart == 0) bootHoldStart = millis();
        uint32_t held = millis() - bootHoldStart;
        if (held >= 10000) {
            displayShowFactoryReset(0);
            delay(1500);
            wifiFactoryReset();            // löscht NVS und startet neu
        }
        uint8_t secsLeft = (uint8_t)((10000 - held) / 1000 + 1);
        displayShowFactoryReset(secsLeft);
        bootShowing = true;
        return;
    }
    if (bootShowing) {
        bootShowing   = false;
        bootHoldStart = 0;
        displayDrawPlayerScreen();         // Screen wiederherstellen nach Abbruch
        return;
    }
    bootHoldStart = 0;

    webserverLoop();       // WiFi-Monitor, DNS (AP), Squeeze-Polling
    int otaPct = -1;
    bool otaDirty = wifiConsumeOtaProgress(otaPct);
    if (wifiIsOtaActive()) {
        if (otaPct < 0) otaPct = max(0, lastOtaPct);
        if (otaPct < 0) otaPct = 0;
        if (otaDirty || otaPct != lastOtaPct) {
            lastOtaPct = otaPct;
        }
        displayShowOTA(otaPct);
        if (otaPct < 100) {
            delay(40);
            return;
        }
    }
    displayUpdatePlayer(); // Geänderte Felder des Player-Screens aktualisieren
    displayHandleTouch();  // Touch nach Display: SPI-DMA ist dann abgeschlossen
    handlePlayPauseButton();
    delay(40);
}
