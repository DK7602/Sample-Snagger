"""Darkens the light-grey glass reflection in the top right of the app icon, so on a light
desktop the circle doesn't look like it has a see-through chunk. (Already applied to
Resources/icon.png and Resources/logo_256.png in 1.4.1; packaging/windows/icon.ico is made from icon.png.)

Run:  python3 fix_icon_glare.py IN.png OUT.png      (needs numpy, scipy and Pillow)"""
import sys
import numpy as np
from PIL import Image
from scipy.ndimage import gaussian_filter

def fix(src, dst):
    im = np.asarray(Image.open(src).convert('RGBA')).astype(np.float32)
    h, w = im.shape[:2]
    rgb, a = im[..., :3], im[..., 3]
    mx, mn = rgb.max(-1), rgb.min(-1)
    sat = (mx - mn) / (mx + 1e-6)
    lum = rgb.mean(-1)
    y, x = np.mgrid[0:h, 0:w] / np.array([h, w])[:, None, None]
    # colourless, fairly bright - but not the white-hot core of the red waveform
    grey = np.clip((0.22 - sat) / 0.12, 0, 1) * np.clip((lum - 45) / 40, 0, 1) * np.clip((225 - lum) / 20, 0, 1)
    upper = np.clip((x - 0.50) / 0.12, 0, 1) * np.clip((0.62 - y) / 0.12, 0, 1)
    right = np.clip((x - 0.66) / 0.06, 0, 1) * np.clip((0.80 - y) / 0.15, 0, 1)   # beside the claw, down to the waveform
    region = np.maximum(upper, right)
    m = gaussian_filter(grey * region, 2.0 * w / 1024)
    target = np.clip(18 + (lum - 45) * 0.22, 12, 60)
    scale = np.where(lum > 1, target / np.maximum(lum, 1), 1)
    new = rgb * (1 - m[..., None]) + rgb * scale[..., None] * m[..., None]
    Image.fromarray(np.dstack([np.clip(new, 0, 255), a]).astype(np.uint8)).save(dst)

fix(sys.argv[1], sys.argv[2])
