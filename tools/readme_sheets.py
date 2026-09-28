"""The README's sheets of Blender renders, from an AmoledOS checkout's HD art.

    python3 tools/readme_sheets.py [path to AmoledOS]

The monsters' sprites are light + colour ids (the game paints them with a
palette, main/mh_art.c: mh_lut_build); this does the same on the PC side.
"""
import json
import os
import sys

import numpy as np
from PIL import Image, ImageDraw, ImageFont

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
AOS = sys.argv[1] if len(sys.argv) > 1 else os.path.join(ROOT, '..', 'ESP32S3_AmoledOS')
HD = os.path.join(AOS, 'apps', 'monsterhop', 'assets_hd')
OUT = os.path.join(ROOT, 'docs', 'img')
BG = (22, 19, 32)


def font(size):
    for f in ('/System/Library/Fonts/Supplemental/Arial Rounded Bold.ttf',
              '/System/Library/Fonts/Supplemental/Arial Bold.ttf'):
        if os.path.exists(f):
            return ImageFont.truetype(f, size)
    return ImageFont.load_default()


def painted(d, name, pal):
    """a light + id sprite in its palette's colours, RGBA"""
    meta = json.load(open(os.path.join(d, 'meta.json')))[name]
    f = meta['files']
    img = np.asarray(Image.open(os.path.join(d, f['img'])).convert('RGBA')).astype(np.float32)
    al = img[..., 3]
    if 'id' not in f:
        return Image.fromarray(img.astype(np.uint8))
    ids = np.asarray(Image.open(os.path.join(d, f['id'])).convert('L')) // 16
    light = img[..., :3].mean(axis=2)
    out = np.zeros(img.shape, np.float32)
    for i in range(16):
        c = pal.get(str(i))
        if not c:
            continue
        m = ids == i
        for k in range(3):
            out[..., k][m] = np.clip(c[k] * light[m] / 196.0, 0, 255)
    out[..., 3] = al
    return Image.fromarray(out.astype(np.uint8))


def cards():
    who = [('monsters', 'zombie', 'Zombie'), ('monsters', 'zombiedog', 'Zombie dog'), ('monsters', 'crow', 'Crow'),
           ('monsters', 'vampire', 'Vampire'), ('monsters', 'bat', 'Bat'), ('monsters', 'armor', 'Armour'),
           ('monsters', 'mummy', 'Mummy'), ('monsters', 'scarab', 'Scarab'), ('monsters', 'werewolf', 'Werewolf'),
           ('monsters', 'raptor', 'Raptor'), ('monsters', 'trike', 'Triceratops'), ('monsters', 'ptero', 'Pterodactyl'),
           ('monsters', 'compy', 'Compy'), ('monsters', 'fishman', 'Fish-man'), ('monsters', 'crab', 'Crab'),
           ('monsters', 'jelly', 'Jellyfish'), ('monsters', 'piranha', 'Piranha'),
           ('bosses', 'brute', 'The Brute'), ('bosses', 'count', 'The Count'), ('bosses', 'pharaoh', 'The Pharaoh'),
           ('bosses', 'alpha', 'The Alpha'), ('bosses', 'trex', 'T-Rex'), ('bosses', 'kraken', 'The Kraken')]
    cw, ch, cols = 260, 300, 6
    rows = (len(who) + cols - 1) // cols
    sheet = Image.new('RGB', (cw * cols, ch * rows + 10), BG)
    dr = ImageDraw.Draw(sheet)
    fnt = font(22)
    pals = {dn: json.load(open(os.path.join(HD, dn, 'palettes.json'))) for dn in ('monsters', 'bosses')}
    for n, (dn, mon, label) in enumerate(who):
        d = os.path.join(HD, dn)
        pal = pals[dn].get(mon, {})
        if isinstance(pal, dict) and not any(k.isdigit() for k in pal):
            pal = next(iter(pal.values()))      # a boss's first look
        im = painted(d, 'card_' + mon, pal)
        im.thumbnail((cw - 30, ch - 60), Image.LANCZOS)
        x, y = (n % cols) * cw, (n // cols) * ch
        sheet.paste(im, (x + (cw - im.width) // 2, y + (ch - 44 - im.height)), im)
        tw = dr.textlength(label, font=fnt)
        dr.text((x + (cw - tw) / 2, y + ch - 38), label, font=fnt, fill=(235, 225, 200))
    sheet.save(os.path.join(OUT, 'monsters.png'), optimize=True)
    print('monsters.png', sheet.size)


def backdrops():
    bd = os.path.join(AOS, 'apps', 'monsterhop', 'assets', 'backdrops')
    zones = [('city', 'Zombie Town'), ('castle', 'Vampire Castle'), ('desert', 'Mummy Desert'),
             ('forest', 'Werewolf Woods'), ('dino', 'Lost Valley'), ('bay', 'Abyss Bay')]
    tw_, th_ = 640, 360
    sheet = Image.new('RGB', (tw_ * 3 + 16 * 4, (th_ + 44) * 2 + 16), BG)
    dr = ImageDraw.Draw(sheet)
    fnt = font(24)
    for n, (z, label) in enumerate(zones):
        im = Image.open(os.path.join(bd, 'backdrop_%s.png' % z)).convert('RGB')
        # the band the game shows: from a bit above the horizon down
        h = im.height
        im = im.crop((0, int(h * 0.1), im.width, int(h * 0.1) + im.width * th_ // tw_)).resize((tw_, th_), Image.LANCZOS)
        x, y = 16 + (n % 3) * (tw_ + 16), 16 + (n // 3) * (th_ + 44)
        dr.text((x, y), label, font=fnt, fill=(235, 225, 200))
        sheet.paste(im, (x, y + 36))
    sheet.save(os.path.join(OUT, 'backdrops.jpg'), quality=88)
    print('backdrops.jpg', sheet.size)


if __name__ == '__main__':
    cards()
    backdrops()
