#!/usr/bin/env python3
"""LoWRecomp - PS3-HUD-Grafiken in die Xbox-360-Archive uebernehmen.

Erzeugt aus einem Xbox-360-Archiv (EPAC) und dem gleichnamigen Archiv der
PS3-Version eine Mischfassung: Aufbau und alle Eintraege der Xbox-Fassung,
aber Grafiken (und die zu ihnen gehoerenden Tabellen) aus der PS3-Fassung,
wo sie sich unterscheiden - z. B. Knopfsymbole im Match-HUD.

Format (Yuke's, aus den Dateien abgeleitet):
  EPAC   0x00 'EPAC', u32 Tabellenlaenge (Byte), u32 Datengroesse, u32 7
         0x800 Tabelle aus 12-Byte-Saetzen (name, offset, size):
               size==0 -> Gruppenname, sonst Eintrag: Daten bei
               0x4000 + offset*0x800, Laenge size*0x100 (aufgerundet).
         Eintraege liegen 0x800-ausgerichtet hintereinander, danach 0x800 Byte
         Kennung des Packwerkzeugs ("EOP5/plugin version 1.25").
  PACH   'PACH', u32 n, n x (u32 id, u32 offset, u32 size), Daten ab 8+12n.
  BPE    'BPE ', u32 0x100, u32 gepackt, u32 entpackt, Daten (Gage-BPE,
         Bloecke mit Paartabelle + 16-Bit-Laenge LE).

Die Mischfassung hat exakt die Aufteilung der Xbox-Datei (gleiche Dateigroesse,
gleiche EPAC-/PACH-Offsets): Das Spiel legt Speicher passend zu den Original-
Eintraegen an; eine um 256 Byte groessere Fassung brachte es zum Absturz.
Innerhalb eines PACH werden die Kinder dicht neu angeordnet (Offsets in der
Tabelle), der PACH selbst behaelt seine Laenge. Groessere PS3-Bloecke werden
mit dem eigenen BPE-Packer neu gepackt; was trotzdem nicht passt, bleibt Xbox.

Regeln beim Mischen (Knoten fuer Knoten, ab der Wurzel):
  * gleiche Bytes -> Xbox.
  * beide PACH -> Kinder einzeln mischen; Offsets bleiben, nur Groessen sinken.
  * beide BPE -> entpacken; sind Aufbau (alle Pfade) gleich und haben alle
    abweichenden Blaetter dieselbe Laenge, wird der PS3-Block 1:1 uebernommen,
    sonst bleibt der Xbox-Block (z. B. japanische Eintraege, die es nur auf
    der Xbox gibt).
  * Blaetter in einem PACH: nur gemeinsam - unterscheiden sich mehrere Kinder,
    werden alle uebernommen, wenn alle gleich lang sind (Schrift-Tabelle und
    zugehoerige Bilder bleiben zusammen), sonst keines.

Aufruf:  ps3_hud_merge.py <xbox.pac> <ps3.pac> <ausgabe.pac>
"""
import struct
import sys


# ---------------------------------------------------------------------------
# BPE (Philip Gage), Bloecke mit 16-Bit-Laenge Little Endian
# ---------------------------------------------------------------------------
def bpe_expand(src, out_size):
    out = bytearray()
    p = 0
    n = len(src)
    while p < n and len(out) < out_size:
        left = list(range(256))
        right = [0] * 256
        c = 0
        while True:
            count = src[p]; p += 1
            if count > 127:
                c += count - 127
                count = 0
            if c == 256:
                break
            for _ in range(count + 1):
                left[c] = src[p]; p += 1
                if left[c] != c:
                    right[c] = src[p]; p += 1
                c += 1
            if c == 256:
                break
        size = src[p] | src[p + 1] << 8; p += 2
        end = p + size
        stack = []
        while p < end or stack:
            if stack:
                c = stack.pop()
            else:
                c = src[p]; p += 1
            if c == left[c]:
                out.append(c)
            else:
                stack.append(right[c]); stack.append(left[c])
    if len(out) != out_size:
        raise ValueError("BPE: Laenge stimmt nicht")
    return bytes(out)


