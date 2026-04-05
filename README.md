# Squeezie

ESP32-S3-basierte Fernsteuerung für einen Squeezebox/Lyrion-Medienserver mit ILI9488 3,5" Touchdisplay.

## Hardware

| Komponente | Modell |
|---|---|
| Mikrocontroller | ESP32-S3-WROOM1-N16R8 (16 MB Flash, 8 MB PSRAM) |
| Display | TN 3,5" ILI9488 SPI-TFT 480×320 |
| Touch | XPT2046 (auf Display-Modul integriert) |

## Verkabelung

Das Display-Modul hat 14 Pins. Display-SPI und Touch-SPI teilen sich MOSI, MISO und SCK.

| Display-Pin | Bezeichnung | ESP32-S3 GPIO | Hinweis |
|---|---|---|---|
| 1 | VCC | 3,3 V | Spannungsversorgung |
| 2 | GND | GND | Masse |
| 3 | CS | GPIO 10 | LCD Chip Select |
| 4 | RST | GPIO 8 | LCD Reset |
| 5 | DC/OC | GPIO 9 | Data/Command |
| 6 | SDI | GPIO 11 | MOSI (geteilt) |
| 7 | SCK | GPIO 12 | SPI-Takt (geteilt) |
| 8 | BL | GPIO 7 | Hintergrundbeleuchtung (HIGH = an) |
| 9 | SDO | GPIO 13 | MISO (geteilt, optional für LCD) |
| 10 | TCK | GPIO 12 | Touch SPI-Takt (geteilt mit SCK) |
| 11 | TCS | GPIO 4 | Touch Chip Select |
| 12 | TDI | GPIO 11 | Touch MOSI (geteilt mit SDI) |
| 13 | TDO | GPIO 13 | Touch MISO (geteilt mit SDO) |
| 14 | PEN | GPIO 5 | Touch-Interrupt (IRQ) |

> **Hinweis:** Pins 9 (SDO) und 13 (TDO) werden beide mit GPIO 13 verbunden.
> Da LCD und Touch unterschiedliche Chip-Select-Leitungen haben, arbeiten sie nie gleichzeitig.

## Software-Einrichtung

### Voraussetzungen
- [PlatformIO](https://platformio.org/) (VSCodium/VSCode Extension oder CLI)
- [cspell](https://cspell.org/) Extension für Rechtschreibprüfung (optional)

### Erstmalige Einrichtung

```bash
# Bibliotheken installieren und kompilieren
pio run

# Auf den ESP32 flashen (USB angeschlossen, /dev/ttyACM0)
pio run --target upload

# Serielle Konsole öffnen
pio device monitor
```

### Touch-Kalibrierung

Beim allerersten Start erscheint automatisch die Kalibrierung. Es werden 5 Kreuzmarkierungen
nacheinander angezeigt, die mit dem Finger berührt werden müssen.

Die Kalibrierungsdaten werden im NVS (Flash) gespeichert und bei jedem Neustart geladen.

**Neu kalibrieren:** Button 3 auf dem Demo-Bildschirm 3 Sekunden gedrückt halten.

## Aktueller Stand

### v0.1.0 — Display-Demo
- ILI9488 Display-Initialisierung
- Touch-Kalibrierung mit NVS-Speicherung
- Demo-Bildschirm: Farbbalken, Systeminfo, Touch-Buttons mit Koordinatenanzeige

## Geplante Funktionen

- [ ] WLAN-Verbindung mit Konfigurationsportal (WiFiManager oder Custom)
- [ ] Webinterface für Grundeinstellungen (IP, NTP, Squeezebox-Server)
- [ ] Lyrion/Squeezebox Server-API (JSON-RPC)
- [ ] Playersteuerung über Touchdisplay (Play/Pause, Vor/Zurück, Lautstärke)
- [ ] Titelanzeige mit Albumcover, scrollender Text
- [ ] OTA-Updates
- [ ] IR-Fernbedienung (Hardware-Erweiterung)

## Lizenz

Privates Projekt — kein öffentliches Lizenzmodell.
