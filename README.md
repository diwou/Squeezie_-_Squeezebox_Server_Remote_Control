# Squeezie

ESP32-S3-basierte Fernsteuerung für einen Lyrion/Squeezebox-Medienserver mit ILI9488 3,5" Touchdisplay.

## Projekt-Metadaten

- **Repository**: https://codeberg.org/diwou/Squeezie_-_Squeezebox_Server_Remote_Control
- **Lizenz**: CC BY-NC-SA 4.0
- **Version**: Development v0.4.5+6354195

## Hardware

| Komponente | Modell |
|---|---|
| Mikrocontroller | ESP32-S3-WROOM1-N16R8 (16 MB Flash, 8 MB PSRAM) |
| Display | TN 3,5" ILI9488 SPI-TFT 480×320 |
| Touch | XPT2046 (auf Display-Modul integriert) |

## Verkabelung

Das Display-Modul hat 14 Pins. Display-SPI und Touch-SPI sind auf getrennte GPIOs gelegt.

| Display-Pin | Bezeichnung | ESP32-S3 GPIO | Hinweis |
|---|---|---|---|
| 1 | VCC | 3,3 V | Spannungsversorgung |
| 2 | GND | GND | Masse |
| 3 | CS | GPIO 17 | LCD Chip Select |
| 4 | RST | GPIO 18 | LCD Reset |
| 5 | DC/OC | GPIO 9 | Data/Command |
| 6 | SDI | GPIO 3 | Display MOSI |
| 7 | SCK | GPIO 46 | Display SPI-Takt |
| 8 | BL | GPIO 9 | Hintergrundbeleuchtung (HIGH = an) |
| 9 | SDO | GPIO 10 | Display MISO (optional) |
| 10 | TCK | GPIO 11 | Touch SPI-Takt |
| 11 | TCS | GPIO 12 | Touch Chip Select |
| 12 | TDI | GPIO 13 | Touch MOSI |
| 13 | TDO | GPIO 14 | Touch MISO |
| 14 | PEN | GPIO 21 | Touch-Interrupt (IRQ) |

## Software-Einrichtung

### Voraussetzungen

- [PlatformIO](https://platformio.org/) (VSCodium/VSCode Extension oder CLI)

### Erstmalige Einrichtung

```bash
# Bibliotheken installieren und kompilieren
pio run

# Auf den ESP32 flashen (USB angeschlossen)
pio run --target upload

# Serielle Konsole öffnen
pio device monitor
```

### WLAN-Konfiguration

Beim ersten Start öffnet das Gerät einen Access Point **Squeezie-Setup**. Im Browser
`192.168.4.1` aufrufen und WLAN-Zugangsdaten eingeben. Nach dem Verbinden mit dem
Heimnetz erscheint die IP-Adresse auf dem Display.

### Touch-Kalibrierung

Beim allerersten Start erscheint automatisch die Kalibrierung. Es werden 3 Kreuzmarkierungen
nacheinander angezeigt, die mit dem Finger berührt werden müssen.

Die Kalibrierungsdaten werden im NVS (Flash) gespeichert und bei jedem Neustart geladen.

**Neu kalibrieren:** Obere linke Ecke des Displays 3 Sekunden gedrückt halten.

## Aktueller Stand — Development v0.4.5+6354195

### Player-Screen (Touch-Display)

- **Cover-Art** (128×128 px, vom LMS-Server geladen)
- **Titelanzeige**: Titel (scrollend), Interpret, Album, Tracknummer / Gesamtdauer
- **Fortschrittsbalken** mit Zeitanzeige, antippen zum Seekten
- **Transport-Buttons**: |< / Pause/Play / >|
- **Lautstärke**: Balken mit [−] / [+], Halten wiederholt
- **Gruppenlautstärke**: Bei synchronisierten Lyrion-Playern werden Lautstärkeänderungen auf die ganze Sync-Gruppe angewendet und lassen sich in den Einstellungen ein- oder ausschalten
- **Modus-Buttons**: Wiederholen (aus / Titel / Liste) und Zufall (an/aus)
- **Uhrzeit** (NTP) in der Statuszeile
- **Einstellungs-Screen**: Server-Suche per UDP-Discovery, Player-Auswahl, Sync-Lautstärke-Schalter

### Webinterface (Browser)

- **Player-Steuerung** (`http://<ip>/`): Titel, Cover, Fortschrittsbalken, Transport, Lautstärke, Modus
- **Einstellungen** (`/settings`): WLAN, Netzwerk (DHCP/Statisch), NTP, Squeezebox-Server, Player-Auswahl
- **OTA-Firmware-Update** (`/update`)
- **Log** (`/log`): Ring-Buffer mit den letzten 20 Systemereignissen

### Technisches

- **Lyrion/Squeezebox JSON-RPC API**: Play/Pause/Next/Prev/Seek/Lautstärke/Wiederholen/Zufall
- **Server-Discovery**: UDP-Broadcast auf Port 3483, TCP-Portscan als Fallback
- **NVS-Persistenz**: alle Einstellungen überleben einen Neustart
- **Cover-Download**: asynchroner FreeRTOS-Task (blockiert die Main-Loop nicht)
- **Selektives Display-Rendering**: nur geänderte Felder werden neu gezeichnet

## Geplante Funktionen

- [ ] IR-Fernbedienung (Hardware-Erweiterung)

## Lizenz

[CC BY-NC-SA 4.0](https://creativecommons.org/licenses/by-nc-sa/4.0/) — Namensnennung,
nicht-kommerziell, Weitergabe unter gleichen Bedingungen.
