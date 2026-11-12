#!/usr/bin/env python3
"""LoWRecomp - builds every derived game asset from YOUR OWN game files.

Nothing in the repository contains game graphics. This tool reads the files of
your own copy of WWE Legends of WrestleMania and writes the variants the launcher
needs next to the game executable:

  glyphs/xbox_hd.bin, xbox_sd.bin   button-symbol page of the Xbox 360 font
  glyphs/kb_hd.bin,   kb_sd.bin     the same page with keyboard keycaps
  kbhud/game2dHK.pac, game2dSK.pac, LoadHK.pac, LoadSK.pac
                                    match HUD / loading screen with keycaps
  (optional, only with the PS3 version's files)
  glyphs/ps3_hd.bin, ps3_sd.bin     PS3 button-symbol page
  ps3hud/game2dHP.pac, game2dSP.pac, LoadHP.pac, LoadSP.pac

Usage:
  low_assets.py build  --game <game dir> --out <dir> [--ps3 <dir with PS3 .pac files>] [--jobs N]
  low_assets.py recipe --game <game dir> --ps3 <dir> --out <recipe.json>   (maintainers)
  low_assets.py icon   --png <image.png> --out <icon.ico>

The keyboard HUD needs to know where the controller buttons sit inside the HUD
images. With the PS3 files this is found by comparing both versions; without
them the tool uses data/kbhud_recipe.json (coordinates and pixel masks only, no
graphics), so the PS3 version is optional.
"""
import argparse
import hashlib
import json
import os
import struct
import sys
import time
from collections import Counter
from concurrent.futures import ProcessPoolExecutor

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

PAGE_BYTES = 256 * 512
RECIPE = os.path.join(HERE, "data", "kbhud_recipe.json")
XEX_SHA1 = "bd3a0afa0dc0b9f5f69281f103015a5a94362337"  # Xbox 360, PAL/EUR

# (archive, path inside the game folder, launcher name for keyboard, for PS3)
HUD_ARCHIVES = [
    ("game2dHD.pac", os.path.join("pac", "game2dHD.pac"), "game2dHK.pac", "game2dHP.pac", 0),
    ("game2dSD.pac", os.path.join("pac", "game2dSD.pac"), "game2dSK.pac", "game2dSP.pac", 1),
    ("LoadHD.pac", os.path.join("pac", "menu", "LoadHD.pac"), "LoadHK.pac", "LoadHP.pac", 0),
    ("LoadSD.pac", os.path.join("pac", "menu", "LoadSD.pac"), "LoadSK.pac", "LoadSP.pac", 1),
]
FONTS = [("hd", os.path.join("pac", "menu", "fontHD.pac"), "fontHD.pac"),
         ("sd", os.path.join("pac", "menu", "fontSD.pac"), "fontSD.pac")]


def log(msg):
    print(msg, flush=True)


def sha1_file(path):
    h = hashlib.sha1()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def read(path):
    with open(path, "rb") as f:
        return f.read()


def write_atomic(path, data):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    tmp = path + ".tmp"
    with open(tmp, "wb") as f:
        f.write(data)
    os.replace(tmp, path)


def glyph_page(font_pac_bytes):
    """The button-symbol page: the 256x512 DXT5 texture (no mips) that occurs most
    often in the font archive (21x on Xbox 360, 8x on PS3)."""
    d = font_pac_bytes
    count = Counter()
    first = {}
    i = 0
    while True:
        o = d.find(b"DDS \x7c\x00\x00\x00", i)
        if o < 0:
            break
        i = o + 4
        if o + 128 + PAGE_BYTES > len(d):
            continue
        h, w = struct.unpack_from("<II", d, o + 12)
        mips = struct.unpack_from("<I", d, o + 28)[0]
        if (w, h) != (256, 512) or mips > 1 or d[o + 84:o + 88] != b"DXT5":
            continue
        page = d[o + 128:o + 128 + PAGE_BYTES]
        k = hashlib.sha1(page).digest()
        count[k] += 1
        first.setdefault(k, page)
    if not count:
        raise SystemExit("No button-symbol page found in the font archive (other game version?)")
    return first[count.most_common(1)[0][0]]


