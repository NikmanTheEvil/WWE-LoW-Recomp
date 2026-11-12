# LoWRecomp – technical notes

## The game

| | |
|---|---|
| Executable | `default.xex`, Xbox 360 PAL/EUR, SHA-1 `bd3a0afa0dc0b9f5f69281f103015a5a94362337`, 7 122 944 bytes |
| Title ID | `54510838` (THQ), media ID `59180D5B` |
| SDK | XDK 2.0.7978 (D3D9, XAudio, XNet, XMP … statically linked) |
| Image | base `0x82000000`, size `0xBF0000`, entry `0x821C1C98` |
| Code | `.text` 0x82090000 (5.75 MiB, ~1.5 million PPC instructions), BINK 0x82650200 |
| Middleware | FMOD Ex 4.12, Bink video, Yuke's engine |

## Recompilation

ReXGlue (pinned to commit `546a082a`) translates every function into C++
(`project/generated/default`, created on your machine). `project/config/default_codegen.toml`
contains what the automatic analysis cannot know:

- `setjmp` 0x82615270 / `longjmp` 0x82614F50
- about 7 000 **function boundaries** without `.pdata` entries. Most are COM/vtable
  methods and callbacks inside code gaps, found by scanning `.rdata`/`.data` for pointers
  into gaps.
  Without them the game crashed on the first XAudio call.
- **mid-asm hooks** (calls into our C++ at specific instructions), see below.

## Runtime hooks (`project/src`)

`low_system.cpp` replaces some kernel imports of the recompiled code (`__imp__Nt*`) and
forwards to the originals in `rexruntime.dll`:

| Feature | How |
|---|---|
| **Language** | `XGetLanguage` returns the language chosen in the launcher (EUR: 1 EN, 3 DE, 4 FR, 5 ES, 6 IT) |
| **Turbo loading** | while `.pac`/`.arc` files are being opened/read, the frame pacing is switched from 60 Hz to 1000 Hz (`vsync` cvar) and back 250 ms after the last access. The game loads one file per 8 frames, so loading gets many times faster. |
| **Button symbols** | when the game opens `fontHD.pac`, `game2dSD.pac`, `LoadHD.pac` …, one character of the guest path is swapped (`fontHD` → `fontHP`/`fontHK`), the variant is opened, and the path is restored. If the variant is missing, the original is opened. |
| **Skip logos** | optional: `movies\logo.bik` is reported as missing |
| **Frame rate** | the game knows 60/50/30/25 fps (struct 0x82B50D08: +8 fps, timing constants next to it). 0x8235EB90 switches 60→30; the `LowSkipFpsDowngrade` hook (cvar `low_fps60`) skips it. `LowFrameTick` (0x8235F37C) measures the real frame rate ("LoW-FPS" in the log). |
| **Storage device** | at cold boot the start-up flow (0x8238AAB8, state 9) asks "You must select a storage device …". `LowAutoStorageYes` answers *yes* (state 11 → device selection → load), at most three times per start. |
| **Save diagnostics** | XamContent* calls, accesses to `SaveData.Dat` and the states of the six save flows are logged as "LoW-Spielstand". |

`low_input.cpp` replaces ReXGlue's input with SDL3 controllers plus a keyboard/mouse layer
that produces virtual XInput states, including chords, bindings from the configuration
and the old trigger layout.

### Save system (for reference)

Worker thread 0x823889A8 runs requests posted via 0x82388258 (jump table 0x82388A1C):
100 search (XContentCreateEnumerator, type 1, looks for `LegendSaveData.dat`), 101/102 save
(CREATE_ALWAYS), 103/104 load (OPEN_EXISTING), 105 SmackDown vs. RAW 2009 import
(XamContentCreateInternal with title 54510826, package `SaveData.dat`), 107/108 device
selection. The file inside the package is `SaveDrive:\SaveData.Dat` (4 390 496 bytes).
Message IDs live in `pac\string.pac` (EPAC → PACH → `sdb` tables: offset, length, id).
50010 is the storage-device question, 50014 "load successful", 50006 "save successful".

ReXGlue stores content under `Documents\lowrecomp\B13EBABEBABEBABE\<title>\00000001\<package>\`
with headers in `…\Headers\00000001\<package>.header`. The launcher's SvR 2009 import reads
an Xbox 360 STFS package (CON) and writes it to the 54510826 slot.

## Yuke's archive formats

```
EPAC   'EPAC', u32 table length (bytes), u32 data size, u32 7
       0x800: table of 12-byte records (name, offset, size); size 0 = group name,
       else entry at 0x4000 + offset*0x800, length size*0x100.
       Last 0x800 bytes: packer signature ("EOP5/plugin version 1.25").
PACH   'PACH', u32 n, n x (u32 id, u32 offset, u32 size), data from 8 + 12n
BPE    'BPE ', u32 0x100, u32 packed, u32 unpacked, data: Gage byte-pair encoding,
       blocks with pair table + 16-bit length (LE)
```

Limits of the original packer, which the game relies on. Exceeding them crashes it:
blocks ≤ 4000 bytes unpacked and < 2048 packed, nesting depth ≤ 16. Archives must keep
exactly the size and entry layout of the original, because the game allocates memory
from the original entries.

Button symbols are private-use characters U+E000–U+E03B in the game texts. Their pictures
sit on a 256×512 DXT5 page that appears 21× in `fontHD.pac`/`fontSD.pac` (8× on PS3); the
glyph table (`font000.yfb`, PACH child 1) holds one rectangle per character.

## Graphics variants (`tools/`)

Everything is generated on the user's machine by `tools/low_assets.py build`:

| Output | From | Tool |
|---|---|---|
| `glyphs/xbox_*.bin` | most frequent 256×512 DXT5 page of the Xbox font | `low_assets.glyph_page` |
| `glyphs/kb_*.bin` | Xbox page + keycaps drawn at the rectangles of the glyph table | `kb_glyphs.py`, `kb_hud_build.py page` |
| `kbhud/*.pac` | Xbox `game2d*.pac` / `Load*.pac`, keycaps instead of buttons | `kb_hud_build.py` |
| `glyphs/ps3_*.bin`, `ps3hud/*.pac` | PS3 game folder (optional; six `.pac` files, kept in `sources\ps3`) | `ps3_hud_merge.py` |

**Keyboard HUD without PS3 data:** to find the buttons inside the HUD images, the
maintainer compares Xbox and PS3 images once (`low_assets.py recipe`). The result,
`tools/data/kbhud_recipe.json`, contains only image keys, SHA-1 of the Xbox image, box
coordinates, button type and a 1-bit mask of the pixels to clear. It contains no image
data. With it, every user can build the keyboard HUD from the Xbox files alone. The
output is byte-identical to the PS3-based build.

Rebuilt archives keep the exact layout of the originals: replaced leaves have the same
length, BPE streams are re-packed with our own packer inside the original limits, and
everything is verified (every leaf is either the original or a replacement).

## Launcher (`project/launcher`)

Dear ImGui + Direct3D 11. Writes `lowrecomp.toml` (preserving comments, `toml_edit.h`),
prepares the symbol variants (`glyph_patch.h`: page swap in the fonts, copy of the HUD
variants, only when the content differs), imports SvR 2009 saves (`svr_import.h`,
`stfs_reader.h`) and starts `lowrecomp.exe`.
