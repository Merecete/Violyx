#!/usr/bin/env python3
"""Builds the files the C compiler carries inside: mkccdata.py OUT.h PATH=FILE ...
PATH is where the compiler sees the file (/cc/include/stdio.h), FILE where it is now.
Assembly loses its .loc and .file lines, which only serve debuggers."""
import sys

out = sys.argv[1]
args = sys.argv[2:]
pre = "cc"  # --name=lic gives lic_file, lic_files, lic_nfiles (the licence texts of the kernel)
if args and args[0].startswith("--name="):
    pre = args.pop(0)[7:]
items = []
for a in args:
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
    f.write(f"typedef struct {{ const char *path; const char *data; unsigned len; }} {pre}_file;\n")
    for i, (p, d) in enumerate(items):
        f.write(f"static const char {pre}_d{i}[] = {{")
        f.write(",".join(str(b) for b in d))
        f.write(",0};\n")
    f.write(f"static const {pre}_file {pre}_files[] = {{\n")
    for i, (p, d) in enumerate(items):
        f.write(f'    {{"{p}", {pre}_d{i}, {len(d)}}},\n')
    f.write("};\n")
    f.write(f"static const int {pre}_nfiles = {len(items)};\n")