# ---------------------------------------------------------------------------
# Worker functions (run in separate processes)
# ---------------------------------------------------------------------------
def _job_keyboard_hud(xbox_pac, ps3_pac, recipe_leaves, pages, out_path):
    import kb_hud_build as KB
    sys.setrecursionlimit(10000)
    t = time.time()
    xd = read(xbox_pac)
    pd = read(ps3_pac) if ps3_pac else None
    recipe = KB.recipe_load(recipe_leaves) if (pd is None and recipe_leaves is not None) else None
    md, repl, st, rep = KB.build_hud(xd, pd, pages["xbox"], pages["kb"], recipe=recipe)
    KB.verify(xd, md, repl)
    write_atomic(out_path, md)
    return (f"{os.path.basename(out_path)}: {len(repl)} images ({', '.join(f'{k} {v}' for k, v in sorted(rep.items()))}), "
            f"{time.time() - t:.0f} s")


def _job_ps3_hud(xbox_pac, ps3_pac, out_path):
    import ps3_hud_merge as M
    sys.setrecursionlimit(10000)
    t = time.time()
    xd, pd = read(xbox_pac), read(ps3_pac)
    md, st = M.merge_epac(xd, pd)
    leaves, from_ps3 = M.verify(xd, pd, md)
    write_atomic(out_path, md)
    return f"{os.path.basename(out_path)}: {from_ps3} images from PS3, {time.time() - t:.0f} s"


def _job_recipe(xbox_pac, ps3_pac, pages, name):
    import kb_hud_build as KB
    sys.setrecursionlimit(10000)
    xd, pd = read(xbox_pac), read(ps3_pac)
    record = {}
    KB.build_hud(xd, pd, pages["xbox"], pages["kb"], record=record)
    return name, hashlib.sha1(xd).hexdigest(), KB.recipe_dump(record)


# ---------------------------------------------------------------------------
def check_game(game):
    xex = os.path.join(game, "default.xex")
    if not os.path.isfile(xex):
        raise SystemExit(f"default.xex not found in {game}")
    missing = [p for _, p, *_ in HUD_ARCHIVES if not os.path.isfile(os.path.join(game, p))]
    missing += [p for _, p, _ in FONTS if not os.path.isfile(os.path.join(game, p))]
    if missing:
        raise SystemExit("Game files missing: " + ", ".join(missing))


def make_pages(game, ps3, out):
    """Symbol pages: Xbox (from the game), keyboard (drawn), PS3 (optional)."""
    import kb_hud_build as KB
    import kb_glyphs as K
    pages = {"xbox": [], "kb": [], "ps3": []}
    for tag, rel, ps3name in FONTS:
        font = os.path.join(game, rel)
        xbox = glyph_page(read(font))
        kb = KB.keyboard_page_bin(xbox, K.rects_from_table(KB.glyph_rects(font)))
        write_atomic(os.path.join(out, "glyphs", f"xbox_{tag}.bin"), xbox)
        write_atomic(os.path.join(out, "glyphs", f"kb_{tag}.bin"), kb)
        pages["xbox"].append(xbox)
        pages["kb"].append(kb)
        if ps3:
            p = glyph_page(read(os.path.join(ps3, ps3name)))
            write_atomic(os.path.join(out, "glyphs", f"ps3_{tag}.bin"), p)
            pages["ps3"].append(p)
    log("Button-symbol pages: Xbox, keyboard" + (", PS3" if ps3 else ""))
    return pages


