#!/usr/bin/env python3
"""SSBU's home screen art: sce_sys/icon0.png (512x512) and sce_sys/pic1.png (1920x1080).

Drawn from shapes only (a dusk sky, a floating stage with two side platforms, "SSBU" in DejaVu Sans Bold, Bitstream
Vera licence): no game asset, character or logo.

Usage: python3 scripts/make-art.py [out dir, default sce_sys]
"""
import os
import random
import sys

from PIL import Image, ImageDraw, ImageFilter, ImageFont

FONT = "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf"


def lerp(a, b, t):
    t = max(0.0, min(1.0, t))
    return tuple(int(round(a[k] + (b[k] - a[k]) * t)) for k in range(len(a)))


def sky(w, h):
    img = Image.new("RGB", (w, h))
    d = ImageDraw.Draw(img)
    top, mid, low = (12, 14, 40), (52, 30, 92), (232, 112, 72)
    for y in range(h):
        t = y / (h - 1)
        d.line([(0, y), (w, y)], fill=lerp(top, mid, t / 0.6) if t < 0.6 else lerp(mid, low, (t - 0.6) / 0.4))
    rnd = random.Random(3)
    for _ in range(int(w * h / 4000)):
        x, y = rnd.random() * w, rnd.random() * h * 0.5
        r = max(1.0, min(w, h) * rnd.uniform(0.001, 0.003))
        d.ellipse([x - r, y - r, x + r, y + r], fill=lerp((180, 180, 220), (255, 250, 230), rnd.random()))
    return img


def stage(img, cx, y, width):
    """A floating stage seen from the side, two small platforms above it, a glow underneath."""
    w, h = img.size
    glow = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    ImageDraw.Draw(glow).ellipse([cx - width * 0.6, y - width * 0.05, cx + width * 0.6, y + width * 0.25],
                                 fill=(255, 170, 90, 120))
    img.paste(glow.filter(ImageFilter.GaussianBlur(width * 0.08)), (0, 0), glow.filter(ImageFilter.GaussianBlur(width * 0.08)))
    d = ImageDraw.Draw(img)
    t = width * 0.06
    d.polygon([(cx - width / 2, y), (cx + width / 2, y), (cx + width * 0.32, y + width * 0.2),
               (cx - width * 0.32, y + width * 0.2)], fill=(30, 22, 48))
    d.rectangle([cx - width / 2, y - t * 0.35, cx + width / 2, y + t * 0.25], fill=(240, 236, 255))
    for side in (-1, 1):
        px = cx + side * width * 0.24
        d.rounded_rectangle([px - width * 0.12, y - width * 0.22, px + width * 0.12, y - width * 0.22 + t * 0.45],
                            radius=t * 0.2, fill=(240, 236, 255))


def title(img, text, cx, cy, size):
    font = ImageFont.truetype(FONT, size)
    d = ImageDraw.Draw(img)
    box = d.textbbox((0, 0), text, font=font)
    x, y = cx - (box[2] - box[0]) / 2 - box[0], cy - (box[3] - box[1]) / 2 - box[1]
    shadow = Image.new("RGBA", img.size, (0, 0, 0, 0))
    ImageDraw.Draw(shadow).text((x, y + size * 0.05), text, font=font, fill=(0, 0, 0, 170))
    shadow = shadow.filter(ImageFilter.GaussianBlur(size * 0.05))
    img.paste(shadow, (0, 0), shadow)
    d.text((x, y), text, font=font, fill=(255, 248, 236))


def make(w, h, title_size, stage_y, stage_w, title_y):
    img = sky(w, h)
    stage(img, w / 2, h * stage_y, w * stage_w)
    title(img, "SSBU", w / 2, h * title_y, title_size)
    return img


def main():
    out = sys.argv[1] if len(sys.argv) > 1 else "sce_sys"
    os.makedirs(out, exist_ok=True)
    make(512, 512, 150, 0.72, 0.8, 0.33).save(os.path.join(out, "icon0.png"))
    make(1920, 1080, 260, 0.74, 0.5, 0.32).save(os.path.join(out, "pic1.png"))
    print("art in", out)


if __name__ == "__main__":
    main()
