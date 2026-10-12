"""Menu background for DeadhuntFX: the upscaled art -> DeadhuntFX/menu_fondo.bin ('DHI1' w h + BGRA, 2160 px high).
    python menuart.py <upscaled image> <out dir> <preview.jpg>
"""
import sys, struct
import numpy as np
from PIL import Image, ImageFilter
from PIL import ImageDraw

src_bg, out_dir, prev = sys.argv[1:4]

def save(img, path):
    a = np.asarray(img.convert('RGBA'))
    bgra = a[..., [2, 1, 0, 3]].copy()
    with open(path, 'wb') as f:
        f.write(b'DHI1' + struct.pack('<II', a.shape[1], a.shape[0]))
        f.write(bgra.tobytes())

bg = Image.open(src_bg).convert('RGB')
h = 2160
bg = bg.resize((round(bg.width * h / bg.height), h), Image.LANCZOS)
save(bg, out_dir + '/menu_fondo.bin')

print('fondo', bg.size)

# preview: 16:9 and 21:9 composites like the game will do (image right-aligned at full height; its
# left 25 % - haze only - stretched over the remaining width)
def comp(W, H):
    p = Image.new('RGB', (W, H))
    iw = round(bg.width * H / bg.height)
    split = 0.14
    gap = max(0, W - iw)
    left = bg.crop((0, 0, int(bg.width * split), bg.height)).resize((gap + int(iw * split), H), Image.LANCZOS)
    right = bg.crop((int(bg.width * split), 0, bg.width, bg.height)).resize((W - left.width, H), Image.LANCZOS)
    p.paste(left, (0, 0)); p.paste(right, (left.width, 0))
    return p
a = comp(1920, 1080); b = comp(2520, 1080)
out = Image.new('RGB', (2520, 2180), 'gray'); out.paste(a, (0, 0)); out.paste(b, (0, 1100))
out.resize((1260, 1090)).save(prev, quality=85)
