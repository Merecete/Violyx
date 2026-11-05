#!/usr/bin/env python3
"""Bettet fertige .vpk-Pakete in den Kernel ein: mkapps.py AUSGABE.h NAME=DATEI ..."""
import sys
out=open(sys.argv[1],'w')
for a in sys.argv[2:]:
    n,f=a.split('=')
    d=open(f,'rb').read()
    out.write(f'static const unsigned char pkg_{n}[{len(d)}] __attribute__((aligned(16))) = {{\n')
    for i in range(0,len(d),32): out.write(','.join(str(x) for x in d[i:i+32])+',\n')
    out.write('};\n')
