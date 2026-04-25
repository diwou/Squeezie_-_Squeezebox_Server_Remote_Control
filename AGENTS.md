# AGENTS.md

Diese Datei beschreibt verbindliche Regeln für Arbeit an diesem Repository. Sie gilt für KI-Assistenten und
Menschen gleichermaßen als Arbeitsgrundlage.

## 0. Verbindliche Kurzprüfung

Das ist bindend. Diese Datei ist für diese Arbeitsumgebung eine harte Vorrangregel.
Keine Interpretation, keine Abkürzung, keine Ausnahme durch Gewohnheit.
Wenn ein Punkt unklar wirkt, gilt nicht "frei auslegen", sondern die Datei erneut lesen.
Wenn eine spontane Annahme, eine Gewohnheit oder eine frühere Arbeitspraxis kollidiert,
hat immer diese Datei Vorrang.

Vor jeder Änderung, vor jeder Prüfung und vor jeder Antwort über den Arbeitsstand gilt diese Reihenfolge:

1. `CHANGELOG.md` zuerst prüfen und bei inhaltlichen Änderungen sofort mitpflegen.
2. Bei echten Codeänderungen danach die nötigen Prüfungen ausführen, nicht weniger und nicht mehr.
3. Bei reinen Doku-, Regel- oder Metadatenänderungen kein blindes `pio run`.
4. `npm run spell` für betroffene Text- und Markdown-Dateien ausführen.
5. `docs/RELEASING.md` bei Release-Arbeiten immer zusätzlich beachten.
6. Changelog-Einträge nur mit eindeutiger ID/Commit-Referenz als vollständig behandeln.
7. Codeberg-Stand nach erfolgreichen Prüfungen aktuell halten; ein Arbeitsstand gilt erst dann als abgeschlossen, wenn der gepushte Codeberg-Stand dem lokalen Stand entspricht.
8. Änderungen erst als erledigt melden, wenn Changelog, Prüfungen, Repo-Zustand und Codeberg-Stand zusammenpassen.
9. Wenn eine Regel, eine Gewohnheit und eine spontane Einschätzung kollidieren, gewinnt die Regel.
10. Wenn du diese Reihenfolge nicht sicher einhalten kannst, musst du erst die Dateien erneut lesen statt zu raten.

## 0.1 Kommunikationsregeln

- Eine klare Arbeitsanweisung wird direkt ausgeführt, nicht erst rückgefragt.
- Am Ende einer erledigten Aufgabe steht eine knappe Umsetzungszusammenfassung, keine Rückfrage und kein Angebot für den nächsten Schritt.
- Formulierungen wie „wenn du willst“, „ich könnte noch“, „soll ich“ oder ähnliche Zusatzoptionen sind nach einer klaren Anweisung zu vermeiden.
- Wenn wirklich eine Entscheidung des Nutzers nötig ist, muss das vor der Ausführung begründet und als echte Blockade benannt werden.
- Standard ist: umsetzen, prüfen, berichten.

## Sprache

- Antworten, Commit-Messages und Changelog-Einträge auf Deutsch.
- Technische Bezeichner wie Funktionsnamen, Dateinamen und Git-Refs bleiben auf Englisch.

## 1. Versionsschema

Dieses Projekt befindet sich in einer `0.x.y`-Phase und folgt inhaltlich SemVer:

- `MAJOR` nur bei Breaking Changes.
- `MINOR` bei neuen, rückwärtskompatiblen Funktionen oder Konfigurationsoptionen.
- `PATCH` bei reinen Bugfixes ohne neue Funktionen.
- Solange das Projekt nicht als stabil markiert ist, dürfen Breaking Changes in dieser Phase auch einen Minor-Sprung auslösen.

### Projektregel für Datum und Build

- Development-Stände behalten die zuletzt freigegebene Versionsbasis und tragen in sichtbaren Metadaten immer den aktuellen Codeberg-Kurz-Hash als Suffix, z. B. `0.4.5+5e0e372`.
- Die sichtbare Basis-Version wird erst bei einem neuen Release angehoben, nicht pro Kalendertag.
- Production-Releases tragen nur die reine Version ohne Hash-Suffix.
- Die Versions-Herkunft liegt in `include/config.h` über `APP_VERSION_BASE`; `include/version_generated.h` wird beim Build erzeugt und darf nicht manuell gepflegt werden.

### Sichtbare Versionen

- `README.md`, Startbildschirm, Firmware-Export und sonstige sichtbare Metadaten müssen zum gleichen Stand passen.
- Wenn sich der Development-Hash durch einen neuen Commit ändert, müssen sichtbare Versionsangaben nachgezogen werden.

## 2. Branching

