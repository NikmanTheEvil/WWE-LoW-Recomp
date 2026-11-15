# LoWRecomp

**WWE Legends of WrestleMania (Xbox 360, 2009) as a native Windows program**, built with
static recompilation ([ReXGlue](https://github.com/rexglue/rexglue-sdk)). The game's
PowerPC code is translated to C++ and compiled for x64. No CPU emulator runs at play time.

> 🇩🇪 Deutsche Anleitung: **[LIESMICH.md](LIESMICH.md)**

> **You need your own copy of the game.** This repository contains no game code,
> graphics or other game data. The installer builds everything on your PC from the
> files you own.

## Features

- The game runs natively: menus and matches work, saving and loading too (not every mode has been tested yet)
- **Launcher** (English / German) with game language, display mode, internal resolution, anti-aliasing, 60 fps
  mode, controls and more
- **Keyboard & mouse** in the style of the modern WWE 2K games (WASD + arrow keys); all
  common controllers via SDL3, with native DualShock 4 / DualSense support
- **Button symbols**: Xbox 360, PlayStation 3 (original PS3 artwork, needs the PS3
  version) or keyboard keycaps, chosen in the launcher or automatically by controller
- **Turbo loading**: loading screens run much faster than on the console
- **Save games** work. Superstars from a *SmackDown vs. RAW 2009* save can be
  imported (tab *SvR 2009* in the launcher).
- Installer with Start menu entry, desktop shortcut and uninstaller

## Requirements

| | |
|---|---|
| Game | **WWE Legends of WrestleMania, Xbox 360, PAL/EUR**: disc image (`.iso`) or the extracted folder with `default.xex` (SHA-1 `bd3a0afa0dc0b9f5f69281f103015a5a94362337`) |
| Optional | PS3 version **NPEB90137** as a game folder (e.g. installed in RPCS3: `dev_hdd0\game\NPEB90137`) or a folder with its `.pac` files, for PlayStation button symbols. A `.pkg` is only archived (it is encrypted). |
| System | Windows 10/11 x64, Direct3D 12 graphics card, about **45 GB** free disk space (55 GB with copies of the originals), internet connection during installation |
| Tools | Git, Python 3.10+, Visual Studio 2022 Build Tools (C++, Clang, CMake). **The installer installs missing tools itself** (via `winget`). |

Other regions or versions of the game are not supported, because the recompiler
configuration is tied to this exact executable.

## Installation

1. Download the latest release, then unpack it.
2. Double-click **`install.bat`**.
3. Choose the installation folder: press Enter for the default
   (`%LOCALAPPDATA%\Programs\LoWRecomp`), `B` to browse, or type a path.
4. Decide whether untouched copies of your originals should be kept in `roms\` (+8 GB,
   default: no).
5. Select your game when asked: the `.iso`, or `default.xex` in the extracted game folder.
6. Optional: say yes to the PS3 question and select the PS3 game folder (`PARAM.SFO`) or `fontHD.pac` in a folder with the PS3 files.
   Only have a `.pkg`? See [PlayStation button symbols](#playstation-button-symbols-optional).
7. Wait. The first installation compiles the ReXGlue SDK and the whole game, which takes
   roughly **1–2 hours** depending on your CPU. Later updates are much faster.

### PlayStation button symbols (optional)

The PlayStation symbols are taken from the PS3 version **NPEB90137**. The installer cannot
read a `.pkg` directly, because PS3 packages are encrypted. Install it in the PS3 emulator
[RPCS3](https://rpcs3.net) first, which unpacks it:

1. Download RPCS3 and set it up as described in its quickstart guide.
2. In RPCS3: **File → Install Packages/Raps/Edats**, then select your `.pkg`.
3. The game now sits in `dev_hdd0\game\NPEB90137` inside the RPCS3 folder (right-click
   the game in RPCS3 → *Open Install Folder*).
4. In the installer, say yes to the PS3 question and select `PARAM.SFO` in that folder.
   Already installed? Run the installer again with the folder:
   `install.bat -Ps3 "C:\path\to\rpcs3\dev_hdd0\game\NPEB90137"`

The installer copies the six files it needs (`fontHD.pac`, `fontSD.pac`, `game2dHD.pac`,
`game2dSD.pac`, `LoadHD.pac`, `LoadSD.pac`) into `sources\ps3`. RPCS3 is not needed for
playing and can be removed afterwards. A folder that already contains these six PS3 files
works too: select `fontHD.pac` in it.

Everything later updates need is kept in the installation folder:

```
LoWLauncher.exe, lowrecomp.exe, ...   the programs
roms\xbox360\                          optional: untouched copy of your original (.iso or game folder)
roms\ps3\                              optional: untouched copy of your PS3 original (game folder or .pkg)
game\                                  working copy of the game (extracted from the .iso or copied)
sources\ps3\                           the six PS3 files for PlayStation symbols (if given)
sources\installer\                     copy of the installer version that was used
build\                                 SDK and build files (about 30 GB, can be deleted - updates then take longer)
```

At the end you get a Start menu entry **"WWE Legends of WrestleMania (LoWRecomp)"**,
an entry *Update or repair*, optionally a desktop shortcut, and an entry under *Settings → Apps*
for uninstalling.

If the chosen folder already contains other files, a `LoWRecomp` subfolder is created in it.
Running the installer again updates the existing installation in place. Advanced options
(PowerShell: `installer\Install-LoWRecomp.ps1 -?`):

| Option | Meaning |
|---|---|
| `-Game <iso or folder>` | game without the file dialog |
| `-Ps3 <folder>` | PS3 game folder for PlayStation symbols |
| `-InstallDir <folder>` | installation folder without asking |
| `-BuildDir <folder>` | folder for SDK and build files (default `<InstallDir>\build`, about 30 GB) |
| `-RexSdk <folder>` | reuse an already built rexglue-sdk checkout |
| `-Icon <file.png/.ico>` | your own icon for the launcher, only on your PC |
| `-DesktopShortcut` | create the desktop shortcut without asking |
| `-KeepRoms` / `-NoRomCopy` | keep / do not keep untouched copies of the originals in `roms\` (+8 GB) without asking |
| `-Reconfigure` | ask for installation folder, game, PS3 version and icon again |

**Update:** download the new version and run `install.bat` again. Settings and save
games are kept. **Uninstall:** *Settings → Apps → WWE Legends of WrestleMania (LoWRecomp)*,
or the uninstall entry in the Start menu. Your game files and save games are kept, unless
you choose to delete the save games.

## Playing

Start **LoWLauncher.exe** from the Start menu or the desktop shortcut. Choose your
settings and press *PLAY*. The launcher and the installer follow your Windows language (English or German, English for
all other languages); the launcher language can be changed under *Game → Launcher language*.

- Controls: see **[docs/CONTROLS.md](docs/CONTROLS.md)** (keyboard layout, controllers, options)
- Save games: `Documents\lowrecomp`
- Logs for bug reports: `logs\lowrecomp.log` in the installation folder

## Known issues

- An internal resolution above 1x crashes the game when loading a match (graphics driver
  "device removed" in the current Xenos renderer). The launcher warns about it.
- The crowd can show graphics glitches while loading tips are on screen.
- Graphics currently go through ReXGlue's Xenos GPU plugin (`rexgpu-xenos.dll`). A native
  renderer would be the next big step.

## How it works

See **[docs/TECHNICAL.md](docs/TECHNICAL.md)**: recompiler configuration, runtime hooks
(file access, turbo loading, button-symbol swap, input, frame rate, save device), the
Yuke's archive formats and how the graphics variants are generated from your own files.
Building by hand without the installer: **[docs/BUILDING.md](docs/BUILDING.md)**.

## Repository layout

```
install.bat                 start the installer (double-click)
installer/                  Install-LoWRecomp.ps1, Uninstall-LoWRecomp.ps1
project/                    CMake project: runtime hooks (src/), launcher (launcher/),
                            recompiler configuration (config/default_codegen.toml), resources
tools/                      asset tools (Python): button symbols, keyboard/PS3 HUD,
                            disc image extraction; analysis/ for maintainers
tests/                      unit tests for the tools (python -m unittest discover -s tests)
docs/                       controls, technical notes, manual build
```

## Credits

- [ReXGlue SDK](https://github.com/rexglue/rexglue-sdk) by Tom Clay, built on the work of the
  [Xenia](https://xenia.jp) project. Without it this project would not exist.
- [Dear ImGui](https://github.com/ocornut/imgui), [SDL](https://libsdl.org),
  [SDL_GameControllerDB](https://github.com/mdqinc/SDL_GameControllerDB),
  [Inter](https://github.com/rsms/inter). See [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).

## Legal

This project is not affiliated with or endorsed by WWE, THQ Nordic, Yuke's or Microsoft.
WWE Legends of WrestleMania and all related names and logos belong to their respective
owners. The repository only contains original code and documentation, released under the
[MIT License](LICENSE). Please do not upload game files, recompiled code (`generated/`)
or built executables. Those contain the game's copyrighted code.