def bpe_inner(blob):
    cs, us = struct.unpack_from('<II', blob, 8)
    return bpe_expand(blob[16:16 + cs], us)


# Packer (Gage: Block bis blocksize Byte / maxchars Zeichen, haeufigstes Paar
# durch freien Code ersetzen, solange es mindestens threshold-mal vorkommt).
BPE_MAX_DEPTH = 16


def _bpe_one_block(data, pos, blocksize, maxchars, threshold):
    """Packt einen Block ab pos (Gage). Liefert (bytes, neue Position)."""
    n = len(data)
    used = set()
    end = pos
    while end < n and end - pos < blocksize:
        c = data[end]
        if c not in used:
            if len(used) >= maxchars:
                break
            used.add(c)
        end += 1
    buf = list(data[pos:end])
    left = list(range(256))
    right = [0] * 256
    # Stapeltiefe beim Entpacken: Gage legt fuer ausstehende rechte Haelften
    # einen Stapel fester Groesse an; das Original bleibt bei hoechstens 16.
    depth = [0] * 256
    unused = [c for c in range(255, -1, -1) if c not in used]
    while unused:
        counts = {}
        for i in range(len(buf) - 1):
            k = (buf[i], buf[i + 1])
            counts[k] = counts.get(k, 0) + 1
        best = None
        for (a, b), cnt in sorted(counts.items(), key=lambda kv: -kv[1]):
            if cnt < threshold:
                break
            if max(depth[a] + 1, depth[b]) <= BPE_MAX_DEPTH:
                best = (a, b)
                break
        if best is None:
            break
        code = unused.pop(0)
        a, b = best
        left[code] = a
        right[code] = b
        depth[code] = max(depth[a] + 1, depth[b])
        nb = []
        i = 0
        while i < len(buf):
            if i + 1 < len(buf) and buf[i] == a and buf[i + 1] == b:
                nb.append(code); i += 2
            else:
                nb.append(buf[i]); i += 1
        buf = nb
    out = bytearray()
    c = 0
    while c < 256:
        if c == left[c]:
            ln = 1; c += 1
            while ln < 127 and c < 256 and c == left[c]:
                ln += 1; c += 1
            out.append(ln + 127); ln = 0
            if c == 256:
                break
        else:
            ln = 0; c += 1
            while (ln < 127 and c < 256 and c != left[c]) or (ln < 125 and c < 254 and c + 1 != left[c + 1]):
                ln += 1; c += 1
            out.append(ln); c -= ln + 1
        for _ in range(ln + 1):
            out.append(left[c])
            if c != left[c]:
                out.append(right[c])
            c += 1
    sz = len(buf)
    out.append(sz & 0xFF); out.append(sz >> 8)
    out += bytes(buf)
    return bytes(out), end


def bpe_compress(data, blocksize=4000, maxchars=200, threshold=3):
    out = bytearray()
    pos = 0
    while pos < len(data):
        blk, pos = _bpe_one_block(data, pos, blocksize, maxchars, threshold)
        out += blk
    return bytes(out)


_VARIANTS = [(mc, th) for mc in (256, 230, 200, 170) for th in (2, 3)]


def bpe_compress_best(data):
    """Blockweise die beste Einstellung (Bytes je Ausgabebyte) waehlen;
    Bloecke hoechstens 4000 Byte wie beim Original-Packer."""
    out = bytearray()
    pos = 0
    while pos < len(data):
        best = None
        for mc, th in _VARIANTS:
            blk, end = _bpe_one_block(data, pos, 4000, mc, th)
            if not bpe_blocks_ok(blk):  # gepackt < 2048, entpackt <= 4000
                continue
            score = (end - pos) / len(blk)
            if best is None or score > best[0]:
                best = (score, blk, end)
        out += best[1]
        pos = best[2]
    return bytes(out)


