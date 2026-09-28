import math
import numpy as np
from PIL import Image, ImageDraw, ImageFilter

S = 2048
cx = S // 2

def lerp_stops(stops, t):
    t = np.clip(t, 0, 1)
    out = np.zeros(t.shape + (3,), dtype=np.float32)
    for k in range(len(stops) - 1):
        t0, c0 = stops[k]; t1, c1 = stops[k + 1]
        m = (t >= t0) & (t <= t1)
        u = ((t - t0) / max(1e-6, (t1 - t0)))[m][:, None]
        out[m] = np.array(c0) * (1 - u) + np.array(c1) * u
    return out

def gradient(stops, angle_deg=0, box=(0, 0, S, S)):
    """Linear gradient image over the canvas; angle 0 = top->bottom, 45 = diagonal."""
    x0, y0, x1, y1 = box
    yy, xx = np.mgrid[0:S, 0:S].astype(np.float32)
    a = math.radians(angle_deg)
    dx, dy = math.sin(a), math.cos(a)
    proj = (xx - x0) * dx + (yy - y0) * dy
    span = abs((x1 - x0) * dx) + abs((y1 - y0) * dy)
    rgb = lerp_stops(stops, proj / span)
    img = np.dstack([rgb, np.full((S, S), 255, np.float32)]).astype(np.uint8)
    return Image.fromarray(img, "RGBA")

def masked(layer, mask):
    out = Image.new("RGBA", (S, S), (0, 0, 0, 0)); out.paste(layer, (0, 0), mask); return out

def bezier(p0, p1, p2, p3, n=60):
    return [((1-t)**3*p0[0] + 3*(1-t)**2*t*p1[0] + 3*(1-t)*t*t*p2[0] + t**3*p3[0],
             (1-t)**3*p0[1] + 3*(1-t)**2*t*p1[1] + 3*(1-t)*t*t*p2[1] + t**3*p3[1]) for t in [i/n for i in range(n+1)]]

def tube(draw, pts, w, w_end=None, fill=255, offset=(0, 0)):
    if w_end is None: w_end = w
    dense = []
    for a, b in zip(pts[:-1], pts[1:]):
        L = max(1.0, math.hypot(b[0]-a[0], b[1]-a[1])); k = int(L / 2) + 1
        dense += [(a[0]+(b[0]-a[0])*i/k, a[1]+(b[1]-a[1])*i/k) for i in range(k)]
    dense.append(pts[-1])
    n = len(dense)
    for i, (x, y) in enumerate(dense):
        r = (w + (w_end - w) * i / max(1, n-1)) / 2
        x += offset[0]; y += offset[1]
        draw.ellipse([x-r, y-r, x+r, y+r], fill=fill)

# 24k gold: warm, saturated yellow gold with polished light / dark bands
GOLD_24K = [(0.00, (255, 246, 196)), (0.14, (255, 214, 88)), (0.30, (236, 168, 22)),
            (0.44, (255, 226, 120)), (0.58, (214, 142, 8)), (0.74, (255, 202, 64)),
            (0.88, (176, 108, 0)), (1.00, (238, 176, 36))]
GOLD_TRACK = [(0.0, (250, 214, 110)), (0.5, (212, 150, 20)), (1.0, (150, 96, 6))]

# one shared "song" so the lifted clip is literally the missing piece of the track
lane_x0, lane_x1 = 200, S - 200
gap_x0, gap_x1 = cx - 300, cx + 300
def amp(x):
    t = (x - lane_x0) / (lane_x1 - lane_x0)
    env = 0.55 + 0.35 * math.sin(t * 19.0) * math.sin(t * 5.3 + 1.0) + 0.25 * math.sin(t * 47.0 + 0.7)
    beat = 0.35 * max(0.0, math.sin(t * 2 * math.pi * 9.0)) ** 6
    return max(0.08, min(1.0, abs(env) + beat))

def waveform_mask(x0, x1, yc, half_h, bar_step=16, bar_w=9):
    m = Image.new("L", (S, S), 0); d = ImageDraw.Draw(m)
    x = x0 + bar_step / 2
    while x < x1 - bar_step / 2 + 1:
        h = amp(x) * half_h
        d.rounded_rectangle([x - bar_w/2, yc - h, x + bar_w/2, yc + h], radius=bar_w/2, fill=255)
        x += bar_step
    return m

