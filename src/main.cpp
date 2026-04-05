#include <Arduino.h>
#include "config.h"
#include "display.h"
#include "webserver.h"

void setup() {
    pinMode(PIN_BOOT_BTN, INPUT_PULLUP);

    displayInit();                         // Display + ggf. Touch-Kalibrierung
    displayShowStartup("v0.3.2", "");      // Startup-Screen sofort anzeigen
    webserverInit();                       // WiFi verbinden (blockiert bis zu 15 s)

    String ip = wifiGetIP();
    srvLog("[Squeezie] IP: %s", ip.c_str());
    displayShowStartup("v0.3.2", ip.c_str());
    delay(1500);

    displayDrawPlayerScreen();             // Player-Screen aktivieren
}

void loop() {
    // ── BOOT-Button: 10 Sek. halten → Factory Reset ───────────────────────────
    static uint32_t bootHoldStart = 0;
    static bool     bootShowing   = false;

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
    displayUpdatePlayer(); // Geänderte Felder des Player-Screens aktualisieren
    displayHandleTouch();  // Touch nach Display: SPI-DMA ist dann abgeschlossen
    delay(40);
}
