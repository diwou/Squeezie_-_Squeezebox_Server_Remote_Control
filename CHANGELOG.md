# Changelog

Alle wichtigen Änderungen an diesem Projekt werden hier dokumentiert.
Das Format basiert auf [Keep a Changelog](https://keepachangelog.com/de/1.0.0/)
und dieses Projekt verwendet [Semantische Versionierung](https://semver.org/lang/de/).

## [0.4.1] — 2026-04-09

### Hinzugefügt
- **Playlist-Auswahl auf dem Display**: im Einstellungs-Screen gibt es jetzt den Button „LISTE"
  im Header; er öffnet einen eigenen Sub-Screen mit allen Server-Playlists. Antippen einer
  Playlist lädt sie sofort und kehrt zum Player-Screen zurück.
- **Playlist-Auswahl im Webinterface**: unter Einstellungen → Server neuer Block
  „Playlist-Auswahl" mit Ladeliste und Auswahlmarkierung; Antippen speichert und lädt die
  Playlist sofort.
- Playlist-ID und -Name werden in NVS gespeichert und beim Start wiederhergestellt.
- Neue API-Endpunkte: `GET /api/playlists` (Serverliste) und `POST /api/config/playlist`.

## [0.4.0] — 2026-04-07

### Behoben
- **Cover-Cache auf LMS-Cover-ID umgestellt**: aktuelle und vorgeladene Cover verwenden jetzt
  die stabile `coverid` als Cache-Key und laden direkt über `/music/<coverid>/cover_128x128...`
  statt über den playerbezogenen `current`-Endpoint; fertige Cover werden vor dem Zeichnen
  zusätzlich gegen den erwarteten Key geprüft, damit bei schnellen Trackwechseln kein
  vorgeladenes Future-Cover dem falschen Titel zugeordnet wird. Cover-lose Tracks löschen nun
  außerdem die alte Cover-URL konsequent, statt versehentlich das vorherige Bild neu anzustoßen.
- **Cover-Wechsel zusätzlich stabilisiert**: neue Cover-IDs werden erst nach zwei identischen
  LMS-Polls übernommen, damit kurze Status-Vorgriffe nicht sofort ein Future-Cover anzeigen.
  Außerdem werden unvollständig geladene JPEGs jetzt verworfen statt als halbes Cover gezeichnet.
- **Cover-Pfad grundlegend vereinfacht**: Prefetch-Cover werden nicht mehr in den sichtbaren
  Cover-Puffer übernommen; das Display lädt nur noch das aktuelle Player-Cover über den
  LMS-`current`-Endpoint, nachdem der Track-Key stabil ist. Der Boot-Cover-Cache wird ebenfalls
  nicht mehr vorab gezeichnet, damit kein altes oder vorgeladenes Bild auf dem Player-Screen
  erscheinen kann.
- **Start und Cover-Laden beschleunigt**: der feste 1,5-Sekunden-Startbildschirm nach der
  WLAN-Verbindung entfällt; direkt nach dem Player-Screen wird ein erster LMS-Poll inklusive
  Cover-Download angestoßen. Der HTTP-Cover-Reader wartet außerdem nicht mehr auf das Schließen
  der Verbindung, sondern beendet den Download sofort nach der per `Content-Length` erwarteten
  JPEG-Größe. Da der `current`-Endpoint pro Track dieselbe URL verwendet, prüft die
  Download-Deduplizierung nun zusätzlich den Track-Key statt nur die URL.
- **WLAN-Startzeit gekürzt**: der erste Verbindungsversuch wartet nur noch bis zu 8 Sekunden,
  der zweite Retry nur noch 4 Sekunden statt erneut 15 Sekunden; außerdem wird die WLAN-
  Sendeleistung wieder auf den normalen Maximalwert gesetzt, damit LMS-HTTP-Abrufe nicht
  unnötig langsam laufen.

## [0.3.3] — 2026-04-06

### Behoben
- **Lautstärke-Skala vereinheitlicht**: UI, Touch und Web arbeiten jetzt durchgängig mit
  `0..100`, während der LMS intern weiter `6..100` erhält; die Umrechnung ist zentralisiert,
  sodass z. B. UI `18` sauber auf LMS `23` abgebildet wird.
- **Lautstärke-Anzeige aus LMS-Status korrigiert**: vom LMS gelesene Lautstärkewerte werden
  nicht mehr fälschlich erneut umgerechnet; dadurch stimmen Anzeige auf ESP und Apps jetzt
  wieder überein.
- **`/api/control` meldet Fehler korrekt**: Web-Befehle liefern jetzt bei fehlgeschlagenen
  LMS-Kommandos ein `ok:false` statt stillschweigend Erfolg zu melden.
- **Cover-Auslieferung threadsicherer**: `/api/cover` liefert jetzt ein Snapshot des JPEGs aus,
  statt direkt auf einen gleichzeitig beschriebenen globalen Puffer zuzugreifen.
- **Log-Ringpuffer robuster**: gleichzeitige Zugriffe aus mehreren Tasks/Cores werden jetzt
  synchronisiert, damit Logeinträge nicht ineinanderlaufen.
- **WLAN-Start nach Power-On/Flash robuster**: der WiFi-Stack wird vor STA/AP-Start sauber
  zurückgesetzt, mit kurzen Delays zwischen Mode-Wechseln; zusätzlich gibt es einen zweiten
  STA-Startversuch und einen Retry für `softAP()`, bevor aufgegeben wird.
- **Player-Buttons optisch überarbeitet**: Display-Transport- und Lautstärke-Buttons sind jetzt
  heller, kontrastreicher und verwenden vertrautere Player-Glyphen statt schwer lesbarer Text-
  Labels.
- **Webinterface: Lautstärke-Icons verbessert**: die ungewöhnlichen bisherigen Pfeil-/Balken-
  Symbole wurden durch klassische Lautsprecher-Symbole ersetzt; außerdem wurden die Buttons
  insgesamt heller und plastischer gestaltet.
- **Display: Mode-Buttons überarbeitet**: Wiederholen und Zufall erscheinen jetzt als klarere
  Taster mit besserem Kontrast, Rand, Schatten und kleinen Symbolen, passend zu den übrigen
  Bedienelementen.
- **Display: Text-Redraw im Info-Bereich korrigiert**: Textzeilen werden vor dem Neuzeichnen
  jetzt vollständig innerhalb ihres Clip-Bereichs gelöscht, damit keine schwarzen Balken oder
  Reste in benachbarte Bereiche hineinwirken.
- **Display: Lautstärke- und Mode-Icons vereinfacht und repariert**: die problematischen
  Symbolzeichnungen wurden auf robustere Primitive umgestellt; außerdem liegen die Lautstärke-
  Buttons jetzt vollständig in ihrer Zeile und werden nicht mehr von den Mode-Buttons überlagert.
- **Display: Lautstärke-Icons weiter vereinfacht**: die Symbole wurden auf einen klareren
  Lautsprecherkörper mit gut lesbarem `-` bzw. `+` reduziert, damit sie auf dem kleinen Display
  eindeutiger erkennbar sind.
- **Display-Steuerung und Status-Sync verbessert**: lokale Touch-Kommandos lösen jetzt sofort
  einen Re-Poll aus, statt bis zum nächsten regulären LMS-Abgleich zu warten; außerdem ist das
  LMS-Polling reaktionsschneller und loggt Fehlschläge.
- **Display: Cover-Reste beim Titelwechsel behoben**: vor dem Zeichnen eines neuen Covers wird
  die komplette Cover-Fläche gelöscht, damit bei anders skalierten oder beschnittenen Bildern
  keine Pixel des vorherigen Covers stehen bleiben.
- **Cover-Wechsel robust gegen Mischbilder gemacht**: neue Cover werden jetzt erst in einen
  separaten Staging-Puffer geladen und erst nach vollständigem JPEG atomar veröffentlicht;
  dadurch greifen Display und `/api/cover` nie mehr auf halb überschriebene Bilddaten zu.
- **Cover-Umschaltung auf Placeholder umgestellt**: bei neuem Track wird das alte Cover auf
  Display und im Web sofort ausgeblendet, bis das neue Bild vollständig vorliegt; so bleiben
  keine fremden Cover oder Bildreste sichtbar.
- **Aktuelles Cover an LMS-`current` gebunden**: das Bild des laufenden Tracks wird nicht mehr
  aus `playlist_loop[0].coverid` gebaut, sondern immer über den playerbezogenen
  `/music/current/...`-Endpoint geladen; dadurch können vorgezogene Future-Cover bei
  asynchronen Playlist-Metadaten nicht mehr als aktuelles Cover erscheinen.
- **OTA-Screen wieder sichtbar gemacht**: der Neustart nach erfolgreichem Web-Update wird jetzt
  kurz verzögert und die Fortschrittsanzeige im Main-Loop über ein Dirty-Flag zuverlässig
  gezeichnet; dadurch verschwindet der OTA-Screen nicht mehr durch einen zu frühen Reboot.
- **OTA-Screen beim Web-Upload sofort sichtbar**: zusätzlich zum Main-Loop wird der OTA-Screen
  jetzt direkt beim Start des Uploads einmal gezeichnet; dadurch bleibt die Anzeige auch dann
  nicht mehr auf dem Player-Screen stehen, wenn der Loop-Refresh zu spät kommt.
- **OTA-Anzeige in den Haupt-Loop verlagert**: Display-Zugriffe laufen beim Web-Upload jetzt
  nicht mehr aus Async-Web-Callbacks heraus, sondern nur noch kontrolliert aus `loop()`; damit
  bleibt das Display waehrend OTA nicht mehr einfach auf dem letzten Player-Bild stehen.
- **Display-Umlaute lesbarer gemacht**: da der verwendete Display-Font kein echtes UTF-8 kann,
  werden deutsche Umlaute jetzt als `ae/oe/ue` bzw. `Ae/Oe/Ue` transliteriert statt nur den
  Grundbuchstaben zu zeigen.
- **Songtexte auf dem Display auf UTF-8-Font umgestellt**: Titel, Artist und Album nutzen jetzt
  einen Unicode-faehigen LovyanGFX-Font statt der ASCII-Transliteration; dadurch erscheinen
  Umlaute in Lieddaten wieder als echte Zeichen.
- **UTF-8-Displayfont wieder vergroessert**: Titel, Artist und Album verwenden jetzt eine
  deutlich groessere Unicode-Gothic-Schrift, damit die Darstellung wieder naeher an der
  bisherigen Lesegroesse liegt.
- **Player-Text nochmals vergroessert**: Titel, Artist und Album wurden jeweils noch eine
  Font-Stufe hoeher gezogen, damit die Unicode-Darstellung auf dem Display wieder besser lesbar
  ist.
- **Player-Info entkoppelt und beruhigt**: die Track-Zeile sitzt jetzt tiefer und wird nicht mehr
  von den scrollenden Unicode-Zeilen halb ueberschrieben; ausserdem werden die Scroll-Zeilen
  ueber ein Offscreen-Sprite gezeichnet, was das sichtbare Flackern deutlich reduziert.
- **Titelanzeige kraeftiger gemacht**: der Titel wird mit derselben Unicode-Schrift leicht
  ueberzeichnet, damit er wie eine fette Variante wirkt, ohne die UTF-8-Darstellung zu verlieren.
- **Cover-Preload mit Unicode-/Current-Cover wiederhergestellt**: das aktuelle Cover kann jetzt
  weiter ueber den LMS-`current`-Endpoint geladen werden, nutzt fuer Prefetch-Treffer aber
  wieder einen separaten `coverid`-basierten Cache-Key; dadurch erscheinen Cover auf dem
  Display wieder ohne die bisherige ~4-Sekunden-Wartezeit.
- **OTA-Screen waehrend Upload hart priorisiert**: solange ein OTA aktiv ist, zeichnet der
  Haupt-Loop den OTA-Screen jetzt in jeder Runde bevorzugt weiter, statt nur auf neue
  Prozentwerte zu reagieren; damit soll die Anzeige nicht mehr einfach auf dem alten
  Player-Bild stehen bleiben.
- **WLAN nach Flash/Boot selbstheilender gemacht**: wenn zwar `WL_CONNECTED` gemeldet wird, aber
  direkt danach ueber mehrere Polls kein LMS-Verkehr moeglich ist, wird die WLAN-Verbindung
  automatisch getrennt und sofort neu aufgebaut statt im scheinbar verbundenen, aber toten
  Zustand zu verbleiben.
- **OTA-Upload robuster gemacht**: Web-Updates prüfen jetzt `Update.begin()` und `Update.write()`
  sauber auf Fehler, zeigen im Fehlerfall einen konkreten Fehlercode an und zeichnen den
  Fortschritt nicht mehr direkt im Async-Upload-Callback; auch `ArduinoOTA` nutzt jetzt denselben
  entkoppelten Fortschrittsweg.
- **OTA-Screen direkt aus dem Upload gespiegelt**: zusätzlich zum Main-Loop wird der Web-OTA-
  Fortschritt jetzt wieder direkt auf das Display gespiegelt, diesmal sauber gedrosselt und mit
  eigenem Display-Bus-Zugriff; dadurch soll der OTA-Screen auch dann sichtbar bleiben, wenn der
  normale `loop()` waehrend des Flashens zu lange blockiert.
- **Web-OTA auf bekannten Anzeige-Pfad zurückgezogen**: nachdem der entkoppelte OTA-
  Fortschrittsweg die Anzeige trotzdem nicht mehr sichtbar machte, nutzt der Web-Upload jetzt
  wieder den zuletzt funktionierenden direkten `displayShowOTA()`-Pfad pro Prozentpunkt; die
  Fehlerpruefung von `Update.begin()`/`Update.write()` und der verzoegerte Neustart bleiben
  dabei erhalten.
- **Externer Play/Pause-Taster vorbereitet**: ein zusätzlicher Hardware-Button an GPIO `1`
  kann jetzt per `INPUT_PULLUP` für `Play/Pause` genutzt werden; der Taster wird entprellt,
  nur auf Tastendruck ausgelöst und stößt danach sofort einen LMS-Re-Poll an.
- **Cover-Prefetch an Track-Metadaten gebunden**: Prefetch-Treffer werden nicht mehr über
  potentiell vorlaufende `coverid`s dem aktuellen Titel zugeordnet, sondern über einen Key aus
  Titel, Artist, Album, Tracknummer und Dauer; dadurch sollen Future-Cover nicht mehr mehrere
  Tracks zu früh auf Display und Web erscheinen.
- **Aktuelles Cover nicht mehr aus dem Poll-Task veröffentlicht**: der LMS-Poller füllt nur noch
  Status und Prefetch-Queue der Folgetracks; das Umschalten auf das aktuelle Cover passiert
  ausschließlich im Player-Update-Pfad, damit ein verfrühter Prefetch-Treffer nicht sofort als
  laufendes Cover sichtbar wird.

## [0.3.2] — 2026-04-05

### Behoben
- **Lautstärke-Anzeige: Level 16% wurde übersprungen**: Ganzzahl-Division in
  `(volume - 6) * 100 / 94` rundet immer ab, wodurch Lautstärke 16% nie erreichbar war
  (Sequenz: …14, 17…). Ersetzt durch `roundf()` — alle Level 0–100% werden jetzt erreicht.
- **Webinterface: Cover-Aktualisierung per Ajax**: Cover wurde nach dem ersten Laden nicht mehr
  aktualisiert, weil die API immer `/api/cover` (unveränderter URL) zurückgab. Jetzt enthält die
  URL einen Cover-Versions-Counter (`/api/cover?v=N`), der bei jedem Track-Wechsel hochzählt —
  der Browser erkennt die neue URL und lädt das Bild zuverlässig neu.
- **OTA-Upload: langsam und Abbruch**: `displayShowOTA()` wurde bei jedem TCP-Chunk (~1000×
  bei 1,5 MB Firmware) aufgerufen und blockierte den async-TCP-Callback mit SPI-Display-
  Operationen so lange, dass der Browser einen Timeout produzierte. Display-Update jetzt
  auf maximal einmal pro Prozentpunkt gedrosselt.

## [0.3.2-prev] — 2026-04-05

### Hinzugefügt
- **Im Web**: BOOT-Button (GPIO 0) 10 Sekunden gehalten → roter
  Countdown-Screen, anschließend alle NVS-Einstellungen löschen und Neustart; loslassen
  bricht ab und stellt den Player-Screen wieder her
- **Einstellungs-Screen**: EINST.-Button öffnet jetzt einen dedizierten Screen statt des
  IP-Popups — Server-Auswahl per UDP-Discovery (SUCHEN-Button), gefundenen Server übernehmen,
  Player-Liste vom Server laden und aktiven Player auswählen; Änderungen werden sofort in NVS
  gespeichert und der LMS-Poll neu gestartet

## [0.3.1] — 2026-04-04

### Behoben
- **Display-Flackern Lautstärkebalken**: Buttons werden jetzt nur einmalig beim Screen-Aufbau
  gezeichnet (`drawVolumeButtons`); `drawVolumeRow` zeichnet nur noch die geänderten Pixel
  selektiv (wie Fortschrittsbalken) — kein komplettes Neuzeichnen des Balkens mehr
- **Display-Flackern Status-Dot**: `displayUpdatePlayer` zeichnete den Status-Dot bei
  `DISPLAY_WIDTH-62, r=5`, `drawPlayerHeader` bei `DISPLAY_WIDTH-70, r=6` → stale Artefakt;
  jetzt überall `DISPLAY_WIDTH-70, r=6`
- **Webinterface: Blauer Touch-Highlight**: `-webkit-tap-highlight-color:transparent` im
  `*`-Selektor gesetzt — Browser-nativer blauer Tap-Effekt auf mobilen Geräten entfernt
- **Webinterface: Blauer Fokusring am Lautstärke-Slider**: `outline:none` auf `#vol-slider` gesetzt
- **Webinterface: Cover-Anzeige**: Cover wird jetzt auch nach einem Titelwechsel ohne Cover
  korrekt auf Placeholder zurückgesetzt (`coverUrl !== prevCover` ohne `&&`-Kurzschluss)
- **Cover-URL-Konstruktion**: `artwork_url` ohne führendes `/` (relative Pfade) werden jetzt
  korrekt mit `/`-Präfix zu einer absoluten URL ergänzt; `coverid = "0"` wird als leer behandelt
- **Blaue Farben auf Display**: Alle Farbkonstanten von RGB565-Hex-Werten auf RGB888 umgestellt
  (LovyanGFX interpretiert `uint32_t`-Parameter als RGB888 — z. B. `0x07E0` = Grün in RGB565,
  aber R=0 G=7 B=224 = Blau in RGB888)
- **Modus-Buttons nicht bedienbar**: Initialwert `g_dRepeat/g_dShuffle = -1` führte zu
  `(-1+1)%3 = 0` → kein sichtbarer Effekt bei erstem Druck; jetzt `max(0, g_dRepeat)+1)%3`
- **Modus-Buttons zeigen sofort Reaktion**: Optimistisches Neuzeichnen nach Touch-Event,
  statt auf den nächsten LMS-Poll (~1 s) zu warten
- **Modus-Labels korrigiert**: Wiederholung-Modus 2 hieß fälschlich „Album" → „Liste"
  (korrekt: „Wdh: aus" / „Wdh: Titel" / „Wdh: Liste")
- **Lautstärkeanzeige**: LMS-Wert [6-100] wird auf reale Prozent [0-100] umgerechnet
  (`(lms-6)*100/94`); LMS-Minimum 6 = 0 %, LMS 53 = 50 % real; Webinterface zeigt dasselbe
  Mapping (Slider-Position bleibt auf LMS-Skala für korrekte Steuerung)
- **Cover-URL**: `coverid`-Fallback korrigiert zu `cover.jpg` (vorher `cover_200x200_f.jpg`,
  vom LMS-Server nicht unterstützt); behebt fehlendes Cover auf TFT und im Webinterface
- **Cover-Download Race Condition**: Bei Titelwechsel während laufendem Download wurde kein
  neuer Task gestartet; `g_dispatchedCoverUrl` verhindert Dopple-Dispatch und ermöglicht
  Neu-Download sobald der alte Task abgeschlossen ist
- **Generelles Flackern (statischer Text)**: nicht-scrollende Zeilen nur bei `infoChanged`
  neu zeichnen; OTA-Screen: `g_screen = SCREEN_STARTUP` verhindert Überschreiben durch
  `displayUpdatePlayer()`; Startup-Doppel-Clear behoben (nur IP-Zeile beim zweiten Aufruf)
- **Scroll**: fraktionaler Pixel-Akkumulator ersetzt Integer-Division (funktioniert für alle
  `scrollMs`-Werte); Ghost-Text an Kanten durch `fillRect` nach `drawString` behoben;
  Textbreiten gecacht — nur bei Titelwechsel neu gemessen
- **PSRAM**: `board_build.arduino.memory_type = qio_opi` aktiviert `ps_malloc()` →
  Cover-Puffer wird korrekt alloziert
- **Touch-Bereich Modus-Buttons**: Raw-Filter-Grenze von 3950 auf 4090 angehoben,
  Modus-Zeile von y=302 auf y=290 verschoben — Touch erreichbar im kalibrierten Bereich
- `COL_DARKGREEN` in `COL_ACTIVE` umbenannt (RGB888 `0x005500`) und in `drawModeRow` verwendet
- **Alle Farbkonstanten: `(uint32_t)`-Cast** — LovyanGFX behandelt `int`-Literale in
  Template-Überladungen anders als `uint32_t`; alle `COL_*`-Makros ohne Cast erschienen
  blau/falsch (Transport-Kreise, Progress-Hintergrund usw.) → überall `((uint32_t)0x...)` ergänzt
- **Scroll-Flackern behoben**: `fillRect` vor `drawString` im scrollenden Zweig von
  `drawScrollLine` verursachte einen kurzen schwarzen Frame pro Scroll-Schritt —
  `setTextColor(fg, bg)` füllt Hintergrund zeichenweise, das separate `fillRect` entfernt
- **Status-Dot**: verschoben von `DISPLAY_WIDTH-70, r=6` auf `DISPLAY_WIDTH-85, r=5` —
  überlappt EINST.-Button nicht mehr
- **Track-Info: Sonderzeichen** — `\xb7` (Mittelpunkt) durch `|` ersetzt, da nicht im
  LovyanGFX-Standardfont enthalten
- **Uhrzeit im Header**: Systemzeit (NTP) wird zentriert in der Statuszeile angezeigt
  (Format `HH:MM`), aktualisiert sich minütlich
- **Serial-Ausgaben entfernt**: alle `Serial.printf/println` in display.cpp, webserver.cpp,
  main.cpp, squeezeapi.cpp entfernt; neuer Ring-Buffer-Log (`srvLog()`) mit 20 Einträgen
  à 128 Zeichen, abrufbar unter `http://<ip>/log`
- **Gesamtflackern (SPI-Bus-Konflikt)**: `touchSPI(FSPI)` und LovyanGFX verwenden denselben
  SPI2-Hardware-Peripheral; `beginTransaction()` während laufendem DMA korrumpierte den Transfer.
  Fix: `tft.startWrite()` / `tft.endWrite()` klammern `displayUpdatePlayer()` komplett ein
  (DMA-Flush vor Bus-Freigabe); außerdem `displayHandleTouch()` nach `displayUpdatePlayer()`
  in der Loop-Reihenfolge verschoben
- **Cover-Puffer**: von 64 KB auf 128 KB erhöht (`ps_malloc(131072)`); Cover-URLs erhalten
  `?size=128`-Parameter — LMS liefert auf 128×128 Pixel vorskalierterte JPEGs (~10–20 KB),
  statt Full-Size-Bilder (oft >64 KB), die den Puffer überliefen und `drawJpg()` mit
  abgeschnittenem JPEG-Daten aufgerufen hätten

## [0.3.0] — 2026-04-03

### Hinzugefügt
- **Startup-Screen**: Nach dem Boot zeigt das Display Logo „Squeezie", Version und
  IP-Adresse (bzw. „Verbinde mit WLAN..." während der Verbindungsaufbau läuft)
- **Player-Screen** (`displayDrawPlayerScreen`, `displayUpdatePlayer`):
  - Cover-Art: Download via HTTP aus LMS, 170×170 px, Platzhalter-Icon wenn kein Cover
  - Titel (groß), Interpret, Album, Track-Nummer / Gesamtdauer
  - Fortschrittsbalken mit Zeitanzeige (antippen zum Seekten)
  - Transport-Buttons |< / Pause/Play / >| als Kreisbuttons; Play-Button grün
  - Lautstärke-Zeile: [−] Balken % [+], Halten wiederholt die Anpassung (±5)
  - Modus-Zeile: Wiederholen und Zufall als Toggle-Buttons mit Statustext
  - Selektives Neuzeichnen — nur geänderte Felder werden aktualisiert (kein Flackern)
  - Kalibrierung: obere linke Ecke 3 Sekunden halten → startet Neu-Kalibrierung
- `wifiGetPlayerMac()` in `webserver.h/.cpp` — Player-MAC für Display-Touch-Aktionen
- `include/display.h`: neue Funktionen `displayShowStartup`, `displayDrawPlayerScreen`,
  `displayUpdatePlayer` ersetzen `displayDrawDemoScreen`
- `src/main.cpp`: Startup-Screen → WiFi-Verbindung → IP anzeigen → Player-Screen

### Geändert
- `src/display.cpp`: `displayHandleTouch()` dispatcht jetzt je nach aktivem Screen;
  Demo-Screen-Logik entfernt, Player-Screen-Touch-Logik implementiert
- **Lautstärke-Bug behoben**: `squeezeGetStatus()` aus dem Async-Control-Handler entfernt
  (lief im ESPAsyncWebServer-Callback-Task, lieferte Race-Condition-Ergebnis) →
  stattdessen `g_lastPoll=0` erzwingt sicheren Re-Poll im Arduino-Loop (~40 ms später);
  Volume-Befehle aktualisieren `g_status.volume` jetzt sofort optimistisch
- LMS-Poll-Intervall von 3000 ms auf 1000 ms reduziert (konsistent mit 1s-JS-Polling)
- **Cover-Download asynchron** (FreeRTOS-Task auf Core 0): blockiert Main-Loop nicht mehr →
  Web-Interface reagiert auch während Cover lädt; Download-Timeout 4 s
- **Display-Artefakte behoben**: `drawInfoLine` nutzt jetzt `TL_DATUM` + `setClipRect` für
  sauberes Überschreiben; bei Titelwechsel wird der gesamte Info-Bereich geleert
- **Umlaute auf Display**: FreeSansBold12pt7b / FreeSans12pt7b / FreeSans9pt7b (LovyanGFX
  FreeFonts) unterstützen Latin Extended → ä ö ü ß werden korrekt dargestellt
- **OTA-Update**: `ArduinoOTA.begin()` + `ArduinoOTA.handle()` ergänzt → `espota`-Protokoll
  funktioniert wieder (Hostname: `squeezie`)
- **Volume-Debug**: Serial-Ausgabe `[LMS] vol=X` zur Diagnose des Lautstärke-Mismatches

## [0.2.0] — 2026-04-02

### Hinzugefügt
- **WLAN-Konfiguration via Access Point**: Beim ersten Start (oder fehlenden Zugangsdaten) startet
  das Gerät als AP „Squeezie-Setup" mit Captive Portal für WLAN-Einrichtung
- **Webinterface** (ESPAsyncWebServer, Port 80):
  - Hauptseite `/`: Player-Steuerung (Titel, Interpret, Album, Cover, Fortschrittsbalken,
    Transport-Buttons, Lautstärke, Wiederholen, Zufall), dynamische Aktualisierung per AJAX
  - Einstellungsseite `/settings`: WLAN, DHCP/Statische IP, DNS, NTP, Squeezebox-Server,
    Player-Auswahl, Scrollgeschwindigkeit
  - OTA-Firmware-Update (`/update`) per Datei-Upload
- **Lyrion/Squeezebox JSON-RPC API** (`squeezeapi.cpp`): Player-Liste, Status-Abfrage,
  Play/Pause/Next/Prev/Seek/Lautstärke/Wiederholen/Zufall
- **Server-Discovery** via UDP-Broadcast auf Port 3483
- **NVS-Persistenz** für alle Einstellungen: WLAN, Netzwerk (DHCP/Statisch), NTP,
  Server-Adresse, Player-MAC, Scrollgeschwindigkeit
- `include/squeezeapi.h`, `src/squeezeapi.cpp`: Squeezebox-API-Modul
- `include/webserver.h`, `src/webserver.cpp`: WiFi-Management + Webserver-Modul

### Geändert
- `src/main.cpp`: Orchestrierung erweitert um `webserverInit()` und `webserverLoop()`
- `include/config.h`: NVS-Schlüssel für WiFi, Netzwerk, Server und AP-Konstanten ergänzt
- **Cover-Art**: Status-Anfrage nutzt jetzt Tag `K` (artwork_url); URL wird direkt aus dem
  Server-Response gebaut (absolut oder relativ), Fallback auf Tag `c` (coverid)
- **Lautstärke**: Relative Steuerbefehle (`+1`/`-1`) statt absolut aus gecachtem Wert —
  Server-/Anzeigewert stimmen jetzt überein; neuer Befehl `vol:N` für absoluten Wert
- **Volume-Slider**: Range-Input (0–100) mit Debouncing ersetzt die Nur-Buttons; ±1-Buttons
  bleiben zusätzlich erhalten
- **UI-Redesign Player-Seite**: Schriften deutlich größer; Transport, Lautstärke und
  Modus-Buttons in drei getrennten Zeilen statt alles nebeneinander; Modus-Buttons zeigen
  aktuellen Status als Text (z.B. „Wiederholen: Playlist")
- **Tab-Leiste**: Inaktive Tabs jetzt `#888` statt `#4a4a4a` — deutlich besser erkennbar
- **Bug-Fix `/api/scan`**: Scan-Logik war im Body-Handler (wird bei POST ohne Body nicht
  aufgerufen); jetzt korrekt im Request-Handler
- **Server-Discovery**: TCP-Portscan als Fallback wenn UDP-Broadcast keine Antwort liefert

## [0.1.0] — 2026-04-02

### Hinzugefügt
- Grundlegende Projektstruktur angelegt (PlatformIO, ESP32-S3-WROOM1 N16R8)
- `CLAUDE.md` mit Projektregeln und Coding-Konventionen
- `include/config.h` mit Pin-Definitionen und Display-Konstanten
- `include/display.h` und `src/display.cpp` als Display-Modul
- `include/LGFX_setup.h`: LovyanGFX-Konfiguration für ILI9488 + XPT2046 (SPI2, shared bus)
- Bibliothek **LovyanGFX** (lovyan03) für ILI9488 SPI-Display integriert
- Demo-Bildschirm (`displayDrawDemoScreen`): Farbbalken, Systeminfo, Touch-Buttons
- Touch-Kalibrierung über NVS gespeichert (uint16_t[8], LovyanGFX-Format)
- Neu-Kalibrierung durch 3-Sekunden-Halten von Button 3 (kein blockierender Boot-Aufruf)
- Button 1 + Button 2 gleichzeitig → sofortiger Neustart (ESP.restart)
- `README.md` mit Verkabelungsplan und Einrichtungsanleitung

### Geändert
- **TFT_eSPI → LovyanGFX** gewechselt: TFT_eSPI v2.5.43 ist inkompatibel mit Arduino-ESP32 v3.x
  (Absturz in `begin_tft_write()` durch geänderte SPI-Treiberstruktur)
- Display-Konfiguration aus Build-Flags entfernt, jetzt ausschließlich via `LGFX_setup.h`-Klasse
- `lib_ignore = WebServer` ergänzt (LDF baut WebServer-Legacy sonst fälschlicherweise ein)
- Build-Flags `-DARDUINO_USB_CDC_ON_BOOT=1` und `-DARDUINO_USB_MODE=1` ergänzt,
  damit `Serial` auf ESP32-S3 über USB-CDC (nicht UART0) ausgegeben wird
- Touch-Kalibrierung aus Boot entfernt; blockierte zuvor dauerhaft wegen fehlendem IRQ-Signal
- XPT2046 auf Polling-Modus umgestellt (`pin_int = -1`), da PEN/IRQ-Pin nicht verbunden
- Touch-Rohwerte (x_min/x_max) auf korrekte ADC-Bereiche 300–3800 gesetzt
- `getTouch`-API korrigiert: `int32_t tx, ty` statt `lgfx::touch_point_t*` (Multi-Touch-API)
- **LovyanGFX auf Version 1.1.12 festgeschrieben** (`lovyan03/LovyanGFX@1.1.12`):
  v1.2.x enthält Regression, die ILI9488-Display-Init auf ESP32-S3 bricht
- **XPT2046-Touch komplett auf direkte `SPIClass`-Lesung umgestellt** (LovyanGFX-Touch-Treiber
  entfernt): `Touch_XPT2046::init()` in LovyanGFX 1.1.12 ruft `SPI.end()+SPI.begin()` auf
  und korrumpiert den SPI-Peripheral-Zustand → Touch funktionierte nie über LovyanGFX
- `LGFX_setup.h`: `cfg.pin_miso = -1` und `cfg.readable = false` gesetzt — ILI9488 tristatet
  SDO nicht bei CS=HIGH → Buskonflikt mit XPT2046 auf MISO behoben, GPIO 13 für Touch freigegeben
- `LGFX_setup.h`: Touch-Abschnitt (`Touch_XPT2046`) und `setTouch()`-Aufruf entfernt
- `display.cpp`: NVS-Schlüssel für Kalibrierung auf dedizierte Keys umgestellt
  (`calX0`, `calX1`, `calY0`, `calY1`, `calSwap`, `calOK`) statt LovyanGFX-uint16_t[8]-Format
- 3-Punkt-Kalibrierung implementiert: Zielpunkte oben-links, oben-rechts, unten-links →
  automatische Erkennung des Achsentauschs (cal_swap), Mittelwertbildung über 600 ms,
  Speicherung in NVS via `Preferences`

### Behoben
- Guru-Meditation-Absturz durch `-mfix-esp32-psram-cache-issue` (nur ESP32, nicht ESP32-S3)
- Guru-Meditation-Absturz durch `-DBOARD_HAS_PSRAM` (initialisiert PSRAM nicht korrekt via TFT_eSPI)
- `WebServer`-Kompilierfehler (`WiFiServer.h` nicht gefunden) durch `lib_ignore = WebServer`
- Touch reagierte nie: ILI9488-MISO-Buskonflikt + LovyanGFX-SPI-Reinitialisierungsfehler behoben
