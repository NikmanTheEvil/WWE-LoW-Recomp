# Building without the installer

The installer (`install.bat`) does all of this automatically. These are the manual steps,
for development.

## Tools

- Visual Studio 2022 (or Build Tools) with *Desktop development with C++*,
  *C++ Clang tools for Windows*, *C++ CMake tools*, Windows SDK 10.0.26100
- Git, Python 3.10+
- Work in a *x64 Native Tools Command Prompt* and put Clang first in `PATH`:
  `set PATH=%VCINSTALLDIR%Tools\Llvm\x64\bin;%PATH%`

## 1. ReXGlue SDK

```bat
git clone https://github.com/rexglue/rexglue-sdk.git C:\Tools\rexglue-sdk
cd /d C:\Tools\rexglue-sdk
git checkout 546a082a4356173635f6c40510a41ff53e518b5a
git submodule update --init --recursive
cmake --preset win-amd64
cmake --build out/build/win-amd64 --config Release --target install
```

## 2. Game files

Put the extracted game (folder with `default.xex`, `pac`, `sound`, `movies` …) next to the
project as `game\`, or adjust the paths in `project\lowrecomp_manifest.toml`.
A disc image can be extracted with

```bat
python tools\xiso_extract.py extract "WWE Legends of WrestleMania.iso" game
```

## 3. Recompile and build

```bat
cd project
C:\Tools\rexglue-sdk\out\install\win-amd64\bin\rexglue.exe codegen lowrecomp_manifest.toml
cmake --preset win-amd64-relwithdebinfo ^
      -Drexglue_DIR=C:/Tools/rexglue-sdk/out/install/win-amd64/lib/cmake/rexglue ^
      -DLOW_IMGUI_DIR=C:/Tools/rexglue-sdk/thirdparty/imgui ^
      -DLOWRECOMP_ASSET_DIR=%CD%\..\assets
cmake --build --preset win-amd64-relwithdebinfo
```

The SDK is built as Release, so the project uses RelWithDebInfo (same CRT, with debug
symbols). Output: `project\out\build\win-amd64-relwithdebinfo\` (`lowrecomp.exe`,
`LoWLauncher.exe`, `rexruntime.dll`, `rexgpu-xenos.dll`).

## 4. Graphics variants

```bat
python -m pip install -r tools\requirements.txt
python tools\low_assets.py build --game game --out assets [--ps3 <folder with PS3 .pac files>]
```

With `-DLOWRECOMP_ASSET_DIR` (step 3) the build copies `assets\glyphs`, `kbhud` and `ps3hud`
next to the executable; otherwise copy them by hand. For `--ps3`, put `fontHD.pac`, `fontSD.pac`,
`game2dHD.pac`, `game2dSD.pac`, `LoadHD.pac` and `LoadSD.pac` from the PS3 version's game folder
(e.g. RPCS3: `dev_hdd0\game\NPEB90137\USRDIR\...`) into one folder.

## Tests

```bat
python -m unittest discover -s tests
```

## Updating the keyboard-HUD recipe (maintainers)

Only needed if the button detection in `tools/kb_hud_build.py` changes. It needs both
versions of the game:

```bat
python tools\low_assets.py recipe --game game --ps3 ps3 --out tools\data\kbhud_recipe.json
```
