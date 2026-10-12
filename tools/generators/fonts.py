"""Builds Deadhunt's bitmap fonts for the Latin languages from the original cp1251 ones.

    python font_build.py <original Data\\Fonts> <out dir> cp1252|cp1250

cp1252 (Spanish, French, German, Italian, Portuguese) -> Data\\FontE, cp1250 (Polish) -> Data\\FontP.
Every accented letter is composed from its plain letter plus a mark taken from the font itself
(the diaeresis of Ё/ё, the apostrophe, ^, ~, the comma, the full stop and the hyphen) and drawn
into a free cell: the letter's own cell when it is wide enough (the Cyrillic glyph there is not
used by these languages), else one of the control-code cells or the empty end of the last row.
The .dat UVs of the code are redirected to the new cell.
"""
import os, struct, sys
from PIL import Image, ImageChops

SRC, DST, ENC = sys.argv[1], sys.argv[2], sys.argv[3]
TEX, CELL_H = 512, 31
K_DIA = {True: (0xC5, 0xA8), False: (0xE5, 0xB8)}    # plain Е/е and Ё/ё (cp1251) -> diaeresis band

# letter -> (base, mark); marks: acute grave circ dia tilde cedil ogonek dot stroke rot
LATIN = {
    "á": ("a", "acute"), "à": ("a", "grave"), "â": ("a", "circ"), "ä": ("a", "dia"), "ã": ("a", "tilde"),
    "é": ("e", "acute"), "è": ("e", "grave"), "ê": ("e", "circ"), "ë": ("e", "dia"),
    "í": ("i", "acute"), "ì": ("i", "grave"), "î": ("i", "circ"), "ï": ("i", "dia"),
    "ó": ("o", "acute"), "ò": ("o", "grave"), "ô": ("o", "circ"), "ö": ("o", "dia"), "õ": ("o", "tilde"),
    "ú": ("u", "acute"), "ù": ("u", "grave"), "û": ("u", "circ"), "ü": ("u", "dia"),
    "ñ": ("n", "tilde"), "ç": ("c", "cedil"), "¿": ("?", "rot"), "¡": ("!", "rot"),
}
POLISH = {
    "ą": ("a", "ogonek"), "ć": ("c", "acute"), "ę": ("e", "ogonek"), "ł": ("l", "stroke"), "ń": ("n", "acute"),
    "ó": ("o", "acute"), "ś": ("s", "acute"), "ź": ("z", "acute"), "ż": ("z", "dot"),
}


def targets():
    table = LATIN if ENC == "cp1252" else POLISH
    out = {}
    for ch, (base, mark) in table.items():
        out[ch] = (base, mark)
        up = ch.upper()
        if up != ch and len(up) == 1:
            out[up] = (base.upper(), mark)
    return [(c.encode(ENC)[0], b, m) for c, (b, m) in out.items()]


def rects(dat):
    return [[round(v * TEX) for v in struct.unpack("<4f", dat[16 + i * 16:32 + i * 16])] for i in range(256)]


def ink_bbox(alpha_cell, thr=40):
    return alpha_cell.point(lambda v: 255 if v > thr else 0).getbbox()


