#!/usr/bin/env python3
"""LoWRecomp - Tastatur-Symbole im Stil der Controller-Symbole.

Zeichnet Tastenkappen (glaenzend, dunkler Rand, weisse Beschriftung mit
Kontur wie bei den Xbox-Knoepfen) fuer die Standardbelegung von low_input:
  X->Pfeil links  Y->hoch  B->rechts  A->runter (in den Farben der Xbox-Knoepfe)
  LS->WASD  RS->Leertaste+Pfeil  L3->F  R3->V  LB->Shift (Rennen)  RB->Strg
  LT->Q  RT->E  Back->Tab  Start->Esc  Steuerkreuz->1/3/4/5 (oben/rechts/unten/links)
Die Rechtecke stammen aus der Zeichentabelle der Schrift (font000.yfb).
"""
import os

from PIL import Image, ImageDraw, ImageFont, ImageFilter

# Inter (SIL Open Font License 1.1), liegt im Repository unter tools/fonts
FONT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "fonts", "Inter-ExtraBold.otf")
SS = 4  # Ueberabtastung

COLORS = {
    'blue': ((70, 120, 255), (20, 45, 170)),
    'yellow': ((255, 205, 70), (200, 120, 10)),
    'red': ((255, 80, 80), (165, 15, 20)),
    'green': ((110, 220, 100), (25, 120, 30)),
    'dark': ((105, 105, 112), (30, 30, 34)),
    'hot': ((255, 70, 70), (175, 10, 10)),  # hervorgehobene Richtung
}


def _font(px):
    return ImageFont.truetype(FONT, max(4, int(px)))


def _vgrad(w, h, top, bottom):
    g = Image.new('RGBA', (w, h))
    d = ImageDraw.Draw(g)
    for y in range(h):
        t = y / max(1, h - 1)
        c = tuple(int(top[i] + (bottom[i] - top[i]) * t) for i in range(3)) + (255,)
        d.line([(0, y), (w, y)], fill=c)
    return g


def keycap(w, h, label='', color='dark', symbol=None, label_scale=1.0):
    """Eine Tastenkappe w x h (Pixel), Rueckgabe RGBA."""
    W, H = w * SS, h * SS
    img = Image.new('RGBA', (W, H), (0, 0, 0, 0))
    r = int(min(W, H) * 0.22)
    o = max(SS, int(min(W, H) * 0.06))  # Randstaerke
    top, bottom = COLORS[color]
    # dunkler Rand / Schatten
    mask = Image.new('L', (W, H), 0)
    ImageDraw.Draw(mask).rounded_rectangle([0, 0, W - 1, H - 1], r, fill=255)
    img.paste((14, 14, 18, 255), (0, 0), mask)
    # Seitenteil (Tiefe) unten
    side = Image.new('L', (W, H), 0)
    ImageDraw.Draw(side).rounded_rectangle([o, o, W - 1 - o, H - 1 - o], max(1, r - o), fill=255)
    dark = tuple(int(c * 0.55) for c in bottom)
    img.paste(dark + (255,), (0, 0), side)
    # Oberseite
    depth = int(H * 0.12)
    face = Image.new('L', (W, H), 0)
    ImageDraw.Draw(face).rounded_rectangle([o, o, W - 1 - o, H - 1 - o - depth], max(1, r - o), fill=255)
    img.paste(_vgrad(W, H, top, bottom), (0, 0), face)
    # Glanz oben
    gloss = Image.new('L', (W, H), 0)
    gd = ImageDraw.Draw(gloss)
    gd.rounded_rectangle([o + W * 0.08, o + H * 0.05, W - 1 - o - W * 0.08, (H - depth) * 0.48],
                         max(1, r - o), fill=110)
    gloss = gloss.filter(ImageFilter.GaussianBlur(SS * 1.2))
    img.paste((255, 255, 255, 255), (0, 0), Image.composite(gloss, Image.new('L', (W, H), 0), face))
    # Beschriftung
    fh = H - depth - 2 * o
    cx, cy = W / 2, o + fh / 2
    d = ImageDraw.Draw(img)
    stroke = max(SS, int(min(W, H) * 0.045))
    if symbol:
        _symbol(d, symbol, cx, cy, min(W - 2 * o, fh), stroke)
    elif label:
        n = len(label)
        size = fh * (0.78 if n == 1 else 0.52 if n == 2 else 0.42) * label_scale
        f = _font(size)
        while f.getlength(label) > (W - 2 * o) * 0.86 and size > 6:
            size *= 0.92
            f = _font(size)
        d.text((cx, cy), label, font=f, anchor='mm', fill=(255, 255, 255, 255),
               stroke_width=stroke, stroke_fill=(10, 10, 14, 255))
    return img.resize((w, h), Image.LANCZOS)


