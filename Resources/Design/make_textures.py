"""Renders the textures behind the Sample Snagger UI:

  Resources/glitter_glass.jpg  black glass with gold and red glitter (the plate everything sits on,
                               and the fill of the SAMPLE SNAGGER title)
  Resources/gold_smooth.jpg    smooth, polished 24k gold (the title panel)

Run:  python3 make_textures.py      (needs numpy, scipy and Pillow)
"""
import os
import numpy as np
from PIL import Image
from scipy.ndimage import gaussian_filter

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.normpath(os.path.join(HERE, ".."))


def save(img, name, quality=88):
    path = os.path.join(OUT, name)
    Image.fromarray(np.clip(img, 0, 255).astype(np.uint8)).save(path, quality=quality, optimize=True, subsampling=0)
    print("wrote", name, os.path.getsize(path) // 1024, "KB")


# ------------------------------------------------------------------------------------------------
def glitter_glass(W=2048, H=1316, seed=11):
    rng = np.random.default_rng(seed)
    y, x = np.mgrid[0:H, 0:W].astype(np.float32)
    u, v = x / W, y / H

    img = np.zeros((H, W, 3), np.float32)
    # deep black glass, a breath lighter in the middle
    base = 2.5 + 3.0 * np.exp(-(((u - 0.5) / 0.6) ** 2 + ((v - 0.45) / 0.6) ** 2))
    img += base[..., None] * np.array([1.0, 0.92, 0.9], np.float32)

    # glitter drifts in soft clouds
    cloud = gaussian_filter(rng.standard_normal((H // 16, W // 16)).astype(np.float32), 3)
    cloud = (cloud - cloud.min()) / (np.ptp(cloud) + 1e-9)
    cloud = np.clip(np.array(Image.fromarray(cloud).resize((W, H), Image.BICUBIC)), 0.0, 1.0)
    density = 0.35 + 0.95 * cloud ** 1.6

    def scatter(n, weight=None):
        xs = rng.integers(0, W, n * 3)
        ys = rng.integers(0, H, n * 3)
        keep = rng.random(n * 3) < density[ys, xs] / density.max()
        return xs[keep][:n], ys[keep][:n]

    gold = np.array([255, 196, 92], np.float32)
    pale = np.array([255, 232, 170], np.float32)
    red = np.array([235, 28, 42], np.float32)

    sparkle = np.zeros((H, W, 3), np.float32)   # points that bloom
    dust = np.zeros((H, W, 3), np.float32)

    # fine gold dust - most specks faint, a few bright
    xs, ys = scatter(42000)
    b = rng.random(len(xs)) ** 3.2
    np.add.at(dust, (ys, xs), (gold * 0.9)[None, :] * b[:, None])
    # fine red dust
    xs, ys = scatter(5200)
    b = rng.random(len(xs)) ** 2.6
    np.add.at(dust, (ys, xs), (red * 0.8)[None, :] * b[:, None])

    # brighter gold and red glints (bloom)
    for colour, count, lo, hi in ((gold, 1400, 0.4, 1.0), (pale, 260, 0.6, 1.0), (red, 520, 0.4, 0.95)):
        xs, ys = scatter(count)
        b = lo + (hi - lo) * rng.random(len(xs)) ** 1.5
        np.add.at(sparkle, (ys, xs), colour[None, :] * b[:, None] * 2.2)

    # small bokeh rings, like out-of-focus glitter
    rings = np.zeros((H, W), np.float32)
    ring_col = np.zeros((H, W, 3), np.float32)
    for _ in range(95):
        cx, cy = rng.integers(8, W - 8), rng.integers(8, H - 8)
        if rng.random() > density[cy, cx] / density.max():
            continue
        r = rng.uniform(2.2, 4.2)
        c = gold if rng.random() < 0.75 else red
        yy, xx = np.mgrid[-7:8, -7:8]
        d = np.sqrt(xx ** 2 + yy ** 2)
        ring = np.exp(-((d - r) / 0.7) ** 2) * rng.uniform(0.5, 1.0) + 0.25 * np.exp(-(d / 1.0) ** 2)
        ys0, xs0 = cy - 7, cx - 7
        ring_col[ys0:ys0 + 15, xs0:xs0 + 15] += ring[..., None] * c[None, None, :]

    bloom = gaussian_filter(sparkle, sigma=(2.2, 2.2, 0)) * 3.2 + gaussian_filter(sparkle, sigma=(0.8, 0.8, 0)) * 1.2
    img += dust * 1.3 + bloom + sparkle * 0.7 + ring_col * 0.75

    # faint warm haze where the glitter is densest
    haze = gaussian_filter(dust + sparkle, sigma=(18, 18, 0)) * 2.2
    img += haze

    img = 255.0 * (1.0 - np.exp(-img / 190.0))   # soft highlight roll-off
    img += rng.normal(0, 0.6, img.shape)          # dither
    save(img, "glitter_glass.jpg", quality=90)


# ------------------------------------------------------------------------------------------------
def gold_smooth(W=2400, H=380, seed=3):
    """Polished (not brushed) gold wall catching soft, blurry lights."""
    rng = np.random.default_rng(seed)
    y, x = np.mgrid[0:H, 0:W].astype(np.float32)
    u, v = x / W, y / H

    L = 0.40 + 0.14 * np.exp(-((v - 0.38) / 0.42) ** 2) - 0.16 * v ** 3

    # soft light patches reflected in the metal, a little stretched sideways
    for _ in range(18):
        cx, cy = rng.uniform(-0.05, 1.05), rng.uniform(0.05, 0.85)
        sx, sy = rng.uniform(0.015, 0.06), rng.uniform(0.12, 0.40)
        a = rng.uniform(0.08, 0.30)
        L += a * np.exp(-(((u - cx) / sx) ** 2 + ((v - cy) / sy) ** 2))
    # darker reflections of the room between the lights
    for _ in range(12):
        cx = rng.uniform(0, 1)
        L -= rng.uniform(0.05, 0.14) * np.exp(-((u - cx) / rng.uniform(0.02, 0.06)) ** 2)

    # a few crisp horizontal lines where panels meet reflect the light more sharply
    for yy in (0.08, 0.92):
        L += 0.10 * np.exp(-((v - yy) / 0.012) ** 2)
    L = gaussian_filter(L, 2.0)
    L += rng.normal(0, 0.004, L.shape)   # no banding
    L = np.clip(L, 0, 1.3)

    stops = [
        (0.00, (46, 26, 2)),
        (0.18, (104, 64, 6)),
        (0.36, (170, 112, 16)),
        (0.52, (214, 156, 30)),
        (0.66, (238, 190, 58)),
        (0.80, (252, 218, 104)),
        (0.95, (255, 238, 166)),
        (1.30, (255, 252, 228)),
    ]
    pos = np.array([s[0] for s in stops], np.float32)
    img = np.zeros((H, W, 3), np.float32)
    for c in range(3):
        img[..., c] = np.interp(L, pos, np.array([s[1][c] for s in stops], np.float32))
    save(img, "gold_smooth.jpg", quality=90)


def glitter_text(W=1024, H=160, seed=5):
    """Dense, bright glitter for the SAMPLE SNAGGER letters (drawn at half size)."""
    rng = np.random.default_rng(seed)
    img = np.zeros((H, W, 3), np.float32) + 3.0
    gold = np.array([255, 200, 96], np.float32)
    pale = np.array([255, 238, 190], np.float32)
    red = np.array([240, 30, 45], np.float32)
    pts = np.zeros((H, W, 3), np.float32)
    for colour, count, lo in ((gold, 5200, 0.25), (pale, 900, 0.5), (red, 1500, 0.3)):
        xs, ys = rng.integers(0, W, count), rng.integers(0, H, count)
        b = lo + (1.0 - lo) * rng.random(count) ** 1.8
        np.add.at(pts, (ys, xs), colour[None, :] * b[:, None])
    img += pts * 0.9 + gaussian_filter(pts, sigma=(1.3, 1.3, 0)) * 3.0
    img = 255.0 * (1.0 - np.exp(-img / 170.0))
    save(img, "glitter_text.jpg", quality=92)


if __name__ == "__main__":
    glitter_glass()
    gold_smooth()
    glitter_text()
