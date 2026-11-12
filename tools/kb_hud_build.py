#!/usr/bin/env python3
"""LoWRecomp - Tastatur-Fassung von Schrift, Match-HUD und Ladebildschirm.

Aufruf:
  kb_hud_build.py page   <xbox_page.bin> <fontXD.pac> <ausgabe.bin>  (Symbolseite, DXT5-Rohdaten)
  kb_hud_build.py hud    <xbox.pac> <ps3.pac> <ausgabe.pac>     (game2d / Load)

Die Stellen mit Knopfsymbolen in den HUD-Bildern ergeben sich aus dem Vergleich
Xbox <-> PS3 (dort unterscheiden sich die Bilder). Jeder Knopf wird an seiner
Farbe erkannt (blau X, gelb Y, rot B, gruen A, grau = Stick) und durch die
passende Tastenkappe (kb_glyphs) ersetzt. Die Archive werden wie bei
ps3_hud_merge.py mit exakt gleicher Aufteilung neu gebaut (eigener BPE-Packer
in den Grenzen des Originals).
"""
import base64
import colorsys
import hashlib
import io
import json
import zlib
import struct
import sys

import numpy as np
from PIL import Image
from scipy import ndimage

import kb_glyphs as K
import ps3_hud_merge as M

PAGE_BYTES = 256 * 512


# ---------------------------------------------------------------------------
# DDS (DXT5) lesen/schreiben mit Original-Kopf
# ---------------------------------------------------------------------------
def dds_image(b):
    return Image.open(io.BytesIO(b)).convert('RGBA')


