#!/usr/bin/env python3
"""TOTK's home screen art: sce_sys/icon0.png (512x512) and sce_sys/pic1.png (1920x1080), and the boot screen's.

Drawn from shapes only (a dusk sky, floating islands, a glowing ring, "TOTK" letters in DejaVu Serif Bold, Bitstream
Vera licence; the boot and menu text in Fredoka, SIL OFL 1.1): no game asset, character or logo.

Usage: python3 scripts/make-art.py <Fredoka.ttf> [out dir, default sce_sys]
       python3 scripts/make-art.py <Fredoka.ttf> <out dir> --boot     (the boot screen's assets, make_boot)
"""
import math
import os
import random
import sys

from PIL import Image, ImageDraw, ImageFilter, ImageFont

TITLE_FONTS = ["/usr/share/fonts/truetype/dejavu/DejaVuSerif-Bold.ttf"]


def lerp(a, b, t):
    t = max(0.0, min(1.0, t))
    return tuple(int(round(a[k] + (b[k] - a[k]) * t)) for k in range(len(a)))


def sky(w, h):
    """Night blue at the top, teal, then a warm amber band low down."""
    img = Image.new("RGB", (w, h))
    stops = [(0.0, (14, 26, 58)), (0.45, (28, 92, 112)), (0.78, (220, 150, 84)), (1.0, (246, 206, 140))]
    d = ImageDraw.Draw(img)
    for y in range(h):
        t = y / (h - 1)
        for k in range(len(stops) - 1):
            if stops[k][0] <= t <= stops[k + 1][0]:
                c = lerp(stops[k][1], stops[k + 1][1], (t - stops[k][0]) / (stops[k + 1][0] - stops[k][0]))
                break
        d.line([(0, y), (w, y)], fill=c)
    return img


def stars(img, count, seed=7):
    w, h = img.size
    rnd = random.Random(seed)
    d = ImageDraw.Draw(img)
    for _ in range(count):
        x, y = rnd.random() * w, rnd.random() * h * 0.45
        r = max(1.0, min(w, h) * rnd.uniform(0.0015, 0.004))
        a = rnd.randint(120, 230)
        d.ellipse([x - r, y - r, x + r, y + r], fill=(255, 246, 220, a))


def ring(img, cx, cy, r):
    """A soft emerald glow with a thin bright ring around it."""
    w, h = img.size
    layer = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    d = ImageDraw.Draw(layer)
    for k in range(8, 0, -1):
        rr = r * (0.55 + 0.11 * k)
        d.ellipse([cx - rr, cy - rr, cx + rr, cy + rr], fill=(80, 230, 170, int(10 + 7 * (8 - k))))
    layer = layer.filter(ImageFilter.GaussianBlur(r * 0.12))
    line = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    ImageDraw.Draw(line).ellipse([cx - r, cy - r, cx + r, cy + r], outline=(150, 255, 210, 200),
                                 width=max(2, int(r * 0.025)))
    line = line.filter(ImageFilter.GaussianBlur(max(1, r * 0.01)))
    img.paste(layer, (0, 0), layer)
    img.paste(line, (0, 0), line)


def island(d, cx, cy, rw, rh, top, rock):
    """A floating island: a flat grassy top over a rock cone that narrows downwards."""
    d.polygon([(cx - rw, cy), (cx + rw, cy), (cx + rw * 0.35, cy + rh * 1.6), (cx, cy + rh * 2.4),
               (cx - rw * 0.4, cy + rh * 1.5)], fill=rock)
    d.ellipse([cx - rw, cy - rh * 0.45, cx + rw, cy + rh * 0.45], fill=top)


def islands(img, low=True):
    w, h = img.size
    layer = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    d = ImageDraw.Draw(layer)
    far = [(0.12, 0.30, 0.07, 0.020), (0.88, 0.24, 0.06, 0.018)]
    near = [(0.20, 0.70, 0.13, 0.035), (0.84, 0.66, 0.11, 0.03)] if low else []
    for (x, y, rw, rh) in far:
        island(d, w * x, h * y, w * rw, h * rh, (60, 112, 108, 255), (36, 52, 70, 255))
    for (x, y, rw, rh) in near:
        island(d, w * x, h * y, w * rw, h * rh, (70, 150, 96, 255), (58, 54, 66, 255))
    # the ground far below, in the haze
    d.ellipse([-w * 0.2, h * 0.93, w * 1.2, h * 1.35], fill=(120, 96, 92, 255))
    img.paste(layer, (0, 0), layer)


def title_font(size, fallback):
    for f in TITLE_FONTS:
        if os.path.exists(f):
            return ImageFont.truetype(f, size)
    font = ImageFont.truetype(fallback, size)
    try:
        font.set_variation_by_axes([700])
    except Exception:
        pass
    return font


def letters(img, font_path, text, size, cx, cy):
    font = title_font(size, font_path)
    w, h = img.size
    layer = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    d = ImageDraw.Draw(layer)
    stroke = int(size * 0.06)
    box = d.textbbox((0, 0), text, font=font, stroke_width=stroke)
    tx, ty = cx - (box[2] - box[0]) / 2 - box[0], cy - (box[3] - box[1]) / 2 - box[1]
    shadow = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    ImageDraw.Draw(shadow).text((tx + size * 0.03, ty + size * 0.05), text, font=font, fill=(6, 20, 30, 190),
                                stroke_width=stroke, stroke_fill=(6, 20, 30, 190))
    shadow = shadow.filter(ImageFilter.GaussianBlur(size * 0.03))
    img.paste(shadow, (0, 0), shadow)
    d.text((tx, ty), text, font=font, fill=(244, 214, 120, 255), stroke_width=stroke, stroke_fill=(20, 58, 64, 255))
    img.paste(layer, (0, 0), layer)