def _symbol(d, sym, cx, cy, s, stroke):
    white, black = (255, 255, 255, 255), (10, 10, 14, 255)
    if sym in ('up', 'down', 'left', 'right'):
        a = s * 0.32
        pts = {'up': [(cx, cy - a), (cx - a, cy + a * 0.7), (cx + a, cy + a * 0.7)],
               'down': [(cx, cy + a), (cx - a, cy - a * 0.7), (cx + a, cy - a * 0.7)],
               'left': [(cx - a, cy), (cx + a * 0.7, cy - a), (cx + a * 0.7, cy + a)],
               'right': [(cx + a, cy), (cx - a * 0.7, cy - a), (cx - a * 0.7, cy + a)]}[sym]
        d.polygon(pts, fill=white, outline=black, width=stroke)
    elif sym == 'shift':  # hohler Pfeil nach oben
        a = s * 0.36
        pts = [(cx, cy - a), (cx - a, cy), (cx - a * 0.45, cy), (cx - a * 0.45, cy + a),
               (cx + a * 0.45, cy + a), (cx + a * 0.45, cy), (cx + a, cy)]
        d.polygon(pts, fill=white, outline=black, width=stroke)
    elif sym == 'space':  # Leerzeichen-Symbol
        a = s * 0.34
        w = max(2, int(s * 0.11))
        for box in ([cx - a, cy + a * 0.15, cx + a, cy + a * 0.15 + w],
                    [cx - a, cy - a * 0.35, cx - a + w, cy + a * 0.15 + w],
                    [cx + a - w, cy - a * 0.35, cx + a, cy + a * 0.15 + w]):
            d.rectangle([box[0] - stroke, box[1] - stroke, box[2] + stroke, box[3] + stroke], fill=black)
        for box in ([cx - a, cy + a * 0.15, cx + a, cy + a * 0.15 + w],
                    [cx - a, cy - a * 0.35, cx - a + w, cy + a * 0.15 + w],
                    [cx + a - w, cy - a * 0.35, cx + a, cy + a * 0.15 + w]):
            d.rectangle(box, fill=white)


def cluster(w, h, keys, layout):
    """Mehrere kleine Kappen in einem Feld. layout: Liste (spalte, zeile) in
    einem Raster cols x rows; keys: Liste (label/symbol-dict)."""
    img = Image.new('RGBA', (w, h), (0, 0, 0, 0))
    cols = max(c for c, _ in layout) + 1
    rows = max(r for _, r in layout) + 1
    kw = w / cols
    kh = h / rows
    cw = int(min(kw, kh * 1.1))
    ch = int(min(kh, kw * 1.25))
    for (c, r), k in zip(layout, keys):
        cap = keycap(cw, ch, **k)
        x = int(c * kw + (kw - cw) / 2)
        y = int(r * kh + (kh - ch) / 2)
        img.alpha_composite(cap, (max(0, x), max(0, y)))
    return img


def wasd(w, h):
    return cluster(w, h, [dict(label='W'), dict(label='A'), dict(label='S'), dict(label='D')],
                   [(1, 0), (0, 1), (1, 1), (2, 1)])


def arrows(w, h):
    return cluster(w, h, [dict(symbol='up'), dict(symbol='left'), dict(symbol='down'), dict(symbol='right')],
                   [(1, 0), (0, 1), (1, 1), (2, 1)])


