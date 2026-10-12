"""Builds DeadhuntFX/hud_atlas.bin: glyphs, icons and shapes for the new in-game HUD.

Every element has its coverage in A and a soft halo (blurred, dilated coverage) in R, so the
shader can draw the readable halo and the element in one pass.

    python hudgen.py <fonts dir> <icon sheet png> <out file>

Font: Oxanium[wght].ttf as Oxanium.ttf (SIL Open Font License), weight 600 for numerals and texts.
Icon sheet: iconsheet.png rendered from iconsheet.html (white icons on black, see the layout below).
"""
import struct, sys, os, math
import numpy as np
from PIL import Image, ImageDraw, ImageFont, ImageFilter

FONTS, SHEET, OUT = sys.argv[1], sys.argv[2], sys.argv[3]
OSW_CHARS = "0123456789/:+-N .,"
BAR_CHARS = ("ABCDEFGHIJKLMNOPQRSTUVWXYZÁÀÂÄÃÉÈÊËÍÌÎÏÓÒÔÖÕÚÙÛÜÑÇ"
             "abcdefghijklmnopqrstuvwxyzáàâäãéèêëíìîïóòôöõúùûüñç"
             "0123456789 .,:;/-+%!?¿¡()'\"[]<>=_*#&«»")
# named elements (order = enum in new_hud.inl)
NAMES = ["white", "scrim", "vignette", "ring", "dot", "arrow", "arc",
         "cross", "skull", "rune", "grenade",
         "w_uzi", "w_hk33", "w_mossberg", "w_jackhammer", "w_m60", "w_steyr", "w_deagle", "disk", "steam"]

elements = []   # (kind, code, coverage array float 0..1, halo array, xoff, yoff, adv, size)
shades = {}      # element index -> brightness 0..1 inside the shape (G channel = coverage * brightness)


def halo_of(cov, radius, grow=1):
    img = Image.fromarray((cov * 255).astype(np.uint8))
    if grow:
        img = img.filter(ImageFilter.MaxFilter(grow * 2 + 1))
    img = img.filter(ImageFilter.GaussianBlur(radius))
    h = np.asarray(img).astype(np.float32) / 255
    m = h.max()
    return np.clip(h / m * 1.15, 0, 1) if m > 0 else h


