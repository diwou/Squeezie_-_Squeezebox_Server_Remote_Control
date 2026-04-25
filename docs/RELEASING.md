# Release-Prozess

Diese Datei beschreibt, wie ein neuer Release dieses Projekts erzeugt wird.
Der Prozess folgt den Konventionen aus [`AGENTS.md`](../AGENTS.md)
(Semantic Versioning, Keep a Changelog, Conventional Commits).

## Verbindliche Kurz-Checkliste

Diese Punkte sind Pflicht, nicht optional, und es gibt dafür keine Abkürzung.
Diese Datei ist ebenfalls eine Vorrangregel und wird nicht frei interpretiert.

1. Erst prüfen, ob `AGENTS.md` und diese Datei aktuelle Vorgaben enthalten.
2. `CHANGELOG.md` vor dem Release vollständig vorbereiten.
3. Versionsstand, sichtbare Metadaten und Changelog müssen zusammenpassen.
4. Build und Tests erst nach echten Codeänderungen erneut ausführen.
5. Release-Tag und Codeberg-Push gehören nach sauberem Arbeitsbaum zum Abschluss; der Codeberg-Stand soll danach dem lokalen Stand entsprechen.
6. Release nur mit eindeutigen Commit-/ID-Referenzen in den Notes.
7. Keine Abkürzungen bei der Dokumentation der Release-Schritte.
8. Wenn du an irgendeinem Punkt unsicher bist, zuerst die Regeln erneut lesen und nicht raten.
9. Wenn ein aktueller KiCad-Stand für Squeezie vorliegt, vor dem Release
   `kicad/` vollständig wie das HUB75-Projekt aktualisieren, also
   `source/`, `fab/rev0.1/` und das `kicad/README.md` mitnehmen.

**Faustregel:** Ein Release entsteht NICHT durch einen Commit, sondern durch
ein Tag. Alles andere ist Vorbereitung.

---

## Voraussetzungen

Bevor überhaupt ein Release angefasst wird, müssen diese Dinge stimmen:

- [ ] Arbeitsverzeichnis ist sauber (`git status` zeigt nichts Unverfolgtes)
- [ ] Alle Änderungen sind auf `main` gemerged
- [ ] `CHANGELOG.md` hat einen ausreichend gefüllten `[Unreleased]`-Abschnitt
- [ ] Tests laufen durch (falls vorhanden): `<projektspezifischer Befehl>`
- [ ] Build funktioniert auf der Zielplattform
- [ ] Signing ist eingerichtet (`git config --get commit.gpgsign` → `true`)

---

## Version festlegen

Die nächste Version ergibt sich aus dem Inhalt des `[Unreleased]`-Abschnitts
im Changelog:

| Enthält Kategorie … | → nächster Sprung |
|---|---|
| `BREAKING CHANGE` / `Removed` | MAJOR (`1.2.3` → `2.0.0`) |
| `Added` oder `Changed` | MINOR (`1.2.3` → `1.3.0`) |
| nur `Fixed` / `Security` | PATCH (`1.2.3` → `1.2.4`) |

Im Zweifel eher den größeren Sprung wählen.

**In der 0.x.y-Phase:** Breaking Changes dürfen in MINOR einfließen.

---

## Schritt für Schritt

### 1. Aktueller Stand prüfen

```bash
git checkout main
git pull
git status
```

Alles clean? Weiter. Sonst vorher aufräumen.

### 2. Letzten Release ermitteln

```bash
git describe --tags --abbrev=0
```

Gibt z.B. `v1.2.3` zurück. Die nächste Version ist dann nach obiger Tabelle
zu wählen — im Folgenden `<NEW>` (z.B. `1.3.0`).

### 3. Changelog aktualisieren

In `CHANGELOG.md`:

1. `[Unreleased]` umbenennen in `[<NEW>] - YYYY-MM-DD` (heutiges Datum)
2. Darüber einen neuen leeren `[Unreleased]`-Abschnitt einfügen:
   ```markdown
   ## [Unreleased]

   ## [<NEW>] - 2026-04-21
   ```
3. Vergleichslinks am Dateiende aktualisieren:
   ```markdown
   [Unreleased]: https://codeberg.org/<user>/<repo>/compare/v<NEW>...HEAD
   [<NEW>]: https://codeberg.org/<user>/<repo>/compare/v<PREV>...v<NEW>
   ```

### 4. Version in Source-of-Truth eintragen

<!-- Projektspezifisch anpassen: -->

```bash
# Python (pyproject.toml):
sed -i 's/^version = .*/version = "<NEW>"/' pyproject.toml

# Node (package.json):
npm version <NEW> --no-git-tag-version

# PlatformIO (platformio.ini):
# manuell Zeile `custom_version = <NEW>` anpassen

# Plain C (version.h):
# manuell #define VERSION "<NEW>" anpassen
```

### 5. README prüfen

