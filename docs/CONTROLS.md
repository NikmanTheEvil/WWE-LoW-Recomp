# LoWRecomp – Controls

> Deutsch: [STEUERUNG.md](STEUERUNG.md)

Code: `project/src/low_input.cpp` · change bindings in `lowrecomp.toml` in the installation
folder (section `[low]`, keys `bind_*`).

## Keyboard & mouse (WWE 2K style)

| In-game action | Keyboard | Mouse | Controller |
|---|---|---|---|
| Move | W A S D | – | left stick |
| Run | hold **Shift** + direction | – | LB |
| Strike | **Arrow ←** | left click | X |
| Grapple | **Arrow ↓** (hold = strong grapple) | right click | A |
| Irish whip | direction *away from the opponent* + **Arrow ↓** | – | stick + A |
| Reversal / block | **Arrow ↑** | middle click | Y |
| Pin, climb, enter/leave ring, pick up | **Arrow →** | mouse button 4 | B |
| Finisher / signature (from chain level 3) | **Arrow ← + Arrow ↓** together | mouse button 5 | X + A |
| Taunt | **2** | – | B + Y |
| Quick grapple | **Space** + arrow key | – | right stick |
| Strong / ultimate control grapple | **Ctrl + Space** + arrow key | – | RB + right stick |
| Move theft (full momentum) | hold **Ctrl + Arrow ↑** | – | RB + Y |
| Steal a taunt (full momentum) | **Ctrl** + 1/3/4/5 | – | RB + d-pad |
| Evasive dodge | **Q** or **E** + direction | – | LT / RT + stick |
| D-pad (taunts) | 1 ↑ · 3 → · 4 ↓ · 5 ← | – | d-pad |
| Menus: move / confirm / back | W A S D / Enter or Arrow ↓ / Backspace or Arrow → | – | stick / A / B |
| Pause | Esc | – | Start |
| Back | Tab | – | Back |
| L3 / R3 | F / V | – | press sticks |

Left hand: WASD (walk), Shift (run), Ctrl (modifier), Space (+ arrow = right stick), Q/E,
1–5. Right hand: the arrow keys are the face buttons, laid out like on the controller
(↑ Y, ↓ A, ← X, → B).

**Chords:** for a combination such as Space + arrow only the combination is sent, not the
single keys as well. Press the first key first, then the second.

Reserved by ReXGlue: F3 debug overlay, F4 settings, F7 achievements, `` ` `` console.

## Controllers

Through SDL3:

| Controller | Connection | Driver |
|---|---|---|
| Xbox 360 | USB / wireless receiver | XInput |
| Xbox One / Series | USB / Bluetooth / wireless adapter | XInput, GameInput, WGI |
| DualShock 4 | USB / Bluetooth | HIDAPI (native, **no DS4Windows needed**) |
| DualSense | USB / Bluetooth | HIDAPI (native) |
| Switch Pro, 8BitDo, Logitech & others | – | HIDAPI / DirectInput + `gamecontrollerdb.txt` |

- Rumble works with all controllers.
- Keyboard/mouse and the first controller together control player 1; further controllers
  become players 2 to 4.
- **DS4Windows** reports a PlayStation controller twice (real + virtual Xbox pad), so one pad
  would control players 1 *and* 2. Close DS4Windows (not needed) or set
  `single_player_input = true` in `lowrecomp.toml`.
- **Button symbols** (launcher → *Controls* → *Button symbols*): *Xbox 360*, *PlayStation 3*
  (original PS3 artwork, only if the PS3 version was given during installation), *Keyboard*
  (keycaps for the default keyboard layout) or *Automatic* (DualShock/DualSense →
  PlayStation, other controller → Xbox, none → keyboard). The launcher writes copies of the
  font, match HUD and loading screen into the game folder (`pac\menu\fontHP.pac`,
  `pac\game2dHK.pac`, …); the originals stay unchanged.

## Options in `lowrecomp.toml` (`[low]`)

| Option | Default | Effect |
|---|---|---|
| `launcher_language` | `auto` | launcher language: `auto` (Windows language), `en` or `de` |
| `kbm` | `true` | keyboard/mouse on/off |
| `single_player_input` | `false` | all devices → player 1 |
| `swap_triggers` | `false` | older layout (run RT, modifier LT, dodge LB/RB) |
| `bind_*` | see above | bindings; alternatives separated by comma, chords with `+` |
| `button_glyphs` | `xbox` | `xbox`, `ps3`, `keyboard` or `auto` |
| `turbo_loading` | `true` | faster loading screens |
| `fps60` | `false` | keep 60 fps where the game would switch to 30 |
| `auto_storage` | `true` | pick the storage device at start-up without asking |

Key names: A–Z, 0–9, F1–F24, Space, Return, Escape, Tab, Backspace, Up, Down, Left, Right,
Shift, Control, Alt, Insert, Delete, Home, End, PageUp, PageDown, Numpad0–9, Comma, Period,
Minus, Plus, Semicolon, Quote, Slash, Backslash, LBracket, RBracket; mouse: LMB, RMB, MMB,
Mouse4, Mouse5.

## How the original layout was derived

The game texts (`pac\string.pac`) contain button symbols as private-use characters
U+E000–U+E03B. The font's glyph table (`fontHD.pac`, entry `font000.yfb`: one rectangle on
the symbol page per character) says which picture belongs to which character, and the game
texts give the meaning:

| Symbol | Button | Evidence in the game text |
|---|---|---|
| U+E007 | Y | "Reverse … (stick away from opponent + `<07>`)" |
| U+E008 | B | "Win the match by pinfall (`<08>` on downed opponent)" |
| U+E009 | A | "Grapple (`<09>`)", "Strong grapple (Hold `<09>`)" |
| U+E00A | X | "Execute a finisher (`<0A>` + `<09>`)" |
| U+E00B | LB (run) | "`<0B>` + `<12>` Run" |
| U+E00E | RB (modifier) | "Hold `<0E>` + `<11>` Strong/Ultimate Control Grapple" |
| U+E00C / U+E00F | LT / RT | "Evasive Dodge: `<0C>` or `<0F>` and `<12>`" |
| U+E011 | right stick | "`<11>` Quick Grapple" |
| U+E012 | left stick | "`<12>` Move" |
| U+E002 | d-pad | "`<02>` Taunt" |
