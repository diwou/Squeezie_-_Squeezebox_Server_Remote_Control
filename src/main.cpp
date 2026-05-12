#include <Arduino.h>
#include "config.h"
#include "display.h"
#include "webserver.h"
#include "esp_task_wdt.h"

struct ButtonDebounceState {
    bool     lastRawState = HIGH;
    bool     stableState = HIGH;
    uint32_t lastChangeAt = 0;
    bool     pressHandled = false;
};

static bool consumeButtonPress(uint8_t pin, ButtonDebounceState& state) {
    const uint32_t debounceMs = 35;
    bool rawState = digitalRead(pin);
    if (rawState != state.lastRawState) {
        state.lastRawState = rawState;
        state.lastChangeAt = millis();
    }
    if (millis() - state.lastChangeAt < debounceMs) return false;
    if (state.stableState == rawState) return false;

    state.stableState = rawState;
    if (state.stableState == HIGH) {
        state.pressHandled = false;
        return false;
    }
    if (state.pressHandled) return false;
    state.pressHandled = true;
    return true;
}

static void handleHardwareButtons() {
    static ButtonDebounceState playBtn;
    static ButtonDebounceState prevBtn;
    static ButtonDebounceState nextBtn;
    static ButtonDebounceState volUpBtn;
    static ButtonDebounceState volDnBtn;
    static ButtonDebounceState muteBtn;

    String mac = wifiGetPlayerMac();
    if (mac.isEmpty()) return;

    if (consumeButtonPress(PIN_PLAY_BTN, playBtn)) {
        SqueezieStatus status = squeezieGetCachedStatus();
        wifiBeginUserCommand();
        bool ok = status.playing ? squeezePause(mac) : squeezePlay(mac);
        if (ok) {
            status.playing = !status.playing;
            wifiUpdateCachedStatus(status);
            srvLog("[HW] Play/Pause-Taster ausgelöst.");
        } else {
            srvLog("[HW] Play/Pause fehlgeschlagen.");
        }
        wifiEndUserCommand(ok);
    }
    if (consumeButtonPress(PIN_PREV_BTN, prevBtn)) {
        wifiBeginUserCommand();
        bool ok = squeezePrev(mac);
        if (ok) srvLog("[HW] Track-zurueck-Taster ausgelöst.");
        else srvLog("[HW] Track-zurueck fehlgeschlagen.");
        wifiEndUserCommand(ok);
    }
    if (consumeButtonPress(PIN_NEXT_BTN, nextBtn)) {
        wifiBeginUserCommand();
        bool ok = squeezeNext(mac);
        if (ok) srvLog("[HW] Track-vor-Taster ausgelöst.");
        else srvLog("[HW] Track-vor fehlgeschlagen.");
        wifiEndUserCommand(ok);
    }
    if (consumeButtonPress(PIN_VOL_UP_BTN, volUpBtn)) {
        wifiBeginUserCommand();
        bool ok = squeezeAdjustVolume(mac, +5);
        if (ok) {
            SqueezieStatus status = squeezieGetCachedStatus();
            status.volume = constrain(status.volume + 5, 0, 100);
            status.muted = false;
            wifiUpdateCachedStatus(status);
            srvLog("[HW] Volume+ Taster ausgelöst.");
        } else {
            srvLog("[HW] Volume+ fehlgeschlagen.");
        }
        wifiEndUserCommand(ok);
    }
    if (consumeButtonPress(PIN_VOL_DN_BTN, volDnBtn)) {
        wifiBeginUserCommand();
        bool ok = squeezeAdjustVolume(mac, -5);
        if (ok) {
            SqueezieStatus status = squeezieGetCachedStatus();
            status.volume = constrain(status.volume - 5, 0, 100);
            status.muted = false;
            wifiUpdateCachedStatus(status);
            srvLog("[HW] Volume- Taster ausgelöst.");
        } else {
            srvLog("[HW] Volume- fehlgeschlagen.");
        }
        wifiEndUserCommand(ok);
    }
    if (consumeButtonPress(PIN_MUTE_BTN, muteBtn)) {
        wifiBeginUserCommand();
        bool ok = squeezeToggleMute(mac);
        if (ok) {
            SqueezieStatus status = squeezieGetCachedStatus();
            status.muted = !status.muted;
            wifiUpdateCachedStatus(status);
            srvLog("[HW] Mute-Taster ausgelöst.");
        } else {
            srvLog("[HW] Mute fehlgeschlagen.");
        }
        wifiEndUserCommand(ok);
    }
}

void setup() {
    pinMode(PIN_BOOT_BTN, INPUT_PULLUP);
    pinMode(PIN_PLAY_BTN, INPUT_PULLUP);
    pinMode(PIN_PREV_BTN, INPUT_PULLUP);
    pinMode(PIN_NEXT_BTN, INPUT_PULLUP);
    pinMode(PIN_VOL_UP_BTN, INPUT_PULLUP);
    pinMode(PIN_VOL_DN_BTN, INPUT_PULLUP);
    pinMode(PIN_MUTE_BTN, INPUT_PULLUP);

    displayInit();                         // Display + ggf. Touch-Kalibrierung
    displayShowStartup("v" APP_VERSION, ""); // Startup-Screen sofort anzeigen
    webserverInit();                       // WiFi verbinden (blockiert bis zu 15 s)

    // ── HW-Watchdog: 10 s Timeout ─────────────────────────────────────────────
    // Erst nach webserverInit() aktivieren, da connectWiFi() länger als 10 s
    // blockieren kann. In loop() wird der Watchdog dann regelmässig mit
    // esp_task_wdt_reset() gefeedet.
    esp_task_wdt_init(10, true);
    esp_task_wdt_add(nullptr);

    String ip = wifiGetIP();
    srvLog("[Squeezie] IP: %s", ip.c_str());
    displayDrawPlayerScreen();             // Player-Screen aktivieren
    wifiPollNow();                         // ersten LMS-Status + Cover sofort anstossen
}

void loop() {
    // ── HW-Watchdog feeden ─────────────────────────────────────────────────────
    esp_task_wdt_reset();

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
    esp_task_wdt_reset();  // Watchdog direkt nach OTA-Paketverarbeitung feeden
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
    handleHardwareButtons();
    delay(20);
}
