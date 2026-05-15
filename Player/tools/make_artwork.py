"""Generate AGK-Vita Live Area artwork: icon0 / bg / startup.

Original design — blue gradient + halftone dot pattern + plain text and a
"VITA" badge. Not a copy of any logo artwork; just the colour family.
Output is 8-bit paletted PNG (color_type 3) as the Vita requires.
"""
from PIL import Image, ImageDraw, ImageFont
import math, os, sys

# Colour palette (blue family)
C_TOP    = (130, 214, 247)   # light cyan
C_BOT    = (38, 116, 196)    # deeper blue
C_BADGE  = (43, 108, 182)    # badge blue
C_DARK   = (38, 43, 54)      # dark text
C_WHITE  = (255, 255, 255)


def lerp(a, b, t):
    return tuple(int(a[i] + (b[i] - a[i]) * t) for i in range(3))


def gradient(w, h):
    img = Image.new('RGB', (w, h))
    px = img.load()
    for y in range(h):
        c = lerp(C_TOP, C_BOT, y / max(1, h - 1))
        for x in range(w):
            px[x, y] = c
    return img


def halftone(img, spacing, max_r, fade=True):
    """Overlay a regular grid of soft dots for a halftone look."""
    w, h = img.size
    d = ImageDraw.Draw(img, 'RGBA')
    for gy in range(0, h + spacing, spacing):
        for gx in range(0, w + spacing, spacing):
            # dot size shrinks toward the centre for a subtle vignette
            if fade:
                cx, cy = w / 2, h / 2
                dist = math.hypot(gx - cx, gy - cy) / math.hypot(cx, cy)
                r = max_r * (0.35 + 0.65 * dist)
            else:
                r = max_r
            a = 70
            d.ellipse([gx - r, gy - r, gx + r, gy + r],
                      fill=(255, 255, 255, a))
    return img


def font(size):
    for name in ("arialbd.ttf", "arial.ttf", "segoeuib.ttf"):
        try:
            return ImageFont.truetype(name, size)
        except Exception:
            pass
    return ImageFont.load_default()


def text_centred(d, cx, y, s, f, fill):
    bb = d.textbbox((0, 0), s, font=f)
    d.text((cx - (bb[2] - bb[0]) / 2, y), s, font=f, fill=fill)
    return bb[3] - bb[1]


def badge(d, cx, cy, text, f):
    bb = d.textbbox((0, 0), text, font=f)
    tw, th = bb[2] - bb[0], bb[3] - bb[1]
    padx, pady = th * 0.6, th * 0.35
    x0, y0 = cx - tw / 2 - padx, cy - th / 2 - pady
    x1, y1 = cx + tw / 2 + padx, cy + th / 2 + pady
    rad = (y1 - y0) * 0.22
    try:
        d.rounded_rectangle([x0, y0, x1, y1], radius=rad, fill=C_BADGE)
    except Exception:
        d.rectangle([x0, y0, x1, y1], fill=C_BADGE)
    d.text((cx - tw / 2 - bb[0], cy - th / 2 - bb[1]), text, font=f, fill=C_WHITE)


def save_paletted(img, path):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    img.convert('P', palette=Image.ADAPTIVE).save(path, 'PNG', optimize=True)
    print("wrote", path)


def make_icon(path):
    w = h = 128
    img = halftone(gradient(w, h), spacing=14, max_r=3.0)
    d = ImageDraw.Draw(img)
    text_centred(d, w / 2, 30, "AGK", font(44), C_DARK)
    badge(d, w / 2, 92, "VITA", font(26))
    save_paletted(img, path)


def make_bg(path):
    w, h = 840, 500
    img = halftone(gradient(w, h), spacing=22, max_r=5.0)
    d = ImageDraw.Draw(img)
    text_centred(d, w / 2, 150, "AppGameKit", font(96), C_DARK)
    badge(d, w / 2, 320, "VITA", font(60))
    save_paletted(img, path)


def make_startup(path):
    w, h = 280, 158
    img = halftone(gradient(w, h), spacing=16, max_r=3.5)
    d = ImageDraw.Draw(img)
    text_centred(d, w / 2, 36, "AppGameKit", font(34), C_DARK)
    badge(d, w / 2, 108, "VITA", font(30))
    save_paletted(img, path)


# Generate into a staging dir, then the caller copies to each sce_sys.
OUT = sys.argv[1] if len(sys.argv) > 1 else r"C:\tmp\agk_art"
make_icon(os.path.join(OUT, "icon0.png"))
make_bg(os.path.join(OUT, "livearea", "contents", "bg.png"))
make_startup(os.path.join(OUT, "livearea", "contents", "startup.png"))
