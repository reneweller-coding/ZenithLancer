"""Draws ZENITH LANCER's icon: the Lancer from above, climbing towards a white-gold sun at the zenith, its exhaust a
blue streak, on a deep night-blue rounded square.

Writes res/zenithlancer.ico (16 to 256 px, the Windows program icon), res/zenithlancer.png (512 px) and src/IconPng.h
(a 128 px PNG as bytes: the window's icon on every platform).

    python tools/make_icon.py
"""
import io
import os

from PIL import Image, ImageDraw, ImageFilter

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
S = 2048   # drawn this large, then scaled down smoothly


def layer():
    return Image.new('RGBA', (S, S), (0, 0, 0, 0))


def icon():
    img = layer()
    base = layer()
    ImageDraw.Draw(base).rounded_rectangle([40, 40, S - 40, S - 40], radius=S // 6, fill=(8, 14, 34, 255))
    mask = base.split()[3]
    # The sun at the zenith: a wide warm glow and a white-gold disc.
    glow = layer()
    ImageDraw.Draw(glow).ellipse([S * 0.18, -S * 0.12, S * 0.82, S * 0.52], fill=(255, 170, 70, 255))
    glow = glow.filter(ImageFilter.GaussianBlur(S * 0.08))
    base = Image.alpha_composite(base, Image.composite(glow, layer(), mask))
    sun = layer()
    ImageDraw.Draw(sun).ellipse([S * 0.36, S * 0.06, S * 0.64, S * 0.34], fill=(255, 244, 214, 255))
    sun = sun.filter(ImageFilter.GaussianBlur(S * 0.006))
    base = Image.alpha_composite(base, Image.composite(sun, layer(), mask))
    img = Image.alpha_composite(img, base)
    # The exhaust: a blue streak below the ship.
    trail = layer()
    ImageDraw.Draw(trail).polygon([(S * 0.46, S * 0.72), (S * 0.54, S * 0.72), (S * 0.515, S * 0.98), (S * 0.485, S * 0.98)],
                                  fill=(90, 190, 255, 255))
    trail = trail.filter(ImageFilter.GaussianBlur(S * 0.02))
    img = Image.alpha_composite(img, Image.composite(trail, layer(), mask))
    # The Lancer: a dart with swept wings, light grey with a blue stripe and a glowing cockpit.
    ship = layer()
    d = ImageDraw.Draw(ship)
    cx = S * 0.5
    d.polygon([(cx, S * 0.30), (cx + S * 0.055, S * 0.52), (cx + S * 0.05, S * 0.74), (cx - S * 0.05, S * 0.74),
               (cx - S * 0.055, S * 0.52)], fill=(212, 220, 232, 255))
    for side in (-1, 1):
        d.polygon([(cx + side * S * 0.04, S * 0.50), (cx + side * S * 0.25, S * 0.68), (cx + side * S * 0.24, S * 0.73),
                   (cx + side * S * 0.045, S * 0.70)], fill=(176, 188, 204, 255))
        d.polygon([(cx + side * S * 0.19, S * 0.63), (cx + side * S * 0.25, S * 0.68), (cx + side * S * 0.24, S * 0.73),
                   (cx + side * S * 0.18, S * 0.70)], fill=(58, 138, 220, 255))
    d.ellipse([cx - S * 0.022, S * 0.44, cx + S * 0.022, S * 0.53], fill=(110, 220, 255, 255))
    shadow = ship.filter(ImageFilter.GaussianBlur(S * 0.012))
    img = Image.alpha_composite(img, Image.composite(Image.new('RGBA', (S, S), (0, 0, 0, 160)), layer(), shadow.split()[3]))
    img = Image.alpha_composite(img, ship)
    return img


def main():
    big = icon()
    os.makedirs(os.path.join(ROOT, 'res'), exist_ok=True)
    sizes = [16, 24, 32, 48, 64, 128, 256]
    big.resize((256, 256), Image.LANCZOS).save(os.path.join(ROOT, 'res', 'zenithlancer.ico'), sizes=[(s, s) for s in sizes])
    big.resize((512, 512), Image.LANCZOS).save(os.path.join(ROOT, 'res', 'zenithlancer.png'))
    buf = io.BytesIO()
    big.resize((128, 128), Image.LANCZOS).save(buf, 'PNG', optimize=True)
    data = buf.getvalue()
    lines = ['/**', ' * @file IconPng.h', " * @brief The program's icon as a 128 px PNG (tools/make_icon.py writes it).",
             ' * @author Rene Weller', ' */', '#pragma once', '', 'inline constexpr unsigned char IconPng[] = {']
    for i in range(0, len(data), 20):
        lines.append('    ' + ', '.join('0x%02x' % b for b in data[i:i + 20]) + ',')
    lines += ['};', '']
    open(os.path.join(ROOT, 'src', 'IconPng.h'), 'w', newline='\n').write('\n'.join(lines))
    with open(os.path.join(ROOT, 'res', 'zenithlancer.rc'), 'w', newline='\n') as f:
        f.write('// The program icon (tools/make_icon.py draws it).\nIDI_ICON1 ICON "zenithlancer.ico"\n')
    print('icon written')


if __name__ == '__main__':
    main()
