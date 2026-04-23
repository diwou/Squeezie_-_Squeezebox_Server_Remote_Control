# KiCad Hardware

Dieses Verzeichnis enthält die Platinenquellen und die exportierten Fertigungsdaten für Squeezie.

## Struktur

- `source/` - bearbeitbare KiCad-Quellen für Revision `rev0.1`
- `fab/rev0.1/bom/` - BOM, Designatoren, Positionen und Netlist
- `fab/rev0.1/Squeezie_Squeezseboxserver_Control_0.1.zip` - komplettes Fertigungspaket für den Bestelldienstleister

## Kurz-BOM

_Abgeleitet aus `fab/rev0.1/bom/bom.csv`. Wenn sich die Platine ändert, diese Übersicht gemeinsam mit der Stückliste prüfen._

| Bereich | Bauteile | Bauform | Menge | Zweck |
|---|---|---|---:|---|
| MCU | `U2` ESP32-S3-WROOM-1 | ESP32-S3-WROOM-1 | 1 | Hauptprozessor, WLAN, Steuerlogik |
| Anzeige / Anschluss | `J1` USB-C, `J2` ILI9488 Display | USB_C_Receptacle_HCTL_HC-TYPE-C-16P-01A, PinSocket_1x14_P2.54mm_Vertical | 2 | USB- und Display-Anschluss |
| Bedienung | `SW1` Boot, `SW2` Reset, `SW3` Prev, `SW4` Next, `SW5` Play, `SW6` Vol-, `SW7` Vol+, `SW8` Mute | SWITCH_FSMARA | 8 | Bedienelemente für den Player |
| Spannungsversorgung | `U1` TLV76133DCY, `D2` SS14, `D1` PWR | SOT-223-3_TabPin2, D_SMA, 0805 | 3 | 3,3-V-Regler, Schutzdiode, Power-LED |
| Widerstände | `R1` 1k, `R2/R3` 5,1k, `R4/R5/R6/R7/R8/R9/R10` 10k | 0805 | 10 | Pullups und Signalbegrenzung |
| Kondensatoren | `C1/C2` 10µ, `C3` 10µ, `C5` 1µ, `C4/C6/C7/C10/C11/C12/C13/C14/C15` 100n | 1206, 0805 | 12 | Abblockung und Pufferung |

## Hinweise

- `H1` bis `H4` sind Montagebohrungen und tauchen deshalb nicht in der BOM-CSV auf.
- Die vollständige, exportierte Stückliste liegt zusätzlich als CSV unter `fab/rev0.1/bom/`.
- `source/*.kicad_prl` und `source/fp-info-cache` sind lokale bzw. generierte Dateien und werden per `.gitignore` ausgeschlossen.
- Das Projekt wird im gleichen Schema gepflegt wie der HUB75-KiCad-Stand.
