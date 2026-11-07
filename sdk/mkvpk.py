#!/usr/bin/env python3
"""Packs a program: mkvpk.py PROGRAM.elf OUTPUT.vpk --name snake --title Snake --version 1.0
[--author NAME] [--cols 60] [--rows 18] [--icon "line1|line2|line3"]"""

import argparse, re, struct

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


ic = (o.icon.split("|") + ["", "", ""])[:3]
elf = open(o.elf, "rb").read()
h = (
    b"VPK1"
    + struct.pack("<IHH", len(elf), o.cols, o.rows)
    + fix(o.name, 24)
    + fix(o.title, 24)
    + fix(o.version, 12)
    + fix(o.author, 32)
    + b"".join(fix(x[:6], 7) for x in ic)
)
open(o.out, "wb").write(h.ljust(256, b"\0") + elf)