def bpe_blocks_ok(src, max_unpacked=4000, max_packed=2047):
    """Prueft die Blockgrenzen eines BPE-Datenstroms (ohne Kopf)."""
    p = 0
    n = len(src)
    while p < n:
        left = list(range(256))
        right = [0] * 256
        c = 0
        while True:
            count = src[p]; p += 1
            if count > 127:
                c += count - 127
                count = 0
            if c == 256:
                break
            for _ in range(count + 1):
                left[c] = src[p]; p += 1
                if left[c] != c:
                    right[c] = src[p]; p += 1
                c += 1
            if c == 256:
                break
        size = src[p] | src[p + 1] << 8; p += 2
        if size > max_packed:
            return False
        out = 0
        stack = []
        q = p
        end = p + size
        while q < end or stack:
            if stack:
                cc = stack.pop()
            else:
                cc = src[q]; q += 1
            if cc == left[cc]:
                out += 1
            else:
                stack.append(right[cc]); stack.append(left[cc])
        if out > max_unpacked:
            return False
        # Stapeltiefe (ausstehende rechte Haelften) wie beim Entpacken
        need = {}
        def dep(cc):
            if cc == left[cc]:
                return 0
            if cc not in need:
                need[cc] = max(dep(left[cc]) + 1, dep(right[cc]))
            return need[cc]
        if any(dep(cc) > BPE_MAX_DEPTH for cc in set(src[p:end])):
            return False
        p = end
    return True


def bpe_block(inner, limit=None):
    """BPE-Block (mit Kopf) fuer inner in den Grenzen des Original-Packers."""
    c = bpe_compress_best(inner)
    if bpe_expand(c, len(inner)) != inner or not bpe_blocks_ok(c):
        return None
    blob = b'BPE ' + struct.pack('<III', 0x100, len(c), len(inner)) + c
    if limit is not None and len(blob) > limit:
        return blob  # Aufrufer entscheidet
    return blob


# ---------------------------------------------------------------------------
# PACH
# ---------------------------------------------------------------------------
def pach_parse(b):
    if b[:4] != b'PACH':
        return None
    cnt = struct.unpack_from('<I', b, 4)[0]
    base = 8 + 12 * cnt
    kids = []
    for k in range(cnt):
        i, a, s = struct.unpack_from('<III', b, 8 + 12 * k)
        kids.append((i, b[base + a:base + a + s]))
    return kids


def pach_build(kids, template):
    """Neu bauen; Ausrichtung je Eintrag wie im Original (template), mind. 4."""
    cnt = len(kids)
    tcnt = struct.unpack_from('<I', template, 4)[0]
    t_offs = [struct.unpack_from('<III', template, 8 + 12 * k)[1] for k in range(tcnt)]

    def align_of(off):
        for al in (16, 8, 4):
            if off % al == 0:
                return al
        return 4

    table = bytearray(b'PACH' + struct.pack('<I', cnt))
    data = bytearray()
    for k, (i, blob) in enumerate(kids):
        al = align_of(t_offs[k]) if k < len(t_offs) else 16
        while len(data) % al:
            data.append(0)
        table += struct.pack('<III', i, len(data), len(blob))
        data += blob
    while len(data) % 4:
        data.append(0)
    return bytes(table + data)


def flat(b, path=()):
    """Alle Blaetter {pfad: bytes} (PACH und BPE werden geoeffnet)."""
    out = {}
    if b[:4] == b'PACH':
        for i, blob in pach_parse(b):
            out.update(flat(blob, path + (i,)))
    elif b[:4] == b'BPE ':
        out.update(flat(bpe_inner(b), path + ('bpe',)))
    else:
        out[path] = b
    return out


# ---------------------------------------------------------------------------
# Mischen
# ---------------------------------------------------------------------------
class Stats:
    def __init__(self):
        self.bpe_taken = 0
        self.bpe_kept = []
        self.bpe_repacked = 0
        self.bpe_too_big = []
        self.bpe_squeezed = 0
        self.leaf_taken = 0
        self.leaf_kept = []


