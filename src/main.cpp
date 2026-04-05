#include <Arduino.h>
#include "display.h"
#include "webserver.h"

void setup() {
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
    webserverLoop();       // WiFi-Monitor, DNS (AP), Squeeze-Polling
    displayUpdatePlayer(); // Geänderte Felder des Player-Screens aktualisieren
    displayHandleTouch();  // Touch nach Display: SPI-DMA ist dann abgeschlossen
    delay(40);
}
