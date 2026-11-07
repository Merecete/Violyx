#!/usr/bin/env python3
"""Turns a picture into a Violyx background: png2vwp.py PICTURE.png OUTPUT.vwp [--dither]
Needs Pillow (pip install pillow). The picture is converted to 80x50 pixels in 256 colours.
Put the .vwp file on a FAT32 stick and open it in Violyx under Files."""

import sys
from PIL import Image

SYS = [
    (0x08, 0x06, 0x0F),
    (0x1C, 0x12, 0x30),
    (0x16, 0x0F, 0x28),
    (0x2A, 0x1C, 0x4A),
    (0x4A, 0x2C, 0x8A),
    (0x6B, 0x5B, 0x95),
    (0x9A, 0x7A, 0xE0),
    (0xB8, 0xA2, 0xF0),
    (0x04, 0x03, 0x08),
    (0xA9, 0x70, 0xFF),
    (0xE0, 0x40, 0xFF),
    (0xDC, 0xCC, 0xFF),
    (0xFF, 0x5C, 0xC8),
    (0x08, 0x06, 0x0F),
    (0xC9, 0xA0, 0xFF),
    (0xF4, 0xEC, 0xFF),
]
L = (0, 95, 135, 175, 215, 255)
PAL = SYS + [(r, g, b) for r in L for g in L for b in L] + [(8 + 10 * i,) * 3 for i in range(24)]
a = sys.argv[1:]
dith = "--dither" in a
a = [x for x in a if x != "--dither"]
if len(a) != 2:
    raise SystemExit(__doc__)
p = Image.new("P", (1, 1))
p.putpalette([v for c in PAL for v in c])
im = Image.open(a[0]).convert("RGB").resize((80, 50), Image.LANCZOS)
q = im.quantize(palette=p, dither=Image.FLOYDSTEINBERG if dith else Image.NONE)
open(a[1], "wb").write(b"VWP1" + q.tobytes())