def merge(x, p, stats, path=()):
    """Neue Bytes fuer Knoten x. PACH-Knoten behalten exakt len(x) (Kinder
    werden darin dicht neu angeordnet); BPE-Bloecke duerfen einzeln wachsen,
    das gleicht der umgebende PACH aus."""
    if x == p:
        return x
    if x[:4] == b'PACH' and p[:4] == b'PACH':
        kx, kp = pach_parse(x), pach_parse(p)
        pmap = dict(kp)
        leaf_diff = [(i, b) for i, b in kx
                     if b[:4] not in (b'PACH', b'BPE ') and i in pmap and pmap[i] != b]
        take_leaves = bool(leaf_diff) and all(
            len(b) == len(pmap[i]) and pmap[i][:4] not in (b'PACH', b'BPE ') for i, b in leaf_diff)
        new = []
        for i, b in kx:
            if i not in pmap or pmap[i] == b:
                new.append(b)
            elif b[:4] in (b'PACH', b'BPE '):
                new.append(merge(b, pmap[i], stats, path + (i,)))
            elif take_leaves:
                stats.leaf_taken += 1
                new.append(pmap[i])
            else:
                stats.leaf_kept.append(path + (i,))
                new.append(b)
        if all(n is o for n, (_, o) in zip(new, kx)):
            return x
        # Passt es nicht: unveraenderte BPE-Nachbarn mit dem eigenen Packer
        # dichter packen (Inhalt identisch), dann ggf. Zuwaechse zuruecknehmen
        built = pach_build([(i, n) for (i, _), n in zip(kx, new)], x)
        if len(built) > len(x):
            for k, (n, (_, o)) in enumerate(zip(new, kx)):
                if n is o and o[:4] == b'BPE ':
                    rb = bpe_block(bpe_inner(o))
                    if rb is not None and len(rb) < len(o):
                        new[k] = rb
                        stats.bpe_squeezed += 1
        while True:
            built = pach_build([(i, n) for (i, _), n in zip(kx, new)], x)
            if len(built) <= len(x):
                return built + bytes(len(x) - len(built))
            grow = [(len(n) - len(o), k) for k, (n, (_, o)) in enumerate(zip(new, kx))
                    if n is not o and len(n) > len(o)]
            g, k = max(grow)
            stats.bpe_too_big.append(path + (kx[k][0],))
            stats.bpe_taken -= 1
            new[k] = kx[k][1]
    if x[:4] == b'BPE ' and p[:4] == b'BPE ':
        inner_p = bpe_inner(p)
        fx, fp = flat(bpe_inner(x)), flat(inner_p)
        if fx.keys() == fp.keys() and all(len(fx[k]) == len(fp[k]) for k in fx):
            stats.bpe_taken += 1
            cs = struct.unpack_from('<I', p, 8)[0]
            if len(p) <= len(x) and bpe_blocks_ok(p[16:16 + cs]):
                return p
            p_ok = bpe_blocks_ok(p[16:16 + cs])
            blob = bpe_block(inner_p, len(x))
            if blob is not None and (not p_ok or len(blob) < len(p)):
                stats.bpe_repacked += 1
                return blob
            if p_ok:
                return p
            stats.bpe_taken -= 1
            stats.bpe_too_big.append(path)
            return x
        stats.bpe_kept.append(path)
        return x
    stats.leaf_kept.append(path)
    return x


# ---------------------------------------------------------------------------
# EPAC
# ---------------------------------------------------------------------------
def epac_records(d):
    if d[:4] != b'EPAC':
        raise ValueError("kein EPAC-Archiv")
    n = struct.unpack_from('<I', d, 4)[0] // 12
    recs = []
    for k in range(n):
        o = 0x800 + k * 12
        name = d[o:o + 4]
        a, s = struct.unpack_from('<II', d, o + 4)
        recs.append([name, a, s])
    return recs


def entry_bytes(d, a, s):
    b = d[0x4000 + a * 0x800:0x4000 + a * 0x800 + s * 0x100]
    if b[:4] == b'PACH':  # echte Laenge (Rest ist Fuellung)
        cnt = struct.unpack_from('<I', b, 4)[0]
        ends = [struct.unpack_from('<III', b, 8 + 12 * k) for k in range(cnt)]
        real = 8 + 12 * cnt + max((o + l for _, o, l in ends), default=0)
        real = (real + 3) & ~3
        return b[:real]
    return b


