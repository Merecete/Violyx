#!/usr/bin/env python3
"""Packs a program: mkvpk.py PROGRAM.elf OUTPUT.vpk --name snake --title Snake --version 1.0
[--author NAME] [--cols 60] [--rows 18] [--icon "line1|line2|line3"] [--icon-png ICON.png]

--icon is the icon in text, three lines of six characters. --icon-png adds a picture icon of
24x24 pixels (Violyx 0.3.5 and later show it, older ones the text icon): transparent pixels let
the tile show through, all others become the nearest of the 240 fixed colours of the screen."""

import argparse, re, struct, zlib

a = argparse.ArgumentParser()
a.add_argument("elf")
a.add_argument("out")
a.add_argument("--name", required=True)
a.add_argument("--title", required=True)
a.add_argument("--version", default="1.0")
a.add_argument("--author", default="")
a.add_argument("--cols", type=int, default=60)
a.add_argument("--rows", type=int, default=18)
a.add_argument("--icon", default="      | [==] |      ")
a.add_argument("--icon-png")
o = a.parse_args()
if not re.fullmatch(r"[a-z0-9_-]{1,23}", o.name):
    raise SystemExit("name: only a-z, 0-9, - and _, up to 23 characters")
if not (20 <= o.cols <= 76 and 5 <= o.rows <= 20):
    raise SystemExit("cols 20-76, rows 5-20")


def fix(s, n):
    b = s.encode("cp437")
    if len(b) >= n:
        raise SystemExit(f"too long: {s}")
    return b.ljust(n, b"\0")


def png_pixels(path):
    """The pixels of a 24x24 PNG as (r, g, b, a): 8 bits per channel, grey, RGB, palette or with alpha."""
    d = open(path, "rb").read()
    if d[:8] != b"\x89PNG\r\n\x1a\n":
        raise SystemExit(f"{path}: not a PNG file")
    p, idat, pal, trns = 8, b"", None, b""
    while p < len(d):
        n, t = struct.unpack(">I4s", d[p:p + 8])
        c = d[p + 8:p + 8 + n]
        if t == b"IHDR":
            w, h, bd, ct, _, _, il = struct.unpack(">IIBBBBB", c)
        elif t == b"PLTE":
            pal = [tuple(c[i:i + 3]) for i in range(0, len(c), 3)]
        elif t == b"tRNS":
            trns = c
        elif t == b"IDAT":
            idat += c
        p += 12 + n
    if (w, h) != (24, 24):
        raise SystemExit(f"{path}: the icon must be 24x24 pixels")
    if bd != 8 or il or ct not in (0, 2, 3, 4, 6):
        raise SystemExit(f"{path}: please 8 bits per channel and not interlaced")
    bpp = {0: 1, 2: 3, 3: 1, 4: 2, 6: 4}[ct]
    raw, out, prev = zlib.decompress(idat), [], bytearray(w * bpp)
    for y in range(h):
        f, line = raw[y * (w * bpp + 1)], bytearray(raw[y * (w * bpp + 1) + 1:(y + 1) * (w * bpp + 1)])
        for i in range(len(line)):
            a_ = line[i - bpp] if i >= bpp else 0
            b_, c_ = prev[i], prev[i - bpp] if i >= bpp else 0
            if f == 1:
                line[i] = (line[i] + a_) & 255
            elif f == 2:
                line[i] = (line[i] + b_) & 255
            elif f == 3:
                line[i] = (line[i] + (a_ + b_) // 2) & 255
            elif f == 4:
                pa, pb, pc = abs(b_ - c_), abs(a_ - c_), abs(a_ + b_ - 2 * c_)
                line[i] = (line[i] + (a_ if pa <= pb and pa <= pc else b_ if pb <= pc else c_)) & 255
        prev = line
        for x in range(w):
            q = line[x * bpp:(x + 1) * bpp]
            if ct == 0:
                out.append((q[0], q[0], q[0], 255))
            elif ct == 4:
                out.append((q[0], q[0], q[0], q[1]))
            elif ct == 2:
                out.append((q[0], q[1], q[2], 255))
            elif ct == 6:
                out.append(tuple(q))
            else:
                out.append(pal[q[0]] + (trns[q[0]] if q[0] < len(trns) else 255,))
    return out


def screen_rgb(i):
    L = [0, 95, 135, 175, 215, 255]
    if i < 232:
        return (L[(i - 16) // 36], L[(i - 16) // 6 % 6], L[(i - 16) % 6])
    return (8 + 10 * (i - 232),) * 3


def icon_bytes(path):
    out = bytearray()
    for r, g, b, al in png_pixels(path):
        if al < 128:
            out.append(0)
        else:
            out.append(min(range(16, 256), key=lambda i: sum((u - v) ** 2 for u, v in zip(screen_rgb(i), (r, g, b)))))
    return bytes(out)


ic = (o.icon.split("|") + ["", "", ""])[:3]
elf = open(o.elf, "rb").read()
pix = b""
if o.icon_png:
    pix = b"ICN1" + struct.pack("<I", 256 + len(elf))
    elf += icon_bytes(o.icon_png)  # at the end, still inside the ELF part: older Violyx ignore it
h = (
    b"VPK1"
    + struct.pack("<IHH", len(elf), o.cols, o.rows)
    + fix(o.name, 24)
    + fix(o.title, 24)
    + fix(o.version, 12)
    + fix(o.author, 32)
    + b"".join(fix(x[:6], 7) for x in ic)
)
open(o.out, "wb").write((h.ljust(128, b"\0") + pix).ljust(256, b"\0") + elf)
