#!/usr/bin/env python3
"""Creates the Unicode to CP437 table for the browser."""

import sys

p = sorted((ord(bytes([b]).decode("cp437")), b) for b in range(0x80, 0x100))
with open(sys.argv[1], "w") as o:
    o.write("static const struct { unsigned short u; unsigned char c; } cp437_map[]={\n")
    o.write("".join(f"{{{u},{c}}}," for u, c in p if u < 0x10000) + "\n};\n")