def merge_epac(xd, pd):
    """Mischfassung mit exakt der Aufteilung der Xbox-Datei (gleiche Groesse,
    gleiche Offsets/Groessen im EPAC). Das Spiel reserviert Speicher passend
    zu den Original-Eintraegen; eine groessere Fassung fuehrte zum Absturz."""
    stats = Stats()
    def keyed(d):
        out = {}
        group = None
        seen = {}
        for name, a, s in epac_records(d):
            if s == 0:
                group = name
                continue
            k = (group, name, seen.get((group, name), 0))
            seen[(group, name)] = k[2] + 1
            out[k] = (a, s)
        return out
    kx, kp = keyed(xd), keyed(pd)
    out = bytearray(xd)
    for k, (a, s) in kx.items():
        if k not in kp:
            continue
        xb = entry_bytes(xd, a, s)
        nb = merge(xb, entry_bytes(pd, *kp[k]), stats, k)
        if nb is xb:
            continue
        assert len(nb) == len(xb)
        o = 0x4000 + a * 0x800
        out[o:o + len(nb)] = nb
    return bytes(out), stats


def verify(xd, pd, md):
    """Jedes Blatt der Mischfassung stammt unveraendert aus Xbox oder PS3."""
    def all_leaves(d):
        res = {}
        group = None
        seen = {}
        for name, a, s in epac_records(d):
            if s == 0:
                group = name
                continue
            k = (group, name, seen.get((group, name), 0))
            seen[(group, name)] = k[2] + 1
            for path, b in flat(entry_bytes(d, a, s)).items():
                res[k + path] = b
        return res
    # Alle BPE-Stroeme der Ausgabe innerhalb der Grenzen des Original-Packers?
    def check_bpe(b):
        if b[:4] == b'PACH':
            for _, c in pach_parse(b):
                check_bpe(c)
        elif b[:4] == b'BPE ':
            cs = struct.unpack_from('<I', b, 8)[0]
            assert bpe_blocks_ok(b[16:16 + cs]), "BPE-Block zu gross"
    for name, a, s in epac_records(md):
        if s:
            check_bpe(entry_bytes(md, a, s))
    fx, fp, fm = all_leaves(xd), all_leaves(pd), all_leaves(md)
    assert fm.keys() == fx.keys(), "Aufbau weicht vom Xbox-Archiv ab"
    from_ps3 = 0
    for k, b in fm.items():
        if b == fx[k]:
            continue
        assert k in fp and b == fp[k], f"Blatt {k} weder Xbox noch PS3"
        from_ps3 += 1
    return len(fm), from_ps3


def main():
    if len(sys.argv) != 4:
        print(__doc__)
        return 2
    xd = open(sys.argv[1], 'rb').read()
    pd = open(sys.argv[2], 'rb').read()
    md, st = merge_epac(xd, pd)
    leaves, from_ps3 = verify(xd, pd, md)
    open(sys.argv[3], 'wb').write(md)
    print(f"{sys.argv[3]}: {len(md)} Byte, {leaves} Blaetter, {from_ps3} aus PS3 "
          f"(BPE-Bloecke uebernommen {st.bpe_taken}, neu gepackt {st.bpe_repacked}, "
          f"zu gross {len(st.bpe_too_big)}, behalten {len(st.bpe_kept)}, "
          f"Xbox-Nachbarn dichter gepackt {st.bpe_squeezed}; "
          f"Blaetter uebernommen {st.leaf_taken}, behalten {len(st.leaf_kept)})")
    for k in st.bpe_kept + st.leaf_kept:
        print("  behalten (Xbox):", k)
    for k in st.bpe_too_big:
        print("  kein Platz (Xbox):", k)
    assert len(md) == len(xd)
    return 0


if __name__ == '__main__':
    sys.exit(main())
