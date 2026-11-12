#!/usr/bin/env python3
"""Builds the files the C compiler carries inside: mkccdata.py OUT.h PATH=FILE ...
PATH is where the compiler sees the file (/cc/include/stdio.h), FILE where it is now.
Assembly loses its .loc and .file lines, which only serve debuggers."""
import sys

out = sys.argv[1]
items = []
for a in sys.argv[2:]:
    path, src = a.split("=", 1)
    data = open(src, "rb").read()
    if src.endswith(".s"):
        keep = []
        for l in data.split(b"\n"):
            t = l.strip()
            if t.startswith(b".loc ") or t.startswith(b".file "):
                continue
            keep.append(l)
        data = b"\n".join(keep)
    items.append((path, data))
with open(out, "w") as f:
    f.write("/* made by tools/mkccdata.py */\n")
    f.write("typedef struct { const char *path; const char *data; unsigned len; } cc_file;\n")
    for i, (p, d) in enumerate(items):
        f.write(f"static const char cc_d{i}[] = {{")
        f.write(",".join(str(b) for b in d))
        f.write(",0};\n")
    f.write("static const cc_file cc_files[] = {\n")
    for i, (p, d) in enumerate(items):
        f.write(f'    {{"{p}", cc_d{i}, {len(d)}}},\n')
    f.write("};\n")
    f.write(f"static const int cc_nfiles = {len(items)};\n")