def dds_encode_like(orig, img):
    """img als DXT5 im Format von orig (Kopf unveraendert, Mips neu)."""
    h, w = struct.unpack_from('<II', orig, 12)
    mips = max(1, struct.unpack_from('<I', orig, 28)[0])
    assert orig[84:88] == b'DXT5' and img.size == (w, h)
    data = bytearray()
    level = img
    for m in range(mips):
        buf = io.BytesIO()
        level.save(buf, 'DDS', pixel_format='DXT5')
        data += buf.getvalue()[128:]
        level = level.resize((max(1, level.size[0] // 2), max(1, level.size[1] // 2)), Image.LANCZOS)
    out = orig[:128] + bytes(data)
    assert len(out) == len(orig), (len(out), len(orig))
    return out


# ---------------------------------------------------------------------------
# Knoepfe in HUD-Bildern ersetzen
# ---------------------------------------------------------------------------
def classify(rgba):
    px = rgba.reshape(-1, 4)
    px = px[px[:, 3] > 128][:, :3] / 255.0
    if len(px) == 0:
        return None
    hsv = np.array([colorsys.rgb_to_hsv(*p) for p in px[::max(1, len(px) // 400)]])
    sat = hsv[:, 1] > 0.45
    if sat.mean() < 0.15:
        return 'LS'  # grauer Stick-Knopf
    hue = np.median(hsv[sat, 0]) * 360
    if hue < 20 or hue > 330:
        return 'B'
    if hue < 70:
        return 'Y'
    if hue < 170:
        return 'A'
    return 'X'


def _hue_of(region):
    px = region.reshape(-1, 4)
    px = px[px[:, 3] > 128][:, :3] / 255.0
    hsv = np.array([colorsys.rgb_to_hsv(*p) for p in px[::max(1, len(px) // 400)]])
    sat = hsv[:, 1] > 0.45
    hue = np.median(hsv[sat, 0]) * 360 if sat.any() else None
    if hue is None:
        return None
    if hue < 20 or hue > 330:
        return 'B'
    if hue < 70:
        return 'Y'
    if hue < 170:
        return 'A'
    return 'X'


def find_jobs(x_img, p_img):
    """Knoepfe finden: Bereiche, in denen Xbox und PS3 sich unterscheiden, darin
    die farbigen Flaechen (Knopf). Text (weiss/grau) bleibt unangetastet.
    Ergebnis: [(knopf, a0, b0, a1, b1, maske)] - maske (bool, b1-b0 x a1-a0) sind
    die Pixel des Xbox-Knopfs, die vor dem Einsetzen der Tastenkappe geloescht werden."""
    xa = np.array(x_img).astype(int)
    pa = np.array(p_img).astype(int)
    diff = (np.abs(xa - pa).max(axis=2) > 40) & ((xa[:, :, 3] > 24) | (pa[:, :, 3] > 24))
    diff = ndimage.binary_dilation(diff, iterations=2)
    # farbige (gesaettigte) Pixel des Xbox-Bilds
    rgb = xa[:, :, :3].astype(float) / 255.0
    mx, mn = rgb.max(axis=2), rgb.min(axis=2)
    sat = np.where(mx > 0, (mx - mn) / np.maximum(mx, 1e-6), 0)
    colored = (sat > 0.45) & (mx > 0.25) & (xa[:, :, 3] > 128) & diff
    lab, n = ndimage.label(ndimage.binary_dilation(colored, iterations=1))
    out = np.array(x_img).copy()
    H, W = out.shape[:2]
    jobs = []
    for sl in ndimage.find_objects(lab):
        y0, y1, x0, x1 = sl[0].start, sl[0].stop, sl[1].start, sl[1].stop
        if (y1 - y0) * (x1 - x0) < 30:
            continue
        btn = _hue_of(xa[y0:y1, x0:x1].astype(np.uint8))
        if btn is None:
            continue
        # Quadrat um die Farbflaeche inkl. dunklem Rand des Knopfs
        size = int(max(y1 - y0, x1 - x0) * 1.12) + 2
        cx, cy = (x0 + x1) / 2, (y0 + y1) / 2
        a0, b0 = int(round(cx - size / 2)), int(round(cy - size / 2))
        jobs.append((btn, max(0, a0), max(0, b0), min(W, a0 + size), min(H, b0 + size)))
    if not jobs and n == 0:
        # grauer Stick-Knopf (z. B. grosses "LS"): ganzer Unterschied, nahezu quadratisch
        dl, dn = ndimage.label(diff)
        for sl in ndimage.find_objects(dl):
            y0, y1, x0, x1 = sl[0].start, sl[0].stop, sl[1].start, sl[1].stop
            w, h = x1 - x0, y1 - y0
            if min(w, h) >= 40 and 0.75 < w / h < 1.33:
                jobs.append(('LS', x0, y0, x1, y1))
    # verschachtelte Treffer (z. B. Innenflaeche des Buchstabens) verwerfen
    jobs.sort(key=lambda j: -(j[3] - j[1]) * (j[4] - j[2]))
    kept = []
    for j in jobs:
        cx, cy = (j[1] + j[3]) / 2, (j[2] + j[4]) / 2
        if any(k[1] <= cx <= k[3] and k[2] <= cy <= k[4] for k in kept):
            continue
        kept.append(j)
    # Xbox-Knopf entfernen: im Quadrat alle Pixel, die sich von PS3 unterscheiden
    # (auch schwache Kanten); gemeinsamer Text/Hintergrund bleibt.
    weak = np.abs(xa - pa).max(axis=2) > 8
    return [(btn, a0, b0, a1, b1, (weak[b0:b1, a0:a1] | diff[b0:b1, a0:a1]).copy())
            for btn, a0, b0, a1, b1 in kept]


def apply_jobs(x_img, jobs):
    """Tastenkappen einsetzen (jobs aus find_jobs oder aus dem Rezept)."""
    out = np.array(x_img).copy()
    used = []
    for btn, a0, b0, a1, b1, m in jobs:
        out[b0:b1, a0:a1][m] = 0
    img = Image.fromarray(out.astype(np.uint8), 'RGBA')
    for btn, a0, b0, a1, b1, _ in jobs:
        w, h = a1 - a0, b1 - b0
        icon = K.symbol_image(btn, w + 4, h + 4)
        img.alpha_composite(icon, (a0 + (w - icon.size[0]) // 2, b0 + (h - icon.size[1]) // 2))
        used.append(btn)
    return img, used


def keyboardize(x_img, p_img):
    return apply_jobs(x_img, find_jobs(x_img, p_img))


# ---------------------------------------------------------------------------
# Archiv neu bauen: Blaetter ersetzen (gleiche Laenge), BPE neu packen
# ---------------------------------------------------------------------------
class Stats:
    def __init__(self):
        self.replaced = 0
        self.repacked = 0
        self.squeezed = 0
        self.no_room = []


def rebuild(x, path, repl, stats):
    if not any(k[:len(path)] == path for k in repl):
        return x
    if x[:4] == b'PACH':
        kids = M.pach_parse(x)
        new = [rebuild(b, path + (i,), repl, stats) for i, b in kids]
        if all(n is o for n, (_, o) in zip(new, kids)):
            return x
        if all(len(n) == len(o) for n, (_, o) in zip(new, kids)):
            out = bytearray(x)
            cnt = len(kids)
            base = 8 + 12 * cnt
            for k, (n, (_, o)) in enumerate(zip(new, kids)):
                if n is o:
                    continue
                off = struct.unpack_from('<III', x, 8 + 12 * k)[1]
                out[base + off:base + off + len(n)] = n
            return bytes(out)
        built = M.pach_build([(i, n) for (i, _), n in zip(kids, new)], x)
        if len(built) > len(x):
            for k, (n, (_, o)) in enumerate(zip(new, kids)):
                if n is o and o[:4] == b'BPE ':
                    rb = M.bpe_block(M.bpe_inner(o))
                    if rb is not None and len(rb) < len(o):
                        new[k] = rb
                        stats.squeezed += 1
        while True:
            built = M.pach_build([(i, n) for (i, _), n in zip(kids, new)], x)
            if len(built) <= len(x):
                return built + bytes(len(x) - len(built))
            grow = [(len(n) - len(o), k) for k, (n, (_, o)) in enumerate(zip(new, kids))
                    if n is not o and len(n) > len(o)]
            _, k = max(grow)
            stats.no_room.append(path + (kids[k][0],))
            new[k] = kids[k][1]
    if x[:4] == b'BPE ':
        inner = M.bpe_inner(x)
        ni = rebuild(inner, path + ('bpe',), repl, stats)
        if ni is inner:
            return x
        assert len(ni) == len(inner)
        blob = M.bpe_block(ni)
        assert blob is not None
        stats.repacked += 1
        return blob
    if path in repl:
        nb = repl[path]
        assert len(nb) == len(x)
        stats.replaced += 1
        return nb
    return x


def all_leaves(d):
    res = {}
    group = None
    seen = {}
    for name, a, s in M.epac_records(d):
        if s == 0:
            group = name
            continue
        k = (group, name, seen.get((group, name), 0))
        seen[(group, name)] = k[2] + 1
        for p, b in M.flat(M.entry_bytes(d, a, s)).items():
            res[k + p] = b
    return res


def build_hud(xd, pd, xbox_pages, kb_pages, recipe=None, record=None):
    """Tastatur-Fassung eines HUD-Archivs.
    pd     : PS3-Fassung desselben Archivs (Knopfstellen per Vergleich) oder None
    recipe : ohne PS3: Knopfstellen aus dem Rezept {schluessel: eintrag} (recipe_load)
    record : dict, in das die gefundenen Knopfstellen geschrieben werden (Rezept bauen)"""
    fx = all_leaves(xd)
    fp = all_leaves(pd) if pd is not None else None
    repl = {}
    report = {}
    for k, b in fx.items():
        if k[0] == b'GJ2D' or b[:4] != b'DDS ':
            continue
        page = b[128:128 + PAGE_BYTES]
        hit = [i for i, xp in enumerate(xbox_pages) if xp == page]
        if hit and len(b) == 128 + PAGE_BYTES:
            repl[k] = b[:128] + kb_pages[hit[0]]
            report['Symbolseite'] = report.get('Symbolseite', 0) + 1
            continue
        if fp is not None:
            if k not in fp or fp[k] == b or len(fp[k]) != len(b):
                continue
            jobs = find_jobs(dds_image(b), dds_image(fp[k]))
            if record is not None and jobs:
                record[k] = (hashlib.sha1(b).hexdigest(), jobs)
        else:
            entry = (recipe or {}).get(k)
            if entry is None:
                continue
            if entry[0] != hashlib.sha1(b).hexdigest():
                report['abweichend'] = report.get('abweichend', 0) + 1
                continue
            jobs = entry[1]
        img, used = apply_jobs(dds_image(b), jobs)
        if not used:
            continue
        repl[k] = dds_encode_like(b, img)
        for u in used:
            report[u] = report.get(u, 0) + 1
    # Ausgabe mit exakt gleicher Aufteilung
    out = bytearray(xd)
    stats = Stats()
    group = None
    seen = {}
    for name, a, s in M.epac_records(xd):
        if s == 0:
            group = name
            continue
        k = (group, name, seen.get((group, name), 0))
        seen[(group, name)] = k[2] + 1
        xb = M.entry_bytes(xd, a, s)
        nb = rebuild(xb, k, {kk: v for kk, v in repl.items() if kk[:3] == k}, stats)
        if nb is xb:
            continue
        assert len(nb) == len(xb)
        o = 0x4000 + a * 0x800
        out[o:o + len(nb)] = nb
    return bytes(out), repl, stats, report


def verify(xd, md, repl):
    fx, fm = all_leaves(xd), all_leaves(md)
    assert fx.keys() == fm.keys()
    for k, b in fm.items():
        assert b == fx[k] or (k in repl and b == repl[k]), k
    def check(b):
        if b[:4] == b'PACH':
            for _, c in M.pach_parse(b):
                check(c)
        elif b[:4] == b'BPE ':
            cs = struct.unpack_from('<I', b, 8)[0]
            assert M.bpe_blocks_ok(b[16:16 + cs])
    for name, a, s in M.epac_records(md):
        if s:
            check(M.entry_bytes(md, a, s))
    assert md[:0x4000] == xd[:0x4000] and len(md) == len(xd)


def page_from_bin(raw):
    hdr = bytearray(128)
    hdr[0:4] = b'DDS '
    struct.pack_into('<IIIIIII', hdr, 4, 124, 0x81007, 512, 256, PAGE_BYTES, 0, 0)
    struct.pack_into('<II', hdr, 76, 32, 4)
    hdr[84:88] = b'DXT5'
    struct.pack_into('<I', hdr, 108, 0x1000)
    return bytes(hdr) + raw


def glyph_rects(font_pac):
    """Zeichentabelle (font000.yfb) aus fontHD/SD.pac: {code: (x, y, w, h)}."""
    d = open(font_pac, 'rb').read()
    rec = [r for r in M.epac_records(d) if r[2]][0]
    e = M.entry_bytes(d, rec[1], rec[2])
    b = dict(M.pach_parse(e))[1]
    p = b.find(bytes.fromhex('00e001e002e0'))
    n = 0
    while struct.unpack_from('<H', b, p + 2 * n)[0] >= 0xE000:
        n += 1
    codes = [struct.unpack_from('<H', b, p + 2 * i)[0] for i in range(n)]
    q = p + 2 * n
    return {c: struct.unpack_from('<8H', b, q + 16 * i)[2:6] for i, c in enumerate(codes)}


def keyboard_page_bin(xbox_raw, rects=None):
    dds = page_from_bin(xbox_raw)
    img = K.keyboard_page(dds_image(dds), rects)
    return dds_encode_like(dds, img)[128:]


# ---------------------------------------------------------------------------
# Rezept: Knopfstellen ohne PS3-Daten (nur Koordinaten und Masken, keine Grafik)
# ---------------------------------------------------------------------------
def _key_to_json(k):
    return [('x:' + v.hex()) if isinstance(v, bytes) else (('s:' + v) if isinstance(v, str) else v)
            for v in k]


def _key_from_json(j):
    out = []
    for v in j:
        if isinstance(v, str) and v.startswith('x:'):
            out.append(bytes.fromhex(v[2:]))
        elif isinstance(v, str) and v.startswith('s:'):
            out.append(v[2:])
        else:
            out.append(v)
    return tuple(out)


def recipe_dump(record):
    """record {schluessel: (sha1, jobs)} -> JSON-taugliche Liste"""
    leaves = []
    for k, (sha, jobs) in sorted(record.items(), key=lambda kv: repr(kv[0])):
        js = []
        for btn, a0, b0, a1, b1, m in jobs:
            bits = np.packbits(m.astype(np.uint8), axis=None).tobytes()
            js.append([btn, int(a0), int(b0), int(a1), int(b1),
                       base64.b64encode(zlib.compress(bits, 9)).decode('ascii')])
        leaves.append({'key': _key_to_json(k), 'sha1': sha, 'jobs': js})
    return leaves


def recipe_load(leaves):
    res = {}
    for e in leaves:
        jobs = []
        for btn, a0, b0, a1, b1, packed in e['jobs']:
            n = (b1 - b0) * (a1 - a0)
            bits = np.frombuffer(zlib.decompress(base64.b64decode(packed)), np.uint8)
            m = np.unpackbits(bits)[:n].astype(bool).reshape(b1 - b0, a1 - a0)
            jobs.append((btn, a0, b0, a1, b1, m))
        res[_key_from_json(e['key'])] = (e['sha1'], jobs)
    return res


def main():
    sys.setrecursionlimit(10000)
    if sys.argv[1] == 'page':  # page <xbox_page.bin> <font.pac> <ausgabe.bin>
        raw = open(sys.argv[2], 'rb').read()
        rects = K.rects_from_table(glyph_rects(sys.argv[3]))
        open(sys.argv[4], 'wb').write(keyboard_page_bin(raw, rects))
        return 0
    if sys.argv[1] == 'hud':
        xd = open(sys.argv[2], 'rb').read()
        pd = open(sys.argv[3], 'rb').read()
        xbox_pages = [open(sys.argv[i], 'rb').read() for i in (5, 6)]
        kb_pages = [open(sys.argv[i], 'rb').read() for i in (7, 8)]
        md, repl, st, rep = build_hud(xd, pd, xbox_pages, kb_pages)
        verify(xd, md, repl)
        open(sys.argv[4], 'wb').write(md)
        print(f"{sys.argv[4]}: {len(repl)} Bilder ersetzt {rep}; BPE neu gepackt {st.repacked}, "
              f"Nachbarn dichter {st.squeezed}, kein Platz {len(st.no_room)}")
        for k in st.no_room:
            print("  kein Platz (Xbox):", k)
        return 0
    print(__doc__)
    return 2


if __name__ == '__main__':
    sys.exit(main())
