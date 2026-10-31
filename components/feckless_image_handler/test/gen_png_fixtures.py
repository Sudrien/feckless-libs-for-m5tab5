#!/usr/bin/env python3
"""The two PNG fixtures pngimgtest.c needs beyond Defeatist's art/ set.

  big1500x1000.png  the four-quadrant picture of art/rgb.png at 1500x1000,
                    for the thinning
  blend.png         2x1 RGBA: an opaque red pixel and a half-transparent
                    red one (alpha 128), for the blend over the background

    ./gen_png_fixtures.py [outdir]     (default fixtures/art)
"""
import os, sys
from PIL import Image

out = sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.path.dirname(__file__), 'fixtures', 'art')
Q = [(220, 30, 30), (30, 200, 40), (40, 50, 210), (255, 255, 255)]

big = Image.new('RGB', (1500, 1000))
for q, c in enumerate(Q):
    big.paste(c, ((q & 1) * 750, (q >> 1) * 500, (q & 1) * 750 + 750, (q >> 1) * 500 + 500))
big.save(os.path.join(out, 'big1500x1000.png'), optimize=True)

b = Image.new('RGBA', (2, 1))
b.putpixel((0, 0), (255, 0, 0, 255))
b.putpixel((1, 0), (255, 0, 0, 128))
b.save(os.path.join(out, 'blend.png'))