def build(fontname):
    dat = bytearray(open(os.path.join(SRC, fontname + ".dat"), "rb").read())
    R = rects(dat)
    src = {"c": Image.open(os.path.join(SRC, fontname + ".jpg")).convert("RGB"),
           "a": Image.open(os.path.join(SRC, "_A_" + fontname + ".jpg")).convert("L")}
    out = {k: v.copy() for k, v in src.items()}
    A = src["a"]

    def cell(img, code):
        return img.crop(R[code])

    def ink(code, thr=40):
        return ink_bbox(cell(A, code), thr)

    def piece(img, code, size=None, thr=40):
        """Inked part of a glyph, optionally resized."""
        p = cell(img, code).crop(ink(code, thr))
        return p.resize(size, Image.LANCZOS) if size else p

    def band(upper):
        """Diaeresis rows of Ё/ё above the plain letter: (x0, y0, x1, y1) inside the Ё cell."""
        plain, donor = K_DIA[upper]
        top = ink(plain)[1]
        a = cell(A, donor)
        return ink_bbox(a.crop((0, 0, a.size[0], max(top - 1, 1))), 20)

    def shear(img, m, k):
        w, h = m.size
        W = w + round(k * (h - 1))
        canvas = Image.new(img.mode, (W, h), 0)
        canvas.paste(m, (0, 0))
        return canvas.transform((W, h), Image.AFFINE, (1, k, -k * (h - 1), 0, 1, 0), Image.BILINEAR)

    def mark_img(img, mark, upper, gb):
        """(mark image, x centre offset, top row) for the glyph whose ink box is gb."""
        bb = band(upper)
        bh = bb[3] - bb[1]
        if mark == "dia":
            return cell(img, K_DIA[upper][1]).crop(bb), 0, bb[1]
        if mark in ("acute", "grave"):
            H = bh + (2 if upper else 3)
            ab = ink(ord("'"))
            tick = cell(img, ord("'")).crop(ab).resize((ab[2] - ab[0], H), Image.LANCZOS)
            m = shear(img, tick, 0.8)
            if mark == "grave":
                m = m.transpose(Image.FLIP_LEFT_RIGHT)
            return m, 0, bb[3] - H
        if mark in ("tilde", "circ"):
            tw = max(6, round((gb[2] - gb[0]) * (0.85 if mark == "tilde" else 0.8)))
            th = max(3, bh + (1 if mark == "tilde" else 2))
            m = piece(img, ord("~" if mark == "tilde" else "^"), (tw, th))
            return m, 0, bb[3] - th
        if mark == "dot":
            pb = ink(ord("."))
            s = min(pb[2] - pb[0], bh + 1)
            return piece(img, ord("."), (s, s)), 0, bb[3] - s
        if mark in ("cedil", "ogonek"):
            cb = ink(ord(","))
            w, h = max(3, round((cb[2] - cb[0]) * 0.7)), 4
            m = piece(img, ord(","), (w, h))
            if mark == "ogonek":
                m = m.transpose(Image.FLIP_LEFT_RIGHT)
                return m, (gb[2] - gb[0]) / 2 - w / 2, CELL_H - h
            return m, 0, CELL_H - h
        if mark == "stroke":
            hb = ink(ord("-"))
            th = hb[3] - hb[1]
            lb = ink(ord("l"))
            m = piece(img, ord("-"), (round((lb[2] - lb[0]) * 2.0), th)).rotate(35, Image.BICUBIC, expand=True)
            return m, 0, (gb[1] + gb[3]) // 2 - m.size[1] // 2 + 1
        raise ValueError(mark)

    # free cells: control codes, then the empty end of the last row
    pool = [(R[c][0], R[c][1], 32) for c in range(1, 32)]
    x = R[0xFF][2] + 1
    while x + 32 <= TEX:
        pool.append((x, R[0xFF][1], 32))
        x += 32

    for code, base, mark in targets():
        b = ord(base)
        bx0, by0, bx1, by1 = R[b]
        w, h = bx1 - bx0, by1 - by0
        own = R[code]
        if own[2] - own[0] >= w and own[3] - own[1] >= h:
            dx, dy = own[0], own[1]
        else:
            dx, dy, _ = pool.pop(0)
        upper = base.isupper()
        gb = ink(b)
        for key, img in src.items():
            g = img.crop((bx0, by0, bx1, by1))
            if mark == "rot":
                ink_ = g.crop(gb).rotate(180)
                g = Image.new(img.mode, (w, h), 0)
                g.paste(ink_, gb[:2])
            else:
                if base == "i" and mark != "ogonek":   # dotless i: wipe everything above x-height
                    xtop = ink(ord("n"))[1]
                    g.paste(Image.new(img.mode, (w, xtop - 1), 0), (0, 0))
                m, xoff, top = mark_img(img, mark, upper, gb)
                cx = (gb[0] + gb[2]) / 2 + xoff
                if mark == "stroke":
                    cx = gb[0] + (gb[2] - gb[0]) * (0.3 if upper else 0.5)
                layer = Image.new(img.mode, (w, h), 0)
                layer.paste(m, (round(cx - m.size[0] / 2), top))
                g = ImageChops.lighter(g, layer)
            out[key].paste(g, (dx, dy))
        struct.pack_into("<4f", dat, 16 + code * 16, dx / TEX, dy / TEX, (dx + w) / TEX, (dy + h) / TEX)

    os.makedirs(DST, exist_ok=True)
    open(os.path.join(DST, fontname + ".dat"), "wb").write(dat)
    out["c"].save(os.path.join(DST, fontname + ".jpg"), quality=97, subsampling=0)
    out["a"].save(os.path.join(DST, "_A_" + fontname + ".jpg"), quality=97, subsampling=0)
    return len(pool)


if __name__ == "__main__":
    for f in ("EngineFont1", "EngineFont2"):
        left = build(f)
    print("ok", ENC, len(targets()), "letras,", left, "celdas libres")
