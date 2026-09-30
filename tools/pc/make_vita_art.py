#!/usr/bin/env python3
# Draws the Vita bubble / LiveArea art (original art: no game graphics).
# usage: tools/pc/make_vita_art.py   (writes src/platform/vita/*.png)
import math, os
from PIL import Image, ImageDraw, ImageFilter, ImageFont

OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', 'src', 'platform', 'vita')
FONT_BOLD = '/usr/share/fonts/truetype/ubuntu/Ubuntu-B.ttf'
FONT_REG = '/usr/share/fonts/truetype/ubuntu/Ubuntu-M.ttf'
if not os.path.exists(FONT_BOLD):
    FONT_BOLD = FONT_REG = '/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf'

BG_TOP, BG_BOTTOM = (8, 46, 38), (2, 14, 12)

def background(w, h):
    img = Image.new('RGB', (w, h))
    d = ImageDraw.Draw(img)
    for y in range(h):
        t = y / max(1, h - 1)
        d.line([(0, y), (w, y)], fill=tuple(int(a + (b - a) * t) for a, b in zip(BG_TOP, BG_BOTTOM)))
    # faint diagonal facets
    over = Image.new('RGBA', (w, h), (0, 0, 0, 0))
    od = ImageDraw.Draw(over)
    for i in range(-h, w, max(24, w // 12)):
        od.polygon([(i, 0), (i + w // 20, 0), (i + w // 20 + h, h), (i + h, h)], fill=(40, 160, 110, 14))
    return Image.alpha_composite(img.convert('RGBA'), over)

def gem(size):
    """faceted emerald (octagonal step cut), size x size RGBA"""
    s = size
    img = Image.new('RGBA', (s, s), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    c = s / 2
    # outer octagon (step cut, taller than wide)
    w, h, k = s * 0.36, s * 0.46, s * 0.12
    outer = [(c - w + k, c - h), (c + w - k, c - h), (c + w, c - h + k), (c + w, c + h - k),
             (c + w - k, c + h), (c - w + k, c + h), (c - w, c + h - k), (c - w, c - h + k)]
    iw, ih, ik = w * 0.55, h * 0.62, k * 0.55
    inner = [(c - iw + ik, c - ih), (c + iw - ik, c - ih), (c + iw, c - ih + ik), (c + iw, c + ih - ik),
             (c + iw - ik, c + ih), (c - iw + ik, c + ih), (c - iw, c + ih - ik), (c - iw, c - ih + ik)]
    shades = [(96, 230, 150), (60, 200, 125), (30, 150, 95), (18, 110, 70),
              (14, 90, 58), (22, 120, 78), (40, 170, 110), (80, 215, 140)]
    for i in range(8):
        d.polygon([outer[i], outer[(i + 1) % 8], inner[(i + 1) % 8], inner[i]], fill=shades[i] + (255,))
    d.polygon(inner, fill=(46, 185, 120, 255))
    # table facets and a highlight
    d.line([inner[0], inner[4]], fill=(70, 210, 140, 255), width=max(1, s // 90))
    d.line([inner[1], inner[5]], fill=(30, 150, 95, 255), width=max(1, s // 90))
    d.polygon([(c - iw * 0.7, c - ih * 0.8), (c - iw * 0.2, c - ih * 0.8), (c - iw * 0.55, c - ih * 0.2)],
              fill=(200, 255, 220, 150))
    d.line(outer + [outer[0]], fill=(170, 255, 205, 255), width=max(1, s // 60))
    return img

def glow(img, radius, color):
    """soft glow around img's shape, on a canvas padded by pad so it fades out
    before the edges; returns (glow, pad)"""
    pad = radius * 3
    alpha = Image.new('L', (img.width + 2 * pad, img.height + 2 * pad), 0)
    alpha.paste(img.split()[3], (pad, pad))
    alpha = alpha.filter(ImageFilter.GaussianBlur(radius)).point(lambda v: min(255, v * 2))
    g = Image.new('RGBA', alpha.size, color + (0,))
    g.putalpha(alpha)
    return g, pad

def text(draw, xy, s, size, font=FONT_BOLD, fill=(240, 255, 245), anchor='mm', shadow=True):
    f = ImageFont.truetype(font, size)
    if shadow:
        draw.text((xy[0] + max(1, size // 18), xy[1] + max(1, size // 18)), s, font=f, fill=(0, 0, 0), anchor=anchor)
    draw.text(xy, s, font=f, fill=fill, anchor=anchor)

def compose(w, h, gem_size, gem_xy, lines):
    img = background(w, h)
    g = gem(gem_size)
    halo, pad = glow(g, gem_size // 10, (60, 255, 150))
    layer = Image.new('RGBA', img.size, (0, 0, 0, 0))
    layer.paste(halo, (gem_xy[0] - pad, gem_xy[1] - pad))
    img = Image.alpha_composite(img, layer)
    img.alpha_composite(g, gem_xy)
    d = ImageDraw.Draw(img)
    for xy, s, size, font, fill in lines:
        text(d, xy, s, size, font, fill)
    return img

def save8(img, name):
    img.convert('RGB').quantize(colors=256, method=Image.MEDIANCUT, dither=Image.NONE).save(os.path.join(OUT, name), optimize=True)

GREEN = (150, 240, 185)
# bubble icon 128x128
icon = compose(128, 128, 84, (22, 4), [((64, 100), 'EMERALD', 21, FONT_BOLD, (240, 255, 245)),
                                        ((64, 118), 'ROGUE', 15, FONT_BOLD, GREEN)])
save8(icon, 'icon0.png')
# LiveArea background 840x500
bg = compose(840, 500, 300, (60, 100), [((590, 205), 'EMERALD', 92, FONT_BOLD, (240, 255, 245)),
                                        ((590, 290), 'ROGUE', 70, FONT_BOLD, GREEN),
                                        ((590, 360), 'PS Vita port', 30, FONT_REG, (170, 210, 190))])
save8(bg, 'livearea/bg.png')
# LiveArea start button 280x158
start = compose(280, 158, 118, (10, 20), [((195, 62), 'EMERALD', 34, FONT_BOLD, (240, 255, 245)),
                                          ((195, 96), 'ROGUE', 26, FONT_BOLD, GREEN)])
save8(start, 'livearea/startup.png')
# boot splash 960x544
pic = compose(960, 544, 330, (80, 107), [((690, 220), 'EMERALD', 104, FONT_BOLD, (240, 255, 245)),
                                         ((690, 316), 'ROGUE', 80, FONT_BOLD, GREEN),
                                         ((690, 395), 'PS Vita port', 34, FONT_REG, (170, 210, 190))])
save8(pic, 'pic0.png')
print('written to', os.path.normpath(OUT))