def space_arrows(w, h):
    """Leertaste ueber vier Pfeiltasten (rechter Stick = Leertaste + Pfeil)."""
    img = Image.new('RGBA', (w, h), (0, 0, 0, 0))
    sh = int(h * 0.42)
    img.alpha_composite(keycap(int(w * 0.92), sh, symbol='space'), ((w - int(w * 0.92)) // 2, 0))
    kw = w // 4
    kh = min(h - sh - 1, int(kw * 1.15))
    for i, sym in enumerate(('left', 'up', 'down', 'right')):
        img.alpha_composite(keycap(kw, kh, symbol=sym), (i * kw + (w - 4 * kw) // 2, h - kh))
    return img


def dpad(w, h, hot=(), corners=None):
    """Steuerkreuz als Kreuz aus 1 (oben), 5 (links), 3 (rechts), 4 (unten)."""
    keys = []
    for lab, dirn in (('1', 'up'), ('5', 'left'), ('3', 'right'), ('4', 'down')):
        keys.append(dict(label=lab, color='hot' if dirn in hot else 'dark'))
    img = cluster(w, h, keys, [(1, 0), (0, 1), (2, 1), (1, 2)])
    if corners:  # Diagonalen wie im Original: rote Ecken
        d = ImageDraw.Draw(img)
        a = max(4, int(min(w, h) * 0.2))
        for cx, cy, sx, sy in corners:
            x = 0 if cx < 0 else w - 1
            y = 0 if cy < 0 else h - 1
            d.polygon([(x, y), (x - sx * a, y), (x, y - sy * a)], fill=(235, 40, 40, 255),
                      outline=(10, 10, 14, 255))
    return img


# Rechtecke (x, y, w, h) je Symbol laut Zeichentabelle; Diagonalen (ohne
# Tabelleneintrag) an den Stellen der Xbox-Seite.
RECTS = {
    'X': (0, 0, 56, 52), 'Y': (64, 0, 56, 52), 'B': (128, 0, 56, 52), 'A': (192, 0, 56, 52),
    'LS': (0, 56, 56, 52), 'RS': (64, 56, 56, 52), 'R3': (128, 56, 56, 52), 'DPAD': (192, 56, 56, 52),
    'UP': (0, 112, 56, 52), 'DOWN': (64, 112, 56, 52), 'LEFT': (128, 112, 56, 52),
    'RIGHT': (192, 112, 56, 52), 'UPDOWN': (0, 168, 56, 52), 'LEFTRIGHT': (64, 168, 56, 52),
    'DIAGUP': (128, 168, 56, 52), 'DIAGDOWN': (192, 168, 56, 52),
    'LB': (0, 224, 66, 52), 'RB': (76, 224, 66, 52), 'BACK': (152, 224, 82, 52),
    'LT': (0, 280, 66, 52), 'RT': (76, 280, 66, 52), 'START': (152, 280, 82, 52),
    'L3': (66, 336, 56, 52),
}

# Aktionsknoepfe = Pfeiltasten, angeordnet wie auf dem Controller (Farben wie Xbox)
FACE = {'X': ('left', 'blue'), 'Y': ('up', 'yellow'), 'B': ('right', 'red'), 'A': ('down', 'green')}


def symbol_image(role, w, h):
    m = 2  # Rand innerhalb des Rechtecks
    w, h = w - 2 * m, h - 2 * m
    sq = min(w, h)
    if role in FACE:
        sym, col = FACE[role]
        return keycap(sq, sq, color=col, symbol=sym)
    if role == 'LS':
        return wasd(w, h)
    if role == 'RS':  # Quick Grapple: Leertaste + Pfeil
        return space_arrows(w, h)
    if role == 'R3':
        return keycap(sq, sq, 'V')
    if role == 'L3':
        return keycap(sq, sq, 'F')
    if role == 'DPAD':
        return dpad(w, h)
    hot = {'UP': ('up',), 'DOWN': ('down',), 'LEFT': ('left',), 'RIGHT': ('right',),
           'UPDOWN': ('up', 'down'), 'LEFTRIGHT': ('left', 'right')}
    if role in hot:
        return dpad(w, h, hot[role])
    if role == 'DIAGUP':
        return dpad(w, h, ('up',), corners=[(-1, -1, -1, -1), (1, -1, 1, -1)])
    if role == 'DIAGDOWN':
        return dpad(w, h, ('down',), corners=[(-1, 1, -1, 1), (1, 1, 1, 1)])
    if role == 'LB':  # Rennen
        return keycap(int(h * 1.45), h, symbol='shift')
    if role == 'RB':  # Modifikator
        return keycap(int(h * 1.3), h, 'Strg')
    if role == 'LT':
        return keycap(sq, sq, 'Q')
    if role == 'RT':
        return keycap(sq, sq, 'E')
    if role == 'BACK':
        return keycap(int(sq * 1.3), sq, 'Tab')
    if role == 'START':
        return keycap(int(sq * 1.3), sq, 'Esc')
    return None


ROLE_CODES = {
    'X': 0xE00A, 'Y': 0xE007, 'B': 0xE008, 'A': 0xE009, 'LS': 0xE012, 'RS': 0xE011,
    'R3': 0xE010, 'DPAD': 0xE002, 'UP': 0xE003, 'DOWN': 0xE004, 'RIGHT': 0xE005,
    'LEFT': 0xE006, 'UPDOWN': 0xE037, 'LEFTRIGHT': 0xE036, 'LB': 0xE00B, 'RB': 0xE00E,
    'LT': 0xE00C, 'RT': 0xE00F, 'BACK': 0xE001, 'START': 0xE000, 'L3': 0xE00D,
}


def rects_from_table(glyph_rects):
    """glyph_rects: {code: (x, y, w, h)} aus font000.yfb -> RECTS-Form."""
    r = {role: glyph_rects[c] for role, c in ROLE_CODES.items() if c in glyph_rects}
    w, h = r['UP'][2], r['UP'][3]
    r['DIAGUP'] = (128, 168, w, h)
    r['DIAGDOWN'] = (192, 168, w, h)
    return r


def keyboard_page(xbox_page, rects=None):
    """Xbox-Symbolseite (RGBA 256x512) -> Tastatur-Fassung (gleiche Rechtecke)."""
    page = xbox_page.copy()
    for role, (x, y, w, h) in (rects or RECTS).items():
        im = symbol_image(role, w, h)
        if im is None:
            continue
        page.paste(Image.new('RGBA', (w, h), (0, 0, 0, 0)), (x, y))
        page.alpha_composite(im, (x + (w - im.size[0]) // 2, y + (h - im.size[1]) // 2))
    return page


def icon_for(button, w, h):
    """Einzelnes Symbol fuer HUD-Grafiken (z. B. 'B' in 'Klettern: B halten')."""
    return symbol_image(button, w + 4, h + 4)