- `main` ist der relevante Hauptzweig.
- Größere Umbauten können in `feature/<kurzname>` oder `fix/<kurzname>` erfolgen.
- Kleine Solo-Änderungen dürfen direkt auf `main` landen, solange der Arbeitsfluss sauber bleibt.
- Kein `develop`-Branch und kein Git-Flow als Standard.

## 3. Commits

- Commit-Messages sollen nach Conventional Commits aufgebaut sein.
- Erlaubte Typen sind insbesondere `feat`, `fix`, `docs`, `style`, `refactor`, `perf`, `test`, `build`, `ci`, `chore`.
- Breaking Changes werden mit `!` und einem `BREAKING CHANGE:`-Hinweis markiert.

## 4. CHANGELOG.md

- Bei jeder inhaltlichen Code- oder Verhaltensänderung `CHANGELOG.md` mitpflegen.
- Jeder nennenswerte Changelog-Punkt braucht zwingend eine Commit-/ID-Referenz am Ende des jeweiligen Eintrags.
- Einträge ohne Referenz gelten als unvollständig.
- Die Versionsblöcke im Changelog orientieren sich an den Tagesständen dieses Projekts.
- Änderungen an sichtbaren Metadaten wie README oder Versionsanzeigen gehören ebenfalls in den Changelog.
- KiCad-Arbeitsstände aus `/projects/Elektronik/KiCad/Squeezie` werden in
  `kicad/` übernommen, vollständig wie beim HUB75-Projekt:
  `source/` für die KiCad-Quellen, `fab/rev0.1/` für Fertigungsdaten und ein
  `README.md` im `kicad/`-Ordner für die Struktur.

## 5. README.md

- `README.md` beschreibt den aktuellen Stand des Projekts, nicht die Historie.
- Die README muss die aktuelle Versionsnummer, das Codeberg-Repository und die Lizenz korrekt nennen.
- Repository-URL: `https://codeberg.org/diwou/Squeezie_-_Squeezebox_Server_Remote_Control`
- Git-Remote `origin`: `ssh://git@codeberg.org/diwou/Squeezie_-_Squeezebox_Server_Remote_Control`
- Lizenz: `CC BY-NC-SA 4.0`
- Release-Notizen gehören nicht in die README, sondern in den Changelog.

## 6. Release-Dokumentation

- Die Release-Anweisungen aus `docs/RELEASING.md` sind zu beachten.
- Bei Release-Arbeiten die dort beschriebenen Schritte und Checklisten verwenden.

## 7. Prüfungen

- `pio run` nur nach echten Codeänderungen ausführen, also wenn Firmware-, Build- oder codewirksame Konfigurationsdateien geändert wurden.
- Bei reinen Dokumentations-, Regel- oder Metadatenänderungen `pio run` nicht blind starten.
- `npm run spell` bei Text- und Markdown-Änderungen ausführen, wenn die betroffenen Dateien vom Spell-Check erfasst werden.
- Wenn nur `AGENTS.md` angepasst wurde, ist in der Regel kein `pio run` nötig.
- Vor einem erneuten Build prüfen, ob sich seit dem letzten erfolgreichen Build tatsächlich Code geändert hat.

## 8. Arbeitsablauf

- Bei inhaltlichen Änderungen zuerst `CHANGELOG.md` mitpflegen.
- Nach Codeänderungen lokal prüfen mit `pio run` und anschließend `npm run spell`, sofern Textdateien oder sichtbare Metadaten betroffen sind.
- Nach reinen Doku-, Regel- oder Metadatenänderungen nur die passenden Textprüfungen laufen lassen, nicht automatisch `pio run`.
- Erst nach erfolgreicher Prüfung und sauberem `git status` gilt eine Änderung als abgeschlossen.
- Codeberg-Uploads und `git push origin main` gehören nach erfolgreichen Prüfungen zum Abschluss eines Arbeitsstands, damit der Codeberg-Stand aktuell bleibt.
- Wenn KiCad-Dateien übernommen wurden, den neuen Stand im `kicad/`-Unterordner
  als vollständigen Projektspiegel pflegen, den Changelog mitführen und den
  Arbeitsbaum erst dann als erledigt behandeln, wenn die Übernahme sauber
  committed und auf Codeberg synchronisiert ist.
- Keine Zwischenstände als erledigt melden, solange lokale Prüfung, Changelog, Repo-Zustand und Codeberg-Stand nicht zusammenpassen.

## 9. Nachvollziehbarkeit

- Codeberg-Commit-Referenzen direkt an den einzelnen Changelog-Punkt schreiben, nicht gesammelt nur im Kopfbereich.
- Wenn sichtbar dokumentiert wird, muss die Referenz zum konkreten Eintrag passen.
- Development-Stände müssen im sichtbaren Output den aktuellen Codeberg-Commit-Hash zeigen.
- Wenn die Release-Doku oder Versionslogik angepasst wird, immer auch die dazugehörigen sichtbaren Stellen prüfen.
