"""Renders the 24k gold faceplate texture used behind the Sample Snagger UI (Resources/gold_plate.jpg).

Brushed, polished 24 karat gold lit by broad studio reflections. Run:  python3 make_gold.py
Needs numpy, scipy and Pillow.
"""
import os
import numpy as np
from PIL import Image
from scipy.ndimage import gaussian_filter, uniform_filter1d

W, H = 2400, 1540
rng = np.random.default_rng(24)

y, x = np.mgrid[0:H, 0:W].astype(np.float32)
u, v = x / W, y / H


def norm(a):
    a = a - a.mean()
    return a / (a.std() + 1e-9)


# --- brushed grain: noise smeared along the brushing direction (horizontal) -------------------
def brushed(length, strength, seed):
    n = np.random.default_rng(seed).standard_normal((H, W)).astype(np.float32)
    n = uniform_filter1d(n, size=length, axis=1, mode="wrap")
    n = uniform_filter1d(n, size=max(3, length // 3), axis=1, mode="wrap")
    return norm(n) * strength

grain = brushed(9, 0.55, 1) + brushed(45, 0.9, 2) + brushed(220, 0.7, 3)
# a few longer, brighter "polish lines"
lines = np.zeros((H, W), np.float32)
for _ in range(260):
    row = rng.integers(0, H)
    x0 = rng.integers(-W // 2, W)
    ln = rng.integers(W // 6, W)
    amp = rng.uniform(0.4, 1.4) * rng.choice([-1, 1])
    xs = np.arange(max(0, x0), min(W, x0 + ln))
    if len(xs) == 0:
        continue
    env = np.sin(np.linspace(0, np.pi, len(xs))) ** 1.5
    lines[row, xs] += amp * env
lines = gaussian_filter(lines, sigma=(0.6, 2.0))
grain = norm(grain + lines * 3.0)

# --- large scale reflections (a polished plate reflecting a soft-box lit studio) ---------------
# Horizontal brushing stretches reflections perpendicular to the grain, so the bands run
# mostly top-to-bottom with a slight lean.
d = u + 0.22 * (v - 0.5)
light = (
    0.34
    + 0.52 * np.exp(-((d - 0.17) / 0.13) ** 2)          # main soft-box
    + 0.2 * np.exp(-((d - 0.27) / 0.035) ** 2)         # its hot core
    + 0.38 * np.exp(-((d - 0.78) / 0.09) ** 2)          # secondary light
    + 0.18 * np.exp(-((d - 0.55) / 0.25) ** 2)          # fill
    - 0.22 * np.exp(-((d - 0.52) / 0.06) ** 2)          # dark gap between lights
    - 0.08 * np.exp(-((d + 0.05) / 0.08) ** 2)          # darker studio wall on the far left
    - 0.20 * np.exp(-((d - 1.00) / 0.07) ** 2)
)
# gentle falloff top/bottom (the plate is lit from slightly above)
light *= 0.93 + 0.12 * np.exp(-((v - 0.32) / 0.55) ** 2)

# soft mottling (hand-polished, not machine perfect)
mott = norm(gaussian_filter(rng.standard_normal((H // 8, W // 8)).astype(np.float32), 6))
mott = np.array(Image.fromarray(mott).resize((W, H), Image.BICUBIC))
light += 0.012 * mott

# brushing shows most in the highlights
L = light + grain * (0.035 + 0.06 * np.clip(light, 0, 1.2))

# fine sparkle / micro scratches
spark = rng.standard_normal((H, W)).astype(np.float32)
spark = uniform_filter1d(spark, size=3, axis=1)
L += 0.012 * spark

# vignette
vig = ((u - 0.5) / 0.75) ** 2 + ((v - 0.5) / 0.7) ** 2
L *= 1.0 - 0.16 * np.clip(vig, 0, 1) ** 1.3

L = np.clip(L, 0.0, 1.25)

# --- 24k colour ramp (luminance -> colour). Pure gold is deep and warm, never lemon yellow. -----
stops = [
    (0.00, (38, 22, 4)),
    (0.14, (74, 45, 9)),
    (0.30, (128, 84, 20)),
    (0.46, (178, 124, 34)),
    (0.60, (212, 158, 52)),
    (0.74, (236, 188, 78)),
    (0.88, (250, 214, 120)),
    (1.00, (255, 234, 170)),
    (1.25, (255, 248, 222)),
]
pos = np.array([s[0] for s in stops], np.float32)
img = np.zeros((H, W, 3), np.float32)
for c in range(3):
    img[..., c] = np.interp(L, pos, np.array([s[1][c] for s in stops], np.float32))

out = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "gold_plate.jpg")
Image.fromarray(np.clip(img, 0, 255).astype(np.uint8)).save(out, quality=84, optimize=True, progressive=False, subsampling=0)
print("wrote", os.path.normpath(out), os.path.getsize(out) // 1024, "KB")