def add_glyphs(kind, font, chars, size, pad):
    for ch in chars:
        adv = font.getlength(ch)
        if ch == " ":
            elements.append((kind, ord(ch), np.zeros((1, 1)), np.zeros((1, 1)), 0, 0, adv, size))
            continue
        x0, y0, x1, y1 = font.getbbox(ch, anchor="ls")
        w, h = x1 - x0 + 2 * pad, y1 - y0 + 2 * pad
        img = Image.new("L", (w, h), 0)
        ImageDraw.Draw(img).text((pad - x0, pad - y0), ch, font=font, fill=255, anchor="ls")
        cov = np.asarray(img).astype(np.float32) / 255
        elements.append((kind, ord(ch), cov, halo_of(cov, size * 0.07, max(1, size // 40)), x0 - pad, y0 - pad, adv, size))


def add_named(name, cov, halo=None, size=None):
    if halo is None:
        halo = np.zeros_like(cov)
    h, w = cov.shape
    elements.append((2, NAMES.index(name), cov, halo, 0, 0, w, size or h))


# ---- fonts
osw = ImageFont.truetype(os.path.join(FONTS, "Oxanium.ttf"), 112)       # numerals
osw.set_variation_by_axes([600])
add_glyphs(0, osw, OSW_CHARS, 112, 14)
bar = ImageFont.truetype(os.path.join(FONTS, "Oxanium.ttf"), 56)        # texts
bar.set_variation_by_axes([600])
add_glyphs(1, bar, BAR_CHARS, 56, 9)
# Russian (kind 3, codes = cp1251 bytes): Oxanium has no Cyrillic, Oswald does
cyr = ImageFont.truetype(os.path.join(FONTS, "Oswald.ttf"), 60)
cyr.set_variation_by_axes([500])
CYR = bytes(range(0xC0, 0x100)) + bytes([0xA8, 0xB8, 0xB9, 0xB2, 0xB3, 0xAF, 0xBF, 0xAA, 0xBA, 0xA5, 0xB4])  # + Ukrainian
for byte in CYR:
    ch = bytes([byte]).decode("cp1251")
    n0 = len(elements)
    add_glyphs(3, cyr, ch, 56, 9)
    elements[n0] = (3, byte) + elements[n0][2:]            # store the cp1251 byte, not the Unicode code point

# Polish (kind 4, codes = cp1250 bytes; loaded only for Polish, over the kind 1 cells)
for ch in "ąćęłńśźżĄĆĘŁŃŚŹŻ":
    n0 = len(elements)
    add_glyphs(4, bar, ch, 56, 9)
    elements[n0] = (4, ch.encode("cp1250")[0]) + elements[n0][2:]

# ---- shapes
add_named("white", np.ones((8, 8), np.float32))
n = 256
yy, xx = np.mgrid[0:n, 0:n].astype(np.float32)
r = np.hypot(xx - (n - 1) / 2, yy - (n - 1) / 2) / (n / 2)
add_named("scrim", np.clip(1 - r, 0, 1) ** 1.6)
add_named("vignette", np.clip((r * 0.72 - 0.42) / 0.3, 0, 1) ** 1.4)
ring = np.clip(1.6 - np.abs(r * (n / 2) - 120) , 0, 1)
add_named("ring", ring, halo_of(ring, 3, 2))
add_named("disk", np.clip((120 - r * (n / 2)) / 1.5, 0, 1))
m = 48
yy2, xx2 = np.mgrid[0:m, 0:m].astype(np.float32)
rd = np.hypot(xx2 - (m - 1) / 2, yy2 - (m - 1) / 2)
dot = np.clip(13 - rd, 0, 1)
outline = np.clip(17 - rd, 0, 1)
add_named("dot", dot, outline)          # halo = solid dark outline
arrow = Image.new("L", (64, 64), 0)
ImageDraw.Draw(arrow).polygon([(32, 6), (48, 56), (32, 46), (16, 56)], fill=255)
ac = np.asarray(arrow).astype(np.float32) / 255
add_named("arrow", ac, halo_of(ac, 2, 2))
# damage arc: left side of a circle, strong in the middle, fading at both ends
aw, ah = 96, 320
yy3, xx3 = np.mgrid[0:ah, 0:aw].astype(np.float32)
cx, cy, R = 300.0, ah / 2, 280.0
d = np.abs(np.hypot(xx3 - cx, yy3 - cy) - R)
band = np.clip(5 - d, 0, 1)
fade = np.clip(1 - np.abs(yy3 - cy) / (ah / 2), 0, 1) ** 1.2
add_named("arc", band * fade)

# ---- icons from the sheet (white on black): weapons in 512x256 cells, small icons in 128px cells
sheet = np.asarray(Image.open(SHEET).convert("L")).astype(np.float32) / 255


def crop_icon(x, y, w, h, target_h):
    a = sheet[y:y + h, x:x + w]
    ys, xs = np.nonzero(a > 0.04)
    a = a[ys.min():ys.max() + 1, xs.min():xs.max() + 1]
    img = Image.fromarray((a * 255).astype(np.uint8))
    s = target_h / img.height
    img = img.resize((max(1, round(img.width * s)), target_h), Image.LANCZOS)
    pad = 10
    canvas = Image.new("L", (img.width + 2 * pad, img.height + 2 * pad), 0)
    canvas.paste(img, (pad, pad))
    cov = np.asarray(canvas).astype(np.float32) / 255
    return cov, halo_of(cov, 4, 2)


for i, name in enumerate(["w_uzi", "w_hk33", "w_mossberg", "w_jackhammer", "w_m60", "w_steyr", "w_deagle"]):
    cov, halo = crop_icon((i % 4) * 512, (i // 4) * 256, 512, 256, 96)
    add_named(name, cov, halo, 96)
for i, name in enumerate(["cross", "skull", "rune", "grenade"]):
    if name == "skull":
        continue
    cov, halo = crop_icon(i * 256 + 64, 576, 128, 128, 88)
    add_named(name, cov, halo, 88)
# the skull is shaded bone, not a flat silhouette: hud/skull_mask.png (shape) + hud/skull_shade.png
# (grey render of hud/skull_shaded.svg); the HUD shader multiplies the tint by the brightness
mask = Image.open(os.path.join(os.path.dirname(SHEET), "skull_mask.png")).convert("L")
shade = Image.open(os.path.join(os.path.dirname(SHEET), "skull_shade.png")).convert("L")
bb = mask.getbbox()
mask, shade = mask.crop(bb), shade.crop(bb)
th = 120
size = (max(1, round(mask.width * th / mask.height)), th)
mask, shade = mask.resize(size, Image.LANCZOS), shade.resize(size, Image.LANCZOS)
pad = 10
cm = Image.new("L", (size[0] + 2 * pad, th + 2 * pad), 0); cm.paste(mask, (pad, pad))
cs = Image.new("L", cm.size, 0); cs.paste(shade, (pad, pad))
cov = np.asarray(cm).astype(np.float32) / 255
br = np.asarray(cs).astype(np.float32) / 255
br = np.clip(br / max(1e-3, br[cov > 0.5].max()), 0, 1)
shades[len(elements)] = br
add_named("skull", cov, halo_of(cov, 4, 2), 88)

# official Steam icon (hud/steam_icon.svg, extracted from Valve's brand guidelines), white on black render
st = Image.open(os.path.join(os.path.dirname(SHEET), "steam_icon.png")).convert("L")
st = st.crop(st.getbbox()).resize((96, 96), Image.LANCZOS)
cs = Image.new("L", (116, 116), 0); cs.paste(st, (10, 10))
cov = np.asarray(cs).astype(np.float32) / 255
add_named("steam", cov, halo_of(cov, 3, 2), 96)

# ---- pack (shelves, tallest first)
W = 2048
order = sorted(range(len(elements)), key=lambda i: -elements[i][2].shape[0])
x = y = shelf = 0
pos = {}
for i in order:
    h, w = elements[i][2].shape
    if x + w + 2 > W:
        x, y, shelf = 0, y + shelf + 2, 0
    pos[i] = (x + 1, y + 1)
    x += w + 2
    shelf = max(shelf, h)
H = 1
while H < y + shelf + 2:
    H *= 2
A = np.zeros((H, W), np.float32)
R = np.zeros((H, W), np.float32)
G = np.zeros((H, W), np.float32)
for i, (kind, code, cov, halo, xo, yo, adv, size) in enumerate(elements):
    px, py = pos[i]
    h, w = cov.shape
    A[py:py + h, px:px + w] = cov
    R[py:py + h, px:px + w] = halo
    G[py:py + h, px:px + w] = cov * shades[i] if i in shades else cov
bgra = np.zeros((H, W, 4), np.uint8)
bgra[..., 2] = (R * 255 + 0.5).astype(np.uint8)            # R = halo
bgra[..., 1] = (G * 255 + 0.5).astype(np.uint8)            # G = coverage x brightness (= A for flat elements)
bgra[..., 3] = (A * 255 + 0.5).astype(np.uint8)            # A = coverage

with open(OUT, "wb") as f:
    f.write(b"DHA1")
    f.write(struct.pack("<III", W, H, len(elements)))
    for i, (kind, code, cov, halo, xo, yo, adv, size) in enumerate(elements):
        px, py = pos[i]
        h, w = cov.shape
        f.write(struct.pack("<BBHHHHHffff", kind, 0, code, px, py, w, h, xo, yo, adv, size))
    f.write(bgra.tobytes())
Image.fromarray(np.dstack([bgra[..., 2], bgra[..., 1], bgra[..., 0], np.full((H, W), 255, np.uint8)]).astype(np.uint8)).save(OUT + ".png")
print("atlas %dx%d, %d elementos -> %s" % (W, H, len(elements), OUT))
