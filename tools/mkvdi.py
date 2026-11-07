#!/usr/bin/env python3
"""Packs the raw image as a fixed-size VirtualBox disk (VDI)."""

import struct, sys, uuid

src, out = sys.argv[1:3]
raw = open(src, "rb").read()
MB = 1 << 20
assert len(raw) % MB == 0
blocks = len(raw) // MB
OB = MB
OD = 2 * MB
h = bytearray(b"<<< Oracle VM VirtualBox Disk Image >>>\n".ljust(64, b"\0"))
h += struct.pack("<IHH", 0xBEDA107F, 1, 1)  # signature, version 1.1
h += struct.pack("<II", 400, 2)  # header size, type 2 = fixed
h += struct.pack("<I", 0) + b"Violyx".ljust(256, b"\0")
h += struct.pack("<II", OB, OD) + struct.pack("<IIII", 0, 0, 0, 512) + struct.pack("<I", 0)
h += struct.pack("<QIIII", len(raw), MB, 0, blocks, blocks)
h += uuid.uuid4().bytes_le + uuid.uuid4().bytes_le + bytes(32) + struct.pack("<IIII", 0, 0, 0, 512)
with open(out, "wb") as f:
    f.write(bytes(h).ljust(OB, b"\0"))
    f.write(struct.pack(f"<{blocks}I", *range(blocks)).ljust(OD - OB, b"\0"))
    f.write(raw)
print(out, (len(raw) + OD) // MB, "MiB")
