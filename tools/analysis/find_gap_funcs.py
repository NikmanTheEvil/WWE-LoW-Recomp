#!/usr/bin/env python3
"""Findet Funktionseinsprünge ohne .pdata: Zeiger aus Datensektionen, die in
Code-Lücken (Bereiche zwischen .pdata-Funktionen) zeigen und an einer
plausiblen Funktionsgrenze liegen. Gibt TOML-[functions]-Einträge aus."""
import struct, sys, bisect
d = open(sys.argv[1], 'rb').read(); B = 0x82000000
w = lambda a: struct.unpack_from('>I', d, a - B)[0]
CODE = [(0x82090000, 0x82090000 + 0x5C01CC), (0x82650200, 0x82650200 + 0x10804)]
DATA = [(0x82000400, 0x82000400 + 0x6323C), (0x82670000, 0x82670000 + 0x554FF8)]
pd = d[0x63800:0x63800 + 0x246A0]
F = sorted({(s, ((i >> 8) & 0x3FFFFF) * 4) for s, i in
            (struct.unpack_from('>II', pd, k * 8) for k in range(len(pd) // 8)) if s})
starts = {s for s, _ in F}

# Lücken = Code, der von keiner pdata-Funktion abgedeckt ist
gaps = []
for lo, hi in CODE:
    cur = lo
    for s, sz in F:
        if s >= hi or s + sz <= lo: continue
        if s > cur: gaps.append((cur, s))
        cur = max(cur, s + sz)
    if cur < hi: gaps.append((cur, hi))
G0 = [g[0] for g in gaps]
def in_gap(a):
    i = bisect.bisect_right(G0, a) - 1
    return gaps[i] if i >= 0 and gaps[i][0] <= a < gaps[i][1] else None
CLO, CHI = CODE[0][0], CODE[-1][1]

def is_term(x):  # blr, bctr, b (unbedingt, ohne Link), Padding
    return x in (0x4E800020, 0x4E800420, 0) or (x >> 26 == 18 and not x & 1)

cands = {}
for lo, hi in DATA:
    for a in range(lo, hi, 4):
        p = w(a)
        if p & 3 or not (CLO <= p < CHI) or p in starts: continue
        g = in_gap(p)
        if not g: continue
        if p != g[0] and not is_term(w(p - 4)): continue
        if w(p) == 0: continue
        cands.setdefault(p, []).append(a)

# Import-Thunks (je 16 Byte) ausschließen – die verwaltet ReXGlue selbst
thunks = set()
for line in open(sys.argv[2] if len(sys.argv) > 2 and not sys.argv[2].startswith('--') else 'out/imports.txt'):
    a = int(line.split()[0], 16); thunks |= set(range(a, a + 16, 4))
for p in list(cands):
    if p in thunks: del cands[p]

# Sprungziele in Lücken: bl (immer Funktionsaufruf) und b aus einer ANDEREN
# Lücke/Funktion (Tailcall), sofern das Ziel an einer Funktionsgrenze liegt.
for lo, hi in CODE:
    for a in range(lo, hi, 4):
        x = w(a)
        if x >> 26 != 18 or x & 2: continue
        li = x & 0x03FFFFFC
        if li & 0x02000000: li -= 0x04000000
        t = (a + li) & 0xFFFFFFFF
        if t in starts or t in thunks: continue
        g = in_gap(t)
        if not g: continue
        if not (x & 1) and in_gap(a) == g: continue          # Sprung innerhalb derselben Lücke
        if t != g[0] and not is_term(w(t - 4)): continue
        cands.setdefault(t, []).append(a)

# Zur Laufzeit gebildete Adressen: lis rA,hi ; ... ; addi/ori rD,rA,lo
# (Funktionszeiger als Argument, z. B. qsort-Vergleicher, Thread-Einsprünge)
for lo, hi in CODE:
    for a in range(lo, hi, 4):
        x = w(a); op = x >> 26
        if op not in (14, 24): continue
        if op == 14:
            rD, rA, imm = (x >> 21) & 31, (x >> 16) & 31, x & 0xFFFF
            if rA == 0: continue
            if imm & 0x8000: imm -= 0x10000
        else:
            rA, rD, imm = (x >> 21) & 31, (x >> 16) & 31, x & 0xFFFF
        for k in range(1, 97):
            y = w(a - 4 * k)
            if y == 0x4E800020 or (a - 4 * k) in starts and k > 1:   # blr / Funktionsanfang
                if (a - 4 * k) in starts and y >> 26 == 15 and (y >> 21) & 31 == rA and (y >> 16) & 31 == 0:
                    pass
                else:
                    break
            yo = y >> 26
            if yo == 15 and (y >> 16) & 31 == 0 and (y >> 21) & 31 == rA:
                t = (((y & 0xFFFF) << 16) + imm) & 0xFFFFFFFF
                if t & 3 or t in starts or t in thunks: break
                g = in_gap(t)
                # Ziel in derselben Lücke wie der Befehl = Basis einer Inline-Sprungtabelle
                if g and g == in_gap(a): break
                if g and (t == g[0] or is_term(w(t - 4))) and w(t) != 0:
                    cands.setdefault(t, []).append(a)
                break
            # rA wird dazwischen als Ziel beschrieben (D-Form-Befehle mit rD-Feld) -> abbrechen
            if (y >> 21) & 31 == rA and yo in (7, 8, 10, 11, 12, 13, 14, 15, 32, 33, 34, 35, 40, 41, 42, 43, 46, 58):
                break
            # Schreibziel im rA-Feld (ori/oris/xori/andi./rlwinm/rlwimi) bzw. or/mr (op 31)
            if (y >> 16) & 31 == rA and (yo in (20, 21, 23, 24, 25, 26, 27, 28, 29) or
                                          (yo == 31 and ((y >> 1) & 0x3FF) in (444, 28, 316, 60, 124, 476, 412, 24, 536, 792, 824, 922, 954, 986))):
                break
            # bl zerstört volatile Register r3-r12
            if yo == 18 and y & 1 and 3 <= rA <= 12:
                break

# Tailcalls innerhalb derselben Lücke: b, das den Bereich der eigenen
# (Kandidaten-)Funktion verlässt. Iterieren, bis sich nichts mehr ändert.
gap_b = []
for lo, hi in CODE:
    for a in range(lo, hi, 4):
        x = w(a)
        if x >> 26 != 18 or x & 3: continue
        li = x & 0x03FFFFFC
        if li & 0x02000000: li -= 0x04000000
        t = (a + li) & 0xFFFFFFFF
        g = in_gap(a)
        if g and in_gap(t) == g and t not in starts:
            gap_b.append((a, t, g))
while True:
    pts = sorted(set(cands) | {g[0] for g in gaps})
    added = 0
    for a, t, g in gap_b:
        if t in cands or t == g[0]: continue
        i = bisect.bisect_right(pts, a) - 1
        f0 = pts[i]; f1 = pts[i + 1] if i + 1 < len(pts) else g[1]
        f1 = min(f1, g[1])
        if not (f0 <= t < f1) and is_term(w(t - 4)):
            cands.setdefault(t, []).append(a); added += 1
    if not added: break

pts = sorted(cands)
total_gap = sum(g1 - g0 for g0, g1 in gaps)
print(f"# Lücken: {len(gaps)} ({total_gap} Byte), Kandidaten: {len(pts)}", file=sys.stderr)
for i, p in enumerate(pts):
    g = in_gap(p)
    j = bisect.bisect_right(pts, p)
    nxt = pts[j] if j < len(pts) and pts[j] < g[1] else g[1]
    # Ende: bis zum nächsten Kandidaten bzw. Lückenende; Padding hinten abschneiden
    end = nxt
    while end > p + 4 and w(end - 4) == 0: end -= 4
    refs = ", ".join(hex(r) for r in cands[p][:3])
    print(f"0x{p:08X} = {{ end = 0x{end:08X} }}  # Ref. von {refs}{' …' if len(cands[p]) > 3 else ''}")

# ---- Diagnose (nur mit --diag) ----
if '--diag' in sys.argv:
    import collections
    prev = collections.Counter()
    for p in pts:
        x = w(p - 4)
        k = 'blr' if x == 0x4E800020 else 'bctr' if x == 0x4E800420 else 'pad' if x == 0 else 'b' if x >> 26 == 18 else 'gapstart'
        prev[k] += 1
    print('# Vorgänger-Instruktion:', dict(prev), file=sys.stderr)
    # Läufe: aufeinanderfolgende Datenwörter, die alle in DIESELBE Lücke zeigen
    runs = 0; run_pts = set()
    for lo, hi in DATA:
        a = lo
        while a < hi:
            g = in_gap(w(a)) if CLO <= w(a) < CHI else None
            b = a
            while g and b < hi and CLO <= w(b) < CHI and in_gap(w(b)) == g: b += 4
            if g and (b - a) // 4 >= 3:
                runs += 1; run_pts |= {w(c) for c in range(a, b, 4)}
            a = max(b, a + 4)
    print(f'# Läufe >=3 in dieselbe Lücke: {runs}, betroffene Kandidaten: {len(run_pts & set(pts))}', file=sys.stderr)
