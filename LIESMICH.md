# LoWRecomp

**WWE Legends of WrestleMania (Xbox 360, 2009) als natives Windows-Programm**, gebaut per
statischer Recompilation ([ReXGlue](https://github.com/rexglue/rexglue-sdk)). Der
PowerPC-Code des Spiels wird nach C++ übersetzt und für x64 kompiliert. Beim Spielen läuft
kein CPU-Emulator.

> **Du brauchst eine eigene Kopie des Spiels.** Dieses Repository enthält keinen Spielcode,
> keine Grafiken und keine anderen Spieldaten. Der Installer baut alles auf deinem PC aus
> deinen eigenen Dateien.

## Was drin ist

- Das Spiel läuft nativ: Menüs und Matches funktionieren, Speichern und Laden auch (noch
  nicht jeder Modus getestet)
- **Launcher** (Englisch / Deutsch) für Spielsprache, Anzeigemodus, interne Auflösung, Kantenglättung, 60-fps-Modus,
  Steuerung und mehr
- **Tastatur & Maus** wie in den aktuellen WWE-2K-Spielen (WASD + Pfeiltasten); alle
  gängigen Controller über SDL3, DualShock 4 / DualSense auch ohne DS4Windows
- **Tastensymbole**: Xbox 360, PlayStation 3 (Originalgrafiken, braucht die PS3-Fassung)
  oder Tastenkappen der Tastatur, wählbar im Launcher oder automatisch nach Controller
- **Turbo-Laden**: Ladebildschirme sind deutlich schneller als auf der Konsole
- **Spielstände** funktionieren. Superstars aus einem Spielstand von *SmackDown vs. RAW 2009*
  lassen sich importieren (Reiter *SvR 2009* im Launcher).
- Installer mit Startmenü-Eintrag, Desktop-Verknüpfung und Deinstallation

## Voraussetzungen

| | |
|---|---|
| Spiel | **WWE Legends of WrestleMania, Xbox 360, PAL/EUR**: Disc-Abbild (`.iso`) oder entpackter Ordner mit `default.xex` (SHA-1 `bd3a0afa0dc0b9f5f69281f103015a5a94362337`) |
| Optional | PS3-Fassung **NPEB90137** als Spielordner (z. B. in RPCS3 installiert: `dev_hdd0\game\NPEB90137`) oder ein Ordner mit ihren `.pac`-Dateien, für PlayStation-Tastensymbole. Eine `.pkg` wird nur archiviert (sie ist verschlüsselt). |
| System | Windows 10/11 x64, Grafikkarte mit Direct3D 12, ca. **45 GB** frei (55 GB mit Kopien der Originale), Internet während der Installation |
| Werkzeuge | Git, Python 3.10+, Visual Studio 2022 Build Tools (C++, Clang, CMake). **Fehlendes installiert der Installer selbst** (über `winget`). |

Andere Regionen oder Fassungen werden nicht unterstützt, weil die Recompiler-Konfiguration
genau zu dieser Programmdatei gehört.

## Installation

1. Repository herunterladen (**Code → Download ZIP**, dann entpacken) oder per `git clone`.
2. **`install.bat`** doppelklicken.
3. Installationsordner festlegen: Enter für den Standardordner
   (`%LOCALAPPDATA%\Programs\LoWRecomp`), `B` zum Durchsuchen oder einen Pfad eintippen.
4. Entscheiden, ob unveränderte Kopien deiner Originale in `roms\` abgelegt werden sollen
   (+8 GB, Standard: nein).
5. Dein Spiel auswählen: die `.iso` oder `default.xex` im entpackten Spielordner.
6. Optional: die Frage nach der PS3-Fassung mit Ja beantworten und den PS3-Spielordner (`PARAM.SFO`) oder `fontHD.pac` in einem Ordner mit den PS3-Dateien auswählen.
   Nur eine `.pkg`? Siehe [PlayStation-Tastensymbole](#playstation-tastensymbole-optional).
7. Warten. Beim ersten Mal werden das ReXGlue-SDK und das ganze Spiel kompiliert, je nach
   Prozessor etwa **1–2 Stunden**. Spätere Updates gehen viel schneller.

### PlayStation-Tastensymbole (optional)

Die PlayStation-Symbole stammen aus der PS3-Fassung **NPEB90137**. Eine `.pkg` kann der
Installer nicht direkt lesen, weil PS3-Pakete verschlüsselt sind. Installiere sie vorher im
PS3-Emulator [RPCS3](https://rpcs3.net), der sie entpackt:

1. RPCS3 herunterladen und nach dessen Schnellstart-Anleitung einrichten.
2. In RPCS3: **File → Install Packages/Raps/Edats** (Datei → Pakete installieren), dann deine `.pkg` auswählen.
3. Das Spiel liegt danach in `dev_hdd0\game\NPEB90137` im RPCS3-Ordner (Rechtsklick auf das
   Spiel in RPCS3 → *Open Install Folder*).
4. Im Installer die PS3-Frage mit Ja beantworten und `PARAM.SFO` in diesem Ordner auswählen.
   Schon installiert? Den Installer einfach mit dem Ordner erneut starten:
   `install.bat -Ps3 "C:\Pfad\zu\rpcs3\dev_hdd0\game\NPEB90137"`

Der Installer kopiert die sechs benötigten Dateien (`fontHD.pac`, `fontSD.pac`, `game2dHD.pac`,
`game2dSD.pac`, `LoadHD.pac`, `LoadSD.pac`) nach `sources\ps3`. Zum Spielen braucht man RPCS3
nicht, es kann danach wieder weg. Ein Ordner, der diese sechs PS3-Dateien schon enthält,
geht auch: darin `fontHD.pac` auswählen.

Alles, was spätere Updates brauchen, bleibt im Installationsordner:

```
LoWLauncher.exe, lowrecomp.exe, ...   die Programme
roms\xbox360\                          optional: unveränderte Kopie deines Originals (.iso oder Spielordner)
roms\ps3\                              optional: unveränderte Kopie deines PS3-Originals (Spielordner oder .pkg)
game\                                  Arbeitskopie des Spiels (aus der .iso entpackt oder kopiert)
sources\ps3\                           die sechs PS3-Dateien für PlayStation-Symbole (falls angegeben)
sources\installer\                     Kopie der verwendeten Installer-Version
build\                                 SDK und Build-Dateien (ca. 30 GB, löschbar - Updates dauern dann länger)
```

Danach gibt es den Startmenü-Eintrag **„WWE Legends of WrestleMania (LoWRecomp)"**, den Eintrag *Aktualisieren oder reparieren*, auf
Wunsch eine Desktop-Verknüpfung und einen Eintrag unter *Einstellungen → Apps* zum
Deinstallieren.

Enthält der gewählte Ordner schon andere Dateien, wird darin ein Unterordner `LoWRecomp`
angelegt. Ein erneuter Start des Installers aktualisiert die vorhandene Installation. Weitere Optionen
(PowerShell: `installer\Install-LoWRecomp.ps1 -?`):

| Option | Bedeutung |
|---|---|
| `-Game <ISO oder Ordner>` | Spiel ohne Dateiauswahl angeben |
| `-Ps3 <Ordner>` | PS3-Spielordner für PlayStation-Symbole |
| `-InstallDir <Ordner>` | Installationsordner ohne Nachfrage |
| `-BuildDir <Ordner>` | Ordner für SDK und Build-Dateien (Standard `<InstallDir>\build`, ca. 30 GB) |
| `-RexSdk <Ordner>` | ein schon gebautes rexglue-sdk weiterverwenden |
| `-Icon <datei.png/.ico>` | eigenes Symbol für den Launcher (nur auf deinem PC) |
| `-DesktopShortcut` | Desktop-Verknüpfung ohne Nachfrage anlegen |
| `-KeepRoms` / `-NoRomCopy` | unveränderte Kopien der Originale in `roms\` ablegen / nicht ablegen (+8 GB), ohne Nachfrage |
| `-Reconfigure` | Installationsordner, Spiel, PS3-Fassung und Symbol neu abfragen |

**Update:** neue Version herunterladen und `install.bat` erneut starten. Einstellungen und
Spielstände bleiben erhalten. **Deinstallieren:** *Einstellungen → Apps → WWE Legends of
WrestleMania (LoWRecomp)* oder der Eintrag im Startmenü. Deine Spieldateien bleiben, die
Spielstände auch, außer du willst sie löschen.

## Spielen

**LoWLauncher.exe** über das Startmenü oder die Desktop-Verknüpfung starten, Einstellungen
wählen, *PLAY*. Launcher und Installer richten sich nach der Windows-Sprache (Deutsch oder Englisch, bei
allen anderen Sprachen Englisch); die Sprache des Launchers lässt sich unter
*Spiel → Sprache des Launchers* ändern.

- Steuerung: **[docs/STEUERUNG.md](docs/STEUERUNG.md)** (Tastaturbelegung, Controller, Optionen)
- Spielstände: `Dokumente\lowrecomp`
- Logs für Fehlerberichte: `logs\lowrecomp.log` im Installationsordner

## Bekannte Probleme

- Eine interne Auflösung über 1x bringt das Spiel beim Laden eines Matches zum Absturz
  (Grafiktreiber „device removed" im aktuellen Xenos-Renderer). Der Launcher warnt davor.
- Im Publikum gibt es Grafikfehler, solange Lade-Tipps eingeblendet sind.
- Die Grafik läuft derzeit über ReXGlues Xenos-GPU-Plugin (`rexgpu-xenos.dll`). Ein eigener
  nativer Renderer wäre der nächste große Schritt.

## Technik

Wie alles funktioniert (Recompiler-Konfiguration, Laufzeit-Hooks, Archivformate, Erzeugung
der Grafikvarianten aus deinen Dateien): **[docs/TECHNICAL.md](docs/TECHNICAL.md)** (Englisch).
Bauen ohne Installer: **[docs/BUILDING.md](docs/BUILDING.md)**.

## Rechtliches

Dieses Projekt steht in keiner Verbindung zu WWE, THQ Nordic, Yuke's oder Microsoft.
WWE Legends of WrestleMania und alle zugehörigen Namen und Logos gehören ihren Inhabern.
Das Repository enthält nur eigenen Code und eigene Dokumentation unter der
[MIT-Lizenz](LICENSE). Bitte keine Spieldateien, keinen übersetzten Code (`generated/`)
und keine gebauten Programme hochladen. Die enthalten den urheberrechtlich geschützten
Code des Spiels.
