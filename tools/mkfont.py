#!/usr/bin/env python3
"""Erzeugt kernel/font.h aus dem Spleen-CP437-BDF (BSD-2-Clause)."""
import sys
g={}; enc=None; rows=None
for l in open(sys.argv[1],errors='replace'):
    p=l.split()
    if not p: continue
    if p[0]=='ENCODING': enc=int(p[1])
    elif p[0]=='BITMAP': rows=[]
    elif p[0]=='ENDCHAR':
        if enc is not None and 0<=enc<256: g[enc]=rows
        rows=None
    elif rows is not None: rows.append(int(p[0],16))
with open(sys.argv[2],'w') as o:
    o.write('/* Erzeugt aus Spleen 8x16 (c) Frederic Cambus, BSD-2-Clause */\nstatic const u8 font[256][16]={\n')
    for c in range(256):
        r=(g.get(c) or [0]*16)+[0]*16
        o.write('{'+','.join(str(x) for x in r[:16])+'},\n')
    o.write('};\n')
print(len(g),'Zeichen')