- Stimmen die aufgelisteten Features noch?
- Ist die Installationsanleitung aktuell?
- Zeigt der Versions-Badge auf die neue Version (bei statischem Link)?
- Bei KiCad-Änderungen zusätzlich prüfen, ob der Projekt-Unterordner
  `kicad/` vollständig gespiegelt ist und dem aktuellen Arbeitsstand
  im externen KiCad-Verzeichnis entspricht.

Änderungen ebenfalls im Changelog unter `Changed` → `docs` vermerken.

### 6. Release-Commit

```bash
git add CHANGELOG.md README.md <source-of-truth-datei>
git commit -m "chore: release v<NEW>"
```

### 7. Signierten Tag setzen

```bash
git tag -s v<NEW> -m "Release <NEW>"
```

Verifikation:

```bash
git tag -v v<NEW>
```

Sollte `Good "git" signature` zeigen.

### 8. Pushen

```bash
git push origin main
git push origin v<NEW>
```

### 9. Codeberg-Release anlegen

1. Im Browser: `https://codeberg.org/<user>/<repo>/releases/new`
2. **Tag:** `v<NEW>` auswählen (existiert bereits durch Push)
3. **Release Title:** `v<NEW>` oder `<NEW> — Kurzbeschreibung`
4. **Release Notes:** Abschnitt `[<NEW>] - ...` aus dem Changelog einfügen
   (ohne die Markdown-Überschrift `##` selbst)
5. **Binärartefakte anhängen** (falls vorhanden):
   <!-- Projektspezifisch: -->
   - Firmware: `.pio/build/<board>/firmware.bin`
   - KiCad-/Fertigungsdaten: `kicad/fab/rev<NEW>/<datei>`
   - etc.
6. **Publish Release**

Den vorab erzeugten Source-Tarball und das Zip erzeugt Codeberg automatisch
aus dem Tag — nicht selbst anhängen.

### 10. Nächste Entwicklungsrunde

Ab jetzt trägt der Code wieder eine Development-Version. Wenn du die
Source-of-Truth-Datei bereits auf die nächste geplante Version stellen
möchtest (z.B. `1.4.0-dev`), geht das in einem separaten Commit:

```bash
# Source-of-Truth auf <NÄCHSTE>-dev anpassen
git commit -m "chore: begin development on v<NÄCHSTE>"
git push
```

Optional — bei kleinen Projekten kann das auch entfallen und erst beim
nächsten Release wieder gesetzt werden.

---

## Hotfix-Release

Für einen kritischen Fix auf einem bereits releasten Stand, ohne
zwischenzeitliche Development-Änderungen aus `main` mitzuziehen:

```bash
# Von letztem Release-Tag ausgehen
git checkout -b hotfix/v<NEW-PATCH> v<LAST>

# Fix einbauen, committen
# ...
git commit -m "fix(<scope>): <beschreibung>"

# Changelog und Version wie oben aktualisieren,
# Versions-Sprung ist PATCH
# Commit, Tag, Push wie oben, damit der veröffentlichte Stand direkt aktuell ist

# Nach Abschluss: Fix nach main backporten
git checkout main
git cherry-pick <commit-sha-des-fixes>
git push
```

---

## Release Candidate (optional)

Bei größeren Releases kann vorab ein RC veröffentlicht werden:

```bash
git tag -s v<NEW>-rc.1 -m "Release Candidate 1 for <NEW>"
git push origin v<NEW>-rc.1
```

RCs werden im Changelog NICHT als eigener Abschnitt geführt — der endgültige
Release-Abschnitt fasst alles zusammen. Auf Codeberg den RC-Release als
**Pre-release** markieren.

---

## Rollback

Wenn nach dem Push festgestellt wird, dass der Release kaputt ist:

**Tag wurde gepusht, aber Codeberg-Release noch nicht angelegt:**

```bash
git tag -d v<NEW>
git push origin :refs/tags/v<NEW>
```

Danach Fehler fixen, Changelog-Datum ggf. anpassen, neu taggen.

**Codeberg-Release ist bereits publiziert:**

NICHT nachträglich überschreiben. Stattdessen neuen PATCH-Release machen
(`<NEW>` → `<NEW+1>`) mit Eintrag im Changelog, der den Fehler beschreibt.
Releases sind historische Artefakte — sie werden nicht umgeschrieben.

---

## Checkliste (Kurzfassung)

```
[ ] main aktuell, clean
[ ] Tests/Build grün
[ ] Version nach SemVer festgelegt: ___________
[ ] CHANGELOG.md: [Unreleased] → [<NEW>] - <datum>, neuer [Unreleased]
[ ] CHANGELOG.md: Vergleichslinks aktualisiert
[ ] Source-of-Truth: Version eingetragen
[ ] README.md: auf Aktualität geprüft
[ ] Commit: "chore: release v<NEW>"
[ ] Tag signiert: git tag -s v<NEW> -m "Release <NEW>"
[ ] git tag -v v<NEW> erfolgreich
[ ] git push origin main && git push origin v<NEW>
[ ] Codeberg-Release angelegt, Notes eingefügt, Assets angehängt
[ ] Release publiziert
```