def make(font, w, h, title_size, subtitle=None, logo_y=0.44, low_stars=True):
    img = sky(w, h).convert("RGBA")
    s = min(w, h)
    stars(img, 140 if w > 1000 else 40)
    ring(img, w * 0.5, h * logo_y, s * 0.30)
    islands(img, low=low_stars)
    letters(img, font, "TOTK", title_size, w * 0.5, h * logo_y)
    if subtitle:
        f = ImageFont.truetype(font, int(title_size * 0.16))
        try:
            f.set_variation_by_axes([600])
        except Exception:
            pass
        d = ImageDraw.Draw(img)
        tw = d.textlength(subtitle, font=f)
        d.text((w / 2 - tw / 2, h * 0.70), subtitle, font=f, fill=(255, 255, 255, 255), stroke_width=4,
               stroke_fill=(20, 58, 64, 255))
    return img.convert("RGB")


GLYPH_W, GLYPH_H, GLYPH_SIZE = 40, 56, 34  # boot screen text cells (ASCII 32-126)


def make_boot(font, out):
    """The boot screen's raw assets (runtime/src/ps4/ps4_boot_screen.cpp reads them from /app0/boot):

    background.rgba  1920x1080 RGBA: the home screen art with the logo high, room for the bar below it
    font.bin         'SMWF', u16 cell width, u16 cell height, u8 first char (32), u8 count (95), count advances
                     (u8), then count cells of RGBA: white Fredoka with a dark teal outline, the baseline at 3/4 of a cell
    """
    os.makedirs(out, exist_ok=True)
    bg = make(font, 1920, 1080, 300, logo_y=0.33, low_stars=False)
    with open(os.path.join(out, "background.rgba"), "wb") as f:
        f.write(bg.convert("RGBA").tobytes())
    f_ = ImageFont.truetype(font, GLYPH_SIZE)
    try:
        f_.set_variation_by_axes([600])
    except Exception:
        pass
    first, count = 32, 95
    advances, cells = bytearray(), bytearray()
    for k in range(count):
        ch = chr(first + k)
        cell = Image.new("RGBA", (GLYPH_W, GLYPH_H), (0, 0, 0, 0))
        d = ImageDraw.Draw(cell)
        d.text((3, int(GLYPH_H * 0.75)), ch, font=f_, anchor="ls", fill=(255, 255, 255, 255), stroke_width=3,
               stroke_fill=(20, 58, 64, 255))
        advances.append(min(GLYPH_W, int(round(d.textlength(ch, font=f_))) + 2))
        cells += cell.tobytes()
    with open(os.path.join(out, "font.bin"), "wb") as f:
        f.write(b"SMWF" + GLYPH_W.to_bytes(2, "little") + GLYPH_H.to_bytes(2, "little") + bytes([first, count]))
        f.write(bytes(advances) + bytes(cells))
    make_menu_font(font, os.path.join(out, "menu_font.bin"))
    bg.save(os.path.join(out, "preview.png"))
    print("wrote", out, "(background.rgba, font.bin, menu_font.bin)")


MENU_W, MENU_H, MENU_SIZE = 30, 42, 29  # the quick menu's and frame counter's text cells


def make_menu_font(font, path):
    """The quick menu's text (runtime/src/ps4/ps4_menu.cpp): the same format as font.bin, but plain white glyphs
    (anti-aliased in alpha, no outline) drawn at their own size, so the menu can colour them."""
    f_ = ImageFont.truetype(font, MENU_SIZE)
    try:
        f_.set_variation_by_axes([500])
    except Exception:
        pass
    first, count = 32, 95
    advances, cells = bytearray(), bytearray()
    for k in range(count):
        ch = chr(first + k)
        cell = Image.new("RGBA", (MENU_W, MENU_H), (255, 255, 255, 0))
        d = ImageDraw.Draw(cell)
        d.text((1, int(MENU_H * 0.76)), ch, font=f_, anchor="ls", fill=(255, 255, 255, 255))
        advances.append(min(MENU_W, int(round(d.textlength(ch, font=f_)))))
        cells += cell.tobytes()
    with open(path, "wb") as f:
        f.write(b"SMWF" + MENU_W.to_bytes(2, "little") + MENU_H.to_bytes(2, "little") + bytes([first, count]))
        f.write(bytes(advances) + bytes(cells))


def main():
    font = sys.argv[1]
    out = sys.argv[2] if len(sys.argv) > 2 else "sce_sys"
    if len(sys.argv) > 3 and sys.argv[3] == "--boot":
        make_boot(font, out)
        return
    os.makedirs(out, exist_ok=True)
    make(font, 512, 512, 150).save(os.path.join(out, "icon0.png"), optimize=True)
    make(font, 1920, 1080, 360, "Switch recomp for PS4").save(os.path.join(out, "pic1.png"), optimize=True)
    print("wrote", os.path.join(out, "icon0.png"), os.path.join(out, "pic1.png"))


if __name__ == "__main__":
    main()
