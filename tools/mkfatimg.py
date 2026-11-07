#!/usr/bin/env python3
"""Creates an empty FAT32 test image. Usage: mkfatimg.py FILE MIB [SECTORS_PER_CLUSTER]"""

import struct, sys

f = sys.argv[1]
mb = int(sys.argv[2])
spc = int(sys.argv[3]) if len(sys.argv) > 3 else 1
S = 512
tot = mb * 2048
rs = 32
cl = (tot - rs) // spc
fz = (cl * 4 + 8 + S - 1) // S + 1
img = bytearray(tot * S)
b = bytearray(S)
b[0:3] = b"\xeb\x58\x90"
b[3:11] = b"mkfs.fat"
struct.pack_into("<HBHBHHBHHHII", b, 11, S, spc, rs, 2, 0, 0, 0xF8, 0, 63, 255, 0, tot)
struct.pack_into("<IHHIHH", b, 36, fz, 0, 0, 2, 1, 6)
b[64] = 0x80
b[66] = 0x29
b[71:82] = b"NO NAME    "
b[82:90] = b"FAT32   "
b[510:512] = b"\x55\xaa"
img[0:S] = b
img[6 * S : 7 * S] = b
fi = bytearray(S)
fi[0:4] = b"RRaA"
fi[484:488] = b"rrAa"
struct.pack_into("<II", fi, 488, cl - 1, 3)
fi[510:512] = b"\x55\xaa"
img[S : 2 * S] = fi
img[7 * S : 8 * S] = fi
e = struct.pack("<III", 0x0FFFFFF8, 0xFFFFFFFF, 0x0FFFFFFF)
for k in range(2):
    img[(rs + k * fz) * S : (rs + k * fz) * S + 12] = e
open(f, "wb").write(img)