def cmd_build(a):
    check_game(a.game)
    xex_sha = sha1_file(os.path.join(a.game, "default.xex"))
    if xex_sha != XEX_SHA1:
        log(f"WARNING: default.xex SHA-1 {xex_sha} is not the supported EUR version.")
    ps3 = a.ps3 if a.ps3 and os.path.isdir(a.ps3) else None
    if ps3:
        need = [n for n, *_ in HUD_ARCHIVES] + [n for *_, n in FONTS]
        miss = [n for n in need if not os.path.isfile(os.path.join(ps3, n))]
        if miss:
            log("PS3 files incomplete (" + ", ".join(miss) + ") - PS3 symbols are skipped.")
            ps3 = None
    if ps3:
        xbox_font = os.path.join(a.game, FONTS[0][1])
        if glyph_page(read(os.path.join(ps3, "fontHD.pac"))) == glyph_page(read(xbox_font)):
            log("The PS3 files are copies of the Xbox files - PS3 symbols are skipped.")
            ps3 = None
    t0 = time.time()
    pages = make_pages(a.game, ps3, a.out)
    recipe = None
    if not ps3:
        with open(RECIPE, encoding="utf-8") as f:
            recipe = json.load(f)
    jobs = []
    with ProcessPoolExecutor(max_workers=max(1, a.jobs)) as ex:
        for name, rel, kbname, ps3name, _ in HUD_ARCHIVES:
            xbox_pac = os.path.join(a.game, rel)
            if ps3:
                ps3_pac = os.path.join(ps3, name)
                leaves = None
            else:
                ps3_pac = None
                arch = recipe["archives"].get(name)
                if arch is None:
                    log(f"{name}: not in recipe, skipped")
                    continue
                if arch["sha1"] != sha1_file(xbox_pac):
                    log(f"{name}: differs from the supported version - keyboard HUD is built from matching images only")
                leaves = arch["leaves"]
            jobs.append(ex.submit(_job_keyboard_hud, xbox_pac, ps3_pac, leaves,
                                  {"xbox": pages["xbox"], "kb": pages["kb"]},
                                  os.path.join(a.out, "kbhud", kbname)))
            if ps3:
                jobs.append(ex.submit(_job_ps3_hud, xbox_pac, os.path.join(ps3, name),
                                      os.path.join(a.out, "ps3hud", ps3name)))
        for j in jobs:
            log(j.result())
    log(f"Assets finished in {time.time() - t0:.0f} s -> {a.out}")
    return 0


def cmd_recipe(a):
    check_game(a.game)
    pages = make_pages(a.game, None, os.path.join(a.out + ".tmpdir"))
    result = {
        "version": 1,
        "about": "Positions and pixel masks of controller buttons inside the HUD images of "
                 "WWE Legends of WrestleMania (Xbox 360, EUR). Contains no image data.",
        "xex_sha1": sha1_file(os.path.join(a.game, "default.xex")),
        "archives": {},
    }
    with ProcessPoolExecutor(max_workers=max(1, a.jobs)) as ex:
        futs = [ex.submit(_job_recipe, os.path.join(a.game, rel), os.path.join(a.ps3, name),
                          {"xbox": pages["xbox"], "kb": pages["kb"]}, name)
                for name, rel, *_ in HUD_ARCHIVES]
        for f in futs:
            name, sha, leaves = f.result()
            result["archives"][name] = {"sha1": sha, "leaves": leaves}
            log(f"{name}: {len(leaves)} images")
    os.makedirs(os.path.dirname(os.path.abspath(a.out)), exist_ok=True)
    with open(a.out, "w", encoding="utf-8") as f:
        json.dump(result, f, separators=(",", ":"))
    import shutil
    shutil.rmtree(a.out + ".tmpdir", ignore_errors=True)
    log(f"Recipe written: {a.out}")
    return 0


def cmd_icon(a):
    from PIL import Image
    img = Image.open(a.png).convert("RGBA")
    # square canvas, image centred
    side = max(img.size)
    canvas = Image.new("RGBA", (side, side), (0, 0, 0, 0))
    canvas.paste(img, ((side - img.width) // 2, (side - img.height) // 2))
    os.makedirs(os.path.dirname(os.path.abspath(a.out)), exist_ok=True)
    canvas.save(a.out, format="ICO", sizes=[(256, 256), (128, 128), (64, 64), (48, 48), (32, 32), (24, 24), (16, 16)])
    log(f"Icon written: {a.out}")
    return 0


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    b = sub.add_parser("build")
    b.add_argument("--game", required=True)
    b.add_argument("--out", required=True)
    b.add_argument("--ps3")
    b.add_argument("--jobs", type=int, default=min(8, os.cpu_count() or 2))
    r = sub.add_parser("recipe")
    r.add_argument("--game", required=True)
    r.add_argument("--ps3", required=True)
    r.add_argument("--out", required=True)
    r.add_argument("--jobs", type=int, default=min(8, os.cpu_count() or 2))
    i = sub.add_parser("icon")
    i.add_argument("--png", required=True)
    i.add_argument("--out", required=True)
    a = ap.parse_args()
    return {"build": cmd_build, "recipe": cmd_recipe, "icon": cmd_icon}[a.cmd](a)


if __name__ == "__main__":
    sys.exit(main())
