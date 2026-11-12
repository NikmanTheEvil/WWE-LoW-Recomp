# LoWRecomp – Steuerung

Code: `project/src/low_input.cpp` · Belegung ändern: `lowrecomp.toml` im Installationsordner (Abschnitt `[low]`, Schlüssel `bind_*`)

## Tastatur & Maus (WWE-2K-Stil)

| Aktion im Spiel | Tastatur | Maus | entspricht Controller |
|---|---|---|---|
| Bewegen | W A S D | – | linker Stick |
| Rennen | **Shift** halten + Richtung | – | LB |
| Strike | **Pfeil ←** | Linksklick | X |
| Grapple | **Pfeil ↓** (halten = Strong Grapple) | Rechtsklick | A |
| Irish Whip | Richtung *weg vom Gegner* + **Pfeil ↓** | – | Stick + A |
| Reversal / Block | **Pfeil ↑** | Mittelklick | Y |
| Pin, Klettern, Ring betreten/verlassen, aufheben | **Pfeil →** | Maustaste 4 | B |
| Finisher / Signature (ab Chain Level 3) | **Pfeil ← + Pfeil ↓** gleichzeitig | Maustaste 5 | X + A |
| Taunt | **2** | – | B + Y |
| Quick Grapple (kaum genutzt) | **Leertaste** + Pfeiltaste | – | rechter Stick |
| Strong / Ultimate Control Grapple | **Strg + Leertaste** + Pfeiltaste | – | RB + rechter Stick |
| Move Theft (volles Momentum) | **Strg + Pfeil ↑** halten | – | RB + Y |
| Taunt stehlen (volles Momentum) | **Strg** + 1/3/4/5 | – | RB + Steuerkreuz |
| Ausweichrolle | **Q** oder **E** + Richtung | – | LT / RT + Stick |
| Steuerkreuz (Taunts) | 1 ↑ · 3 → · 4 ↓ · 5 ← | – | Steuerkreuz |
| Menü bewegen / bestätigen / zurück | W A S D / Enter oder Pfeil ↓ / Backspace oder Pfeil → | – | Stick / A / B |
| Pause | Esc | – | Start |
| Back | Tab | – | Back |
| L3 / R3 | F / V | – | Sticks drücken |

**Aufteilung:** linke Hand WASD (gehen) + Shift (rennen), Strg (Modifikator), Leertaste
(+ Pfeil = rechter Stick), Q/E, 1–5; rechte Hand Pfeiltasten als Aktionsknöpfe, angeordnet
wie auf dem Controller (↑ Y, ↓ A, ← X, → B). Esc = Pause.

**Akkorde:** Bei einer Tastenkombination (z. B. Leertaste + Pfeil) kommt nur die Kombination
an, nicht zusätzlich die einzelnen Tasten. Die erste Taste also zuerst drücken, dann die zweite.

Menüs bedienst du mit WASD, Enter (oder Pfeil ↓) und Backspace (oder Pfeil →).

Belegt ReXGlue selbst: F3 Debug-Overlay, F4 Einstellungen, F7 Erfolge, `^` Konsole.

## Controller

Läuft über SDL3 mit diesen Treibern:

| Controller | Anschluss | Treiber |
|---|---|---|
| Xbox 360 | USB / Wireless-Empfänger | XInput |
| Xbox One / Series | USB / Bluetooth / Wireless Adapter | XInput, GameInput, WGI |
| DualShock 4 (PS4) | USB / Bluetooth | HIDAPI (nativ, **ohne DS4Windows**) |
| DualSense (PS5) | USB / Bluetooth | HIDAPI (nativ) |
| Switch Pro, 8BitDo, Logitech & Co. | – | HIDAPI / DirectInput + `gamecontrollerdb.txt` |

- Vibration funktioniert über SDL bei allen Controllern.
- Tastatur/Maus und der erste Controller steuern gemeinsam Spieler 1. Weitere Controller
  werden Spieler 2 bis 4.
- **DS4Windows:** Das Programm meldet den PlayStation-Controller doppelt (einmal echt, einmal
  als virtuellen Xbox-Controller). Dann steuert ein Pad Spieler 1 *und* 2. Lösungen:
  DS4Windows beenden (der DS4 wird nativ unterstützt) oder in `lowrecomp.toml`
  `single_player_input = true` setzen.