def render(with_body=True):
    img = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    body_mask = Image.new("L", (S, S), 0)
    ImageDraw.Draw(body_mask).rounded_rectangle([80, 80, S-80, S-80], radius=420, fill=255)
    if with_body:
        img = Image.alpha_composite(img, masked(gradient([(0, (42, 40, 44)), (1, (6, 6, 8))], 0, (80, 80, S-80, S-80)), body_mask))
        halo = Image.new("RGBA", (S, S), (0, 0, 0, 0))
        ImageDraw.Draw(halo).ellipse([cx-700, 640, cx+700, 1500], fill=(255, 190, 40, 40))
        img = Image.alpha_composite(img, masked(halo.filter(ImageFilter.GaussianBlur(170)), body_mask))

    # ---------------- gold audio track (with the section cut out) ----------------
    lane_y0, lane_y1 = 1470, 1700
    lane_c, lane_half = (lane_y0 + lane_y1) / 2, (lane_y1 - lane_y0) / 2 - 22
    lane = Image.new("RGBA", (S, S), (0, 0, 0, 0)); ld = ImageDraw.Draw(lane)
    ld.rounded_rectangle([lane_x0, lane_y0, lane_x1, lane_y1], radius=30, fill=(30, 22, 6, 200))
    img = Image.alpha_composite(img, lane)
    wf_left  = waveform_mask(lane_x0 + 14, gap_x0 - 8, lane_c, lane_half)
    wf_right = waveform_mask(gap_x1 + 8, lane_x1 - 14, lane_c, lane_half)
    wf = Image.fromarray(np.maximum(np.array(wf_left), np.array(wf_right)))
    img = Image.alpha_composite(img, masked(gradient(GOLD_TRACK, 0, (0, lane_y0, S, lane_y1)), wf))
    # lane outline, broken where the section was removed
    edge = Image.new("L", (S, S), 0); ed = ImageDraw.Draw(edge)
    ed.rounded_rectangle([lane_x0, lane_y0, lane_x1, lane_y1], radius=30, outline=255, width=8)
    ed.rectangle([gap_x0, lane_y0 - 10, gap_x1, lane_y1 + 10], fill=0)
    img = Image.alpha_composite(img, masked(gradient(GOLD_TRACK, 0, (0, lane_y0, S, lane_y1)), edge))
    # the empty slot: dark hole + faint red dashed outline
    hole = Image.new("RGBA", (S, S), (0, 0, 0, 0)); hd = ImageDraw.Draw(hole)
    hd.rounded_rectangle([gap_x0, lane_y0, gap_x1, lane_y1], radius=24, fill=(8, 4, 6, 235))
    for x in range(int(gap_x0) + 10, int(gap_x1) - 10, 44):
        hd.line([(x, lane_y0), (x + 22, lane_y0)], fill=(255, 40, 70, 150), width=7)
        hd.line([(x, lane_y1), (x + 22, lane_y1)], fill=(255, 40, 70, 150), width=7)
    for y in range(lane_y0 + 10, lane_y1 - 10, 44):
        hd.line([(gap_x0, y), (gap_x0, y + 22)], fill=(255, 40, 70, 150), width=7)
        hd.line([(gap_x1, y), (gap_x1, y + 22)], fill=(255, 40, 70, 150), width=7)
    img = Image.alpha_composite(img, hole)

    # ---------------- the lifted red audio clip ----------------
    clip_y0, clip_y1 = 978, 1246
    clip_c, clip_half = (clip_y0 + clip_y1) / 2 + 14, (clip_y1 - clip_y0) / 2 - 40
    clip_mask = Image.new("L", (S, S), 0)
    ImageDraw.Draw(clip_mask).rounded_rectangle([gap_x0, clip_y0, gap_x1, clip_y1], radius=30, fill=255)
    # outer glow
    glow = Image.new("RGBA", (S, S), (0, 0, 0, 0)); glow.paste((255, 30, 60, 255), (0, 0), clip_mask)
    img = Image.alpha_composite(img, glow.filter(ImageFilter.GaussianBlur(90)))
    img = Image.alpha_composite(img, glow.filter(ImageFilter.GaussianBlur(34)))
    # clip body: deep red glass
    img = Image.alpha_composite(img, masked(gradient([(0, (70, 2, 12)), (1, (26, 0, 5))], 0, (0, clip_y0, S, clip_y1)), clip_mask))
    # clip header strip (like a DAW region)
    hdr = Image.new("L", (S, S), 0)
    ImageDraw.Draw(hdr).rounded_rectangle([gap_x0, clip_y0, gap_x1, clip_y0 + 46], radius=30, fill=255)
    ImageDraw.Draw(hdr).rectangle([gap_x0, clip_y0 + 26, gap_x1, clip_y0 + 46], fill=255)
    img = Image.alpha_composite(img, masked(gradient([(0, (255, 70, 96)), (1, (210, 12, 40))], 0, (0, clip_y0, S, clip_y0 + 46)), hdr))
    # glowing red waveform inside (same audio as the gap)
    cw = waveform_mask(gap_x0 + 8, gap_x1 - 8, clip_c, clip_half)
    wglow = Image.new("RGBA", (S, S), (0, 0, 0, 0)); wglow.paste((255, 30, 60, 255), (0, 0), cw)
    img = Image.alpha_composite(img, wglow.filter(ImageFilter.GaussianBlur(26)))
    img = Image.alpha_composite(img, wglow.filter(ImageFilter.GaussianBlur(10)))
    img = Image.alpha_composite(img, masked(gradient([(0, (255, 150, 160)), (0.5, (255, 36, 64)), (1, (255, 150, 160))], 0,
                                                     (0, clip_c - clip_half, S, clip_c + clip_half)), cw))
    # bright neon border
    border = Image.new("L", (S, S), 0)
    ImageDraw.Draw(border).rounded_rectangle([gap_x0, clip_y0, gap_x1, clip_y1], radius=30, outline=255, width=9)
    bl = Image.new("RGBA", (S, S), (0, 0, 0, 0)); bl.paste((255, 30, 60, 255), (0, 0), border)
    img = Image.alpha_composite(img, bl.filter(ImageFilter.GaussianBlur(6)))
    img = Image.alpha_composite(img, masked(Image.new("RGBA", (S, S), (255, 60, 88, 255)), border))

    # ---------------- 24k gold claw ----------------
    top = 80 if with_body else 140
    claw = Image.new("L", (S, S), 0); cd = ImageDraw.Draw(claw)
    spec = Image.new("L", (S, S), 0); sd = ImageDraw.Draw(spec)
    cd.rounded_rectangle([cx-22, top, cx+22, 540], radius=22, fill=255)        # cable
    sd.rounded_rectangle([cx-12, top, cx-4, 530], radius=4, fill=255)
    cd.rounded_rectangle([cx-160, 510, cx+160, 675], radius=50, fill=255)     # motor housing
    cd.rounded_rectangle([cx-100, 665, cx+100, 742], radius=30, fill=255)     # collar
    arms = []
    for sgn in (-1, 1):
        hub_pt = (cx + sgn*80, 712); elbow = (cx + sgn*400, 860)
        knee = (cx + sgn*380, 1125); tip = (cx + sgn*250, 1262)
        arms.append((hub_pt, elbow, knee, tip, sgn))
        tube(cd, [hub_pt, elbow], 68)
        tube(cd, bezier(elbow, (cx + sgn*438, 950), (cx + sgn*418, 1045), knee, 40), 64, 58)
        tube(cd, bezier(knee, (cx + sgn*356, 1195), (cx + sgn*304, 1250), tip, 30), 58, 20)
        cd.ellipse([elbow[0]-44, elbow[1]-44, elbow[0]+44, elbow[1]+44], fill=255)
        # specular highlight running along each arm
        tube(sd, [hub_pt, elbow], 14, offset=(0, -16))
        tube(sd, bezier(elbow, (cx + sgn*438, 950), (cx + sgn*418, 1045), knee, 40), 12, 10, offset=(-sgn*12, 0))

    # shadow, warm outer glow, 24k fill, specular + sheen
    sh = Image.new("RGBA", (S, S), (0, 0, 0, 0)); sh.paste((0, 0, 0, 210), (12, 30), claw)
    img = Image.alpha_composite(img, sh.filter(ImageFilter.GaussianBlur(20)))
    og = Image.new("RGBA", (S, S), (0, 0, 0, 0)); og.paste((255, 196, 40, 150), (0, 0), claw)
    img = Image.alpha_composite(img, og.filter(ImageFilter.GaussianBlur(40)))
    img = Image.alpha_composite(img, masked(gradient(GOLD_24K, 38, (cx - 460, 80, cx + 460, 1300)), claw))
    spec_blur = spec.filter(ImageFilter.GaussianBlur(5))
    spec_blur = Image.fromarray((np.array(spec_blur).astype(np.float32) * 0.75).astype(np.uint8))
    img = Image.alpha_composite(img, masked(Image.new("RGBA", (S, S), (255, 252, 228, 255)), spec_blur))

    det = Image.new("RGBA", (S, S), (0, 0, 0, 0)); dd = ImageDraw.Draw(det)
    dd.rounded_rectangle([cx-128, 526, cx+128, 574], radius=24, fill=(255, 255, 235, 110))     # housing shine
    dd.line([(cx-160, 672), (cx+160, 672)], fill=(120, 72, 0, 255), width=10)                  # seam
    dd.line([(cx-100, 740), (cx+100, 740)], fill=(120, 72, 0, 180), width=6)
    for _, elbow, _, _, _ in arms:
        ex, ey = elbow
        dd.ellipse([ex-16, ey-16, ex+16, ey+16], fill=(130, 80, 0, 255))                       # hinge rivets
        dd.ellipse([ex-7, ey-9, ex+3, ey+1], fill=(255, 240, 190, 200))
    dd.ellipse([cx-28, 588, cx+28, 644], fill=(255, 31, 61, 255))                              # neon light
    img = Image.alpha_composite(img, det)
    rl = Image.new("RGBA", (S, S), (0, 0, 0, 0)); ImageDraw.Draw(rl).ellipse([cx-46, 570, cx+46, 662], fill=(255, 31, 61, 210))
    img = Image.alpha_composite(img, rl.filter(ImageFilter.GaussianBlur(24)))
    ImageDraw.Draw(img).ellipse([cx-14, 598, cx+2, 614], fill=(255, 255, 255, 180))

    if with_body:
        gl = Image.new("RGBA", (S, S), (0, 0, 0, 0)); gd = ImageDraw.Draw(gl)
        for y in range(80, int(S * 0.44)):
            gd.line([(80, y), (S-80, y)], fill=(255, 255, 255, int(24 * (1 - (y - 80) / (S * 0.44 - 80)))))
        glmask = Image.new("L", (S, S), 0); ImageDraw.Draw(glmask).rounded_rectangle([96, 96, S-96, S-96], radius=400, fill=255)
        img = Image.alpha_composite(img, masked(gl, glmask))
        edge = Image.new("L", (S, S), 0)
        ImageDraw.Draw(edge).rounded_rectangle([80, 80, S-80, S-80], radius=420, outline=255, width=12)
        img = Image.alpha_composite(img, masked(gradient(GOLD_24K, 38, (80, 80, S-80, S-80)), edge))
    else:
        img = Image.fromarray(np.where(np.array(body_mask)[..., None] > 0, np.array(img), 0).astype(np.uint8), "RGBA")
    return img.resize((1024, 1024), Image.LANCZOS)

icon = render(True)
icon.save("/tmp/claude-0/icon/claw24k_icon.png")

sheet = Image.new("RGBA", (1120, 560), (18, 18, 22, 255))
big = icon.resize((512, 512), Image.LANCZOS); sheet.paste(big, (20, 24), big)
x = 560
for sz, y in ((256, 24), (128, 300), (64, 332), (32, 348), (16, 356)):
    im = icon.resize((sz, sz), Image.LANCZOS); sheet.paste(im, (x, y), im)
    x = 840 if sz == 256 else x + sz + 24
    if sz == 256: x = 560
sheet.save("/tmp/claude-0/icon/preview24k.png")
print("done")
