# SPDX-License-Identifier: GPL-3.0-or-later
"""README banner (1600x400, original artwork): starfield, game title and mod title in Cinzel -> banner.png
needs: python3, numpy, opencv-python, pillow.  python3 banner.py [fonts folder] [output]; run in movie/, writes ../docs/banner.png"""
import os, sys, cv2, numpy as np
from PIL import Image, ImageDraw, ImageFont
FONTS = sys.argv[1] if len(sys.argv) > 1 else "fonts"
OUT = sys.argv[2] if len(sys.argv) > 2 else "banner.png"
W, H = 1600, 400
rng = np.random.default_rng(11)
yy, xx = np.mgrid[0:H, 0:W].astype(np.float32)
t = (yy / H)[..., None]
img = np.array([6, 8, 16], np.float32) * (1 - t) + np.array([10, 14, 28], np.float32) * t
for sc, amp, col in ((140, 1.0, (28, 36, 72)), (60, 0.6, (22, 30, 58))):
    n = cv2.GaussianBlur(rng.random((H, W)).astype(np.float32), (0, 0), sc); n = (n - n.min()) / (n.max() - n.min())
    n = np.clip((n - 0.45) * 2.2, 0, 1)[..., None]
    img += n * np.array(col, np.float32) * amp * 0.55
img += np.exp(-((H - yy) / 80.0) ** 2)[..., None] * np.array([34, 46, 72], np.float32) * 0.9
def star(x, y, b, r):
    x0, x1 = int(max(0, x - 6 * r - 2)), int(min(W, x + 6 * r + 3)); y0, y1 = int(max(0, y - 6 * r - 2)), int(min(H, y + 6 * r + 3))
    sy, sx = np.mgrid[y0:y1, x0:x1].astype(np.float32); d2 = (sx - x) ** 2 + (sy - y) ** 2
    v = (np.exp(-d2 / (2 * (0.55 * r) ** 2)) + np.exp(-d2 / (2 * (2.6 * r) ** 2)) * 0.18) * b
    img[y0:y1, x0:x1] += v[..., None] * (1 + rng.uniform(-0.08, 0.08) * np.array([-1, 0, 1])) * 255
for _ in range(700): star(rng.uniform(0, W), rng.uniform(0, H), rng.uniform(0.18, 0.55), 1.0)
for _ in range(110): star(rng.uniform(0, W), rng.uniform(0, H), rng.uniform(0.5, 0.9), 1.4)
for _ in range(14):  star(rng.uniform(30, W - 30), rng.uniform(15, H - 40), rng.uniform(0.8, 1.0), 2.4)
img = np.clip(img, 0, 255)
mask = np.zeros((H, W), np.float32); mask[30:370, 380:1220] = 1
mask = cv2.GaussianBlur(mask, (0, 0), 70)[..., None]
img = img * (1 - 0.5 * mask) + np.array([4, 6, 12], np.float32) * 0.5 * mask
vg = 1 - 0.3 * (((xx - W / 2) / (W / 2)) ** 2 + ((yy - H / 2) / (H / 2)) ** 2)[..., None] * 0.7
base = Image.fromarray(np.clip(img * vg, 0, 255).astype(np.uint8)).convert("RGBA")
d = ImageDraw.Draw(base)
def cinzel(s, w=700):
    f = ImageFont.truetype(os.path.join(FONTS, "Cinzel.ttf"), s); f.set_variation_by_axes([w]); return f
def inter(n, s): return ImageFont.truetype(os.path.join(FONTS, "Inter-%s.otf" % n), s)
def tw(t, f, tr): return sum(d.textlength(c, font=f) for c in t) + tr * (len(t) - 1)
def tracked(y, t, f, tr, gradient=None, fill=None):
    x = (W - tw(t, f, tr)) / 2; layer = Image.new("L", (W, H), 0); ld = ImageDraw.Draw(layer)
    for c in t: ld.text((x, y), c, font=f, fill=255); x += d.textlength(c, font=f) + tr
    sh = Image.new("RGBA", (W, H), (0, 0, 0, 0)); sh.putalpha(layer.point(lambda v: int(v * 0.9))); base.alpha_composite(sh, (2, 2))
    if gradient:
        a, b, y0, y1 = gradient; col = np.zeros((H, W, 3), np.uint8)
        for r in range(H):
            k = min(1, max(0, (r - y0) / (y1 - y0))); col[r, :] = [int(a[i] * (1 - k) + b[i] * k) for i in range(3)]
        base.paste(Image.fromarray(col), (0, 0), layer)
    else: base.paste(Image.new("RGBA", (W, H), fill + (255,)), (0, 0), layer)
LIGHT, DARK, TAN = (247, 226, 186), (176, 128, 74), (200, 180, 155)
y = 34
tracked(y, "STAR WARS EPISODE III", cinzel(54, 800), 8, gradient=(LIGHT, DARK, y + 6, y + 60)); y += 78
tracked(y, "REVENGE OF THE SITH", cinzel(30, 600), 10, fill=TAN); y += 58
for x in range(450, 1150):
    k = 1 - abs(x - 800) / 350.0; d.point((x, y), fill=TAN + (int(255 * min(1, k * 1.6)),)); d.point((x, y + 1), fill=TAN + (int(255 * min(1, k * 1.6)),))
y += 24
tracked(y, "UNOFFICIAL CO-OP MOD", cinzel(72, 800), 8, gradient=(LIGHT, DARK, y + 8, y + 76)); y += 112
f = inter("Medium", 26); t = "Two-player couch co-op for the original Xbox  ·  NTSC-U"
w = d.textlength(t, font=f); d.text(((W - w) / 2 + 1, y + 1), t, font=f, fill=(0, 0, 0, 255)); d.text(((W - w) / 2, y), t, font=f, fill=(236, 236, 236))
print("last y", y + 30)
base.convert("RGB").save(OUT)