- **Tastensymbole:** Launcher → *Controls* → *Button symbols* (deutsch: Steuerung → Tastensymbole):
  - *Xbox 360* – Original
  - *PlayStation 3* – Originalsymbole der PS3-Fassung (Kreuz/Kreis/Quadrat/Dreieck, L1/R1/L2/R2,
    L3/R3, Select/Start); nur wenn bei der Installation die PS3-Fassung angegeben wurde
  - *Tastatur* – Tastenkappen im Stil der Controller-Knöpfe für die Standardbelegung
    (Pfeiltasten in den Farben von X/Y/B/A, WASD, Leertaste+Pfeil, Shift, Strg, Q/E, Tab/Esc,
    F/V, 1/3/4/5); eigene Belegungen werden nicht abgebildet
  - *Automatisch* – beim Spielstart: DualShock/DualSense → PlayStation, anderer Controller →
    Xbox, kein Controller → Tastatur

  Der Launcher legt dafür Kopien von Schrift, Match-HUD und Ladebildschirm im Spielordner an
  (`pac\menu\fontHP.pac`, `pac\game2dHK.pac` usw.). Die Originale bleiben unverändert; die
  Deinstallation entfernt die Kopien wieder.

## Optionen in `lowrecomp.toml` (`[low]`)

| Option | Standard | Wirkung |
|---|---|---|
| `launcher_language` | `auto` | Sprache des Launchers: `auto` (Windows-Sprache), `en` oder `de` |
| `kbm` | `true` | Tastatur/Maus an/aus |
| `single_player_input` | `false` | alle Geräte → Spieler 1 |
| `swap_triggers` | `false` | frühere Belegung (Rennen RT, Modifikator LT, Ausweichen LB/RB) |
| `bind_*` | s. o. | Belegung; Alternativen mit Komma, Akkorde mit `+` |
| `button_glyphs` | `xbox` | `xbox`, `ps3`, `keyboard` oder `auto` |
| `turbo_loading` | `true` | schnellere Ladebildschirme |
| `fps60` | `false` | 60 fps behalten, wo das Spiel auf 30 schalten würde |
| `auto_storage` | `true` | Speichergerät beim Start ohne Nachfrage wählen |

## Herleitung der Original-Belegung

Die Spieltexte (`pac\string.pac`) enthalten Tastensymbole als private Unicode-Zeichen
U+E000–U+E03B. Welches Bild zu welchem Zeichen gehört, steht in der Zeichentabelle der
Schrift (`fontHD.pac`, Eintrag `font000.yfb`: je Zeichen ein Rechteck auf der Symbolseite).
Damit ist die Zuordnung eindeutig; die Bedeutung zeigen die Spieltexte:

| Symbol | Taste | Beleg im Spieltext |
|---|---|---|
| U+E007 | Y | „Reverse … (Stick weg vom Gegner + `<07>`)" |
| U+E008 | B | „Win the match by pinfall (`<08>` on downed opponent)" |
| U+E009 | A | „Grapple (`<09>`)", „Strong grapple (Hold `<09>`)" |
| U+E00A | X | „Execute a finisher (`<0A>` + `<09>`)" + Review: Finisher = A+X |
| U+E00B | LB (Rennen) | „`<0B>` + `<12>` Run" |
| U+E00E | RB (Modifikator) | „Hold `<0E>` + `<11>` Strong/Ultimate Control Grapple" |
| U+E00C / U+E00F | LT / RT | „Evasive Dodge: `<0C>` or `<0F>` and `<12>`" |
| U+E011 | rechter Stick | „`<11>` Quick Grapple" |
| U+E012 | linker Stick | „`<12>` Move" |
| U+E002 | Steuerkreuz | „`<02>` Taunt" |

Das passt zur GamesRadar-Vorschau: „X for Strikes, Y for Defense (block, reversals)" und
„A grapples and B is an all-purpose move (exit ring, turnbuckle, pin)". Taunt = `<08>` + `<07>`
(B + Y) deckt sich mit Gaming Nexus: Reverse- und B-Taste gleichzeitig.

## Hinweise

- Maus als rechter Stick ist bewusst aus: Im Wrestling löst eine Mausbewegung sonst
  versehentlich Quick Grapples aus.
- Tastensymbole, die fest in andere Grafiken gemalt sind, werden von den Varianten nicht
  erfasst; gefunden wurden bisher nur die Symbolseite der Schrift sowie Match-HUD und
  Ladebildschirm.
