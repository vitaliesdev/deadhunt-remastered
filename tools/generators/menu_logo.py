"""Builds DeadhuntFX/menu_logo.bin: 'DEADHUNT' in the style of the original logo (heavy condensed
black letters with a light outline and a soft halo) plus a red 'REMASTERED' line.
    python menulogo.py <fonts dir> <out .bin> [preview.png]
"""
import sys, struct
import numpy as np
from PIL import Image, ImageDraw, ImageFont, ImageFilter

FONTS, OUT = sys.argv[1], sys.argv[2]
S = 4                                   # supersampling
big = ImageFont.truetype(FONTS + "/Oswald.ttf", 300 * S)
big.set_variation_by_axes([700])
small = ImageFont.truetype(FONTS + "/Oswald.ttf", 78 * S)
small.set_variation_by_axes([600])

title, sub = "DEADHUNT", "REMASTERED"
track = 6 * S
x0, y0, x1, y1 = big.getbbox(title)
tw = sum(big.getlength(c) for c in title) + track * (len(title) - 1)
W, H = int(tw + 120 * S), int((y1 - y0) + 220 * S)
pad = 60 * S

def text_layer(font, s, x, y, tr):
    img = Image.new("L", (W, H), 0)
    d = ImageDraw.Draw(img)
    for c in s:
        d.text((x, y), c, font=font, fill=255)
        x += font.getlength(c) + tr
    return img

ty = pad - y0
letters = text_layer(big, title, pad, ty, track)
outline = letters.filter(ImageFilter.MaxFilter(2 * (5 * S) + 1))       # light rim around the letters
halo = letters.filter(ImageFilter.MaxFilter(2 * (9 * S) + 1)).filter(ImageFilter.GaussianBlur(14 * S))
# REMASTERED: spread to the title width, right under it
sw = sum(small.getlength(c) for c in sub)
str_ = (tw - sw) / (len(sub) - 1)
sb = small.getbbox(sub)
sy = pad + (y1 - y0) + 34 * S - sb[1]
subl = text_layer(small, sub, pad, sy, str_)
subhalo = subl.filter(ImageFilter.MaxFilter(2 * (4 * S) + 1)).filter(ImageFilter.GaussianBlur(8 * S))

L = lambda im: np.asarray(im).astype(np.float32) / 255
a_let, a_out, a_halo, a_sub, a_subh = L(letters), L(outline), L(halo), L(subl), L(subhalo)
rgb = np.zeros((H, W, 3), np.float32)
alpha = np.zeros((H, W), np.float32)
def over(col, a):
    global rgb, alpha
    col = np.array(col, np.float32) / 255
    rgb = rgb * (1 - a[..., None]) + col * a[..., None]
    alpha = alpha * (1 - a) + a
over((235, 230, 222), a_halo * 0.45)              # soft light halo (the original's glow)
over((20, 18, 18), a_subh * 0.55)                 # dark halo behind the red line
over((205, 200, 192), a_out)                      # light outline
# letters: near black with a faint vertical sheen, like the original's chrome edge
yy = np.linspace(0, 1, H)[:, None]
over((14, 13, 13), a_let)
sheen = np.clip(1 - np.abs(yy - 0.3) * 6, 0, 1) * a_let * 0.12
rgb += sheen[..., None]
over((226, 54, 31), a_sub)                        # REMASTERED in blood red
out = Image.fromarray(np.dstack([np.clip(rgb, 0, 1) * 255, alpha * 255]).astype(np.uint8), "RGBA")
out = out.crop(out.getbbox()).resize((out.getbbox()[2] - out.getbbox()[0]) // S * 1 and
                                     ((out.getbbox()[2] - out.getbbox()[0]) // S, (out.getbbox()[3] - out.getbbox()[1]) // S),
                                     Image.LANCZOS)
a = np.asarray(out)
with open(OUT, "wb") as f:
    f.write(b"DHI1" + struct.pack("<II", a.shape[1], a.shape[0]))
    f.write(a[..., [2, 1, 0, 3]].copy().tobytes())
if len(sys.argv) > 3:
    out.save(sys.argv[3])
print("logo", out.size)
